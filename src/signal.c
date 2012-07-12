
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <assert.h>

#include "mapper_internal.h"
#include "types_internal.h"
#include <mapper/mapper.h>

#define MAX_INSTANCES 100

static void msig_update_instance_internal(mapper_signal_instance si,
                                          int send_as_instance, void *value);

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
    sig->instance_overflow_handler = 0;
    sig->props.instances = 0;
    sig->props.user_data = user_data;
    msig_set_minimum(sig, minimum);
    msig_set_maximum(sig, maximum);
    sig->instance_allocation_type = IN_UNDEFINED;

    // Reserve one instance to start
    sig->instances = 0;
    msig_reserve_instances(sig, 1);
    return sig;
}

mapper_db_signal msig_properties(mapper_signal sig)
{
    return &sig->props;
}

static void *msig_instance_value_internal(mapper_signal_instance si,
                                          mapper_timetag_t *timetag)
{
    if (!si) return 0;
    if (si->history.position == -1)
        return 0;
    if (timetag)
        timetag = &si->history.timetag[si->history.position];
    return msig_history_value_pointer(si->history);
}

void *msig_instance_value(mapper_signal sig,
                          int instance_id,
                          mapper_timetag_t *timetag)
{
    mapper_signal_instance si = msig_find_instance_with_id(sig, instance_id);
    if (si)
        return msig_instance_value_internal(si, timetag);
    return 0;
}

void *msig_value(mapper_signal sig,
                 mapper_timetag_t *timetag)
{
    if (!sig) return 0;
    if (!sig->instances) return 0;
    return msig_instance_value_internal(sig->instances, timetag);
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

void msig_set_query_callback(mapper_signal sig,
                             mapper_signal_handler *handler,
                             void *user_data)
{
    if (!sig || !sig->props.is_output)
        return;
    if (!sig->handler && handler) {
        // Need to register a new liblo handler
        mdev_add_signal_query_response_callback(sig->device, sig);
    }
    else if (sig->handler && !handler) {
        // Need to remove liblo query handler
        mdev_remove_signal_query_response_callback(sig->device, sig);
    }
    sig->handler = handler;
    sig->props.user_data = user_data;
}

void msig_free(mapper_signal sig)
{
    if (!sig) return;

    // Free instances
    mapper_signal_instance si;
    while (sig->instances) {
        si = sig->instances;
        sig->instances = si->next;
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

    if (sig && sig->instances)
        msig_update_instance_internal(sig->instances, 0, &value);
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

    if (sig && sig->instances)
        msig_update_instance_internal(sig->instances, 0, &value);
}

void msig_update(mapper_signal sig, void *value)
{
    if (sig && sig->instances)
        msig_update_instance_internal(sig->instances, 0, value);
}

static void msig_instance_init(mapper_signal_instance si,
                               mapper_instance_id_map id_map)
{
    si->history.position = -1;
    lo_timetag_now(&si->creation_time);
    if (id_map) {
        si->id_map->local = id_map->local;
        si->id_map->group = id_map->group;
        si->id_map->remote = id_map->remote;
    }
}

mapper_signal_instance msig_find_instance_with_id(mapper_signal sig,
                                                  int id)
{
    // TODO: hash table, binary search, etc.
    mapper_signal_instance si = sig->instances;
    while (si && (si->id_map->local != id)) {
        si = si->next;
    }
    return si;
}

mapper_signal_instance msig_find_instance_with_id_map(mapper_signal sig,
                                                      int group_id,
                                                      int remote_id)
{
    // TODO: hash table, binary search, etc.
    mapper_signal_instance si = sig->instances;
    while (si && ((si->id_map->remote != remote_id) || (si->id_map->group != group_id))) {
        si = si->next;
    }
    return si;
}

void msig_instance_set_data(mapper_signal sig,
                            int instance_id,
                            void *user_data)
{
    mapper_signal_instance si = msig_get_instance_with_id(sig, instance_id, 0);
    if (si)
        si->user_data = user_data;
}

void *msig_instance_get_data(mapper_signal sig,
                             int instance_id)
{
    mapper_signal_instance si = msig_find_instance_with_id(sig, instance_id);
    if (si)
        return si->user_data;
    return 0;
}

void msig_reserve_instances(mapper_signal sig, int num)
{
    if (!sig)
        return;
    int available = MAX_INSTANCES - sig->props.instances;
    if (num > available)
        num = available;
    int i;
    for (i = 0; i < num; i++) {
        mapper_signal_instance si = (mapper_signal_instance) calloc(1,
                                    sizeof(struct _mapper_signal_instance));
        si->history.type = sig->props.type;
        si->history.size = sig->props.history_size > 1 ? sig->props.history_size : 1;
        si->history.length = sig->props.length;
        // allocate history vectors
        si->history.value = calloc(1, msig_vector_bytes(sig) * si->history.size);
        si->history.timetag = calloc(1, sizeof(mapper_timetag_t) * si->history.size);
        si->signal = sig;
        si->is_active = 0;
        si->id_map = calloc(1, sizeof(struct _mapper_instance_id_map));
        si->id_map->local = sig->props.instances++;
        msig_instance_init(si, 0);
        si->user_data = 0;

        si->next = sig->instances;
        sig->instances = si;
        si->connections = 0;

        if (!sig->device)
            continue;

        // add connection instances to signal instance
        // for each router...
        mapper_router router = sig->device->routers;
        while (router) {
            // ...find signal connection
            mapper_signal_connection sc = router->outgoing;
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
            router = router->next;
        }
    }
}

mapper_signal_instance msig_get_instances(mapper_signal sig)
{
    if (sig)
        return sig->instances;
    else
        return 0;
}

mapper_signal_instance msig_instance_next(mapper_signal_instance si)
{
    if (si)
        return si->next;
    else
        return 0;
}

int msig_num_active_instances(mapper_signal sig)
{
    if (!sig)
        return -1;
    mapper_signal_instance si = sig->instances;
    int i = 0;
    while (si) {
        if (si->is_active)
            i++;
        si = si->next;
    }
    return i;
}

int msig_num_reserved_instances(mapper_signal sig)
{
    if (!sig)
        return -1;
    mapper_signal_instance si = sig->instances;
    int i = 0;
    while (si) {
        if (!si->is_active)
            i++;
        si = si->next;
    }
    return i;
}

int msig_active_instance_id(mapper_signal sig, int index)
{
    int i = -1;
    mapper_signal_instance si = sig->instances;
    while (si) {
        if (si->is_active)
            i++;
        if (i >= index)
            break;
        si = si->next;
    }
    return si->id_map->local;
}

mapper_connection_instance msig_add_connection_instance(mapper_signal_instance si,
                                                        mapper_connection c)
{
    mapper_connection_instance ci = (mapper_connection_instance) calloc(1,
                                    sizeof(struct _mapper_connection_instance));
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

void msig_release_instance(mapper_signal sig, int instance_id)
{
    if (!sig)
        return;
    mapper_signal_instance si = msig_find_instance_with_id(sig, instance_id);
    if (si)
        msig_release_instance_internal(si);
}

void msig_release_instance_internal(mapper_signal_instance si)
{
    if (!si)
        return;
    if (!si->is_active)
        return;
    if (si->id_map->group == mdev_port(si->signal->device) &&
        si->signal->props.is_output) {
        // First send zero signal
        msig_update_instance_internal(si, 1, NULL);
    }
    si->is_active = 0;
}

void msig_set_instance_allocation_mode(mapper_signal sig,
                                       mapper_instance_allocation_type mode)
{
    if (sig && (mode >= 0) && (mode < N_MAPPER_INSTANCE_ALLOCATION_TYPES))
        sig->instance_allocation_type = mode;
}

void msig_set_instance_overflow_handler(mapper_signal sig,
                                        mapper_signal_instance_overflow_handler h)
{
    sig->instance_overflow_handler = h;
}

mapper_signal_instance msig_get_instance_with_id(mapper_signal sig,
                                                 int instance_id,
                                                 int is_new_instance)
{
    if (!sig)
        return 0;

    mapper_instance_id_map id_map = mdev_find_instance_id_map_by_local(sig->device,
                                                                       instance_id);
    mapper_signal_instance si = msig_find_instance_with_id(sig, instance_id);
    if (si) {
        if (!si->is_active) {
            if (!id_map) {
                // Claim ID map locally
                id_map = mdev_set_instance_id_map(sig->device, instance_id,
                                                  mdev_port(sig->device), instance_id);
            }
            msig_instance_init(si, id_map);
        }
        return si;
    }

    // no instance with that ID exists - need to try to activate instance and create new ID map
    si = sig->instances;
    while (si) {
        if (!si->is_active)
            break;
        si = si->next;
    }
    if (si) {
        if (!id_map) {
            // Claim ID map locally
            id_map = mdev_set_instance_id_map(sig->device, instance_id,
                                              mdev_port(sig->device), instance_id);
        }
        msig_instance_init(si, id_map);
        return si;
    }

    if (!is_new_instance) {
        // Do not allow stealing unless this is a new instance
        return 0;
    }

    // If no reserved instance is available, steal an active instance
    si = sig->instances;
    mapper_signal_instance stolen = si;
    mapper_instance_allocation_type mode = sig->instance_allocation_type;
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
                goto stole;
            case IN_STEAL_NEWEST:
                while (si) {
                    if ((si->creation_time.sec > stolen->creation_time.sec) ||
                        (si->creation_time.sec == stolen->creation_time.sec &&
                         si->creation_time.frac > stolen->creation_time.frac))
                        stolen = si;
                    si = si->next;
                }
                goto stole;
            default:
                trace("Unknown instance allocation strategy (msig_get_instance)\n");
                return 0;
        }
    }
    return 0;

  stole:
    /* value = NULL signifies release of the instance */
    if (sig->handler)
        sig->handler(
            sig, stolen->id_map->local, &sig->props,
            &stolen->history.timetag[stolen->history.position],
            NULL);
    msig_release_instance_internal(stolen);
    if (!id_map) {
        // Claim ID map locally
        id_map = mdev_set_instance_id_map(sig->device, instance_id,
                                          mdev_port(sig->device), instance_id);
    }
    msig_instance_init(stolen, id_map);
    return stolen;
}

mapper_signal_instance msig_get_instance_with_id_map(mapper_signal sig,
                                                     int group_id,
                                                     int remote_id,
                                                     int is_new_instance)
{
    if (!sig)
        return 0;

    mapper_signal_instance si = 0;
    mapper_instance_id_map id_map = 0;

    si = msig_find_instance_with_id_map(sig, group_id, remote_id);
    if (si) {
        if (!si->is_active) {
            si->is_active = 1;
            msig_instance_init(si, id_map);
            return si;
        }
        return si;
    }

    id_map = mdev_find_instance_id_map_by_remote(sig->device, group_id, remote_id);

    // no ID-mapped instance exists - need to try to activate instance and create new ID map
    si = sig->instances;
    while (si) {
        if (!si->is_active)
            break;
        si = si->next;
    }
    if (si) {
        si->is_active = 1;
        if (!id_map)
            id_map = mdev_set_instance_id_map(sig->device, si->id_map->local,
                                              group_id, remote_id);
        msig_instance_init(si, id_map);
        return si;
    }

    if (!is_new_instance) {
        // Do not allow stealing unless this is a new instance
        return 0;
    }

    // If no reserved instance is available, steal an active instance
    si = sig->instances;
    mapper_signal_instance stolen = si;
    mapper_instance_allocation_type mode = sig->instance_allocation_type;
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
                goto stole;
            case IN_STEAL_NEWEST:
                while (si) {
                    if ((si->creation_time.sec > stolen->creation_time.sec) ||
                        (si->creation_time.sec == stolen->creation_time.sec &&
                         si->creation_time.frac > stolen->creation_time.frac))
                        stolen = si;
                    si = si->next;
                }
                goto stole;
            default:
                trace("Unknown instance allocation strategy (msig_get_instance)\n");
                return 0;
        }
    }
    return 0;

stole:
    /* value = NULL signifies release of the instance */
    if (sig->handler)
        sig->handler(sig, stolen->id_map->local, &sig->props,
                     &stolen->history.timetag[stolen->history.position],
                     NULL);
    msig_release_instance_internal(stolen);
    if (!id_map)
        id_map = mdev_set_instance_id_map(sig->device, stolen->id_map->local,
                                          group_id, remote_id);
    msig_instance_init(stolen, id_map);
    stolen->is_active = 1;
    return stolen;
}

void msig_remove_instance(mapper_signal_instance si)
{
    if (!si) return;

    // First send zero signal
    msig_update_instance_internal(si, 1, NULL);

    // Remove connection instances
    mapper_connection_instance ci;
    while (si->connections) {
        ci = si->connections;
        si->connections = ci->next;
        msig_free_connection_instance(ci);
    }

    // Remove signal instance
    mapper_signal_instance *msi = &si->signal->instances;
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

void msig_reallocate_instances(mapper_signal sig, int input_history_size,
                               mapper_connection c, int output_history_size)
{
    // At least for now, exit if this is an input signal
    if (!sig->props.is_output)
        return;

    // If there is no expression, then no memory needs to be
    // reallocated.
    if (!c->expr)
        return;

    if (input_history_size < 1)
        input_history_size = 1;
    mapper_signal_instance si;
    if (input_history_size > sig->props.history_size) {
        si = sig->instances;
        int sample_size = msig_vector_bytes(sig);
        while (si) {
            mhist_realloc(&si->history, input_history_size, sample_size, 0);
            si = si->next;
        }
        sig->props.history_size = input_history_size;
    }
    else if (input_history_size < sig->props.history_size) {
        // Do nothing for now...
        // Find maximum input length needed for connections
        /*si = sig->active;
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
        }*/
    }

    if (output_history_size > mapper_expr_output_history_size(c->expr)) {
        si = sig->instances;
        while (si) {
            mapper_connection_instance ci = si->connections;
            while (ci) {
                if (ci->connection == c) {
                    int sample_size = mapper_type_size(ci->history.type)
                                      * ci->connection->props.dest_length;
                    mhist_realloc(&ci->history, output_history_size, sample_size, 1);
                }
                ci = ci->next;
            }
            si = si->next;
        }
    }
    else if (output_history_size < mapper_expr_output_history_size(c->expr)) {
        // Do nothing for now...
    }
}

void mhist_realloc(mapper_signal_history_t *history, int history_size, int sample_size, int is_output)
{
    if (!history || !history_size || !sample_size)
        return;
    if (history_size == history->size)
        return;
    if (is_output || (history_size > history->size) || (history->position == 0)) {
        // realloc in place
        history->value = realloc(history->value, history_size * sample_size);
        history->timetag = realloc(history->timetag, history_size * sizeof(mapper_timetag_t));
        if (is_output) {
            history->position = -1;
        }
        else if (history->position != 0) {
            int new_position = history_size - history->size + history->position;
            memcpy(history->value + sample_size * new_position,
                   history->value + sample_size * history->position,
                   sample_size * (history->size - history->position));
            memcpy(&history->timetag[new_position],
                   &history->timetag[history->position], sizeof(mapper_timetag_t)
                   * (history->size - history->position));
            history->position = new_position;
        }
    }
    else {
        // copying into smaller array
        if (history->position >= history_size * 2) {
            // no overlap - memcpy ok
            int new_position = history_size - history->size + history->position;
            memcpy(history->value,
                   history->value + sample_size * (new_position - history_size),
                   sample_size * history_size);
            memcpy(&history->timetag,
                   &history->timetag[history->position - history_size],
                   sizeof(mapper_timetag_t) * history_size);
            history->value = realloc(history->value, history_size * sample_size);
            history->timetag = realloc(history->timetag, history_size * sizeof(mapper_timetag_t));
        }
        else {
            // there is overlap between new and old arrays - need to allocate new memory
            mapper_signal_history_t temp;
            temp.value = malloc(sample_size * history_size);
            temp.timetag = malloc(sizeof(mapper_timetag_t) * history_size);
            if (history->position < history_size) {
                memcpy(temp.value, history->value,
                       sample_size * history->position);
                memcpy(temp.value + sample_size * history->position,
                       history->value + sample_size
                       * (history->size - history_size + history->position),
                       sample_size * (history_size - history->position));
                memcpy(temp.timetag, history->timetag,
                       sizeof(mapper_timetag_t) * history->position);
                memcpy(&temp.timetag[history->position],
                       &history->timetag[history->size - history_size + history->position],
                       sizeof(mapper_timetag_t) * (history_size - history->position));
            }
            else {
                memcpy(temp.value, history->value + sample_size
                       * (history->position - history_size),
                       sample_size * history_size);
                memcpy(temp.timetag,
                       &history->timetag[history->position - history_size],
                       sizeof(mapper_timetag_t) * history_size);
                history->position = history_size - 1;
            }
            free(history->value);
            free(history->timetag);
            history->value = temp.value;
            history->timetag = temp.timetag;
        }
    }
    history->size = history_size;
}

void msig_start_new_instance(mapper_signal sig, int instance_id)
{
    if (!sig)
        return;

    mapper_signal_instance si = msig_get_instance_with_id(sig, instance_id, 1);
    if (!si && sig->instance_overflow_handler) {
        sig->instance_overflow_handler(sig, 0, instance_id);
        // try again
        si = msig_get_instance_with_id(sig, instance_id, 1);
    }
}

void msig_update_instance(mapper_signal sig,
                          int instance_id,
                          void *value)
{
    if (!sig)
        return;

    if (!value) {
        msig_release_instance(sig, instance_id);
        return;
    }

    mapper_signal_instance si = msig_get_instance_with_id(sig, instance_id, 0);
    if (!si && sig->instance_overflow_handler) {
        sig->instance_overflow_handler(sig, 0, instance_id);
        // try again
        si = msig_get_instance_with_id(sig, instance_id, 0);
    }
    if (si)
        msig_update_instance_internal(si, 1, value);
}

static
void msig_update_instance_internal(mapper_signal_instance si,
                                   int send_as_instance, void *value)
{
    if (!si) return;
    if (!si->signal) return;

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
        msig_send_instance(si, send_as_instance);
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

void msig_send_instance(mapper_signal_instance si, int send_as_instance)
{
    // for each connection, construct a mapped signal and send it
    mapper_connection_instance ci = si->connections;
    int is_new = 0;
    if (si->history.position == -1) {
        while (ci) {
            ci->history.position = -1;
            if (!send_as_instance)
                mapper_router_send_signal(ci, send_as_instance);
            else {
                if (mapper_router_in_group(ci->connection->router, si->id_map->group))
                    mapper_router_send_signal(ci, send_as_instance);
            }
            ci = ci->next;
        }
        return;
    }
    else if (!si->is_active) {
        is_new = 1;
        si->is_active = 1;
    }
    while (ci) {
        if (send_as_instance && !mapper_router_in_group(ci->connection->router, si->id_map->group)) {
            ci = ci->next;
            continue;
        }
        if (mapper_connection_perform(ci->connection, &si->history, &ci->history)) {
            if (mapper_clipping_perform(ci->connection, &ci->history)) {
                if (send_as_instance && is_new)
                    mapper_router_send_new_instance(ci);
                mapper_router_send_signal(ci, send_as_instance);
            }
        }
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

int msig_query_remote(mapper_signal sig)
{
    // stick to output signals for now
    // TODO: process queries on inputs also
    if (!sig->props.is_output)
        return -1;
    if (!sig->device->server) {
        // no server running so we cannot process query returns
        // TODO: start server if necessary
        return -1;
    }
    if (!sig->handler) {
        // no handler defined so we cannot process query returns
        return -1;
    }
    return mdev_route_query(sig->device, sig);
}
