
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

static int in_connection_scope(mapper_connection c, uint32_t name_hash)
{
    int i;
    for (i=0; i<c->props.scope.size; i++) {
        if (   c->props.scope.hashes[i] == name_hash
            || c->props.scope.hashes[i] == 0)
            return 1;
    }
    return 0;
}

mapper_router mapper_router_new(mapper_device device, const char *host,
                                int admin_port, int data_port,
                                const char *name)
{
    char str[16];
    mapper_router r = (mapper_router) calloc(1, sizeof(struct _mapper_link));
    r->props.src_name = strdup(mdev_name(device));
    r->props.dest_host = strdup(host);
    r->props.dest_port = data_port;
    sprintf(str, "%d", data_port);
    r->data_addr = lo_address_new(host, str);
    sprintf(str, "%d", admin_port);
    r->admin_addr = lo_address_new(host, str);
    r->props.dest_name = strdup(name);
    r->props.dest_name_hash = crc32(0L, (const Bytef *)name, strlen(name));

    r->props.extra = table_new();
    r->device = device;
    r->signals = 0;
    r->num_connections = 0;

    r->clock.new = 1;
    r->clock.sent.message_id = 0;
    r->clock.response.message_id = -1;

    if (!r->data_addr) {
        mapper_router_free(r);
        return 0;
    }
    return r;
}

void mapper_router_free(mapper_router r)
{
    if (r) {
        if (r->props.src_name)
            free(r->props.src_name);
        if (r->props.dest_host)
            free(r->props.dest_host);
        if (r->props.dest_name)
            free(r->props.dest_name);
        if (r->admin_addr)
            lo_address_free(r->admin_addr);
        if (r->data_addr)
            lo_address_free(r->data_addr);
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
        if (r->props.extra)
            table_free(r->props.extra, 1);
        free(r);
    }
}

int mapper_router_set_from_message(mapper_router r, mapper_message_t *msg)
{
    return mapper_msg_add_or_update_extra_params(r->props.extra, msg);
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
            c->history[i].size = c->props.dest_history_size;
            c->history[i].value = calloc(1, mapper_type_size(c->props.dest_type)
                                         * c->props.dest_history_size);
            c->history[i].timetag = calloc(1, sizeof(mapper_timetag_t)
                                           * c->props.dest_history_size);
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
            if ((c->props.mode != MO_REVERSE) &&
                (!c->props.send_as_instance || in_connection_scope(c, map->origin))) {
                m = mapper_router_build_message(0, c->props.dest_length,
                                                c->props.dest_type, 0,
                                                c->props.send_as_instance ? map : 0);
                if (m)
                    mapper_router_send_or_bundle_message(r, c->props.dest_name,
                                                         m, tt);
            }
            // also need to reset associated output memory
            memset(c->history[id].value, 0, c->props.dest_history_size *
                   c->props.dest_length * mapper_type_size(c->props.dest_type));
            memset(c->history[id].timetag, 0, c->props.dest_history_size *
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
        if ((c->props.mode == MO_REVERSE)
            || (c->props.send_as_instance && !in_scope)) {
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
                m = mapper_router_build_message(msig_history_value_pointer(c->history[id]),
                                                c->props.dest_length,
                                                c->props.dest_type,
                                                typestring,
                                                c->props.send_as_instance ? map : 0);
                if (m)
                    mapper_router_send_or_bundle_message(r, c->props.dest_name, m, tt);
            }
            j++;
        }
        if (count > 1 && (c->props.mode != MO_REVERSE) &&
            (!c->props.send_as_instance || in_scope)) {
            m = mapper_router_build_message(out_value_p,
                                            c->props.dest_length * j,
                                            c->props.dest_type,
                                            typestring,
                                            c->props.send_as_instance ? map : 0);
            if (m)
                mapper_router_send_or_bundle_message(r, c->props.dest_name,
                                                     m, tt);
        }
        c = c->next;
    }
}

/*! Build a value update message for a given connection. */
lo_message mapper_router_build_message(void *value, int length, char type,
                                       char *typestring, mapper_id_map id_map)
{
    int i;

    lo_message m = lo_message_new();
    if (!m)
        return 0;

    if (value && typestring) {
        if (type == 'f') {
            float *v = (float*)value;
            for (i = 0; i < length; i++) {
                if (typestring[i] == 'N')
                    lo_message_add_nil(m);
                else
                    lo_message_add_float(m, v[i]);
            }
        }
        else if (type == 'i') {
            int *v = (int*)value;
            for (i = 0; i < length; i++) {
                if (typestring[i] == 'N')
                    lo_message_add_nil(m);
                else
                    lo_message_add_int32(m, v[i]);
            }
        }
        else if (type == 'd') {
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

    if (id_map) {
        lo_message_add_string(m, "@instance");
        lo_message_add_int32(m, id_map->origin);
        lo_message_add_int32(m, id_map->public);
    }
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
        lo_send_bundle_from(r->data_addr, r->device->server, b);
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
            lo_send_bundle_from(r->data_addr,
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
    c->props.expression = 0;
    c->props.bound_min = BA_NONE;
    c->props.bound_max = BA_NONE;
    c->props.muted = 0;
    c->props.send_as_instance = (rs->num_instances > 1);

    c->props.src_min = 0;
    c->props.src_max = 0;
    c->props.dest_min = 0;
    c->props.dest_max = 0;
    c->props.range_known = 0;

    // scopes
    c->props.scope.size = 1;
    c->props.scope.names = (char **) malloc(sizeof(char *));
    c->props.scope.names[0] = strdup(mdev_name(r->device));
    c->props.scope.hashes = (uint32_t *) malloc(sizeof(uint32_t));
    c->props.scope.hashes[0] = mdev_id(r->device);

    c->props.extra = table_new();

    int len = strlen(dest_name) + 5;
    c->props.query_name = malloc(len);
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
    r->num_connections++;
    return c;
}

static void mapper_router_free_connection(mapper_router r,
                                          mapper_connection c)
{
    int i, j;
    if (r && c) {
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
        for (i=0; i<c->props.scope.size; i++) {
            free(c->props.scope.names[i]);
        }
        free(c->props.scope.names);
        free(c->props.scope.hashes);

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
            r->num_connections--;
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

mapper_router mapper_router_find_by_dest_hash(mapper_router router,
                                              uint32_t name_hash)
{
    while (router) {
        if (name_hash == router->props.dest_name_hash)
            return router;
        router = router->next;
    }
    return 0;
}
