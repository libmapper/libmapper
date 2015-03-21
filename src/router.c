
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

static int get_in_scope(mapper_connection c, uint32_t name_hash)
{
    int i;
    for (i=0; i<c->props.scope.size; i++) {
        if (c->props.scope.hashes[i] == name_hash ||
            c->props.scope.hashes[i] == 0)
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
    l->props.name = strdup(name);
    l->props.name_hash = crc32(0L, (const Bytef *)name, strlen(name));

    l->device = router->device;
    l->props.num_connections_in = 0;
    l->props.num_connections_out = 0;

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

    l->next = router->links;
    router->links = l;

    if (!host) {
        // request missing metadata
        char cmd[256];
        snprintf(cmd, 256, "%s/subscribe", name);
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
    // check if any connection use this link
    mapper_router_signal rs = router->signals;
    while (rs) {
        for (i = 0; i < rs->num_incoming_connections; i++) {
            mapper_connection c = rs->incoming_connections[i];
            if (!c)
                continue;
            for (j = 0; j < c->props.num_sources; j++) {
                if (c->sources[j].link == link) {
                    // TODO: free connection
                }
            }
        }
        for (i = 0; i < rs->num_outgoing_slots; i++) {
            if (rs->outgoing_slots[i] && rs->outgoing_slots[i]->link == link) {
                // TODO: free connection
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

void mapper_router_num_instances_changed(mapper_router r,
                                         mapper_signal sig,
                                         int size)
{
    // TODO: check for mismatched instance counts when using multiple sources
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

    if (size <= rs->num_instances)
        return;

    // Need to allocate more instances for router signal
    rs->history = realloc(rs->history, sizeof(struct _mapper_history) * size);
    int i, j, k;
    for (i = rs->num_instances; i < size; i++) {
        rs->history[i].type = sig->props.type;
        rs->history[i].length = sig->props.length;
        rs->history[i].size = rs->history_size;
        rs->history[i].value = calloc(1, msig_vector_bytes(sig)
                                      * rs->history_size);
        rs->history[i].timetag = calloc(1, sizeof(mapper_timetag_t)
                                        * rs->history_size);
        rs->history[i].position = -1;
    }
    rs->num_instances = size;

    // for array of incoming connections, may need to reallocate source instances
    for (i = 0; i < rs->num_incoming_connections; i++) {
        mapper_connection c = rs->incoming_connections[i];
        if (!c)
            continue;

        // history memory may have been moved by realloc()
        c->destination.history = rs->history;

        if (!(c->status & MAPPER_TYPE_KNOWN) || !(c->status & MAPPER_LENGTH_KNOWN)) {
            c->destination.props->num_instances = size;
            continue;
        }

        // check if source histories need to be reallocated
        for (j = 0; j < c->props.num_sources; j++) {
            mapper_connection_slot s = &c->sources[i];
            if (!s->local && s->props->num_instances < size) {
                s->history = realloc(s->history, sizeof(struct _mapper_history)
                                     * size);
                for (k = s->props->num_instances; k < size; k++) {
                    s->history[k].type = s->props->type;
                    s->history[k].length = s->props->length;
                    s->history[k].size = s->history_size;
                    s->history[k].value = calloc(1, mapper_type_size(s->props->type)
                                                 * s->history_size);
                    s->history[k].timetag = calloc(1, sizeof(mapper_timetag_t)
                                                   * s->history_size);
                    s->history[k].position = -1;
                }
                s->props->num_instances = size;
            }
        }

        // check if expression variable histories need to be reallocated
        if (size > c->num_var_instances) {
            c->expr_vars = realloc(c->expr_vars, sizeof(mapper_history*) * size);
            for (j = c->num_var_instances; j < size; j++) {
                c->expr_vars[j] = malloc(sizeof(struct _mapper_history) *
                                         c->num_expr_vars);
                for (k = 0; k < c->num_expr_vars; k++) {
                    c->expr_vars[j][k].type = c->expr_vars[0][k].type;
                    c->expr_vars[j][k].length = c->expr_vars[0][k].length;
                    c->expr_vars[j][k].size = c->expr_vars[0][k].size;
                    c->expr_vars[j][k].position = -1;
                    c->expr_vars[j][k].value = calloc(1, sizeof(double) *
                                                      c->expr_vars[j][k].length *
                                                      c->expr_vars[j][k].size);
                    c->expr_vars[j][k].timetag = calloc(1, sizeof(mapper_timetag_t) *
                                                        c->expr_vars[j][k].size);
                }
            }
            c->num_var_instances = size;
        }
        c->destination.props->num_instances = size;
    }
    // for array of outgoing slots, may need to reallocate destination instances
    for (i = 0; i < rs->num_outgoing_slots; i++) {
        mapper_connection_slot s = rs->outgoing_slots[i];
        if (!s)
            continue;

        // history memory may have been moved by realloc()
        s->history = rs->history;

        mapper_connection c = s->connection;
        if (!(c->status & MAPPER_TYPE_KNOWN) || !(c->status & MAPPER_LENGTH_KNOWN)) {
            s->props->num_instances = size;
            continue;
        }

        // check if expression variable histories need to be reallocated
        if (size > c->num_var_instances) {
            c->expr_vars = realloc(c->expr_vars, sizeof(mapper_history*)
                                   * size);
            for (j = c->num_var_instances; j < size; j++) {
                c->expr_vars[j] = malloc(sizeof(struct _mapper_history) *
                                         c->num_expr_vars);
                for (k = 0; k < c->num_expr_vars; k++) {
                    c->expr_vars[j][k].type = c->expr_vars[0][k].type;
                    c->expr_vars[j][k].length = c->expr_vars[0][k].length;
                    c->expr_vars[j][k].size = c->expr_vars[0][k].size;
                    c->expr_vars[j][k].position = -1;
                    c->expr_vars[j][k].value = calloc(1, sizeof(double) *
                                                      c->expr_vars[j][k].length *
                                                      c->expr_vars[j][k].size);
                    c->expr_vars[j][k].timetag = calloc(1, sizeof(mapper_timetag_t) *
                                                        c->expr_vars[j][k].size);
                }
            }
            c->num_var_instances = size;
        }

        // check if destination history needs to be reallocated
        s = &c->destination;
        if (!s->local && s->props->num_instances < size) {
            s->history = realloc(s->history, sizeof(struct _mapper_history)
                                 * size);
            for (j = s->props->num_instances; j < size; j++) {
                s->history[j].type = s->props->type;
                s->history[j].length = s->props->length;
                s->history[j].size = s->history_size;
                s->history[j].value = calloc(1, mapper_type_size(s->props->type)
                                             * s->history_size);
                s->history[j].timetag = calloc(1, sizeof(mapper_timetag_t)
                                               * s->history_size);
                s->history[j].position = -1;
            }
            s->props->num_instances = size;
        }
        s->props->num_instances = size;
    }
}

void mapper_router_process_signal(mapper_router r, mapper_signal sig,
                                  int instance, void *value, int count,
                                  mapper_timetag_t tt)
{
    mapper_id_map map = sig->id_maps[instance].map;
    lo_message m;

    // find the signal connection
    mapper_router_signal rs = r->signals;
    while (rs) {
        if (rs->signal == sig)
            break;
        rs = rs->next;
    }
    if (!rs)
        return;

    int i, j, k, id = sig->id_maps[instance].instance->index;
    mapper_connection c;

    if (!value) {
        // reset associated input memory for this instance
        rs->history[id].position = -1;
        memset(rs->history[id].value, 0, rs->history_size *
               msig_vector_bytes(rs->signal));
        memset(rs->history[id].timetag, 0, rs->history_size *
               sizeof(mapper_timetag_t));
        mapper_connection_slot s;
        mapper_db_connection_slot p;
        for (i = 0; i < rs->num_outgoing_slots; i++) {
            if (!rs->outgoing_slots[i])
                continue;

            s = rs->outgoing_slots[i];
            c = s->connection;

            if (c->status < MAPPER_ACTIVE)
                continue;

            if (s->props->direction == DI_OUTGOING) {
                s = &c->destination;
                p = &c->props.destination;
                if (!s->local) {
                    // also need to reset associated output memory
                    s->history[id].position= -1;
                    memset(s->history[id].value, 0, s->history_size *
                           p->length * mapper_type_size(p->type));
                    memset(s->history[id].timetag, 0, s->history_size *
                           sizeof(mapper_timetag_t));
                }
                if (!p->send_as_instance)
                    m = mapper_connection_build_message(c, s, 0, 1, 0, 0);
                else if (get_in_scope(c, map->origin))
                    m = mapper_connection_build_message(c, s, 0, 1, 0, map);
                if (m)
                    send_or_bundle_message(s->link, p->signal_name, m, tt);
            }
        }
        // also need to release incoming connection instances
        for (i = 0; i < rs->num_incoming_connections; i++) {
            if (!rs->incoming_connections[i])
                continue;
            c = rs->incoming_connections[i];
            if (!get_in_scope(c, map->origin))
                continue;

            for (j = 0; j < c->props.num_sources; j++) {
                if (!c->sources[j].props->send_as_instance)
                    continue;
                s = &c->sources[j];
                p = &c->props.sources[j];
                // send release to upstream
                m = mapper_connection_build_message(c, s, 0, 1, 0, map);
                if (m)
                    send_or_bundle_message(s->link, p->signal_name, m, tt);
                if (!s->local) {
                    // also need to reset associated input memory
                    memset(s->history[id].value, 0, s->history_size *
                           p->length * mapper_type_size(p->type));
                    memset(s->history[id].timetag, 0, s->history_size *
                           sizeof(mapper_timetag_t));
                }
            }
        }
        return;
    }

    // if count > 1, we need to allocate sufficient memory for largest output
    // vector so that we can store calculated values before sending
    // TODO: calculate max_output_size, cache in link_signal
    void *out_value_p = count == 1 ? 0 : alloca(count * sig->props.length * sizeof(double));
    for (i = 0; i < rs->num_outgoing_slots; i++) {
        if (!rs->outgoing_slots[i])
            continue;

        mapper_connection_slot s = rs->outgoing_slots[i];
        c = s->connection;

        if (c->status < MAPPER_ACTIVE)
            continue;

        int in_scope = get_in_scope(c, map->origin);
        if ((s->props->direction == DI_INCOMING)
            || (s->props->send_as_instance && !in_scope)) {
            continue;
        }

        mapper_db_connection_slot sp = s->props;
        mapper_db_connection_slot dp = &c->props.destination;
        int to_length = ((c->props.process_location == MAPPER_SOURCE)
                         ? sp->length : dp->length);
        int to_size = ((c->props.process_location == MAPPER_SOURCE)
                       ? mapper_type_size(sp->type) * sp->length
                       : mapper_type_size(dp->type) * dp->length);
        char typestring[to_length];
        k = 0;
        for (j = 0; j < count; j++) {
            // copy input history
            size_t n = msig_vector_bytes(sig);
            rs->history[id].position = ((rs->history[id].position + 1)
                                        % rs->history[id].size);
            memcpy(mapper_history_value_ptr(rs->history[id]),
                   value + n * j, n);
            memcpy(mapper_history_tt_ptr(rs->history[id]),
                   &tt, sizeof(mapper_timetag_t));

            if (!(mapper_boundary_perform(&s->history[id], sp))) {
                // back up position index
                --s->history[id].position;
                if (s->history[id].position < 0)
                    s->history[id].position = s->history[id].size - 1;
                continue;
            }

            if (!sp->cause_update)
                continue;

            // handle cases in which part of count update does not cause output
            if (!(mapper_connection_perform(c, s, instance,
                                            typestring + to_length * k)))
                continue;

            void *result = mapper_history_value_ptr(c->destination.history[id]);

            if (count > 1) {
                memcpy((char*)out_value_p + to_size * j, result, to_size);
            }
            else {
                m = mapper_connection_build_message(c, s, result, 1, typestring,
                                                    sp->send_as_instance ? map : 0);
                if (m)
                    send_or_bundle_message(c->destination.link, dp->signal_name, m, tt);
            }
            k++;
        }
        if (count > 1 && (!s->props->send_as_instance || in_scope)) {
            m = mapper_connection_build_message(c, s, out_value_p, k, typestring,
                                                sp->send_as_instance ? map : 0);
            if (m)
                send_or_bundle_message(c->destination.link, dp->signal_name, m, tt);
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
    // find this signal in list of connections
    mapper_router_signal rs = r->signals;
    while (rs && rs->signal != sig)
        rs = rs->next;

    // exit without failure if signal is not mapped
    if (!rs)
        return 0;

    // for each connection, query the remote signal
    int i, j, count = 0;
    int len = (int) strlen(sig->props.name) + 5;
    char *response_string = (char*) malloc(len);
    snprintf(response_string, len, "%s/got", sig->props.name);
    char query_string[256];
    for (i = 0; i < rs->num_outgoing_slots; i++) {
        if (!rs->outgoing_slots[i])
            continue;
        mapper_connection c = rs->outgoing_slots[i]->connection;
        if (c->status != MAPPER_ACTIVE)
            continue;
        lo_message m = lo_message_new();
        if (!m)
            continue;
        lo_message_add_string(m, response_string);
        lo_message_add_int32(m, sig->props.length);
        lo_message_add_char(m, sig->props.type);
        // TODO: include response address as argument to allow TCP queries?
        // TODO: always use TCP for queries?

        snprintf(query_string, 256, "%s/get", c->props.destination.signal_name);
        send_or_bundle_message(c->destination.link, query_string, m, tt);
        count++;
    }
    for (i = 0; i < rs->num_incoming_connections; i++) {
        if (!rs->incoming_connections[i])
            continue;
        mapper_connection c = rs->incoming_connections[i];
        if (c->status != MAPPER_ACTIVE)
            continue;
        lo_message m = lo_message_new();
        if (!m)
            continue;
        lo_message_add_string(m, response_string);
        lo_message_add_int32(m, sig->props.length);
        lo_message_add_char(m, sig->props.type);
        // TODO: include response address as argument to allow TCP queries?
        // TODO: always use TCP for queries?
        for (j = 0; j < c->props.num_sources; j++) {
            snprintf(query_string, 256, "%s/get", c->props.sources[0].signal_name);
            send_or_bundle_message(c->destination.link, query_string, m, tt);
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
        rs->num_instances = sig->props.num_instances;
        rs->history = malloc(sizeof(struct _mapper_history) * rs->num_instances);
        rs->num_incoming_connections = 1;
        rs->incoming_connections = malloc(sizeof(mapper_connection *));
        rs->incoming_connections[0] = 0;
        rs->num_outgoing_slots = 1;
        rs->outgoing_slots = malloc(sizeof(mapper_connection_slot *));
        rs->outgoing_slots[0] = 0;
        int i;
        for (i=0; i<rs->num_instances; i++) {
            rs->history[i].type = sig->props.type;
            rs->history[i].length = sig->props.length;
            rs->history[i].size = 1;
            rs->history[i].value = calloc(1, msig_vector_bytes(sig));
            rs->history[i].timetag = calloc(1, sizeof(mapper_timetag_t));
            rs->history[i].position = -1;
        }
        rs->next = r->signals;
        r->signals = rs;
    }
    return rs;
}

static int router_signal_store_slot(mapper_router_signal rs,
                                    mapper_connection_slot slot)
{
    int i;
    for (i = 0; i < rs->num_outgoing_slots; i++) {
        if (!rs->outgoing_slots[i]) {
            // store pointer at empty index
            rs->outgoing_slots[i] = slot;
            return i;
        }
    }
    // all indices occupied, allocate more
    rs->outgoing_slots = realloc(rs->outgoing_slots,
                                 sizeof(mapper_connection_slot *)
                                 * rs->num_outgoing_slots * 2);
    rs->outgoing_slots[rs->num_outgoing_slots] = slot;
    for (i = rs->num_outgoing_slots+1; i < rs->num_outgoing_slots * 2; i++) {
        rs->outgoing_slots[i] = 0;
    }
    i = rs->num_outgoing_slots;
    rs->num_outgoing_slots *= 2;
    return i;
}

static int router_signal_store_connection(mapper_router_signal rs,
                                          mapper_connection connection)
{
    int i;
    for (i = 0; i < rs->num_incoming_connections; i++) {
        if (!rs->incoming_connections[i]) {
            // store pointer at empty index
            rs->incoming_connections[i] = connection;
            return i;
        }
    }
    // all indices occupied, allocate more
    rs->incoming_connections = realloc(rs->incoming_connections,
                                       sizeof(mapper_connection *)
                                       * rs->num_incoming_connections * 2);
    rs->incoming_connections[rs->num_incoming_connections] = connection;
    for (i = rs->num_incoming_connections+1; i < rs->num_incoming_connections * 2; i++) {
        rs->incoming_connections[i] = 0;
    }
    i = rs->num_incoming_connections;
    rs->num_incoming_connections *= 2;
    return i;
}

mapper_connection mapper_router_add_connection(mapper_router r,
                                               mapper_signal sig,
                                               int num_sources,
                                               mapper_signal *local_signals,
                                               const char **remote_signal_names,
                                               int direction)
{
    int i, ready = 1;

    if (num_sources > MAX_NUM_CONNECTION_SOURCES) {
        trace("error: maximum number of remote signals in a connection exceeded.\n");
        return 0;
    }

    mapper_router_signal rs = find_or_add_router_signal(r, sig);

    mapper_connection c = ((mapper_connection)
                           calloc(1, sizeof(struct _mapper_connection)));
    c->router = r;
    c->status = 0;

    // TODO: configure number of instances available for each slot
    c->num_var_instances = sig->props.num_instances;

    c->props.num_sources = num_sources;
    c->sources = ((mapper_connection_slot)
                  calloc(1, sizeof(struct _mapper_connection_slot)
                         * num_sources));
    c->props.sources = ((mapper_db_connection_slot)
                        calloc(1, sizeof(struct _mapper_db_connection_slot)
                               * num_sources));
    // scopes
    c->props.scope.size = direction == DI_OUTGOING ? 1 : num_sources;
    c->props.scope.names = (char **) malloc(sizeof(char *) * c->props.scope.size);
    c->props.scope.hashes = (uint32_t *) malloc(sizeof(uint32_t)
                                                * c->props.scope.size);

    // is_admin property will be corrected later if necessary
    c->is_admin = 1;
    c->props.id = direction == DI_INCOMING ? r->id_counter++ : -1;

    mapper_link link;
    char devname[256];
    const char *devname_p;
    int devnamelen, scope_count = 0, local_scope = 0;
    const char *signame;
    if (direction == DI_OUTGOING) {
        ready = 0;
        for (i = 0; i < num_sources; i++) {
            // find router_signal
            mapper_router_signal rs2 = find_or_add_router_signal(r, local_signals[i]);
            c->sources[i].local = rs2;
            c->sources[i].history = rs2->history;
            c->props.sources[i].num_instances = rs2->num_instances;
            c->props.sources[i].signal_name = local_signals[i]->props.name;
            c->props.sources[i].device_name = mdev_name(r->device);
            c->props.sources[i].type = local_signals[i]->props.type;
            c->props.sources[i].length = local_signals[i]->props.length;
            c->sources[i].status = MAPPER_READY;
            c->props.sources[i].direction = DI_OUTGOING;
            c->props.sources[i].device = &r->device->props;

            if (rs2->num_instances > c->num_var_instances)
                c->num_var_instances = rs2->num_instances;

            /* slot index will be overwritten if necessary by
             * mapper_connection_set_from_message() */
            c->props.sources[i].slot_id = -1;

            // also need to add connection to lists kept by source rs
            router_signal_store_slot(rs2, &c->sources[i]);
        }

        signame = strchr(remote_signal_names[0]+1, '/');
        devnamelen = signame - remote_signal_names[0];
        if (devnamelen >= 256) {
            // TODO: free partially-built connection structure
            return 0;
        }
        strncpy(devname, remote_signal_names[0], devnamelen);
        devname[devnamelen] = 0;
        c->props.destination.signal_name = strdup(signame);
        c->props.destination.num_instances = sig->props.num_instances;
        c->props.destination.direction = DI_OUTGOING;

        link = mapper_router_find_link_by_remote_name(r, devname);
        if (link) {
            c->destination.link = link;
        }
        else
            c->destination.link = mapper_router_add_link(r, 0, 0, 0, devname);
        c->props.destination.device = &c->destination.link->props;
        c->props.destination.device_name = c->destination.link->props.name;

        // apply local scope as default
        c->props.scope.names[0] = strdup(mdev_name(r->device));
        c->props.scope.hashes = (uint32_t *) malloc(sizeof(uint32_t));
        c->props.scope.hashes[0] = mdev_id(r->device);
    }
    else {
        c->destination.local = rs;
        c->destination.history = rs->history;
        c->props.destination.num_instances = sig->props.num_instances;
        c->props.destination.signal_name = sig->props.name;
        c->props.destination.device_name = mdev_name(r->device);
        c->props.destination.type = sig->props.type;
        c->props.destination.length = sig->props.length;
        c->destination.status = MAPPER_READY;
        c->props.destination.direction = DI_INCOMING;

        router_signal_store_connection(rs, c);

        for (i = 0; i < num_sources; i++) {
            if (local_signals && local_signals[i]) {
                devname_p = mdev_name(r->device);

                // find router_signal
                mapper_router_signal rs2 = find_or_add_router_signal(r, local_signals[i]);
                c->sources[i].local = rs2;
                c->sources[i].history = rs2->history;
                c->props.sources[i].num_instances = rs2->num_instances;
                c->props.sources[i].signal_name = local_signals[i]->props.name;
                c->props.sources[i].device_name = mdev_name(r->device);
                c->props.sources[i].type = local_signals[i]->props.type;
                c->props.sources[i].length = local_signals[i]->props.length;
                c->sources[i].status = MAPPER_READY;
                c->props.sources[i].direction = DI_OUTGOING;

                if (rs2->num_instances > c->num_var_instances)
                    c->num_var_instances = rs2->num_instances;

                // ensure local scope is only added once
                if (!local_scope) {
                    c->props.scope.names[scope_count] = strdup(devname_p);
                    c->props.scope.hashes[scope_count] = crc32(0L,
                                                               (const Bytef *)devname_p,
                                                               strlen(devname));
                    scope_count++;
                    local_scope = 1;
                }

                // also need to add connection to lists kept by source rs
                router_signal_store_slot(rs2, &c->sources[i]);
            }
            else {
                ready = 0;
                signame = strchr(remote_signal_names[i]+1, '/');
                devnamelen = signame - remote_signal_names[i];
                if (devnamelen >= 256) {
                    // TODO: free partially-built connection structure
                    return 0;
                }
                strncpy(devname, remote_signal_names[i], devnamelen);
                devname[devnamelen] = 0;
                devname_p = devname;
                c->props.sources[i].signal_name = strdup(signame);
                c->props.sources[i].num_instances = sig->props.num_instances;
                c->props.sources[i].direction = DI_INCOMING;

                // TODO: check that scope is not added multiple times
                c->props.scope.names[scope_count] = strdup(devname);
                c->props.scope.hashes[scope_count] = crc32(0L,
                                                           (const Bytef *)devname,
                                                           strlen(devname));
                scope_count++;
            }

            link = mapper_router_find_link_by_remote_name(r, devname_p);
            if (link) {
                c->sources[i].link = link;
            }
            else
                c->sources[i].link = mapper_router_add_link(r, 0, 0, 0, devname_p);

            c->props.sources[i].device = &c->sources[i].link->props;
            c->props.sources[i].device_name = c->sources[i].link->props.name;
            c->props.sources[i].slot_id = rs->id_counter++;
        }
        if (scope_count != num_sources) {
            c->props.scope.size = scope_count;
            c->props.scope.names = realloc(c->props.scope.names,
                                           sizeof(char *) * scope_count);
            c->props.scope.hashes = realloc(c->props.scope.hashes,
                                            sizeof(uint32_t) * scope_count);
        }
    }

    for (i = 0; i < num_sources; i++) {
        c->sources[i].connection = c;
        c->props.sources[i].cause_update = 1;
        c->props.sources[i].send_as_instance = c->props.sources[i].num_instances > 1;
        c->props.sources[i].bound_min = BA_NONE;
        c->props.sources[i].bound_max = BA_NONE;
        c->sources[i].props = &c->props.sources[i];
    }
    c->destination.connection = c;
    c->destination.props = &c->props.destination;
    c->props.destination.slot_id = -1;
    c->props.destination.cause_update = 1;
    c->props.destination.send_as_instance = c->props.destination.num_instances > 1;
    c->props.destination.bound_min = BA_NONE;
    c->props.destination.bound_max = BA_NONE;

    c->props.muted = 0;
    c->props.mode = MO_UNDEFINED;
    c->props.expression = 0;
    c->props.extra = table_new();

    // check if all sources belong to same remote device
    c->one_source = 1;
    for (i = 1; i < c->props.num_sources; i++) {
        if (c->sources[i].link != c->sources[0].link) {
            c->one_source = 0;
            break;
        }
    }
    // default to processing at source device unless heterogeneous sources
    if (c->one_source)
        c->props.process_location = MAPPER_SOURCE;
    else
        c->props.process_location = MAPPER_DESTINATION;

    if (ready) {
        // all reference signals are local
        c->is_local = 1;
        c->is_admin = 1;
        link = c->sources[0].link;
        c->destination.link = link;
    }
    return c;
}

static void check_link(mapper_router r, mapper_link l)
{
    if (l->props.num_connections_in || l->props.num_connections_out)
        return;
    // TODO: no connections, we can remove link
}

void mapper_router_remove_signal(mapper_router r, mapper_router_signal rs)
{
    if (r && rs) {
        // No connections remaining – we can remove the router_signal also
        mapper_router_signal *rstemp = &r->signals;
        while (*rstemp) {
            if (*rstemp == rs) {
                *rstemp = rs->next;
                int i;
                for (i=0; i<rs->num_instances; i++) {
                    free(rs->history[i].value);
                    free(rs->history[i].timetag);
                }
                free(rs->history);
                free(rs->incoming_connections);
                free(rs->outgoing_slots);
                free(rs);
                break;
            }
            rstemp = &(*rstemp)->next;
        }
    }
}

static void free_slot_memory(mapper_connection_slot s)
{
    int i;
    if (s->props->minimum)
        free(s->props->minimum);
    if (s->props->maximum)
        free(s->props->maximum);
    if (!s->local) {
        if (s->props->signal_name)
            free((void*)s->props->signal_name);
        if (s->history) {
            for (i = 0; i < s->props->num_instances; i++) {
                free(s->history[i].value);
                free(s->history[i].timetag);
            }
            free(s->history);
        }
    }
}

int mapper_router_remove_connection(mapper_router r, mapper_connection c)
{
    // do not free local names since it points to signal's copy
    int i, j;
    if (!c)
        return 1;

    // remove connection and slots from router_signal lists if necessary
    if (c->destination.local) {
        mapper_router_signal rs = c->destination.local;
        for (i = 0; i < rs->num_incoming_connections; i++) {
            if (rs->incoming_connections[i] == c) {
                rs->incoming_connections[i] = 0;
            }
        }
        if (c->status >= MAPPER_READY && c->destination.link) {
            c->destination.link->props.num_connections_in--;
            check_link(r, c->destination.link);
        }
    }
    else if (c->status >= MAPPER_READY && c->destination.link) {
        c->destination.link->props.num_connections_out--;
        check_link(r, c->destination.link);
    }
    free_slot_memory(&c->destination);
    for (i = 0; i < c->props.num_sources; i++) {
        if (c->sources[i].local) {
            mapper_router_signal rs = c->sources[i].local;
            for (j = 0; j < rs->num_outgoing_slots; j++) {
                if (rs->outgoing_slots[j] == &c->sources[i]) {
                    rs->outgoing_slots[j] = 0;
                }
            }
        }
        if (c->status >= MAPPER_READY && c->sources[i].link) {
            c->sources[i].link->props.num_connections_in--;
            check_link(r, c->sources[i].link);
        }
        free_slot_memory(&c->sources[i]);
    }
    free(c->sources);
    free(c->props.sources);

    // free buffers associated with user-defined expression variables
    for (i = 0; i < c->num_var_instances; i++) {
        if (c->num_expr_vars) {
            for (j = 0; j < c->num_expr_vars; j++) {
                free(c->expr_vars[i][j].value);
                free(c->expr_vars[i][j].timetag);
            }
        }
        free(c->expr_vars[i]);
    }
    if (c->expr_vars)
        free(c->expr_vars);
    if (c->expr)
        mapper_expr_free(c->expr);

    if (c->props.expression)
        free(c->props.expression);
    for (i=0; i<c->props.scope.size; i++) {
        free(c->props.scope.names[i]);
    }
    free(c->props.scope.names);
    free(c->props.scope.hashes);
    table_free(c->props.extra, 1);

    free(c);
    return 0;
}

static int match_slot(mapper_device md, mapper_connection_slot slot,
                      const char *full_name)
{
    if (!full_name)
        return 1;
    const char *sig_name = strchr(full_name+1, '/');
    if (!sig_name)
        return 1;
    int len = sig_name - full_name;
    const char *local_devname = slot->local ? mdev_name(md) : slot->link->props.name;

    // first compare device name
    if (strlen(local_devname) != len || strncmp(full_name, local_devname, len))
        return 1;

    if (strcmp(sig_name, slot->props->signal_name) == 0)
        return 0;
    return 1;
}

mapper_connection mapper_router_find_outgoing_connection(mapper_router router,
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

    // find associated connection
    int i, j;
    for (i = 0; i < rs->num_outgoing_slots; i++) {
        if (!rs->outgoing_slots[i])
            continue;
        mapper_connection_slot s = rs->outgoing_slots[i];
        mapper_connection c = s->connection;

        // check destination
        if (match_slot(router->device, &c->destination, dest_name))
            continue;
        // check sources
        int found = 1;
        for (j = 0; j < c->props.num_sources; j++) {
            if (c->sources[j].local == rs)
                continue;
            if (match_slot(router->device, &c->sources[j], src_names[j])) {
                found = 0;
                break;
            }
        }
        if (found)
            return c;
    }
    return 0;
}

mapper_connection mapper_router_find_incoming_connection(mapper_router router,
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

    // find associated connection
    int i, j;
    for (i = 0; i < rs->num_incoming_connections; i++) {
        if (!rs->incoming_connections[i])
            continue;
        mapper_connection c = rs->incoming_connections[i];

        // check sources
        int found = 1;
        for (j = 0; j < num_sources; j++) {
            if (match_slot(router->device, &c->sources[j], src_names[j])) {
                found = 0;
                break;
            }
        }
        if (found)
            return c;
    }
    return 0;
}

mapper_connection mapper_router_find_incoming_connection_id(mapper_router router,
                                                            mapper_signal local_dest,
                                                            int id)
{
    mapper_router_signal rs = router->signals;
    while (rs && rs->signal != local_dest)
        rs = rs->next;
    if (!rs)
        return 0;

    int i;
    for (i = 0; i < rs->num_incoming_connections; i++) {
        if (!rs->incoming_connections[i])
            continue;
        mapper_connection c = rs->incoming_connections[i];
        if (c->props.id == id)
            return c;
    }
    return 0;
}

mapper_connection mapper_router_find_outgoing_connection_id(mapper_router router,
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

    const char *signame = strchr(dest_name+1, '/');
    int devnamelen = signame ? signame - dest_name : strlen(dest_name);
    for (i = 0; i < rs->num_outgoing_slots; i++) {
        if (!rs->outgoing_slots[i])
            continue;
        mapper_connection c = rs->outgoing_slots[i]->connection;
        if (c->props.id != id)
            continue;
        const char *match = (c->destination.local
                             ? mdev_name(router->device)
                             : c->destination.link->props.name);
        if (strlen(match)==devnamelen
            && strncmp(match, dest_name, devnamelen)==0) {
            return c;
        }
    }
    return 0;
}

mapper_connection_slot mapper_router_find_connection_slot(mapper_router router,
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
    mapper_connection c;
    for (i = 0; i < rs->num_incoming_connections; i++) {
        c = rs->incoming_connections[i];
        if (!c)
            continue;
        // check incoming slots for this connection
        for (j = 0; j < c->props.num_sources; j++) {
            if (c->sources[j].props->slot_id == slot_id)
                return &c->sources[j];
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
                                                   uint32_t name_hash)
{
    mapper_link l = router->links;
    while (l) {
        if (name_hash == l->props.name_hash)
            return l;
        l = l->next;
    }
    return 0;
}
