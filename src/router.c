#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <zlib.h>

#include <lo/lo.h>

#include "mapper_internal.h"
#include "types_internal.h"
#include <mapper/mapper.h>

static void send_or_bundle_message(mapper_link link, const char *path,
                                   lo_message m, mapper_timetag_t tt);

static int get_in_scope(mapper_map map, uint32_t hash)
{
    int i;
    for (i = 0; i < map->props.scope.size; i++) {
        if (map->props.scope.hashes[i] == hash ||
            map->props.scope.hashes[i] == 0)
            return 1;
    }
    return 0;
}

static void mapper_link_free(mapper_link l)
{
    if (l) {
        if (l->props.host)
            free(l->props.host);
        if (l->props.name)
            free(l->props.name);
        if (l->admin_addr)
            lo_address_free(l->admin_addr);
        if (l->data_addr)
            lo_address_free(l->data_addr);
        while (l->queues) {
            mapper_queue q = l->queues;
            lo_bundle_free_messages(q->bundle);
            l->queues = q->next;
            free(q);
        }
        free(l);
    }
}

mapper_link mapper_router_add_link(mapper_router router, const char *host,
                                   int admin_port, int data_port,
                                   const char *name)
{
    if (!name)
        return 0;

    char str[16];
    mapper_link l = (mapper_link) calloc(1, sizeof(struct _mapper_link));
    if (host) {
        l->props.host = strdup(host);
        l->props.port = data_port;
        snprintf(str, 16, "%d", data_port);
        l->data_addr = lo_address_new(host, str);
        snprintf(str, 16, "%d", admin_port);
        l->admin_addr = lo_address_new(host, str);
    }
    name += (name[0]=='/');
    l->props.name = strdup(name);
    l->props.hash = crc32(0L, (const Bytef *)name, strlen(name));

    l->device = router->device;
    l->props.num_incoming_maps = 0;
    l->props.num_outgoing_maps = 0;

    if (name == mdev_name(router->device)) {
        /* Add data_addr for use by self-connections. In the future we may
         * decide to call local handlers directly, however this could result in
         * unfortunate loops/stack overflow. Sending data for self-connections
         * to localhost adds the messages to liblo's stack and imposes a delay
         * since the receiving handler will not be called until mdev_poll(). */
        snprintf(str, 16, "%d", router->device->props.port);
        l->data_addr = lo_address_new("localhost", str);
        l->self_link = 1;
    }

    l->clock.new = 1;
    l->clock.sent.message_id = 0;
    l->clock.response.message_id = -1;
    mapper_clock_t *clock = &router->device->admin->clock;
    mapper_clock_now(clock, &clock->now);
    l->clock.response.timetag.sec = clock->now.sec + 10;

    l->next = router->links;
    router->links = l;

    if (!host) {
        // request missing metadata
        char cmd[256];
        snprintf(cmd, 256, "/%s/subscribe", name);
        lo_message m = lo_message_new();
        if (m) {
            lo_message_add_string(m, "device");
            mapper_admin_set_bundle_dest_bus(router->device->admin);
            lo_bundle_add_message(router->device->admin->bundle, cmd, m);
            mapper_admin_send_bundle(router->device->admin);
        }
    }

    return l;
}

void mapper_router_update_link(mapper_router router, mapper_link link,
                               const char *host, int admin_port, int data_port)
{
    char str[16];
    link->props.host = strdup(host);
    link->props.port = data_port;
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
            if (map->destination.link == link) {
                mapper_router_remove_map(router, map);
                continue;
            }
            for (j = 0; j < map->props.num_sources; j++) {
                if (map->sources[j].link == link) {
                    mapper_router_remove_map(router, map);
                    break;
                }
            }
        }
        rs = rs->next;
    }
    mapper_link *l = &router->links;
    while (*l) {
        if (*l == link) {
            *l = (*l)->next;
            break;
        }
        l = &(*l)->next;
    }
    mapper_link_free(link);
}

static void reallocate_slot_instances(mapper_map_slot slot, int size)
{
    int i;
    if (slot->props->num_instances < size) {
        slot->history = realloc(slot->history, sizeof(struct _mapper_history) * size);
        for (i = slot->props->num_instances; i < size; i++) {
            slot->history[i].type = slot->props->type;
            slot->history[i].length = slot->props->length;
            slot->history[i].size = slot->history_size;
            slot->history[i].value = calloc(1, mapper_type_size(slot->props->type)
                                            * slot->history_size);
            slot->history[i].timetag = calloc(1, sizeof(mapper_timetag_t)
                                              * slot->history_size);
            slot->history[i].position = -1;
        }
        slot->props->num_instances = size;
    }
}

static void reallocate_map_instances(mapper_map map, int size)
{
    int i, j;
    if (!(map->status & MAPPER_TYPE_KNOWN) || !(map->status & MAPPER_LENGTH_KNOWN)) {
        for (i = 0; i < map->props.num_sources; i++) {
            map->sources[i].props->num_instances = size;
        }
        map->destination.props->num_instances = size;
        return;
    }

    // check if source histories need to be reallocated
    for (i = 0; i < map->props.num_sources; i++)
        reallocate_slot_instances(&map->sources[i], size);

    // check if destination histories need to be reallocated
    reallocate_slot_instances(&map->destination, size);

    // check if expression variable histories need to be reallocated
    if (size > map->num_var_instances) {
        map->expr_vars = realloc(map->expr_vars, sizeof(mapper_history*) * size);
        for (i = map->num_var_instances; i < size; i++) {
            map->expr_vars[i] = malloc(sizeof(struct _mapper_history)
                                       * map->num_expr_vars);
            for (j = 0; j < map->num_expr_vars; j++) {
                map->expr_vars[i][j].type = map->expr_vars[0][j].type;
                map->expr_vars[i][j].length = map->expr_vars[0][j].length;
                map->expr_vars[i][j].size = map->expr_vars[0][j].size;
                map->expr_vars[i][j].position = -1;
                map->expr_vars[i][j].value = calloc(1, sizeof(double)
                                                    * map->expr_vars[i][j].length
                                                    * map->expr_vars[i][j].size);
                map->expr_vars[i][j].timetag = calloc(1, sizeof(mapper_timetag_t)
                                                      * map->expr_vars[i][j].size);
            }
        }
        map->num_var_instances = size;
    }
}

// TODO: check for mismatched instance counts when using multiple sources
void mapper_router_num_instances_changed(mapper_router r,
                                         mapper_signal sig,
                                         int size)
{
    int i;
    // check if we have a reference to this signal
    mapper_router_signal rs = r->signals;
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
        mapper_map_slot slot = rs->slots[i];
        if (slot)
            reallocate_map_instances(slot->map, size);
    }
}

void mapper_router_process_signal(mapper_router r, mapper_signal sig,
                                  int instance, void *value, int count,
                                  mapper_timetag_t tt)
{
    mapper_id_map id_map = sig->id_maps[instance].map;
    lo_message m;

    // find the router signal
    mapper_router_signal rs = r->signals;
    while (rs) {
        if (rs->signal == sig)
            break;
        rs = rs->next;
    }
    if (!rs)
        return;

    int i, j, k, id = sig->id_maps[instance].instance->index;
    mapper_map map;

    if (!value) {
        mapper_map_slot s;
        mapper_db_map_slot p;

        for (i = 0; i < rs->num_slots; i++) {
            if (!rs->slots[i])
                continue;

            s = rs->slots[i];
            map = s->map;

            if (map->status < MAPPER_ACTIVE)
                continue;

            if (s->props->direction == DI_OUTGOING) {
                mapper_map_slot ds = &map->destination;
                mapper_db_map_slot dp = &map->props.destination;

                // also need to reset associated output memory
                ds->history[id].position= -1;
                memset(ds->history[id].value, 0, ds->history_size
                       * dp->length * mapper_type_size(dp->type));
                memset(ds->history[id].timetag, 0, ds->history_size
                       * sizeof(mapper_timetag_t));

                if (!s->props->send_as_instance)
                    m = mapper_map_build_message(map, s, 0, 1, 0, 0);
                else if (get_in_scope(map, id_map->origin))
                    m = mapper_map_build_message(map, s, 0, 1, 0, id_map);
                if (m)
                    send_or_bundle_message(map->destination.link,
                                           dp->signal->path, m, tt);
                continue;
            }
            else if (!get_in_scope(map, id_map->origin))
                continue;
            for (j = 0; j < map->props.num_sources; j++) {
                if (!map->sources[j].props->send_as_instance)
                    continue;
                s = &map->sources[j];
                p = &map->props.sources[j];
                // send release to upstream
                m = mapper_map_build_message(map, s, 0, 1, 0, id_map);
                if (m)
                    send_or_bundle_message(s->link, p->signal->path, m, tt);

                // also need to reset associated input memory
                memset(s->history[id].value, 0, s->history_size
                       * p->length * mapper_type_size(p->type));
                memset(s->history[id].timetag, 0, s->history_size
                       * sizeof(mapper_timetag_t));
            }
        }
        return;
    }

    // if count > 1, we need to allocate sufficient memory for largest output
    // vector so that we can store calculated values before sending
    // TODO: calculate max_output_size, cache in link_signal
    void *out_value_p = count == 1 ? 0 : alloca(count * sig->props.length
                                                * sizeof(double));
    for (i = 0; i < rs->num_slots; i++) {
        if (!rs->slots[i])
            continue;

        mapper_map_slot s = rs->slots[i];
        map = s->map;

        if (map->status < MAPPER_ACTIVE)
            continue;

        int in_scope = get_in_scope(map, id_map->origin);
        // TODO: should we continue for out-of-scope local destinaton updates?
        if (s->props->send_as_instance && !in_scope) {
            continue;
        }

        mapper_db_map_slot sp = s->props;
        mapper_db_map_slot dp = &map->props.destination;
        mapper_db_map_slot to = (map->props.process_location == MAPPER_SOURCE ? dp : sp);
        int to_size = mapper_type_size(to->type) * to->length;
        char typestring[to->length * count];
        memset(typestring, to->type, to->length * count);
        k = 0;
        for (j = 0; j < count; j++) {
            // copy input history
            size_t n = msig_vector_bytes(sig);
            s->history[id].position = ((s->history[id].position + 1)
                                       % s->history[id].size);
            memcpy(mapper_history_value_ptr(s->history[id]), value + n * j, n);
            memcpy(mapper_history_tt_ptr(s->history[id]),
                   &tt, sizeof(mapper_timetag_t));

            // process source boundary behaviour
            if ((mapper_boundary_perform(&s->history[id], sp,
                                         typestring + to->length * k))) {
                // back up position index
                --s->history[id].position;
                if (s->history[id].position < 0)
                    s->history[id].position = s->history[id].size - 1;
                continue;
            }

            if (sp->direction == DI_INCOMING) {
                continue;
            }

            if (map->props.process_location == MAPPER_SOURCE && !sp->cause_update)
                continue;

            if (!(mapper_map_perform(map, s, instance, typestring + to->length * k)))
                continue;

            if (map->props.process_location == MAPPER_SOURCE) {
                // also process destination boundary behaviour
                if ((mapper_boundary_perform(&map->destination.history[id], dp,
                                             typestring + to->length * k))) {
                    // back up position index
                    --map->destination.history[id].position;
                    if (map->destination.history[id].position < 0)
                        map->destination.history[id].position = map->destination.history[id].size - 1;
                    continue;
                }
            }

            void *result = mapper_history_value_ptr(map->destination.history[id]);

            if (count > 1) {
                memcpy((char*)out_value_p + to_size * j, result, to_size);
            }
            else {
                m = mapper_map_build_message(map, s, result, 1, typestring,
                                             sp->send_as_instance ? id_map : 0);
                if (m)
                    send_or_bundle_message(map->destination.link,
                                           dp->signal->path, m, tt);
            }
            k++;
        }
        if (count > 1 && s->props->direction == DI_OUTGOING
            && (!s->props->send_as_instance || in_scope)) {
            m = mapper_map_build_message(map, s, out_value_p, k, typestring,
                                         sp->send_as_instance ? id_map : 0);
            if (m)
                send_or_bundle_message(map->destination.link, dp->signal->path, m, tt);
        }
    }
}

int mapper_router_send_query(mapper_router r,
                             mapper_signal sig,
                             mapper_timetag_t tt)
{
    // TODO: cache the response string
    if (!sig->handler) {
        trace("not sending queries since signal has no handler.\n");
        return 0;
    }
    // find the corresponding router_signal
    mapper_router_signal rs = r->signals;
    while (rs && rs->signal != sig)
        rs = rs->next;

    // exit without failure if signal is not mapped
    if (!rs)
        return 0;

    // for each map, query remote signals
    int i, j, count = 0;
    int len = (int) strlen(sig->props.path) + 5;
    char *response_string = (char*) malloc(len);
    snprintf(response_string, len, "%s/got", sig->props.path);
    char query_string[256];
    for (i = 0; i < rs->num_slots; i++) {
        if (!rs->slots[i])
            continue;
        mapper_map map = rs->slots[i]->map;
        if (map->status != MAPPER_ACTIVE)
            continue;
        lo_message m = lo_message_new();
        if (!m)
            continue;
        lo_message_add_string(m, response_string);
        lo_message_add_int32(m, sig->props.length);
        lo_message_add_char(m, sig->props.type);
        // TODO: include response address as argument to allow TCP queries?
        // TODO: always use TCP for queries?

        if (rs->slots[i]->props->direction == DI_OUTGOING) {
            snprintf(query_string, 256, "%s/get",
                     map->props.destination.signal->path);
            send_or_bundle_message(map->destination.link, query_string, m, tt);
        }
        else {
            for (j = 0; j < map->props.num_sources; j++) {
                snprintf(query_string, 256, "%s/get", map->props.sources[j].signal->path);
                send_or_bundle_message(map->sources[j].link, query_string, m, tt);
            }
        }
        count++;
    }
    free(response_string);
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
        lo_send_bundle_from(link->data_addr, link->device->server, b);
        lo_bundle_free_messages(b);
    }
}

void mapper_router_start_queue(mapper_router r,
                               mapper_timetag_t tt)
{
    // first check if queue already exists
    mapper_link l = r->links;
    while (l) {
        mapper_queue q = l->queues;
        while (q) {
            if (memcmp(&q->tt, &tt,
                       sizeof(mapper_timetag_t))==0)
                return;
            q = q->next;
        }

        // need to create new queue
        q = malloc(sizeof(struct _mapper_queue));
        memcpy(&q->tt, &tt, sizeof(mapper_timetag_t));
        q->bundle = lo_bundle_new(tt);
        q->next = l->queues;
        l->queues = q;
        l = l->next;
    }
}

void mapper_router_send_queue(mapper_router r,
                              mapper_timetag_t tt)
{
    mapper_link l = r->links;
    while (l) {
        mapper_queue *q = &l->queues;
        while (*q) {
            if (memcmp(&(*q)->tt, &tt, sizeof(mapper_timetag_t))==0)
                break;
            q = &(*q)->next;
        }
        if (*q) {
#ifdef HAVE_LIBLO_BUNDLE_COUNT
            if (lo_bundle_count((*q)->bundle))
#endif
                lo_send_bundle_from(l->data_addr,
                                    l->device->server, (*q)->bundle);
            lo_bundle_free_messages((*q)->bundle);
            mapper_queue temp = *q;
            *q = (*q)->next;
            free(temp);
        }
        l = l->next;
    }
}

static mapper_router_signal find_or_add_router_signal(mapper_router r,
                                                      mapper_signal sig)
{
    // find signal in router_signal list
    mapper_router_signal rs = r->signals;
    while (rs && rs->signal != sig)
        rs = rs->next;

    // if not found, create a new list entry
    if (!rs) {
        rs = ((mapper_router_signal)
              calloc(1, sizeof(struct _mapper_router_signal)));
        rs->signal = sig;
        rs->num_slots = 1;
        rs->slots = malloc(sizeof(mapper_map_slot *));
        rs->slots[0] = 0;
        rs->next = r->signals;
        r->signals = rs;
    }
    return rs;
}

static int router_signal_store_slot(mapper_router_signal rs,
                                    mapper_map_slot slot)
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
    rs->slots = realloc(rs->slots, sizeof(mapper_map_slot*) * rs->num_slots * 2);
    rs->slots[rs->num_slots] = slot;
    for (i = rs->num_slots+1; i < rs->num_slots * 2; i++) {
        rs->slots[i] = 0;
    }
    i = rs->num_slots;
    rs->num_slots *= 2;
    return i;
}

mapper_map mapper_router_add_map(mapper_router r, mapper_signal sig,
                                 int num_sources, mapper_signal *local_signals,
                                 const char **remote_signal_names,
                                 mapper_direction_t direction)
{
    int i, ready = 1;

    if (num_sources > MAX_NUM_MAP_SOURCES) {
        trace("error: maximum number of remote signals in a map exceeded.\n");
        return 0;
    }

    mapper_router_signal rs = find_or_add_router_signal(r, sig);
    mapper_map map = (mapper_map) calloc(1, sizeof(struct _mapper_map));
    map->router = r;
    map->status = 0;

    // TODO: configure number of instances available for each slot
    map->num_var_instances = sig->props.num_instances;

    map->props.num_sources = num_sources;
    map->sources = (mapper_map_slot) calloc(1, sizeof(struct _mapper_map_slot)
                                            * num_sources);
    map->props.sources = ((mapper_db_map_slot)
                          calloc(1, sizeof(struct _mapper_db_map_slot)
                                 * num_sources));
    // scopes
    map->props.scope.size = (direction == DI_OUTGOING) ? 1 : num_sources;
    map->props.scope.names = (char **) malloc(sizeof(char *)
                                              * map->props.scope.size);
    map->props.scope.hashes = (uint32_t *) malloc(sizeof(uint32_t)
                                                  * map->props.scope.size);

    // is_admin property will be corrected later if necessary
    map->is_admin = 1;
    map->props.id = direction == DI_INCOMING ? r->id_counter++ : -1;

    mapper_link link;
    char devname[256], *devnamep, *signame;
    const char *remote_devname_ptr;
    int devnamelen, scope_count = 0, local_scope = 0;
    mapper_db_map_slot sp;
    if (direction == DI_OUTGOING) {
        ready = 0;
        for (i = 0; i < num_sources; i++) {
            // find router_signal
            mapper_router_signal rs2 = find_or_add_router_signal(r, local_signals[i]);
            map->sources[i].local = rs2;
            sp = map->sources[i].props = &map->props.sources[i];
            sp->signal = &local_signals[i]->props;
            sp->type = local_signals[i]->props.type;
            sp->length = local_signals[i]->props.length;
            map->sources[i].status = MAPPER_READY;
            sp->direction = DI_OUTGOING;
            sp->num_instances = local_signals[i]->props.num_instances;
            if (sp->num_instances > map->num_var_instances)
                map->num_var_instances = sp->num_instances;

            /* slot index will be overwritten if necessary by
             * mapper_map_set_from_message() */
            sp->slot_id = -1;

            // also need to add map to lists kept by source rs
            router_signal_store_slot(rs2, &map->sources[i]);
        }

        devnamelen = mapper_parse_names(remote_signal_names[0],
                                        &devnamep, &signame);
        if (!devnamelen || devnamelen >= 256) {
            // TODO: free partially-built map structure
            return 0;
        }
        strncpy(devname, devnamep, devnamelen);
        devname[devnamelen] = 0;
        sp = map->destination.props = &map->props.destination;
        sp->signal = calloc(1, sizeof(mapper_db_signal_t));
        int signamelen = strlen(signame)+2;
        sp->signal->path = malloc(signamelen);
        snprintf(sp->signal->path, signamelen, "/%s", signame);
        sp->signal->name = sp->signal->path+1;
        sp->direction = DI_OUTGOING;
        sp->num_instances = sig->props.num_instances;

        link = mapper_router_find_link_by_remote_name(r, devname);
        if (link) {
            map->destination.link = link;
        }
        else
            map->destination.link = mapper_router_add_link(r, 0, 0, 0, devname);
        sp->signal->device = &map->destination.link->props;

        // apply local scope as default
        map->props.scope.names[0] = strdup(mdev_name(r->device));
        map->props.scope.hashes[0] = mdev_hash(r->device);
    }
    else {
        map->destination.local = rs;
        sp = map->destination.props = &map->props.destination;
        sp->signal = &sig->props;
        sp->type = sig->props.type;
        sp->length = sig->props.length;
        map->destination.status = MAPPER_READY;
        sp->direction = DI_INCOMING;
        sp->num_instances = sig->props.num_instances;

        router_signal_store_slot(rs, &map->destination);

        for (i = 0; i < num_sources; i++) {
            sp = map->sources[i].props = &map->props.sources[i];
            if (local_signals && local_signals[i]) {
                remote_devname_ptr = mdev_name(r->device);

                // find router_signal
                mapper_router_signal rs2 = find_or_add_router_signal(r, local_signals[i]);
                map->sources[i].local = rs2;
                sp->signal = &local_signals[i]->props;
                sp->type = local_signals[i]->props.type;
                sp->length = local_signals[i]->props.length;
                map->sources[i].status = MAPPER_READY;
                sp->direction = DI_OUTGOING;
                sp->num_instances = local_signals[i]->props.num_instances;

                if (sp->num_instances > map->num_var_instances)
                    map->num_var_instances = sp->num_instances;

                // ensure local scope is only added once
                if (!local_scope) {
                    map->props.scope.names[scope_count] = strdup(remote_devname_ptr);
                    map->props.scope.hashes[scope_count] = crc32(0L,
                                                                 (const Bytef *)remote_devname_ptr,
                                                                 strlen(devname));
                    scope_count++;
                    local_scope = 1;
                }

                // also need to add map to lists kept by source rs
                router_signal_store_slot(rs2, &map->sources[i]);
            }
            else {
                ready = 0;
                devnamelen = mapper_parse_names(remote_signal_names[i],
                                                &devnamep, &signame);
                if (!devnamelen || devnamelen >= 256) {
                    // TODO: free partially-built map structure
                    return 0;
                }
                strncpy(devname, devnamep, devnamelen);
                devname[devnamelen] = 0;
                remote_devname_ptr = devname;
                sp->signal = calloc(1, sizeof(mapper_db_signal_t));
                int signamelen = strlen(signame)+2;
                sp->signal->path = malloc(signamelen);
                snprintf(sp->signal->path, signamelen, "/%s", signame);
                sp->signal->name = sp->signal->path+1;
                sp->direction = DI_INCOMING;
                sp->num_instances = sig->props.num_instances;

                // TODO: check that scope is not added multiple times
                map->props.scope.names[scope_count] = strdup(devname);
                map->props.scope.hashes[scope_count] = crc32(0L,
                                                             (const Bytef *)devname,
                                                             strlen(devname));
                scope_count++;
            }

            link = mapper_router_find_link_by_remote_name(r, remote_devname_ptr);
            if (link) {
                map->sources[i].link = link;
            }
            else
                map->sources[i].link = mapper_router_add_link(r, 0, 0, 0,
                                                              remote_devname_ptr);

            if (!map->sources[i].local)
                sp->signal->device = &map->sources[i].link->props;
            sp->slot_id = rs->id_counter++;
        }
        if (scope_count != num_sources) {
            map->props.scope.size = scope_count;
            map->props.scope.names = realloc(map->props.scope.names,
                                             sizeof(char *) * scope_count);
            map->props.scope.hashes = realloc(map->props.scope.hashes,
                                              sizeof(uint32_t) * scope_count);
        }
    }

    for (i = 0; i < num_sources; i++) {
        map->sources[i].map = map;
        map->props.sources[i].cause_update = 1;
        map->props.sources[i].send_as_instance = map->props.sources[i].num_instances > 1;
        map->props.sources[i].bound_min = BA_NONE;
        map->props.sources[i].bound_max = BA_NONE;
    }
    map->destination.map = map;
    map->props.destination.slot_id = -1;
    map->props.destination.cause_update = 1;
    map->props.destination.send_as_instance = map->props.destination.num_instances > 1;
    map->props.destination.bound_min = map->props.destination.bound_max = BA_NONE;
    map->props.destination.minimum = map->props.destination.maximum = 0;

    map->props.muted = 0;
    map->props.mode = MO_UNDEFINED;
    map->props.expression = 0;
    map->props.extra = table_new();

    // check if all sources belong to same remote device
    map->one_source = 1;
    for (i = 1; i < map->props.num_sources; i++) {
        if (map->sources[i].link != map->sources[0].link) {
            map->one_source = 0;
            break;
        }
    }
    // default to processing at source device unless heterogeneous sources
    if (map->one_source)
        map->props.process_location = MAPPER_SOURCE;
    else
        map->props.process_location = MAPPER_DESTINATION;

    if (ready) {
        // all reference signals are local
        map->is_local = 1;
        map->is_admin = 1;
        link = map->sources[0].link;
        map->destination.link = link;
    }
    return map;
}

static void check_link(mapper_router r, mapper_link l)
{
    /* We could remove link the link here if it has no associated maps,
     * however under normal usage it is likely that users will add new
     * maps after deleting the old ones. If we remove the link
     * immediately it will have to be re-established in this scenario, so we
     * will allow the admin house-keeping routines to clean up empty links
     * after the link ping timeout period. */
}

void mapper_router_remove_signal(mapper_router r, mapper_router_signal rs)
{
    if (r && rs) {
        // No maps remaining – we can remove the router_signal also
        mapper_router_signal *rstemp = &r->signals;
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

static void free_slot_memory(mapper_map_slot s)
{
    int i;
    if (s->props->minimum)
        free(s->props->minimum);
    if (s->props->maximum)
        free(s->props->maximum);
    if (!s->local) {
        if (s->props->signal) {
            if (s->props->signal->path)
                free((void*)s->props->signal->path);
            free(s->props->signal);
        }
        if (s->history) {
            for (i = 0; i < s->props->num_instances; i++) {
                free(s->history[i].value);
                free(s->history[i].timetag);
            }
            free(s->history);
        }
    }
}

int mapper_router_remove_map(mapper_router r, mapper_map map)
{
    // do not free local names since they point to signal's copy
    int i, j;
    if (!map)
        return 1;

    // remove map and slots from router_signal lists if necessary
    if (map->destination.local) {
        mapper_router_signal rs = map->destination.local;
        for (i = 0; i < rs->num_slots; i++) {
            if (rs->slots[i] == &map->destination) {
                rs->slots[i] = 0;
                break;
            }
        }
        if (map->status >= MAPPER_READY && map->destination.link) {
            map->destination.link->props.num_incoming_maps--;
            check_link(r, map->destination.link);
        }
    }
    else if (map->status >= MAPPER_READY && map->destination.link) {
        map->destination.link->props.num_outgoing_maps--;
        check_link(r, map->destination.link);
    }
    free_slot_memory(&map->destination);
    for (i = 0; i < map->props.num_sources; i++) {
        if (map->sources[i].local) {
            mapper_router_signal rs = map->sources[i].local;
            for (j = 0; j < rs->num_slots; j++) {
                if (rs->slots[j] == &map->sources[i]) {
                    rs->slots[j] = 0;
                }
            }
        }
        if (map->status >= MAPPER_READY && map->sources[i].link) {
            map->sources[i].link->props.num_incoming_maps--;
            check_link(r, map->sources[i].link);
        }
        free_slot_memory(&map->sources[i]);
    }
    free(map->sources);
    free(map->props.sources);

    // free buffers associated with user-defined expression variables
    if (map->num_expr_vars) {
        for (i = 0; i < map->num_var_instances; i++) {
            if (map->num_expr_vars) {
                for (j = 0; j < map->num_expr_vars; j++) {
                    free(map->expr_vars[i][j].value);
                    free(map->expr_vars[i][j].timetag);
                }
            }
            free(map->expr_vars[i]);
        }
    }
    if (map->expr_vars)
        free(map->expr_vars);
    if (map->expr)
        mapper_expr_free(map->expr);

    if (map->props.expression)
        free(map->props.expression);
    for (i=0; i<map->props.scope.size; i++) {
        free(map->props.scope.names[i]);
    }
    free(map->props.scope.names);
    free(map->props.scope.hashes);
    table_free(map->props.extra, 1);

    free(map);
    return 0;
}

static int match_slot(mapper_device md, mapper_map_slot slot,
                      const char *full_name)
{
    if (!full_name)
        return 1;
    full_name += (full_name[0]=='/');
    const char *sig_name = strchr(full_name+1, '/');
    if (!sig_name)
        return 1;
    int len = sig_name - full_name;
    const char *local_devname = slot->local ? mdev_name(md) : slot->link->props.name;

    // first compare device name
    if (strlen(local_devname) != len || strncmp(full_name, local_devname, len))
        return 1;

    if (strcmp(sig_name+1, slot->props->signal->name) == 0)
        return 0;
    return 1;
}

mapper_map mapper_router_find_outgoing_map(mapper_router router,
                                           mapper_signal local_src,
                                           int num_sources,
                                           const char **src_names,
                                           const char *dest_name)
{
    // find associated router_signal
    mapper_router_signal rs = router->signals;
    while (rs && rs->signal != local_src)
        rs = rs->next;
    if (!rs)
        return 0;

    // find associated map
    int i, j;
    for (i = 0; i < rs->num_slots; i++) {
        if (!rs->slots[i] || rs->slots[i]->props->direction == DI_INCOMING)
            continue;
        mapper_map_slot s = rs->slots[i];
        mapper_map map = s->map;

        // check destination
        if (match_slot(router->device, &map->destination, dest_name))
            continue;

        // check sources
        int found = 1;
        for (j = 0; j < map->props.num_sources; j++) {
            if (map->sources[j].local == rs)
                continue;

            if (match_slot(router->device, &map->sources[j], src_names[j])) {
                found = 0;
                break;
            }
        }
        if (found)
            return map;
    }
    return 0;
}

mapper_map mapper_router_find_incoming_map(mapper_router router,
                                           mapper_signal local_dest,
                                           int num_sources,
                                           const char **src_names)
{
    // find associated router_signal
    mapper_router_signal rs = router->signals;
    while (rs && rs->signal != local_dest)
        rs = rs->next;
    if (!rs)
        return 0;

    // find associated map
    int i, j;
    for (i = 0; i < rs->num_slots; i++) {
        if (!rs->slots[i] || rs->slots[i]->props->direction == DI_OUTGOING)
            continue;
        mapper_map map = rs->slots[i]->map;

        // check sources
        int found = 1;
        for (j = 0; j < num_sources; j++) {
            if (match_slot(router->device, &map->sources[j], src_names[j])) {
                found = 0;
                break;
            }
        }
        if (found)
            return map;
    }
    return 0;
}

mapper_map mapper_router_find_incoming_map_by_id(mapper_router router,
                                                 mapper_signal local_dest,
                                                 int id)
{
    mapper_router_signal rs = router->signals;
    while (rs && rs->signal != local_dest)
        rs = rs->next;
    if (!rs)
        return 0;

    int i;
    for (i = 0; i < rs->num_slots; i++) {
        if (!rs->slots[i] || rs->slots[i]->props->direction == DI_OUTGOING)
            continue;
        mapper_map map = rs->slots[i]->map;
        if (map->props.id == id)
            return map;
    }
    return 0;
}

mapper_map mapper_router_find_outgoing_map_by_id(mapper_router router,
                                                 mapper_signal local_src,
                                                 const char *dest_name,
                                                 int id)
{
    int i;
    mapper_router_signal rs = router->signals;
    while (rs && rs->signal != local_src)
        rs = rs->next;
    if (!rs)
        return 0;

    char *devname, *signame;
    int devnamelen = mapper_parse_names(dest_name, &devname, &signame);
    if (!devnamelen || devnamelen >= 256)
        return 0;
    for (i = 0; i < rs->num_slots; i++) {
        if (!rs->slots[i] || rs->slots[i]->props->direction == DI_INCOMING)
            continue;
        mapper_map map = rs->slots[i]->map;
        if (map->props.id != id)
            continue;
        const char *match = (map->destination.local
                             ? mdev_name(router->device)
                             : map->destination.link->props.name);
        if (strlen(match)==devnamelen
            && strncmp(match, devname, devnamelen)==0) {
            return map;
        }
    }
    return 0;
}

mapper_map_slot mapper_router_find_map_slot(mapper_router router,
                                            mapper_signal signal,
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
        if (!rs->slots[i] || rs->slots[i]->props->direction == DI_OUTGOING)
            continue;
        map = rs->slots[i]->map;
        // check incoming slots for this map
        for (j = 0; j < map->props.num_sources; j++) {
            if (map->sources[j].props->slot_id == slot_id)
                return &map->sources[j];
        }
    }
    return NULL;
}

mapper_link mapper_router_find_link_by_remote_address(mapper_router r,
                                                      const char *host,
                                                      int port)
{
    mapper_link l = r->links;
    while (l) {
        if (l->props.port == port && (strcmp(l->props.host, host)==0))
            return l;
        l = l->next;
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

    mapper_link l = router->links;
    while (l) {
        if (strncmp(l->props.name, name, n)==0 && l->props.name[n]==0)
            return l;
        l = l->next;
    }
    return 0;
}

mapper_link mapper_router_find_link_by_remote_hash(mapper_router router,
                                                   uint32_t hash)
{
    mapper_link l = router->links;
    while (l) {
        if (hash == l->props.hash)
            return l;
        l = l->next;
    }
    return 0;
}
