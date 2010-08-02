
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <lo/lo.h>

#include "mapper_internal.h"
#include "types_internal.h"
#include "operations.h"
#include "expression.h"
#include <mapper/mapper.h>

/*void*/ int get_expr_Tree (Tree *T,char *expr);

mapper_router mapper_router_new(const char *host, int port, char *name)
{
    char str[16];
    mapper_router router = calloc(1,sizeof(struct _mapper_router));
    sprintf(str, "%d", port);
    router->addr = lo_address_new(host, str);
	router->target_name=strdup(name);

    if (!router->addr) {
        mapper_router_free(router);
        return 0;
    }
    return router;
}

void mapper_router_free(mapper_router router)
{
    if (router) {
        if (router->addr)
            lo_address_free(router->addr);
        if (router->mappings)
        {
            mapper_signal_mapping sm = router->mappings;
            while (sm)
            {
                mapper_signal_mapping tmp = sm->next;
                if (sm->mapping) {

                    mapper_mapping m = sm->mapping;
                    while (m) {
                        mapper_mapping tmp = m->next;
                        if (tmp->name)
                            free(tmp->name);
                        free(m);
                        m = tmp;
                     }
                }
                free(sm);
                sm = tmp;
            }
        }
        free(router);
    }
}

void mapper_router_receive_signal(mapper_router router, mapper_signal sig,
                                  mapper_signal_value_t *value)
{

    // find this signal in list of mappings
    mapper_signal_mapping sm = router->mappings;
    while (sm && sm->signal != sig)
        sm = sm->next;

    // exit without failure if signal is not mapped
    if (!sm)
		{
			return;
		}

    // for each mapping, construct a mapped signal and send it
    mapper_mapping m = sm->mapping;
	int c=1;
	int i=0;
	char *name;
    while (m)
    {
        struct _mapper_signal signal = *sig; 
        c=1;
		i=0;
		
		signal.name=strdup(m->name);

		/* The mapping name is the full name of the destination signal (/<device>/<param>) and we just need here the /<param> name to create the new signal*/
		/*name=strdup(m->name);
		while ((name)[c]!='/' && c<strlen(name))
			c++;
		if (c<strlen(name) && c>1)
			{
				while ((name)[c+i]!='\0')
					{				
						name[i]=(name)[c+i];
						i++;
					}
				signal.name=strndup(name,i);
				free(name);
			}*/

		mapper_signal_value_t v;
        mapper_mapping_perform(m, value, &v);
        mapper_router_send_signal(router, &signal, &v);
        m = m->next;
    }
}

void mapper_router_send_signal(mapper_router router, mapper_signal sig,
                               mapper_signal_value_t *value)
{

    int i;
    lo_message m;
    if (!router->addr) 	
		return;

    m = lo_message_new();
    if (!m) 
		return;

    for (i=0; i<sig->length; i++)
   	 	mval_add_to_message(m, sig, &value[i]);
	
    int send=lo_send_message(router->addr, sig->name, m);
    lo_message_free(m);
    return;
}

void mapper_router_add_mapping(mapper_router router, mapper_signal sig,
                               mapper_mapping mapping)
{
    // find signal in signal mapping list
    mapper_signal_mapping sm = router->mappings;
    while (sm && sm->signal != sig)
        sm = sm->next;

    // if not found, create a new list entry
    if (!sm) {
        sm = (mapper_signal_mapping)
            calloc(1,sizeof(struct _mapper_signal_mapping));
        sm->signal = sig;
        sm->next = router->mappings;
        router->mappings = sm;
    }

    // add new mapping to this signal's list
    mapping->next = sm->mapping;
    sm->mapping = mapping;
}


void mapper_router_remove_mapping(mapper_signal_mapping sm, mapper_mapping mapping)
{

    mapper_mapping *m = &sm->mapping;
    while (*m) {
        if (*m == mapping) {
            *m = mapping->next;
            break;
        }
        m = &(*m)->next;
    }
}


void mapper_router_add_direct_mapping(mapper_router router, mapper_signal sig, const char *name)
{
    mapper_mapping mapping =
        calloc(1,sizeof(struct _mapper_mapping));

    mapping->type = BYPASS;
    mapping->name = strdup(name);
	mapping->expression = strdup("y=x");
	mapping->use_range = 0;

    mapper_router_add_mapping(router, sig, mapping);
}

void mapper_router_add_linear_mapping(mapper_router router, mapper_signal sig,
                                      const char *name, float src_min, float src_max, float dest_min, float dest_max)
{
    mapper_mapping mapping =
        calloc(1,sizeof(struct _mapper_mapping));

    mapping->type=LINEAR;
    mapping->name = strdup(name);
	
	free(mapping->expression);		
	mapping->expression=malloc(256*sizeof(char));
	snprintf(mapping->expression, 256, "y=(x-%g)*%g+%g",
			 src_range_min,(dest_range_max-dest_range_min)/(src_range_max-src_range_min),dest_range_min);

	mapping->range[0] = src_min;
	mapping->range[1] = src_max;
	mapping->range[2] = dest_min;
	mapping->range[3] = dest_max;
	mapping->use_range = 1;

    Tree *T=NewTree();

    int success_tree=get_expr_Tree(T, expr);
	if (!success_tree)
		return;
		
    mapping->expr_tree=T;		

    /*mapping->coef_input[0] = scale.f;
    mapping->order_input = 1;

	mapping->expression = strdup(expression);
	mapping->range[0]=src_min;
	mapping->range[1]=src_max;
	mapping->range[2]=dest_min;
	mapping->range[3]=dest_max;*/


    mapper_router_add_mapping(router, sig, mapping);
}

void mapper_router_add_calibrate_mapping(mapper_router router, mapper_signal sig,
                                      const char *name, float dest_min, float dest_max)
{
    mapper_mapping mapping =
	calloc(1,sizeof(struct _mapper_mapping));
	
    mapping->type = CALIBRATE;
    mapping->name = strdup(name);
	
	mapping->range[2] = dest_min;
	mapping->range[3] = dest_max;
	mapping->use_range = 0;
	mapping->rewrite = 1;
	
    Tree *T=NewTree();
	
    int success_tree=get_expr_Tree(T, expr);
	if (!success_tree)
		return;
	
    mapping->expr_tree=T;		
	
    /*mapping->coef_input[0] = scale.f;
	 mapping->order_input = 1;
	 
	 mapping->expression = strdup(expression);
	 mapping->range[0]=src_min;
	 mapping->range[1]=src_max;
	 mapping->range[2]=dest_min;
	 mapping->range[3]=dest_max;*/
	
	
    mapper_router_add_mapping(router, sig, mapping);
}

void mapper_router_add_expression_mapping(mapper_router router, mapper_signal sig,
                                      const char *name, char *expr)
{
    mapper_mapping mapping =
        calloc(1,sizeof(struct _mapper_mapping));

    mapping->type=EXPRESSION;
    mapping->name = strdup(name);
	mapping->expression = strdup(expr);
	mapping->use_range = 0;

    Tree *T=NewTree();
    get_expr_Tree(T, expr);
    mapping->expr_tree=T;

    mapper_router_add_mapping(router, sig, mapping);
}
