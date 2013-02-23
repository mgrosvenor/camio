 /*
 * Copyright  (C) Matthew P. Grosvenor, 2012, All Rights Reserved
 *
 * parse stream description options.
 *
 */

#ifndef CAMIO_OPT_PARSER_H_
#define CAMIO_OPT_PARSER_H_

#include "camio_descr.h"

int camio_descr_has_opts(struct camio_opt_t* opts);
struct camio_opt_t* camio_descr_has_opt(struct camio_opt_t* opts, const char* name);

int camio_descr_get_opt_uint(struct camio_opt_t* opt, uint64_t* value_out);
int camio_descr_get_opt_int(struct camio_opt_t* opt, int64_t* value_out);
int camio_descr_get_opt_double(struct camio_opt_t* opt, double* value_out);
int camio_descr_get_opt_bool(struct camio_opt_t* opt, int* value_out);
int camio_descr_get_opt_string(struct camio_opt_t* opt, char** value_out);


#endif /* CAMIO_OPT_PARSER_H_ */
