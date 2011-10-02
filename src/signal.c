
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
    sig->props.instances = 0;
    sig->props.user_data = user_data;
    sig->props.hidden = 0;
    msig_set_minimum(sig, minimum);
    msig_set_maximum(sig, maximum);
    sig->instance_count = 0;
    sig->instance_allocation_type = IN_UNDEFINED;

    // Create one instance to start
    sig->active = 0;
    msig_add_instance(sig, 0, 0);
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
    if (!sig->active) return 0;
    return msig_instance_value(sig->active, timetag);
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

    // Free active instances
    mapper_signal_instance si;
    while (sig->active) {
        si = sig->active;
        sig->active = si->next;
        msig_free_instance(si);
    }
    // Free reserved instances
    while (sig->reserve) {
        si = sig->reserve;
        sig->reserve = si->next;
        msig_free_instance(si);
    }
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
        msig_update_instance(sig->active, &value);
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
        msig_update_instance(sig->active, &value);
}

void msig_update(mapper_signal sig, void *value)
{
    /* We have to assume that value points to an array of correct type
     * and size. */
    if (sig)
        msig_update_instance(sig->active, value);
}

mapper_signal_instance msig_add_instance(mapper_signal sig,
                                         mapper_signal_instance_handler *handler,
                                         void *user_data)
{
    // TODO: ensure sig->instance_count doesn't exceed some maximum
    if (!sig)
        return 0;
    mapper_signal_instance si = (mapper_signal_instance) calloc(1,
                                sizeof(struct _mapper_signal_instance));
    si->handler = handler;
    si->history.type = sig->props.type;
    si->history.size = sig->props.history_size > 1 ? sig->props.history_size : 1;
    si->history.length = sig->props.length;
    // allocate history vectors
    si->history.value = calloc(1, msig_vector_bytes(sig) * si->history.size);
    si->history.timetag = calloc(1, sizeof(mapper_timetag_t) * si->history.size);
    si->history.position = -1;
    si->signal = sig;
    si->id = sig->instance_count++;
    si->is_active = 1;
    lo_timetag_now(&si->creation_time);

    // add signal instance to signal
    si->next = sig->active;
    sig->active = si;
    si->connections = 0;
    ++sig->props.instances;

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
                while (c) {
                    msig_add_connection_instance(si, c);
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

void *msig_instance_value(mapper_signal_instance si,
                          mapper_timetag_t *timetag)
{
    if (!si) return 0;
    if (si->history.position == -1)
        return 0;
    if (timetag)
        timetag = &si->history.timetag[si->history.position];
    return msig_history_value_pointer(si->history);
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
            si->is_active = 0;
            sig->active = si->next;
            si->next = sig->reserve;
            sig->reserve = si;
        }
    }
}

int msig_num_active_instances(mapper_signal sig)
{
    if (!sig)
        return -1;
    mapper_signal_instance si = sig->active;
    int i = 0;
    while (si) {
        i++;
        si = si->next;
    }
    return i;
}
int msig_num_reserved_instances(mapper_signal sig)
{
    if (!sig)
        return -1;
    mapper_signal_instance si = sig->reserve;
    int i = 0;
    while (si) {
        i++;
        si = si->next;
    }
    return i;
}

mapper_connection_instance msig_add_connection_instance(mapper_signal_instance si,
                                                        mapper_connection c)
{
    mapper_connection_instance ci = (mapper_connection_instance) calloc(1,
                                    sizeof(struct _mapper_connection_instance));
    ci->id = si->id;
    ci->history.type = c->props.dest_type;
    ci->history.size = c->props.dest_history_size > 1 ? c->props.dest_history_size : 1;
    ci->history.length = c->props.dest_length;
    // allocate history vectors
    ci->history.value = calloc(1, mapper_type_size(ci->history.type)
                               * ci->history.length * ci->history.size);
    ci->history.timetag = calloc(1, sizeof(mapper_timetag_t)
                                 * ci->history.size);
    ci->history.position = -1;
    ci->next = si->connections;
    ci->parent = si;
    ci->connection = c;
    si->connections = ci;
    return ci;
}

void msig_release_instance(mapper_signal_instance si)
{
    if (!si)
        return;
    if (!si->is_active)
        return;

    if (si->signal->props.is_output) {
        // First send zero signal
        msig_update_instance(si, NULL);
    }

    // Remove instance from active list, place in reserve
    mapper_signal_instance *msi = &si->signal->active;
    while (*msi) {
        if (*msi == si) {
            *msi = si->next;
            si->is_active = 0;
            si->next = si->signal->reserve;
            si->signal->reserve = si;
            break;
        }
        msi = &(*msi)->next;
    }
}

void msig_resume_instance(mapper_signal_instance si)
{
    if (!si)
        return;
    if (si->is_active)
        return;

    // Remove instance from reserve list, place in active
    mapper_signal_instance *msi = &si->signal->reserve;
    while (*msi) {
        if (*msi == si) {
            *msi = si->next;
            si->is_active = 1;
            si->history.position = -1;
            lo_timetag_now(&si->creation_time);
            si->next = si->signal->active;
            si->signal->active = si;
            break;
        }
        msi = &(*msi)->next;
    }
}

void msig_set_instance_allocation_mode(mapper_signal sig,
                                       mapper_instance_allocation_type mode)
{
    if (sig && mode >= 0 && mode < N_MAPPER_INSTANCE_ALLOCATION_TYPES)
        sig->instance_allocation_type = mode;
}

mapper_signal_instance msig_get_instance(mapper_signal sig,
                                         mapper_instance_allocation_type mode)
{
    if (!sig)
        return 0;

    mapper_signal_instance si = sig->reserve;
    if (si) {
        sig->reserve = si->next;
        si->next = sig->active;
        sig->active = si;
        si->is_active = 1;
        si->history.position = -1;
        lo_timetag_now(&si->creation_time);
        return si;
    }

    // If no reserved instance is available, steal an active instance
    si = sig->active;
    mapper_signal_instance stolen = si;
    if (si && mode) {
        switch (mode) {
            case IN_STEAL_OLDEST:
                while (si) {
                    if ((si->creation_time.sec < stolen->creation_time.sec) ||
                        (si->creation_time.sec == stolen->creation_time.sec &&
                         si->creation_time.frac < stolen->creation_time.frac))
                        stolen = si;
                    si = si->next;
                }
                return stolen;
                break;
            case IN_STEAL_NEWEST:
                while (si) {
                    if ((si->creation_time.sec > stolen->creation_time.sec) ||
                        (si->creation_time.sec == stolen->creation_time.sec &&
                         si->creation_time.frac > stolen->creation_time.frac))
                        stolen = si;
                    si = si->next;
                }
                return stolen;
                break;
            default:
                return 0;
                break;
        }
    }
    return 0;
}

mapper_signal_instance msig_get_instance_by_id(mapper_signal sig, int id)
{
    if (!sig)
        return 0;
    if (id < 0)
        return 0;
    // find signal instance
    mapper_signal_instance si = sig->active;
    while (si) {
        if (si->id == id) {
            return si;
        }
        si = si->next;
    }
    // check reserved instances
    si = sig->reserve;
    while (si) {
        if (si->id == id) {
            msig_resume_instance(si);
            return si;
        }
        si = si->next;
    }
    return 0;
}

int msig_get_instance_id(mapper_signal_instance si)
{
    if (!si)
        return 0;
    return si->id;
}

void msig_remove_instance(mapper_signal_instance si)
{
    if (!si) return;

    // do not allow instance 0 to be removed
    if (si->id == 0) return;

    // First send zero signal
    msig_update_instance(si, NULL);

    // Remove connection instances
    mapper_connection_instance ci;
    while (si->connections) {
        ci = si->connections;
        si->connections = ci->next;
        msig_free_connection_instance(ci);
    }

    // Remove signal instance
    mapper_signal_instance *msi = &si->signal->active;
    while (*msi) {
        if (*msi == si) {
            *msi = si->next;
            --si->signal->props.instances;
            msig_free_instance(si);
            break;
        }
        msi = &(*msi)->next;
    }
}

void msig_reallocate_instances(mapper_signal sig)
{
    // At least for now, exit if this is an input signal
    if (!sig->props.is_output)
        return;

    // Find maximum input length needed for connections
    int input_history_size = 1;
    // Iterate through instances
    mapper_signal_instance si = sig->active;
    while (si) {
        mapper_connection_instance ci = si->connections;
        while (ci) {
            if (ci->connection->props.mode == MO_EXPRESSION) {
                if (ci->connection->expr->input_history_size > input_history_size) {
                    input_history_size = ci->connection->expr->input_history_size;
                }
            }
            ci = ci->next;
        }
        si = si->next;
    }
    sig->props.history_size = input_history_size;
    si = sig->active;
    int sample_size = msig_vector_bytes(sig);
    while (si) {
        // Check if input history size has changed
        if ((si->history.size != input_history_size) && (input_history_size > 0)) {
            mapper_signal_history_t history;
            history.value = calloc(1, sample_size * input_history_size);
            history.timetag = calloc(1, sizeof(mapper_timetag_t)
                                     * input_history_size);
            if (si->history.size > input_history_size) {
                // copying data into smaller array
                if (si->history.position < input_history_size) {
                    memcpy(history.value, si->history.value,
                           sample_size * si->history.position);
                    memcpy(history.value + sample_size * si->history.position,
                           si->history.value + sample_size
                           * (si->history.size - input_history_size + si->history.position),
                           sample_size * (input_history_size - si->history.position));
                    memcpy(history.timetag, si->history.timetag,
                           sizeof(mapper_timetag_t) * si->history.position);
                    memcpy(&history.timetag[si->history.position],
                           &si->history.timetag[si->history.size - input_history_size
                           + si->history.position], sizeof(mapper_timetag_t)
                           * (input_history_size - si->history.position));
                }
                else {
                    memcpy(history.value, si->history.value + sample_size
                           * (si->history.position - input_history_size),
                           sample_size * input_history_size);
                    memcpy(history.timetag,
                           &si->history.timetag[si->history.position - input_history_size],
                           sizeof(mapper_timetag_t) * input_history_size);
                    si->history.position = input_history_size - 1;
                }

            }
            else {
                // copying data into larger array
                memcpy(history.value, si->history.value,
                       sample_size * si->history.position);
                memcpy(history.value + sample_size
                       * (input_history_size - si->history.size + si->history.position),
                       si->history.value + sample_size * si->history.position,
                       sample_size * (si->history.size - si->history.position));
                memcpy(history.timetag, si->history.timetag,
                       sizeof(mapper_timetag_t) * si->history.position);
                memcpy(&history.timetag[input_history_size - si->history.size
                       + si->history.position],
                       &si->history.timetag[si->history.position], sizeof(mapper_timetag_t)
                       * (si->history.size - si->history.position));
            }
            si->history.size = input_history_size;
            free(si->history.value);
            free(si->history.timetag);
            si->history.value = history.value;
            si->history.timetag = history.timetag;
        }
        // Check if output history sizes have changed
        mapper_connection_instance ci = si->connections;
        while (ci) {
            if (ci->connection->props.mode == MO_EXPRESSION) {
                if ((ci->history.size != ci->connection->expr->output_history_size)
                    && (ci->connection->expr->output_history_size > 0)) {
                    ci->history.size = ci->connection->expr->output_history_size;
                    mapper_signal_history_t history;
                    history.value = calloc(1, mapper_type_size(ci->history.type)
                                           * ci->connection->props.dest_length
                                           * ci->history.size);
                    history.timetag = calloc(1, sizeof(mapper_timetag_t)
                                             * ci->history.size);
                    /* We could copy output history data to the new arrays here,
                     * but this would result in initializing IIR filters with
                     * unexpected/unwanted data. Leaving them initialized to zero
                     * will result in more easily-predictable results. */
                    ci->history.position = -1;
                    free(ci->history.value);
                    free(ci->history.timetag);
                    ci->history.value = history.value;
                    ci->history.timetag = history.timetag;
                }
            }
            ci = ci->next;
        }
        si = si->next;
    }
}

void msig_update_instance(mapper_signal_instance si, void *value)
{
    if (!si) return;
    if (!si->signal) return;
    if (!si->is_active) return;

    /* We have to assume that value points to an array of correct type
     * and size. */

    if (value) {
        si->history.position = (si->history.position + 1)
                                % si->history.size;
        memcpy(msig_history_value_pointer(si->history),
               value, msig_vector_bytes(si->signal));
        lo_timetag_now(&si->history.timetag[si->history.position]);
    }
    else {
        si->history.position = -1;
    }
    if (si->signal->props.is_output)
        msig_send_instance(si);
}

mapper_signal msig_instance_get_signal(mapper_signal_instance si)
{
    if (si)
        return si->signal;
    else
        return 0;
}

void msig_free_instance(mapper_signal_instance si)
{
    if (!si)
        return;
    mapper_connection_instance ci;
    while (si->connections) {
        ci = si->connections;
        si->connections = ci->next;
        msig_free_connection_instance(ci);
    }
    if (si->history.value)
        free(si->history.value);
    if (si->history.timetag)
        free(si->history.timetag);
    free(si);
}

void msig_free_connection_instance(mapper_connection_instance ci)
{
    if (!ci)
        return;
    if (ci->history.value)
        free(ci->history.value);
    if (ci->history.timetag)
        free(ci->history.timetag);
    free(ci);
}

void msig_send_instance(mapper_signal_instance si)
{
    // for each connection, construct a mapped signal and send it
    mapper_connection_instance ci = si->connections;
    if (si->history.position == -1) {
        while (ci) {
            ci->history.position = -1;
            mapper_router_send_signal(ci);
            ci = ci->next;
        }
        return;
    }
    while (ci) {
        if (mapper_connection_perform(ci->connection, &si->history, &ci->history))
            if (mapper_clipping_perform(ci->connection, &ci->history))
                mapper_router_send_signal(ci);
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
