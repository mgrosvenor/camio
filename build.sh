#! /bin/sh

cake apps/camio_cat.c $@ --append-CFLAGS="-D_GNU_SOURCE" --begintests tests/test_num_parser.c --endtests
cake apps/camio_chat.c $@
cake apps/camio_httpd.c $@  
#cake apps/camio_perf.c $@
cake apps/camio_tp_bench.c $@

#./buildlib.sh

