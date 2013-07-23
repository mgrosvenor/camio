/*
 * camio_fio.h
 *
 *  Created on: Nov 14, 2012
 *      Author: root
 */

#ifndef CAMIO_ISTREAM_FIO_H_
#define CAMIO_ISTREAM_FIO_H_

#include "camio_istream.h"

#define CAMIO_ISTREAM_FIO_BLOCKING    1
#define CAMIO_ISTREAM_FIO_NONBLOCKING 0

/********************************************************************
 *                  PRIVATE DEFS
 ********************************************************************/


typedef struct {
    int fd; //Allow creator to bypass file open stage and supply their own FD;
} camio_istream_fio_params_t;

typedef struct {
    camio_istream_t istream;
    int64_t max_chunk;                   //How big should each read chunk be?
    int is_closed;                       //Has close be called?
    size_t read_buff_size;               //Size of a line that is ready for start_read
    uint8_t* read_buff;                  //Place to read data from in by calling start_read
    int64_t read_buff_data_size;         //Amount of data currently waiting in the read buffer
    camio_istream_fio_params_t* params;  //Parameters passed in from the outside
    camio_perf_t* perf_mon;

} camio_istream_fio_t;




/********************************************************************
 *                  PUBLIC DEFS
 ********************************************************************/

camio_istream_t* camio_istream_fio_new( const camio_descr_t* descr, camio_clock_t* clock, camio_istream_fio_params_t* params, camio_perf_t* perf_mon);


#endif /* CAMIO_ISTREAM_FIO_H_ */
