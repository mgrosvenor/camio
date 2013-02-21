/*
 * Copyright  (C) Matthew P. Grosvenor, 2012, All Rights Reserved
 *
 * Input stream factory definition
 *
 */

#include <string.h>

#include "camio_clock.h"
#include "../camio_errors.h"

//#include "camio_clock_gtod.h"
#include "camio_clock_tistream.h"


camio_clock_t* camio_clock_new( char* description, void* parameters){
    camio_clock_t* result = NULL;

    //Time of day clock. Uses a call to linux gettimeofday syscall
    if(strcmp(description,"gtod") == 0 ){
        //result = camio_clock_tod_new(&descr,clock, NULL);
    }
    //Timed istream. Uses one or more timed istreams to drive the clock forward.
    else if(strcmp(description,"tistream") == 0){
        result = camio_clock_tistream_new( parameters );
    }
    else{
        eprintf_exit(CAMIO_ERR_UNKNOWN_CLOCK,"Could not create clock from description \"%s\" \n", description);
    }

    return result;

}
