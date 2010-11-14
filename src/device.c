
#include <lo/lo.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>
#include <assert.h>
#include <sys/time.h>

#include "mapper_internal.h"
#include "types_internal.h"
#include "config.h"
#include <mapper/mapper.h>

/*! Internal function to get the current time. */
static double get_current_time()
{
#ifdef HAVE_GETTIMEOFDAY
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (double) tv.tv_sec + tv.tv_usec / 1000000.0;
#else
#error No timing method known on this platform.
#endif
}

//! Allocate and initialize a mapper device.
mapper_device mdev_new(const char *name_prefix, int initial_port,
                       mapper_admin admin)
{
    if (initial_port == 0)
        initial_port = 9000;

    mapper_device md =
        (mapper_device) calloc(1, sizeof(struct _mapper_device));
    md->name_prefix = strdup(name_prefix);

    if (admin) {
        md->admin = admin;
        md->own_admin = 0;
    }
    else {
        md->admin = mapper_admin_new(0, 0, 0);
        md->own_admin = 1;
    }

    if (!md->admin) {
        mdev_free(md);
        return NULL;
    }

    mapper_admin_add_device(md->admin, md, name_prefix, initial_port);

    md->admin->port.on_lock = mdev_on_port_and_ordinal;
    md->admin->ordinal.on_lock = mdev_on_port_and_ordinal;
    md->routers = 0;
    md->extra = table_new();
    return md;
}

//! Free resources used by a mapper device.
void mdev_free(mapper_device md)
{
    int i;
    if (md) {
        if (md->admin && md->own_admin)
            mapper_admin_free(md->admin);
        for (i = 0; i < md->n_inputs; i++)
            msig_free(md->inputs[i]);
        if (md->inputs)
            free(md->inputs);
        for (i = 0; i < md->n_outputs; i++)
            msig_free(md->outputs[i]);
        if (md->outputs)
            free(md->outputs);
        if (md->extra)
            table_free(md->extra, 1);
        free(md);
    }
}

#ifdef __GNUC__
// when gcc inlines this with O2 or O3, it causes a crash. bug?
__attribute__ ((noinline))
#endif
static void grow_ptr_array(void **array, int length, int *size)
{
    if (*size < length && !*size)
        (*size)++;
    while (*size < length)
        (*size) *= 2;
    *array = realloc(*array, sizeof(void *) * (*size));
}

//! Register an input signal with a mapper device.
void mdev_register_input(mapper_device md, mapper_signal sig)
{
    md->n_inputs++;
    grow_ptr_array((void **) &md->inputs, md->n_inputs,
                   &md->n_alloc_inputs);

    md->inputs[md->n_inputs - 1] = sig;
    sig->device = md;
    if (md->admin->name)
        sig->props.device_name = md->admin->name;
    if (md->admin->registered) {
        md->update = 1;
    }

    mdev_start_server(md);
}

//! Register an output signal with a mapper device.
void mdev_register_output(mapper_device md, mapper_signal sig)
{
    md->n_outputs++;
    grow_ptr_array((void **) &md->outputs, md->n_outputs,
                   &md->n_alloc_outputs);

    md->outputs[md->n_outputs - 1] = sig;
    sig->device = md;
    if (md->admin->name)
        sig->props.device_name = md->admin->name;
    if (md->admin->registered) {
        md->update = 1;
    }
}

int mdev_num_inputs(mapper_device md)
{
    return md->n_inputs;
}

int mdev_num_outputs(mapper_device md)
{
    return md->n_outputs;
}

int mdev_find_input_by_name(mapper_device md, const char *name,
                            mapper_signal *result)
{
    int i;
    int slash = name[0]=='/' ? 1 : 0;
    for (i=0; i<md->n_inputs; i++)
    {
        if (strcmp(md->inputs[i]->props.name + 1,
                   name + slash)==0)
        {
            if (result)
                *result = md->inputs[i];
            return i;
        }
    }
    if (result)
        *result = 0;
    return -1;
}

int mdev_find_output_by_name(mapper_device md, const char *name,
                             mapper_signal *result)
{
    int i;
    int slash = name[0]=='/' ? 1 : 0;
    for (i=0; i<md->n_outputs; i++)
    {
        if (strcmp(md->outputs[i]->props.name + 1,
                   name + slash)==0)
        {
            if (result)
                *result = md->outputs[i];
            return i;
        }
    }
    if (result)
        *result = 0;
    return -1;
}

int mdev_poll(mapper_device md, int block_ms)
{
    double then = get_current_time();
    int count = mapper_admin_poll(md->admin);
    int count2 = 0;

    if (md->server) {
        while (count2 < md->n_inputs 
               && lo_server_recv_noblock(md->server, 0)
               && get_current_time() - then < block_ms * 1000) {
            count2++;
        }
        return count + count2;
    }
    else
        usleep(block_ms * 1000 - (get_current_time() - then));
    return 0;
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

/*! Called when once when the port is allocated and again when the
 *  ordinal is allocated, or vice-versa.  Must start server when both
 *  have been allocated. (No point starting it earlier since we won't
 *  be able to register any handlers. */
void mdev_on_port_and_ordinal(mapper_device md,
                              mapper_admin_allocated_t *resource)
{
    if (!(md->admin->ordinal.locked && md->admin->port.locked))
        return;

    trace
        ("device '%s.%d' acknowledged port and ordinal allocation for %d\n",
         md->name_prefix, md->admin->ordinal.value, md->admin->port.value);

    mdev_start_server(md);
}

static void liblo_error_handler(int num, const char *msg, const char *path)
{
    printf("[libmapper] liblo server error %d in path %s: %s\n",
           num, path, msg);
}

static mapper_signal_value_t *sv = 0;
static int handler_signal(const char *path, const char *types,
                          lo_arg **argv, int argc, lo_message msg,
                          void *user_data)
{
    mapper_signal sig = (mapper_signal) user_data;
    mapper_device md = sig->device;


    if (!md) {
        trace("error, sig->device==0\n");
        return 0;
    }

    if (sig->handler) {
        int i;
        sv = (mapper_signal_value_t*) realloc(
            sv, sizeof(mapper_signal_value_t) * sig->props.length);
        switch (sig->props.type) {
        case 'f':
            for (i = 0; i < sig->props.length; i++)
                sv[i].f = argv[i]->f;
            break;
        case 'd':
            for (i = 0; i < sig->props.length; i++)
                sv[i].d = argv[i]->d;
            break;
        case 'i':
            for (i = 0; i < sig->props.length; i++)
                sv[i].i32 = argv[i]->i;
            break;
        default:
            assert(0);
        }
        sig->handler(sig, sv);
    }

    return 0;
}

void mdev_start_server(mapper_device md)
{

    if (md->n_inputs > 0 && md->admin->port.locked && !md->server) {
        int i, j;
        char port[16], *type = 0;

        sprintf(port, "%d", md->admin->port.value);
        md->server = lo_server_new(port, liblo_error_handler);

        if (md->server) {
            trace("device '%s' opened server on port %d\n",
                  mapper_admin_name(md->admin), md->admin->port.value);

        } else {
            trace("error opening server on port %d for device '%s'\n",
                  md->admin->port.value, md->name_prefix);
            return;
        }

        for (i = 0; i < md->n_inputs; i++) {
            type = (char*) realloc(type, md->inputs[i]->props.length + 1);
            for (j = 0; j < md->inputs[i]->props.length; j++)
                type[j] = md->inputs[i]->props.type;
            type[j] = 0;
            lo_server_add_method(md->server,
                                 md->inputs[i]->props.name,
                                 type,
                                 handler_signal, (void *) (md->inputs[i]));
        }
        free(type);
    }
}

const char *mdev_name(mapper_device md)
{
    /* Hand this off to the admin struct, where the name may be
     * cached. However: manually checking ordinal.locked here so that
     * we can safely trace bad usage when mapper_admin_full_name is
     * called inappropriately. */
    if (md->admin->ordinal.locked)
        return mapper_admin_name(md->admin);
    else
        return 0;
}

unsigned int mdev_port(mapper_device md)
{
    if (md->admin->port.locked)
        return md->admin->port.value;
    else
        return 0;
}

int mdev_ready(mapper_device device)
{
    if (!device)
        return 0;

    return device->admin->registered;
}

void mdev_set_property(mapper_device dev, const char *property,
                       char type, lo_arg *value)
{
    mapper_table_add_or_update_osc_value(dev->extra,
                                         property, type, value);
}

void mdev_remove_property(mapper_device dev, const char *property)
{
    table_remove_key(dev->extra, property, 1);
}
