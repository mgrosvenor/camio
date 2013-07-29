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



static int camio_iostream_delimiter_open(camio_iostream_t* this){
    camio_iostream_delimiter_t* priv = this->priv;

    //Allocate 1024 pages for the initial buffer, this may have to grow later.
    priv->working_buffer_size = 4* getpagesize() * 1024;
    priv->working_buffer = malloc(priv->working_buffer_size);
    if(!priv->working_buffer){
        eprintf_exit( "Failed to allocate working buffer\n");
    }

    priv->is_closed = 0;
    return 0;

}


static void camio_iostream_delimiter_close(camio_iostream_t* this){
    camio_iostream_delimiter_t* priv = this->priv;
    if(!priv->is_closed){
        free(priv->working_buffer);
        priv->working_buffer = NULL;
        priv->working_buffer_size = 0;
        priv->working_buffer_contents_size = 0;
        priv->result_buffer = NULL;
        priv->result_buffer_size = 0;
        priv->is_closed = 1;
    }
}


static int prepare_next(camio_iostream_delimiter_t* priv){

    if(priv->result_buffer && priv->result_buffer_size){
        return priv->result_buffer_size;
    }

    if(priv->working_buffer_contents_size){
        int64_t delimit_size = priv->delimit(priv->working_buffer, priv->working_buffer_contents_size);
        if(delimit_size > 0){
            priv->result_buffer = priv->working_buffer;
            priv->result_buffer_size = delimit_size;
            return priv->result_buffer_size;
        }
    }


    if(priv->base->rready(priv->base)){

        //----- BASE START READ
        priv->read_buffer_size = priv->base->start_read(priv->base, &priv->read_buffer);
        //printf("Read another %lubytes from %p\n", priv->read_buffer_size, priv->read_buffer);

        //There is no more data to read, time to give up
        if(priv->read_buffer_size == 0){
            camio_iostream_delimiter_close(&priv->iostream);
            return 1;
        }

        while(priv->read_buffer_size > priv->working_buffer_size - priv->working_buffer_contents_size){
            //printf("Growing working buffer from %lu to %lu\n", priv->working_buffer_size, priv->working_buffer_size * 2 );
            priv->working_buffer_size *= 2;
            priv->working_buffer = realloc(priv->working_buffer, priv->working_buffer_size);
        }

        //TODO XXX, can potentially avoid this if the delimiter says that data in read_buffer is a complete packet but have to deal
        //(potentially) with a partial fragment(s) left over in the buffer.
	        
        //printf("Adding %lu bytes at %p (offset=%lu)\n", priv->read_buffer_size, priv->working_buffer + priv->working_buffer_contents_size, priv->working_buffer_contents_size);
	    memcpy(priv->working_buffer + priv->working_buffer_contents_size, priv->read_buffer, priv->read_buffer_size);
        priv->working_buffer_contents_size += priv->read_buffer_size;

        priv->base->end_read(priv->base, NULL);
        //------ BASE END READ

        priv->read_buffer = NULL;
        priv->read_buffer_size = 0;

        int64_t delimit_size = priv->delimit(priv->working_buffer, priv->working_buffer_contents_size);
        if(delimit_size > 0 && delimit_size <= priv->working_buffer_contents_size){
            priv->result_buffer = priv->working_buffer;
            priv->result_buffer_size = delimit_size;
            //printf("Found packet size=%lu\n", priv->result_buffer_size);
            return priv->result_buffer_size;
        }
    }

    return 0;

}

static int camio_iostream_delimiter_rready(camio_iostream_t* this){
    camio_iostream_delimiter_t* priv = this->priv;
    if(priv->is_closed){
        return 1;
    }

    if(priv->result_buffer_size){
        return priv->result_buffer_size;
    }


    return prepare_next(priv);
}


static int camio_iostream_delimiter_start_read(camio_iostream_t* this, uint8_t** out){
    camio_iostream_delimiter_t* priv = this->priv;
    *out = NULL;

    if(priv->is_closed){
        return 0;
    }

    //Spin waiting for the stream to become available
    while(!priv->result_buffer_size){
        prepare_next(priv);
    }

    //printf("result_buffer=%p, result_buffer_size=%lu, result_offset=%lu, working_size=%lu content_size=%lu content leftover=%lu\n",  priv->result_buffer, priv->result_buffer_size, priv->result_buffer - priv->working_buffer, priv->working_buffer_size, priv->working_buffer_contents_size, priv->working_buffer_contents_size % 288);
    *out = priv->result_buffer;
    return  priv->result_buffer_size;
}

//** Warning: This is a complicated function with a lot of edge cases. It's vital to performance and
//            correctness to get this one right!
//
// We enter this function as a result of a successful call to start read, which implies a successful
// call to prepare_next(). prepar_next() may have succeeded for one of two reasons, 1) either new data was
// read and the delimiter was successful, or, 2) we have optimistically found another delimited result in
// a previous call to end_read(). In the first case, priv->working_buffer is at least equal to the size of
// priv->result_buffer_size, in the second case, priv->working buffer size may be samller than
// priv->result_buffer_size. We must handle both of these cases here.

static int camio_iostream_delimiter_end_read(camio_iostream_t* this, uint8_t* free_buff){
    camio_iostream_delimiter_t* priv = this->priv;

    uint8_t* result_head_next = priv->result_buffer + priv->result_buffer_size ;

    //Handle case 1)
    //New data was read and the delimiter was successful
    if(priv->result_buffer_size <= priv->working_buffer_contents_size){

        //This is how much is now left over
        priv->working_buffer_contents_size -= priv->result_buffer_size;

        //If there is nothing left over, give up and bug out
        if(!priv->working_buffer_size){
            return 0;
        }

        //Something is left over we can optimistically check if there happens to be another packet ready to go.
        //The reduces the number of memmoves and significantly improves performance especially with TCP where the
        //read size can be *huge*. (>4MB with TSO turned on)
        uint64_t delimit_size = priv->delimit(result_head_next, priv->working_buffer_contents_size);
        if(delimit_size && delimit_size <= priv->working_buffer_contents_size){
            //Ready for the next round with data available! No memmove required!
            priv->result_buffer = result_head_next;
            priv->result_buffer_size = delimit_size;
            return 0;
        }

        //Nope, ok, bite the bullet and move stuff around. We'll need to call read() and get some more data
        //printf("Doing mem move of %lu from %p to %p (total=%lu)\n", priv->working_buffer_contents_size, result_head_next, priv->working_buffer, result_head_next - priv->working_buffer);
        memmove(priv->working_buffer, result_head_next, priv->working_buffer_contents_size);
    }

    //Handle case 2)
    //There is some data leftover, but less than the last result buffer size.
    else{

        //Optimistically check if there is a new packet in this left over. Remember, packets are not all the same
        //size, so a samll packet could follow a big packet.
        uint64_t delimit_size = priv->delimit(result_head_next, priv->working_buffer_contents_size);
        if(delimit_size && delimit_size <= priv->working_buffer_contents_size){
            //Ready for the next round with data available! Yay! No memmove required!
            priv->result_buffer = result_head_next;
            priv->result_buffer_size = delimit_size;
            return 0;
        }

        //Nope, ok, bite the bullet and move stuff around. We'll need to call read() and get some more data
        //printf("Doing mem move of %lu from %p to %p (total=%lu)\n", priv->working_buffer_contents_size, result_head_next, priv->working_buffer, result_head_next - priv->working_buffer);
        memmove(priv->working_buffer, result_head_next, priv->working_buffer_contents_size);

    }

    //Ready for the next round, there is no new delimited packet available, so reset everything and get ready for a call to read()
    priv->result_buffer_size = 0;
    priv->result_buffer = NULL;
    return 0;

}


static int camio_iostream_delimiter_selector_ready(camio_selectable_t* stream){
    camio_iostream_t* this = container_of(stream, camio_iostream_t,selector);
    return this->rready(this);
}


static void camio_iostream_delimiter_delete(camio_iostream_t* this){
    camio_iostream_delimiter_close(this);

    camio_iostream_delimiter_t* priv = this->priv;
    priv->base->delete(priv->base);
    free(priv);
}


static int camio_iostream_delimiter_wready(camio_iostream_t* this){
    camio_iostream_delimiter_t* priv = this->priv;
    return priv->base->wready(priv->base);
}


static uint8_t* camio_iostream_delimiter_start_write(camio_iostream_t* this, size_t len ){
    camio_iostream_delimiter_t* priv = this->priv;
    return priv->base->start_write(priv->base, len);
}

static uint8_t* camio_iostream_delimiter_end_write(camio_iostream_t* this, size_t len){
    camio_iostream_delimiter_t* priv = this->priv;
    return priv->base->end_write(priv->base, len);
}


static int camio_iostream_delimiter_can_assign_write(camio_iostream_t* this){
    camio_iostream_delimiter_t* priv = this->priv;
    return priv->base->can_assign_write(priv->base);
}

static int camio_iostream_delimiter_assign_write(camio_iostream_t* this, uint8_t* buffer, size_t len){
    camio_iostream_delimiter_t* priv = this->priv;
    return priv->base->assign_write(priv->base, buffer,len);
}

static void camio_iostream_delimiter_wsync(camio_iostream_t* this){
    camio_iostream_delimiter_t* priv = this->priv;
    return priv->base->wsync(priv->base);
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

    priv->iostream.start_write      = camio_iostream_delimiter_start_write;
    priv->iostream.end_write        = camio_iostream_delimiter_end_write;
    priv->iostream.can_assign_write = camio_iostream_delimiter_can_assign_write;
    priv->iostream.assign_write     = camio_iostream_delimiter_assign_write;
    priv->iostream.wready           = camio_iostream_delimiter_wready;
    priv->iostream.wsync            = camio_iostream_delimiter_wsync;

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
