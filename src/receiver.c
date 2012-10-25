
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <zlib.h>

#include <lo/lo.h>

#include "mapper_internal.h"
#include "types_internal.h"
#include <mapper/mapper.h>

mapper_receiver mapper_receiver_new(mapper_device device, const char *host,
                                    int port, const char *name)
{
    char str[16];
    mapper_receiver r = (mapper_receiver) calloc(1, sizeof(struct _mapper_link));
    sprintf(str, "%d", port);
    r->props.src_addr = lo_address_new(host, str);
    r->props.src_name = strdup(name);
    r->props.extra = table_new();
    r->device = device;
    r->signals = 0;
    r->n_connections = 0;

    if (!r->props.src_addr) {
        mapper_receiver_free(r);
        return 0;
    }
    return r;
}

void mapper_receiver_free(mapper_receiver r)
{
    if (!r)
        return;

    if (r->props.src_addr)
        lo_address_free(r->props.src_addr);
    if (r->signals) {
        mapper_receiver_signal rs = r->signals;
        while (rs) {
            mapper_receiver_signal tmp = rs->next;
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
    free(r);
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
    c->props.clip_min = CT_NONE;
    c->props.clip_max = CT_NONE;
    c->props.muted = 0;
    c->props.extra = table_new();

    // add new connection to this signal's list
    c->next = rs->connections;
    rs->connections = c;
    c->parent = rs;
    r->n_connections++;

    return c;
}

int mapper_receiver_remove_connection(mapper_receiver r,
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
            r->n_connections--;
            return 0;
        }
        temp = &(*temp)->next;
    }
    return 1;
}

int mapper_receiver_add_scope(mapper_receiver r, const char *scope)
{
    if (!scope)
        return 1;
    // Check if scope is already stored for this router
    int i, hash = crc32(0L, (const Bytef *)scope, strlen(scope));
    mapper_db_link props = &r->props;
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

void mapper_receiver_remove_scope(mapper_receiver receiver, const char *scope)
{
    int i, j, hash;
    mapper_device md = receiver->device;

    if (!scope)
        return;

    hash = crc32(0L, (const Bytef *)scope, strlen(scope));

    mapper_db_link props = &receiver->props;
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

    // Check if there are other incoming links with this scope
    mapper_receiver rc = md->receivers;
    while (rc) {
        if (rc == receiver)
            continue;
        if (mapper_receiver_in_scope(rc, hash))
            return;
        rc = rc->next;
    }

    mapper_signal_instance si;

    // Release input instances owned by remote device
    mapper_signal *psig = mdev_get_inputs(md);
    for (i=0; i < mdev_num_inputs(md); i++) {
        // get instances
        si = psig[i]->active_instances;
        while (si) {
            if (si->id_map->group == hash) {
                if (psig[i]->handler) {
                    psig[i]->handler(psig[i], &psig[i]->props,
                                     si->id_map->local, 0, 0, 0);
                }
                msig_release_instance_internal(psig[i], si, 0,
                                               MAPPER_TIMETAG_NOW);
            }
            si = si->next;
        }
    }

    // Release output instances owned by remote device
    psig = mdev_get_outputs(md);
    for (i=0; i < mdev_num_outputs(md); i++) {
        // get instances
        si = psig[i]->active_instances;
        while (si) {
            if (si->id_map->group == hash) {
                msig_release_instance_internal(psig[i], si, 0,
                                               MAPPER_TIMETAG_NOW);
            }
            si = si->next;
        }
    }
    
    // Remove instance maps referring to remote device
    mapper_instance_id_map map = md->active_id_map;
    while (map) {
        if (map->group == hash) {
            mapper_instance_id_map temp = map->next;
            mdev_remove_instance_id_map(md, map);
            map = temp;
        }
        else
            map = map->next;
    }
}

int mapper_receiver_in_scope(mapper_receiver r, int id)
{
    int i;
    for (i=0; i<r->props.num_scopes; i++)
        if (r->props.scope_hashes[i] == id)
            return 1;
    return 0;
}

mapper_receiver mapper_receiver_find_by_src_address(mapper_receiver r,
                                                    lo_address src_addr)
{
    const char *host_to_match = lo_address_get_hostname(src_addr);
    const char *port_to_match = lo_address_get_port(src_addr);

    while (r) {
        const char *host = lo_address_get_hostname(r->props.src_addr);
        const char *port = lo_address_get_port(r->props.src_addr);
        if ((strcmp(host, host_to_match)==0) && (strcmp(port, port_to_match)==0))
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
