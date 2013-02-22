/*
 * Copyright  (C) Matthew P. Grosvenor, 2012, All Rights Reserved
 *
 * Fe2+ clock driven by an istream
 *
 */

#ifndef CAMIO_CLOCK_TISTREAM_H_
#define CAMIO_CLOCK_TISTREAM_H_

#include "camio_clock.h"


/********************************************************************
 *                  PRIVATE DEFS
 ********************************************************************/


typedef struct {
    //No params yet
} camio_clock_tistream_params_t;

typedef struct {
    camio_time_t time;
    camio_clock_t clock;
} camio_clock_tistream_t;



/********************************************************************
 *                  PUBLIC DEFS
 ********************************************************************/

camio_clock_t* camio_clock_tistream_new( camio_clock_tistream_params_t* params);


#endif /* CAMIO_CLOCK_TISTREAM_H_ */
