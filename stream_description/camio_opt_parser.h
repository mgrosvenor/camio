 /*
 * Copyright  (C) Matthew P. Grosvenor, 2012, All Rights Reserved
 *
 * parse stream description options.
 *
 */

#ifndef CAMIO_OPT_PARSER_H_
#define CAMIO_OPT_PARSER_H_

#include "camio_descr.h"

int has_opts(struct camio_opt_t* opts);
struct camio_opt_t* has_opt(struct camio_opt_t* opts, const char* name);

int get_opt_uint(struct camio_opt_t* opt, const char* name, uint64_t* value_out);
int get_opt_int(struct camio_opt_t* opt, const char* name, int64_t* value_out);
int get_opt_double(struct camio_opt_t* opt, const char* name, double* value_out);
int get_opt_bool(struct camio_opt_t* opt, const char* name, int* value_out);
int get_opt_string(struct camio_opt_t* opt, const char* name, char** value_out);


#endif /* CAMIO_OPT_PARSER_H_ */
