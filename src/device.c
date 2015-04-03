
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

extern const char* admin_msg_strings[N_ADM_STRINGS];

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
    md->props.description = 0;
    md->props.name_hash = 0;
    md->props.lib_version = PACKAGE_VERSION;
    md->ordinal.value = 1;
    md->ordinal.locked = 0;
    int i;
    for (i = 0; i < 8; i++) {
        md->ordinal.suggestion[i] = 0.;
    }
    md->registered = 0;
    md->active_id_map = 0;
    md->reserve_id_map = 0;
    md->id_counter = 0;
    md->props.extra = table_new();
    md->flags = 0;
    md->signal_slot_counter = -1;

    md->router = (mapper_router) calloc(1, sizeof(struct _mapper_router));
    md->router->device = md;

    md->link_timeout_sec = ADMIN_TIMEOUT_SEC;
//    md->link_timeout_sec = 0;

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

    if (md->outputs) {
        while (md->props.num_outputs) {
            mdev_remove_output(md, md->outputs[0]);
        }
        free(md->outputs);
    }
    if (md->inputs) {
        while (md->props.num_inputs) {
            mdev_remove_input(md, md->inputs[0]);
        }
        free(md->inputs);
    }

    // Links reference parent signals so release them first
    while (md->router->links)
        mapper_router_remove_link(md->router, md->router->links);

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
    if (md->props.description)
        free(md->props.description);
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
        for (j = 0; j < md->inputs[i]->id_map_length; j++) {
            if (md->inputs[i]->id_maps[j].map &&
                md->inputs[i]->id_maps[j].map->origin == 0)
                md->inputs[i]->id_maps[j].map->origin = md->props.name_hash;
        }
    }
    for (i = 0; i < md->props.num_outputs; i++) {
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
#ifdef DEBUG
        printf("error: unexpected length.\n");
#endif
        return 0;
    }
    for (i = 0; i < len; i++) {
        if (types[i] != type && types[i] != 'N') {
#ifdef DEBUG
            printf("error: unexpected typestring (expected %c%i).\n", type, len);
#endif
            return 0;
        }
    }
    return len / vector_len;
}

/* Notes:
 * - Incoming signal values may be scalars or vectors, but much match the
 *   length of the target signal or connection slot.
 * - Vectors are of homogeneous type ('i', 'f' or 'd') however individual
 *   elements may have no value (type 'N')
 * - A vector consisting completely of nulls indicates a signal instance release
 * - Updates to a specific signal instance are indicated using the label
 *   "@instance" followed by two integers which uniquely identify this instance
 *   within the network of libmapper devices
 * - Updates to specific "slots" of a convergent (i.e. multi-source) connection
 *   are indicated using the label "@slot" followed by a single integer slot #
 * - Multiple "samples" of a signal value may be packed into a single message
 * - In future updates, instance release may be triggered by expression eval
 */
static int handler_signal(const char *path, const char *types,
                          lo_arg **argv, int argc, lo_message msg,
                          void *user_data)
{
    mapper_signal sig = (mapper_signal) user_data;
    mapper_device md;
    int i = 0, j, k, count = 1, nulls = 0;
    int is_instance_update = 0, map_index, slot = -1;
    int instance_group, instance_id;
    mapper_id_map map;
    mapper_connection c = 0;
    mapper_connection_slot s = 0;

    if (!sig || !(md = sig->device)) {
#ifdef DEBUG
        printf("error in handler_signal, cannot retrieve user_data\n");
#endif
        return 0;
    }

    if (!argc)
        return 0;

    // We need to consider that there may be properties appended to the msg
    // check length and find properties if any
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
#ifdef DEBUG
            printf("error in handler_signal: unexpected argument type.\n");
#endif
            return 0;
        }
        if (strcmp(&argv[argnum]->s, "@instance") == 0 && argc >= argnum + 3) {
            if (types[argnum+1] != 'i' || types[argnum+2] != 'i') {
#ifdef DEBUG
                printf("error in handler_signal: bad arguments "
                       "for @instance property.\n");
#endif
                return 0;
            }
            is_instance_update = 1;
            instance_group = argv[argnum+1]->i32;
            instance_id = argv[argnum+2]->i32;
            argnum += 3;
        }
        else if (strcmp(&argv[argnum]->s, "@slot") == 0 && argc >= argnum + 2) {
            if (types[argnum+1] != 'i') {
#ifdef DEBUG
                printf("error in handler_signal: bad arguments "
                       "for @slot property.\n");
#endif
                return 0;
            }
            slot = argv[argnum+1]->i32;
            argnum += 2;
        }
        else {
#ifdef DEBUG
            printf("error in handler_signal: unknown property name '%s'.\n",
                   &argv[argnum]->s);
#endif
            return 0;
        }
    }

    if (slot >= 0) {
        // check if we have a combiner for this signal
        // retrieve connection associated with this slot
        s = mapper_router_find_connection_slot(md->router, sig, slot);
        if (!s) {
#ifdef DEBUG
            printf("error in handler_signal: slot %d not found.\n", slot);
#endif
            return 0;
        }
        c = s->connection;
        if (c->status < MAPPER_READY) {
#ifdef DEBUG
            printf("error in handler_signal: connection not yet ready.\n");
#endif
            return 0;
        }
        if (!c->expr) {
#ifdef DEBUG
            printf("error in handler_signal: missing expression.\n");
#endif
            return 0;
        }
        if (c->props.process_location == MAPPER_DESTINATION) {
            count = check_types(types, value_len, s->props->type, s->props->length);
        }
        else {
            // value has already been processed at source device
            c = 0;
            count = check_types(types, value_len, sig->props.type, sig->props.length);
        }
    }
    else {
        count = check_types(types, value_len, sig->props.type, sig->props.length);
    }

    if (!count)
        return 0;

    // TODO: optionally discard out-of-order messages
    // requires timebase sync for many-to-one connections or local updates
    //    if (sig->discard_out_of_order && out_of_order(si->timetag, tt))
    //        return 0;
    lo_timetag tt = lo_message_get_timestamp(msg);

    if (is_instance_update) {
        map_index = msig_find_instance_with_remote_ids(sig, instance_group,
                                                       instance_id,
                                                       IN_RELEASED_LOCALLY);
        if (map_index < 0) {
            // no instance found with this map
            if (nulls == value_len * count) {
                // Don't activate instance just to release it again
                return 0;
            }
            // otherwise try to init reserved/stolen instance with device map
            map_index = msig_get_instance_with_remote_ids(sig, instance_group,
                                                          instance_id, 0, &tt);
            if (map_index < 0) {
#ifdef DEBUG
                printf("no local instances available for remote instance %ld:%ld\n",
                       (long)instance_group, (long)instance_id);
#endif
                return 0;
            }
        }
        else {
            if (sig->id_maps[map_index].status & IN_RELEASED_LOCALLY) {
                /* map was already released locally, we are only interested
                 * in release messages */
                if (count == 1 && nulls == value_len) {
                    // we can clear signal's reference to map
                    map = sig->id_maps[map_index].map;
                    sig->id_maps[map_index].map = 0;
                    map->refcount_remote--;
                    if (map->refcount_remote <= 0 && map->refcount_local <= 0) {
                        mdev_remove_instance_id_map(md, map);
                    }
                }
                return 0;
            }
            else if (!sig->id_maps[map_index].instance) {
#ifdef DEBUG
                printf("error in handler_signal: missing instance!\n");
#endif
                return 0;
            }
        }
    }
    else {
        map_index = 0;
        if (!sig->id_maps[0].instance)
            map_index = msig_get_instance_with_local_id(sig, 0, 1, &tt);
        if (map_index < 0)
            return 0;
    }
    mapper_signal_instance si = sig->id_maps[map_index].instance;
    map = sig->id_maps[map_index].map;

    int size = (s ? mapper_type_size(s->props->type)
                : mapper_type_size(sig->props.type));
    void *out_buffer = alloca(count * value_len * size);
    int vals, out_count = 0, active = 1;

    if (c) {
        for (i = 0, k = 0; i < count; i++) {
            vals = 0;
            for (j = 0; j < s->props->length; j++, k++) {
                vals += (types[k] != 'N');
            }
            /* partial vector updates not allowed in convergent connections
             * since slot value mirrors remote signal value. */
            // vals is allowed to equal 0 or s->props->length (for release)
            if (vals == 0) {
                if (count > 1) {
#ifdef DEBUG
                    printf("error in handler_signal: instance release cannot "
                           "be embedded in multi-count update");
#endif
                    return 0;
                }
                if (is_instance_update) {
                    sig->id_maps[map_index].status |= IN_RELEASED_REMOTELY;
                    map->refcount_remote--;
                    if (map->refcount_remote <= 0 && map->refcount_local <= 0) {
                        mdev_remove_instance_id_map(md, map);
                    }
                    if (sig->instance_event_handler
                        && (sig->instance_event_flags & IN_UPSTREAM_RELEASE)) {
                        sig->instance_event_handler(sig, &sig->props, map->local,
                                                    IN_UPSTREAM_RELEASE, &tt);
                    }
                }
                /* Do not call mdev_route_signal() here, since we don't know if
                 * the local signal instance will actually be released. */
                if (sig->handler) {
                    sig->handler(sig, &sig->props, map->local, 0, 1, &tt);
                }
                continue;
            }
            else if (vals != s->props->length) {
#ifdef DEBUG
                printf("error in handler_signal: partial vector update applied "
                       "to convergent connection slot.");
#endif
                return 0;
            }
            else if (is_instance_update && !active) {
                // may need to activate instance
                map_index = msig_find_instance_with_remote_ids(sig, instance_group,
                                                               instance_id,
                                                               IN_RELEASED_REMOTELY);
                if (map_index < 0) {
                    // no instance found with this map
                    if (nulls == value_len * count) {
                        // Don't activate instance just to release it again
                        return 0;
                    }
                    // otherwise try to init reserved/stolen instance with device map
                    map_index = msig_get_instance_with_remote_ids(sig, instance_group,
                                                                  instance_id, 0, &tt);
                    if (map_index < 0) {
#ifdef DEBUG
                        printf("no local instances available for remote instance %ld:%ld\n",
                               (long)instance_group, (long)instance_id);
#endif
                        return 0;
                    }
                }
                si = sig->id_maps[map_index].instance;
                map = sig->id_maps[map_index].map;
            }
            s->history[si->index].position = ((s->history[si->index].position + 1)
                                             % s->history[si->index].size);
            memcpy(mapper_history_value_ptr(s->history[si->index]),
                   argv[i*count], size * s->props->length);
            memcpy(mapper_history_tt_ptr(s->history[si->index]),
                   &tt, sizeof(mapper_timetag_t));
            if (s->props->cause_update) {
                char typestring[c->destination.props->length];
                mapper_history sources[c->props.num_sources];
                for (j = 0; j < c->props.num_sources; j++)
                    sources[j] = &c->sources[j].history[si->index];
                if (!mapper_expr_evaluate(c->expr, sources, &c->expr_vars[si->index],
                                          &c->destination.history[si->index], &tt,
                                          typestring)) {
                    continue;
                }
                // TODO: check if expression has triggered instance-release
                if (mapper_boundary_perform(&c->destination.history[si->index],
                                            &c->props.destination, typestring)) {
                    continue;
                }
                void *result = mapper_history_value_ptr(c->destination.history[si->index]);
                vals = 0;
                for (j = 0; j < c->destination.props->length; j++) {
                    if (typestring[j] == 'N')
                        continue;
                    memcpy(si->value + j * size, result + j * size, size);
                    si->has_value_flags[j / 8] |= 1 << (j % 8);
                    vals++;
                }
                if (vals == 0) {
                    // try to release instance
                    // first call handler with value buffer
                    if (out_count) {
                        if (!(sig->props.direction & DI_OUTGOING))
                            mdev_route_signal(md, sig, map_index, out_buffer,
                                              out_count, tt);
                        if (sig->handler)
                            sig->handler(sig, &sig->props, map->local,
                                         out_buffer, out_count, &tt);
                        out_count = 0;
                    }
                    // next call handler with release
                    if (is_instance_update) {
                        sig->id_maps[map_index].status |= IN_RELEASED_REMOTELY;
                        map->refcount_remote--;
                        if (sig->instance_event_handler
                            && (sig->instance_event_flags & IN_UPSTREAM_RELEASE)) {
                            sig->instance_event_handler(sig, &sig->props, map->local,
                                                        IN_UPSTREAM_RELEASE, &tt);
                        }
                    }
                    /* Do not call mdev_route_signal() here, since we don't know if
                     * the local signal instance will actually be released. */
                    if (sig->handler)
                        sig->handler(sig, &sig->props, map->local, 0, 1, &tt);
                    // mark instance as possibly released
                    active = 0;
                    continue;
                }
                if (memcmp(si->has_value_flags, sig->has_complete_value,
                           sig->props.length / 8 + 1)==0) {
                    si->has_value = 1;
                }
                if (si->has_value) {
                    memcpy(&si->timetag, &tt, sizeof(mapper_timetag_t));
                    if (count > 1) {
                        memcpy(out_buffer + out_count * sig->props.length * size,
                               si->value, size);
                        out_count++;
                    }
                    else {
                        if (!(sig->props.direction & DI_OUTGOING))
                            mdev_route_signal(md, sig, map_index, si->value, 1, tt);
                        if (sig->handler)
                            sig->handler(sig, &sig->props, map->local, si->value, 1, &tt);
                    }
                }
            }
            else {
                continue;
            }
        }
        if (out_count) {
            if (!(sig->props.direction & DI_OUTGOING))
                mdev_route_signal(md, sig, map_index, out_buffer, out_count, tt);
            if (sig->handler)
                sig->handler(sig, &sig->props, map->local, out_buffer, out_count, &tt);
        }
    }
    else {
        for (i = 0, k = 0; i < count; i++) {
            vals = 0;
            for (j = 0; j < sig->props.length; j++, k++) {
                if (types[k] == 'N')
                    continue;
                memcpy(si->value + j * size, argv[k], size);
                si->has_value_flags[j / 8] |= 1 << (j % 8);
                vals++;
            }
            if (vals == 0) {
                if (count > 1) {
#ifdef DEBUG
                    printf("error in handler_signal: instance release cannot "
                           "be embedded in multi-count update");
#endif
                    return 0;
                }
                if (is_instance_update) {
                    sig->id_maps[map_index].status |= IN_RELEASED_REMOTELY;
                    map->refcount_remote--;
                    if (sig->instance_event_handler
                        && (sig->instance_event_flags & IN_UPSTREAM_RELEASE)) {
                        sig->instance_event_handler(sig, &sig->props, map->local,
                                                    IN_UPSTREAM_RELEASE, &tt);
                    }
                }
                /* Do not call mdev_route_signal() here, since we don't know if
                 * the local signal instance will actually be released. */
                if (sig->handler)
                    sig->handler(sig, &sig->props, map->local, 0, 1, &tt);
                return 0;
            }
            if (memcmp(si->has_value_flags, sig->has_complete_value,
                       sig->props.length / 8 + 1)==0) {
                si->has_value = 1;
            }
            if (si->has_value) {
                memcpy(&si->timetag, &tt, sizeof(mapper_timetag_t));
                if (count > 1) {
                    memcpy(out_buffer + out_count * sig->props.length * size,
                           si->value, size);
                    out_count++;
                }
                else {
                    if (!(sig->props.direction & DI_OUTGOING))
                        mdev_route_signal(md, sig, map_index, si->value, 1, tt);
                    if (sig->handler)
                        sig->handler(sig, &sig->props, map->local, si->value, 1, &tt);
                }
            }
        }
        if (out_count) {
            if (!(sig->props.direction & DI_OUTGOING))
                mdev_route_signal(md, sig, map_index, out_buffer, out_count, tt);
            if (sig->handler)
                sig->handler(sig, &sig->props, map->local, out_buffer, out_count, &tt);
        }
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
#ifdef DEBUG
        printf("error, sig->device==0\n");
#endif
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
    mapper_signal sig = msig_new(name, length, type, DI_INCOMING, unit, minimum,
                                 maximum, handler, user_data);
    if (!sig)
        return 0;
    md->props.num_inputs++;
    grow_ptr_array((void **) &md->inputs, md->props.num_inputs,
                   &md->n_alloc_inputs);

    mdev_increment_version(md);

    md->inputs[md->props.num_inputs - 1] = sig;
    sig->device = md;
    sig->props.device = &md->props;

    lo_server_add_method(md->server, sig->props.name, NULL, handler_signal,
                         (void *) (sig));

    int len = strlen(sig->props.name) + 5;
    signal_get = (char*) realloc(signal_get, len);
    snprintf(signal_get, len, "%s%s", sig->props.name, "/get");
    lo_server_add_method(md->server, signal_get, NULL,
                         handler_query, (void *) (sig));

    free(signal_get);

    if (md->registered) {
        // Notify subscribers
        mapper_admin_set_bundle_dest_subscribers(md->admin,
                                                 SUBSCRIBE_DEVICE_INPUTS);
        mapper_admin_send_signal(md->admin, md, sig);
    }

    return sig;
}

// Add an output signal to a mapper device.
mapper_signal mdev_add_output(mapper_device md, const char *name, int length,
                              char type, const char *unit,
                              void *minimum, void *maximum)
{
    if (mdev_get_signal_by_name(md, name, 0))
        return 0;
    mapper_signal sig = msig_new(name, length, type, DI_OUTGOING, unit, minimum,
                                 maximum, 0, 0);
    if (!sig)
        return 0;
    md->props.num_outputs++;
    grow_ptr_array((void **) &md->outputs, md->props.num_outputs,
                   &md->n_alloc_outputs);

    mdev_increment_version(md);

    md->outputs[md->props.num_outputs - 1] = sig;
    sig->device = md;
    sig->props.device = &md->props;

    if (md->registered) {
        // Notify subscribers
        mapper_admin_set_bundle_dest_subscribers(md->admin,
                                                 SUBSCRIBE_DEVICE_OUTPUTS);
        mapper_admin_send_signal(md->admin, md, sig);
    }

    return sig;
}

void mdev_add_signal_methods(mapper_device md, mapper_signal sig)
{
    // TODO: handle adding and removing input signal methods also?
    if (!(sig->props.direction & DI_OUTGOING))
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
    if (!(sig->props.direction & DI_OUTGOING))
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

static void send_disconnect(mapper_admin admin, mapper_connection c)
{
    if (!c->status)
        return;

    // TODO: send appropriate messages using mesh
//    mapper_admin_set_bundle_dest_mesh(admin, c->remote_dest->admin_addr);
    mapper_admin_set_bundle_dest_bus(admin);

    lo_message m = lo_message_new();
    if (!m)
        return;

    char dest_name[1024], source_names[1024];
    int i, len = 0, result;
    for (i = 0; i < c->props.num_sources; i++) {
        result = snprintf(&source_names[len], 1024-len, "%s%s",
                          c->sources[i].props->device_name,
                          c->sources[i].props->signal_name);
        if (result < 0 || (len + result + 1) >= 1024) {
            trace("Error encoding sources for combined /connected msg");
            lo_message_free(m);
            return;
        }
        lo_message_add_string(m, &source_names[len]);
        len += result + 1;
    }
    lo_message_add_string(m, "->");
    snprintf(dest_name, 1024, "%s%s", c->destination.props->device_name,
             c->destination.props->signal_name);
    lo_bundle_add_message(admin->bundle, admin_msg_strings[ADM_DISCONNECT], m);
    mapper_admin_send_bundle(admin);
}

void mdev_remove_input(mapper_device md, mapper_signal sig)
{
    int i, n;
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

    char str[1024];
    snprintf(str, 1024, "%s/get", sig->props.name);
    lo_server_del_method(md->server, str, NULL);

    mapper_router_signal rs = md->router->signals;
    while (rs && rs->signal != sig)
        rs = rs->next;
    if (rs) {
        // need to disconnect
        for (i = 0; i < rs->num_outgoing_slots; i++) {
            if (rs->outgoing_slots[i]) {
                mapper_connection c = rs->outgoing_slots[i]->connection;
                send_disconnect(md->admin, c);
                mapper_router_remove_connection(md->router, c);
            }
        }
        for (i = 0; i < rs->num_incoming_connections; i++) {
            if (rs->incoming_connections[i]) {
                send_disconnect(md->admin, rs->incoming_connections[i]);
                mapper_router_remove_connection(md->router,
                                                rs->incoming_connections[i]);
            }
        }
        mapper_router_remove_signal(md->router, rs);
    }

    if (md->registered) {
        // Notify subscribers
        mapper_admin_set_bundle_dest_subscribers(md->admin,
                                                 SUBSCRIBE_DEVICE_INPUTS);
        mapper_admin_send_signal_removed(md->admin, md, sig);
    }

    md->props.num_inputs --;
    mdev_increment_version(md);
    msig_free(sig);
}

void mdev_remove_output(mapper_device md, mapper_signal sig)
{
    int i, n;
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
        char str[1024];
        snprintf(str, 1024, "%s/got", sig->props.name);
        lo_server_del_method(md->server, str, NULL);
    }
    if (sig->instance_event_handler &&
        (sig->instance_event_flags & IN_DOWNSTREAM_RELEASE)) {
        lo_server_del_method(md->server, sig->props.name, "iiF");
    }

    mapper_router_signal rs = md->router->signals;
    while (rs && rs->signal != sig)
        rs = rs->next;
    if (rs) {
        // need to disconnect
        for (i = 0; i < rs->num_outgoing_slots; i++) {
            if (rs->outgoing_slots[i]) {
                mapper_connection c = rs->outgoing_slots[i]->connection;
                send_disconnect(md->admin, c);
                mapper_router_remove_connection(md->router, c);
            }
        }
        for (i = 0; i < rs->num_incoming_connections; i++) {
            if (rs->incoming_connections[i]) {
                send_disconnect(md->admin, rs->incoming_connections[i]);
                mapper_router_remove_connection(md->router,
                                                rs->incoming_connections[i]);
            }
        }
        mapper_router_remove_signal(md->router, rs);
    }

    if (md->registered) {
        // Notify subscribers
        mapper_admin_set_bundle_dest_subscribers(md->admin,
                                                 SUBSCRIBE_DEVICE_OUTPUTS);
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

int mdev_num_connections_in(mapper_device md)
{
    mapper_link l = md->router->links;
    int count = 0;
    while (l) {
        count += l->props.num_connections_in;
        l = l->next;
    }
    return count;
}

int mdev_num_connections_out(mapper_device md)
{
    mapper_link l = md->router->links;
    int count = 0;
    while (l) {
        count += l->props.num_connections_out;
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
    map->refcount_local = 1;
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
    if (strcmp(property, "host") == 0 ||
        strcmp(property, "libversion") == 0 ||
        strcmp(property, "name") == 0 ||
        strcmp(property, "num_connections_in") == 0 ||
        strcmp(property, "num_connections_out") == 0 ||
        strcmp(property, "num_inputs") == 0 ||
        strcmp(property, "num_outputs") == 0 ||
        strcmp(property, "port") == 0 ||
        strcmp(property, "synced") == 0 ||
        strcmp(property, "user_data") == 0 ||
        strcmp(property, "version") == 0) {
        trace("Cannot set locked device property '%s'\n", property);
        return;
    }
    else if (strcmp(property, "description")==0) {
        if (type == 's' && length == 1) {
            if (dev->props.description)
                free(dev->props.description);
            dev->props.description = strdup((const char*)value);
        }
        else if (!value || !length) {
            if (dev->props.description)
                free(dev->props.description);
        }
    }
    else
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
