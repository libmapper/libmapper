
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
    char str[16];
    mapper_link l = (mapper_link) calloc(1, sizeof(struct _mapper_link));
    l->remote_host = strdup(host);
    l->remote_port = data_port;
    sprintf(str, "%d", data_port);
    l->data_addr = lo_address_new(host, str);
    sprintf(str, "%d", admin_port);
    l->admin_addr = lo_address_new(host, str);
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

    if (!l->data_addr) {
        mapper_link_free(l);
        return 0;
    }

    l->next = router->links;
    router->links = l;

    return l;
}

static void remove_link_signals(mapper_router router, mapper_link link)
{
    mapper_router_signal rs = router->signals;
    while (rs) {
        mapper_connection c = rs->connections;
        rs = rs->next;
        while (c) {
            if (c->remote_dest == link) {
                mapper_connection temp = c;
                c = c->next;
                mapper_router_remove_connection(router, temp);
            }
            else
                c = c->next;
        }
    }
}

void mapper_router_remove_link(mapper_router router, mapper_link link)
{
    mapper_link *links = &router->links;
    while (*links) {
        if (*links == link) {
            *links = link->next;
            remove_link_signals(router, link);
            mapper_link_free(link);
            break;
        }
        links = &(*links)->next;
    }
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
    rs->value.history = realloc(rs->value.history, sizeof(struct _mapper_signal_history)
                                * size);
    int i, j;
    for (i=rs->num_instances; i<size; i++) {
        rs->value.history[i].type = sig->props.type;
        rs->value.history[i].length = sig->props.length;
        rs->value.history[i].size = rs->value.history_size;
        rs->value.history[i].value = calloc(1, msig_vector_bytes(sig)
                                            * rs->value.history_size);
        rs->value.history[i].timetag = calloc(1, sizeof(mapper_timetag_t)
                                              * rs->value.history_size);
        rs->value.history[i].position = -1;
    }

    // reallocate connection instances
    mapper_connection c = rs->connections;
    while (c) {
        c->expr_vars = realloc(c->expr_vars, sizeof(mapper_signal_history_t*)
                               * size);
        for (i = 0; i < c->props.num_sources; i++) {
            mapper_connection_history h = c->sources[i];
            mapper_db_connection_slot_props p = c->props.sources[i];
            h->history = realloc(h->history, sizeof(struct _mapper_signal_history)
                                 * size);
            for (i=rs->num_instances; i<size; i++) {
                h->history[i].type = p->type;
                h->history[i].length = p->length;
                h->history[i].size = h->history_size;
                h->history[i].value = calloc(1, mapper_type_size(p->type)
                                             * h->history_size);
                h->history[i].timetag = calloc(1, sizeof(mapper_timetag_t)
                                               * h->history_size);
                h->history[i].position = -1;
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
                                  int instance_index,
                                  void *value,
                                  int count,
                                  mapper_timetag_t tt)
{
    mapper_id_map map = sig->id_maps[instance_index].map;
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

    int id = sig->id_maps[instance_index].instance->index;
    mapper_connection c;

    if (!value) {
        rs->value.history[id].position = -1;
        // reset associated input memory for this instance
        memset(rs->value.history[id].value, 0, rs->value.history_size *
               msig_vector_bytes(rs->signal));
        memset(rs->value.history[id].timetag, 0, rs->value.history_size *
               sizeof(mapper_timetag_t));
        c = rs->connections;
        while (c) {
            mapper_connection_history h = c->sources[0];
            mapper_db_connection_slot_props p = c->props.sources[0];
            h->history[id].position = -1;
            if (c->props.direction == DI_OUTGOING) {
                if (!c->props.send_as_instance
                    || in_connection_scope(c, map->origin)) {
                    m = mapper_connection_build_message(c, 0, 1, 0, map);
                    if (m)
                        send_or_bundle_message(c->remote_dest, p->name, m, tt);
                    // also need to reset associated output memory
                    memset(h->history[id].value, 0, h->history_size *
                           p->length * mapper_type_size(p->type));
                    memset(h->history[id].timetag, 0, h->history_size *
                           sizeof(mapper_timetag_t));
                }
            }
            else if (c->props.send_as_instance
                     && in_connection_scope(c, map->origin)) {
                // send release to upstream
                m = mapper_connection_build_message(c, 0, 1, 0, map);
                if (m)
                    send_or_bundle_message(c->remote_dest, p->name, m, tt);
            }
            // also need to reset associated output memory
            memset(h->history[id].value, 0, h->history_size *
                   p->length * mapper_type_size(p->type));
            memset(h->history[id].timetag, 0, h->history_size *
                   sizeof(mapper_timetag_t));

            c = c->next;
        }
        return;
    }

    // if count > 1, we need to allocate sufficient memory for largest output
    // vector so that we can store calculated values before sending
    // TODO: calculate max_output_size, cache in link_signal
    void *out_value_p = count == 1 ? 0 : alloca(count * sig->props.length * sizeof(double));
    int i, j;
    c = rs->connections;
    while (c) {
        int in_scope = in_connection_scope(c, map->origin);
        if ((c->props.direction == DI_INCOMING)
            || (c->props.send_as_instance && !in_scope)) {
            c = c->next;
            continue;
        }

        mapper_connection_history h = c->sources[0];
        mapper_db_connection_slot_props dp = c->props.dest;
        mapper_db_connection_slot_props sp = c->props.sources[0];
        int to_size = ((c->props.mode == MO_RAW)
                       ? mapper_type_size(sp->type) * sp->length
                       : mapper_type_size(dp->type) * dp->length);
        char typestring[dp->length * count];
        j = 0;
        for (i = 0; i < count; i++) {
            // copy input history
            size_t n = msig_vector_bytes(sig);
            rs->value.history[id].position = ((rs->value.history[id].position + 1)
                                              % rs->value.history[id].size);
            memcpy(msig_history_value_pointer(rs->value.history[id]),
                   value + n * i, n);
            memcpy(msig_history_tt_pointer(rs->value.history[id]),
                   &tt, sizeof(mapper_timetag_t));

            void *from_ptr = msig_history_value_pointer((c->props.mode == MO_RAW)
                                                        ? c->sources[0]->history[id]
                                                        : h->history[id]);

            // handle cases in which part of count update does not cause output
            if (!(mapper_connection_perform(c, &rs->value.history[id],
                                            &c->expr_vars[id],
                                            &h->history[id], typestring
                                            + dp->length*j)))
                continue;
            if (!(mapper_boundary_perform(c, &h->history[id])))
                continue;

            if (count > 1) {
                memcpy((char*)out_value_p + to_size * i, from_ptr, to_size);
            }
            else {
                m = mapper_connection_build_message(c, from_ptr, 1,
                                                    typestring, map);
                if (m)
                    send_or_bundle_message(c->remote_dest, dp->name, m, tt);
            }
            j++;
        }
        if (count > 1 && (!c->props.send_as_instance || in_scope)) {
            m = mapper_connection_build_message(c, out_value_p, j,
                                                typestring, map);
            if (m)
                send_or_bundle_message(c->remote_dest, dp->name, m, tt);
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
        send_or_bundle_message(c->remote_dest, query_string, m, tt);
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
        rs = (mapper_router_signal)
        calloc(1, sizeof(struct _mapper_router_signal));
        rs->signal = sig;
        rs->num_instances = sig->props.num_instances;
        rs->value.history = malloc(sizeof(struct _mapper_signal_history)
                                   * rs->num_instances);
        int i;
        for (i=0; i<rs->num_instances; i++) {
            rs->value.history[i].type = sig->props.type;
            rs->value.history[i].length = sig->props.length;
            rs->value.history[i].size = 1;
            rs->value.history[i].value = calloc(1, msig_vector_bytes(sig));
            rs->value.history[i].timetag = calloc(1, sizeof(mapper_timetag_t));
            rs->value.history[i].position = -1;
        }
        rs->next = r->signals;
        r->signals = rs;
    }
    return rs;
}

mapper_connection mapper_router_add_connection(mapper_router r,
                                               mapper_link link,
                                               mapper_signal sig,
                                               int num_remote_signals,
                                               const char **remote_signal_names,
                                               int direction)
{
    // if is local-only, use direction OUTGOING
    // if is direction OUTGOING
    //      – reference at least one local signal
    //      – references only one local/remote signal (destination) - "result"?
    //      – may call local signal handler
    //      – may send OSC message
    // if is direction INCOMING
    //      – references at least one remote signal
    //      – references only one local signal = "result"
    //      – may call local signal handler
    // for each:
    //      check if muted
    //      run expression if any
    //      run boundary if any
    //      call local handlers if any
    //      send messages if any
    int i;
    // TODO:
    //  check if links exists, if so add pointer, mark ready | LINK_KNOWN
    //  check if self connection, if so add local connection info

    if (num_remote_signals > MAX_NUM_REMOTE_SIGNALS) {
        trace("error: maximum number of remote signals in a connection exceeded.\n");
        return 0;
    }

    mapper_router_signal rs = find_or_add_router_signal(r, sig);

    mapper_connection c = (mapper_connection)
        calloc(1, sizeof(struct _mapper_connection));
    c->new = 1;
    c->ready = 0;
    c->props.direction = direction;
    c->props.dest = (mapper_db_connection_slot_props) calloc(1, sizeof(mapper_db_connection_slot_props));

    // get or request link!
    c->remote_dest = link;

    // TODO: expand to possible multiple local signals
    if (direction == DI_OUTGOING) {
        c->props.num_sources = 1;
        c->local_src = (mapper_router_signal) calloc(1, sizeof(struct _mapper_router_signal *));
        c->local_src[0] = rs;
        c->props.sources = &((mapper_db_connection_slot_props) calloc(1, sizeof(struct _mapper_db_connection_slot_props)));
        c->props.sources[0]->name = rs->signal->props.name;
        c->props.sources[0]->signal_name = rs->signal->props.name;
        c->props.sources[0]->type = sig->props.type;
        c->props.sources[0]->length = sig->props.length;
        c->props.dest->name = strdup(remote_signal_names[0]);
        c->props.dest->free_vars = 1;
    }
    else {
        c->props.num_sources = num_remote_signals;
        c->local_dest = rs;
        c->props.dest->name = sig->props.name;
        c->props.dest->signal_name = sig->props.name;
        c->props.dest->type = sig->props.type;
        c->props.dest->length = sig->props.length;
        mapper_db_connection_slot_props *sources = (mapper_db_connection_slot_props*)calloc(1, sizeof(struct _mapper_db_connection_slot_props) * num_remote_signals);
        c->props.sources = sources;
        for (i = 0; i < num_remote_signals; i++) {
            c->props.sources[i]->name = strdup(remote_signal_names[i]);
            c->props.sources[i]->signal_name = strchr(c->props.sources[i]->name+1, '/');
            c->props.sources[i]->free_vars = 1;
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

    // TODO: move to subconnections init (connected ACK)
//    c->history = malloc(sizeof(struct _mapper_signal_history)
//                        * rs->num_instances);
//    c->expr_vars = calloc(1, sizeof(mapper_signal_history_t*) * rs->num_instances);
//    c->num_expr_vars = 0;
//    for (i=0; i<rs->num_instances; i++) {
//        // allocate history vectors
//        c->history[i].type = remote_type;
//        c->history[i].length = remote_length;
//        c->history[i].size = 1;
//        c->history[i].value = calloc(1, mapper_type_size(c->props.remote_type) *
//                                     c->props.remote_length);
//        c->history[i].timetag = calloc(1, sizeof(mapper_timetag_t));
//        c->history[i].position = -1;
//    }

    // add new connection to this signal's list
    // TODO: could be more than 1 local signal
    c->next = rs->connections;
    rs->connections = c;

    // TODO: only increment num_connections props when connection is "ready"
    if (direction == DI_OUTGOING)
        link->num_connections_out++;
    else
        link->num_connections_in++;

    return c;
}

static void free_connection(mapper_connection c)
{
    // do not free local_name since it points to signal's copy
    int i, j;
    if (c) {
        for (i = 0; i < c->props.num_instances; i++) {
            if (c->num_expr_vars) {
                for (j=0; j<c->num_expr_vars; j++) {
                    free(c->expr_vars[i][j].value);
                    free(c->expr_vars[i][j].timetag);
                }
            }
            free(c->expr_vars[i]);
        }
        for (i = 0; i < c->props.num_sources; i++) {
            if (c->props.sources[i]->minimum)
                free(c->props.sources[i]->minimum);
            if (c->props.sources[i]->maximum)
                free(c->props.sources[i]->maximum);
            // some source info & history might belong to router_signal
            if (c->props.sources[i]->free_vars) {
                if (c->props.sources[i]->name)
                    free(c->props.sources[i]->name);
                for (j = 0; j < c->props.num_instances; j++) {
                    free(c->sources[i]->history[j].value);
                    free(c->sources[i]->history[j].timetag);
                }
                if (c->sources[i]->history)
                    free(c->sources[i]->history);
            }
        }
        free(c->sources);
        if (c->props.dest->name && c->props.dest->free_vars)
            free(c->props.dest->name);
        if (c->props.dest->minimum)
            free(c->props.dest->minimum);
        if (c->props.dest->maximum)
            free(c->props.dest->maximum);
        free(c->props.dest);
        if (c->result.history) {
            for (j = 0; j < c->props.num_instances; j++) {
                free(c->result.history[j].value);
                free(c->result.history[j].timetag);
            }
            free(c->result.history);
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
    }
}

int mapper_router_remove_connection(mapper_router r,
                                    mapper_connection c)
{
    int found = 0, count = 0;
    // TODO: expand to possible multiple local signals
    mapper_router_signal rs;
    if (!(rs = c->local_src[0]))
        rs = c->local_dest;
    if (!rs)
        return -1;
    mapper_connection *temp = &rs->connections;
    while (*temp) {
        if (*temp == c) {
            *temp = c->next;
            if (c->props.direction == DI_OUTGOING)
                c->remote_dest->num_connections_out--;
            else
                c->remote_dest->num_connections_in--;
            free_connection(c);
            found = 1;
            break;
        }
        temp = &(*temp)->next;
    }

    // Count remaining connections
    temp = &rs->connections;
    while (*temp) {
        count++;
        temp = &(*temp)->next;
    }
    if (!count) {
        // We need to remove the router_signal also
        mapper_router_signal *rstemp = &r->signals;
        while (*rstemp) {
            if (*rstemp == rs) {
                *rstemp = rs->next;
                int i;
                for (i=0; i<rs->num_instances; i++) {
                    free(rs->value.history[i].value);
                    free(rs->value.history[i].timetag);
                }
                free(rs->value.history);
                free(rs);
                break;
            }
            rstemp = &(*rstemp)->next;
        }
    }
    return !found;
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
                && strcmp(c->props.dest->name, remote_names[0])==0)
                return c;
        }
        else if (num_remote_signals == c->props.num_sources) {
            int i, found = 1;
            for (i = 0; i < num_remote_signals; i++) {
                if (strcmp(c->props.sources[i]->name, remote_names[i])) {
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
        int temp_slot = slot_number - c->slot_start;
        if (temp_slot >= 0 && temp_slot < c->num_slots) {
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
        if (strncmp(l->remote_name, name, n)==0)
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
