/*
 * Copyright  (C) Matthew P. Grosvenor, 2012, All Rights Reserved
 *
 * Camio log (newline separated) output stream
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

#include "camio_ostream_log.h"

#define CAMIO_OSTREAM_LOG_BUFF_INIT (4 * 1024 * 1024) //4MB

int camio_ostream_log_open(camio_ostream_t* this, const camio_descr_t* descr ){
    camio_ostream_log_t* priv = this->priv;

    if(unlikely(camio_descr_has_opts(descr->opt_head))){
        struct camio_opt_t*  opt;
        if( (opt = camio_descr_has_opt(descr->opt_head,"escape")) ){
            camio_descr_get_opt_bool(opt, &priv->escape);
        }
        else{
            eprintf_exit( "Unknown option supplied \"s\". Valid options for this stream are: \"escape=<bool>\"\n", descr->opt_head->name);
        }
    }

    priv->buffer = malloc(CAMIO_OSTREAM_LOG_BUFF_INIT);
    if(!priv->buffer){
        eprintf_exit( "Could not allocate output buffer\n");
    }
    priv->buffer_size  = CAMIO_OSTREAM_LOG_BUFF_INIT;

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

void camio_ostream_log_close(camio_ostream_t* this){
    camio_ostream_log_t* priv = this->priv;
    close(this->fd);
    priv->is_closed = 1;
}



//Returns a pointer to a space of size len, ready for data
uint8_t* camio_ostream_log_start_write(camio_ostream_t* this, size_t len ){
    camio_ostream_log_t* priv = this->priv;
    len += 1; //Add space for a newline

    //Grow the buffer if it's not big enough
    if(len > priv->buffer_size){
        priv->buffer = realloc(priv->buffer, len);
        if(!priv->buffer){
            eprintf_exit( "Could not grow line buffer\n");
        }
        priv->buffer_size = len;
    }

    return priv->buffer;
}

//Returns non-zero if a call to start_write will be non-blocking
int camio_ostream_log_ready(camio_ostream_t* this){
    //Not implemented
    eprintf_exit( "\n");
    return 0;
}


//Commit the data that's now in the buffer that was previously allocated
//Len must be equal to or less than len called with start_write
uint8_t* camio_ostream_log_end_write(camio_ostream_t* this, size_t len){
    camio_ostream_log_t* priv = this->priv;
    int result = 0;

    if(!priv->escape){ //The simple (fast) case
        if(priv->assigned_buffer){
            result = write(this->fd,priv->assigned_buffer,len);
            result += write(this->fd,"\n",1); //Hmmm don't like this...

            priv->assigned_buffer    = NULL;
            priv->assigned_buffer_sz = 0;
            return NULL;
        }

        priv->buffer[len] = '\n';
        result = write(this->fd,priv->buffer,len+1);
        return NULL;
    }

    const uint8_t* buffer = priv->assigned_buffer ? priv->assigned_buffer : priv->buffer;
    size_t i = 0;
    size_t begin = 0;
    char escaped_hex[5];
    for(; i < len; i++){
        if(buffer[i] < 0x20 || buffer[i] > 0x7E   ){
            if( (i > 0) && (i - begin > 0) ){
                result += write(this->fd,buffer + begin, i - begin);
            }
            snprintf(escaped_hex,5,"\\x%02X",(uint8_t)buffer[i]);
            result += write(this->fd,escaped_hex,4);
            begin = i+1;
        }
    }
    result += write(this->fd,"\n",1);

    if(priv->assigned_buffer){
        priv->assigned_buffer    = NULL;
        priv->assigned_buffer_sz = 0;
        return NULL;
    }

    return NULL;

}


void camio_ostream_log_delete(camio_ostream_t* ostream){
    ostream->close(ostream);
    camio_ostream_log_t* priv = ostream->priv;
    free(priv);
}

//Is this stream capable of taking over another stream buffer
int camio_ostream_log_can_assign_write(camio_ostream_t* this){
    return 1;
}

//Assign the write buffer to the stream
int camio_ostream_log_assign_write(camio_ostream_t* this, uint8_t* buffer, size_t len){
    camio_ostream_log_t* priv = this->priv;

    if(!buffer){
        eprintf_exit("Assigned buffer is null.");
    }

    priv->assigned_buffer    = buffer;
    priv->assigned_buffer_sz = len;

    return 0;
}


/* ****************************************************
 * Construction heavy lifting
 */

camio_ostream_t* camio_ostream_log_construct(camio_ostream_log_t* priv, const camio_descr_t* descr, camio_clock_t* clock, camio_ostream_log_params_t* params){
    if(!priv){
        eprintf_exit("log stream supplied is null\n");
    }
    //Initialize the local variables
    priv->is_closed             = 1;
    priv->escape                = 0; //Off for the moment until the istream works //Escape by default
    priv->buffer_size           = 0;
    priv->buffer                = NULL;
    priv->assigned_buffer       = NULL;
    priv->assigned_buffer_sz    = 0;
    priv->params                = params;


    //Populate the function members
    priv->ostream.priv              = priv; //Lets us access private members from public functions
    priv->ostream.open              = camio_ostream_log_open;
    priv->ostream.close             = camio_ostream_log_close;
    priv->ostream.start_write       = camio_ostream_log_start_write;
    priv->ostream.end_write         = camio_ostream_log_end_write;
    priv->ostream.ready             = camio_ostream_log_ready;
    priv->ostream.delete            = camio_ostream_log_delete;
    priv->ostream.can_assign_write  = camio_ostream_log_can_assign_write;
    priv->ostream.assign_write      = camio_ostream_log_assign_write;
    priv->ostream.clock             = clock;
    priv->ostream.fd                = -1;

    //Call open, because its the obvious thing to do now...
    priv->ostream.open(&priv->ostream, descr);

    //Return the generic ostream interface for the outside world
    return &priv->ostream;

}

camio_ostream_t* camio_ostream_log_new( const camio_descr_t* descr, camio_clock_t* clock, camio_ostream_log_params_t* params){
    camio_ostream_log_t* priv = malloc(sizeof(camio_ostream_log_t));
    if(!priv){
        eprintf_exit("No memory available for ostream log creation\n");
    }
    return camio_ostream_log_construct(priv, descr, clock, params);
}



