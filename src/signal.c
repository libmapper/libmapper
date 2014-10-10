
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

static int compare_ids(const void *l, const void *r)
{
    if ((*(mapper_signal_instance *)l)->id <  (*(mapper_signal_instance *)r)->id) return -1;
    if ((*(mapper_signal_instance *)l)->id == (*(mapper_signal_instance *)r)->id) return 0;
    else return 1;
}

static mapper_signal_instance find_instance_by_id(mapper_signal sig, int instance_id)
{
    if (!sig->props.num_instances)
        return 0;

    mapper_signal_instance_t si;
    mapper_signal_instance sip = &si;
    si.id = instance_id;

    mapper_signal_instance *sipp = bsearch(&sip, sig->instances, sig->props.num_instances,
                                           sizeof(mapper_signal_instance), compare_ids);
    if (sipp && *sipp)
        return *sipp;
    return 0;
}

mapper_signal msig_new(const char *name, int length, char type,
                       int is_output, const char *unit,
                       void *minimum, void *maximum, int num_instances,
                       mapper_signal_update_handler *handler,
                       void *user_data)
{
    int i;
    if (length < 1) return 0;
    if (!name) return 0;
    if (type != 'f' && type != 'i' && type != 'd')
        return 0;

    mapper_signal sig =
        (mapper_signal) calloc(1, sizeof(struct _mapper_signal));

    mapper_db_signal_init(&sig->props, is_output, type, length, name, unit);
    sig->handler = handler;
    sig->has_complete_value = calloc(1, length / 8 + 1);
    for (i = 0; i < length; i++) {
        sig->has_complete_value[i/8] |= 1 << (i % 8);
    }
    sig->props.user_data = user_data;
    msig_set_minimum(sig, minimum);
    msig_set_maximum(sig, maximum);

    if (num_instances < 0) {
        // this is a single-instance signal
        sig->props.num_instances = 0;

        // Reserve one instance to start
        msig_reserve_instances(sig, 1, 0, 0);
    }
    else {
        sig->props.num_instances = msig_reserve_instances(sig, num_instances, 0, 0);
    }

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
    for (i = 0; i < sig->props.num_instances; i++) {
        if (sig->instances[i]->value)
            free(sig->instances[i]->value);
        if (sig->instances[i]->has_value_flags)
            free(sig->instances[i]->has_value_flags);
        free(sig->instances[i]);
    }
    free(sig->instances);
    if (sig->props.minimum)
        free(sig->props.minimum);
    if (sig->props.maximum)
        free(sig->props.maximum);
    if (sig->props.name)
        free((char*)sig->props.name);
    if (sig->props.unit)
        free((char*)sig->props.unit);
    if (sig->has_complete_value)
        free(sig->has_complete_value);
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
        mdev_now(sig->device, ttp);
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
    mdev_now(sig->device, tt);

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
    mdev_now(sig->device, tt);

    int index = 0;
    if (!sig->id_maps[0].instance)
        index = msig_get_instance_with_local_id(sig, 0, 0, tt);
    msig_update_internal(sig, index, &value, 1, *tt);
}

void msig_update_double(mapper_signal sig, double value)
{
    if (!sig)
        return;

#ifdef DEBUG
    if (sig->props.type != 'd') {
        trace("called msig_update_double() on non-double signal!\n");
        return;
    }

    if (sig->props.length != 1) {
        trace("called msig_update_double() on non-scalar signal!\n");
        return;
    }

    if (!sig->device) {
        trace("signal does not have a device in msig_update_double().\n");
        return;
    }
#endif

    mapper_timetag_t *tt = &sig->device->admin->clock.now;
    mdev_now(sig->device, tt);

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

int msig_find_instance_with_remote_ids(mapper_signal sig, int origin,
                                       int public_id, int flags)
{
    int i;
    for (i = 0; i < sig->id_map_length; i++) {
        if (!sig->id_maps[i].map)
            continue;
        if (sig->id_maps[i].map->origin == origin && sig->id_maps[i].map->public == public_id) {
            if (sig->id_maps[i].status & ~flags)
                return -1;
            else
                return i;
        }
    }
    return -1;
}

static mapper_signal_instance get_reserved_instance(mapper_signal sig)
{
    int i;
    for (i = 0; i < sig->props.num_instances; i++) {
        if (!sig->instances[i]->is_active) {
            return sig->instances[i];
        }
    }
    return 0;
}

int msig_get_instance_with_local_id(mapper_signal sig, int instance_id,
                                    int flags, mapper_timetag_t *tt)
{
    if (!sig)
        return 0;

    mapper_signal_id_map_t *maps = sig->id_maps;

    mapper_signal_instance si;
    int i;
    for (i = 0; i < sig->id_map_length; i++) {
        if (maps[i].instance && maps[i].map->local == instance_id) {
            if (maps[i].status & ~flags)
                return -1;
            else {
                return i;
            }
        }
    }

    // check if device has record of id map
    mapper_id_map map = mdev_find_instance_id_map_by_local(sig->device, instance_id);

    // no instance with that id exists - need to try to activate instance and create new id map
    if ((si = find_instance_by_id(sig, instance_id))) {
        if (!map) {
            // Claim id map locally, add id map to device and link from signal
            map = mdev_add_instance_id_map(sig->device, instance_id, mdev_id(sig->device),
                                           sig->device->id_counter++);
            map->refcount_local = 1;
        }
        else {
            map->refcount_local++;
        }

        // store pointer to device map in a new signal map
        msig_init_instance(si);
        i = msig_add_id_map(sig, si, map);
        if (sig->instance_event_handler &&
            (sig->instance_event_flags & IN_NEW)) {
            sig->instance_event_handler(sig, &sig->props, instance_id, IN_NEW, tt);
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
    if ((si = find_instance_by_id(sig, instance_id))) {
        if (!map) {
            // Claim id map locally add id map to device and link from signal
            map = mdev_add_instance_id_map(sig->device, instance_id, mdev_id(sig->device),
                                           sig->device->id_counter++);
            map->refcount_local = 1;
        }
        else {
            map->refcount_local++;
        }
        msig_init_instance(si);
        i = msig_add_id_map(sig, si, map);
        if (sig->instance_event_handler &&
            (sig->instance_event_flags & IN_NEW)) {
            sig->instance_event_handler(sig, &sig->props, instance_id, IN_NEW, tt);
        }
        return i;
    }
    return -1;
}

int msig_get_instance_with_remote_ids(mapper_signal sig, int origin, int public_id,
                                      int flags, mapper_timetag_t *tt)
{
    if (!sig)
        return 0;

    mapper_signal_id_map_t *maps = sig->id_maps;

    mapper_signal_instance si;
    int i;
    for (i = 0; i < sig->id_map_length; i++) {
        if (maps[i].instance && maps[i].map->origin == origin
            && maps[i].map->public == public_id) {
            if (maps[i].status & ~flags)
                return -1;
            else {
                return i;
            }
        }
    }

    // check if the device already has a map for this remote
    mapper_id_map map = mdev_find_instance_id_map_by_remote(sig->device, origin, public_id);
    if (!map) {
        /* Here we still risk creating conflicting maps if two signals are updated asynchronously.
         * This is easy to avoid by not allowing a local id to be used with multiple active remote
         * maps, however users may wish to create devices with multiple object classes which do not
         * require mutual instance id synchronization - e.g. instance 1 of object class A is not
         * related to instance 1 of object B. */
        // TODO: add object groups for explictly sharing id maps

        if ((si = get_reserved_instance(sig))) {
            map = mdev_add_instance_id_map(sig->device, si->id, origin, public_id);
            map->refcount_remote = 1;

            si->is_active = 1;
            msig_init_instance(si);
            i = msig_add_id_map(sig, si, map);
            if (sig->instance_event_handler &&
                (sig->instance_event_flags & IN_NEW)) {
                sig->instance_event_handler(sig, &sig->props, si->id, IN_NEW, tt);
            }
            return i;
        }
    }
    else {
        si = find_instance_by_id(sig, map->local);
        if (!si) {
            // TODO: Once signal groups are explicit, allow re-mapping to another instance if possible.
            trace("Signal %s has no instance %i available.\n", sig->props.name, map->local);
            return -1;
        }
        else if (!si->is_active) {
            si->is_active = 1;
            msig_init_instance(si);
            i = msig_add_id_map(sig, si, map);
            map->refcount_remote++;

            if (sig->instance_event_handler &&
                (sig->instance_event_flags & IN_NEW)) {
                sig->instance_event_handler(sig, &sig->props, si->id, IN_NEW, tt);
            }
            return i;
        }
    }

    // try releasing instance in use
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
    if (!map) {
        if ((si = get_reserved_instance(sig))) {
            map = mdev_add_instance_id_map(sig->device, si->id, origin, public_id);
            map->refcount_remote = 1;

            si->is_active = 1;
            msig_init_instance(si);
            i = msig_add_id_map(sig, si, map);
            if (sig->instance_event_handler &&
                (sig->instance_event_flags & IN_NEW)) {
                sig->instance_event_handler(sig, &sig->props, si->id, IN_NEW, tt);
            }
            return i;
        }
    }
    else {
        si = find_instance_by_id(sig, map->local);
        if (!si) {
            trace("Signal %s has no instance %i available.", sig->props.name, map->local);
            return -1;
        }
        if (si) {
            if (si->is_active) {
                return -1;
            }
            si->is_active = 1;
            msig_init_instance(si);
            i = msig_add_id_map(sig, si, map);
            map->refcount_remote++;

            if (sig->instance_event_handler &&
                (sig->instance_event_flags & IN_NEW)) {
                sig->instance_event_handler(sig, &sig->props, si->id, IN_NEW, tt);
            }
            return i;
        }
    }
    return -1;
}

static int msig_reserve_instance_internal(mapper_signal sig, int *id,
                                          void *user_data)
{
    if (!sig || sig->props.num_instances >= MAX_INSTANCES)
        return -1;

    int i, lowest, found;
    mapper_signal_instance si;

    // check if instance with this id already exists! If so, stop here.
    if (id && find_instance_by_id(sig, *id))
        return -1;

    // reallocate array of instances
    sig->instances = realloc(sig->instances,
                             sizeof(mapper_signal_instance)
                             * (sig->props.num_instances+1));
    sig->instances[sig->props.num_instances] =
        (mapper_signal_instance) calloc(1, sizeof(struct _mapper_signal_instance));
    si = sig->instances[sig->props.num_instances];
    si->value = calloc(1, msig_vector_bytes(sig));
    si->has_value_flags = calloc(1, sig->props.length / 8 + 1);
    si->has_value = 0;

    if (id)
        si->id = *id;
    else {
        // find lowest unused positive id
        lowest = -1, found = 0;
        while (!found) {
            lowest++;
            found = 1;
            for (i = 0; i < sig->props.num_instances; i++) {
                if (sig->instances[i]->id == lowest) {
                    found = 0;
                    break;
                }
            }
        }
        si->id = lowest;
    }
    // find lowest unused positive index
    lowest = -1, found = 0;
    while (!found) {
        lowest++;
        found = 1;
        for (i = 0; i < sig->props.num_instances; i++) {
            if (sig->instances[i]->index == lowest) {
                found = 0;
                break;
            }
        }
    }
    si->index = lowest;

    msig_init_instance(si);
    si->user_data = user_data;

    sig->props.num_instances ++;
    qsort(sig->instances, sig->props.num_instances,
          sizeof(mapper_signal_instance), compare_ids);

    // return largest index
    int highest = -1;
    for (i = 0; i < sig->props.num_instances; i++) {
        if (sig->instances[i]->index > highest)
            highest = sig->instances[i]->index;
    }

    return highest;
}

int msig_reserve_instances(mapper_signal sig, int num, int *ids, void **user_data)
{
    int i, count = 0, highest = -1;
    for (i = 0; i < num; i++) {
        int result = msig_reserve_instance_internal(sig, ids ? &ids[i] : 0,
                                                    user_data ? user_data[i] : 0);
        if (result == -1)
            continue;
        highest = result;
        count++;
    }
    if (highest != -1)
        mdev_num_instances_changed(sig->device, sig, highest + 1);
    return count;
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
        mdev_now(sig->device, &si->timetag);
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
        mdev_now(sig->device, ttp);
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
    smap->instance->is_active = 0;
    smap->instance = 0;
}

void msig_remove_instance(mapper_signal sig, int instance_id)
{
    if (!sig) return;

    int i;
    for (i = 0; i < sig->props.num_instances; i++) {
        if (sig->instances[i]->id == instance_id) {
            if (sig->instances[i]->is_active) {
                // First release instance
                mapper_timetag_t tt = sig->device->admin->clock.now;
                mdev_now(sig->device, &tt);
                msig_release_instance_internal(sig, i, tt);
            }
            break;
        }
    }

    if (i == sig->props.num_instances)
        return;

    if (sig->instances[i]->value)
        free(sig->instances[i]->value);
    if (sig->instances[i]->has_value_flags)
        free(sig->instances[i]->has_value_flags);
    free(sig->instances[i]);
    i++;
    for (; i < sig->props.num_instances; i++) {
        sig->instances[i-1] = sig->instances[i];
    }
    --sig->props.num_instances;
    sig->instances = realloc(sig->instances,
                             sizeof(mapper_signal_instance) * sig->props.num_instances);

    // TODO: could also realloc signal value histories
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
    int index = msig_find_instance_with_local_id(sig, instance_id,
                                                 IN_RELEASED_REMOTELY);
    if (index < 0)
        return 0;
    return msig_instance_value_internal(sig, index, timetag);
}

void msig_match_instances(mapper_signal from, mapper_signal to, int instance_id)
{
    // TODO: remove this function
    if (from == to)
        return;

    // Find "from" instance
    int index = msig_find_instance_with_local_id(from, instance_id,
                                                 IN_RELEASED_REMOTELY);
    if (index < 0)
        return;

    mapper_timetag_t tt = from->device->admin->clock.now;
    mdev_now(from->device, &tt);

    // Get "to" instance with same map
    msig_get_instance_with_remote_ids(to, from->id_maps[index].map->origin,
                                      from->id_maps[index].map->public, 0,
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
    int i, j = 0;
    for (i = 0; i < sig->props.num_instances; i++) {
        if (!sig->instances[i]->is_active)
            j++;
    }
    return j;
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
    // TODO: use separate user_data for instance event callback?
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
    mapper_signal_instance si = find_instance_by_id(sig, instance_id);
    if (si)
        si->user_data = user_data;
}

void *msig_get_instance_data(mapper_signal sig,
                             int instance_id)
{
    mapper_signal_instance si = find_instance_by_id(sig, instance_id);
    if (si)
        return si->user_data;

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
            sig->props.minimum = malloc(msig_vector_bytes(sig));
        memcpy(sig->props.minimum, minimum, msig_vector_bytes(sig));
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
            sig->props.maximum = malloc(msig_vector_bytes(sig));
        memcpy(sig->props.maximum, maximum, msig_vector_bytes(sig));
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
                       char type, void *value, int length)
{
    if (strcmp(property, "device_name") == 0 ||
        strcmp(property, "name") == 0 ||
        strcmp(property, "type") == 0 ||
        strcmp(property, "length") == 0 ||
        strcmp(property, "direction") == 0 ||
        strcmp(property, "user_data") == 0) {
        trace("Cannot set locked signal property '%s'\n", property);
        return;
    }

    if (strcmp(property, "min") == 0 ||
        strcmp(property, "minimum") == 0) {
        if (!length || !value)
            msig_set_minimum(sig, 0);
        else if (length == sig->props.length && type == sig->props.type)
            msig_set_minimum(sig, value);
        // TODO: if types differ need to cast entire vector
        return;
    }
    else if (strcmp(property, "max") == 0 ||
             strcmp(property, "maximum") == 0) {
        if (!length || !value)
            msig_set_maximum(sig, 0);
        else if (length == sig->props.length && type == sig->props.type)
            msig_set_maximum(sig, value);
        // TODO: if types differ need to cast entire vector
        return;
    }
    else if ((strcmp(property, "unit") == 0 ||
             strcmp(property, "units") == 0) &&
             (type == 's' || type == 'S')) {
        if (!length || !value) {
            if (sig->props.unit) {
                free(sig->props.unit);
                sig->props.unit = 0;
            }
            return;
        }
        if (!sig->props.unit || strcmp(sig->props.unit, (char*)value)) {
            sig->props.unit = realloc(sig->props.unit, strlen((char*)value)+1);
            strcpy(sig->props.unit, (char*)value);
        }
        return;
    }
    else if (strcmp(property, "value") == 0) {
        if (!length || !value)
            msig_update(sig, 0, 0, MAPPER_NOW);
        else if (length == sig->props.length || type == sig->props.type)
            msig_update(sig, value, 1, MAPPER_NOW);
        return;
    }

    mapper_table_add_or_update_typed_value(sig->props.extra, property,
                                           type, value, length);
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

void message_add_coerced_signal_instance_value(lo_message m,
                                               mapper_signal sig,
                                               mapper_signal_instance si,
                                               int length,
                                               const char type)
{
    int i;
    int min_length = length < sig->props.length ?
                     length : sig->props.length;

    if (sig->props.type == 'f') {
        float *v = (float *) si->value;
        for (i = 0; i < min_length; i++) {
            if (!si->has_value && !(si->has_value_flags[i/8] & 1 << (i % 8)))
                lo_message_add_nil(m);
            else if (type == 'f')
                lo_message_add_float(m, v[i]);
            else if (type == 'i')
                lo_message_add_int32(m, (int)v[i]);
            else if (type == 'd')
                lo_message_add_double(m, (double)v[i]);
        }
    }
    else if (sig->props.type == 'i') {
        int *v = (int *) si->value;
        for (i = 0; i < min_length; i++) {
            if (!si->has_value && !(si->has_value_flags[i/8] & 1 << (i % 8)))
                lo_message_add_nil(m);
            else if (type == 'i')
                lo_message_add_int32(m, v[i]);
            else if (type == 'f')
                lo_message_add_float(m, (float)v[i]);
            else if (type == 'd')
                lo_message_add_double(m, (double)v[i]);
        }
    }
    else if (sig->props.type == 'd') {
        double *v = (double *) si->value;
        for (i = 0; i < min_length; i++) {
            if (!si->has_value && !(si->has_value_flags[i/8] & 1 << (i % 8)))
                lo_message_add_nil(m);
            else if (type == 'd')
                lo_message_add_double(m, (int)v[i]);
            else if (type == 'i')
                lo_message_add_int32(m, (int)v[i]);
            else if (type == 'f')
                lo_message_add_float(m, (float)v[i]);
        }
    }
    for (i = min_length; i < length; i++)
        lo_message_add_nil(m);
}
