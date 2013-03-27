/*
 * Copyright  (C) Matthew P. Grosvenor, 2012, All Rights Reserved
 *
 * SHMEM Input/Output stream definition
 *
 */

#ifndef CAMIO_IOSTREAM_SHMEM_H_
#define CAMIO_IOSTREAM_SHMEM_H_

#include <netinet/in.h>

#include "camio_iostream.h"

/********************************************************************
 *                  PRIVATE DEFS
 ********************************************************************/

typedef struct {
} camio_iostream_shmem_params_t;


typedef struct {
    char* filename;
    camio_iostream_t iostream;
    volatile uint8_t* shmem;
    size_t shmem_size;
    int is_closed; //Has close be called?
    camio_iostream_shmem_params_t* params;  //Parameters passed in from the outside
    camio_perf_t* perf_mon;
    uint64_t offset;

} camio_iostream_shmem_t;


/********************************************************************
 *                  PUBLIC DEFS
 ********************************************************************/

camio_iostream_t* camio_iostream_shmem_new( const camio_descr_t* descr, camio_clock_t* clock, camio_iostream_shmem_params_t* params, camio_perf_t* perf_mon);


#endif /* CAMIO_IOSTREAM_SHMEM_H_ */
