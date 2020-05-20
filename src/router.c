#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <lo/lo.h>

#include "mpr_internal.h"
#include "types_internal.h"
#include <mpr/mpr.h>

static void send_or_bundle_msg(mpr_link link, mpr_sig dst, lo_message msg,
                               mpr_time t, mpr_proto proto);

static int map_in_scope(mpr_map map, mpr_id id)
{
    int i;
    id &= 0xFFFFFFFF00000000; // interested in device hash part only
    for (i = 0; i < map->num_scopes; i++) {
        if (map->scopes[i] == 0 || map->scopes[i]->obj.id == id)
            return 1;
    }
    return 0;
}

static void realloc_slot_insts(mpr_slot slot, int size)
{
    int i;
    if (slot->sig->use_inst && slot->num_inst < size) {
        slot->loc->hist = realloc(slot->loc->hist, sizeof(struct _mpr_hist) * size);
        for (i = slot->num_inst; i < size; i++) {
            slot->loc->hist[i].type = slot->sig->type;
            slot->loc->hist[i].len = slot->sig->len;
            slot->loc->hist[i].mem = slot->loc->mem;
            slot->loc->hist[i].val = calloc(1, mpr_type_get_size(slot->sig->type)
                                            * slot->loc->mem);
            slot->loc->hist[i].time = calloc(1, sizeof(mpr_time) * slot->loc->mem);
            slot->loc->hist[i].pos = -1;
        }
        slot->num_inst = size;
    }
}

static void realloc_map_insts(mpr_map map, int size)
{
    int i, j;

    // check if source histories need to be reallocated
    for (i = 0; i < map->num_src; i++)
        realloc_slot_insts(map->src[i], size);

    // check if destination histories need to be reallocated
    realloc_slot_insts(map->dst, size);

    // check if expression variable histories need to be reallocated
    mpr_local_map lmap = map->loc;
    if (size > lmap->num_var_inst) {
        lmap->expr_var = realloc(lmap->expr_var, sizeof(mpr_hist) * size);
        for (i = lmap->num_var_inst; i < size; i++) {
            lmap->expr_var[i] = malloc(sizeof(struct _mpr_hist)
                                       * lmap->num_expr_var);
            for (j = 0; j < lmap->num_expr_var; j++) {
                lmap->expr_var[i][j].type = lmap->expr_var[0][j].type;
                lmap->expr_var[i][j].len = lmap->expr_var[0][j].len;
                lmap->expr_var[i][j].mem = lmap->expr_var[0][j].mem;
                lmap->expr_var[i][j].pos = 0;
                lmap->expr_var[i][j].val = calloc(1, sizeof(double)
                                                  * lmap->expr_var[i][j].len
                                                  * lmap->expr_var[i][j].mem);
                lmap->expr_var[i][j].time = calloc(1, sizeof(mpr_time)
                                                   * lmap->expr_var[i][j].mem);
            }
        }
        lmap->num_var_inst = size;
    }
}

// TODO: check for mismatched instance counts when using multiple sources
void mpr_rtr_num_inst_changed(mpr_rtr rtr, mpr_sig sig, int size)
{
    int i;
    // check if we have a reference to this signal
    mpr_rtr_sig rs = rtr->sigs;
    while (rs) {
        if (rs->sig == sig)
            break;
        rs = rs->next;
    }
    RETURN_UNLESS(rs);

    // for array of slots, may need to reallocate destination instances
    for (i = 0; i < rs->num_slots; i++) {
        mpr_slot slot = rs->slots[i];
        if (slot)
            realloc_map_insts(slot->map, size);
    }
}

static void mpr_rtr_update_map_count(mpr_rtr rtr)
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

void mpr_rtr_process_sig(mpr_rtr rtr, mpr_sig sig, int inst, const void *val,
                         mpr_time t)
{
    // abort if signal is already being processed - might be a local loop
    if (sig->loc->locked) {
        trace_dev(rtr->dev, "Mapping loop detected on signal %s! (1)\n",
                  sig->name);
        return;
    }
    mpr_id_map idmap = sig->loc->idmaps[inst].map;
    lo_message msg;

    // find the router signal
    mpr_rtr_sig rs = rtr->sigs;
    while (rs) {
        if (rs->sig == sig)
            break;
        rs = rs->next;
    }
    RETURN_UNLESS(rs);

    int i, j, idx = sig->loc->idmaps[inst].inst->idx;
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

            if (map->status < MPR_STATUS_ACTIVE)
                continue;

            mpr_slot dst_slot = map->dst;
            mpr_local_slot dst_lslot = dst_slot->loc;
            int in_scope = map_in_scope(map, idmap->GID);

            // also need to reset associated output memory
            memset(dst_lslot->hist[idx].val, 0, dst_lslot->mem
                   * dst_slot->sig->len * mpr_type_get_size(dst_slot->sig->type));
            memset(dst_lslot->hist[idx].time, 0, dst_lslot->mem * sizeof(mpr_time));
            dst_lslot->hist[idx].pos = -1;

            // send release to downstream
            if (slot->dir == MPR_DIR_OUT
                && !(sig->loc->idmaps[inst].status & RELEASED_REMOTELY)) {
                msg = 0;
                if (!slot->use_inst)
                    msg = mpr_map_build_msg(map, slot, 0, 0, 0);
                else if (in_scope)
                    msg = mpr_map_build_msg(map, slot, 0, 0, idmap);
                if (msg)
                    send_or_bundle_msg(dst_slot->link, dst_slot->sig, msg, t,
                                       map->protocol);
            }

            // send release to upstream
            for (j = 0; j < map->num_src; j++) {
                slot = map->src[j];
                if (!slot->sig->use_inst)
                    continue;
                lslot = slot->loc;

                // also need to reset associated input memory
                memset(lslot->hist[idx].val, 0, lslot->mem
                       * slot->sig->len * mpr_type_get_size(slot->sig->type));
                memset(lslot->hist[idx].time, 0, lslot->mem * sizeof(mpr_time));
                lslot->hist[idx].pos = -1;

                if (!in_scope)
                    continue;

                if (sig->loc->idmaps[inst].status & RELEASED_REMOTELY)
                    continue;

                if (slot->dir == MPR_DIR_IN) {
                    msg = mpr_map_build_msg(map, slot, 0, 0, idmap);
                    if (msg)
                        send_or_bundle_msg(slot->link, slot->sig, msg, t,
                                           map->protocol);
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

        int in_scope = map_in_scope(map, sig->loc->idmaps[inst].map->GID);
        // TODO: should we continue for out-of-scope local destination updates?
        if (slot->use_inst && !in_scope)
            continue;

        if (slot->dir == MPR_DIR_IN)
            continue;

        if (MPR_LOC_DST == map->process_loc) {
            // bypass map processing and bundle value without type coercion
            char types[sig->len];
            memset(types, sig->type, sig->len);
            msg = mpr_map_build_msg(map, slot, val, types,
                                    sig->use_inst ? idmap : 0);
            if (msg)
                send_or_bundle_msg(map->dst->link, map->dst->sig, msg, t,
                                   map->protocol);
            continue;
        }

        mpr_local_slot lslot = slot->loc;
        mpr_slot dst_slot = map->dst;
        mpr_slot to = (map->process_loc == MPR_LOC_SRC ? dst_slot : slot);
        char src_types[slot->sig->len];
        memset(src_types, slot->sig->type, slot->sig->len);
        char dst_types[to->sig->len];
        memset(dst_types, to->sig->type, to->sig->len);

        size_t n = mpr_sig_get_vector_bytes(sig);

        /* If this signal is non-instanced but the map has other instanced
         * sources we will need to update all of the map instances. */
        if (!sig->use_inst)
            inst = 0;
        int all = !sig->use_inst && map->num_src > 1 && map->loc->num_var_inst > 1;
        if (all) {
            // find a source signal with more instances
            for (j = 0; j < map->num_src; j++)
                if (map->src[j]->sig->loc && map->src[j]->sig->num_inst > sig->num_inst)
                    sig = map->src[j]->sig;
        }

        if (!sig->use_inst)
            idmap = mpr_expr_get_manages_instances(map->loc->expr) ? map->idmap : 0;

        // copy input history
        lslot->hist[idx].pos = ((lslot->hist[idx].pos + 1) % lslot->hist[idx].mem);
        memcpy(mpr_hist_get_val_ptr(lslot->hist[idx]), val, n);
        memcpy(mpr_hist_get_time_ptr(lslot->hist[idx]), &t, sizeof(mpr_time));

        if (map->process_loc == MPR_LOC_SRC && !slot->causes_update)
            continue;

        struct _mpr_sig_idmap *idmaps = sig->loc->idmaps;
        for (; inst < sig->loc->idmap_len; inst++) {
            // check if map instance is active
            if ((all || sig->use_inst) && !idmaps[inst].inst)
                continue;
            idx = idmaps[inst].inst->idx;
            int status = mpr_map_perform(map, dst_types, &t, idx);
            // TODO: only allow if src signal is non-instanced
            // TODO: extend for instanced signals also?
            if (!status) {
                // no updates or releases
                if (!all)
                    break;
                else
                    continue;
            }
            if (idmap && status & RELEASED_BEFORE_UPDATE) {
                /* send instance release if dst is instanced and either src or
                 * map is also instanced. */
                if (dst_slot->use_inst && (sig->use_inst || idmap)) {
                    msg = mpr_map_build_msg(map, slot, 0, 0,
                                            sig->use_inst ? idmaps[inst].map : idmap);
                    if (msg)
                        send_or_bundle_msg(dst_slot->link, dst_slot->sig, msg,
                                           t, map->protocol);
                    if (!sig->use_inst) {
                        mpr_dev_LID_decref(rtr->dev, 0, idmap);
                        idmap = map->idmap = 0;
                    }
                }
            }
            if (status & MPR_SIG_UPDATE) {
                // send instance update
                if (!sig->use_inst && slot->use_inst && !idmap) {
                    // create an id_map and store it in the map
                    mpr_id GID = mpr_dev_generate_unique_id(sig->dev);
                    idmap = map->idmap = mpr_dev_add_idmap(sig->dev, sig->loc->group,
                                                           0, GID);
                }
                void *result = mpr_hist_get_val_ptr(dst_slot->loc->hist[idx]);
                msg = mpr_map_build_msg(map, slot, result, dst_types,
                                        sig->use_inst ? idmaps[inst].map : idmap);
                if (msg)
                    send_or_bundle_msg(dst_slot->link, dst_slot->sig, msg,
                                       *(mpr_time*)mpr_hist_get_time_ptr(dst_slot->loc->hist[idx]),
                                       map->protocol);
            }
            if (idmap && status & RELEASED_AFTER_UPDATE) {
                /* send instance release if dst is instanced and either src or
                 * map is also instanced. */
                if (dst_slot->use_inst && (sig->use_inst || idmap)) {
                    msg = mpr_map_build_msg(map, slot, 0, 0,
                                            sig->use_inst ? idmaps[inst].map : idmap);
                    if (msg)
                        send_or_bundle_msg(dst_slot->link, dst_slot->sig, msg,
                                           t, map->protocol);
                    if (!sig->use_inst) {
                        mpr_dev_LID_decref(rtr->dev, 0, idmap);
                        idmap = map->idmap = 0;
                    }
                }
            }
            if (!all)
                break;
        }
    }
    *lock = 0;
}

// note on memory handling of mpr_rtr_bundle_msg():
// path: not owned, will not be freed (assumed is signal name, owned by signal)
// message: will be owned, will be freed when done
void send_or_bundle_msg(mpr_link link, mpr_sig dst, lo_message msg, mpr_time t,
                        mpr_proto proto)
{
    // Check if a matching bundle exists
    mpr_queue q = link->queues;
    while (q) {
        if (memcmp(&q->time, &t, sizeof(mpr_time))==0)
            break;
        q = q->next;
    }
    int is_local_only = link->local_dev == link->remote_dev;
    if (q && !q->locked) {
        // Add message to existing bundle
        lo_bundle b = (proto == MPR_PROTO_UDP || is_local_only) ? q->bundle.udp : q->bundle.tcp;
        lo_bundle_add_message(b, dst->path, msg);
    }
    else if (is_local_only) {
        // set out-of-band timestamp
        mpr_dev_bundle_start(t, NULL);
        // call handler directly instead of sending over the network
        mpr_dev_handler(NULL, lo_message_get_types(msg), lo_message_get_argv(msg),
                        lo_message_get_argc(msg), msg, (void*)dst);
        lo_message_free(msg);
    }
    else {
        // Send message immediately
        lo_bundle b = lo_bundle_new(t);
        lo_bundle_add_message(b, dst->path, msg);
        lo_address a;
        lo_server s;
        if (proto == MPR_PROTO_TCP) {
            a = link->addr.tcp;
            s = link->obj.graph->net.server.tcp;
        }
        else {
            a = link->addr.udp;
            s = link->obj.graph->net.server.udp;
        }
        RETURN_UNLESS(a);
        lo_send_bundle_from(a, s, b);
        lo_bundle_free_recursive(b);
    }
}

static mpr_rtr_sig add_rtr_sig(mpr_rtr r, mpr_sig s)
{
    // find signal in rtr_sig list
    mpr_rtr_sig rs = r->sigs;
    while (rs && rs->sig != s)
        rs = rs->next;

    // if not found, create a new list entry
    if (!rs) {
        rs = (mpr_rtr_sig)calloc(1, sizeof(struct _mpr_rtr_sig));
        rs->sig = s;
        rs->num_slots = 1;
        rs->slots = malloc(sizeof(mpr_slot));
        rs->slots[0] = 0;
        rs->next = r->sigs;
        r->sigs = rs;
    }
    return rs;
}

static int rtr_sig_store_slot(mpr_rtr_sig rs, mpr_slot slot)
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

static mpr_id unused_map_id(mpr_dev dev, mpr_rtr rtr)
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

static void alloc_and_init_local_slot(mpr_rtr rtr, mpr_slot slot, int is_src,
                                      int *max_inst, int *use_inst)
{
    slot->dir = (is_src ^ (slot->sig->loc ? 1 : 0)) ? MPR_DIR_IN : MPR_DIR_OUT;
    slot->loc = (mpr_local_slot)calloc(1, sizeof(struct _mpr_local_slot));
    if (slot->sig->loc) {
        slot->loc->rsig = add_rtr_sig(rtr, slot->sig);
        rtr_sig_store_slot(slot->loc->rsig, slot);

        if (slot->sig->num_inst > *max_inst)
            *max_inst = slot->sig->num_inst;
        if (slot->sig->use_inst)
            *use_inst = 1;
    }
    if (!slot->sig->loc || (is_src && slot->map->dst->sig->loc)) {
        mpr_link link = mpr_graph_add_link(rtr->dev->obj.graph, rtr->dev,
                                           slot->sig->dev);
        mpr_link_init(link);
        slot->link = link;
    }

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
    lmap->num_var_inst = 0;

    // Allocate local slot structures
    int max_num_inst = 0, use_inst = 0;
    for (i = 0; i < map->num_src; i++)
        alloc_and_init_local_slot(rtr, map->src[i], 1, &max_num_inst, &use_inst);
    alloc_and_init_local_slot(rtr, map->dst, 0, &max_num_inst, &use_inst);

    // Set num_inst and use_inst properties
    map->dst->num_inst = max_num_inst;
    map->dst->use_inst = use_inst;
    for (i = 0; i < map->num_src; i++) {
        if (map->src[i]->sig->loc) {
            map->src[i]->num_inst = map->src[i]->sig->num_inst;
            map->src[i]->use_inst = map->src[i]->sig->use_inst;
        }
        else {
            map->src[i]->num_inst = map->dst->num_inst;
            map->src[i]->use_inst = map->dst->use_inst;
        }
    }
    lmap->num_var_inst = max_num_inst;

    // assign a unique id to this map if we are the destination
    if (local_dst)
        map->obj.id = unused_map_id(rtr->dev, rtr);

    /* assign indices to source slots - may be overwritten later by message */
    for (i = 0; i < map->num_src; i++) {
        map->src[i]->obj.id = i;
    }
    map->dst->obj.id = (local_dst ? map->dst->loc->rsig->id_counter++ : -1);

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
    if (lmap->one_src)
        map->process_loc = MPR_LOC_SRC;
    else
        map->process_loc = MPR_LOC_DST;

    if (local_dst && (local_src == map->num_src)) {
        // all reference signals are local
        lmap->is_local_only = 1;
        map->dst->link = map->src[0]->link;
    }

    mpr_rtr_update_map_count(rtr);
}

static void check_link(mpr_rtr rtr, mpr_link link)
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

static void free_slot_memory(mpr_slot slot)
{
    RETURN_UNLESS(slot->loc);
    int i;
    // TODO: use rtr_sig for holding memory of local slots for effiency
//    if (!slot->loc->rsig) {
        if (slot->loc->hist) {
            for (i = 0; i < slot->num_inst; i++) {
                free(slot->loc->hist[i].val);
                free(slot->loc->hist[i].time);
            }
            free(slot->loc->hist);
        }
//    }
    free(slot->loc);
}

int mpr_rtr_remove_map(mpr_rtr rtr, mpr_map map)
{
    RETURN_UNLESS(map && map->loc, 1);
    // do not free local names since they point to signal's copy
    int i, j;

    // remove map and slots from rtr_sig lists if necessary
    if (map->dst->loc->rsig) {
        mpr_rtr_sig rs = map->dst->loc->rsig;
        for (i = 0; i < rs->num_slots; i++) {
            if (rs->slots[i] == map->dst) {
                rs->slots[i] = 0;
                break;
            }
        }
    }
    else if (map->status >= MPR_STATUS_READY && map->dst->link) {
        --map->dst->link->num_maps[0];
        check_link(rtr, map->dst->link);
    }
    free_slot_memory(map->dst);
    for (i = 0; i < map->num_src; i++) {
        if (map->src[i]->loc->rsig) {
            mpr_rtr_sig rs = map->src[i]->loc->rsig;
            for (j = 0; j < rs->num_slots; j++) {
                if (rs->slots[j] == map->src[i])
                    rs->slots[j] = 0;
            }
        }
        else if (map->status >= MPR_STATUS_READY && map->src[i]->link) {
            --map->src[i]->link->num_maps[1];
            check_link(rtr, map->src[i]->link);
        }
        free_slot_memory(map->src[i]);
    }

    // one more case: if map is local only need to decrement num_maps in local map
    if (map->loc->is_local_only) {
        mpr_link link = mpr_dev_get_link_by_remote(rtr->dev, rtr->dev);
        if (link)
            --link->num_maps[0];
    }

    // free buffers associated with user-defined expression variables
    if (map->loc->expr_var) {
        for (i = 0; i < map->loc->num_var_inst; i++) {
            if (map->loc->num_expr_var) {
                for (j = 0; j < map->loc->num_expr_var; j++) {
                    free(map->loc->expr_var[i][j].val);
                    free(map->loc->expr_var[i][j].time);
                }
            }
            free(map->loc->expr_var[i]);
        }
        free(map->loc->expr_var);
    }
    FUNC_IF(mpr_expr_free, map->loc->expr);
    free(map->loc);
    mpr_rtr_update_map_count(rtr);
    return 0;
}

int mpr_rtr_loop_check(mpr_rtr rtr, mpr_sig local_sig, int num_remotes,
                       const char **remotes)
{
    mpr_rtr_sig rs = rtr->sigs;
    while (rs && rs->sig != local_sig)
        rs = rs->next;
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

mpr_map mpr_rtr_map_out(mpr_rtr rtr, mpr_sig local_src, int num_src,
                        const char **src_names, const char *dst_name)
{
    // find associated rtr_sig
    mpr_rtr_sig rs = rtr->sigs;
    while (rs && rs->sig != local_src)
        rs = rs->next;
    RETURN_UNLESS(rs, 0);

    // find associated map
    int i, j;
    for (i = 0; i < rs->num_slots; i++) {
        if (!rs->slots[i] || rs->slots[i]->dir == MPR_DIR_IN)
            continue;
        mpr_slot slot = rs->slots[i];
        mpr_map map = slot->map;

        // check destination
        if (mpr_slot_match_full_name(map->dst, dst_name))
            continue;

        // check sources
        int found = 1;
        for (j = 0; j < map->num_src; j++) {
            if (map->src[j]->loc->rsig == rs)
                continue;
            if (mpr_slot_match_full_name(map->src[j], src_names[j])) {
                found = 0;
                break;
            }
        }
        if (found)
            return map;
    }
    return 0;
}

mpr_map mpr_rtr_map_in(mpr_rtr rtr, mpr_sig local_dst, int num_src,
                       const char **src_names)
{
    // find associated rtr_sig
    mpr_rtr_sig rs = rtr->sigs;
    while (rs && rs->sig != local_dst)
        rs = rs->next;
    RETURN_UNLESS(rs, 0);

    // find associated map
    int i, j;
    for (i = 0; i < rs->num_slots; i++) {
        if (!rs->slots[i] || rs->slots[i]->dir == MPR_DIR_OUT)
            continue;
        mpr_map map = rs->slots[i]->map;

        // check sources
        int found = 1;
        for (j = 0; j < num_src; j++) {
            if (mpr_slot_match_full_name(map->src[j], src_names[j])) {
                found = 0;
                break;
            }
        }
        if (found)
            return map;
    }
    return 0;
}

mpr_map mpr_rtr_map_by_id(mpr_rtr rtr, mpr_sig local_sig, mpr_id id, mpr_dir dir)
{
    int i;
    mpr_rtr_sig rs = rtr->sigs;
    while (rs && rs->sig != local_sig)
        rs = rs->next;
    RETURN_UNLESS(rs, 0);

    for (i = 0; i < rs->num_slots; i++) {
        if (!rs->slots[i] || (dir && rs->slots[i]->dir != dir))
            continue;
        if (rs->slots[i]->map->obj.id == id)
            return rs->slots[i]->map;
    }
    return 0;
}

mpr_slot mpr_rtr_get_slot(mpr_rtr rtr, mpr_sig sig, int slot_id)
{
    // only interested in incoming slots
    mpr_rtr_sig rs = rtr->sigs;
    while (rs && rs->sig != sig)
        rs = rs->next;
    RETURN_UNLESS(rs, NULL);

    int i, j;
    mpr_map map;
    for (i = 0; i < rs->num_slots; i++) {
        if (!rs->slots[i] || rs->slots[i]->dir == MPR_DIR_OUT)
            continue;
        map = rs->slots[i]->map;
        // check incoming slots for this map
        for (j = 0; j < map->num_src; j++) {
            if (map->src[j]->obj.id == slot_id)
                return map->src[j];
        }
    }
    return NULL;
}
