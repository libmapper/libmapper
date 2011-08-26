
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <assert.h>

#include "mapper_internal.h"
#include "types_internal.h"
#include <mapper/mapper.h>

mapper_signal msig_new(const char *name, int length, char type,
                       int is_output, const char *unit,
                       void *minimum, void *maximum,
                       mapper_signal_handler *handler, void *user_data)
{
    if (length < 1) return 0;
    if (!name) return 0;
    if (type != 'f' && type != 'i')
        return 0;

    mapper_signal sig =
        (mapper_signal) calloc(1, sizeof(struct _mapper_signal));

    mapper_db_signal_init(&sig->props, is_output, type, length, name, unit);
    sig->handler = handler;
    sig->props.user_data = user_data;
    sig->props.hidden = 0;
    msig_set_minimum(sig, minimum);
    msig_set_maximum(sig, maximum);

    // Create one instance to start
    sig->input = msig_spawn_instance(sig, 1);
    sig->input->next = 0;
    return sig;
}

mapper_db_signal msig_properties(mapper_signal sig)
{
    return &sig->props;
}

void *msig_value(mapper_signal sig,
                 mapper_timetag_t *timetag)
{
    if (!sig) return 0;
    if (!sig->input) return 0;
    if (sig->input->history.position == -1)
        return 0;
    //if (timetag)
    //    timetag = sig->input->timetag + sig->input->position;
    return sig->input->history.value + sig->input->history.position;
}

void msig_set_property(mapper_signal sig, const char *property,
                       lo_type type, lo_arg *value)
{
    mapper_table_add_or_update_osc_value(sig->props.extra,
                                         property, type, value);
}

void msig_remove_property(mapper_signal sig, const char *property)
{
    table_remove_key(sig->props.extra, property, 1);
}

void msig_set_minimum(mapper_signal sig, void *minimum)
{
    if (minimum) {
        if (!sig->props.minimum)
            sig->props.minimum = (mapper_signal_value_t *)
                malloc(sizeof(mapper_signal_value_t));
        if (sig->props.type == 'f')
            sig->props.minimum->f = *(float*)minimum;
        else if (sig->props.type == 'i')
            sig->props.minimum->i32 = *(int*)minimum;
    }
    else {
        if (sig->props.minimum)
            free(sig->props.minimum);
        sig->props.minimum = 0;
    }
}

void msig_set_maximum(mapper_signal sig, void *maximum)
{
    if (maximum) {
        if (!sig->props.maximum)
            sig->props.maximum = (mapper_signal_value_t *)
                malloc(sizeof(mapper_signal_value_t));
        if (sig->props.type == 'f')
            sig->props.maximum->f = *(float*)maximum;
        else if (sig->props.type == 'i')
            sig->props.maximum->i32 = *(int*)maximum;
    }
    else {
        if (sig->props.maximum)
            free(sig->props.maximum);
        sig->props.maximum = 0;
    }
}

void msig_free(mapper_signal sig)
{
    if (!sig) return;

    mapper_signal_instance si = sig->input;
    while (si) {
        mapper_signal_free_instance(si);
        si = si->next;
    }
    // TODO: free connection instances
    if (sig->props.minimum)
        free(sig->props.minimum);
    if (sig->props.maximum)
        free(sig->props.maximum);
    if (sig->props.name)
        free((char*)sig->props.name);
    if (sig->props.unit)
        free((char*)sig->props.unit);
    if (sig->props.extra)
        table_free(sig->props.extra, 1);
    free(sig);
}

void msig_update_int(mapper_signal sig, int value)
{
#ifdef DEBUG
    if (sig->props.type != 'i') {
        trace("called msig_update_int() on non-int signal!\n");
        return;
    }

    if (sig->props.length != 1) {
        trace("called msig_update_int() on non-scalar signal!\n");
        return;
    }

    if (!sig->device) {
        trace("signal does not have a device in msig_update_int().\n");
        return;
    }
#endif

    sig->input->history.position = (sig->input->history.position + 1)
                                    % sig->input->history.size;

    memcpy(sig->input->history.value + sig->input->history.position
           * sig->props.length, &value, msig_vector_bytes(sig));
    if (sig->props.is_output)
        mdev_route_signal(sig->device, sig, (mapper_signal_value_t*)&value);
}

void msig_update_float(mapper_signal sig, float value)
{
#ifdef DEBUG
    if (sig->props.type != 'f') {
        trace("called msig_update_float() on non-float signal!\n");
        return;
    }

    if (sig->props.length != 1) {
        trace("called msig_update_float() on non-scalar signal!\n");
        return;
    }

    if (!sig->device) {
        trace("signal does not have a device in msig_update_float().\n");
        return;
    }
#endif

    sig->input->history.position = (sig->input->history.position + 1)
                                    % sig->input->history.size;
    memcpy(sig->input->history.value + sig->input->history.position
           * sig->props.length, &value, msig_vector_bytes(sig));
    if (sig->props.is_output)
        mdev_route_signal(sig->device, sig, (mapper_signal_value_t*)&value);
}

void msig_update(mapper_signal sig, void *value)
{
    /* We have to assume that value points to an array of correct type
     * and size. */

#ifdef DEBUG
    if (!sig->device) {
        trace("signal does not have a device in msig_update_float().\n");
        return;
    }
#endif

    sig->input->history.position = (sig->input->history.position + 1)
                                    % sig->input->history.size;
    memcpy(sig->input->history.value + sig->input->history.position
           * sig->props.length, value, msig_vector_bytes(sig));
    if (sig->props.is_output)
        mdev_route_signal(sig->device, sig, (mapper_signal_value_t*)value);
}

mapper_signal_instance msig_spawn_instance(mapper_signal sig)
{
    if (!sig)
        return 0;
    mapper_signal_instance si = (mapper_signal_instance) calloc(1,
                                sizeof(struct _mapper_signal_instance));
    // allocate history vectors
    si->history.value = calloc(1, sizeof(mapper_signal_value_t)
                               * sig->props.length * sig->props.history_size);
    si->history.timetag = calloc(1, sizeof(mapper_timetag_t)
                                 * history_size);
    si->history.position = -1;
    si->history.size = history_size;

    // add instance to signal
    si->next = sig->input;
    sig->input = si;
    
    // add instance to connections
    // for each router...
    mapper_router r = si->signal->device->routers;
    while (r) {
        // ...find signal connection
        mapper_signal_connection sc = r->connections;
        while (sc) {
            if (sc->signal == si->signal) {
                // For each connection...
                mapper_connection c = sc->connection;
                while (c) {
                    mapper_connection_spawn_instance(c);
                    c = c->next;
                }
                continue;
            }
            sc = sc->next;
        }
        r = r->next;
    }
    
    return si;
}

void msig_kill_instance(mapper_signal_instance si)
{
    if (!si)
        return;
    // First send zero signal
    msig_update_instance(si, NULL);

    // for each router...
    mapper_router r = si->signal->device->routers;
    while (r) {
        // ...find signal connection
        mapper_signal_connection sc = r->connections;
        while (sc) {
            if (sc->signal == si->signal) {
                // For each connection...
                mapper_connection c = sc->connection;
                while (c) {
                    mapper_connection_kill_instance(c);
                    c = c->next;
                }
                continue;
            }
            sc = sc->next;
        }
        r = r->next;
    }
    // Kill signal instance
    si->signal->input = si->next;
    mapper_signal_free_instance(si);
}

void msig_reallocate_instances(history_size)
{
    foo;
}

void msig_update_instance(mapper_signal_instance instance, void *value)
{
    if (!instance)
        return;
}

void msig_free_instance(mapper_signal_instance mi)
{
    if (!mi)
        return;
    free(mi->history.value);
    //free(mi->history.timetag);
}

void mval_add_to_message(lo_message m, mapper_signal sig,
                         mapper_signal_value_t *value)
{
    switch (sig->props.type) {
    case 'f':
        lo_message_add_float(m, value->f);
        break;
    case 'd':
        lo_message_add_double(m, value->d);
        break;
    case 'i':
        lo_message_add_int32(m, value->i32);
        break;
    default:
        // Unknown signal type
        assert(0);
        break;
    }
}

int msig_full_name(mapper_signal sig, char *name, int len)
{
    const char *mdname = mdev_name(sig->device);
    if (!mdname)
        return 0;

    int mdlen = strlen(mdname);
    if (mdlen >= len)
        return 0;
    if ((mdlen + strlen(sig->props.name)) > len)
        return 0;

    strncpy(name, mdname, len);
    strncat(name, sig->props.name, len);
    return strlen(name);
}

int msig_query_remote(mapper_signal sig, mapper_signal receiver)
{
    // stick to output signals for now
    if (!sig->props.is_output)
        return -1;
    if (!sig->device->server) {
        // no server running so we cannot process query returns
        return -1;
    }
    if (receiver) {
        mapper_signal sig2 = (mapper_signal) receiver;
        const char *alias = sig2->props.name;
        if (alias)
            return mdev_route_query(sig->device, sig, alias);
    }
    else
        return mdev_route_query(sig->device, sig, 0);
    return 0;
}
