
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <stddef.h>

#include "mapper_internal.h"
#include "types_internal.h"
#include <mapper/mapper.h>

#define MAX_INSTANCES 128

/* Function prototypes */
static void mapper_signal_update_internal(mapper_signal sig, int instance_index,
                                          void *value, int count,
                                          mapper_timetag_t timetag);

static void *mapper_signal_instance_value_internal(mapper_signal sig,
                                                   int instance_index,
                                                   mapper_timetag_t *timetag);

static int mapper_signal_oldest_active_instance_internal(mapper_signal sig);

static int mapper_signal_newest_active_instance_internal(mapper_signal sig);

static int mapper_signal_find_instance_with_local_id(mapper_signal sig, int id,
                                                     int flags);

static int mapper_signal_add_id_map(mapper_signal sig, mapper_signal_instance si,
                                    mapper_id_map map);

/* Static data for property tables embedded in the signal data structure.
 *
 * Signals have properties that can be indexed by name, and can have extra
 * properties attached to them either by using setter functions on a local
 * signal, or if they are specified in the description of remote signals.
 * The utility of this is to allow for attaching of arbitrary metadata to
 * objects on the network.  For example, a signal could have a 'position'
 * value to indicate its physical position in the room.
 *
 * It is also useful to be able to look up standard properties like vector
 * length, name, or unit by specifying these as a string.
 *
 * The following data provides a string table (as usable by the implementation
 * in table.c) for indexing the static data existing in the signal data
 * structure.  Some of these static properties may not actually exist, such as
 * 'minimum' and 'maximum' which are optional properties of signals.  Therefore
 * an 'indirect' form is available where the table points to a pointer to the
 * value, which may be null.
 *
 * A property lookup consists of looking through the 'extra' properties of a
 * structure.  If the requested property is not found, then the 'static'
 * properties are searched---in the worst case, an unsuccessful lookup may
 * therefore take twice as long.
 *
 * To iterate through all available properties, the caller must request by
 * index, starting at 0, and incrementing until failure.  They are not
 * guaranteed to be in a particular order.
 */

#define SIG_OFFSET(x)   offsetof(mapper_signal_t, x)
#define SIG_TYPE        (SIG_OFFSET(type))
#define SIG_LENGTH      (SIG_OFFSET(length))

/* Here type 'o', which is not an OSC type, was reserved to mean "same type as
 * the signal's type".  The lookup and index functions will return the sig->type
 * instead of the value's type. */
static property_table_value_t sig_values[] = {
    { 's', {1},        -1,         SIG_OFFSET(description) },
    { 'i', {0},        -1,         SIG_OFFSET(direction) },
    { 'h', {0},        -1,         SIG_OFFSET(id) },
    { 'i', {0},        -1,         SIG_OFFSET(length) },
    { 'o', {SIG_TYPE}, SIG_LENGTH, SIG_OFFSET(maximum) },
    { 'o', {SIG_TYPE}, SIG_LENGTH, SIG_OFFSET(minimum) },
    { 's', {1},        -1,         SIG_OFFSET(name) },
    { 'f', {0},        -1,         SIG_OFFSET(rate) },
    { 'c', {0},        -1,         SIG_OFFSET(type) },
    { 's', {1},        -1,         SIG_OFFSET(unit) },
    { 'i', {0},         0,         SIG_OFFSET(user_data) },
};

/* This table must remain in alphabetical order. */
static string_table_node_t sig_strings[] = {
    { "description", &sig_values[0] },
    { "direction",   &sig_values[1] },
    { "id",          &sig_values[2] },
    { "length",      &sig_values[3] },
    { "max",         &sig_values[4] },
    { "min",         &sig_values[5] },
    { "name",        &sig_values[6] },
    { "rate",        &sig_values[7] },
    { "type",        &sig_values[8] },
    { "unit",        &sig_values[9] },
    { "user_data",   &sig_values[10] },
};

const int NUM_SIG_STRINGS = sizeof(sig_strings)/sizeof(sig_strings[0]);
static mapper_string_table_t sig_table = {
    sig_strings, NUM_SIG_STRINGS, NUM_SIG_STRINGS
};

static int compare_ids(const void *l, const void *r)
{
    if ((*(mapper_signal_instance *)l)->id < (*(mapper_signal_instance *)r)->id)
        return -1;
    if ((*(mapper_signal_instance *)l)->id == (*(mapper_signal_instance *)r)->id)
        return 0;
    return 1;
}

static mapper_signal_instance find_instance_by_id(mapper_signal sig,
                                                  int instance_id)
{
    if (!sig->num_instances)
        return 0;

    mapper_signal_instance_t si;
    mapper_signal_instance sip = &si;
    si.id = instance_id;

    mapper_signal_instance *sipp = bsearch(&sip, sig->local->instances,
                                           sig->num_instances,
                                           sizeof(mapper_signal_instance),
                                           compare_ids);
    if (sipp && *sipp)
        return *sipp;
    return 0;
}

void mapper_signal_init(mapper_signal sig, const char *name, int length,
                        char type, mapper_direction_t direction,
                        const char *unit, void *minimum, void *maximum,
                        mapper_signal_update_handler *handler, void *user_data)
{
    int i;
    if (check_signal_length(length) || check_signal_type(type))
        return;
    if (!name)
        return;

    name = skip_slash(name);
    int len = strlen(name)+2;
    sig->path = malloc(len);
    snprintf(sig->path, len, "/%s", name);
    sig->name = (char*)sig->path+1;

    sig->length = length;
    sig->type = type;
    sig->direction = direction;
    sig->unit = unit ? strdup(unit) : 0;
    sig->extra = table_new();
    sig->minimum = sig->maximum = 0;
    sig->description = 0;
    
    sig->local->handler = handler;
    sig->num_instances = 0;

    sig->user_data = user_data;
    
    mapper_signal_set_minimum(sig, minimum);
    mapper_signal_set_maximum(sig, maximum);

    if (sig->local) {
        sig->local->has_complete_value = calloc(1, length / 8 + 1);
        for (i = 0; i < length; i++) {
            sig->local->has_complete_value[i/8] |= 1 << (i % 8);
        }

        // Reserve one instance to start
        mapper_signal_reserve_instances(sig, 1, 0, 0);

        // Reserve one instance id map
        sig->local->id_map_length = 1;
        sig->local->id_maps = calloc(1, sizeof(struct _mapper_signal_id_map));
    }
}

void mapper_signal_free(mapper_signal sig)
{
    int i;
    if (!sig) return;

    if (sig->minimum)
        free(sig->minimum);
    if (sig->maximum)
        free(sig->maximum);
    if (sig->path)
        free(sig->path);
    if (sig->description)
        free(sig->description);
    if (sig->unit)
        free(sig->unit);
    if (sig->extra)
        table_free(sig->extra);

    if (sig->local) {
        // Free instances
        for (i = 0; i < sig->local->id_map_length; i++) {
            if (sig->local->id_maps[i].instance) {
                mapper_signal_release_instance_internal(sig, i, MAPPER_NOW);
            }
        }
        free(sig->local->id_maps);
        for (i = 0; i < sig->num_instances; i++) {
            if (sig->local->instances[i]->value)
                free(sig->local->instances[i]->value);
            if (sig->local->instances[i]->has_value_flags)
                free(sig->local->instances[i]->has_value_flags);
            free(sig->local->instances[i]);
        }
        free(sig->local->instances);
        if (sig->local->has_complete_value)
            free(sig->local->has_complete_value);
        free(sig->local);
    }
    free(sig);
}

void mapper_signal_update(mapper_signal sig, void *value, int count,
                          mapper_timetag_t tt)
{
    if (!sig || !sig->local)
        return;

    mapper_timetag_t *ttp;
    if (memcmp(&tt, &MAPPER_NOW, sizeof(mapper_timetag_t))==0) {
        ttp = &sig->db->admin->clock.now;
        mapper_device_now(sig->device, ttp);
    }
    else
        ttp = &tt;

    int index = 0;
    if (!sig->local->id_maps[0].instance)
        index = mapper_signal_instance_with_local_id(sig, 0, 0, ttp);
    mapper_signal_update_internal(sig, index, value, count, *ttp);
}

void mapper_signal_update_int(mapper_signal sig, int value)
{
    if (!sig || !sig->local)
        return;

#ifdef DEBUG
    if (sig->type != 'i') {
        trace("called mapper_signal_update_int() on non-int signal!\n");
        return;
    }

    if (sig->length != 1) {
        trace("called mapper_signal_update_int() on non-scalar signal!\n");
        return;
    }

    if (!sig->device) {
        trace("signal does not have a device in mapper_signal_update_int().\n");
        return;
    }
#endif

    mapper_timetag_t *tt = &sig->db->admin->clock.now;
    mapper_device_now(sig->device, tt);

    int index = 0;
    if (!sig->local->id_maps[0].instance)
        index = mapper_signal_instance_with_local_id(sig, 0, 0, tt);
    mapper_signal_update_internal(sig, index, &value, 1, *tt);
}

void mapper_signal_update_float(mapper_signal sig, float value)
{
    if (!sig || !sig->local)
        return;

#ifdef DEBUG
    if (sig->type != 'f') {
        trace("called mapper_signal_update_float() on non-float signal!\n");
        return;
    }

    if (sig->length != 1) {
        trace("called mapper_signal_update_float() on non-scalar signal!\n");
        return;
    }

    if (!sig->device) {
        trace("signal does not have a device in mapper_signal_update_float().\n");
        return;
    }
#endif

    mapper_timetag_t *tt = &sig->db->admin->clock.now;
    mapper_device_now(sig->device, tt);

    int index = 0;
    if (!sig->local->id_maps[0].instance)
        index = mapper_signal_instance_with_local_id(sig, 0, 0, tt);
    mapper_signal_update_internal(sig, index, &value, 1, *tt);
}

void mapper_signal_update_double(mapper_signal sig, double value)
{
    if (!sig || !sig->local)
        return;

#ifdef DEBUG
    if (sig->type != 'd') {
        trace("called mapper_signal_update_double() on non-double signal!\n");
        return;
    }

    if (sig->length != 1) {
        trace("called mapper_signal_update_double() on non-scalar signal!\n");
        return;
    }

    if (!sig->device) {
        trace("signal does not have a device in mapper_signal_update_double().\n");
        return;
    }
#endif

    mapper_timetag_t *tt = &sig->db->admin->clock.now;
    mapper_device_now(sig->device, tt);

    int index = 0;
    if (!sig->local->id_maps[0].instance)
        index = mapper_signal_instance_with_local_id(sig, 0, 0, tt);
    mapper_signal_update_internal(sig, index, &value, 1, *tt);
}

void *mapper_signal_value(mapper_signal sig, mapper_timetag_t *timetag)
{
    if (!sig || !sig->local)
        return 0;
    int index = 0;
    if (!sig->local->id_maps[0].instance)
        index = mapper_signal_find_instance_with_local_id(sig, 0, 0);
    if (index < 0)
        return 0;
    return mapper_signal_instance_value_internal(sig, index, timetag);
}

/**** Instances ****/

static void mapper_signal_init_instance(mapper_signal_instance si)
{
    si->has_value = 0;
    lo_timetag_now(&si->created);
}

static int mapper_signal_find_instance_with_local_id(mapper_signal sig, int id,
                                                     int flags)
{
    int i;
    for (i = 0; i < sig->local->id_map_length; i++) {
        if (   sig->local->id_maps[i].instance
            && sig->local->id_maps[i].map->local == id) {
            if (sig->local->id_maps[i].status & ~flags)
                return -1;
            else
                return i;
        }
    }
    return -1;
}

int mapper_signal_find_instance_with_global_id(mapper_signal sig,
                                               uint64_t global_id,
                                               int flags)
{
    int i;
    for (i = 0; i < sig->local->id_map_length; i++) {
        if (!sig->local->id_maps[i].map)
            continue;
        if (sig->local->id_maps[i].map->global == global_id) {
            if (sig->local->id_maps[i].status & ~flags)
                return -1;
            else
                return i;
        }
    }
    return -1;
}

static mapper_signal_instance reserved_instance(mapper_signal sig)
{
    int i;
    for (i = 0; i < sig->num_instances; i++) {
        if (!sig->local->instances[i]->is_active) {
            return sig->local->instances[i];
        }
    }
    return 0;
}

int mapper_signal_instance_with_local_id(mapper_signal sig, int instance_id,
                                         int flags, mapper_timetag_t *tt)
{
    if (!sig || !sig->local)
        return -1;

    mapper_signal_id_map_t *maps = sig->local->id_maps;

    mapper_signal_instance si;
    int i;
    for (i = 0; i < sig->local->id_map_length; i++) {
        if (maps[i].instance && maps[i].map->local == instance_id) {
            if (maps[i].status & ~flags)
                return -1;
            else {
                return i;
            }
        }
    }

    // check if device has record of id map
    mapper_id_map map = mapper_device_find_instance_id_map_by_local(sig->device,
                                                                    instance_id);

    /* No instance with that id exists - need to try to activate instance and
     * create new id map. */
    if ((si = find_instance_by_id(sig, instance_id))) {
        if (!map) {
            // Claim id map locally, add id map to device and link from signal
            map = mapper_device_add_instance_id_map(sig->device, instance_id,
                                                    mapper_device_unique_id(sig->device));
        }
        else {
            map->refcount_local++;
        }

        // store pointer to device map in a new signal map
        mapper_signal_init_instance(si);
        i = mapper_signal_add_id_map(sig, si, map);
        if (sig->local->instance_event_handler &&
            (sig->local->instance_event_flags & IN_NEW)) {
            sig->local->instance_event_handler(sig, instance_id, IN_NEW, tt);
        }
        return i;
    }

    if (sig->local->instance_event_handler &&
        (sig->local->instance_event_flags & IN_OVERFLOW)) {
        // call instance event handler
        sig->local->instance_event_handler(sig, -1, IN_OVERFLOW, tt);
    }
    else if (sig->local->handler) {
        if (sig->local->instance_allocation_type == IN_STEAL_OLDEST) {
            i = mapper_signal_oldest_active_instance_internal(sig);
            if (i < 0)
                return -1;
            sig->local->handler(sig, sig->local->id_maps[i].map->local, 0, 0, tt);
        }
        else if (sig->local->instance_allocation_type == IN_STEAL_NEWEST) {
            i = mapper_signal_newest_active_instance_internal(sig);
            if (i < 0)
                return -1;
            sig->local->handler(sig, sig->local->id_maps[i].map->local, 0, 0, tt);
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
            map = mapper_device_add_instance_id_map(sig->device, instance_id,
                                                    mapper_device_unique_id(sig->device));
        }
        else {
            map->refcount_local++;
        }
        mapper_signal_init_instance(si);
        i = mapper_signal_add_id_map(sig, si, map);
        if (sig->local->instance_event_handler &&
            (sig->local->instance_event_flags & IN_NEW)) {
            sig->local->instance_event_handler(sig, instance_id, IN_NEW, tt);
        }
        return i;
    }
    return -1;
}

int mapper_signal_instance_with_global_id(mapper_signal sig, uint64_t global_id,
                                          int flags, mapper_timetag_t *tt)
{
    if (!sig || !sig->local)
        return -1;

    mapper_signal_id_map_t *maps = sig->local->id_maps;

    mapper_signal_instance si;
    int i;
    for (i = 0; i < sig->local->id_map_length; i++) {
        if (maps[i].instance && maps[i].map->global == global_id) {
            if (maps[i].status & ~flags)
                return -1;
            else {
                return i;
            }
        }
    }

    // check if the device already has a map for this global id
    mapper_id_map map = mapper_device_find_instance_id_map_by_global(sig->device,
                                                                     global_id);
    if (!map) {
        /* Here we still risk creating conflicting maps if two signals are
         * updated asynchronously.  This is easy to avoid by not allowing a
         * local id to be used with multiple active remote maps, however users
         * may wish to create devices with multiple object classes which do not
         * require mutual instance id synchronization - e.g. instance 1 of
         * object class A is not related to instance 1 of object B. */
        // TODO: add object groups for explictly sharing id maps

        if ((si = reserved_instance(sig))) {
            map = mapper_device_add_instance_id_map(sig->device, si->id,
                                                    global_id);
            map->refcount_global = 1;

            si->is_active = 1;
            mapper_signal_init_instance(si);
            i = mapper_signal_add_id_map(sig, si, map);
            if (sig->local->instance_event_handler &&
                (sig->local->instance_event_flags & IN_NEW)) {
                sig->local->instance_event_handler(sig, si->id, IN_NEW, tt);
            }
            return i;
        }
    }
    else {
        si = find_instance_by_id(sig, map->local);
        if (!si) {
            /* TODO: Once signal groups are explicit, allow re-mapping to
             * another instance if possible. */
            trace("Signal %s has no instance %i available.\n",
                  sig->name, map->local);
            return -1;
        }
        else if (!si->is_active) {
            si->is_active = 1;
            mapper_signal_init_instance(si);
            i = mapper_signal_add_id_map(sig, si, map);
            map->refcount_local++;
            map->refcount_global++;

            if (sig->local->instance_event_handler &&
                (sig->local->instance_event_flags & IN_NEW)) {
                sig->local->instance_event_handler(sig, si->id, IN_NEW, tt);
            }
            return i;
        }
    }

    // try releasing instance in use
    if (sig->local->instance_event_handler &&
        (sig->local->instance_event_flags & IN_OVERFLOW)) {
        // call instance event handler
        sig->local->instance_event_handler(sig, -1, IN_OVERFLOW, tt);
    }
    else if (sig->local->handler) {
        if (sig->local->instance_allocation_type == IN_STEAL_OLDEST) {
            i = mapper_signal_oldest_active_instance_internal(sig);
            if (i < 0)
                return -1;
            sig->local->handler(sig, sig->local->id_maps[i].map->local, 0, 0, tt);
        }
        else if (sig->local->instance_allocation_type == IN_STEAL_NEWEST) {
            i = mapper_signal_newest_active_instance_internal(sig);
            if (i < 0)
                return -1;
            sig->local->handler(sig, sig->local->id_maps[i].map->local, 0, 0, tt);
        }
        else
            return -1;
    }
    else
        return -1;

    // try again
    if (!map) {
        if ((si = reserved_instance(sig))) {
            map = mapper_device_add_instance_id_map(sig->device, si->id,
                                                    global_id);
            map->refcount_global = 1;

            si->is_active = 1;
            mapper_signal_init_instance(si);
            i = mapper_signal_add_id_map(sig, si, map);
            if (sig->local->instance_event_handler &&
                (sig->local->instance_event_flags & IN_NEW)) {
                sig->local->instance_event_handler(sig, si->id, IN_NEW, tt);
            }
            return i;
        }
    }
    else {
        si = find_instance_by_id(sig, map->local);
        if (!si) {
            trace("Signal %s has no instance %i available.",
                  sig->name, map->local);
            return -1;
        }
        if (si) {
            if (si->is_active) {
                return -1;
            }
            si->is_active = 1;
            mapper_signal_init_instance(si);
            i = mapper_signal_add_id_map(sig, si, map);
            map->refcount_local++;
            map->refcount_global++;

            if (sig->local->instance_event_handler &&
                (sig->local->instance_event_flags & IN_NEW)) {
                sig->local->instance_event_handler(sig, si->id, IN_NEW, tt);
            }
            return i;
        }
    }
    return -1;
}

static int mapper_signal_reserve_instance_internal(mapper_signal sig, int *id,
                                                   void *user_data)
{
    if (sig->num_instances >= MAX_INSTANCES)
        return -1;

    int i, lowest, found;
    mapper_signal_instance si;

    // check if instance with this id already exists! If so, stop here.
    if (id && find_instance_by_id(sig, *id))
        return -1;

    // reallocate array of instances
    sig->local->instances = realloc(sig->local->instances,
                                    sizeof(mapper_signal_instance)
                                    * (sig->num_instances+1));
    sig->local->instances[sig->num_instances] =
        (mapper_signal_instance) calloc(1, sizeof(struct _mapper_signal_instance));
    si = sig->local->instances[sig->num_instances];
    si->value = calloc(1, mapper_signal_vector_bytes(sig));
    si->has_value_flags = calloc(1, sig->length / 8 + 1);
    si->has_value = 0;

    if (id)
        si->id = *id;
    else {
        // find lowest unused positive id
        lowest = -1, found = 0;
        while (!found) {
            lowest++;
            found = 1;
            for (i = 0; i < sig->num_instances; i++) {
                if (sig->local->instances[i]->id == lowest) {
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
        for (i = 0; i < sig->num_instances; i++) {
            if (sig->local->instances[i]->index == lowest) {
                found = 0;
                break;
            }
        }
    }
    si->index = lowest;

    mapper_signal_init_instance(si);
    si->user_data = user_data;

    sig->num_instances ++;
    qsort(sig->local->instances, sig->num_instances,
          sizeof(mapper_signal_instance), compare_ids);

    // return largest index
    int highest = -1;
    for (i = 0; i < sig->num_instances; i++) {
        if (sig->local->instances[i]->index > highest)
            highest = sig->local->instances[i]->index;
    }

    return highest;
}

int mapper_signal_reserve_instances(mapper_signal sig, int num, int *ids,
                                    void **user_data)
{
    if (!sig || !sig->local)
        return 0;

    int i, count = 0, highest = -1, result;
    for (i = 0; i < num; i++) {
        result = mapper_signal_reserve_instance_internal(sig, ids ? &ids[i] : 0,
                                                         user_data ? user_data[i] : 0);
        if (result == -1)
            continue;
        highest = result;
        count++;
    }
    if (highest != -1)
        mapper_device_num_instances_changed(sig->device, sig, highest + 1);
    return count;
}

void mapper_signal_update_instance(mapper_signal sig, int instance_id,
                                   void *value, int count,
                                   mapper_timetag_t timetag)
{
    if (!sig || !sig->local)
        return;

    if (!value) {
        mapper_signal_release_instance(sig, instance_id, timetag);
        return;
    }

    int index = mapper_signal_instance_with_local_id(sig, instance_id, 0,
                                                     &timetag);
    if (index >= 0)
        mapper_signal_update_internal(sig, index, value, count, timetag);
}


int mapper_signal_oldest_active_instance(mapper_signal sig, int *id)
{
    if (!sig || !sig->local)
        return 1;

    int index = mapper_signal_oldest_active_instance_internal(sig);
    if (index < 0)
        return 1;
    else
        *id = sig->local->id_maps[index].map->local;
    return 0;
}

int mapper_signal_oldest_active_instance_internal(mapper_signal sig)
{
    int i;
    mapper_signal_instance si;
    for (i = 0; i < sig->local->id_map_length; i++) {
        if (sig->local->id_maps[i].instance)
            break;
    }
    if (i == sig->local->id_map_length) {
        // no active instances to steal!
        return -1;
    }
    int oldest = i;
    for (i = oldest+1; i < sig->local->id_map_length; i++) {
        if (!(si = sig->local->id_maps[i].instance))
            continue;
        if ((si->created.sec < sig->local->id_maps[oldest].instance->created.sec) ||
            (si->created.sec == sig->local->id_maps[oldest].instance->created.sec &&
             si->created.frac < sig->local->id_maps[oldest].instance->created.frac))
            oldest = i;
    }
    return oldest;
}

int mapper_signal_newest_active_instance(mapper_signal sig, int *id)
{
    if (!sig || !sig->local)
        return 1;

    int index = mapper_signal_newest_active_instance_internal(sig);
    if (index < 0)
        return 1;
    else
        *id = sig->local->id_maps[index].map->local;
    return 0;
}

int mapper_signal_newest_active_instance_internal(mapper_signal sig)
{
    int i;
    mapper_signal_instance si;
    for (i = 0; i < sig->local->id_map_length; i++) {
        if (sig->local->id_maps[i].instance)
            break;
    }
    if (i == sig->local->id_map_length) {
        // no active instances to steal!
        return -1;
    }
    int newest = i;
    for (i = newest+1; i < sig->local->id_map_length; i++) {
        if (!(si = sig->local->id_maps[i].instance))
            continue;
        if ((si->created.sec > sig->local->id_maps[newest].instance->created.sec) ||
            (si->created.sec == sig->local->id_maps[newest].instance->created.sec &&
             si->created.frac > sig->local->id_maps[newest].instance->created.frac))
            newest = i;
    }
    return newest;
}

static void mapper_signal_update_internal(mapper_signal sig, int instance_index,
                                          void *value, int count,
                                          mapper_timetag_t tt)
{
    mapper_signal_instance si = sig->local->id_maps[instance_index].instance;

    /* We have to assume that value points to an array of correct type
     * and size. */

    if (value) {
        if (count<=0) count=1;
        size_t n = mapper_signal_vector_bytes(sig);
        memcpy(si->value, value + n*(count-1), n);
        si->has_value = 1;
    }
    else {
        si->has_value = 0;
    }

    if (memcmp(&tt, &MAPPER_NOW, sizeof(mapper_timetag_t))==0)
        mapper_device_now(sig->device, &si->timetag);
    else
        memcpy(&si->timetag, &tt, sizeof(mapper_timetag_t));

    mapper_device_route_signal(sig->device, sig, instance_index, value,
                               count, si->timetag);
}

void mapper_signal_release_instance(mapper_signal sig, int id,
                                    mapper_timetag_t timetag)
{
    if (!sig || !sig->local)
        return;

    int index = mapper_signal_find_instance_with_local_id(sig, id,
                                                          IN_RELEASED_REMOTELY);
    if (index >= 0)
        mapper_signal_release_instance_internal(sig, index, timetag);
}

void mapper_signal_release_instance_internal(mapper_signal sig,
                                             int instance_index,
                                             mapper_timetag_t tt)
{
    mapper_signal_id_map_t *smap = &sig->local->id_maps[instance_index];
    if (!smap->instance)
        return;

    mapper_timetag_t *ttp;
    if (memcmp(&tt, &MAPPER_NOW, sizeof(mapper_timetag_t))==0) {
        ttp = &sig->db->admin->clock.now;
        mapper_device_now(sig->device, ttp);
    }
    else
        ttp = &tt;

    if (!(smap->status & IN_RELEASED_REMOTELY)) {
        mapper_device_route_signal(sig->device, sig, instance_index, 0, 0, *ttp);
    }

    smap->map->refcount_local--;
    if (smap->map->refcount_local <= 0 && smap->map->refcount_global <= 0) {
        mapper_device_remove_instance_id_map(sig->device, smap->map);
        smap->map = 0;
    }
    else if ((sig->direction & DI_OUTGOING)
             || smap->status & IN_RELEASED_REMOTELY) {
        // TODO: consider multiple upstream source instances?
        smap->map = 0;
    }
    else {
        // mark map as locally-released but do not remove it
        sig->local->id_maps[instance_index].status |= IN_RELEASED_LOCALLY;
    }

    // Put instance back in reserve list
    smap->instance->is_active = 0;
    smap->instance = 0;
}

void mapper_signal_remove_instance(mapper_signal sig, int instance_id)
{
    if (!sig || !sig->local)
        return;

    int i;
    for (i = 0; i < sig->num_instances; i++) {
        if (sig->local->instances[i]->id == instance_id) {
            if (sig->local->instances[i]->is_active) {
                // First release instance
                mapper_timetag_t tt = sig->db->admin->clock.now;
                mapper_device_now(sig->device, &tt);
                mapper_signal_release_instance_internal(sig, i, tt);
            }
            break;
        }
    }

    if (i == sig->num_instances)
        return;

    if (sig->local->instances[i]->value)
        free(sig->local->instances[i]->value);
    if (sig->local->instances[i]->has_value_flags)
        free(sig->local->instances[i]->has_value_flags);
    free(sig->local->instances[i]);
    i++;
    for (; i < sig->num_instances; i++) {
        sig->local->instances[i-1] = sig->local->instances[i];
    }
    --sig->num_instances;
    sig->local->instances = realloc(sig->local->instances,
                                    sizeof(mapper_signal_instance)
                                    * sig->num_instances);

    // TODO: could also realloc signal value histories
}

static void *mapper_signal_instance_value_internal(mapper_signal sig,
                                                   int instance_index,
                                                   mapper_timetag_t *timetag)
{
    mapper_signal_instance si = sig->local->id_maps[instance_index].instance;
    if (!si) return 0;
    if (!si->has_value)
        return 0;
    if (timetag)
        timetag = &si->timetag;
    return si->value;
}

void *mapper_signal_instance_value(mapper_signal sig, int instance_id,
                                   mapper_timetag_t *timetag)
{
    if (!sig || !sig->local)
        return 0;

    int index = mapper_signal_find_instance_with_local_id(sig, instance_id,
                                                          IN_RELEASED_REMOTELY);
    if (index < 0)
        return 0;
    return mapper_signal_instance_value_internal(sig, index, timetag);
}

int mapper_signal_num_active_instances(mapper_signal sig)
{
    if (!sig || !sig->local)
        return -1;
    int i, j = 0;
    for (i = 0; i < sig->local->id_map_length; i++) {
        if (sig->local->id_maps[i].instance)
            j++;
    }
    return j;
}

int mapper_signal_num_reserved_instances(mapper_signal sig)
{
    if (!sig || !sig->local)
        return -1;
    int i, j = 0;
    for (i = 0; i < sig->num_instances; i++) {
        if (!sig->local->instances[i]->is_active)
            j++;
    }
    return j;
}

int mapper_signal_active_instance_id(mapper_signal sig, int index)
{
    if (!sig || !sig->local)
        return -1;
    int i, j = -1;
    for (i = 0; i < sig->local->id_map_length; i++) {
        if (sig->local->id_maps[i].instance)
            j++;
        if (j >= index)
            return sig->local->id_maps[i].map->local;
    }
    return -1;
}

void mapper_signal_set_instance_allocation_mode(mapper_signal sig,
                                                mapper_instance_allocation_type mode)
{
    if (sig && sig->local)
        sig->local->instance_allocation_type = mode;
}

mapper_instance_allocation_type mapper_instance_allocation_mode(mapper_signal sig)
{
    if (sig && sig->local)
        return sig->local->instance_allocation_type;
    return 0;
}

void mapper_signal_set_instance_event_callback(mapper_signal sig,
                                               mapper_instance_event_handler h,
                                               int flags,
                                               void *user_data)
{
    if (!sig || !sig->local)
        return;

    if (!h || !flags) {
        sig->local->instance_event_handler = 0;
        sig->local->instance_event_flags = 0;
        return;
    }

    sig->local->instance_event_handler = h;
    // TODO: use separate user_data for instance event callback?
    sig->user_data = user_data;

    sig->local->instance_event_flags = flags;
}

void mapper_signal_set_instance_data(mapper_signal sig, int instance_id,
                                     void *user_data)
{
    if (!sig || !sig->local)
        return;
    mapper_signal_instance si = find_instance_by_id(sig, instance_id);
    if (si)
        si->user_data = user_data;
}

void *mapper_signal_instance_data(mapper_signal sig, int instance_id)
{
    if (!sig || !sig->local)
        return 0;
    mapper_signal_instance si = find_instance_by_id(sig, instance_id);
    if (si)
        return si->user_data;
    return 0;
}

/**** Queries ****/

void mapper_signal_set_callback(mapper_signal sig,
                                mapper_signal_update_handler *handler,
                                void *user_data)
{
    if (!sig || !sig->local)
        return;
    if (!sig->local->handler && handler) {
        // Need to register a new liblo methods
        sig->local->handler = handler;
        sig->user_data = user_data;
        mapper_device_add_signal_methods(sig->device, sig);
    }
    else if (sig->local->handler && !handler) {
        // Need to remove liblo methods
        sig->local->handler = 0;
        sig->user_data = user_data;
        mapper_device_remove_signal_methods(sig->device, sig);
    }
}

int mapper_signal_query_remotes(mapper_signal sig, mapper_timetag_t tt)
{
    if (!sig || !sig->local)
        return -1;
    if (!sig->local->handler) {
        // no handler defined so we cannot process query responses
        return -1;
    }

    mapper_timetag_t *ttp;
    if (memcmp(&tt, &MAPPER_NOW, sizeof(mapper_timetag_t))==0) {
        ttp = &sig->db->admin->clock.now;
        mapper_device_now(sig->device, ttp);
    }
    else
        ttp = &tt;
    return mapper_device_route_query(sig->device, sig, *ttp);
}

/**** Signal Properties ****/

// Internal function only
int mapper_signal_full_name(mapper_signal sig, char *name, int len)
{
    const char *dev_name = mapper_device_name(sig->device);
    if (!dev_name)
        return 0;

    int dev_name_len = strlen(dev_name);
    if (dev_name_len >= len)
        return 0;
    if ((dev_name_len + strlen(sig->name) + 1) > len)
        return 0;

    snprintf(name, len, "%s%s", dev_name, sig->path);
    return strlen(name);
}

mapper_device mapper_signal_device(mapper_signal sig)
{
    return sig->device;
}

const char *mapper_signal_description(mapper_signal sig)
{
    return sig->description;
}

mapper_direction_t mapper_signal_direction(mapper_signal sig)
{
    return sig->direction;
}

uint64_t mapper_signal_id(mapper_signal sig)
{
    return sig->id;
}

int mapper_signal_length(mapper_signal sig)
{
    return sig->length;
}

void *mapper_signal_maximum(mapper_signal sig)
{
    return sig->maximum;
}

void *mapper_signal_minimum(mapper_signal sig)
{
    return sig->minimum;
}

const char *mapper_signal_name(mapper_signal sig)
{
    return sig->name;
}

int mapper_signal_num_maps(mapper_signal sig)
{
    if (!sig)
        return 0;
    int i, count = 0;
    mapper_router_signal rs = sig->device->local->router->signals;
    while (rs && rs->signal != sig)
        rs = rs->next;
    if (rs) {
        for (i = 0; i < rs->num_slots; i++) {
            if (rs->slots[i])
                count++;
        }
    }
    return count;
}

float mapper_signal_rate(mapper_signal sig)
{
    return sig->rate;
}

char mapper_signal_type(mapper_signal sig)
{
    return sig->type;
}

const char *mapper_signal_unit(mapper_signal sig)
{
    return sig->unit;
}

int mapper_signal_property(mapper_signal sig, const char *property, char *type,
                           const void **value, int *length)
{
    if (!sig)
        return 0;
    return mapper_db_property(sig, sig->extra, property, type, value,
                              length, &sig_table);
}

int mapper_signal_property_index(mapper_signal sig, unsigned int index,
                                 const char **property, char *type,
                                 const void **value, int *length)
{
    if (!sig)
        return 0;
    return mapper_db_property_index(sig, sig->extra, index, property, type,
                                    value, length, &sig_table);
}

void mapper_signal_set_description(mapper_signal sig, const char *description)
{
    if (!sig)
        return;
    mapper_prop_set_string(&sig->description, description);
}

void mapper_signal_set_maximum(mapper_signal sig, void *maximum)
{
    if (!sig)
        return;
    if (maximum) {
        if (!sig->maximum)
            sig->maximum = malloc(mapper_signal_vector_bytes(sig));
        memcpy(sig->maximum, maximum, mapper_signal_vector_bytes(sig));
    }
    else {
        if (sig->maximum)
            free(sig->maximum);
        sig->maximum = 0;
    }
}

void mapper_signal_set_minimum(mapper_signal sig, void *minimum)
{
    if (!sig)
        return;
    if (minimum) {
        if (!sig->minimum)
            sig->minimum = malloc(mapper_signal_vector_bytes(sig));
        memcpy(sig->minimum, minimum, mapper_signal_vector_bytes(sig));
    }
    else {
        if (sig->minimum)
            free(sig->minimum);
        sig->minimum = 0;
    }
}

void mapper_signal_set_rate(mapper_signal sig, float rate)
{
    if (!sig)
        return;
    sig->rate = rate;
}

void mapper_signal_set_unit(mapper_signal sig, const char *unit)
{
    if (!sig)
        return;
    mapper_prop_set_string(&sig->unit, unit);
}

// TODO: use sig_table to simplify error check
void mapper_signal_set_property(mapper_signal sig, const char *property,
                                char type, void *value, int length)
{
    if (!sig || !property)
        return;
    if (   strcmp(property, "direction") == 0
        || strcmp(property, "id") == 0
        || strcmp(property, "length") == 0
        || strcmp(property, "name") == 0
        || strcmp(property, "type") == 0
        || strcmp(property, "user_data") == 0) {
        trace("Cannot set static signal property '%s'\n", property);
        return;
    }
    if (strcmp(property, "min") == 0 || strcmp(property, "minimum") == 0) {
        if (!length || !value)
            mapper_signal_set_minimum(sig, 0);
        else if (length == sig->length && type == sig->type)
            mapper_signal_set_minimum(sig, value);
        // TODO: if types differ need to cast entire vector
        return;
    }
    else if (strcmp(property, "max") == 0 || strcmp(property, "maximum") == 0) {
        if (!length || !value)
            mapper_signal_set_maximum(sig, 0);
        else if (length == sig->length && type == sig->type)
            mapper_signal_set_maximum(sig, value);
        // TODO: if types differ need to cast entire vector
        return;
    }
    else if ((strcmp(property, "unit") == 0 || strcmp(property, "units") == 0)
             && is_string_type(type)) {
        mapper_prop_set_string(&sig->unit, length ? (const char*)value : 0);
        return;
    }
    else if (strcmp(property, "value") == 0) {
        if (!length || !value)
            mapper_signal_update(sig, 0, 0, MAPPER_NOW);
        else if (length == sig->length || type == sig->type)
            mapper_signal_update(sig, value, 1, MAPPER_NOW);
        return;
    }

    mapper_table_add_or_update_typed_value(sig->extra, property, type, value,
                                           length);
}

// TODO: use sig_table to simplify error check
void mapper_signal_remove_property(mapper_signal sig, const char *property)
{
    if (!sig || !property)
        return;
    if (strcmp(property, "direction") == 0 ||
        strcmp(property, "id") == 0 ||
        strcmp(property, "length") == 0 ||
        strcmp(property, "name") == 0 ||
        strcmp(property, "rate") == 0 ||
        strcmp(property, "type") == 0 ||
        strcmp(property, "user_data") == 0) {
        trace("Cannot remove static signal property '%s'\n", property);
        return;
    }
    if (strcmp(property, "description")==0)
        mapper_signal_set_description(sig, 0);
    else if (strcmp(property, "maximum")==0)
        mapper_signal_set_maximum(sig, 0);
    else if (strcmp(property, "minimum")==0)
        mapper_signal_set_minimum(sig, 0);
    table_remove_key(sig->extra, property, 1);
}

static int mapper_signal_add_id_map(mapper_signal sig, mapper_signal_instance si,
                                    mapper_id_map map)
{
    // find unused signal map
    int i;

    for (i = 0; i < sig->local->id_map_length; i++) {
        if (!sig->local->id_maps[i].map)
            break;
    }
    if (i == sig->local->id_map_length) {
        // need more memory
        if (sig->local->id_map_length >= MAX_INSTANCES) {
            // Arbitrary limit to number of tracked id_maps
            return -1;
        }
        sig->local->id_map_length *= 2;
        sig->local->id_maps = realloc(sig->local->id_maps,
                                      sig->local->id_map_length *
                                      sizeof(struct _mapper_signal_id_map));
        memset(sig->local->id_maps + i, 0,
               (sig->local->id_map_length - i)
               * sizeof(struct _mapper_signal_id_map));
    }
    sig->local->id_maps[i].map = map;
    sig->local->id_maps[i].instance = si;
    sig->local->id_maps[i].status = 0;

    return i;
}

void message_add_coerced_signal_instance_value(lo_message m,
                                               mapper_signal sig,
                                               mapper_signal_instance si,
                                               int length,
                                               const char type)
{
    int i;
    int min_length = length < sig->length ? length : sig->length;

    switch (sig->type) {
        case 'i': {
            int *v = (int*)si->value;
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
            break;
        }
        case 'f': {
            float *v = (float*)si->value;
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
            break;
        }
        case 'd': {
            double *v = (double*)si->value;
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
            break;
        }
    }
    for (i = min_length; i < length; i++)
        lo_message_add_nil(m);
}

void mapper_signal_prepare_message(mapper_signal sig, lo_message msg)
{
    /* unique id */
    lo_message_add_string(msg, mapper_param_string(AT_ID));
    lo_message_add_int64(msg, sig->id);

    /* direction */
    lo_message_add_string(msg, mapper_param_string(AT_DIRECTION));
    if (sig->direction == DI_BOTH)
        lo_message_add_string(msg, "both");
    else if (sig->direction == DI_OUTGOING)
        lo_message_add_string(msg, "output");
    else
        lo_message_add_string(msg, "input");

    /* data type */
    lo_message_add_string(msg, mapper_param_string(AT_TYPE));
    lo_message_add_char(msg, sig->type);

    /* vector length */
    lo_message_add_string(msg, mapper_param_string(AT_LENGTH));
    lo_message_add_int32(msg, sig->length);

    /* unit */
    if (sig->unit) {
        lo_message_add_string(msg, mapper_param_string(AT_UNITS));
        lo_message_add_string(msg, sig->unit);
    }

    /* minimum */
    if (sig->minimum) {
        lo_message_add_string(msg, mapper_param_string(AT_MIN));
        mapper_message_add_typed_value(msg, sig->type, sig->length, sig->minimum);
    }

    /* maximum */
    if (sig->maximum) {
        lo_message_add_string(msg, mapper_param_string(AT_MAX));
        mapper_message_add_typed_value(msg, sig->type, sig->length, sig->maximum);
    }

    /* number of instances */
    lo_message_add_string(msg, mapper_param_string(AT_INSTANCES));
    lo_message_add_int32(msg, sig->num_instances);

    /* update rate */
    lo_message_add_string(msg, mapper_param_string(AT_RATE));
    lo_message_add_float(msg, sig->rate);

    /* "extra" properties */
    mapper_message_add_value_table(msg, sig->extra);
}

mapper_signal *mapper_signal_query_union(mapper_signal *query1,
                                         mapper_signal *query2)
{
    return (mapper_signal*)mapper_list_query_union((void**)query1,
                                                   (void**)query2);
}

mapper_signal *mapper_signal_query_intersection(mapper_signal *query1,
                                                mapper_signal *query2)
{
    return (mapper_signal*)mapper_list_query_intersection((void**)query1,
                                                          (void**)query2);
}

mapper_signal *mapper_signal_query_difference(mapper_signal *query1,
                                              mapper_signal *query2)
{
    return (mapper_signal*)mapper_list_query_difference((void**)query1,
                                                        (void**)query2);
}

mapper_signal mapper_signal_query_index(mapper_signal *sig, int index)
{
    return (mapper_signal)mapper_list_query_index((void**)sig, index);
}

mapper_signal *mapper_signal_query_next(mapper_signal *sig)
{
    return (mapper_signal*)mapper_list_query_next((void**)sig);
}

void mapper_signal_query_done(mapper_signal *sig)
{
    mapper_list_query_done((void**)sig);
}
