/*
 * Copyright  (C) Matthew P. Grosvenor, 2012, All Rights Reserved
 *
 * Input stream definition
 *
 */

#ifndef CAMIO_PERF_H_
#define CAMIO_PERF_H_

#include <unistd.h>

#define CAMIO_PERF_EVENT_MAX (1024 * 128)

typedef struct {
    uint64_t ts;               //Time the event was logged
    uint64_t event_id : 32;  //ID of the event, used to tie start and stop operations together
    uint64_t cond_id : 32;   //ID of the event, used to differentiate different start/stop conditions for the same ID.
} camio_perf_event_t;


typedef struct {
    char* output_descr;
    uint64_t event_count;
    uint64_t event_index;
    camio_perf_event_t events[CAMIO_PERF_EVENT_MAX];

} camio_perf_t;


camio_perf_t* camio_perf_init();
void camio_perf_finish(camio_perf_t* camio_perf);

#define camio_perf_event_start(camio_perf, event_id, cond_id)                                   \
    if(likely(camio_perf->event_index < CAMIO_PERF_EVENT_MAX)){                                 \
        camio_perf->events[camio_perf->event_index].event_id = event_id;                        \
        camio_perf->events[camio_perf->event_index].cond_id  = cond_id;                         \
        __asm __volatile("rdtsc" : "=A" (camio_perf->events[camio_perf->event_index].ts));      \
        camio_perf->event_index++;                                                              \
    }                                                                                           \
    camio_perf->event_count++;


#define camio_perf_event_stop(camio_perf, event_id, cond_id)                                    \
    if(likely(camio_perf->event_index < CAMIO_PERF_EVENT_MAX)){                                 \
        __asm __volatile("rdtsc" : "=A" (camio_perf->events[camio_perf->event_index].ts));      \
        camio_perf->events[camio_perf->event_index].event_id = event_id + (1<<31);              \
        camio_perf->events[camio_perf->event_index].cond_id  = cond_id;                         \
        camio_perf->event_index++;                                                              \
    }                                                                                           \
    camio_perf->event_count++;



#endif /* CAMIO_PERF_H_ */
