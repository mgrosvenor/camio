/*
 * delimiter.h
 *
 *  Created on: Mar 11, 2013
 *      Author: mgrosvenor
 */

#ifndef DELIMITER_H_
#define DELIMITER_H_


//Returns the size of the delimited packet, or 0
typedef int64_t (*delimiter_f)(uint8_t*, uint64_t);

typedef struct {
    //Nothing to see here, move along
} camio_iostream_delimiter_params_t;


typedef struct {
    camio_iostream_t iostream;
    camio_iostream_t* base;                     //Base stream that will be "decorated"
    camio_iostream_delimiter_params_t* params;  //Parameters passed in from the outside
    delimiter_f delimit;
    int is_closed;
    uint8_t* working_buffer;
    uint64_t working_buffer_size;
    uint64_t working_buffer_contents_size;
    uint8_t* read_buffer;
    uint64_t read_buffer_size;
    uint8_t* result_buffer;
    uint64_t result_buffer_size;


} camio_iostream_delimiter_t;


camio_iostream_t* camio_iostream_delimiter_new(camio_iostream_t* base, delimiter_f delimit, void* parameters);



#endif /* DELIMITER_H_ */
