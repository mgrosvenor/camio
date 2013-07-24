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
static camio_iostream_t* con_listener = NULL;

static camio_perf_t* perf_mon = NULL;

enum { CONLISTEN = 0, IOSTREAM, INSTREAM, OUTSTREAM };


void term(int signum){
    camio_perf_finish(perf_mon);
    if(iostream) { iostream->delete(iostream); }
    exit(0);
}

#define STATIC_CONTENT_SIZE (1024*23)
char static_content[STATIC_CONTENT_SIZE] = {};

void http_do_get(uint8_t* buffer, uint64_t size){


    char* match = strstr((char*)buffer," HTTP/1.");
    if(!match){
        wprintf("Error malformed request, could not find HTTP/1.\n");
        printf("request string:\n%.*s\n\n",(int)size,buffer);
        exit(-1);
        return;
    }

    *match = '\0'; //Null terminate the string so that we can pull out the request in-situ
    const char* request = (char*)buffer;
    char response_head[1024] = {};
    char response_body[1024] = {};

    //printf("Get request for \"%s\"\n", request);

    if(strcmp(request,"/demo.html")){
        snprintf(response_body,1024,"<html><h1> Error 404 - Page \"%s\" Not Found </h1></html>\n\n",request);
        snprintf(response_head,1024, "HTTP/1.1 404 Not Found\n"
                                     "Content-Type: text/html;charset=utf-8\n"
                                     "Content-Length: %lu\n\n", strlen(response_body));
    }
    else{

        snprintf(response_body,1024,"<html><h1> You requested %s </h1></html>\n\n",request);
        snprintf(response_head,1024, "HTTP/1.1 200 OK\n"
                                     "Content-Type: text/html;charset=utf-8\n"
                                     "Content-Length: %lu\n\n", strlen(response_body) + (STATIC_CONTENT_SIZE));
    }

    iostream->assign_write(iostream, (uint8_t*)response_head,strlen(response_head));
    iostream->end_write(iostream,strlen(response_head));
    iostream->assign_write(iostream, (uint8_t*)response_body,strlen(response_body));
    iostream->end_write(iostream,strlen(response_body));
    iostream->assign_write(iostream, (uint8_t*)static_content,STATIC_CONTENT_SIZE);
    iostream->end_write(iostream,STATIC_CONTENT_SIZE);
    iostream->close(iostream);

}



void http_decode(uint8_t* buffer, uint64_t size){
    char* match = strstr((char*)buffer,"GET ");
    if(match){
        http_do_get((uint8_t*)match + 4, size - 4);
    }
}



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
    camio_options_add(CAMIO_OPTION_UNLIMTED,  'i', "stream",   "An iostream description such. [tcp:127.0.0.1:2000]",  CAMIO_STRINGS, &options.stream, "tcps:127.0.0.1:2000");
    camio_options_add(CAMIO_OPTION_OPTIONAL,  'r', "http-root", "Root of the HTTP tree", CAMIO_STRING, &options.http_root, "http_root" );
    camio_options_add(CAMIO_OPTION_OPTIONAL,  's', "selector", "Selector description eg selection", CAMIO_STRING, &options.selector, "spin" );
    camio_options_add(CAMIO_OPTION_OPTIONAL,  'p', "perf-mon", "Performance monitoring output path", CAMIO_STRING, &options.perf_out, "log:/tmp/camio_httpd.perf" );
    camio_options_long_description("A simple HTTP server to demonstrate CamIO delimiter stream and CamIO connection server");
    camio_options_parse(argc, argv);

    camio_selector_t* selector = camio_selector_new(options.selector,NULL,NULL);

    perf_mon = camio_perf_init(options.perf_out, 128 * 1024);
    con_listener = camio_iostream_new(options.stream.items[0],NULL, NULL, perf_mon);
    selector->insert(selector,&con_listener->selector,CONLISTEN);

    uint8_t* buff = NULL;
    size_t len = 0;
    size_t which = ~0;

    int i = 0;
    for(;i < STATIC_CONTENT_SIZE; i++){
        static_content[i] = ' ' + i%(126-32);
    }
    static_content[STATIC_CONTENT_SIZE -1] = '\0';


    while(selector->count(selector)){

        //Wait for some input
        which = selector->select(selector);
        if(which == CONLISTEN){
                len = con_listener->start_read(con_listener,&buff);

                camio_iostream_tcp_params_t params = { .listen = 0, .fd = *(int*)buff };
                iostream = camio_iostream_delimiter_new( camio_iostream_new("tcp",NULL,&params, perf_mon) , http_delimiter, NULL) ;

                //We use the integer value of the iostream pointer as its identifier in the selector
                selector->insert(selector,&iostream->selector,(size_t)iostream);
                //printf("[0x%016lx] new stream\n", (size_t)iostream);
                con_listener->end_read(con_listener, NULL);
        }
        else{
            iostream = (camio_iostream_t*)which;
            len = iostream->start_read(iostream,&buff);
            //printf("[0x%016lx] stream has %lu bytes data\n", (size_t)iostream, len);

            if(len == 0){
                iostream->end_read(iostream, NULL);
                selector->remove(selector, which);
                iostream->delete(iostream);
                //printf("[0x%016lx] stream deleted\n", (size_t)iostream);
                continue;
            }
            http_decode(buff,len);
            iostream->end_read(iostream, NULL);
        }

    }

    term(0);

    //Unreachable
    return 0;
}

