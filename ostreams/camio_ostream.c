/*
 * Copyright  (C) Matthew P. Grosvenor, 2012, All Rights Reserved
 *
 * Input stream factory definition
 *
 */

#include <string.h>
#include <stdlib.h>
#include <unistd.h>

#include "camio_ostream.h"
#include "../errors/camio_errors.h"

#include "camio_ostream_log.h"
#include "camio_ostream_raw.h"
#include "camio_ostream_udp.h"
#include "camio_ostream_ring.h"
#include "camio_ostream_blob.h"
#include "camio_ostream_netmap.h"


camio_ostream_t* camio_ostream_new( char* description, camio_clock_t* clock, void* parameters, camio_perf_t* perf_mon){
    camio_ostream_t* result = NULL;
    camio_descr_t descr;
    camio_descr_construct(&descr);
    camio_descr_parse(description,&descr);

    if(!perf_mon){
        perf_mon = camio_perf_init("",0);
    }

    if(strcmp(descr.protocol,"log") == 0 ){
        result = camio_ostream_log_new(&descr,clock, parameters, perf_mon);
    }
    else if(strcmp(descr.protocol,"std-log") == 0){
        camio_ostream_log_params_t params = { .fd = STDOUT_FILENO };
        result = camio_ostream_log_new(&descr,clock, &params, perf_mon);
    }
    else if(strcmp(descr.protocol,"raw") == 0 ){
            result = camio_ostream_raw_new(&descr,clock, parameters, perf_mon);
    }
    else if(strcmp(descr.protocol,"ring") == 0 ){
            result = camio_ostream_ring_new(&descr,clock, parameters, perf_mon);
    }
    else if(strcmp(descr.protocol,"udp") == 0 ){
            result = camio_ostream_udp_new(&descr,clock, parameters, perf_mon);
    }
    else if(strcmp(descr.protocol,"blob") == 0 ){
            result = camio_ostream_blob_new(&descr,clock, parameters, perf_mon);
    }
    else if(strcmp(descr.protocol,"nmap") == 0 ){
            result = camio_ostream_netmap_new(&descr,clock, parameters, perf_mon);
    }
    else{
        eprintf_exit("Could not create ostream from description \"%s\" \n", description);
    }

    camio_descr_destroy(&descr);
    return result;

}
