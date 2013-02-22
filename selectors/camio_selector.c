/*
 * Copyright  (C) Matthew P. Grosvenor, 2012, All Rights Reserved
 *
 * Selector definition
 *
 */

#include <string.h>

#include "../errors/camio_errors.h"

#include "camio_selector.h"
#include "camio_selector_spin.h"
#include "camio_selector_seq.h"
#include "camio_selector_poll.h"



camio_selector_t* camio_selector_new(const char* description, camio_clock_t* clock, void* parameters){
    camio_selector_t* result = NULL;

    if(strcmp(description,"spin") == 0 ){
        result = camio_selector_spin_new(clock, parameters);
    }
    else if(strcmp(description,"seq") == 0 ){
        result = camio_selector_seq_new(clock, parameters);
    }
    else if(strcmp(description,"poll") == 0 ){
        result = camio_selector_poll_new(clock, parameters);
    }

    else{
        eprintf_exit(CAMIO_ERR_UNKNOWN_SELECTOR,"Could not create selector from description \"%s\" \n", description);
    }

    return result;

}
