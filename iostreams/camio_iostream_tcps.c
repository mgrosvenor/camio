/*
 * Copyright  (C) Matthew P. Grosvenor, 2012, All Rights Reserved
 *
 * TCPS Input/Output stream definition
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

#include "camio_iostream_tcps.h"
#include "../errors/camio_errors.h"
#include "../stream_description/camio_opt_parser.h"
#include "../utils/camio_util.h"



int camio_iostream_tcps_open(camio_iostream_t* this, const camio_descr_t* descr, camio_perf_t* perf_mon ){
    camio_iostream_tcps_t* priv = this->priv;

    if(unlikely(perf_mon == NULL)){
        eprintf_exit("No performance monitor supplied\n");
    }
    priv->perf_mon = perf_mon;


    char ip_addr[17]; //IP addr is worst case, 16 bytes long (255.255.255.255)
    char tcps_port[6]; //TCPS port is wost case, 5 bytes long (65536)
    int tcps_sock_fd;

    if(unlikely(camio_descr_has_opts(descr->opt_head))){
        eprintf_exit( "Option(s) supplied, but none expected\n");
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
            memcpy(tcps_port,&descr->query[i+1],query_len - i -1);
            tcps_port[query_len - i -1] = '\0';
            break;
        }
    }


    /* Open the tcps socket */
    tcps_sock_fd = socket(AF_INET,SOCK_STREAM,0);
    if (tcps_sock_fd < 0 ){
        eprintf_exit("%s\n",strerror(errno));
    }


    int reuse_opt = 1;
    if(setsockopt(tcps_sock_fd, SOL_SOCKET, SO_REUSEADDR, &reuse_opt, sizeof(int)) < 0) {
        eprintf_exit("%s\n",strerror(errno));
    }

    struct sockaddr_in addr;
    memset(&addr,0,sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = inet_addr(ip_addr);
    addr.sin_port        = htons(strtol(tcps_port,NULL,10));

    if(bind(tcps_sock_fd, (struct sockaddr *)&addr, sizeof(addr)) ){
        uint64_t i = 0;

        //Will wait up to two minutes trying if the address is in use.
        //Helpful for quick restarts of apps as linux keeps some state
        //arround for a while.
        const int64_t seconds_per_try = 5;
        const int64_t seconds_total = 120;
        for(i = 0; i < seconds_total / seconds_per_try && errno == EADDRINUSE; i++){
            wprintf("%i] %s --> sleeping for %i seconds...\n",i, strerror(errno), seconds_per_try);
            bind(tcps_sock_fd, (struct sockaddr *)&addr, sizeof(addr));
            sleep(seconds_per_try);
        }

        if(errno){
            eprintf_exit("%s\n",strerror(errno));
        }
        else{
            wprintf("Successfully bound after delay.\n");
        }
    }

    this->selector.fd = tcps_sock_fd;

    int result = listen(priv->iostream.selector.fd, 0);
    camio_perf_event_start(priv->perf_mon, CAMIO_PERF_EVENT_IOSTREAM_TCPS, CAMIO_PERF_COND_NEW_DATA);
    if(unlikely( result < 0 )){
        eprintf_exit("%s\n",strerror(errno));
    }


//    int RCVBUFF_SIZE = 512 * 1024 * 1024;
//    if (setsockopt(tcps_sock_fd, SOL_SOCKET, SO_RCVBUF, &RCVBUFF_SIZE, sizeof(RCVBUFF_SIZE)) < 0) {
//        eprintf_exit("%s\n",strerror(errno));
//    }
//
//    int SNDBUFF_SIZE = 512 * 1024 * 1024;
//    if (setsockopt(tcps_sock_fd, SOL_SOCKET, SO_SNDBUF, &SNDBUFF_SIZE, sizeof(SNDBUFF_SIZE)) < 0) {
//        eprintf_exit("%s\n",strerror(errno));
//    }

    priv->addr = addr;
    priv->is_closed = 0;
    return 0;

}


void camio_iostream_tcps_close(camio_iostream_t* this){
    close(this->selector.fd);

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

static int prepare_next(camio_iostream_tcps_t* priv, int blocking){
    if(priv->accept_fd >= 0){
        camio_perf_event_start(priv->perf_mon, CAMIO_PERF_EVENT_IOSTREAM_TCPS, CAMIO_PERF_COND_EXISTING_DATA);
        return sizeof(int);
    }

    set_fd_blocking(priv->iostream.selector.fd, blocking);

    priv->accept_fd = accept(priv->iostream.selector.fd, NULL, NULL);
    if( priv->accept_fd < 0 ){
        if(errno == EAGAIN || errno == EWOULDBLOCK){
            return 0; //Reading would have blocked, we don't want this
        }

        wprintf("Accept failed - %s\n",strerror(errno));
        return 0;
    }

    priv->bytes_read = sizeof(int);
    return priv->bytes_read;

}



int camio_iostream_tcps_rready(camio_iostream_t* this){
    camio_iostream_tcps_t* priv = this->priv;
    if(priv->bytes_read || priv->is_closed){
        return 1;
    }

    return prepare_next(priv,0);
}


static int camio_iostream_tcps_start_read(camio_iostream_t* this, uint8_t** out){
    *out = NULL;

    camio_iostream_tcps_t* priv = this->priv;
    if(priv->is_closed){
        camio_perf_event_start(priv->perf_mon, CAMIO_PERF_EVENT_IOSTREAM_TCPS, CAMIO_PERF_COND_NO_DATA);
        return 0;
    }

    //Called read without calling ready, they must want to block
    while(!priv->bytes_read){
        prepare_next(priv,1);
    }

    *out = (uint8_t*)(&priv->accept_fd);
    size_t result = priv->bytes_read; //Strip off the newline
    priv->bytes_read = 0;

    return  result;
}


int camio_iostream_tcps_end_read(camio_iostream_t* this, uint8_t* free_buff){
    camio_iostream_tcps_t* priv = this->priv;
    priv->accept_fd = -1;
    return 0; //Always true for socket I/O
}


int camio_iostream_tcps_selector_ready(camio_selectable_t* stream){
    camio_iostream_t* this = container_of(stream, camio_iostream_t,selector);
    return this->rready(this);
}


void camio_iostream_tcps_delete(camio_iostream_t* this){
    this->close(this);
    camio_iostream_tcps_t* priv = this->priv;
    free(priv);
}

//Returns a pointer to a space of size len, ready for data
uint8_t* camio_iostream_tcps_start_write(camio_iostream_t* this, size_t len ){
    eprintf_exit("Not implemented\n");
    return NULL;
}

//Returns non-zero if a call to start_write will be non-blocking
int camio_iostream_tcps_wready(camio_iostream_t* this){
    //Not implemented
    eprintf_exit("Not implemented\n");
    return 0;
}


//Commit the data to the buffer previously allocated
//Len must be equal to or less than len called with start_write
uint8_t* camio_iostream_tcps_end_write(camio_iostream_t* this, size_t len){
    eprintf_exit("Not implemented\n");
    return NULL;
}

//Is this stream capable of taking over another stream buffer
int camio_iostream_tcps_can_assign_write(camio_iostream_t* this){
    return 1;
}

//Assign the write buffer to the stream
int camio_iostream_tcps_assign_write(camio_iostream_t* this, uint8_t* buffer, size_t len){
    eprintf_exit("Not implemented\n");
    return 0;
}



/* ****************************************************
 * Construction
 */

camio_iostream_t* camio_iostream_tcps_construct(camio_iostream_tcps_t* priv, const camio_descr_t* descr, camio_clock_t* clock, camio_iostream_tcps_params_t* params, camio_perf_t* perf_mon){
    if(!priv){
        eprintf_exit("tcps stream supplied is null\n");
    }
    //Initialize the local variables
    priv->is_closed         = 1;
    priv->bytes_read        = 0;
    priv->accept_fd         = -1;
    priv->params            = params;


    //Populate the function members
    priv->iostream.priv             = priv; //Lets us access private members
    priv->iostream.open             = camio_iostream_tcps_open;
    priv->iostream.close            = camio_iostream_tcps_close;
    priv->iostream.delete           = camio_iostream_tcps_delete;
    priv->iostream.start_read       = camio_iostream_tcps_start_read;
    priv->iostream.end_read         = camio_iostream_tcps_end_read;
    priv->iostream.rready           = camio_iostream_tcps_rready;
    priv->iostream.selector.ready   = camio_iostream_tcps_selector_ready;

    priv->iostream.start_write      = camio_iostream_tcps_start_write;
    priv->iostream.end_write        = camio_iostream_tcps_end_write;
    priv->iostream.can_assign_write = camio_iostream_tcps_can_assign_write;
    priv->iostream.assign_write     = camio_iostream_tcps_assign_write;
    priv->iostream.wready           = camio_iostream_tcps_wready;

    priv->iostream.clock            = clock;
    priv->iostream.selector.fd      = -1;


    //Call open, because its the obvious thing to do now...
    priv->iostream.open(&priv->iostream, descr, perf_mon);

    //Return the generic istream interface for the outside world to use
    return &priv->iostream;

}

camio_iostream_t* camio_iostream_tcps_new( const camio_descr_t* descr, camio_clock_t* clock, camio_iostream_tcps_params_t* params, camio_perf_t* perf_mon){
    camio_iostream_tcps_t* priv = malloc(sizeof(camio_iostream_tcps_t));
    if(!priv){
        eprintf_exit("No memory available for tcps istream creation\n");
    }
    return camio_iostream_tcps_construct(priv, descr, clock, params, perf_mon);
}

