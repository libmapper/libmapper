
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <lo/lo.h>

#include "mapper_internal.h"
#include "types_internal.h"
#include "operations.h"
#include "expression.h"
#include <mapper/mapper.h>

void get_expr_Tree (Tree *T);

mapper_router mapper_router_new(const char *host, int port, char *name)
{
    char str[16];
    mapper_router router = calloc(1,sizeof(struct _mapper_router));
    sprintf(str, "%d", port);
    router->addr = lo_address_new(host, str);
	strcpy(router->target_name, name);

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

	/*printf("ENTREE MAPPER_ROUTER_RECEIVE_SIGNAl\n");*/
    // find this signal in list of mappings
    mapper_signal_mapping sm = router->mappings;
    while (sm && sm->signal != sig)
        sm = sm->next;

    // exit without failure if signal is not mapped
    if (!sm)
		{
			/*printf("Signal a updater n'est pas mappé dans le router !!\n");*/
			return;
		}

	/*else printf("signal bien mappé dans le router\n");*/

    // for each mapping, construct a mapped signal and send it
    mapper_mapping m = sm->mapping;
    while (m)
    {
        struct _mapper_signal signal = *sig; 
        

		signal.name = m->name;
		printf("RECEIVE : SIGNAL NAME = %s\n", signal.name);  
		/**************************************************************************************************************************/
		int c=1;
		int i=0;
		char name[1024];
		while ((signal.name)[c]!='/' && c<strlen(signal.name))
			c++;
		if (c<strlen(signal.name) && c>1)
			{
				while ((signal.name)[c+i]!='\0')
					{				
						name[i]=(signal.name)[c+i];
						i++;
					}
				name[c+i]='\0';
				signal.name =name;
			}
		/**************************************************************************************************************************/
		printf("RECEIVE : SIGNAL NAME = %s\n", signal.name);       
		mapper_signal_value_t v;
        mapper_mapping_perform(m, value, &v);
        mapper_router_send_signal(router, &signal, &v);
        m = m->next;
    }
}

void mapper_router_send_signal(mapper_router router, mapper_signal sig,
                               mapper_signal_value_t *value)
{

	/*printf("ENTREE MAPPER_ROUTER_SEND_SIGNAl\n");*/

    int i;
    lo_message m;
    if (!router->addr) 
		{	
			/*printf("pas d'adresse dans le router\n");*/
			return;
		}
    /*else 
		{	
			printf("il y a bien une adresse dans le router\n");
		}*/

    m = lo_message_new();
    if (!m) return; /*else printf("message bien créé\n");*/

    for (i=0; i<sig->length; i++) {
    mval_add_to_message(m, sig, &value[i]);
	printf("%f bien ajouté au message\n", value->f);}

	int send;
printf("!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!  %s  !!!!!!!!!!!!!!!!!!!!!!!!!!!!\n",sig->name);
    /***********************************************************/send=lo_send_message(router->addr, sig->name, m);/**********************************************************/

	printf("%s\n\n",send==-1?"ECHEC ENVOI MESSAGE":"SUCCES ENVOI MESSAGE");
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

void mapper_router_add_direct_mapping(mapper_router router, mapper_signal sig,
                                      const char *name)
{
    mapper_mapping mapping =
        calloc(1,sizeof(struct _mapper_mapping));

    mapping->type=BYPASS;
    mapping->name = strdup(name);

    mapper_router_add_mapping(router, sig, mapping);
}

void mapper_router_add_linear_mapping(mapper_router router, mapper_signal sig,
                                      const char *name, mapper_signal_value_t scale)
{
    mapper_mapping mapping =
        calloc(1,sizeof(struct _mapper_mapping));

    mapping->type=LINEAR;
    mapping->name = strdup(name);
    mapping->coef_input[0] = scale.f;
    mapping->order_input = 1;

    mapper_router_add_mapping(router, sig, mapping);
}

void mapper_router_add_expression_mapping(mapper_router router, mapper_signal sig,
                                      const char *name)
{
    mapper_mapping mapping =
        calloc(1,sizeof(struct _mapper_mapping));

    mapping->type=EXPRESSION;
    mapping->name = strdup(name);

    Tree *T=NewTree();
    get_expr_Tree(T);
    mapping->expr_tree=T;

    mapper_router_add_mapping(router, sig, mapping);
}
