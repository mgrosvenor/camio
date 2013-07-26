/*
 * Copyright  (C) Matthew P. Grosvenor, 2012, All Rights Reserved
 *
 * Camio ring (newline separated) input stream
 *
 */
#include <errno.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <memory.h>

#include "camio_istream_ring.h"
#include "../errors/camio_errors.h"
#include "../utils/camio_util.h"
#include "../stream_description/camio_opt_parser.h"
#include "../utils/camio_ring.h"


int camio_istream_ring_open(camio_istream_t* this, const camio_descr_t* descr, camio_perf_t* perf_mon ){
    camio_istream_ring_t* priv = this->priv;
    int ring_fd = -1;
    volatile uint8_t* ring = NULL;

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

    //Wait until there is a ring file to open.
    while( (ring_fd = open(descr->query, O_RDWR)) < 0 ){ usleep(100 * 1000); }

    ring = mmap( NULL, CAMIO_RING_MEM_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, ring_fd, 0);
    if(unlikely(ring == MAP_FAILED)){
        eprintf_exit( "Could not memory map ring file \"%s\". Error=%s\n", descr->query, strerror(errno));
    }

    //Remove the filename from the filesystem. Since the and reader are both still connected
    //to the file, the space will continue to be available until they both exit.
    if(unlink(descr->query) < 0){
        wprintf("Could not remove ring file \"%s\". Error = \"%s\"", descr->query, strerror(errno));
    }

    priv->ring_size = CAMIO_RING_SLOT_COUNT * CAMIO_RING_SLOT_SIZE;
    this->selector.fd = ring_fd;
    priv->ring = ring;
    priv->curr = ring;
    priv->is_closed = 0;

    ring_connected = 1;
    //printf("CAMIO_RING: Set Ring TO CONNECTED\n");

    return 0;
}


void camio_istream_ring_close(camio_istream_t* this){
    camio_istream_ring_t* priv = this->priv;
    munmap((void*)priv->ring, priv->ring_size);
    close(this->selector.fd);
    priv->is_closed = 1;
}




static int prepare_next(camio_istream_ring_t* priv){

    //Simple case, there's already data waiting
    if(unlikely(priv->read_size)){
        camio_perf_event_start(priv->perf_mon,CAMIO_PERF_EVENT_ISTREAM_RING,CAMIO_PERF_COND_EXISTING_DATA);
        return priv->read_size;
    }

    //Is there new data?
    register uint64_t curr_sync_count = *((volatile uint64_t*)(priv->curr + CAMIO_RING_SLOT_SIZE - sizeof(uint64_t)));
//    if(curr_sync_count != 0){
//        printf("CAMIO_RING: istream[%i]: sync count=%lu priv=%lu\n", priv->istream.selector.fd, curr_sync_count, priv->sync_counter);
//    }
    if( likely(curr_sync_count == priv->sync_counter)){
        const uint64_t data_len  = *((volatile uint64_t*)(priv->curr + CAMIO_RING_SLOT_SIZE - 2* sizeof(uint64_t)));
        priv->read_size = data_len;
        camio_perf_event_start(priv->perf_mon,CAMIO_PERF_EVENT_ISTREAM_RING,CAMIO_PERF_COND_NEW_DATA);
        return data_len;
    }

    if( likely(curr_sync_count > priv->sync_counter)){
        //wprintf( "Ring overflow. Catching up now. Dropping payloads from %lu to %lu\n", priv->sync_counter, curr_sync_count -1);
        priv->sync_counter = curr_sync_count;
        const uint64_t data_len  = *((volatile uint64_t*)(priv->curr + CAMIO_RING_SLOT_SIZE - 2* sizeof(uint64_t)));
        priv->read_size = data_len;
        camio_perf_event_start(priv->perf_mon,CAMIO_PERF_EVENT_ISTREAM_RING,CAMIO_PERF_COND_READ_ERROR);
        return data_len;
    }

    return 0;
}

int camio_istream_ring_ready(camio_istream_t* this){
    camio_istream_ring_t* priv = this->priv;
    if(priv->read_size || priv->is_closed){
        return 1;
    }

    return prepare_next(priv);
}

int camio_istream_ring_start_read(camio_istream_t* this, uint8_t** out){
    camio_istream_ring_t* priv = this->priv;
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


int camio_istream_ring_end_read(camio_istream_t* this, uint8_t* free_buff){
    camio_istream_ring_t* priv = this->priv;

    register uint64_t curr_sync_count = *((volatile uint64_t*)(priv->curr + CAMIO_RING_SLOT_SIZE - sizeof(uint64_t)));
    if( unlikely(curr_sync_count != priv->sync_counter)){
        //wprintf(CAMIO_ERR_BUFFER_OVERRUN, "Detected overrun in ring buffer sync count is now=%lu, expected sync count=%lu\n", curr_sync_count, priv->sync_counter);
        priv->sync_counter = curr_sync_count;
        priv->read_size = 0;
        return -1;
    }

    priv->read_size = 0;
    priv->sync_counter++;
    priv->index = (priv->index + 1) % (CAMIO_RING_SLOT_COUNT);
    priv->curr  = priv->ring + (priv->index * CAMIO_RING_SLOT_SIZE);


    return 0;
}


int camio_istream_ring_selector_ready(camio_selectable_t* stream){
    camio_istream_t* this = container_of(stream, camio_istream_t,selector);
    return this->ready(this);
}


void camio_istream_ring_delete(camio_istream_t* this){
    this->close(this);
    camio_istream_ring_t* priv = this->priv;
    free(priv);
}

/* ****************************************************
 * Construction
 */

camio_istream_t* camio_istream_ring_construct(camio_istream_ring_t* priv, const camio_descr_t* descr, camio_clock_t* clock, camio_istream_ring_params_t* params, camio_perf_t* perf_mon ){
    if(!priv){
        eprintf_exit("ring stream supplied is null\n");
    }

    //Initialize the local variables
    priv->is_closed         = 1;
    priv->ring              = NULL;
    priv->ring_size         = 0;
    priv->curr              = NULL;
    priv->read_size         = 0;
    priv->sync_counter      = 1; //We will expect 1 when the first write occurs
    priv->index             = 0;
    priv->params            = params;

    //Populate the function members
    priv->istream.priv           = priv; //Lets us access private members
    priv->istream.open           = camio_istream_ring_open;
    priv->istream.close          = camio_istream_ring_close;
    priv->istream.start_read     = camio_istream_ring_start_read;
    priv->istream.end_read       = camio_istream_ring_end_read;
    priv->istream.ready          = camio_istream_ring_ready;
    priv->istream.delete         = camio_istream_ring_delete;
    priv->istream.clock          = clock;
    priv->istream.selector.fd    = -1;
    priv->istream.selector.ready = camio_istream_ring_selector_ready;

    //Call open, because its the obvious thing to do now...
    priv->istream.open(&priv->istream, descr, perf_mon);

    //Return the generic istream interface for the outside world to use
    return &priv->istream;

}

camio_istream_t* camio_istream_ring_new( const camio_descr_t* descr, camio_clock_t* clock, camio_istream_ring_params_t* params, camio_perf_t* perf_mon ){
    camio_istream_ring_t* priv = malloc(sizeof(camio_istream_ring_t));
    if(!priv){
        eprintf_exit("No memory available for ring istream creation\n");
    }
    return camio_istream_ring_construct(priv, descr, clock, params, perf_mon );
}





