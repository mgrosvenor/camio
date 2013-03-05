/*
 * Copyright  (C) Matthew P. Grosvenor, 2012, All Rights Reserved
 *
 * Input/Output stream definition
 *
 */

#ifndef CAMIO_IOSTREAM_H_
#define CAMIO_IOSTREAM_H_

#include <unistd.h>

#include "../stream_description/camio_descr.h"
#include "../clocks/camio_clock.h"
#include "../selectors/camio_selector.h"
#include "../perf/camio_perf.h"

struct camio_iostream;
typedef struct camio_iostream camio_iostream_t;

struct camio_iostream{
     int(*open)(camio_iostream_t* this, const camio_descr_t* desc, camio_perf_t* perf_mon );    //Open the stream and prepare for reading/writing, return 0 if it succeeds
     void(*close)(camio_iostream_t* this);                                                      //Close the stream
     void(*delete)(camio_iostream_t* this);                                                     //Closes the stream and deletes the memory used

     int (*rready)(camio_iostream_t* this);                                                     //Returns non-zero if a call to start_read will be non-blocking
     int (*start_read)(camio_iostream_t* this, uint8_t** out_bytes);                            //Returns the number of bytes available to read, this can be 0. If bytes available is non-zero, out_bytes has a pointer to the start of the bytes to read
     int (*end_read)(camio_iostream_t* this, uint8_t* free_buff);                               //Returns 0 if the contents of out_bytes have NOT changed since the call to start_read. For buffers this may fail, if this is the case, data read in start_read maybe corrupt.
     void(*rsync)(camio_iostream_t* this);                                                      //Some streams require explicit syncronisation, and the timing of that is performance critical. This interface exists for these streams
     camio_selectable_t selector;                                                               //Allows the read endpoint to be selected on

     int (*wready)(camio_iostream_t* this);                                                     //Returns non-zero if a call to start_write will be non-blocking
     uint8_t* (*start_write)(camio_iostream_t* this, size_t len );                              //Returns a pointer to a space of size len, ready for data
     uint8_t* (*end_write)(camio_iostream_t* this, size_t len);                                 //Commit the data to the buffer previously allocated, if the write was "assigned" and write want's to keep the buffer, optionally return a fresh one
     int (*can_assign_write)(camio_iostream_t*);                                                //Is this stream capable of taking over another stream buffer
     int (*assign_write)(camio_iostream_t* this, uint8_t* buffer, size_t len);                  //Assign the write buffer to the stream
     void(*wsync)(camio_iostream_t* this);                                                      //Some streams require explicit syncronisation, and the timing of that is performance critical. This interface exists for these streams

     camio_clock_t* clock;
     void* priv;
};

camio_iostream_t* camio_iostream_new(const char* description, camio_clock_t* clock, void* parameters, camio_perf_t* perf_mon);

#endif /* CAMIO_IOSTREAM_H_ */
