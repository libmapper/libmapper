#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <stddef.h>

#include "mapper_internal.h"
#include "types_internal.h"
#include <mapper/mapper.h>

#define MAX_INSTANCES 128

/* TODO: MPR_DEFAULT_INST is actually a valid id - we should use
 * another method for distinguishing non-instanced updates. */
#define MPR_DEFAULT_INST -1

/* Function prototypes */
static int _add_idmap(mpr_sig s, mpr_sig_inst si, mpr_id_map map);

static int _compare_inst_ids(const void *l, const void *r)
{
    return memcmp(&(*(mpr_sig_inst*)l)->id, &(*(mpr_sig_inst*)r)->id, sizeof(mpr_id));
}

static mpr_sig_inst _find_inst_by_id(mpr_sig s, mpr_id id)
{
    RETURN_UNLESS(s->num_inst, 0);
    mpr_sig_inst_t si;
    mpr_sig_inst sip = &si;
    si.id = id;
    mpr_sig_inst *sipp = bsearch(&sip, s->loc->inst, s->num_inst,
                                 sizeof(mpr_sig_inst), _compare_inst_ids);
    return (sipp && *sipp) ? *sipp : 0;
}

// Add a signal to a parent object.
mpr_sig mpr_sig_new(mpr_dev dev, mpr_dir dir, const char *name, int len,
                    mpr_type type, const char *unit, const void *min,
                    const void *max, int *num_inst, mpr_sig_handler *h,
                    int events)
{
    // For now we only allow adding signals to devices.
    RETURN_UNLESS(dev && dev->loc, 0);
    RETURN_UNLESS(name && !check_sig_length(len) && mpr_type_get_is_num(type), 0);
    TRACE_RETURN_UNLESS(name[strlen(name)-1] != '/', 0,
                        "trailing slash detected in signal name.\n");
    TRACE_RETURN_UNLESS(dir == MPR_DIR_IN || dir == MPR_DIR_OUT, 0,
                        "signal direction must be either input or output.\n")
    mpr_graph g = dev->obj.graph;
    mpr_sig s;
    if ((s = mpr_dev_get_sig_by_name(dev, name)))
        return s;

    s = (mpr_sig)mpr_list_add_item((void**)&g->sigs, sizeof(mpr_sig_t));
    s->loc = (mpr_local_sig)calloc(1, sizeof(mpr_local_sig_t));
    s->dev = dev;
    s->obj.id = mpr_dev_get_unused_sig_id(dev);
    s->obj.graph = g;
    s->period = -1;
    s->loc->handler = h;
    s->loc->event_flags = events;
    mpr_sig_init(s, dir, name, len, type, unit, min, max, num_inst);

    if (dir == MPR_DIR_IN)
        ++dev->num_inputs;
    else
        ++dev->num_outputs;

    mpr_obj_increment_version((mpr_obj)dev);

    mpr_dev_add_sig_methods(dev, s);
    if (dev->loc->registered) {
        /* Notify subscribers */
        mpr_net_use_subscribers(&g->net, dev, ((dir == MPR_DIR_IN) ? MPR_SIG_IN : MPR_SIG_OUT));
        mpr_sig_send_state(s, MSG_SIG);
    }
    return s;
}

void mpr_sig_init(mpr_sig s, mpr_dir dir, const char *name, int len,
                  mpr_type type, const char *unit, const void *min,
                  const void *max, int *num_inst)
{
    RETURN_UNLESS(name);
    name = skip_slash(name);
    int i, str_len = strlen(name)+2;
    s->path = malloc(str_len);
    snprintf(s->path, str_len, "/%s", name);
    s->name = (char*)s->path+1;
    s->len = len;
    s->type = type;
    s->dir = dir ?: MPR_DIR_OUT;
    s->unit = unit ? strdup(unit) : strdup("unknown");
    s->min = s->max = 0;
    s->num_inst = 0;
    s->use_inst = 0;

    if (s->loc) {
        s->loc->vec_known = calloc(1, len / 8 + 1);
        for (i = 0; i < len; i++)
            set_bitflag(s->loc->vec_known, i);
        s->loc->updated_inst = 0;
        if (num_inst) {
            mpr_sig_reserve_inst(s, *num_inst, 0, 0);
            s->use_inst = 1;
        }
        else {
            mpr_sig_reserve_inst(s, 1, 0, 0);
        }

        /* Reserve one instance id map */
        s->loc->idmap_len = 1;
        s->loc->idmaps = calloc(1, sizeof(struct _mpr_sig_idmap));
    }
    else
        s->obj.props.staged = mpr_tbl_new();

    s->obj.type = MPR_SIG;
    s->obj.props.synced = mpr_tbl_new();
    s->obj.props.mask = 0;

    mpr_tbl t = s->obj.props.synced;
    int loc_mod = s->loc ? MODIFIABLE : NON_MODIFIABLE;
    int rem_mod = s->loc ? NON_MODIFIABLE : MODIFIABLE;

    // these properties need to be added in alphabetical order
    mpr_tbl_link(t, PROP(DATA), 1, MPR_PTR, &s->obj.data,
                 LOCAL_MODIFY | INDIRECT | LOCAL_ACCESS_ONLY);
    mpr_tbl_link(t, PROP(DEV), 1, MPR_DEV, &s->dev,
                 NON_MODIFIABLE | INDIRECT | LOCAL_ACCESS_ONLY);
    mpr_tbl_link(t, PROP(DIR), 1, MPR_INT32, &s->dir, rem_mod);
    mpr_tbl_link(t, PROP(ID), 1, MPR_INT64, &s->obj.id, rem_mod);
    mpr_tbl_link(t, PROP(JITTER), 1, MPR_FLT, &s->jitter, NON_MODIFIABLE);
    mpr_tbl_link(t, PROP(LEN), 1, MPR_INT32, &s->len, rem_mod);
    mpr_tbl_link(t, PROP(MAX), s->len, s->type, &s->max, MODIFIABLE | INDIRECT);
    mpr_tbl_link(t, PROP(MIN), s->len, s->type, &s->min, MODIFIABLE | INDIRECT);
    mpr_tbl_link(t, PROP(NAME), 1, MPR_STR, &s->name, NON_MODIFIABLE | INDIRECT);
    mpr_tbl_link(t, PROP(NUM_INST), 1, MPR_INT32, &s->num_inst, NON_MODIFIABLE);
    mpr_tbl_link(t, PROP(NUM_MAPS_IN), 1, MPR_INT32, &s->num_maps_in, NON_MODIFIABLE);
    mpr_tbl_link(t, PROP(NUM_MAPS_OUT), 1, MPR_INT32, &s->num_maps_out, NON_MODIFIABLE);
    mpr_tbl_link(t, PROP(PERIOD), 1, MPR_FLT, &s->period, NON_MODIFIABLE);
    mpr_tbl_link(t, PROP(STEAL_MODE), 1, MPR_INT32, &s->steal_mode, MODIFIABLE);
    mpr_tbl_link(t, PROP(TYPE), 1, MPR_TYPE, &s->type, NON_MODIFIABLE);
    mpr_tbl_link(t, PROP(UNIT), 1, MPR_STR, &s->unit, loc_mod | INDIRECT);
    mpr_tbl_link(t, PROP(USE_INST), 1, MPR_BOOL, &s->use_inst, NON_MODIFIABLE);
    mpr_tbl_link(t, PROP(VERSION), 1, MPR_INT32, &s->obj.version, NON_MODIFIABLE);

    if (min && max) {
        /* make sure in the right order */
#define TYPED_CASE(TYPE, CTYPE)                                     \
case TYPE: {                                                        \
    for (i = 0; i < len; i++) {                                     \
        if (((CTYPE*)min)[i] > ((CTYPE*)max)[i]) {                  \
            CTYPE tmp = ((CTYPE*)min)[i];                           \
            ((CTYPE*)min)[i] = ((CTYPE*)max)[i];                    \
            ((CTYPE*)max)[i] = tmp;                                 \
        }                                                           \
    }                                                               \
    break;                                                          \
}
        switch (type) {
            TYPED_CASE(MPR_INT32, int)
            TYPED_CASE(MPR_FLT, float)
            TYPED_CASE(MPR_DBL, double)
        }
    }
    if (min)
        mpr_tbl_set(t, PROP(MIN), NULL, len, type, min, LOCAL_MODIFY);
    if (max)
        mpr_tbl_set(t, PROP(MAX), NULL, len, type, max, LOCAL_MODIFY);

    mpr_tbl_set(t, PROP(IS_LOCAL), NULL, 1, MPR_BOOL, &s->loc, LOCAL_ACCESS_ONLY | NON_MODIFIABLE);
}

void mpr_sig_free(mpr_sig sig)
{
    RETURN_UNLESS(sig && sig->loc);
    int i;
    mpr_dev dev = sig->dev;

    /* release active instances */
    for (i = 0; i < sig->loc->idmap_len; i++) {
        if (sig->loc->idmaps[i].inst)
            mpr_dev_LID_decref(sig->dev, sig->loc->group, sig->loc->idmaps[i].map);
    }

    /* release associated OSC methods */
    mpr_dev_remove_sig_methods(dev, sig);
    mpr_net net = &sig->obj.graph->net;
    mpr_rtr rtr = net->rtr;
    mpr_rtr_sig rs = rtr->sigs;
    while (rs && rs->sig != sig)
        rs = rs->next;
    if (rs) {
        // need to unmap
        for (i = 0; i < rs->num_slots; i++) {
            if (!rs->slots[i])
                continue;
            mpr_map m = rs->slots[i]->map;
            mpr_map_release(m);
            mpr_rtr_remove_map(rtr, m);
        }
        mpr_rtr_remove_sig(rtr, rs);
    }
    if (dev->loc->registered) {
        /* Notify subscribers */
        int dir = (sig->dir == MPR_DIR_IN) ? MPR_SIG_IN : MPR_SIG_OUT;
        mpr_net_use_subscribers(net, dev, dir);
        mpr_sig_send_removed(sig);
    }
    free(sig->loc->updated_inst);
    mpr_graph_remove_sig(sig->obj.graph, sig, MPR_OBJ_REM);
    mpr_obj_increment_version((mpr_obj)dev);
}

void mpr_sig_free_internal(mpr_sig s)
{
    RETURN_UNLESS(s);
    int i;
    if (s->loc) {
        /* Free instances */
        for (i = 0; i < s->loc->idmap_len; i++) {
            if (s->loc->idmaps[i].inst)
                mpr_sig_release_inst_internal(s, i);
        }
        free(s->loc->idmaps);
        for (i = 0; i < s->num_inst; i++) {
            FUNC_IF(free, s->loc->inst[i]->val);
            FUNC_IF(free, s->loc->inst[i]->has_val_flags);
            free(s->loc->inst[i]);
        }
        free(s->loc->inst);
        FUNC_IF(free, s->loc->vec_known);
        free(s->loc);
    }

    FUNC_IF(mpr_tbl_free, s->obj.props.synced);
    FUNC_IF(mpr_tbl_free, s->obj.props.staged);
    FUNC_IF(free, s->max);
    FUNC_IF(free, s->min);
    FUNC_IF(free, s->path);
    FUNC_IF(free, s->unit);
}

void mpr_sig_call_handler(mpr_sig sig, int evt, mpr_id inst, int len,
                          const void *val, mpr_time *time, float diff)
{
    // abort if signal is already being processed - might be a local loop
    if (sig->loc->locked) {
        trace_dev(sig->dev, "Mapping loop detected on signal %s! (2)\n", sig->name);
        return;
    }
    /* non-instanced signals cannot have a null value */
    if (!val && !sig->use_inst)
        return;
    mpr_sig_update_timing_stats(sig, diff);
    mpr_sig_handler *h = sig->loc->handler;
    if (h && (evt & sig->loc->event_flags))
        h(sig, evt, sig->use_inst ? inst : 0, len, sig->type, val, *time);
}

/**** Instances ****/

static void _init_inst(mpr_sig_inst si)
{
    si->has_val = 0;
    mpr_time_set(&si->created, MPR_NOW);
    mpr_time_set(&si->time, si->created);
}

static mpr_sig_inst _reserved_inst(mpr_sig s, mpr_id *id)
{
    int i;
    for (i = 0; i < s->num_inst; i++) {
        if (!s->loc->inst[i]->active) {
            if (id)
                s->loc->inst[i]->id = *id;
            qsort(s->loc->inst, s->num_inst, sizeof(mpr_sig_inst), _compare_inst_ids);
            return s->loc->inst[i];
        }
    }
    return 0;
}

int _oldest_inst(mpr_sig sig)
{
    int i;
    mpr_sig_inst si;
    for (i = 0; i < sig->loc->idmap_len; i++) {
        if (sig->loc->idmaps[i].inst)
            break;
    }
    if (i == sig->loc->idmap_len) {
        /* no active instances to steal! */
        return -1;
    }
    int oldest = i;
    for (i = oldest+1; i < sig->loc->idmap_len; i++) {
        if (!(si = sig->loc->idmaps[i].inst))
            continue;
        if ((si->created.sec < sig->loc->idmaps[oldest].inst->created.sec) ||
            (si->created.sec == sig->loc->idmaps[oldest].inst->created.sec &&
             si->created.frac < sig->loc->idmaps[oldest].inst->created.frac))
            oldest = i;
    }
    return oldest;
}

mpr_id mpr_sig_get_oldest_inst_id(mpr_sig sig)
{
    RETURN_UNLESS(sig && sig->loc && sig->use_inst, 0);
    int idx = _oldest_inst(sig);
    return (idx >= 0) ? sig->loc->idmaps[idx].map->LID : 0;
}

int _newest_inst(mpr_sig sig)
{
    int i;
    mpr_sig_inst si;
    for (i = 0; i < sig->loc->idmap_len; i++) {
        if (sig->loc->idmaps[i].inst)
            break;
    }
    if (i == sig->loc->idmap_len) {
        /* no active instances to steal! */
        return -1;
    }
    int newest = i;
    for (i = newest+1; i < sig->loc->idmap_len; i++) {
        if (!(si = sig->loc->idmaps[i].inst))
            continue;
        if ((si->created.sec > sig->loc->idmaps[newest].inst->created.sec) ||
            (si->created.sec == sig->loc->idmaps[newest].inst->created.sec &&
             si->created.frac > sig->loc->idmaps[newest].inst->created.frac))
            newest = i;
    }
    return newest;
}

mpr_id mpr_sig_get_newest_inst_id(mpr_sig sig)
{
    RETURN_UNLESS(sig && sig->loc && sig->use_inst, 0);
    int idx = _newest_inst(sig);
    return (idx >= 0) ? sig->loc->idmaps[idx].map->LID : 0;
}

int mpr_sig_get_idmap_with_LID(mpr_sig s, mpr_id LID, int flags, mpr_time t, int activate)
{
    RETURN_UNLESS(s && s->loc, -1);
    if (!s->use_inst)
        LID = MPR_DEFAULT_INST;
    mpr_sig_idmap_t *maps = s->loc->idmaps;
    mpr_sig_handler *h = s->loc->handler;
    mpr_sig_inst si;
    int i;
    for (i = 0; i < s->loc->idmap_len; i++) {
        if (maps[i].inst && maps[i].map->LID == LID)
            return (maps[i].status & ~flags) ? -1 : i;
    }
    RETURN_UNLESS(activate, -1);

    // check if device has record of id map
    mpr_id_map map = mpr_dev_get_idmap_by_LID(s->dev, s->loc->group, LID);

    /* No instance with that id exists - need to try to activate instance and
     * create new id map if necessary. */
    if ((si = _find_inst_by_id(s, LID)) || (si = _reserved_inst(s, &LID))) {
        if (!map) {
            /* Claim id map locally */
            map = mpr_dev_add_idmap(s->dev, s->loc->group, LID, 0);
        }
        else
            mpr_dev_LID_incref(s->dev, map);

        /* store pointer to device map in a new signal map */
        si->active = 1;
        _init_inst(si);
        i = _add_idmap(s, si, map);
        if (h && (s->loc->event_flags & MPR_SIG_INST_NEW))
            h(s, MPR_SIG_INST_NEW, LID, 0, s->type, NULL, t);
        return i;
    }

    RETURN_UNLESS(h, -1);
    if (s->loc->event_flags & MPR_SIG_INST_OFLW) {
        /* call instance event handler */
        h(s, MPR_SIG_INST_OFLW, 0, 0, s->type, NULL, t);
    }
    else if (s->steal_mode == MPR_STEAL_OLDEST) {
        i = _oldest_inst(s);
        if (i < 0)
            return -1;
        int evt = (MPR_SIG_REL_UPSTRM & s->loc->event_flags ? MPR_SIG_REL_UPSTRM : MPR_SIG_UPDATE);
        h(s, evt, s->loc->idmaps[i].map->LID, 0, s->type, 0, t);
    }
    else if (s->steal_mode == MPR_STEAL_NEWEST) {
        i = _newest_inst(s);
        if (i < 0)
            return -1;
        int evt = (MPR_SIG_REL_UPSTRM & s->loc->event_flags ? MPR_SIG_REL_UPSTRM : MPR_SIG_UPDATE);
        h(s, evt, s->loc->idmaps[i].map->LID, 0, s->type, 0, t);
    }
    else
        return -1;

    /* try again */
    if ((si = _find_inst_by_id(s, LID)) || (si = _reserved_inst(s, &LID))) {
        if (!map) {
            /* Claim id map locally */
            map = mpr_dev_add_idmap(s->dev, s->loc->group, LID, 0);
        }
        else
            mpr_dev_LID_incref(s->dev, map);
        si->active = 1;
        _init_inst(si);
        i = _add_idmap(s, si, map);
        if (h && (s->loc->event_flags & MPR_SIG_INST_NEW))
            h(s, MPR_SIG_INST_NEW, LID, 0, s->type, NULL, t);
        return i;
    }
    return -1;
}

int mpr_sig_get_idmap_with_GID(mpr_sig s, mpr_id GID, int flags, mpr_time t, int activate)
{
    RETURN_UNLESS(s && s->loc, -1);
    mpr_sig_idmap_t *maps = s->loc->idmaps;
    mpr_sig_handler *h = s->loc->handler;
    mpr_sig_inst si;
    int i;
    for (i = 0; i < s->loc->idmap_len; i++) {
        if (maps[i].map && maps[i].map->GID == GID)
            return (maps[i].status & ~flags) ? -1 : i;
    }
    RETURN_UNLESS(activate, -1);

    // check if the device already has a map for this global id
    mpr_id_map map = mpr_dev_get_idmap_by_GID(s->dev, s->loc->group, GID);
    if (!map) {
        /* Here we still risk creating conflicting maps if two signals are
         * updated asynchronously.  This is easy to avoid by not allowing a
         * local id to be used with multiple active remote maps, however users
         * may wish to create devices with multiple object classes which do not
         * require mutual instance id synchronization - e.g. instance 1 of
         * object class A is not related to instance 1 of object B. */
        if ((si = _reserved_inst(s, NULL))) {
            map = mpr_dev_add_idmap(s->dev, s->loc->group, si->id, GID);
            map->GID_refcount = 1;
            si->active = 1;
            _init_inst(si);
            i = _add_idmap(s, si, map);
            if (h && (s->loc->event_flags & MPR_SIG_INST_NEW))
                h(s, MPR_SIG_INST_NEW, si->id, 0, s->type, NULL, t);
            return i;
        }
    }
    else if ((si = _find_inst_by_id(s, map->LID)) || (si = _reserved_inst(s, &map->LID))) {
        if (!si->active) {
            si->active = 1;
            _init_inst(si);
            i = _add_idmap(s, si, map);
            mpr_dev_LID_incref(s->dev, map);
            mpr_dev_GID_incref(s->dev, map);
            if (h && (s->loc->event_flags & MPR_SIG_INST_NEW))
                h(s, MPR_SIG_INST_NEW, si->id, 0, s->type, NULL, t);
            return i;
        }
    }
    else {
        /* TODO: Once signal groups are explicit, allow re-mapping to
         * another instance if possible. */
        trace("Signal %s has no instance %"PR_MPR_ID" available.\n", s->name, map->LID);
        return -1;
    }

    RETURN_UNLESS(h, -1);

    /* try releasing instance in use */
    if (s->loc->event_flags & MPR_SIG_INST_OFLW) {
        /* call instance event handler */
        h(s, MPR_SIG_INST_OFLW, 0, 0, s->type, NULL, t);
    }
    else if (s->steal_mode == MPR_STEAL_OLDEST) {
        i = _oldest_inst(s);
        if (i < 0)
            return -1;
        int evt = (MPR_SIG_REL_UPSTRM & s->loc->event_flags ? MPR_SIG_REL_UPSTRM : MPR_SIG_UPDATE);
        h(s, evt, s->loc->idmaps[i].map->LID, 0, s->type, 0, t);
    }
    else if (s->steal_mode == MPR_STEAL_NEWEST) {
        i = _newest_inst(s);
        if (i < 0)
            return -1;
        int evt = (MPR_SIG_REL_UPSTRM & s->loc->event_flags ? MPR_SIG_REL_UPSTRM : MPR_SIG_UPDATE);
        h(s, evt, s->loc->idmaps[i].map->LID, 0, s->type, 0, t);
    }
    else
        return -1;

    /* try again */
    if (!map) {
        if ((si = _reserved_inst(s, NULL))) {
            map = mpr_dev_add_idmap(s->dev, s->loc->group, si->id, GID);
            map->GID_refcount = 1;
            si->active = 1;
            _init_inst(si);
            i = _add_idmap(s, si, map);
            if (h && (s->loc->event_flags & MPR_SIG_INST_NEW))
                h(s, MPR_SIG_INST_NEW, si->id, 0, s->type, NULL, t);
            return i;
        }
    }
    else {
        si = _find_inst_by_id(s, map->LID);
        TRACE_RETURN_UNLESS(si && !si->active, -1, "Signal %s has no instance %"
                            PR_MPR_ID" available.", s->name, map->LID);
        si->active = 1;
        _init_inst(si);
        i = _add_idmap(s, si, map);
        mpr_dev_LID_incref(s->dev, map);
        mpr_dev_GID_incref(s->dev, map);
        if (h && (s->loc->event_flags & MPR_SIG_INST_NEW))
            h(s, MPR_SIG_INST_NEW, si->id, 0, s->type, NULL, t);
        return i;
    }
    return -1;
}

static int _reserve_inst(mpr_sig sig, mpr_id *id, void *data)
{
    RETURN_UNLESS(sig->num_inst < MAX_INSTANCES, -1);
    int i, cont;
    mpr_sig_inst si;

    /* check if instance with this id already exists! If so, stop here. */
    if (id && _find_inst_by_id(sig, *id))
        return -1;

    /* reallocate array of instances */
    sig->loc->inst = realloc(sig->loc->inst, sizeof(mpr_sig_inst) * (sig->num_inst+1));
    sig->loc->inst[sig->num_inst] = (mpr_sig_inst) calloc(1, sizeof(struct _mpr_sig_inst));
    si = sig->loc->inst[sig->num_inst];
    si->val = calloc(1, mpr_sig_get_vector_bytes(sig));
    si->has_val_flags = calloc(1, sig->len / 8 + 1);
    si->has_val = 0;

    if (id)
        si->id = *id;
    else {
        /* find lowest unused id */
        mpr_id lowest_id = 0;
        cont = 1;
        while (cont) {
            cont = 0;
            for (i = 0; i < sig->num_inst; i++) {
                if (sig->loc->inst[i]->id == lowest_id) {
                    cont = 1;
                    break;
                }
            }
            lowest_id += cont;
        }
        si->id = lowest_id;
    }
    si->idx = sig->num_inst;
    _init_inst(si);
    si->data = data;

    if (++sig->num_inst > 1) {
        if (!sig->use_inst) {
            /* TODO: modify associated maps for instanced signals */
        }
        sig->use_inst = 1;
    }
    qsort(sig->loc->inst, sig->num_inst, sizeof(mpr_sig_inst), _compare_inst_ids);
    return sig->num_inst-1;;
}

int mpr_sig_reserve_inst(mpr_sig sig, int num, mpr_id *ids, void **data)
{
    RETURN_UNLESS(sig && sig->loc && num, 0);
    int i = 0, count = 0, highest = -1, result, old_num = sig->num_inst;
    if (sig->num_inst == 1 && !sig->loc->inst[0]->id && !sig->loc->inst[0]->data) {
        /* we will overwite the default instance first */
        if (ids)
            sig->loc->inst[0]->id = ids[0];
        if (data)
            sig->loc->inst[0]->data = data[0];
        ++i;
        ++count;
    }
    for (; i < num; i++) {
        result = _reserve_inst(sig, ids ? &ids[i] : 0, data ? data[i] : 0);
        if (result == -1)
            continue;
        highest = result;
        ++count;
    }
    if (highest != -1)
        mpr_rtr_num_inst_changed(sig->obj.graph->net.rtr, sig, highest + 1);

    if (old_num > 0 && (sig->num_inst / 8) == (old_num / 8))
        return count;

    // reallocate instance update bitflags
    char *updated_inst = calloc(1, sig->num_inst / 8 + 1);
    if (sig->loc->updated_inst) {
        memcpy(updated_inst, sig->loc->updated_inst, old_num / 8 + 1);
        free(sig->loc->updated_inst);
    }
    sig->loc->updated_inst = updated_inst;
    return count;
}

int mpr_sig_get_inst_is_active(mpr_sig sig, mpr_id id)
{
    RETURN_UNLESS(sig, 0);
    RETURN_UNLESS(sig->use_inst, 1);
    int idmap_idx = mpr_sig_get_idmap_with_LID(sig, id, 0, MPR_NOW, 0);
    return (idmap_idx >= 0) ? sig->loc->idmaps[idmap_idx].inst->active : 0;
}

void mpr_sig_update_timing_stats(mpr_sig sig, float diff)
{
    /* make sure time is monotonic */
    if (diff < 0)
        diff = 0;
    if (-1 == sig->period)
        sig->period = 0;
    else if (0 == sig->period)
        sig->period = diff;
    else {
        sig->jitter *= 0.99;
        sig->jitter += (0.01 * fabsf(sig->period - diff));
        sig->period *= 0.99;
        sig->period += (0.01 * diff);
    }
}

void mpr_sig_set_value(mpr_sig sig, mpr_id id, int len, mpr_type type, const void *val)
{
    RETURN_UNLESS(sig && sig->loc);
    if (!len || !val) {
        mpr_sig_release_inst(sig, id);
        return;
    }
    if (!mpr_type_get_is_num(type)) {
#ifdef DEBUG
        trace("called update on signal '%s' with non-number type '%c'\n", sig->name, type);
#endif
        return;
    }
    if (len && (len != sig->len)) {
#ifdef DEBUG
        trace("called update on signal '%s' with value length %d (should be %d)\n",
              sig->name, len, sig->len);
#endif
        return;
    }
    if (type != MPR_INT32) {
        /* check for NaN */
        int i;
        if (type == MPR_FLT) {
            for (i = 0; i < len; i++)
                RETURN_UNLESS(((float*)val)[i] == ((float*)val)[i]);
        }
        else if (type == MPR_DBL) {
            for (i = 0; i < len; i++)
                RETURN_UNLESS(((double*)val)[i] == ((double*)val)[i]);
        }
    }
    mpr_time time = mpr_dev_get_time(sig->dev);
    int idmap_idx = mpr_sig_get_idmap_with_LID(sig, id, 0, time, 1);
    RETURN_UNLESS(idmap_idx >= 0);
    mpr_sig_inst si = sig->loc->idmaps[idmap_idx].inst;

    // update time
    double diff = si->has_val ? mpr_time_get_diff(time, si->time) : 0;
    mpr_sig_update_timing_stats(sig, diff);
    memcpy(&si->time, &time, sizeof(mpr_time));

    /* update value */
    if (type != sig->type)
        set_coerced_val(sig->len, type, val, sig->len, sig->type, si->val);
    else
        memcpy(si->val, (void*)val, mpr_sig_get_vector_bytes(sig));
    si->has_val = 1;

    /* mark instance as updated */
    set_bitflag(sig->loc->updated_inst, si->idx);
    sig->dev->loc->updated = sig->loc->updated = 1;

    mpr_rtr rtr = sig->obj.graph->net.rtr;
    mpr_rtr_process_sig(rtr, sig, idmap_idx, si->has_val ? si->val : 0, si->time);
}

void mpr_sig_release_inst(mpr_sig sig, mpr_id id)
{
    RETURN_UNLESS(sig && sig->loc && sig->use_inst);
    int idmap_idx = mpr_sig_get_idmap_with_LID(sig, id, RELEASED_REMOTELY, MPR_NOW, 0);
    if (idmap_idx >= 0)
        mpr_sig_release_inst_internal(sig, idmap_idx);
}

void mpr_sig_release_inst_internal(mpr_sig sig, int idmap_idx)
{
    mpr_sig_idmap_t *smap = &sig->loc->idmaps[idmap_idx];
    RETURN_UNLESS(smap->inst);

    /* mark instance as updated */
    set_bitflag(sig->loc->updated_inst, smap->inst->idx);
    sig->dev->loc->updated = sig->loc->updated = 1;

    mpr_rtr rtr = sig->obj.graph->net.rtr;
    mpr_rtr_process_sig(rtr, sig, idmap_idx, 0, smap->inst->time);

    if (mpr_dev_LID_decref(sig->dev, sig->loc->group, smap->map))
        smap->map = 0;
    else if ((sig->dir & MPR_DIR_OUT) || smap->status & RELEASED_REMOTELY) {
        /* TODO: consider multiple upstream source instances? */
        smap->map = 0;
    }
    else {
        /* mark map as locally-released but do not remove it */
        smap->status |= RELEASED_LOCALLY;
    }

    /* Put instance back in reserve list */
    smap->inst->active = 0;
    smap->inst = 0;
}

void mpr_sig_remove_inst(mpr_sig sig, mpr_id id)
{
    RETURN_UNLESS(sig && sig->loc && sig->use_inst);

    int i, remove_idx;
    for (i = 0; i < sig->num_inst; i++) {
        if (sig->loc->inst[i]->id == id)
            break;
    }
    RETURN_UNLESS(i < sig->num_inst);

    if (sig->loc->inst[i]->active) {
       /* First release instance */
       mpr_sig_release_inst_internal(sig, i);
    }

    remove_idx = sig->loc->inst[i]->idx;

    /* Free value and timetag memory held by instance */
    FUNC_IF(free, sig->loc->inst[i]->val);
    FUNC_IF(free, sig->loc->inst[i]->has_val_flags);
    free(sig->loc->inst[i]);

    for (++i; i < sig->num_inst; i++)
        sig->loc->inst[i-1] = sig->loc->inst[i];
    --sig->num_inst;
    sig->loc->inst = realloc(sig->loc->inst, sizeof(mpr_sig_inst) * sig->num_inst);

    /* Remove instance memory held by map slots */
    mpr_rtr_remove_inst(sig->obj.graph->net.rtr, sig, remove_idx);

    for (i = 0; i < sig->num_inst; i++) {
        if (sig->loc->inst[i]->idx > remove_idx)
            --sig->loc->inst[i]->idx;
    }
}

const void *mpr_sig_get_value(mpr_sig sig, mpr_id id, mpr_time *time)
{
    RETURN_UNLESS(sig && sig->loc, 0);
    mpr_sig_inst si;
    if (!sig->use_inst)
        si = sig->loc->idmaps[0].inst;
    else {
        int idmap_idx = mpr_sig_get_idmap_with_LID(sig, id, RELEASED_REMOTELY, MPR_NOW, 0);
        RETURN_UNLESS(idmap_idx >= 0, 0);
        si = sig->loc->idmaps[idmap_idx].inst;
    }
    RETURN_UNLESS(si && si->has_val, 0)
    if (time) {
        time->sec = si->time.sec;
        time->frac = si->time.frac;
    }
    mpr_time now;
    mpr_time_set(&now, MPR_NOW);
    double diff = mpr_time_get_diff(now, si->time);
    mpr_sig_update_timing_stats(sig, diff);
    return si->val;
}

int mpr_sig_get_num_inst(mpr_sig sig, mpr_status status)
{
    RETURN_UNLESS(sig && sig->loc, 0);
    RETURN_UNLESS(sig->use_inst, 1);
    int i, j = 0, both = MPR_STATUS_ACTIVE | MPR_STATUS_RESERVED;
    if ((status & both) == both)
        return sig->num_inst;
    status = status & MPR_STATUS_ACTIVE ? 1 : 0;
    for (i = 0; i < sig->num_inst; i++) {
        if (sig->loc->inst[i]->active == status)
            ++j;
    }
    return j;
}

mpr_id mpr_sig_get_inst_id(mpr_sig sig, int idx, mpr_status status)
{
    RETURN_UNLESS(sig && sig->loc, 0);
    RETURN_UNLESS(sig->use_inst, 0);
    RETURN_UNLESS(idx >= 0 && idx < sig->num_inst, 0);
    int i, j = -1, both = MPR_STATUS_ACTIVE | MPR_STATUS_RESERVED;
    if ((status & both) == both)
        return sig->loc->inst[idx]->id;
    status = status & MPR_STATUS_ACTIVE ? 1 : 0;
    for (i = 0; i < sig->num_inst; i++) {
        if (sig->loc->inst[i]->active != status)
            continue;
        if (++j == idx)
            return sig->loc->inst[i]->id;
    }
    return 0;
}

int mpr_sig_activate_inst(mpr_sig sig, mpr_id id)
{
    RETURN_UNLESS(sig && sig->loc && sig->use_inst, 0);
    mpr_time time = mpr_dev_get_time(sig->dev);
    int idmap_idx = mpr_sig_get_idmap_with_LID(sig, id, 0, time, 1);
    return idmap_idx >= 0;
}

void mpr_sig_set_inst_data(mpr_sig sig, mpr_id id, const void *data)
{
    RETURN_UNLESS(sig && sig->loc && sig->use_inst);
    mpr_sig_inst si = _find_inst_by_id(sig, id);
    if (si)
        si->data = (void*)data;
}

void *mpr_sig_get_inst_data(mpr_sig sig, mpr_id id)
{
    RETURN_UNLESS(sig && sig->loc && sig->use_inst, 0);
    mpr_sig_inst si = _find_inst_by_id(sig, id);
    return si ? si->data : 0;
}

/**** Queries ****/

void mpr_sig_set_cb(mpr_sig s, mpr_sig_handler *h, int events)
{
    RETURN_UNLESS(s && s->loc);
    if (!s->loc->handler && h && events) {
        /* Need to register a new liblo methods */
        mpr_dev_add_sig_methods(s->dev, s);
    }
    else if (s->loc->handler && !(h || events)) {
        /* Need to remove liblo methods */
        mpr_dev_remove_sig_methods(s->dev, s);
    }
    s->loc->handler = h;
    s->loc->event_flags = events;
}

/**** Signal Properties ****/

/* Internal function only */
int mpr_sig_full_name(mpr_sig s, char *name, int len)
{
    const char *dev_name = mpr_dev_get_name(s->dev);
    RETURN_UNLESS(dev_name, 0);

    int dev_name_len = strlen(dev_name);
    if (dev_name_len >= len)
        return 0;
    if ((dev_name_len + strlen(s->name) + 1) > len)
        return 0;

    snprintf(name, len, "%s%s", dev_name, s->path);
    return strlen(name);
}

mpr_dev mpr_sig_get_dev(mpr_sig s)
{
    return s->dev;
}

static int cmp_qry_sig_maps(const void *context_data, mpr_map map)
{
    mpr_sig s = *(mpr_sig*)context_data;
    int dir = *(int*)(context_data + sizeof(mpr_sig*));
    if (!dir || (dir & MPR_DIR_OUT)) {
        int i;
        for (i = 0; i < map->num_src; i++) {
            if (map->src[i]->sig == s)
                return 1;
        }
    }
    if (!dir || (dir & MPR_DIR_IN)) {
        if (map->dst->sig == s)
            return 1;
    }
    return 0;
}

mpr_list mpr_sig_get_maps(mpr_sig s, mpr_dir dir)
{
    RETURN_UNLESS(s && s->obj.graph->maps, 0);
    mpr_list q = mpr_list_new_query((const void**)&s->obj.graph->maps,
                                    cmp_qry_sig_maps, "vi", &s, dir);
    return mpr_list_start(q);
}

static int _add_idmap(mpr_sig s, mpr_sig_inst si, mpr_id_map map)
{
    /* find unused signal map */
    int i;
    for (i = 0; i < s->loc->idmap_len; i++) {
        if (!s->loc->idmaps[i].map)
            break;
    }
    if (i == s->loc->idmap_len) {
        /* need more memory */
        if (s->loc->idmap_len >= MAX_INSTANCES) {
            /* Arbitrary limit to number of tracked idmaps */
            /* TODO: add checks for this return value */
            return -1;
        }
        s->loc->idmap_len = s->loc->idmap_len ? s->loc->idmap_len * 2 : 1;
        s->loc->idmaps = realloc(s->loc->idmaps, (s->loc->idmap_len *
                                                  sizeof(struct _mpr_sig_idmap)));
        memset(s->loc->idmaps + i, 0, ((s->loc->idmap_len - i) * sizeof(struct _mpr_sig_idmap)));
    }
    s->loc->idmaps[i].map = map;
    s->loc->idmaps[i].inst = si;
    s->loc->idmaps[i].status = 0;
    return i;
}

void mpr_sig_send_state(mpr_sig s, net_msg_t cmd)
{
    RETURN_UNLESS(s);
    NEW_LO_MSG(msg, return);

    char str[1024];
    if (cmd == MSG_SIG_MOD) {
        lo_message_add_string(msg, s->name);

        /* properties */
        mpr_tbl_add_to_msg(s->loc ? s->obj.props.synced : 0, s->obj.props.staged, msg);

        snprintf(str, 1024, "/%s/signal/modify", s->dev->name);
        mpr_net_add_msg(&s->obj.graph->net, str, 0, msg);
        /* send immediately since path string is not cached */
        mpr_net_send(&s->obj.graph->net);
    }
    else {
        mpr_sig_full_name(s, str, 1024);
        lo_message_add_string(msg, str);

        /* properties */
        mpr_tbl_add_to_msg(s->loc ? s->obj.props.synced : 0, s->obj.props.staged, msg);

        mpr_net_add_msg(&s->obj.graph->net, 0, cmd, msg);
    }
}

void mpr_sig_send_removed(mpr_sig s)
{
    NEW_LO_MSG(msg, return);
    char sig_name[1024];
    mpr_sig_full_name(s, sig_name, 1024);
    lo_message_add_string(msg, sig_name);
    mpr_net_add_msg(&s->obj.graph->net, 0, MSG_SIG_REM, msg);
}

/*! Update information about a signal record based on message properties. */
int mpr_sig_set_from_msg(mpr_sig s, mpr_msg msg)
{
    RETURN_UNLESS(msg, 0);
    mpr_tbl tbl = s->obj.props.synced;
    mpr_msg_atom a;
    int i, updated = 0;
    for (i = 0; i < msg->num_atoms; i++) {
        a = &msg->atoms[i];
        if (s->loc && (MASK_PROP_BITFLAGS(a->prop) != PROP(EXTRA)))
            continue;
        switch (a->prop) {
            case PROP(DIR): {
                int dir = 0;
                if (strcmp(&(*a->vals)->s, "output")==0)
                    dir = MPR_DIR_OUT;
                else if (strcmp(&(*a->vals)->s, "input")==0)
                    dir = MPR_DIR_IN;
                else
                    break;
                updated += mpr_tbl_set(tbl, PROP(DIR), NULL, 1, MPR_INT32, &dir, REMOTE_MODIFY);
                break;
            }
            case PROP(ID):
                if (a->types[0] == 'h') {
                    if (s->obj.id != (a->vals[0])->i64) {
                        s->obj.id = (a->vals[0])->i64;
                        ++updated;
                    }
                }
                break;
            case PROP(STEAL_MODE): {
                int stl;
                if (strcmp(&(*a->vals)->s, "none")==0)
                    stl = MPR_STEAL_NONE;
                else if (strcmp(&(*a->vals)->s, "oldest")==0)
                    stl = MPR_STEAL_OLDEST;
                else if (strcmp(&(*a->vals)->s, "newest")==0)
                    stl = MPR_STEAL_NEWEST;
                else
                    break;
                updated += mpr_tbl_set(tbl, PROP(STEAL_MODE), NULL, 1,
                                       MPR_INT32, &stl, REMOTE_MODIFY);
                break;
            }
            default:
                updated += mpr_tbl_set_from_atom(tbl, a, REMOTE_MODIFY);
                break;
        }
    }
    return updated;
}
