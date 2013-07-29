/*
 * Copyright  (C) Matthew P. Grosvenor, 2012, All Rights Reserved
 *
 * Camio ring (newline separated) output stream
 *
 */
#include <errno.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <memory.h>

#include "../utils/camio_util.h"
#include "../errors/camio_errors.h"
#include "../stream_description/camio_opt_parser.h"
#include "../utils/camio_ring.h"

#include "camio_ostream_ring.h"


static int camio_ostream_ring_open(camio_ostream_t* this, const camio_descr_t* descr, camio_perf_t* perf_mon ){
    camio_ostream_ring_t* priv = this->priv;
    int ring_fd = -1;
    volatile uint8_t* ring = NULL;

    if(unlikely(perf_mon == NULL)){
        eprintf_exit("No performance monitor supplied\n");
    }
    priv->perf_mon = perf_mon;


    if(unlikely(camio_descr_has_opts(descr->opt_head))){
        eprintf_exit( "Option(s) supplied, but none expected\n");
    }

    if(!descr->query){
        eprintf_exit( "No filename supplied\n");
    }

    //Make a local copy of the filename in case the descr pointer goes away (probable)
    size_t filename_len = strlen(descr->query);
    priv->filename = malloc(filename_len + 1);
    memcpy(priv->filename,descr->query, filename_len);
    priv->filename[filename_len] = '\0'; //Make sure it's null terminated


    //See if a ring file already exists, if so, get rid of it.
    ring_fd = open(descr->query, O_RDONLY);
    if(unlikely(ring_fd > 0)){
        wprintf("Found stale ring file. Trying to remove it.\n");
        close(ring_fd);
        if( unlink(descr->query) < 0){
            eprintf_exit("Could remove stale ring file \"%s\". Error=%s\n", descr->query, strerror(errno));
        }

    }

    ring_fd = open(descr->query, O_RDWR | O_CREAT | O_TRUNC , (mode_t)(0666));
    if(unlikely(ring_fd < 0)){
        eprintf_exit("Could not open file \"%s\". Error=%s\n", descr->query, strerror(errno));
    }

    //Resize the file
    if(lseek(ring_fd, CAMIO_RING_MEM_SIZE -1, SEEK_SET) < 0){
        eprintf_exit( "Could not resize file for shared region \"%s\". Error=%s\n", descr->query, strerror(errno));
    }

    if(write(ring_fd, "", 1) < 0){
        eprintf_exit( "Could not resize file for shared region \"%s\". Error=%s\n", descr->query, strerror(errno));
    }

    ring = mmap( NULL, CAMIO_RING_MEM_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, ring_fd, 0);
    if(unlikely(ring == MAP_FAILED)){
        eprintf_exit("Could not memory map ring file \"%s\". Error=%s\n", descr->query, strerror(errno));
    }

    //Initialize the ring with 0
    memset((uint8_t*)ring, 0, CAMIO_RING_MEM_SIZE);

    priv->ring_size = CAMIO_RING_MEM_SIZE;
    this->fd = ring_fd;
    priv->ring = ring;
    priv->curr = ring;

    ring_ostream_created = 1; //Tell any istreams that are listening that we are all initiliased.
    priv->is_closed = 0;


    return 0;
}

static void camio_ostream_ring_close(camio_ostream_t* this){
    camio_ostream_ring_t* priv = this->priv;
    munmap((void*)priv->ring, priv->ring_size);
    close(this->fd);
    unlink(priv->filename); //Delete the file so reader can't get confused
    priv->is_closed = 1;
}


//Returns a pointer to a space of size len, ready for data
//Returns NULL if this is impossible
static uint8_t* camio_ostream_ring_start_write(camio_ostream_t* this, size_t len ){
    camio_ostream_ring_t* priv = this->priv;
    CHECK_LEN_OK(len);

    if(unlikely(!ring_istream_connected)){
        //printf("Waiting for istream to connect...\n");
        while(!ring_istream_connected){
            asm("PAUSE"); //Wait for an istream to connect before you send anything
        }
    }

    return (uint8_t*)priv->curr;
}

//Returns non-zero if a call to start_write will be non-blocking
static int camio_ostream_ring_ready(camio_ostream_t* this){
    //Not implemented
    eprintf_exit( "\n");
    return 0;
}


//Commit the data to the buffer previously allocated
//Len must be equal to or less than len called with start_write
static uint8_t* camio_ostream_ring_end_write(camio_ostream_t* this, size_t len){
    camio_ostream_ring_t* priv = this->priv;
    CHECK_LEN_OK(len);

    camio_perf_event_stop(priv->perf_mon, CAMIO_PERF_EVENT_OSTREAM_RING, CAMIO_PERF_COND_WRITE);

    //Memory copy is done implicitly here
    if(priv->assigned_buffer){

        memcpy((uint8_t*)priv->curr,priv->assigned_buffer,len);
        priv->assigned_buffer    = NULL;
        priv->assigned_buffer_sz = 0;
    }


    priv->sync_count++;
    *(volatile uint64_t*)(priv->curr + CAMIO_RING_SLOT_SIZE-2*sizeof(uint64_t)) = len;
    *(volatile uint64_t*)(priv->curr + CAMIO_RING_SLOT_SIZE-1*sizeof(uint64_t)) = priv->sync_count; //Write is now committed
    //printf("CAMIO_RING: Sync count = %lu\n", priv->sync_count);

    priv->index = (priv->index + 1) % ( CAMIO_RING_SLOT_COUNT);
    priv->curr  = priv->ring + (priv->index * CAMIO_RING_SLOT_SIZE);

    return NULL;
}


static void camio_ostream_ring_delete(camio_ostream_t* ostream){
    ostream->close(ostream);
    camio_ostream_ring_t* priv = ostream->priv;
    free(priv);
}

//Is this stream capable of taking over another stream buffer
static int camio_ostream_ring_can_assign_write(camio_ostream_t* this){
    return 1;
}

//Assign the write buffer to the stream
static int camio_ostream_ring_assign_write(camio_ostream_t* this, uint8_t* buffer, size_t len){
    camio_ostream_ring_t* priv = this->priv;

    if(!buffer){
        eprintf_exit("Assigned buffer is null.");
    }

    CHECK_LEN_OK(len);


    if(unlikely(!ring_istream_connected)){
        //printf("Waiting for istream to connect...\n");
        while(!ring_istream_connected){
            asm("PAUSE"); //Wait for an istream to connect before you send anything
        }
    }

    priv->assigned_buffer    = buffer;
    priv->assigned_buffer_sz = len;

    return 0;
}


/* ****************************************************
 * Construction heavy lifting
 */

static camio_ostream_t* camio_ostream_ring_construct(camio_ostream_ring_t* priv, const camio_descr_t* descr, camio_clock_t* clock, camio_ostream_ring_params_t* params, camio_perf_t* perf_mon){
    if(!priv){
        eprintf_exit("ring stream supplied is null\n");
    }
    //Initialize the local variables
    priv->is_closed             = 1;
    priv->ring                  = NULL;
    priv->ring_size             = 0;
    priv->curr                  = NULL;
    priv->sync_count            = 0;
    priv->index                 = 0;
    priv->assigned_buffer       = NULL;
    priv->assigned_buffer_sz    = 0;
    priv->params                = params;


    //Populate the function members
    priv->ostream.priv              = priv; //Lets us access private members from public functions
    priv->ostream.open              = camio_ostream_ring_open;
    priv->ostream.close             = camio_ostream_ring_close;
    priv->ostream.start_write       = camio_ostream_ring_start_write;
    priv->ostream.end_write         = camio_ostream_ring_end_write;
    priv->ostream.ready             = camio_ostream_ring_ready;
    priv->ostream.delete            = camio_ostream_ring_delete;
    priv->ostream.can_assign_write  = camio_ostream_ring_can_assign_write;
    priv->ostream.assign_write      = camio_ostream_ring_assign_write;
    priv->ostream.clock             = clock;
    priv->ostream.fd                = -1;

    //Call open, because its the obvious thing to do now...
    priv->ostream.open(&priv->ostream, descr, perf_mon);

    //Return the generic ostream interface for the outside world
    return &priv->ostream;

}

camio_ostream_t* camio_ostream_ring_new( const camio_descr_t* descr, camio_clock_t* clock, camio_ostream_ring_params_t* params, camio_perf_t* perf_mon){
    camio_ostream_ring_t* priv = malloc(sizeof(camio_ostream_ring_t));
    if(!priv){
        eprintf_exit("No memory available for ostream ring creation\n");
    }
    return camio_ostream_ring_construct(priv, descr, clock, params, perf_mon);
}



