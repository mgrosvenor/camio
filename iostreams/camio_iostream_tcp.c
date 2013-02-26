/*
 * Copyright  (C) Matthew P. Grosvenor, 2012, All Rights Reserved
 *
 * TCP Input/Output stream definition
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

#include "camio_iostream_tcp.h"
#include "../errors/camio_errors.h"
#include "../stream_description/camio_opt_parser.h"
#include "../utils/camio_util.h"



int camio_iostream_tcp_open(camio_iostream_t* this, const camio_descr_t* descr ){
    camio_iostream_tcp_t* priv = this->priv;
    char ip_addr[17]; //IP addr is worst case, 16 bytes long (255.255.255.255)
    char tcp_port[6]; //TCP port is wost case, 5 bytes long (65536)
    int tcp_sock_fd;

    if(unlikely(camio_descr_has_opts(descr->opt_head))){
        eprintf_exit( "Option(s) supplied, but none expected\n");
    }

    if(priv->params){
        if(priv->params->listen){
            priv->type = CAMIO_IOSTREAM_TCP_TYPE_SERVER;
        }
        else{
            priv->type = CAMIO_IOSTREAM_TCP_TYPE_CLIENT;
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
            memcpy(tcp_port,&descr->query[i+1],query_len - i -1);
            tcp_port[query_len - i -1] = '\0';
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


    /* Open the tcp socket */
    tcp_sock_fd = socket(AF_INET,SOCK_STREAM,0);
    if (tcp_sock_fd < 0 ){
        eprintf_exit(strerror(errno));
    }

    struct sockaddr_in addr;
    memset(&addr,0,sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = inet_addr(ip_addr);
    addr.sin_port        = htons(strtol(tcp_port,NULL,10));

    if(priv->type == CAMIO_IOSTREAM_TCP_TYPE_CLIENT){
        if( connect(tcp_sock_fd,(struct sockaddr*)&addr,sizeof(addr)) ) {
            eprintf_exit("%s\n",strerror(errno));
        }

        this->selector.fd = tcp_sock_fd;
    }
    else{
        if( bind(tcp_sock_fd, (struct sockaddr *)&addr, sizeof(addr)) ){
             eprintf_exit("%s\n",strerror(errno));
        }

        if( listen(tcp_sock_fd, 0)){
            eprintf_exit("%s\n",strerror(errno));
        }

        this->selector.fd = accept(tcp_sock_fd, NULL, NULL);
        if( this->selector.fd < 0 ){
            eprintf_exit("%s\n",strerror(errno));
        }

        priv->listener_fd = tcp_sock_fd;
    }


    int RCVBUFF_SIZE = 512 * 1024 * 1024;
    if (setsockopt(tcp_sock_fd, SOL_SOCKET, SO_RCVBUF, &RCVBUFF_SIZE, sizeof(RCVBUFF_SIZE)) < 0) {
        eprintf_exit("%s\n",strerror(errno));
    }

    int SNDBUFF_SIZE = 512 * 1024 * 1024;
    if (setsockopt(tcp_sock_fd, SOL_SOCKET, SO_SNDBUF, &SNDBUFF_SIZE, sizeof(SNDBUFF_SIZE)) < 0) {
        eprintf_exit("%s\n",strerror(errno));
    }

    priv->addr = addr;
    this->selector.fd = tcp_sock_fd;
    priv->is_closed = 0;
    return 0;

}


void camio_iostream_tcp_close(camio_iostream_t* this){
    camio_iostream_tcp_t* priv = this->priv;

    close(this->selector.fd);
    if(priv->type == CAMIO_IOSTREAM_TCP_TYPE_SERVER){
        close(priv->listener_fd);
    }

    free(priv->rbuffer);
    free(priv->wbuffer);
}

static void set_fd_blocking(int fd, int blocking){
    int flags = fcntl(fd, F_GETFL, 0);

    if (flags == -1){
        eprintf_exit( "Could not get file flags\n");
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

static int prepare_next(camio_iostream_tcp_t* priv, int blocking){
    if(priv->bytes_read){
        return priv->bytes_read;
    }

    set_fd_blocking(priv->iostream.selector.fd, blocking);

    int bytes = read(priv->iostream.selector.fd,priv->rbuffer,priv->rbuffer_size);
    if( bytes < 0){
        if(errno == EAGAIN || errno == EWOULDBLOCK){
            return 0; //Reading would have blocked, we don't want this
        }
        eprintf_exit("%s\n",strerror(errno));
    }

    priv->bytes_read = bytes;
    return bytes;
    return 0;

}

int camio_iostream_tcp_rready(camio_iostream_t* this){
    camio_iostream_tcp_t* priv = this->priv;
    if(priv->bytes_read || priv->is_closed){
        return 1;
    }

    return prepare_next(priv,0);
    return 0;
}


static int camio_iostream_tcp_start_read(camio_iostream_t* this, uint8_t** out){
    *out = NULL;

    camio_iostream_tcp_t* priv = this->priv;
    if(priv->is_closed){
        return 0;
    }

    //Called read without calling ready, they must want to block
    if(!priv->bytes_read){
        if(!prepare_next(priv,1)){
            return 0;
        }
    }

    *out = priv->rbuffer;
    size_t result = priv->bytes_read; //Strip off the newline
    priv->bytes_read = 0;

    return  result;
}


int camio_iostream_tcp_end_read(camio_iostream_t* this, uint8_t* free_buff){
    return 0; //Always true for socket I/O
}


int camio_iostream_tcp_selector_ready(camio_selectable_t* stream){
    camio_iostream_t* this = container_of(stream, camio_iostream_t,selector);
    return this->rready(this);
}


void camio_iostream_tcp_delete(camio_iostream_t* this){
    this->close(this);
    camio_iostream_tcp_t* priv = this->priv;
    free(priv);
}



//Returns a pointer to a space of size len, ready for data
uint8_t* camio_iostream_tcp_start_write(camio_iostream_t* this, size_t len ){
    camio_iostream_tcp_t* priv = this->priv;

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
int camio_iostream_tcp_wready(camio_iostream_t* this){
    //Not implemented
    eprintf_exit("Not implemented\n");
    return 0;
}


//Commit the data to the buffer previously allocated
//Len must be equal to or less than len called with start_write
uint8_t* camio_iostream_tcp_end_write(camio_iostream_t* this, size_t len){
    camio_iostream_tcp_t* priv = this->priv;
    int result = 0;

    if(priv->assigned_buffer){
        result = write(this->selector.fd,priv->assigned_buffer,len);
        if(result < 1){
            eprintf_exit( "Could not send on tcp socket. Error = %s\n", strerror(errno));
        }

        priv->assigned_buffer    = NULL;
        priv->assigned_buffer_sz = 0;
        return NULL;
    }

    result = write(this->selector.fd,priv->wbuffer, priv->wbuffer_size);
    if(result < 1){
        eprintf_exit( "Could not send on tcp socket. Error = %s\n", strerror(errno));
    }
    return NULL;
}

//Is this stream capable of taking over another stream buffer
int camio_iostream_tcp_can_assign_write(camio_iostream_t* this){
    return 1;
}

//Assign the write buffer to the stream
int camio_iostream_tcp_assign_write(camio_iostream_t* this, uint8_t* buffer, size_t len){
    camio_iostream_tcp_t* priv = this->priv;

    if(!buffer){
        eprintf_exit("Assigned buffer is null.");
    }

    priv->assigned_buffer    = buffer;
    priv->assigned_buffer_sz = len;
    return 0;
}



/* ****************************************************
 * Construction
 */

camio_iostream_t* camio_iostream_tcp_construct(camio_iostream_tcp_t* priv, const camio_descr_t* descr, camio_clock_t* clock, camio_iostream_tcp_params_t* params){
    if(!priv){
        eprintf_exit("tcp stream supplied is null\n");
    }
    //Initialize the local variables
    priv->is_closed         = 1;
    priv->rbuffer           = NULL;
    priv->rbuffer_size      = 0;
    priv->wbuffer           = NULL;
    priv->wbuffer_size      = 0;
    priv->bytes_read        = 0;
    priv->type              = CAMIO_IOSTREAM_TCP_TYPE_CLIENT;
    priv->params            = params;


    //Populate the function members
    priv->iostream.priv             = priv; //Lets us access private members
    priv->iostream.open             = camio_iostream_tcp_open;
    priv->iostream.close            = camio_iostream_tcp_close;
    priv->iostream.delete           = camio_iostream_tcp_delete;
    priv->iostream.start_read       = camio_iostream_tcp_start_read;
    priv->iostream.end_read         = camio_iostream_tcp_end_read;
    priv->iostream.rready           = camio_iostream_tcp_rready;
    priv->iostream.selector.ready   = camio_iostream_tcp_selector_ready;

    priv->iostream.start_write      = camio_iostream_tcp_start_write;
    priv->iostream.end_write        = camio_iostream_tcp_end_write;
    priv->iostream.can_assign_write = camio_iostream_tcp_can_assign_write;
    priv->iostream.assign_write     = camio_iostream_tcp_assign_write;
    priv->iostream.wready           = camio_iostream_tcp_wready;

    priv->iostream.clock            = clock;
    priv->iostream.selector.fd      = -1;


    //Call open, because its the obvious thing to do now...
    priv->iostream.open(&priv->iostream, descr);

    //Return the generic istream interface for the outside world to use
    return &priv->iostream;

}

camio_iostream_t* camio_iostream_tcp_new( const camio_descr_t* descr, camio_clock_t* clock, camio_iostream_tcp_params_t* params){
    camio_iostream_tcp_t* priv = malloc(sizeof(camio_iostream_tcp_t));
    if(!priv){
        eprintf_exit("No memory available for tcp istream creation\n");
    }
    return camio_iostream_tcp_construct(priv, descr, clock, params);
}

