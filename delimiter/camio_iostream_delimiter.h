/*
 * delimiter.h
 *
 *  Created on: Mar 11, 2013
 *      Author: mgrosvenor
 */

#ifndef DELIMITER_H_
#define DELIMITER_H_

typedef struct {
    //Nothing to see here, move along
} camio_iostream_delimiter_params_t;


typedef struct {
    camio_iostream_t iostream;
    camio_iostream_t* base;                     //Base stream that will be "decorated"
    camio_iostream_delimiter_params_t* params;  //Parameters passed in from the outside
    int64_t (*delimit)(uint8_t* data, uint64_t size); //Returns the size of the delimited packet, or 0, the stream will handle buffer jiggling
    int is_closed;
    uint8_t* rbuffer;
    uint64_t rbuffer_size;
} camio_iostream_delimiter_t;


camio_iostream_t* camio_iostream_delimiter_new(camio_iostream_t* iostream, int64_t (*delimit)(uint8_t* data, uint64_t size), void* parameters, camio_perf_t* perf_mon);



#endif /* DELIMITER_H_ */
