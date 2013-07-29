/*
 * Copyright  (C) Matthew P. Grosvenor, 2012, All Rights Reserved
 *
 * WRAPPER Input/Output stream definition
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

#include "camio_iostream_wrapper.h"
#include "../errors/camio_errors.h"
#include "../stream_description/camio_opt_parser.h"
#include "../utils/camio_util.h"


int camio_iostream_wrapper_rready(camio_iostream_t* this){
    camio_iostream_wrapper_t* priv = this->priv;
    return priv->base_istream->ready(priv->base_istream);
}

int camio_iostream_wrapper_selector_ready(camio_selectable_t* stream){
    camio_iostream_t* this = container_of(stream, camio_iostream_t,selector);
    return this->rready(this);
}


static int camio_iostream_wrapper_start_read(camio_iostream_t* this, uint8_t** out){
    camio_iostream_wrapper_t* priv = this->priv;
    return priv->base_istream->start_read(priv->base_istream,out);
}


int camio_iostream_wrapper_end_read(camio_iostream_t* this, uint8_t* free_buff){
    camio_iostream_wrapper_t* priv = this->priv;
    return priv->base_istream->end_read(priv->base_istream,free_buff);
}


//Returns non-zero if a call to start_write will be non-blocking
int camio_iostream_wrapper_wready(camio_iostream_t* this){
    //Not implemented
    eprintf_exit("Not implemented\n");
    return 0;
}


//Returns a pointer to a space of size len, ready for data
uint8_t* camio_iostream_wrapper_start_write(camio_iostream_t* this, size_t len ){
    camio_iostream_wrapper_t* priv = this->priv;
    return priv->base_ostream->start_write(priv->base_ostream, len);
}

//Commit the data to the buffer previously allocated
//Len must be equal to or less than len called with start_write
uint8_t* camio_iostream_wrapper_end_write(camio_iostream_t* this, size_t len){
    camio_iostream_wrapper_t* priv = this->priv;
    return priv->base_ostream->end_write(priv->base_ostream, len);
}

//Is this stream capable of taking over another stream buffer
int camio_iostream_wrapper_can_assign_write(camio_iostream_t* this){
    camio_iostream_wrapper_t* priv = this->priv;
    return priv->base_ostream->can_assign_write(priv->base_ostream);
}

//Assign the write buffer to the stream
int camio_iostream_wrapper_assign_write(camio_iostream_t* this, uint8_t* buffer, size_t len){
    camio_iostream_wrapper_t* priv = this->priv;
    return priv->base_ostream->assign_write(priv->base_ostream, buffer, len);
}



int camio_iostream_wrapper_open(camio_iostream_t* this, const camio_descr_t* descr, camio_perf_t* perf_mon ){
    camio_iostream_wrapper_t* priv = this->priv;

    if(unlikely(perf_mon == NULL)){
        eprintf_exit("No performance monitor supplied\n");
    }
    priv->perf_mon = perf_mon;

    priv->base_ostream = camio_ostream_new(priv->ostream_descr,NULL,priv->ostream_params, perf_mon);
    priv->base_istream = camio_istream_new(priv->istream_descr,NULL,priv->istream_params, perf_mon);

    return 0;
}


void camio_iostream_wrapper_close(camio_iostream_t* this){
    camio_iostream_wrapper_t* priv = this->priv;
    priv->base_istream->close(priv->base_istream);
    priv->base_ostream->close(priv->base_ostream);

}


void camio_iostream_wrapper_delete(camio_iostream_t* this){
    this->close(this);
    camio_iostream_wrapper_t* priv = this->priv;
    free(priv);
}




/* ****************************************************
 * Construction
 */

camio_iostream_t* camio_iostream_wrapper_construct(
        camio_iostream_wrapper_t* priv,
        char* istream_descr,
        char* ostream_descr,
        void* istream_params,
        void* ostream_params,
        camio_iostream_wrapper_params_t* params,
        camio_perf_t* perf_mon){

    if(!priv){
        eprintf_exit("wrapper stream supplied is null\n");
    }
    //Initialize the local variables
    priv->base_istream      = NULL;
    priv->base_ostream      = NULL;
    priv->istream_descr     = istream_descr;
    priv->ostream_descr     = ostream_descr;
    priv->istream_params    = istream_params;
    priv->ostream_params    = ostream_params;
    priv->params            = params;


    //Populate the function members
    priv->iostream.priv             = priv; //Lets us access private members
    priv->iostream.open             = camio_iostream_wrapper_open;
    priv->iostream.close            = camio_iostream_wrapper_close;
    priv->iostream.delete           = camio_iostream_wrapper_delete;
    priv->iostream.start_read       = camio_iostream_wrapper_start_read;
    priv->iostream.end_read         = camio_iostream_wrapper_end_read;
    priv->iostream.rready           = camio_iostream_wrapper_rready;
    priv->iostream.selector.ready   = camio_iostream_wrapper_selector_ready;

    priv->iostream.start_write      = camio_iostream_wrapper_start_write;
    priv->iostream.end_write        = camio_iostream_wrapper_end_write;
    priv->iostream.can_assign_write = camio_iostream_wrapper_can_assign_write;
    priv->iostream.assign_write     = camio_iostream_wrapper_assign_write;
    priv->iostream.wready           = camio_iostream_wrapper_wready;

    priv->iostream.selector.fd      = -1;

    //Call open, because its the obvious thing to do now...
    priv->iostream.open(&priv->iostream, NULL, perf_mon);

    //Return the generic istream interface for the outside world to use
    return &priv->iostream;

}

camio_iostream_t* camio_iostream_wrapper_new(
        char* istream_descr,
        char* ostream_descr,
        void* istream_params,
        void* ostream_params,
        camio_iostream_wrapper_params_t*
        params, camio_perf_t* perf_mon){

    camio_iostream_wrapper_t* priv = malloc(sizeof(camio_iostream_wrapper_t));
    if(!priv){
        eprintf_exit("No memory available for wrapper iostream wrapper creation\n");
    }
    return camio_iostream_wrapper_construct(priv, istream_descr, ostream_descr, istream_params, ostream_params, params, perf_mon);
}



















int camio_iostream_wrapper_open_e(camio_iostream_t* this, const camio_descr_t* descr, camio_perf_t* perf_mon ){
    camio_iostream_wrapper_t* priv = this->priv;

    if(unlikely(perf_mon == NULL)){
        eprintf_exit("No performance monitor supplied\n");
    }
    priv->perf_mon = perf_mon;

    return 0;
}




camio_iostream_t* camio_iostream_wrapper_construct_e(
        camio_iostream_wrapper_t* priv,
        camio_istream_t* istream,
        camio_ostream_t* ostream,
        camio_iostream_wrapper_params_t* params,
        camio_perf_t* perf_mon){

    if(!priv){
        eprintf_exit("wrapper stream supplied is null\n");
    }
    //Initialize the local variables
    priv->base_istream      = istream;
    priv->base_ostream      = ostream;
    priv->params            = params;


    //Populate the function members
    priv->iostream.priv             = priv; //Lets us access private members
    priv->iostream.open             = camio_iostream_wrapper_open_e;
    priv->iostream.close            = camio_iostream_wrapper_close;
    priv->iostream.delete           = camio_iostream_wrapper_delete;
    priv->iostream.start_read       = camio_iostream_wrapper_start_read;
    priv->iostream.end_read         = camio_iostream_wrapper_end_read;
    priv->iostream.rready           = camio_iostream_wrapper_rready;
    priv->iostream.selector.ready   = camio_iostream_wrapper_selector_ready;

    priv->iostream.start_write      = camio_iostream_wrapper_start_write;
    priv->iostream.end_write        = camio_iostream_wrapper_end_write;
    priv->iostream.can_assign_write = camio_iostream_wrapper_can_assign_write;
    priv->iostream.assign_write     = camio_iostream_wrapper_assign_write;
    priv->iostream.wready           = camio_iostream_wrapper_wready;

    priv->iostream.selector.fd      = -1;

    //Call open, because its the obvious thing to do now...
    priv->iostream.open(&priv->iostream, NULL, perf_mon);

    //Return the generic istream interface for the outside world to use
    return &priv->iostream;

}

camio_iostream_t* camio_iostream_wrapper_new_e(
        camio_istream_t* istream,
        camio_ostream_t* ostream,
        camio_iostream_wrapper_params_t*
        params, camio_perf_t* perf_mon){

    camio_iostream_wrapper_t* priv = malloc(sizeof(camio_iostream_wrapper_t));
    if(!priv){
        eprintf_exit("No memory available for wrapper iostream wrapper creation\n");
    }
    return camio_iostream_wrapper_construct_e(priv, istream, ostream, params, perf_mon);
}








