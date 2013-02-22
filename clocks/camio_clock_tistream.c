/*
 * Copyright  (C) Matthew P. Grosvenor, 2012, All Rights Reserved
 *
 */

#include "camio_clock_tistream.h"
#include "../errors/camio_errors.h"
#include "../utils/camio_util.h"

//Initialize the new clock
int camio_clock_tistream_init(camio_clock_t* this){
    camio_clock_tistream_t* priv = this->priv;
    priv->time.counter = 0;
    return 0;
}

//Is this clock from a free running source, or is is it driven by something
int camio_clock_tistream_is_driven(camio_clock_t* this){
    return 1; //Clock drives it's input from a timed istream
}

//Get the current clock time structure
camio_time_t* camio_clock_tistream_get(camio_clock_t* this){
    camio_clock_tistream_t* priv = this->priv;
    return &priv->time;
}

//Set the new current time. Clock is monotonically increasing,
//if the new time is older than the current time, no change should occur
int camio_clock_tistream_set(camio_clock_t* this, camio_time_t* current){
    camio_clock_tistream_t* priv = this->priv;

    //Only increment if the new time is newer
    if(likely(priv->time.counter < current->counter)){
        priv->time.counter = current->counter;
    }
    return 0;
}


/* ****************************************************
 * Construction
 */

camio_clock_t* camio_clock_tistream_construct(camio_clock_tistream_t* priv, camio_clock_tistream_params_t* params){
    if(!priv){
        eprintf_exit("Clock supplied is null\n");
    }
    //Initialize the local variables
    priv-> time.counter     = 0;

    //Populate the function members
    priv->clock.priv            = priv; //Lets us access private members
    priv->clock.init            = camio_clock_tistream_init;
    priv->clock.is_driven       = camio_clock_tistream_is_driven;
    priv->clock.get             = camio_clock_tistream_get;
    priv->clock.set             = camio_clock_tistream_set;

    //Call ini, because its the obvious thing to do now...
    priv->clock.init(&priv->clock);

    //Return the generic clock interface for the outside world to use
    return &priv->clock;

}

camio_clock_t* camio_clock_tistream_new( camio_clock_tistream_params_t* params){
    camio_clock_tistream_t* priv = malloc(sizeof(camio_clock_tistream_t));
    if(!priv){
        eprintf_exit("No memory available for log istream creation\n");
    }
    return camio_clock_tistream_construct(priv, params);
}

