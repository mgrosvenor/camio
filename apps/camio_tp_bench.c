
#include <stdio.h>
#include <stdlib.h>
#include <memory.h>
#include <signal.h>
#include <sys/time.h>

#include "camio.h"
#include "utils/camio_ring.h"

static struct camio_cat_options_t{
    char* stream;
    int listen;
    char* selector;
    char* perf_out;
    int begin;
} options ;

static camio_istream_t*     in = NULL;
static camio_ostream_t*    out = NULL;
static camio_iostream_t* inout = NULL;

static void term(int signum){
    if(in){ free(in); }
    if(out){ free(out); }
    if(inout){ free(inout); }
    exit(0);
}


static void do_listener(){
    printf("Initializing do_listener...\n");
    in = camio_istream_new(options.stream, NULL, NULL, NULL);
    uint64_t read_count = 0;
    struct timeval start, end;
    uint8_t* buff;
    uint64_t len;
    uint64_t total_data = 0;
    uint64_t error_count = 0;

    //wait until the first data is ready before starting timing
    len = in->start_read(in,&buff);
    in->end_read(in, NULL);

    printf("Running do_listener...\n");
    gettimeofday(&start, NULL);
    while(1){
        if(likely(in->ready(in))){
            len = in->start_read(in,&buff);
            if(in->end_read(in,NULL)){
                error_count++;
            }
            else{
                read_count++;
                total_data += len;
            }

            if(unlikely( read_count && !(read_count % (1000)) )){
                gettimeofday(&end,NULL);
                const uint64_t nanos_start  = start.tv_sec * 1000 * 1000 + start.tv_usec;
                const uint64_t nanos_end    = end.tv_sec * 1000 * 1000 + end.tv_usec;
                printf("%c,%luMB/s\n", 'l', total_data / (nanos_end - nanos_start));
                total_data = 0;
                error_count = 0;
                gettimeofday(&start,NULL);
            }
        }
    }
}

static void do_sender(){
    printf("Initializing do_sender...\n");
    out = camio_ostream_new(options.stream, NULL, NULL, NULL);
    uint64_t write_count = 0;
    struct timeval start, end;
    uint8_t* buff;
    uint64_t total_data = 0;

    //Make some test data
    uint64_t test_data[CAMIO_RING_SLOT_AVAIL / sizeof(uint64_t)];
    const uint64_t test_pattern = 0xCAFEFEEDDEADBEEFULL;
    memset((char*)test_data,~0,CAMIO_RING_SLOT_AVAIL);
    uint64_t i = 0;
    for(i = 0; i < CAMIO_RING_SLOT_AVAIL / sizeof(uint64_t); i++){
        test_data[i] = test_pattern + i;
    }

    //Wait until the ring is connected
    while(! out->start_write(out,CAMIO_RING_SLOT_AVAIL)){
        //Don't spin too hard
        usleep(100 * 1000);
        out->end_write(out,CAMIO_RING_SLOT_AVAIL);
    }


    printf("Running do_sender...\n");
    gettimeofday(&start, NULL);
    while(1){
        if(unlikely( write_count && !(write_count % (1000)) )){
            gettimeofday(&end,NULL);
            const uint64_t nanos_start  = start.tv_sec * 1000 * 1000 + start.tv_usec;
            const uint64_t nanos_end    = end.tv_sec * 1000 * 1000 + end.tv_usec;
            printf("%c,%luMB/s\n", 's', total_data / (nanos_end - nanos_start));
            total_data = 0;
            gettimeofday(&start,NULL);
        }

        if(options.begin){
            buff = out->start_write(out,CAMIO_RING_SLOT_AVAIL);
            //Do some work here?
            (void)buff;
            out->end_write(out,CAMIO_RING_SLOT_AVAIL);
        }
        else{
            out->assign_write(out,(uint8_t*)test_data,CAMIO_RING_SLOT_AVAIL );
            out->end_write(out,CAMIO_RING_SLOT_AVAIL);
        }
        write_count++;
        total_data+= CAMIO_RING_SLOT_AVAIL;

    }


}

int main(int argc, char** argv){
    signal(SIGTERM, term);
    signal(SIGINT, term);

    camio_options_short_description("camio_tp_bench");
    camio_options_add(CAMIO_OPTION_OPTIONAL,  'd', "stream",   "An istream or ostream description such. [ring:/tmp/bench.ring]",  CAMIO_STRING, &options.stream, "ring:/tmp/tp_bench.ring");
    camio_options_add(CAMIO_OPTION_FLAG,      'l', "listen",   "If the program is listen mode, the tx and rx pipes loop-back on each other", CAMIO_BOOL, &options.listen, 0);
    camio_options_add(CAMIO_OPTION_FLAG,      'b', "begin-write",   "Use begin_write instead of assign write", CAMIO_BOOL, &options.begin, 0);
    camio_options_add(CAMIO_OPTION_OPTIONAL,  's', "selector", "Selector description eg selection", CAMIO_STRING, &options.selector, "spin" );
    camio_options_add(CAMIO_OPTION_OPTIONAL,  'p', "perf-mon", "Performance monitoring output path", CAMIO_STRING, &options.perf_out, "log:/tmp/camio_chat.perf" );
    camio_options_long_description("Tests I/O streams as either a client or server.");
    camio_options_parse(argc, argv);


    if(options.listen){
        printf("Starting TP Bench in listener mode...\n");
        do_listener();
    }
    else{
        printf("Starting TP Bench in sender mode...\n");
        do_sender();
    }


    term(0);
    return 0;
}
