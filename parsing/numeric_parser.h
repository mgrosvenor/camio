/*
 * numeric_parser.h
 *
 *  Created on: Dec 20, 2012
 *      Author: mgrosvenor
 */

#ifndef NUMERIC_PARSER_H_
#define NUMERIC_PARSER_H_

#include <stdint.h>
#include "../camio_types.h"

typedef struct{
    camio_types_e type;
    union{
        uint64_t val_uint;
        int64_t  val_int;
        double   val_dble;
    };
} num_result_t;


num_result_t parse_number(const char* c, size_t i);


#endif /* NUMERIC_PARSER_H_ */
