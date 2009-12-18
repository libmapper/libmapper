
#include <lo/lo.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>

#ifdef POSIX
#include <time.h>
#endif

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

//! Register a signal with a mapper device.
void mdev_register_input(mapper_device md,
                       mapper_signal sig)
{
    md->n_inputs ++;
    grow_ptr_array(&md->inputs, md->n_inputs,
                   &md->n_alloc_inputs);

    mapper_signal s = calloc(1, sizeof(struct _mapper_signal));
    md->inputs[md->n_inputs-1] = s;
    s->device = md;
    memcpy(s, sig, sizeof(struct _mapper_signal));
}

//! Unregister a signal with a mapper md.
void mdev_register_output(mapper_device md,
                        mapper_signal sig)
{
    md->n_outputs ++;
    grow_ptr_array(&md->outputs, md->n_outputs,
                   &md->n_alloc_outputs);

    mapper_signal s = calloc(1, sizeof(struct _mapper_signal));
    md->outputs[md->n_outputs-1] = s;
    s->device = md;
    memcpy(s, sig, sizeof(struct _mapper_signal));
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
    // TODO: pass value to each router
}

#if 0
// TOOD: use this code for router
int mdev_send_signal(mapper_device md, mapper_signal sig)
{
    int i;
    lo_message m;
    if (!md->addr) return 1;
    m = lo_message_new();
    if (!m) return 1;
    for (i=0; i<sig->length; i++) {
        switch (sig->type) {
        case 'f':
            lo_message_add(m, "f", sig->current_value[i].f);
            break;
        case 'i':
            lo_message_add(m, "i", sig->current_value[i].i32);
            break;
        }
    }
    lo_send_message(md->addr, sig->name, m);
    lo_message_free(m);
    return 0;
}
#endif
