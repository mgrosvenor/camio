/*
 * Copyright  (C) Matthew P. Grosvenor, 2012, All Rights Reserved
 *
 * Input/Output stream factory definition
 *
 */

#include <string.h>

#include "camio_iostream.h"
#include "../iostreams/camio_iostream_tcp.h"


#include "../errors/camio_errors.h"


camio_iostream_t* camio_iostream_new(const char* description, camio_clock_t* clock, void* parameters){
    camio_iostream_t* result = NULL;
    camio_descr_t descr;
    camio_descr_construct(&descr);
    camio_descr_parse(description,&descr);

    if(strcmp(descr.protocol,"tcp") == 0 ){
        result = camio_iostream_tcp_new(&descr,clock,parameters);
    }
    else{
        eprintf_exit("Could not create iostream from description \"%s\" \n", description);
    }

    camio_descr_destroy(&descr);
    return result;

}


