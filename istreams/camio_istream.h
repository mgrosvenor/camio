/*
 * Copyright  (C) Matthew P. Grosvenor, 2012, All Rights Reserved
 *
 * Input stream definition
 *
 */

#ifndef CAMIO_ISTREAM_H_
#define CAMIO_ISTREAM_H_

#include <unistd.h>

#include "../stream_description/camio_descr.h"
#include "../clocks/camio_clock.h"
#include "../selectors/camio_selector.h"

struct camio_istream;
typedef struct camio_istream camio_istream_t;

struct camio_istream{
     int(*open)(camio_istream_t* this, const camio_descr_t* desc ); //Open the stream and prepare for reading, return 0 if it succeeds
     void(*close)(camio_istream_t* this);                         //Close the stream
     int (*ready)(camio_istream_t* this);                         //Returns non-zero if a call to start_read will be non-blocking
     int (*start_read)(camio_istream_t* this, uint8_t** out_bytes);  //Returns the number of bytes available to read, this can be 0. If bytes available is non-zero, out_bytes has a pointer to the start of the bytes to read
     int (*end_read)(camio_istream_t* this, uint8_t* free_buff);     //Returns 0 if the contents of out_bytes have NOT changed since the call to start_read. For buffers this may fail, if this is the case, data read in start_read maybe corrupt.
     void(*delete)(camio_istream_t* this);                        //Closes the stream and deletes the memory used
     camio_clock_t* clock;
     camio_selectable_t selector;
     void* priv;
};

camio_istream_t* camio_istream_new(const char* description, camio_clock_t* clock, void* parameters);

#endif /* CAMIO_ISTREAM_H_ */
