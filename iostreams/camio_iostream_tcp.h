/*
 * Copyright  (C) Matthew P. Grosvenor, 2012, All Rights Reserved
 *
 * TCP Input/Output stream definition
 *
 */

#ifndef CAMIO_IOSTREAM_TCP_H_
#define CAMIO_IOSTREAM_TCP_H_

#include <netinet/in.h>

#include "camio_iostream.h"

/********************************************************************
 *                  PRIVATE DEFS
 ********************************************************************/

typedef struct {
    //No params at this stage
} camio_iostream_tcp_params_t;

typedef struct {
    camio_iostream_t iostream;
    uint8_t* buffer;
    size_t buffer_size;
    size_t bytes_read;
    int is_closed;                      //Has close be called?
    struct sockaddr_in addr;            //Source address/port
    camio_iostream_tcp_params_t* params;  //Parameters passed in from the outside

} camio_iostream_tcp_t;



/********************************************************************
 *                  PUBLIC DEFS
 ********************************************************************/

camio_iostream_t* camio_iostream_tcp_new( const camio_descr_t* descr, camio_clock_t* clock, camio_iostream_tcp_params_t* params);


#endif /* CAMIO_IOSTREAM_TCP_H_ */
