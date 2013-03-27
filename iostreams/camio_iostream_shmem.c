/*
 * Copyright  (C) Matthew P. Grosvenor, 2012, All Rights Reserved
 *
 * SHMEM Input/Output stream definition
 *
 */

#include <errno.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <memory.h>

#include "camio_iostream_shmem.h"
#include "../errors/camio_errors.h"
#include "../stream_description/camio_opt_parser.h"
#include "../utils/camio_util.h"

#define CAMIO_SHMEM_MEM_SIZE (64 * 1024 * 1204) //64MB


int camio_iostream_shmem_open(camio_iostream_t* this, const camio_descr_t* descr, camio_perf_t* perf_mon ){
    camio_iostream_shmem_t* priv = this->priv;
    int shmem_fd = -1;
    volatile uint8_t* shmem = NULL;


    if(unlikely(perf_mon == NULL)){
        eprintf_exit("No performance monitor supplied\n");
    }
    priv->perf_mon = perf_mon;
    //Make a local copy of the filename in case the descr pointer goes away (probable)
    size_t filename_len = strlen(descr->query);
    priv->filename = malloc(filename_len + 1);
    memcpy(priv->filename,descr->query, filename_len);
    priv->filename[filename_len] = '\0'; //Make sure it's null terminated


    //Try to create the new shmem file
    shmem_fd = open(descr->query, O_RDWR | O_CREAT | O_TRUNC , (mode_t)(0666));
    if(shmem_fd >= 0){

        //Resize the file
        if(lseek(shmem_fd, CAMIO_SHMEM_MEM_SIZE -1, SEEK_SET) < 0){
            eprintf_exit( "Could not resize file for shared region \"%s\". Error=%s\n", descr->query, strerror(errno));
        }

        if(write(shmem_fd, "", 1) < 0){
            eprintf_exit( "Could not resize file for shared region \"%s\". Error=%s\n", descr->query, strerror(errno));
        }

        shmem = mmap( NULL, CAMIO_SHMEM_MEM_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, shmem_fd, 0);
        if(unlikely(shmem == MAP_FAILED)){
            eprintf_exit("Could not memory map shmem file \"%s\". Error=%s\n", descr->query, strerror(errno));
        }

        //Initialize the shmem with 0
        memset((uint8_t*)shmem, 0, CAMIO_SHMEM_MEM_SIZE);
    }
    //If a shmem file already exists, the other side has created it, so open it and mmap
    else{
        shmem_fd = open(descr->query, O_RDWR);
        if(unlikely(shmem_fd < 0)){
            eprintf_exit("Could not open shmem file. Giving up\n");
        }

        shmem = mmap( NULL, CAMIO_SHMEM_MEM_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, shmem_fd, 0);
        if(unlikely(shmem == MAP_FAILED)){
            eprintf_exit("Could not memory map shmem file \"%s\". Error=%s\n", descr->query, strerror(errno));
        }
    }

    priv->shmem_size    = CAMIO_SHMEM_MEM_SIZE;
    this->selector.fd   = shmem_fd;
    priv->shmem         = shmem;
    priv->is_closed     = 0;
    return 0;

}


void camio_iostream_shmem_close(camio_iostream_t* this){
    camio_iostream_shmem_t* priv = this->priv;
    priv->shmem_size = 0;
    munmap((void*)priv->shmem, CAMIO_SHMEM_MEM_SIZE);
    close(this->selector.fd);
}


static int prepare_next(camio_iostream_shmem_t* priv, int blocking){
    if(priv->offset == priv->shmem_size || priv->is_closed){
        camio_perf_event_start(priv->perf_mon,CAMIO_PERF_EVENT_ISTREAM_BLOB,CAMIO_PERF_COND_NO_DATA);
        return 0; //Nothing more to read
    }

    //There is data, it is unread
    priv->offset = priv->shmem_size;
    camio_perf_event_start(priv->perf_mon,CAMIO_PERF_EVENT_ISTREAM_BLOB,CAMIO_PERF_COND_NEW_DATA);

    return priv->offset;

}

int camio_iostream_shmem_rready(camio_iostream_t* this){
    camio_iostream_shmem_t* priv = this->priv;
    if(priv->offset || priv->is_closed){
        return 1;
    }

    return prepare_next(priv,0);
}


static int camio_iostream_shmem_start_read(camio_iostream_t* this, uint8_t** out){
    *out = NULL;

    camio_iostream_shmem_t* priv = this->priv;
    if(priv->is_closed){
        camio_perf_event_start(priv->perf_mon, CAMIO_PERF_EVENT_IOSTREAM_SHMEM, CAMIO_PERF_COND_NO_DATA);
        return 0;
    }

    //Called read without calling ready, they must want to block
    prepare_next(priv,1);

    *out = (void*)priv->shmem;
    size_t result = priv->offset;
    return  result;
}


int camio_iostream_shmem_end_read(camio_iostream_t* this, uint8_t* free_buff){
    //Move along. Nothing to see here
    return 0;
}


int camio_iostream_shmem_selector_ready(camio_selectable_t* stream){
    camio_iostream_t* this = container_of(stream, camio_iostream_t,selector);
    return this->rready(this);
}


void camio_iostream_shmem_delete(camio_iostream_t* this){
    this->close(this);
    camio_iostream_shmem_t* priv = this->priv;
    free(priv);
}



//Returns a pointer to a space of size len, ready for data
uint8_t* camio_iostream_shmem_start_write(camio_iostream_t* this, size_t len ){
    camio_iostream_shmem_t* priv = this->priv;

    if(len > CAMIO_SHMEM_MEM_SIZE){ \
        eprintf_exit("Length supplied (%lu) is greater than memory size (%lu, corruption is likely if you continue.\n", len, CAMIO_SHMEM_MEM_SIZE ); \
    }

    //Grow the shmem if it's not big enough
    return (void*)priv->shmem;

}

//Returns non-zero if a call to start_write will be non-blocking
int camio_iostream_shmem_wready(camio_iostream_t* this){
    //Not implemented
    eprintf_exit("Not implemented\n");
    return 0;
}


//Commit the data to the shmem previously allocated
//Len must be equal to or less than len called with start_write
uint8_t* camio_iostream_shmem_end_write(camio_iostream_t* this, size_t len){
    return NULL;
}

//Is this stream capable of taking over another stream shmem
int camio_iostream_shmem_can_assign_write(camio_iostream_t* this){
    return 0;
}

//Assign the write shmem to the stream
int camio_iostream_shmem_assign_write(camio_iostream_t* this, uint8_t* buff, size_t len){
    return -1;
}



/* ****************************************************
 * Construction
 */

camio_iostream_t* camio_iostream_shmem_construct(camio_iostream_shmem_t* priv, const camio_descr_t* descr, camio_clock_t* clock, camio_iostream_shmem_params_t* params, camio_perf_t* perf_mon){
    if(!priv){
        eprintf_exit("shmem stream supplied is null\n");
    }
    //Initialize the local variables
    priv->is_closed     = 1;
    priv->shmem         = NULL;
    priv->shmem_size    = 0;
    priv->filename      = NULL;
    priv->params        = params;
    priv->offset        = 0;


    //Populate the function members
    priv->iostream.priv             = priv; //Lets us access private members
    priv->iostream.open             = camio_iostream_shmem_open;
    priv->iostream.close            = camio_iostream_shmem_close;
    priv->iostream.delete           = camio_iostream_shmem_delete;
    priv->iostream.start_read       = camio_iostream_shmem_start_read;
    priv->iostream.end_read         = camio_iostream_shmem_end_read;
    priv->iostream.rready           = camio_iostream_shmem_rready;
    priv->iostream.selector.ready   = camio_iostream_shmem_selector_ready;

    priv->iostream.start_write      = camio_iostream_shmem_start_write;
    priv->iostream.end_write        = camio_iostream_shmem_end_write;
    priv->iostream.can_assign_write = camio_iostream_shmem_can_assign_write;
    priv->iostream.assign_write     = camio_iostream_shmem_assign_write;
    priv->iostream.wready           = camio_iostream_shmem_wready;

    priv->iostream.clock            = clock;
    priv->iostream.selector.fd      = -1;


    //Call open, because its the obvious thing to do now...
    priv->iostream.open(&priv->iostream, descr, perf_mon);

    //Return the generic istream interface for the outside world to use
    return &priv->iostream;

}

camio_iostream_t* camio_iostream_shmem_new( const camio_descr_t* descr, camio_clock_t* clock, camio_iostream_shmem_params_t* params, camio_perf_t* perf_mon){
    camio_iostream_shmem_t* priv = malloc(sizeof(camio_iostream_shmem_t));
    if(!priv){
        eprintf_exit("No memory available for shmem istream creation\n");
    }
    return camio_iostream_shmem_construct(priv, descr, clock, params, perf_mon);
}

