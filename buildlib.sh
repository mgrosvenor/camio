#! /bin/bash

cake camio.c $@ --append-CXXFLAGS="-D_GNU_SOURCE" --variant=release --static-library
gcc libcamio_cat.c -L bin -lcamio -lpthread -lrt -o libcamio_cat




