
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
static int _get_oldest_inst(mapper_signal sig);

static int _get_newest_inst(mapper_signal sig);

static int _find_inst_with_local_id(mapper_signal sig, mapper_id id, int flags);

static int _add_idmap(mapper_signal sig, mapper_signal_inst si, mapper_idmap map);

static int _compare_ids(const void *l, const void *r)
{
    return memcmp(&(*(mapper_signal_inst *)l)->id, &(*(mapper_signal_inst *)r)->id,
                  sizeof(mapper_id));
}

static mapper_signal_inst _find_inst_by_id(mapper_signal sig, mapper_id id)
{
    if (!sig->num_inst)
        return 0;

    mapper_signal_inst_t si;
    mapper_signal_inst sip = &si;
    si.id = id;

    mapper_signal_inst *sipp = bsearch(&sip, sig->local->inst, sig->num_inst,
                                       sizeof(mapper_signal_inst), _compare_ids);
    if (sipp && *sipp)
        return *sipp;
    return 0;
}

void mapper_signal_init(mapper_signal sig, mapper_direction dir,
                        int num_inst, const char *name, int len,
                        mapper_type type, const char *unit,
                        const void *min, const void *max,
                        mapper_signal_update_handler *handler)
{
    int i;
    if (!name)
        return;

    name = skip_slash(name);
    int str_len = strlen(name)+2;
    sig->path = malloc(str_len);
    snprintf(sig->path, str_len, "/%s", name);
    sig->name = (char*)sig->path+1;

    sig->len = len;
    sig->type = type;
    sig->dir = dir ?: MAPPER_DIR_OUT;
    sig->unit = unit ? strdup(unit) : strdup("unknown");
    sig->min = sig->max = 0;

    sig->num_inst = 0;

    if (sig->local) {
        sig->local->update_handler = handler;
        sig->local->vec_known = calloc(1, len / 8 + 1);
        for (i = 0; i < len; i++) {
            sig->local->vec_known[i/8] |= 1 << (i % 8);
        }

        if (num_inst)
            mapper_signal_reserve_instances(sig, num_inst, 0, 0);

        // Reserve one instance id map
        sig->local->idmap_len = 1;
        sig->local->idmaps = calloc(1, sizeof(struct _mapper_signal_idmap));
    }
    else {
        sig->obj.props.staged = mapper_table_new();
    }

    sig->obj.type = MAPPER_OBJ_SIGNAL;
    sig->obj.props.synced = mapper_table_new();
    sig->obj.props.mask = 0;

    mapper_table tab = sig->obj.props.synced;

    // these properties need to be added in alphabetical order
    mapper_table_link(tab, MAPPER_PROP_DEVICE, 1, MAPPER_DEVICE, &sig->dev,
                      NON_MODIFIABLE | INDIRECT | LOCAL_ACCESS_ONLY);

    mapper_table_link(tab, MAPPER_PROP_DIR, 1, MAPPER_INT32, &sig->dir,
                      NON_MODIFIABLE);

    mapper_table_link(tab, MAPPER_PROP_ID, 1, MAPPER_INT64, &sig->obj.id,
                      NON_MODIFIABLE);

    mapper_table_link(tab, MAPPER_PROP_LENGTH, 1, MAPPER_INT32, &sig->len,
                      NON_MODIFIABLE);

    mapper_table_link(tab, MAPPER_PROP_MAX, sig->len, sig->type, &sig->max,
                      (sig->local ? MODIFIABLE : NON_MODIFIABLE) | INDIRECT);

    mapper_table_link(tab, MAPPER_PROP_MIN, sig->len, sig->type, &sig->min,
                      (sig->local ? MODIFIABLE : NON_MODIFIABLE) | INDIRECT);

    mapper_table_link(tab, MAPPER_PROP_NAME, 1, MAPPER_STRING, &sig->name,
                      NON_MODIFIABLE | INDIRECT);

    mapper_table_link(tab, MAPPER_PROP_NUM_INSTANCES, 1, MAPPER_INT32,
                      &sig->num_inst, NON_MODIFIABLE);

    mapper_table_link(tab, MAPPER_PROP_NUM_MAPS_IN, 1, MAPPER_INT32,
                      &sig->num_maps_in, NON_MODIFIABLE);

    mapper_table_link(tab, MAPPER_PROP_NUM_MAPS_OUT, 1, MAPPER_INT32,
                      &sig->num_maps_out, NON_MODIFIABLE);

    mapper_table_link(tab, MAPPER_PROP_PERIOD, 1, MAPPER_FLOAT, &sig->period,
                      NON_MODIFIABLE);

    mapper_table_link(tab, MAPPER_PROP_JITTER, 1, MAPPER_FLOAT, &sig->jitter,
                      NON_MODIFIABLE);

    mapper_table_link(tab, MAPPER_PROP_TYPE, 1, MAPPER_CHAR, &sig->type,
                      NON_MODIFIABLE);

    mapper_table_link(tab, MAPPER_PROP_UNIT, 1, MAPPER_STRING, &sig->unit,
                      (sig->local ? MODIFIABLE : NON_MODIFIABLE) | INDIRECT);

    mapper_table_link(tab, MAPPER_PROP_USER_DATA, 1, MAPPER_PTR, &sig->obj.user,
                      MODIFIABLE | INDIRECT | LOCAL_ACCESS_ONLY);

    mapper_table_link(tab, MAPPER_PROP_VERSION, 1, MAPPER_INT32,
                      &sig->obj.version, NON_MODIFIABLE);

    if (min)
        mapper_table_set_record(tab, MAPPER_PROP_MIN, NULL, len, type,
                                min, LOCAL_MODIFY);
    if (max)
        mapper_table_set_record(tab, MAPPER_PROP_MAX, NULL, len, type,
                                max, LOCAL_MODIFY);

    mapper_table_set_record(tab, MAPPER_PROP_IS_LOCAL, NULL, 1, MAPPER_BOOL,
                            &sig->local, LOCAL_ACCESS_ONLY | NON_MODIFIABLE);
}

void mapper_signal_free(mapper_signal sig)
{
    int i;
    if (!sig) return;

    if (sig->local) {
        // Free instances
        for (i = 0; i < sig->local->idmap_len; i++) {
            if (sig->local->idmaps[i].inst) {
                mapper_signal_release_inst_internal(sig, i, MAPPER_NOW);
            }
        }
        free(sig->local->idmaps);
        for (i = 0; i < sig->num_inst; i++) {
            if (sig->local->inst[i]->val)
                free(sig->local->inst[i]->val);
            if (sig->local->inst[i]->has_val_flags)
                free(sig->local->inst[i]->has_val_flags);
            free(sig->local->inst[i]);
        }
        free(sig->local->inst);
        if (sig->local->vec_known)
            free(sig->local->vec_known);
        free(sig->local);
    }

    if (sig->obj.props.synced)
        mapper_table_free(sig->obj.props.synced);
    if (sig->obj.props.staged)
        mapper_table_free(sig->obj.props.staged);
    if (sig->max)
        free(sig->max);
    if (sig->min)
        free(sig->min);
    if (sig->path)
        free(sig->path);
    if (sig->unit)
        free(sig->unit);
}

/**** Instances ****/

static void _init_inst(mapper_signal_inst si)
{
    si->has_val = 0;
    mapper_time_now(&si->created);
}

static int _find_inst_with_local_id(mapper_signal sig, mapper_id id, int flags)
{
    int i;
    for (i = 0; i < sig->local->idmap_len; i++) {
        if (   sig->local->idmaps[i].inst
            && sig->local->idmaps[i].map->local == id) {
            if (sig->local->idmaps[i].status & ~flags)
                return -1;
            else
                return i;
        }
    }
    return -1;
}

int mapper_signal_find_inst_with_global_id(mapper_signal sig, mapper_id id,
                                           int flags)
{
    int i;
    for (i = 0; i < sig->local->idmap_len; i++) {
        if (!sig->local->idmaps[i].map)
            continue;
        if (sig->local->idmaps[i].map->global == id) {
            if (sig->local->idmaps[i].status & ~flags)
                return -1;
            else
                return i;
        }
    }
    return -1;
}

static mapper_signal_inst _reserved_inst(mapper_signal sig)
{
    int i;
    for (i = 0; i < sig->num_inst; i++) {
        if (!sig->local->inst[i]->active) {
            return sig->local->inst[i];
        }
    }
    return 0;
}

int mapper_signal_inst_with_local_id(mapper_signal sig, mapper_id id, int flags,
                                     mapper_time_t *t)
{
    if (!sig || !sig->local)
        return -1;

    mapper_signal_idmap_t *maps = sig->local->idmaps;
    mapper_signal_update_handler *update_h = sig->local->update_handler;
    mapper_instance_event_handler *event_h = sig->local->inst_event_handler;

    mapper_signal_inst si;
    int i;
    for (i = 0; i < sig->local->idmap_len; i++) {
        if (maps[i].inst && maps[i].map->local == id) {
            if (maps[i].status & ~flags)
                return -1;
            else {
                return i;
            }
        }
    }

    // check if device has record of id map
    mapper_idmap map = mapper_device_idmap_by_local(sig->dev, sig->local->group, id);

    /* No instance with that id exists - need to try to activate instance and
     * create new id map. */
    if ((si = _find_inst_by_id(sig, id))) {
        if (!map) {
            // Claim id map locally, add id map to device and link from signal
            mapper_id global = mapper_device_generate_unique_id(sig->dev);
            map = mapper_device_add_idmap(sig->dev, sig->local->group, id,
                                          global);
        }
        else {
            ++map->refcount_local;
        }

        // store pointer to device map in a new signal map
        si->active = 1;
        _init_inst(si);
        i = _add_idmap(sig, si, map);
        if (event_h && (sig->local->inst_event_flags & MAPPER_NEW_INSTANCE)) {
            event_h(sig, id, MAPPER_NEW_INSTANCE, t);
        }
        return i;
    }

    if (event_h && (sig->local->inst_event_flags & MAPPER_INSTANCE_OVERFLOW)) {
        // call instance event handler
        event_h(sig, 0, MAPPER_INSTANCE_OVERFLOW, t);
    }
    else if (update_h) {
        if (sig->local->stealing_mode == MAPPER_STEAL_OLDEST) {
            i = _get_oldest_inst(sig);
            if (i < 0)
                return -1;
            update_h(sig, sig->local->idmaps[i].map->local, 0, sig->type, 0, t);
        }
        else if (sig->local->stealing_mode == MAPPER_STEAL_NEWEST) {
            i = _get_newest_inst(sig);
            if (i < 0)
                return -1;
            update_h(sig, sig->local->idmaps[i].map->local, 0, sig->type, 0, t);
        }
        else
            return -1;
    }
    else
        return -1;

    // try again
    if ((si = _find_inst_by_id(sig, id))) {
        if (!map) {
            // Claim id map locally add id map to device and link from signal
            mapper_id global = mapper_device_generate_unique_id(sig->dev);
            map = mapper_device_add_idmap(sig->dev, sig->local->group, id,
                                          global);
        }
        else {
            ++map->refcount_local;
        }
        si->active = 1;
        _init_inst(si);
        i = _add_idmap(sig, si, map);
        if (event_h && (sig->local->inst_event_flags & MAPPER_NEW_INSTANCE)) {
            event_h(sig, id, MAPPER_NEW_INSTANCE, t);
        }
        return i;
    }
    return -1;
}

int mapper_signal_inst_with_global_id(mapper_signal sig, mapper_id id,
                                      int flags, mapper_time_t *t)
{
    if (!sig || !sig->local)
        return -1;

    mapper_signal_idmap_t *maps = sig->local->idmaps;
    mapper_signal_update_handler *update_h = sig->local->update_handler;
    mapper_instance_event_handler *event_h = sig->local->inst_event_handler;

    mapper_signal_inst si;
    int i;
    for (i = 0; i < sig->local->idmap_len; i++) {
        if (maps[i].inst && maps[i].map->global == id) {
            if (maps[i].status & ~flags)
                return -1;
            else {
                return i;
            }
        }
    }

    // check if the device already has a map for this global id
    mapper_idmap map = mapper_device_idmap_by_global(sig->dev, sig->local->group, id);
    if (!map) {
        /* Here we still risk creating conflicting maps if two signals are
         * updated asynchronously.  This is easy to avoid by not allowing a
         * local id to be used with multiple active remote maps, however users
         * may wish to create devices with multiple object classes which do not
         * require mutual instance id synchronization - e.g. instance 1 of
         * object class A is not related to instance 1 of object B. */
        // TODO: add object groups for explictly sharing id maps

        if ((si = _reserved_inst(sig))) {
            map = mapper_device_add_idmap(sig->dev, sig->local->group, si->id, id);
            map->refcount_global = 1;

            si->active = 1;
            _init_inst(si);
            i = _add_idmap(sig, si, map);
            if (event_h && (sig->local->inst_event_flags & MAPPER_NEW_INSTANCE)) {
                event_h(sig, si->id, MAPPER_NEW_INSTANCE, t);
            }
            return i;
        }
    }
    else {
        si = _find_inst_by_id(sig, map->local);
        if (!si) {
            /* TODO: Once signal groups are explicit, allow re-mapping to
             * another instance if possible. */
            trace("Signal %s has no instance %"PR_MAPPER_ID" available.\n",
                  sig->name, map->local);
            return -1;
        }
        else if (!si->active) {
            si->active = 1;
            _init_inst(si);
            i = _add_idmap(sig, si, map);
            ++map->refcount_local;
            ++map->refcount_global;

            if (event_h && (sig->local->inst_event_flags & MAPPER_NEW_INSTANCE)) {
                event_h(sig, si->id, MAPPER_NEW_INSTANCE, t);
            }
            return i;
        }
    }

    // try releasing instance in use
    if (event_h && (sig->local->inst_event_flags & MAPPER_INSTANCE_OVERFLOW)) {
        // call instance event handler
        event_h(sig, 0, MAPPER_INSTANCE_OVERFLOW, t);
    }
    else if (update_h) {
        if (sig->local->stealing_mode == MAPPER_STEAL_OLDEST) {
            i = _get_oldest_inst(sig);
            if (i < 0)
                return -1;
            update_h(sig, sig->local->idmaps[i].map->local, 0, sig->type, 0, t);
        }
        else if (sig->local->stealing_mode == MAPPER_STEAL_NEWEST) {
            i = _get_newest_inst(sig);
            if (i < 0)
                return -1;
            update_h(sig, sig->local->idmaps[i].map->local, 0, sig->type, 0, t);
        }
        else
            return -1;
    }
    else
        return -1;

    // try again
    if (!map) {
        if ((si = _reserved_inst(sig))) {
            map = mapper_device_add_idmap(sig->dev, sig->local->group, si->id, id);
            map->refcount_global = 1;

            si->active = 1;
            _init_inst(si);
            i = _add_idmap(sig, si, map);
            if (event_h && (sig->local->inst_event_flags & MAPPER_NEW_INSTANCE)) {
                event_h(sig, si->id, MAPPER_NEW_INSTANCE, t);
            }
            return i;
        }
    }
    else {
        si = _find_inst_by_id(sig, map->local);
        if (!si) {
            trace("Signal %s has no instance %"PR_MAPPER_ID" available.",
                  sig->name, map->local);
            return -1;
        }
        if (si) {
            if (si->active) {
                return -1;
            }
            si->active = 1;
            _init_inst(si);
            i = _add_idmap(sig, si, map);
            ++map->refcount_local;
            ++map->refcount_global;

            if (event_h && (sig->local->inst_event_flags & MAPPER_NEW_INSTANCE)) {
                event_h(sig, si->id, MAPPER_NEW_INSTANCE, t);
            }
            return i;
        }
    }
    return -1;
}

static int _reserve_inst(mapper_signal sig, mapper_id *id, void *user)
{
    if (sig->num_inst >= MAX_INSTANCES)
        return -1;

    int i, lowest, cont;
    mapper_signal_inst si;

    // check if instance with this id already exists! If so, stop here.
    if (id && _find_inst_by_id(sig, *id))
        return -1;

    // reallocate array of instances
    sig->local->inst = realloc(sig->local->inst,
                               sizeof(mapper_signal_inst) * (sig->num_inst+1));
    sig->local->inst[sig->num_inst] =
        (mapper_signal_inst) calloc(1, sizeof(struct _mapper_signal_inst));
    si = sig->local->inst[sig->num_inst];
    si->val = calloc(1, mapper_signal_vector_bytes(sig));
    si->has_val_flags = calloc(1, sig->len / 8 + 1);
    si->has_val = 0;

    if (id)
        si->id = *id;
    else {
        // find lowest unused id
        mapper_id lowest_id = 0;
        cont = 1;
        while (cont) {
            cont = 0;
            for (i = 0; i < sig->num_inst; i++) {
                if (sig->local->inst[i]->id == lowest_id) {
                    cont = 1;
                    break;
                }
            }
            lowest_id += cont;
        }
        si->id = lowest_id;
    }
    // find lowest unused positive index
    lowest = 0;
    cont = 1;
    while (cont) {
        cont = 0;
        for (i = 0; i < sig->num_inst; i++) {
            if (sig->local->inst[i]->idx == lowest) {
                cont = 1;
                break;
            }
        }
        lowest += cont;
    }
    si->idx = lowest;

    _init_inst(si);
    si->user = user;

    ++sig->num_inst;
    qsort(sig->local->inst, sig->num_inst, sizeof(mapper_signal_inst), _compare_ids);

    // return largest index
    int highest = -1;
    for (i = 0; i < sig->num_inst; i++) {
        if (sig->local->inst[i]->idx > highest)
            highest = sig->local->inst[i]->idx;
    }

    return highest;
}

int mapper_signal_reserve_instances(mapper_signal sig, int num, mapper_id *ids,
                                    void **user)
{
    if (!sig || !sig->local || !num)
        return 0;

    int i = 0, count = 0, highest = -1, result;
    if (sig->num_inst == 1 && !sig->local->inst[0]->id
        && !sig->local->inst[0]->user) {
        // we will overwite the default instance first
        if (ids)
            sig->local->inst[0]->id = ids[0];
        if (user)
            sig->local->inst[0]->user = user[0];
        ++i;
        ++count;
    }
    for (; i < num; i++) {
        result = _reserve_inst(sig, ids ? &ids[i] : 0, user ? user[i] : 0);
        if (result == -1)
            continue;
        highest = result;
        ++count;
    }
    if (highest != -1)
        mapper_device_on_num_inst_changed(sig->dev, sig, highest + 1);
    return count;
}

int mapper_signal_get_instance_is_active(mapper_signal sig, mapper_id id)
{
    if (!sig)
        return 0;
    int idx = _find_inst_with_local_id(sig, id, 0);
    if (idx < 0)
        return 0;
    return sig->local->idmaps[idx].inst->active;
}

mapper_id mapper_signal_get_oldest_instance_id(mapper_signal sig)
{
    if (!sig || !sig->local)
        return 0;

    int idx = _get_oldest_inst(sig);
    if (idx < 0)
        return 0;
    else
        return sig->local->idmaps[idx].map->local;
    return 0;
}

int _get_oldest_inst(mapper_signal sig)
{
    int i;
    mapper_signal_inst si;
    for (i = 0; i < sig->local->idmap_len; i++) {
        if (sig->local->idmaps[i].inst)
            break;
    }
    if (i == sig->local->idmap_len) {
        // no active instances to steal!
        return -1;
    }
    int oldest = i;
    for (i = oldest+1; i < sig->local->idmap_len; i++) {
        if (!(si = sig->local->idmaps[i].inst))
            continue;
        if ((si->created.sec < sig->local->idmaps[oldest].inst->created.sec) ||
            (si->created.sec == sig->local->idmaps[oldest].inst->created.sec &&
             si->created.frac < sig->local->idmaps[oldest].inst->created.frac))
            oldest = i;
    }
    return oldest;
}

mapper_id mapper_signal_get_newest_instance(mapper_signal sig)
{
    if (!sig || !sig->local)
        return 0;

    int idx = _get_newest_inst(sig);
    if (idx < 0)
        return 0;
    else
        return sig->local->idmaps[idx].map->local;
    return 0;
}

int _get_newest_inst(mapper_signal sig)
{
    int i;
    mapper_signal_inst si;
    for (i = 0; i < sig->local->idmap_len; i++) {
        if (sig->local->idmaps[i].inst)
            break;
    }
    if (i == sig->local->idmap_len) {
        // no active instances to steal!
        return -1;
    }
    int newest = i;
    for (i = newest+1; i < sig->local->idmap_len; i++) {
        if (!(si = sig->local->idmaps[i].inst))
            continue;
        if ((si->created.sec > sig->local->idmaps[newest].inst->created.sec) ||
            (si->created.sec == sig->local->idmaps[newest].inst->created.sec &&
             si->created.frac > sig->local->idmaps[newest].inst->created.frac))
            newest = i;
    }
    return newest;
}

void mapper_signal_update_timing_stats(mapper_signal sig, float diff)
{
    if (-1 == sig->period)
        sig->period = 0;
    else if (0 == sig->period) {
        sig->period = diff;
    }
    else {
        sig->jitter *= 0.99;
        sig->jitter += (0.01 * fabsf(sig->period - diff));
        sig->period *= 0.99;
        sig->period += (0.01 * diff);
    }
}

void mapper_signal_set_value(mapper_signal sig, mapper_id id, int len,
                             mapper_type type, const void *val,
                             mapper_time_t time)
{
    if (!sig || !sig->local)
        return;

    if (!val) {
        mapper_signal_release_instance(sig, id, time);
        return;
    }

    if (!type_is_num(type)) {
#ifdef DEBUG
        trace("called update on signal '%s' with non-number type '%c'\n",
              sig->name, type);
#endif
        return;
    }
    if (len % sig->len != 0) {
#ifdef DEBUG
        trace("called update on signal '%s' with value length %d (should be "
              "a multiple of %d)\n", sig->name, len, sig->len);
#endif
        return;
    }

    int idx = mapper_signal_inst_with_local_id(sig, id, 0, &time);
    if (idx < 0)
        return;

    mapper_signal_inst si = sig->local->idmaps[idx].inst;

    void *coerced = (void*)val;

    if (len && val) {
        if (type != sig->type) {
            coerced = alloca(mapper_type_size(sig->type) * sig->len);
            set_coerced_val(len, type, val, sig->len, sig->type, coerced);
        }
        // need to copy last value to signal instance memory
        size_t n = mapper_signal_vector_bytes(sig);
        memcpy(si->val, coerced + n * (len - sig->len), n);
        si->has_val = 1;
    }
    else {
        si->has_val = 0;
    }

    float diff;
    if (time_is_now(&time)) {
        mapper_time_t now;
        mapper_time_now(&now);
        diff = mapper_time_difference(now, si->time);
        memcpy(&si->time, &now, sizeof(mapper_time_t));
    }
    else {
        diff = mapper_time_difference(time, si->time);
        memcpy(&si->time, &time, sizeof(mapper_time_t));
    }
    mapper_signal_update_timing_stats(sig, diff);

    mapper_device_route_signal(sig->dev, sig, idx, coerced, len / sig->len,
                               si->time);
}

void mapper_signal_release_instance(mapper_signal sig, mapper_id id,
                                    mapper_time_t time)
{
    if (!sig || !sig->local)
        return;

    int idx = _find_inst_with_local_id(sig, id, RELEASED_REMOTELY);
    if (idx >= 0)
        mapper_signal_release_inst_internal(sig, idx, time);
}

void mapper_signal_release_inst_internal(mapper_signal sig, int idx,
                                         mapper_time_t t)
{
    mapper_signal_idmap_t *smap = &sig->local->idmaps[idx];
    if (!smap->inst)
        return;

    if (time_is_now(&t))
        mapper_time_now(&t);

    mapper_device_route_signal(sig->dev, sig, idx, 0, 0, t);

    --smap->map->refcount_local;
    if (smap->map->refcount_local <= 0 && smap->map->refcount_global <= 0) {
        mapper_device_remove_idmap(sig->dev, sig->local->group, smap->map);
        smap->map = 0;
    }
    else if ((sig->dir & MAPPER_DIR_OUT) || smap->status & RELEASED_REMOTELY) {
        // TODO: consider multiple upstream source instances?
        smap->map = 0;
    }
    else {
        // mark map as locally-released but do not remove it
        sig->local->idmaps[idx].status |= RELEASED_LOCALLY;
    }

    // Put instance back in reserve list
    smap->inst->active = 0;
    smap->inst = 0;
}

void mapper_signal_remove_instance(mapper_signal sig, mapper_id id)
{
    if (!sig || !sig->local)
        return;

    int i;
    for (i = 0; i < sig->num_inst; i++) {
        if (sig->local->inst[i]->id == id) {
            if (sig->local->inst[i]->active) {
                // First release instance
                mapper_time_t t;
                mapper_time_now(&t);
                mapper_signal_release_inst_internal(sig, i, t);
            }
            break;
        }
    }

    if (i == sig->num_inst)
        return;

    if (sig->local->inst[i]->val)
        free(sig->local->inst[i]->val);
    if (sig->local->inst[i]->has_val_flags)
        free(sig->local->inst[i]->has_val_flags);
    free(sig->local->inst[i]);
    ++i;
    for (; i < sig->num_inst; i++) {
        sig->local->inst[i-1] = sig->local->inst[i];
    }
    --sig->num_inst;
    sig->local->inst = realloc(sig->local->inst,
                               sizeof(mapper_signal_inst) * sig->num_inst);

    // TODO: could also realloc signal value histories
}

const void *mapper_signal_get_value(mapper_signal sig, mapper_id id,
                                    mapper_time_t *time)
{
    if (!sig || !sig->local)
        return 0;

    int idx = _find_inst_with_local_id(sig, id, RELEASED_REMOTELY);
    if (idx < 0)
        return 0;

    mapper_signal_inst si = sig->local->idmaps[idx].inst;
    if (!si) return 0;
    if (!si->has_val)
        return 0;
    if (time) {
        time->sec = si->time.sec;
        time->frac = si->time.frac;
    }
    return si->val;
}

int mapper_signal_get_num_instances(mapper_signal sig)
{
    return sig ? sig->num_inst : -1;
}

int mapper_signal_get_num_active_instances(mapper_signal sig)
{
    if (!sig || !sig->local)
        return -1;
    int i, j = 0;
    for (i = 0; i < sig->local->idmap_len; i++) {
        if (sig->local->idmaps[i].inst)
            ++j;
    }
    return j;
}

int mapper_signal_get_num_reserved_instances(mapper_signal sig)
{
    if (!sig || !sig->local)
        return -1;
    int i, j = 0;
    for (i = 0; i < sig->num_inst; i++) {
        if (!sig->local->inst[i]->active)
            ++j;
    }
    return j;
}

mapper_id mapper_signal_get_instance_id(mapper_signal sig, int idx)
{
    if (!sig || !sig->local)
        return 0;
    int i;
    for (i = 0; i < sig->num_inst; i++) {
        if (i == idx)
            return sig->local->inst[i]->id;
    }
    return 0;
}

mapper_id mapper_signal_get_active_instance_id(mapper_signal sig, int idx)
{
    if (!sig || !sig->local)
        return 0;
    int i, j = -1;
    for (i = 0; i < sig->local->idmap_len; i++) {
        if (sig->local->idmaps[i].inst)
            ++j;
        if (j == idx)
            return sig->local->idmaps[i].map->local;
    }
    return 0;
}

mapper_id mapper_signal_get_reserved_instance_id(mapper_signal sig, int idx)
{
    if (!sig || !sig->local)
        return 0;
    int i, j = -1;
    for (i = 0; i < sig->num_inst; i++) {
        if (!sig->local->inst[i]->active)
            ++j;
        if (j == idx)
            return sig->local->inst[i]->id;
    }
    return 0;
}

    // TODO: move to normal properties
void mapper_signal_set_stealing_mode(mapper_signal sig,
                                     mapper_stealing_type mode)
{
    if (sig && sig->local)
        sig->local->stealing_mode = mode;
}

mapper_stealing_type mapper_signal_get_stealing_mode(mapper_signal sig)
{
    if (sig && sig->local)
        return sig->local->stealing_mode;
    return 0;
}

void mapper_signal_set_instance_event_callback(mapper_signal sig,
                                               mapper_instance_event_handler h,
                                               int flags)
{
    if (!sig || !sig->local)
        return;

    if (!h || !flags) {
        sig->local->inst_event_handler = 0;
        sig->local->inst_event_flags = 0;
        return;
    }

    sig->local->inst_event_handler = h;
    sig->local->inst_event_flags = flags;
}

int mapper_signal_activate_instance(mapper_signal sig, mapper_id id)
{
    if (!sig || !sig->local)
        return 0;
    int idx = mapper_signal_inst_with_local_id(sig, id, 0, 0);
    return idx >= 0;
}

void mapper_signal_set_instance_user_data(mapper_signal sig, mapper_id id,
                                          const void *user)
{
    if (!sig || !sig->local)
        return;
    mapper_signal_inst si = _find_inst_by_id(sig, id);
    if (si)
        si->user = (void*)user;
}

void *mapper_signal_get_instance_user_data(mapper_signal sig, mapper_id id)
{
    if (!sig || !sig->local)
        return 0;
    mapper_signal_inst si = _find_inst_by_id(sig, id);
    if (si)
        return si->user;
    return 0;
}

/**** Queries ****/

void mapper_signal_set_callback(mapper_signal sig,
                                mapper_signal_update_handler *handler)
{
    if (!sig || !sig->local)
        return;
    if (!sig->local->update_handler && handler) {
        // Need to register a new liblo methods
        sig->local->update_handler = handler;
        mapper_device_add_signal_methods(sig->dev, sig);
    }
    else if (sig->local->update_handler && !handler) {
        // Need to remove liblo methods
        sig->local->update_handler = 0;
        mapper_device_remove_signal_methods(sig->dev, sig);
    }
}

mapper_signal_group mapper_signal_get_group(mapper_signal sig)
{
    return (sig && sig->local) ? sig->local->group : 0;
}

void mapper_signal_set_group(mapper_signal sig, mapper_signal_group group)
{
    // first check if group exists
    if (!sig || !sig->local || group >= sig->dev->local->num_signal_groups)
        return;

    sig->local->group = group;
}

/**** Signal Properties ****/

// Internal function only
int mapper_signal_full_name(mapper_signal sig, char *name, int len)
{
    const char *dev_name = mapper_device_get_name(sig->dev);
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

mapper_device mapper_signal_get_device(mapper_signal sig)
{
    return sig->dev;
}

int mapper_signal_get_num_maps(mapper_signal sig, mapper_direction dir)
{
    if (!sig)
        return 0;
    if (!dir)
        return sig->num_maps_in + sig->num_maps_out;
    return (  ((dir & MAPPER_DIR_IN) ? sig->num_maps_in : 0)
            + ((dir & MAPPER_DIR_OUT) ? sig->num_maps_out : 0));
}

static int cmp_query_signal_maps(const void *context_data, mapper_map map)
{
    mapper_signal sig = *(mapper_signal *)context_data;
    int dir = *(int*)(context_data + sizeof(int64_t));
    if (!dir || (dir & MAPPER_DIR_OUT)) {
        int i;
        for (i = 0; i < map->num_src; i++) {
            if (map->src[i]->sig == sig)
            return 1;
        }
    }
    if (!dir || (dir & MAPPER_DIR_IN)) {
        if (map->dst->sig == sig)
        return 1;
    }
    return 0;
}

mapper_object *mapper_signal_get_maps(mapper_signal sig, mapper_direction dir)
{
    if (!sig || !sig->obj.graph->maps)
        return 0;
    return ((mapper_object *)
            mapper_list_new_query(sig->obj.graph->maps, cmp_query_signal_maps,
                                  "vi", &sig, dir));
}

static int _add_idmap(mapper_signal sig, mapper_signal_inst si, mapper_idmap map)
{
    // find unused signal map
    int i;

    for (i = 0; i < sig->local->idmap_len; i++) {
        if (!sig->local->idmaps[i].map)
            break;
    }
    if (i == sig->local->idmap_len) {
        // need more memory
        if (sig->local->idmap_len >= MAX_INSTANCES) {
            // Arbitrary limit to number of tracked idmaps
            return -1;
        }
        sig->local->idmap_len *= 2;
        sig->local->idmaps = realloc(sig->local->idmaps,
                                     sig->local->idmap_len *
                                     sizeof(struct _mapper_signal_idmap));
        memset(sig->local->idmaps + i, 0,
               (sig->local->idmap_len - i)
               * sizeof(struct _mapper_signal_idmap));
    }
    sig->local->idmaps[i].map = map;
    sig->local->idmaps[i].inst = si;
    sig->local->idmaps[i].status = 0;

    return i;
}

void msg_add_coerced_signal_inst_val(lo_message m, mapper_signal sig,
                                     mapper_signal_inst si, int len,
                                     const mapper_type type)
{
    int i;
    int min_len = len < sig->len ? len : sig->len;

    switch (sig->type) {
        case MAPPER_INT32: {
            int *v = (int*)si->val;
            for (i = 0; i < min_len; i++) {
                if (!si->has_val && !(si->has_val_flags[i/8] & 1 << (i % 8)))
                    lo_message_add_nil(m);
                else if (type == MAPPER_INT32)
                    lo_message_add_int32(m, v[i]);
                else if (type == MAPPER_FLOAT)
                    lo_message_add_float(m, (float)v[i]);
                else if (type == MAPPER_DOUBLE)
                    lo_message_add_double(m, (double)v[i]);
            }
            break;
        }
        case MAPPER_FLOAT: {
            float *v = (float*)si->val;
            for (i = 0; i < min_len; i++) {
                if (!si->has_val && !(si->has_val_flags[i/8] & 1 << (i % 8)))
                    lo_message_add_nil(m);
                else if (type == MAPPER_FLOAT)
                    lo_message_add_float(m, v[i]);
                else if (type == MAPPER_INT32)
                    lo_message_add_int32(m, (int)v[i]);
                else if (type == MAPPER_DOUBLE)
                    lo_message_add_double(m, (double)v[i]);
            }
            break;
        }
        case MAPPER_DOUBLE: {
            double *v = (double*)si->val;
            for (i = 0; i < min_len; i++) {
                if (!si->has_val && !(si->has_val_flags[i/8] & 1 << (i % 8)))
                    lo_message_add_nil(m);
                else if (type == MAPPER_DOUBLE)
                    lo_message_add_double(m, (int)v[i]);
                else if (type == MAPPER_INT32)
                    lo_message_add_int32(m, (int)v[i]);
                else if (type == MAPPER_FLOAT)
                    lo_message_add_float(m, (float)v[i]);
            }
            break;
        }
    }
    for (i = min_len; i < len; i++)
        lo_message_add_nil(m);
}

void mapper_signal_send_state(mapper_signal sig, network_msg_t cmd)
{
    if (!sig)
        return;
    lo_message msg = lo_message_new();
    if (!msg) {
        trace("couldn't allocate lo_message\n");
        return;
    }

    char str[1024];
    if (cmd == MSG_SIGNAL_MODIFY) {
        lo_message_add_string(msg, sig->name);

        /* properties */
        mapper_table_add_to_msg(sig->local ? sig->obj.props.synced : 0,
                                sig->obj.props.staged, msg);

        snprintf(str, 1024, "/%s/signal/modify", sig->dev->name);
        mapper_network_add_msg(&sig->obj.graph->net, str, 0, msg);
        // send immediately since path string is not cached
        mapper_network_send(&sig->obj.graph->net);
    }
    else {
        mapper_signal_full_name(sig, str, 1024);
        lo_message_add_string(msg, str);

        /* properties */
        mapper_table_add_to_msg(sig->local ? sig->obj.props.synced : 0,
                                sig->obj.props.staged, msg);

        mapper_network_add_msg(&sig->obj.graph->net, 0, cmd, msg);
    }
}

void mapper_signal_send_removed(mapper_signal sig)
{
    lo_message msg = lo_message_new();
    if (!msg) {
        trace("couldn't allocate lo_message\n");
        return;
    }
    char sig_name[1024];
    mapper_signal_full_name(sig, sig_name, 1024);
    lo_message_add_string(msg, sig_name);
    mapper_network_add_msg(&sig->obj.graph->net, 0, MSG_SIGNAL_REMOVED, msg);
}

/*! Update information about a signal record based on message properties. */
int mapper_signal_set_from_msg(mapper_signal sig, mapper_msg msg)
{
    mapper_msg_atom atom;
    int i, updated = 0, len_type_diff = 0;

    if (!msg)
        return updated;

    for (i = 0; i < msg->num_atoms; i++) {
        atom = &msg->atoms[i];
        if (sig->local && (MASK_PROP_BITFLAGS(atom->prop) != MAPPER_PROP_EXTRA))
            continue;
        switch (atom->prop) {
            case MAPPER_PROP_DIR: {
                int dir = 0;
                if (strcmp(&(*atom->vals)->s, "output")==0)
                    dir = MAPPER_DIR_OUT;
                else if (strcmp(&(*atom->vals)->s, "input")==0)
                    dir = MAPPER_DIR_IN;
                else
                    break;
                updated += mapper_table_set_record(sig->obj.props.synced,
                                                   MAPPER_PROP_DIR, NULL, 1,
                                                   MAPPER_INT32, &dir,
                                                   REMOTE_MODIFY);
                break;
            }
            case MAPPER_PROP_LENGTH:
            case MAPPER_PROP_TYPE:
                len_type_diff += mapper_table_set_record_from_atom(sig->obj.props.synced,
                                                                   atom, REMOTE_MODIFY);
                break;
            default:
                updated += mapper_table_set_record_from_atom(sig->obj.props.synced,
                                                             atom, REMOTE_MODIFY);
                break;
        }
    }
    if (len_type_diff) {
        // may need to upgrade extrema props for associated map slots
        mapper_object *maps = mapper_signal_get_maps(sig, MAPPER_DIR_ANY);
        mapper_slot slot;
        while (maps) {
            slot = mapper_map_get_slot_by_signal((mapper_map)*maps, sig);
            mapper_slot_upgrade_extrema_memory(slot);
            maps = mapper_object_list_next(maps);
        }
    }
    return updated + len_type_diff;
}
