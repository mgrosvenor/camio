/*
 * Copyright  (C) Matthew P. Grosvenor, 2012, All Rights Reserved
 *
 * Camio netmap_eth (newline separated) output stream
 *
 */
#include <errno.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>


#include "../errors/camio_errors.h"
#include "../utils/camio_util.h"
#include "../clocks/camio_time.h"
#include "../stream_description/camio_opt_parser.h"
#include "camio_ostream_netmap.h"

#include "camio_ostream_netmap_eth.h"
#include "camio_ostream_netmap.h"

typedef struct {
    uint8_t dst_mac[6];
    uint8_t src_mac[6];
    uint16_t ether_type;
} __attribute(( __packed__ )) ether_head_t;


static int camio_ostream_netmap_eth_open(camio_ostream_t* this, const camio_descr_t* descr, camio_perf_t* perf_mon ){
    camio_ostream_netmap_eth_t* priv = this->priv;

    priv->netmap_base = camio_ostream_netmap_new(descr,NULL,priv->params,perf_mon);
    priv->is_closed = 0;
    return 0;
}

static void camio_ostream_netmap_eth_close(camio_ostream_t* this){
    camio_ostream_netmap_eth_t* priv = this->priv;
    priv->netmap_base->close(priv->netmap_base);
}



//Returns a pointer to a space of size len, ready for data
static uint8_t* camio_ostream_netmap_eth_start_write(camio_ostream_t* this, size_t len ){
    camio_ostream_netmap_eth_t* priv = this->priv;

    uint8_t* buff;
    buff = priv->netmap_base->start_write(priv->netmap_base, len + sizeof(ether_head_t));
    if(!buff){
        wprintf("Write length (%lu) is too long. Failing\n",len);
        return buff;
    }

    priv->base_buff_size = len + sizeof(ether_head_t);

    //Prepare the ethernet header
    ether_head_t* ether_head = (ether_head_t*)buff;
    memset(ether_head->dst_mac,0xFF,6);
    memset(ether_head->src_mac,0xEE,6);
    ether_head->ether_type = 0xAAAA;

    return buff + sizeof(ether_head); //Return the payload field
}

//Returns non-zero if a call to start_write will be non-blocking
static int camio_ostream_netmap_eth_ready(camio_ostream_t* this){
    camio_ostream_netmap_eth_t* priv = this->priv;
    return priv->netmap_base->ready(priv->netmap_base);
}



static uint8_t* camio_ostream_netmap_eth_end_write(camio_ostream_t* this, size_t len){
    camio_ostream_netmap_eth_t* priv = this->priv;

    if(len > priv->base_buff_size){
        wprintf("Truncating write. Write size (%lu) is greater than buffer size (%lu)\n", len, priv->base_buff_size);
    }

    priv->netmap_base->end_write(priv->netmap_base, MIN(len + sizeof(ether_head_t), priv->base_buff_size));
    return NULL;
}


static void camio_ostream_netmap_eth_flush(camio_ostream_t* this){
    camio_ostream_netmap_eth_t* priv = this->priv;
    return priv->netmap_base->flush(priv->netmap_base);
}



static  void camio_ostream_netmap_eth_delete(camio_ostream_t* ostream){
    camio_ostream_netmap_eth_t* priv = ostream->priv;
    priv->netmap_base->delete(priv->netmap_base);
    free(priv);
}

//Is this stream capable of taking over another stream buffer
static int camio_ostream_netmap_eth_can_assign_write(camio_ostream_t* this){
    //camio_ostream_netmap_eth_t* priv = this->priv;
    return 1;
}

//Assign the write buffer to the stream
static int camio_ostream_netmap_eth_assign_write(camio_ostream_t* this, uint8_t* buffer, size_t len){
    camio_ostream_netmap_eth_t* priv = this->priv;


    uint8_t* buff = priv->netmap_base->start_write(priv->netmap_base, len + sizeof(ether_head_t));
    if(!buff){
        wprintf("Write length (%lu) is too long. Failing\n",len);
        return -1;
    }
    priv->base_buff_size = len + sizeof(ether_head_t);

    //Prepare the ethernet header
    ether_head_t* ether_head = (ether_head_t*)buff;
    memset(ether_head->dst_mac,0xFF,6);
    memset(ether_head->src_mac,0xEE,6);
    ether_head->ether_type = 0xAAAA;

    memcpy(buff + sizeof(ether_head_t), buffer, len);

    return 0;
}


/* ****************************************************
 * Construction heavy lifting
 */

camio_ostream_t* camio_ostream_netmap_eth_construct(camio_ostream_netmap_eth_t* priv, const camio_descr_t* descr, camio_clock_t* clock, camio_ostream_netmap_params_t* params, camio_perf_t* perf_mon){
    if(!priv){
        eprintf_exit("netmap_eth stream supplied is null\n");
    }
    //Initialize the local variables
    priv->is_closed             = 0;
    priv->netmap_base           = NULL;
    priv->data_head             = NULL;
    priv->data_size             = 0;
    priv->base_buff_size        = 0;
    priv->params                = params;

    //Populate the function members
    priv->ostream.priv              = priv; //Lets us access private members from public functions
    priv->ostream.open              = camio_ostream_netmap_eth_open;
    priv->ostream.close             = camio_ostream_netmap_eth_close;
    priv->ostream.start_write       = camio_ostream_netmap_eth_start_write;
    priv->ostream.end_write         = camio_ostream_netmap_eth_end_write;
    priv->ostream.ready             = camio_ostream_netmap_eth_ready;
    priv->ostream.delete            = camio_ostream_netmap_eth_delete;
    priv->ostream.can_assign_write  = camio_ostream_netmap_eth_can_assign_write;
    priv->ostream.assign_write      = camio_ostream_netmap_eth_assign_write;
    priv->ostream.flush             = camio_ostream_netmap_eth_flush;
    priv->ostream.clock             = clock;
    priv->ostream.fd                = -1;

    //Call open, because its the obvious thing to do now...
    priv->ostream.open(&priv->ostream, descr, perf_mon);

    //Return the generic ostream interface for the outside world
    return &priv->ostream;

}

camio_ostream_t* camio_ostream_netmap_eth_new( const camio_descr_t* descr, camio_clock_t* clock, camio_ostream_netmap_params_t* params, camio_perf_t* perf_mon){
    camio_ostream_netmap_eth_t* priv = malloc(sizeof(camio_ostream_netmap_eth_t));
    if(!priv){
        eprintf_exit("No memory available for ostream netmap_eth creation\n");
    }
    return camio_ostream_netmap_eth_construct(priv, descr, clock, params, perf_mon);
}



