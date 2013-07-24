/*
 * Copyright  (C) Matthew P. Grosvenor, 2012, All Rights Reserved
 *
 * Camio spin socket input stream
 *
 */

#ifndef CAMIO_SELECTOR_SPIN_H_
#define CAMIO_SELECTOR_SPIN_H_

#include "camio_selector.h"
#include "../istreams/camio_istream.h"

/********************************************************************
 *                  PRIVATE DEFS
 ********************************************************************/

typedef struct {
    //No params at this stage
} camio_selector_spin_params_t;


typedef struct {
    camio_selectable_t* stream;
    size_t index;
} camio_selector_spin_stream_t;

#define CAMIO_SELECTOR_SPIN_MAX_STREAMS 4096

typedef struct {
    camio_selector_t selector;                         //Underlying selector interface
    camio_selector_spin_params_t* params;              //Parameters passed in from the outside
    camio_selector_spin_stream_t streams[CAMIO_SELECTOR_SPIN_MAX_STREAMS]; //Statically allow up to n streams on this (simple) selector
    size_t stream_count;                              //Number of streams added to the slector
    size_t last;
} camio_selector_spin_t;



/********************************************************************
 *                  PUBLIC DEFS
 ********************************************************************/

camio_selector_t* camio_selector_spin_new( camio_clock_t* clock, camio_selector_spin_params_t* params);


#endif /* CAMIO_SELECTOR_SPIN_H_ */
