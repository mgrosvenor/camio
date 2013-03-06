#! /bin/bash

cake apps/camio_cat.c $@ --append-CFLAGS="-D_GNU_SOURCE" --begintests tests/test_num_parser.c --endtests
cake apps/camio_chat.c $@ 

#./buildlib.sh

