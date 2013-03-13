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
    priv->working_buffer_size = getpagesize() * 1024;
    priv->working_buffer = malloc(priv->working_buffer_size);
    if(!priv->working_buffer){
        eprintf_exit( "Failed to allocate working buffer\n");
    }

    priv->is_closed = 0;
    return 0;

}


void camio_iostream_delimiter_close(camio_iostream_t* this){
    camio_iostream_delimiter_t* priv = this->priv;
    priv->is_closed = 1;
    free(priv->working_buffer);
}


static int prepare_next(camio_iostream_delimiter_t* priv){

    if(priv->base->rready(priv->base)){

        //----- BASE START READ
        priv->read_buffer_size = priv->base->start_read(priv->base, &priv->read_buffer);

        while(priv->read_buffer_size > priv->working_buffer_size - priv->working_buffer_contents_size){
            priv->working_buffer_size *= 2;
            priv->working_buffer = realloc(priv->working_buffer, priv->working_buffer_size);
        }

        //TODO XXX, can potentially avoid this if the delimiter says that data in read_buffer is a complete packet but have to deal
        //(potentially) with a partial fragment(s) left over in the buffer.
        memcpy(priv->working_buffer + priv->working_buffer_contents_size, priv->read_buffer, priv->read_buffer_size);
        priv->working_buffer_contents_size += priv->read_buffer_size;

        priv->base->end_read(priv->base, NULL);
        //------ BASE END READ

        priv->read_buffer = NULL;
        priv->read_buffer_size = 0;

        uint64_t delimit_size = priv->delimit(priv->working_buffer, priv->working_buffer_contents_size);
        if(delimit_size){
            priv->result_buffer = priv->working_buffer;
            priv->result_buffer_size = delimit_size;
            return priv->read_buffer_size;
        }
    }

    return 0;

}

int camio_iostream_delimiter_rready(camio_iostream_t* this){
    camio_iostream_delimiter_t* priv = this->priv;
    if(priv->is_closed){
        return 0;
    }

    if(priv->result_buffer_size){
        return priv->result_buffer_size;
    }


    return prepare_next(priv);
}


static int camio_iostream_delimiter_start_read(camio_iostream_t* this, uint8_t** out){
    camio_iostream_delimiter_t* priv = this->priv;
    *out = NULL;

    //Spin waiting for the stream to become available
    while(!prepare_next(priv)){
    }

    *out = priv->result_buffer;
    return  priv->result_buffer_size;
}


int camio_iostream_delimiter_end_read(camio_iostream_t* this, uint8_t* free_buff){
    camio_iostream_delimiter_t* priv = this->priv;
    if(priv->result_buffer_size < priv->working_buffer_contents_size){
        priv->working_buffer_contents_size -= priv->result_buffer_size;

        //Optimistically check if there happens to be another packet ready to go?
        uint64_t delimit_size = priv->delimit(priv->working_buffer, priv->working_buffer_contents_size);
        if(delimit_size){
            //Ready for the next round with data available!
            priv->result_buffer = priv->working_buffer;
            priv->result_buffer_size = delimit_size;
            return 0;
        }

        //Nope, ok, bite the bullet and move stuff around.
        memmove(priv->working_buffer, priv->working_buffer + priv->result_buffer_size, priv->working_buffer_contents_size);
    }
    else{
        priv->working_buffer_contents_size = 0;
    }

    //Ready for the next round
    priv->result_buffer_size = 0;
    priv->result_buffer = NULL;
    return 0;

}


int camio_iostream_delimiter_selector_ready(camio_selectable_t* stream){
    camio_iostream_t* this = container_of(stream, camio_iostream_t,selector);
    return this->rready(this);
}


void camio_iostream_delimiter_delete(camio_iostream_t* this){
    camio_iostream_delimiter_close(this);

    camio_iostream_delimiter_t* priv = this->priv;
    priv->base->delete(priv->base);
    free(priv);
}


/* ****************************************************
 * Construction
 */

camio_iostream_t* camio_iostream_delimiter_construct(camio_iostream_delimiter_t* priv, camio_iostream_t* base, delimiter_f delimit, camio_iostream_delimiter_params_t* params){
    if(!priv){
        eprintf_exit("delimiter stream supplied is null\n");
    }

    //Initialize the local variables
    priv->base                          = base;
    priv->params                        = params;
    priv->delimit                       = delimit;
    priv->read_buffer                   = NULL;
    priv->read_buffer_size              = 0;
    priv->working_buffer                = NULL;
    priv->working_buffer_size           = 0;
    priv->working_buffer_contents_size  = 0;
    priv->result_buffer                 = NULL;
    priv->result_buffer_size            = 0;



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

    priv->iostream.selector.fd      = base->selector.fd;


    //Call open, because its the obvious thing to do now...
    camio_iostream_delimiter_open(&priv->iostream);

    //Return the generic iostream interface for the outside world to use
    return &priv->iostream;

}

camio_iostream_t* camio_iostream_delimiter_new(camio_iostream_t* base, delimiter_f delimit, void* parameters){
    camio_iostream_delimiter_t* priv = malloc(sizeof(camio_iostream_delimiter_t));
    if(!priv){
        eprintf_exit("No memory available for delimiter istream creation\n");
    }
    return camio_iostream_delimiter_construct(priv, base, delimit, parameters);
}


#endif /* DELIMITER_C_ */
