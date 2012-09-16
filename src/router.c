
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <zlib.h>

#include <lo/lo.h>

#include "mapper_internal.h"
#include "types_internal.h"
#include <mapper/mapper.h>

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
    router->connections = 0;

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
        if (router->connections) {
            mapper_signal_connection sc = router->connections;
            while (sc) {
                mapper_signal_connection tmp = sc->next;
                if (sc->connection) {

                    mapper_connection c = sc->connection;
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
                free(sc);
                sc = tmp;
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

void mapper_router_send_signal(mapper_connection_instance ci,
                               int send_as_instance)
{
    int i;
    lo_message m;
    if (!ci->connection->router->props.dest_addr)
        return;

    m = lo_message_new();
    if (!m)
        return;

    if (send_as_instance) {
        lo_message_add_int32(m, ci->parent->id_map->group);
        lo_message_add_int32(m, ci->parent->id_map->remote);
    }

    if (ci->history.position != -1) {
        if (ci->history.type == 'f') {
            float *v = msig_history_value_pointer(ci->history);
            for (i = 0; i < ci->history.length; i++)
                lo_message_add_float(m, v[i]);
        }
        else if (ci->history.type == 'i') {
            int *v = msig_history_value_pointer(ci->history);
            for (i = 0; i < ci->history.length; i++)
                lo_message_add_int32(m, v[i]);
        }
        else if (ci->history.type == 'd') {
            double *v = msig_history_value_pointer(ci->history);
            for (i = 0; i < ci->history.length; i++)
                lo_message_add_double(m, v[i]);
        }
    }
    else if (mdev_id(ci->connection->router->device) == ci->parent->id_map->group) {
        // If instance is locally owned, send instance release...
        lo_message_add_nil(m);
    }
    else {
        // ...otherwise send release request.
        lo_message_add_false(m);
    }

    lo_send_message_from(ci->connection->router->props.dest_addr,
                         ci->connection->router->device->server,
                         ci->connection->props.dest_name,
                         m);
    lo_message_free(m);
    return;
}

void mapper_router_send_new_instance(mapper_connection_instance ci)
{
    lo_message m = lo_message_new();
    if (!m)
        return;

    lo_message_add_int32(m, ci->parent->id_map->group);
    lo_message_add_int32(m, ci->parent->id_map->remote);
    lo_message_add_true(m);

    lo_send_message_from(ci->connection->router->props.dest_addr,
                         ci->connection->router->device->server,
                         ci->connection->props.dest_name,
                         m);
    lo_message_free(m);
    return;
}

int mapper_router_send_query(mapper_router router, mapper_signal sig)
{
    // find this signal in list of connections
    mapper_signal_connection sc = router->connections;
    while (sc && sc->signal != sig)
        sc = sc->next;

    // exit without failure if signal is not mapped
    if (!sc) {
        return 0;
    }
    // for each connection, query the remote signal
    mapper_connection c = sc->connection;
    int count = 0, query_len = 0;
    char *query_string = 0;
    int response_len = (int) strlen(sig->props.name) + 5;
    char *response_string = (char*) malloc(response_len);
    snprintf(response_string, response_len, "%s%s", sig->props.name, "/got");
    while (c) {
        query_len = (int) strlen(c->props.dest_name) + 5;
        query_string = (char*) realloc(query_string, query_len);
        snprintf(query_string, query_len, "%s%s", c->props.dest_name, "/get");
        lo_send_from(router->props.dest_addr, router->device->server,
                     LO_TT_IMMEDIATE, query_string, "s", response_string);
        count++;
        c = c->next;
    }
    return count;
}

mapper_connection mapper_router_add_connection(mapper_router router,
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
              n, router->props.dest_name, dest_name);
        return 0;
    }

    mapper_connection connection = (mapper_connection)
        calloc(1, sizeof(struct _mapper_connection));
    
    connection->props.src_name = strdup(sig->props.name);
    connection->props.src_type = sig->props.type;
    connection->props.src_length = sig->props.length;
    connection->props.dest_name = strdup(dest_name);
    connection->props.dest_type = dest_type;
    connection->props.dest_length = dest_length;
    connection->props.mode = MO_UNDEFINED;
    connection->props.expression = strdup("y=x");
    connection->props.clip_min = CT_NONE;
    connection->props.clip_max = CT_NONE;
    connection->props.muted = 0;
    connection->source = sig;
    connection->router = router;
    connection->props.extra = table_new();

    // create connection instances as necessary
    mapper_signal_instance si = sig->instances;
    while (si) {
        msig_add_connection_instance(si, connection);
        si = si->next;
    }

    // find signal in signal connection list
    mapper_signal_connection sc = router->connections;
    while (sc && sc->signal != sig)
        sc = sc->next;

    // if not found, create a new list entry
    if (!sc) {
        sc = (mapper_signal_connection)
            calloc(1, sizeof(struct _mapper_signal_connection));
        sc->signal = sig;
        sc->next = router->connections;
        router->connections = sc;
    }
    // add new connection to this signal's list
    connection->next = sc->connection;
    sc->connection = connection;
    router->device->n_connections++;
    
    return connection;
}

int mapper_router_remove_connection(mapper_router router, 
                                    mapper_connection connection)
{
    // remove associated connection instances
    mapper_signal_instance si = connection->source->instances;
    while (si) {
        mapper_connection_instance temp, *ci = &si->connections;
        while (*ci) {
            if ((*ci)->connection == connection) {
                temp = *ci;
                *ci = (*ci)->next;
                if (temp->parent->id_map &&
                    connection->source->props.num_instances > 1) {
                    temp->history.position = -1;
                    mapper_router_send_signal(temp, 1);
                }
                msig_free_connection_instance(temp);
                break;
            }
            ci = &(*ci)->next;
        }
        si = si->next;
    }
    // find signal in signal connection list
    mapper_signal_connection sc = router->connections;
    while (sc) {
        mapper_connection *c = &sc->connection;
        while (*c) {
            if (*c == connection) {
                *c = connection->next;
                free(connection);
                router->device->n_connections--;
                return 0;
            }
            c = &(*c)->next;
        }
        sc = sc->next;
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
