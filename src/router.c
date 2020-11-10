#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <lo/lo.h>

#include "mapper_internal.h"
#include "types_internal.h"
#include <mapper/mapper.h>

static int _is_map_in_scope(mpr_map map, mpr_id id)
{
    int i;
    id &= 0xFFFFFFFF00000000; // interested in device hash part only
    for (i = 0; i < map->num_scopes; i++) {
        if (map->scopes[i] == 0 || map->scopes[i]->obj.id == id)
            return 1;
    }
    return 0;
}

static mpr_rtr_sig _find_rtr_sig(mpr_rtr rtr, mpr_sig sig)
{
    mpr_rtr_sig rs = rtr->sigs;
    while (rs && rs->sig != sig)
        rs = rs->next;
    return rs;
}

void mpr_rtr_remove_inst(mpr_rtr rtr, mpr_sig sig, int inst_idx) {
    int i;
    mpr_rtr_sig rs = _find_rtr_sig(rtr, sig);
    RETURN_UNLESS(rs);
    for (i = 0; i < rs->num_slots; i++)
        mpr_slot_remove_inst(rs->slots[i], inst_idx);
}

// TODO: check for mismatched instance counts when using multiple sources
void mpr_rtr_num_inst_changed(mpr_rtr rtr, mpr_sig sig, int size)
{
    int i;
    // check if we have a reference to this signal
    mpr_rtr_sig rs = _find_rtr_sig(rtr, sig);
    RETURN_UNLESS(rs);

    // for array of slots, may need to reallocate destination instances
    for (i = 0; i < rs->num_slots; i++) {
        mpr_slot slot = rs->slots[i];
        if (slot)
            mpr_map_alloc_values(slot->map);
    }
}

static void _update_map_count(mpr_rtr rtr)
{
    // find associated rtr_sig
    mpr_rtr_sig rs = rtr->sigs;
    int i, dev_maps_in = 0, dev_maps_out = 0;
    mpr_dev dev = 0;
    while (rs) {
        if (!dev)
            dev = rs->sig->dev;
        int sig_maps_in = 0, sig_maps_out = 0;
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

void mpr_rtr_process_sig(mpr_rtr rtr, mpr_sig sig, int idmap_idx, const void *val, mpr_time t)
{
    // abort if signal is already being processed - might be a local loop
    if (sig->loc->locked) {
        trace_dev(rtr->dev, "Mapping loop detected on signal %s! (1)\n", sig->name);
        return;
    }
    mpr_id_map idmap = sig->loc->idmaps[idmap_idx].map;
    lo_message msg;

    // find the router signal
    mpr_rtr_sig rs = _find_rtr_sig(rtr, sig);
    RETURN_UNLESS(rs);

    int i, j, inst_idx = sig->loc->idmaps[idmap_idx].inst->idx;
    uint8_t bundle_idx = rtr->dev->loc->bundle_idx % NUM_BUNDLES;
    rtr->dev->loc->updated = 1; // mark as updated
    mpr_map map;
    uint8_t *lock = &sig->loc->locked;
    *lock = 1;

    if (!val) {
        mpr_local_slot lslot;
        mpr_slot slot;

        for (i = 0; i < rs->num_slots; i++) {
            if (!rs->slots[i])
                continue;

            slot = rs->slots[i];
            map = slot->map;

            if (!map->use_inst || map->status < MPR_STATUS_ACTIVE)
                continue;

            mpr_slot dst_slot = map->dst;
            mpr_local_slot dst_lslot = dst_slot->loc;
            int in_scope = _is_map_in_scope(map, idmap->GID);

            // reset associated output memory
            mpr_value_reset_inst(&dst_lslot->val, inst_idx);

            // send release to downstream
            if (slot->dir == MPR_DIR_OUT) {
                msg = 0;
                if (!map->use_inst || in_scope) {
                    msg = mpr_map_build_msg(map, slot, 0, 0, idmap);
                    mpr_link_add_msg(dst_slot->link, dst_slot->sig, msg, t, map->protocol, bundle_idx);
                }
            }

            // send release to upstream
            for (j = 0; j < map->num_src; j++) {
                slot = map->src[j];
                if (!slot->sig->use_inst)
                    continue;
                lslot = slot->loc;

                // reset associated input memory
                mpr_value_reset_inst(&lslot->val, inst_idx);

                if (!in_scope)
                    continue;

                if (sig->loc->idmaps[idmap_idx].status & RELEASED_REMOTELY)
                    continue;

                if (slot->dir == MPR_DIR_IN) {
                    msg = mpr_map_build_msg(map, slot, 0, 0, idmap);
                    mpr_link_add_msg(slot->link, slot->sig, msg, t, map->protocol, bundle_idx);
                }
            }
        }
        *lock = 0;
        return;
    }
    for (i = 0; i < rs->num_slots; i++) {
        if (!rs->slots[i])
            continue;

        mpr_slot slot = rs->slots[i];
        map = slot->map;

        if (map->status < MPR_STATUS_ACTIVE)
            continue;

        int in_scope = _is_map_in_scope(map, sig->loc->idmaps[idmap_idx].map->GID);
        // TODO: should we continue for out-of-scope local destination updates?
        if (map->use_inst && !in_scope)
            continue;

        if (slot->dir == MPR_DIR_IN)
            continue;

        /* If this signal is non-instanced but the map has other instanced
         * sources we will need to update all of the active map instances. */
        int all = (!sig->use_inst && map->num_src > 1 && map->loc->num_inst > 1);

        if (MPR_LOC_DST == map->process_loc) {
            // bypass map processing and bundle value without type coercion
            char types[sig->len];
            memset(types, sig->type, sig->len);
            msg = mpr_map_build_msg(map, slot, val, types, sig->use_inst ? idmap : 0);
            mpr_link_add_msg(map->dst->link, map->dst->sig, msg, t, map->protocol, bundle_idx);
            continue;
        }

        // copy input value
        mpr_local_slot lslot = slot->loc;
        mpr_value_set_sample(&lslot->val, inst_idx, (void*)val, t);

        if (map->process_loc == MPR_LOC_SRC && !slot->causes_update)
            continue;

        mpr_slot dst_slot = map->dst;
        mpr_slot to = (map->process_loc == MPR_LOC_SRC ? dst_slot : slot);
        char src_types[slot->sig->len], dst_types[to->sig->len];
        memset(src_types, slot->sig->type, slot->sig->len);
        memset(dst_types, to->sig->type, to->sig->len);

        if (all) {
            // find a source signal with more instances
            for (j = 0; j < map->num_src; j++)
                if (map->src[j]->sig->loc && map->src[j]->num_inst > slot->num_inst)
                    sig = map->src[j]->sig;
            idmap_idx = 0;
        }

        int map_manages_inst = 0;
        if (!sig->use_inst) {
            if (mpr_expr_get_manages_inst(map->loc->expr)) {
                map_manages_inst = 1;
                idmap = map->idmap;
            }
            else
                idmap = 0;
        }

        struct _mpr_sig_idmap *idmaps = sig->loc->idmaps;
        for (; idmap_idx < sig->loc->idmap_len; idmap_idx++) {
            // check if map instance is active
            if ((all || sig->use_inst) && !idmaps[idmap_idx].inst)
                continue;
            inst_idx = idmaps[idmap_idx].inst->idx;
            int status = mpr_map_perform(map, dst_types, &t, inst_idx);
            if (!status) {
                // no updates or releases
                if (!all)
                    break;
                else
                    continue;
            }
            /* send instance release if dst is instanced and either src or map is also instanced. */
            if (idmap && status & EXPR_RELEASE_BEFORE_UPDATE && map->use_inst) {
                msg = mpr_map_build_msg(map, slot, 0, 0, sig->use_inst ? idmaps[idmap_idx].map : idmap);
                mpr_link_add_msg(dst_slot->link, dst_slot->sig, msg, t, map->protocol, bundle_idx);
                if (map_manages_inst) {
                    mpr_dev_LID_decref(rtr->dev, 0, idmap);
                    idmap = map->idmap = 0;
                }
            }
            if (status & EXPR_UPDATE) {
                // send instance update
                void *result = mpr_value_get_samp(&dst_slot->loc->val, inst_idx);
                if (map_manages_inst) {
                    if (!idmap) {
                        // create an id_map and store it in the map
                        mpr_id GID = mpr_dev_generate_unique_id(sig->dev);
                        idmap = map->idmap = mpr_dev_add_idmap(sig->dev, 0, 0, GID);
                    }
                    msg = mpr_map_build_msg(map, slot, result, dst_types, map->idmap);
                }
                else {
                    msg = mpr_map_build_msg(map, slot, result, dst_types, idmaps[idmap_idx].map);
                }
                mpr_link_add_msg(dst_slot->link, dst_slot->sig, msg,
                                 *(mpr_time*)mpr_value_get_time(&dst_slot->loc->val, inst_idx),
                                 map->protocol, bundle_idx);
            }
            /* send instance release if dst is instanced and either src or map
             * is also instanced. */
            if (idmap && status & EXPR_RELEASE_AFTER_UPDATE && map->use_inst) {
                msg = mpr_map_build_msg(map, slot, 0, 0, sig->use_inst ? idmaps[idmap_idx].map : idmap);
                mpr_link_add_msg(dst_slot->link, dst_slot->sig, msg, t, map->protocol, bundle_idx);
                if (map_manages_inst) {
                    mpr_dev_LID_decref(rtr->dev, 0, idmap);
                    idmap = map->idmap = 0;
                }
            }
            if (!all)
                break;
        }
    }
    *lock = 0;
}

static mpr_rtr_sig _add_rtr_sig(mpr_rtr rtr, mpr_sig sig)
{
    // find signal in rtr_sig list
    mpr_rtr_sig rs = _find_rtr_sig(rtr, sig);

    // if not found, create a new list entry
    if (!rs) {
        rs = (mpr_rtr_sig)calloc(1, sizeof(struct _mpr_rtr_sig));
        rs->sig = sig;
        rs->num_slots = 1;
        rs->slots = malloc(sizeof(mpr_slot));
        rs->slots[0] = 0;
        rs->next = rtr->sigs;
        rtr->sigs = rs;
    }
    return rs;
}

static int _store_slot(mpr_rtr_sig rs, mpr_slot slot)
{
    int i;
    for (i = 0; i < rs->num_slots; i++) {
        if (!rs->slots[i]) {
            // store pointer at empty index
            rs->slots[i] = slot;
            return i;
        }
    }
    // all indices occupied, allocate more
    rs->slots = realloc(rs->slots, sizeof(mpr_slot) * rs->num_slots * 2);
    rs->slots[rs->num_slots] = slot;
    for (i = rs->num_slots+1; i < rs->num_slots * 2; i++)
        rs->slots[i] = 0;
    i = rs->num_slots;
    rs->num_slots *= 2;
    return i;
}

static mpr_id _get_unused_map_id(mpr_dev dev, mpr_rtr rtr)
{
    int i, done = 0;
    mpr_id id;
    while (!done) {
        done = 1;
        id = mpr_dev_generate_unique_id(dev);
        // check if map exists with this id
        mpr_rtr_sig rs = rtr->sigs;
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

static void _add_local_slot(mpr_rtr rtr, mpr_slot slot, int is_src, int *max_inst, int *use_inst)
{
    slot->dir = (is_src ^ (slot->sig->loc ? 1 : 0)) ? MPR_DIR_IN : MPR_DIR_OUT;
    slot->loc = (mpr_local_slot)calloc(1, sizeof(struct _mpr_local_slot));
    if (slot->sig->loc) {
        slot->loc->rsig = _add_rtr_sig(rtr, slot->sig);
        _store_slot(slot->loc->rsig, slot);

        if (slot->sig->num_inst > *max_inst)
            *max_inst = slot->sig->num_inst;
        if (slot->num_inst > *max_inst)
            *max_inst = slot->num_inst;
        if (slot->sig->use_inst)
            *use_inst = 1;
    }
    if (!slot->sig->loc || (is_src && slot->map->dst->sig->loc))
        slot->link = mpr_link_new(rtr->dev, slot->sig->dev);

    // set some sensible defaults
    slot->causes_update = 1;
}

void mpr_rtr_add_map(mpr_rtr rtr, mpr_map map)
{
    RETURN_UNLESS(!map->loc);
    int i, local_src = 0, local_dst = map->dst->sig->loc ? 1 : 0;
    for (i = 0; i < map->num_src; i++) {
        if (map->src[i]->sig->loc)
            ++local_src;
    }

    mpr_local_map lmap = (mpr_local_map)calloc(1, sizeof(struct _mpr_local_map));
    map->loc = lmap;
    lmap->rtr = rtr;

    // TODO: configure number of instances available for each slot
    lmap->num_inst = 0;

    // Add local slot structures
    int max_num_inst = 0, use_inst = 0;
    for (i = 0; i < map->num_src; i++)
        _add_local_slot(rtr, map->src[i], 1, &max_num_inst, &use_inst);
    _add_local_slot(rtr, map->dst, 0, &max_num_inst, &use_inst);

    // default to using instanced maps if any of the contributing signals are instanced
    map->use_inst = use_inst;
    map->protocol = use_inst ? MPR_PROTO_TCP : MPR_PROTO_UDP;

    // assign a unique id to this map if we are the destination
    if (local_dst)
        map->obj.id = _get_unused_map_id(rtr->dev, rtr);

    /* assign indices to source slots */
    if (local_dst) {
        for (i = 0; i < map->num_src; i++)
            map->src[i]->obj.id = map->dst->loc->rsig->id_counter++;
    }
    else {
        /* may be overwritten later by message */
        for (i = 0; i < map->num_src; i++)
            map->src[i]->obj.id = i;
    }

    // add scopes
    int scope_count = 0;
    map->num_scopes = map->num_src;
    map->scopes = (mpr_dev *) malloc(sizeof(mpr_dev) * map->num_scopes);

    for (i = 0; i < map->num_src; i++) {
        // check that scope has not already been added
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

    // check if all sources belong to same remote device
    lmap->one_src = 1;
    for (i = 1; i < map->num_src; i++) {
        if (map->src[i]->link != map->src[0]->link) {
            lmap->one_src = 0;
            break;
        }
    }

    // default to processing at source device unless heterogeneous sources
    map->process_loc = (lmap->one_src) ? MPR_LOC_SRC : MPR_LOC_DST;

    if (local_dst && (local_src == map->num_src)) {
        // all reference signals are local
        lmap->is_local_only = 1;
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
    // check if any maps use this link
    mpr_rtr_sig rs = rtr->sigs;
    while (rs) {
        for (i = 0; i < rs->num_slots; i++) {
            if (!rs->slots[i])
                continue;
            mpr_map map = rs->slots[i]->map;
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
        // No maps remaining – we can remove the rtr_sig also
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

int mpr_rtr_remove_map(mpr_rtr rtr, mpr_map map)
{
    RETURN_UNLESS(map && map->loc, 1);
    // do not free local names since they point to signal's copy
    int i, j;
    mpr_time t;
    mpr_time_set(&t, MPR_NOW);

    if (map->idmap) {
        // release map-generated instances
        if (map->dst->loc->rsig) {
            lo_message msg = mpr_map_build_msg(map, 0, 0, 0, map->idmap);
            mpr_dev_bundle_start(t, NULL);
            mpr_dev_handler(NULL, lo_message_get_types(msg), lo_message_get_argv(msg),
                            lo_message_get_argc(msg), msg, (void*)map->dst->sig->loc);
        }
        else
            mpr_dev_LID_decref(rtr->dev, 0, map->idmap);
    }

    // remove map and slots from rtr_sig lists if necessary
    if (map->dst->loc->rsig) {
        mpr_rtr_sig rs = map->dst->loc->rsig;
        // release instances if necessary
        // TODO: determine whether instances were activated through this map
        // need to trigger "remote" release
        mpr_sig s = rs->sig;
        mpr_sig_idmap_t *maps = s->loc->idmaps;
        for (i = 0; i < s->loc->idmap_len; i++) {
            if (!maps[i].map)
                continue;
            if (maps[i].status & RELEASED_LOCALLY) {
                mpr_dev_GID_decref(rtr->dev, s->loc->group, maps[i].map);
                maps[i].map = 0;
            }
            else {
                maps[i].status |= RELEASED_REMOTELY;
                mpr_dev_GID_decref(rtr->dev, s->loc->group, maps[i].map);
                if (s->use_inst) {
                    int evt = (  MPR_SIG_REL_UPSTRM & s->loc->event_flags
                               ? MPR_SIG_REL_UPSTRM : MPR_SIG_UPDATE);
                    mpr_sig_call_handler(s, evt, maps[i].map->LID, 0, 0, &t, 0);
                }
                else {
                    mpr_dev_LID_decref(rtr->dev, s->loc->group, maps[i].map);
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
        if (map->src[i]->loc->rsig) {
            mpr_rtr_sig rs = map->src[i]->loc->rsig;
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

    // one more case: if map is local only need to decrement num_maps in local map
    if (map->loc->is_local_only) {
        mpr_link link = mpr_dev_get_link_by_remote(rtr->dev, rtr->dev);
        if (link)
            --link->num_maps[0];
    }

    // free buffers associated with user-defined expression variables
    if (map->loc->vars) {
        for (i = 0; i < map->loc->num_vars; i++) {
            mpr_value_free(&map->loc->vars[i]);
            free((void*)map->loc->var_names[i]);
        }
        free(map->loc->vars);
        free(map->loc->var_names);
    }
    FUNC_IF(mpr_expr_free, map->loc->expr);
    free(map->loc);
    _update_map_count(rtr);
    return 0;
}

int mpr_rtr_loop_check(mpr_rtr rtr, mpr_sig local_sig, int num_remotes,
                       const char **remotes)
{
    mpr_rtr_sig rs = _find_rtr_sig(rtr, local_sig);
    RETURN_UNLESS(rs, 0);
    int i, j;
    for (i = 0; i < rs->num_slots; i++) {
        if (!rs->slots[i] || rs->slots[i]->dir == MPR_DIR_IN)
            continue;
        mpr_slot slot = rs->slots[i];
        mpr_map map = slot->map;

        // check destination
        for (j = 0; j < num_remotes; j++) {
            if (!mpr_slot_match_full_name(map->dst, remotes[j]))
                return 1;
        }
    }
    return 0;
}

mpr_slot mpr_rtr_get_slot(mpr_rtr rtr, mpr_sig sig, int slot_id)
{
    // only interested in incoming slots
    mpr_rtr_sig rs = _find_rtr_sig(rtr, sig);
    RETURN_UNLESS(rs, NULL);

    int i, j;
    mpr_map map;
    for (i = 0; i < rs->num_slots; i++) {
        if (!rs->slots[i] || rs->slots[i]->dir == MPR_DIR_OUT)
            continue;
        map = rs->slots[i]->map;
        // check incoming slots for this map
        for (j = 0; j < map->num_src; j++) {
            if ((int)map->src[j]->obj.id == slot_id)
                return map->src[j];
        }
    }
    return NULL;
}
