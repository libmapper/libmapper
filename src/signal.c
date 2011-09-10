
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
    sig->instance_count = 0;

    // Create one instance to start
    sig->input = 0;
    sig->input = msig_add_instance(sig, 0, 0);
    sig->reserve = 0;
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
        msig_free_instance(si);
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

    if (sig)
        msig_update_instance(sig->input, (mapper_signal_value_t*)&value);
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

    if (sig)
        msig_update_instance(sig->input, (mapper_signal_value_t*)&value);
}

void msig_update(mapper_signal sig, void *value)
{
    /* We have to assume that value points to an array of correct type
     * and size. */
    if (sig)
        msig_update_instance(sig->input, value);
}

mapper_signal_instance msig_add_instance(mapper_signal sig,
                                         mapper_signal_instance_handler *handler,
                                         void *user_data)
{
    if (!sig)
        return 0;
    mapper_signal_instance si = (mapper_signal_instance) calloc(1,
                                sizeof(struct _mapper_signal_instance));
    si->handler = handler;
    // allocate history vectors
    si->history.value = calloc(1, sizeof(mapper_signal_value_t)
                               * sig->props.length * sig->props.history_size);
    si->history.timetag = calloc(1, sizeof(mapper_timetag_t)
                                 * sig->props.history_size);
    si->history.position = -1;
    si->history.size = sig->props.history_size > 1 ? sig->props.history_size : 1;
    si->signal = sig;
    si->id = sig->props.is_output ? sig->instance_count++ : -1;
    lo_timetag_now(&si->creation_time);

    // add signal instance to signal
    si->next = sig->input;
    sig->input = si;
    si->connections = 0;

    if (!sig->device)
        return si;

    // add connection instances to signal instance
    // for each router...
    mapper_router r = sig->device->routers;
    while (r) {
        // ...find signal connection
        mapper_signal_connection sc = r->connections;
        while (sc) {
            if (sc->signal == si->signal) {
                // For each connection...
                mapper_connection c = sc->connection;
                mapper_connection_instance mci = 0;
                while (c) {
                    mapper_connection_instance ci = (mapper_connection_instance) calloc(1,
                                                    sizeof(struct _mapper_connection_instance));
                    // allocate history vectors
                    ci->history.value = calloc(1, sizeof(mapper_signal_value_t)
                                               * c->props.dest_length * c->props.dest_history_size);
                    ci->history.timetag = calloc(1, sizeof(mapper_timetag_t)
                                                 * c->props.dest_history_size);
                    ci->history.position = -1;
                    ci->next = mci;
                    si->connections = mci = ci;
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

void msig_reserve_instances(mapper_signal sig, int num,
                            mapper_signal_instance_handler *handler,
                            void *user_data)
{
    int i;
    mapper_signal_instance si;
    for (i = 0; i < num; i++) {
        si = msig_add_instance(sig, handler, user_data);
        if (si) {
            // Remove instance from active list, place in reserve
            sig->input = si->next;
            si->next = sig->reserve;
            sig->reserve = si;
        }
    }
}

mapper_connection_instance msig_add_connection_instance(mapper_signal_instance si,
                                                        mapper_connection c)
{
    mapper_connection_instance ci = (mapper_connection_instance) calloc(1,
                                    sizeof(struct _mapper_connection_instance));
    ci->id = si->id;
    // allocate history vectors
    ci->history.value = calloc(1, sizeof(mapper_signal_value_t)
                               * c->props.dest_length * c->props.dest_history_size);
    ci->history.timetag = calloc(1, sizeof(mapper_timetag_t)
                                 * c->props.dest_history_size);
    ci->history.position = -1;
    ci->history.size = c->props.dest_history_size > 1 ? c->props.dest_history_size : 1;
    ci->next = si->connections;
    ci->parent = si;
    ci->connection = c;
    si->connections = ci;
    return ci;
}

void msig_suspend_instance(mapper_signal_instance si)
{
    if (!si) return;

    if (si->signal->props.is_output) {
        // First send zero signal
        msig_update_instance(si, NULL);
    }
    else {
        // Set instance ids of input signals to -1
        si->id = -1;
    }

    // Remove instance from active list, place in reserve
    mapper_signal_instance *msi = &si->signal->input;
    while (*msi) {
        if (*msi == si) {
            *msi = si->next;
            si->next = si->signal->reserve;
            si->signal->reserve = si;
            break;
        }
        msi = &(*msi)->next;
    }
}

mapper_signal_instance msig_resume_instance(mapper_signal sig)
{
    mapper_signal_instance si = sig->reserve;
    if (si) {
        sig->reserve = si->next;
        si->next = sig->input;
        sig->input = si;
        lo_timetag_now(&si->creation_time);
        return si;
    }
    else {
        return 0;
    }
}

void msig_remove_instance(mapper_signal_instance si)
{
    if (!si) return;

    // do not allow instance 0 to be removed
    if (si->id == 0) return;

    // First send zero signal
    msig_update_instance(si, NULL);

    // Remove connection instances
    mapper_connection_instance ci = si->connections;
    while (si->connections) {
        si->connections = ci->next;
        mapper_connection_free_instance(ci);
    }
    
    // Remove signal instance
    si->signal->input = si->next;
    msig_free_instance(si);
}

void msig_reallocate_instances(mapper_signal sig)
{
    // Find maximum input length needed for connections
    int oldest_samps = 0;
    // Iterate through instances
    mapper_signal_instance si = sig->input;
    while (si) {
        mapper_connection_instance ci = si->connections;
        while (ci) {
            if (ci->connection->props.mode == MO_EXPRESSION) {
                if (ci->connection->expr->history_size > oldest_samps) {
                    oldest_samps = ci->connection->expr->history_size;
                }
            }
            ci = ci->next;
        }
        si = si->next;
    }
    // If input history size has changed...
    if (oldest_samps != sig->input->history.size) {
        sig->input->history.size = oldest_samps > 1 ? oldest_samps : 1;
        // ...allocate a new array...
        mapper_signal_history_t history;
        history.value = calloc(1, sizeof(mapper_signal_value_t)
                               * sig->props.length * sig->input->history.size);
        history.timetag = calloc(1, sizeof(mapper_timetag_t)
                                 * sig->input->history.size);
        // ... TODO: copy the current history to new arrays...
        sig->input->history.position = -1;
        // ... and free the old array
        free(sig->input->history.value);
        free(sig->input->history.timetag);
        sig->input->history.value = history.value;
        sig->input->history.timetag = history.timetag;
    }
    
    // If connection histories have changed...
    mapper_connection_instance ci = sig->input->connections;
    while (ci) {
        int new_size = ci->connection->expr->history_size;
        if (ci->history.size != new_size) {
            mapper_signal_history_t history;
            history.value = calloc(1, sizeof(mapper_signal_value_t)
                                   * ci->connection->props.dest_length
                                   * ci->connection->props.dest_history_size);
            history.timetag = calloc(1, sizeof(mapper_timetag_t)
                                     * ci->connection->props.dest_history_size);
            ci->history.size = new_size;
            ci->history.position = -1;
            free(ci->history.value);
            free(ci->history.timetag);
            ci->history.value = history.value;
            ci->history.timetag = history.timetag;
        }
        ci = ci->next;
    }
}

void msig_update_instance(mapper_signal_instance instance, void *value)
{
    if (!instance) return;
    if (!instance->signal) return;

    /* We have to assume that value points to an array of correct type
     * and size. */
    
#ifdef DEBUG
    if (!instance->signal->device) {
        trace("signal does not have a device in msig_update_float().\n");
        return;
    }
#endif

    /* TODO: move instance history update value to mapper_expr_evaluate
     * (once full vector support has been added) */
    if (value) {
        instance->history.position = (instance->history.position + 1)
                                      % instance->history.size;
        memcpy(instance->history.value + instance->history.position
               * instance->signal->props.length, value, msig_vector_bytes(instance->signal));
        lo_timetag_now(instance->history.timetag + instance->history.position
                       * sizeof(mapper_timetag_t));
    }
    else {
        instance->history.position = -1;
    }
    if (instance->signal->props.is_output)
        msig_send_instance(instance, (mapper_signal_value_t*)value);
}

void msig_free_instance(mapper_signal_instance mi)
{
    if (!mi)
        return;
    free(mi->history.value);
    free(mi->history.timetag);
}

void msig_send_instance(mapper_signal_instance si, void *value)
{
    // for each connection, construct a mapped signal and send it
    mapper_connection_instance ci = si->connections;
    if (!value) {
        while (ci) {
            ci->history.position = -1;
            mapper_router_send_signal(ci, 0);
            ci = ci->next;
        }
        return;
    }
    while (ci) {
        struct _mapper_signal signal;
        signal.props.name = ci->connection->props.dest_name;
        signal.props.type = ci->connection->props.dest_type;
        signal.props.length = ci->connection->props.dest_length;
        mapper_signal_value_t applied[signal.props.length];
        int i = 0;
        int s = 4;
        void *p = value;

        /* Currently expressions on vectors are not supported by the
         * evaluator.  For now, we half-support it by performing
         * element-wise operations on each item in the vector. */
        if (signal.props.type == 'i')
            s = sizeof(int);
        else if (signal.props.type == 'f')
            s = sizeof(float);
        /* TODO: move instance history update to mapper_expr_evaluate
         * (once full vector support has been added) */
        ci->history.position = (ci->history.position + 1)
                                % ci->history.size;
        for (i = 0; i < signal.props.length; i++) {
            mapper_signal_value_t v, w;
            if (mapper_connection_perform(ci, p, &v)) {
                // copy result to history vector
                memcpy(ci->history.value + ci->history.position
                       * ci->parent->signal->props.length + i,
                       &v, sizeof(mapper_signal_value_t));
                // copy timetag from signal instance
                memcpy(ci->history.timetag + ci->history.position * sizeof(mapper_timetag_t),
                       si->history.timetag, sizeof(mapper_timetag_t));
                if (mapper_clipping_perform(ci->connection, &v, &w))
                    applied[i] = w;
                else
                    break;
            }
            else
                break;
            p += s;
        }
        if (i == signal.props.length)
            mapper_router_send_signal(ci, applied);
        ci = ci->next;
    }
}

void mval_add_to_message(lo_message m, char type,
                         mapper_signal_value_t *value)
{
    switch (type) {
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
