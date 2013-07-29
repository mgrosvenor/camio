/*
 * camio_ostream_netmap_eth.h
 *
 *  Created on: Nov 15, 2012
 *      Author: root
 */

#ifndef CAMIO_OSTREAM_NETMAP_ETH_H_
#define CAMIO_OSTREAM_NETMAP_ETH_H_

#include "camio_ostream.h"
#include "camio_ostream_netmap.h"

/********************************************************************
 *                  PRIVATE DEFS
 ********************************************************************/

typedef struct {
    int is_closed;
    camio_ostream_t* netmap_base;
    uint8_t* data_head;
    uint64_t data_size;
    camio_ostream_t ostream;
    camio_ostream_netmap_params_t* params;      //Parameters from the outside world
    uint64_t base_buff_size;
    camio_perf_t* perf_mon;

} camio_ostream_netmap_eth_t;



/********************************************************************
 *                  PUBLIC DEFS
 ********************************************************************/

camio_ostream_t* camio_ostream_netmap_eth_new( const camio_descr_t* opts, camio_clock_t* clock, camio_ostream_netmap_params_t* params, camio_perf_t* perf_mon);



#endif /* CAMIO_OSTREAM_NETMAP_ETH_H_ */
