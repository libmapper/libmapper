
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <assert.h>

#include "mapper_internal.h"
#include "types_internal.h"
#include <mapper/mapper.h>

#define MAX_INSTANCES 100

static void msig_update_internal(mapper_signal sig,
                                 mapper_signal_instance si,
                                 void *value,
                                 int count,
                                 int send_as_instance,
                                 mapper_timetag_t timetag);

static void *msig_instance_value_internal(mapper_signal sig,
                                          mapper_signal_instance si,
                                          mapper_timetag_t *timetag);

static void msig_free_instance(mapper_signal sig,
                               mapper_signal_instance si);

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
    sig->instance_management_handler = 0;
    sig->props.num_instances = 0;
    sig->props.user_data = user_data;
    msig_set_minimum(sig, minimum);
    msig_set_maximum(sig, maximum);
    sig->instance_allocation_type = IN_UNDEFINED;

    // Reserve one instance to start
    sig->active_instances = 0;
    sig->reserve_instances = 0;
    msig_reserve_instances(sig, 1);
    return sig;
}

void msig_free(mapper_signal sig)
{
    if (!sig) return;

    // Free instances
    mapper_signal_instance si;
    while (sig->active_instances) {
        si = sig->active_instances;
        sig->active_instances = si->next;
        msig_free_instance(sig, si);
    }
    while (sig->reserve_instances) {
        si = sig->reserve_instances;
        sig->reserve_instances = si->next;
        msig_free_instance(sig, si);
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

void msig_update(mapper_signal sig, void *value,
                 int count, mapper_timetag_t tt)
{
    if (!sig)
        return;
    if (!sig->active_instances)
        msig_get_instance_with_id(sig, 0, 1);
    msig_update_internal(sig, sig->active_instances,
                         value, count, 0, tt);
}

void msig_update_int(mapper_signal sig, int value)
{
    if (!sig)
        return;

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

    if (!sig->active_instances)
        msig_get_instance_with_id(sig, 0, 1);
    msig_update_internal(sig, sig->active_instances, &value,
                         1, 0, MAPPER_TIMETAG_NOW);
}

void msig_update_float(mapper_signal sig, float value)
{
    if (!sig)
        return;

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

    if (!sig->active_instances)
        msig_get_instance_with_id(sig, 0, 1);
    msig_update_internal(sig, sig->active_instances, &value,
                         1, 0, MAPPER_TIMETAG_NOW);
}

void *msig_value(mapper_signal sig,
                 mapper_timetag_t *timetag)
{
    if (!sig) return 0;
    if (!sig->active_instances) return 0;
    return msig_instance_value_internal(sig, sig->active_instances, timetag);
}

/**** Instances ****/

static void msig_init_instance(mapper_signal_instance si,
                               mapper_instance_id_map id_map)
{
    si->has_value = 0;
    lo_timetag_now(&si->creation_time);
    si->id_map = id_map;
}

mapper_signal_instance msig_find_instance_with_id(mapper_signal sig,
                                                  int id)
{
    // TODO: hash table, binary search, etc.
    mapper_signal_instance si = sig->active_instances;
    while (si) {
        if (si->id_map && (si->id_map->local == id))
            break;
        si = si->next;
    }
    return si;
}

mapper_signal_instance msig_find_instance_with_id_map(mapper_signal sig,
                                                      mapper_instance_id_map map)
{
    // TODO: hash table, binary search, etc.
    mapper_signal_instance si = sig->active_instances;
    while (si) {
        if (si->id_map == map)
            break;
        si = si->next;
    }
    return si;
}

mapper_signal_instance msig_get_instance_with_id(mapper_signal sig,
                                                 int instance_id,
                                                 int is_new_instance)
{
    if (!sig)
        return 0;

    mapper_signal_instance si;
    mapper_instance_id_map map = mdev_find_instance_id_map_by_local(sig->device,
                                                                    instance_id);
    if (map) {
        if ((si = msig_find_instance_with_id_map(sig, map)))
            return si;
    }

    // no instance with that ID exists - need to try to activate instance and create new ID map
    if ((si = sig->reserve_instances)) {
        if (!map) {
            // Claim ID map locally
            map = mdev_add_instance_id_map(sig->device, instance_id,
                                           mdev_id(sig->device), sig->device->id_counter++);
        }
        else {
            map->reference_count++;
        }
        sig->reserve_instances = si->next;
        si->next = sig->active_instances;
        sig->active_instances = si;
        msig_init_instance(si, map);
        return si;
    }

    if (!is_new_instance) {
        // Do not allow stealing unless this is a new instance
        return 0;
    }

    // If no reserved instance is available, steal an active instance
    si = sig->active_instances;
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
                trace("Unknown instance allocation strategy (msig_get_instance_with_id)\n");
                return 0;
        }
    }
    return 0;

stole:
    /* value = NULL signifies release of the instance */
    if (sig->handler) {
        // TODO: should use current time for timetag?
        sig->handler(sig, stolen->id_map->local, &sig->props, 0, NULL);
    }
    msig_release_instance_internal(sig, stolen, 1, MAPPER_TIMETAG_NOW);
    if (!map) {
        // Claim id map locally
        map = mdev_add_instance_id_map(sig->device, instance_id,
                                       mdev_id(sig->device), sig->device->id_counter++);
    }
    else {
        map->reference_count++;
    }
    msig_init_instance(stolen, map);
    stolen->is_active = 0;
    return stolen;
}

mapper_signal_instance msig_get_instance_with_id_map(mapper_signal sig,
                                                     mapper_instance_id_map map,
                                                     int is_new_instance)
{
    // If the map pointer is null, we need to create a new map if necessary
    if (!sig)
        return 0;

    mapper_signal_instance si = 0;

    if (map) {
        if ((si = msig_find_instance_with_id_map(sig, map)))
            return si;
    }

    // no ID-mapped instance exists - need to try to activate instance and create new ID map
    if ((si = sig->reserve_instances)) {
        if (map) {
            map->reference_count++;
        }
        sig->reserve_instances = si->next;
        si->next = sig->active_instances;
        sig->active_instances = si;
        msig_init_instance(si, map);
        si->is_active = 1;
        return si;
    }

    if (!is_new_instance) {
        // Do not allow stealing unless this is a new instance
        return 0;
    }

    // If no reserved instance is available, steal an active instance
    si = sig->active_instances;
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
                trace("Unknown instance allocation strategy (msig_get_instance_with_id_map)\n");
                return 0;
        }
    }
    return 0;

stole:
    /* value = NULL signifies release of the instance */
    if (sig->handler) {
        // TODO: should use current time for timetag?
        sig->handler(sig, stolen->id_map->local, &sig->props, 0, NULL);
    }
    msig_release_instance_internal(sig, stolen, 1, MAPPER_TIMETAG_NOW);
    if (map) {
        map->reference_count++;
    }
    msig_init_instance(stolen, map);
    return stolen;
}

void msig_reserve_instances(mapper_signal sig, int num)
{
    if (!sig)
        return;
    int available = MAX_INSTANCES - sig->props.num_instances;
    if (num > available)
        num = available;
    int i;
    for (i = 0; i < num; i++) {
        mapper_signal_instance si = (mapper_signal_instance) calloc(1,
                                    sizeof(struct _mapper_signal_instance));
        si->value = calloc(1, msig_vector_bytes(sig));
        si->has_value = 0;
        si->is_active = 0;
        si->id = sig->props.num_instances++;
        msig_init_instance(si, 0);
        si->user_data = 0;

        si->next = sig->reserve_instances;
        sig->reserve_instances = si;
    }
    mdev_num_instances_changed(sig->device, sig);
}

void msig_start_new_instance(mapper_signal sig, int instance_id)
{
    if (!sig)
        return;

    mapper_signal_instance si = msig_get_instance_with_id(sig, instance_id, 1);
    if (!si && sig->instance_management_handler) {
        sig->instance_management_handler(sig, -1, &sig->props, IN_OVERFLOW);
        // try again
        si = msig_get_instance_with_id(sig, instance_id, 1);
    }
}

void msig_update_instance(mapper_signal sig, int instance_id,
                          void *value, int count, mapper_timetag_t timetag)
{
    if (!sig)
        return;

    if (!value) {
        msig_release_instance(sig, instance_id, timetag);
        return;
    }

    mapper_signal_instance si = msig_get_instance_with_id(sig, instance_id, 0);
    if (!si && sig->instance_management_handler) {
        sig->instance_management_handler(sig, -1, &sig->props, IN_OVERFLOW);
        // try again
        si = msig_get_instance_with_id(sig, instance_id, 0);
    }
    if (si)
        msig_update_internal(sig, si, value, count, 1, timetag);
}

static void msig_update_internal(mapper_signal sig,
                                 mapper_signal_instance si,
                                 void *value,
                                 int count,
                                 int send_as_instance,
                                 mapper_timetag_t timetag)
{
    if (!si) return;

    /* We have to assume that value points to an array of correct type
     * and size. */

    if (value) {
        if (count==0) count=1;
        size_t n = msig_vector_bytes(sig);
        memcpy(si->value, value + n*(count-1), n);
        si->has_value = 1;
    }
    else {
        si->has_value = 0;
    }
    if (sig->props.is_output) {
        int flags = FLAGS_SEND_AS_INSTANCE * send_as_instance;
        mdev_route_signal(sig->device, sig, si, value, count, timetag, flags);
    }
}

void msig_release_instance(mapper_signal sig, int instance_id,
                           mapper_timetag_t timetag)
{
    if (!sig)
        return;
    mapper_signal_instance si = msig_find_instance_with_id(sig, instance_id);
    if (si)
        msig_release_instance_internal(sig, si, 0, timetag);
}

void msig_release_instance_internal(mapper_signal sig,
                                    mapper_signal_instance si,
                                    int stolen,
                                    mapper_timetag_t timetag)
{
    if (!si || !si->is_active)
        return;

    if (sig->props.is_output) {
        // Send release notification to remote devices
        msig_update_internal(sig, si, NULL, 0, 1, timetag);
    }

    if (si->id_map && --si->id_map->reference_count <= 0)
        mdev_remove_instance_id_map(sig->device, si->id_map);

    if (stolen) {
        if (sig->instance_management_handler)
            sig->instance_management_handler(sig, si->id_map->local,
                                             &sig->props, IN_STOLEN);
    }
    else {
        si->is_active = 0;
        // Remove instance from active list, place in reserve
        mapper_signal_instance *msi = &sig->active_instances;
        while (*msi) {
            if (*msi == si) {
                *msi = si->next;
                si->next = sig->reserve_instances;
                sig->reserve_instances = si;
                break;
            }
            msi = &(*msi)->next;
        }
    }
}

void msig_remove_instance(mapper_signal sig,
                          mapper_signal_instance si)
{
    if (!si) return;

    // First release instance
    msig_release_instance_internal(sig, si, 0, MAPPER_TIMETAG_NOW);

    // Remove signal instance
    mapper_signal_instance *msi = &sig->active_instances;
    while (*msi) {
        if (*msi == si) {
            *msi = si->next;
            --sig->props.num_instances;
            msig_free_instance(sig, si);
            return;
        }
        msi = &(*msi)->next;
    }
    msi = &sig->reserve_instances;
    while (*msi) {
        if (*msi == si) {
            *msi = si->next;
            --sig->props.num_instances;
            msig_free_instance(sig, si);
            return;
        }
        msi = &(*msi)->next;
    }
}

static void msig_free_instance(mapper_signal sig,
                               mapper_signal_instance si)
{
    if (!si)
        return;
    msig_release_instance_internal(sig, si, 0, MAPPER_TIMETAG_NOW);
    if (si->value)
        free(si->value);
    free(si);
}

static void *msig_instance_value_internal(mapper_signal sig,
                                          mapper_signal_instance si,
                                          mapper_timetag_t *timetag)
{
    if (!si) return 0;
    if (!si->has_value)
        return 0;
    if (timetag)
        timetag = &si->timetag;
    return si->value;
}

void *msig_instance_value(mapper_signal sig,
                          int instance_id,
                          mapper_timetag_t *timetag)
{
    mapper_signal_instance si = msig_find_instance_with_id(sig, instance_id);
    if (si)
        return msig_instance_value_internal(sig, si, timetag);
    return 0;
}

void msig_match_instances(mapper_signal from, mapper_signal to, int instance_id)
{
    if (from == to)
        return;

    // Find from instance
    mapper_signal_instance si_from = msig_find_instance_with_id(from, instance_id);
    if (!si_from)
        return;

    // Find to instance
    mapper_signal_instance si_to = msig_get_instance_with_id(to, instance_id, 1);
    if (!si_to)
        return;

    // Copy instance ID map reference
    si_to->id_map = si_from->id_map;
}

int msig_num_active_instances(mapper_signal sig)
{
    if (!sig)
        return -1;
    mapper_signal_instance si = sig->active_instances;
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
    mapper_signal_instance si = sig->reserve_instances;
    int i = 0;
    while (si) {
        i++;
        si = si->next;
    }
    return i;
}

int msig_active_instance_id(mapper_signal sig, int index)
{
    int i = -1;
    mapper_signal_instance si = sig->active_instances;
    while (si) {
        i++;
        if (i >= index)
            break;
        si = si->next;
    }
    return si->id_map->local;
}

void msig_set_instance_allocation_mode(mapper_signal sig,
                                       mapper_instance_allocation_type mode)
{
    if (sig && (mode >= 0) && (mode < N_MAPPER_INSTANCE_ALLOCATION_TYPES))
        sig->instance_allocation_type = mode;
}

void msig_set_instance_management_callback(mapper_signal sig,
                                           mapper_signal_instance_management_handler h)
{
    sig->instance_management_handler = h;
}

void msig_set_instance_data(mapper_signal sig,
                            int instance_id,
                            void *user_data)
{
    mapper_signal_instance si = msig_get_instance_with_id(sig, instance_id, 0);
    if (si)
        si->user_data = user_data;
}

void *msig_get_instance_data(mapper_signal sig,
                             int instance_id)
{
    mapper_signal_instance si = msig_find_instance_with_id(sig, instance_id);
    if (si)
        return si->user_data;
    return 0;
}

/**** Queries ****/

void msig_set_query_callback(mapper_signal sig,
                             mapper_signal_handler *handler,
                             void *user_data)
{
    if (!sig || !sig->props.is_output)
        return;
    if (!sig->handler && handler) {
        // Need to register a new liblo handler
        sig->handler = handler;
        sig->props.user_data = user_data;
        mdev_add_signal_query_response_callback(sig->device, sig);
    }
    else if (sig->handler && !handler) {
        // Need to remove liblo query handler
        sig->handler = 0;
        sig->props.user_data = user_data;
        mdev_remove_signal_query_response_callback(sig->device, sig);
    }
}

int msig_query_remotes(mapper_signal sig, mapper_timetag_t tt)
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
    return mdev_route_query(sig->device, sig, tt);
}

/**** Signal Properties ****/

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

void msig_set_rate(mapper_signal sig, float rate)
{
    sig->props.rate = rate;
}

float msig_get_rate(mapper_signal sig)
{
    return sig->props.rate;
}


mapper_db_signal msig_properties(mapper_signal sig)
{
    return &sig->props;
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
