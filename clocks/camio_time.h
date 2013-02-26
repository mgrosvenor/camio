/*
 * Copyright  (C) Matthew P. Grosvenor, 2012, All Rights Reserved
 *
 * Camio time implementation
 *
 */

#ifndef CAMIO_TIME_H_
#define CAMIO_TIME_H_


typedef struct {
    int64_t counter;
} camio_time_t;



void camio_time_to_timespec();
void camio_timespec_to_time();


#endif /* CAMIO_TIME_H_ */
