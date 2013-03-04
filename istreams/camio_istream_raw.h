/*
 * Copyright  (C) Matthew P. Grosvenor, 2012, All Rights Reserved
 *
 * Camio raw socket input stream
 *
 */

#ifndef CAMIO_ISTREAM_RAW_H_
#define CAMIO_ISTREAM_RAW_H_

#include "camio_istream.h"

/********************************************************************
 *                  PRIVATE DEFS
 ********************************************************************/

typedef struct {
    //No params at this stage
} camio_istream_raw_params_t;

typedef struct {
    camio_istream_t istream;
    uint8_t* buffer;
    size_t buffer_size;
    size_t bytes_read;
    int is_closed;                      //Has close be called?
    camio_istream_raw_params_t* params;  //Parameters passed in from the outside
    camio_perf_t* perf_mon;

} camio_istream_raw_t;



/********************************************************************
 *                  PUBLIC DEFS
 ********************************************************************/

camio_istream_t* camio_istream_raw_new( const camio_descr_t* opts, camio_clock_t* clock, camio_istream_raw_params_t* params, camio_perf_t* perf_mon );


#endif /* CAMIO_ISTREAM_RAW_H_ */
