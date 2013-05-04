
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "mapper_internal.h"
#include "types_internal.h"
#include <mapper/mapper.h>

#define MAX_INSTANCES 128

static void msig_update_internal(mapper_signal sig,
                                 int instance_index,
                                 void *value,
                                 int count,
                                 mapper_timetag_t timetag);

static void *msig_instance_value_internal(mapper_signal sig,
                                          int instance_index,
                                          mapper_timetag_t *timetag);

static int msig_get_oldest_active_instance_internal(mapper_signal sig);

static int msig_get_newest_active_instance_internal(mapper_signal sig);

static void msig_free_instance(mapper_signal sig,
                               mapper_signal_instance si);

mapper_signal msig_new(const char *name, int length, char type,
                       int is_output, const char *unit,
                       void *minimum, void *maximum,
                       mapper_signal_update_handler *handler,
                       void *user_data)
{
    if (length < 1) return 0;
    if (!name) return 0;
    if (type != 'f' && type != 'i')
        return 0;

    mapper_signal sig =
        (mapper_signal) calloc(1, sizeof(struct _mapper_signal));

    mapper_db_signal_init(&sig->props, is_output, type, length, name, unit);
    sig->handler = handler;
    sig->props.num_instances = 0;
    sig->props.user_data = user_data;
    msig_set_minimum(sig, minimum);
    msig_set_maximum(sig, maximum);

    // Reserve one instance to start
    msig_reserve_instances(sig, 1);

    // Reserve one instance id map
    sig->id_map_length = 1;
    sig->id_maps = calloc(1, sizeof(struct _mapper_signal_id_map));

    return sig;
}

void msig_free(mapper_signal sig)
{
    int i;
    if (!sig) return;

    // Free instances
    for (i = 0; i < sig->id_map_length; i++) {
        if (sig->id_maps[i].instance) {
            msig_release_instance_internal(sig, i, MAPPER_NOW);
        }
    }
    free(sig->id_maps);
    mapper_signal_instance si;
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

    mapper_timetag_t *ttp;
    if (memcmp(&tt, &MAPPER_NOW, sizeof(mapper_timetag_t))==0) {
        ttp = &sig->device->admin->clock.now;
        mdev_timetag_now(sig->device, ttp);
    }
    else
        ttp = &tt;

    int index = 0;
    if (!sig->id_maps[0].instance)
        index = msig_get_instance_with_local_id(sig, 0, 0, ttp);
    msig_update_internal(sig, index, value, count, *ttp);
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

    mapper_timetag_t *tt = &sig->device->admin->clock.now;
    mdev_timetag_now(sig->device, tt);

    int index = 0;
    if (!sig->id_maps[0].instance)
        index = msig_get_instance_with_local_id(sig, 0, 0, tt);
    msig_update_internal(sig, index, &value, 1, *tt);
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

    mapper_timetag_t *tt = &sig->device->admin->clock.now;
    mdev_timetag_now(sig->device, tt);

    int index = 0;
    if (!sig->id_maps[0].instance)
        index = msig_get_instance_with_local_id(sig, 0, 0, tt);
    msig_update_internal(sig, index, &value, 1, *tt);
}

void *msig_value(mapper_signal sig,
                 mapper_timetag_t *timetag)
{
    if (!sig) return 0;
    int index = 0;
    if (!sig->id_maps[0].instance)
        index = msig_find_instance_with_local_id(sig, 0, 0);
    if (index < 0)
        return 0;
    return msig_instance_value_internal(sig, index, timetag);
}

/**** Instances ****/

static void msig_init_instance(mapper_signal_instance si)
{
    si->has_value = 0;
    lo_timetag_now(&si->creation_time);
}

int msig_find_instance_with_local_id(mapper_signal sig,
                                     int id, int flags)
{
    int i;
    for (i = 0; i < sig->id_map_length; i++) {
        if (sig->id_maps[i].instance && sig->id_maps[i].map->local == id) {
            if (sig->id_maps[i].status & ~flags)
                return -1;
            else
                return i;
        }
    }
    return -1;
}

int msig_find_instance_with_remote_ids(mapper_signal sig, int group,
                                       int id, int flags)
{
    int i;
    for (i = 0; i < sig->id_map_length; i++) {
        if (!sig->id_maps[i].map)
            continue;
        if (sig->id_maps[i].map->group == group && sig->id_maps[i].map->remote == id) {
            if (sig->id_maps[i].status & ~flags)
                return -1;
            else
                return i;
        }
    }
    return -1;
}

int msig_get_instance_with_local_id(mapper_signal sig, int id,
                                    int flags, mapper_timetag_t *tt)
{
    if (!sig)
        return 0;

    mapper_signal_id_map_t *maps = sig->id_maps;

    mapper_signal_instance si;
    int i;
    for (i = 0; i < sig->id_map_length; i++) {
        if (maps[i].instance && maps[i].map->local == id) {
            if (maps[i].status & ~flags)
                return -1;
            else {
                return i;
            }
        }
    }

    // check if device has record of id map
    mapper_id_map map = mdev_find_instance_id_map_by_local(sig->device, id);

    // no instance with that id exists - need to try to activate instance and create new id map
    if ((si = sig->reserve_instances)) {
        if (!map) {
            // Claim id map locally, add id map to device and link from signal
            map = mdev_add_instance_id_map(sig->device, id, mdev_id(sig->device),
                                           sig->device->id_counter++);
            map->refcount_local = 1;
        }
        else {
            map->refcount_local++;
        }

        // store pointer to device map in a new signal map
        sig->reserve_instances = si->next;
        msig_init_instance(si);
        i = msig_add_id_map(sig, si, map);
        if (sig->instance_event_handler &&
            (sig->instance_event_flags & IN_NEW)) {
            sig->instance_event_handler(sig, &sig->props, id, IN_NEW, tt);
        }
        return i;
    }

    if (sig->instance_event_handler &&
        (sig->instance_event_flags & IN_OVERFLOW)) {
        // call instance event handler
        sig->instance_event_handler(sig, &sig->props, -1, IN_OVERFLOW, tt);
    }
    else if (sig->handler) {
        if (sig->instance_allocation_type == IN_STEAL_OLDEST) {
            i = msig_get_oldest_active_instance_internal(sig);
            if (i < 0)
                return -1;
            sig->handler(sig, &sig->props, sig->id_maps[i].map->local,
                         0, 0, tt);
        }
        else if (sig->instance_allocation_type == IN_STEAL_NEWEST) {
            i = msig_get_newest_active_instance_internal(sig);
            if (i < 0)
                return -1;
            sig->handler(sig, &sig->props, sig->id_maps[i].map->local,
                         0, 0, tt);
        }
        else
            return -1;
    }
    else
        return -1;

    // try again
    if ((si = sig->reserve_instances)) {
        if (!map) {
            // Claim id map locally add id map to device and link from signal
            map = mdev_add_instance_id_map(sig->device, id, mdev_id(sig->device),
                                           sig->device->id_counter++);
            map->refcount_local = 1;
        }
        else {
            map->refcount_local++;
        }
        sig->reserve_instances = si->next;
        msig_init_instance(si);
        i = msig_add_id_map(sig, si, map);
        if (sig->instance_event_handler &&
            (sig->instance_event_flags & IN_NEW)) {
            sig->instance_event_handler(sig, &sig->props, id, IN_NEW, tt);
        }
        return i;
    }
    return -1;
}

int msig_get_instance_with_remote_ids(mapper_signal sig, int group, int id,
                                      int flags, mapper_timetag_t *tt)
{
    // If the map pointer is null, we need to create a new map if necessary
    if (!sig)
        return 0;
    
    mapper_signal_id_map_t *maps = sig->id_maps;
    
    mapper_signal_instance si;
    int i;
    for (i = 0; i < sig->id_map_length; i++) {
        if (maps[i].instance && maps[i].map->group == group
            && maps[i].map->remote == id) {
            if (maps[i].status & ~flags)
                return -1;
            else {
                return i;
            }
        }
    }

    mapper_id_map map = mdev_find_instance_id_map_by_remote(sig->device, group, id);

    // no ID-mapped instance exists - need to try to activate instance and create new ID map
    if ((si = sig->reserve_instances)) {
        if (!map) {
            // add id map to device
            map = mdev_add_instance_id_map(sig->device, si->index, group, id);
            map->refcount_remote = 1;
        }
        else {
            map->refcount_remote++;
        }
        sig->reserve_instances = si->next;
        msig_init_instance(si);
        i = msig_add_id_map(sig, si, map);
        if (sig->instance_event_handler &&
            (sig->instance_event_flags & IN_NEW)) {
            sig->instance_event_handler(sig, &sig->props, si->index, IN_NEW, tt);
        }
        return i;
    }

    if (sig->instance_event_handler &&
        (sig->instance_event_flags & IN_OVERFLOW)) {
        // call instance event handler
        sig->instance_event_handler(sig, &sig->props, -1, IN_OVERFLOW, tt);
    }
    else if (sig->handler) {
        if (sig->instance_allocation_type == IN_STEAL_OLDEST) {
            i = msig_get_oldest_active_instance_internal(sig);
            if (i < 0)
                return -1;
            sig->handler(sig, &sig->props, sig->id_maps[i].map->local,
                         0, 0, tt);
        }
        else if (sig->instance_allocation_type == IN_STEAL_NEWEST) {
            i = msig_get_newest_active_instance_internal(sig);
            if (i < 0)
                return -1;
            sig->handler(sig, &sig->props, sig->id_maps[i].map->local,
                         0, 0, tt);
        }
        else
            return -1;
    }
    else
        return -1;

    // try again
    if ((si = sig->reserve_instances)) {
        if (!map) {
            // add id map to device
            map = mdev_add_instance_id_map(sig->device, si->index, group, id);
            map->refcount_remote = 1;
        }
        else {
            map->refcount_remote++;
        }
        sig->reserve_instances = si->next;
        msig_init_instance(si);
        i = msig_add_id_map(sig, si, map);
        if (sig->instance_event_handler &&
            (sig->instance_event_flags & IN_NEW)) {
            sig->instance_event_handler(sig, &sig->props, si->index, IN_NEW, tt);
        }
        return i;
    }
    return -1;
}

void msig_reserve_instances(mapper_signal sig, int num)
{
    if (!sig || num <= 0)
        return;
    int available = MAX_INSTANCES - sig->props.num_instances;
    if (num > available)
        num = available;
    int i, j, index = -1;
    for (i = 0; i < num; i++) {
        mapper_signal_instance si = (mapper_signal_instance) calloc(1,
                                    sizeof(struct _mapper_signal_instance));
        si->value = calloc(1, msig_vector_bytes(sig));
        si->has_value = 0;
        if (index >= sig->props.num_instances) {
            si->index = index++;
        }
        else {
            // find lowest unused index in active and reserve lists
            mapper_signal_instance temp;
            int index_found = 0;
            while (!index_found) {
                index++;
                index_found = 1;
                for (j = 0; j < sig->id_map_length; j++) {
                    if (!sig->id_maps[j].instance)
                        continue;
                    if (sig->id_maps[j].instance->index == index) {
                        index_found = 0;
                        break;
                    }
                }
                if (index_found) {
                    temp = sig->reserve_instances;
                    while (temp) {
                        if (temp->index == index) {
                            index_found = 0;
                            break;
                        }
                        temp = temp->next;
                    }
                }
            }
            si->index = index++;
        }
        msig_init_instance(si);
        si->user_data = 0;

        si->next = sig->reserve_instances;
        sig->reserve_instances = si;
    }
    sig->props.num_instances += num;
    mdev_num_instances_changed(sig->device, sig);
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

    int index = msig_get_instance_with_local_id(sig, instance_id, 0, &timetag);
    if (index >= 0)
        msig_update_internal(sig, index, value, count, timetag);
}


int msig_get_oldest_active_instance(mapper_signal sig, int *id)
{
    int index = msig_get_oldest_active_instance_internal(sig);
    if (index < 0)
        return 1;
    else
        *id = sig->id_maps[index].map->local;
    return 0;
}

int msig_get_oldest_active_instance_internal(mapper_signal sig)
{
    int i;
    mapper_signal_instance si;
    for (i = 0; i < sig->id_map_length; i++) {
        if (sig->id_maps[i].instance)
            break;
    }
    if (i == sig->id_map_length) {
        // no active instances to steal!
        return -1;
    }
    int oldest = i;
    for (i = oldest+1; i < sig->id_map_length; i++) {
        if (!(si = sig->id_maps[i].instance))
            continue;
        if ((si->creation_time.sec < sig->id_maps[oldest].instance->creation_time.sec) ||
            (si->creation_time.sec == sig->id_maps[oldest].instance->creation_time.sec &&
             si->creation_time.frac < sig->id_maps[oldest].instance->creation_time.frac))
            oldest = i;
    }
    return oldest;
}

int msig_get_newest_active_instance(mapper_signal sig, int *id)
{
    int index = msig_get_newest_active_instance_internal(sig);
    if (index < 0)
        return 1;
    else
        *id = sig->id_maps[index].map->local;
    return 0;
}

int msig_get_newest_active_instance_internal(mapper_signal sig)
{
    int i;
    mapper_signal_instance si;
    for (i = 0; i < sig->id_map_length; i++) {
        if (sig->id_maps[i].instance)
            break;
    }
    if (i == sig->id_map_length) {
        // no active instances to steal!
        return -1;
    }
    int newest = i;
    for (i = newest+1; i < sig->id_map_length; i++) {
        if (!(si = sig->id_maps[i].instance))
            continue;
        if ((si->creation_time.sec > sig->id_maps[newest].instance->creation_time.sec) ||
            (si->creation_time.sec == sig->id_maps[newest].instance->creation_time.sec &&
             si->creation_time.frac > sig->id_maps[newest].instance->creation_time.frac))
            newest = i;
    }
    return newest;
}

static void msig_update_internal(mapper_signal sig,
                                 int instance_index,
                                 void *value,
                                 int count,
                                 mapper_timetag_t tt)
{
    mapper_signal_instance si = sig->id_maps[instance_index].instance;

    /* We have to assume that value points to an array of correct type
     * and size. */

    if (value) {
        if (count<=0) count=1;
        size_t n = msig_vector_bytes(sig);
        memcpy(si->value, value + n*(count-1), n);
        si->has_value = 1;
    }
    else {
        si->has_value = 0;
    }

    if (memcmp(&tt, &MAPPER_NOW, sizeof(mapper_timetag_t))==0)
        mdev_timetag_now(sig->device, &si->timetag);
    else
        memcpy(&si->timetag, &tt, sizeof(mapper_timetag_t));

    if (sig->props.is_output) {
        mdev_route_signal(sig->device, sig, instance_index, value,
                          count, si->timetag);
    }
    else {
        mdev_receive_update(sig->device, sig, instance_index, si->timetag);
    }
}

void msig_release_instance(mapper_signal sig, int id,
                           mapper_timetag_t timetag)
{
    if (!sig)
        return;
    int index = msig_find_instance_with_local_id(sig, id, IN_RELEASED_REMOTELY);
    if (index >= 0)
        msig_release_instance_internal(sig, index, timetag);
}

void msig_release_instance_internal(mapper_signal sig,
                                    int instance_index,
                                    mapper_timetag_t tt)
{
    mapper_signal_id_map_t *smap = &sig->id_maps[instance_index];
    if (!smap->instance)
        return;

    mapper_timetag_t *ttp;
    if (memcmp(&tt, &MAPPER_NOW, sizeof(mapper_timetag_t))==0) {
        ttp = &sig->device->admin->clock.now;
        mdev_timetag_now(sig->device, ttp);
    }
    else
        ttp = &tt;

    if (!(smap->status & IN_RELEASED_REMOTELY)) {
        mdev_route_released(sig->device, sig, instance_index, *ttp);
    }

    smap->map->refcount_local--;
    if (smap->map->refcount_local <= 0 && smap->map->refcount_remote <= 0) {
        mdev_remove_instance_id_map(sig->device, smap->map);
        smap->map = 0;
    }
    else if (sig->props.is_output || smap->status & IN_RELEASED_REMOTELY) {
        // TODO: consider multiple upstream source instances?
        smap->map = 0;
    }
    else {
        // mark map as locally-released but do not remove it
        sig->id_maps[instance_index].status |= IN_RELEASED_LOCALLY;
    }

    // Put instance back in reserve list
    smap->instance->next = sig->reserve_instances;
    sig->reserve_instances = smap->instance;
    smap->instance = 0;
}

void msig_remove_instance(mapper_signal sig,
                          mapper_signal_instance si)
{
    if (!si) return;

    // Remove signal instance
    int i;
    for (i = 0; i < sig->id_map_length; i++) {
        if (sig->id_maps[i].instance == si) {
            // First release instance
            mapper_timetag_t tt = sig->device->admin->clock.now;
            mdev_timetag_now(sig->device, &tt);
            msig_release_instance_internal(sig, i, tt);
            msig_free_instance(sig, si);
            sig->id_maps[i].instance = 0;
            --sig->props.num_instances;
            return;
        }
    }
    mapper_signal_instance *msi = &sig->reserve_instances;
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
    if (si->value)
        free(si->value);
    free(si);
}

static void *msig_instance_value_internal(mapper_signal sig,
                                          int instance_index,
                                          mapper_timetag_t *timetag)
{
    mapper_signal_instance si = sig->id_maps[instance_index].instance;
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
    int index = msig_find_instance_with_local_id(sig, instance_id, 0);
    if (index < 0)
        return 0;
    return msig_instance_value_internal(sig, index, timetag);
}

void msig_match_instances(mapper_signal from, mapper_signal to, int instance_id)
{
    if (from == to)
        return;

    // Find "from" instance
    int index = msig_find_instance_with_local_id(from, instance_id, 0);
    if (index < 0)
        return;

    mapper_timetag_t tt = from->device->admin->clock.now;
    mdev_timetag_now(from->device, &tt);

    // Get "to" instance with same map
    msig_get_instance_with_remote_ids(to, from->id_maps[index].map->group,
                                      from->id_maps[index].map->remote, 0,
                                      &tt);
}

int msig_num_active_instances(mapper_signal sig)
{
    if (!sig)
        return -1;
    int i, j = 0;
    for (i = 0; i < sig->id_map_length; i++) {
        if (sig->id_maps[i].instance)
            j++;
    }
    return j;
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
    int i, j = -1;
    for (i = 0; i < sig->id_map_length; i++) {
        if (sig->id_maps[i].instance)
            j++;
        if (j >= index)
            return sig->id_maps[i].map->local;
    }
    return -1;
}

void msig_set_instance_allocation_mode(mapper_signal sig,
                                       mapper_instance_allocation_type mode)
{
    if (sig)
        sig->instance_allocation_type = mode;
}

mapper_instance_allocation_type msig_get_instance_allocation_mode(mapper_signal sig)
{
    if (sig)
        return sig->instance_allocation_type;
    return 0;
}

void msig_set_instance_event_callback(mapper_signal sig,
                                      mapper_signal_instance_event_handler h,
                                      int flags,
                                      void *user_data)
{
    if (!sig)
        return;

    if (!h || !flags) {
        sig->instance_event_handler = 0;
        sig->instance_event_flags = 0;
        return;
    }

    sig->instance_event_handler = h;
    sig->props.user_data = user_data;

    if (flags & IN_DOWNSTREAM_RELEASE) {
        if (!(sig->instance_event_flags & IN_DOWNSTREAM_RELEASE)) {
            // Add liblo method for processing instance release requests
            sig->instance_event_flags = flags;
            mdev_add_instance_release_request_callback(sig->device, sig);
        }
    }
    else {
        if (sig->instance_event_flags & IN_DOWNSTREAM_RELEASE) {
            // Remove liblo method for processing instance release requests
            sig->instance_event_flags = flags;
            mdev_remove_instance_release_request_callback(sig->device, sig);
        }
    }
    sig->instance_event_flags = flags;
}

void msig_set_instance_data(mapper_signal sig,
                            int instance_id,
                            void *user_data)
{
    mapper_timetag_t tt = sig->device->admin->clock.now;
    mdev_timetag_now(sig->device, &tt);

    int index = msig_get_instance_with_local_id(sig, instance_id, 0, &tt);
    if (index >= 0)
        sig->id_maps[index].instance->user_data = user_data;
}

void *msig_get_instance_data(mapper_signal sig,
                             int instance_id)
{
    int index = msig_find_instance_with_local_id(sig, instance_id, 0);
    if (index >= 0)
        return sig->id_maps[index].instance->user_data;
    return 0;
}

/**** Queries and reverse connections ****/

void msig_set_callback(mapper_signal sig,
                       mapper_signal_update_handler *handler,
                       void *user_data)
{
    if (!sig)
        return;
    if (!sig->handler && handler) {
        // Need to register a new liblo methods
        sig->handler = handler;
        sig->props.user_data = user_data;
        mdev_add_signal_methods(sig->device, sig);
    }
    else if (sig->handler && !handler) {
        // Need to remove liblo methods
        sig->handler = 0;
        sig->props.user_data = user_data;
        mdev_remove_signal_methods(sig->device, sig);
    }
}

int msig_query_remotes(mapper_signal sig, mapper_timetag_t tt)
{
    // TODO: process queries on inputs also
    if (!sig->props.is_output)
        return -1;
    if (!sig->handler) {
        // no handler defined so we cannot process query responses
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

int msig_add_id_map(mapper_signal sig, mapper_signal_instance si,
                    mapper_id_map map)
{
    // find unused signal map
    int i;

    for (i = 0; i < sig->id_map_length; i++) {
        if (!sig->id_maps[i].map)
            break;
    }
    if (i == sig->id_map_length) {
        // need more memory
        if (sig->id_map_length >= MAX_INSTANCES) {
            // Arbitrary limit to number of tracked id_maps
            return -1;
        }
        sig->id_map_length *= 2;
        sig->id_maps = realloc(sig->id_maps, sig->id_map_length *
                               sizeof(struct _mapper_signal_id_map));
        memset(sig->id_maps + i,
               0, (sig->id_map_length - i) * sizeof(struct _mapper_signal_id_map));
    }
    sig->id_maps[i].map = map;
    sig->id_maps[i].instance = si;
    sig->id_maps[i].status = 0;

    return i;
}

int msig_num_connections(mapper_signal sig)
{
    mapper_link l = sig->props.is_output ?
                    sig->device->routers : sig->device->receivers;
    int count = 0;
    while (l) {
        mapper_link_signal ls = l->signals;
        while (ls) {
            if (ls->signal == sig) {
                mapper_connection c = ls->connections;
                while (c) {
                    count++;
                    c = c->next;
                }
            }
            ls = ls->next;
        }
        l = l->next;
    }
    return count;
}
