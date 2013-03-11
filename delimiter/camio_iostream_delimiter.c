/*
 * delimiter.c
 *
 *  Created on: Mar 11, 2013
 *      Author: mgrosvenor
 */

#ifndef DELIMITER_C_
#define DELIMITER_C_

#include "../iostreams/camio_iostream.h"


/*
 * Copyright  (C) Matthew P. Grosvenor, 2012, All Rights Reserved
 *
 * DELIMITER Input/Output stream definition
 *
 */

#include <errno.h>
#include <unistd.h>
#include <stdint.h>

#include "camio_iostream_delimiter.h"
#include "../errors/camio_errors.h"
#include "../utils/camio_util.h"



int camio_iostream_delimiter_open(camio_iostream_t* this){
    camio_iostream_delimiter_t* priv = this->priv;

    //Allocate 1024 pages for the initial buffer, this may have to grow later.
    priv->rbuffer = malloc(getpagesize() * 1024);
    if(!priv->rbuffer){
        eprintf_exit( "Failed to allocate working buffer\n");
    }
    priv->rbuffer_size = getpagesize() * 1024;

    priv->is_closed = 0;
    return 0;

}


void camio_iostream_delimiter_close(camio_iostream_t* this){
    camio_iostream_delimiter_t* priv = this->priv;
    free(priv->rbuffer);
}


static int prepare_next(camio_iostream_delimiter_t* priv, int blocking){

    return 0;

}

int camio_iostream_delimiter_rready(camio_iostream_t* this){
    camio_iostream_delimiter_t* priv = this->priv;
    if(priv->bytes_read || priv->is_closed){
        return 1;
    }

    return prepare_next(priv,0);
    return 0;
}


static int camio_iostream_delimiter_start_read(camio_iostream_t* this, uint8_t** out){
    *out = NULL;

    return  result;
}


int camio_iostream_delimiter_end_read(camio_iostream_t* this, uint8_t* free_buff){
    return 0; //Always true for socket I/O
}


int camio_iostream_delimiter_selector_ready(camio_selectable_t* stream){
    camio_iostream_t* this = container_of(stream, camio_iostream_t,selector);
    return this->rready(this);
}


void camio_iostream_delimiter_delete(camio_iostream_t* this){
    this->close(this);

    camio_iostream_delimiter_t* priv = this->priv;
    priv->base->delete(priv->base);
    free(priv);
}


/* ****************************************************
 * Construction
 */

camio_iostream_t* camio_iostream_delimiter_construct(camio_iostream_delimiter_t* priv, camio_iostream_t* base, camio_iostream_delimiter_params_t* params){
    if(!priv){
        eprintf_exit("delimiter stream supplied is null\n");
    }

    //Initialize the local variables
    priv->base              = base;
    priv->params            = params;
    priv->rbuffer           = NULL;
    priv->rbuffer_size      = 0;


    //Populate the function members
    priv->iostream.priv             = priv; //Lets us access private members
    priv->iostream.open             = NULL;
    priv->iostream.close            = camio_iostream_delimiter_close;
    priv->iostream.delete           = camio_iostream_delimiter_delete;
    priv->iostream.start_read       = camio_iostream_delimiter_start_read;
    priv->iostream.end_read         = camio_iostream_delimiter_end_read;
    priv->iostream.rready           = camio_iostream_delimiter_rready;
    priv->iostream.selector.ready   = camio_iostream_delimiter_selector_ready;

    priv->iostream.start_write      = base->start_write;
    priv->iostream.end_write        = base->end_write;
    priv->iostream.can_assign_write = base->can_assign_write;
    priv->iostream.assign_write     = base->assign_write;
    priv->iostream.wready           = base->wready;

    priv->iostream.selector.fd      = -1;


    //Call open, because its the obvious thing to do now...
    priv->iostream.open(&priv->iostream);

    //Return the generic istream interface for the outside world to use
    return &priv->iostream;

}

camio_iostream_t* camio_iostream_delimiter_new(camio_iostream_t* base, void* parameters){
    camio_iostream_delimiter_t* priv = malloc(sizeof(camio_iostream_delimiter_t));
    if(!priv){
        eprintf_exit("No memory available for delimiter istream creation\n");
    }
    return camio_iostream_delimiter_construct(priv, base, parameters);
}


#endif /* DELIMITER_C_ */
