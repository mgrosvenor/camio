/*
 * camio_bring.h
 *
 *  Created on: Nov 14, 2012
 *      Author: root
 */

#ifndef CAMIO_ISTREAM_BRING_H_
#define CAMIO_ISTREAM_BRING_H_

#include "camio_istream.h"

#define CAMIO_ISTREAM_BRING_BLOCKING    1
#define CAMIO_ISTREAM_BRING_NONBLOCKING 0

/********************************************************************
 *                  PRIVATE DEFS
 ********************************************************************/


typedef struct {
    //No params yet
} camio_istream_bring_params_t;

typedef struct {
    camio_istream_t istream;
    int is_closed;                       //Has close be called?
    volatile uint8_t* bring;              //Pointer to the head of the bring
    size_t bring_size;                    //Size of the bring buffer
    volatile uint8_t* curr;              //Current slot in the bring
    size_t read_size;                    //Size of the current read waiting (if any)
    uint64_t sync_counter;               //Synchronization counter
    uint64_t index;                      //Current index into the buffer
    camio_istream_bring_params_t* params;  //Parameters passed in from the outside
    camio_perf_t* perf_mon;

} camio_istream_bring_t;




/********************************************************************
 *                  PUBLIC DEFS
 ********************************************************************/

camio_istream_t* camio_istream_bring_new( const camio_descr_t* opts, camio_clock_t* clock, camio_istream_bring_params_t* params, camio_perf_t* perf_mon );


#endif /* CAMIO_ISTREAM_BRING_H_ */
