
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
    router->target_name = strdup(name);
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

        signal.name = strdup(m->name);

        mapper_signal_value_t v;
        mapper_mapping_perform(m, value, &v);
        if(mapper_clipping_perform(m, value, &v))
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

    for (i = 0; i < sig->length; i++)
        mval_add_to_message(m, sig, &value[i]);

    lo_send_message(router->addr, sig->name, m);
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

    router->device->num_mappings_out++;
}

void mapper_router_remove_mapping(      /*mapper_router router, */
                                     mapper_signal_mapping sm,
                                     mapper_mapping mapping)
{

    mapper_mapping *m = &sm->mapping;
    while (*m) {
        if (*m == mapping) {
            *m = mapping->next;
            /* lo_send(router->device->admin->admin_addr,"/disconnected",
                       "ss", strcat(src_device_name, src_param_name),
                       strcat(target_device_name, target_param_name) );
               router->device->num_mappings_out--; */
            break;
        }
        m = &(*m)->next;
    }
}

mapper_mapping mapper_router_add_blank_mapping(mapper_router router,
                                               mapper_signal sig,
                                               const char *name)
{
    mapper_mapping mapping = (mapper_mapping) calloc(1, sizeof(struct _mapper_mapping));

    // Some default values?
    mapping->scaling = SC_BYPASS;
    mapping->name = strdup(name);
    mapping->expression = strdup("y=x");
    mapping->range.known = 0;

    mapper_router_add_mapping(router, sig, mapping);
    return mapping;
}

void mapper_router_add_direct_mapping(mapper_router router,
                                      mapper_signal sig, const char *name)
{
    mapper_mapping mapping = (mapper_mapping) calloc(1, sizeof(struct _mapper_mapping));
    char src_name[1024], dest_name[1024];

    mapping->scaling = SC_BYPASS;
    mapping->name = strdup(name);
    mapping->expression = strdup("y=x");
    mapping->range.known = 0;

    mapper_router_add_mapping(router, sig, mapping);

    snprintf(src_name, 256, "/%s.%d%s", router->device->admin->identifier,
             router->device->admin->ordinal.value, sig->name);
    snprintf(dest_name, 256, "%s%s", router->target_name, name);

    lo_send(router->device->admin->admin_addr, "/connected", "ssss",
            src_name, dest_name, "@scaling", "bypass");
}

void mapper_router_add_linear_range_mapping(mapper_router router,
                                            mapper_signal sig,
                                            const char *name,
                                            float src_min, float src_max,
                                            float dest_min, float dest_max)
{
    mapper_mapping mapping = (mapper_mapping) calloc(1, sizeof(struct _mapper_mapping));
    char src_name[1024], dest_name[1024];

    mapping->scaling = SC_LINEAR;
    mapping->name = strdup(name);

    float scale = (dest_min - dest_max) / (src_min - src_max);
    float offset =
        (dest_max * src_min - dest_min * src_max) / (src_min - src_max);

    free(mapping->expression);
    mapping->expression = (char*) malloc(256 * sizeof(char));
    snprintf(mapping->expression, 256, "y=x*%g+%g", scale, offset);

    mapping->range.src_min = src_min;
    mapping->range.src_max = src_max;
    mapping->range.dest_min = dest_min;
    mapping->range.dest_max = dest_max;
    mapping->range.known = RANGE_KNOWN;

    Tree *T = NewTree();

    int success_tree = get_expr_Tree(T, mapping->expression);
    if (!success_tree)
        return;

    mapping->expr_tree = T;

    /*mapping->coef_input[0] = scale.f;
       mapping->order_input = 1;

       mapping->expression = strdup(expression);
       mapping->range[0]=src_min;
       mapping->range[1]=src_max;
       mapping->range[2]=dest_min;
       mapping->range[3]=dest_max; */


    mapper_router_add_mapping(router, sig, mapping);

    snprintf(src_name, 256, "/%s.%d%s", router->device->admin->identifier,
             router->device->admin->ordinal.value, sig->name);
    snprintf(dest_name, 256, "%s%s", router->target_name, name);

    lo_send(router->device->admin->admin_addr, "/connected", "sssssssffff",
            src_name, dest_name, "@scaling", "linear", "@expression",
            mapping->expression, "@range", src_min, src_max, dest_min,
            dest_max);
}

void mapper_router_add_linear_scale_mapping(mapper_router router,
                                            mapper_signal sig,
                                            const char *name,
                                            float scale, float offset)
{
    mapper_mapping mapping = (mapper_mapping) calloc(1, sizeof(struct _mapper_mapping));
    char src_name[1024], dest_name[1024];

    mapping->scaling = SC_LINEAR;
    mapping->name = strdup(name);

    free(mapping->expression);
    mapping->expression = (char*) malloc(256 * sizeof(char));
    snprintf(mapping->expression, 256, "y=x*%g+%g", scale, offset);

    mapping->range.known = 0;

    Tree *T = NewTree();

    int success_tree = get_expr_Tree(T, mapping->expression);
    if (!success_tree)
        return;

    mapping->expr_tree = T;

    mapper_router_add_mapping(router, sig, mapping);

    snprintf(src_name, 1024, "/%s%s",
             mapper_admin_name(router->device->admin), sig->name);
    snprintf(dest_name, 1024, "%s%s", router->target_name, name);

    lo_send(router->device->admin->admin_addr, "/connected", "ssssss",
            src_name, dest_name, "@scaling", "linear", "@expression",
            mapping->expression);
}

void mapper_router_add_calibrate_mapping(mapper_router router,
                                         mapper_signal sig,
                                         const char *name, float dest_min,
                                         float dest_max)
{
    mapper_mapping mapping = (mapper_mapping) calloc(1, sizeof(struct _mapper_mapping));
    char src_name[1024], dest_name[1024];

    mapping->scaling = SC_CALIBRATE;
    mapping->name = strdup(name);

    free(mapping->expression);
    mapping->expression = (char*) malloc(256 * sizeof(char));
    snprintf(mapping->expression, 256, "y=%g", dest_min);

    mapping->range.dest_min = dest_min;
    mapping->range.dest_max = dest_max;
    mapping->range.known = RANGE_DEST_MIN | RANGE_DEST_MAX;
    mapping->range.rewrite = 1;

    /*mapping->coef_input[0] = scale.f;
       mapping->order_input = 1;

       mapping->expression = strdup(expression);
       mapping->range[0]=src_min;
       mapping->range[1]=src_max;
       mapping->range[2]=dest_min;
       mapping->range[3]=dest_max; */


    mapper_router_add_mapping(router, sig, mapping);

    snprintf(src_name, 256, "/%s.%d%s", router->device->admin->identifier,
             router->device->admin->ordinal.value, sig->name);
    snprintf(dest_name, 256, "%s%s", router->target_name, name);

    lo_send(router->device->admin->admin_addr, "/connected", "sssssssssff",
            src_name, dest_name, "@scaling", "calibrate", "@expression",
            mapping->expression, "@range", "-", "-", dest_min, dest_max);
}

void mapper_router_add_expression_mapping(mapper_router router,
                                          mapper_signal sig,
                                          const char *name, char *expr)
{
    mapper_mapping mapping = (mapper_mapping) calloc(1, sizeof(struct _mapper_mapping));
    char src_name[1024], dest_name[1024];

    mapping->scaling = SC_EXPRESSION;
    mapping->name = strdup(name);
    mapping->expression = strdup(expr);
    mapping->range.known = 0;

    Tree *T = NewTree();
    get_expr_Tree(T, expr);
    mapping->expr_tree = T;

    mapper_router_add_mapping(router, sig, mapping);

    snprintf(src_name, 256, "/%s.%d%s", router->device->admin->identifier,
             router->device->admin->ordinal.value, sig->name);
    snprintf(dest_name, 256, "%s%s", router->target_name, name);

    lo_send(router->device->admin->admin_addr, "/connected", "ssssss",
            src_name, dest_name, "@scaling", "expression", "@expression",
            mapping->expression);
}

mapper_router mapper_router_find_by_target_name(mapper_router router,
                                                const char* target_name)
{
    while (router) {
        if (strcmp(router->target_name, target_name)==0)
            return router;
        router = router->next;
    }
    return 0;
}
