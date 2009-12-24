
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
        free(router);
    }
}

void mapper_router_receive_signal(mapper_router router, mapper_signal sig,
                                  mapper_signal_value_t *value)
{
    // map to a send
    // TODO: for now, straight through
    mapper_router_send_signal(router, sig, value);
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
