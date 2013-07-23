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


    char* match = strstr((char*)buffer," HTTP/1.1");
    if(!match){
        wprintf("Error malformed request, could not find HTTP/1.1\n");
    }

    *match = '\0'; //Null terminate the string so that we can pull out the request in-situ
    const char* request = (char*)buffer;

    char response_head[1024];
    char response_body[1024];

    snprintf(response_body,1024,"<html><h1> You requested %s </h1></html>\n\n",request);

    snprintf(response_head,1024, "HTTP/1.1 200 OK\n"
                                 "Content-Type: text/html;charset=utf-8\n"
                                 "Connection: close\n"
                                 "Content-Length: %lu\n\n", strlen(response_body));


    iostream->assign_write(iostream, (uint8_t*)response_head,strlen(response_head));
    iostream->end_write(iostream,strlen(response_head));
    iostream->assign_write(iostream, (uint8_t*)response_body,strlen(response_body));
    iostream->end_write(iostream,strlen(response_body));

}



void http_decode(uint8_t* buffer, uint64_t size){
    uint64_t i = 0;
    for(i = 0; i < size; i++){
        if(memcmp(&buffer[i],"GET ", 4) == 0){
            http_do_get(buffer + 4, size - 4);
        }
    }
}


//TODO XXX: Consider using http://en.wikipedia.org/wiki/Knuth%E2%80%93Morris%E2%80%93Pratt_algorithm
int64_t http_delimiter(uint8_t* buffer, uint64_t size) {
    const char* match = strstr((char*)buffer,"\r\n\r\n");

    if(!match){
        return -1; //None found
    }

    //Return the index directly after the delimiter
    return (match + 4) - (char*)buffer;
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
