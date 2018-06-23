#include <lo/lo.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>
#include <assert.h>
#include <sys/time.h>
#include <zlib.h>
#include <stddef.h>

#include "mapper_internal.h"
#include "types_internal.h"
#include "config.h"
#include <mapper/mapper.h>

#ifdef HAVE_PTHREAD
#include <pthread.h>
#endif

extern const char* network_msg_strings[NUM_MSG_STRINGS];

// prototypes
void mapper_device_start_servers(mapper_device dev);

void init_device_prop_table(mapper_device dev)
{
    
    dev->obj.props.mask = 0;
    dev->obj.props.synced = mapper_table_new();
    if (!dev->local)
        dev->obj.props.staged = mapper_table_new();
    int flags = dev->local ? NON_MODIFIABLE : MODIFIABLE;

    mapper_table tab = dev->obj.props.synced;

    // these properties need to be added in alphabetical order
    mapper_table_link(tab, MAPPER_PROP_ID, 1, MAPPER_INT64, &dev->obj.id, flags);

    mapper_table_link(tab, MAPPER_PROP_NAME, 1, MAPPER_STRING, &dev->name,
                      flags | INDIRECT | LOCAL_ACCESS_ONLY);

    mapper_table_link(tab, MAPPER_PROP_NUM_INPUTS, 1, MAPPER_INT32,
                      &dev->num_inputs, flags);

    mapper_table_link(tab, MAPPER_PROP_NUM_MAPS_IN, 1, MAPPER_INT32,
                      &dev->num_maps_in, flags);

    mapper_table_link(tab, MAPPER_PROP_NUM_MAPS_OUT, 1, MAPPER_INT32,
                      &dev->num_maps_out, flags);

    mapper_table_link(tab, MAPPER_PROP_NUM_OUTPUTS, 1, MAPPER_INT32,
                      &dev->num_outputs, flags);

    mapper_table_link(tab, MAPPER_PROP_ORDINAL, 1, MAPPER_INT32,
                      &dev->ordinal, flags);

    mapper_table_link(tab, MAPPER_PROP_STATUS, 1, MAPPER_INT32, &dev->status,
                      flags | LOCAL_ACCESS_ONLY);

    mapper_table_link(tab, MAPPER_PROP_SYNCED, 1, MAPPER_TIME, &dev->synced,
                      flags | LOCAL_ACCESS_ONLY);

    mapper_table_link(tab, MAPPER_PROP_USER_DATA, 1, MAPPER_PTR, &dev->obj.user,
                      LOCAL_MODIFY | INDIRECT | LOCAL_ACCESS_ONLY);

    mapper_table_link(tab, MAPPER_PROP_VERSION, 1, MAPPER_INT32,
                      &dev->obj.version, flags);

    if (dev->local) {
        mapper_table_set_record(tab, MAPPER_PROP_LIB_VERSION, NULL, 1,
                                MAPPER_STRING, PACKAGE_VERSION, NON_MODIFIABLE);
    }
    mapper_table_set_record(tab, MAPPER_PROP_IS_LOCAL, NULL, 1, MAPPER_BOOL,
                            &dev->local, LOCAL_ACCESS_ONLY | NON_MODIFIABLE);
}

/*! Allocate and initialize a mapper device. This function is called to create
 *  a new mapper_device, not to create a representation of remote devices. */
mapper_device mapper_device_new(const char *name_prefix, mapper_graph g)
{
    if (!name_prefix)
        return 0;
    if (!g) {
        g = mapper_graph_new(0);
        g->own = 0;
    }

    mapper_device dev;
    dev = (mapper_device)mapper_list_add_item((void**)&g->devs,
                                              sizeof(mapper_device_t));
    dev->obj.type = MAPPER_OBJ_DEVICE;
    dev->obj.graph = g;
    dev->local = (mapper_local_device)calloc(1, sizeof(mapper_local_device_t));

    init_device_prop_table(dev);

    dev->identifier = strdup(name_prefix);
    mapper_device_start_servers(dev);

    if (!g->net.server.udp || !g->net.server.tcp) {
        mapper_device_free(dev);
        return NULL;
    }

    if (name_prefix[0] == '/')
        ++name_prefix;
    if (strchr(name_prefix, '/')) {
        trace_dev(dev, "error: character '/' is not permitted in device name.\n");
        mapper_device_free(dev);
        return NULL;
    }

    dev->local->ordinal.val = 1;

    dev->local->rtr = (mapper_router)calloc(1, sizeof(mapper_router_t));
    dev->local->rtr->dev = dev;

    dev->local->link_timeout_sec = TIMEOUT_SEC;

    dev->local->idmaps.active = (mapper_idmap *) malloc(sizeof(mapper_idmap *));
    dev->local->idmaps.active[0] = 0;
    dev->local->num_signal_groups = 1;

    mapper_network_add_device(&g->net, dev);

    dev->status = STATUS_STAGED;

    return dev;
}

//! Free resources used by a mapper device.
void mapper_device_free(mapper_device dev)
{
    int i;
    if (!dev || !dev->local)
        return;
    if (!dev->obj.graph) {
        free(dev);
        return;
    }
    mapper_graph g = dev->obj.graph;
    mapper_network net = &g->net;

    // free any queued outgoing messages without sending
    mapper_network_free_msgs(net);

    // remove OSC handlers associated with this device
    mapper_network_remove_device_methods(net, dev);

    // remove subscribers
    mapper_subscriber s;
    while (dev->local->subscribers) {
        s = dev->local->subscribers;
        if (s->addr)
            lo_address_free(s->addr);
        dev->local->subscribers = s->next;
        free(s);
    }

    mapper_object *sigs = mapper_device_get_signals(dev, MAPPER_DIR_ANY);
    while (sigs) {
        mapper_signal sig = (mapper_signal)*sigs;
        sigs = mapper_object_list_next(sigs);
        if (sig->local) {
            // release active instances
            for (i = 0; i < sig->local->idmap_len; i++) {
                if (sig->local->idmaps[i].inst) {
                    mapper_signal_release_inst_internal(sig, i, MAPPER_NOW);
                }
            }
        }
        mapper_device_remove_signal(dev, sig);
    }

    if (dev->local->registered) {
        // A registered device must tell the network it is leaving.
        lo_message msg = lo_message_new();
        if (msg) {
            mapper_network_bus(net);
            lo_message_add_string(msg, mapper_device_get_name(dev));
            mapper_network_add_msg(net, 0, MSG_LOGOUT, msg);
            mapper_network_send(net);
        }
        else {
            trace_dev(dev, "couldn't allocate lo_message for /logout\n");
        }
    }

    // Release links to other devices
    mapper_object *links = mapper_device_get_links(dev, MAPPER_DIR_ANY);
    while (links) {
        mapper_link link = (mapper_link)*links;
        links = mapper_object_list_next(links);
        mapper_graph_remove_link(g, link, MAPPER_REMOVED);
    }

    // Release device id maps
    mapper_idmap map;
    for (i = 0; i < dev->local->num_signal_groups; i++) {
        while (dev->local->idmaps.active[i]) {
            map = dev->local->idmaps.active[i];
            dev->local->idmaps.active[i] = map->next;
            free(map);
        }
    }
    while (dev->local->idmaps.reserve) {
        map = dev->local->idmaps.reserve;
        dev->local->idmaps.reserve = map->next;
        free(map);
    }

    if (dev->local->rtr) {
        while (dev->local->rtr->sigs) {
            mapper_router_sig rs = dev->local->rtr->sigs;
            dev->local->rtr->sigs = dev->local->rtr->sigs->next;
            free(rs);
        }
        free(dev->local->rtr);
    }

    if (net->server.udp)
        lo_server_free(net->server.udp);
    if (net->server.tcp)
        lo_server_free(net->server.tcp);
    free(dev->local);

    if (dev->identifier)
        free(dev->identifier);

    if (g->own)
        mapper_graph_remove_device(g, dev, MAPPER_REMOVED, 1);
    else
        mapper_graph_free(g);
}

void mapper_device_on_registered(mapper_device dev)
{
    int i;
    /* Add unique device id to locally-activated signal instances. */
    mapper_object *sigs = mapper_device_get_signals(dev, MAPPER_DIR_ANY);
    while (sigs) {
        mapper_signal sig = (mapper_signal)*sigs;
        if (sig->local) {
            for (i = 0; i < sig->local->idmap_len; i++) {
                if (   sig->local->idmaps[i].map
                    && !(sig->local->idmaps[i].map->global >> 32)) {
                    sig->local->idmaps[i].map->global |= dev->obj.id;
                }
            }
            sig->obj.id |= dev->obj.id;
        }
        sigs = mapper_object_list_next(sigs);
    }
    dev->local->registered = 1;
    dev->ordinal = dev->local->ordinal.val;
    dev->status = STATUS_READY;
}

static void mapper_device_increment_version(mapper_device dev)
{
    ++dev->obj.version;
    dev->obj.props.synced->dirty = 1;
}

static int check_types(const mapper_type *types, int len, mapper_type type,
                       int vector_len)
{
    int i;
    if (len < vector_len || len % vector_len != 0) {
#ifdef DEBUG
        printf("error: unexpected length.\n");
#endif
        return 0;
    }
    for (i = 0; i < len; i++) {
        if (types[i] != type && types[i] != MAPPER_NULL) {
#ifdef DEBUG
            printf("error: unexpected typestring (expected %c%i).\n", type, len);
#endif
            return 0;
        }
    }
    return len / vector_len;
}

static void call_handler(mapper_signal sig, mapper_id inst, int len,
                         mapper_type type, const void *val, mapper_time_t *time,
                         float diff)
{
    mapper_signal_update_timing_stats(sig, diff);
    mapper_signal_update_handler *h = sig->local->update_handler;
    if (h)
        h(sig, inst, len, type, val, time);
}

/* Notes:
 * - Incoming signal values may be scalars or vectors, but much match the
 *   length of the target signal or mapping slot.
 * - Vectors are of homogeneous type (MAPPER_INT32, MAPPER_FLOAT or
 *   MAPPER_DOUBLE) however individual elements may have no value (type
 *   MAPPER_NULL)
 * - A vector consisting completely of nulls indicates a signal instance release
 *   TODO: use more specific message for release?
 * - Updates to a specific signal instance are indicated using the label
 *   "@instance" followed by two integers which uniquely identify this instance
 *   within the network of libmapper devices
 * - Updates to specific "slots" of a convergent (i.e. multi-source) mapping
 *   are indicated using the label "@slot" followed by a single integer slot #
 * - Multiple "samples" of a signal value may be packed into a single message
 * - In future updates, instance release may be triggered by expression eval
 */
static int handler_signal(const char *path, const char *types, lo_arg **argv,
                          int argc, lo_message msg, void *user_data)
{
    mapper_signal sig = (mapper_signal)user_data;
    mapper_device dev;
    int i = 0, j, k, count = 1, nulls = 0;
    int idmap_idx, slot_idx = -1;
    mapper_id global_id = 0;
    mapper_idmap idmap;
    mapper_map map = 0;
    mapper_slot slot = 0;

    if (!sig || !(dev = sig->dev)) {
#ifdef DEBUG
        printf("error in handler_signal, cannot retrieve user_data\n");
#endif
        return 0;
    }

    if (!sig->num_inst) {
#ifdef DEBUG
        trace_dev(dev, "signal '%s' has no instances.\n", sig->name);
#endif
        return 0;
    }

    if (!argc)
        return 0;

    mapper_instance_event_handler *event_h = sig->local->inst_event_handler;

    // We need to consider that there may be properties appended to the msg
    // check length and find properties if any
    int val_len = 0;
    while (val_len < argc && types[val_len] != MAPPER_STRING) {
        // count nulls here also to save time
        if (types[i] == MAPPER_NULL)
            ++nulls;
        ++val_len;
    }

    int argnum = val_len;
    while (argnum < argc) {
        // Parse any attached properties (instance ids, slot number)
        if (types[argnum] != MAPPER_STRING) {
#ifdef DEBUG
            trace_dev(dev, "error in handler_signal: unexpected argument "
                      "type.\n");
#endif
            return 0;
        }
        if (strcmp(&argv[argnum]->s,
                   mapper_prop_protocol_string(MAPPER_PROP_INSTANCE)) == 0
            && argc >= argnum + 2) {
            if (types[argnum+1] != MAPPER_INT64) {
#ifdef DEBUG
                trace_dev(dev, "error in handler_signal: bad arguments for "
                          "'instance' property.\n");
#endif
                return 0;
            }
            global_id = argv[argnum+1]->i64;
            argnum += 2;
        }
        else if (strcmp(&argv[argnum]->s,
                        mapper_prop_protocol_string(MAPPER_PROP_SLOT)) == 0
                 && argc >= argnum + 2) {
            if (types[argnum+1] != MAPPER_INT32) {
#ifdef DEBUG
                trace_dev(dev, "error in handler_signal: bad arguments for "
                          "'slot' property.\n");
#endif
                return 0;
            }
            slot_idx = argv[argnum+1]->i32;
            argnum += 2;
        }
        else {
#ifdef DEBUG
            trace_dev(dev, "error in handler_signal: unknown property name "
                      "'%s'.\n", &argv[argnum]->s);
#endif
            return 0;
        }
    }

    if (slot_idx >= 0) {
        // retrieve mapping associated with this slot
        slot = mapper_router_slot(dev->local->rtr, sig, slot_idx);
        if (!slot) {
#ifdef DEBUG
            trace_dev(dev, "error in handler_signal: slot %d not found.\n",
                      slot_idx);
#endif
            return 0;
        }
        map = slot->map;
        if (map->status < STATUS_READY) {
#ifdef DEBUG
            trace_dev(dev, "error in handler_signal: mapping not yet ready.\n");
#endif
            return 0;
        }
        if (!map->local->expr) {
#ifdef DEBUG
            trace_dev(dev, "error in handler_signal: missing expression.\n");
#endif
            return 0;
        }
        if (map->process_loc == MAPPER_LOC_DST) {
            count = check_types(types, val_len, slot->sig->type, slot->sig->len);
        }
        else {
            // value has already been processed at source device
            map = 0;
            count = check_types(types, val_len, sig->type, sig->len);
        }
    }
    else {
        count = check_types(types, val_len, sig->type, sig->len);
    }

    if (!count)
        return 0;

    // TODO: optionally discard out-of-order messages
    // requires timebase sync for many-to-one mappings or local updates
    //    if (sig->discard_out_of_order && out_of_order(si->time, t))
    //        return 0;
    lo_timetag t = lo_message_get_timestamp(msg);

    if (global_id) {
        idmap_idx = mapper_signal_find_inst_with_global_id(sig, global_id,
                                                           RELEASED_LOCALLY);
        if (idmap_idx < 0) {
            // no instance found with this map
            if (nulls == val_len * count) {
                // Don't activate instance just to release it again
                return 0;
            }
            // otherwise try to init reserved/stolen instance with device map
            idmap_idx = mapper_signal_inst_with_global_id(sig, global_id, 0, &t);
            if (idmap_idx < 0) {
#ifdef DEBUG
                trace_dev(dev, "no local instances available for global "
                          "instance id %"PR_MAPPER_ID"\n", global_id);
#endif
                return 0;
            }
        }
        else {
            if (sig->local->idmaps[idmap_idx].status & RELEASED_LOCALLY) {
                /* map was already released locally, we are only interested
                 * in release messages */
                if (count == 1 && nulls == val_len) {
                    // we can clear signal's reference to map
                    idmap = sig->local->idmaps[idmap_idx].map;
                    sig->local->idmaps[idmap_idx].map = 0;
                    --idmap->refcount_global;
                    if (idmap->refcount_global <= 0
                        && idmap->refcount_local <= 0) {
                        mapper_device_remove_idmap(dev, sig->local->group, idmap);
                    }
                }
                return 0;
            }
            else if (!sig->local->idmaps[idmap_idx].inst) {
#ifdef DEBUG
                trace_dev(dev, "error in handler_signal: missing instance!\n");
#endif
                return 0;
            }
        }
    }
    else {
        // use the first available instance
        idmap_idx = 0;
        if (!sig->local->idmaps[0].inst)
            idmap_idx = mapper_signal_inst_with_local_id(sig,
                                                         sig->local->inst[0]->id,
                                                         1, &t);
        if (idmap_idx < 0)
            return 0;
    }
    mapper_signal_inst si = sig->local->idmaps[idmap_idx].inst;
    int id = si->idx;
    float diff = mapper_time_difference(t, si->time);
    idmap = sig->local->idmaps[idmap_idx].map;

    int size = (slot ? mapper_type_size(slot->sig->type)
                : mapper_type_size(sig->type));
    void *out_buffer = alloca(count * val_len * size);
    int vals, out_count = 0, active = 1;

    if (map) {
        mapper_local_slot slot_loc = slot->local;
        for (i = 0, k = 0; i < count; i++) {
            vals = 0;
            for (j = 0; j < slot->sig->len; j++, k++) {
                vals += (types[k] != MAPPER_NULL);
            }
            /* partial vector updates not allowed in convergent mappings
             * since slot value mirrors remote signal value. */
            // vals is allowed to equal 0 or s->len (for release)
            if (vals == 0) {
                if (count > 1) {
#ifdef DEBUG
                    trace_dev(dev, "error in handler_signal: instance release "
                              "cannot be embedded in multi-count update");
#endif
                    return 0;
                }
                if (global_id) {
                    // TODO: mark SLOT status as remotely released rather than map
                    sig->local->idmaps[idmap_idx].status |= RELEASED_REMOTELY;
                    --idmap->refcount_global;
                    if (idmap->refcount_global <= 0 && idmap->refcount_local <= 0) {
                        mapper_device_remove_idmap(dev, sig->local->group, idmap);
                    }
                    if (event_h && (sig->local->inst_event_flags
                                    & MAPPER_UPSTREAM_RELEASE)) {
                        event_h(sig, idmap->local, MAPPER_UPSTREAM_RELEASE, &t);
                    }
                }
                /* Do not call mapper_device_route_signal() here, since we don't know if
                 * the local signal instance will actually be released. */
                call_handler(sig, idmap->local, 0, sig->type, 0, &t, diff);
                if (map->process_loc != MAPPER_LOC_DST)
                    continue;
                /* Reset memory for corresponding source slot. */
                memset(slot_loc->hist[id].val, 0,
                       slot_loc->hist_size * slot->sig->len * size);
                memset(slot_loc->hist[id].time, 0,
                       slot_loc->hist_size * sizeof(mapper_time_t));
                slot_loc->hist[id].pos = -1;
                continue;
            }
            else if (vals != slot->sig->len) {
#ifdef DEBUG
                trace_dev(dev, "error in handler_signal: partial vector update "
                          "applied to convergent mapping slot.");
#endif
                return 0;
            }
            else if (global_id && !active) {
                // may need to activate instance
                idmap_idx = mapper_signal_find_inst_with_global_id(sig, global_id,
                                                                   RELEASED_REMOTELY);
                if (idmap_idx < 0) {
                    // no instance found with this map
                    if (nulls == val_len * count) {
                        // Don't activate instance just to release it again
                        return 0;
                    }
                    // otherwise try to init reserved/stolen instance with device map
                    idmap_idx = mapper_signal_inst_with_global_id(sig, global_id,
                                                                  0, &t);
                    if (idmap_idx < 0) {
#ifdef DEBUG
                        trace_dev(dev, "no local instances available for global"
                                  " instance id %"PR_MAPPER_ID"\n", global_id);
#endif
                        return 0;
                    }
                }
            }
            slot_loc->hist[id].pos = ((slot_loc->hist[id].pos + 1)
                                      % slot_loc->hist[id].size);
            memcpy(mapper_hist_val_ptr(slot_loc->hist[id]), argv[i*count],
                   size * slot->sig->len);
            memcpy(mapper_hist_time_ptr(slot_loc->hist[id]), &t,
                   sizeof(mapper_time_t));
            if (slot->causes_update) {
                mapper_type typestring[map->dst->sig->len];
                mapper_hist src[map->num_src];
                for (j = 0; j < map->num_src; j++)
                    src[j] = &map->src[j]->local->hist[id];
                if (!mapper_expr_eval(map->local->expr, src,
                                      &map->local->expr_var[id],
                                      &map->dst->local->hist[id],
                                      &t, (mapper_type*)types)) {
                    continue;
                }
                // TODO: check if expression has triggered instance-release
                void *result = mapper_hist_val_ptr(map->dst->local->hist[id]);
                vals = 0;
                for (j = 0; j < map->dst->sig->len; j++) {
                    if (typestring[j] == MAPPER_NULL)
                        continue;
                    memcpy(si->val + j * size, result + j * size, size);
                    si->has_val_flags[j / 8] |= 1 << (j % 8);
                    ++vals;
                }
                if (vals == 0) {
                    // try to release instance
                    // first call handler with value buffer
                    if (out_count) {
                        if (!(sig->dir & MAPPER_DIR_OUT))
                            mapper_device_route_signal(dev, sig, idmap_idx,
                                                       out_buffer, out_count, t);
                        call_handler(sig, idmap->local, sig->len * out_count,
                                     sig->type, out_buffer, &t, diff);
                        out_count = 0;
                    }
                    // next call handler with release
                    if (global_id) {
                        sig->local->idmaps[idmap_idx].status |= RELEASED_REMOTELY;
                        --idmap->refcount_global;
                        if (event_h && (sig->local->inst_event_flags
                                        & MAPPER_UPSTREAM_RELEASE)) {
                            event_h(sig, idmap->local, MAPPER_UPSTREAM_RELEASE, &t);
                        }
                    }
                    /* Do not call mapper_device_route_signal() here, since we
                     * don't know if the local signal instance will actually be
                     * released. */
                    call_handler(sig, idmap->local, 0, sig->type, 0, &t, diff);
                    // mark instance as possibly released
                    active = 0;
                    continue;
                }
                if (memcmp(si->has_val_flags, sig->local->vec_known,
                           sig->len / 8 + 1)==0) {
                    si->has_val = 1;
                }
                if (si->has_val) {
                    memcpy(&si->time, &t, sizeof(mapper_time_t));
                    if (count > 1) {
                        memcpy(out_buffer + out_count * sig->len * size,
                               si->val, size);
                        ++out_count;
                    }
                    else {
                        if (!(sig->dir & MAPPER_DIR_OUT))
                            mapper_device_route_signal(dev, sig, idmap_idx,
                                                       si->val, 1, t);
                        call_handler(sig, idmap->local, sig->len, sig->type,
                                     si->val, &t, diff);
                    }
                }
            }
            else {
                continue;
            }
        }
        if (out_count) {
            if (!(sig->dir & MAPPER_DIR_OUT))
                mapper_device_route_signal(dev, sig, idmap_idx, out_buffer,
                                           out_count, t);
            call_handler(sig, idmap->local, sig->len * out_count, sig->type,
                         out_buffer, &t, diff);
        }
    }
    else {
        for (i = 0, k = 0; i < count; i++) {
            vals = 0;
            for (j = 0; j < sig->len; j++, k++) {
                if (types[k] == MAPPER_NULL)
                    continue;
                memcpy(si->val + j * size, argv[k], size);
                si->has_val_flags[j / 8] |= 1 << (j % 8);
                ++vals;
            }
            if (vals == 0) {
                if (count > 1) {
#ifdef DEBUG
                    trace_dev(dev, "error in handler_signal: instance release "
                              "cannot be embedded in multi-count update");
#endif
                    return 0;
                }
                if (global_id) {
                    sig->local->idmaps[idmap_idx].status |= RELEASED_REMOTELY;
                    --idmap->refcount_global;
                    if (event_h && (sig->local->inst_event_flags
                                    & MAPPER_UPSTREAM_RELEASE)) {
                        event_h(sig, idmap->local, MAPPER_UPSTREAM_RELEASE, &t);
                    }
                }
                /* Do not call mapper_device_route_signal() here, since we don't
                 * know if the local signal instance will actually be released. */
                call_handler(sig, idmap->local, 0, sig->type, 0, &t, diff);
                return 0;
            }
            if (memcmp(si->has_val_flags, sig->local->vec_known,
                       sig->len / 8 + 1)==0) {
                si->has_val = 1;
            }
            if (si->has_val) {
                memcpy(&si->time, &t, sizeof(mapper_time_t));
                if (count > 1) {
                    memcpy(out_buffer + out_count * sig->len * size, si->val, size);
                    ++out_count;
                }
                else {
                    if (!(sig->dir & MAPPER_DIR_OUT))
                        mapper_device_route_signal(dev, sig, idmap_idx,
                                                   si->val, 1, t);
                    call_handler(sig, idmap->local, sig->len, sig->type,
                                 si->val, &t, diff);
                }
            }
        }
        if (out_count) {
            if (!(sig->dir & MAPPER_DIR_OUT))
                mapper_device_route_signal(dev, sig, idmap_idx, out_buffer,
                                           out_count, t);
            call_handler(sig, idmap->local, sig->len * out_count, sig->type,
                         out_buffer, &t, diff);
        }
    }

    return 0;
}

//static int handler_inst_release_request(const char *path, const char *types,
//                                        lo_arg **argv, int argc, lo_message msg,
//                                        const void *user_data)
//{
//    mapper_signal sig = (mapper_signal) user_data;
//    mapper_device dev = sig->dev;
//
//    if (!dev)
//        return 0;
//
//    if (!sig->local->inst_event_handler ||
//        !(sig->local->inst_event_flags & MAPPER_DOWNSTREAM_RELEASE))
//        return 0;
//
//    lo_timetag t = lo_message_get_timestamp(msg);
//
//    int idx = mapper_signal_find_inst_with_global_id(sig, argv[0]->i64, 0);
//    if (idx < 0)
//        return 0;
//
//    if (sig->local->inst_event_handler) {
//        sig->local->inst_event_handler(sig, sig->local->idmaps[idx].map->local,
//                                           MAPPER_DOWNSTREAM_RELEASE, &t);
//    }
//
//    return 0;
//}

static int handler_query(const char *path, const char *types, lo_arg **argv,
                         int argc, lo_message msg, void *user_data)
{
    mapper_signal sig = (mapper_signal)user_data;
    mapper_device dev = sig->dev;
    int len = sig->len;
    mapper_type type = sig->type;

    if (!dev) {
#ifdef DEBUG
        printf("error, sig->dev==0\n");
#endif
        return 0;
    }

    if (!argc)
        return 0;
    else if (types[0] != MAPPER_STRING)
        return 0;

    int i, j, sent = 0;

    // respond with same timestamp as query
    // TODO: should we also include actual timestamp for signal value?
    lo_timetag t = lo_message_get_timestamp(msg);
    lo_bundle b = lo_bundle_new(t);

    // query response string is first argument
    const char *response_path = &argv[0]->s;

    // vector length and data type may also be provided
    if (argc >= 3) {
        if (types[1] == MAPPER_INT32)
            len = argv[1]->i32;
        if (types[2] == MAPPER_CHAR)
            type = argv[2]->c;
    }

    mapper_signal_inst si;
    for (i = 0; i < sig->local->idmap_len; i++) {
        if (!(si = sig->local->idmaps[i].inst))
            continue;
        lo_message m = lo_message_new();
        if (!m)
            continue;
        msg_add_coerced_signal_inst_val(m, sig, si, len, type);
        if (sig->num_inst > 1) {
            lo_message_add_string(m, "@instance");
            lo_message_add_int64(m, sig->local->idmaps[i].map->global);
        }
        lo_bundle_add_message(b, response_path, m);
        ++sent;
    }
    if (!sent) {
        // If there are no active instances, send a single null response
        lo_message m = lo_message_new();
        if (m) {
            for (j = 0; j < len; j++)
                lo_message_add_nil(m);
            lo_bundle_add_message(b, response_path, m);
        }
    }

    lo_send_bundle(lo_message_get_source(msg), b);
    lo_bundle_free_recursive(b);
    return 0;
}

static mapper_id get_unused_signal_id(mapper_device dev)
{
    int done = 0;
    mapper_id id;
    while (!done) {
        done = 1;
        id = mapper_device_generate_unique_id(dev);
        // check if input signal exists with this id
        mapper_object *sigs = mapper_device_get_signals(dev, MAPPER_DIR_ANY);
        while (sigs) {
            if ((*sigs)->id == id) {
                done = 0;
                mapper_object_list_free(sigs);
                break;
            }
            sigs = mapper_object_list_next(sigs);
        }
    }
    return id;
}

// Add a signal to a mapper device.
mapper_signal mapper_device_add_signal(mapper_device dev, mapper_direction dir,
                                       int num_inst, const char *name,
                                       int len, mapper_type type, const char *unit,
                                       const void *minimum, const void *maximum,
                                       mapper_signal_update_handler *handler)
{
    if (!dev || !dev->local)
        return 0;
    if (!name || check_signal_length(len) || check_signal_type(type))
        return 0;
    if (dir != MAPPER_DIR_IN && dir != MAPPER_DIR_OUT) {
        trace_dev(dev, "signal direction must be either input or output.\n");
        return 0;
    }

    mapper_graph g = dev->obj.graph;
    mapper_signal sig;
    if ((sig = mapper_device_get_signal_by_name(dev, name)))
        return sig;

    sig = (mapper_signal)mapper_list_add_item((void**)&g->sigs,
                                              sizeof(mapper_signal_t));
    sig->local = (mapper_local_signal)calloc(1, sizeof(mapper_local_signal_t));

    sig->dev = dev;
    sig->obj.id = get_unused_signal_id(dev);
    sig->obj.graph = g;
    sig->period = -1;
    mapper_signal_init(sig, dir, num_inst, name, len, type, unit, minimum,
                       maximum, handler);

    if (dir == MAPPER_DIR_IN)
        ++dev->num_inputs;
    else
        ++dev->num_outputs;

    mapper_device_increment_version(dev);

    mapper_device_add_signal_methods(dev, sig);

    if (dev->local->registered) {
        // Notify subscribers
        mapper_network_subscribers(&g->net, ((dir == MAPPER_DIR_IN)
                                             ? MAPPER_OBJ_INPUT_SIGNAL
                                             : MAPPER_OBJ_OUTPUT_SIGNAL));
        mapper_signal_send_state(sig, MSG_SIGNAL);
    }

    return sig;
}

void mapper_device_add_signal_methods(mapper_device dev, mapper_signal sig)
{
    if (!sig || !sig->local)
        return;

    mapper_network net = &dev->obj.graph->net;
    void *ptr = (void*)sig;

    int len = strlen(sig->path) + 5;
    char *path = (char*)malloc(sizeof(char) * len);
    snprintf(path, len, "%s%s", sig->path, "/get");
    lo_server_add_method(net->server.udp, path, NULL, handler_query, ptr);
    lo_server_add_method(net->server.tcp, path, NULL, handler_query, ptr);
    free(path);

    lo_server_add_method(net->server.udp, sig->path, NULL, handler_signal, ptr);
    lo_server_add_method(net->server.tcp, sig->path, NULL, handler_signal, ptr);

    ++dev->local->n_output_callbacks;
}

void mapper_device_remove_signal_methods(mapper_device dev, mapper_signal sig)
{
    if (!sig || !sig->local)
        return;

    mapper_network net = &dev->obj.graph->net;

    char *path = 0;
    int len = (int)strlen(sig->path) + 5;
    path = (char*)realloc(path, len);
    snprintf(path, len, "%s%s", sig->path, "/get");
    lo_server_del_method(net->server.udp, path, NULL);
    lo_server_del_method(net->server.tcp, path, NULL);
    free(path);

    lo_server_del_method(net->server.udp, sig->path, NULL);
    lo_server_del_method(net->server.tcp, sig->path, NULL);

    --dev->local->n_output_callbacks;
}

static void send_unmap(mapper_network net, mapper_map map)
{
    if (!map->status)
        return;

    // TODO: send appropriate messages using mesh
    mapper_network_bus(net);

    lo_message m = lo_message_new();
    if (!m)
        return;

    char dst_name[1024], src_names[1024];
    int i, len = 0, result;
    for (i = 0; i < map->num_src; i++) {
        result = snprintf(&src_names[len], 1024-len, "%s%s",
                          map->src[i]->sig->dev->name,
                          map->src[i]->sig->path);
        if (result < 0 || (len + result + 1) >= 1024) {
            trace_dev(net->dev, "error encoding sources for /unmap msg.\n");
            lo_message_free(m);
            return;
        }
        lo_message_add_string(m, &src_names[len]);
        len += result + 1;
    }
    lo_message_add_string(m, "->");
    snprintf(dst_name, 1024, "%s%s", map->dst->sig->dev->name,
             map->dst->sig->path);
    lo_message_add_string(m, dst_name);
    mapper_network_add_msg(net, 0, MSG_UNMAP, m);
}

void mapper_device_remove_signal(mapper_device dev, mapper_signal sig)
{
    int i;
    if (!dev || !sig || !sig->local || sig->dev != dev)
        return;

    mapper_direction dir = sig->dir;
    mapper_device_remove_signal_methods(dev, sig);
    mapper_network net = &dev->obj.graph->net;

    mapper_router_sig rs = dev->local->rtr->sigs;
    while (rs && rs->sig != sig)
        rs = rs->next;
    if (rs) {
        // need to unmap
        for (i = 0; i < rs->num_slots; i++) {
            if (rs->slots[i]) {
                mapper_map map = rs->slots[i]->map;
                send_unmap(net, map);
                mapper_router_remove_map(dev->local->rtr, map);
            }
        }
        mapper_router_remove_sig(dev->local->rtr, rs);
    }

    if (dev->local->registered) {
        // Notify subscribers
        mapper_network_subscribers(net, ((dir == MAPPER_DIR_IN)
                                         ? MAPPER_OBJ_INPUT_SIGNAL
                                         : MAPPER_OBJ_OUTPUT_SIGNAL));
        mapper_signal_send_removed(sig);
    }

    mapper_graph_remove_signal(dev->obj.graph, sig, MAPPER_REMOVED);
    mapper_device_increment_version(dev);
}

int mapper_device_get_num_signals(mapper_device dev, mapper_direction dir)
{
    if (!dev)
        return 0;
    return (  (dir & MAPPER_DIR_IN ? dev->num_inputs : 0)
            + (dir & MAPPER_DIR_OUT ? dev->num_outputs : 0));
}

static int cmp_query_dev_sigs(const void *context_data, mapper_signal sig)
{
    mapper_id dev_id = *(mapper_id*)context_data;
    int dir = *(int*)(context_data + sizeof(mapper_id));
    return ((dir & sig->dir) && (dev_id == sig->dev->obj.id));
}

mapper_object *mapper_device_get_signals(mapper_device dev, mapper_direction dir)
{
    if (!dev || !dev->obj.graph->sigs)
        return 0;
    return ((mapper_object *)
            mapper_list_new_query(dev->obj.graph->sigs, cmp_query_dev_sigs,
                                  "hi", dev->obj.id, dir));
}

mapper_signal mapper_device_get_signal_by_name(mapper_device dev,
                                               const char *sig_name)
{
    if (!dev)
        return 0;
    mapper_object *sigs = mapper_list_from_data(dev->obj.graph->sigs);
    while (sigs) {
        mapper_signal sig = (mapper_signal)*sigs;
        if ((sig->dev == dev) && strcmp(sig->name, skip_slash(sig_name))==0)
            return sig;
        sigs = mapper_object_list_next(sigs);
    }
    return 0;
}

int mapper_device_get_num_maps(mapper_device dev, mapper_direction dir)
{
    if (!dev)
        return 0;
    return (  (dir & MAPPER_DIR_IN ? dev->num_maps_in : 0)
            + (dir & MAPPER_DIR_OUT ? dev->num_maps_out : 0));
}

static int cmp_query_device_maps(const void *context_data, mapper_map map)
{
    mapper_id dev_id = *(mapper_id*)context_data;
    mapper_direction dir = *(int*)(context_data + sizeof(mapper_id));
    if (dir == MAPPER_DIR_BOTH) {
        if (map->dst->sig->dev->obj.id != dev_id)
            return 0;
        int i;
        for (i = 0; i < map->num_src; i++) {
            if (map->src[i]->sig->dev->obj.id != dev_id)
                return 0;
        }
        return 1;
    }
    if (dir & MAPPER_DIR_OUT) {
        int i;
        for (i = 0; i < map->num_src; i++) {
            if (map->src[i]->sig->dev->obj.id == dev_id)
                return 1;
        }
    }
    if (dir & MAPPER_DIR_IN) {
        if (map->dst->sig->dev->obj.id == dev_id)
            return 1;
    }
    return 0;
}

mapper_object *mapper_device_get_maps(mapper_device dev, mapper_direction dir)
{
    if (!dev || !dev->obj.graph->maps)
        return 0;
    return ((mapper_object *)
            mapper_list_new_query(dev->obj.graph->maps,
                                  cmp_query_device_maps, "hi", dev->obj.id,
                                  dir));
}

static int cmp_query_device_links(const void *context_data, mapper_link link)
{
    mapper_id dev_id = *(mapper_id*)context_data;
    mapper_direction dir = *(int*)(context_data + sizeof(mapper_id));
    if (link->devs[0]->obj.id == dev_id) {
        switch (dir) {
            case MAPPER_DIR_BOTH:
                return link->num_maps[0] && link->num_maps[1];
            case MAPPER_DIR_IN:
                return link->num_maps[1];
            case MAPPER_DIR_OUT:
                return link->num_maps[0];
            default:
                return 1;
        }
    }
    else if (link->devs[1]->obj.id == dev_id) {
        switch (dir) {
            case MAPPER_DIR_BOTH:
                return link->num_maps[0] && link->num_maps[1];
            case MAPPER_DIR_IN:
                return link->num_maps[0];
            case MAPPER_DIR_OUT:
                return link->num_maps[1];
            default:
                return 1;
        }
    }
    return 0;
}

mapper_object *mapper_device_get_links(mapper_device dev, mapper_direction dir)
{
    if (!dev || !dev->obj.graph->links)
        return 0;
    return ((mapper_object *)
            mapper_list_new_query(dev->obj.graph->links,
                                  cmp_query_device_links, "hi", dev->obj.id,
                                  dir));
}

mapper_link mapper_device_get_link_by_remote_device(mapper_device dev,
                                                    mapper_device remote_dev)
{
    if (!dev)
        return 0;
    mapper_object *links = mapper_list_from_data(dev->obj.graph->links);
    while (links) {
        mapper_link link = (mapper_link)*links;
        if (link->devs[0] == dev) {
            if (link->devs[1] == remote_dev)
                return link;
        }
        else if (link->devs[1] == dev) {
            if (link->devs[0] == remote_dev)
                return link;
        }
        links = mapper_object_list_next(links);
    }
    return 0;
}

int mapper_device_poll(mapper_device dev, int block_ms)
{
    if (!dev || !dev->local)
        return 0;

    int admin_count = 0, device_count = 0, status[4];
    mapper_network net = &dev->obj.graph->net;

    mapper_network_poll(net);

    if (!dev->local->registered) {
        if (lo_servers_recv_noblock(net->server.admin, status, 2, 0)) {
            admin_count = (status[0] > 0) + (status[1] > 0);
            net->msgs_recvd |= admin_count;
        }
        return admin_count;
    }
    else if (!block_ms) {
        if (lo_servers_recv_noblock(net->server.all, status, 4, 0)) {
            admin_count = (status[0] > 0) + (status[1] > 0);
            device_count = (status[2] > 0) + (status[3] > 0);
            net->msgs_recvd |= admin_count;
        }
        return admin_count + device_count;
    }

    double then = mapper_get_current_time();
    int left_ms = block_ms, elapsed, checked_admin = 0;
    while (left_ms > 0) {
        // set timeout to a maximum of 100ms
        if (left_ms > 100)
            left_ms = 100;

        if (lo_servers_recv_noblock(net->server.all, status, 4, left_ms)) {
            admin_count += (status[0] > 0) + (status[1] > 0);
            device_count += (status[2] > 0) + (status[3] > 0);
        }

        elapsed = (mapper_get_current_time() - then) * 1000;
        if ((elapsed - checked_admin) > 100) {
            mapper_network_poll(net);
            checked_admin = elapsed;
        }

        left_ms = block_ms - elapsed;
    }

    /* When done, or if non-blocking, check for remaining messages up to a
     * proportion of the number of input signals. Arbitrarily choosing 1 for
     * now, but perhaps could be a heuristic based on a recent number of
     * messages per channel per poll. */
    while (device_count < (dev->num_inputs + dev->local->n_output_callbacks)*1
           && (lo_servers_recv_noblock(net->server.device, &status[2], 2, 0))) {
        device_count += (status[2] > 0) + (status[3] > 0);
    }

    if (dev->obj.props.synced->dirty && mapper_device_ready(dev)
        && dev->local->subscribers) {
        // inform device subscribers of change props
        mapper_network_subscribers(net, MAPPER_OBJ_DEVICE);
        mapper_device_send_state(dev, MSG_DEVICE);
    }

    net->msgs_recvd |= admin_count;
    return admin_count + device_count;
}

void mapper_device_on_num_inst_changed(mapper_device dev, mapper_signal sig,
                                       int size)
{
    if (!dev)
        return;
    mapper_router_num_inst_changed(dev->local->rtr, sig, size);
}

void mapper_device_route_signal(mapper_device dev, mapper_signal sig,
                                int inst_idx, const void *val,
                                int count, mapper_time_t time)
{
    mapper_router_process_sig(dev->local->rtr, sig, inst_idx, val, count, time);
}

// Function to start a signal update queue
mapper_time_t mapper_device_start_queue(mapper_device dev, mapper_time_t t)
{
    if (!dev)
        return t;

    if (time_is_now(&t))
        mapper_time_now(&t);

    mapper_object *links = mapper_list_from_data(dev->obj.graph->links);
    while (links) {
        mapper_link link = (mapper_link)*links;
        mapper_link_start_queue(link, t);
        links = mapper_object_list_next(links);
    }
    return t;
}

// Function to send a signal update queue
void mapper_device_send_queue(mapper_device dev, mapper_time_t t)
{
    if (!dev)
        return;

    mapper_object *links = mapper_list_from_data(dev->obj.graph->links);
    while (links) {
        mapper_link link = (mapper_link)*links;
        mapper_link_send_queue(link, t);
        links = mapper_object_list_next(links);
    }
}

void mapper_device_reserve_idmap(mapper_device dev)
{
    mapper_idmap map;
    map = (mapper_idmap)calloc(1, sizeof(mapper_idmap_t));
    map->next = dev->local->idmaps.reserve;
    dev->local->idmaps.reserve = map;
}

mapper_idmap mapper_device_add_idmap(mapper_device dev, int group,
                                     mapper_id local, mapper_id global)
{
    if (!dev->local->idmaps.reserve)
        mapper_device_reserve_idmap(dev);

    mapper_idmap map = dev->local->idmaps.reserve;
    map->local = local;
    map->global = global;
    map->refcount_local = 1;
    map->refcount_global = 0;
    dev->local->idmaps.reserve = map->next;
    map->next = dev->local->idmaps.active[group];
    dev->local->idmaps.active[group] = map;
    return map;
}

void mapper_device_remove_idmap(mapper_device dev, int group, mapper_idmap rem)
{
    mapper_idmap *map = &dev->local->idmaps.active[group];
    while (*map) {
        if ((*map) == rem) {
            *map = (*map)->next;
            rem->next = dev->local->idmaps.reserve;
            dev->local->idmaps.reserve = rem;
            break;
        }
        map = &(*map)->next;
    }
}

mapper_idmap mapper_device_idmap_by_local(mapper_device dev, int group,
                                          mapper_id local)
{
    mapper_idmap map = dev->local->idmaps.active[group];
    while (map) {
        if (map->local == local)
            return map;
        map = map->next;
    }
    return 0;
}

mapper_idmap mapper_device_idmap_by_global(mapper_device dev, int group,
                                           mapper_id global)
{
    mapper_idmap map = dev->local->idmaps.active[group];
    while (map) {
        if (map->global == global)
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

void mapper_device_start_servers(mapper_device dev)
{
    mapper_network net = &dev->obj.graph->net;
    if (net->server.udp || net->server.tcp)
        return;

    int len;
    char port[16], *pport = 0, *path = 0;

    while (!(net->server.udp = lo_server_new(pport, liblo_error_handler))) {
        pport = 0;
    }

    snprintf(port, 16, "%d", lo_server_get_port(net->server.udp));
    pport = port;

    while (!(net->server.tcp = lo_server_new_with_proto(pport, LO_TCP,
                                                        liblo_error_handler))) {
        pport = 0;
    }

    // Disable liblo message queueing
    lo_server_enable_queue(net->server.udp, 0, 1);
    lo_server_enable_queue(net->server.tcp, 0, 1);

    int portnum = lo_server_get_port(net->server.udp);
    mapper_table_set_record(dev->obj.props.synced, MAPPER_PROP_PORT, NULL,
                            1, MAPPER_INT32, &portnum, NON_MODIFIABLE);

    trace_dev(dev, "bound to UDP port %i\n", portnum);
    trace_dev(dev, "bound to TCP port %i\n",
              lo_server_get_port(net->server.tcp));

    char *url = lo_server_get_url(net->server.udp);
    char *host = lo_url_get_hostname(url);
    mapper_table_set_record(dev->obj.props.synced, MAPPER_PROP_HOST, NULL,
                            1, MAPPER_STRING, host, NON_MODIFIABLE);
    free(host);
    free(url);
    trace_dev(dev, "bound to port %i\n", portnum);

    // add signal methods
    mapper_object *sigs = mapper_device_get_signals(dev, MAPPER_DIR_ANY);
    while (sigs) {
        mapper_signal sig = (mapper_signal)*sigs;
        if (sig->local->update_handler) {
            lo_server_add_method(net->server.udp, sig->path, NULL,
                                 handler_signal, (void*)sig);
            lo_server_add_method(net->server.tcp, sig->path, NULL,
                                 handler_signal, (void*)sig);
        }

        len = (int)strlen(sig->path) + 5;
        path = (char*)realloc(path, len);
        snprintf(path, len, "%s%s", sig->path, "/get");
        lo_server_add_method(net->server.udp, path, NULL, handler_query,
                             (void*)sig);
        lo_server_add_method(net->server.tcp, path, NULL, handler_query,
                             (void*)sig);
        sigs = mapper_object_list_next(sigs);
    }
    free(path);
}

const char *mapper_device_get_name(mapper_device dev)
{
    if (dev->local && (!dev->local->registered || !dev->local->ordinal.locked))
        return 0;

    if (dev->name)
        return dev->name;

    unsigned int len = strlen(dev->identifier) + 6;
    dev->name = (char*)malloc(len);
    dev->name[0] = 0;
    snprintf(dev->name, len, "%s.%d", dev->identifier, dev->local->ordinal.val);
    return dev->name;
}

int mapper_device_ready(mapper_device dev)
{
    return dev->status >= STATUS_READY;
}

mapper_id mapper_device_generate_unique_id(mapper_device dev) {
    if (!dev)
        return 0;
    mapper_id id = ++dev->obj.graph->resource_counter;
    if (dev->local && dev->local->registered)
        id |= dev->obj.id;
    return id;
}

void mapper_device_send_state(mapper_device dev, network_msg_t cmd)
{
    if (!dev)
        return;
    lo_message msg = lo_message_new();
    if (!msg) {
        trace_dev(dev, "couldn't allocate lo_message\n");
        return;
    }

    /* device name */
    lo_message_add_string(msg, mapper_device_get_name(dev));

    /* properties */
    mapper_table_add_to_msg(dev->local ? dev->obj.props.synced : 0,
                            dev->obj.props.staged, msg);

    mapper_network net = &dev->obj.graph->net;
    if (cmd == MSG_DEVICE_MODIFY) {
        char str[1024];
        snprintf(str, 1024, "/%s/modify", dev->name);
        mapper_network_add_msg(net, str, 0, msg);
        mapper_network_send(net);
    }
    else
        mapper_network_add_msg(net, 0, cmd, msg);

    dev->obj.props.synced->dirty = 0;
}

/*! Update information about a device record based on message properties. */
int mapper_device_set_from_msg(mapper_device dev, mapper_msg msg)
{
    if (!msg)
        return 0;
    return mapper_table_set_from_msg(dev->obj.props.synced, msg, REMOTE_MODIFY);
}

static int mapper_device_send_signals(mapper_device dev, mapper_direction dir,
                                      int min, int max)
{
    int i = 0;
    mapper_object *sigs = mapper_device_get_signals(dev, dir);
    while (sigs) {
        if (i > max && max > 0) {
            mapper_object_list_free(sigs);
            return 1;
        }
        if (i >= min)
            mapper_signal_send_state((mapper_signal)*sigs, MSG_SIGNAL);
        ++i;
        sigs = mapper_object_list_next(sigs);
    }
    return 0;
}

static int mapper_device_send_maps(mapper_device dev, mapper_direction dir,
                                    int min, int max)
{
    int i = 0;
    mapper_object *maps = mapper_device_get_maps(dev, dir);
    while (maps) {
        if (i > max && max > 0) {
            mapper_object_list_free(maps);
            return 1;
        }
        if (i >= min) {
            mapper_map_send_state((mapper_map)*maps, -1, MSG_MAPPED);
        }
        ++i;
        maps = mapper_object_list_next(maps);
    }
    return 0;
}

// Add/renew/remove a subscription.
void mapper_device_manage_subscriber(mapper_device dev, lo_address addr,
                                     int flags, int timeout_sec, int revision)
{
    mapper_subscriber *s = &dev->local->subscribers;
    const char *ip = lo_address_get_hostname(addr);
    const char *port = lo_address_get_port(addr);
    if (!ip || !port) {
        trace_dev(dev, "error managing subscription: %s not found\n",
                  ip ? "port" : "ip");
        return;
    }

    mapper_time_t t;
    mapper_time_now(&t);

    while (*s) {
        const char *s_ip = lo_address_get_hostname((*s)->addr);
        const char *s_port = lo_address_get_port((*s)->addr);
        if (strcmp(ip, s_ip)==0 && strcmp(port, s_port)==0) {
            // subscriber already exists
            if (!flags || !timeout_sec) {
                trace_dev(dev, "removing subscription from %s:%s\n", s_ip, s_port);
                // remove subscription
                mapper_subscriber temp = *s;
                int prev_flags = temp->flags;
                *s = temp->next;
                if (temp->addr)
                    lo_address_free(temp->addr);
                free(temp);
                if (!flags || !(flags &= ~prev_flags))
                    return;
            }
            else {
                // reset timeout
                trace_dev(dev, "renewing subscription from %s:%s for %d seconds\n",
                          s_ip, s_port, timeout_sec);
                (*s)->lease_exp = t.sec + timeout_sec;
                int temp = flags;
                flags &= ~(*s)->flags;
                (*s)->flags = temp;
            }
            break;
        }
        s = &(*s)->next;
    }

    if (!flags)
        return;

    if (!(*s) && timeout_sec) {
        // add new subscriber
        trace_dev(dev, "adding new subscription from %s:%s with flags %d\n",
              ip, port, flags);
        mapper_subscriber sub = malloc(sizeof(struct _mapper_subscriber));
        sub->addr = lo_address_new(ip, port);
        sub->lease_exp = t.sec + timeout_sec;
        sub->flags = flags;
        sub->next = dev->local->subscribers;
        dev->local->subscribers = sub;
        s = &sub;
    }

    // bring new subscriber up to date
    mapper_network net = &dev->obj.graph->net;
    mapper_network_mesh(net, addr);
    mapper_device_send_state(dev, MSG_DEVICE);
    mapper_network_send(net);

    if (flags & MAPPER_OBJ_SIGNAL) {
        mapper_direction dir = 0;
        if (flags & MAPPER_OBJ_INPUT_SIGNAL)
            dir |= MAPPER_DIR_IN;
        if (flags & MAPPER_OBJ_OUTPUT_SIGNAL)
            dir |= MAPPER_DIR_OUT;
        int batch = 0, done = 0;
        while (!done) {
            mapper_network_mesh(net, addr);
            if (!mapper_device_send_signals(dev, dir, batch, batch + 9))
                done = 1;
            mapper_network_send(net);
            batch += 10;
        }
    }

    if (flags & MAPPER_OBJ_MAP) {
        mapper_direction dir = 0;
        if (flags & MAPPER_OBJ_MAP_IN)
            dir |= MAPPER_DIR_IN;
        if (flags & MAPPER_OBJ_MAP_OUT)
            dir |= MAPPER_DIR_OUT;
        int batch = 0, done = 0;
        while (!done) {
            mapper_network_mesh(net, addr);
            if (!mapper_device_send_maps(dev, dir, batch, batch + 9))
                done = 1;
            mapper_network_send(net);
            batch += 10;
        }
    }
}

mapper_signal_group mapper_device_add_signal_group(mapper_device dev)
{
    if (!dev->local)
        return 0;
    ++dev->local->num_signal_groups;
    dev->local->idmaps.active = realloc(dev->local->idmaps.active,
                                        dev->local->num_signal_groups
                                        * sizeof(mapper_idmap*));
    dev->local->idmaps.active[dev->local->num_signal_groups-1] = 0;

    return dev->local->num_signal_groups-1;
}

void mapper_device_remove_signal_group(mapper_device dev,
                                       mapper_signal_group group)
{
    if (!dev->local || group >= dev->local->num_signal_groups)
        return;

    int i = (int)group + 1;
    for (; i < dev->local->num_signal_groups; i++) {
        dev->local->idmaps.active[i-1] = dev->local->idmaps.active[i];
    }
    --dev->local->num_signal_groups;
    dev->local->idmaps.active = realloc(dev->local->idmaps.active,
                                        dev->local->num_signal_groups
                                        * sizeof(mapper_idmap *));
}
