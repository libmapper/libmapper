#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <stddef.h>

#include "mapper_internal.h"
#include "types_internal.h"
#include <mapper/mapper.h>

#define MAX_INSTANCES 128
#define BUFFSIZE 512

/* TODO: MPR_DEFAULT_INST is actually a valid id - we should use
 * another method for distinguishing non-instanced updates. */
#define MPR_DEFAULT_INST -1

/* Function prototypes */
static int _add_idmap(mpr_local_sig lsig, mpr_sig_inst si, mpr_id_map map);

static int _compare_inst_ids(const void *l, const void *r)
{
    return memcmp(&(*(mpr_sig_inst*)l)->id, &(*(mpr_sig_inst*)r)->id, sizeof(mpr_id));
}

static mpr_sig_inst _find_inst_by_id(mpr_local_sig lsig, mpr_id id)
{
    mpr_sig_inst_t si, *sip, **sipp;
    RETURN_ARG_UNLESS(lsig->num_inst, 0);
    sip = &si;
    si.id = id;
    sipp = bsearch(&sip, lsig->inst, lsig->num_inst, sizeof(mpr_sig_inst), _compare_inst_ids);
    return (sipp && *sipp) ? *sipp : 0;
}

/* Add a signal to a parent object. */
mpr_sig mpr_sig_new(mpr_dev dev, mpr_dir dir, const char *name, int len,
                    mpr_type type, const char *unit, const void *min,
                    const void *max, int *num_inst, mpr_sig_handler *h,
                    int events)
{
    mpr_graph g;
    mpr_local_sig lsig;

    /* For now we only allow adding signals to devices. */
    RETURN_ARG_UNLESS(dev && dev->is_local, 0);
    RETURN_ARG_UNLESS(name && !check_sig_length(len) && mpr_type_get_is_num(type), 0);
    TRACE_RETURN_UNLESS(name[strlen(name)-1] != '/', 0,
                        "trailing slash detected in signal name.\n");
    TRACE_RETURN_UNLESS(dir == MPR_DIR_IN || dir == MPR_DIR_OUT, 0,
                        "signal direction must be either input or output.\n")
    g = dev->obj.graph;
    if ((lsig = (mpr_local_sig)mpr_dev_get_sig_by_name(dev, name)))
        return (mpr_sig)lsig;

    lsig = (mpr_local_sig)mpr_list_add_item((void**)&g->sigs, sizeof(mpr_local_sig_t));
    lsig->dev = (mpr_local_dev)dev;
    lsig->obj.id = mpr_dev_get_unused_sig_id((mpr_local_dev)dev);
    lsig->obj.graph = g;
    lsig->period = -1;
    lsig->handler = (void*)h;
    lsig->event_flags = events;
    lsig->is_local = 1;
    mpr_sig_init((mpr_sig)lsig, dir, name, len, type, unit, min, max, num_inst);

    if (dir == MPR_DIR_IN)
        ++dev->num_inputs;
    else
        ++dev->num_outputs;

    mpr_obj_increment_version((mpr_obj)dev);

    mpr_dev_add_sig_methods((mpr_local_dev)dev, lsig);
    if (((mpr_local_dev)dev)->registered) {
        /* Notify subscribers */
        mpr_net_use_subscribers(&g->net, (mpr_local_dev)dev,
                                ((dir == MPR_DIR_IN) ? MPR_SIG_IN : MPR_SIG_OUT));
        mpr_sig_send_state((mpr_sig)lsig, MSG_SIG);
    }
    return (mpr_sig)lsig;
}

void mpr_sig_init(mpr_sig sig, mpr_dir dir, const char *name, int len, mpr_type type,
                  const char *unit, const void *min, const void *max, int *num_inst)
{
    int i, str_len, loc_mod, rem_mod;
    mpr_tbl tbl;
    RETURN_UNLESS(name);

    name = skip_slash(name);
    str_len = strlen(name)+2;
    sig->path = malloc(str_len);
    snprintf(sig->path, str_len, "/%s", name);
    sig->name = (char*)sig->path+1;
    sig->len = len;
    sig->type = type;
    sig->dir = dir ? dir : MPR_DIR_OUT;
    sig->unit = unit ? strdup(unit) : strdup("unknown");
    sig->min = sig->max = 0;
    sig->num_inst = 0;
    sig->use_inst = 0;

    if (sig->is_local) {
        mpr_local_sig lsig = (mpr_local_sig)sig;
        lsig->vec_known = calloc(1, len / 8 + 1);
        for (i = 0; i < len; i++)
            set_bitflag(lsig->vec_known, i);
        lsig->updated_inst = 0;
        if (num_inst) {
            mpr_sig_reserve_inst((mpr_sig)lsig, *num_inst, 0, 0);
            lsig->use_inst = 1;
        }
        else {
            mpr_sig_reserve_inst((mpr_sig)lsig, 1, 0, 0);
        }

        /* Reserve one instance id map */
        lsig->idmap_len = 1;
        lsig->idmaps = calloc(1, sizeof(struct _mpr_sig_idmap));
    }
    else
        sig->obj.props.staged = mpr_tbl_new();

    sig->obj.type = MPR_SIG;
    sig->obj.props.synced = mpr_tbl_new();

    tbl = sig->obj.props.synced;
    loc_mod = sig->is_local ? MODIFIABLE : NON_MODIFIABLE;
    rem_mod = sig->is_local ? NON_MODIFIABLE : MODIFIABLE;

    /* these properties need to be added in alphabetical order */
    mpr_tbl_link(tbl, PROP(DATA), 1, MPR_PTR, &sig->obj.data,
                 LOCAL_MODIFY | INDIRECT | LOCAL_ACCESS_ONLY);
    mpr_tbl_link(tbl, PROP(DEV), 1, MPR_DEV, &sig->dev,
                 NON_MODIFIABLE | INDIRECT | LOCAL_ACCESS_ONLY);
    mpr_tbl_link(tbl, PROP(DIR), 1, MPR_INT32, &sig->dir, MODIFIABLE);
    mpr_tbl_link(tbl, PROP(ID), 1, MPR_INT64, &sig->obj.id, rem_mod);
    mpr_tbl_link(tbl, PROP(JITTER), 1, MPR_FLT, &sig->jitter, NON_MODIFIABLE);
    mpr_tbl_link(tbl, PROP(LEN), 1, MPR_INT32, &sig->len, rem_mod);
    mpr_tbl_link(tbl, PROP(MAX), sig->len, sig->type, &sig->max, MODIFIABLE | INDIRECT);
    mpr_tbl_link(tbl, PROP(MIN), sig->len, sig->type, &sig->min, MODIFIABLE | INDIRECT);
    mpr_tbl_link(tbl, PROP(NAME), 1, MPR_STR, &sig->name, NON_MODIFIABLE | INDIRECT);
    mpr_tbl_link(tbl, PROP(NUM_INST), 1, MPR_INT32, &sig->num_inst, NON_MODIFIABLE);
    mpr_tbl_link(tbl, PROP(NUM_MAPS_IN), 1, MPR_INT32, &sig->num_maps_in, NON_MODIFIABLE);
    mpr_tbl_link(tbl, PROP(NUM_MAPS_OUT), 1, MPR_INT32, &sig->num_maps_out, NON_MODIFIABLE);
    mpr_tbl_link(tbl, PROP(PERIOD), 1, MPR_FLT, &sig->period, NON_MODIFIABLE);
    mpr_tbl_link(tbl, PROP(STEAL_MODE), 1, MPR_INT32, &sig->steal_mode, MODIFIABLE);
    mpr_tbl_link(tbl, PROP(TYPE), 1, MPR_TYPE, &sig->type, NON_MODIFIABLE);
    mpr_tbl_link(tbl, PROP(UNIT), 1, MPR_STR, &sig->unit, loc_mod | INDIRECT);
    mpr_tbl_link(tbl, PROP(USE_INST), 1, MPR_BOOL, &sig->use_inst, NON_MODIFIABLE);
    mpr_tbl_link(tbl, PROP(VERSION), 1, MPR_INT32, &sig->obj.version, NON_MODIFIABLE);

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
        mpr_tbl_set(tbl, PROP(MIN), NULL, len, type, min, LOCAL_MODIFY);
    if (max)
        mpr_tbl_set(tbl, PROP(MAX), NULL, len, type, max, LOCAL_MODIFY);

    mpr_tbl_set(tbl, PROP(IS_LOCAL), NULL, 1, MPR_BOOL, &sig->is_local, LOCAL_ACCESS_ONLY | NON_MODIFIABLE);
}

void mpr_sig_free(mpr_sig sig)
{
    int i;
    mpr_local_dev ldev;
    mpr_net net;
    mpr_local_sig lsig = (mpr_local_sig)sig;
    mpr_rtr rtr;
    mpr_rtr_sig rs;
    RETURN_UNLESS(sig && sig->is_local);
    ldev = (mpr_local_dev)sig->dev;

    /* release active instances */
    for (i = 0; i < lsig->idmap_len; i++) {
        if (lsig->idmaps[i].inst)
            mpr_dev_LID_decref(ldev, lsig->group, lsig->idmaps[i].map);
    }

    /* release associated OSC methods */
    mpr_dev_remove_sig_methods(ldev, lsig);
    net = &sig->obj.graph->net;
    rtr = net->rtr;
    rs = rtr->sigs;
    while (rs && rs->sig != lsig)
        rs = rs->next;
    if (rs) {
        mpr_local_map map;
        /* need to unmap */
        for (i = 0; i < rs->num_slots; i++) {
            if (!rs->slots[i])
                continue;
            map = (mpr_local_map)rs->slots[i]->map;
            mpr_map_release((mpr_map)map);
            mpr_rtr_remove_map(rtr, map);
        }
        mpr_rtr_remove_sig(rtr, rs);
    }
    if (ldev->registered) {
        /* Notify subscribers */
        int dir = (sig->dir == MPR_DIR_IN) ? MPR_SIG_IN : MPR_SIG_OUT;
        mpr_net_use_subscribers(net, ldev, dir);
        mpr_sig_send_removed(lsig);
    }
    free(lsig->updated_inst);
    mpr_graph_remove_sig(sig->obj.graph, sig, MPR_OBJ_REM);
    mpr_obj_increment_version((mpr_obj)ldev);
}

void mpr_sig_free_internal(mpr_sig sig)
{
    int i;
    RETURN_UNLESS(sig);
    if (sig->is_local) {
        mpr_local_sig lsig = (mpr_local_sig)sig;
        /* Free instances */
        for (i = 0; i < lsig->idmap_len; i++) {
            if (lsig->idmaps[i].inst)
                mpr_sig_release_inst_internal(lsig, i);
        }
        free(lsig->idmaps);
        for (i = 0; i < lsig->num_inst; i++) {
            FUNC_IF(free, lsig->inst[i]->val);
            FUNC_IF(free, lsig->inst[i]->has_val_flags);
            free(lsig->inst[i]);
        }
        free(lsig->inst);
        FUNC_IF(free, lsig->vec_known);
    }

    FUNC_IF(mpr_tbl_free, sig->obj.props.synced);
    FUNC_IF(mpr_tbl_free, sig->obj.props.staged);
    FUNC_IF(free, sig->max);
    FUNC_IF(free, sig->min);
    FUNC_IF(free, sig->path);
    FUNC_IF(free, sig->unit);
}

void mpr_sig_call_handler(mpr_local_sig lsig, int evt, mpr_id inst, int len,
                          const void *val, mpr_time *time, float diff)
{
    mpr_sig_handler *h;
    /* abort if signal is already being processed - might be a local loop */
    if (lsig->locked) {
        trace_dev(lsig->dev, "Mapping loop detected on signal %s! (2)\n", lsig->name);
        return;
    }
    /* non-instanced signals cannot have a null value */
    if (!val && !lsig->use_inst)
        return;
    mpr_sig_update_timing_stats(lsig, diff);
    h = (mpr_sig_handler*)lsig->handler;
    if (h && (evt & lsig->event_flags))
        h((mpr_sig)lsig, evt, lsig->use_inst ? inst : 0, len, lsig->type, val, *time);
}

/**** Instances ****/

static void _init_inst(mpr_sig_inst si)
{
    si->has_val = 0;
    mpr_time_set(&si->created, MPR_NOW);
    mpr_time_set(&si->time, si->created);
}

static mpr_sig_inst _reserved_inst(mpr_local_sig lsig, mpr_id *id)
{
    int i;
    for (i = 0; i < lsig->num_inst; i++) {
        if (!lsig->inst[i]->active) {
            if (id)
                lsig->inst[i]->id = *id;
            qsort(lsig->inst, lsig->num_inst, sizeof(mpr_sig_inst), _compare_inst_ids);
            return lsig->inst[i];
        }
    }
    return 0;
}

int _oldest_inst(mpr_local_sig lsig)
{
    int i, oldest;
    mpr_sig_inst si;
    for (i = 0; i < lsig->idmap_len; i++) {
        if (lsig->idmaps[i].inst)
            break;
    }
    if (i == lsig->idmap_len) {
        /* no active instances to steal! */
        return -1;
    }
    oldest = i;
    for (i = oldest+1; i < lsig->idmap_len; i++) {
        if (!(si = lsig->idmaps[i].inst))
            continue;
        if ((si->created.sec < lsig->idmaps[oldest].inst->created.sec) ||
            (si->created.sec == lsig->idmaps[oldest].inst->created.sec &&
             si->created.frac < lsig->idmaps[oldest].inst->created.frac))
            oldest = i;
    }
    return oldest;
}

mpr_id mpr_sig_get_oldest_inst_id(mpr_sig sig)
{
    int idx;
    RETURN_ARG_UNLESS(sig && sig->is_local && sig->use_inst, 0);
    idx = _oldest_inst((mpr_local_sig)sig);
    return (idx >= 0) ? ((mpr_local_sig)sig)->idmaps[idx].map->LID : 0;
}

int _newest_inst(mpr_local_sig lsig)
{
    int i, newest;
    mpr_sig_inst si;
    for (i = 0; i < lsig->idmap_len; i++) {
        if (lsig->idmaps[i].inst)
            break;
    }
    if (i == lsig->idmap_len) {
        /* no active instances to steal! */
        return -1;
    }
    newest = i;
    for (i = newest + 1; i < lsig->idmap_len; i++) {
        if (!(si = lsig->idmaps[i].inst))
            continue;
        if ((si->created.sec > lsig->idmaps[newest].inst->created.sec) ||
            (si->created.sec == lsig->idmaps[newest].inst->created.sec &&
             si->created.frac > lsig->idmaps[newest].inst->created.frac))
            newest = i;
    }
    return newest;
}

mpr_id mpr_sig_get_newest_inst_id(mpr_sig sig)
{
    int idx;
    RETURN_ARG_UNLESS(sig && sig->is_local && sig->use_inst, 0);
    idx = _newest_inst((mpr_local_sig)sig);
    return (idx >= 0) ? ((mpr_local_sig)sig)->idmaps[idx].map->LID : 0;
}

int mpr_sig_get_idmap_with_LID(mpr_local_sig lsig, mpr_id LID, int flags, mpr_time t, int activate)
{
    mpr_sig_idmap_t *maps;
    mpr_sig_handler *h;
    mpr_sig_inst si;
    mpr_id_map map;
    int i;
    if (!lsig->use_inst)
        LID = MPR_DEFAULT_INST;
    maps = lsig->idmaps;
    h = (mpr_sig_handler*)lsig->handler;
    for (i = 0; i < lsig->idmap_len; i++) {
        if (maps[i].inst && maps[i].map->LID == LID)
            return (maps[i].status & ~flags) ? -1 : i;
    }
    RETURN_ARG_UNLESS(activate, -1);

    /* check if device has record of id map */
    map = mpr_dev_get_idmap_by_LID((mpr_local_dev)lsig->dev, lsig->group, LID);

    /* No instance with that id exists - need to try to activate instance and
     * create new id map if necessary. */
    if ((si = _find_inst_by_id(lsig, LID)) || (si = _reserved_inst(lsig, &LID))) {
        if (!map) {
            /* Claim id map locally */
            map = mpr_dev_add_idmap((mpr_local_dev)lsig->dev, lsig->group, LID, 0);
        }
        else
            mpr_dev_LID_incref((mpr_local_dev)lsig->dev, map);

        /* store pointer to device map in a new signal map */
        si->active = 1;
        _init_inst(si);
        i = _add_idmap(lsig, si, map);
        if (h && (lsig->event_flags & MPR_SIG_INST_NEW))
            h((mpr_sig)lsig, MPR_SIG_INST_NEW, LID, 0, lsig->type, NULL, t);
        return i;
    }

    RETURN_ARG_UNLESS(h, -1);
    if (lsig->event_flags & MPR_SIG_INST_OFLW) {
        /* call instance event handler */
        h((mpr_sig)lsig, MPR_SIG_INST_OFLW, 0, 0, lsig->type, NULL, t);
    }
    else if (lsig->steal_mode == MPR_STEAL_OLDEST) {
        i = _oldest_inst(lsig);
        if (i < 0)
            return -1;
        h((mpr_sig)lsig, MPR_SIG_REL_UPSTRM & lsig->event_flags ? MPR_SIG_REL_UPSTRM : MPR_SIG_UPDATE,
          lsig->idmaps[i].map->LID, 0, lsig->type, 0, t);
    }
    else if (lsig->steal_mode == MPR_STEAL_NEWEST) {
        i = _newest_inst(lsig);
        if (i < 0)
            return -1;
        h((mpr_sig)lsig, MPR_SIG_REL_UPSTRM & lsig->event_flags ? MPR_SIG_REL_UPSTRM : MPR_SIG_UPDATE,
          lsig->idmaps[i].map->LID, 0, lsig->type, 0, t);
    }
    else
        return -1;

    /* try again */
    if ((si = _find_inst_by_id(lsig, LID)) || (si = _reserved_inst(lsig, &LID))) {
        if (!map) {
            /* Claim id map locally */
            map = mpr_dev_add_idmap((mpr_local_dev)lsig->dev, lsig->group, LID, 0);
        }
        else
            mpr_dev_LID_incref((mpr_local_dev)lsig->dev, map);
        si->active = 1;
        _init_inst(si);
        i = _add_idmap(lsig, si, map);
        if (h && (lsig->event_flags & MPR_SIG_INST_NEW))
            h((mpr_sig)lsig, MPR_SIG_INST_NEW, LID, 0, lsig->type, NULL, t);
        return i;
    }
    return -1;
}

int mpr_sig_get_idmap_with_GID(mpr_local_sig lsig, mpr_id GID, int flags, mpr_time t, int activate)
{
    mpr_sig_idmap_t *maps;
    mpr_sig_handler *h;
    mpr_sig_inst si;
    mpr_id_map map;
    int i;
    maps = lsig->idmaps;
    h = (mpr_sig_handler*)lsig->handler;
    for (i = 0; i < lsig->idmap_len; i++) {
        if (maps[i].map && maps[i].map->GID == GID)
            return (maps[i].status & ~flags) ? -1 : i;
    }
    RETURN_ARG_UNLESS(activate, -1);

    /* check if the device already has a map for this global id */
    map = mpr_dev_get_idmap_by_GID((mpr_local_dev)lsig->dev, lsig->group, GID);
    if (!map) {
        /* Here we still risk creating conflicting maps if two signals are
         * updated asynchronously.  This is easy to avoid by not allowing a
         * local id to be used with multiple active remote maps, however users
         * may wish to create devices with multiple object classes which do not
         * require mutual instance id synchronization - e.g. instance 1 of
         * object class A is not related to instance 1 of object B. */
        if ((si = _reserved_inst(lsig, NULL))) {
            map = mpr_dev_add_idmap((mpr_local_dev)lsig->dev, lsig->group, si->id, GID);
            map->GID_refcount = 1;
            si->active = 1;
            _init_inst(si);
            i = _add_idmap(lsig, si, map);
            if (h && (lsig->event_flags & MPR_SIG_INST_NEW))
                h((mpr_sig)lsig, MPR_SIG_INST_NEW, si->id, 0, lsig->type, NULL, t);
            return i;
        }
    }
    else if ((si = _find_inst_by_id(lsig, map->LID)) || (si = _reserved_inst(lsig, &map->LID))) {
        if (!si->active) {
            si->active = 1;
            _init_inst(si);
            i = _add_idmap(lsig, si, map);
            mpr_dev_LID_incref((mpr_local_dev)lsig->dev, map);
            mpr_dev_GID_incref((mpr_local_dev)lsig->dev, map);
            if (h && (lsig->event_flags & MPR_SIG_INST_NEW))
                h((mpr_sig)lsig, MPR_SIG_INST_NEW, si->id, 0, lsig->type, NULL, t);
            return i;
        }
    }
    else {
        /* TODO: Once signal groups are explicit, allow re-mapping to
         * another instance if possible. */
        trace("Signal %s has no instance %"PR_MPR_ID" available.\n", lsig->name, map->LID);
        return -1;
    }

    RETURN_ARG_UNLESS(h, -1);

    /* try releasing instance in use */
    if (lsig->event_flags & MPR_SIG_INST_OFLW) {
        /* call instance event handler */
        h((mpr_sig)lsig, MPR_SIG_INST_OFLW, 0, 0, lsig->type, NULL, t);
    }
    else if (lsig->steal_mode == MPR_STEAL_OLDEST) {
        i = _oldest_inst(lsig);
        if (i < 0)
            return -1;
        h((mpr_sig)lsig, MPR_SIG_REL_UPSTRM & lsig->event_flags ? MPR_SIG_REL_UPSTRM : MPR_SIG_UPDATE,
          lsig->idmaps[i].map->LID, 0, lsig->type, 0, t);
    }
    else if (lsig->steal_mode == MPR_STEAL_NEWEST) {
        i = _newest_inst(lsig);
        if (i < 0)
            return -1;
        h((mpr_sig)lsig, MPR_SIG_REL_UPSTRM & lsig->event_flags ? MPR_SIG_REL_UPSTRM : MPR_SIG_UPDATE,
          lsig->idmaps[i].map->LID, 0, lsig->type, 0, t);
    }
    else
        return -1;

    /* try again */
    if (!map) {
        if ((si = _reserved_inst(lsig, NULL))) {
            map = mpr_dev_add_idmap((mpr_local_dev)lsig->dev, lsig->group, si->id, GID);
            map->GID_refcount = 1;
            si->active = 1;
            _init_inst(si);
            i = _add_idmap(lsig, si, map);
            if (h && (lsig->event_flags & MPR_SIG_INST_NEW))
                h((mpr_sig)lsig, MPR_SIG_INST_NEW, si->id, 0, lsig->type, NULL, t);
            return i;
        }
    }
    else {
        si = _find_inst_by_id(lsig, map->LID);
        TRACE_RETURN_UNLESS(si && !si->active, -1, "Signal %s has no instance %"
                            PR_MPR_ID" available.", lsig->name, map->LID);
        si->active = 1;
        _init_inst(si);
        i = _add_idmap(lsig, si, map);
        mpr_dev_LID_incref((mpr_local_dev)lsig->dev, map);
        mpr_dev_GID_incref((mpr_local_dev)lsig->dev, map);
        if (h && (lsig->event_flags & MPR_SIG_INST_NEW))
            h((mpr_sig)lsig, MPR_SIG_INST_NEW, si->id, 0, lsig->type, NULL, t);
        return i;
    }
    return -1;
}

static int _reserve_inst(mpr_local_sig lsig, mpr_id *id, void *data)
{
    int i, cont;
    mpr_sig_inst si;
    RETURN_ARG_UNLESS(lsig->num_inst < MAX_INSTANCES, -1);

    /* check if instance with this id already exists! If so, stop here. */
    if (id && _find_inst_by_id(lsig, *id))
        return -1;

    /* reallocate array of instances */
    lsig->inst = realloc(lsig->inst, sizeof(mpr_sig_inst) * (lsig->num_inst + 1));
    lsig->inst[lsig->num_inst] = (mpr_sig_inst) calloc(1, sizeof(struct _mpr_sig_inst));
    si = lsig->inst[lsig->num_inst];
    si->val = calloc(1, mpr_sig_get_vector_bytes((mpr_sig)lsig));
    si->has_val_flags = calloc(1, lsig->len / 8 + 1);
    si->has_val = 0;

    if (id)
        si->id = *id;
    else {
        /* find lowest unused id */
        mpr_id lowest_id = 0;
        cont = 1;
        while (cont) {
            cont = 0;
            for (i = 0; i < lsig->num_inst; i++) {
                if (lsig->inst[i]->id == lowest_id) {
                    cont = 1;
                    break;
                }
            }
            lowest_id += cont;
        }
        si->id = lowest_id;
    }
    si->idx = lsig->num_inst;
    _init_inst(si);
    si->data = data;

    if (++lsig->num_inst > 1) {
        if (!lsig->use_inst) {
            /* TODO: modify associated maps for instanced signals */
        }
        lsig->use_inst = 1;
    }
    qsort(lsig->inst, lsig->num_inst, sizeof(mpr_sig_inst), _compare_inst_ids);
    return lsig->num_inst - 1;;
}

int mpr_sig_reserve_inst(mpr_sig sig, int num, mpr_id *ids, void **data)
{
    int i = 0, count = 0, highest = -1, result, old_num = sig->num_inst;
    mpr_local_sig lsig = (mpr_local_sig)sig;
    RETURN_ARG_UNLESS(sig && sig->is_local && num, 0);

    if (lsig->num_inst == 1 && !lsig->inst[0]->id && !lsig->inst[0]->data) {
        /* we will overwite the default instance first */
        if (ids)
            lsig->inst[0]->id = ids[0];
        if (data)
            lsig->inst[0]->data = data[0];
        ++i;
        ++count;
    }
    for (; i < num; i++) {
        result = _reserve_inst(lsig, ids ? &ids[i] : 0, data ? data[i] : 0);
        if (result == -1)
            continue;
        highest = result;
        ++count;
    }
    if (highest != -1)
        mpr_rtr_num_inst_changed(lsig->obj.graph->net.rtr, lsig, highest + 1);

    if (old_num > 0 && (lsig->num_inst / 8) == (old_num / 8))
        return count;

    /* reallocate instance update bitflags */
    if (!lsig->updated_inst)
        lsig->updated_inst = calloc(1, lsig->num_inst / 8 + 1);
    else if ((old_num / 8) == (lsig->num_inst / 8))
        return count;

    lsig->updated_inst = realloc(lsig->updated_inst, lsig->num_inst / 8 + 1);
    memset(lsig->updated_inst + old_num / 8 + 1, 0, (lsig->num_inst / 8) - (old_num / 8));
    return count;
}

int mpr_sig_get_inst_is_active(mpr_sig sig, mpr_id id)
{
    int idmap_idx;
    RETURN_ARG_UNLESS(sig && sig->is_local, 0);
    RETURN_ARG_UNLESS(sig->use_inst, 1);

    idmap_idx = mpr_sig_get_idmap_with_LID((mpr_local_sig)sig, id, 0, MPR_NOW, 0);
    return (idmap_idx >= 0) ? ((mpr_local_sig)sig)->idmaps[idmap_idx].inst->active : 0;
}

void mpr_sig_update_timing_stats(mpr_local_sig lsig, float diff)
{
    /* make sure time is monotonic */
    if (diff < 0)
        diff = 0;
    if (-1 == lsig->period)
        lsig->period = 0;
    else if (0 == lsig->period)
        lsig->period = diff;
    else {
        lsig->jitter *= 0.99;
        lsig->jitter += (0.01 * fabsf(lsig->period - diff));
        lsig->period *= 0.99;
        lsig->period += (0.01 * diff);
    }
}

void mpr_sig_set_value(mpr_sig sig, mpr_id id, int len, mpr_type type, const void *val)
{
    mpr_time time;
    int idmap_idx;
    mpr_local_sig lsig = (mpr_local_sig)sig;
    mpr_sig_inst si;
    RETURN_UNLESS(sig && sig->is_local);
    if (!len || !val) {
        mpr_sig_release_inst(sig, id);
        return;
    }
    if (!mpr_type_get_is_num(type)) {
#ifdef DEBUG
        trace("called update on signal '%s' with non-number type '%c'\n", lsig->name, type);
#endif
        return;
    }
    if (len && (len != lsig->len)) {
#ifdef DEBUG
        trace("called update on signal '%s' with value length %d (should be %d)\n",
              lsig->name, len, lsig->len);
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
    time = mpr_dev_get_time(sig->dev);
    idmap_idx = mpr_sig_get_idmap_with_LID(lsig, id, 0, time, 1);
    RETURN_UNLESS(idmap_idx >= 0);
    si = lsig->idmaps[idmap_idx].inst;

    /* update time */
    mpr_sig_update_timing_stats(lsig, si->has_val ? mpr_time_get_diff(time, si->time) : 0);
    memcpy(&si->time, &time, sizeof(mpr_time));

    /* update value */
    if (type != lsig->type)
        set_coerced_val(lsig->len, type, val, lsig->len, lsig->type, si->val);
    else
        memcpy(si->val, (void*)val, mpr_sig_get_vector_bytes(sig));
    si->has_val = 1;

    /* mark instance as updated */
    set_bitflag(lsig->updated_inst, si->idx);
    ((mpr_local_dev)lsig->dev)->sending = lsig->updated = 1;

    mpr_rtr_process_sig(lsig->obj.graph->net.rtr, lsig, idmap_idx, si->has_val ? si->val : 0, si->time);
}

void mpr_sig_release_inst(mpr_sig sig, mpr_id id)
{
    int idmap_idx;
    RETURN_UNLESS(sig && sig->is_local && sig->use_inst);
    idmap_idx = mpr_sig_get_idmap_with_LID((mpr_local_sig)sig, id, RELEASED_REMOTELY, MPR_NOW, 0);
    if (idmap_idx >= 0)
        mpr_sig_release_inst_internal((mpr_local_sig)sig, idmap_idx);
}

void mpr_sig_release_inst_internal(mpr_local_sig lsig, int idmap_idx)
{
    mpr_sig_idmap_t *smap = &lsig->idmaps[idmap_idx];
    RETURN_UNLESS(smap->inst);

    /* mark instance as updated */
    set_bitflag(lsig->updated_inst, smap->inst->idx);
    ((mpr_local_dev)lsig->dev)->sending = lsig->updated = 1;

    mpr_rtr_process_sig(lsig->obj.graph->net.rtr, lsig, idmap_idx, 0, smap->inst->time);

    if (mpr_dev_LID_decref((mpr_local_dev)lsig->dev, lsig->group, smap->map))
        smap->map = 0;
    else if ((lsig->dir & MPR_DIR_OUT) || smap->status & RELEASED_REMOTELY) {
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
    int i, remove_idx;
    mpr_local_sig lsig = (mpr_local_sig)sig;
    RETURN_UNLESS(sig && sig->is_local && sig->use_inst);
    for (i = 0; i < lsig->num_inst; i++) {
        if (lsig->inst[i]->id == id)
            break;
    }
    RETURN_UNLESS(i < lsig->num_inst);

    if (lsig->inst[i]->active) {
       /* First release instance */
       mpr_sig_release_inst_internal(lsig, i);
    }

    remove_idx = lsig->inst[i]->idx;

    /* Free value and timetag memory held by instance */
    FUNC_IF(free, lsig->inst[i]->val);
    FUNC_IF(free, lsig->inst[i]->has_val_flags);
    free(lsig->inst[i]);

    for (++i; i < lsig->num_inst; i++)
    lsig->inst[i-1] = lsig->inst[i];
    --lsig->num_inst;
    lsig->inst = realloc(lsig->inst, sizeof(mpr_sig_inst) * lsig->num_inst);

    /* Remove instance memory held by map slots */
    mpr_rtr_remove_inst(lsig->obj.graph->net.rtr, lsig, remove_idx);

    for (i = 0; i < lsig->num_inst; i++) {
        if (lsig->inst[i]->idx > remove_idx)
            --lsig->inst[i]->idx;
    }
}

const void *mpr_sig_get_value(mpr_sig sig, mpr_id id, mpr_time *time)
{
    mpr_local_sig lsig = (mpr_local_sig)sig;
    mpr_sig_inst si;
    mpr_time now;
    RETURN_ARG_UNLESS(sig && sig->is_local, 0);

    if (!lsig->use_inst)
        si = lsig->idmaps[0].inst;
    else {
        int idmap_idx = mpr_sig_get_idmap_with_LID(lsig, id, RELEASED_REMOTELY, MPR_NOW, 0);
        RETURN_ARG_UNLESS(idmap_idx >= 0, 0);
        si = lsig->idmaps[idmap_idx].inst;
    }
    RETURN_ARG_UNLESS(si && si->has_val, 0)
    if (time) {
        time->sec = si->time.sec;
        time->frac = si->time.frac;
    }
    mpr_time_set(&now, MPR_NOW);
    mpr_sig_update_timing_stats(lsig, mpr_time_get_diff(now, si->time));
    return si->val;
}

int mpr_sig_get_num_inst(mpr_sig sig, mpr_status status)
{
    int i, j;
    RETURN_ARG_UNLESS(sig && sig->is_local, 0);
    RETURN_ARG_UNLESS(sig->use_inst, 1);
    if ((status & (MPR_STATUS_ACTIVE | MPR_STATUS_RESERVED)) == (MPR_STATUS_ACTIVE | MPR_STATUS_RESERVED))
        return sig->num_inst;
    status = status & MPR_STATUS_ACTIVE ? 1 : 0;
    for (i = 0, j = 0; i < sig->num_inst; i++) {
        if (((mpr_local_sig)sig)->inst[i]->active == status)
            ++j;
    }
    return j;
}

mpr_id mpr_sig_get_inst_id(mpr_sig sig, int idx, mpr_status status)
{
    int i, j;
    mpr_local_sig lsig = (mpr_local_sig)sig;
    RETURN_ARG_UNLESS(sig && sig->is_local && sig->use_inst, 0);
    RETURN_ARG_UNLESS(idx >= 0 && idx < sig->num_inst, 0);
    if ((status & (MPR_STATUS_ACTIVE | MPR_STATUS_RESERVED)) == (MPR_STATUS_ACTIVE | MPR_STATUS_RESERVED))
        return lsig->inst[idx]->id;
    status = status & MPR_STATUS_ACTIVE ? 1 : 0;
    for (i = 0, j = -1; i < lsig->num_inst; i++) {
        if (lsig->inst[i]->active != status)
            continue;
        if (++j == idx)
            return lsig->inst[i]->id;
    }
    return 0;
}

int mpr_sig_activate_inst(mpr_sig sig, mpr_id id)
{
    int idmap_idx;
    mpr_time time;
    RETURN_ARG_UNLESS(sig && sig->is_local && sig->use_inst, 0);
    time = mpr_dev_get_time(sig->dev);
    idmap_idx = mpr_sig_get_idmap_with_LID((mpr_local_sig)sig, id, 0, time, 1);
    return idmap_idx >= 0;
}

void mpr_sig_set_inst_data(mpr_sig sig, mpr_id id, const void *data)
{
    mpr_sig_inst si;
    RETURN_UNLESS(sig && sig->is_local && sig->use_inst);
    si = _find_inst_by_id((mpr_local_sig)sig, id);
    if (si)
        si->data = (void*)data;
}

void *mpr_sig_get_inst_data(mpr_sig sig, mpr_id id)
{
    mpr_sig_inst si;
    RETURN_ARG_UNLESS(sig && sig->is_local && sig->use_inst, 0);
    si = _find_inst_by_id((mpr_local_sig)sig, id);
    return si ? si->data : 0;
}

/**** Queries ****/

void mpr_sig_set_cb(mpr_sig sig, mpr_sig_handler *h, int events)
{
    mpr_local_sig lsig = (mpr_local_sig)sig;
    RETURN_UNLESS(sig && sig->is_local);
    if (!lsig->handler && h && events) {
        /* Need to register a new liblo methods */
        mpr_dev_add_sig_methods((mpr_local_dev)sig->dev, lsig);
    }
    else if (lsig->handler && !(h || events)) {
        /* Need to remove liblo methods */
        mpr_dev_remove_sig_methods((mpr_local_dev)sig->dev, lsig);
    }
    lsig->handler = (void*)h;
    lsig->event_flags = events;
}

/**** Signal Properties ****/

/* Internal function only */
int mpr_sig_full_name(mpr_sig sig, char *name, int len)
{
    int dev_name_len;
    const char *dev_name = mpr_dev_get_name(sig->dev);
    RETURN_ARG_UNLESS(dev_name, 0);

    dev_name_len = strlen(dev_name);
    if (dev_name_len >= len)
        return 0;
    if ((dev_name_len + strlen(sig->name) + 1) > len)
        return 0;

    snprintf(name, len, "%s%s", dev_name, sig->path);
    return strlen(name);
}

mpr_dev mpr_sig_get_dev(mpr_sig sig)
{
    return sig->dev;
}

static int cmp_qry_sig_maps(const void *context_data, mpr_map map)
{
    mpr_sig sig = *(mpr_sig*)context_data;
    int dir = *(int*)((char*)context_data + sizeof(mpr_sig*));
    if (!dir || (dir & MPR_DIR_OUT)) {
        int i;
        for (i = 0; i < map->num_src; i++) {
            if (map->src[i]->sig == sig)
                return 1;
        }
    }
    if (!dir || (dir & MPR_DIR_IN)) {
        if (map->dst->sig == sig)
            return 1;
    }
    return 0;
}

mpr_list mpr_sig_get_maps(mpr_sig sig, mpr_dir dir)
{
    mpr_list q;
    RETURN_ARG_UNLESS(sig && sig->obj.graph->maps, 0);
    q = mpr_list_new_query((const void**)&sig->obj.graph->maps, (void*)cmp_qry_sig_maps,
                           "vi", &sig, dir);
    return mpr_list_start(q);
}

static int _add_idmap(mpr_local_sig lsig, mpr_sig_inst si, mpr_id_map map)
{
    /* find unused signal map */
    int i;
    for (i = 0; i < lsig->idmap_len; i++) {
        if (!lsig->idmaps[i].map)
            break;
    }
    if (i == lsig->idmap_len) {
        /* need more memory */
        if (lsig->idmap_len >= MAX_INSTANCES) {
            /* Arbitrary limit to number of tracked idmaps */
            /* TODO: add checks for this return value */
            return -1;
        }
        lsig->idmap_len = lsig->idmap_len ? lsig->idmap_len * 2 : 1;
        lsig->idmaps = realloc(lsig->idmaps, (lsig->idmap_len * sizeof(struct _mpr_sig_idmap)));
        memset(lsig->idmaps + i, 0, ((lsig->idmap_len - i) * sizeof(struct _mpr_sig_idmap)));
    }
    lsig->idmaps[i].map = map;
    lsig->idmaps[i].inst = si;
    lsig->idmaps[i].status = 0;
    return i;
}

void mpr_sig_send_state(mpr_sig sig, net_msg_t cmd)
{
    char str[BUFFSIZE];
    lo_message msg;
    RETURN_UNLESS(sig);
    msg = lo_message_new();
    RETURN_UNLESS(msg);

    if (cmd == MSG_SIG_MOD) {
        lo_message_add_string(msg, sig->name);

        /* properties */
        mpr_tbl_add_to_msg(sig->is_local ? sig->obj.props.synced : 0, sig->obj.props.staged, msg);

        snprintf(str, BUFFSIZE, "/%s/signal/modify", sig->dev->name);
        mpr_net_add_msg(&sig->obj.graph->net, str, 0, msg);
        /* send immediately since path string is not cached */
        mpr_net_send(&sig->obj.graph->net);
    }
    else {
        mpr_sig_full_name(sig, str, BUFFSIZE);
        lo_message_add_string(msg, str);

        /* properties */
        mpr_tbl_add_to_msg(sig->is_local ? sig->obj.props.synced : 0, sig->obj.props.staged, msg);
        mpr_net_add_msg(&sig->obj.graph->net, 0, cmd, msg);
    }
}

void mpr_sig_send_removed(mpr_local_sig lsig)
{
    char sig_name[BUFFSIZE];
    NEW_LO_MSG(msg, return);
    mpr_sig_full_name((mpr_sig)lsig, sig_name, BUFFSIZE);
    lo_message_add_string(msg, sig_name);
    mpr_net_add_msg(&lsig->obj.graph->net, 0, MSG_SIG_REM, msg);
}

/*! Update information about a signal record based on message properties. */
int mpr_sig_set_from_msg(mpr_sig sig, mpr_msg msg)
{
    mpr_msg_atom a;
    int i, updated = 0;
    mpr_tbl tbl = sig->obj.props.synced;
    RETURN_ARG_UNLESS(msg, 0);

    for (i = 0; i < msg->num_atoms; i++) {
        a = &msg->atoms[i];
        if (sig->is_local && (MASK_PROP_BITFLAGS(a->prop) != PROP(EXTRA)))
            continue;
        switch (a->prop) {
            case PROP(DIR): {
                int dir = 0;
                if (!mpr_type_get_is_str(a->types[0]))
                    break;
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
                    if (sig->obj.id != (a->vals[0])->i64) {
                        sig->obj.id = (a->vals[0])->i64;
                        ++updated;
                    }
                }
                break;
            case PROP(STEAL_MODE): {
                int stl;
                if (!mpr_type_get_is_str(a->types[0]))
                    break;
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
