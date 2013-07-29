/*
 * Copyright  (C) Matthew P. Grosvenor, 2012, All Rights Reserved
 *
 * Camio NETMAP_ETH card input stream
 *
 */



#include <errno.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>

#include "camio_istream_netmap.h"
#include "camio_istream_netmap_eth.h"
#include "../errors/camio_errors.h"
#include "../utils/camio_util.h"
#include "../clocks/camio_time.h"

#include "../utils/camio_util.h"
#include "../stream_description/camio_opt_parser.h"

typedef struct {
    uint8_t dst_mac[6];
    uint8_t src_mac[6];
    uint16_t ether_type;
} __attribute(( __packed__ )) ether_head_t;


static int camio_istream_netmap_eth_open(camio_istream_t* this, const camio_descr_t* descr, camio_perf_t* perf_mon ){
    camio_istream_netmap_eth_t* priv = this->priv;

    priv->netmap_base = camio_istream_netmap_new(descr,NULL,priv->params,perf_mon);

    this->selector.fd = priv->netmap_base->selector.fd;
    priv->is_closed = 0;
    return 0;
}


static void camio_istream_netmap_eth_close(camio_istream_t* this){
    camio_istream_netmap_eth_t* priv = this->priv;
    priv->netmap_base->close(priv->netmap_base);
}


static int camio_istream_netmap_eth_ready(camio_istream_t* this){
    camio_istream_netmap_eth_t* priv = this->priv;
    return priv->netmap_base->ready(priv->netmap_base);
}


static int camio_istream_netmap_eth_start_read(camio_istream_t* this, uint8_t** out){
    camio_istream_netmap_eth_t* priv = this->priv;

    uint8_t* buff;
    const uint64_t len = priv->netmap_base->start_read(priv->netmap_base, &buff);
    printf("Read %lu bytes into %p\n", len, buff);

    if(len < sizeof(ether_head_t)){
        wprintf("Packet of size (%lu) is too small to be an ethernet frame (%lu)!\n", len, sizeof(ether_head_t));
        return len;
    }

    priv->data_head = buff + sizeof(ether_head_t);
    priv->data_size = len - sizeof(ether_head_t);

    *out = priv->data_head;

    return priv->data_size;

}


static int camio_istream_netmap_eth_end_read(camio_istream_t* this,uint8_t* free_buff){
    camio_istream_netmap_eth_t* priv = this->priv;

    priv->data_head = NULL;
    priv->data_size = 0;

    return priv->netmap_base->end_read(priv->netmap_base, free_buff);
}


static int camio_istream_netmap_eth_selector_ready(camio_selectable_t* stream){
    camio_istream_t* this = container_of(stream, camio_istream_t,selector);
    return this->ready(this);
}

static void camio_istream_netmap_eth_delete(camio_istream_t* this){
    this->close(this);
    camio_istream_netmap_eth_t* priv = this->priv;
    free(priv);
}

/* ****************************************************
 * Construction
 */

static camio_istream_t* camio_istream_netmap_eth_construct(camio_istream_netmap_eth_t* priv, const camio_descr_t* descr, camio_clock_t* clock, camio_istream_netmap_params_t* params, camio_perf_t* perf_mon){
    if(!priv){
        eprintf_exit("netmap_eth stream supplied is null\n");
    }

    //Initialize the local variables
    priv->is_closed             = 0;
    priv->netmap_base           = NULL;
    priv->data_head             = NULL;
    priv->data_size             = 0;
    priv->params                = params;


    //Populate the function members
    priv->istream.priv           = priv; //Lets us access private members
    priv->istream.open           = camio_istream_netmap_eth_open;
    priv->istream.close          = camio_istream_netmap_eth_close;
    priv->istream.start_read     = camio_istream_netmap_eth_start_read;
    priv->istream.end_read       = camio_istream_netmap_eth_end_read;
    priv->istream.ready          = camio_istream_netmap_eth_ready;
    priv->istream.delete         = camio_istream_netmap_eth_delete;
    priv->istream.clock          = clock;
    priv->istream.selector.fd    = -1;
    priv->istream.selector.ready = camio_istream_netmap_eth_selector_ready;

    //Call open, because its the obvious thing to do now...
    priv->istream.open(&priv->istream, descr, perf_mon);

    //Return the generic istream interface for the outside world to use
    return &priv->istream;

}

camio_istream_t* camio_istream_netmap_eth_new( const camio_descr_t* descr, camio_clock_t* clock, camio_istream_netmap_params_t* params, camio_perf_t* perf_mon){
    camio_istream_netmap_eth_t* priv = malloc(sizeof(camio_istream_netmap_eth_t));
    if(!priv){
        eprintf_exit("No memory available for netmap_eth istream creation\n");
    }
    return camio_istream_netmap_eth_construct(priv, descr, clock, params, perf_mon);
}



