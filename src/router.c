#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <zlib.h>

#include <lo/lo.h>

#include "mapper_internal.h"
#include "types_internal.h"
#include <mapper/mapper.h>

static void send_or_bundle_message(mapper_link link, const char *path,
                                   lo_message msg, mapper_timetag_t tt);

static int map_in_scope(mapper_map map, uint64_t id)
{
    int i;
    id = id >> 32; // interested in device hash part only
    for (i = 0; i < map->scope.size; i++) {
        if (map->scope.hashes[i] == id ||
            map->scope.hashes[i] == 0)
            return 1;
    }
    return 0;
}

static void mapper_link_free(mapper_link link)
{
    if (link) {
        if (link->remote_device->host)
            free(link->remote_device->host);
        if (link->remote_device->name)
            free(link->remote_device->name);
        if (link->admin_addr)
            lo_address_free(link->admin_addr);
        if (link->data_addr)
            lo_address_free(link->data_addr);
        while (link->queues) {
            mapper_queue queue = link->queues;
            lo_bundle_free_messages(queue->bundle);
            link->queues = queue->next;
            free(queue);
        }
        free(link);
    }
}

// TODO: reuse device listed in db for remote
mapper_link mapper_router_add_link(mapper_router router, const char *host,
                                   int admin_port, int data_port,
                                   const char *name)
{
    if (!name)
        return 0;

    char str[16];
    mapper_link link = (mapper_link) calloc(1, sizeof(struct _mapper_link));
    link->remote_device = (mapper_device) calloc(1, sizeof(mapper_device_t));
    if (host) {
        link->remote_device->host = strdup(host);
        link->remote_device->port = data_port;
        snprintf(str, 16, "%d", data_port);
        link->data_addr = lo_address_new(host, str);
        snprintf(str, 16, "%d", admin_port);
        link->admin_addr = lo_address_new(host, str);
    }
    name += (name[0]=='/');
    link->remote_device->name = strdup(name);
    link->remote_device->id = crc32(0L, (const Bytef *)name, strlen(name) << 32);

    link->local_device = router->device;
    link->num_incoming_maps = 0;
    link->num_outgoing_maps = 0;

    if (name == mapper_device_name(router->device)) {
        /* Add data_addr for use by self-connections. In the future we may
         * decide to call local handlers directly, however this could result in
         * unfortunate loops/stack overflow. Sending data for self-connections
         * to localhost adds the messages to liblo's stack and imposes a delay
         * since the receiving handler will not be called until
         * mapper_device_poll(). */
        snprintf(str, 16, "%d", router->device->port);
        link->data_addr = lo_address_new("localhost", str);
    }

    link->clock.new = 1;
    link->clock.sent.message_id = 0;
    link->clock.response.message_id = -1;
    mapper_clock_t *clock = &router->device->db->admin->clock;
    mapper_clock_now(clock, &clock->now);
    link->clock.response.timetag.sec = clock->now.sec + 10;

    link->next = router->links;
    router->links = link;

    if (!host) {
        // request missing metadata
        char cmd[256];
        snprintf(cmd, 256, "/%s/subscribe", name);
        lo_message m = lo_message_new();
        if (m) {
            lo_message_add_string(m, "device");
            mapper_admin_set_bundle_dest_bus(router->device->db->admin);
            lo_bundle_add_message(router->device->db->admin->bundle, cmd, m);
            mapper_admin_send_bundle(router->device->db->admin);
        }
    }

    return link;
}

void mapper_router_update_link(mapper_router router, mapper_link link,
                               const char *host, int admin_port, int data_port)
{
    char str[16];
    link->remote_device->host = strdup(host);
    link->remote_device->port = data_port;
    sprintf(str, "%d", data_port);
    link->data_addr = lo_address_new(host, str);
    sprintf(str, "%d", admin_port);
    link->admin_addr = lo_address_new(host, str);
}

void mapper_router_remove_link(mapper_router router, mapper_link link)
{
    int i, j;
    // check if any maps use this link
    mapper_router_signal rs = router->signals;
    while (rs) {
        for (i = 0; i < rs->num_slots; i++) {
            if (!rs->slots[i])
                continue;
            mapper_map map = rs->slots[i]->map;
            if (map->destination.local->link == link) {
                mapper_router_remove_map(router, map);
                continue;
            }
            for (j = 0; j < map->num_sources; j++) {
                if (map->sources[j].local->link == link) {
                    mapper_router_remove_map(router, map);
                    break;
                }
            }
        }
        rs = rs->next;
    }
    mapper_link *links = &router->links;
    while (*links) {
        if (*links == link) {
            *links = (*links)->next;
            break;
        }
        links = &(*links)->next;
    }
    mapper_link_free(link);
}

static void reallocate_slot_instances(mapper_slot slot, int size)
{
    int i;
    if (slot->num_instances < size) {
        slot->local->history = realloc(slot->local->history,
                                       sizeof(struct _mapper_history) * size);
        for (i = slot->num_instances; i < size; i++) {
            slot->local->history[i].type = slot->type;
            slot->local->history[i].length = slot->length;
            slot->local->history[i].size = slot->local->history_size;
            slot->local->history[i].value = calloc(1, mapper_type_size(slot->type)
                                                   * slot->local->history_size);
            slot->local->history[i].timetag = calloc(1, sizeof(mapper_timetag_t)
                                                     * slot->local->history_size);
            slot->local->history[i].position = -1;
        }
        slot->num_instances = size;
    }
}

static void reallocate_map_instances(mapper_map map, int size)
{
    int i, j;
    if (   !(map->local->status & MAPPER_TYPE_KNOWN)
        || !(map->local->status & MAPPER_LENGTH_KNOWN)) {
        for (i = 0; i < map->num_sources; i++) {
            map->sources[i].num_instances = size;
        }
        map->destination.num_instances = size;
        return;
    }

    // check if source histories need to be reallocated
    for (i = 0; i < map->num_sources; i++)
        reallocate_slot_instances(&map->sources[i], size);

    // check if destination histories need to be reallocated
    reallocate_slot_instances(&map->destination, size);

    // check if expression variable histories need to be reallocated
    mapper_map_internal imap = map->local;
    if (size > imap->num_var_instances) {
        imap->expr_vars = realloc(imap->expr_vars, sizeof(mapper_history*) * size);
        for (i = imap->num_var_instances; i < size; i++) {
            imap->expr_vars[i] = malloc(sizeof(struct _mapper_history)
                                        * imap->num_expr_vars);
            for (j = 0; j < imap->num_expr_vars; j++) {
                imap->expr_vars[i][j].type = imap->expr_vars[0][j].type;
                imap->expr_vars[i][j].length = imap->expr_vars[0][j].length;
                imap->expr_vars[i][j].size = imap->expr_vars[0][j].size;
                imap->expr_vars[i][j].position = -1;
                imap->expr_vars[i][j].value = calloc(1, sizeof(double)
                                                     * imap->expr_vars[i][j].length
                                                     * imap->expr_vars[i][j].size);
                imap->expr_vars[i][j].timetag = calloc(1, sizeof(mapper_timetag_t)
                                                       * imap->expr_vars[i][j].size);
            }
        }
        imap->num_var_instances = size;
    }
}

// TODO: check for mismatched instance counts when using multiple sources
void mapper_router_num_instances_changed(mapper_router rtr, mapper_signal sig,
                                         int size)
{
    int i;
    // check if we have a reference to this signal
    mapper_router_signal rs = rtr->signals;
    while (rs) {
        if (rs->signal == sig)
            break;
        rs = rs->next;
    }

    if (!rs) {
        // The signal is not mapped through this router.
        return;
    }

    // for array of slots, may need to reallocate destination instances
    for (i = 0; i < rs->num_slots; i++) {
        mapper_slot slot = rs->slots[i];
        if (slot)
            reallocate_map_instances(slot->map, size);
    }
}

void mapper_router_process_signal(mapper_router rtr, mapper_signal sig,
                                  int instance, void *value, int count,
                                  mapper_timetag_t tt)
{
    mapper_id_map id_map = sig->local->id_maps[instance].map;
    lo_message msg;

    // find the router signal
    mapper_router_signal rs = rtr->signals;
    while (rs) {
        if (rs->signal == sig)
            break;
        rs = rs->next;
    }
    if (!rs)
        return;

    int i, j, k, id = sig->local->id_maps[instance].instance->index;
    mapper_map map;

    if (!value) {
        mapper_slot_internal islot;
        mapper_slot slot;

        for (i = 0; i < rs->num_slots; i++) {
            if (!rs->slots[i])
                continue;

            slot = rs->slots[i];
            map = slot->map;

            if (map->local->status < MAPPER_ACTIVE)
                continue;

            if (slot->direction == DI_OUTGOING) {
                mapper_slot dst_slot = &map->destination;
                mapper_slot_internal dst_islot = dst_slot->local;

                // also need to reset associated output memory
                dst_islot->history[id].position= -1;
                memset(dst_islot->history[id].value, 0, dst_islot->history_size
                       * dst_slot->length * mapper_type_size(dst_slot->type));
                memset(dst_islot->history[id].timetag, 0, dst_islot->history_size
                       * sizeof(mapper_timetag_t));

                if (!slot->send_as_instance)
                    msg = mapper_map_build_message(map, slot, 0, 1, 0, 0);
                else if (map_in_scope(map, id_map->global))
                    msg = mapper_map_build_message(map, slot, 0, 1, 0, id_map);
                if (msg)
                    send_or_bundle_message(dst_islot->link,
                                           dst_slot->signal->path, msg, tt);
                continue;
            }
            else if (!map_in_scope(map, id_map->global))
                continue;
            for (j = 0; j < map->num_sources; j++) {
                if (!map->sources[j].send_as_instance)
                    continue;
                slot = &map->sources[j];
                islot = slot->local;
                // send release to upstream
                msg = mapper_map_build_message(map, slot, 0, 1, 0, id_map);
                if (msg)
                    send_or_bundle_message(islot->link, slot->signal->path, msg, tt);

                // also need to reset associated input memory
                memset(islot->history[id].value, 0, islot->history_size
                       * slot->length * mapper_type_size(slot->type));
                memset(islot->history[id].timetag, 0, islot->history_size
                       * sizeof(mapper_timetag_t));
            }
        }
        return;
    }

    // if count > 1, we need to allocate sufficient memory for largest output
    // vector so that we can store calculated values before sending
    // TODO: calculate max_output_size, cache in link_signal
    void *out_value_p = count == 1 ? 0 : alloca(count * sig->length
                                                * sizeof(double));
    for (i = 0; i < rs->num_slots; i++) {
        if (!rs->slots[i])
            continue;

        mapper_slot slot = rs->slots[i];
        map = slot->map;

        if (map->local->status < MAPPER_ACTIVE)
            continue;

        int in_scope = map_in_scope(map, id_map->global);
        // TODO: should we continue for out-of-scope local destinaton updates?
        if (slot->send_as_instance && !in_scope) {
            continue;
        }

        mapper_slot_internal islot = slot->local;
        mapper_slot dst_slot = &map->destination;
        mapper_slot to = (map->process_location == LOC_SOURCE ? dst_slot : slot);
        int to_size = mapper_type_size(to->type) * to->length;
        char typestring[to->length * count];
        memset(typestring, to->type, to->length * count);
        k = 0;
        for (j = 0; j < count; j++) {
            // copy input history
            size_t n = mapper_signal_vector_bytes(sig);
            islot->history[id].position = ((islot->history[id].position + 1)
                                           % islot->history[id].size);
            memcpy(mapper_history_value_ptr(islot->history[id]), value + n * j, n);
            memcpy(mapper_history_tt_ptr(islot->history[id]),
                   &tt, sizeof(mapper_timetag_t));

            // process source boundary behaviour
            if ((mapper_boundary_perform(&islot->history[id], slot,
                                         typestring + to->length * k))) {
                // back up position index
                --islot->history[id].position;
                if (islot->history[id].position < 0)
                    islot->history[id].position = islot->history[id].size - 1;
                continue;
            }

            if (slot->direction == DI_INCOMING) {
                continue;
            }

            if (map->process_location == LOC_SOURCE && !slot->cause_update)
                continue;

            if (!(mapper_map_perform(map, slot, instance, typestring + to->length * k)))
                continue;

            if (map->process_location == LOC_SOURCE) {
                // also process destination boundary behaviour
                if ((mapper_boundary_perform(&map->destination.local->history[id],
                                             slot, typestring + to->length * k))) {
                    // back up position index
                    --map->destination.local->history[id].position;
                    if (map->destination.local->history[id].position < 0)
                        map->destination.local->history[id].position = map->destination.local->history[id].size - 1;
                    continue;
                }
            }

            void *result = mapper_history_value_ptr(map->destination.local->history[id]);

            if (count > 1) {
                memcpy((char*)out_value_p + to_size * j, result, to_size);
            }
            else {
                msg = mapper_map_build_message(map, slot, result, 1, typestring,
                                               slot->send_as_instance ? id_map : 0);
                if (msg)
                    send_or_bundle_message(map->destination.local->link,
                                           dst_slot->signal->path, msg, tt);
            }
            k++;
        }
        if (count > 1 && slot->direction == DI_OUTGOING
            && (!slot->send_as_instance || in_scope)) {
            msg = mapper_map_build_message(map, slot, out_value_p, k, typestring,
                                           slot->send_as_instance ? id_map : 0);
            if (msg)
                send_or_bundle_message(map->destination.local->link,
                                       dst_slot->signal->path, msg, tt);
        }
    }
}

int mapper_router_send_query(mapper_router rtr, mapper_signal sig,
                             mapper_timetag_t tt)
{
    if (!sig->local->update_handler) {
        trace("not sending queries since signal has no handler.\n");
        return 0;
    }
    // find the corresponding router_signal
    mapper_router_signal rs = rtr->signals;
    while (rs && rs->signal != sig)
        rs = rs->next;

    // exit without failure if signal is not mapped
    if (!rs)
        return 0;

    // for each map, query remote signals
    int i, j, count = 0;
    char query_string[256];
    for (i = 0; i < rs->num_slots; i++) {
        if (!rs->slots[i])
            continue;
        mapper_map map = rs->slots[i]->map;
        if (map->local->status != MAPPER_ACTIVE)
            continue;
        lo_message msg = lo_message_new();
        if (!msg)
            continue;
        lo_message_add_string(msg, sig->path);
        lo_message_add_int32(msg, sig->length);
        lo_message_add_char(msg, sig->type);
        // TODO: include response address as argument to allow TCP queries?
        // TODO: always use TCP for queries?

        if (rs->slots[i]->direction == DI_OUTGOING) {
            snprintf(query_string, 256, "%s/get", map->destination.signal->path);
            send_or_bundle_message(map->destination.local->link, query_string, msg, tt);
        }
        else {
            for (j = 0; j < map->num_sources; j++) {
                snprintf(query_string, 256, "%s/get", map->sources[j].signal->path);
                send_or_bundle_message(map->sources[j].local->link, query_string, msg, tt);
            }
        }
        count++;
    }
    return count;
}

// note on memory handling of mapper_router_bundle_message():
// path: not owned, will not be freed (assumed is signal name, owned
//       by signal)
// message: will be owned, will be freed when done
void send_or_bundle_message(mapper_link link, const char *path,
                            lo_message m, mapper_timetag_t tt)
{
    // Check if a matching bundle exists
    mapper_queue q = link->queues;
    while (q) {
        if (memcmp(&q->tt, &tt,
                   sizeof(mapper_timetag_t))==0)
            break;
        q = q->next;
    }
    if (q) {
        // Add message to existing bundle
        lo_bundle_add_message(q->bundle, path, m);
    }
    else {
        // Send message immediately
        lo_bundle b = lo_bundle_new(tt);
        lo_bundle_add_message(b, path, m);
        lo_send_bundle_from(link->data_addr,
                            link->local_device->local->server, b);
        lo_bundle_free_messages(b);
    }
}

void mapper_router_start_queue(mapper_router rtr, mapper_timetag_t tt)
{
    // first check if queue already exists
    mapper_link link = rtr->links;
    while (link) {
        mapper_queue queue = link->queues;
        while (queue) {
            if (memcmp(&queue->tt, &tt, sizeof(mapper_timetag_t))==0)
                return;
            queue = queue->next;
        }

        // need to create new queue
        queue = malloc(sizeof(struct _mapper_queue));
        memcpy(&queue->tt, &tt, sizeof(mapper_timetag_t));
        queue->bundle = lo_bundle_new(tt);
        queue->next = link->queues;
        link->queues = queue;
        link = link->next;
    }
}

void mapper_router_send_queue(mapper_router rtr, mapper_timetag_t tt)
{
    mapper_link link = rtr->links;
    while (link) {
        mapper_queue *queue = &link->queues;
        while (*queue) {
            if (memcmp(&(*queue)->tt, &tt, sizeof(mapper_timetag_t))==0)
                break;
            queue = &(*queue)->next;
        }
        if (*queue) {
#ifdef HAVE_LIBLO_BUNDLE_COUNT
            if (lo_bundle_count((*queue)->bundle))
#endif
                lo_send_bundle_from(link->data_addr,
                                    link->local_device->local->server,
                                    (*queue)->bundle);
            lo_bundle_free_messages((*queue)->bundle);
            mapper_queue temp = *queue;
            *queue = (*queue)->next;
            free(temp);
        }
        link = link->next;
    }
}

static mapper_router_signal find_or_add_router_signal(mapper_router rtr,
                                                      mapper_signal sig)
{
    // find signal in router_signal list
    mapper_router_signal rs = rtr->signals;
    while (rs && rs->signal != sig)
        rs = rs->next;

    // if not found, create a new list entry
    if (!rs) {
        rs = ((mapper_router_signal)
              calloc(1, sizeof(struct _mapper_router_signal)));
        rs->signal = sig;
        rs->num_slots = 1;
        rs->slots = malloc(sizeof(mapper_slot_internal *));
        rs->slots[0] = 0;
        rs->next = rtr->signals;
        rtr->signals = rs;
    }
    return rs;
}

static int router_signal_store_slot(mapper_router_signal rs, mapper_slot slot)
{
    int i;
    for (i = 0; i < rs->num_slots; i++) {
        if (!rs->slots[i]) {
            // store pointer at empty index
            rs->slots[i] = slot;
            return i;
        }
    }
    // all indices occupied, allocate more
    rs->slots = realloc(rs->slots, sizeof(mapper_slot*) * rs->num_slots * 2);
    rs->slots[rs->num_slots] = slot;
    for (i = rs->num_slots+1; i < rs->num_slots * 2; i++) {
        rs->slots[i] = 0;
    }
    i = rs->num_slots;
    rs->num_slots *= 2;
    return i;
}

static uint64_t unused_map_id(mapper_device dev, mapper_router rtr)
{
    int i, done = 0;
    uint64_t id;
    while (!done) {
        done = 1;
        id = mapper_device_unique_id(dev);
        // check if map exists with this id
        mapper_router_signal rs = rtr->signals;
        while (rs) {
            for (i = 0; i < rs->num_slots; i++) {
                if (!rs->slots[i])
                    continue;
                if (rs->slots[i]->map->id == id) {
                    done = 0;
                    break;
                }
            }
            rs = rs->next;
        }
    }
    return id;
}

mapper_map mapper_router_add_map(mapper_router rtr, mapper_signal sig,
                                 int num_sources, mapper_signal *local_sigs,
                                 const char **sig_names, mapper_direction_t dir)
{
    int i, ready = 1;

    if (num_sources > MAX_NUM_MAP_SOURCES) {
        trace("error: maximum number of remote signals in a map exceeded.\n");
        return 0;
    }

    mapper_router_signal rtr_sig = find_or_add_router_signal(rtr, sig);
    mapper_router_signal rtr_sig2;
    mapper_map map = (mapper_map) calloc(1, sizeof(struct _mapper_map));
    mapper_map_internal imap = ((mapper_map_internal)
                                calloc(1, sizeof(struct _mapper_map_internal)));
    map->local = imap;

    imap->router = rtr;

    // TODO: configure number of instances available for each slot
    imap->num_var_instances = sig->num_instances;

    map->num_sources = num_sources;
    map->sources = (mapper_slot) calloc(1, sizeof(struct _mapper_slot)
                                        * num_sources);
    for (i = 0; i < num_sources; i++)
        map->sources[i].local = ((mapper_slot_internal)
                                 calloc(1, sizeof(struct _mapper_slot_internal)));
    map->destination.local = ((mapper_slot_internal)
                              calloc(1, sizeof(struct _mapper_slot_internal)));

    // scopes
    map->scope.size = (dir == DI_OUTGOING) ? 1 : num_sources;
    map->scope.names = (char **) malloc(sizeof(char *) * map->scope.size);
    map->scope.hashes = (uint32_t *) malloc(sizeof(uint32_t) * map->scope.size);

    // is_admin property will be corrected later if necessary
    imap->is_admin = 1;
    if (dir == DI_INCOMING)
        map->id = unused_map_id(rtr->device, rtr);

    mapper_link link;
    char devname[256], *devnamep, *signame;
    const char *remote_devname_ptr;
    int devnamelen, scope_count = 0, local_scope = 0;
    mapper_slot slot;
    if (dir == DI_OUTGOING) {
        ready = 0;
        for (i = 0; i < num_sources; i++) {
            // find router_signal
            rtr_sig2 = find_or_add_router_signal(rtr, local_sigs[i]);
            map->sources[i].local->router_sig = rtr_sig2;
            slot = &map->sources[i];
            slot->signal = local_sigs[i];
            slot->type = local_sigs[i]->type;
            slot->length = local_sigs[i]->length;
            slot->local->status = MAPPER_READY;
            slot->direction = DI_OUTGOING;
            slot->num_instances = local_sigs[i]->num_instances;
            if (slot->num_instances > imap->num_var_instances)
                imap->num_var_instances = slot->num_instances;

            /* slot index will be overwritten if necessary by
             * mapper_map_set_from_message() */
            slot->id = -1;

            // also need to add map to lists kept by source rs
            router_signal_store_slot(rtr_sig2, &map->sources[i]);
        }

        devnamelen = mapper_parse_names(sig_names[0], &devnamep, &signame);
        if (!devnamelen || devnamelen >= 256) {
            // TODO: free partially-built map structure
            return 0;
        }
        strncpy(devname, devnamep, devnamelen);
        devname[devnamelen] = 0;
        slot = &map->destination;
        slot->signal = calloc(1, sizeof(mapper_signal_t));
        int signamelen = strlen(signame)+2;
        slot->signal->path = malloc(signamelen);
        snprintf(slot->signal->path, signamelen, "/%s", signame);
        slot->signal->name = slot->signal->path+1;
        slot->direction = DI_OUTGOING;
        slot->num_instances = sig->num_instances;

        link = mapper_router_find_link_by_remote_name(rtr, devname);
        if (link) {
            map->destination.local->link = link;
        }
        else
            map->destination.local->link = mapper_router_add_link(rtr, 0, 0,
                                                                  0, devname);
        slot->signal->device = map->destination.local->link->remote_device;

        // apply local scope as default
        map->scope.names[0] = strdup(mapper_device_name(rtr->device));
        map->scope.hashes[0] = mapper_device_id(rtr->device) >> 32;
    }
    else {
        map->destination.local->router_sig = rtr_sig;
        slot = &map->destination;
        slot->signal = sig;
        slot->type = sig->type;
        slot->length = sig->length;
        slot->local->status = MAPPER_READY;
        slot->direction = DI_INCOMING;
        slot->num_instances = sig->num_instances;
        slot->id = rtr_sig->id_counter++;

        router_signal_store_slot(rtr_sig, &map->destination);

        for (i = 0; i < num_sources; i++) {
            slot = &map->sources[i];
            slot->id = i;
            if (local_sigs && local_sigs[i]) {
                remote_devname_ptr = mapper_device_name(rtr->device);

                // find router_signal
                rtr_sig2 = find_or_add_router_signal(rtr, local_sigs[i]);
                slot->local->router_sig = rtr_sig2;
                slot->signal = local_sigs[i];
                slot->type = local_sigs[i]->type;
                slot->length = local_sigs[i]->length;
                slot->local->status = MAPPER_READY;
                slot->direction = DI_OUTGOING;
                slot->num_instances = local_sigs[i]->num_instances;

                if (slot->num_instances > imap->num_var_instances)
                    imap->num_var_instances = slot->num_instances;

                // ensure local scope is only added once
                if (!local_scope) {
                    map->scope.names[scope_count] = strdup(remote_devname_ptr);
                    map->scope.hashes[scope_count] = crc32(0L,
                                                           (const Bytef *)remote_devname_ptr,
                                                           strlen(devname));
                    scope_count++;
                    local_scope = 1;
                }

                // also need to add map to lists kept by source rs
                router_signal_store_slot(rtr_sig2, &map->sources[i]);
            }
            else {
                ready = 0;
                devnamelen = mapper_parse_names(sig_names[i], &devnamep, &signame);
                if (!devnamelen || devnamelen >= 256) {
                    // TODO: free partially-built map structure
                    return 0;
                }
                strncpy(devname, devnamep, devnamelen);
                devname[devnamelen] = 0;
                remote_devname_ptr = devname;
                slot->signal = calloc(1, sizeof(mapper_signal_t));
                int signamelen = strlen(signame)+2;
                slot->signal->path = malloc(signamelen);
                snprintf(slot->signal->path, signamelen, "/%s", signame);
                slot->signal->name = slot->signal->path+1;
                slot->direction = DI_INCOMING;
                slot->num_instances = sig->num_instances;

                // TODO: check that scope is not added multiple times
                map->scope.names[scope_count] = strdup(devname);
                map->scope.hashes[scope_count] = crc32(0L, (const Bytef *)devname,
                                                       strlen(devname));
                scope_count++;
            }

            link = mapper_router_find_link_by_remote_name(rtr, remote_devname_ptr);
            if (link) {
                slot->local->link = link;
            }
            else
                slot->local->link = mapper_router_add_link(rtr, 0, 0, 0,
                                                           remote_devname_ptr);

            if (!slot->local->router_sig)
                slot->signal->device = slot->local->link->remote_device;
        }
        if (scope_count != num_sources) {
            map->scope.size = scope_count;
            map->scope.names = realloc(map->scope.names,
                                       sizeof(char *) * scope_count);
            map->scope.hashes = realloc(map->scope.hashes,
                                        sizeof(uint32_t) * scope_count);
        }
    }

    for (i = 0; i < num_sources; i++) {
        map->sources[i].map = map;
        map->sources[i].cause_update = 1;
        map->sources[i].send_as_instance = map->sources[i].num_instances > 1;
        map->sources[i].bound_min = BA_NONE;
        map->sources[i].bound_max = BA_NONE;
    }
    map->destination.map = map;
    map->destination.id = -1;
    map->destination.cause_update = 1;
    map->destination.send_as_instance = map->destination.num_instances > 1;
    map->destination.bound_min = map->destination.bound_max = BA_NONE;
    map->destination.minimum = map->destination.maximum = 0;

    map->muted = 0;
    map->mode = MO_UNDEFINED;
    map->expression = 0;
    map->extra = table_new();
    map->updater = table_new();

    // check if all sources belong to same remote device
    imap->one_source = 1;
    for (i = 1; i < map->num_sources; i++) {
        if (map->sources[i].local->link != map->sources[0].local->link) {
            imap->one_source = 0;
            break;
        }
    }
    // default to processing at source device unless heterogeneous sources
    if (imap->one_source)
        map->process_location = LOC_SOURCE;
    else
        map->process_location = LOC_DESTINATION;

    if (ready) {
        // all reference signals are local
        imap->is_local = 1;
        imap->is_admin = 1;
        link = map->sources[0].local->link;
        map->destination.local->link = link;
    }
    return map;
}

static void check_link(mapper_router rtr, mapper_link link)
{
    /* We could remove link the link here if it has no associated maps,
     * however under normal usage it is likely that users will add new
     * maps after deleting the old ones. If we remove the link
     * immediately it will have to be re-established in this scenario, so we
     * will allow the admin house-keeping routines to clean up empty links
     * after the link ping timeout period. */
}

void mapper_router_remove_signal(mapper_router rtr, mapper_router_signal rs)
{
    if (rtr && rs) {
        // No maps remaining – we can remove the router_signal also
        mapper_router_signal *rstemp = &rtr->signals;
        while (*rstemp) {
            if (*rstemp == rs) {
                *rstemp = rs->next;
                free(rs->slots);
                free(rs);
                break;
            }
            rstemp = &(*rstemp)->next;
        }
    }
}

static void free_slot_memory(mapper_slot slot)
{
    int i;
    if (slot->minimum)
        free(slot->minimum);
    if (slot->maximum)
        free(slot->maximum);
    if (slot->local && !slot->local->router_sig) {
        if (slot->signal) {
            if (slot->signal->path)
                free((void*)slot->signal->path);
            free(slot->signal);
        }
        if (slot->local->history) {
            for (i = 0; i < slot->num_instances; i++) {
                free(slot->local->history[i].value);
                free(slot->local->history[i].timetag);
            }
            free(slot->local->history);
        }
    }
}

int mapper_router_remove_map(mapper_router rtr, mapper_map map)
{
    // do not free local names since they point to signal's copy
    int i, j;
    if (!map)
        return 1;

    // remove map and slots from router_signal lists if necessary
    if (map->destination.local->router_sig) {
        mapper_router_signal rs = map->destination.local->router_sig;
        for (i = 0; i < rs->num_slots; i++) {
            if (rs->slots[i] == &map->destination) {
                rs->slots[i] = 0;
                break;
            }
        }
        if (map->local->status >= MAPPER_READY && map->destination.local->link) {
            map->destination.local->link->num_incoming_maps--;
            check_link(rtr, map->destination.local->link);
        }
    }
    else if (map->local->status >= MAPPER_READY && map->destination.local->link) {
        map->destination.local->link->num_outgoing_maps--;
        check_link(rtr, map->destination.local->link);
    }
    free_slot_memory(&map->destination);
    for (i = 0; i < map->num_sources; i++) {
        if (map->sources[i].local->router_sig) {
            mapper_router_signal rs = map->sources[i].local->router_sig;
            for (j = 0; j < rs->num_slots; j++) {
                if (rs->slots[j] == &map->sources[i]) {
                    rs->slots[j] = 0;
                }
            }
        }
        if (map->local->status >= MAPPER_READY && map->sources[i].local->link) {
            map->sources[i].local->link->num_incoming_maps--;
            check_link(rtr, map->sources[i].local->link);
        }
        free_slot_memory(&map->sources[i]);
    }
    free(map->sources);

    // free buffers associated with user-defined expression variables
    if (map->local->num_expr_vars) {
        for (i = 0; i < map->local->num_var_instances; i++) {
            if (map->local->num_expr_vars) {
                for (j = 0; j < map->local->num_expr_vars; j++) {
                    free(map->local->expr_vars[i][j].value);
                    free(map->local->expr_vars[i][j].timetag);
                }
            }
            free(map->local->expr_vars[i]);
        }
    }
    if (map->local->expr_vars)
        free(map->local->expr_vars);
    if (map->local->expr)
        mapper_expr_free(map->local->expr);

    if (map->expression)
        free(map->expression);
    for (i=0; i<map->scope.size; i++) {
        free(map->scope.names[i]);
    }
    free(map->scope.names);
    free(map->scope.hashes);
    table_free(map->extra);
    table_free(map->updater);

    free(map);
    return 0;
}

static int match_slot(mapper_device dev, mapper_slot slot, const char *full_name)
{
    if (!full_name)
        return 1;
    full_name += (full_name[0]=='/');
    const char *sig_name = strchr(full_name+1, '/');
    if (!sig_name)
        return 1;
    int len = sig_name - full_name;
    const char *slot_devname = (slot->local->router_sig ? mapper_device_name(dev)
                                : slot->local->link->remote_device->name);

    // first compare device name
    if (strlen(slot_devname) != len || strncmp(full_name, slot_devname, len))
        return 1;

    if (strcmp(sig_name+1, slot->signal->name) == 0)
        return 0;
    return 1;
}

mapper_map mapper_router_find_outgoing_map(mapper_router rtr,
                                           mapper_signal local_src,
                                           int num_sources,
                                           const char **src_names,
                                           const char *dest_name)
{
    // find associated router_signal
    mapper_router_signal rs = rtr->signals;
    while (rs && rs->signal != local_src)
        rs = rs->next;
    if (!rs)
        return 0;

    // find associated map
    int i, j;
    for (i = 0; i < rs->num_slots; i++) {
        if (!rs->slots[i] || rs->slots[i]->direction == DI_INCOMING)
            continue;
        mapper_slot slot = rs->slots[i];
        mapper_map map = slot->map;

        // check destination
        if (match_slot(rtr->device, &map->destination, dest_name))
            continue;

        // check sources
        int found = 1;
        for (j = 0; j < map->num_sources; j++) {
            if (map->sources[j].local->router_sig == rs)
                continue;

            if (match_slot(rtr->device, &map->sources[j], src_names[j])) {
                found = 0;
                break;
            }
        }
        if (found)
            return map;
    }
    return 0;
}

mapper_map mapper_router_find_incoming_map(mapper_router rtr,
                                           mapper_signal local_dest,
                                           int num_sources,
                                           const char **src_names)
{
    // find associated router_signal
    mapper_router_signal rs = rtr->signals;
    while (rs && rs->signal != local_dest)
        rs = rs->next;
    if (!rs)
        return 0;

    // find associated map
    int i, j;
    for (i = 0; i < rs->num_slots; i++) {
        if (!rs->slots[i] || rs->slots[i]->direction == DI_OUTGOING)
            continue;
        mapper_map map = rs->slots[i]->map;

        // check sources
        int found = 1;
        for (j = 0; j < num_sources; j++) {
            if (match_slot(rtr->device, &map->sources[j], src_names[j])) {
                found = 0;
                break;
            }
        }
        if (found)
            return map;
    }
    return 0;
}

mapper_map mapper_router_find_incoming_map_by_id(mapper_router rtr,
                                                 mapper_signal local_dest,
                                                 uint64_t id)
{
    mapper_router_signal rs = rtr->signals;
    while (rs && rs->signal != local_dest)
        rs = rs->next;
    if (!rs)
        return 0;

    int i;
    for (i = 0; i < rs->num_slots; i++) {
        if (!rs->slots[i] || rs->slots[i]->direction == DI_OUTGOING)
            continue;
        if (rs->slots[i]->map->id == id)
            return rs->slots[i]->map;
    }
    return 0;
}

mapper_map mapper_router_find_outgoing_map_by_id(mapper_router router,
                                                 mapper_signal local_src,
                                                 uint64_t id)
{
    int i;
    mapper_router_signal rs = router->signals;
    while (rs && rs->signal != local_src)
        rs = rs->next;
    if (!rs)
        return 0;

    for (i = 0; i < rs->num_slots; i++) {
        if (!rs->slots[i] || rs->slots[i]->direction == DI_INCOMING)
            continue;
        if (rs->slots[i]->map->id == id)
            return rs->slots[i]->map;
    }
    return 0;
}

mapper_slot mapper_router_find_slot(mapper_router router, mapper_signal signal,
                                    int slot_id)
{
    // only interested in incoming slots
    mapper_router_signal rs = router->signals;
    while (rs && rs->signal != signal)
        rs = rs->next;
    if (!rs)
        return NULL; // no associated router_signal

    int i, j;
    mapper_map map;
    for (i = 0; i < rs->num_slots; i++) {
        if (!rs->slots[i] || rs->slots[i]->direction == DI_OUTGOING)
            continue;
        map = rs->slots[i]->map;
        // check incoming slots for this map
        for (j = 0; j < map->num_sources; j++) {
            if (map->sources[j].id == slot_id)
                return &map->sources[j];
        }
    }
    return NULL;
}

mapper_link mapper_router_find_link_by_remote_address(mapper_router rtr,
                                                      const char *host,
                                                      int port)
{
    mapper_link link = rtr->links;
    while (link) {
        if (link->remote_device->port == port
            && (strcmp(link->remote_device->host, host)==0))
            return link;
        link = link->next;
    }
    return 0;
}

mapper_link mapper_router_find_link_by_remote_name(mapper_router router,
                                                   const char *name)
{
    int n = strlen(name);
    const char *slash = strchr(name+1, '/');
    if (slash)
        n = slash - name;

    mapper_link link = router->links;
    while (link) {
        if (strncmp(link->remote_device->name, name, n)==0
            && link->remote_device->name[n]==0)
            return link;
        link = link->next;
    }
    return 0;
}

mapper_link mapper_router_find_link_by_remote_id(mapper_router router,
                                                 uint32_t id)
{
    mapper_link link = router->links;
    while (link) {
        if (id == link->remote_device->id)
            return link;
        link = link->next;
    }
    return 0;
}
