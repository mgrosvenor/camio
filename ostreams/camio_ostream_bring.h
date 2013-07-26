/*
 * camio_ostream_bring.h
 *
 *  Created on: Nov 15, 2012
 *      Author: root
 */

#ifndef CAMIO_OSTREAM_BRING_H_
#define CAMIO_OSTREAM_BRING_H_

#include "camio_ostream.h"

/********************************************************************
 *                  PRIVATE DEFS
 ********************************************************************/


typedef struct {
    uint64_t slot_size;
    uint64_t slot_count;
} camio_ostream_bring_params_t;

typedef struct {
    camio_ostream_t ostream;
    char* filename;                         //Keep the file name so we can delete it
    int is_closed;              			//Has close be called?
    volatile uint8_t* bring;				//Pointer to the head of the bring
    size_t bring_size;                      //Size of the bring buffer
    volatile uint8_t* curr;                 //Current slot in the bring
    uint8_t* assigned_buffer;               //Assigned write buffer
    size_t assigned_buffer_sz;              //Assigned write buffer size
    uint64_t sync_count;                    //Synchronization counter
    uint64_t index;                         //Current slot in the bring
    uint64_t slot_size;                     //Size of each slot in the ring
    uint64_t slot_count;                    //Number of slots in the ring
    camio_ostream_bring_params_t* params;   //Parameters from the outside world
    camio_perf_t* perf_mon;

} camio_ostream_bring_t;



/********************************************************************
 *                  PUBLIC DEFS
 ********************************************************************/

camio_ostream_t* camio_ostream_bring_new( const camio_descr_t* opts, camio_clock_t* clock, camio_ostream_bring_params_t* params, camio_perf_t* perf_mon);



#endif /* CAMIO_OSTREAM_BRING_H_ */
