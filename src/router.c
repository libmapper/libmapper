
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <lo/lo.h>

#include "mapper_internal.h"
#include "types_internal.h"
#include "operations.h"
#include "expression.h"
#include <mapper/mapper.h>

mapper_router mapper_router_new(mapper_device device, const char *host,
                                int port, const char *name)
{
    char str[16];
    mapper_router router = (mapper_router) calloc(1, sizeof(struct _mapper_router));
    sprintf(str, "%d", port);
    router->addr = lo_address_new(host, str);
    router->dest_name = strdup(name);
    router->device = device;

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
        if (router->mappings) {
            mapper_signal_mapping sm = router->mappings;
            while (sm) {
                mapper_signal_mapping tmp = sm->next;
                if (sm->mapping) {

                    mapper_mapping m = sm->mapping;
                    while (m) {
                        mapper_mapping tmp = m->next;
                        if (tmp->props.src_name)
                            free(tmp->props.src_name);
                        if (tmp->props.dest_name)
                            free(tmp->props.dest_name);
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
    if (!sm) {
        return;
    }
    // for each mapping, construct a mapped signal and send it
    mapper_mapping m = sm->mapping;
    int c = 1;
    int i = 0;
    while (m) {
        struct _mapper_signal signal = *sig;
        c = 1;
        i = 0;

        signal.props.name = strdup(m->props.dest_name);

        mapper_signal_value_t v, w;
        if (mapper_mapping_perform(m, value, &v))
            if (mapper_clipping_perform(m, &v, &w))
                mapper_router_send_signal(router, &signal, &w);
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

    for (i = 0; i < sig->props.length; i++)
        mval_add_to_message(m, sig, &value[i]);

    lo_send_message(router->addr, sig->props.name, m);
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
            calloc(1, sizeof(struct _mapper_signal_mapping));
        sm->signal = sig;
        sm->next = router->mappings;
        router->mappings = sm;
    }
    // add new mapping to this signal's list
    mapping->next = sm->mapping;
    sm->mapping = mapping;
}

int mapper_router_remove_mapping(mapper_router router, 
								 mapper_mapping mapping)
{
    // find signal in signal mapping list
    mapper_signal_mapping sm = router->mappings;
    while (sm) {
        mapper_mapping *m = &sm->mapping;
        while (*m) {
            if (*m == mapping) {
                *m = mapping->next;
                free(mapping);
                return 0;
            }
            m = &(*m)->next;
        }
        sm = sm->next;
    }
    return 1;
}

mapper_mapping mapper_router_add_blank_mapping(mapper_router router,
                                               mapper_signal sig,
                                               const char *name)
{
    mapper_mapping mapping = (mapper_mapping) calloc(1, sizeof(struct _mapper_mapping));

    // Some default values?
    mapping->props.src_name = strdup(sig->props.name);
    mapping->props.dest_name = strdup(name);

    mapper_router_add_mapping(router, sig, mapping);
    return mapping;
}

mapper_mapping mapper_router_add_direct_mapping(mapper_router router,
                                      mapper_signal sig, const char *name)
{
    mapper_mapping mapping = (mapper_mapping) calloc(1, sizeof(struct _mapper_mapping));
    mapping->props.src_name = strdup(sig->props.name);
    mapping->props.dest_name = strdup(name);
    mapping->props.range.known = 0;

    mapper_router_add_mapping(router, sig, mapping);
    mapper_mapping_set_direct(mapping);

    return mapping;
}

void mapper_router_add_linear_range_mapping(mapper_router router,
                                            mapper_signal sig,
                                            const char *name,
                                            float src_min, float src_max,
                                            float dest_min, float dest_max)
{
    mapper_mapping mapping =
        (mapper_mapping) calloc(1, sizeof(struct _mapper_mapping));
    mapping->props.src_name = strdup(sig->props.name);
    mapping->props.dest_name = strdup(name);

    mapper_mapping_range_t range;
    
    range.src_min = src_min;
    range.src_max = src_max;
    range.dest_min = dest_min;
    range.dest_max = dest_max;
    range.known = MAPPING_RANGE_KNOWN;
    
    mapper_mapping_set_linear_range(mapping, &range);
    mapper_router_add_mapping(router, sig, mapping);
}

void mapper_router_add_linear_scale_mapping(mapper_router router,
                                            mapper_signal sig,
                                            const char *name,
                                            float scale, float offset)
{
    mapper_mapping mapping = (mapper_mapping) calloc(1, sizeof(struct _mapper_mapping));

    mapping->props.scaling = SC_LINEAR;
    mapping->props.src_name = strdup(sig->props.name);
    mapping->props.dest_name = strdup(name);

    free(mapping->props.expression);
    mapping->props.expression = (char*) malloc(256 * sizeof(char));
    snprintf(mapping->props.expression, 256, "y=x*%g+%g", scale, offset);

    mapping->props.range.known = 0;

    Tree *T = NewTree();

    int success_tree = get_expr_Tree(T, mapping->props.expression);
    if (!success_tree)
        return;

    mapping->expr_tree = T;

    mapper_router_add_mapping(router, sig, mapping);
}

void mapper_router_add_calibrate_mapping(mapper_router router,
                                         mapper_signal sig,
                                         const char *name, float dest_min,
                                         float dest_max)
{
    mapper_mapping mapping =
        (mapper_mapping) calloc(1, sizeof(struct _mapper_mapping));

    mapping->props.src_name = strdup(sig->props.name);
    mapping->props.dest_name = strdup(name);
    mapper_mapping_set_calibrate(mapping, dest_min, dest_max);
    mapper_router_add_mapping(router, sig, mapping);
}

void mapper_router_add_expression_mapping(mapper_router router,
                                          mapper_signal sig,
                                          const char *name, char *expr)
{
    mapper_mapping mapping =
        (mapper_mapping) calloc(1, sizeof(struct _mapper_mapping));

    mapping->props.scaling = SC_EXPRESSION;
    mapping->props.src_name = strdup(sig->props.name);
    mapping->props.dest_name = strdup(name);
    mapper_mapping_set_expression(mapping, expr);
    mapper_router_add_mapping(router, sig, mapping);
}

mapper_router mapper_router_find_by_dest_name(mapper_router router,
                                                const char* dest_name)
{
    int n = strlen(dest_name);
    const char *slash = strchr(dest_name+1, '/');
    if (slash)
        n = slash - dest_name;

    while (router) {
        if (strncmp(router->dest_name, dest_name, n)==0)
            return router;
        router = router->next;
    }
    return 0;
}
