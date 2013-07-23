/*
 * Copyright  (C) Matthew P. Grosvenor, 2012, All Rights Reserved
 *
 * Camio fio (newline separated) input stream
 *
 */
#include <errno.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

#include "camio_istream_fio.h"
#include "../errors/camio_errors.h"
#include "../stream_description/camio_opt_parser.h"
#include "../utils/camio_util.h"

#define CAMIO_ISTREAM_ISTREAM_FIO_BUFF_INIT 4096

int camio_istream_fio_open(camio_istream_t* this, const camio_descr_t* descr, camio_perf_t* perf_mon ){
    camio_istream_fio_t* priv = this->priv;

    if(unlikely(perf_mon == NULL)){
        eprintf_exit("No performance monitor supplied\n");
    }
    priv->perf_mon = perf_mon;


    if(unlikely(camio_descr_has_opts(descr->opt_head))){
        struct camio_opt_t*  opt;
        if( (opt = camio_descr_has_opt(descr->opt_head,"chunk")) ){
            camio_descr_get_opt_int(opt, &priv->max_chunk);
        }
        else{
            eprintf_exit( "Unknown option supplied \"s\". Valid options for this stream are: \"chunk=<uint64>\"\n", descr->opt_head->name);
        }
    }

    if(priv->max_chunk <= 0){
        priv->max_chunk = (1024 * 32); //32Kb default read chunk.
    }


    priv->read_buff = malloc(priv->max_chunk);
    if(!priv->read_buff){
        eprintf_exit("Could not allocate buffer. Giving up\n");
    }
    priv->read_buff_size  = priv->max_chunk;

    //If we have a file descriptor from the outside world, then use it!
    if(priv->params){
        if(priv->params->fd > -1){
            this->selector.fd = priv->params->fd;
            priv->is_closed = 0;
            return 0;
        }
    }

    //Grab a file descriptor and rock on
    if(!descr->query){
        eprintf_exit( "No filename supplied\n");
    }

    this->selector.fd = open(descr->query, O_RDONLY);
    if(this->selector.fd == -1){
        printf("\"%s\"",descr->query);
        eprintf_exit( "Could not open file \"%s\"\n", descr->query);
    }
    priv->is_closed = 0;
    return 0;
}


void camio_istream_fio_close(camio_istream_t* this){
    camio_istream_fio_t* priv = this->priv;
    close(this->selector.fd);
    priv->is_closed = 1;
    priv->read_buff = NULL;
    priv->read_buff_size = 0;
}


static inline void set_fd_blocking(int fd, int blocking){
    int flags = fcntl(fd, F_GETFL, 0);

    if (flags == -1){
        eprintf_exit("Could not get file flags\n");
    }

    if (blocking){
        flags &= ~O_NONBLOCK;
    }
    else{
        flags |= O_NONBLOCK;
    }

    if( fcntl(fd, F_SETFL, flags) == -1){
        eprintf_exit("Could not set file flags\n");
    }
}



static int prepare_next(camio_istream_fio_t* priv, int blocking){
    if(priv->read_buff_data_size){
        return priv->read_buff_size;
    }

    set_fd_blocking(priv->istream.selector.fd,blocking);

    //Read at most max_chunk of data
    int bytes = read(priv->istream.selector.fd,priv->read_buff,priv->max_chunk);

    //Was there some error
    if(bytes < 0){
        if(errno == EAGAIN || errno == EWOULDBLOCK){
            return 0; //Reading would have blocked, we don't want this
        }

        //Uh ohh, some other error! Eek! Die!
        camio_perf_event_start(priv->perf_mon,CAMIO_PERF_EVENT_ISTREAM_FIO,CAMIO_PERF_COND_READ_ERROR);
        eprintf_exit("Could not read file, input error no=%i (%s)\n", errno, strerror(errno));

    }

    //We've hit the end of the file. Close and leave.
    if(bytes == 0){
        priv->istream.close(&priv->istream);
        camio_perf_event_start(priv->perf_mon,CAMIO_PERF_EVENT_ISTREAM_FIO,CAMIO_PERF_COND_NO_DATA);
        return 0;
    }

    //Woot
    priv->read_buff_data_size = bytes;
    camio_perf_event_start(priv->perf_mon,CAMIO_PERF_EVENT_ISTREAM_FIO,CAMIO_PERF_COND_NEW_DATA);
    return bytes;


}

int camio_istream_fio_ready(camio_istream_t* this){
    camio_istream_fio_t* priv = this->priv;
    if(priv->read_buff_data_size || priv->is_closed){
        return 1;
    }

    prepare_next(priv,CAMIO_ISTREAM_FIO_NONBLOCKING);

    return priv->read_buff_data_size || priv->is_closed;
}

int camio_istream_fio_start_read(camio_istream_t* this, uint8_t** out){
    *out = NULL;

    camio_istream_fio_t* priv = this->priv;
    if(priv->is_closed){
        return 0;
    }

    //Called read without calling ready, they must want to block
    if(!priv->read_buff_data_size){
        if(!prepare_next(priv,CAMIO_ISTREAM_FIO_BLOCKING)){
            return 0;
        }
    }

    *out = priv->read_buff;
    size_t result = priv->read_buff_data_size; //Strip off the newline

    return result;
}


int camio_istream_fio_end_read(camio_istream_t* this, uint8_t* free_buff){
    camio_istream_fio_t* priv = this->priv;
    priv->read_buff_data_size = 0; //Done with the data
    return 0; //Always true for file I/O
}

int camio_istream_fio_selector_ready(camio_selectable_t* stream){
    camio_istream_t* this = container_of(stream, camio_istream_t,selector);
    return this->ready(this);
}


void camio_istream_fio_delete(camio_istream_t* this){
    this->close(this);
    camio_istream_fio_t* priv = this->priv;
    free(priv);
}

/* ****************************************************
 * Construction
 */

camio_istream_t* camio_istream_fio_construct(camio_istream_fio_t* priv, const camio_descr_t* descr, camio_clock_t* clock, camio_istream_fio_params_t* params, camio_perf_t* perf_mon){
    if(!priv){
        eprintf_exit("fio stream supplied is null\n");
    }
    //Initialize the local variables
    priv->is_closed             = 1;
    priv->max_chunk             = -1;
    priv->read_buff             = NULL;
    priv->read_buff_size        = 0;
    priv->read_buff_data_size   = 0;
    priv->params                = params;

    //Populate the function members
    priv->istream.priv           = priv; //Lets us access private members
    priv->istream.open           = camio_istream_fio_open;
    priv->istream.close          = camio_istream_fio_close;
    priv->istream.start_read     = camio_istream_fio_start_read;
    priv->istream.end_read       = camio_istream_fio_end_read;
    priv->istream.ready          = camio_istream_fio_ready;
    priv->istream.delete         = camio_istream_fio_delete;
    priv->istream.clock          = clock;
    priv->istream.selector.fd    = -1;
    priv->istream.selector.ready = camio_istream_fio_selector_ready;

    //Call open, because its the obvious thing to do now...
    priv->istream.open(&priv->istream, descr, perf_mon);

    //Return the generic istream interface for the outside world to use
    return &priv->istream;

}

camio_istream_t* camio_istream_fio_new( const camio_descr_t* descr, camio_clock_t* clock, camio_istream_fio_params_t* params, camio_perf_t* perf_mon){
    camio_istream_fio_t* priv = malloc(sizeof(camio_istream_fio_t));
    if(!priv){
        eprintf_exit("No memory available for fio istream creation\n");
    }
    return camio_istream_fio_construct(priv, descr, clock, params, perf_mon);
}






