
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <zlib.h>

#include <lo/lo.h>

#include "mapper_internal.h"
#include "types_internal.h"
#include <mapper/mapper.h>

static void mapper_router_send_or_bundle_message(mapper_router router,
                                                 const char *path,
                                                 lo_message m,
                                                 mapper_timetag_t tt);

mapper_router mapper_router_new(mapper_device device, const char *host,
                                int port, const char *name, int default_scope)
{
    char str[16];
    mapper_router r = (mapper_router) calloc(1, sizeof(struct _mapper_link));
    r->props.src_name = strdup(mdev_name(device));
    r->props.dest_host = strdup(host);
    r->props.dest_port = port;
    sprintf(str, "%d", port);
    r->remote_addr = lo_address_new(host, str);
    r->props.dest_name = strdup(name);
    r->props.dest_name_hash = crc32(0L, (const Bytef *)name, strlen(name));
    if (default_scope) {
        r->props.num_scopes = 1;
        r->props.scope_names = (char **) malloc(sizeof(char *));
        r->props.scope_names[0] = strdup(mdev_name(device));
        r->props.scope_hashes = (uint32_t *) malloc(sizeof(uint32_t));
        r->props.scope_hashes[0] = mdev_id(device);
    }
    else {
        r->props.num_scopes = 0;
    }
    r->props.extra = table_new();
    r->device = device;
    r->signals = 0;
    r->n_connections = 0;

    if (!r->remote_addr) {
        mapper_router_free(r);
        return 0;
    }
    return r;
}

void mapper_router_free(mapper_router r)
{
    int i;

    if (r) {
        if (r->props.src_name)
            free(r->props.src_name);
        if (r->props.dest_host)
            free(r->props.dest_host);
        if (r->props.dest_name)
            free(r->props.dest_name);
        if (r->remote_addr)
            lo_address_free(r->remote_addr);
        while (r->signals && r->signals->connections) {
            // router_signal is freed with last child connection
            mapper_router_remove_connection(r, r->signals->connections);
        }
        while (r->queues) {
            mapper_queue q = r->queues;
            lo_bundle_free_messages(q->bundle);
            r->queues = q->next;
            free(q);
        }
        for (i=0; i<r->props.num_scopes; i++) {
            free(r->props.scope_names[i]);
        }
        free(r->props.scope_names);
        free(r->props.scope_hashes);
        if (r->props.extra)
            table_free(r->props.extra, 1);
        free(r);
    }
}

void mapper_router_set_from_message(mapper_router router,
                                    mapper_message_t *msg)
{
    /* Extra properties. */
    mapper_msg_add_or_update_extra_params(router->props.extra, msg);
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
    int i;
    for (i=rs->num_instances; i<size; i++) {
        rs->history[i].type = sig->props.type;
        rs->history[i].length = sig->props.length;
        rs->history[i].size = sig->props.history_size;
        rs->history[i].value = calloc(1, msig_vector_bytes(sig)
                                      * sig->props.history_size);
        rs->history[i].timetag = calloc(1, sizeof(mapper_timetag_t)
                                        * sig->props.history_size);
        rs->history[i].position = -1;
    }

    // reallocate connection instances
    mapper_connection c = rs->connections;
    while (c) {
        c->history = realloc(c->history, sizeof(struct _mapper_signal_history)
                             * size);
        for (i=rs->num_instances; i<size; i++) {
            c->history[i].type = c->props.dest_type;
            c->history[i].length = c->props.dest_length;
            c->history[i].size = c->props.dest_history_size;
            c->history[i].value = calloc(1, mapper_type_size(c->props.dest_type)
                                         * c->props.dest_history_size);
            c->history[i].timetag = calloc(1, sizeof(mapper_timetag_t)
                                           * c->props.dest_history_size);
            c->history[i].position = -1;
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
    int in_scope = mapper_router_in_scope(r, map->origin);

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
        c = rs->connections;
        while (c) {
            c->history[id].position = -1;
            if ((c->props.mode != MO_REVERSE) &&
                (!c->props.send_as_instance || in_scope))
                mapper_router_send_update(r, c, id, c->props.send_as_instance ?
                                          map : 0, tt, 0);

            c = c->next;
        }
        return;
    }

    if (count > 1) {
        // allocate blob for each connection
        c = rs->connections;
        while (c) {
            if ((c->props.mode != MO_REVERSE) &&
                (!c->props.send_as_instance || in_scope))
                c->blob = realloc(c->blob, mapper_type_size(c->props.dest_type)
                                  * c->props.dest_length * count);
            c = c->next;
        }
    }

    int i;
    for (i=0; i<count; i++) {
        // copy input history
        size_t n = msig_vector_bytes(sig);
        rs->history[id].position = (rs->history[id].position + 1)
                                   % rs->history[id].size;
        memcpy(msig_history_value_pointer(rs->history[id]),
               value + n * i, n);
        memcpy(msig_history_tt_pointer(rs->history[id]),
               &tt, sizeof(mapper_timetag_t));

        c = rs->connections;
        while (c) {
            if ((c->props.mode == MO_REVERSE) ||
                (c->props.send_as_instance && !in_scope)) {
                c = c->next;
                continue;
            }
            if (mapper_connection_perform(c, &rs->history[id],
                                          &c->history[id]))
            {
                if (mapper_boundary_perform(c, &c->history[id])) {
                    if (count > 1)
                        memcpy(c->blob + mapper_type_size(c->props.dest_type) *
                               c->props.dest_length * i,
                               msig_history_value_pointer(c->history[id]),
                               mapper_type_size(c->props.dest_type) *
                               c->props.dest_length);
                    else
                        mapper_router_send_update(r, c, id, c->props.send_as_instance ?
                                                  map : 0, tt, 0);
                }
            }
            c = c->next;
        }
    }
    if (count > 1) {
        c = rs->connections;
        while (c) {
            if ((c->props.mode != MO_REVERSE) &&
                (!c->props.send_as_instance || in_scope)) {
                lo_blob blob = lo_blob_new(mapper_type_size(c->props.dest_type)
                                           * c->props.dest_length * count, c->blob);
                mapper_router_send_update(r, c, id, c->props.send_as_instance ?
                                          map : 0, tt, blob);
            }
            c = c->next;
        }
    }
}

/*! Build a value update message for a given connection. */
void mapper_router_send_update(mapper_router r,
                               mapper_connection c,
                               int history_index,
                               mapper_id_map id_map,
                               mapper_timetag_t tt,
                               lo_blob blob)
{
    int i;
    if (!r->remote_addr)
        return;

    lo_message m = lo_message_new();
    if (!m)
        return;

    if (id_map) {
        lo_message_add_int32(m, id_map->origin);
        lo_message_add_int32(m, id_map->public);
    }

    if (c->history[history_index].position != -1) {
        if (blob) {
            lo_message_add_blob(m, blob);
            lo_blob_free(blob);
        }
        else if (c->history[history_index].type == 'f') {
            float *v = msig_history_value_pointer(c->history[history_index]);
            for (i = 0; i < c->history[history_index].length; i++)
                lo_message_add_float(m, v[i]);
        }
        else if (c->history[history_index].type == 'i') {
            int *v = msig_history_value_pointer(c->history[history_index]);
            for (i = 0; i < c->history[history_index].length; i++)
                lo_message_add_int32(m, v[i]);
        }
        else if (c->history[history_index].type == 'd') {
            double *v = msig_history_value_pointer(c->history[history_index]);
            for (i = 0; i < c->history[history_index].length; i++)
                lo_message_add_double(m, v[i]);
        }
    }
    else if (id_map) {
        lo_message_add_nil(m);
    }

    mapper_router_send_or_bundle_message(r, c->props.dest_name, m, tt);
}

int mapper_router_send_query(mapper_router r,
                             mapper_signal sig,
                             mapper_timetag_t tt)
{
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
        mapper_router_send_or_bundle_message(r, c->props.query_name, m, tt);
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
void mapper_router_send_or_bundle_message(mapper_router r,
                                          const char *path,
                                          lo_message m,
                                          mapper_timetag_t tt)
{
    // Check if a matching bundle exists
    mapper_queue q = r->queues;
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
        lo_send_bundle_from(r->remote_addr, r->device->server, b);
        lo_bundle_free_messages(b);
    }
}

void mapper_router_start_queue(mapper_router r,
                               mapper_timetag_t tt)
{
    // first check if queue already exists
    mapper_queue q = r->queues;
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
    q->next = r->queues;
    r->queues = q;
}

void mapper_router_release_queue(mapper_router r,
                                 mapper_queue q)
{
    mapper_queue *temp = &r->queues;
    while (*temp) {
        if (*temp == q) {
            *temp = q->next;
            free(q);
            return;
        }
        temp = &(*temp)->next;
    }
}

void mapper_router_send_queue(mapper_router r,
                              mapper_timetag_t tt)
{
    mapper_queue q = r->queues;
    while (q) {
        if (memcmp(&q->tt, &tt, sizeof(mapper_timetag_t))==0)
            break;
        q = q->next;
    }
    if (q) {
#ifdef HAVE_LIBLO_BUNDLE_COUNT
        if (lo_bundle_count(q->bundle))
#endif
            lo_send_bundle_from(r->remote_addr,
                                r->device->server, q->bundle);
        lo_bundle_free_messages(q->bundle);
        mapper_router_release_queue(r, q);
    }
}

mapper_connection mapper_router_add_connection(mapper_router r,
                                               mapper_signal sig,
                                               const char *dest_name,
                                               char dest_type,
                                               int dest_length)
{
    /* Currently, fail if lengths don't match.  TODO: In the future,
     * we'll have to examine the expression to see if its input and
     * output lengths are compatible. */
    if (sig->props.length != dest_length) {
        char n[1024];
        msig_full_name(sig, n, 1024);
        trace("rejecting connection %s -> %s%s because lengths "
              "don't match (not yet supported)\n",
              n, r->props.dest_name, dest_name);
        return 0;
    }

    // find signal in router_signal list
    mapper_router_signal rs = r->signals;
    while (rs && rs->signal != sig)
        rs = rs->next;

    // if not found, create a new list entry
    if (!rs) {
        rs = (mapper_router_signal)
            calloc(1, sizeof(struct _mapper_link_signal));
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

    c->props.src_name = strdup(sig->props.name);
    c->props.src_type = sig->props.type;
    c->props.src_length = sig->props.length;
    c->props.dest_name = strdup(dest_name);
    c->props.dest_type = dest_type;
    c->props.dest_length = dest_length;
    c->props.mode = MO_UNDEFINED;
    c->props.expression = strdup("y=x");
    c->props.bound_min = BA_NONE;
    c->props.bound_max = BA_NONE;
    c->props.muted = 0;
    c->props.send_as_instance = (rs->num_instances > 1);
    c->props.extra = table_new();

    int len = strlen(dest_name) + 5;
    c->props.query_name = malloc(len);
    snprintf(c->props.query_name, len, "%s%s", dest_name, "/get");

    c->history = malloc(sizeof(struct _mapper_signal_history)
                        * rs->num_instances);
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
    r->n_connections++;

    return c;
}

static void mapper_router_free_connection(mapper_router r,
                                          mapper_connection c)
{
    int i;
    if (r && c) {
        if (c->props.src_name)
            free(c->props.src_name);
        if (c->props.dest_name)
            free(c->props.dest_name);
        if (c->expr)
            mapper_expr_free(c->expr);
        if (c->props.expression)
            free(c->props.expression);
        if (c->props.query_name)
            free(c->props.query_name);
        table_free(c->props.extra, 1);
        for (i=0; i<c->parent->num_instances; i++) {
            free(c->history[i].value);
            free(c->history[i].timetag);
        }
        if (c->history)
            free(c->history);
        if (c->blob)
            free(c->blob);
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
            mapper_router_free_connection(r, c);
            r->n_connections--;
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
                for (i=0; i < rs->num_instances; i++) {
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

mapper_connection mapper_router_find_connection_by_names(mapper_router rt,
                                                         const char* src_name,
                                                         const char* dest_name)
{
    // find associated router_signal
    mapper_router_signal rs = rt->signals;
    while (rs && strcmp(rs->signal->props.name, src_name) != 0)
        rs = rs->next;
    if (!rs)
        return NULL;

    // find associated connection
    mapper_connection c = rs->connections;
    while (c && strcmp(c->props.dest_name, dest_name) != 0)
        c = c->next;
    if (!c)
        return NULL;
    else
        return c;
}

int mapper_router_add_scope(mapper_router router, const char *scope)
{
    return mapper_db_link_add_scope(&router->props, scope);
}

int mapper_router_remove_scope(mapper_router router, const char *scope)
{
    return mapper_db_link_remove_scope(&router->props, scope);

    /* Here we could release mapped signal instances with this scope,
     * but we will let the receiver-side handle it instead. */
}

int mapper_router_in_scope(mapper_router router, uint32_t name_hash)
{
    int i;
    for (i=0; i<router->props.num_scopes; i++)
        if (router->props.scope_hashes[i] == name_hash ||
            router->props.scope_hashes[i] == 0)
            return 1;
    return 0;
}

mapper_router mapper_router_find_by_dest_address(mapper_router r,
                                                 const char *host,
                                                 int port)
{
    while (r) {
        if (r->props.dest_port == port && (strcmp(r->props.dest_host, host)==0))
            return r;
        r = r->next;
    }
    return 0;
}

mapper_router mapper_router_find_by_dest_name(mapper_router router,
                                              const char *dest_name)
{
    int n = strlen(dest_name);
    const char *slash = strchr(dest_name+1, '/');
    if (slash)
        n = slash - dest_name;

    while (router) {
        if (strncmp(router->props.dest_name, dest_name, n)==0)
            return router;
        router = router->next;
    }
    return 0;
}
