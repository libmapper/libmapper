#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <lo/lo.h>

#include "mapper_internal.h"
#include "types_internal.h"
#include <mapper/mapper.h>

static int _is_map_in_scope(mpr_local_map map, mpr_id id)
{
    int i;
    id &= 0xFFFFFFFF00000000; /* interested in device hash part only */
    for (i = 0; i < map->num_scopes; i++) {
        if (map->scopes[i] == 0 || map->scopes[i]->obj.id == id)
            return 1;
    }
    return 0;
}

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
        if (slot)
            mpr_map_alloc_values(slot->map);
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
            dev = rs->sig->dev;
        for (i = 0; i < rs->num_slots; i++) {
            if (!rs->slots[i] || rs->slots[i]->map->status < MPR_STATUS_ACTIVE)
                continue;
            if (MPR_DIR_IN == rs->slots[i]->dir)
                ++sig_maps_in;
            else if (MPR_DIR_OUT == rs->slots[i]->dir)
                ++sig_maps_out;
        }
        rs->sig->num_maps_in = sig_maps_in;
        rs->sig->num_maps_out = sig_maps_out;
        dev_maps_in += sig_maps_in;
        dev_maps_out += sig_maps_out;
        rs = rs->next;
    }
    dev->num_maps_in = dev_maps_in;
    dev->num_maps_out = dev_maps_out;
}

void mpr_rtr_process_sig(mpr_rtr rtr, mpr_local_sig sig, int idmap_idx, const void *val, mpr_time t)
{
    mpr_id_map idmap;
    lo_message msg;
    mpr_rtr_sig rs;
    mpr_local_map map;
    int i, j, inst_idx;
    uint8_t bundle_idx, *lock;

    /* abort if signal is already being processed - might be a local loop */
    if (sig->locked) {
        trace_dev(rtr->dev, "Mapping loop detected on signal %s! (1)\n", sig->name);
        return;
    }
    idmap = sig->idmaps[idmap_idx].map;

    /* find the router signal */
    rs = _find_rtr_sig(rtr, sig);
    RETURN_UNLESS(rs);

    inst_idx = sig->idmaps[idmap_idx].inst->idx;
    bundle_idx = rtr->dev->bundle_idx % NUM_BUNDLES;
    /* TODO: remove duplicate flag set */
    rtr->dev->sending = 1; /* mark as updated */
    lock = &sig->locked;
    *lock = 1;

    if (!val) {
        mpr_local_slot slot, dst_slot;

        for (i = 0; i < rs->num_slots; i++) {
            int in_scope;
            if (!rs->slots[i])
                continue;

            slot = rs->slots[i];
            map = slot->map;

            if (map->status < MPR_STATUS_ACTIVE)
                continue;

            dst_slot = map->dst;
            in_scope = _is_map_in_scope(map, idmap->GID);

            /* send release to upstream */
            for (j = 0; j < map->num_src; j++) {
                slot = map->src[j];
                if (!slot->sig->use_inst)
                    continue;

                /* reset associated input memory */
                mpr_value_reset_inst(&slot->val, inst_idx);

                if (!in_scope)
                    continue;

                if (sig->idmaps[idmap_idx].status & RELEASED_REMOTELY)
                    continue;

                if (slot->dir == MPR_DIR_IN) {
                    msg = mpr_map_build_msg(map, slot, 0, 0, idmap);
                    mpr_link_add_msg(slot->link, slot->sig, msg, t, map->protocol, bundle_idx);
                }
            }

            if (!map->use_inst)
                continue;

            /* reset associated output memory */
            mpr_value_reset_inst(&dst_slot->val, inst_idx);

            /* send release to downstream */
            if (slot->dir == MPR_DIR_OUT) {
                msg = 0;
                if (in_scope) {
                    msg = mpr_map_build_msg(map, slot, 0, 0, idmap);
                    mpr_link_add_msg(dst_slot->link, dst_slot->sig, msg, t, map->protocol,
                                     bundle_idx);
                }
            }
        }
        *lock = 0;
        return;
    }
    for (i = 0; i < rs->num_slots; i++) {
        mpr_local_slot slot;
        struct _mpr_sig_idmap *idmaps;
        int in_scope, all;
        if (!rs->slots[i])
            continue;

        slot = rs->slots[i];
        map = slot->map;

        if (map->status < MPR_STATUS_ACTIVE)
            continue;

        in_scope = _is_map_in_scope(map, sig->idmaps[idmap_idx].map->GID);
        /* TODO: should we continue for out-of-scope local destination updates? */
        if (map->use_inst && !in_scope)
            continue;

        if (slot->dir == MPR_DIR_IN)
            continue;

        /* If this signal is non-instanced but the map has other instanced
         * sources we will need to update all of the active map instances. */
        all = (!sig->use_inst && map->num_src > 1 && map->num_inst > 1);

        if (MPR_LOC_DST == map->process_loc) {
            /* bypass map processing and bundle value without type coercion */
            char *types = alloca(sig->len * sizeof(char));
            memset(types, sig->type, sig->len);
            msg = mpr_map_build_msg(map, slot, val, types, sig->use_inst ? idmap : 0);
            mpr_link_add_msg(map->dst->link, map->dst->sig, msg, t, map->protocol, bundle_idx);
            continue;
        }

        /* copy input value */
        mpr_value_set_samp(&slot->val, inst_idx, (void*)val, t);

        if (!slot->causes_update)
            continue;

        if (all) {
            /* find a source signal with more instances */
            for (j = 0; j < map->num_src; j++)
                if (map->src[j]->sig->is_local && map->src[j]->num_inst > slot->num_inst)
                    sig = (mpr_local_sig)map->src[j]->sig;
            idmap_idx = 0;
        }

        idmaps = sig->idmaps;
        for (; idmap_idx < sig->idmap_len; idmap_idx++) {
            /* check if map instance is active */
            if ((all || sig->use_inst) && !idmaps[idmap_idx].inst)
                continue;
            inst_idx = idmaps[idmap_idx].inst->idx;
            set_bitflag(map->updated_inst, inst_idx);
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
    for (i = rs->num_slots+1; i < rs->num_slots * 2; i++)
        rs->slots[i] = 0;
    i = rs->num_slots;
    rs->num_slots *= 2;
    return i;
}

static mpr_id _get_unused_map_id(mpr_local_dev dev, mpr_rtr rtr)
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
                if (!rs->slots[i])
                    continue;
                if (rs->slots[i]->map->obj.id == id) {
                    done = 0;
                    break;
                }
            }
            rs = rs->next;
        }
    }
    return id;
}

static void _add_local_slot(mpr_rtr rtr, mpr_local_slot slot, int is_src, int *max_inst,
                            int *use_inst)
{
    slot->dir = (is_src ^ (slot->sig->is_local ? 1 : 0)) ? MPR_DIR_IN : MPR_DIR_OUT;
    if (slot->sig->is_local) {
        slot->rsig = _add_rtr_sig(rtr, (mpr_local_sig)slot->sig);
        _store_slot(slot->rsig, slot);

        if (slot->sig->num_inst > *max_inst)
            *max_inst = slot->sig->num_inst;
        if (slot->num_inst > *max_inst)
            *max_inst = slot->num_inst;
        if (slot->sig->use_inst)
            *use_inst = 1;
    }
    if (!slot->sig->is_local || (is_src && slot->map->dst->sig->is_local))
        slot->link = mpr_link_new(rtr->dev, slot->sig->dev);

    /* set some sensible defaults */
    slot->causes_update = 1;
}

void mpr_rtr_add_map(mpr_rtr rtr, mpr_local_map map)
{
    int i, local_src = 0, local_dst, max_num_inst = 0, use_inst = 0, scope_count;
    RETURN_UNLESS(!map->is_local);
    for (i = 0; i < map->num_src; i++) {
        if (map->src[i]->sig->is_local)
            ++local_src;
    }
    local_dst = map->dst->sig->is_local ? 1 : 0;
    map->rtr = rtr;
    map->is_local = 1;

    /* TODO: configure number of instances available for each slot */
    map->num_inst = 0;

    /* Add local slot structures */
    for (i = 0; i < map->num_src; i++)
        _add_local_slot(rtr, map->src[i], 1, &max_num_inst, &use_inst);
    _add_local_slot(rtr, map->dst, 0, &max_num_inst, &use_inst);

    /* default to using instanced maps if any of the contributing signals are instanced */
    map->use_inst = use_inst;
    map->protocol = use_inst ? MPR_PROTO_TCP : MPR_PROTO_UDP;

    /* assign a unique id to this map if we are the destination */
    if (local_dst)
        map->obj.id = _get_unused_map_id(rtr->dev, rtr);

    /* assign indices to source slots */
    if (local_dst) {
        for (i = 0; i < map->num_src; i++)
            map->src[i]->id = map->dst->rsig->id_counter++;
    }
    else {
        /* may be overwritten later by message */
        for (i = 0; i < map->num_src; i++)
            map->src[i]->id = i;
    }

    /* add scopes */
    scope_count = 0;
    map->num_scopes = map->num_src;
    map->scopes = (mpr_dev *) malloc(sizeof(mpr_dev) * map->num_scopes);

    for (i = 0; i < map->num_src; i++) {
        /* check that scope has not already been added */
        int j, found = 0;
        for (j = 0; j < scope_count; j++) {
            if (map->scopes[j] == map->src[i]->sig->dev) {
                found = 1;
                break;
            }
        }
        if (!found) {
            map->scopes[scope_count] = map->src[i]->sig->dev;
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
        if (map->src[i]->link != map->src[0]->link) {
            map->one_src = 0;
            break;
        }
    }

    /* default to processing at source device unless heterogeneous sources */
    map->process_loc = (map->is_local_only || map->one_src) ? MPR_LOC_SRC : MPR_LOC_DST;

    if (local_dst && (local_src == map->num_src)) {
        /* all reference signals are local */
        map->is_local_only = 1;
        map->dst->link = map->src[0]->link;
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
            map = rs->slots[i]->map;
            if (map->dst->link == link) {
                mpr_rtr_remove_map(rtr, map);
                continue;
            }
            for (j = 0; j < map->num_src; j++) {
                if (map->src[j]->link == link) {
                    mpr_rtr_remove_map(rtr, map);
                    break;
                }
            }
        }
        rs = rs->next;
    }
}

void mpr_rtr_remove_sig(mpr_rtr rtr, mpr_rtr_sig rs)
{
    if (rtr && rs) {
        /* No maps remaining – we can remove the rtr_sig also */
        mpr_rtr_sig *rstemp = &rtr->sigs;
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
}

int mpr_rtr_remove_map(mpr_rtr rtr, mpr_local_map map)
{
    /* do not free local names since they point to signal's copy */

    int i, j;
    mpr_time t;
    RETURN_ARG_UNLESS(map, 1);
    mpr_time_set(&t, MPR_NOW);

    if (map->idmap) {
        /* release map-generated instances */
        if (map->dst->rsig) {
            lo_message msg = mpr_map_build_msg(map, 0, 0, 0, map->idmap);
            mpr_dev_bundle_start(t, NULL);
            mpr_dev_handler(NULL, lo_message_get_types(msg), lo_message_get_argv(msg),
                            lo_message_get_argc(msg), msg, (void*)map->dst->sig);
        }
        else
            mpr_dev_LID_decref(rtr->dev, 0, map->idmap);
    }

    /* remove map and slots from rtr_sig lists if necessary */
    if (map->dst->rsig) {
        mpr_rtr_sig rs = map->dst->rsig;
        /* release instances if necessary */
        /* TODO: determine whether instances were activated through this map */
        /* need to trigger "remote" release */
        mpr_local_sig sig = rs->sig;
        mpr_sig_idmap_t *maps = sig->idmaps;
        for (i = 0; i < sig->idmap_len; i++) {
            if (!maps[i].map)
                continue;
            if (maps[i].status & RELEASED_LOCALLY) {
                mpr_dev_GID_decref(rtr->dev, sig->group, maps[i].map);
                maps[i].map = 0;
            }
            else {
                maps[i].status |= RELEASED_REMOTELY;
                mpr_dev_GID_decref(rtr->dev, sig->group, maps[i].map);
                if (sig->use_inst) {
                    int evt = (  MPR_SIG_REL_UPSTRM & sig->event_flags
                               ? MPR_SIG_REL_UPSTRM : MPR_SIG_UPDATE);
                    mpr_sig_call_handler(sig, evt, maps[i].map->LID, 0, 0, &t, 0);
                }
                else {
                    mpr_dev_LID_decref(rtr->dev, sig->group, maps[i].map);
                    maps[i].map = 0;
                    maps[i].inst->active = 0;
                    maps[i].inst = 0;
                }
            }
        }

        for (i = 0; i < rs->num_slots; i++) {
            if (rs->slots[i] == map->dst) {
                rs->slots[i] = 0;
                break;
            }
        }
    }
    else if (map->dst->link) {
        mpr_link_remove_map(map->dst->link, map);
        _check_link(rtr, map->dst->link);
    }
    mpr_slot_free_value(map->dst);
    for (i = 0; i < map->num_src; i++) {
        if (map->src[i]->rsig) {
            mpr_rtr_sig rs = map->src[i]->rsig;
            for (j = 0; j < rs->num_slots; j++) {
                if (rs->slots[j] == map->src[i])
                    rs->slots[j] = 0;
            }
        }
        else if (map->src[i]->link) {
            mpr_link_remove_map(map->src[i]->link, map);
            _check_link(rtr, map->src[i]->link);
        }
        mpr_slot_free_value(map->src[i]);
    }

    /* one more case: if map is local only need to decrement num_maps in local map */
    if (map->is_local_only) {
        mpr_link link = mpr_dev_get_link_by_remote(rtr->dev, (mpr_dev)rtr->dev);
        if (link)
            --link->num_maps[0];
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

int mpr_rtr_loop_check(mpr_rtr rtr, mpr_local_sig sig, int num_remotes, const char **remotes)
{
    int i, j;
    mpr_local_map map;
    mpr_local_slot slot;
    mpr_rtr_sig rs = _find_rtr_sig(rtr, sig);
    RETURN_ARG_UNLESS(rs, 0);
    for (i = 0; i < rs->num_slots; i++) {
        if (!rs->slots[i] || rs->slots[i]->dir == MPR_DIR_IN)
            continue;
        slot = rs->slots[i];
        map = slot->map;

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
    /* only interested in incoming slots */
    mpr_rtr_sig rs = _find_rtr_sig(rtr, sig);
    RETURN_ARG_UNLESS(rs, NULL);
    for (i = 0; i < rs->num_slots; i++) {
        if (!rs->slots[i] || sig->dir != rs->slots[i]->dir)
            continue;
        map = rs->slots[i]->map;
        /* check incoming slots for this map */
        for (j = 0; j < map->num_src; j++) {
            if ((int)map->src[j]->id == slot_id)
                return map->src[j];
        }
    }
    return NULL;
}
