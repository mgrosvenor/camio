/*
 * camio_ostream_raw.h
 *
 *  Created on: Nov 15, 2012
 *      Author: root
 */

#ifndef CAMIO_OSTREAM_RAW_H_
#define CAMIO_OSTREAM_RAW_H_

#include "camio_ostream.h"

/********************************************************************
 *                  PRIVATE DEFS
 ********************************************************************/


typedef struct {
    //No params at this stage
} camio_ostream_raw_params_t;

typedef struct {
    camio_ostream_t ostream;
    int is_closed;                          //Has close be called?
    uint8_t* buffer;                        //Space to build output
    size_t buffer_size;                     //Size of output buffer
    uint8_t* assigned_buffer;               //Assigned write buffer
    size_t assigned_buffer_sz;              //Assigned write buffer size
    camio_ostream_raw_params_t* params;      //Parameters from the outside world
    camio_perf_t* perf_mon;

} camio_ostream_raw_t;



/********************************************************************
 *                  PUBLIC DEFS
 ********************************************************************/

camio_ostream_t* camio_ostream_raw_new( const camio_descr_t* opts, camio_clock_t* clock, camio_ostream_raw_params_t* params, camio_perf_t* perf_mon);



#endif /* CAMIO_OSTREAM_RAW_H_ */
