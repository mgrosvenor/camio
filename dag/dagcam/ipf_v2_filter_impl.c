
#include "ipf_v2_filter_impl.h"
#include "infiniband_proto.h"
#include "dag_config_api.h"
#include "ipf_v2_parse.h"
#include "idt52k_lib.h"

//segments supported in the TCAM
#define MAX_SEGMENTS 8  
#define MAX_POSSIBLE_RULES 32768

#define IPF_MAX_RULESETS 2

//TODO....???
#define IPF_SHM_MAGIC_NUMBER 0x3a0bb782
#define IPF_SHM_KEY 0x397eed17

typedef struct
{
    int max_rules;
    int rule_width;
    ipf_capabilities_t ipf_cap;
    uint8_t is_interface_based_segment; /* a boolean value . = 1 if ports init'ed to more than 1 ports*/
    FilterStateHeaderPtr ptr_filter_header;
    dag_card_ref_t card_ref;
    unified_filter_linktype_t ppp_indicator;
}ipf_v2_filter_private_state_t;

typedef struct 
{
	uint32_t magic_number;
    uint32_t previous_ports;
	uint32_t previous_rulesets;

} shm_ipf_v2_state_t;
#pragma pack(1)

typedef struct ipf_576bit_tcam_entry
{
    uint64_t ruleset:1;
    uint64_t interface_id:2;
    uint64_t ppp_indicator:1;
    uint64_t l2_proto:16;
    uint64_t mpls_top:32;
    uint64_t mpls_bottom:32;
    uint64_t label_cnt:3;
    uint64_t src_ip_addr_lower:64;
    uint64_t src_ip_addr_higher:64;
    uint64_t dst_ip_addr_lower:64;
    uint64_t dst_ip_addr_higher:64;
    uint64_t ip_protocol:8;
    uint64_t src_port:16;
    uint64_t dst_port:16;
    uint64_t tcp_flags:6;
    uint8_t reserved[24]; //for 187 bits dummy
}ipf_576bit_tcam_entry_t;


typedef struct ipf_144bit_tcam_entry
{
    uint32_t src_ip_address:32;
    uint32_t dst_ip_address:32;
    uint32_t src_port:16;
    uint32_t dst_port:16;
    uint32_t ip_protocol:8;
    uint32_t tcp_flags:6;
    uint32_t ether_type:16;
    uint32_t reserved:16;
    uint32_t ruleset:1;
    uint32_t iface:1;
}ipf_144bit_tcam_entry_t;

typedef struct ipf_288bit_tcam_entry
{
    uint64_t ruleset:1;
    uint64_t interface_id:2;
    uint64_t ppp_indicator:1;
    uint64_t l2_proto:16;
    uint64_t mpls_top:32;
    uint64_t mpls_bottom:32;
    uint64_t label_cnt:3;
    uint64_t src_ip_addr:32;
    uint64_t dst_ip_addr:32;
    uint64_t ip_protocol:8;
    uint64_t src_port:16;
    uint64_t dst_port:16;
    uint64_t tcp_flags:6;
    uint8_t reserved[12]; //for 91 bits dummy
}ipf_288bit_tcam_entry_t;


#pragma  pack () 
/* virtual functions to be assigned */
static unified_filter_error_t   ipf_v2_parse_file(unified_filter_handle_p, char*, unified_rule_list_t*);
static unified_filter_error_t   ipf_v2_write_rules(unified_filter_handle_p, unified_rule_list_t*);
static unified_filter_error_t   ipf_v2_cleanup_rules(unified_filter_handle_p, unified_rule_list_t*);
static unified_filter_error_t   ipf_v2_dispose_filter(unified_filter_handle_p);
static unified_filter_error_t   ipf_v2_print_rule_list(unified_filter_handle_p handle, unified_rule_list_t *in_list, FILE* out_stream);
static unified_filter_error_t   ipf_v2_enable_ruleset(unified_filter_handle_p handle, int iface, int ruleset);
static unified_filter_error_t   ipf_v2_configure_filter(unified_filter_handle_p handle, unified_filter_param_t *param);

/* internal functions */
static unified_filter_error_t ipf_v2_initialize_tcam( ipf_v2_filter_private_state_t *filter_state);
unified_filter_error_t read_verfiy_144bits_rules(ipf_v2_filter_private_state_t *filter_state, unified_rule_list_t *list);
unified_filter_error_t read_verfiy_576bits_rules(ipf_v2_filter_private_state_t *filter_state, unified_rule_list_t *list);
unified_filter_error_t ipf_v2_read_verify_rule_list(unified_filter_handle_p handle, unified_rule_list_t *in_list);
static int find_width_for_this_rule(ipf_v2_filter_rule_t *rule, ipf_capabilities_t *cap);
static void print_rule(int rule_number, ipf_v2_filter_rule_t *in_rule, FILE *out_stream);
static uint8_t get_ipf_boolean_attribute_value( dag_card_ref_t card_ref, dag_component_t comp, dag_attribute_code_t code);
static void set_ipf_boolean_attribute_value( dag_card_ref_t card_ref, dag_component_t comp, dag_attribute_code_t code, uint8_t value);
static uint8_t get_ipf_uint32_attribute_value( dag_card_ref_t card_ref, dag_component_t comp, dag_attribute_code_t code);
static void set_ipf_uint32_attribute_value( dag_card_ref_t card_ref, dag_component_t comp, dag_attribute_code_t code, uint32_t value);

static int fill_ifp_v2_filtering_capabilities ( ipf_v2_filter_private_state_t* filter_state);
static void retrieve_previous_ipfv2_state(shm_ipf_v2_state_t* previous_stage);
static void store_current_ipfv2_state(shm_ipf_v2_state_t* stored_stage);
static uint8_t validate_current_iface_ruleset( unified_filter_handle_p handle);
static unified_filter_error_t write_144bit_rules_list(ipf_v2_filter_private_state_t *filter_state, unified_rule_list_t *list);
static unified_filter_error_t write_576bit_rules_list(ipf_v2_filter_private_state_t *filter_state, unified_rule_list_t *list);

/* encode and decode functions*/
static int  ipf_v2_rule_encode_144bit(ipf_v2_filter_rule_t* rule, ipf_144bit_tcam_entry_t *tcam_data, ipf_144bit_tcam_entry_t *tcam_mask);
int  ipf_v2_rule_decode_144bit(ipf_v2_filter_rule_t* rule,ipf_144bit_tcam_entry_t *tcam_data, ipf_144bit_tcam_entry_t *tcam_mask);
static int  ipf_v2_rule_encode_576bit(ipf_v2_filter_rule_t* rule,ipf_576bit_tcam_entry_t* tcam_data,ipf_576bit_tcam_entry_t* tcam_mask);
int  ipf_v2_rule_decode_576bit(ipf_v2_filter_rule_t* rule,ipf_576bit_tcam_entry_t* tcam_data,ipf_576bit_tcam_entry_t* tcam_mask);
int  ipf_v2_rule_encode_288bit(ipf_v2_filter_rule_t* rule,ipf_288bit_tcam_entry_t* tcam_data,ipf_288bit_tcam_entry_t* tcam_mask);
int  ipf_v2_rule_decode_288bit(ipf_v2_filter_rule_t* rule,ipf_288bit_tcam_entry_t* tcam_data,ipf_288bit_tcam_entry_t* tcam_mask);

unified_filter_error_t  ipf_v2_filter_creator(unified_filter_handle_p handle, unified_filter_param_t *in_params)
{
    ipf_v2_filter_private_state_t *filter_state = NULL;
    shm_ipf_v2_state_t  store_state;
    uint8_t *iom = 0;
    if ( NULL == handle )
        return kUnifiedFilterGeneralError;
    dagutil_verbose_level(1,"Creating IPF V2 filter object\n");
    filter_state = (ipf_v2_filter_private_state_t *) malloc ( sizeof(ipf_v2_filter_private_state_t));
    memset( filter_state, 0, sizeof (ipf_v2_filter_private_state_t) );

    filter_state->max_rules = MAX_POSSIBLE_RULES;

    /* set function pointers and private state */
    set_private_state(handle,(void*)filter_state);
    set_parse_file_function( handle, ipf_v2_parse_file);
    set_cleanup_rules_function(handle, ipf_v2_cleanup_rules);
    set_dispose_filter_function(handle, ipf_v2_dispose_filter);
    set_print_rule_function(handle, ipf_v2_print_rule_list);

   
    if( 0 ==  get_is_parse_only(handle))
    {
        set_write_rules_function(handle, ipf_v2_write_rules);
        set_enable_ruleset_function(handle, ipf_v2_enable_ruleset);
        set_read_verify_filter_function(handle , ipf_v2_read_verify_rule_list);

        /* create and initialize ptr_filter_header */
        filter_state->ptr_filter_header = (FilterStateHeaderPtr) malloc ( sizeof(FilterStateHeader));
        memset( filter_state->ptr_filter_header, 0, sizeof(FilterStateHeader));
        if( (iom = get_iom (handle)) != NULL )
        {
            int count = 0;
            dag_reg_t    regs[DAG_REG_MAX_ENTRIES];
            count = dag_reg_find((char*) iom, DAG_REG_PPF, regs);
            if ( count > 0 )
            {
                filter_state->ptr_filter_header->ipf_register_base =  (iom + regs[0].addr);
                /* check for version here .. */
                if ( regs[0].version != 3 )
                {
                    dagutil_warning("IPF version %d does not support IPF V2 filtering \n",regs[0].version);
                    //ipf_v2_dispose_filter(handle);
                    //return kUnifiedFilterDAGOpenError;
                }
            }
            count = 0;
            count = dag_reg_find((char*) iom, DAG_REG_IDT_TCAM, regs);
            if ( count > 0 )
            {
                filter_state->ptr_filter_header->copro_register_base = (uint32_t*) (iom + regs[0].addr);
            }
            
            /* initialize card_ref */
            filter_state->card_ref = dag_config_init(get_dagname(handle));
            if ( NULL == filter_state->card_ref )
            {
                dagutil_error("Could not initialize the card reference \n");
                ipf_v2_dispose_filter(handle);
                return kUnifiedFilterGeneralError;
            }
            if ( !fill_ifp_v2_filtering_capabilities(filter_state) )
		    {
                dagutil_warning(" fill_ifp_v2_filtering_capabilities failed \n");
		    }
        }

	    if( 1 == get_is_initialze(handle))
        {
            /* set the configure function */
            set_configure_filter_function(handle , ipf_v2_configure_filter);

            filter_state->ptr_filter_header->interface_number = get_init_interfaces(handle);
            /* check the ruleset count exceeds what is supported by IPF v2 */
            if ( get_init_rulesets(handle) > IPF_MAX_RULESETS )
            {
                dagutil_warning("IPF V2 filter does not supported %d rulesets. Making it to max=%d\n",get_init_rulesets(handle), IPF_MAX_RULESETS);
                set_init_rulesets(handle, IPF_MAX_RULESETS);
            }
            filter_state->ptr_filter_header->rule_set = get_init_rulesets(handle) ;

           /* Get the TCAM width from the input params */
            filter_state->rule_width = in_params->tcam_width ;
            
            if ( kUnifiedFilterSuccess != ipf_v2_initialize_tcam(filter_state ))
            {
                dagutil_error("TCAM Init failed \n");
                ipf_v2_dispose_filter(handle);
                return kUnifiedFilterGeneralError;
            }
            /* init successfull. So Save the current state */
            memset( &store_state, 0 , sizeof (shm_ipf_v2_state_t));
            store_state.previous_ports =  (filter_state->ptr_filter_header)?(filter_state->ptr_filter_header->interface_number):get_init_interfaces(handle);
            store_state.previous_rulesets = (filter_state->ptr_filter_header)?(filter_state->ptr_filter_header->rule_set):get_init_rulesets(handle);
            store_current_ipfv2_state(&store_state);
        }
        else
        {
            /* read previously initialized data */
            dagutil_verbose_level(2,"IPF V2 filter, Retrieving the previous initialized state.. \n");
            memset( &store_state, 0 , sizeof (shm_ipf_v2_state_t));
            retrieve_previous_ipfv2_state(&store_state);

            filter_state->ptr_filter_header->interface_number   = store_state.previous_ports;
            filter_state->ptr_filter_header->rule_set           = store_state.previous_rulesets;
            set_init_rulesets(handle, store_state.previous_rulesets);
            set_init_interfaces(handle , store_state.previous_ports);

            /* get the previously initialized TCAM width */
            filter_state->rule_width =  idt52k_get_tcam_mode(filter_state->ptr_filter_header);
            if ( (144 != filter_state->rule_width)  && (576 != filter_state->rule_width) )
            {
                dagutil_error("Invalid TCAM rule width - %d. Please re-initialize \n", filter_state->rule_width);
                ipf_v2_dispose_filter(handle);
                return kUnifiedFilterInvalidParameter;
            }
        }

         /* validate current interface and current ruleset */
        if ( 0 == validate_current_iface_ruleset(handle))
        {
            ipf_v2_dispose_filter(handle);
            return kUnifiedFilterInvalidParameter;
        }
        
        filter_state->is_interface_based_segment = (get_init_interfaces ( handle ) > 1 )? 1:0;

        /* update the max rules allowed*/
        filter_state->max_rules = MAX_POSSIBLE_RULES / (((get_init_interfaces(handle) > 0)? get_init_interfaces(handle) :1) * ( (get_init_rulesets(handle) > 0)? get_init_rulesets(handle):1)  * (filter_state->rule_width / 144));
        dagutil_verbose_level(1,"IPF V2: The max Rules supported would be %d\n",filter_state->max_rules);

        /* get the ppp indicator from the params */
        filter_state->ppp_indicator = in_params->linktype;
        
        /* if not parse only, then the parser needs to be informed abt the IPF capabilities */
		set_ipf_capabiity( &(filter_state->ipf_cap));	
        /* set the current rule width */
        set_ipf_v2_rule_width( filter_state->rule_width );

    }
    dagutil_verbose_level(0,"IPF V2: Filter creation is complete\n");
    return kUnifiedFilterSuccess;
}

unified_filter_error_t ipf_v2_parse_file(unified_filter_handle_p handle, char *file_name, unified_rule_list_t *list)
{
    FILE *fin = NULL;
	int retval = 0;
    ipf_v2_filter_private_state_t *filter_state = NULL;
    if ( kIPFV2Filter != get_filter_type(handle) )
    {
        dagutil_error("Invalid Filtertype(%d) for ipf_v2 filter in %s\n",get_filter_type(handle),__FUNCTION__);
        return kUnifiedFilterInvalidFilterType;
    }

    filter_state = (ipf_v2_filter_private_state_t*)get_private_state(handle);
    if( NULL == filter_state)
    {
        dagutil_error("Unable to get the private state of IPF filter\n");
        return kUnifiedFilterGeneralError;
    }
    fin = fopen(file_name,"r");
	if( fin == NULL )
	{
		dagutil_error("Filter file is missing or no access\n");
		return kUnifiedFilterFileOpenError;
	}
 
    /* assume the rule width is 144 bit */
    list->rule_width = 144;

    ipf_v2restart(fin);
    memset( &ipf_v2_filter_rule, 0, sizeof (ipf_v2_filter_rule));
    while(1)
	{
        ipf_v2_filter_rule_t *this_rule = NULL;
        unified_rule_node_p this_node = NULL;
        retval = ipf_v2lex();
		if(retval == T_RULE_DONE)
		{
            if( list->count >= filter_state->max_rules )
            {
                dagutil_warning("Number of rules exceeded the maximum allowed-%d\n",filter_state->max_rules);
                continue;
            }
            ipf_v2_filter_rule.rule_set = get_current_rule_set(handle);
#if defined(_WIN32) 
			list->rule_width = max ( list->rule_width,  find_width_for_this_rule(&ipf_v2_filter_rule, &(filter_state->ipf_cap)));
#else
            list->rule_width = MAX ( list->rule_width,  find_width_for_this_rule(&ipf_v2_filter_rule, &(filter_state->ipf_cap)));
#endif            
            /* if not parse only and if the current interface is given, override iface with the current iface*/
            if ( (0 == get_is_parse_only(handle)) && (-1 != get_current_iface( handle)) )
            {
                if (0 != ipf_v2_filter_rule.iface.mask)
                {
                    dagutil_warning("Overriding the iface of rule %d to %d\n",list->count +1, get_current_iface( handle) );
                }
                ipf_v2_filter_rule.iface.data = (uint8_t) get_current_iface( handle) ;
                ipf_v2_filter_rule.iface.mask= 0x03;
            }

            /* if not parse only and the iface given in the rule file is grater thaan the init'ed iface */
            if ( (0 == get_is_parse_only(handle)) && (ipf_v2_filter_rule.iface.data  > get_init_interfaces( handle ) - 1) ) 
            {
                dagutil_error ( "Invalid iface for rule #%d\n",list->count + 1 );
                return kUnifiedFilterInvalidParameter;
            }

            /* start adding the rule to the list . Allocate rule and node for holding the rule */
            this_rule = (ipf_v2_filter_rule_t*) malloc ( sizeof(ipf_v2_filter_rule_t));
            this_node = (unified_rule_node_p) malloc ( sizeof(unified_rule_node_t));
            if ( (NULL == this_rule)  || (NULL == this_node))
            {
                dagutil_error("malloc Error in Rule parsing.\n");
                return kUnifiedFilterGeneralError;
            }
            memset(this_node , 0 , sizeof (unified_rule_node_t));
		  //copy the parsed rule into the ruleset
            memcpy( this_rule,&ipf_v2_filter_rule,sizeof(ipf_v2_filter_rule) );
            this_node->generic_rule = (unified_rule_p) this_rule;
		    if (NULL != list->head)
            {
                list->tail->next = this_node;
                list->tail = this_node;
            }
            else
            {
                list->head = this_node;
                list->tail = this_node;
            }
            list->count++;
		}
		else if (retval == T_RULE_CONTINUE)
		{
			printf("This state is unused please contact suppot@endace.com \
 and send the rule file used and this line print out. retval: %d rules_count:%d\n",retval,list->count);
		}
		else if ( retval < 0 )
		{
			printf(" errors flex returns: %d at rule: %d\n",retval,list->count);
			break;
		}
		else if (retval == 0)
		{
			break;
			
		} else {
			printf("Unknown state please contact suppot@endace.com \
 and send the rule file used and this line print out. retval: %d rules_count:%d\n",retval,list->count);
		}
    /* make the extern variable ready for next rule */
    memset( &ipf_v2_filter_rule, 0, sizeof (ipf_v2_filter_rule));
	}	
    if ( list->count > 0)
    {
        list->filter_type = kIPFV2Filter;
    }
	fclose(fin);
    if ( 0 == retval)
    {
        dagutil_verbose_level(0,"Ruleset file parsed successifully, %d rules have been created.Width to accomodate the rule is %d bits\n", list->count,list->rule_width);
        return kUnifiedFilterSuccess;
    }
    else
    {
        return kUnifiedFilterParseError;
    }
}

unified_filter_error_t ipf_v2_write_rules(unified_filter_handle_p handle, unified_rule_list_t *list)
{
    ipf_v2_filter_private_state_t *filter_state = NULL;
    unified_filter_error_t ret_val = kUnifiedFilterSuccess;

    if ( kIPFV2Filter != get_filter_type(handle) || (kIPFV2Filter != list->filter_type))
    {
        dagutil_error("Invalid Filtertype(%d) for ipf_v2 filter in %s\n",get_filter_type(handle),__FUNCTION__);
        return kUnifiedFilterInvalidFilterType;
    }

    filter_state = (ipf_v2_filter_private_state_t*)get_private_state(handle);
    if( NULL == filter_state)
    {
        dagutil_error("Unable to get the private state of IPF filter in %s\n",__FUNCTION__);
        return kUnifiedFilterGeneralError;
    }
    
    dagutil_verbose_level(1,"%s TCAM is configured in rule width %d  .Current Rule width %d\n", __FUNCTION__, filter_state->rule_width, list->rule_width);
    if( filter_state->rule_width < list->rule_width )
    {
        dagutil_error("TCAM's initialized rule width is smaller than the filter's rule width \n");
        return kUnifiedFilterInvalidParameter; 
    }
    if( 144 == filter_state->rule_width )
    {
        ret_val =  write_144bit_rules_list(filter_state, list);
    }
    else if ( 576 == filter_state->rule_width )
    {
        ret_val =  write_576bit_rules_list(filter_state, list);
    }
    else
    {
        /* should not reach here */
        dagutil_error("Unknown rule width: %d in %s\n",filter_state->rule_width, __FUNCTION__);
        ret_val = kUnifiedFilterInvalidParameter;
    }
    if ( kUnifiedFilterSuccess == ret_val )
    {
        /* assign the list's width to the written width - would be useful in verify */
        list->rule_width = filter_state->rule_width;
        dagutil_verbose_level(0, "Rules have been successfully written into the TCAM \n");
    }
    return ret_val;
}

unified_filter_error_t ipf_v2_cleanup_rules(unified_filter_handle_p handle, unified_rule_list_t *list)
{
    unified_rule_node_p current = NULL , next = NULL;
    dagutil_verbose_level(1,"in %s .Rule count is %d\n",__FUNCTION__,list->count);
    if ( kIPFV2Filter != get_filter_type(handle) )
    {
        dagutil_error("Invalid Filtertype(%d) for ipf_v2 filter in %s\n",get_filter_type(handle),__FUNCTION__);
        return kUnifiedFilterInvalidFilterType;
    }
    current = list->head;
    while( current)
    {
        free ( current->generic_rule);
        next = current->next;
        free (current);
        current = next;
    }
    list->head = NULL;
    list->tail = NULL;
    list->count = 0;
    return kUnifiedFilterSuccess;
}

unified_filter_error_t ipf_v2_dispose_filter(unified_filter_handle_p handle)
{
    ipf_v2_filter_private_state_t *filter_state = NULL;
    if ( kIPFV2Filter != get_filter_type(handle) )
    {
        dagutil_error("Invalid Filtertype(%d) for ipf_v2 filter in %s\n",get_filter_type(handle),__FUNCTION__);
        return kUnifiedFilterInvalidFilterType;
    }

    filter_state = (ipf_v2_filter_private_state_t*)get_private_state(handle);
    if( NULL == filter_state)
    {
        dagutil_error("Unable to get the private state of IPF filter\n");
        return kUnifiedFilterGeneralError;
    }
    
    /* dispose the card_ref */
    if ( NULL != filter_state->card_ref)
    {
        dag_config_dispose(filter_state->card_ref);
    }

    /* remove the filter state's malloc-ed memory here */
    if ( filter_state->ptr_filter_header)
    {
        free(filter_state->ptr_filter_header);
        filter_state->ptr_filter_header = NULL;
    }
    return kUnifiedFilterSuccess;
}

unified_filter_error_t ipf_v2_print_rule_list(unified_filter_handle_p handle, unified_rule_list_t *in_list, FILE* out_stream)
{
    unified_rule_node_p current = NULL;
    ipf_v2_filter_rule_t *ipf_rule = NULL;
    int rule_count = 0;
    if ( kIPFV2Filter != get_filter_type(handle) )
    {
        dagutil_error("Invalid Filtertype(%d) for ipf_v2 filter in %s\n",get_filter_type(handle),__FUNCTION__);
        return kUnifiedFilterInvalidFilterType;
    }

    if( NULL == in_list )
    {
        dagutil_error("NULL rule list in %s\n",__FUNCTION__);
        return kUnifiedFilterInvalidParameter;
    }
    
    if ( NULL == out_stream )
    {
        dagutil_error("NULL out stream in  %s\n",__FUNCTION__);
        return kUnifiedFilterInvalidParameter;
    }
    if ( in_list->count <= 0 )
    {
        dagutil_warning("No Rules be printed \n");
    }
    
    current = in_list->head;
    while( current)
    {
        ipf_rule = (ipf_v2_filter_rule_t*) current->generic_rule;
        if( NULL == ipf_rule )
        {
            dagutil_verbose_level(3,"Skipping NULL rule in %s\n", __FUNCTION__);
            continue;
        }
        rule_count++;
        print_rule(rule_count, ipf_rule,out_stream);
        current = current->next;
    }
    return kUnifiedFilterSuccess;
}
unified_filter_error_t ipf_v2_enable_ruleset(unified_filter_handle_p handle, int iface, int ruleset)
{
    ipf_v2_filter_private_state_t *filter_state = NULL;
    dag_component_t root_component = 0, ipf_component = 0;
    if ( kIPFV2Filter != get_filter_type(handle) )
    {
        dagutil_error("Invalid Filtertype(%d) for ipf_v2 filter in %s\n",get_filter_type(handle),__FUNCTION__);
        return kUnifiedFilterInvalidFilterType;
    }

    filter_state = (ipf_v2_filter_private_state_t*)get_private_state(handle);
    if( NULL == filter_state)
    {
        dagutil_error("Unable to get the private state of IPF filter\n");
        return kUnifiedFilterGeneralError;
    }
    
    if ( ruleset > (get_init_rulesets(handle) - 1) )
    {
        dagutil_error("Invalid ruleset. Should be less than %d\n", get_init_rulesets(handle));
        return kUnifiedFilterInvalidParameter;
    }
    
    if ( iface > (get_init_interfaces(handle) - 1) )
    {
        dagutil_error("Invalid interface. Should be less than %d\n", get_init_interfaces(handle));
        return kUnifiedFilterInvalidParameter;
    }
    
    if ( NULL == filter_state->card_ref)
    {
        dagutil_error(" Card ref NULL in %s\n",__FUNCTION__);
        return kUnifiedFilterGeneralError;
    }
    root_component = dag_config_get_root_component(filter_state->card_ref);
    if ( NULL == root_component )
    {
        dagutil_error(" root_component  NULL in %s\n",__FUNCTION__);
        return kUnifiedFilterGeneralError;
    }
    ipf_component = dag_component_get_subcomponent(root_component, kComponentIPF, 0 );
    if ( NULL == ipf_component )
    {
        dagutil_error(" ipf_component NULL in %s\n",__FUNCTION__);
        return kUnifiedFilterGeneralError;
    }
    switch ( iface)
    {
        case 0:
            set_ipf_boolean_attribute_value(filter_state->card_ref, ipf_component, kBooleanAttributeIPFRulesetInterface0, (uint8_t) ruleset); 
            break;
        case 1:
            set_ipf_boolean_attribute_value(filter_state->card_ref, ipf_component, kBooleanAttributeIPFRulesetInterface1, (uint8_t) ruleset); 
            break;
        case 2:
            set_ipf_boolean_attribute_value(filter_state->card_ref, ipf_component, kBooleanAttributeIPFRulesetInterface2, (uint8_t) ruleset); 
            break;
        case 3:
            set_ipf_boolean_attribute_value(filter_state->card_ref, ipf_component, kBooleanAttributeIPFRulesetInterface3, (uint8_t) ruleset);
            break;
        default:
            dagutil_error("IPF does not support more than 4 interface \n");
            break;
    }
    return kUnifiedFilterSuccess;
}

unified_filter_error_t ipf_v2_read_verify_rule_list(unified_filter_handle_p handle, unified_rule_list_t *in_list)
{
    ipf_v2_filter_private_state_t *filter_state = NULL;
    int tcam_width = 0;
    
    if ( kIPFV2Filter != get_filter_type(handle) || (kIPFV2Filter != in_list->filter_type))
    {
        dagutil_error("Invalid Filtertype(%d) for ipf_v2 filter in %s\n",get_filter_type(handle),__FUNCTION__);
        return kUnifiedFilterInvalidFilterType;
    }

    filter_state = (ipf_v2_filter_private_state_t*)get_private_state(handle);
    if( NULL == filter_state)
    {
        dagutil_error("Unable to get the private state of IPF filter in %s\n",__FUNCTION__);
        return kUnifiedFilterGeneralError;
    }
    tcam_width =  idt52k_get_tcam_mode(filter_state->ptr_filter_header);
    if ( tcam_width != in_list->rule_width)
    {
        dagutil_error("%s: TCAM was configured in rule width %d  .Current Rule in_list width %d\n", __FUNCTION__, tcam_width, in_list->rule_width);
        return kUnifiedFilterInvalidParameter;
    }

    if( 144 == tcam_width )
    {
        return read_verfiy_144bits_rules(filter_state, in_list);
    }
    else if ( 576 == tcam_width )
    {
        return read_verfiy_576bits_rules(filter_state, in_list);
    }

    /* should not reach here */
    dagutil_error("Unknown rule width: %d in %s\n",tcam_width, __FUNCTION__);
    return kUnifiedFilterInvalidParameter;
}

unified_filter_error_t read_verfiy_144bits_rules(ipf_v2_filter_private_state_t *filter_state, unified_rule_list_t *list)
{
    /* encode and write into TCAM */
    ipf_144bit_tcam_entry_t data;
    ipf_144bit_tcam_entry_t mask;
    uint32_t color_entry;
    uint32_t  database = 0;
    int index_array[MAX_SEGMENTS] = {0};
    ipf_v2_filter_rule_t *current_rule ;
    unified_rule_node_p current = NULL ;
    ipf_v2_filter_rule_t rule_read;
    uint32_t read_color_entry = 0;
    unified_filter_error_t ret_val = kUnifiedFilterSuccess;

    current = list->head;
    
    while(NULL != current)
    {
        memset(&rule_read, 0 , sizeof( ipf_v2_filter_rule_t));
        current_rule = (ipf_v2_filter_rule_t*)(current->generic_rule);
        color_entry = current_rule->user_class &0x03;
        color_entry |= current_rule->user_tag << 2;
        if ( filter_state->is_interface_based_segment )
        {
            database = (current_rule->iface.data * filter_state->ptr_filter_header->rule_set ) + (current_rule->rule_set);
            index_array[database]++;
        }
        else
        {
            database = current_rule->rule_set;
            index_array[database]++;
        }
        if ( 0 != tcam_read_cam_144_entry_ex(filter_state->ptr_filter_header, database, index_array[database]-1, (uint8_t*)&data,(uint8_t*)&mask, &read_color_entry) )
        {
            ret_val  = kUnifiedFilterTCAMReadError;
            dagutil_error("TCAM read failed for Rule: %d\n",index_array[database]-1);
            break;
        }
        rule_read.user_class = read_color_entry & 0x3;
        rule_read.user_tag   = (read_color_entry >> 2 ) & 0xffff;
        /* just make use of memcmp. assiging the fields which cud not be assumed from  tcam data */
        rule_read.action     = current_rule->action;
        rule_read.ip_address_type = current_rule->ip_address_type;
        ipf_v2_rule_decode_144bit(&rule_read, &data, &mask);
        if ( 0 != memcmp(&rule_read, current_rule , sizeof(ipf_v2_filter_rule_t)) )
        {
            dagutil_error("Verfiy failed for Rule :%d \n",index_array[database]-1);
            dagutil_error("Rule read frm TCAM: \n");
            print_rule(index_array[database]-1, &rule_read, stderr);
            dagutil_error("Rule in the list: \n");
            print_rule(index_array[database]-1, current_rule, stderr);
            ret_val = kUnifiedFilterInvalidParameter;
            break; 
        }
        current = current->next;
    }
    return ret_val;
}

unified_filter_error_t read_verfiy_576bits_rules(ipf_v2_filter_private_state_t *filter_state, unified_rule_list_t *list)
{
    /* encode and write into TCAM */
    ipf_576bit_tcam_entry_t data;
    ipf_576bit_tcam_entry_t mask;
    uint32_t color_entry;
    uint32_t  database = 0;
    int index_array[MAX_SEGMENTS] = {0};
    ipf_v2_filter_rule_t *current_rule ;
    unified_rule_node_p current = NULL ;
    ipf_v2_filter_rule_t rule_read;
    uint32_t read_color_entry = 0;
    unified_filter_error_t ret_val = kUnifiedFilterSuccess;

    current = list->head;
    
    while(NULL != current)
    {
        memset(&rule_read, 0 , sizeof( ipf_v2_filter_rule_t));
        current_rule = (ipf_v2_filter_rule_t*)(current->generic_rule);
        color_entry = current_rule->user_class &0x03;
        color_entry |= current_rule->user_tag << 2;
        if ( filter_state->is_interface_based_segment )
        {
            database = (current_rule->iface.data * filter_state->ptr_filter_header->rule_set ) + (current_rule->rule_set);
            index_array[database]++;
        }
        else
        {
            database = current_rule->rule_set;
            index_array[database]++;
        }
        if ( 0 != tcam_read_cam_576_entry_ex(filter_state->ptr_filter_header, database, index_array[database]-1, (uint8_t*)&data,(uint8_t*)&mask, &read_color_entry) )
        {
            ret_val  = kUnifiedFilterTCAMReadError;
            dagutil_error("TCAM read failed for Rule: %d\n",index_array[database]-1);
            break;
        }
        rule_read.user_class = read_color_entry & 0x3;
        rule_read.user_tag   = (read_color_entry >> 2 ) & 0xffff;
        /* just make use of memcmp. assiging the fields which cud not be assumed from  tcam data */
        rule_read.action     = current_rule->action;
        rule_read.ip_address_type = current_rule->ip_address_type;
        ipf_v2_rule_decode_576bit(&rule_read, &data, &mask);
        /* mpls and vlan tags share the same bits, so another work around to use memcmp */
        if ( (current_rule->mpls_bottom.mask != 0) || (current_rule->mpls_top.mask != 0))
        {
           rule_read.vlan_1.data = 0;
           rule_read.vlan_1.mask= 0;
           rule_read.vlan_2.data = 0;
           rule_read.vlan_2.mask= 0;
        }
        else
        {
            rule_read.mpls_top.data= 0;
            rule_read.mpls_top.mask = 0;
            rule_read.mpls_bottom.data= 0;
            rule_read.mpls_bottom.mask = 0;
        }

        if ( 0 != memcmp(&rule_read, current_rule , sizeof(ipf_v2_filter_rule_t)) )
        {
            dagutil_error("Verfiy failed for Rule :%d \n",index_array[database]-1);
            dagutil_error("Rule read frm TCAM: \n");
            print_rule(index_array[database]-1, &rule_read, stderr);
            dagutil_error("Rule in the list: \n");
            print_rule(index_array[database]-1, current_rule, stderr);
            ret_val = kUnifiedFilterInvalidParameter;
            break; 
        }
        current = current->next;
    }
    return ret_val;
}

unified_filter_error_t write_144bit_rules_list(ipf_v2_filter_private_state_t *filter_state, unified_rule_list_t *list)
{
    /* encode and write into TCAM */
    ipf_144bit_tcam_entry_t data;
    ipf_144bit_tcam_entry_t mask;
    uint32_t color_entry;
    uint32_t  database = 0;
    int index_array[MAX_SEGMENTS] = {0};
    ipf_v2_filter_rule_t *current_rule ;
    unified_rule_node_p current = NULL ;
    unified_filter_error_t ret_val = kUnifiedFilterSuccess;

    current = list->head;
    
    while(NULL != current)
    {
        current_rule = (ipf_v2_filter_rule_t*)(current->generic_rule);
        ipf_v2_rule_encode_144bit(current_rule,&data,&mask);
        color_entry = current_rule->user_class &0x03;
        color_entry |= current_rule->user_tag << 2;
        if ( filter_state->is_interface_based_segment )
        {
            database = (current_rule->iface.data * filter_state->ptr_filter_header->rule_set ) + (current_rule->rule_set);
            index_array[database]++;
        }
        else
        {
            database = current_rule->rule_set;
            index_array[database]++;
        }
        dagutil_verbose_level(3, " In %s Writing into TCAM: DB %d Index %d\n", __FUNCTION__, database, index_array[database]-1);
        if ( 0 != tcam_write_cam_144_entry_ex(filter_state->ptr_filter_header, database, index_array[database]-1, (uint8_t*)&data,(uint8_t*)&mask, color_entry) )
        {
            ret_val  = kUnifiedFilterTCAMWriteError;
            dagutil_error("TCAM write failed for Rule: %d\n",index_array[database]-1);
            break;
        }
        current = current->next;
    }
    return ret_val;
}
unified_filter_error_t write_576bit_rules_list(ipf_v2_filter_private_state_t *filter_state, unified_rule_list_t *list)
{
    /* encode into 576  and write into TCAM */
    ipf_576bit_tcam_entry_t data;
    ipf_576bit_tcam_entry_t mask;
    uint32_t color_entry;
    uint32_t  database = 0;
    int index_array[MAX_SEGMENTS] = {0};
    ipf_v2_filter_rule_t *current_rule ;
    unified_rule_node_p current = NULL ;
    unified_filter_error_t ret_val = kUnifiedFilterSuccess;

    current = list->head;
    
    while(NULL != current)
    {
        current_rule = (ipf_v2_filter_rule_t*)(current->generic_rule);
        ipf_v2_rule_encode_576bit(current_rule,&data,&mask);
        /* if the ppp indicator is given in cmd line, fill it here */
        if ( (kLinkEthernet == filter_state->ppp_indicator ) || (kLinkHDLC == filter_state->ppp_indicator) )
        {
            data.ppp_indicator = 0;
            mask.ppp_indicator = 1;
        }
        else if (kLinkPPP == filter_state->ppp_indicator )
        {
            data.ppp_indicator = 1;
            mask.ppp_indicator = 1;
        }
        color_entry = current_rule->user_class &0x03;
        color_entry |= current_rule->user_tag<< 2;
        if ( filter_state->is_interface_based_segment )
        {
            database = (current_rule->iface.data * filter_state->ptr_filter_header->rule_set ) + (current_rule->rule_set);
            index_array[database]++;
        }
        else
        {
            database = current_rule->rule_set;
            index_array[database]++;
        }
        dagutil_verbose_level(3, " In %s Writing into TCAM: DB %d Index %d\n", __FUNCTION__, database, index_array[database]-1);
        if ( 0 !=tcam_write_cam_576_entry_ex(filter_state->ptr_filter_header, database, index_array[database]-1, (uint8_t*)&data,(uint8_t*)&mask, color_entry) )
        {
            ret_val  = kUnifiedFilterTCAMWriteError;
            dagutil_error("TCAM write failed for Rule: %d\n",index_array[database]-1);
            break;
        }
        current = current->next;
    }
    return ret_val;
}
int find_width_for_this_rule(ipf_v2_filter_rule_t *rule, ipf_capabilities_t *cap)
{
    /* no rule means 0 length */
    if (NULL == rule)
        return 0;
    if( (k128Bit == rule->ip_address_type) )
        return 576;
    /* if mpls or vlan are given (giving dont' cares is as good as not mentioning), it must be 576 bit*/
    if ((0 != rule->mpls_top.mask) || (0 != rule->mpls_bottom.mask) )
        return 576;
    if ( (0 != rule->vlan_1.data) || (0 != rule->vlan_2.mask) )
        return 576;
    if( NULL != cap )
    {
        if ( cap->width576_supported && !(cap->mpls_skipping_supported) )
        {
            return 576;
        }
        if ( cap->width576_supported  && !(cap->vlan_skipping_supported) )
        {
            return 576;
        }
    }
    return 144;
}

uint8_t get_ipf_boolean_attribute_value( dag_card_ref_t card_ref, dag_component_t comp, dag_attribute_code_t code)
{
    attr_uuid_t current_attribute = 0;
    current_attribute = dag_component_get_attribute_uuid( comp, code );
    if (current_attribute )
    {
        return dag_config_get_boolean_attribute(card_ref, current_attribute);
    }
    return 0;
}

uint8_t get_ipf_uint32_attribute_value( dag_card_ref_t card_ref, dag_component_t comp, dag_attribute_code_t code)
{
    attr_uuid_t current_attribute = 0;
    current_attribute = dag_component_get_attribute_uuid( comp, code );
    if (current_attribute )
    {
        return dag_config_get_uint32_attribute(card_ref, current_attribute);
    }
    return 0;
}

void set_ipf_boolean_attribute_value( dag_card_ref_t card_ref, dag_component_t comp, dag_attribute_code_t code, uint8_t value)
{
    attr_uuid_t current_attribute = 0;
    current_attribute = dag_component_get_attribute_uuid( comp, code );
    if (current_attribute )
    {
        dag_config_set_boolean_attribute(card_ref, current_attribute, value);
        return;
    }
    return ;
}
void set_ipf_uint32_attribute_value( dag_card_ref_t card_ref, dag_component_t comp, dag_attribute_code_t code, uint32_t value)
{
    attr_uuid_t current_attribute = 0;
    current_attribute = dag_component_get_attribute_uuid( comp, code );
    if (current_attribute )
    {
        dag_config_set_uint32_attribute(card_ref, current_attribute, value);
        return;
    }
    return ;
}
int fill_ifp_v2_filtering_capabilities (ipf_v2_filter_private_state_t *filter_state)
{
    dag_component_t root_component = 0, ipf_component = 0;
    ipf_capabilities_t *cap = NULL;
    uint32_t width_capability = 0;
    if ( NULL == filter_state)
    {
        return 0;
    }
    if ( NULL == filter_state->card_ref )
    {
        return 0;
    }
    cap = &(filter_state->ipf_cap);
    memset ( cap, 0, sizeof (ipf_capabilities_t));

    root_component = dag_config_get_root_component(filter_state->card_ref);
    if ( NULL == root_component )
    {
        return 0;
    }
    ipf_component = dag_component_get_subcomponent(root_component, kComponentIPF, 0 );
    if ( NULL == ipf_component )
    {
        return 0;
    }
    cap->ipv4_supported             = get_ipf_boolean_attribute_value (filter_state->card_ref, ipf_component, kBooleanAttributeIPFV4Support);
    cap->ipv6_supported             = get_ipf_boolean_attribute_value (filter_state->card_ref, ipf_component, kBooleanAttributeIPFV6Support);
    cap->vlan_skipping_supported    = get_ipf_boolean_attribute_value (filter_state->card_ref, ipf_component, kBooleanAttributeVLANSkipping);
    cap->vlan_filtering_supported   = get_ipf_boolean_attribute_value (filter_state->card_ref, ipf_component, kBooleanAttributeVLANFiltering);
    cap->vlan_tag_supported         = get_ipf_boolean_attribute_value (filter_state->card_ref, ipf_component, kBooleanAttributeVLANTags);
    cap->mpls_skipping_supported    = get_ipf_boolean_attribute_value (filter_state->card_ref, ipf_component, kBooleanAttributeMPLSSkipping);
    cap->mpls_filtering_supported   = get_ipf_boolean_attribute_value (filter_state->card_ref, ipf_component, kBooleanAttributeMPLSFiltering);
    width_capability                = get_ipf_uint32_attribute_value (filter_state->card_ref, ipf_component, kUint32AttributeCurrentRuleWidth);
    cap->width576_supported         = width_capability & BIT0;
    cap->width288_supported         = width_capability & BIT1;
    return 1;
}

int  ipf_v2_rule_encode_144bit(ipf_v2_filter_rule_t* rule, ipf_144bit_tcam_entry_t *tcam_data, ipf_144bit_tcam_entry_t *tcam_mask)
{
    uint8_t *temp = NULL;
    int i = 0;
     /* initialize to Zeroes */
    memset(tcam_data, 0 , sizeof (ipf_144bit_tcam_entry_t));
    memset(tcam_mask, 0 , sizeof (ipf_144bit_tcam_entry_t));


    tcam_data->src_ip_address  =  ((uint32_t)(rule->src_ip.data[0]));
    tcam_mask->src_ip_address  =   ((uint32_t)(rule->src_ip.mask[0]));

    tcam_data->dst_ip_address  = ((uint32_t)(rule->dst_ip.data[0]));
    tcam_mask->dst_ip_address  = ((uint32_t)(rule->dst_ip.mask[0]));

    tcam_data->src_port =  (rule->src_port.data);
    tcam_mask->src_port =  (rule->src_port.mask);

    tcam_data->dst_port =  (rule->dst_port.data);
    tcam_mask->dst_port =  (rule->dst_port.mask);
    
    tcam_data->ip_protocol = rule->ip_prot.data;
    tcam_mask->ip_protocol = rule->ip_prot.mask;

    tcam_data->tcp_flags = rule->tcp_flags.data;
    tcam_mask->tcp_flags = rule->tcp_flags.mask;

    tcam_data->ether_type = (rule->l2_proto.data);
    tcam_mask->ether_type = (rule->l2_proto.mask);

    tcam_data->iface = rule->iface.data & 0x01;
    tcam_mask->iface = rule->iface.mask & 0x01;

    tcam_data->ruleset = rule->rule_set & 0x01;
    tcam_mask->ruleset = 1;
    if ( dagutil_get_verbosity () >2 )
    {
        printf("Encoded data\n");
        temp = (uint8_t*) tcam_data;
        for (i = 0; i < 18; i++)
        {
            printf("%02x ", temp[i]);
            if ( ((i + 1) % 9) == 0 ) printf("\n");
        }
    
        printf("Encoded mask\n");
        temp = (uint8_t*) tcam_mask;
        for (i = 0; i < 18; i++)
        {
            printf("%02x ", temp[i]);
            if ( ((i + 1) % 9) == 0 ) printf("\n");
        }
    }
    return 1;
}

int  ipf_v2_rule_decode_144bit(ipf_v2_filter_rule_t* rule,ipf_144bit_tcam_entry_t *tcam_data, ipf_144bit_tcam_entry_t *tcam_mask)
{
    uint8_t *temp = NULL;
    int i = 0;

    rule->src_ip.data[0] = ( tcam_data->src_ip_address);
    rule->src_ip.mask[0] = ( tcam_mask->src_ip_address);

    rule->dst_ip.data[0] = ( tcam_data->dst_ip_address);
    rule->dst_ip.mask[0] = ( tcam_mask->dst_ip_address);

    rule->src_port.data = (tcam_data->src_port);
    rule->src_port.mask = (tcam_mask->src_port);

    rule->dst_port.data = (tcam_data->dst_port);
    rule->dst_port.mask = (tcam_mask->dst_port);

    rule->ip_prot.data  = tcam_data->ip_protocol;
    rule->ip_prot.mask  = tcam_mask->ip_protocol;

    rule->tcp_flags.data = tcam_data->tcp_flags;
    rule->tcp_flags.mask = tcam_mask->tcp_flags;

    rule->l2_proto.data = (tcam_data->ether_type);
    rule->l2_proto.mask = (tcam_mask->ether_type);

    rule->iface.data = tcam_data->iface ;
    rule->iface.mask = tcam_mask->iface ;
    
    rule->rule_set = tcam_data->ruleset  & tcam_mask->ruleset;
     if ( dagutil_get_verbosity () > 2 )
    {
        printf("Decoded data\n");
        temp = (uint8_t*) tcam_data;
        for (i = 0; i < 18; i++)
        {
            printf("%02x ", temp[i]);
            if ( ((i + 1) % 9) == 0 ) printf("\n");
        }
    
        printf("Decoded mask\n");
        temp = (uint8_t*) tcam_mask;
        for (i = 0; i < 18; i++)
        {
            printf("%02x ", temp[i]);
            if ( ((i + 1) % 9) == 0 ) printf("\n");
        }
    }
    
    return 1;
}

int  ipf_v2_rule_encode_576bit(ipf_v2_filter_rule_t* rule,ipf_576bit_tcam_entry_t* tcam_data,ipf_576bit_tcam_entry_t* tcam_mask)
{
    uint8_t *temp = NULL;
    int i  = 0;

    /* initialize to Zeroes */
    memset(tcam_data, 0 , sizeof (ipf_576bit_tcam_entry_t) );
    memset(tcam_mask, 0 , sizeof (ipf_576bit_tcam_entry_t) );

    tcam_data->ruleset = rule->rule_set & 0x01;
    tcam_mask->ruleset = 1;

    tcam_data->interface_id = rule->iface.data;
    tcam_mask->interface_id = rule->iface.mask; 
    
    tcam_data->l2_proto = rule->l2_proto.data;
    tcam_mask->l2_proto = rule->l2_proto.mask;
/*
     ppp indicator is filled outside this function, as its not a part of the rule 
*/
    if ( ( rule->mpls_top.data != 0 ) || (rule->mpls_top.mask != 0) ) 
    {
       /* fill mpls top 32 bit value */
        tcam_data->mpls_top = (rule->mpls_top.data);
        tcam_mask->mpls_top = (rule->mpls_top.mask);
    }
    else  if ( ( rule->vlan_1.data != 0 ) || (rule->vlan_1.mask != 0) ) 
    {
        /* fill mpls top 16 bit value */
        tcam_data->mpls_top = (uint32_t)(rule->vlan_1.data);
        tcam_mask->mpls_top = (uint32_t)(rule->vlan_1.mask);
    }

    if ( ( rule->mpls_bottom.data != 0 ) || (rule->mpls_bottom.mask != 0) ) 
    {
       /* fill mpls top 32 bit value */
        tcam_data->mpls_bottom = (rule->mpls_bottom.data);
        tcam_mask->mpls_bottom = (rule->mpls_bottom.mask);
    }
    else  if ( ( rule->vlan_2.data != 0 ) || (rule->vlan_2.mask != 0) ) 
    {
        /* fill mpls top 16 bit value */
        tcam_data->mpls_bottom = (uint32_t) (rule->vlan_2.data);
        tcam_mask->mpls_bottom = (uint32_t)(rule->vlan_2.mask);
    }
    
    tcam_data->label_cnt = rule->label_cnt.data;
    tcam_mask->label_cnt = rule->label_cnt.mask;
    
    if ( k128Bit == rule->ip_address_type )
    {
        tcam_data->src_ip_addr_lower = (rule->src_ip.data[1]);
        tcam_data->src_ip_addr_higher = (rule->src_ip.data[0]);
        tcam_mask->src_ip_addr_lower = (rule->src_ip.mask[1]);
        tcam_mask->src_ip_addr_higher = (rule->src_ip.mask[0]);
    }
    else if ( k32Bit == rule->ip_address_type )
    {
        tcam_data->src_ip_addr_lower = ((uint32_t)rule->src_ip.data[0]);
        tcam_mask->src_ip_addr_lower = ((uint32_t)rule->src_ip.mask[0]);
    }

    if ( k128Bit == rule->ip_address_type )
    {
        tcam_data->dst_ip_addr_lower = (rule->dst_ip.data[1]);
        tcam_data->dst_ip_addr_higher = (rule->dst_ip.data[0]);
        tcam_mask->dst_ip_addr_lower = (rule->dst_ip.mask[1]);
        tcam_mask->dst_ip_addr_higher = (rule->dst_ip.mask[0]);
    }
    else if ( k32Bit == rule->ip_address_type )
    {
        tcam_data->dst_ip_addr_lower = ((uint32_t)rule->dst_ip.data[0]);
        tcam_mask->dst_ip_addr_lower = ((uint32_t)rule->dst_ip.mask[0]);
    }

    tcam_data->ip_protocol = rule->ip_prot.data;
    tcam_mask->ip_protocol =  rule->ip_prot.mask;
    
    tcam_data->src_port =  (rule->src_port.data);
    tcam_mask->src_port =  (rule->src_port.mask);

    tcam_data->dst_port =  (rule->dst_port.data);
    tcam_mask->dst_port =  (rule->dst_port.mask);
    
    tcam_data->tcp_flags = rule->tcp_flags.data;
    tcam_mask->tcp_flags = rule->tcp_flags.mask;

    if ( dagutil_get_verbosity () > 2 )
    {
        printf("Encoded data\n");
        temp = (uint8_t*) tcam_data;
        for (i = 0; i < 72; i++)
        {
            printf("%02x ", temp[i]);
            if ( ((i + 1) % 9) == 0 ) printf("\n");
        }
    
        printf("Encoded mask\n");
        temp = (uint8_t*) tcam_mask;
        for (i = 0; i < 72; i++)
        {
            printf("%02x ", temp[i]);
            if ( ((i + 1) % 9) == 0 ) printf("\n");
        }
    }
    
   return 1;
}


int  ipf_v2_rule_decode_576bit(ipf_v2_filter_rule_t* rule,ipf_576bit_tcam_entry_t* tcam_data,ipf_576bit_tcam_entry_t* tcam_mask)
{
    uint8_t *temp = NULL;
    int i  = 0;
    rule->iface.data = tcam_data->interface_id  ;
    rule->iface.mask = tcam_mask->interface_id  ;

    rule->rule_set = tcam_data->ruleset  & tcam_mask->ruleset;
    
    rule->l2_proto.data = (tcam_data->l2_proto) ;
    rule->l2_proto.mask = (tcam_mask->l2_proto);
    /* 32 bit value to mpls top */
    rule->mpls_top.data = (tcam_data->mpls_top);
    rule->mpls_top.mask = (tcam_mask->mpls_top);
    /* 16 bit value to Vlan - 1 */
    rule->vlan_1.data   =   ( (uint16_t) tcam_data->mpls_top) ;
    rule->vlan_1.mask   =   ( (uint16_t) tcam_mask->mpls_top) ;

    

    /* 32 bit value to mpls top */
    rule->mpls_top.data = (tcam_data->mpls_top);
    rule->mpls_top.mask = (tcam_mask->mpls_top);
    /* 16 bit value to Vlan - 1 */
    rule->vlan_1.data   =   ( (uint16_t) tcam_data->mpls_top) ;
    rule->vlan_1.mask   =   ( (uint16_t) tcam_mask->mpls_top) ;

      /* 32 bit value to mpls bottom*/
    rule->mpls_bottom.data = (tcam_data->mpls_bottom);
    rule->mpls_bottom.mask = (tcam_mask->mpls_bottom);
    /* 16 bit value to Vlan - 2 */
    rule->vlan_2.data   =   ( (uint16_t) tcam_data->mpls_bottom) ;
    rule->vlan_2.mask   =   ( (uint16_t) tcam_mask->mpls_bottom) ;
    
    
    rule->label_cnt.data = tcam_data->label_cnt;
    rule->label_cnt.mask = tcam_mask->label_cnt ;
    
    if ( k32Bit !=  rule->ip_address_type )
    {
        rule->src_ip.data[1] = (tcam_data->src_ip_addr_lower);
        rule->src_ip.data[0] = (tcam_data->src_ip_addr_higher);
        rule->src_ip.mask[1] = (tcam_mask->src_ip_addr_lower);
        rule->src_ip.mask[0] = (tcam_mask->src_ip_addr_higher);
        
        
        rule->dst_ip.data[1] = (tcam_data->dst_ip_addr_lower);
        rule->dst_ip.data[0] = (tcam_data->dst_ip_addr_higher);
        rule->dst_ip.mask[1] = (tcam_mask->dst_ip_addr_lower);
        rule->dst_ip.mask[0] = (tcam_mask->dst_ip_addr_higher);
    }
    else
    {
        rule->src_ip.data[0] = (tcam_data->src_ip_addr_lower);
        rule->src_ip.mask[0] = (tcam_mask->src_ip_addr_lower);

        rule->dst_ip.data[0] = (tcam_data->dst_ip_addr_lower);
        rule->dst_ip.mask[0] = (tcam_mask->dst_ip_addr_lower);
    }
    rule->ip_prot.data = tcam_data->ip_protocol ;
    rule->ip_prot.mask = tcam_mask->ip_protocol  ;
    
    rule->src_port.data =  (tcam_data->src_port);
    rule->src_port.mask =  (tcam_mask->src_port);

    rule->dst_port.data =  (tcam_data->dst_port);
    rule->dst_port.mask =  (tcam_mask->dst_port);
    
    rule->tcp_flags.data = tcam_data->tcp_flags;
    rule->tcp_flags.mask = tcam_mask->tcp_flags ;
 if ( dagutil_get_verbosity ()  > 2 )
    {
        printf("Decoded data\n");
        temp = (uint8_t*) tcam_data;
        for (i = 0; i < 72; i++)
        {
            printf("%02x ", temp[i]);
            if ( ((i + 1) % 9) == 0 ) printf("\n");
        }
    
        printf("Decoded mask\n");
        temp = (uint8_t*) tcam_mask;
        for (i = 0; i < 72; i++)
        {
            printf("%02x ", temp[i]);
            if ( ((i + 1) % 9) == 0 ) printf("\n");
        }
    }

    return 1;
}


void retrieve_previous_ipfv2_state(shm_ipf_v2_state_t* previous_state)
{
#if defined (__linux__) || defined (__FreeBSD__) || (defined(__APPLE__) && defined(__ppc__))
	int shared_segment_size = getpagesize();
	int segment_id = shmget(IPF_SHM_KEY, shared_segment_size, S_IRUSR | S_IWUSR);
#elif defined (_WIN32)
	FILE *fr = fopen("temp.dat", "r");
	char str[32];
#endif

    if ( NULL == previous_state)
    {
        dagutil_error("Error: NULL ptr in %s\n",__FUNCTION__);
        return;
    }
#if defined (__linux__) || defined (__FreeBSD__) || (defined(__APPLE__) && defined(__ppc__))

	if (-1 != segment_id)
	{
		/* Attach the shared memory segment. */
		char* shared_memory = (char*) shmat(segment_id, 0, 0);
		shm_ipf_v2_state_t* state = (shm_ipf_v2_state_t*) shared_memory;
			
		assert((uintptr_t)-1 != (uintptr_t)shared_memory);
		assert(IPF_SHM_MAGIC_NUMBER == state->magic_number);

		memcpy(previous_state, state, sizeof(shm_ipf_v2_state_t));
        dagutil_verbose_level(2,"IPF V2 Retrieving State: ports %d Ruleset %d \n", state->previous_ports, state->previous_rulesets);

		/* Detach the shared memory segment. */
		shmdt(shared_memory);
	}
	else
	{
		/* Shared memory does not exist, assume 1 port and 1 ruleset. */
        dagutil_warning("Gettting previous data failed.Assuming 1 port and 1 ruleset (only one database)\n");
		previous_state->previous_ports = 1;
		previous_state->previous_rulesets = 1;
	}
#elif defined (_WIN32)
	if (fr != NULL)
	{
		fscanf(fr,"%s",str);
		fscanf(fr,"%x",&(previous_state->previous_ports));
		fscanf(fr,"%s",str);
		/*CHECKME: ruleset_count is not a member of shm_bfs_state_t*/
//		fscanf(fr,"%x",&(previous_state->ruleset_count));
		fscanf(fr,"%s",str);
		fscanf(fr,"%s",str);
    	fscanf(fr,"%x",&(previous_state->magic_number));
		
		assert(SHM_MAGIC_NUMBER == previous_state->magic_number);
    }
	else
	{
		fprintf(stderr,"\nCannot open file for reading");
		previous_state->previous_ports = 1;
		/*CHECKME: ruleset_count is not a member of shm_bfs_state_t*/
//      previous_state->ruleset_count  = 1;
    }
#endif
    return;
}

void store_current_ipfv2_state(shm_ipf_v2_state_t* current_state)
{
#if defined (__linux__) || defined (__FreeBSD__) || (defined(__APPLE__) && defined(__ppc__))
	int shared_segment_size = getpagesize();
	int segment_id = shmget(IPF_SHM_KEY, shared_segment_size, IPC_CREAT | S_IRUSR | S_IWUSR);
#elif defined (_WIN32)
	FILE *fw=NULL;
	char str[32];
#endif

    if ( NULL == current_state)
    {
        dagutil_error("Error: NULL ptr in %s\n",__FUNCTION__);
        return;
    }
#if defined (__linux__) || defined (__FreeBSD__) || (defined(__APPLE__) && defined(__ppc__))
	if (-1 != segment_id)
	{
		/* Attach the shared memory segment. */
		char* shared_memory = (char*) shmat(segment_id, 0, 0);
		shm_ipf_v2_state_t* state = (shm_ipf_v2_state_t*) shared_memory;

		assert((uintptr_t)-1 != (uintptr_t)shared_memory);
			
		/* Write the values into the shared memory area. */
	    memcpy(state, current_state, sizeof(shm_ipf_v2_state_t));
        state->magic_number = IPF_SHM_MAGIC_NUMBER;

        dagutil_verbose_level(2,"IPF V2 Storing State: ports %d Ruleset %d\n", state->previous_ports, state->previous_rulesets);

		/* Detach the shared memory segment. */
		shmdt(shared_memory);
	}
	else
	{
		dagutil_error("could not store ports and rulesets\n");
	}
#elif defined (_WIN32)
	fw=fopen("temp.dat","w");
	if(fw!=NULL)
	{
		fprintf(fw,"ports: %x",current_state->previous_ports);
		fprintf(fw," rulesets: %x",current_state->previous_rulesets);
        fprintf(fw," magic_number: %x",current_state->magic_number);
		fclose(fw);
	}
	else
	{
		fprintf(stderr,"\nCannot open file for writing");
	}
#endif
    return;
}

void print_rule(int rule_number, ipf_v2_filter_rule_t *in_rule, FILE *out_stream)
{
    fprintf(out_stream,"Rule Number: %d\n", rule_number);
    fprintf( out_stream, "User tag: %d, action: %s, steering: %d \n", 
            in_rule->user_tag, (in_rule->action == 0)?"through":"drop" , in_rule->user_class );
    fprintf( out_stream, "iface 0x%04x,0x%04x \n",in_rule->iface.data, in_rule->iface.mask);
    fprintf( out_stream, "l2-proto 0x%04x,0x%04x\n",in_rule->l2_proto.data, in_rule->l2_proto.mask);
    fprintf( out_stream, "mpls-top 0x%04x,0x%04x\n",in_rule->mpls_top.data, in_rule->mpls_top.mask);
    fprintf( out_stream, "mpls-btm 0x%04x,0x%04x\n",in_rule->mpls_bottom.data, in_rule->mpls_bottom.mask);
    fprintf( out_stream, "vlan-1 0x%04x,0x%04x\n",in_rule->vlan_1.data, in_rule->vlan_1.mask);
    fprintf( out_stream, "vlan-2 0x%04x,0x%04x\n",in_rule->vlan_2.data, in_rule->vlan_2.mask);
    if ( k128Bit != in_rule->ip_address_type )
    {
        fprintf( out_stream, "dst-ip 0x%04x,0x%04x\n",(uint32_t)in_rule->dst_ip.data[0], (uint32_t)in_rule->dst_ip.mask[0]);
        fprintf( out_stream, "src-ip 0x%04x,0x%04x\n",(uint32_t) in_rule->src_ip.data[0], (uint32_t) in_rule->src_ip.mask[0]);
    }
    else 
    {
        fprintf( out_stream, "dst-ip128 0x%016"PRIx64"%016"PRIx64",0x%016"PRIx64"%016"PRIx64" \n",in_rule->dst_ip.data[0],in_rule->dst_ip.data[1], in_rule->dst_ip.mask[0],in_rule->dst_ip.mask[1]);
        fprintf( out_stream, "src-ip 0x%016"PRIx64"%016"PRIx64",0x%016"PRIx64"%016"PRIx64"\n",in_rule->src_ip.data[0],in_rule->src_ip.data[1], in_rule->src_ip.mask[0],in_rule->src_ip.mask[1]);
    }
    fprintf( out_stream, "ip-prot 0x%04x,0x%04x\n",in_rule->ip_prot.data, in_rule->ip_prot.mask);
    fprintf( out_stream, "src-port 0x%04x,0x%04x\n",in_rule->src_port.data, in_rule->src_port.mask);
    fprintf( out_stream, "dst-port 0x%04x,0x%04x\n",in_rule->dst_port.data, in_rule->dst_port.mask);
    fprintf( out_stream, "tcp-flags 0x%04x,0x%04x\n",in_rule->tcp_flags.data, in_rule->tcp_flags.mask);
    fprintf( out_stream, "label-cnt 0x%04x,0x%04x\n",in_rule->label_cnt.data, in_rule->label_cnt.mask);
    return;
}

unified_filter_error_t ipf_v2_configure_filter(unified_filter_handle_p handle, unified_filter_param_t *param)
{
    ipf_v2_filter_private_state_t *filter_state = NULL;
    dag_component_t root_component = 0, ipf_component = 0;
    int temp_width = 0;
    if ( kIPFV2Filter != get_filter_type(handle) )
    {
        dagutil_error("Invalid Filtertype(%d) for ipf_v2 filter in %s\n",get_filter_type(handle),__FUNCTION__);
        return kUnifiedFilterInvalidFilterType;
    }

    filter_state = (ipf_v2_filter_private_state_t*)get_private_state(handle);
    if( NULL == filter_state)
    {
        dagutil_error("Unable to get the private state of IPF filter\n");
        return kUnifiedFilterGeneralError;
    }
 
    if ( NULL == filter_state->card_ref)
    {
        dagutil_error(" Card ref NULL in %s\n",__FUNCTION__);
        return kUnifiedFilterGeneralError;
    }
    root_component = dag_config_get_root_component(filter_state->card_ref);
    if ( NULL == root_component )
    {
        dagutil_error(" root_component  NULL in %s\n",__FUNCTION__);
        return kUnifiedFilterGeneralError;
    }
    ipf_component = dag_component_get_subcomponent(root_component, kComponentIPF, 0 );
    if ( NULL == ipf_component )
    {
        dagutil_error(" ipf_component NULL in %s\n",__FUNCTION__);
        return kUnifiedFilterGeneralError;
    }
    /* enable IPF */
    set_ipf_boolean_attribute_value(filter_state->card_ref, ipf_component, kBooleanAttributeIPFEnable, 1 );

    if (kMappingRxError == param->mapping)
    {
        set_ipf_boolean_attribute_value(filter_state->card_ref, ipf_component, kBooleanAttributeIPFUseRXError, 1 );
    }
    if (kMappingLossCntrl == param->mapping)
    {
        set_ipf_boolean_attribute_value(filter_state->card_ref, ipf_component, kBooleanAttributeIPFSelLctr, 1 );
    }
    if (kMappingPadOffset == param->mapping)
    {
        set_ipf_boolean_attribute_value(filter_state->card_ref, ipf_component, kBooleanAttributeIPFSelLctr, 0 );
    }

    set_ipf_boolean_attribute_value(filter_state->card_ref, ipf_component, kBooleanAttributeIPFDropEnable,(param->is_no_drop)?0:1 );

    temp_width = param->tcam_width;
    if ( 0 == temp_width)
    {
        /* try to get from filter state */
        temp_width = filter_state->rule_width;
    }
    
    if ( 576 == temp_width)
    {
        set_ipf_uint32_attribute_value(filter_state->card_ref, ipf_component,  kUint32AttributeCurrentRuleWidth, 1 );
    }
    else if ( 144 == temp_width)
    {
        set_ipf_boolean_attribute_value(filter_state->card_ref, ipf_component, kUint32AttributeCurrentRuleWidth, 0 );
    }
    else
    {
        dagutil_warning("Invalid Rule width in %s . Not setting the rule in classifier",__FUNCTION__);
    }

    return kUnifiedFilterSuccess;
}
unified_filter_error_t ipf_v2_initialize_tcam( ipf_v2_filter_private_state_t *filter_state)
{
    int err_code = 0;
    int tcam_mod = 0;
    if (576 == filter_state->rule_width)
    {
        if ( 0 == filter_state->ipf_cap.width576_supported)
        {
            dagutil_error("576 bit not supported.\n");
            return kUnifiedFilterInvalidParameter;
        }
        tcam_mod = 1;
    }
    else if (144 == filter_state->rule_width )
    {
        tcam_mod = 0;
    }
    else if ( 0 == filter_state->rule_width ) /* assume the user has not given it */
    {
        filter_state->rule_width = (filter_state->ipf_cap.width576_supported)? 576: 144;
        tcam_mod = 1;
        dagutil_warning("Invalid Rule Width in %s.Assuming it to be %d bit \n",__FUNCTION__,filter_state->rule_width);
    }
    else
    {
        dagutil_error("Invalid TCAM rule width for IPF filtering:%d \n", filter_state->rule_width);
        return kUnifiedFilterInvalidParameter;
    }
    /* make sure that header ptr is not null */
    if ( NULL != filter_state->ptr_filter_header)
    {
        dagutil_verbose_level(1, "Initializing TCAM interfaces: %d rulesets: %d width: %d \n", filter_state->ptr_filter_header->interface_number, filter_state->ptr_filter_header->rule_set, filter_state->rule_width);
        err_code = idt52k_tcam_initialise(filter_state->ptr_filter_header, tcam_mod);
    }
    else
    {
        dagutil_error("TCAM parameters not initialized. Initialization failed \n");
        return kUnifiedFilterInvalidParameter; 
    }
    if ( 0 == err_code )
    {
        return kUnifiedFilterSuccess;
    }
    return kUnifiedFilterGeneralError;
}

 uint8_t validate_current_iface_ruleset( unified_filter_handle_p handle)
{
    /* if the current iface is greater than init'ed inface count */
   if ( (get_current_iface( handle ) > (get_init_interfaces(handle) - 1)) )
    {
        dagutil_error("Invalid interface: %d. Should be less than init'ed value(%d)\n",get_current_iface( handle ), get_init_interfaces(handle));
        return 0;
    }
    /* if the current ruleset is greater than init'ed ruleset count */
    if ( get_current_rule_set( handle ) > (get_init_rulesets(handle) - 1) )
    {
        dagutil_error("Invalid ruleset: %d\n",get_current_rule_set( handle ));
        return 0;
    }
    /* if init`ed to more than one port and the the current interface is not specified */
    if ( (-1 == get_current_iface(handle) ) && ( get_init_interfaces( handle) > 1 ))
    {
        dagutil_error("Please specify an interface [0-%d] \n",get_init_interfaces( handle) -1 );
        return 0;
    }
    /* if init'ed to single port and the current port is specified */
    if ( (-1 != get_current_iface(handle) ) && ( get_init_interfaces( handle) == 1 ))
    {
        dagutil_error("Can not specifiy current port as it is initialized to a single port \n");
        return 0;
    }
    return 1;
 }

int  ipf_v2_rule_encode_288bit(ipf_v2_filter_rule_t* rule,ipf_288bit_tcam_entry_t* tcam_data,ipf_288bit_tcam_entry_t* tcam_mask)
{
    uint8_t *temp = NULL;
    int i  = 0;

    /* initialize to Zeroes */
    memset(tcam_data, 0 , sizeof (ipf_288bit_tcam_entry_t) );
    memset(tcam_mask, 0 , sizeof (ipf_288bit_tcam_entry_t) );

    tcam_data->ruleset = rule->rule_set & 0x01;
    tcam_mask->ruleset = 1;

    tcam_data->interface_id = rule->iface.data;
    tcam_mask->interface_id = rule->iface.mask; 
    
    tcam_data->l2_proto = rule->l2_proto.data;
    tcam_mask->l2_proto = rule->l2_proto.mask;
/*
     ppp indicator is filled outside this function, as its not a part of the rule 
*/
    if ( ( rule->mpls_top.data != 0 ) || (rule->mpls_top.mask != 0) ) 
    {
       /* fill mpls top 32 bit value */
        tcam_data->mpls_top = (rule->mpls_top.data);
        tcam_mask->mpls_top = (rule->mpls_top.mask);
    }
    else  if ( ( rule->vlan_1.data != 0 ) || (rule->vlan_1.mask != 0) ) 
    {
        /* fill mpls top 16 bit value */
        tcam_data->mpls_top = (uint32_t)(rule->vlan_1.data);
        tcam_mask->mpls_top = (uint32_t)(rule->vlan_1.mask);
    }

    if ( ( rule->mpls_bottom.data != 0 ) || (rule->mpls_bottom.mask != 0) ) 
    {
       /* fill mpls top 32 bit value */
        tcam_data->mpls_bottom = (rule->mpls_bottom.data);
        tcam_mask->mpls_bottom = (rule->mpls_bottom.mask);
    }
    else  if ( ( rule->vlan_2.data != 0 ) || (rule->vlan_2.mask != 0) ) 
    {
        /* fill mpls top 16 bit value */
        tcam_data->mpls_bottom = (uint32_t) (rule->vlan_2.data);
        tcam_mask->mpls_bottom = (uint32_t)(rule->vlan_2.mask);
    }
    
    tcam_data->label_cnt = rule->label_cnt.data;
    tcam_mask->label_cnt = rule->label_cnt.mask;
    
    tcam_data->src_ip_addr  =  ((uint32_t)(rule->src_ip.data[0]));
    tcam_mask->src_ip_addr  =   ((uint32_t)(rule->src_ip.mask[0]));

    tcam_data->dst_ip_addr  = ((uint32_t)(rule->dst_ip.data[0]));
    tcam_mask->dst_ip_addr  = ((uint32_t)(rule->dst_ip.mask[0]));

    tcam_data->ip_protocol = rule->ip_prot.data;
    tcam_mask->ip_protocol =  rule->ip_prot.mask;
    
    tcam_data->src_port =  (rule->src_port.data);
    tcam_mask->src_port =  (rule->src_port.mask);

    tcam_data->dst_port =  (rule->dst_port.data);
    tcam_mask->dst_port =  (rule->dst_port.mask);
    
    tcam_data->tcp_flags = rule->tcp_flags.data;
    tcam_mask->tcp_flags = rule->tcp_flags.mask;

    if ( dagutil_get_verbosity () > 2 )
    {
        printf("Encoded data\n");
        temp = (uint8_t*) tcam_data;
        for (i = 0; i < 72; i++)
        {
            printf("%02x ", temp[i]);
            if ( ((i + 1) % 9) == 0 ) printf("\n");
        }
    
        printf("Encoded mask\n");
        temp = (uint8_t*) tcam_mask;
        for (i = 0; i < 72; i++)
        {
            printf("%02x ", temp[i]);
            if ( ((i + 1) % 9) == 0 ) printf("\n");
        }
    }
    
   return 1;
}


int  ipf_v2_rule_decode_288bit(ipf_v2_filter_rule_t* rule,ipf_288bit_tcam_entry_t* tcam_data,ipf_288bit_tcam_entry_t* tcam_mask)
{
    uint8_t *temp = NULL;
    int i  = 0;
    rule->iface.data = tcam_data->interface_id  ;
    rule->iface.mask = tcam_mask->interface_id  ;

    rule->rule_set = tcam_data->ruleset  & tcam_mask->ruleset;
    
    rule->l2_proto.data = (tcam_data->l2_proto) ;
    rule->l2_proto.mask = (tcam_mask->l2_proto);
    /* 32 bit value to mpls top */
    rule->mpls_top.data = (tcam_data->mpls_top);
    rule->mpls_top.mask = (tcam_mask->mpls_top);
    /* 16 bit value to Vlan - 1 */
    rule->vlan_1.data   =   ( (uint16_t) tcam_data->mpls_top) ;
    rule->vlan_1.mask   =   ( (uint16_t) tcam_mask->mpls_top) ;

    

    /* 32 bit value to mpls top */
    rule->mpls_top.data = (tcam_data->mpls_top);
    rule->mpls_top.mask = (tcam_mask->mpls_top);
    /* 16 bit value to Vlan - 1 */
    rule->vlan_1.data   =   ( (uint16_t) tcam_data->mpls_top) ;
    rule->vlan_1.mask   =   ( (uint16_t) tcam_mask->mpls_top) ;

      /* 32 bit value to mpls bottom*/
    rule->mpls_bottom.data = (tcam_data->mpls_bottom);
    rule->mpls_bottom.mask = (tcam_mask->mpls_bottom);
    /* 16 bit value to Vlan - 2 */
    rule->vlan_2.data   =   ( (uint16_t) tcam_data->mpls_bottom) ;
    rule->vlan_2.mask   =   ( (uint16_t) tcam_mask->mpls_bottom) ;
    
    
    rule->label_cnt.data = tcam_data->label_cnt;
    rule->label_cnt.mask = tcam_mask->label_cnt ;
    
    rule->src_ip.data[0] = ( tcam_data->src_ip_addr);
    rule->src_ip.mask[0] = ( tcam_mask->src_ip_addr);

    rule->dst_ip.data[0] = ( tcam_data->dst_ip_addr);
    rule->dst_ip.mask[0] = ( tcam_mask->dst_ip_addr);

    rule->ip_prot.data = tcam_data->ip_protocol ;
    rule->ip_prot.mask = tcam_mask->ip_protocol  ;
    
    rule->src_port.data =  (tcam_data->src_port);
    rule->src_port.mask =  (tcam_mask->src_port);

    rule->dst_port.data =  (tcam_data->dst_port);
    rule->dst_port.mask =  (tcam_mask->dst_port);
    
    rule->tcp_flags.data = tcam_data->tcp_flags;
    rule->tcp_flags.mask = tcam_mask->tcp_flags ;
    if ( dagutil_get_verbosity ()  > 2 )
    {
        printf("Decoded data\n");
        temp = (uint8_t*) tcam_data;
        for (i = 0; i < 72; i++)
        {
            printf("%02x ", temp[i]);
            if ( ((i + 1) % 9) == 0 ) printf("\n");
        }
    
        printf("Decoded mask\n");
        temp = (uint8_t*) tcam_mask;
        for (i = 0; i < 72; i++)
        {
            printf("%02x ", temp[i]);
            if ( ((i + 1) % 9) == 0 ) printf("\n");
        }
    }

    return 1;
}
