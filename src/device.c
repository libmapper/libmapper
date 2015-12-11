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

extern const char* network_message_strings[NUM_MSG_STRINGS];

void init_device_prop_table(mapper_device dev)
{
    dev->props = mapper_table_new();
    dev->staged_props = mapper_table_new();
    int flags = dev->local ? NON_MODIFIABLE : MODIFIABLE;

    // these properties need to be added in alphabetical order
    mapper_table_link_value(dev->props, AT_DESCRIPTION, 1, 's',
                            &dev->description, flags | INDIRECT);

    mapper_table_link_value(dev->props, AT_HOST, 1, 's', &dev->host,
                            flags | INDIRECT);

    mapper_table_link_value(dev->props, AT_ID, 1, 'h', &dev->id, flags);

    mapper_table_link_value(dev->props, AT_LIB_VERSION, 1, 's',
                            &dev->lib_version, flags | INDIRECT);

    mapper_table_link_value(dev->props, AT_NAME, 1, 's', &dev->name,
                            flags | INDIRECT);

    mapper_table_link_value(dev->props, AT_NUM_INCOMING_MAPS, 1, 'i',
                            &dev->num_incoming_maps, flags);

    mapper_table_link_value(dev->props, AT_NUM_INPUTS, 1, 'i',
                            &dev->num_inputs, flags);

    mapper_table_link_value(dev->props, AT_NUM_OUTGOING_MAPS, 1, 'i',
                            &dev->num_outgoing_maps, flags);

    mapper_table_link_value(dev->props, AT_NUM_OUTPUTS, 1, 'i',
                            &dev->num_outputs, flags);

    mapper_table_link_value(dev->props, AT_PORT, 1, 'i', &dev->port, flags);

    mapper_table_link_value(dev->props, AT_STATUS, 1, 'i', &dev->status, flags);

    mapper_table_link_value(dev->props, AT_SYNCED, 1, 't', &dev->synced, flags);

    mapper_table_link_value(dev->props, AT_USER_DATA, 1, 'v', &dev->user_data,
                            LOCAL_MODIFY | INDIRECT);

    mapper_table_link_value(dev->props, AT_VERSION, 1, 'i', &dev->version,
                            flags);
}

/*! Allocate and initialize a mapper device. This function is called to create
 *  a new mapper_device, not to create a representation of remote devices. */
mapper_device mapper_device_new(const char *name_prefix, int port,
                                mapper_network net)
{
    if (!name_prefix)
        return 0;

    if (!net) {
        net = mapper_network_new(0, 0, 0);
        net->own_network = 0;
    }

    mapper_db db = &net->db;
    mapper_device dev;
    dev = (mapper_device)mapper_list_add_item((void**)&db->devices,
                                              sizeof(mapper_device_t));
    dev->db = db;
    dev->local = (mapper_local_device)calloc(1, sizeof(mapper_local_device_t));
    dev->local->own_network = 1 - net->own_network;

    mapper_device_start_server(dev, port);

    if (!dev->local->server) {
        mapper_device_free(dev);
        return NULL;
    }

    if (name_prefix[0] == '/')
        name_prefix++;
    if (strchr(name_prefix, '/')) {
        trace("error: character '/' is not permitted in device name.\n");
        mapper_device_free(dev);
        return NULL;
    }

    dev->local->ordinal.value = 1;
    dev->identifier = strdup(name_prefix);
    dev->lib_version = PACKAGE_VERSION;

    dev->local->router = (mapper_router)calloc(1, sizeof(mapper_router_t));
    dev->local->router->device = dev;

    dev->local->link_timeout_sec = TIMEOUT_SEC;

    mapper_network_add_device(net, dev);

    dev->status = STATUS_STAGED;

    init_device_prop_table(dev);

    return dev;
}

//! Free resources used by a mapper device.
void mapper_device_free(mapper_device dev)
{
    int i;
    if (!dev || !dev->local)
        return;
    if (!dev->db) {
        free(dev);
        return;
    }
    mapper_db db = dev->db;
    mapper_network net = dev->db->network;

    // free any queued outgoing messages without sending
    mapper_network_free_messages(net);

    // remove subscribers
    mapper_subscriber s;
    while (dev->local->subscribers) {
        s = dev->local->subscribers;
        if (s->address)
            lo_address_free(s->address);
        dev->local->subscribers = s->next;
        free(s);
    }

    mapper_signal *sigs = mapper_device_signals(dev, MAPPER_DIR_ANY);
    while (sigs) {
        mapper_signal sig = *sigs;
        sigs = mapper_signal_query_next(sigs);
        if (sig->local) {
            // release active instances
            for (i = 0; i < sig->local->id_map_length; i++) {
                if (sig->local->id_maps[i].instance) {
                    mapper_signal_instance_release_internal(sig, i, MAPPER_NOW);
                }
            }
        }
        mapper_device_remove_signal(dev, sig);
    }

    if (dev->local->registered) {
        // A registered device must tell the network it is leaving.
        lo_message msg = lo_message_new();
        if (msg) {
            mapper_network_set_dest_bus(net);
            lo_message_add_string(msg, mapper_device_name(dev));
            mapper_network_add_message(net, 0, MSG_LOGOUT, msg);
            mapper_network_send(net);
        }
        else {
            trace("couldn't allocate lo_message for /logout\n");
        }
    }

    // Links reference parent signals so release them first
    while (dev->local->router->links)
        mapper_router_remove_link(dev->local->router, dev->local->router->links);

    // Release device id maps
    mapper_id_map map;
    while (dev->local->active_id_map) {
        map = dev->local->active_id_map;
        dev->local->active_id_map = map->next;
        free(map);
    }
    while (dev->local->reserve_id_map) {
        map = dev->local->reserve_id_map;
        dev->local->reserve_id_map = map->next;
        free(map);
    }

    if (dev->local->router) {
        while (dev->local->router->signals) {
            mapper_router_signal rs = dev->local->router->signals;
            dev->local->router->signals = dev->local->router->signals->next;
            free(rs);
        }
        free(dev->local->router);
    }

    int own_network = dev->local->own_network;

    if (dev->local->server)
        lo_server_free(dev->local->server);
    free(dev->local);

    if (dev->identifier)
        free(dev->identifier);

    mapper_db_remove_device(dev->db, dev, 1);

    if (own_network) {
        mapper_db_free(db);
        mapper_network_free(net);
    }
}

void mapper_device_push(mapper_device dev)
{
    if (!dev || !dev->props->dirty)
        return;

    if (dev->local)
        mapper_network_set_dest_subscribers(dev->db->network,
                                            MAPPER_SUBSCRIBE_DEVICES);
    else
        mapper_network_set_dest_bus(dev->db->network);
    mapper_device_send_state(dev, UPDATED_PROPS);
}

void mapper_device_set_user_data(mapper_device dev, const void *user_data)
{
    if (dev)
        dev->user_data = (void*)user_data;
}

void *mapper_device_user_data(mapper_device dev)
{
    return dev ? dev->user_data : 0;
}

mapper_network mapper_device_network(mapper_device dev)
{
    return dev->db->network;
}

void mapper_device_registered(mapper_device dev)
{
    int i;
    /* Add unique device id to locally-activated signal instances. */
    mapper_signal *sig = mapper_device_signals(dev, MAPPER_DIR_ANY);
    while (sig) {
        if ((*sig)->local) {
            for (i = 0; i < (*sig)->local->id_map_length; i++) {
                if ((*sig)->local->id_maps[i].map &&
                    !((*sig)->local->id_maps[i].map->global >> 32)) {
                    (*sig)->local->id_maps[i].map->global |= dev->id;
                }
            }
        }
        sig = mapper_signal_query_next(sig);
    }
    dev->local->registered = 1;
    dev->status = STATUS_READY;
}

static void mapper_device_increment_version(mapper_device dev)
{
    dev->version ++;
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
 *   length of the target signal or mapping slot.
 * - Vectors are of homogeneous type ('i', 'f' or 'd') however individual
 *   elements may have no value (type 'N')
 * - A vector consisting completely of nulls indicates a signal instance release
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
    int is_instance_update = 0, id_map_index, slot_index = -1;
    mapper_id global_id;
    mapper_id_map id_map;
    mapper_map map = 0;
    mapper_slot slot = 0;

    if (!sig || !(dev = sig->device)) {
#ifdef DEBUG
        printf("error in handler_signal, cannot retrieve user_data\n");
#endif
        return 0;
    }

    if (!argc)
        return 0;

    mapper_signal_update_handler *update_h = sig->local->update_handler;
    mapper_instance_event_handler *event_h = sig->local->instance_event_handler;

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
        if (strcmp(&argv[argnum]->s, mapper_protocol_string(AT_INSTANCE)) == 0
            && argc >= argnum + 2) {
            if (types[argnum+1] != 'h') {
#ifdef DEBUG
                printf("error in handler_signal: bad arguments "
                       "for 'instance' property.\n");
#endif
                return 0;
            }
            is_instance_update = 1;
            global_id = argv[argnum+1]->i64;
            argnum += 2;
        }
        else if (strcmp(&argv[argnum]->s, mapper_protocol_string(AT_SLOT)) == 0
                 && argc >= argnum + 2) {
            if (types[argnum+1] != 'i') {
#ifdef DEBUG
                printf("error in handler_signal: bad arguments "
                       "for 'slot' property.\n");
#endif
                return 0;
            }
            slot_index = argv[argnum+1]->i32;
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

    if (slot_index >= 0) {
        // retrieve mapping associated with this slot
        slot = mapper_router_find_slot(dev->local->router, sig, slot_index);
        if (!slot) {
#ifdef DEBUG
            printf("error in handler_signal: slot %d not found.\n", slot_index);
#endif
            return 0;
        }
        map = slot->map;
        if (map->status < STATUS_READY) {
#ifdef DEBUG
            printf("error in handler_signal: mapping not yet ready.\n");
#endif
            return 0;
        }
        if (!map->local->expr) {
#ifdef DEBUG
            printf("error in handler_signal: missing expression.\n");
#endif
            return 0;
        }
        if (map->process_location == MAPPER_LOC_DESTINATION) {
            count = check_types(types, value_len, slot->signal->type,
                                slot->signal->length);
        }
        else {
            // value has already been processed at source device
            map = 0;
            count = check_types(types, value_len, sig->type, sig->length);
        }
    }
    else {
        count = check_types(types, value_len, sig->type, sig->length);
    }

    if (!count)
        return 0;

    // TODO: optionally discard out-of-order messages
    // requires timebase sync for many-to-one mappings or local updates
    //    if (sig->discard_out_of_order && out_of_order(si->timetag, tt))
    //        return 0;
    lo_timetag tt = lo_message_get_timestamp(msg);

    if (is_instance_update) {
        id_map_index = mapper_signal_find_instance_with_global_id(sig,
                                                                  global_id,
                                                                  RELEASED_LOCALLY);
        if (id_map_index < 0) {
            // no instance found with this map
            if (nulls == value_len * count) {
                // Don't activate instance just to release it again
                return 0;
            }
            // otherwise try to init reserved/stolen instance with device map
            id_map_index = mapper_signal_instance_with_global_id(sig, global_id,
                                                                 0, &tt);
            if (id_map_index < 0) {
#ifdef DEBUG
                printf("no local instances available for global instance id"
                       " %llu\n", global_id);
#endif
                return 0;
            }
        }
        else {
            if (sig->local->id_maps[id_map_index].status & RELEASED_LOCALLY) {
                /* map was already released locally, we are only interested
                 * in release messages */
                if (count == 1 && nulls == value_len) {
                    // we can clear signal's reference to map
                    id_map = sig->local->id_maps[id_map_index].map;
                    sig->local->id_maps[id_map_index].map = 0;
                    id_map->refcount_global--;
                    if (id_map->refcount_global <= 0
                        && id_map->refcount_local <= 0) {
                        mapper_device_remove_instance_id_map(dev, id_map);
                    }
                }
                return 0;
            }
            else if (!sig->local->id_maps[id_map_index].instance) {
#ifdef DEBUG
                printf("error in handler_signal: missing instance!\n");
#endif
                return 0;
            }
        }
    }
    else {
        id_map_index = 0;
        if (!sig->local->id_maps[0].instance)
            id_map_index = mapper_signal_instance_with_local_id(sig, 0, 1, &tt);
        if (id_map_index < 0)
            return 0;
    }
    mapper_signal_instance si = sig->local->id_maps[id_map_index].instance;
    id_map = sig->local->id_maps[id_map_index].map;

    int size = (slot ? mapper_type_size(slot->signal->type) : mapper_type_size(sig->type));
    void *out_buffer = alloca(count * value_len * size);
    int vals, out_count = 0, active = 1;

    if (map) {
        for (i = 0, k = 0; i < count; i++) {
            vals = 0;
            for (j = 0; j < slot->signal->length; j++, k++) {
                vals += (types[k] != 'N');
            }
            /* partial vector updates not allowed in convergent mappings
             * since slot value mirrors remote signal value. */
            // vals is allowed to equal 0 or s->length (for release)
            if (vals == 0) {
                if (count > 1) {
#ifdef DEBUG
                    printf("error in handler_signal: instance release cannot "
                           "be embedded in multi-count update");
#endif
                    return 0;
                }
                if (is_instance_update) {
                    sig->local->id_maps[id_map_index].status |= RELEASED_REMOTELY;
                    id_map->refcount_global--;
                    if (id_map->refcount_global <= 0 && id_map->refcount_local <= 0) {
                        mapper_device_remove_instance_id_map(dev, id_map);
                    }
                    if (event_h && (sig->local->instance_event_flags
                                    & MAPPER_UPSTREAM_RELEASE)) {
                        event_h(sig, id_map->local, MAPPER_UPSTREAM_RELEASE, &tt);
                    }
                }
                /* Do not call mapper_device_route_signal() here, since we don't know if
                 * the local signal instance will actually be released. */
                if (update_h) {
                    update_h(sig, id_map->local, 0, 1, &tt);
                }
                continue;
            }
            else if (vals != slot->signal->length) {
#ifdef DEBUG
                printf("error in handler_signal: partial vector update applied "
                       "to convergent mapping slot.");
#endif
                return 0;
            }
            else if (is_instance_update && !active) {
                // may need to activate instance
                id_map_index = mapper_signal_find_instance_with_global_id(sig,
                                                                          global_id,
                                                                          RELEASED_REMOTELY);
                if (id_map_index < 0) {
                    // no instance found with this map
                    if (nulls == value_len * count) {
                        // Don't activate instance just to release it again
                        return 0;
                    }
                    // otherwise try to init reserved/stolen instance with device map
                    id_map_index = mapper_signal_instance_with_global_id(sig,
                                                                         global_id,
                                                                         0, &tt);
                    if (id_map_index < 0) {
#ifdef DEBUG
                        printf("no local instances available for global"
                               " instance id %llu\n", global_id);
#endif
                        return 0;
                    }
                }
                si = sig->local->id_maps[id_map_index].instance;
                id_map = sig->local->id_maps[id_map_index].map;
            }
            slot->local->history[si->index].position = ((slot->local->history[si->index].position + 1)
                                                        % slot->local->history[si->index].size);
            memcpy(mapper_history_value_ptr(slot->local->history[si->index]),
                   argv[i*count], size * slot->signal->length);
            memcpy(mapper_history_tt_ptr(slot->local->history[si->index]),
                   &tt, sizeof(mapper_timetag_t));
            if (slot->causes_update) {
                char typestring[map->destination.signal->length];
                mapper_history sources[map->num_sources];
                for (j = 0; j < map->num_sources; j++)
                    sources[j] = &map->sources[j].local->history[si->index];
                if (!mapper_expr_evaluate(map->local->expr, sources,
                                          &map->local->expr_vars[si->index],
                                          &map->destination.local->history[si->index],
                                          &tt, typestring)) {
                    continue;
                }
                // TODO: check if expression has triggered instance-release
                if (mapper_boundary_perform(&map->destination.local->history[si->index],
                                            &map->destination, typestring)) {
                    continue;
                }
                void *result = mapper_history_value_ptr(map->destination.local->history[si->index]);
                vals = 0;
                for (j = 0; j < map->destination.signal->length; j++) {
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
                        if (!(sig->direction & MAPPER_DIR_OUTGOING))
                            mapper_device_route_signal(dev, sig, id_map_index,
                                                       out_buffer, out_count, tt);
                        if (update_h)
                            update_h(sig, id_map->local, out_buffer, out_count, &tt);
                        out_count = 0;
                    }
                    // next call handler with release
                    if (is_instance_update) {
                        sig->local->id_maps[id_map_index].status |= RELEASED_REMOTELY;
                        id_map->refcount_global--;
                        if (event_h && (sig->local->instance_event_flags
                                        & MAPPER_UPSTREAM_RELEASE)) {
                            event_h(sig, id_map->local, MAPPER_UPSTREAM_RELEASE, &tt);
                        }
                    }
                    /* Do not call mapper_device_route_signal() here, since we
                     * don't know if the local signal instance will actually be
                     * released. */
                    if (update_h)
                        update_h(sig, id_map->local, 0, 1, &tt);
                    // mark instance as possibly released
                    active = 0;
                    continue;
                }
                if (memcmp(si->has_value_flags, sig->local->has_complete_value,
                           sig->length / 8 + 1)==0) {
                    si->has_value = 1;
                }
                if (si->has_value) {
                    memcpy(&si->timetag, &tt, sizeof(mapper_timetag_t));
                    if (count > 1) {
                        memcpy(out_buffer + out_count * sig->length * size,
                               si->value, size);
                        out_count++;
                    }
                    else {
                        if (!(sig->direction & MAPPER_DIR_OUTGOING))
                            mapper_device_route_signal(dev, sig, id_map_index,
                                                       si->value, 1, tt);
                        if (update_h)
                            update_h(sig, id_map->local, si->value, 1, &tt);
                    }
                }
            }
            else {
                continue;
            }
        }
        if (out_count) {
            if (!(sig->direction & MAPPER_DIR_OUTGOING))
                mapper_device_route_signal(dev, sig, id_map_index, out_buffer,
                                           out_count, tt);
            if (update_h)
                update_h(sig, id_map->local, out_buffer, out_count, &tt);
        }
    }
    else {
        for (i = 0, k = 0; i < count; i++) {
            vals = 0;
            for (j = 0; j < sig->length; j++, k++) {
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
                    sig->local->id_maps[id_map_index].status |= RELEASED_REMOTELY;
                    id_map->refcount_global--;
                    if (event_h && (sig->local->instance_event_flags
                                    & MAPPER_UPSTREAM_RELEASE)) {
                        event_h(sig, id_map->local, MAPPER_UPSTREAM_RELEASE, &tt);
                    }
                }
                /* Do not call mapper_device_route_signal() here, since we don't
                 * know if the local signal instance will actually be released. */
                if (update_h)
                    update_h(sig, id_map->local, 0, 1, &tt);
                return 0;
            }
            if (memcmp(si->has_value_flags, sig->local->has_complete_value,
                       sig->length / 8 + 1)==0) {
                si->has_value = 1;
            }
            if (si->has_value) {
                memcpy(&si->timetag, &tt, sizeof(mapper_timetag_t));
                if (count > 1) {
                    memcpy(out_buffer + out_count * sig->length * size,
                           si->value, size);
                    out_count++;
                }
                else {
                    if (!(sig->direction & MAPPER_DIR_OUTGOING))
                        mapper_device_route_signal(dev, sig, id_map_index,
                                                   si->value, 1, tt);
                    if (update_h)
                        update_h(sig, id_map->local, si->value, 1, &tt);
                }
            }
        }
        if (out_count) {
            if (!(sig->direction & MAPPER_DIR_OUTGOING))
                mapper_device_route_signal(dev, sig, id_map_index, out_buffer,
                                           out_count, tt);
            if (update_h)
                update_h(sig, id_map->local, out_buffer, out_count, &tt);
        }
    }

    return 0;
}

//static int handler_instance_release_request(const char *path, const char *types,
//                                            lo_arg **argv, int argc, lo_message msg,
//                                            const void *user_data)
//{
//    mapper_signal sig = (mapper_signal) user_data;
//    mapper_device dev = sig->device;
//
//    if (!dev)
//        return 0;
//
//    if (!sig->local->instance_event_handler ||
//        !(sig->local->instance_event_flags & MAPPER_DOWNSTREAM_RELEASE))
//        return 0;
//
//    lo_timetag tt = lo_message_get_timestamp(msg);
//
//    int index = mapper_signal_find_instance_with_global_id(sig, argv[0]->i64, 0);
//    if (index < 0)
//        return 0;
//
//    if (sig->local->instance_event_handler) {
//        sig->local->instance_event_handler(sig, sig->local->id_maps[index].map->local,
//                                           MAPPER_DOWNSTREAM_RELEASE, &tt);
//    }
//
//    return 0;
//}

static int handler_query(const char *path, const char *types, lo_arg **argv,
                         int argc, lo_message msg, void *user_data)
{
    mapper_signal sig = (mapper_signal)user_data;
    mapper_device dev = sig->device;
    int length = sig->length;
    char type = sig->type;

    if (!dev) {
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
    for (i = 0; i < sig->local->id_map_length; i++) {
        if (!(si = sig->local->id_maps[i].instance))
            continue;
        lo_message m = lo_message_new();
        if (!m)
            continue;
        message_add_coerced_signal_instance_value(m, sig, si, length, type);
        if (sig->num_instances > 1) {
            lo_message_add_string(m, "@instance");
            lo_message_add_int64(m, sig->local->id_maps[i].map->global);
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

static mapper_id get_unused_signal_id(mapper_device dev)
{
    int done = 0;
    mapper_id id;
    while (!done) {
        done = 1;
        id = mapper_device_unique_id(dev);
        // check if input signal exists with this id
        mapper_signal *sig = mapper_device_signals(dev, MAPPER_DIR_ANY);
        while (sig) {
            if ((*sig)->id == id) {
                done = 0;
                mapper_signal_query_done(sig);
                break;
            }
            sig = mapper_signal_query_next(sig);
        }
    }
    return id;
}

// Add a signal to a mapper device.
static mapper_signal mapper_device_add_signal(mapper_device dev,
                                              mapper_direction dir,
                                              const char *name, int length,
                                              char type, const char *unit,
                                              const void *minimum,
                                              const void *maximum,
                                              mapper_signal_update_handler *handler,
                                              const void *user_data)
{
    if (!dev || !dev->local)
        return 0;
    if (!name || check_signal_length(length) || check_signal_type(type))
        return 0;

    mapper_db db = dev->db;
    mapper_signal sig;
    if ((sig = mapper_device_signal_by_name(dev, name)))
        return sig;

    sig = (mapper_signal)mapper_list_add_item((void**)&db->signals,
                                              sizeof(mapper_signal_t));
    sig->local = (mapper_local_signal)calloc(1, sizeof(mapper_local_signal_t));

    sig->device = dev;
    mapper_signal_init(sig, name, length, type, dir, unit, minimum, maximum,
                       handler, user_data);

    sig->id = get_unused_signal_id(dev);

    if (dir == MAPPER_DIR_INCOMING)
        dev->num_inputs++;
    if (dir == MAPPER_DIR_OUTGOING) {
        dev->num_outputs++;
    }

    mapper_device_increment_version(dev);

    mapper_device_add_signal_methods(dev, sig);

    if (dev->local->registered) {
        // Notify subscribers
        mapper_network_set_dest_subscribers(db->network,
                                            (dir == MAPPER_DIR_INCOMING)
                                            ? MAPPER_SUBSCRIBE_INPUTS
                                            : MAPPER_SUBSCRIBE_OUTPUTS);
        mapper_signal_send_state(sig, STATIC_PROPS);
    }

    return sig;
}

mapper_signal mapper_device_add_input(mapper_device dev, const char *name,
                                      int length, char type, const char *unit,
                                      const void *minimum, const void *maximum,
                                      mapper_signal_update_handler *handler,
                                      const void *user_data)
{
    return mapper_device_add_signal(dev, MAPPER_DIR_INCOMING, name, length,
                                    type, unit, minimum, maximum, handler,
                                    user_data);
}

mapper_signal mapper_device_add_output(mapper_device dev, const char *name,
                                       int length, char type, const char *unit,
                                       const void *minimum, const void *maximum)
{
    return mapper_device_add_signal(dev, MAPPER_DIR_OUTGOING, name, length,
                                    type, unit, minimum, maximum, 0, 0);
}

void mapper_device_add_signal_methods(mapper_device dev, mapper_signal sig)
{
    if (!sig || !sig->local)
        return;

    char *path = 0;
    int len = strlen(sig->path) + 5;
    path = (char*)realloc(path, len);
    snprintf(path, len, "%s%s", sig->path, "/get");
    lo_server_add_method(dev->local->server, path, NULL, handler_query,
                         (void*)(sig));
    free(path);

    if (sig->local->update_handler)
        lo_server_add_method(dev->local->server, sig->path, NULL,
                             handler_signal, (void*)(sig));

    dev->local->n_output_callbacks ++;
}

void mapper_device_remove_signal_methods(mapper_device dev, mapper_signal sig)
{
    if (!sig || !sig->local)
        return;

    char *path = 0;
    int len = (int)strlen(sig->path) + 5;
    path = (char*)realloc(path, len);
    snprintf(path, len, "%s%s", sig->path, "/get");
    lo_server_del_method(dev->local->server, path, NULL);
    free(path);

    lo_server_del_method(dev->local->server, sig->path, NULL);

    dev->local->n_output_callbacks --;
}

static void send_unmap(mapper_network net, mapper_map map)
{
    if (!map->status)
        return;

    // TODO: send appropriate messages using mesh
//    mapper_network_set_dest_mesh(net, map->remote_dest->network_addr);
    mapper_network_set_dest_bus(net);

    lo_message m = lo_message_new();
    if (!m)
        return;

    char dest_name[1024], source_names[1024];
    int i, len = 0, result;
    for (i = 0; i < map->num_sources; i++) {
        result = snprintf(&source_names[len], 1024-len, "%s%s",
                          map->sources[i].signal->device->name,
                          map->sources[i].signal->path);
        if (result < 0 || (len + result + 1) >= 1024) {
            trace("Error encoding sources for /unmap msg");
            lo_message_free(m);
            return;
        }
        lo_message_add_string(m, &source_names[len]);
        len += result + 1;
    }
    lo_message_add_string(m, "->");
    snprintf(dest_name, 1024, "%s%s", map->destination.signal->device->name,
             map->destination.signal->path);
    lo_message_add_string(m, dest_name);
    mapper_network_add_message(net, 0, MSG_UNMAP, m);
    mapper_network_send(net);
}

void mapper_device_remove_signal(mapper_device dev, mapper_signal sig)
{
    int i;
    if (!dev || !sig || !sig->local || sig->device != dev)
        return;

    mapper_direction dir = sig->direction;
    mapper_device_remove_signal_methods(dev, sig);

    mapper_router_signal rs = dev->local->router->signals;
    while (rs && rs->signal != sig)
        rs = rs->next;
    if (rs) {
        // need to unmap
        for (i = 0; i < rs->num_slots; i++) {
            if (rs->slots[i]) {
                mapper_map map = rs->slots[i]->map;
                send_unmap(dev->db->network, map);
                mapper_router_remove_map(dev->local->router, map);
            }
        }
        mapper_router_remove_signal(dev->local->router, rs);
    }

    if (dev->local->registered) {
        // Notify subscribers
        mapper_network_set_dest_subscribers(dev->db->network,
                                            (dir == MAPPER_DIR_INCOMING)
                                            ? MAPPER_SUBSCRIBE_INPUTS
                                            : MAPPER_SUBSCRIBE_OUTPUTS);
        mapper_signal_send_removed(sig);
    }

    mapper_db_remove_signal(dev->db, sig);
    mapper_device_increment_version(dev);
}

int mapper_device_num_signals(mapper_device dev, mapper_direction dir)
{
    if (!dev)
        return 0;
    if (!dir)
        return dev->num_inputs + dev->num_outputs;
    return (  (dir == MAPPER_DIR_INCOMING) * dev->num_inputs
            + (dir == MAPPER_DIR_OUTGOING) * dev->num_outputs);
}

static int cmp_query_device_signals(const void *context_data, mapper_signal sig)
{
    mapper_id dev_id = *(mapper_id*)context_data;
    int direction = *(int*)(context_data + sizeof(mapper_id));
    return ((!direction || (sig->direction & direction))
            && (dev_id == sig->device->id));
}

mapper_signal *mapper_device_signals(mapper_device dev, mapper_direction dir)
{
    if (!dev || !dev->db->signals)
        return 0;
    return ((mapper_signal *)
            mapper_list_new_query(dev->db->signals, cmp_query_device_signals,
                                  "hi", dev->name ? dev->id : 0, dir));
}

mapper_signal mapper_device_signal_by_id(mapper_device dev, mapper_id id)
{
    if (!dev)
        return 0;
    mapper_signal sig = dev->db->signals;
    if (!sig)
        return 0;

    while (sig) {
        if ((sig->device == dev) && (sig->id == id))
            return sig;
        sig = mapper_list_next(sig);
    }
    return 0;
}

mapper_signal mapper_device_signal_by_name(mapper_device dev,
                                           const char *sig_name)
{
    if (!dev)
        return 0;
    mapper_signal sig = dev->db->signals;
    if (!sig)
        return 0;

    while (sig) {
        if ((sig->device == dev) && strcmp(sig->name, skip_slash(sig_name))==0)
        return sig;
        sig = mapper_list_next(sig);
    }
    return 0;
}

mapper_signal mapper_device_signal_by_index(mapper_device dev, int index,
                                            mapper_direction dir)
{
    if (!dev || index < 0)
        return 0;
    mapper_signal sig = dev->db->signals;
    if (!sig)
        return 0;

    int count = -1;
    while (sig && count < index) {
        if ((sig->device == dev) && (!dir || (sig->direction & dir))) {
            if (++count == index)
            return sig;
        }
        sig = mapper_list_next(sig);
    }
    return 0;
}

int mapper_device_num_maps(mapper_device dev, mapper_direction dir)
{
    if (!dev)
        return 0;
    if (!dir)
        return dev->num_incoming_maps + dev->num_outgoing_maps;
    return (  ((dir & MAPPER_DIR_INCOMING) ? dev->num_incoming_maps : 0)
            + ((dir & MAPPER_DIR_OUTGOING) ? dev->num_outgoing_maps : 0));
}

static int cmp_query_device_maps(const void *context_data, mapper_map map)
{
    mapper_id dev_id = *(mapper_id*)context_data;
    int direction = *(int*)(context_data + sizeof(mapper_id));
    if (!direction || (direction & MAPPER_DIR_OUTGOING)) {
        int i;
        for (i = 0; i < map->num_sources; i++) {
            if (map->sources[i].signal->device->id == dev_id)
            return 1;
        }
    }
    if (!direction || (direction & MAPPER_DIR_INCOMING)) {
        if (map->destination.signal->device->id == dev_id)
        return 1;
    }
    return 0;
}

mapper_map *mapper_device_maps(mapper_device dev, mapper_direction dir)
{
    if (!dev || !dev->db->maps)
        return 0;
    return ((mapper_map *)
            mapper_list_new_query(dev->db->maps, cmp_query_device_maps,
                                  "hi", dev->id, dir));
}

int mapper_device_poll(mapper_device dev, int block_ms)
{
    if (!dev || !dev->local)
        return 0;
    int admin_count = mapper_network_poll(dev->db->network);
    int count = 0;

    if (dev->local->server) {

        /* If a timeout is specified, loop until the time is up. */
        if (block_ms)
        {
            double then = mapper_get_current_time();
            int left_ms = block_ms;
            while (left_ms > 0)
            {
                if (lo_server_recv_noblock(dev->local->server, left_ms))
                    count++;
                double elapsed = mapper_get_current_time() - then;
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
        while (count < (dev->num_inputs + dev->local->n_output_callbacks)*1
               && lo_server_recv_noblock(dev->local->server, 0))
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

int mapper_device_num_fds(mapper_device dev)
{
    // Two for the admin inputs (bus and mesh), and one for the signal input.
    return 3;
}

int mapper_device_fds(mapper_device dev, int *fds, int num)
{
    if (!dev || !dev->local)
        return 0;
    if (num > 0)
        fds[0] = lo_server_get_socket_fd(dev->db->network->bus_server);
    if (num > 1) {
        fds[1] = lo_server_get_socket_fd(dev->db->network->mesh_server);
        if (num > 2)
            fds[2] = lo_server_get_socket_fd(dev->local->server);
        else
            return 2;
    }
    else
        return 1;
    return 3;
}

void mapper_device_service_fd(mapper_device dev, int fd)
{
    if (!dev || !dev->local)
        return;
    // TODO: separate fds for bus and mesh comms
    if (fd == lo_server_get_socket_fd(dev->db->network->bus_server))
        mapper_network_poll(dev->db->network);
    else if (dev->local->server
             && fd == lo_server_get_socket_fd(dev->local->server))
        lo_server_recv_noblock(dev->local->server, 0);
}

void mapper_device_num_instances_changed(mapper_device dev, mapper_signal sig,
                                         int size)
{
    if (!dev)
        return;
    mapper_router_num_instances_changed(dev->local->router, sig, size);
}

void mapper_device_route_signal(mapper_device dev, mapper_signal sig,
                                int instance_index, const void *value,
                                int count, mapper_timetag_t timetag)
{
    mapper_router_process_signal(dev->local->router, sig, instance_index,
                                 value, count, timetag);
}

// Function to start a mapper queue
void mapper_device_start_queue(mapper_device dev, mapper_timetag_t tt)
{
    if (!dev)
        return;
    mapper_router_start_queue(dev->local->router, tt);
}

void mapper_device_send_queue(mapper_device dev, mapper_timetag_t tt)
{
    if (!dev)
        return;
    mapper_router_send_queue(dev->local->router, tt);
}

int mapper_device_route_query(mapper_device dev, mapper_signal sig,
                              mapper_timetag_t tt)
{
    return mapper_router_send_query(dev->local->router, sig, tt);
}

void mapper_device_reserve_instance_id_map(mapper_device dev)
{
    mapper_id_map map;
    map = (mapper_id_map)calloc(1, sizeof(mapper_id_map_t));
    map->next = dev->local->reserve_id_map;
    dev->local->reserve_id_map = map;
}

mapper_id_map mapper_device_add_instance_id_map(mapper_device dev,
                                                mapper_id local_id,
                                                mapper_id global_id)
{
    if (!dev->local->reserve_id_map)
        mapper_device_reserve_instance_id_map(dev);

    mapper_id_map map = dev->local->reserve_id_map;
    map->local = local_id;
    map->global = global_id;
    map->refcount_local = 1;
    map->refcount_global = 0;
    dev->local->reserve_id_map = map->next;
    map->next = dev->local->active_id_map;
    dev->local->active_id_map = map;
    return map;
}

void mapper_device_remove_instance_id_map(mapper_device dev, mapper_id_map map)
{
    mapper_id_map *id_map = &dev->local->active_id_map;
    while (*id_map) {
        if ((*id_map) == map) {
            *id_map = (*id_map)->next;
            map->next = dev->local->reserve_id_map;
            dev->local->reserve_id_map = map;
            break;
        }
        id_map = &(*id_map)->next;
    }
}

mapper_id_map mapper_device_find_instance_id_map_by_local(mapper_device dev,
                                                          mapper_id local_id)
{
    mapper_id_map map = dev->local->active_id_map;
    while (map) {
        if (map->local == local_id)
            return map;
        map = map->next;
    }
    return 0;
}

mapper_id_map mapper_device_find_instance_id_map_by_global(mapper_device dev,
                                                           mapper_id global_id)
{
    mapper_id_map map = dev->local->active_id_map;
    while (map) {
        if (map->global == global_id)
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

void mapper_device_start_server(mapper_device dev, int starting_port)
{
    if (dev->local->server)
        return;

    int len;
    char port[16], *pport = port, *path = 0;

    if (starting_port)
        sprintf(port, "%d", starting_port);
    else
        pport = 0;

    while (!(dev->local->server = lo_server_new(pport, liblo_error_handler))) {
        pport = 0;
    }

    // Disable liblo message queueing
    lo_server_enable_queue(dev->local->server, 0, 1);

    dev->port = lo_server_get_port(dev->local->server);
    trace("bound to port %i\n", dev->port);

    // add signal methods
    mapper_signal *sig = mapper_device_signals(dev, MAPPER_DIR_ANY);
    while (sig) {
        if ((*sig)->local->update_handler)
            lo_server_add_method(dev->local->server, (*sig)->path, NULL,
                                 handler_signal, (void*)(*sig));

        len = (int)strlen((*sig)->path) + 5;
        path = (char*)realloc(path, len);
        snprintf(path, len, "%s%s", (*sig)->path, "/get");
        lo_server_add_method(dev->local->server, path, NULL, handler_query,
                             (void*)(*sig));
        sig = mapper_signal_query_next(sig);
    }
    free(path);
}

const char *mapper_device_name(mapper_device dev)
{
    if (dev->local && (!dev->local->registered || !dev->local->ordinal.locked))
        return 0;

    if (dev->name)
        return dev->name;

    unsigned int len = strlen(dev->identifier) + 6;
    dev->name = (char*)malloc(len);
    dev->name[0] = 0;
    snprintf(dev->name, len, "%s.%d", dev->identifier, dev->local->ordinal.value);
    return dev->name;
}

const char *mapper_device_description(mapper_device dev)
{
    return dev->description;
}

const char *mapper_device_host(mapper_device dev)
{
    return dev->host;
}

mapper_id mapper_device_id(mapper_device dev)
{
    return dev->id;
}

int mapper_device_is_local(mapper_device dev)
{
    return dev && dev->local;
}

unsigned int mapper_device_port(mapper_device dev)
{
    return dev->port;
}

unsigned int mapper_device_ordinal(mapper_device dev)
{
    return dev->ordinal;
}

int mapper_device_ready(mapper_device dev)
{
    return dev->status >= STATUS_READY;
}

void mapper_device_synced(mapper_device dev, mapper_timetag_t *timetag)
{
    if (!dev || !timetag)
        return;
    if (dev->local)
        mapper_clock_now(&dev->db->network->clock, timetag);
    else
        mapper_timetag_copy(timetag, dev->synced);
}

int mapper_device_version(mapper_device dev)
{
    return dev ? dev->version : 0;
}

void mapper_device_set_property(mapper_device dev, const char *name, int length,
                                char type, const void *value)
{
    mapper_property_t prop = mapper_property_from_string(name);
    mapper_table_set_record(dev->props, prop, name, length, type, value,
                            dev->local ? LOCAL_MODIFY : REMOTE_MODIFY);
}

void mapper_device_set_description(mapper_device dev, const char *description)
{
    if (!dev)
        return;
    mapper_table_set_record(dev->props, AT_DESCRIPTION, NULL, 1, 's',
                            description, LOCAL_MODIFY);
}

int mapper_device_property(mapper_device dev, const char *name,
                           int *length, char *type, const void **value)
{
    return mapper_table_property(dev->props, name, length, type, value);
}

int mapper_device_property_index(mapper_device dev, unsigned int index,
                                 const char **name, int *length, char *type,
                                 const void **value)
{
    return mapper_table_property_index(dev->props, index, name, length, type,
                                       value);
}

void mapper_device_remove_property(mapper_device dev, const char *name)
{
    mapper_property_t prop = mapper_property_from_string(name);
    mapper_table_remove_record(dev->props, prop, name, 1, LOCAL_MODIFY);
}

lo_server mapper_device_lo_server(mapper_device dev)
{
    return dev->local->server;
}

void mapper_device_now(mapper_device dev, mapper_timetag_t *timetag)
{
    if (!dev || !timetag)
        return;
    mapper_clock_now(&dev->db->network->clock, timetag);
}

mapper_id mapper_device_unique_id(mapper_device dev) {
    return ++dev->db->resource_counter | dev->id;
}

void mapper_device_send_state(mapper_device dev, int flags)
{
    if (!dev)
        return;
    lo_message msg = lo_message_new();
    if (!msg) {
        trace("couldn't allocate lo_message\n");
        return;
    }

    /* device name */
    lo_message_add_string(msg, mapper_device_name(dev));

    /* properties */
    int i, indirect;
    mapper_table_t *tab = ((flags == UPDATED_PROPS)
                           ? dev->staged_props : dev->props);
    mapper_table_record_t *rec;
    for (i = 0; i < tab->num_records; i++) {
        rec = &tab->records[i];
        indirect = (flags != UPDATED_PROPS) && (rec->flags & INDIRECT);
        if (!rec->value)
            continue;
        if (indirect && !(*rec->value))
            continue;
        switch (rec->index) {
            case AT_STATUS:
            case AT_SYNCED:
            case AT_USER_DATA:
                // local access only
                break;
            case AT_NAME:
                // already sent
                break;
            case AT_EXTRA:
                lo_message_add_string(msg, rec->key);
                mapper_message_add_typed_value(msg, rec->length, rec->type,
                                               indirect ? *rec->value : rec->value);
                break;
            default:
                lo_message_add_string(msg, mapper_protocol_string(rec->index));
                mapper_message_add_typed_value(msg, rec->length, rec->type,
                                               indirect ? *rec->value : rec->value);
                break;
        }
    }

    mapper_network_add_message(dev->db->network, 0, MSG_DEVICE, msg);
}

void mapper_device_set_map_handler(mapper_device dev,
                                   mapper_device_map_handler *h,
                                   const void *user)
{
    if (!dev || !dev->local)
        return;
    dev->local->map_handler = h;
    dev->local->map_handler_userdata = (void*)user;
}

mapper_db mapper_device_db(mapper_device dev)
{
    return dev->db;
}

mapper_device *mapper_device_query_union(mapper_device *query1,
                                         mapper_device *query2)
{
    return (mapper_device*)mapper_list_query_union((void**)query1,
                                                   (void**)query2);
}

mapper_device *mapper_device_query_intersection(mapper_device *query1,
                                                mapper_device *query2)
{
    return (mapper_device*)mapper_list_query_intersection((void**)query1,
                                                          (void**)query2);
}

mapper_device *mapper_device_query_difference(mapper_device *query1,
                                              mapper_device *query2)
{
    return (mapper_device*)mapper_list_query_difference((void**)query1,
                                                        (void**)query2);
}

mapper_device mapper_device_query_index(mapper_device *query, int index)
{
    return (mapper_device)mapper_list_query_index((void**)query, index);
}

mapper_device *mapper_device_query_next(mapper_device *query)
{
    return (mapper_device*)mapper_list_query_next((void**)query);
}

mapper_device *mapper_device_query_copy(mapper_device *query)
{
    return (mapper_device*)mapper_list_query_copy((void**)query);
}

void mapper_device_query_done(mapper_device *query)
{
    mapper_list_query_done((void**)query);
}

/*! Update information about a device record based on message properties. */
int mapper_device_set_from_message(mapper_device dev, mapper_message msg)
{
    if (!msg)
        return 0;
    return mapper_table_set_from_message(dev->props, msg, REMOTE_MODIFY);
}

void mapper_device_send_inputs(mapper_device dev, int min, int max)
{
    if (min < 0)
        min = 0;
    else if (min > dev->num_inputs)
        return;
    if (max < 0 || max > dev->num_inputs)
        max = dev->num_inputs-1;

    int i = 0;
    mapper_signal *sig = mapper_device_signals(dev, MAPPER_DIR_INCOMING);
    while (sig) {
        if (i > max) {
            mapper_signal_query_done(sig);
            return;
        }
        if (i >= min)
            mapper_signal_send_state(*sig, STATIC_PROPS);
        i++;
        sig = mapper_signal_query_next(sig);
    }
}

void mapper_device_send_outputs(mapper_device dev, int min, int max)
{
    if (min < 0)
        min = 0;
    else if (min > dev->num_outputs)
        return;
    if (max < 0 || max > dev->num_outputs)
        max = dev->num_outputs-1;

    int i = 0;
    mapper_signal *sig = mapper_device_signals(dev, MAPPER_DIR_OUTGOING);
    while (sig) {
        if (i > max) {
            mapper_signal_query_done(sig);
            return;
        }
        if (i >= min)
            mapper_signal_send_state(*sig, STATIC_PROPS);
        i++;
        sig = mapper_signal_query_next(sig);
    }
}

static void mapper_device_send_incoming_maps(mapper_device dev, int min, int max)
{
    int i, count = 0;
    mapper_router_signal rs = dev->local->router->signals;
    while (rs) {
        for (i = 0; i < rs->num_slots; i++) {
            if (!rs->slots[i] || rs->slots[i]->direction == MAPPER_DIR_OUTGOING)
                continue;
            if (max > 0 && count > max)
                return;
            if (count >= min) {
                mapper_map map = rs->slots[i]->map;
                mapper_network_init(dev->db->network);
                mapper_map_send_state(map, -1, MSG_MAPPED, STATIC_PROPS);
            }
            count++;
        }
        rs = rs->next;
    }
}

static void mapper_device_send_outgoing_maps(mapper_device dev, int min, int max)
{
    int i, count = 0;
    mapper_router_signal rs = dev->local->router->signals;
    while (rs) {
        for (i = 0; i < rs->num_slots; i++) {
            if (!rs->slots[i] || rs->slots[i]->direction == MAPPER_DIR_INCOMING)
                continue;
            if (max > 0 && count > max)
                return;
            if (count >= min) {
                mapper_map map = rs->slots[i]->map;
                mapper_network_init(dev->db->network);
                mapper_map_send_state(map, -1, MSG_MAPPED, STATIC_PROPS);
            }
            count++;
        }
        rs = rs->next;
    }
}

// Add/renew/remove a subscription.
void mapper_device_manage_subscriber(mapper_device dev, lo_address address,
                                     int flags, int timeout_seconds,
                                     int revision)
{
    mapper_subscriber *s = &dev->local->subscribers;
    const char *ip = lo_address_get_hostname(address);
    const char *port = lo_address_get_port(address);
    if (!ip || !port)
        return;

    mapper_clock_t *clock = &dev->db->network->clock;
    mapper_clock_now(clock, &clock->now);

    while (*s) {
        if (strcmp(ip, lo_address_get_hostname((*s)->address))==0 &&
            strcmp(port, lo_address_get_port((*s)->address))==0) {
            // subscriber already exists
            if (!flags || !timeout_seconds) {
                // remove subscription
                mapper_subscriber temp = *s;
                int prev_flags = temp->flags;
                *s = temp->next;
                if (temp->address)
                    lo_address_free(temp->address);
                free(temp);
                if (!flags || !(flags &= ~prev_flags))
                    return;
            }
            else {
                // reset timeout
                (*s)->lease_expiration_sec = clock->now.sec + timeout_seconds;
                if ((*s)->flags == flags) {
                    if (revision)
                        return;
                    else
                        break;
                }
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

    if (!(*s) && timeout_seconds) {
        // add new subscriber
        mapper_subscriber sub = malloc(sizeof(struct _mapper_subscriber));
        sub->address = lo_address_new(ip, port);
        sub->lease_expiration_sec = clock->now.sec + timeout_seconds;
        sub->flags = flags;
        sub->next = dev->local->subscribers;
        dev->local->subscribers = sub;
        s = &sub;
    }

    if (revision == dev->version)
        return;

    // bring new subscriber up to date
    mapper_network_set_dest_mesh(dev->db->network, address);
    mapper_device_send_state(dev, STATIC_PROPS);
    if (flags & MAPPER_SUBSCRIBE_INPUTS)
        mapper_device_send_inputs(dev, -1, -1);
    if (flags & MAPPER_SUBSCRIBE_OUTPUTS)
        mapper_device_send_outputs(dev, -1, -1);
    if (flags & MAPPER_SUBSCRIBE_INCOMING_MAPS)
        mapper_device_send_incoming_maps(dev, -1, -1);
    if (flags & MAPPER_SUBSCRIBE_OUTGOING_MAPS)
        mapper_device_send_outgoing_maps(dev, -1, -1);
    mapper_network_send(dev->db->network);
}

void mapper_device_pp(mapper_device dev)
{
    if (!dev) {
        printf("NULL\n");
        return;
    }

    printf("%s", dev->name);

    int i = 0;
    const char *name;
    char type;
    const void *val;
    int len;
    while (!mapper_device_property_index(dev, i++, &name, &len, &type, &val)) {
        die_unless(val!=0, "returned zero value\n");

        // already printed this
        if (strcmp(name, "name")==0)
            continue;
        if (strcmp(name, "synced")==0) {
            mapper_timetag_t *tt = (mapper_timetag_t *)val;
            printf(", synced=%lu:%lu", (unsigned long)tt->sec,
                   (unsigned long)tt->frac);
            if (tt->sec) {
                mapper_timetag_t now;
                mapper_device_now(dev, &now);
                printf(" (%f seconds ago)", mapper_timetag_difference(now, *tt));
            }
        }
        else if (len) {
            printf(", %s=", name);
            mapper_property_pp(len, type, val);
        }
    }
    printf("\n");
}
