/*
 * Copyright  (C) Matthew P. Grosvenor, 2012, All Rights Reserved
 *
 * parse stream description options.
 *
 */

#include "camio_opt_parser.h"
#include "../errors/camio_errors.h"
#include "../parsing/numeric_parser.h"
#include "../parsing/bool_parser.h"

#include <stdlib.h>
#include <stdint.h>

struct camio_opt_t* get_next(struct camio_opt_t* opt){
    return opt->next;
}

int has_opts(struct camio_opt_t* opt){
    return opt != NULL;
}

struct camio_opt_t* get_opt(struct camio_opt_t* opt, const char* name){
    struct camio_opt_t* o;
    for(o = opt; o != NULL; o = get_next(o)){
        if(strcmp(o->name, name) == 0 ){
            return o;
        }
    }

    return NULL;
}


struct camio_opt_t* has_opt(struct camio_opt_t* opt, const char* name){
    return get_opt(opt,name);
}


int get_opt_num(struct camio_opt_t* opt, const char* name,  num_result_t* num ){
    if(!opt){
        eprintf_exit("Cannot parse empty options");
        return -1;
    }

    struct camio_opt_t* o = get_opt(opt,name);
    if(o == NULL){
        eprintf_exit("Could not find option name %s\n", name);
        return -1;
    }


   *num = parse_number(o->value, strlen(o->value));
   return 0;
}



int get_opt_uint(struct camio_opt_t* opt, const char* name, uint64_t* value_out){
    num_result_t num;
    get_opt_num(opt, name, &num);

    if(num.type != CAMIO_UINT64){
        eprintf_exit("Expected UINT64 but %lu not found\n", num.type);
        return -1;
    }

    *value_out = num.val_uint;
    return 0;
}


int get_opt_int(struct camio_opt_t* opt, const char* name, int64_t* value_out){
    num_result_t num;
    get_opt_num(opt, name, &num);

    if(num.type != CAMIO_INT64 && num.type != CAMIO_UINT64){
        eprintf_exit("Expected INT64 but %lu not found\n", num.type);
        return -1;
    }

    if(num.type == CAMIO_UINT64){
        *value_out = (int64_t)num.val_uint;
    }

    if(num.type == CAMIO_INT64){
        *value_out = num.val_int;
    }


    return 0;
}


int get_opt_double(struct camio_opt_t* opt, const char* name, double* value_out){
    num_result_t num;
    get_opt_num(opt, name, &num);

    if(num.type != CAMIO_INT64 && num.type != CAMIO_UINT64 && num.type != CAMIO_DOUBLE){
        eprintf_exit("Expected DOUBLE but %lu not found\n", num.type);
        return -1;
    }

    if(num.type == CAMIO_INT64){
        *value_out = (double)num.val_int;
    }

    if(num.type == CAMIO_UINT64){
        *value_out = (double)num.val_uint;
    }

    if(num.type == CAMIO_DOUBLE){
        *value_out = num.val_dble;
    }

    return 0;
}


int get_opt_bool(struct camio_opt_t* opt, const char* name, int* value_out){
    if(!opt){
        eprintf_exit("Cannot parse empty options");
        return -1;
    }

    struct camio_opt_t* o = get_opt(opt,name);
    if(o == NULL){
        eprintf_exit("Could not find option name %s\n", name);
        return -1;
    }


   num_result_t num = parse_bool(o->value, strlen(o->value), 0);

   if(num.type != CAMIO_INT64){
       eprintf_exit("Expected INT64 (BOOL) but %lu not found\n", num.type);
       return 0;
   }


   *value_out = (int)num.val_int;
   return 0;

}

int get_opt_string(struct camio_opt_t* opt, const char* name, char** value_out){
    if(!opt){
        eprintf_exit("Cannot parse empty options");
        return -1;
    }

    struct camio_opt_t* o = get_opt(opt,name);
    if(o == NULL){
        eprintf_exit("Could not find option name %s\n", name);
        return -1;
    }

    *value_out = opt->value;
    return 0;

}


