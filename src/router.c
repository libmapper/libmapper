
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <zlib.h>

#include <lo/lo.h>

#include "mapper_internal.h"
#include "types_internal.h"
#include <mapper/mapper.h>

#define MAX_NUM_REMOTE_SIGNALS 8    // arbitrary

static void send_or_bundle_message(mapper_link link, const char *path,
                                   lo_message m, mapper_timetag_t tt);

static int in_connection_scope(mapper_connection c, uint32_t name_hash)
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
        if (l->remote_host)
            free(l->remote_host);
        if (l->remote_name)
            free(l->remote_name);
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
        l->remote_host = strdup(host);
        l->remote_port = data_port;
        sprintf(str, "%d", data_port);
        l->data_addr = lo_address_new(host, str);
        sprintf(str, "%d", admin_port);
        l->admin_addr = lo_address_new(host, str);
    }
    l->remote_name = strdup(name);
    l->remote_name_hash = crc32(0L, (const Bytef *)name, strlen(name));

    l->device = router->device;
    l->num_connections_in = 0;
    l->num_connections_out = 0;

    if (name == mdev_name(router->device))
        l->self_link = 1;

    l->clock.new = 1;
    l->clock.sent.message_id = 0;
    l->clock.response.message_id = -1;

    l->next = router->links;
    router->links = l;

    if (!host) {
        // request missing metadata
        mapper_admin_set_bundle_dest_bus(router->device->admin);
        mapper_admin_bundle_message(router->device->admin, ADM_LINK_TO, 0,
                                    "ss", mdev_name(router->device), name,
                                    AT_PORT, router->device->props.port);
    }

    return l;
}

void mapper_router_update_link(mapper_router router, mapper_link link,
                               const char *host, int admin_port, int data_port)
{
    char str[16];
    link->remote_host = strdup(host);
    link->remote_port = data_port;
    sprintf(str, "%d", data_port);
    link->data_addr = lo_address_new(host, str);
    sprintf(str, "%d", admin_port);
    link->admin_addr = lo_address_new(host, str);
}

void mapper_router_remove_link(mapper_router router, mapper_link link)
{
    // check if any connection use this link
    mapper_router_signal s = router->signals;
    while (s) {
        mapper_connection c = s->connections;
        while (c) {
            // TODO: free connections
            c = c->next;
        }
        s = s->next;
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

    // Need to allocate more instances
    rs->history = realloc(rs->history, sizeof(struct _mapper_signal_history)
                          * size);
    // TODO
    int i, j;
    for (i=rs->num_instances; i<size; i++) {
        rs->history[i].type = sig->props.type;
        rs->history[i].length = sig->props.length;
        rs->history[i].size = rs->history_size;
        rs->history[i].value = calloc(1, msig_vector_bytes(sig)
                                      * rs->history_size);
        rs->history[i].timetag = calloc(1, sizeof(mapper_timetag_t)
                                        * rs->history_size);
        rs->history[i].position = -1;
    }

    // reallocate connection instances
    mapper_connection c = rs->connections;
    while (c) {
        c->expr_vars = realloc(c->expr_vars, sizeof(mapper_signal_history_t*)
                               * size);
        for (i = 0; i < c->props.num_sources; i++) {
            mapper_connection_slot s = &c->sources[i];
            if (s->local) {
                if (s->local == rs)
                    s->history = rs->history;
                continue;
            }
            mapper_db_connection_slot p = &c->props.sources[i];
            s->history = realloc(s->history, sizeof(struct _mapper_signal_history)
                                 * size);
            for (i=rs->num_instances; i<size; i++) {
                s->history[i].type = p->type;
                s->history[i].length = p->length;
                s->history[i].size = s->history_size;
                s->history[i].value = calloc(1, mapper_type_size(p->type)
                                             * s->history_size);
                s->history[i].timetag = calloc(1, sizeof(mapper_timetag_t)
                                               * s->history_size);
                s->history[i].position = -1;
            }
        }
        mapper_connection_slot s = &c->destination;
        if (s->local) {
            if (s->local == rs)
                s->history = rs->history;
        }
        else {
            mapper_db_connection_slot p = &c->props.destination;
            s->history = realloc(s->history, sizeof(struct _mapper_signal_history)
                                 * size);
            for (i=rs->num_instances; i<size; i++) {
                s->history[i].type = p->type;
                s->history[i].length = p->length;
                s->history[i].size = s->history_size;
                s->history[i].value = calloc(1, mapper_type_size(p->type)
                                             * s->history_size);
                s->history[i].timetag = calloc(1, sizeof(mapper_timetag_t)
                                               * s->history_size);
                s->history[i].position = -1;
            }
        }
        for (i=rs->num_instances; i<size; i++) {
            c->expr_vars[i] = malloc(sizeof(struct _mapper_signal_history) *
                                     c->num_expr_vars);
        }
        if (rs->num_instances > 0 && c->num_expr_vars > 0) {
            for (i=rs->num_instances; i<size; i++) {
                for (j=0; j<c->num_expr_vars; j++) {
                    c->expr_vars[i][j].type = c->expr_vars[0][j].type;
                    c->expr_vars[i][j].length = c->expr_vars[0][j].length;
                    c->expr_vars[i][j].size = c->expr_vars[0][j].size;
                    c->expr_vars[i][j].position = -1;
                    c->expr_vars[i][j].value = calloc(1, sizeof(double) *
                                                     c->expr_vars[i][j].length *
                                                     c->expr_vars[i][j].size);
                    c->expr_vars[i][j].timetag = calloc(1, sizeof(mapper_timetag_t) *
                                                       c->expr_vars[i][j].size);
                }
            }
        }
        c = c->next;
    }

    rs->num_instances = size;
}

void mapper_router_process_signal(mapper_router r,
                                  mapper_signal sig,
                                  int instance,
                                  void *value,
                                  int count,
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

    int i, j, slot, id = sig->id_maps[instance].instance->index;
    mapper_connection c;

    if (!value) {
        rs->history[id].position = -1;
        // reset associated input memory for this instance
        memset(rs->history[id].value, 0, rs->history_size *
               msig_vector_bytes(rs->signal));
        memset(rs->history[id].timetag, 0, rs->history_size *
               sizeof(mapper_timetag_t));
        c = rs->connections;
        while (c) {
            // find source slot
            mapper_connection_slot s = 0;
            mapper_db_connection_slot p = 0;
            for (i = 0; i < c->props.num_sources; i++) {
                if (c->sources[i].local == rs) {
                    s = &c->sources[i];
                    p = &c->props.sources[0];
                    slot = i;
                    break;
                }
            }
            if (s) {
                mapper_db_connection_slot p = &c->props.sources[0];
                s->history[id].position = -1;
                if (c->props.direction == DI_OUTGOING) {
                    if (!c->props.send_as_instance
                        || in_connection_scope(c, map->origin)) {
                        m = mapper_connection_build_message(c, 0, 1, 0, map);
                        if (m)
                            send_or_bundle_message(c->destination.link, p->name, m, tt);
                        // also need to reset associated output memory
                        memset(s->history[id].value, 0, s->history_size *
                               p->length * mapper_type_size(p->type));
                        memset(s->history[id].timetag, 0, s->history_size *
                               sizeof(mapper_timetag_t));
                    }
                }
                else if (c->props.send_as_instance
                         && in_connection_scope(c, map->origin)) {
                    // send release to upstream
                    m = mapper_connection_build_message(c, 0, 1, 0, map);
                    if (m)
                        send_or_bundle_message(c->destination.link, p->name, m, tt);
                }
                // also need to reset associated output memory
                memset(s->history[id].value, 0, s->history_size *
                       p->length * mapper_type_size(p->type));
                memset(s->history[id].timetag, 0, s->history_size *
                       sizeof(mapper_timetag_t));
            }
            c = c->next;
        }
        return;
    }

    // if count > 1, we need to allocate sufficient memory for largest output
    // vector so that we can store calculated values before sending
    // TODO: calculate max_output_size, cache in link_signal
    void *out_value_p = count == 1 ? 0 : alloca(count * sig->props.length * sizeof(double));
    c = rs->connections;
    while (c) {
        int in_scope = in_connection_scope(c, map->origin);
        if ((c->props.direction == DI_INCOMING)
            || (c->props.send_as_instance && !in_scope)) {
            c = c->next;
            continue;
        }

        // find slot
        mapper_connection_slot s = 0;
        mapper_db_connection_slot sp = 0;
        for (i = 0; i < c->props.num_sources; i++) {
            if (c->sources[i].local == rs) {
                s = &c->sources[i];
                sp = &c->props.sources[0];
                slot = i;
                break;
            }
        }
        if (!s) {
            c = c->next;
            continue;
        }
        mapper_db_connection_slot dp = &c->props.destination;
        int to_size = ((c->props.mode == MO_RAW)
                       ? mapper_type_size(sp->type) * sp->length
                       : mapper_type_size(dp->type) * dp->length);
        char typestring[dp->length * count];
        j = 0;
        for (i = 0; i < count; i++) {
            // copy input history
            size_t n = msig_vector_bytes(sig);
            rs->history[id].position = ((rs->history[id].position + 1)
                                        % rs->history[id].size);
            memcpy(msig_history_value_pointer(rs->history[id]),
                   value + n * i, n);
            memcpy(msig_history_tt_pointer(rs->history[id]),
                   &tt, sizeof(mapper_timetag_t));

            void *from_ptr = msig_history_value_pointer((c->props.mode == MO_RAW)
                                                        ? c->sources[0].history[id]
                                                        : s->history[id]);

            // handle cases in which part of count update does not cause output
            if (!(mapper_connection_perform(c, slot, instance,
                                            typestring + dp->length*j)))
                continue;
            if (!(mapper_boundary_perform(c, &s->history[id])))
                continue;

            if (count > 1) {
                memcpy((char*)out_value_p + to_size * i, from_ptr, to_size);
            }
            else {
                m = mapper_connection_build_message(c, from_ptr, 1,
                                                    typestring, map);
                if (m)
                    send_or_bundle_message(c->destination.link, dp->name, m, tt);
            }
            j++;
        }
        if (count > 1 && (!c->props.send_as_instance || in_scope)) {
            m = mapper_connection_build_message(c, out_value_p, j,
                                                typestring, map);
            if (m)
                send_or_bundle_message(c->destination.link, dp->name, m, tt);
        }
        c = c->next;
    }
}

int mapper_router_send_query(mapper_router r,
                             mapper_signal sig,
                             mapper_timetag_t tt)
{
    // TODO: cache the response string
    // find this signal in list of connections
    mapper_router_signal rs = r->signals;
    while (rs && rs->signal != sig)
        rs = rs->next;

    // exit without failure if signal is not mapped
    if (!rs) {
        return 0;
    }
    // for each connection, query the remote signal
    mapper_connection c = rs->connections;
    int count = 0;
    int len = (int) strlen(sig->props.name) + 5;
    char *response_string = (char*) malloc(len);
    snprintf(response_string, len, "%s%s", sig->props.name, "/got");
    char *query_string = (char*) malloc(len);
    snprintf(query_string, len, "%s%s", sig->props.name, "/get");
    while (c) {
        lo_message m = lo_message_new();
        if (!m)
            continue;
        lo_message_add_string(m, response_string);
        lo_message_add_int32(m, sig->props.length);
        // include response address as argument to allow TCP queries?
        // TODO: always use TCP for queries?
        lo_message_add_char(m, sig->props.type);
        // TODO: if we are querying connection source, could be multiple sources
        send_or_bundle_message(c->destination.link, query_string, m, tt);
        count++;
        c = c->next;
    }
    free(response_string);
    free(query_string);
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
        rs->history = malloc(sizeof(struct _mapper_signal_history)
                             * rs->num_instances);
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

mapper_connection mapper_router_add_connection(mapper_router r,
                                               mapper_signal sig,
                                               int num_remote_signals,
                                               const char **remote_signal_names,
                                               int direction)
{
    int i;

    if (num_remote_signals > MAX_NUM_REMOTE_SIGNALS) {
        trace("error: maximum number of remote signals in a connection exceeded.\n");
        return 0;
    }

    mapper_router_signal rs = find_or_add_router_signal(r, sig);

    mapper_connection c = (mapper_connection)
        calloc(1, sizeof(struct _mapper_connection));
    c->router = r;
    c->status = 0;
    c->props.direction = direction;
    c->slot_start = rs->slot_start++ * MAX_NUM_REMOTE_SIGNALS;

    // TODO: configure number of instances available for each slot
    c->num_var_instances = 1;

    if (direction == DI_OUTGOING)
        num_remote_signals = 1;
    c->props.num_sources = num_remote_signals;
    c->sources = ((mapper_connection_slot)
                  calloc(1, sizeof(struct _mapper_connection_slot)
                         * num_remote_signals));
    c->props.sources = ((mapper_db_connection_slot)
                        calloc(1, sizeof(struct _mapper_db_connection_slot)
                               * num_remote_signals));

//    if (   (num_remote_signals == 1 && direction == DI_OUTGOING)
//        || (num_remote_signals > 1 && direction == DI_INCOMING))
    c->is_admin = num_remote_signals > 1 ^ direction == DI_OUTGOING;
    
    mapper_link link;
    char devname[256];
    int devnamelen;
    const char *signame;
    if (direction == DI_OUTGOING) {
        // TODO: expand to possible multiple local signals
        c->sources[0].local = rs;
        c->sources[0].history = rs->history;
        c->props.sources[0].num_instances = rs->signal->props.num_instances;
        c->props.sources[0].name = sig->props.name;
        c->props.sources[0].type = sig->props.type;
        c->props.sources[0].length = sig->props.length;
        c->sources[0].status = MAPPER_READY;
        signame = strchr(remote_signal_names[0]+1, '/');
        devnamelen = signame - remote_signal_names[0];
        if (devnamelen >= 256) {
            // TODO: free partially-built connection structure
            return 0;
        }
        strncpy(devname, remote_signal_names[0], devnamelen);
        devname[devnamelen] = 0;
        c->props.destination.name = strdup(signame);
        c->props.destination.num_instances = 1;
        link = mapper_router_find_link_by_remote_name(r, devname);
        if (link)
            c->destination.link = link;
        else
            c->destination.link = mapper_router_add_link(r, 0, 0, 0, devname);
    }
    else {
        c->destination.local = rs;
        c->destination.history = rs->history;
        c->props.destination.num_instances = rs->signal->props.num_instances;
        c->props.destination.name = sig->props.name;
        c->props.destination.type = sig->props.type;
        c->props.destination.length = sig->props.length;
        c->destination.status = MAPPER_READY;
        for (i = 0; i < num_remote_signals; i++) {
            signame = strchr(remote_signal_names[i]+1, '/');
            devnamelen = signame - remote_signal_names[i];
            if (devnamelen >= 256) {
                // TODO: free partially-built connection structure
                return 0;
            }
            strncpy(devname, remote_signal_names[i], devnamelen);
            devname[devnamelen] = 0;
            c->props.sources[i].name = strdup(signame);
            c->props.sources[i].num_instances = 1;
            link = mapper_router_find_link_by_remote_name(r, devname);
            if (link)
                c->sources[i].link = link;
            else
                c->sources[i].link = mapper_router_add_link(r, 0, 0, 0, devname);
        }
    }

    c->props.mode = MO_UNDEFINED;
    c->props.expression = 0;
    c->props.bound_min = BA_NONE;
    c->props.bound_max = BA_NONE;
    c->props.muted = 0;
    c->props.send_as_instance = (rs->num_instances > 1);

    // scopes
    c->props.scope.size = 1;
    c->props.scope.names = (char **) malloc(sizeof(char *));
    c->props.scope.names[0] = strdup(mdev_name(r->device));
    c->props.scope.hashes = (uint32_t *) malloc(sizeof(uint32_t));
    c->props.scope.hashes[0] = mdev_id(r->device);

    c->props.extra = table_new();

    // add new connection to this signal's list
    // TODO: could be more than 1 local signal
    c->next = rs->connections;
    rs->connections = c;

    return c;
}

static void check_link(mapper_router r, mapper_link l)
{
    if (l->num_connections_in || l->num_connections_out)
        return;
    // TODO: no connections, we can remove link
}

static void mapper_router_signal_remove_connection(mapper_router r,
                                                   mapper_router_signal s,
                                                   mapper_connection c)
{
    if (!r || !s || !c)
        return;

    mapper_connection *temp = &s->connections;
    while (*temp) {
        if (*temp == c) {
            *temp = c->next;
            break;
        }
        temp = &(*temp)->next;
    }

    if (!*temp && *temp != s->connections) {
        // No connections remaining – we need to remove the router_signal also
        mapper_router_signal *rstemp = &r->signals;
        while (*rstemp) {
            if (*rstemp == s) {
                *rstemp = s->next;
                int i;
                for (i=0; i<s->num_instances; i++) {
                    free(s->history[i].value);
                    free(s->history[i].timetag);
                }
                free(s->history);
                free(s);
                break;
            }
            rstemp = &(*rstemp)->next;
        }
    }
}

int mapper_router_remove_connection(mapper_router r, mapper_connection c)
{
    // do not free local names since it points to signal's copy
    int i, j;
    if (!c)
        return 1;

    // free connection source structures
    for (i = 0; i < c->props.num_sources; i++) {
        if (c->props.sources[i].minimum)
            free(c->props.sources[i].minimum);
        if (c->props.sources[i].maximum)
            free(c->props.sources[i].maximum);
        if (c->sources[i].link) {
            // TODO: check for multiple sources from same device
            c->sources[i].link->num_connections_in--;
            check_link(r, c->sources[i].link);
        }
        if (c->sources[i].local) {
            mapper_router_signal_remove_connection(r, c->sources[i].local, c);
        }
        else {
            if (c->props.sources[i].name)
                free(c->props.sources[i].name);
            if (c->sources[i].history) {
                for (j = 0; j < c->props.sources[i].num_instances; j++) {
                    free(c->sources[i].history[j].value);
                    free(c->sources[i].history[j].timetag);
                }
                free(c->sources[i].history);
            }
        }
    }
    free(c->sources);
    free(c->props.sources);

    // free connection destination structures
    if (c->props.destination.minimum)
        free(c->props.destination.minimum);
    if (c->props.destination.maximum)
        free(c->props.destination.maximum);
    if (c->destination.link) {
        c->destination.link->num_connections_out--;
        check_link(r, c->destination.link);
    }
    if (c->destination.local) {
        mapper_router_signal_remove_connection(r, c->destination.local, c);
    }
    else {
        if (c->props.destination.name)
            free(c->props.destination.name);
        if (c->destination.history) {
            for (j = 0; j < c->props.destination.num_instances; j++) {
                free(c->destination.history->value);
                free(c->destination.history->timetag);
            }
            free(c->destination.history);
        }
    }

    // free buffers associated with user-defined expression variables
    for (i = 0; i < c->num_var_instances; i++) {
        if (c->num_expr_vars) {
            for (j=0; j<c->num_expr_vars; j++) {
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

mapper_connection mapper_router_find_connection(mapper_router router,
                                                mapper_signal signal,
                                                int num_remote_signals,
                                                const char **remote_names)
{
    // find associated router_signal
    mapper_router_signal rs = router->signals;
    while (rs && rs->signal != signal)
        rs = rs->next;
    if (!rs)
        return NULL;

    // find associated connection
    mapper_connection c = rs->connections;
    while (c) {
        if (c->props.direction == DI_OUTGOING) {
            if (num_remote_signals == 1
                && strcmp(c->props.destination.name, remote_names[0])==0)
                return c;
        }
        else if (num_remote_signals == c->props.num_sources) {
            int i, found = 1;
            for (i = 0; i < num_remote_signals; i++) {
                if (strcmp(c->props.sources[i].name, remote_names[i])) {
                    found = 0;
                    break;
                }
            }
            if (found)
                break;
        }
        c = c->next;
    }
    return c;
}

mapper_connection mapper_router_find_connection_by_slot(mapper_router router,
                                                        mapper_signal signal,
                                                        int *slot_number)
{
    if (!slot_number)
        return NULL;

    mapper_router_signal rs = router->signals;
    while (rs && rs->signal != signal)
        rs = rs->next;
    if (!rs || !rs->connections)
        return NULL; // no associated router_signal
    mapper_connection c = rs->connections;
    while (c) {
        int temp_slot = *slot_number - c->slot_start;
        if (temp_slot >= 0 && temp_slot < c->props.num_sources) {
            *slot_number = temp_slot;
            return c;
        }
        c = c->next;
    }
    return NULL;
}

mapper_link mapper_router_find_link_by_remote_address(mapper_router r,
                                                      const char *host,
                                                      int port)
{
    mapper_link l = r->links;
    while (l) {
        if (l->remote_port == port && (strcmp(l->remote_host, host)==0))
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
        if (strncmp(l->remote_name, name, n)==0 && l->remote_name[n]==0)
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
        if (name_hash == l->remote_name_hash)
            return l;
        l = l->next;
    }
    return 0;
}
