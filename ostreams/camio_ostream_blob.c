/*
 * Copyright  (C) Matthew P. Grosvenor, 2012, All Rights Reserved
 *
 * Fe2+ blob (newline separated) output stream
 *
 */
#include <errno.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

#include "../utils/camio_util.h"
#include "../errors/camio_errors.h"
#include "../stream_description/camio_opt_parser.h"

#include "camio_ostream_blob.h"

#define CAMIO_OSTREAM_BLOB_INIT_BUFF_SIZE (4 * 1024ULL) //4kB initial buffer, sensible size
#define CAMIO_OSTREAM_BLOB_INIT_WRITE_SIZE (512 * 1024 * 1024ULL) //512MB per write. This is stupidly large.

int camio_ostream_blob_open(camio_ostream_t* this, const camio_descr_t* descr ){
    camio_ostream_blob_t* priv = this->priv;

    if(has_opts(descr->opt_head)){
        eprintf_exit( "Option(s) supplied, but none expected\n");
    }

    if(!descr->query){
        eprintf_exit( "No interface supplied\n");
    }

    priv->buffer = malloc(CAMIO_OSTREAM_BLOB_INIT_BUFF_SIZE);
    if(!priv->buffer){
        eprintf_exit( "Could not allocate output buffer\n");
    }
    priv->buffer_size  = CAMIO_OSTREAM_BLOB_INIT_BUFF_SIZE;

    //If we have a file descriptor from the outside world, then use it!
    if(priv->params){
        if(priv->params->fd > -1){
            this->fd = priv->params->fd;
            priv->is_closed = 0;
            return 0;
        }
    }

    //Grab a file descriptor and rock on
    if(!descr->query){
        eprintf_exit( "No filename supplied\n");
    }

    this->fd = open(descr->query, O_WRONLY | O_CREAT | O_TRUNC, (mode_t)(0666));
    if(this->fd == -1){
        eprintf_exit( "Could not open file \"%s\"\n", descr->query);
    }
    priv->is_closed = 0;
    return 0;
}

void camio_ostream_blob_close(camio_ostream_t* this){
    camio_ostream_blob_t* priv = this->priv;
    close(this->fd);
    free(priv->buffer);
    priv->is_closed = 1;
}



//Returns a pointer to a space of size len, ready for data
uint8_t* camio_ostream_blob_start_write(camio_ostream_t* this, size_t len ){
    camio_ostream_blob_t* priv = this->priv;

    //Grow the buffer if it's not big enough
    if(unlikely(len > priv->buffer_size)){
        priv->buffer = realloc(priv->buffer, len);
        if(!priv->buffer){
            eprintf_exit( "Could not grow line buffer\n");
        }
        priv->buffer_size = len;
    }

    //Hopefully full straight through
    return priv->buffer;
}

//Returns non-zero if a call to start_write will be non-blocking
int camio_ostream_blob_ready(camio_ostream_t* this){
    //Not implemented
    eprintf_exit( "\n");
    return 0;
}


//Commit the data that's now in the buffer that was previously allocated
//Len must be equal to or less than len called with start_write
uint8_t* camio_ostream_blob_end_write(camio_ostream_t* this, size_t len){
    camio_ostream_blob_t* priv = this->priv;
    size_t total_written = 0;
    size_t written = 0;

    const uint8_t* buffer = priv->assigned_buffer ? priv->assigned_buffer : priv->buffer;
    while(likely(total_written < len)){
    	const size_t left_to_write = len - total_written;
    	written = write(this->fd,buffer + total_written, MIN(left_to_write, priv->write_size));
        total_written += written;

        //Grow this stupidly fast, we really don't want to be falling into this case
        if(unlikely(written >= priv->write_size)){
        	//Catch the overflow case
            if(unlikely(priv->write_size * 8 < priv->write_size)){
            	priv->write_size = ~0;
            }
            else{
            	priv->write_size *= 8;
            }
        }
    }

    if(likely(priv->assigned_buffer != NULL)){
        priv->assigned_buffer    = NULL;
        priv->assigned_buffer_sz = 0;
    }

    return NULL;
}


void camio_ostream_blob_delete(camio_ostream_t* ostream){
    ostream->close(ostream);
    camio_ostream_blob_t* priv = ostream->priv;
    free(priv);
}

//Is this stream capable of taking over another stream buffer
int camio_ostream_blob_can_assign_write(camio_ostream_t* this){
    return 1;
}

//Assign the write buffer to the stream
int camio_ostream_blob_assign_write(camio_ostream_t* this, uint8_t* buffer, size_t len){
    camio_ostream_blob_t* priv = this->priv;

    if(unlikely(!buffer)){
        eprintf_exit("Assigned buffer is null.");
    }

    priv->assigned_buffer    = buffer;
    priv->assigned_buffer_sz = len;

    return 0;
}


/* ****************************************************
 * Construction heavy lifting
 */

camio_ostream_t* camio_ostream_blob_construct(camio_ostream_blob_t* priv, const camio_descr_t* descr, camio_clock_t* clock, camio_ostream_blob_params_t* params){
    if(!priv){
        eprintf_exit("blob stream supplied is null\n");
    }
    //Initialize the local variables
    priv->is_closed             = 1;
    priv->escape                = 0; //Off for the moment until the istream works //Escape by default
    priv->buffer_size           = 0;
    priv->buffer                = NULL;
    priv->assigned_buffer       = NULL;
    priv->assigned_buffer_sz    = 0;
    priv->write_size            = CAMIO_OSTREAM_BLOB_INIT_WRITE_SIZE;
    priv->params                = params;


    //Populate the function members
    priv->ostream.priv              = priv; //Lets us access private members from public functions
    priv->ostream.open              = camio_ostream_blob_open;
    priv->ostream.close             = camio_ostream_blob_close;
    priv->ostream.start_write       = camio_ostream_blob_start_write;
    priv->ostream.end_write         = camio_ostream_blob_end_write;
    priv->ostream.ready             = camio_ostream_blob_ready;
    priv->ostream.delete            = camio_ostream_blob_delete;
    priv->ostream.can_assign_write  = camio_ostream_blob_can_assign_write;
    priv->ostream.assign_write      = camio_ostream_blob_assign_write;
    priv->ostream.clock             = clock;
    priv->ostream.fd                = -1;

    //Call open, because its the obvious thing to do now...
    priv->ostream.open(&priv->ostream, descr);

    //Return the generic ostream interface for the outside world
    return &priv->ostream;

}

camio_ostream_t* camio_ostream_blob_new( const camio_descr_t* descr, camio_clock_t* clock, camio_ostream_blob_params_t* params){
    camio_ostream_blob_t* priv = malloc(sizeof(camio_ostream_blob_t));
    if(!priv){
        eprintf_exit("No memory available for ostream blob creation\n");
    }
    return camio_ostream_blob_construct(priv, descr, clock, params);
}



