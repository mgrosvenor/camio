/*
 * Copyright  (C) Matthew P. Grosvenor, 2012, All Rights Reserved
 *
 * TCPS Input/Output stream definition
 *
 */

#ifndef CAMIO_IOSTREAM_TCPS_H_
#define CAMIO_IOSTREAM_TCPS_H_

#include <netinet/in.h>

#include "camio_iostream.h"

/********************************************************************
 *                  PRIVATE DEFS
 ********************************************************************/

typedef struct {
    int listen;
} camio_iostream_tcps_params_t;


enum camio_iostream_tcps_type { CAMIO_IOSTREAM_TCPS_TYPE_CLIENT, CAMIO_IOSTREAM_TCPS_TYPE_SERVER};

typedef struct {
    camio_iostream_t iostream;
    uint8_t* rbuffer;
    size_t rbuffer_size;
    size_t bytes_read;
    int is_closed; //Has close be called?
    uint8_t* wbuffer;                           //Space to build output
    size_t wbuffer_size;                     //Size of output buffer
    uint8_t* assigned_buffer;                  //Assigned write buffer
    size_t assigned_buffer_sz;              //Assigned write buffer size
    enum camio_iostream_tcps_type type;
    struct sockaddr_in addr;            //Source address/port
    int accept_fd;                     //FD of the tcps listener
    camio_iostream_tcps_params_t* params;  //Parameters passed in from the outside
    camio_perf_t* perf_mon;
} camio_iostream_tcps_t;



/********************************************************************
 *                  PUBLIC DEFS
 ********************************************************************/

camio_iostream_t* camio_iostream_tcps_new( const camio_descr_t* descr, camio_clock_t* clock, camio_iostream_tcps_params_t* params, camio_perf_t* perf_mon);


#endif /* CAMIO_IOSTREAM_TCPS_H_ */
