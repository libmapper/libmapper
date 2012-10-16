
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
                                int port, const char *name, int local)
{
    char str[16];
    mapper_router router = (mapper_router) calloc(1, sizeof(struct _mapper_router));
    sprintf(str, "%d", port);
    router->props.dest_addr = lo_address_new(host, str);
    router->props.dest_name = strdup(name);
    if (local) {
        router->props.num_scopes = 1;
        router->props.scope_names = (char **) malloc(sizeof(char *));
        router->props.scope_names[0] = strdup(mdev_name(device));
        router->props.scope_hashes = (int *) malloc(sizeof(int));
        router->props.scope_hashes[0] = mdev_id(device);
    }
    else {
        router->props.num_scopes = 0;
    }
    router->props.extra = table_new();
    router->device = device;
    router->signals = 0;
    router->n_connections_out = 0;

    if (!router->props.dest_addr) {
        mapper_router_free(router);
        return 0;
    }
    return router;
}

void mapper_router_free(mapper_router router)
{
    if (router) {
        if (router->props.dest_addr)
            lo_address_free(router->props.dest_addr);
        if (router->signals) {
            mapper_router_signal rs = router->signals;
            while (rs) {
                mapper_router_signal tmp = rs->next;
                if (rs->connections) {
                    mapper_connection c = rs->connections;
                    while (c) {
                        mapper_connection tmp = c->next;
                        if (tmp->props.src_name)
                            free(tmp->props.src_name);
                        if (tmp->props.dest_name)
                            free(tmp->props.dest_name);
                        free(c);
                        c = tmp;
                    }
                }
                int i;
                for (i=0; i<rs->num_instances; i++) {
                    free(rs->history[i].value);
                    free(rs->history[i].timetag);
                }
                free(rs->history);
                free(rs);
                rs = tmp;
            }
        }
        free(router);
    }
}

void mapper_router_set_from_message(mapper_router router,
                                    mapper_message_t *msg)
{
    /* Extra properties. */
    mapper_msg_add_or_update_extra_params(router->props.extra, msg);
}

void mapper_router_num_instances_changed(mapper_router r,
                                         mapper_signal sig)
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

    if (sig->props.num_instances <= rs->num_instances)
        return;

    // Need to allocate more instances
    rs->history = realloc(rs->history, sizeof(struct _mapper_signal_history)
                          * sig->props.num_instances);
    int i;
    for (i=rs->num_instances; i<sig->props.num_instances; i++) {
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
                             * sig->props.num_instances);
        for (i=rs->num_instances; i<sig->props.num_instances; i++) {
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

    rs->num_instances = sig->props.num_instances;
}

void mapper_router_process_signal(mapper_router r,
                                  mapper_signal sig,
                                  mapper_signal_instance si,
                                  void *value,
                                  int count,
                                  mapper_timetag_t tt,
                                  int flags)
{
    int send_as_instance = (flags & FLAGS_SEND_AS_INSTANCE);
    if (send_as_instance && !mapper_router_in_scope(r, si->id_map->group))
        return;

    // find the signal connection
    mapper_router_signal rs = r->signals;
    while (rs) {
        if (rs->signal == sig)
            break;
        rs = rs->next;
    }
    if (!rs)
        return;

    int id = si->id;
    mapper_connection c;

    if (!value) {
        rs->history[id].position = -1;
        c = rs->connections;
        while (c) {
            c->history[id].position = -1;
            mapper_router_send_update(r, c, id, send_as_instance ?
                                      si->id_map : 0, tt, 0);
            c = c->next;
        }
        return;
    }

    if (count > 1) {
        // allocate blob for each connection
        c = rs->connections;
        while (c) {
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
            if (mapper_connection_perform(c, &rs->history[id],
                                          &c->history[id]))
            {
                if (mapper_clipping_perform(c, &c->history[id])) {
                    if (send_as_instance && (flags & FLAGS_IS_NEW_INSTANCE))
                        mapper_router_send_new_instance(r, c, id,
                                                        send_as_instance ?
                                                        si->id_map : 0, tt);
                    if (count > 1)
                        memcpy(c->blob + mapper_type_size(c->props.dest_type) *
                               c->props.dest_length * i,
                               msig_history_value_pointer(c->history[id]),
                               mapper_type_size(c->props.dest_type) *
                               c->props.dest_length);
                    else
                        mapper_router_send_update(r, c, id, send_as_instance ?
                                                  si->id_map : 0, tt, 0);
                }
            }
            c = c->next;
        }
    }
    if (count > 1) {
        c = rs->connections;
        while (c) {
            lo_blob blob = lo_blob_new(mapper_type_size(c->props.dest_type)
                                       * c->props.dest_length * count, c->blob);
            mapper_router_send_update(r, c, id, send_as_instance ?
                                      si->id_map : 0, tt, blob);
            c = c->next;
        }
    }
}

/*! Build a value update message for a given connection. */
void mapper_router_send_update(mapper_router r,
                               mapper_connection c,
                               int index,
                               mapper_instance_id_map id_map,
                               mapper_timetag_t tt,
                               lo_blob blob)
{
    int i;
    if (!r->props.dest_addr)
        return;

    lo_message m = lo_message_new();
    if (!m)
        return;

    if (id_map) {
        lo_message_add_int32(m, id_map->group);
        lo_message_add_int32(m, id_map->remote);
    }

    if (c->history[index].position != -1) {
        if (blob) {
            lo_message_add_blob(m, blob);
        }
        else if (c->history[index].type == 'f') {
            float *v = msig_history_value_pointer(c->history[index]);
            for (i = 0; i < c->history[index].length; i++)
                lo_message_add_float(m, v[i]);
        }
        else if (c->history[index].type == 'i') {
            int *v = msig_history_value_pointer(c->history[index]);
            for (i = 0; i < c->history[index].length; i++)
                lo_message_add_int32(m, v[i]);
        }
        else if (c->history[index].type == 'd') {
            double *v = msig_history_value_pointer(c->history[index]);
            for (i = 0; i < c->history[index].length; i++)
                lo_message_add_double(m, v[i]);
        }
    }
    else if (mdev_id(r->device) == id_map->group) {
        // If instance is locally owned, send instance release...
        lo_message_add_nil(m);
    }
    else {
        // ...otherwise send release request.
        lo_message_add_false(m);
    }

    mapper_router_send_or_bundle_message(r, c->props.dest_name, m, tt);
}

void mapper_router_send_new_instance(mapper_router r,
                                     mapper_connection c,
                                     int index,
                                     mapper_instance_id_map id_map,
                                     mapper_timetag_t tt)
{
    lo_message m = lo_message_new();
    if (!m)
        return;

    lo_message_add_int32(m, id_map->group);
    lo_message_add_int32(m, id_map->remote);
    lo_message_add_true(m);

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
    int count = 0, query_len = 0;
    char *query_string = 0;
    int response_len = (int) strlen(sig->props.name) + 5;
    char *response_string = (char*) malloc(response_len);
    snprintf(response_string, response_len, "%s%s", sig->props.name, "/got");
    while (c) {
        query_len = (int) strlen(c->props.dest_name) + 5;
        query_string = (char*) realloc(query_string, query_len);
        snprintf(query_string, query_len, "%s%s", c->props.dest_name, "/get");
        lo_message m = lo_message_new();
        if (!m)
            continue;
        lo_message_add_string(m, response_string);
        mapper_router_send_or_bundle_message(r, query_string, m, tt);
        count++;
        c = c->next;
    }
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
    mapper_router_queue q = r->queues;
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
        //lo_bundle b = lo_bundle_new(tt);
        lo_bundle b = lo_bundle_new(LO_TT_IMMEDIATE);
        lo_bundle_add_message(b, path, m);
        lo_send_bundle_from(r->props.dest_addr, r->device->server, b);
        lo_bundle_free_messages(b);
    }
}

void mapper_router_start_queue(mapper_router r,
                               mapper_timetag_t tt)
{
    // first check if queue already exists
    mapper_router_queue q = r->queues;
    while (q) {
        if (memcmp(&q->tt, &tt,
                   sizeof(mapper_timetag_t))==0)
            return;
        q = q->next;
    }

    // need to create new queue
    q = malloc(sizeof(struct _mapper_router_queue));
    memcpy(&q->tt, &tt, sizeof(mapper_timetag_t));
    //q->bundle = lo_bundle_new(tt);
    q->bundle = lo_bundle_new(LO_TT_IMMEDIATE);
    q->next = r->queues;
    r->queues = q;
}

void mapper_router_release_queue(mapper_router r,
                                 mapper_router_queue q)
{
    mapper_router_queue *temp = &r->queues;
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
    mapper_router_queue q = r->queues;
    while (q) {
        if (memcmp(&q->tt, &tt, sizeof(mapper_timetag_t))==0)
            break;
        q = q->next;
    }
    if (q) {
        lo_send_bundle_from(r->props.dest_addr, r->device->server, q->bundle);
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

    // find signal in signal connection list
    mapper_router_signal rs = r->signals;
    while (rs && rs->signal != sig)
        rs = rs->next;
    
    // if not found, create a new list entry
    if (!rs) {
        rs = (mapper_router_signal)
            calloc(1, sizeof(struct _mapper_router_signal));
        rs->signal = sig;
        rs->history = malloc(sizeof(struct _mapper_signal_history)
                             * sig->props.num_instances);
        int i;
        for (i=0; i<sig->props.num_instances; i++) {
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
    c->props.clip_min = CT_NONE;
    c->props.clip_max = CT_NONE;
    c->props.muted = 0;
    c->props.extra = table_new();

    c->history = malloc(sizeof(struct _mapper_signal_history)
                        * sig->props.num_instances);
    int i;
    for (i=0; i<sig->props.num_instances; i++) {
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
    r->n_connections_out++;

    return c;
}

int mapper_router_remove_connection(mapper_router r,
                                    mapper_connection c)
{
    mapper_connection *temp = &c->parent->connections;
    while (*temp) {
        if (*temp == c) {
            *temp = c->next;
            int i;
            for (i=0; i<c->parent->num_instances; i++) {
                free(c->history[i].value);
                free(c->history[i].timetag);
            }
            if (c->history)
                free(c->history);
            if (c->blob)
                free(c->blob);
            free(c);
            r->n_connections_out--;
            return 0;
        }
        temp = &(*temp)->next;
    }
    return 1;
}

int mapper_router_add_scope(mapper_router router, const char *scope)
{
    if (!scope)
        return 1;
    // Check if scope is already stored for this router
    int i, hash = crc32(0L, (const Bytef *)scope, strlen(scope));
    mapper_db_link props = &router->props;
    for (i=0; i<props->num_scopes; i++)
        if (props->scope_hashes[i] == hash)
            return 1;
    // not found - add a new scope
    i = ++props->num_scopes;
    props->scope_names = realloc(props->scope_names, i * sizeof(char *));
    props->scope_names[i-1] = strdup(scope);
    props->scope_hashes = realloc(props->scope_hashes, i * sizeof(int));
    props->scope_hashes[i-1] = hash;
    return 0;
}

void mapper_router_remove_scope(mapper_router router, const char *scope)
{
    if (!scope)
        return;
    int i, j, hash = crc32(0L, (const Bytef *)scope, strlen(scope));
    mapper_db_link props = &router->props;
    for (i=0; i<props->num_scopes; i++) {
        if (props->scope_hashes[i] == hash) {
            free(props->scope_names[i]);
            for (j=i+1; j<props->num_scopes; j++) {
                props->scope_names[j-1] = props->scope_names[j];
                props->scope_hashes[j-1] = props->scope_hashes[j];
            }
            props->num_scopes--;
            props->scope_names = realloc(props->scope_names,
                                         props->num_scopes * sizeof(char *));
            props->scope_hashes = realloc(props->scope_hashes,
                                          props->num_scopes * sizeof(int));
            return;
        }
    }
}

int mapper_router_in_scope(mapper_router router, int id)
{
    int i;
    for (i=0; i<router->props.num_scopes; i++)
        if (router->props.scope_hashes[i] == id)
            return 1;
    return 0;
}

mapper_router mapper_router_find_by_dest_address(mapper_router router,
                                                 lo_address dest_addr)
{
    const char *host_to_match = lo_address_get_hostname(dest_addr);
    const char *port_to_match = lo_address_get_port(dest_addr);

    while (router) {
        const char *host = lo_address_get_hostname(router->props.dest_addr);
        const char *port = lo_address_get_port(router->props.dest_addr);
        if ((strcmp(host, host_to_match)==0) && (strcmp(port, port_to_match)==0))
            return router;
        router = router->next;
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
