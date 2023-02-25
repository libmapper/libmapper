#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <stddef.h>
#include <assert.h>

#include "bitflags.h"
#include "device.h"
#include "graph.h"
#include "list.h"
#include "mpr_signal.h"
#include "network.h"
#include "path.h"
#include "router.h"
#include "slot.h"
#include "table.h"
#include "util/mpr_set_coerced.h"

#include <mapper/mapper.h>

#ifdef _MSC_VER
#include <malloc.h>
#endif

#define MAX_INST 128
#define BUFFSIZE 512

/* TODO: MPR_DEFAULT_INST is actually a valid id - we should use
 * another method for distinguishing non-instanced updates. */
#define MPR_DEFAULT_INST -1

/* Function prototypes */
static int _init_and_add_id_map(mpr_local_sig lsig, mpr_sig_inst si, mpr_id_map id_map);

static int _compare_inst_ids(const void *l, const void *r)
{
    return (*(mpr_sig_inst*)l)->id - (*(mpr_sig_inst*)r)->id;
}

static mpr_sig_inst _find_inst_by_id(mpr_local_sig lsig, mpr_id id)
{
    mpr_sig_inst_t si, *sip, **sipp;
    RETURN_ARG_UNLESS(lsig->num_inst, 0);
    RETURN_ARG_UNLESS(lsig->use_inst, lsig->inst[0]);
    sip = &si;
    si.id = id;
    sipp = bsearch(&sip, lsig->inst, lsig->num_inst, sizeof(mpr_sig_inst), _compare_inst_ids);
    return (sipp && *sipp) ? *sipp : 0;
}

/*! Helper to find the size in bytes of a signal's full vector. */
MPR_INLINE static size_t get_vector_size(mpr_sig sig)
{
    return mpr_type_get_size(sig->type) * sig->len;
}

/*! Helper to check if a type character is valid. */
MPR_INLINE static int check_sig_length(int length)
{
    return (length < 1 || length > MPR_MAX_VECTOR_LEN);
}

MPR_INLINE static int check_types(const mpr_type *types, int len, mpr_type type, int vector_len)
{
    int i, vals = 0;
    RETURN_ARG_UNLESS(len >= vector_len, -1);
    for (i = 0; i < len; i++) {
        if (types[i] == type)
            ++vals;
        else if (types[i] != MPR_NULL)
            return -1;
    }
    return vals;
}

/* Notes:
 * - Incoming signal values may be scalars or vectors, but much match the
 *   length of the target signal or mapping slot.
 * - Vectors are of homogeneous type (MPR_INT32, MPR_FLT or MPR_DBL) however
 *   individual elements may have no value (type MPR_NULL)
 * - A vector consisting completely of nulls indicates a signal instance release
 *   TODO: use more specific message for release?
 * - Updates to a specific signal instance are indicated using the label
 *   "@in" followed by a 64bit integer which uniquely identifies this
 *   instance within the network of libmapper devices
 * - Updates to specific "slots" of a convergent (i.e. multi-source) mapping
 *   are indicated using the label "@sl" followed by a single integer slot #
 * - Instance creation and release may also be triggered by expression
 *   evaluation. Refer to the document "Using Instanced Signals with Libmapper"
 *   for more information.
 */
int mpr_sig_lo_handler(const char *path, const char *types, lo_arg **argv, int argc,
                       lo_message msg, void *data)
{
    mpr_local_sig sig = (mpr_local_sig)data;
    mpr_local_dev dev;
    mpr_sig_inst si;
    mpr_net net = mpr_graph_get_net(sig->obj.graph);
    mpr_rtr rtr = mpr_net_get_rtr(net);
    int i, val_len = 0, vals, size, all;
    int id_map_idx, inst_idx, slot_idx = -1, map_manages_inst = 0;
    mpr_id GID = 0;
    mpr_id_map id_map;
    mpr_local_map map = 0;
    mpr_local_slot slot = 0;
    mpr_sig slot_sig = 0;
    float diff;
    mpr_time ts;

    TRACE_RETURN_UNLESS(sig && (dev = sig->dev), 0,
                        "error in mpr_sig_lo_handler, missing user data\n");
    TRACE_RETURN_UNLESS(sig->num_inst, 0, "signal '%s' has no instances.\n", sig->name);
    RETURN_ARG_UNLESS(argc, 0);

    ts = mpr_net_get_bundle_time(net);

    /* We need to consider that there may be properties appended to the msg
     * check length and find properties if any */
    while (val_len < argc && types[val_len] != MPR_STR)
        ++val_len;
    i = val_len;
    while (i < argc) {
        /* Parse any attached properties (instance ids, slot number) */
        TRACE_RETURN_UNLESS(types[i] == MPR_STR, 0,
                            "error in mpr_sig_lo_handler: unexpected argument type.\n")
        if ((strcmp(&argv[i]->s, "@in") == 0) && argc >= i + 2) {
            TRACE_RETURN_UNLESS(types[i+1] == MPR_INT64, 0,
                                "error in mpr_sig_lo_handler: bad arguments for 'instance' prop.\n")
            GID = argv[i+1]->i64;
            i += 2;
        }
        else if ((strcmp(&argv[i]->s, "@sl") == 0) && argc >= i + 2) {
            TRACE_RETURN_UNLESS(types[i+1] == MPR_INT32, 0,
                                "error in mpr_sig_lo_handler: bad arguments for 'slot' prop.\n")
            slot_idx = argv[i+1]->i32;
            i += 2;
        }
        else {
            trace("error in mpr_sig_lo_handler: unknown property name '%s'.\n", &argv[i]->s);
            return 0;
        }
    }

    if (slot_idx >= 0) {
        /* retrieve mapping associated with this slot */
        slot = mpr_rtr_get_slot(rtr, sig, slot_idx);
        TRACE_RETURN_UNLESS(slot, 0, "error in mpr_sig_lo_handler: slot %d not found.\n", slot_idx);
        slot_sig = mpr_slot_get_sig((mpr_slot)slot);
        map = (mpr_local_map)mpr_slot_get_map((mpr_slot)slot);
        TRACE_RETURN_UNLESS(map->status >= MPR_STATUS_READY, 0,
                            "error in mpr_sig_lo_handler: map not yet ready.\n");
        if (map->expr && !map->is_local_only) {
            vals = check_types(types, val_len, slot_sig->type, slot_sig->len);
            map_manages_inst = mpr_expr_get_manages_inst(map->expr);
        }
        else {
            /* value has already been processed at source device */
            map = 0;
            vals = check_types(types, val_len, sig->type, sig->len);
        }
    }
    else
        vals = check_types(types, val_len, sig->type, sig->len);
    RETURN_ARG_UNLESS(vals >= 0, 0);

    /* TODO: optionally discard out-of-order messages
     * requires timebase sync for many-to-one mappings or local updates
     *    if (sig->discard_out_of_order && out_of_order(si->time, t))
     *        return 0;
     */

    if (GID) {
        id_map_idx = mpr_sig_get_id_map_with_GID(sig, GID, RELEASED_LOCALLY, ts, 0);
        if (id_map_idx < 0) {
            /* No instance found with this id_map – don't activate instance just to release it again */
            RETURN_ARG_UNLESS(vals && sig->dir == MPR_DIR_IN, 0);

            if (map_manages_inst && vals == slot_sig->len) {
                /* special case: do a dry-run to check whether this map will
                 * cause a release. If so, don't bother stealing an instance. */
                /* TODO: move to e.g. mpr_map_dry_run(map, ...) */
                mpr_value *src;
                mpr_value_t v = {0, 0, 1, 0, 1};
                mpr_value_buffer_t b = {0, 0, -1};
                int slot_id = mpr_slot_get_id((mpr_slot)slot);
                b.samps = argv[0];
                v.inst = &b;
                v.vlen = val_len;
                v.type = slot_sig->type;
                src = alloca(map->num_src * sizeof(mpr_value));
                for (i = 0; i < map->num_src; i++)
                    src[i] = (i == slot_id) ? &v : 0;
                if (  mpr_expr_eval(mpr_rtr_get_expr_stack(rtr), map->expr, src, 0, 0, 0, 0, 0)
                    & EXPR_RELEASE_BEFORE_UPDATE)
                    return 0;
            }

            /* otherwise try to init reserved/stolen instance with device map */
            id_map_idx = mpr_sig_get_id_map_with_GID(sig, GID, RELEASED_REMOTELY, ts, 1);
            TRACE_RETURN_UNLESS(id_map_idx >= 0, 0,
                                "no instances available for GUID %"PR_MPR_ID" (1)\n", GID);
        }
        else if (sig->id_maps[id_map_idx].status & RELEASED_LOCALLY) {
            /* map was already released locally, we are only interested in release messages */
            if (0 == vals) {
                /* we can clear signal's reference to map */
                id_map = sig->id_maps[id_map_idx].id_map;
                sig->id_maps[id_map_idx].id_map = 0;
                mpr_dev_GID_decref(dev, sig->group, id_map);
            }
            return 0;
        }
        TRACE_RETURN_UNLESS(sig->id_maps[id_map_idx].inst, 0,
                            "error in mpr_sig_lo_handler: missing instance!\n");
    }
    else {
        /* use the first available instance */
        for (i = 0; i < sig->num_inst; i++) {
            if (sig->inst[i]->active)
                break;
        }
        if (i >= sig->num_inst)
            i = 0;
        id_map_idx = mpr_sig_get_id_map_with_LID(sig, sig->inst[i]->id, RELEASED_REMOTELY, ts, 1);
        RETURN_ARG_UNLESS(id_map_idx >= 0, 0);
    }
    si = sig->id_maps[id_map_idx].inst;
    inst_idx = si->idx;
    diff = mpr_time_get_diff(ts, si->time);
    id_map = sig->id_maps[id_map_idx].id_map;

    size = mpr_type_get_size(map ? slot_sig->type : sig->type);
    if (vals == 0) {
        if (GID) {
            /* TODO: mark SLOT status as remotely released rather than id_map? */
            sig->id_maps[id_map_idx].status |= RELEASED_REMOTELY;
            mpr_dev_GID_decref(dev, sig->group, id_map);
            if (!sig->ephemeral) {
                /* clear signal's reference to id_map */
                mpr_dev_LID_decref(dev, sig->group, id_map);
                sig->id_maps[id_map_idx].id_map = 0;
                return 0;
            }
        }
        RETURN_ARG_UNLESS(sig->ephemeral && (!map || map->use_inst), 0);

        /* Try to release instance, but do not call mpr_rtr_process_sig() here, since we don't
         * know if the local signal instance will actually be released. */
        if (sig->dir == MPR_DIR_IN)
            mpr_sig_call_handler(sig, MPR_SIG_REL_UPSTRM, id_map->LID, 0, 0, ts, diff);
        else
            mpr_sig_call_handler(sig, MPR_SIG_REL_DNSTRM, id_map->LID, 0, 0, ts, diff);

        RETURN_ARG_UNLESS(map && MPR_LOC_DST == map->process_loc && sig->dir == MPR_DIR_IN, 0);

        /* Reset memory for corresponding source slot. */
        mpr_slot_reset_inst(slot, inst_idx);
        return 0;
    }
    else if (sig->dir == MPR_DIR_OUT && !sig->handler)
        return 0;

    /* Partial vector updates are not allowed in convergent maps since the slot value mirrors the
     * remote signal value. */
    if (map && vals != slot_sig->len) {
#ifdef DEBUG
        trace_dev(dev, "error in mpr_dev_handler: partial vector update "
                  "applied to convergent mapping slot.");
#endif
        return 0;
    }

    all = !GID;
    if (map) {
        /* Or if this signal slot is non-instanced but the map has other instanced
         * sources we will need to update all of the map instances. */
        all |= !map->use_inst || (map->num_src > 1 && map->num_inst > slot_sig->num_inst);
    }
    if (all)
        id_map_idx = 0;

    if (map) {
        for (; id_map_idx < sig->id_map_len; id_map_idx++) {
            /* check if map instance is active */
            if ((si = sig->id_maps[id_map_idx].inst) && si->active) {
                inst_idx = si->idx;
                /* Setting to local timestamp here */
                /* TODO: jitter mitigation etc. */
                if (mpr_slot_set_value(slot, inst_idx, argv[0], mpr_dev_get_time((mpr_dev)dev))) {
                    mpr_bitflags_set(map->updated_inst, inst_idx);
                    map->updated = 1;
                    mpr_local_dev_set_receiving(dev);
                }
            }
            if (!all)
                break;
        }
        return 0;
    }

    for (; id_map_idx < sig->id_map_len; id_map_idx++) {
        /* check if instance is active */
        if ((si = sig->id_maps[id_map_idx].inst) && si->active) {
            id_map = sig->id_maps[id_map_idx].id_map;
            for (i = 0; i < sig->len; i++) {
                if (types[i] == MPR_NULL)
                    continue;
                memcpy((char*)si->val + i * size, argv[i], size);
                mpr_bitflags_set(si->has_val_flags, i);
            }
            if (!mpr_bitflags_compare(si->has_val_flags, sig->vec_known, sig->len))
                si->has_val = 1;
            if (si->has_val) {
                memcpy(&si->time, &ts, sizeof(mpr_time));
                mpr_bitflags_unset(sig->updated_inst, si->idx);
                mpr_sig_call_handler(sig, MPR_SIG_UPDATE, id_map->LID, sig->len, si->val, ts, diff);
                /* Pass this update downstream if signal is an input and was not updated in handler. */
                if (!(sig->dir & MPR_DIR_OUT) && !mpr_bitflags_get(sig->updated_inst, si->idx)) {
                    mpr_rtr_process_sig(rtr, sig, id_map_idx, si->val, ts);
                    /* TODO: ensure update is propagated within this poll cycle */
                }
            }
        }
        if (!all)
            break;
    }
    return 0;
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
    RETURN_ARG_UNLESS(dev && mpr_obj_get_is_local((mpr_obj)dev), 0);
    RETURN_ARG_UNLESS(name && !check_sig_length(len) && mpr_type_get_is_num(type), 0);
    TRACE_RETURN_UNLESS(name[strlen(name)-1] != '/', 0,
                        "trailing slash detected in signal name.\n");
    TRACE_RETURN_UNLESS(dir == MPR_DIR_IN || dir == MPR_DIR_OUT, 0,
                        "signal direction must be either input or output.\n")
    g = mpr_obj_get_graph((mpr_obj)dev);
    if ((lsig = (mpr_local_sig)mpr_dev_get_sig_by_name(dev, name)))
        return (mpr_sig)lsig;

    lsig = (mpr_local_sig)mpr_graph_add_list_item(g, MPR_SIG, sizeof(mpr_local_sig_t));
    lsig->dev = (mpr_local_dev)dev;
    lsig->obj.id = mpr_dev_get_unused_sig_id((mpr_local_dev)dev);
    lsig->period = -1;
    lsig->handler = (void*)h;
    lsig->event_flags = events;
    lsig->obj.is_local = 1;
    mpr_sig_init((mpr_sig)lsig, dir, name, len, type, unit, min, max, num_inst);

    mpr_local_dev_add_server_method((mpr_local_dev)dev, lsig->path, mpr_sig_lo_handler, lsig);
    mpr_local_dev_add_sig((mpr_local_dev)dev, lsig, dir);
    return (mpr_sig)lsig;
}

void mpr_sig_init(mpr_sig sig, mpr_dir dir, const char *name, int len, mpr_type type,
                  const char *unit, const void *min, const void *max, int *num_inst)
{
    int i, str_len, loc_mod, rem_mod;
    mpr_tbl tbl;
    RETURN_UNLESS(name);

    name = mpr_path_skip_slash(name);
    str_len = strlen(name)+2;
    sig->path = malloc(str_len);
    snprintf(sig->path, str_len, "/%s", name);
    sig->name = (char*)sig->path+1;
    sig->len = len;
    sig->type = type;
    sig->dir = dir ? dir : MPR_DIR_OUT;
    sig->unit = unit ? strdup(unit) : strdup("unknown");
    sig->ephemeral = 0;
    sig->steal_mode = MPR_STEAL_NONE;

    if (sig->obj.is_local) {
        mpr_local_sig lsig = (mpr_local_sig)sig;
        sig->num_inst = 0;
        lsig->vec_known = calloc(1, len / 8 + 1);
        for (i = 0; i < len; i++)
            mpr_bitflags_set(lsig->vec_known, i);
        lsig->updated_inst = 0;
        if (num_inst) {
            mpr_sig_reserve_inst((mpr_sig)lsig, *num_inst, 0, 0);
            /* default to ephemeral */
            lsig->ephemeral = 1;
        }
        else {
            mpr_sig_reserve_inst((mpr_sig)lsig, 1, 0, 0);
            lsig->use_inst = 0;
        }

        /* Reserve one instance id map */
        lsig->id_map_len = 1;
        lsig->id_maps = calloc(1, sizeof(struct _mpr_sig_id_map));
    }
    else {
        sig->num_inst = 1;
        sig->use_inst = 0;
        sig->obj.props.staged = mpr_tbl_new();
    }

    sig->obj.type = MPR_SIG;
    sig->obj.props.synced = mpr_tbl_new();

    tbl = sig->obj.props.synced;
    loc_mod = sig->obj.is_local ? MODIFIABLE : NON_MODIFIABLE;
    rem_mod = sig->obj.is_local ? NON_MODIFIABLE : MODIFIABLE;

    /* these properties need to be added in alphabetical order */
    mpr_tbl_link_value(tbl, PROP(DATA), 1, MPR_PTR, &sig->obj.data,
                       LOCAL_MODIFY | INDIRECT | LOCAL_ACCESS_ONLY);
    mpr_tbl_link_value(tbl, PROP(DEV), 1, MPR_DEV, &sig->dev,
                       NON_MODIFIABLE | INDIRECT | LOCAL_ACCESS_ONLY);
    mpr_tbl_link_value(tbl, PROP(DIR), 1, MPR_INT32, &sig->dir, MODIFIABLE);
    mpr_tbl_link_value(tbl, PROP(EPHEM), 1, MPR_BOOL, &sig->ephemeral, loc_mod);
    mpr_tbl_link_value(tbl, PROP(ID), 1, MPR_INT64, &sig->obj.id, rem_mod);
    mpr_tbl_link_value(tbl, PROP(JITTER), 1, MPR_FLT, &sig->jitter, NON_MODIFIABLE);
    mpr_tbl_link_value(tbl, PROP(LEN), 1, MPR_INT32, &sig->len, rem_mod);
    mpr_tbl_link_value(tbl, PROP(NAME), 1, MPR_STR, &sig->name, NON_MODIFIABLE | INDIRECT);
    mpr_tbl_link_value(tbl, PROP(NUM_INST), 1, MPR_INT32, &sig->num_inst, NON_MODIFIABLE);
    mpr_tbl_link_value(tbl, PROP(NUM_MAPS_IN), 1, MPR_INT32, &sig->num_maps_in, NON_MODIFIABLE);
    mpr_tbl_link_value(tbl, PROP(NUM_MAPS_OUT), 1, MPR_INT32, &sig->num_maps_out, NON_MODIFIABLE);
    mpr_tbl_link_value(tbl, PROP(PERIOD), 1, MPR_FLT, &sig->period, NON_MODIFIABLE);
    mpr_tbl_link_value(tbl, PROP(STEAL_MODE), 1, MPR_INT32, &sig->steal_mode, MODIFIABLE);
    mpr_tbl_link_value(tbl, PROP(TYPE), 1, MPR_TYPE, &sig->type, NON_MODIFIABLE);
    mpr_tbl_link_value(tbl, PROP(UNIT), 1, MPR_STR, &sig->unit, loc_mod | INDIRECT);
    mpr_tbl_link_value(tbl, PROP(USE_INST), 1, MPR_BOOL, &sig->use_inst, NON_MODIFIABLE);
    mpr_tbl_link_value(tbl, PROP(VERSION), 1, MPR_INT32, &sig->obj.version, NON_MODIFIABLE);

    if (min)
        mpr_tbl_add_record(tbl, PROP(MIN), NULL, len, type, min, LOCAL_MODIFY);
    if (max)
        mpr_tbl_add_record(tbl, PROP(MAX), NULL, len, type, max, LOCAL_MODIFY);

    mpr_tbl_add_record(tbl, PROP(IS_LOCAL), NULL, 1, MPR_BOOL, &sig->obj.is_local,
                       LOCAL_ACCESS_ONLY | NON_MODIFIABLE);
}

void mpr_sig_free(mpr_sig sig)
{
    int i;
    mpr_local_dev ldev;
    mpr_net net;
    mpr_local_sig lsig = (mpr_local_sig)sig;
    RETURN_UNLESS(sig && sig->obj.is_local);
    ldev = (mpr_local_dev)sig->dev;

    /* release active instances */
    for (i = 0; i < lsig->id_map_len; i++) {
        if (lsig->id_maps[i].id_map) {
            mpr_dev_LID_decref(ldev, lsig->group, lsig->id_maps[i].id_map);
            lsig->id_maps[i].id_map = NULL;
        }
    }

    /* release associated OSC methods */
    mpr_local_dev_remove_server_method(ldev, lsig->path);
    net = mpr_graph_get_net(sig->obj.graph);
    mpr_rtr_remove_sig(net->rtr, lsig);

    if (mpr_dev_get_is_registered((mpr_dev)ldev)) {
        /* Notify subscribers */
        int dir = (sig->dir == MPR_DIR_IN) ? MPR_SIG_IN : MPR_SIG_OUT;
        mpr_net_use_subscribers(net, ldev, dir);
        mpr_sig_send_removed(lsig);
    }
    mpr_graph_remove_sig(sig->obj.graph, sig, MPR_OBJ_REM);
    mpr_obj_increment_version((mpr_obj)ldev);
}

void mpr_sig_free_internal(mpr_sig sig)
{
    int i;
    RETURN_UNLESS(sig);
    mpr_dev_remove_sig(sig->dev, sig);
    if (sig->obj.is_local) {
        mpr_local_sig lsig = (mpr_local_sig)sig;
        /* Free instances */
        for (i = 0; i < lsig->id_map_len; i++) {
            if (lsig->id_maps[i].inst)
                mpr_sig_release_inst_internal(lsig, i);
        }
        free(lsig->id_maps);
        for (i = 0; i < lsig->num_inst; i++) {
            FUNC_IF(free, lsig->inst[i]->val);
            FUNC_IF(free, lsig->inst[i]->has_val_flags);
            free(lsig->inst[i]);
        }
        free(lsig->inst);
        free(lsig->updated_inst);
        FUNC_IF(free, lsig->vec_known);
    }

    mpr_obj_free(&sig->obj);
    FUNC_IF(free, sig->path);
    FUNC_IF(free, sig->unit);
}

void mpr_sig_call_handler(mpr_local_sig lsig, int evt, mpr_id inst, int len,
                          const void *val, mpr_time time, float diff)
{
    mpr_sig_handler *h;
    /* abort if signal is already being processed - might be a local loop */
    if (lsig->locked) {
        trace_dev(lsig->dev, "Mapping loop detected on signal %s! (2)\n", lsig->name);
        return;
    }

    /* Non-ephemeral signals cannot have a null value */
    RETURN_UNLESS(val || lsig->ephemeral)

    mpr_sig_update_timing_stats(lsig, diff);
    RETURN_UNLESS(evt & lsig->event_flags);
    RETURN_UNLESS((h = (mpr_sig_handler*)lsig->handler));
    h((mpr_sig)lsig, evt, lsig->use_inst ? inst : 0, val ? len : 0, lsig->type, val, time);
}

/**** Instances ****/

static mpr_sig_inst _reserved_inst(mpr_local_sig lsig, mpr_id *id)
{
    int i, j;
    mpr_sig_inst si;

    /* First we will try to find an inactive instance */
    for (i = 0; i < lsig->num_inst; i++) {
        si = lsig->inst[i];
        if (!si->active)
            goto done;
    }

    /* Otherwise if the signal is not ephemeral we will choose instances with local id_maps */
    RETURN_ARG_UNLESS(!lsig->ephemeral, 0);

    for (i = 0; i < lsig->num_inst; i++) {
        si = lsig->inst[i];
        for (j = 0; j < lsig->id_map_len; j++) {
            mpr_id_map id_map = lsig->id_maps[j].id_map;
            if (!id_map)
                goto done;
            if (lsig->id_maps[j].inst != si)
                continue;
            if (id_map->GID >> 32 != mpr_obj_get_id((mpr_obj)lsig->dev) >> 32)
                continue;
            /* locally claimed instance, allow replacing id_map */
            mpr_dev_LID_decref((mpr_local_dev)lsig->dev, lsig->group, id_map);
            lsig->id_maps[j].id_map = NULL;
            goto done;
        }
    }
    return 0;
done:
    if (id)
        si->id = *id;
    qsort(lsig->inst, lsig->num_inst, sizeof(mpr_sig_inst), _compare_inst_ids);
    return si;
}

int _oldest_inst(mpr_local_sig lsig)
{
    int i, oldest;
    mpr_sig_inst si;
    for (i = 0; i < lsig->id_map_len; i++) {
        if (lsig->id_maps[i].inst)
            break;
    }
    if (i == lsig->id_map_len) {
        /* no active instances to steal! */
        return -1;
    }
    oldest = i;
    for (i = oldest+1; i < lsig->id_map_len; i++) {
        if (!(si = lsig->id_maps[i].inst))
            continue;
        if ((si->created.sec < lsig->id_maps[oldest].inst->created.sec) ||
            (si->created.sec == lsig->id_maps[oldest].inst->created.sec &&
             si->created.frac < lsig->id_maps[oldest].inst->created.frac))
            oldest = i;
    }
    return oldest;
}

mpr_id mpr_sig_get_oldest_inst_id(mpr_sig sig)
{
    int idx;
    mpr_local_sig lsig = (mpr_local_sig)sig;
    RETURN_ARG_UNLESS(sig && sig->obj.is_local, 0);
    RETURN_ARG_UNLESS(sig->ephemeral, lsig->id_maps[0].id_map->LID);
    idx = _oldest_inst((mpr_local_sig)sig);
    return (idx >= 0) ? lsig->id_maps[idx].id_map->LID : 0;
}

int _newest_inst(mpr_local_sig lsig)
{
    int i, newest;
    mpr_sig_inst si;
    for (i = 0; i < lsig->id_map_len; i++) {
        if (lsig->id_maps[i].inst)
            break;
    }
    if (i == lsig->id_map_len) {
        /* no active instances to steal! */
        return -1;
    }
    newest = i;
    for (i = newest + 1; i < lsig->id_map_len; i++) {
        if (!(si = lsig->id_maps[i].inst))
            continue;
        if ((si->created.sec > lsig->id_maps[newest].inst->created.sec) ||
            (si->created.sec == lsig->id_maps[newest].inst->created.sec &&
             si->created.frac > lsig->id_maps[newest].inst->created.frac))
            newest = i;
    }
    return newest;
}

mpr_id mpr_sig_get_newest_inst_id(mpr_sig sig)
{
    int idx;
    mpr_local_sig lsig = (mpr_local_sig)sig;
    RETURN_ARG_UNLESS(sig && sig->obj.is_local, 0);
    RETURN_ARG_UNLESS(sig->ephemeral, lsig->id_maps[0].id_map->LID);
    idx = _newest_inst((mpr_local_sig)sig);
    return (idx >= 0) ? lsig->id_maps[idx].id_map->LID : 0;
}

int mpr_sig_get_id_map_with_LID(mpr_local_sig lsig, mpr_id LID, int flags, mpr_time t, int activate)
{
    mpr_sig_handler *h;
    mpr_sig_inst si;
    mpr_id_map id_map;
    int i;
    if (!lsig->use_inst)
        LID = MPR_DEFAULT_INST;
    h = (mpr_sig_handler*)lsig->handler;
    for (i = 0; i < lsig->id_map_len; i++) {
        mpr_sig_id_map sig_id_map = &lsig->id_maps[i];
        if (sig_id_map->inst && sig_id_map->id_map && sig_id_map->id_map->LID == LID)
            return (sig_id_map->status & ~flags) ? -1 : i;
    }
    RETURN_ARG_UNLESS(activate, -1);

    /* check if device has record of id map */
    id_map = mpr_dev_get_id_map_by_LID((mpr_local_dev)lsig->dev, lsig->group, LID);

    /* No instance with that id exists - need to try to activate instance and
     * create new id map if necessary. */
    if ((si = _find_inst_by_id(lsig, LID)) || (si = _reserved_inst(lsig, &LID))) {
        if (!id_map) {
            /* Claim id map locally */
            id_map = mpr_dev_add_id_map((mpr_local_dev)lsig->dev, lsig->group, LID, 0);
        }
        else
            mpr_dev_LID_incref((mpr_local_dev)lsig->dev, id_map);

        /* store pointer to device map in a new signal map */
        i = _init_and_add_id_map(lsig, si, id_map);
        if (h && lsig->ephemeral && (lsig->event_flags & MPR_SIG_INST_NEW))
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
          lsig->id_maps[i].id_map->LID, 0, lsig->type, 0, t);
    }
    else if (lsig->steal_mode == MPR_STEAL_NEWEST) {
        i = _newest_inst(lsig);
        if (i < 0)
            return -1;
        h((mpr_sig)lsig, MPR_SIG_REL_UPSTRM & lsig->event_flags ? MPR_SIG_REL_UPSTRM : MPR_SIG_UPDATE,
          lsig->id_maps[i].id_map->LID, 0, lsig->type, 0, t);
    }
    else
        return -1;

    /* try again */
    if ((si = _find_inst_by_id(lsig, LID)) || (si = _reserved_inst(lsig, &LID))) {
        if (!id_map) {
            /* Claim id map locally */
            id_map = mpr_dev_add_id_map((mpr_local_dev)lsig->dev, lsig->group, LID, 0);
        }
        else
            mpr_dev_LID_incref((mpr_local_dev)lsig->dev, id_map);
        i = _init_and_add_id_map(lsig, si, id_map);
        if (h && lsig->ephemeral && (lsig->event_flags & MPR_SIG_INST_NEW))
            h((mpr_sig)lsig, MPR_SIG_INST_NEW, LID, 0, lsig->type, NULL, t);
        return i;
    }
    return -1;
}

int mpr_sig_get_id_map_with_GID(mpr_local_sig lsig, mpr_id GID, int flags, mpr_time t, int activate)
{
    mpr_sig_handler *h;
    mpr_sig_inst si;
    mpr_id_map id_map;
    int i;
    h = (mpr_sig_handler*)lsig->handler;
    for (i = 0; i < lsig->id_map_len; i++) {
        mpr_sig_id_map sig_id_map = &lsig->id_maps[i];
        if (sig_id_map->id_map && sig_id_map->id_map->GID == GID)
            return (sig_id_map->status & ~flags) ? -1 : i;
    }
    RETURN_ARG_UNLESS(activate, -1);

    /* check if the device already has a map for this global id */
    id_map = mpr_dev_get_id_map_by_GID((mpr_local_dev)lsig->dev, lsig->group, GID);
    if (!id_map) {
        /* Here we still risk creating conflicting maps if two signals are updated asynchronously.
         * This is easy to avoid by not allowing a local id to be used with multiple active remote
         * maps, however users may wish to create devices with multiple object classes which do not
         * require mutual instance id synchronization - e.g. instance 1 of object class A is not
         * related to instance 1 of object B. This problem should be solved by adopting
         * hierarchical object structure. */
        if ((si = _reserved_inst(lsig, NULL))) {
            id_map = mpr_dev_add_id_map((mpr_local_dev)lsig->dev, lsig->group, si->id, GID);
            id_map->GID_refcount = 1;
            i = _init_and_add_id_map(lsig, si, id_map);
            if (h && lsig->ephemeral && (lsig->event_flags & MPR_SIG_INST_NEW))
                h((mpr_sig)lsig, MPR_SIG_INST_NEW, si->id, 0, lsig->type, NULL, t);
            return i;
        }
    }
    else if ((si = _find_inst_by_id(lsig, id_map->LID)) || (si = _reserved_inst(lsig, &id_map->LID))) {
        if (!si->active) {
            i = _init_and_add_id_map(lsig, si, id_map);
            mpr_dev_LID_incref((mpr_local_dev)lsig->dev, id_map);
            mpr_dev_GID_incref((mpr_local_dev)lsig->dev, id_map);
            if (h && lsig->ephemeral && (lsig->event_flags & MPR_SIG_INST_NEW))
                h((mpr_sig)lsig, MPR_SIG_INST_NEW, si->id, 0, lsig->type, NULL, t);
            return i;
        }
    }
    else {
        /* TODO: Once signal groups are explicit, allow re-mapping to
         * another instance if possible. */
        trace("Signal %s has no instance %"PR_MPR_ID" available.\n", lsig->name, id_map->LID);
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
          lsig->id_maps[i].id_map->LID, 0, lsig->type, 0, t);
    }
    else if (lsig->steal_mode == MPR_STEAL_NEWEST) {
        i = _newest_inst(lsig);
        if (i < 0)
            return -1;
        h((mpr_sig)lsig, MPR_SIG_REL_UPSTRM & lsig->event_flags ? MPR_SIG_REL_UPSTRM : MPR_SIG_UPDATE,
          lsig->id_maps[i].id_map->LID, 0, lsig->type, 0, t);
    }
    else
        return -1;

    /* try again */
    if (!id_map) {
        if ((si = _reserved_inst(lsig, NULL))) {
            id_map = mpr_dev_add_id_map((mpr_local_dev)lsig->dev, lsig->group, si->id, GID);
            id_map->GID_refcount = 1;
            i = _init_and_add_id_map(lsig, si, id_map);
            if (h && lsig->ephemeral && (lsig->event_flags & MPR_SIG_INST_NEW))
                h((mpr_sig)lsig, MPR_SIG_INST_NEW, si->id, 0, lsig->type, NULL, t);
            return i;
        }
    }
    else {
        si = _find_inst_by_id(lsig, id_map->LID);
        TRACE_RETURN_UNLESS(si && !si->active, -1, "Signal %s has no instance %"
                            PR_MPR_ID" available.", lsig->name, id_map->LID);
        i = _init_and_add_id_map(lsig, si, id_map);
        mpr_dev_LID_incref((mpr_local_dev)lsig->dev, id_map);
        mpr_dev_GID_incref((mpr_local_dev)lsig->dev, id_map);
        if (h && lsig->ephemeral && (lsig->event_flags & MPR_SIG_INST_NEW))
            h((mpr_sig)lsig, MPR_SIG_INST_NEW, si->id, 0, lsig->type, NULL, t);
        return i;
    }
    return -1;
}

static int _reserve_inst(mpr_local_sig lsig, mpr_id *id, void *data)
{
    int i, cont;
    mpr_sig_inst si;
    RETURN_ARG_UNLESS(lsig->num_inst < MAX_INST, -1);

    /* check if instance with this id already exists! If so, stop here. */
    if (id && _find_inst_by_id(lsig, *id))
        return -1;

    /* reallocate array of instances */
    lsig->inst = realloc(lsig->inst, sizeof(mpr_sig_inst) * (lsig->num_inst + 1));
    lsig->inst[lsig->num_inst] = (mpr_sig_inst) calloc(1, sizeof(struct _mpr_sig_inst));
    si = lsig->inst[lsig->num_inst];
    si->val = calloc(1, get_vector_size((mpr_sig)lsig));
    si->has_val_flags = calloc(1, lsig->len / 8 + 1);
    si->has_val = 0;
    si->active = 0;

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
    si->data = data;

    ++lsig->num_inst;
    qsort(lsig->inst, lsig->num_inst, sizeof(mpr_sig_inst), _compare_inst_ids);
    return lsig->num_inst - 1;
}

int mpr_sig_reserve_inst(mpr_sig sig, int num, mpr_id *ids, void **data)
{
    int i = 0, count = 0, highest = -1, result, old_num = sig->num_inst;
    mpr_local_sig lsig = (mpr_local_sig)sig;
    RETURN_ARG_UNLESS(sig && sig->obj.is_local && num, 0);
    sig->use_inst = 1;

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
        mpr_rtr_num_inst_changed(mpr_net_get_rtr(mpr_graph_get_net(lsig->obj.graph)),
                                 lsig, highest + 1);

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
    int id_map_idx;
    RETURN_ARG_UNLESS(sig && sig->obj.is_local, 0);
    RETURN_ARG_UNLESS(sig->ephemeral, 1);

    id_map_idx = mpr_sig_get_id_map_with_LID((mpr_local_sig)sig, id, 0, MPR_NOW, 0);
    return (id_map_idx >= 0) ? ((mpr_local_sig)sig)->id_maps[id_map_idx].inst->active : 0;
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

static void _mpr_remote_sig_set_value(mpr_sig sig, int len, mpr_type type, const void *val)
{
    mpr_dev dev;
    int port, i;
    const char *host;
    char port_str[10];
    lo_message msg = NULL;
    lo_address addr = NULL;
    const void *coerced = val;

    /* find destination IP and port */
    dev = sig->dev;
    host = mpr_obj_get_prop_as_str((mpr_obj)dev, MPR_PROP_HOST, NULL);
    port = mpr_obj_get_prop_as_int32((mpr_obj)dev, MPR_PROP_PORT, NULL);
    RETURN_UNLESS(host && port);

    if (!(msg = lo_message_new()))
        return;

    if (type != sig->type || len < sig->len) {
        coerced = alloca(mpr_type_get_size(sig->type) * sig->len);
        mpr_set_coerced(len, type, val, sig->len, sig->type, (void*)coerced);
    }
    switch (sig->type) {
        case MPR_INT32:
            for (i = 0; i < sig->len; i++)
                lo_message_add_int32(msg, ((int*)coerced)[i]);
            break;
        case MPR_FLT:
            for (i = 0; i < sig->len; i++)
                lo_message_add_float(msg, ((float*)coerced)[i]);
            break;
        case MPR_DBL:
            for (i = 0; i < sig->len; i++)
                lo_message_add_double(msg, ((double*)coerced)[i]);
            break;
    }

    snprintf(port_str, 10, "%d", port);
    if (!(addr = lo_address_new(host, port_str)))
        goto done;
    lo_send_message(addr, sig->path, msg);

done:
    FUNC_IF(lo_message_free, msg);
    FUNC_IF(lo_address_free, addr);
}

void mpr_sig_set_value(mpr_sig sig, mpr_id id, int len, mpr_type type, const void *val)
{
    mpr_time time;
    int id_map_idx;
    mpr_local_sig lsig = (mpr_local_sig)sig;
    mpr_sig_inst si;
    RETURN_UNLESS(sig);
    if (!sig->obj.is_local) {
        _mpr_remote_sig_set_value(sig, len, type, val);
        return;
    }
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
    id_map_idx = mpr_sig_get_id_map_with_LID(lsig, id, 0, time, 1);
    RETURN_UNLESS(id_map_idx >= 0);
    si = lsig->id_maps[id_map_idx].inst;

    /* update time */
    mpr_sig_update_timing_stats(lsig, si->has_val ? mpr_time_get_diff(time, si->time) : 0);
    memcpy(&si->time, &time, sizeof(mpr_time));

    /* update value */
    if (type != lsig->type || len < lsig->len)
        mpr_set_coerced(lsig->len, type, val, lsig->len, lsig->type, si->val);
    else
        memcpy(si->val, (void*)val, get_vector_size(sig));
    si->has_val = 1;

    /* mark instance as updated */
    mpr_bitflags_set(lsig->updated_inst, si->idx);
    lsig->updated = 1;
    mpr_local_dev_set_sending(lsig->dev);

    mpr_rtr_process_sig(mpr_net_get_rtr(mpr_graph_get_net(lsig->obj.graph)), lsig,
                        id_map_idx, si->has_val ? si->val : 0, si->time);
}

void mpr_sig_release_inst(mpr_sig sig, mpr_id id)
{
    int id_map_idx;
    RETURN_UNLESS(sig && sig->obj.is_local && sig->ephemeral);
    id_map_idx = mpr_sig_get_id_map_with_LID((mpr_local_sig)sig, id, RELEASED_REMOTELY, MPR_NOW, 0);
    if (id_map_idx >= 0)
        mpr_sig_release_inst_internal((mpr_local_sig)sig, id_map_idx);
}

void mpr_sig_release_inst_internal(mpr_local_sig lsig, int id_map_idx)
{
    mpr_sig_id_map smap = &lsig->id_maps[id_map_idx];
    RETURN_UNLESS(smap->inst);

    /* mark instance as updated */
    mpr_bitflags_set(lsig->updated_inst, smap->inst->idx);
    lsig->updated = 1;
    mpr_local_dev_set_sending(lsig->dev);

    mpr_rtr_process_sig(mpr_net_get_rtr(mpr_graph_get_net(lsig->obj.graph)),
                        lsig, id_map_idx, 0, smap->inst->time);

    if (smap->id_map && mpr_dev_LID_decref((mpr_local_dev)lsig->dev, lsig->group, smap->id_map)) {
        smap->id_map = 0;
    }
    else if ((lsig->dir & MPR_DIR_OUT) || smap->status & RELEASED_REMOTELY) {
        /* TODO: consider multiple upstream source instances? */
        smap->id_map = 0;
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
    RETURN_UNLESS(sig && sig->obj.is_local && sig->use_inst);
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
    mpr_rtr_remove_inst(mpr_net_get_rtr(mpr_graph_get_net(lsig->obj.graph)), lsig, remove_idx);

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
    RETURN_ARG_UNLESS(sig && sig->obj.is_local, 0);

    if (!lsig->use_inst)
        si = lsig->id_maps[0].inst;
    else {
        int id_map_idx = mpr_sig_get_id_map_with_LID(lsig, id, RELEASED_REMOTELY, MPR_NOW, 0);
        RETURN_ARG_UNLESS(id_map_idx >= 0, 0);
        si = lsig->id_maps[id_map_idx].inst;
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
    RETURN_ARG_UNLESS(sig, 0);
    RETURN_ARG_UNLESS(sig->obj.is_local && sig->ephemeral, sig->num_inst);
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
    RETURN_ARG_UNLESS(sig && sig->obj.is_local && sig->use_inst, 0);
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
    int id_map_idx;
    mpr_time time;
    RETURN_ARG_UNLESS(sig && sig->obj.is_local && sig->use_inst, 0);
    time = mpr_dev_get_time(sig->dev);
    id_map_idx = mpr_sig_get_id_map_with_LID((mpr_local_sig)sig, id, 0, time, 1);
    return id_map_idx >= 0;
}

void mpr_sig_set_inst_data(mpr_sig sig, mpr_id id, const void *data)
{
    mpr_sig_inst si;
    RETURN_UNLESS(sig && sig->obj.is_local && sig->use_inst);
    si = _find_inst_by_id((mpr_local_sig)sig, id);
    if (si)
        si->data = (void*)data;
}

void *mpr_sig_get_inst_data(mpr_sig sig, mpr_id id)
{
    mpr_sig_inst si;
    RETURN_ARG_UNLESS(sig && sig->obj.is_local && sig->use_inst, 0);
    si = _find_inst_by_id((mpr_local_sig)sig, id);
    return si ? si->data : 0;
}

/**** Queries ****/

void mpr_sig_set_cb(mpr_sig sig, mpr_sig_handler *h, int events)
{
    mpr_local_sig lsig = (mpr_local_sig)sig;
    RETURN_UNLESS(sig && sig->obj.is_local);
    if (!lsig->handler && h && events) {
        /* Need to register a new liblo methods */
        mpr_local_dev_add_server_method((mpr_local_dev)sig->dev, lsig->path,
                                        mpr_sig_lo_handler, (void*)lsig);
    }
    else if (lsig->handler && !(h || events)) {
        /* Need to remove liblo methods */
        mpr_local_dev_remove_server_method((mpr_local_dev)sig->dev, lsig->path);
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
    return sig ? sig->dev : NULL;
}

static int cmp_qry_sig_maps(const void *context_data, mpr_map map)
{
    mpr_sig sig = *(mpr_sig*)context_data;
    int dir = *(int*)((char*)context_data + sizeof(mpr_sig*));
    if (!dir || (dir & MPR_DIR_OUT)) {
        int i;
        for (i = 0; i < map->num_src; i++) {
            if (mpr_slot_get_sig(map->src[i]) == sig)
                return 1;
        }
    }
    if (!dir || (dir & MPR_DIR_IN)) {
        if (mpr_slot_get_sig(map->dst) == sig)
            return 1;
    }
    return 0;
}

mpr_list mpr_sig_get_maps(mpr_sig sig, mpr_dir dir)
{
    RETURN_ARG_UNLESS(sig, 0);
    return mpr_graph_new_query(sig->obj.graph, 1, MPR_MAP, (void*)cmp_qry_sig_maps, "vi", &sig, dir);
}

static int _init_and_add_id_map(mpr_local_sig lsig, mpr_sig_inst si, mpr_id_map id_map)
{
    int i;
    if (!si->active) {
        si->active = 1;
        si->has_val = 0;
        mpr_time_set(&si->created, MPR_NOW);
        mpr_time_set(&si->time, si->created);
    }

    /* find unused signal map */
    for (i = 0; i < lsig->id_map_len; i++) {
        if (!lsig->id_maps[i].id_map)
            break;
    }
    if (i == lsig->id_map_len) {
        /* need more memory */
        if (lsig->id_map_len >= MAX_INST) {
            /* Arbitrary limit to number of tracked id_maps */
            /* TODO: add checks for this return value */
            return -1;
        }
        lsig->id_map_len = lsig->id_map_len ? lsig->id_map_len * 2 : 1;
        lsig->id_maps = realloc(lsig->id_maps, (lsig->id_map_len * sizeof(struct _mpr_sig_id_map)));
        memset(lsig->id_maps + i, 0, ((lsig->id_map_len - i) * sizeof(struct _mpr_sig_id_map)));
    }
    lsig->id_maps[i].id_map = id_map;
    lsig->id_maps[i].inst = si;
    lsig->id_maps[i].status = 0;
    return i;
}

void mpr_sig_send_state(mpr_sig sig, net_msg_t cmd)
{
    char str[BUFFSIZE];
    lo_message msg;
    mpr_net net;
    RETURN_UNLESS(sig);
    msg = lo_message_new();
    RETURN_UNLESS(msg);
    net = mpr_graph_get_net(sig->obj.graph);

    if (cmd == MSG_SIG_MOD) {
        lo_message_add_string(msg, sig->name);

        /* properties */
        mpr_obj_add_props_to_msg((mpr_obj)sig, msg);

        snprintf(str, BUFFSIZE, "/%s/signal/modify", mpr_dev_get_name(sig->dev));
        mpr_net_add_msg(net, str, 0, msg);
        /* send immediately since path string is not cached */
        mpr_net_send(net);
    }
    else {
        RETURN_UNLESS(mpr_sig_full_name(sig, str, BUFFSIZE));
        lo_message_add_string(msg, str);

        /* properties */
        mpr_obj_add_props_to_msg((mpr_obj)sig, msg);
        mpr_net_add_msg(net, 0, cmd, msg);
    }
}

void mpr_sig_send_removed(mpr_local_sig lsig)
{
    char sig_name[BUFFSIZE];
    NEW_LO_MSG(msg, return);
    RETURN_UNLESS(mpr_sig_full_name((mpr_sig)lsig, sig_name, BUFFSIZE));
    lo_message_add_string(msg, sig_name);
    mpr_net_add_msg(mpr_graph_get_net(lsig->obj.graph), 0, MSG_SIG_REM, msg);
}

/*! Update information about a signal record based on message properties. */
int mpr_sig_set_from_msg(mpr_sig sig, mpr_msg msg)
{
    mpr_msg_atom a;
    int i, updated = 0, prop;
    mpr_tbl tbl = sig->obj.props.synced;
    const mpr_type *types;
    lo_arg **vals;
    RETURN_ARG_UNLESS(msg, 0);

    for (i = 0; i < mpr_msg_get_num_atoms(msg); i++) {
        a = mpr_msg_get_atom(msg, i);
        prop = mpr_msg_atom_get_prop(a);
        if (sig->obj.is_local && (MASK_PROP_BITFLAGS(prop) != PROP(EXTRA)))
            continue;
        types = mpr_msg_atom_get_types(a);
        vals = mpr_msg_atom_get_values(a);
        switch (prop) {
            case PROP(DIR): {
                int dir = 0;
                if (!mpr_type_get_is_str(types[0]))
                    break;
                if (strcmp(&(*vals)->s, "output")==0)
                    dir = MPR_DIR_OUT;
                else if (strcmp(&(*vals)->s, "input")==0)
                    dir = MPR_DIR_IN;
                else
                    break;
                updated += mpr_tbl_add_record(tbl, PROP(DIR), NULL, 1, MPR_INT32, &dir, REMOTE_MODIFY);
                break;
            }
            case PROP(ID):
                if (types[0] == 'h') {
                    if (sig->obj.id != (vals[0])->i64) {
                        sig->obj.id = (vals[0])->i64;
                        ++updated;
                    }
                }
                break;
            case PROP(STEAL_MODE): {
                int stl;
                if (!mpr_type_get_is_str(types[0]))
                    break;
                if (strcmp(&(*vals)->s, "none")==0)
                    stl = MPR_STEAL_NONE;
                else if (strcmp(&(*vals)->s, "oldest")==0)
                    stl = MPR_STEAL_OLDEST;
                else if (strcmp(&(*vals)->s, "newest")==0)
                    stl = MPR_STEAL_NEWEST;
                else
                    break;
                updated += mpr_tbl_add_record(tbl, PROP(STEAL_MODE), NULL, 1,
                                              MPR_INT32, &stl, REMOTE_MODIFY);
                break;
            }
            default:
                updated += mpr_tbl_add_record_from_msg_atom(tbl, a, REMOTE_MODIFY);
                break;
        }
    }
    return updated;
}

void mpr_local_sig_set_updated(mpr_local_sig sig, int inst_idx)
{
    mpr_bitflags_set(sig->updated_inst, inst_idx);
    mpr_local_dev_set_sending(sig->dev);
    sig->updated = 1;
}
