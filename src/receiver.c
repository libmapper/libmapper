
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <zlib.h>

#include <lo/lo.h>

#include "mapper_internal.h"
#include "types_internal.h"
#include <mapper/mapper.h>

mapper_receiver mapper_receiver_new(mapper_device device, const char *host,
                                    int port, const char *name, int default_scope)
{
    char str[16];
    mapper_receiver r = (mapper_receiver) calloc(1, sizeof(struct _mapper_link));
    r->props.src_host = strdup(host);
    r->props.src_port = port;
    sprintf(str, "%d", port);
    r->remote_addr = lo_address_new(host, str);
    r->props.src_name = strdup(name);
    r->props.src_name_hash = crc32(0L, (const Bytef *)name, strlen(name));
    r->props.dest_name = strdup(mdev_name(device));
    if (default_scope) {
        r->props.num_scopes = 1;
        r->props.scope_names = (char **) malloc(sizeof(char *));
        r->props.scope_names[0] = strdup(name);
        r->props.scope_hashes = (uint32_t *) malloc(sizeof(uint32_t));
        r->props.scope_hashes[0] = crc32(0L, (const Bytef *)name, strlen(name));;
    }
    else {
        r->props.num_scopes = 0;
    }
    r->props.extra = table_new();
    r->device = device;
    r->signals = 0;
    r->n_connections = 0;

    if (!r->remote_addr) {
        mapper_receiver_free(r);
        return 0;
    }
    return r;
}

void mapper_receiver_free(mapper_receiver r)
{
    int i;

    if (r) {
        if (r->props.src_name)
            free(r->props.src_name);
        if (r->props.src_host)
            free(r->props.src_host);
        if (r->remote_addr)
            lo_address_free(r->remote_addr);
        if (r->props.dest_name)
            free(r->props.dest_name);
        while (r->signals && r->signals->connections) {
            // receiver_signal is freed with last child connection
            mapper_receiver_remove_connection(r, r->signals->connections);
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

/*! Set a router's properties based on message parameters. */
void mapper_receiver_set_from_message(mapper_receiver rc,
                                      mapper_message_t *msg)
{
    /* Extra properties. */
    mapper_msg_add_or_update_extra_params(rc->props.extra, msg);
}

static void message_add_coerced_signal_value(lo_message m,
                                             mapper_signal sig,
                                             mapper_signal_instance si,
                                             const char coerce_type)
{
    int i;
    if (sig->props.type == 'f') {
        float *v = si->value;
        if (coerce_type == 'f') {
            for (i = 0; i < sig->props.length; i++)
                lo_message_add_float(m, v[i]);
        }
        else if (coerce_type == 'i') {
            for (i = 0; i < sig->props.length; i++)
                lo_message_add_int32(m, (int)v[i]);
        }
        else if (coerce_type == 'd') {
            for (i = 0; i < sig->props.length; i++)
                lo_message_add_double(m, (double)v[i]);
        }
    }
    else if (sig->props.type == 'i') {
        int *v = si->value;
        if (coerce_type == 'i') {
            for (i = 0; i < sig->props.length; i++)
                lo_message_add_int32(m, v[i]);
        }
        else if (coerce_type == 'f') {
            for (i = 0; i < sig->props.length; i++)
                lo_message_add_float(m, (float)v[i]);
        }
        else if (coerce_type == 'd') {
            for (i = 0; i < sig->props.length; i++)
                lo_message_add_double(m, (double)v[i]);
        }
    }
    else if (sig->props.type == 'd') {
        double *v = si->value;
        if (coerce_type == 'd') {
            for (i = 0; i < sig->props.length; i++)
                lo_message_add_double(m, (int)v[i]);
        }
        else if (coerce_type == 'i') {
            for (i = 0; i < sig->props.length; i++)
                lo_message_add_int32(m, (int)v[i]);
        }
        else if (coerce_type == 'f') {
            for (i = 0; i < sig->props.length; i++)
                lo_message_add_float(m, (float)v[i]);
        }
    }
}

void mapper_receiver_send_update(mapper_receiver r,
                                 mapper_signal sig,
                                 int instance_index,
                                 mapper_timetag_t tt)
{
    int i, count=0;
    mapper_id_map map = sig->id_maps[instance_index].map;

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
        if (c->props.mode != MO_REVERSE) {
            c = c->next;
            continue;
        }

        if (sig->props.num_instances == 1) {
            lo_message m = lo_message_new();
            if (!m)
                return;
            message_add_coerced_signal_value(m, sig, sig->id_maps[0].instance,
                                             c->props.src_type);
            lo_bundle_add_message(b, c->props.query_name, m);
        }
        else if (instance_index >= 0) {
            mapper_signal_instance si = sig->id_maps[instance_index].instance;
            lo_message m = lo_message_new();
            if (!m)
                return;
            lo_message_add_int32(m, map->origin);
            lo_message_add_int32(m, map->public);
            if (si->has_value)
                message_add_coerced_signal_value(m, sig, si, c->props.src_type);
            else
                lo_message_add_nil(m);
            lo_bundle_add_message(b, c->props.query_name, m);
        }
        else {
            int sent = 0;
            for (i = 0; i < sig->id_map_length; i++) {
                mapper_signal_instance si = sig->id_maps[i].instance;
                if (!si)
                    continue;
                lo_message m = lo_message_new();
                if (!m)
                    return;
                lo_message_add_int32(m, map->origin);
                lo_message_add_int32(m, map->public);
                if (si->has_value)
                    message_add_coerced_signal_value(m, sig, si,
                                                     c->props.src_type);
                else
                    lo_message_add_nil(m);
                lo_bundle_add_message(b, c->props.query_name, m);
                sent++;
            }
            if (!sent) {
                // If there are no active instances, send null response
                lo_message m = lo_message_new();
                if (!m)
                    return;
                lo_message_add_nil(m);
                lo_bundle_add_message(b, c->props.query_name, m);
            }
        }
        c = c->next;
    }
    lo_send_bundle(r->remote_addr, b);
    lo_bundle_free_messages(b);
}

void mapper_receiver_send_released(mapper_receiver r, mapper_signal sig,
                                   int instance_index, mapper_timetag_t tt)
{
    mapper_receiver_signal rs = r->signals;
    mapper_connection c;

    mapper_id_map map = sig->id_maps[instance_index].map;

    if (!mapper_receiver_in_scope(r, map->origin))
        return;

    while (rs) {
        if (rs->signal == sig)
            break;
        rs = rs->next;
    }
    if (!rs)
        return;

    lo_bundle b = lo_bundle_new(tt);

    c = rs->connections;
    while (c) {
        lo_message m = lo_message_new();
        if (!m)
            return;
        lo_message_add_int32(m, map->origin);
        lo_message_add_int32(m, map->public);
        lo_message_add_false(m);
        lo_bundle_add_message(b, c->props.src_name, m);
        c = c->next;
    }

    if (lo_bundle_count(b))
        lo_send_bundle_from(r->remote_addr, r->device->server, b);

    lo_bundle_free_messages(b);
}

mapper_connection mapper_receiver_add_connection(mapper_receiver r,
                                                 mapper_signal sig,
                                                 const char *src_name,
                                                 char src_type,
                                                 int src_length)
{
    /* Currently, fail if lengths don't match.  TODO: In the future,
     * we'll have to examine the expression to see if its input and
     * output lengths are compatible. */
    if (sig->props.length != src_length) {
        char n[1024];
        msig_full_name(sig, n, 1024);
        trace("rejecting connection %s -> %s%s because lengths "
              "don't match (not yet supported)\n",
              n, r->props.src_name, src_name);
        return 0;
    }

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
    c->props.expression = strdup("y=x");
    c->props.bound_min = BA_NONE;
    c->props.bound_max = BA_NONE;
    c->props.muted = 0;
    c->props.extra = table_new();

    int len = strlen(src_name) + 5;
    c->props.query_name = malloc(len);
    snprintf(c->props.query_name, len, "%s%s", src_name, "/got");

    // add new connection to this signal's list
    c->next = rs->connections;
    rs->connections = c;
    c->parent = rs;
    r->n_connections++;

    return c;
}

static void mapper_receiver_free_connection(mapper_receiver r, mapper_connection c)
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
        r->n_connections--;
        return;
    }
}

int mapper_receiver_remove_connection(mapper_receiver r,
                                      mapper_connection c)
{
    int i = 0, j, found = 0, count = 0;
    mapper_receiver_signal rs = c->parent;

    /* Release signal instances owned by remote device. This is a bit tricky
     * since there may be other remote signals connected to this signal.
     * We will check: a) if the parent link contains other connections to
     * the target signal of this connection, and b) if other links contain
     * connections to this signal. In the latter case, we can still release
     * instances scoped uniquely to this connection's receiver. */

    // TODO: this situation should be avoided using e.g. signal "slots"

    mapper_connection *ctemp = &c->parent->connections;
    while (*ctemp) {
        i++;
        ctemp = &(*ctemp)->next;
    }
    if (i <= 1 && c->parent->signal) {
        /* need to compile a list of scopes used by this link not used by other
         * links including connections to this signal. */
        int count = 0;
        int *scope_matches = (int *) calloc(1, sizeof(int) * r->props.num_scopes);
        mapper_receiver rtemp = r->device->receivers;
        while (rtemp) {
            if (rtemp == r) {
                rtemp = rtemp->next;
                continue;
            }
            mapper_receiver_signal stemp = rtemp->signals;
            while (stemp) {
                if (stemp->signal == rs->signal) {
                    // check if scopes are shared
                    for (i=0; i<rtemp->props.num_scopes; i++) {
                        if (mapper_receiver_in_scope(r, rtemp->props.scope_hashes[i])) {
                            if (scope_matches[i] == 0) {
                                count++;
                                if (count >= r->props.num_scopes)
                                    break;
                            }
                            scope_matches[i] = 1;
                        }
                    }
                    if (count >= r->props.num_scopes)
                        break;
                }
                stemp = stemp->next;
            }
            if (count >= r->props.num_scopes)
                break;
            rtemp = rtemp->next;
        }

        mapper_timetag_t *tt = &r->device->admin->clock.now;
        mdev_now(r->device, tt);
        if (count < r->props.num_scopes) {
            // can release instances with untouched scopes
            for (i = 0; i < rs->signal->id_map_length; i++) {
                mapper_signal_id_map_t *id_map = &rs->signal->id_maps[i];
                if (!id_map->map || !id_map->instance || id_map->status & IN_RELEASED_REMOTELY)
                    continue;
                for (j = 0; j < r->props.num_scopes; j++) {
                    if (scope_matches[j]) {
                        // scope is used by another link
                        continue;
                    }
                    if (id_map->map->origin == r->props.scope_hashes[j]) {
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
            }
        }
        free(scope_matches);
    }

    // Now free the connection
    mapper_connection *temp = &c->parent->connections;
    while (*temp) {
        if (*temp == c) {
            *temp = c->next;
            mapper_receiver_free_connection(r, c);
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

int mapper_receiver_add_scope(mapper_receiver r, const char *scope)
{
    return mapper_db_link_add_scope(&r->props, scope);
}

int mapper_receiver_remove_scope(mapper_receiver receiver, const char *scope)
{
    mapper_device md = receiver->device;
    if (mapper_db_link_remove_scope(&receiver->props, scope))
        return 1;

    /* If there are other incoming links with this scope, do not continue. */
    /* TODO: really we should proceed but with caution: we can release input
     * instances as long as the signals are not mapped from another link with
     * this scope. */
    mapper_receiver rc = md->receivers;
    uint32_t hash = crc32(0L, (const Bytef *)scope, strlen(scope));
    while (rc) {
        if (rc != receiver && mapper_receiver_in_scope(rc, hash))
            return 0;
        rc = rc->next;
    }

    /* Release input instances owned by remote device. */
    mapper_timetag_t *tt = &md->admin->clock.now;
    mdev_now(md, tt);
    mapper_receiver_signal rs = receiver->signals;
    while (rs) {
        int i;
        for (i = 0; i < rs->signal->id_map_length; i++) {
            mapper_id_map map = rs->signal->id_maps[i].map;
            if (map->origin == hash) {
                if (rs->signal->instance_event_handler &&
                    (rs->signal->instance_event_flags & IN_UPSTREAM_RELEASE)) {
                    rs->signal->instance_event_handler(rs->signal, &rs->signal->props,
                                                       map->local, IN_UPSTREAM_RELEASE,
                                                       tt);
                }
                else if (rs->signal->handler) {
                    rs->signal->handler(rs->signal, &rs->signal->props,
                                        map->local, 0, 0, tt);
                }
                continue;
            }
        }
        rs = rs->next;
    }

    /* Rather than releasing output signal instances owned by the remote scope,
     * we will trust that whatever mechanism created these instances is also
     * capable of releasing them appropriately when the input handlers are called.
     * Likewise, we will not remove instance maps referring to the remote device
     * since local instances may be using them. The maps will be removed
     * automatically once all referring instances have been released. */

    return 0;
}

int mapper_receiver_in_scope(mapper_receiver r, uint32_t name_hash)
{
    int i;
    for (i=0; i<r->props.num_scopes; i++)
        if (r->props.scope_hashes[i] == name_hash)
            return 1;
    return 0;
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
