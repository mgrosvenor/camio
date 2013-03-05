/*
 * Copyright  (C) Matthew P. Grosvenor, 2012, All Rights Reserved
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <memory.h>
#include <signal.h>

#include "../istreams/camio_istream.h"
#include "../ostreams/camio_ostream.h"
#include "../iostreams/camio_iostream.h"
#include "../prog_options/camio_prog_options.h"
#include "../types/camio_types.h"
#include "../utils/camio_util.h"
#include "../errors/camio_errors.h"
#include "../perf/camio_perf.h"
#include "../iostreams/camio_iostream_tcp.h"


static struct camio_cat_options_t{
    camio_list_t(string) stream;
    int listen;
    char* selector;
    char* perf_out;
} options ;


static camio_iostream_t* iostream = NULL;
static camio_istream_t* stdinstr = NULL;
static camio_ostream_t* stdoutstr = NULL;
static camio_perf_t* perf_mon = NULL;

enum { IOSTREAM =0, INSTREAM, OUTSTREAM };


void term(int signum){
    camio_perf_finish(perf_mon);
    if(iostream) { iostream->delete(iostream); }
    if(stdinstr) { stdinstr->delete(stdinstr); }
    if(stdoutstr) { stdoutstr->delete(stdoutstr); }

    exit(0);
}




int main(int argc, char** argv){

    signal(SIGTERM, term);
    signal(SIGINT, term);

    camio_options_short_description("camio_cat");
    camio_options_add(CAMIO_OPTION_UNLIMTED,  'i', "stream",   "An iostream description such. [tcp:127.0.0.1:2000]",  CAMIO_STRINGS, &options.stream, "tcp:127.0.0.1:2000");
    camio_options_add(CAMIO_OPTION_FLAG,      'l', "listen",   "If the program is listen mode, the tx and rx pipes loop-back on each other", CAMIO_BOOL, &options.listen, 0);
    camio_options_add(CAMIO_OPTION_OPTIONAL,  's', "selector", "Selector description eg selection", CAMIO_STRING, &options.selector, "spin" );
    camio_options_add(CAMIO_OPTION_OPTIONAL,  'p', "perf-mon", "Performance monitoring output path", CAMIO_STRING, &options.perf_out, "log:/tmp/camio_chat.perf" );
    camio_options_long_description("Tests I/O streams as either a client or server.");
    camio_options_parse(argc, argv);

    camio_selector_t* selector = camio_selector_new(options.selector,NULL,NULL);

    perf_mon = camio_perf_init(options.perf_out, 128 * 1024);
    camio_iostream_tcp_params_t parms = { .listen = options.listen };
    iostream = camio_iostream_new(options.stream.items[0],NULL,&parms, perf_mon);
    selector->insert(selector,&iostream->selector,IOSTREAM);



    if(!options.listen  ){
        stdinstr = camio_istream_new("std-log",NULL,NULL,perf_mon);
        selector->insert(selector,&stdinstr->selector,INSTREAM);
        stdoutstr = camio_ostream_new("std-log", NULL, NULL, perf_mon);
    }


    uint8_t* buff = NULL;
    size_t len = 0;
    size_t which = ~0;


    while(selector->count(selector)){

        //Wait for some input
        which = selector->select(selector);

        switch(which){
            case IOSTREAM:
                len = iostream->start_read(iostream,&buff);
                if(options.listen){
                    iostream->assign_write(iostream,buff,len);
                    iostream->end_write(iostream,len);
                }
                else{
                    stdoutstr->assign_write(stdoutstr,buff,len);
                    stdoutstr->end_write(stdoutstr,len);
                }
                iostream->end_read(iostream, NULL);
                break;
            case INSTREAM:
                len = stdinstr->start_read(stdinstr,&buff);
                iostream->assign_write(iostream,buff,len);
                iostream->end_write(iostream,len);
                stdinstr->end_read(stdinstr, NULL);
                break;
        }

    }

    term(0);

    //Unreachable
    return 0;
}
