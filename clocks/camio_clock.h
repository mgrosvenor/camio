/*
 * Copyright  (C) Matthew P. Grosvenor, 2012, All Rights Reserved
 *
 * Camio clock implementation
 *
 */

#ifndef CAMIO_CLOCK_H_
#define CAMIO_CLOCK_H_

#include <stdint.h>
#include "camio_time.h"


struct camio_clock;
typedef struct camio_clock camio_clock_t;

struct camio_clock{
     int(*init)(camio_clock_t* this);                                //Initialize the new clock
     int (*is_driven)(camio_clock_t* this);                          //Is this clock from a free running source, or is is it driven by something
     camio_time_t* (*get)(camio_clock_t* this);                       //Get the current clock time structure
     int (*set)(camio_clock_t* this, camio_time_t* current);          //Set the new current time. Clock is monotonically increasing, if the new time is older than the current time, no change should occur
     void* priv;                                                    //For clock specific structures.
};

camio_clock_t* camio_clock_new( char* description, void* params);


#endif /* CAMIO_CLOCK_H_ */
