/*
 * Copyright  (C) Matthew P. Grosvenor, 2012, All Rights Reserved
 *
 * Camio bring (newline separated) input stream
 *
 */
#include <errno.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <memory.h>

#include "camio_istream_bring.h"
#include "../errors/camio_errors.h"
#include "../utils/camio_util.h"
#include "../stream_description/camio_opt_parser.h"
#include "../utils/camio_bring.h"





static int prepare_next(camio_istream_bring_t* priv){

    //Simple case, there's already data waiting
    if(unlikely(priv->read_size)){
        camio_perf_event_start(priv->perf_mon,CAMIO_PERF_EVENT_ISTREAM_BRING,CAMIO_PERF_COND_EXISTING_DATA);
        return priv->read_size;
    }

    //Is there new data?
    register uint64_t curr_sync_count = *((volatile uint64_t*)(priv->curr + priv->slot_size - sizeof(uint64_t)));
    if( likely(curr_sync_count == priv->sync_counter)){
        const uint64_t data_len  = *((volatile uint64_t*)(priv->curr + priv->slot_size - 2* sizeof(uint64_t)));
        priv->read_size = data_len;
        camio_perf_event_start(priv->perf_mon,CAMIO_PERF_EVENT_ISTREAM_BRING,CAMIO_PERF_COND_NEW_DATA);
        return data_len;
    }

    if( likely(curr_sync_count > priv->sync_counter)){
        eprintf_exit( "Ring overflow. This should not happen with a blocking ring %lu to %lu\n", priv->sync_counter, curr_sync_count -1);
    }

    return 0;
}

static int camio_istream_bring_ready(camio_istream_t* this){
    camio_istream_bring_t* priv = this->priv;
    if(priv->read_size || priv->is_closed){
        return 1;
    }

    return prepare_next(priv);
}

static int camio_istream_bring_start_read(camio_istream_t* this, uint8_t** out){
    camio_istream_bring_t* priv = this->priv;
    *out = NULL;

    if(unlikely(priv->is_closed)){
        return 0;
    }

    //Called read without calling ready, they must want to block/spin waiting for data
    if(unlikely(!priv->read_size)){
        while(!prepare_next(priv)){
            asm("pause"); //Tell the CPU we're spinning
        }
    }

    *out = (uint8_t*)priv->curr;
    size_t result = priv->read_size;
    return result;
}


static int camio_istream_bring_end_read(camio_istream_t* this, uint8_t* free_buff){
    camio_istream_bring_t* priv = this->priv;

    //Free this slot
    *((volatile uint64_t*)(priv->curr + priv->slot_size - sizeof(uint64_t))) = 0x00ULL;

    priv->read_size = 0;
    priv->sync_counter++;
    priv->index = (priv->index + 1) % (priv->slot_count);
    priv->curr  = priv->bring + (priv->index * priv->slot_size);


    return 0;
}


static int camio_istream_bring_selector_ready(camio_selectable_t* stream){
    camio_istream_t* this = container_of(stream, camio_istream_t,selector);
    return this->ready(this);
}


static int camio_istream_bring_open(camio_istream_t* this, const camio_descr_t* descr, camio_perf_t* perf_mon ){
    camio_istream_bring_t* priv = this->priv;
    int bring_fd = -1;
    volatile uint8_t* bring = NULL;

    if(unlikely(perf_mon == NULL)){
        eprintf_exit("No performance monitor supplied\n");
    }
    priv->perf_mon = perf_mon;

    if(unlikely(camio_descr_has_opts(descr->opt_head))){
        eprintf_exit( "Option(s) supplied, but none expected\n");
    }

    if(unlikely(!descr->query)){
        eprintf_exit( "No filename supplied\n");
    }


    if(priv->params){
        priv->slot_size  = priv->params->slot_size;
        priv->slot_count = priv->params->slot_count;
    }
    else{
        priv->slot_size  = CAMIO_BRING_SLOT_SIZE_DEFAULT;
        priv->slot_count = CAMIO_BRING_SLOT_COUNT_DEFAULT;
    }


    //Wait until there is a bring file to open.
    while( (bring_fd = open(descr->query, O_RDWR)) < 0 ){ usleep(1000); }

    bring = mmap( NULL, CAMIO_BRING_MEM_SIZE(priv->slot_count,priv->slot_size), PROT_READ | PROT_WRITE, MAP_SHARED, bring_fd, 0);
    if(unlikely(bring == MAP_FAILED)){
        eprintf_exit( "Could not memory map bring file \"%s\". Error=%s\n", descr->query, strerror(errno));
    }

    //Remove the filename from the filesystem. Since the and reader are both still connected
    //to the file, the space will continue to be available until they both exit.
    if(unlink(descr->query) < 0){
        wprintf("Could not remove bring file \"%s\". Error = \"%s\"", descr->query, strerror(errno));
    }

    priv->bring_size = priv->slot_count * priv->slot_size;
    this->selector.fd = bring_fd;
    priv->bring = bring;
    priv->curr = bring;
    priv->is_closed = 0;

    bring_connected = 1;

    return 0;
}


static void camio_istream_bring_close(camio_istream_t* this){
    camio_istream_bring_t* priv = this->priv;
    munmap((void*)priv->bring, priv->bring_size);
    close(this->selector.fd);
    priv->is_closed = 1;
}

static void camio_istream_bring_delete(camio_istream_t* this){
    this->close(this);
    camio_istream_bring_t* priv = this->priv;
    free(priv);
}




/* ****************************************************
 * Construction
 */

static camio_istream_t* camio_istream_bring_construct(camio_istream_bring_t* priv, const camio_descr_t* descr, camio_clock_t* clock, camio_istream_bring_params_t* params, camio_perf_t* perf_mon ){
    if(!priv){
        eprintf_exit("bring stream supplied is null\n");
    }

    //Initialize the local variables
    priv->is_closed         = 1;
    priv->bring              = NULL;
    priv->bring_size         = 0;
    priv->curr              = NULL;
    priv->read_size         = 0;
    priv->sync_counter      = 1; //We will expect 1 when the first write occurs
    priv->index             = 0;
    priv->slot_count        = 0;
    priv->slot_size         = 0;
    priv->params            = params;


    //Populate the function members
    priv->istream.priv           = priv; //Lets us access private members
    priv->istream.open           = camio_istream_bring_open;
    priv->istream.close          = camio_istream_bring_close;
    priv->istream.start_read     = camio_istream_bring_start_read;
    priv->istream.end_read       = camio_istream_bring_end_read;
    priv->istream.ready          = camio_istream_bring_ready;
    priv->istream.delete         = camio_istream_bring_delete;
    priv->istream.clock          = clock;
    priv->istream.selector.fd    = -1;
    priv->istream.selector.ready = camio_istream_bring_selector_ready;

    //Call open, because its the obvious thing to do now...
    priv->istream.open(&priv->istream, descr, perf_mon);

    //Return the generic istream interface for the outside world to use
    return &priv->istream;

}

camio_istream_t* camio_istream_bring_new( const camio_descr_t* descr, camio_clock_t* clock, camio_istream_bring_params_t* params, camio_perf_t* perf_mon ){
    camio_istream_bring_t* priv = malloc(sizeof(camio_istream_bring_t));
    if(!priv){
        eprintf_exit("No memory available for bring istream creation\n");
    }
    return camio_istream_bring_construct(priv, descr, clock, params, perf_mon );
}





