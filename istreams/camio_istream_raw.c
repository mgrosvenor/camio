/*
 * Copyright  (C) Matthew P. Grosvenor, 2012, All Rights Reserved
 *
 * Camio raw socket input stream
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

#include "camio_istream_raw.h"
#include "../errors/camio_errors.h"
#include "../utils/camio_util.h"
#include "../stream_description/camio_opt_parser.h"



int camio_istream_raw_open(camio_istream_t* this, const camio_descr_t* descr ){
    camio_istream_raw_t* priv = this->priv;
    const char* iface = descr->query;
    int raw_sock_fd;

    if(unlikely(camio_descr_has_opts(descr->opt_head))){
        eprintf_exit( "Option(s) supplied, but none expected\n");
    }

    if(!descr->query){
        eprintf_exit( "No interface supplied\n");
    }

    priv->buffer = malloc(getpagesize()* 1024); //Allocate 4MB
    if(!priv->buffer){
        eprintf_exit("Failed to allocate message buffer\n");
    }
    priv->buffer_size = getpagesize() * 1024;

    /* Open the raw socket MAC/PHY layer output stage */
    if ( !(raw_sock_fd = socket(AF_PACKET, SOCK_RAW, htons(ETH_P_ALL))) ){
        eprintf_exit("Could not open raw socket. Error = %s\n",strerror(errno));
    }

    /* get the interface num */
    struct ifreq if_idx;
    memset(&if_idx, 0, sizeof(struct ifreq));
    strncpy(if_idx.ifr_name, iface, IFNAMSIZ-1);
    if (ioctl(raw_sock_fd, SIOCGIFINDEX, &if_idx) < 0){
        eprintf_exit("Could not get interface name. Error = %s\n",strerror(errno));
    }

    struct sockaddr_ll socket_address;
    memset(&socket_address,0,sizeof(socket_address));
    socket_address.sll_family   = PF_PACKET;
    socket_address.sll_protocol = htons(ETH_P_ALL);
    socket_address.sll_pkttype  = PACKET_HOST;
    socket_address.sll_ifindex  = if_idx.ifr_ifindex;

    if( bind(raw_sock_fd, (struct sockaddr *)&socket_address, sizeof(socket_address)) ){
        eprintf_exit("Could not bind raw socket. Error = %s\n",strerror(errno));
    }

    //Set the port into promiscuous mode
    struct packet_mreq mr;
    memset(&mr, 0, sizeof(mr));
    mr.mr_ifindex = if_idx.ifr_ifindex;
    mr.mr_type = PACKET_MR_PROMISC;

    if(setsockopt(raw_sock_fd, SOL_PACKET, PACKET_ADD_MEMBERSHIP, (uint8_t*)&mr, sizeof(mr)) < 0){
        eprintf_exit("Could not set socket option. Error = %s\n",strerror(errno));
    }

    int RCVBUFF_SIZE = 512 * 1024 * 1024;
    if (setsockopt(raw_sock_fd, SOL_SOCKET, SO_RCVBUF, &RCVBUFF_SIZE, sizeof(RCVBUFF_SIZE)) < 0) {
        eprintf_exit("Could not set socket option. Error = %s\n",strerror(errno));
    }

    this->selector.fd = raw_sock_fd;
    priv->is_closed = 0;
    return 0;
}


void camio_istream_raw_close(camio_istream_t* this){
    camio_istream_raw_t* priv = this->priv;
    close(this->selector.fd);
    free(priv->buffer);
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

static int prepare_next(camio_istream_raw_t* priv, int blocking){
    if(priv->bytes_read){
        return priv->bytes_read;
    }

    set_fd_blocking(priv->istream.selector.fd, blocking);

    int bytes = recv(priv->istream.selector.fd,priv->buffer,priv->buffer_size, 0);
    if( bytes < 0){
        eprintf_exit("Could not receive from socket. Error = %s\n",strerror(errno));
    }

    priv->bytes_read = bytes;
    return bytes;

}

int camio_istream_raw_ready(camio_istream_t* this){
    camio_istream_raw_t* priv = this->priv;
    if(priv->bytes_read || priv->is_closed){
        return 1;
    }

    return prepare_next(priv,0);
}


static int camio_istream_raw_start_read(camio_istream_t* this, uint8_t** out){
    *out = NULL;

    camio_istream_raw_t* priv = this->priv;
    if(priv->is_closed){
        return 0;
    }

    //Called read without calling ready, they must want to block
    if(!priv->bytes_read){
        if(!prepare_next(priv,1)){
            return 0;
        }
    }

    *out = priv->buffer;
    size_t result = priv->bytes_read; //Strip off the newline
    priv->bytes_read = 0;

    return  result;
}


int camio_istream_raw_end_read(camio_istream_t* this, uint8_t* free_buff){
    return 0; //Always true for socket I/O
}


int camio_istream_raw_selector_ready(camio_selectable_t* stream){
    camio_istream_t* this = container_of(stream, camio_istream_t,selector);
    return this->ready(this);
}

void camio_istream_raw_delete(camio_istream_t* this){
    this->close(this);
    camio_istream_raw_t* priv = this->priv;
    free(priv);
}

/* ****************************************************
 * Construction
 */

camio_istream_t* camio_istream_raw_construct(camio_istream_raw_t* priv, const camio_descr_t* descr, camio_clock_t* clock, camio_istream_raw_params_t* params){
    if(!priv){
        eprintf_exit("raw stream supplied is null\n");
    }
    //Initialize the local variables
    priv->is_closed         = 1;
    priv->buffer            = NULL;
    priv->buffer_size       = 0;
    priv->bytes_read        = 0;
    priv->params            = params;


    //Populate the function members
    priv->istream.priv           = priv; //Lets us access private members
    priv->istream.open           = camio_istream_raw_open;
    priv->istream.close          = camio_istream_raw_close;
    priv->istream.start_read     = camio_istream_raw_start_read;
    priv->istream.end_read       = camio_istream_raw_end_read;
    priv->istream.ready          = camio_istream_raw_ready;
    priv->istream.delete         = camio_istream_raw_delete;
    priv->istream.clock          = clock;
    priv->istream.selector.fd    = -1;
    priv->istream.selector.ready = camio_istream_raw_selector_ready;

    //Call open, because its the obvious thing to do now...
    priv->istream.open(&priv->istream, descr);

    //Return the generic istream interface for the outside world to use
    return &priv->istream;

}

camio_istream_t* camio_istream_raw_new( const camio_descr_t* descr, camio_clock_t* clock, camio_istream_raw_params_t* params){
    camio_istream_raw_t* priv = malloc(sizeof(camio_istream_raw_t));
    if(!priv){
        eprintf_exit("No memory available for raw istream creation\n");
    }
    return camio_istream_raw_construct(priv, descr, clock, params);
}





