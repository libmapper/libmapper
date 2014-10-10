
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

    if (admin) {
        md->admin = admin;
        md->own_admin = 0;
    }
    else {
        md->admin = mapper_admin_new(0, 0, 0);
        md->own_admin = 1;
    }

    mdev_start_server(md, port);

    if (!md->admin || !md->server) {
        mdev_free(md);
        return NULL;
    }

    md->props.identifier = strdup(name_prefix);
    md->props.name = 0;
    md->props.name_hash = 0;
    md->ordinal.value = 1;
    md->ordinal.locked = 0;
    md->registered = 0;
    md->active_id_map = 0;
    md->reserve_id_map = 0;
    md->id_counter = 0;
    md->props.extra = table_new();
    md->flags = 0;
    md->signal_slot_counter = -1;

    md->router = (mapper_router) calloc(1, sizeof(struct _mapper_router));
    md->router->device = md;

//    md->link_timeout_sec = ADMIN_TIMEOUT_SEC;
    md->link_timeout_sec = 0;

    mapper_admin_add_device(md->admin, md);

    return md;
}

//! Free resources used by a mapper device.
void mdev_free(mapper_device md)
{
    int i, j;
    if (!md)
        return;

    if (md->registered) {
        // A registered device must tell the network it is leaving.
        mapper_admin_set_bundle_dest_bus(md->admin);
        mapper_admin_bundle_message(md->admin, ADM_LOGOUT, 0, "s", mdev_name(md));
    }

    // First release active instances
    mapper_signal sig;
    if (md->outputs) {
        // release all active output instances
        for (i = 0; i < md->props.num_outputs; i++) {
            sig = md->outputs[i];
            for (j = 0; j < sig->id_map_length; j++) {
                if (sig->id_maps[j].instance) {
                    msig_release_instance_internal(sig, j, MAPPER_NOW);
                }
            }
        }
    }
    if (md->inputs) {
        // release all active input instances
        for (i = 0; i < md->props.num_inputs; i++) {
            sig = md->inputs[i];
            for (j = 0; j < sig->id_map_length; j++) {
                if (sig->id_maps[j].instance) {
                    msig_release_instance_internal(sig, j, MAPPER_NOW);
                }
            }
        }
    }

    // Links reference parent signals so release them first
    while (md->router->links)
        mapper_router_remove_link(md->router, md->router->links);

    if (md->outputs) {
        for (i = 0; i < md->props.num_outputs; i++)
            msig_free(md->outputs[i]);
        free(md->outputs);
    }
    if (md->inputs) {
        for (i = 0; i < md->props.num_inputs; i++) {
            msig_free(md->inputs[i]);
        }
        free(md->inputs);
    }

    // Release device id maps
    mapper_id_map map;
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

    if (md->router) {
        while (md->router->signals) {
            mapper_router_signal rs = md->router->signals;
            md->router->signals = md->router->signals->next;
            free(rs);
        }
        free(md->router);
    }
    if (md->props.extra)
        table_free(md->props.extra, 1);
    if (md->props.identifier)
        free(md->props.identifier);
    if (md->props.name)
        free(md->props.name);
    if (md->props.host)
        free(md->props.host);
    if (md->admin) {
        if (md->own_admin)
            mapper_admin_free(md->admin);
        else
            md->admin->device = 0;
    }
    if (md->server)
        lo_server_free(md->server);
    free(md);
}

void mdev_registered(mapper_device md)
{
    int i, j;
    md->registered = 1;
    /* Add device name to signals. Also add device name hash to
     * locally-activated signal instances. */
    for (i = 0; i < md->props.num_inputs; i++) {
        md->inputs[i]->props.device_name = (char *)mdev_name(md);
        for (j = 0; j < md->inputs[i]->id_map_length; j++) {
            if (md->inputs[i]->id_maps[j].map &&
                md->inputs[i]->id_maps[j].map->origin == 0)
                md->inputs[i]->id_maps[j].map->origin = md->props.name_hash;
        }
    }
    for (i = 0; i < md->props.num_outputs; i++) {
        md->outputs[i]->props.device_name = (char *)mdev_name(md);
        for (j = 0; j < md->outputs[i]->id_map_length; j++) {
            if (md->outputs[i]->id_maps[j].map &&
                md->outputs[i]->id_maps[j].map->origin == 0)
                md->outputs[i]->id_maps[j].map->origin = md->props.name_hash;
        }
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
    md->props.version ++;
    if (md->registered) {
        md->flags |= FLAGS_DEVICE_ATTRIBS_CHANGED;
    }
}

static int check_types(const char *types, int len, char type, int vector_len)
{
    int i;
    if (len < vector_len || len % vector_len != 0) {
        trace("error: unexpected length.\n");
        return 0;
    }
    for (i = 0; i < len; i++) {
        if (types[i] != type && types[i] != 'N') {
            trace("error: unexpected typestring (expected %c%i).\n", type, len);
            return 0;
        }
    }
    return len / vector_len;
}

/* This handler needs to be able to handle a number of overlapping cases:
 *     - scalar and vector signal values
 *     - null vector elements
 *     - entirely null typestrings (used to release instances)
 *     - multiple signal samples in one message ("count" > 1)
 */

/* Note: a vector consisting completely of nulls indicates release of a
 * signal instance. As currently implemented, it is not possible for such an
 * update to be embedded in a multi-sample buffer update (i.e. using the
 * "count" variable in msig_update()). Null vectors will always be sent as
 * separate "release" messages. */
static int handler_signal(const char *path, const char *types,
                          lo_arg **argv, int argc, lo_message msg,
                          void *user_data)
{
    mapper_signal sig = (mapper_signal) user_data;
    mapper_device md;
    int i = 0, j, k, count = 1, nulls = 0;
    int index = 0, is_instance_update = 0, origin, public_id;
    mapper_id_map map;
    mapper_combiner cb;
    mapper_combiner_slot s = 0;
    int slot = -1;

    if (!sig || !sig->handler || !(md = sig->device)) {
        trace("error, cannot retrieve user_data\n");
        return 0;
    }

    if (!argc)
        return 0;

    // We need to consider that there may be properties appended to the msg
    // check length and find properties is any
    int value_len = 0;
    while (value_len < argc && types[value_len] != 's' && types[value_len] != 'S') {
        // count nulls here also to save time
        if (types[i] == 'N')
            nulls++;
        value_len++;
    }

    int argnum = value_len;
    while (argnum < argc) {
        // Parse any attached properties (instance ids, slot number)
        if (types[argnum] != 's' && types[argnum] != 'S') {
            trace("error in signal handler.\n");
            return 0;
        }
        if (strcmp(&argv[argnum]->s, "@instance") == 0
            && argc >= argnum + 3) {
            if (types[argnum+1] != 'i' || types[argnum+2] != 'i')
                return 0;
            is_instance_update = 1;
            origin = argv[argnum+1]->i32;
            public_id = argv[argnum+2]->i32;
            argnum += 3;
        }
        else if (strcmp(&argv[argnum]->s, "@slot") == 0
                 && argc >= argnum + 2) {
            if (types[argnum+1] != 'i')
                return 0;
            slot = argv[argnum+1]->i32;
            argnum += 2;
        }
        else {
            trace("error parsing signal update properties.\n");
            return 0;
        }
    }

    if (slot >= 0) {
        // check if we have a combiner for this signal
        cb = mapper_router_find_combiner(md->router, sig);
        if (cb) {
            s = mapper_combiner_get_slot(cb, slot);
            if (s && s->connection)
                count = check_types(types, value_len,
                                    s->connection->props.remote_type,
                                    s->connection->props.remote_length);
            else
                s = 0;
        }
    }
    else
        count = check_types(types, value_len, sig->props.type, sig->props.length);

    if (!count)
        return 0;

    // TODO: optionally discard out-of-order messages
    // requires timebase sync for many-to-one connections or local updates
    //    if (sig->discard_out_of_order && out_of_order(si->timetag, tt))
    //        return 0;
    lo_timetag tt = lo_message_get_timestamp(msg);

    if (is_instance_update) {
        index = msig_find_instance_with_remote_ids(sig, origin, public_id,
                                                   IN_RELEASED_REMOTELY);
        if (index < 0) {
            // no instance found with this map
            if (count == 1 && nulls == value_len) {
                // Don't activate instance just to release it again
                return 0;
            }
            // otherwise try to init reserved/stolen instance with device map
            index = msig_get_instance_with_remote_ids(sig, origin, public_id, 0, &tt);
            if (index < 0) {
                trace("no local instances available for remote instance %ld:%ld\n",
                      (long)origin, (long)public_id);
                return 0;
            }
        }
        else {
            if (sig->id_maps[index].status & IN_RELEASED_LOCALLY) {
                /* map was already released locally, we are only interested
                 * in release messages */
                if (count == 1 && nulls == sig->props.length) {
                    // we can clear signal's reference to map
                    map = sig->id_maps[index].map;
                    sig->id_maps[index].map = 0;
                    map->refcount_remote--;
                    if (map->refcount_remote <= 0 && map->refcount_local <= 0) {
                        mdev_remove_instance_id_map(md, map);
                    }
                }
                return 0;
            }
            else if (!sig->id_maps[index].instance) {
                trace("error: missing instance!\n");
                return 0;
            }
        }
    }
    else {
        index = 0;
        if (!sig->id_maps[0].instance)
            index = msig_get_instance_with_local_id(sig, 0, 1, &tt);
        if (index < 0)
            return 0;
    }
    mapper_signal_instance si = sig->id_maps[index].instance;
    map = sig->id_maps[index].map;

    // TODO: alter timetags for multicount updates with missing handler calls?
    if (!s) {
        si->timetag.sec = tt.sec;
        si->timetag.frac = tt.frac;
    }

    int size = (s ? mapper_type_size(s->connection->props.remote_type)
                : mapper_type_size(sig->props.type));
    void *out_buffer = count == 1 ? 0 : alloca(count * sig->props.length * size);
    int vals, out_count = 0;

    /* As currently implemented, instance release messages cannot be embedded in
     * multi-count messages. */
    for (i = 0, k = 0; i < count; i++) {
        // check if each update will result in a handler call
        // all nulls -> release instance, sig has no value, break "count"
        vals = 0;
        if (s) {
            mapper_connection c = s->connection;
            for (j = 0; j < c->props.remote_length; j++) {
                if (types[k] == 'N')
                    continue;
                memcpy(c->history[index].value + j * size, argv[k], size);
                memcpy(c->history[index].timetag + j * sizeof(mapper_timetag_t),
                       &tt, sizeof(mapper_timetag_t));
            }
            if (s->connection->props.cause_update
                && (vals = mapper_combiner_perform(cb, index))) {
                memcpy(si->value,
                       msig_history_value_pointer(c->parent->history[index]),
                       mapper_type_size(sig->props.type) * sig->props.length);
            }
        }
        else {
            for (j = 0; j < sig->props.length; j++, k++) {
                if (types[k] == 'N')
                    continue;
                memcpy(si->value + j * size, argv[k], size);
                si->has_value_flags[j / 8] |= 1 << (j % 8);
                vals++;
            }
            if (memcmp(si->has_value_flags, sig->has_complete_value,
                       sig->props.length / 8 + 1)==0)
                si->has_value = 1;

            if (vals == 0 && si->has_value) {
                // protocol: update with all nulls sets signal has_value to 0
                si->has_value = 0;
                // TODO: need to use combiner here instead
                memset(si->has_value_flags, 0, sig->props.length / 8 + 1);
                if (is_instance_update) {
                    // TODO: handle multiple upstream devices
                    sig->id_maps[index].status |= IN_RELEASED_REMOTELY;
                    map->refcount_remote--;
                    if (sig->instance_event_handler
                        && (sig->instance_event_flags & IN_UPSTREAM_RELEASE)) {
                        sig->instance_event_handler(sig, &sig->props, map->local,
                                                    IN_UPSTREAM_RELEASE, &tt);
                    }
                    else
                        sig->handler(sig, &sig->props, map->local, 0, 1, &tt);
                }
                else
                    sig->handler(sig, &sig->props, map->local, 0, 1, &tt);
                /* Do not call mdev_route_signal() here, since we don't know if
                 * the local signal instance will actually be released. */
            }
        }

        if (s) {
            if (s->connection->props.cause_update) {
                if (count > 1) {
                    memcpy(out_buffer + out_count * sig->props.length * size,
                           si->value, size);
                    out_count++;
                }
                else {
                    sig->handler(sig, &sig->props, map->local, si->value, 1, &tt);
                    if (!sig->props.is_output)
                        mdev_route_signal(md, sig, index, si->value, 1, tt);
                }
                si->has_value = 1;
            }
        }
        else if (si->has_value) {
            if (count > 1) {
                memcpy(out_buffer + out_count * sig->props.length * size,
                       si->value, size);
                out_count++;
            }
            else {
                sig->handler(sig, &sig->props, map->local, si->value, 1, &tt);
                if (!sig->props.is_output)
                    mdev_route_signal(md, sig, index, si->value, 1, tt);
            }
            si->has_value = 1;
        }
    }
    if (out_count) {
        sig->handler(sig, &sig->props, map->local, out_buffer, out_count, &tt);
        if (!sig->props.is_output)
            mdev_route_signal(md, sig, index, out_buffer, out_count, tt);
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

    if (!sig->instance_event_handler ||
        !(sig->instance_event_flags & IN_DOWNSTREAM_RELEASE))
        return 0;

    lo_timetag tt = lo_message_get_timestamp(msg);

    int index = msig_find_instance_with_remote_ids(sig, argv[0]->i32, argv[1]->i32, 0);
    if (index < 0)
        return 0;

    if (sig->instance_event_handler) {
        sig->instance_event_handler(sig, &sig->props, sig->id_maps[index].map->local,
                                    IN_DOWNSTREAM_RELEASE, &tt);
    }

    return 0;
}

static int handler_query(const char *path, const char *types,
                         lo_arg **argv, int argc, lo_message msg,
                         void *user_data)
{
    mapper_signal sig = (mapper_signal) user_data;
    mapper_device md = sig->device;
    int length = sig->props.length;
    char type = sig->props.type;

    if (!md) {
        trace("error, sig->device==0\n");
        return 0;
    }

    if (!argc)
        return 0;
    else if (types[0] != 's' && types[0] != 'S')
        return 0;

    int i, j, sent = 0;

    // respond with same timestamp as query
    // TODO: should we also include actual timestamp for signal value?
    lo_timetag tt = lo_message_get_timestamp(msg);
    lo_bundle b = lo_bundle_new(tt);

    // query response string is first argument
    const char *response_path = &argv[0]->s;

    // vector length and data type may also be provided
    if (argc >= 3) {
        if (types[1] == 'i')
            length = argv[1]->i32;
        if (types[2] == 'c')
            type = argv[2]->c;
    }

    mapper_signal_instance si;
    for (i = 0; i < sig->id_map_length; i++) {
        if (!(si = sig->id_maps[i].instance))
            continue;
        lo_message m = lo_message_new();
        if (!m)
            continue;
        message_add_coerced_signal_instance_value(m, sig, si, length, type);
        if (sig->props.num_instances > 1) {
            lo_message_add_string(m, "@instance");
            lo_message_add_int32(m, (long)sig->id_maps[i].map->origin);
            lo_message_add_int32(m, (long)sig->id_maps[i].map->public);
        }
        lo_bundle_add_message(b, response_path, m);
        sent++;
    }
    if (!sent) {
        // If there are no active instances, send a single null response
        lo_message m = lo_message_new();
        if (m) {
            for (j = 0; j < length; j++)
                lo_message_add_nil(m);
            lo_bundle_add_message(b, response_path, m);
        }
    }

    lo_send_bundle(lo_message_get_source(msg), b);
    lo_bundle_free_messages(b);
    return 0;
}

// Add an input signal to a mapper device.
mapper_signal mdev_add_input(mapper_device md, const char *name, int length,
                             char type, const char *unit,
                             void *minimum, void *maximum,
                             mapper_signal_update_handler *handler,
                             void *user_data)
{
    if (mdev_get_signal_by_name(md, name, 0))
        return 0;
    char *signal_get = 0;
    mapper_signal sig = msig_new(name, length, type, 0, unit, minimum,
                                 maximum, handler, user_data);
    if (!sig)
        return 0;
    md->props.num_inputs++;
    grow_ptr_array((void **) &md->inputs, md->props.num_inputs,
                   &md->n_alloc_inputs);

    mdev_increment_version(md);

    md->inputs[md->props.num_inputs - 1] = sig;
    sig->device = md;

    lo_server_add_method(md->server, sig->props.name, NULL, handler_signal,
                         (void *) (sig));

    int len = strlen(sig->props.name) + 5;
    signal_get = (char*) realloc(signal_get, len);
    snprintf(signal_get, len, "%s%s", sig->props.name, "/get");
    lo_server_add_method(md->server, signal_get, NULL,
                         handler_query, (void *) (sig));

    free(signal_get);

    if (md->registered) {
        sig->props.device_name = (char *)mdev_name(md);
        // Notify subscribers
        mapper_admin_set_bundle_dest_subscribers(md->admin, SUB_DEVICE_INPUTS);
        mapper_admin_send_signal(md->admin, md, sig);
    }

    return sig;
}

// Add an output signal to a mapper device.
mapper_signal mdev_add_output(mapper_device md, const char *name, int length,
                              char type, const char *unit, void *minimum, void *maximum)
{
    if (mdev_get_signal_by_name(md, name, 0))
        return 0;
    mapper_signal sig = msig_new(name, length, type, 1, unit, minimum,
                                 maximum, 0, 0);
    if (!sig)
        return 0;
    md->props.num_outputs++;
    grow_ptr_array((void **) &md->outputs, md->props.num_outputs,
                   &md->n_alloc_outputs);

    mdev_increment_version(md);

    md->outputs[md->props.num_outputs - 1] = sig;
    sig->device = md;

    if (md->registered) {
        sig->props.device_name = (char *)mdev_name(md);
        // Notify subscribers
        mapper_admin_set_bundle_dest_subscribers(md->admin, SUB_DEVICE_OUTPUTS);
        mapper_admin_send_signal(md->admin, md, sig);
    }

    return sig;
}

void mdev_add_signal_methods(mapper_device md, mapper_signal sig)
{
    // TODO: handle adding and removing input signal methods also?
    if (!sig->props.is_output)
        return;

    char *path = 0;
    lo_server_add_method(md->server, sig->props.name, NULL, handler_signal,
                         (void *) (sig));

    int len = strlen(sig->props.name) + 5;
    path = (char*) realloc(path, len);
    snprintf(path, len, "%s%s", sig->props.name, "/get");
    lo_server_add_method(md->server, path, NULL,
                         handler_query, (void *) (sig));
    snprintf(path, len, "%s%s", sig->props.name, "/got");
    lo_server_add_method(md->server, path, NULL, handler_signal,
                         (void *) (sig));

    free(path);
    md->n_output_callbacks ++;
}

void mdev_remove_signal_methods(mapper_device md, mapper_signal sig)
{
    char *path = 0;
    int len, i;
    if (!md || !sig)
        return;
    for (i=0; i<md->props.num_outputs; i++) {
        if (md->outputs[i] == sig)
            break;
    }
    if (i==md->props.num_outputs)
        return;

    len = (int) strlen(sig->props.name) + 5;
    path = (char*) realloc(path, len);
    snprintf(path, len, "%s%s", sig->props.name, "/got");
    lo_server_del_method(md->server, path, NULL);
    md->n_output_callbacks --;
}

void mdev_add_instance_release_request_callback(mapper_device md, mapper_signal sig)
{
    if (!sig->props.is_output)
        return;

    // TODO: use normal release message?
    lo_server_add_method(md->server, sig->props.name, "iiF",
                         handler_instance_release_request, (void *) (sig));
    md->n_output_callbacks ++;
}

void mdev_remove_instance_release_request_callback(mapper_device md, mapper_signal sig)
{
    int i;
    if (!md || !sig)
        return;
    for (i=0; i<md->props.num_outputs; i++) {
        if (md->outputs[i] == sig)
            break;
    }
    if (i==md->props.num_outputs)
        return;
    lo_server_del_method(md->server, sig->props.name, "iiF");
    md->n_output_callbacks --;
}

void mdev_remove_input(mapper_device md, mapper_signal sig)
{
    int i, n;
    char str1[1024], str2[1024];
    for (i=0; i<md->props.num_inputs; i++) {
        if (md->inputs[i] == sig)
            break;
    }
    if (i==md->props.num_inputs)
        return;

    for (n=i; n<(md->props.num_inputs-1); n++) {
        md->inputs[n] = md->inputs[n+1];
    }

    lo_server_del_method(md->server, sig->props.name, NULL);

    snprintf(str1, 1024, "%s/get", sig->props.name);
    lo_server_del_method(md->server, str1, NULL);

    msig_full_name(sig, str1, 1024);
    mapper_router_signal rs = md->router->signals;
    while (rs) {
        if (rs->signal == sig) {
            // need to disconnect
            mapper_connection c = rs->connections;
            while (c) {
                mapper_admin_set_bundle_dest_mesh(md->admin, c->link->admin_addr);
                if (!c->link->self_link) {
                    snprintf(str2, 1024, "%s%s", c->link->props.remote_name,
                             c->props.remote_name);
                    if (c->props.direction == DI_OUTGOING)
                        mapper_admin_bundle_message(md->admin, ADM_DISCONNECT,
                                                    0, "ss", str1, str2);
                    else
                        mapper_admin_bundle_message(md->admin, ADM_DISCONNECT,
                                                    0, "ss", str2, str1);
                }
                mapper_connection temp = c->next;
                mapper_router_remove_connection(md->router, c);
                c = temp;
            }
            break;
        }
        rs = rs->next;
    }

    if (md->registered) {
        // Notify subscribers
        mapper_admin_set_bundle_dest_subscribers(md->admin, SUB_DEVICE_INPUTS);
        mapper_admin_send_signal_removed(md->admin, md, sig);
    }

    md->props.num_inputs --;
    mdev_increment_version(md);
    msig_free(sig);
}

void mdev_remove_output(mapper_device md, mapper_signal sig)
{
    int i, n;
    char str1[1024], str2[1024];
    for (i=0; i<md->props.num_outputs; i++) {
        if (md->outputs[i] == sig)
            break;
    }
    if (i==md->props.num_outputs)
        return;

    for (n=i; n<(md->props.num_outputs-1); n++) {
        md->outputs[n] = md->outputs[n+1];
    }
    if (sig->handler) {
        snprintf(str1, 1024, "%s/got", sig->props.name);
        lo_server_del_method(md->server, str1, NULL);
    }
    if (sig->instance_event_handler &&
        (sig->instance_event_flags & IN_DOWNSTREAM_RELEASE)) {
        lo_server_del_method(md->server, sig->props.name, "iiF");
    }

    msig_full_name(sig, str1, 1024);
    mapper_router_signal rs = md->router->signals;
    while (rs) {
        if (rs->signal == sig) {
            // need to disconnect
            mapper_connection c = rs->connections;
            while (c) {
                if (!c->link->self_link) {
                    mapper_admin_set_bundle_dest_mesh(md->admin, c->link->admin_addr);
                    snprintf(str2, 1024, "%s%s", c->link->props.remote_name,
                             c->props.remote_name);
                    if (c->props.direction == DI_OUTGOING)
                        mapper_admin_bundle_message(md->admin, ADM_DISCONNECT, 0,
                                                    "ss", str1, str2);
                    else
                        mapper_admin_bundle_message(md->admin, ADM_DISCONNECT, 0,
                                                    "ss", str2, str1);
                }
                mapper_connection temp = c->next;
                mapper_router_remove_connection(md->router, c);
                c = temp;
            }
            break;
        }
        rs = rs->next;
    }

    if (md->registered) {
        // Notify subscribers
        mapper_admin_set_bundle_dest_subscribers(md->admin, SUB_DEVICE_OUTPUTS);
        mapper_admin_send_signal_removed(md->admin, md, sig);
    }

    md->props.num_outputs --;
    mdev_increment_version(md);
    msig_free(sig);
}

int mdev_num_inputs(mapper_device md)
{
    return md->props.num_inputs;
}

int mdev_num_outputs(mapper_device md)
{
    return md->props.num_outputs;
}

int mdev_num_links(mapper_device md)
{
    return md->props.num_links;
}

int mdev_num_connections_in(mapper_device md)
{
    mapper_link l = md->router->links;
    int count = 0;
    while (l) {
        count += l->num_connections_in;
        l = l->next;
    }
    return count;
}

int mdev_num_connections_out(mapper_device md)
{
    mapper_link l = md->router->links;
    int count = 0;
    while (l) {
        count += l->num_connections_out;
        l = l->next;
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

mapper_signal mdev_get_signal_by_name(mapper_device md, const char *name,
                                      int *index)
{
    mapper_signal sig = mdev_get_input_by_name(md, name, index);
    if (sig)
        return sig;
    sig = mdev_get_output_by_name(md, name, index);
    return sig;
}

mapper_signal mdev_get_input_by_name(mapper_device md, const char *name,
                                     int *index)
{
    int i, slash;
    if (!name)
        return 0;

    slash = name[0]=='/' ? 1 : 0;
    for (i=0; i<md->props.num_inputs; i++)
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
    int i, slash;
    if (!name)
        return 0;

    slash = name[0]=='/' ? 1 : 0;
    for (i=0; i<md->props.num_outputs; i++)
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
    if (index >= 0 && index < md->props.num_inputs)
        return md->inputs[index];
    return 0;
}

mapper_signal mdev_get_output_by_index(mapper_device md, int index)
{
    if (index >= 0 && index < md->props.num_outputs)
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
        while (count < (md->props.num_inputs + md->n_output_callbacks)*1
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

int mdev_num_fds(mapper_device md)
{
    // Two for the admin inputs (bus and mesh), and one for the signal input.
    return 3;
}

int mdev_get_fds(mapper_device md, int *fds, int num)
{
    if (num > 0)
        fds[0] = lo_server_get_socket_fd(md->admin->bus_server);
    if (num > 1) {
        fds[1] = lo_server_get_socket_fd(md->admin->mesh_server);
        if (num > 2)
            fds[2] = lo_server_get_socket_fd(md->server);
        else
            return 2;
    }
    else
        return 1;
    return 3;
}

void mdev_service_fd(mapper_device md, int fd)
{
    // TODO: separate fds for bus and mesh comms
    if (fd == lo_server_get_socket_fd(md->admin->bus_server))
        mapper_admin_poll(md->admin);
    else if (md->server
             && fd == lo_server_get_socket_fd(md->server))
        lo_server_recv_noblock(md->server, 0);
}

void mdev_num_instances_changed(mapper_device md,
                                mapper_signal sig,
                                int size)
{
    if (!md)
        return;
    mapper_router_num_instances_changed(md->router, sig, size);
}

void mdev_route_signal(mapper_device md,
                       mapper_signal sig,
                       int instance_index,
                       void *value,
                       int count,
                       mapper_timetag_t timetag)
{
    mapper_router_process_signal(md->router, sig, instance_index,
                                 value, count, timetag);
}

// Function to start a mapper queue
void mdev_start_queue(mapper_device md, mapper_timetag_t tt)
{
    if (!md)
        return;
    mapper_router_start_queue(md->router, tt);
}

void mdev_send_queue(mapper_device md, mapper_timetag_t tt)
{
    if (!md)
        return;
    mapper_router_send_queue(md->router, tt);
}

int mdev_route_query(mapper_device md, mapper_signal sig,
                     mapper_timetag_t tt)
{
    return mapper_router_send_query(md->router, sig, tt);
}

void mdev_reserve_instance_id_map(mapper_device dev)
{
    mapper_id_map map;
    map = (mapper_id_map)calloc(1, sizeof(struct _mapper_id_map));
    map->next = dev->reserve_id_map;
    dev->reserve_id_map = map;
}

mapper_id_map mdev_add_instance_id_map(mapper_device dev, int local_id,
                                       int origin, int public_id)
{
    if (!dev->reserve_id_map)
        mdev_reserve_instance_id_map(dev);

    mapper_id_map map = dev->reserve_id_map;
    map->local = local_id;
    map->origin = origin;
    map->public = public_id;
    map->refcount_local = 0;
    map->refcount_remote = 0;
    dev->reserve_id_map = map->next;
    map->next = dev->active_id_map;
    dev->active_id_map = map;
    return map;
}

void mdev_remove_instance_id_map(mapper_device dev, mapper_id_map map)
{
    mapper_id_map *id_map = &dev->active_id_map;
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

mapper_id_map mdev_find_instance_id_map_by_local(mapper_device dev,
                                                 int local_id)
{
    mapper_id_map map = dev->active_id_map;
    while (map) {
        if (map->local == local_id)
            return map;
        map = map->next;
    }
    return 0;
}

mapper_id_map mdev_find_instance_id_map_by_remote(mapper_device dev,
                                                  int origin, int public_id)
{
    mapper_id_map map = dev->active_id_map;
    while (map) {
        if (map->origin == origin && map->public == public_id)
            return map;
        map = map->next;
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

void mdev_start_server(mapper_device md, int starting_port)
{
    if (!md->server) {
        int i;
        char port[16], *pport = port, *path = 0;

        if (starting_port)
            sprintf(port, "%d", starting_port);
        else
            pport = 0;

        while (!(md->server = lo_server_new(pport, liblo_error_handler))) {
            pport = 0;
        }

        // Disable liblo message queueing
        lo_server_enable_queue(md->server, 0, 1);

        md->props.port = lo_server_get_port(md->server);
        trace("bound to port %i\n", md->props.port);

        for (i = 0; i < md->props.num_inputs; i++) {
            lo_server_add_method(md->server, md->inputs[i]->props.name, NULL,
                                 handler_signal, (void *) (md->inputs[i]));

            int len = (int) strlen(md->inputs[i]->props.name) + 5;
            path = (char*) realloc(path, len);
            snprintf(path, len, "%s%s", md->inputs[i]->props.name, "/get");
            lo_server_add_method(md->server, path, NULL, handler_query,
                                 (void *) (md->inputs[i]));
        }
        for (i = 0; i < md->props.num_outputs; i++) {
            if (md->outputs[i]->handler) {
                int len = (int) strlen(md->outputs[i]->props.name) + 5;
                path = (char*) realloc(path, len);
                snprintf(path, len, "%s%s", md->outputs[i]->props.name, "/got");
                lo_server_add_method(md->server, path, NULL, handler_signal,
                                     (void *) (md->outputs[i]));
                md->n_output_callbacks ++;
            }
            if (md->outputs[i]->instance_event_handler &&
                (md->outputs[i]->instance_event_flags & IN_DOWNSTREAM_RELEASE)) {
                lo_server_add_method(md->server,
                                     md->outputs[i]->props.name,
                                     "iiF",
                                     handler_instance_release_request,
                                     (void *) (md->outputs[i]));
                md->n_output_callbacks ++;
            }
        }
        free(path);
    }
}

const char *mdev_name(mapper_device md)
{
    if (!md->registered || !md->ordinal.locked)
        return 0;

    if (md->props.name)
        return md->props.name;

    unsigned int len = strlen(md->props.identifier) + 6;
    md->props.name = (char *) malloc(len);
    md->props.name[0] = 0;
    snprintf(md->props.name, len, "/%s.%d", md->props.identifier,
             md->ordinal.value);
    return md->props.name;
}

unsigned int mdev_id(mapper_device md)
{
    if (md->registered)
        return md->props.name_hash;
    else
        return 0;
}

unsigned int mdev_port(mapper_device md)
{
    if (md->registered)
        return md->props.port;
    else
        return 0;
}

const struct in_addr *mdev_ip4(mapper_device md)
{
    if (md->registered)
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
    if (md->registered)
        return md->ordinal.value;
    else
        return 0;
}

int mdev_ready(mapper_device device)
{
    if (!device)
        return 0;

    return device->registered;
}

mapper_device_props mdev_properties(mapper_device dev)
{
    return &dev->props;
}

void mdev_set_property(mapper_device dev, const char *property,
                       char type, void *value, int length)
{
    if (strcmp(property, "name") == 0 ||
        strcmp(property, "host") == 0 ||
        strcmp(property, "port") == 0 ||
        strcmp(property, "user_data") == 0) {
        trace("Cannot set locked device property '%s'\n", property);
        return;
    }
    mapper_table_add_or_update_typed_value(dev->props.extra, property,
                                           type, value, length);
}

int mdev_property_lookup(mapper_device dev, const char *property,
                         char *type, const void **value, int *length)
{
    return mapper_db_device_property_lookup(&dev->props, property,
                                            type, value, length);
}

void mdev_remove_property(mapper_device dev, const char *property)
{
    table_remove_key(dev->props.extra, property, 1);
}

lo_server mdev_get_lo_server(mapper_device md)
{
    return md->server;
}

void mdev_now(mapper_device dev, mapper_timetag_t *timetag)
{
    mapper_clock_now(&dev->admin->clock, timetag);
}

void mdev_set_link_callback(mapper_device dev,
                            mapper_device_link_handler *h, void *user)
{
    dev->link_cb = h;
    dev->link_cb_userdata = user;
}

void mdev_set_connection_callback(mapper_device dev,
                                  mapper_device_connection_handler *h,
                                  void *user)
{
    dev->connection_cb = h;
    dev->connection_cb_userdata = user;
}

int mdev_get_signal_slot(mapper_device dev)
{
    ++dev->signal_slot_counter;
    if (dev->signal_slot_counter < 0)
        dev->signal_slot_counter = 0;
    return dev->signal_slot_counter;
}
