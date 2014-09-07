
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

static int in_incoming_link_scope(mapper_link link, uint32_t name_hash)
{
    int i;
    for (i=0; i<link->props.incoming_scopes.size; i++) {
        if (link->props.incoming_scopes.hashes[i] == name_hash ||
            link->props.incoming_scopes.hashes[i] == 0)
            return 1;
    }
    return 0;
}

static int in_outgoing_link_scope(mapper_link link, uint32_t name_hash)
{
    int i;
    for (i=0; i<link->props.outgoing_scopes.size; i++) {
        if (link->props.outgoing_scopes.hashes[i] == name_hash ||
            link->props.outgoing_scopes.hashes[i] == 0)
            return 1;
    }
    return 0;
}


static void mapper_link_free(mapper_link l)
{
    int i;

    if (l) {
        if (l->props.local_name)
            free(l->props.local_name);
        if (l->props.remote_host)
            free(l->props.remote_host);
        if (l->props.remote_name)
            free(l->props.remote_name);
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
        for (i=0; i<l->props.outgoing_scopes.size; i++) {
            free(l->props.outgoing_scopes.names[i]);
        }
        free(l->props.outgoing_scopes.names);
        free(l->props.outgoing_scopes.hashes);
        for (i=0; i<l->props.incoming_scopes.size; i++) {
            free(l->props.incoming_scopes.names[i]);
        }
        free(l->props.incoming_scopes.names);
        free(l->props.incoming_scopes.hashes);
        if (l->props.extra)
            table_free(l->props.extra, 1);
        free(l);
    }
}

mapper_link mapper_router_add_link(mapper_router router, const char *host,
                                   int admin_port, int data_port,
                                   const char *name)
{
    char str[16];
    mapper_link l = (mapper_link) calloc(1, sizeof(struct _mapper_link));
    l->props.local_name = strdup(mdev_name(router->device));
    l->props.remote_host = strdup(host);
    l->props.remote_port = data_port;
    sprintf(str, "%d", data_port);
    l->data_addr = lo_address_new(host, str);
    sprintf(str, "%d", admin_port);
    l->admin_addr = lo_address_new(host, str);
    l->props.remote_name = strdup(name);
    l->props.remote_name_hash = crc32(0L, (const Bytef *)name, strlen(name));

    // outgoing scopes
    l->props.outgoing_scopes.size = 1;
    l->props.outgoing_scopes.names = (char **) malloc(sizeof(char *));
    l->props.outgoing_scopes.names[0] = strdup(mdev_name(router->device));
    l->props.outgoing_scopes.hashes = (uint32_t *) malloc(sizeof(uint32_t));
    l->props.outgoing_scopes.hashes[0] = mdev_id(router->device);

    // incoming scopes
    l->props.incoming_scopes.size = 1;
    l->props.incoming_scopes.names = (char **) malloc(sizeof(char *));
    l->props.incoming_scopes.names[0] = strdup(name);
    l->props.incoming_scopes.hashes = (uint32_t *) malloc(sizeof(uint32_t));
    l->props.incoming_scopes.hashes[0] = l->props.dest_name_hash;

    l->props.extra = table_new();
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
    router->device->props.num_links++;

    return l;
}

static void remove_link_signals(mapper_router router, mapper_link link)
{
    mapper_router_signal rs = router->signals;
    while (rs) {
        mapper_connection c = rs->connections;
        rs = rs->next;
        while (c) {
            if (c->link == link) {
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
            router->device->props.num_links--;
            break;
        }
        links = &(*links)->next;
    }
}

int mapper_router_set_link_from_message(mapper_router r, mapper_link l,
                                        mapper_message_t *msg, int swap)
{
    int updated = mapper_msg_add_or_update_extra_params(l->props.extra, msg);

    lo_arg **a_scopes = mapper_msg_get_param(msg, AT_SCOPE);
    int num_scopes = mapper_msg_get_length(msg, AT_SCOPE);
    mapper_db_link_update_scopes(swap ? &l->props.dest_scopes : &l->props.src_scopes,
                                 a_scopes, num_scopes);

    a_scopes = mapper_msg_get_param(msg, AT_SRC_SCOPE);
    num_scopes = mapper_msg_get_length(msg, AT_SRC_SCOPE);
    mapper_db_link_update_scopes(swap ? &l->props.dest_scopes : &l->props.src_scopes,
                                 a_scopes, num_scopes);

    a_scopes = mapper_msg_get_param(msg, AT_DEST_SCOPE);
    num_scopes = mapper_msg_get_length(msg, AT_DEST_SCOPE);
    mapper_db_link_update_scopes(swap ? &l->props.src_scopes : &l->props.dest_scopes,
                                 a_scopes, num_scopes);

    return updated;
}

void mapper_router_num_instances_changed(mapper_router r,
                                         mapper_signal sig,
                                         int size)
{
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
        c->history = realloc(c->history, sizeof(struct _mapper_signal_history)
                             * size);
        c->expr_vars = realloc(c->expr_vars, sizeof(mapper_signal_history_t*)
                               * size);
        for (i=rs->num_instances; i<size; i++) {
            c->history[i].type = c->props.dest_type;
            c->history[i].length = c->props.dest_length;
            c->history[i].size = c->output_history_size;
            c->history[i].value = calloc(1, mapper_type_size(c->props.dest_type)
                                         * c->output_history_size);
            c->history[i].timetag = calloc(1, sizeof(mapper_timetag_t)
                                           * c->output_history_size);
            c->history[i].position = -1;

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
        rs->history[id].position = -1;
        // reset associated input memory for this instance
        memset(rs->history[id].value, 0, rs->history_size *
               msig_vector_bytes(rs->signal));
        memset(rs->history[id].timetag, 0, rs->history_size *
               sizeof(mapper_timetag_t));
        c = rs->connections;
        while (c) {
            c->history[id].position = -1;
            if (c->direction == DI_OUTGOING) {
                if (!c->props.send_as_instance
                    || in_outgoing_link_scope(c->link, map->origin)) {
                    m = mapper_router_build_message(&c->props, 0, c->props.dest_length,
                                                    0, map);
                    if (m)
                        send_or_bundle_message(c->link, c->props.dest_name, m, tt);
                    // also need to reset associated output memory
                    memset(c->history[id].value, 0, c->output_history_size *
                           c->props.dest_length * mapper_type_size(c->props.dest_type));
                    memset(c->history[id].timetag, 0, c->output_history_size *
                           sizeof(mapper_timetag_t));
                }
            }
            else if (c->props.send_as_instance
                     && in_incoming_link_scope(c->link, map->origin)) {
                // send release to upstream
                m = mapper_router_build_message(&c->props, 0, c->props.dest_length,
                                                0, map);
                if (m)
                    send_or_bundle_message(c->link, c->props.dest_name, m, tt);
            }
            // also need to reset associated output memory
            memset(c->history[id].value, 0, c->output_history_size *
                   c->props.dest_length * mapper_type_size(c->props.dest_type));
            memset(c->history[id].timetag, 0, c->output_history_size *
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
        int out_scope = in_outgoing_link_scope(c->link, map->origin);
        if ((c->direction == DI_INCOMING)
            || (c->props.send_as_instance && !out_scope)) {
            c = c->next;
            continue;
        }
        char typestring[c->props.dest_length * count];
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

            // handle cases in which part of count update does not cause output
            if (!(mapper_connection_perform(c, &rs->history[id],
                                            &c->expr_vars[id],
                                            &c->history[id],
                                            typestring+c->props.dest_length*j)))
                continue;
            if (!(mapper_boundary_perform(c, &c->history[id])))
                continue;

            if (count > 1) {
                memcpy((char*)out_value_p + mapper_type_size(c->props.dest_type) *
                       c->props.dest_length * i,
                       msig_history_value_pointer(c->history[id]),
                       mapper_type_size(c->props.dest_type) *
                       c->props.dest_length);
            }
            else {
                m = mapper_router_build_message(&c->props,
                                                msig_history_value_pointer(c->history[id]),
                                                c->props.dest_length,
                                                typestring, map);
                if (m)
                    send_or_bundle_message(c->link, c->props.dest_name, m, tt);
            }
            j++;
        }
        if (count > 1 && (!c->props.send_as_instance || out_scope)) {
            m = mapper_router_build_message(&c->props, out_value_p,
                                            c->props.dest_length * j,
                                            typestring, map);
            if (m)
                send_or_bundle_message(c->link, c->props.dest_name, m, tt);
        }
        c = c->next;
    }
}

/*! Build a value update message for a given connection. */
lo_message mapper_router_build_message(mapper_db_connection props, void *value,
                                       int length, char *typestring,
                                       mapper_id_map id_map)
{
    int i;

    lo_message m = lo_message_new();
    if (!m)
        return 0;

    if (value && typestring) {
        if (props->dest_type == 'f') {
            float *v = (float*)value;
            for (i = 0; i < length; i++) {
                if (typestring[i] == 'N')
                    lo_message_add_nil(m);
                else
                    lo_message_add_float(m, v[i]);
            }
        }
        else if (props->dest_type == 'i') {
            int *v = (int*)value;
            for (i = 0; i < length; i++) {
                if (typestring[i] == 'N')
                    lo_message_add_nil(m);
                else
                    lo_message_add_int32(m, v[i]);
            }
        }
        else if (props->dest_type == 'd') {
            double *v = (double*)value;
            for (i = 0; i < length; i++) {
                if (typestring[i] == 'N')
                    lo_message_add_nil(m);
                else
                    lo_message_add_double(m, v[i]);
            }
        }
    }
    else if (id_map) {
        for (i = 0; i < length; i++)
            lo_message_add_nil(m);
    }

    if (props->send_as_instance && id_map) {
        lo_message_add_string(m, "@instance");
        lo_message_add_int32(m, id_map->origin);
        lo_message_add_int32(m, id_map->public);
    }

    // add slot
    lo_message_add_string(m, "@slot");
    lo_message_add_int32(m, props->slot);

    return m;
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
    int response_len = (int) strlen(sig->props.name) + 5;
    char *response_string = (char*) malloc(response_len);
    snprintf(response_string, response_len, "%s%s", sig->props.name, "/got");
    while (c) {
        lo_message m = lo_message_new();
        if (!m)
            continue;
        lo_message_add_string(m, response_string);
        lo_message_add_int32(m, sig->props.length);
        // include response address as argument to allow TCP queries?
        // TODO: always use TCP for queries?
        lo_message_add_char(m, sig->props.type);
        send_or_bundle_message(c->link, c->props.query_name, m, tt);
        count++;
        c = c->next;
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

mapper_connection mapper_router_add_connection(mapper_router r,
                                               mapper_link link,
                                               mapper_signal sig,
                                               const char *dest_name,
                                               char dest_type,
                                               int dest_length,
                                               int direction)
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

    mapper_connection c = (mapper_connection)
        calloc(1, sizeof(struct _mapper_connection));
    c->new = 1;
    c->direction = DI_OUTGOING;
    c->link = link;

    c->props.src_name = strdup(sig->props.name);
    c->props.src_type = sig->props.type;
    c->props.src_length = sig->props.length;
    c->props.dest_name = strdup(dest_name);
    c->props.dest_type = dest_type;
    c->props.dest_length = dest_length;
    c->props.mode = MO_UNDEFINED;
    c->props.expression = 0;
    c->props.bound_min = BA_NONE;
    c->props.bound_max = BA_NONE;
    c->props.muted = 0;
    c->props.send_as_instance = (rs->num_instances > 1);

    if (direction == DI_INCOMING)
        mapper_connection_swap_direction(c);

    c->props.src_min = 0;
    c->props.src_max = 0;
    c->props.dest_min = 0;
    c->props.dest_max = 0;
    c->props.range_known = 0;

    c->props.extra = table_new();

    int len = strlen(dest_name) + 5;
    c->props.query_name = malloc(len);
    // TODO: handle queries in regular signal handler?
    snprintf(c->props.query_name, len, "%s%s", dest_name, "/get");

    c->history = malloc(sizeof(struct _mapper_signal_history)
                        * rs->num_instances);
    c->expr_vars = calloc(1, sizeof(mapper_signal_history_t*) * rs->num_instances);
    c->num_expr_vars = 0;
    int i;
    for (i=0; i<rs->num_instances; i++) {
        // allocate history vectors
        c->history[i].type = dest_type;
        c->history[i].length = dest_length;
        c->history[i].size = 1;
        c->history[i].value = calloc(1, mapper_type_size(c->props.dest_type) *
                                     c->props.dest_length);
        c->history[i].timetag = calloc(1, sizeof(mapper_timetag_t));
        c->history[i].position = -1;
    }

    // add new connection to this signal's list
    c->next = rs->connections;
    rs->connections = c;
    c->parent = rs;
    if (direction == DI_OUTGOING)
        link->num_connections_out++;
    else
        link->num_connections_in++;

    return c;
}

static void free_connection(mapper_connection c)
{
    int i, j;
    if (c) {
        if (c->props.src_name)
            free(c->props.src_name);
        if (c->props.dest_name)
            free(c->props.dest_name);
        if (c->props.query_name)
            free(c->props.query_name);
        if (c->props.src_min)
            free(c->props.src_min);
        if (c->props.src_max)
            free(c->props.src_max);
        if (c->props.dest_min)
            free(c->props.dest_min);
        if (c->props.dest_max)
            free(c->props.dest_max);
        table_free(c->props.extra, 1);
        for (i=0; i<c->parent->num_instances; i++) {
            free(c->history[i].value);
            free(c->history[i].timetag);
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
        if (c->history)
            free(c->history);
        if (c->expr)
            mapper_expr_free(c->expr);
        if (c->props.expression)
            free(c->props.expression);
        free(c);
    }
}

int mapper_router_remove_connection(mapper_router r,
                                    mapper_connection c)
{
    int found = 0, count = 0;
    mapper_router_signal rs = c->parent;
    mapper_connection *temp = &c->parent->connections;
    while (*temp) {
        if (*temp == c) {
            *temp = c->next;
            if (c->direction == DI_OUTGOING)
                c->link->num_connections_out--;
            else
                c->link->num_connections_in--;
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
                    free(rs->history[i].value);
                    free(rs->history[i].timetag);
                }
                free(rs->history);
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
                                                const char* remote_signal_name)
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
        const char *compare = (c->direction == DI_OUTGOING
                               ? c->props.dest_name : c->props.src_name);
        if (strcmp(compare, remote_signal_name) == 0)
            break;
        c = c->next;
    }
    return c;
}

mapper_link mapper_router_find_link_by_remote_address(mapper_router r,
                                                      const char *host,
                                                      int port)
{
    mapper_link l = r->links;
    while (l) {
        if (l->props.dest_port == port && (strcmp(l->props.dest_host, host)==0))
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
        if (strncmp(l->props.dest_name, name, n)==0)
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
        if (name_hash == l->props.dest_name_hash)
            return l;
        l = l->next;
    }
    return 0;
}
