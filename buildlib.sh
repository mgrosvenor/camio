#! /bin/bash

./makeinclude
cake camio.c $@ --append-CXXFLAGS="-D_GNU_SOURCE" --variant=release --static-library
gcc libcamio_cat.c -o bin/libcamio_cat -L bin -lcamio -lpthread -lrt 




