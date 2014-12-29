
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <zlib.h>

#include <lo/lo.h>

#include "mapper_internal.h"
#include "types_internal.h"
#include <mapper/mapper.h>

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

mapper_receiver mapper_receiver_new(mapper_device device, const char *host,
                                    int admin_port, int data_port,
                                    const char *name)
{
    char str[16];
    mapper_receiver r = (mapper_receiver) calloc(1, sizeof(struct _mapper_link));
    r->props.src_host = strdup(host);
    r->props.src_port = data_port;
    sprintf(str, "%d", data_port);
    r->data_addr = lo_address_new(host, str);
    sprintf(str, "%d", admin_port);
    r->admin_addr = lo_address_new(host, str);
    r->props.src_name = strdup(name);
    r->props.src_name_hash = crc32(0L, (const Bytef *)name, strlen(name));
    r->props.dest_name = strdup(mdev_name(device));

    r->props.extra = table_new();
    r->device = device;
    r->signals = 0;
    r->num_connections = 0;

    r->clock.new = 1;
    r->clock.sent.message_id = 0;
    r->clock.response.message_id = -1;

    if (!r->data_addr) {
        mapper_receiver_free(r);
        return 0;
    }
    return r;
}

void mapper_receiver_free(mapper_receiver r)
{
    if (r) {
        if (r->props.src_name)
            free(r->props.src_name);
        if (r->props.src_host)
            free(r->props.src_host);
        if (r->admin_addr)
            lo_address_free(r->admin_addr);
        if (r->data_addr)
            lo_address_free(r->data_addr);
        if (r->props.dest_name)
            free(r->props.dest_name);
        while (r->signals && r->signals->connections) {
            // receiver_signal is freed with last child connection
            mapper_receiver_remove_connection(r, r->signals->connections);
        }
        if (r->props.extra)
            table_free(r->props.extra, 1);
        free(r);
    }
}

/*! Set a router's properties based on message parameters. */
int mapper_receiver_set_from_message(mapper_receiver r, mapper_message_t *msg)
{
    return mapper_msg_add_or_update_extra_params(r->props.extra, msg);
}

void mapper_receiver_send_update(mapper_receiver r,
                                 mapper_signal sig,
                                 int instance_index,
                                 mapper_timetag_t tt)
{
    // TODO: check vector has_values flags, include defined elements even if !has_value?
    int count=0;
    mapper_id_map map = sig->id_maps[instance_index].map;
    if (!map)
        return;

    // find the signal connection
    mapper_receiver_signal rc = r->signals;
    while (rc) {
        if (rc->signal == sig)
            break;
        rc = rc->next;
    }
    if (!rc)
        return;

    mapper_connection c = rc->connections;
    while (c) {
        if (c->props.mode == MO_REVERSE)
            count++;
        c = c->next;
    }
    if (!count)
        return;

    lo_bundle b = lo_bundle_new(tt);

    c = rc->connections;
    while (c) {
        if (c->props.mode != MO_REVERSE
            || (c->props.send_as_instance && !in_connection_scope(c, map->origin))) {
            c = c->next;
            continue;
        }

        lo_message m = lo_message_new();
        if (!m)
            return;

        mapper_signal_instance si = sig->id_maps[instance_index].instance;
        message_add_coerced_signal_instance_value(m, sig, si,
                                                  c->props.src_length,
                                                  c->props.src_type);
        if (c->props.send_as_instance) {
            lo_message_add_string(m, "@instance");
            lo_message_add_int32(m, map->origin);
            lo_message_add_int32(m, map->public);
        }
        lo_bundle_add_message(b, c->props.query_name, m);
        c = c->next;
    }
    lo_send_bundle(r->data_addr, b);
    lo_bundle_free_messages(b);
}

void mapper_receiver_send_released(mapper_receiver r, mapper_signal sig,
                                   int instance_index, mapper_timetag_t tt)
{
    mapper_receiver_signal rs = r->signals;
    mapper_connection c;

    mapper_id_map map = sig->id_maps[instance_index].map;

    while (rs) {
        if (rs->signal == sig)
            break;
        rs = rs->next;
    }
    if (!rs)
        return;

    lo_bundle b = lo_bundle_new(tt);

    c = rs->connections;
    int i;
    while (c) {
        if (in_connection_scope(c, map->origin)) {
            lo_message m = lo_message_new();
            if (!m)
                return;
            for (i = 0; i < c->props.src_length; i++)
                lo_message_add_nil(m);
            lo_message_add_string(m, "@instance");
            lo_message_add_int32(m, map->origin);
            lo_message_add_int32(m, map->public);
            lo_bundle_add_message(b, c->props.src_name, m);
        }
        c = c->next;
    }

    if (lo_bundle_count(b))
        lo_send_bundle_from(r->data_addr, r->device->server, b);

    lo_bundle_free_messages(b);
}

mapper_connection mapper_receiver_add_connection(mapper_receiver r,
                                                 mapper_signal sig,
                                                 const char *src_name,
                                                 char src_type,
                                                 int src_length)
{
    // find signal in signal connection list
    mapper_receiver_signal rs = r->signals;
    while (rs && rs->signal != sig)
        rs = rs->next;

    // if not found, create a new list entry
    if (!rs) {
        rs = (mapper_receiver_signal)
            calloc(1, sizeof(struct _mapper_link_signal));
        rs->signal = sig;
        rs->next = r->signals;
        r->signals = rs;
    }

    mapper_connection c = (mapper_connection)
        calloc(1, sizeof(struct _mapper_connection));

    c->props.src_name = strdup(src_name);
    c->props.src_type = src_type;
    c->props.src_length = src_length;
    c->props.dest_name = strdup(sig->props.name);
    c->props.dest_type = sig->props.type;
    c->props.dest_length = sig->props.length;
    c->props.mode = MO_UNDEFINED;
    c->props.expression = 0;
    c->props.bound_min = BA_NONE;
    c->props.bound_max = BA_NONE;
    c->props.muted = 0;

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

    int len = strlen(src_name) + 5;
    c->props.query_name = malloc(len);
    snprintf(c->props.query_name, len, "%s%s", src_name, "/got");

    // add new connection to this signal's list
    c->next = rs->connections;
    rs->connections = c;
    c->parent = rs;
    r->num_connections++;

    return c;
}

static void mapper_receiver_free_connection(mapper_receiver r, mapper_connection c)
{
    int i, j;
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
                free(c->expr_vars[i]);
            }
        }
        if (c->expr_vars)
            free(c->expr_vars);
        if (c->history)
            free(c->history);
        for (i=0; i<c->props.scope.size; i++) {
            free(c->props.scope.names[i]);
        }
        free(c->props.scope.names);
        free(c->props.scope.hashes);

        free(c);
        r->num_connections--;
        return;
    }
}

int mapper_receiver_remove_connection(mapper_receiver r,
                                      mapper_connection c)
{
    int i = 0, found = 0, count = 0;
    mapper_receiver_signal rs = c->parent;

    /* Call release handlers for connected signal instances matching the scope
     * of this connection. TODO: we could keep track of which connection
     * actually activated the signal instance and not release others, or we
     * could check to see if any other connections could have activated the
     * instance. */
    mapper_timetag_t *tt = &r->device->admin->clock.now;
    mdev_now(r->device, tt);
    for (i = 0; i < rs->signal->id_map_length; i++) {
        mapper_signal_id_map_t *id_map = &rs->signal->id_maps[i];
        if (!id_map->map || !id_map->instance || id_map->status & IN_RELEASED_REMOTELY)
            continue;
        if (in_connection_scope(c, id_map->map->origin)) {
            if (rs->signal->instance_event_handler &&
                (rs->signal->instance_event_flags & IN_UPSTREAM_RELEASE)) {
                rs->signal->instance_event_handler(rs->signal, &rs->signal->props,
                                                   id_map->map->local, IN_UPSTREAM_RELEASE,
                                                   tt);
            }
            else if (rs->signal->handler)
                rs->signal->handler(rs->signal, &rs->signal->props,
                                    id_map->map->local, 0, 0, tt);
            continue;
        }
    }

    // Now free the connection
    mapper_connection *temp = &c->parent->connections;
    while (*temp) {
        if (*temp == c) {
            *temp = c->next;
            mapper_receiver_free_connection(r, c);
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

mapper_connection mapper_receiver_find_connection_by_names(mapper_receiver rc,
                                                           const char* src_name,
                                                           const char* dest_name)
{
    // find associated receiver_signal
    mapper_receiver_signal rs = rc->signals;
    while (rs && strcmp(rs->signal->props.name, dest_name) != 0)
        rs = rs->next;
    if (!rs)
        return NULL;

    // find associated connection
    mapper_connection c = rs->connections;
    while (c && strcmp(c->props.src_name, src_name) != 0)
        c = c->next;
    if (!c)
        return NULL;
    else
        return c;
}

mapper_receiver mapper_receiver_find_by_src_address(mapper_receiver r,
                                                    const char *host,
                                                    int port)
{
    while (r) {
        if (r->props.src_port == port && (strcmp(r->props.src_host, host)==0))
            return r;
        r = r->next;
    }
    return 0;
}

mapper_receiver mapper_receiver_find_by_src_name(mapper_receiver r,
                                                 const char *src_name)
{
    int n = strlen(src_name);
    const char *slash = strchr(src_name+1, '/');
    if (slash)
        n = slash - src_name;

    while (r) {
        if (strncmp(r->props.src_name, src_name, n)==0)
            return r;
        r = r->next;
    }
    return 0;
}

mapper_receiver mapper_receiver_find_by_src_hash(mapper_receiver r,
                                                 uint32_t name_hash)
{
    while (r) {
        if (name_hash == r->props.src_name_hash)
            return r;
        r = r->next;
    }
    return 0;
}
