/*
 * Copyright  (C) Matthew P. Grosvenor, 2012, All Rights Reserved
 *
 * Camio NETMAP card input stream
 *
 */


#ifndef CAMIO_ISTREAM_NETMAP_H_
#define CAMIO_ISTREAM_NETMAP_H_

#include "camio_istream.h"



/********************************************************************
 *                  PRIVATE DEFS
 ********************************************************************/

typedef struct {
    void* nm_mem; //Can take a memory argument as a parameter to ensure all instances are working
                  //from the same pointer range. This will ensure that zero-copy optimisations are
                  //possible
    size_t nm_mem_size;
    int fd;
} camio_istream_netmap_params_t;

typedef struct {
    int is_closed;
    void* nm_mem;
    size_t mem_size;
    struct netmap_if *nifp;
    struct netmap_ring  *rx;
    size_t begin;
    size_t end;
    size_t packets_waiting;
    void* packet;
    size_t packet_size;
    void* packet_buff_bottom;
    struct netmap_slot* nm_slot;
    struct netmap_ring *ring;

    camio_istream_t istream;
    camio_istream_netmap_params_t* params;  //Parameters passed in from the outside
    camio_perf_t* perf_mon;                 //Performance measurement

} camio_istream_netmap_t;




/********************************************************************
 *                  PUBLIC DEFS
 ********************************************************************/

camio_istream_t* camio_istream_netmap_new( const camio_descr_t* opts, camio_clock_t* clock, camio_istream_netmap_params_t* params, camio_perf_t* perf_mon);


#endif /* CAMIO_ISTREAM_NETMAP_H_ */
