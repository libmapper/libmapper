
#include <stdlib.h>
#include <stdio.h>

#include <lo/lo.h>

#include "mapper_internal.h"
#include "types_internal.h"
#include <mapper/mapper.h>

mapper_router mapper_router_new(const char *host, int port)
{
    char str[16];
    mapper_router router = calloc(1,sizeof(struct _mapper_router));
    sprintf(str, "%d", port);
    router->addr = lo_address_new(host, str);
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
    if (!sm) return;

    // for each mapping, construct a mapped signal and send it
    mapper_mapping m = sm->mapping;
    while (m)
    {
        struct _mapper_signal signal = *sig;
        signal.name = m->name;
        mapper_router_send_signal(router, &signal, value);
        m = m->next;
    }
}

void mapper_router_send_signal(mapper_router router, mapper_signal sig,
                               mapper_signal_value_t *value)
{
    int i;
    lo_message m;
    if (!router->addr) return;
    m = lo_message_new();
    if (!m) return;
    for (i=0; i<sig->length; i++) {
        switch (sig->type) {
        case 'f':
            lo_message_add(m, "f", value[i].f);
            break;
        case 'i':
            lo_message_add(m, "i", value[i].i32);
            break;
        }
    }
    lo_send_message(router->addr, sig->name, m);
    lo_message_free(m);
    return;
}

void mapper_router_add_mapping(mapper_router router, mapper_signal sig,
                               const char *name)
{
    mapper_mapping mapping =
        calloc(1,sizeof(struct _mapper_mapping));
    mapping->name = name;

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
