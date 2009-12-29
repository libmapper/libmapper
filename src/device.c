
#include <lo/lo.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>
#include <assert.h>

#include "mapper_internal.h"
#include "types_internal.h"
#include <mapper/mapper.h>

//! Allocate and initialize a mapper device.
mapper_device mdev_new(const char *name_prefix, int initial_port)
{
    if (initial_port == 0)
        initial_port = 9000;

    mapper_device md =
        (mapper_device)calloc(1, sizeof(struct _mapper_device));
    md->name_prefix = strdup(name_prefix);
    md->admin = mapper_admin_new(name_prefix,
                                 md,
                                 MAPPER_DEVICE_SYNTH,
                                 initial_port);
    md->admin->port.on_lock = mdev_on_port;
    return md;
}

//! Free resources used by a mapper device.
void mdev_free(mapper_device md)
{
    int i;
    if (md) {
        mapper_admin_free(md->admin);
        for (i=0; i<md->n_inputs; i++)
            free(md->inputs[i]);
        free(md->inputs);
        for (i=0; i<md->n_outputs; i++)
            free(md->outputs[i]);
        free(md->outputs);
        free(md);
    }
}

static void grow_ptr_array(void** array, int length, int *size)
{
    if (*size < length && !*size)
        (*size) ++;
    while (*size < length)
        (*size) *= 2;
    *array = realloc(*array, sizeof(void*)*(*size));
}

//! Register an input signal with a mapper device.
void mdev_register_input(mapper_device md,
                         mapper_signal sig)
{
    md->n_inputs ++;
    grow_ptr_array((void**)&md->inputs, md->n_inputs,
                   &md->n_alloc_inputs);

    md->inputs[md->n_inputs-1] = sig;
    sig->device = md;

    mdev_start_server(md);
}

//! Register an output signal with a mapper device.
void mdev_register_output(mapper_device md,
                          mapper_signal sig)
{
    md->n_outputs ++;
    grow_ptr_array((void**)&md->outputs, md->n_outputs,
                   &md->n_alloc_outputs);

    md->outputs[md->n_outputs-1] = sig;
    sig->device = md;
}

int mdev_num_inputs(mapper_device md)
{
    return md->n_inputs;
}

int mdev_num_outputs(mapper_device md)
{
    return md->n_outputs;
}

void mdev_poll(mapper_device md, int block_ms)
{
    mapper_admin_poll(md->admin);
    if (md->server)
        lo_server_recv_noblock(md->server, block_ms);
    else
        usleep(block_ms*1000);
}

void mdev_route_signal(mapper_device md, mapper_signal sig,
                       mapper_signal_value_t *value)
{
    mapper_router r = md->routers;
    while (r) {
        mapper_router_receive_signal(r, sig, value);
        r = r->next;
    }
}

void mdev_add_router(mapper_device md, mapper_router rt)
{
    mapper_router *r = &md->routers;
    rt->next = *r;
    *r = rt;
}

void mdev_remove_router(mapper_device md, mapper_router rt)
{
    mapper_router *r = &md->routers;
    while (*r) {
        if (*r == rt) {
            *r = rt->next;
            break;
        }
        r = &(*r)->next;
    }
}

void mdev_on_port(mapper_device md,
                  mapper_admin_allocated_t *resource)
{
    trace("device '%s' acknowledged port allocation for %d\n",
          md->name_prefix, resource->value);

    mdev_start_server(md);
}

static void liblo_error_handler(int num, const char *msg, const char *path)
{
    printf("liblo server error %d in path %s: %s\n", num, path, msg);
}

static mapper_signal_value_t *sv = 0;
static int handler_signal(const char *path, const char *types, lo_arg **argv,
                          int argc, lo_message msg, void *user_data)
{
    mapper_signal sig = (mapper_signal)user_data;
    mapper_device md = sig->device;
    if (!md) { trace("error, sig->device==0\n"); return 0; }

    if (sig->handler) {
        int i;
        sv = realloc(sv, sizeof(mapper_signal_value_t)*sig->length);
        switch (sig->type) {
        case 'f':
            for (i=0; i < sig->length; i++)
                sv[i].f = argv[i]->f;
            break;
        case 'd':
            for (i=0; i < sig->length; i++)
                sv[i].d = argv[i]->d;
            break;
        case 'i':
            for (i=0; i < sig->length; i++)
                sv[i].i32 = argv[i]->i;
            break;
        default:
            assert(0);
        }
        sig->handler(md, sv);
    }

    return 0;
}

void mdev_start_server(mapper_device md)
{
    if (md->n_inputs > 0
        && md->admin->port.locked
        && !md->server)
    {
        int i,j;
        char port[16], *type = 0;

        sprintf(port, "%d", md->admin->port.value);
        md->server = lo_server_new(port, liblo_error_handler);

        if (md->server) {
            trace("device '%s' opened server on port %d\n",
                  md->name_prefix, md->admin->port.value);
        } else {
            trace("error opening server on port %d for device '%s'\n",
                  md->admin->port.value, md->name_prefix);
        }

        for (i=0; i<md->n_inputs; i++) {
            type = realloc(type, md->inputs[i]->length+1);
            for (j=0; j<md->inputs[i]->length; j++)
                type[j] = md->inputs[i]->type;
            type[j] = 0;
            lo_server_add_method(md->server,
                                 md->inputs[i]->name,
                                 type,
                                 handler_signal,
                                 (void*)(md->inputs[i]));
        }
        free(type);
    }
}
