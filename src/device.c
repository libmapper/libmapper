
#include <lo/lo.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>
#include <assert.h>
#include <sys/time.h>
#include <zlib.h>

#include "mapper_internal.h"
#include "types_internal.h"
#include "config.h"
#include <mapper/mapper.h>

#ifdef HAVE_PTHREAD
#include <pthread.h>
#endif

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
mapper_device mdev_new(const char *name_prefix, int port,
                       mapper_admin admin)
{
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

    mapper_admin_add_device(md->admin, md, name_prefix, port);

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
        mapper_instance_id_map map;
        while (md->active_id_map) {
            map = md->active_id_map;
            md->active_id_map = map->next;
            free(map);
        }
        while (md->reserve_id_map) {
            map = md->reserve_id_map;
            md->reserve_id_map = map->next;
            free(map);
        }
        while (md->routers)
            mdev_remove_router(md, md->routers);
        while (md->receivers)
            mdev_remove_receiver(md, md->receivers);
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

static void mdev_increment_version(mapper_device md)
{
    md->version ++;
    if (md->admin->registered) {
        md->flags |= FLAGS_DEVICE_ATTRIBS_CHANGED;
    }
}

static int handler_signal(const char *path, const char *types,
                          lo_arg **argv, int argc, lo_message msg,
                          void *user_data)
{
    mapper_signal sig = (mapper_signal) user_data;
    mapper_device md = sig->device;
    void *dataptr = 0;
    int count = 1;

    if (!md) {
        trace("error, sig->device==0\n");
        return 0;
    }

    if (!sig)
        return 0;

    mapper_signal_instance si = sig->active_instances;
    if (!si)
        si = msig_get_instance_with_id(sig, 0, 1);
    if (!si)
        return 0;

    if (types[0] == LO_BLOB) {
        dataptr = lo_blob_dataptr((lo_blob)argv[0]);
        count = lo_blob_datasize((lo_blob)argv[0]) /
            mapper_type_size(sig->props.type);
    }
    else
        dataptr = argv[0];

    lo_timetag tt = lo_message_get_timestamp(msg);

    if (types[0] == LO_NIL) {
        si->has_value = 0;
    }
    else {
        /* This is cheating a bit since we know that the arguments pointed
         * to by argv are layed out sequentially in memory.  It's not
         * clear if liblo's semantics guarantee it, but known to be true
         * on all platforms. */
        // TODO: should copy last value from sample vector
        memcpy(si->value, dataptr, msig_vector_bytes(sig));
        si->has_value = 1;
    }
    si->timetag.sec = tt.sec;
    si->timetag.frac = tt.frac;
    if (sig->handler)
        sig->handler(sig, &sig->props, si->id_map->local, dataptr, count, &tt);
    si = si->next;
    if (!sig->props.is_output)
        mdev_receive_update(md, sig, si, tt);

    return 0;
}

static int handler_signal_instance(const char *path, const char *types,
                                   lo_arg **argv, int argc, lo_message msg,
                                   void *user_data)
{
    mapper_signal sig = (mapper_signal) user_data;
    mapper_device md = sig->device;
    void *dataptr = 0;
    int count = 1;

    if (!md) {
        trace("error, sig->device==0\n");
        return 0;
    }
    if (argc < 3)
        return 0;

    int group_id = argv[0]->i32;
    int instance_id = argv[1]->i32;
    int is_new = (types[2] == LO_TRUE);

    mapper_signal_instance si = 0;
    mapper_instance_id_map map = mdev_find_instance_id_map_by_remote(md, group_id,
                                                                     instance_id);

    // If map not found but I own the instance, stop here.
    if (!map && group_id == md->admin->name_hash)
        return 0;

    // Map may be waiting for release
    if (map && map->reference_count <= 0)
        return 0;

    // Don't activate instance just to release it again
    if (types[2] == LO_NIL || types[2] == LO_FALSE) {
        if (!map || !msig_find_instance_with_id_map(sig, map))
            return 0;
    }

    si = msig_get_instance_with_id_map(sig, map, is_new, 0);
    if (!si) {
        if (sig->instance_management_handler &&
            (sig->instance_management_flags & IN_OVERFLOW)) {
            sig->instance_management_handler(sig, &sig->props, -1, IN_OVERFLOW);
            // try again
            si = msig_get_instance_with_id_map(sig, map, is_new, 0);
        }
    }
    if (!si) {
        trace("no instances available for group=%ld, id=%ld\n",
              (long)group_id, (long)instance_id);
        return 0;
    }
    else if (!map) {
        // Need to add id map to device and link to new signal instance
        si->id_map = mdev_add_instance_id_map(md, si->id, group_id, instance_id);
    }

    if (types[2] == LO_TRUE) {
        if (sig->instance_management_handler &&
            (sig->instance_management_flags & IN_NEW))
            sig->instance_management_handler(sig, &sig->props, si->id_map->local,
                                             IN_NEW);
        return 0;
    }

    if (types[2] == LO_BLOB) {
        dataptr = lo_blob_dataptr((lo_blob)argv[2]);
        count = lo_blob_datasize((lo_blob)argv[2]) /
            mapper_type_size(sig->props.type);
    }
    else if (types[2] != LO_NIL) {
        /* This is cheating a bit since we know that the arguments pointed
         * to by argv are layed out sequentially in memory.  It's not
         * clear if liblo's semantics guarantee it, but known to be true
         * on all platforms. */
        memcpy(si->value, argv[2], msig_vector_bytes(sig));
        si->has_value = 1;
        dataptr = si->value;
    }
    lo_timetag tt = lo_message_get_timestamp(msg);
    si->timetag.sec = tt.sec;
    si->timetag.frac = tt.frac;

    if (sig->handler) {
        sig->handler(sig, &sig->props, si->id_map->local,
                     dataptr, count, &si->timetag);
    }
    if (!sig->props.is_output)
        mdev_receive_update(md, sig, si, tt);
    if (types[2] == LO_NIL) {
        /* Set remote release time to 2 seconds in the future,
         * since we want to filter out any stray packets. */
        mdev_timetag_now(md, &md->admin->clock.now);
        si->id_map->release_time = md->admin->clock.now.sec + 2;
        msig_release_instance_internal(sig, si, 0, 0, si->timetag);
    }
    return 0;
}

static int handler_instance_release_request(const char *path, const char *types,
                                            lo_arg **argv, int argc, lo_message msg,
                                            void *user_data)
{
    mapper_signal sig = (mapper_signal) user_data;
    mapper_device md = sig->device;

    if (!md)
        return 0;

    if (!sig->instance_management_handler ||
        !(sig->instance_management_flags & IN_REQUEST_RELEASE))
        return 0;

    mapper_instance_id_map map =
        mdev_find_instance_id_map_by_remote(md, argv[0]->i32, argv[1]->i32);
    if (!map)
        return 0;

    mapper_signal_instance si = msig_get_instance_with_id_map(sig, map, 0, 0);
    if (!si)
        return 0;

    if (sig->instance_management_handler)
        sig->instance_management_handler(sig, &sig->props, si->id_map->local,
                                         IN_REQUEST_RELEASE);

    return 0;
}

static int handler_query(const char *path, const char *types,
                         lo_arg **argv, int argc, lo_message msg,
                         void *user_data)
{
    mapper_signal sig = (mapper_signal) user_data;
    mapper_device md = sig->device;

    if (!md) {
        trace("error, sig->device==0\n");
        return 0;
    }

    if (!argc)
        return 0;
    else if (types[0] != 's' && types[0] != 'S')
        return 0;

    int i;
    lo_message m = lo_message_new();
    if (!m)
        return 0;
    lo_timetag tt = lo_message_get_timestamp(msg);
    lo_bundle b = lo_bundle_new(tt);

    mapper_signal_instance si = sig->active_instances;
    if (!si) {
        // If there are no active instances, send null response
        lo_message_add_nil(m);
        lo_bundle_add_message(b, &argv[0]->s, m);
        lo_send_bundle(lo_message_get_source(msg), b);
        lo_bundle_free_messages(b);
        return 0;
    }

    while (si) {
        if (sig->props.num_instances > 1) {
            lo_message_add_int32(m, (long)si->id_map->group);
            lo_message_add_int32(m, (long)si->id_map->local);
        }
        if (si->has_value) {
            if (sig->props.type == 'f') {
                float *v = si->value;
                for (i = 0; i < sig->props.length; i++)
                    lo_message_add_float(m, v[i]);
            }
            else if (sig->props.type == 'i') {
                int *v = si->value;
                for (i = 0; i < sig->props.length; i++)
                    lo_message_add_int32(m, v[i]);
            }
            else if (sig->props.type == 'd') {
                double *v = si->value;
                for (i = 0; i < sig->props.length; i++)
                    lo_message_add_double(m, v[i]);
            }
        }
        else {
            lo_message_add_nil(m);
        }
        lo_bundle_add_message(b, &argv[0]->s, m);
        si = si->next;
    }
    lo_send_bundle(lo_message_get_source(msg), b);
    lo_bundle_free_messages(b);
    return 0;
}


// Add an input signal to a mapper device.
mapper_signal mdev_add_input(mapper_device md, const char *name, int length,
                             char type, const char *unit,
                             void *minimum, void *maximum,
                             mapper_signal_handler *handler, void *user_data)
{
    if (mdev_get_input_by_name(md, name, 0))
        return 0;
    char *type_string = 0, *signal_get = 0;
    mapper_signal sig = msig_new(name, length, type, 0, unit, minimum,
                                 maximum, handler, user_data);
    if (!sig)
        return 0;
    md->n_inputs++;
    grow_ptr_array((void **) &md->inputs, md->n_inputs,
                   &md->n_alloc_inputs);

    mdev_increment_version(md);

    md->inputs[md->n_inputs - 1] = sig;
    sig->device = md;
    if (md->admin->name)
        sig->props.device_name = md->admin->name;

    if (!md->server)
        mdev_start_server(md);
    else {
        type_string = (char*) realloc(type_string, sig->props.length + 3);
        type_string[0] = type_string[1] = 'i';
        memset(type_string + 2, sig->props.type, sig->props.length);
        type_string[sig->props.length + 2] = 0;
        lo_server_add_method(md->server,
                             sig->props.name,
                             type_string + 2,
                             handler_signal, (void *) (sig));
        lo_server_add_method(md->server,
                             sig->props.name,
                             "b",
                             handler_signal, (void *) (sig));
        lo_server_add_method(md->server,
                             sig->props.name,
                             "N",
                             handler_signal, (void *) (sig));
        lo_server_add_method(md->server,
                             sig->props.name,
                             type_string,
                             handler_signal_instance, (void *) (sig));
        lo_server_add_method(md->server,
                             sig->props.name,
                             "iib",
                             handler_signal_instance, (void *) (sig));
        lo_server_add_method(md->server,
                             sig->props.name,
                             "iiT",
                             handler_signal_instance, (void *) (sig));
        lo_server_add_method(md->server,
                             sig->props.name,
                             "iiN",
                             handler_signal_instance, (void *) (sig));
        int len = strlen(sig->props.name) + 5;
        signal_get = (char*) realloc(signal_get, len);
        snprintf(signal_get, len, "%s%s", sig->props.name, "/get");
        lo_server_add_method(md->server,
                             signal_get,
                             "s",
                             handler_query, (void *) (sig));
        free(type_string);
        free(signal_get);
    }

    return sig;
}

// Add an output signal to a mapper device.
mapper_signal mdev_add_output(mapper_device md, const char *name, int length,
                              char type, const char *unit, void *minimum, void *maximum)
{
    if (mdev_get_output_by_name(md, name, 0))
        return 0;
    mapper_signal sig = msig_new(name, length, type, 1, unit, minimum,
                                 maximum, 0, 0);
    if (!sig)
        return 0;
    md->n_outputs++;
    grow_ptr_array((void **) &md->outputs, md->n_outputs,
                   &md->n_alloc_outputs);

    mdev_increment_version(md);

    md->outputs[md->n_outputs - 1] = sig;
    sig->device = md;
    if (md->admin->name)
        sig->props.device_name = md->admin->name;
    return sig;
}

void mdev_add_signal_methods(mapper_device md, mapper_signal sig)
{
    // TODO: handle adding and removing input signal methods also?
    if (!sig->props.is_output)
        return;
    char *type = 0, *path = 0;
    int len;
    if (!md->server)
        mdev_start_server(md);
    else {
        len = (int) strlen(sig->props.name) + 5;
        path = (char*) realloc(path, len);
        snprintf(path, len, "%s%s", sig->props.name, "/got");
        type = (char*) realloc(type, sig->props.length + 3);
        type[0] = type[1] = 'i';
        memset(type + 2, sig->props.type,
               sig->props.length);
        type[sig->props.length + 2] = 0;
        int len = (int) strlen(sig->props.name) + 5;
        path = (char*) realloc(path, len);
        snprintf(path, len, "%s%s", sig->props.name, "/got");
        lo_server_add_method(md->server,
                             path,
                             type + 2,
                             handler_signal, (void *)sig);
        lo_server_add_method(md->server,
                             path,
                             type,
                             handler_signal_instance, (void *)sig);
        lo_server_add_method(md->server,
                             path,
                             "N",
                             handler_signal, (void *)sig);
        lo_server_add_method(md->server,
                             path,
                             "iiN",
                             handler_signal_instance, (void *)sig);
        md->n_output_callbacks ++;
        free(path);
    }
}

void mdev_remove_signal_methods(mapper_device md, mapper_signal sig)
{
    char *type = 0, *path = 0;
    int len, i;
    if (!md || !sig)
        return;
    for (i=0; i<md->n_outputs; i++) {
        if (md->outputs[i] == sig)
            break;
    }
    if (i==md->n_outputs)
        return;
    type = (char*) realloc(type, sig->props.length + 3);
    type[0] = type[1] = 'i';
    memset(type + 2, sig->props.type,
           sig->props.length);
    type[sig->props.length + 2] = 0;
    len = (int) strlen(sig->props.name) + 5;
    path = (char*) realloc(path, len);
    snprintf(path, len, "%s%s", sig->props.name, "/got");
    lo_server_del_method(md->server, path, type);
    lo_server_del_method(md->server, path, type + 2);
    lo_server_del_method(md->server, path, "N");
    lo_server_del_method(md->server, path, "iiN");
    md->n_output_callbacks --;
}

void mdev_add_instance_release_request_callback(mapper_device md, mapper_signal sig)
{
    if (!sig->props.is_output)
        return;

    if (!md->server)
        mdev_start_server(md);
    else {
        lo_server_add_method(md->server,
                             sig->props.name,
                             "iiF",
                             handler_instance_release_request, (void *) (sig));
        md->n_output_callbacks ++;
    }
}

void mdev_remove_instance_release_request_callback(mapper_device md, mapper_signal sig)
{
    int i;
    if (!md || !sig)
        return;
    for (i=0; i<md->n_outputs; i++) {
        if (md->outputs[i] == sig)
            break;
    }
    if (i==md->n_outputs)
        return;
    lo_server_del_method(md->server, sig->props.name, "iiF");
    md->n_output_callbacks --;
}

void mdev_remove_input(mapper_device md, mapper_signal sig)
{
    int i, n;
    for (i=0; i<md->n_inputs; i++) {
        if (md->inputs[i] == sig)
            break;
    }
    if (i==md->n_inputs)
        return;

    for (n=i; n<(md->n_inputs-1); n++) {
        md->inputs[n] = md->inputs[n+1];
    }
    if (md->server) {
        char *type_string = (char*) malloc(sig->props.length + 3);
        type_string[0] = type_string[1] = 'i';
        memset(type_string + 2, sig->props.type, sig->props.length);
        type_string[sig->props.length + 2] = 0;
        lo_server_del_method(md->server, sig->props.name, type_string);
        lo_server_del_method(md->server, sig->props.name, type_string + 2);
        lo_server_del_method(md->server, sig->props.name, "b");
        lo_server_del_method(md->server, sig->props.name, "N");
        lo_server_del_method(md->server, sig->props.name, "iib");
        lo_server_del_method(md->server, sig->props.name, "iiT");
        lo_server_del_method(md->server, sig->props.name, "iiN");
        free(type_string);
        int len = strlen(sig->props.name) + 5;
        char *signal_get = (char*) malloc(len);
        strncpy(signal_get, sig->props.name, len);
        strncat(signal_get, "/get", len);
        lo_server_del_method(md->server, signal_get, NULL);
        free(signal_get);
    }
    md->n_inputs --;
    mdev_increment_version(md);
    msig_free(sig);
}

void mdev_remove_output(mapper_device md, mapper_signal sig)
{
    int i, n;
    for (i=0; i<md->n_outputs; i++) {
        if (md->outputs[i] == sig)
            break;
    }
    if (i==md->n_outputs)
        return;

    for (n=i; n<(md->n_outputs-1); n++) {
        md->outputs[n] = md->outputs[n+1];
    }
    if (sig->handler && md->server) {
        int len = strlen(sig->props.name) + 5;
        char *path = (char*) malloc(len);
        strncpy(path, sig->props.name, len);
        strncat(path, "/got", len);
        lo_server_del_method(md->server, path, NULL);
        free(path);
    }
    if (sig->instance_management_handler &&
        (sig->instance_management_flags & IN_REQUEST_RELEASE)) {
        lo_server_del_method(md->server, sig->props.name, "iiF");
    }
    md->n_outputs --;
    mdev_increment_version(md);
    msig_free(sig);
}

int mdev_num_inputs(mapper_device md)
{
    return md->n_inputs;
}

int mdev_num_outputs(mapper_device md)
{
    return md->n_outputs;
}

int mdev_num_links_in(mapper_device md)
{
    return md->n_links_in;
}

int mdev_num_links_out(mapper_device md)
{
    return md->n_links_out;
}

int mdev_num_connections_in(mapper_device md)
{
    mapper_receiver r = md->receivers;
    int count = 0;
    while (r) {
        count += r->n_connections;
        r = r->next;
    }
    return count;
}

int mdev_num_connections_out(mapper_device md)
{
    mapper_router r = md->routers;
    int count = 0;
    while (r) {
        count += r->n_connections;
        r = r->next;
    }
    return count;
}

mapper_signal *mdev_get_inputs(mapper_device md)
{
    return md->inputs;
}

mapper_signal *mdev_get_outputs(mapper_device md)
{
    return md->outputs;
}

mapper_signal mdev_get_input_by_name(mapper_device md, const char *name,
                                     int *index)
{
    int i;
    int slash = name[0]=='/' ? 1 : 0;
    for (i=0; i<md->n_inputs; i++)
    {
        if (strcmp(md->inputs[i]->props.name + 1,
                   name + slash)==0)
        {
            if (index)
                *index = i;
            return md->inputs[i];
        }
    }
    return 0;
}

mapper_signal mdev_get_output_by_name(mapper_device md, const char *name,
                                      int *index)
{
    int i;
    int slash = name[0]=='/' ? 1 : 0;
    for (i=0; i<md->n_outputs; i++)
    {
        if (strcmp(md->outputs[i]->props.name + 1,
                   name + slash)==0)
        {
            if (index)
                *index = i;
            return md->outputs[i];
        }
    }
    return 0;
}

mapper_signal mdev_get_input_by_index(mapper_device md, int index)
{
    if (index >= 0 && index < md->n_inputs)
        return md->inputs[index];
    return 0;
}

mapper_signal mdev_get_output_by_index(mapper_device md, int index)
{
    if (index >= 0 && index < md->n_outputs)
        return md->outputs[index];
    return 0;
}

int mdev_poll(mapper_device md, int block_ms)
{
    int admin_count = mapper_admin_poll(md->admin);
    int count = 0;

    if (md->server) {

        /* If a timeout is specified, loop until the time is up. */
        if (block_ms)
        {
            double then = get_current_time();
            int left_ms = block_ms;
            while (left_ms > 0)
            {
                if (lo_server_recv_noblock(md->server, left_ms))
                    count++;
                double elapsed = get_current_time() - then;
                left_ms = block_ms - (int)(elapsed*1000);
            }
        }

        /* When done, or if non-blocking, check for remaining messages
         * up to a proportion of the number of input
         * signals. Arbitrarily choosing 1 for now, since we don't
         * support "combining" multiple incoming streams, so there's
         * no point.  Perhaps if this is supported in the future it
         * can be a heuristic based on a recent number of messages per
         * channel per poll. */
        while (count < (md->n_inputs + md->n_output_callbacks)*1
               && lo_server_recv_noblock(md->server, 0))
            count++;
    }
    else if (block_ms) {
#ifdef WIN32
        Sleep(block_ms);
#else
        usleep(block_ms * 1000);
#endif
    }

    return admin_count + count;
}

void mdev_num_instances_changed(mapper_device md,
                                mapper_signal sig)
{
    if (!md)
        return;

    mapper_router r = md->routers;
    while (r) {
        mapper_router_num_instances_changed(r, sig);
        r = r->next;
    }
}

void mdev_route_signal(mapper_device md,
                       mapper_signal sig,
                       mapper_signal_instance si,
                       void *value,
                       int count,
                       mapper_timetag_t timetag)
{
    int flags = 0;
    if (!si->is_active && si->has_value) {
        flags = FLAGS_IS_NEW_INSTANCE;
        si->is_active = 1;
    }
    // pass update to each router in turn
    mapper_router r = md->routers;
    while (r) {
        mapper_router_process_signal(r, sig, si, value, count,
                                     timetag, flags);
        r = r->next;
    }
}

void mdev_receive_update(mapper_device md,
                         mapper_signal sig,
                         mapper_signal_instance si,
                         mapper_timetag_t tt)
{
    // pass update to each receiver in turn
    mapper_receiver r = md->receivers;
    while (r) {
        mapper_receiver_send_update(r, sig, si, tt);
        r = r->next;
    }

    return;
}


// Function to start a mapper queue
void mdev_start_queue(mapper_device md, mapper_timetag_t tt)
{
    if (!md)
        return;
    mapper_router r = md->routers;
    while (r) {
        mapper_router_start_queue(r, tt);
        r = r->next;
    }
}

void mdev_send_queue(mapper_device md, mapper_timetag_t tt)
{
    if (!md)
        return;
    mapper_router r = md->routers;
    while (r) {
        mapper_router_send_queue(r, tt);
        r = r->next;
    }
}

int mdev_route_query(mapper_device md, mapper_signal sig,
                     mapper_timetag_t tt)
{
    int count = 0;
    mapper_router r = md->routers;
    while (r) {
        count += mapper_router_send_query(r, sig, tt);
        r = r->next;
    }
    return count;
}

void mdev_route_release_request(mapper_device md,
                                mapper_signal sig,
                                mapper_signal_instance si,
                                mapper_timetag_t tt)
{
    // pass update to each receiver in turn
    mapper_receiver r = md->receivers;
    while (r) {
        mapper_receiver_send_release_request(r, sig, si, tt);
        r = r->next;
    }
}

void mdev_add_router(mapper_device md, mapper_router rt)
{
    mapper_router *r = &md->routers;
    rt->next = *r;
    *r = rt;
    md->n_links_out++;
}

void mdev_remove_router(mapper_device md, mapper_router rt)
{
    mapper_router *r = &md->routers;
    while (*r) {
        if (*r == rt) {
            *r = rt->next;
            mapper_router_free(rt);
            break;
        }
        r = &(*r)->next;
    }
    md->n_links_out--;
}

void mdev_add_receiver(mapper_device md, mapper_receiver rc)
{
    mapper_receiver *r = &md->receivers;
    rc->next = *r;
    *r = rc;
    md->n_links_in++;
}

void mdev_remove_receiver(mapper_device md, mapper_receiver rc)
{
    // remove receiver
    mapper_receiver *r = &md->receivers;
    while (*r) {
        if (*r == rc) {
            *r = rc->next;
            mapper_receiver_free(rc);
            break;
        }
        r = &(*r)->next;
    }
    md->n_links_in--;
}

void mdev_reserve_instance_id_map(mapper_device dev)
{
    mapper_instance_id_map map;
    map = (mapper_instance_id_map)calloc(1, sizeof(struct _mapper_instance_id_map));
    map->next = dev->reserve_id_map;
    dev->reserve_id_map = map;
}

mapper_instance_id_map mdev_add_instance_id_map(mapper_device dev, int local_id,
                                                int group_id, int remote_id)
{
    if (!dev->reserve_id_map)
        mdev_reserve_instance_id_map(dev);

    mapper_instance_id_map map = dev->reserve_id_map;
    map->local = local_id;
    map->group = group_id;
    map->remote = remote_id;
    map->reference_count = 1;
    map->release_time = 0;
    dev->reserve_id_map = map->next;
    map->next = dev->active_id_map;
    dev->active_id_map = map;
    return map;
}

void mdev_remove_instance_id_map(mapper_device dev, mapper_instance_id_map map)
{
    mapper_instance_id_map *id_map = &dev->active_id_map;
    while (*id_map) {
        if ((*id_map) == map) {
            *id_map = (*id_map)->next;
            map->next = dev->reserve_id_map;
            dev->reserve_id_map = map;
            break;
        }
        id_map = &(*id_map)->next;
    }
}

mapper_instance_id_map mdev_find_instance_id_map_by_local(mapper_device dev,
                                                          int local_id)
{
    mdev_timetag_now(dev, &dev->admin->clock.now);

    mapper_instance_id_map *map = &dev->active_id_map;
    while (*map) {
        if ((*map)->reference_count <= 0) {
            // Map has been released and is waiting for timeout
            if (!(*map)->release_time ||
                ((*map)->release_time < dev->admin->clock.now.sec)) {
                // Clear expired maps as we go
                mapper_instance_id_map temp = *map;
                *map = (*map)->next;
                temp->next = dev->reserve_id_map;
                dev->reserve_id_map = temp;
            }
            else
                map = &(*map)->next;
            // We should never return an expired map locally
            continue;
        }
        if ((*map)->local == local_id)
            return (*map);
        map = &(*map)->next;
    }
    return 0;
}

mapper_instance_id_map mdev_find_instance_id_map_by_remote(mapper_device dev,
                                                           int group_id, int remote_id)
{
    mdev_timetag_now(dev, &dev->admin->clock.now);

    mapper_instance_id_map *map = &dev->active_id_map;
    while (*map) {
        if ((*map)->reference_count <= 0) {
            // Map has been released and is waiting for timeout
            if (!(*map)->release_time ||
                ((*map)->release_time < dev->admin->clock.now.sec)) {
                // Clear expired maps as we go
                mapper_instance_id_map temp = *map;
                *map = (*map)->next;
                temp->next = dev->reserve_id_map;
                dev->reserve_id_map = temp;
                continue;
            }
        }
        if (((*map)->group == group_id) && ((*map)->remote == remote_id))
            return (*map);
        map = &(*map)->next;
    }
    return 0;
}

/* Note: any call to liblo where get_liblo_error will be called
 * afterwards must lock this mutex, otherwise there is a race
 * condition on receiving this information.  Could be fixed by the
 * liblo error handler having a user context pointer. */
static int liblo_error_num = 0;
static void liblo_error_handler(int num, const char *msg, const char *path)
{
    liblo_error_num = num;
    if (num == LO_NOPORT) {
        trace("liblo could not start a server because port unavailable\n");
    } else
        fprintf(stderr, "[libmapper] liblo server error %d in path %s: %s\n",
               num, path, msg);
}

void mdev_start_server(mapper_device md)
{
    if (!md->server) {
        int i;
        char port[16], *pport = port, *type = 0, *path = 0;

        if (md->admin->port)
            sprintf(port, "%d", md->admin->port);
        else
            pport = 0;

        while (!(md->server = lo_server_new(pport, liblo_error_handler))) {
            pport = 0;
        }

        // Disable liblo message queueing
        lo_server_enable_queue(md->server, 0, 1);

        md->admin->port = lo_server_get_port(md->server);
        trace("bound to port %i\n", md->admin->port);

        for (i = 0; i < md->n_inputs; i++) {
            type = (char*) realloc(type, md->inputs[i]->props.length + 3);
            type[0] = type[1] = 'i';
            memset(type + 2, md->inputs[i]->props.type,
                   md->inputs[i]->props.length);
            type[md->inputs[i]->props.length + 2] = 0;
            lo_server_add_method(md->server,
                                 md->inputs[i]->props.name,
                                 type + 2,
                                 handler_signal, (void *) (md->inputs[i]));
            lo_server_add_method(md->server,
                                 md->inputs[i]->props.name,
                                 "b",
                                 handler_signal, (void *) (md->inputs[i]));
            lo_server_add_method(md->server,
                                 md->inputs[i]->props.name,
                                 "N",
                                 handler_signal, (void *) (md->inputs[i]));
            lo_server_add_method(md->server,
                                 md->inputs[i]->props.name,
                                 type,
                                 handler_signal_instance, (void *) (md->inputs[i]));
            lo_server_add_method(md->server,
                                 md->inputs[i]->props.name,
                                 "iib",
                                 handler_signal_instance, (void *) (md->inputs[i]));
            lo_server_add_method(md->server,
                                 md->inputs[i]->props.name,
                                 "iiT",
                                 handler_signal_instance, (void *) (md->inputs[i]));
            lo_server_add_method(md->server,
                                 md->inputs[i]->props.name,
                                 "iiN",
                                 handler_signal_instance, (void *) (md->inputs[i]));
            int len = (int) strlen(md->inputs[i]->props.name) + 5;
            path = (char*) realloc(path, len);
            snprintf(path, len, "%s%s", md->inputs[i]->props.name, "/get");
            lo_server_add_method(md->server,
                                 path,
                                 "s",
                                 handler_query, (void *) (md->inputs[i]));
        }
        for (i = 0; i < md->n_outputs; i++) {
            if (md->outputs[i]->handler) {
                type = (char*) realloc(type, md->outputs[i]->props.length + 3);
                type[0] = type[1] = 'i';
                memset(type + 2, md->outputs[i]->props.type,
                       md->outputs[i]->props.length);
                type[md->outputs[i]->props.length + 2] = 0;
                int len = (int) strlen(md->outputs[i]->props.name) + 5;
                path = (char*) realloc(path, len);
                snprintf(path, len, "%s%s", md->outputs[i]->props.name, "/got");
                lo_server_add_method(md->server,
                                     path,
                                     type + 2,
                                     handler_signal, (void *) (md->outputs[i]));
                lo_server_add_method(md->server,
                                     path,
                                     type,
                                     handler_signal_instance, (void *) (md->outputs[i]));
                lo_server_add_method(md->server,
                                     path,
                                     "N",
                                     handler_signal, (void *) (md->outputs[i]));
                lo_server_add_method(md->server,
                                     path,
                                     "iiN",
                                     handler_signal_instance, (void *) (md->outputs[i]));
                md->n_output_callbacks ++;
            }
            if (md->outputs[i]->instance_management_handler &&
                (md->outputs[i]->instance_management_flags & IN_REQUEST_RELEASE)) {
                lo_server_add_method(md->server,
                                     md->outputs[i]->props.name,
                                     "iiF",
                                     handler_instance_release_request,
                                     (void *) (md->outputs[i]));
                md->n_output_callbacks ++;
            }
        }
        free(type);
        free(path);
    }
}

const char *mdev_name(mapper_device md)
{
    /* Hand this off to the admin struct, where the name may be
     * cached. However: manually checking ordinal.locked here so that
     * we can safely trace bad usage when mapper_admin_full_name is
     * called inappropriately. */
    if (md->admin->registered)
        return mapper_admin_name(md->admin);
    else
        return 0;
}

unsigned int mdev_id(mapper_device md)
{
    if (md->admin->registered)
        return md->admin->name_hash;
    else
        return 0;
}

unsigned int mdev_port(mapper_device md)
{
    if (md->admin->registered)
        return md->admin->port;
    else
        return 0;
}

const struct in_addr *mdev_ip4(mapper_device md)
{
    if (md->admin->registered)
        return &md->admin->interface_ip;
    else
        return 0;
}

const char *mdev_interface(mapper_device md)
{
    return md->admin->interface_name;
}

unsigned int mdev_ordinal(mapper_device md)
{
    if (md->admin->registered)
        return md->admin->ordinal.value;
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
                       lo_type type, lo_arg *value)
{
    mapper_table_add_or_update_osc_value(dev->extra,
                                         property, type, value);
}

void mdev_remove_property(mapper_device dev, const char *property)
{
    table_remove_key(dev->extra, property, 1);
}

lo_server mdev_get_lo_server(mapper_device md)
{
    return md->server;
}
