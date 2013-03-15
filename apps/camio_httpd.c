/*
 * Copyright  (C) Matthew P. Grosvenor, 2012, All Rights Reserved
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <memory.h>
#include <signal.h>

#include "../camio.h"
#include "../iostreams/camio_iostream_tcp.h"

static struct camio_cat_options_t{
    camio_list_t(string) stream;
    int listen;
    char* selector;
    char* perf_out;
    char* http_root;
} options ;


static camio_iostream_t* iostream = NULL;
static camio_perf_t* perf_mon = NULL;

enum { IOSTREAM =0, INSTREAM, OUTSTREAM };


void term(int signum){
    camio_perf_finish(perf_mon);
    if(iostream) { iostream->delete(iostream); }
    exit(0);
}



void http_do_get(uint8_t* buffer, uint64_t size){
    char file_path[1024];
    char camio_path[1024];
    uint64_t i = 0;
    for(; i < size; i++){
        if(buffer[i] == ' '){
            break;
        }
    }

    if(i >= size){
        wprintf("Error malformed request, could not extract filename\n");
        return;
    }


    snprintf(file_path, 1024, "%s%.*s", options.http_root, (int)i, buffer);
    snprintf(camio_path, 1024, "%s%.*s", options.http_root, (int)i, buffer);

    //Should stat file path here

    camio_istream_t* http_file = camio_istream_new(file_path, NULL, NULL, NULL);

    uint8_t* buff;
    uint64_t len;
    len = http_file->start_read(http_file, &buff );




}



void http_decode(uint8_t* buffer, uint64_t size){
    uint64_t i = 0;
    for(i = 0; i < size; i++){
        if(memcmp(&buffer[i],"GET ", 4) == 0){
            http_do_get(buffer + 4, size - 4);
        }
    }
}

int64_t http_delimiter(uint8_t* buffer, uint64_t size) {
    uint64_t i = 0;
    for(i = 0; i < size - 3; i++){
        if(memcmp(&buffer[i],"\r\n\r\n", 4) == 0){
            return i + 4;
        }
    }
    return -1;
}

int main(int argc, char** argv){

    signal(SIGTERM, term);
    signal(SIGINT, term);

    camio_options_short_description("camio_httpd");
    camio_options_add(CAMIO_OPTION_UNLIMTED,  'i', "stream",   "An iostream description such. [tcp:127.0.0.1:2000]",  CAMIO_STRINGS, &options.stream, "tcp:127.0.0.1:2000");
    camio_options_add(CAMIO_OPTION_OPTIONAL,  'r', "http-root", "Root of the HTTP tree", CAMIO_STRING, &options.http_root, "http_root" );
    camio_options_add(CAMIO_OPTION_OPTIONAL,  's', "selector", "Selector description eg selection", CAMIO_STRING, &options.selector, "spin" );
    camio_options_add(CAMIO_OPTION_OPTIONAL,  'p', "perf-mon", "Performance monitoring output path", CAMIO_STRING, &options.perf_out, "log:/tmp/camio_httpd.perf" );
    camio_options_long_description("A simple HTTP server to demonstrate CamIO delimiter stream and CamIO connection server");
    camio_options_parse(argc, argv);

    camio_selector_t* selector = camio_selector_new(options.selector,NULL,NULL);

    perf_mon = camio_perf_init(options.perf_out, 128 * 1024);
    camio_iostream_tcp_params_t parms = { .listen = 1 };
    iostream =  camio_iostream_delimiter_new( camio_iostream_new(options.stream.items[0],NULL,&parms, perf_mon) , http_delimiter, NULL) ;

    selector->insert(selector,&iostream->selector,IOSTREAM);

    uint8_t* buff = NULL;
    size_t len = 0;
    size_t which = ~0;


    while(selector->count(selector)){

        //Wait for some input
        which = selector->select(selector);

        switch(which){
            case IOSTREAM:
                len = iostream->start_read(iostream,&buff);

                if(len == 0){
                    iostream->end_read(iostream, NULL);
                    selector->remove(selector, IOSTREAM);
                    continue;
                }

                http_decode(buff,len);

                iostream->end_read(iostream, NULL);

                break;
        }

    }

    term(0);

    //Unreachable
    return 0;
}
