#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <lo/lo.h>

#include "bitflags.h"
#include "device.h"
#include "expression.h"
#include "map.h"
#include "mpr_signal.h"
#include "object.h"
#include "router.h"
#include "slot.h"
#include "value.h"

#include <mapper/mapper.h>

#ifdef _MSC_VER
#include <malloc.h>
#endif

/*! The rtr_sig is a linked list containing a signal and a list of mapping
 *  slots.  TODO: This should be replaced with a more efficient approach
 *  such as a hash table or search tree. */
typedef struct _mpr_rtr_sig {
    struct _mpr_rtr_sig *next;      /*!< The next rtr_sig in the list. */
    struct _mpr_local_sig *sig;     /*!< The associated signal. */
    mpr_local_slot *slots;
    int num_slots;
    int id_counter;
} mpr_rtr_sig_t;

/*! The router structure. */
typedef struct _mpr_rtr {
    mpr_net net;
    /* TODO: rtr should either be stored in local_dev or shared */
    struct _mpr_local_dev *dev;     /*!< The device associated with this link. */
    mpr_rtr_sig sigs;               /*!< The list of mappings for each signal. */
    mpr_expr_stack expr_stack;
} mpr_rtr_t;

mpr_rtr mpr_rtr_new(mpr_net net)
{
    mpr_rtr rtr = (mpr_rtr) calloc(1, sizeof(mpr_rtr_t));
    rtr->net = net;
    rtr->expr_stack = mpr_expr_stack_new();
    return rtr;
}

void mpr_rtr_free(mpr_rtr rtr)
{
    FUNC_IF(mpr_expr_stack_free, rtr->expr_stack);
    free(rtr);
}

static int _is_map_in_scope(mpr_local_map map, mpr_id id)
{
    int i;
    id &= 0xFFFFFFFF00000000; /* interested in device hash part only */
    for (i = 0; i < map->num_scopes; i++) {
        if (map->scopes[i] == 0 || mpr_obj_get_id((mpr_obj)map->scopes[i]) == id)
            return 1;
    }
    return 0;
}

/* TODO: integrate router_sig struct with local_sig struct, eliminate this lookup */
static mpr_rtr_sig _find_rtr_sig(mpr_rtr rtr, mpr_local_sig sig)
{
    mpr_rtr_sig rs = rtr->sigs;
    while (rs && rs->sig != sig)
        rs = rs->next;
    return rs;
}

void mpr_rtr_remove_inst(mpr_rtr rtr, mpr_local_sig sig, int inst_idx) {
    int i;
    mpr_rtr_sig rs = _find_rtr_sig(rtr, sig);
    RETURN_UNLESS(rs);
    for (i = 0; i < rs->num_slots; i++)
        mpr_slot_remove_inst(rs->slots[i], inst_idx);
}

/* TODO: check for mismatched instance counts when using multiple sources */
void mpr_rtr_num_inst_changed(mpr_rtr rtr, mpr_local_sig sig, int size)
{
    int i;
    /* check if we have a reference to this signal */
    mpr_rtr_sig rs = _find_rtr_sig(rtr, sig);
    RETURN_UNLESS(rs);

    /* for array of slots, may need to reallocate destination instances */
    for (i = 0; i < rs->num_slots; i++) {
        mpr_local_slot slot = rs->slots[i];
        mpr_local_map map;
        if (!slot)
            continue;
        map = (mpr_local_map)mpr_slot_get_map((mpr_slot)slot);
        mpr_map_alloc_values(map);

        if (MPR_DIR_OUT == mpr_slot_get_dir((mpr_slot)map->dst)) {
            /* Inform remote destination */
            mpr_net_use_mesh(rtr->net, mpr_link_get_admin_addr(mpr_slot_get_link((mpr_slot)map->dst)));
            mpr_map_send_state((mpr_map)map, -1, MSG_MAPPED);
        }
        else {
            /* Inform remote sources */
            for (i = 0; i < map->num_src; i++) {
                mpr_net_use_mesh(rtr->net, mpr_link_get_admin_addr(mpr_slot_get_link((mpr_slot)map->src[i])));
                i = mpr_map_send_state((mpr_map)map, ((mpr_local_map)map)->one_src ? -1 : i,
                                       MSG_MAPPED);
            }
        }
    }
}

static void _update_map_count(mpr_rtr rtr)
{
    /* find associated rtr_sig */
    mpr_rtr_sig rs = rtr->sigs;
    int i, dev_maps_in = 0, dev_maps_out = 0;
    mpr_local_dev dev = 0;
    while (rs) {
        int sig_maps_in = 0, sig_maps_out = 0;
        if (!dev)
            dev = (mpr_local_dev)mpr_sig_get_dev((mpr_sig)rs->sig);
        for (i = 0; i < rs->num_slots; i++) {
            mpr_map map;
            mpr_dir dir;
            if (!rs->slots[i])
                continue;
            map = mpr_slot_get_map((mpr_slot)rs->slots[i]);
            if (map->status < MPR_STATUS_ACTIVE)
                continue;
            dir = mpr_slot_get_dir((mpr_slot)rs->slots[i]);
            if (MPR_DIR_IN == dir)
                ++sig_maps_in;
            else if (MPR_DIR_OUT == dir)
                ++sig_maps_out;
        }
        mpr_sig_set_num_maps((mpr_sig)rs->sig, sig_maps_in, sig_maps_out);
        dev_maps_in += sig_maps_in;
        dev_maps_out += sig_maps_out;
        rs = rs->next;
    }
    RETURN_UNLESS(dev);
    mpr_dev_set_num_maps((mpr_dev)dev, dev_maps_in, dev_maps_out);
}

void mpr_rtr_process_sig(mpr_rtr rtr, mpr_local_sig sig, int id_map_idx, const void *val, mpr_time t)
{
    mpr_id_map id_map;
    lo_message msg;
    mpr_rtr_sig rs;
    mpr_sig_inst si;
    mpr_local_map map;
    int i, j, inst_idx;
    uint8_t bundle_idx, *lock = mpr_local_sig_get_lock(sig);

    /* abort if signal is already being processed - might be a local loop */
    if (*lock) {
        trace_dev(rtr->dev, "Mapping loop detected on signal %s! (1)\n",
                  mpr_sig_get_name((mpr_sig)sig));
        return;
    }

    /* find the router signal */
    rs = _find_rtr_sig(rtr, sig);
    RETURN_UNLESS(rs);

    si = mpr_local_sig_get_inst_by_id_map_idx(sig, id_map_idx, &id_map);
    inst_idx = mpr_sig_inst_get_idx(si);
    bundle_idx = mpr_local_dev_get_bundle_idx(rtr->dev);
    /* TODO: remove duplicate flag set */
    mpr_local_dev_set_sending(rtr->dev); /* mark as updated */
    *lock = 1;

    if (!val) {
        mpr_local_slot slot, dst_slot;

        for (i = 0; i < rs->num_slots; i++) {
            int in_scope;
            if (!rs->slots[i])
                continue;

            slot = rs->slots[i];
            map = (mpr_local_map)mpr_slot_get_map((mpr_slot)slot);

            if (map->status < MPR_STATUS_ACTIVE)
                continue;

            dst_slot = map->dst;
            in_scope = _is_map_in_scope(map, id_map->GID);

            /* send release to upstream */
            for (j = 0; j < map->num_src; j++) {
                mpr_sig slot_sig;
                slot = map->src[j];
                slot_sig = mpr_slot_get_sig((mpr_slot)slot);
                if (!mpr_sig_get_use_inst(slot_sig))
                    continue;

                /* reset associated input memory */
                mpr_slot_reset_inst(slot, inst_idx);

                if (!in_scope)
                    continue;

                if (mpr_local_sig_get_id_map_status(sig, id_map_idx) & RELEASED_REMOTELY)
                    continue;

                if (MPR_DIR_IN == mpr_slot_get_dir((mpr_slot)slot)) {
                    msg = mpr_map_build_msg(map, slot, 0, 0, id_map);
                    mpr_slot_send_msg(slot, msg, t, map->protocol, bundle_idx);
                }
            }

            if (!map->use_inst)
                continue;

            /* reset associated output memory */
            mpr_slot_reset_inst(dst_slot, inst_idx);

            /* send release to downstream */
            if (MPR_DIR_OUT == mpr_slot_get_dir((mpr_slot)slot)) {
                msg = 0;
                if (in_scope) {
                    msg = mpr_map_build_msg(map, slot, 0, 0, id_map);
                    mpr_slot_send_msg(dst_slot, msg, t, map->protocol, bundle_idx);
                }
            }
        }
        *lock = 0;
        return;
    }
    for (i = 0; i < rs->num_slots; i++) {
        mpr_local_slot slot;
        int in_scope, all;
        if (!rs->slots[i])
            continue;

        slot = rs->slots[i];
        if (MPR_DIR_IN == mpr_slot_get_dir((mpr_slot)slot))
            continue;

        map = (mpr_local_map)mpr_slot_get_map((mpr_slot)slot);
        if (map->status < MPR_STATUS_ACTIVE)
            continue;

        in_scope = _is_map_in_scope(map, id_map->GID);
        /* TODO: should we continue for out-of-scope local destination updates? */
        if (map->use_inst && !in_scope)
            continue;

        /* If this signal is non-instanced but the map has other instanced
         * sources we will need to update all of the active map instances. */
        all = (map->num_src > 1 && map->num_inst > mpr_local_sig_get_num_inst(sig));

        if (MPR_LOC_DST == map->process_loc) {
            /* bypass map processing and bundle value without type coercion */
            int len = mpr_sig_get_len((mpr_sig)sig);
            char *types = alloca(len * sizeof(char));
            memset(types, mpr_sig_get_type((mpr_sig)sig), len);
            msg = mpr_map_build_msg(map, slot, val, types,
                                    mpr_sig_get_use_inst((mpr_sig)sig) ? id_map : 0);
            mpr_slot_send_msg(map->dst, msg, t, map->protocol, bundle_idx);
            continue;
        }

        if (!map->expr) {
            trace("error: missing expression!\n");
            continue;
        }

        /* copy input value */
        mpr_slot_set_value(slot, inst_idx, (void*)val, t);

        if (!mpr_slot_get_causes_update((mpr_slot)slot))
            continue;

        if (all) {
            /* find a source signal with more instances */
            for (j = 0; j < map->num_src; j++) {
                mpr_sig src_sig = mpr_slot_get_sig((mpr_slot)map->src[j]);
                if (   mpr_obj_get_is_local((mpr_obj)src_sig)
                    && mpr_slot_get_num_inst((mpr_slot)map->src[j]) > mpr_slot_get_num_inst((mpr_slot)slot))
                    sig = (mpr_local_sig)src_sig;
            }
            id_map_idx = 0;
        }

        for (; id_map_idx < mpr_local_sig_get_num_id_maps(sig); id_map_idx++) {
            /* check if map instance is active */
            mpr_sig_inst si = mpr_local_sig_get_inst_by_id_map_idx(sig, id_map_idx, NULL);
            if (!si && (all || mpr_sig_get_use_inst((mpr_sig)sig)))
                continue;
            inst_idx = mpr_sig_inst_get_idx(si);
            mpr_bitflags_set(map->updated_inst, inst_idx);
            map->updated = 1;
            if (!all)
                break;
        }
    }
    *lock = 0;
}

static mpr_rtr_sig _add_rtr_sig(mpr_rtr rtr, mpr_local_sig sig)
{
    /* find signal in rtr_sig list */
    mpr_rtr_sig rs = _find_rtr_sig(rtr, sig);

    /* if not found, create a new list entry */
    if (!rs) {
        rs = (mpr_rtr_sig)calloc(1, sizeof(struct _mpr_rtr_sig));
        rs->sig = sig;
        rs->num_slots = 1;
        rs->slots = malloc(sizeof(mpr_local_slot));
        rs->slots[0] = 0;
        rs->next = rtr->sigs;
        rtr->sigs = rs;
    }
    return rs;
}

static int _store_slot(mpr_rtr_sig rs, mpr_local_slot slot)
{
    int i;
    for (i = 0; i < rs->num_slots; i++) {
        if (!rs->slots[i]) {
            /* store pointer at empty index */
            rs->slots[i] = slot;
            return i;
        }
    }
    /* all indices occupied, allocate more */
    rs->slots = realloc(rs->slots, sizeof(mpr_local_slot) * rs->num_slots * 2);
    rs->slots[rs->num_slots] = slot;
    for (i = rs->num_slots + 1; i < rs->num_slots * 2; i++)
        rs->slots[i] = 0;
    i = rs->num_slots;
    rs->num_slots *= 2;
    return i;
}

static mpr_id _get_unused_map_id(mpr_dev dev, mpr_rtr rtr)
{
    int i, done = 0;
    mpr_id id;
    mpr_rtr_sig rs;
    while (!done) {
        done = 1;
        id = mpr_dev_generate_unique_id((mpr_dev)dev);
        /* check if map exists with this id */
        rs = rtr->sigs;
        while (rs) {
            for (i = 0; i < rs->num_slots; i++) {
                mpr_map map;
                if (!rs->slots[i])
                    continue;
                map = mpr_slot_get_map((mpr_slot)rs->slots[i]);
                if (map->obj.id == id) {
                    done = 0;
                    break;
                }
            }
            rs = rs->next;
        }
    }
    return id;
}

static void _add_local_slot(mpr_rtr rtr, mpr_local_slot slot, int is_src, int *use_inst)
{
    mpr_local_sig sig = (mpr_local_sig)mpr_slot_get_sig((mpr_slot)slot);
    mpr_map map = mpr_slot_get_map((mpr_slot)slot);
    int is_local = mpr_obj_get_is_local((mpr_obj)sig);
    mpr_slot_set_dir((mpr_slot)slot, (is_src ^ (is_local ? 1 : 0)) ? MPR_DIR_IN : MPR_DIR_OUT);
    if (is_local) {
        mpr_rtr_sig rs = _add_rtr_sig(rtr, sig);
        mpr_slot_set_rtr_sig(slot, rs);
        _store_slot(rs, slot);

        if (mpr_sig_get_use_inst((mpr_sig)sig))
            *use_inst = 1;
    }
    if (   !is_local
        || (is_src && mpr_obj_get_is_local((mpr_obj)mpr_slot_get_sig((mpr_slot)map->dst))))
        mpr_slot_set_link((mpr_slot)slot, mpr_link_new(rtr->dev, mpr_sig_get_dev((mpr_sig)sig)));

    /* set some sensible defaults */
    mpr_slot_set_causes_update((mpr_slot)slot, 1);
}

/* The obj.is_local flag is used to keep track of whether the map has been added to the router.
 * TODO: consider looking up map in router instead. */
void mpr_rtr_add_map(mpr_rtr rtr, mpr_local_map map)
{
    int i, local_src = 0, local_dst, use_inst = 0, scope_count;
    /* try just checking if map->rtr == NULL? */
    RETURN_UNLESS(!mpr_obj_get_is_local((mpr_obj)map));
    for (i = 0; i < map->num_src; i++) {
        if (mpr_obj_get_is_local((mpr_obj)mpr_slot_get_sig((mpr_slot)map->src[i])))
            ++local_src;
    }
    local_dst = mpr_obj_get_is_local((mpr_obj)mpr_slot_get_sig((mpr_slot)map->dst));
    map->rtr = rtr;
    mpr_obj_set_is_local((mpr_obj)map, 1);

    /* TODO: configure number of instances available for each slot */
    map->num_inst = 0;

    /* Add local slot structures */
    for (i = 0; i < map->num_src; i++)
        _add_local_slot(rtr, map->src[i], 1, &use_inst);
    _add_local_slot(rtr, map->dst, 0, &use_inst);

    /* default to using instanced maps if any of the contributing signals are instanced */
    map->use_inst = use_inst;
    map->protocol = use_inst ? MPR_PROTO_TCP : MPR_PROTO_UDP;

    /* assign a unique id to this map if we are the destination */
    if (local_dst)
        map->obj.id = _get_unused_map_id(mpr_sig_get_dev(mpr_slot_get_sig((mpr_slot)map->dst)), rtr);

    /* assign indices to source slots */
    if (local_dst) {
        for (i = 0; i < map->num_src; i++) {
            mpr_rtr_sig rsig = mpr_slot_get_rtr_sig(map->dst);
            mpr_slot_set_id((mpr_slot)map->src[i], rsig->id_counter++);
        }
    }
    else {
        /* may be overwritten later by message */
        for (i = 0; i < map->num_src; i++)
            mpr_slot_set_id((mpr_slot)map->src[i], i);
    }

    /* add scopes */
    scope_count = 0;
    map->num_scopes = map->num_src;
    map->scopes = (mpr_dev *) malloc(sizeof(mpr_dev) * map->num_scopes);

    for (i = 0; i < map->num_src; i++) {
        /* check that scope has not already been added */
        int j, found = 0;
        mpr_sig sig = mpr_slot_get_sig((mpr_slot)map->src[i]);
        mpr_dev dev = mpr_sig_get_dev(sig);
        for (j = 0; j < scope_count; j++) {
            if (map->scopes[j] == dev) {
                found = 1;
                break;
            }
        }
        if (!found) {
            map->scopes[scope_count] = dev;
            ++scope_count;
        }
    }

    if (scope_count != map->num_src) {
        map->num_scopes = scope_count;
        map->scopes = realloc(map->scopes, sizeof(mpr_dev) * scope_count);
    }

    /* check if all sources belong to same remote device */
    map->one_src = 1;
    for (i = 1; i < map->num_src; i++) {
        if (mpr_slot_get_link((mpr_slot)map->src[i]) != mpr_slot_get_link((mpr_slot)map->src[0])) {
            map->one_src = 0;
            break;
        }
    }

    /* default to processing at source device unless heterogeneous sources */
    map->process_loc = (map->is_local_only || map->one_src) ? MPR_LOC_SRC : MPR_LOC_DST;

    if (local_dst && (local_src == map->num_src)) {
        /* all reference signals are local */
        map->is_local_only = 1;
        mpr_slot_set_link((mpr_slot)map->dst, mpr_slot_get_link((mpr_slot)map->src[0]));
    }

    _update_map_count(rtr);
}

static void _check_link(mpr_rtr rtr, mpr_link link)
{
    /* We could remove link the link here if it has no associated maps, however
     * under normal usage it is likely that users will add new maps after
     * deleting the old ones. If we remove the link immediately it will have to
     * be re-established in this scenario, so we will allow the house-keeping
     * routines to clean up empty links after the link ping timeout period. */
}

void mpr_rtr_remove_link(mpr_rtr rtr, mpr_link link)
{
    int i, j;
    mpr_local_map map;
    /* check if any maps use this link */
    mpr_rtr_sig rs = rtr->sigs;
    while (rs) {
        for (i = 0; i < rs->num_slots; i++) {
            if (!rs->slots[i])
                continue;
            map = (mpr_local_map)mpr_slot_get_map((mpr_slot)rs->slots[i]);
            if (mpr_slot_get_link((mpr_slot)map->dst) == link) {
                mpr_rtr_remove_map(rtr, map);
                continue;
            }
            for (j = 0; j < map->num_src; j++) {
                if (mpr_slot_get_link((mpr_slot)map->src[j]) == link) {
                    mpr_rtr_remove_map(rtr, map);
                    break;
                }
            }
        }
        rs = rs->next;
    }
}

void mpr_rtr_remove_sig_internal(mpr_rtr rtr, mpr_rtr_sig rs)
{
    if (rtr && rs) {

    }
}

void mpr_rtr_remove_sig(mpr_rtr rtr, mpr_local_sig sig)
{
    int i;
    mpr_rtr_sig *rstemp, rs = rtr->sigs;

    while (rs && rs->sig != sig)
        rs = rs->next;
    if (!rs)
        return;

    /* need to unmap */
    for (i = 0; i < rs->num_slots; i++) {
        mpr_local_map map;
        if (!rs->slots[i])
            continue;
        map = (mpr_local_map)mpr_slot_get_map((mpr_slot)rs->slots[i]);
        mpr_map_release((mpr_map)map);
        mpr_rtr_remove_map(rtr, map);
    }

    rstemp = &rtr->sigs;
    while (*rstemp) {
        if (*rstemp == rs) {
            *rstemp = rs->next;
            free(rs->slots);
            free(rs);
            break;
        }
        rstemp = &(*rstemp)->next;
    }
}

int mpr_rtr_remove_map(mpr_rtr rtr, mpr_local_map map)
{
    /* do not free local names since they point to signal's copy */

    int i, j;
    mpr_time t;
    mpr_link link;
    mpr_rtr_sig rs = mpr_slot_get_rtr_sig(map->dst);
    RETURN_ARG_UNLESS(map, 1);
    mpr_time_set(&t, MPR_NOW);

    if (map->id_map) {
        /* release map-generated instances */
        if (rs) {
            lo_message msg = mpr_map_build_msg(map, 0, 0, 0, map->id_map);
            mpr_net_set_bundle_time(rtr->net, t);
            mpr_sig_lo_handler(NULL, lo_message_get_types(msg), lo_message_get_argv(msg),
                               lo_message_get_argc(msg), msg,
                               (void*)mpr_slot_get_sig((mpr_slot)map->dst));
            lo_message_free(msg);
        }
        if (MPR_DIR_OUT == mpr_slot_get_dir((mpr_slot)map->dst) || map->is_local_only)
            mpr_dev_LID_decref(rtr->dev, 0, map->id_map);
    }

    /* remove map and slots from rtr_sig lists if necessary */
    if (rs) {
        mpr_local_sig_release_map_inst(rs->sig, t);
        for (i = 0; i < rs->num_slots; i++) {
            if (rs->slots[i] == map->dst) {
                rs->slots[i] = 0;
                break;
            }
        }
    }
    else if ((link = mpr_slot_get_link((mpr_slot)map->dst))) {
        mpr_link_remove_map(link, map);
        _check_link(rtr, link);
    }
    mpr_slot_free_value(map->dst);
    for (i = 0; i < map->num_src; i++) {
        mpr_rtr_sig rs = mpr_slot_get_rtr_sig(map->src[i]);
        if (rs) {
            for (j = 0; j < rs->num_slots; j++) {
                if (rs->slots[j] == map->src[i])
                    rs->slots[j] = 0;
            }
        }
        else if ((link = mpr_slot_get_link((mpr_slot)map->src[i]))) {
            mpr_link_remove_map(link, map);
            _check_link(rtr, link);
        }
        mpr_slot_free_value(map->src[i]);
    }

    /* one more case: if map is local only need to decrement num_maps in local map */
    if (map->is_local_only) {
        mpr_link link = mpr_dev_get_link_by_remote(rtr->dev, (mpr_dev)rtr->dev);
        if (link)
            mpr_link_remove_map(link, map);
    }

    /* free buffers associated with user-defined expression variables */
    if (map->vars) {
        for (i = 0; i < map->num_vars; i++) {
            mpr_value_free(&map->vars[i]);
            free((void*)map->var_names[i]);
        }
        free(map->vars);
        free(map->var_names);
    }

    FUNC_IF(free, map->updated_inst);
    FUNC_IF(mpr_expr_free, map->expr);
    _update_map_count(rtr);
    return 0;
}

/* Check if there is already a map from a local signal to any of a list of remote signals. */
int mpr_rtr_loop_check(mpr_rtr rtr, mpr_local_sig sig, int num_remotes, const char **remotes)
{
    int i, j;
    mpr_local_map map;
    mpr_local_slot slot;
    mpr_rtr_sig rs = _find_rtr_sig(rtr, sig);
    RETURN_ARG_UNLESS(rs, 0);
    for (i = 0; i < rs->num_slots; i++) {
        if (!rs->slots[i] || MPR_DIR_IN == mpr_slot_get_dir((mpr_slot)rs->slots[i]))
            continue;
        slot = rs->slots[i];
        map = (mpr_local_map)mpr_slot_get_map((mpr_slot)slot);

        /* check destination */
        for (j = 0; j < num_remotes; j++) {
            if (!mpr_slot_match_full_name((mpr_slot)map->dst, remotes[j]))
                return 1;
        }
    }
    return 0;
}

/* TODO: speed this up with sorted slots and binary search */
mpr_local_slot mpr_rtr_get_slot(mpr_rtr rtr, mpr_local_sig sig, int slot_id)
{
    int i, j;
    mpr_local_map map;
    mpr_rtr_sig rs = _find_rtr_sig(rtr, sig);
    RETURN_ARG_UNLESS(rs, NULL);
    for (i = 0; i < rs->num_slots; i++) {
        /* Check if signal direction matches the slot direction. This handles both 'incoming'
         * destination slots (for processing map updates) and outgoing source slots (for
         * processing 'downstream instance release' events). */
        if (!rs->slots[i] || mpr_sig_get_dir((mpr_sig)sig) != mpr_slot_get_dir((mpr_slot)rs->slots[i]))
            continue;
        map = (mpr_local_map)mpr_slot_get_map((mpr_slot)rs->slots[i]);
        /* check incoming slots for this map */
        for (j = 0; j < map->num_src; j++) {
            if ((int)mpr_slot_get_id((mpr_slot)map->src[j]) == slot_id)
                return map->src[j];
        }
    }
    return NULL;
}

void mpr_rtr_check_links(mpr_rtr rtr, mpr_list links)
{
    mpr_rtr_sig rtr_sig = rtr->sigs;
    while (rtr_sig) {
        int i;
        for (i = 0; i < rtr_sig->num_slots; i++) {
            mpr_local_map map;
            mpr_local_slot slot = rtr_sig->slots[i];
            if (!slot)
                continue;
            map = (mpr_local_map)mpr_slot_get_map((mpr_slot)slot);
            if (MPR_DIR_OUT == mpr_slot_get_dir((mpr_slot)slot)) {
                mpr_list cpy;
                /* only send /mapTo once even if we have multiple local sources */
                if (map->one_src && (slot != map->src[0]))
                    continue;
                cpy = mpr_list_get_cpy(links);
                while (cpy) {
                    mpr_link link = (mpr_link)*cpy;
                    cpy = mpr_list_get_next(cpy);
                    if (   mpr_obj_get_is_local((mpr_obj)link)
                        && mpr_slot_get_link((mpr_slot)map->dst) == link) {
                        int j;
                        mpr_net_use_mesh(rtr->net, mpr_link_get_admin_addr(link));
                        for (j = 0; j < map->num_src; j++) {
                            mpr_sig sig = mpr_slot_get_sig((mpr_slot)map->src[j]);
                            if (!mpr_obj_get_is_local((mpr_obj)sig))
                                continue;
                            mpr_sig_send_state(sig, MSG_SIG);
                        }
                        mpr_map_send_state((mpr_map)map, -1, MSG_MAP_TO);
                    }
                }
            }
            else {
                mpr_list cpy = mpr_list_get_cpy(links);
                while (cpy) {
                    int j;
                    mpr_link link = (mpr_link)*cpy;
                    cpy = mpr_list_get_next(cpy);
                    if (!mpr_obj_get_is_local((mpr_obj)link))
                        continue;
                    for (j = 0; j < map->num_src; j++) {
                        if (mpr_slot_get_link((mpr_slot)map->src[j]) != link)
                            continue;
                        mpr_net_use_mesh(rtr->net, mpr_link_get_admin_addr(link));
                        mpr_sig_send_state(mpr_slot_get_sig((mpr_slot)map->dst), MSG_SIG);
                        j = mpr_map_send_state((mpr_map)map, map->one_src ? -1 : j, MSG_MAP_TO);
                    }
                }
            }
        }
        rtr_sig = rtr_sig->next;
    }
}

mpr_expr_stack mpr_rtr_get_expr_stack(mpr_rtr rtr)
{
    return rtr->expr_stack;
}

void mpr_rtr_call_local_handler(mpr_rtr rtr, const char *path, lo_message msg)
{
    /* need to look up signal by path */
    mpr_rtr_sig rs = rtr->sigs;
    while (rs) {
        if (0 == strcmp(path, mpr_sig_get_path((mpr_sig)rs->sig))) {
            mpr_sig_lo_handler(NULL, lo_message_get_types(msg), lo_message_get_argv(msg),
                               lo_message_get_argc(msg), msg, (void*)rs->sig);
            break;
        }
        rs = rs->next;
    }
}

/* temporary! */
void mpr_rtr_add_dev(mpr_rtr rtr, mpr_local_dev dev)
{
    rtr->dev = dev;
}
