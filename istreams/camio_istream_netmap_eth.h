/*
 * Copyright  (C) Matthew P. Grosvenor, 2012, All Rights Reserved
 *
 * Camio NETMAP_ETH_ETH card input stream
 *
 */


#ifndef CAMIO_ISTREAM_NETMAP_ETH_ETH_H_
#define CAMIO_ISTREAM_NETMAP_ETH_ETH_H_

#include "camio_istream.h"
#include "camio_istream_netmap.h"


/********************************************************************
 *                  PRIVATE DEFS
 ********************************************************************/

typedef struct {
    int is_closed;
    camio_istream_t* netmap_base;
    uint8_t* data_head;
    uint8_t* data_size;

    camio_istream_t istream;
    camio_istream_netmap_params_t* params;  //Parameters passed in from the outside
    camio_perf_t* perf_mon;                 //Performance measurement

} camio_istream_netmap_eth_t;




/********************************************************************
 *                  PUBLIC DEFS
 ********************************************************************/

camio_istream_t* camio_istream_netmap_eth_new( const camio_descr_t* opts, camio_clock_t* clock, camio_istream_netmap_eth_params_t* params, camio_perf_t* perf_mon);


#endif /* CAMIO_ISTREAM_NETMAP_ETH_ETH_H_ */
