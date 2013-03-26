/*
 * Copyright  (C) Matthew P. Grosvenor, 2012, All Rights Reserved
 *
 * UDP Input/Output stream definition
 *
 */

#include <errno.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <net/if_arp.h>
#include <sys/socket.h>
#include <netpacket/packet.h>
#include <net/ethernet.h>
#include <string.h>
#include <fcntl.h>
#include <arpa/inet.h>

#include "camio_iostream_udp.h"
#include "../errors/camio_errors.h"
#include "../stream_description/camio_opt_parser.h"
#include "../utils/camio_util.h"
#include "../perf/camio_perf.h"



static int camio_iostream_udp_open(camio_iostream_t* this, const camio_descr_t* descr, camio_perf_t* perf_mon ){
    camio_iostream_udp_t* priv = this->priv;

    if(unlikely(perf_mon == NULL)){
        eprintf_exit("No performance monitor supplied\n");
    }
    priv->perf_mon = perf_mon;


    char ip_addr[17]; //IP addr is worst case, 16 bytes long (255.255.255.255)
    char udp_port[6]; //UDP port is wost case, 5 bytes long (65536)
    int udp_sock_fd;

    if(unlikely(camio_descr_has_opts(descr->opt_head))){
        eprintf_exit( "Option(s) supplied, but none expected\n");
    }

    if(priv->params){
        if(priv->params->listen){
            priv->type = CAMIO_IOSTREAM_UDP_TYPE_SERVER;
        }
        else{
            priv->type = CAMIO_IOSTREAM_UDP_TYPE_CLIENT;
        }
    }

    if(!descr->query){
        eprintf_exit( "No address supplied\n");
    }

    const size_t query_len = strlen(descr->query);
    if(query_len > 22){
        eprintf_exit( "Query is too long %s\n", descr->query);
    }

    //Find the IP:port
    size_t i = 0;
    for(; i < query_len; i++ ){
        if(descr->query[i] == ':'){
            memcpy(ip_addr,descr->query,i);
            ip_addr[i] = '\0';
            memcpy(udp_port,&descr->query[i+1],query_len - i -1);
            udp_port[query_len - i -1] = '\0';
            break;
        }
    }


    priv->rbuffer = malloc(getpagesize() * 1024); //Allocate 1024 page for the buffer
    if(!priv->rbuffer){
        eprintf_exit( "Failed to allocate transmit buffer\n");
    }
    priv->rbuffer_size = getpagesize() * 1024;

    priv->wbuffer = malloc(getpagesize() * 1024); //Allocate 1024 page for the buffer
    if(!priv->wbuffer){
        eprintf_exit( "Failed to allocate receive buffer\n");
    }
    priv->wbuffer_size = getpagesize() * 1024;

    /* Open the udp socket */
    udp_sock_fd = socket(AF_INET,SOCK_DGRAM,0);
    if (udp_sock_fd < 0 ){
        eprintf_exit("%s\n",strerror(errno));
    }

    struct sockaddr_in in_addr, out_addr;
    memset(&out_addr,0,sizeof(out_addr));
    out_addr.sin_family      = AF_INET;
    out_addr.sin_addr.s_addr = inet_addr(ip_addr);
    out_addr.sin_port        = htons(atoi(udp_port));
    priv->addr = out_addr;

    memset(&in_addr,0,sizeof(in_addr));
    in_addr.sin_family      = AF_INET;
    in_addr.sin_addr.s_addr = INADDR_ANY;
    in_addr.sin_port        = 0;

    if(bind(udp_sock_fd, (struct sockaddr *)&in_addr, sizeof(in_addr) ) ){
        eprintf_exit("Could not bind to addres. Error \"%s\"\n",strerror(errno));
    }

    int RCVBUFF_SIZE = 512 * 1024 * 1024;
    if (setsockopt(udp_sock_fd, SOL_SOCKET, SO_RCVBUF, &RCVBUFF_SIZE, sizeof(RCVBUFF_SIZE)) < 0) {
        eprintf_exit("%s\n",strerror(errno));
    }

    int SNDBUFF_SIZE = 512 * 1024 * 1024;
    if (setsockopt(udp_sock_fd, SOL_SOCKET, SO_SNDBUF, &SNDBUFF_SIZE, sizeof(SNDBUFF_SIZE)) < 0) {
        eprintf_exit("%s\n",strerror(errno));
    }

    priv->iostream.selector.fd = udp_sock_fd;
    priv->is_closed = 0;
    return 0;

}


static void camio_iostream_udp_close(camio_iostream_t* this){
    camio_iostream_udp_t* priv = this->priv;

    close(this->selector.fd);
    free(priv->rbuffer);
    free(priv->wbuffer);
}

static void set_fd_blocking(int fd, int blocking){
    int flags = fcntl(fd, F_GETFL, 0);

    if(flags == -1){
        eprintf_exit( "Could not get file flags (\"%s\")\n", strerror(errno));
    }

    if (blocking){
        flags &= ~O_NONBLOCK;
    }
    else{
        flags |= O_NONBLOCK;
    }

    if( fcntl(fd, F_SETFL, flags) == -1){
        eprintf_exit( "Could not set file flags\n");
    }
}

static int prepare_next(camio_iostream_udp_t* priv, int blocking){
    if(priv->bytes_read){
        camio_perf_event_start(priv->perf_mon, CAMIO_PERF_EVENT_IOSTREAM_UDP, CAMIO_PERF_COND_EXISTING_DATA);
        return priv->bytes_read;
    }

    set_fd_blocking(priv->iostream.selector.fd, blocking);

    size_t sock_addr_len = sizeof(priv->addr);
    int bytes = recvfrom(priv->iostream.selector.fd,priv->rbuffer,priv->rbuffer_size, 0, (struct sockaddr*)&priv->addr, (socklen_t*)&sock_addr_len );
    camio_perf_event_start(priv->perf_mon, CAMIO_PERF_EVENT_IOSTREAM_UDP, CAMIO_PERF_COND_NEW_DATA);
    if( bytes < 0){
        if(errno == EAGAIN || errno == EWOULDBLOCK){
            return 0; //Reading would have blocked, we don't want this
        }
        eprintf_exit("%s\n",strerror(errno));
    }

    priv->bytes_read = bytes;
    return 1;

}

static int camio_iostream_udp_rready(camio_iostream_t* this){
    camio_iostream_udp_t* priv = this->priv;
    if(priv->bytes_read || priv->is_closed){
        return 1;
    }

    return prepare_next(priv,0);
}


static int camio_iostream_udp_start_read(camio_iostream_t* this, uint8_t** out){
    *out = NULL;

    camio_iostream_udp_t* priv = this->priv;
    if(priv->is_closed){
        camio_perf_event_start(priv->perf_mon, CAMIO_PERF_EVENT_IOSTREAM_UDP, CAMIO_PERF_COND_NO_DATA);
        return 0;
    }

    //Called read without calling ready, they must want to block
    while(!priv->bytes_read){
        prepare_next(priv,1);
    }

    *out = priv->rbuffer;
    size_t result = priv->bytes_read; //Strip off the newline
    priv->bytes_read = 0;

    return  result;
}


static int camio_iostream_udp_end_read(camio_iostream_t* this, uint8_t* free_buff){
    return 0; //Always true for socket I/O
}


static int camio_iostream_udp_selector_ready(camio_selectable_t* stream){
    camio_iostream_t* this = container_of(stream, camio_iostream_t,selector);
    return this->rready(this);
}


static void camio_iostream_udp_delete(camio_iostream_t* this){
    this->close(this);
    camio_iostream_udp_t* priv = this->priv;
    free(priv);
}



//Returns a pointer to a space of size len, ready for data
static uint8_t* camio_iostream_udp_start_write(camio_iostream_t* this, size_t len ){
    camio_iostream_udp_t* priv = this->priv;

    //Grow the buffer if it's not big enough
    if(len > priv->wbuffer_size){
        priv->rbuffer = realloc(priv->wbuffer, len);
        if(!priv->wbuffer){
            eprintf_exit( "Could not grow message buffer\n");
        }
        priv->wbuffer_size = len;
    }

    return priv->wbuffer;
}

//Returns non-zero if a call to start_write will be non-blocking
static int camio_iostream_udp_wready(camio_iostream_t* this){
    //Not implemented
    eprintf_exit("Not implemented\n");
    return 0;
}


//Commit the data to the buffer previously allocated
//Len must be equal to or less than len called with start_write
static uint8_t* camio_iostream_udp_end_write(camio_iostream_t* this, size_t len){
    camio_iostream_udp_t* priv = this->priv;
    int result = 0;

    if(priv->assigned_buffer){
        camio_perf_event_stop(priv->perf_mon, CAMIO_PERF_EVENT_IOSTREAM_UDP, CAMIO_PERF_COND_WRITE_ASSIGNED);
        printf("Writing Assigned buffer=%lu buff=%p\n", priv->assigned_buffer_sz, priv->assigned_buffer);
        result = sendto(this->selector.fd,priv->assigned_buffer, priv->assigned_buffer_sz, 0, (struct sockaddr*)&priv->addr, sizeof(priv->addr));
        if(result < 0){
            eprintf_exit( "Could not send on udp socket. Error = %s\n", strerror(errno));
        }

        priv->assigned_buffer    = NULL;
        priv->assigned_buffer_sz = 0;
        return NULL;
    }

    camio_perf_event_stop(priv->perf_mon, CAMIO_PERF_EVENT_IOSTREAM_UDP, CAMIO_PERF_COND_WRITE);
    result = sendto(this->selector.fd,priv->wbuffer, priv->wbuffer_size, 0, (struct sockaddr*)&priv->addr, sizeof(priv->addr));
    if(result < 0){
        eprintf_exit( "Could not send on udp socket. Error = %s\n", strerror(errno));
    }
    return NULL;
}

//Is this stream capable of taking over another stream buffer
static int camio_iostream_udp_can_assign_write(camio_iostream_t* this){
    return 1;
}

//Assign the write buffer to the stream
static int camio_iostream_udp_assign_write(camio_iostream_t* this, uint8_t* buffer, size_t len){
    camio_iostream_udp_t* priv = this->priv;

    if(!buffer){
        eprintf_exit("Assigned buffer is null.");
    }

    priv->assigned_buffer    = buffer;
    priv->assigned_buffer_sz = len;

    printf("Assigned buffer=%lu buff=%p\n", priv->assigned_buffer_sz, priv->assigned_buffer);
    return 0;
}



/* ****************************************************
 * Construction
 */

static camio_iostream_t* camio_iostream_udp_construct(camio_iostream_udp_t* priv, const camio_descr_t* descr, camio_clock_t* clock, camio_iostream_udp_params_t* params, camio_perf_t* perf_mon){
    if(!priv){
        eprintf_exit("udp stream supplied is null\n");
    }
    //Initialize the local variables
    priv->is_closed         = 1;
    priv->rbuffer           = NULL;
    priv->rbuffer_size      = 0;
    priv->wbuffer           = NULL;
    priv->wbuffer_size      = 0;
    priv->bytes_read        = 0;
    priv->type              = CAMIO_IOSTREAM_UDP_TYPE_CLIENT;
    priv->params            = params;


    //Populate the function members
    priv->iostream.priv             = priv; //Lets us access private members
    priv->iostream.open             = camio_iostream_udp_open;
    priv->iostream.close            = camio_iostream_udp_close;
    priv->iostream.delete           = camio_iostream_udp_delete;
    priv->iostream.start_read       = camio_iostream_udp_start_read;
    priv->iostream.end_read         = camio_iostream_udp_end_read;
    priv->iostream.rready           = camio_iostream_udp_rready;
    priv->iostream.selector.ready   = camio_iostream_udp_selector_ready;

    priv->iostream.start_write      = camio_iostream_udp_start_write;
    priv->iostream.end_write        = camio_iostream_udp_end_write;
    priv->iostream.can_assign_write = camio_iostream_udp_can_assign_write;
    priv->iostream.assign_write     = camio_iostream_udp_assign_write;
    priv->iostream.wready           = camio_iostream_udp_wready;

    priv->iostream.clock            = clock;
    priv->iostream.selector.fd      = -1;


    //Call open, because its the obvious thing to do now...
    priv->iostream.open(&priv->iostream, descr, perf_mon);

    //Return the generic istream interface for the outside world to use
    return &priv->iostream;

}

camio_iostream_t* camio_iostream_udp_new( const camio_descr_t* descr, camio_clock_t* clock, camio_iostream_udp_params_t* params, camio_perf_t* perf_mon){
    camio_iostream_udp_t* priv = malloc(sizeof(camio_iostream_udp_t));
    if(!priv){
        eprintf_exit("No memory available for udp istream creation\n");
    }
    return camio_iostream_udp_construct(priv, descr, clock, params, perf_mon);
}

