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


struct camio_cat_options_t{
    char* stream;
    int server;
    int client;
    char* selector;
} options ;


camio_iostream_t* iostream;
camio_istream_t* stdinstr;
camio_ostream_t* stdoutstr;

enum { IOSTREAM =0, INSTREAM, OUTSTREAM };


void term(int signum){
    iostream->delete(iostream);
    exit(0);
}




int main(int argc, char** argv){

    signal(SIGTERM, term);
    signal(SIGINT, term);


    camio_options_short_description("camio_cat");
    camio_options_add(CAMIO_OPTION_REQUIRED,  's', "stream",   "An iostream description such as tcp:127.0.0.1:2000",  CAMIO_STRING, &options.stream, "");
    camio_options_add(CAMIO_OPTION_FLAG,      'C', "client",   "If the program is client mode, std-in is connected to the tx pipe and std-out is connected to the rx pipe [default]", CAMIO_BOOL, &options.client, 1);
    camio_options_add(CAMIO_OPTION_FLAG,      'S', "server",   "If the program is server mode, the tx and rx pipes loop-back on each other", CAMIO_BOOL, &options.server, 0);
    camio_options_add(CAMIO_OPTION_OPTIONAL,  's', "selector", "Selector description eg selection", CAMIO_STRING, &options.selector, "spin" );
    camio_options_long_description("Tests I/O streams  .");
    camio_options_parse(argc, argv);

    if(options.server && options.client){
        eprintf_exit("Error, both client and server mode cannot be enabled at the same time\n");
    }

    camio_selector_t* selector = camio_selector_new(options.selector,NULL,NULL);
    iostream = camio_iostream_new(options.stream,NULL,NULL);
    selector->insert(selector,&iostream->selector,IOSTREAM);

    if(options.client){
        stdinstr = camio_istream_new("std-in",NULL,NULL);
        selector->insert(selector,&stdinstr->selector,INSTREAM);
        stdoutstr = camio_ostream_new("std-out", NULL, NULL);
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
                if(options.server){
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
