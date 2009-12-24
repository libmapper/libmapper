
#include <lo/lo.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>

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
                                 MAPPER_DEVICE_SYNTH,
                                 initial_port);
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
