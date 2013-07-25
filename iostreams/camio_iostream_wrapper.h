/*
 * Copyright  (C) Matthew P. Grosvenor, 2012, All Rights Reserved
 *
 * WRAPPER Input/Output stream definition
 *
 */

#ifndef CAMIO_IOSTREAM_WRAPPER_H_
#define CAMIO_IOSTREAM_WRAPPER_H_

#include "camio_iostream.h"
#include "../istreams/camio_istream.h"
#include "../ostreams/camio_ostream.h"

/********************************************************************
 *                  PRIVATE DEFS
 ********************************************************************/

typedef struct {
    //Nothing here
} camio_iostream_wrapper_params_t;


typedef struct {
    camio_iostream_t iostream;

    camio_istream_t* base_istream;
    char* istream_descr;
    void* istream_params;

    camio_ostream_t* base_ostream;
    char* ostream_descr;
    void* ostream_params;

    camio_iostream_wrapper_params_t* params;  //Parameters passed in from the outside
    camio_perf_t* perf_mon;
} camio_iostream_wrapper_t;



/********************************************************************
 *                  PUBLIC DEFS
 ********************************************************************/

camio_iostream_t* camio_iostream_wrapper_new(
        char* istream_descr,
        char* ostream_descr,
        void* istream_params,
        void* ostream_params,
        camio_iostream_wrapper_params_t*
        params, camio_perf_t* perf_mon);


#endif /* CAMIO_IOSTREAM_WRAPPER_H_ */

