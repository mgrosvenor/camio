#! /bin/bash

cake apps/camio_cat.c $@ --append-CXXFLAGS="-D_GNU_SOURCE" --begintests tests/test_num_parser.c --endtests

./buildlib.sh

