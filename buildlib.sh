#! /bin/bash

./makeinclude > .tmp.makeincludeout
rm -rf .tmp.makeincludeout
cake camio.c $@ --append-CXXFLAGS="-D_GNU_SOURCE" --variant=release --static-library
gcc apps/libcamio_cat.c -o bin/camio_cat -L bin -lcamio -lpthread -lrt 




