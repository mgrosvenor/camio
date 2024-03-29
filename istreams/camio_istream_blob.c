/*
 * Copyright  (C) Matthew P. Grosvenor, 2012, All Rights Reserved
 *
 * Camio blob (newline separated) input stream
 *
 */
#include <errno.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <memory.h>
#include <sys/stat.h>

#include "camio_istream_blob.h"
#include "../errors/camio_errors.h"
#include "../utils/camio_util.h"
#include "../stream_description/camio_opt_parser.h"


int camio_istream_blob_open(camio_istream_t* this, const camio_descr_t* descr, camio_perf_t* perf_mon ){
    camio_istream_blob_t* priv = this->priv;

    if(unlikely(perf_mon == NULL)){
        eprintf_exit("No performance monitor supplied\n");
    }
    priv->perf_mon = perf_mon;

    if(unlikely(camio_descr_has_opts(descr->opt_head))){
        eprintf_exit( "Option(s) supplied, but none expected\n");
    }

    if(unlikely(!descr->query)){
        eprintf_exit("No filename supplied\n");
    }

    this->selector.fd = open(descr->query, O_RDONLY);
    if(unlikely(this->selector.fd < 0)){
        eprintf_exit("Could not open file \"%s\". Error=%s\n", descr->query, strerror(errno));
    }

    //Get the file size
    struct stat st;
    stat(descr->query, &st);
    priv->blob_size = st.st_size;

    if(priv->blob_size)
    {
        //Map the whole thing into memory
        priv->blob = mmap( NULL, priv->blob_size, PROT_READ, MAP_SHARED, this->selector.fd, 0);
        if(unlikely(priv->blob == MAP_FAILED)){
            eprintf_exit("Could not memory map blob file \"%s\". Error=%s\n", descr->query, strerror(errno));
        }

        priv->is_closed = 0;
    }

    return 0;
}


void camio_istream_blob_close(camio_istream_t* this){
    camio_istream_blob_t* priv = this->priv;
    munmap((void*)priv->blob, priv->blob_size);
    close(this->selector.fd);
    priv->is_closed = 1;
}

static int prepare_next(camio_istream_blob_t* priv){
    if(priv->offset == priv->blob_size || priv->is_closed){
        priv->read_size = 0;
        camio_perf_event_start(priv->perf_mon,CAMIO_PERF_EVENT_ISTREAM_BLOB,CAMIO_PERF_COND_NO_DATA);
        return 0; //Nothing more to read
    }

    //There is data, it is unread
    priv->read_size = priv->blob_size;
    camio_perf_event_start(priv->perf_mon,CAMIO_PERF_EVENT_ISTREAM_BLOB,CAMIO_PERF_COND_NEW_DATA);

    return priv->read_size;
}

int camio_istream_blob_ready(camio_istream_t* this){
    camio_istream_blob_t* priv = this->priv;

    //This cannot fail
    prepare_next(priv);
    return 1;
}

int camio_istream_blob_start_read(camio_istream_t* this, uint8_t** out){
    *out = NULL;

    camio_istream_blob_t* priv = this->priv;
    if(unlikely(priv->is_closed)){
        return 0;
    }

    prepare_next(priv);

    if(unlikely(!priv->read_size)){
        *out = NULL;
        return 0;
    }

    *out = priv->blob;
    priv->offset = priv->blob_size;

    return priv->blob_size;
}


int camio_istream_blob_end_read(camio_istream_t* this, uint8_t* free_buff){
    return 0; //Always true for memory I/O
}


int camio_istream_blob_selector_ready(camio_selectable_t* stream){
    camio_istream_t* this = container_of(stream, camio_istream_t,selector);
    return this->ready(this);
}


void camio_istream_blob_delete(camio_istream_t* this){
    this->close(this);
    camio_istream_blob_t* priv = this->priv;
    free(priv);
}

/* ****************************************************
 * Construction
 */

camio_istream_t* camio_istream_blob_construct(camio_istream_blob_t* priv, const camio_descr_t* descr, camio_clock_t* clock, camio_istream_blob_params_t* params, camio_perf_t* perf_mon){
    if(!priv){
        eprintf_exit("Blob stream supplied is null\n");
    }

    //Initialize the local variables
    priv->is_closed         = 1;
    priv->blob              = NULL;
    priv->blob_size         = 0;
    priv->read_size         = 0;
    priv->offset            = 0;
    priv->params            = params;

    //Populate the function members
    priv->istream.priv           = priv; //Lets us access private members
    priv->istream.open           = camio_istream_blob_open;
    priv->istream.close          = camio_istream_blob_close;
    priv->istream.start_read     = camio_istream_blob_start_read;
    priv->istream.end_read       = camio_istream_blob_end_read;
    priv->istream.ready          = camio_istream_blob_ready;
    priv->istream.delete         = camio_istream_blob_delete;
    priv->istream.clock          = clock;
    priv->istream.selector.fd    = -1;
    priv->istream.selector.ready = camio_istream_blob_selector_ready;

    //Call open, because its the obvious thing to do now...
    priv->istream.open(&priv->istream, descr, perf_mon);

    //Return the generic istream interface for the outside world to use
    return &priv->istream;

}

camio_istream_t* camio_istream_blob_new( const camio_descr_t* descr, camio_clock_t* clock, camio_istream_blob_params_t* params, camio_perf_t* perf_mon){
    camio_istream_blob_t* priv = malloc(sizeof(camio_istream_blob_t));
    if(!priv){
        eprintf_exit("No memory available for blob istream creation\n");
    }
    return camio_istream_blob_construct(priv, descr, clock, params, perf_mon);
}





