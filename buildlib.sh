#! /bin/bash

./makeinclude > .tmp.makeincludeout
rm -rf .tmp.makeincludeout
cake camio.c $@ --append-CFLAGS="-D_GNU_SOURCE -DLIBCAMIO" --variant=release --static-library
cake apps/camio_cat.c --append-CFLAGS="-I ./include -D LIBCAMIO" --append-LINKFLAGS="-Lbin -lcamio -lrt -lpthread" $@ 
