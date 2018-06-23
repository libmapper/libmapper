#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <zlib.h>

#include <lo/lo.h>

#include "mapper_internal.h"
#include "types_internal.h"
#include <mapper/mapper.h>

static void send_or_bundle_msg(mapper_link link, const char *path, lo_message msg,
                               mapper_time_t t, mapper_protocol proto);

static int map_in_scope(mapper_map map, mapper_id id)
{
    int i;
    id &= 0xFFFFFFFF00000000; // interested in device hash part only
    for (i = 0; i < map->num_scopes; i++) {
        if (map->scopes[i] == 0 || map->scopes[i]->obj.id == id)
            return 1;
    }
    return 0;
}

static void realloc_slot_insts(mapper_slot slot, int size)
{
    int i;
    if (slot->num_inst < size) {
        slot->local->hist = realloc(slot->local->hist,
                                    sizeof(struct _mapper_hist) * size);
        for (i = slot->num_inst; i < size; i++) {
            slot->local->hist[i].type = slot->sig->type;
            slot->local->hist[i].len = slot->sig->len;
            slot->local->hist[i].size = slot->local->hist_size;
            slot->local->hist[i].val = calloc(1, mapper_type_size(slot->sig->type)
                                              * slot->local->hist_size);
            slot->local->hist[i].time = calloc(1, sizeof(mapper_time_t)
                                               * slot->local->hist_size);
            slot->local->hist[i].pos = -1;
        }
        slot->num_inst = size;
    }
}

static void realloc_map_insts(mapper_map map, int size)
{
    int i, j;

    // check if source histories need to be reallocated
    for (i = 0; i < map->num_src; i++)
        realloc_slot_insts(map->src[i], size);

    // check if destination histories need to be reallocated
    realloc_slot_insts(map->dst, size);

    // check if expression variable histories need to be reallocated
    mapper_local_map lmap = map->local;
    if (size > lmap->num_var_inst) {
        lmap->expr_var = realloc(lmap->expr_var, sizeof(mapper_hist*) * size);
        for (i = lmap->num_var_inst; i < size; i++) {
            lmap->expr_var[i] = malloc(sizeof(struct _mapper_hist)
                                       * lmap->num_expr_var);
            for (j = 0; j < lmap->num_expr_var; j++) {
                lmap->expr_var[i][j].type = lmap->expr_var[0][j].type;
                lmap->expr_var[i][j].len = lmap->expr_var[0][j].len;
                lmap->expr_var[i][j].size = lmap->expr_var[0][j].size;
                lmap->expr_var[i][j].pos = -1;
                lmap->expr_var[i][j].val = calloc(1, sizeof(double)
                                                  * lmap->expr_var[i][j].len
                                                  * lmap->expr_var[i][j].size);
                lmap->expr_var[i][j].time = calloc(1, sizeof(mapper_time_t)
                                                   * lmap->expr_var[i][j].size);
            }
        }
        lmap->num_var_inst = size;
    }
}

// TODO: check for mismatched instance counts when using multiple sources
void mapper_router_num_inst_changed(mapper_router rtr, mapper_signal sig,
                                    int size)
{
    int i;
    // check if we have a reference to this signal
    mapper_router_sig rs = rtr->sigs;
    while (rs) {
        if (rs->sig == sig)
            break;
        rs = rs->next;
    }

    if (!rs) {
        // The signal is not mapped through this router.
        return;
    }

    // for array of slots, may need to reallocate destination instances
    for (i = 0; i < rs->num_slots; i++) {
        mapper_slot slot = rs->slots[i];
        if (slot)
            realloc_map_insts(slot->map, size);
    }
}

void mapper_router_process_sig(mapper_router rtr, mapper_signal sig, int inst,
                               const void *val, int count, mapper_time_t t)
{
    mapper_idmap idmap = sig->local->idmaps[inst].map;
    lo_message msg;

    // find the router signal
    mapper_router_sig rs = rtr->sigs;
    while (rs) {
        if (rs->sig == sig)
            break;
        rs = rs->next;
    }
    if (!rs)
        return;

    int i, j, k, idx = sig->local->idmaps[inst].inst->idx;
    mapper_map map;
    mapper_local_map lmap;

    if (!val) {
        mapper_local_slot lslot;
        mapper_slot slot;

        for (i = 0; i < rs->num_slots; i++) {
            if (!rs->slots[i])
                continue;

            slot = rs->slots[i];
            map = slot->map;
            lmap = map->local;

            if (map->status < STATUS_ACTIVE)
                continue;

            // need to reset user variable memory for this instance
            for (j = 0; j < lmap->num_expr_var; j++) {
                memset(lmap->expr_var[idx][j].val, 0, sizeof(double)
                       * lmap->expr_var[idx][j].len
                       * lmap->expr_var[idx][j].size);
                memset(lmap->expr_var[idx][j].time, 0,
                       sizeof(mapper_time_t) * lmap->expr_var[idx][j].size);
                lmap->expr_var[idx][j].pos = -1;
            }

            mapper_slot dst_slot = map->dst;
            mapper_local_slot dst_lslot = dst_slot->local;

            // also need to reset associated output memory
            dst_lslot->hist[idx].pos= -1;
            memset(dst_lslot->hist[idx].val, 0, dst_lslot->hist_size
                   * dst_slot->sig->len * mapper_type_size(dst_slot->sig->type));
            memset(dst_lslot->hist[idx].time, 0, dst_lslot->hist_size
                   * sizeof(mapper_time_t));
            dst_lslot->hist[idx].pos = -1;

            if (slot->dir == MAPPER_DIR_OUT
                && !(sig->local->idmaps[inst].status & RELEASED_REMOTELY)) {
                msg = 0;
                if (!slot->use_inst)
                    msg = mapper_map_build_msg(map, slot, 0, 1, 0, 0);
                else if (map_in_scope(map, idmap->global))
                    msg = mapper_map_build_msg(map, slot, 0, 1, 0, idmap);
                if (msg)
                    send_or_bundle_msg(dst_slot->link, dst_slot->sig->path,
                                       msg, t, map->protocol);
            }

            for (j = 0; j < map->num_src; j++) {
                slot = map->src[j];
                lslot = slot->local;

                // also need to reset associated input memory
                memset(lslot->hist[idx].val, 0, lslot->hist_size
                       * slot->sig->len * mapper_type_size(slot->sig->type));
                memset(lslot->hist[idx].time, 0, lslot->hist_size
                       * sizeof(mapper_time_t));
                lslot->hist[idx].pos = -1;

                if (!map->src[j]->use_inst)
                    continue;

                if (!map_in_scope(map, idmap->global))
                    continue;

                if (sig->local->idmaps[inst].status & RELEASED_REMOTELY)
                    continue;

                if (slot->dir == MAPPER_DIR_IN) {
                    // send release to upstream
                    msg = mapper_map_build_msg(map, slot, 0, 1, 0, idmap);
                    if (msg)
                        send_or_bundle_msg(slot->link, slot->sig->path, msg,
                                           t, map->protocol);
                }
            }
        }
        return;
    }

    // if count > 1, we need to allocate sufficient memory for largest output
    // vector so that we can store calculated values before sending
    // TODO: calculate max_output_size, cache in link_signal
    void *out_val_p = count == 1 ? 0 : alloca(count * sig->len * sizeof(double));
    for (i = 0; i < rs->num_slots; i++) {
        if (!rs->slots[i])
            continue;

        mapper_slot slot = rs->slots[i];
        map = slot->map;

        if (map->status < STATUS_ACTIVE)
            continue;

        int in_scope = map_in_scope(map, idmap->global);
        // TODO: should we continue for out-of-scope local destination updates?
        if (slot->use_inst && !in_scope) {
            continue;
        }

        mapper_local_slot lslot = slot->local;
        mapper_slot dst_slot = map->dst;
        mapper_slot to = (map->process_loc == MAPPER_LOC_SRC ? dst_slot : slot);
        int to_size = mapper_type_size(to->sig->type) * to->sig->len;
        char src_types[slot->sig->len * count];
        memset(src_types, slot->sig->type, slot->sig->len * count);
        char dst_types[to->sig->len * count];
        memset(dst_types, to->sig->type, to->sig->len * count);
        k = 0;
        for (j = 0; j < count; j++) {
            // copy input history
            size_t n = mapper_signal_vector_bytes(sig);
            lslot->hist[idx].pos = ((lslot->hist[idx].pos + 1)
                                    % lslot->hist[idx].size);
            memcpy(mapper_hist_val_ptr(lslot->hist[idx]), val + n * j, n);
            memcpy(mapper_hist_time_ptr(lslot->hist[idx]), &t,
                   sizeof(mapper_time_t));

            if (slot->dir == MAPPER_DIR_IN) {
                continue;
            }

            if (map->process_loc == MAPPER_LOC_SRC && !slot->causes_update)
                continue;

            if (!(mapper_map_perform(map, slot, idx, dst_types + to->sig->len * k)))
                continue;

            void *result = mapper_hist_val_ptr(map->dst->local->hist[idx]);

            if (count > 1) {
                memcpy((char*)out_val_p + to_size * j, result, to_size);
            }
            else {
                msg = mapper_map_build_msg(map, slot, result, 1, dst_types,
                                           slot->use_inst ? idmap : 0);
                if (msg)
                    send_or_bundle_msg(map->dst->link, dst_slot->sig->path,
                                       msg, t, map->protocol);
            }
            ++k;
        }
        if (count > 1 && slot->dir == MAPPER_DIR_OUT
            && (!slot->use_inst || in_scope)) {
            msg = mapper_map_build_msg(map, slot, out_val_p, k, dst_types,
                                       slot->use_inst ? idmap : 0);
            if (msg)
                send_or_bundle_msg(map->dst->link, dst_slot->sig->path, msg,
                                   t, map->protocol);
        }
    }
}

// note on memory handling of mapper_router_bundle_msg():
// path: not owned, will not be freed (assumed is signal name, owned by signal)
// message: will be owned, will be freed when done
void send_or_bundle_msg(mapper_link link, const char *path, lo_message msg,
                        mapper_time_t t, mapper_protocol proto)
{
    // Check if a matching bundle exists
    mapper_queue q = link->queues;
    while (q) {
        if (memcmp(&q->time, &t, sizeof(mapper_time_t))==0)
            break;
        q = q->next;
    }
    if (q) {
        // Add message to existing bundle
        lo_bundle b = (proto == MAPPER_PROTO_TCP) ? q->bundle.tcp : q->bundle.udp;
        lo_bundle_add_message(b, path, msg);
    }
    else {
        // Send message immediately
        lo_bundle b = lo_bundle_new(t);
        lo_bundle_add_message(b, path, msg);
        lo_address a;
        lo_server s;
        if (proto == MAPPER_PROTO_TCP) {
            a = link->addr.tcp;
            s = link->obj.graph->net.server.tcp;
        }
        else {
            a = link->addr.udp;
            s = link->obj.graph->net.server.udp;
        }
        lo_send_bundle_from(a, s, b);
        lo_bundle_free_recursive(b);
    }
}

static mapper_router_sig add_router_sig(mapper_router r, mapper_signal s)
{
    // find signal in router_sig list
    mapper_router_sig rs = r->sigs;
    while (rs && rs->sig != s)
        rs = rs->next;

    // if not found, create a new list entry
    if (!rs) {
        rs = ((mapper_router_sig)
              calloc(1, sizeof(struct _mapper_router_sig)));
        rs->sig = s;
        rs->num_slots = 1;
        rs->slots = malloc(sizeof(mapper_local_slot *));
        rs->slots[0] = 0;
        rs->next = r->sigs;
        r->sigs = rs;
    }
    return rs;
}

static int router_sig_store_slot(mapper_router_sig rs, mapper_slot slot)
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
    rs->slots = realloc(rs->slots, sizeof(mapper_slot*) * rs->num_slots * 2);
    rs->slots[rs->num_slots] = slot;
    for (i = rs->num_slots+1; i < rs->num_slots * 2; i++) {
        rs->slots[i] = 0;
    }
    i = rs->num_slots;
    rs->num_slots *= 2;
    return i;
}

static mapper_id unused_map_id(mapper_device dev, mapper_router rtr)
{
    int i, done = 0;
    mapper_id id;
    while (!done) {
        done = 1;
        id = mapper_device_generate_unique_id(dev);
        // check if map exists with this id
        mapper_router_sig rs = rtr->sigs;
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

static void alloc_and_init_local_slot(mapper_router rtr, mapper_slot slot,
                                      int is_src, int *max_inst)
{
    slot->dir = ((is_src ^ (slot->sig->local ? 1 : 0))
                 ? MAPPER_DIR_IN : MAPPER_DIR_OUT);

    slot->local = ((mapper_local_slot)
                   calloc(1, sizeof(struct _mapper_local_slot)));
    if (slot->sig->local) {
        slot->local->rsig = add_router_sig(rtr, slot->sig);
        router_sig_store_slot(slot->local->rsig, slot);

        if (slot->sig->num_inst > *max_inst)
            *max_inst = slot->sig->num_inst;

        // start with signal extrema if known
        if (!slot->max && slot->sig->max) {
            // copy range from signal
            slot->max = malloc(mapper_signal_vector_bytes(slot->sig));
            memcpy(slot->max, slot->sig->max,
                   mapper_signal_vector_bytes(slot->sig));
        }
        if (!slot->min && slot->sig->min) {
            // copy range from signal
            slot->min = malloc(mapper_signal_vector_bytes(slot->sig));
            memcpy(slot->min, slot->sig->min,
                   mapper_signal_vector_bytes(slot->sig));
        }
    }
    if (!slot->sig->local || (is_src && slot->map->dst->sig->local)) {
        mapper_link link;
        link = mapper_graph_add_link(rtr->dev->obj.graph, rtr->dev,
                                     slot->sig->dev);
        link->obj.type = MAPPER_OBJ_LINK;
        mapper_link_init(link);
        slot->link = link;
    }

    // set some sensible defaults
    slot->calib = 0;
    slot->causes_update = 1;
}

void mapper_router_add_map(mapper_router rtr, mapper_map map)
{
    int i;
    int local_dst = map->dst->sig->local ? 1 : 0;
    int local_src = 0;
    for (i = 0; i < map->num_src; i++) {
        if (map->src[i]->sig->local)
            ++local_src;
    }

    if (map->local) {
        trace("error in mapper_router_add_map – local structures already exist.\n");
        return;
    }

    mapper_local_map lmap = ((mapper_local_map)
                             calloc(1, sizeof(struct _mapper_local_map)));
    map->local = lmap;
    lmap->rtr = rtr;

    // TODO: configure number of instances available for each slot
    lmap->num_var_inst = 0;

    // Allocate local slot structures
    int max_num_inst = 0;
    for (i = 0; i < map->num_src; i++)
        alloc_and_init_local_slot(rtr, map->src[i], 1, &max_num_inst);
    alloc_and_init_local_slot(rtr, map->dst, 0, &max_num_inst);

    // Set num_inst property
    map->dst->num_inst = max_num_inst;
    map->dst->use_inst = map->dst->num_inst > 1;
    for (i = 0; i < map->num_src; i++) {
        if (map->src[i]->sig->local)
            map->src[i]->num_inst = map->src[i]->sig->num_inst;
        else
            map->src[i]->num_inst = map->dst->num_inst;
        map->src[i]->use_inst = map->src[i]->num_inst > 1;
    }
    lmap->num_var_inst = max_num_inst;

    // assign a unique id to this map if we are the destination
    if (local_dst)
        map->obj.id = unused_map_id(rtr->dev, rtr);

    /* assign indices to source slots - may be overwritten later by message */
    for (i = 0; i < map->num_src; i++) {
        map->src[i]->obj.id = i;
    }
    map->dst->obj.id = (local_dst ? map->dst->local->rsig->id_counter++ : -1);

    // add scopes
    int scope_count = 0;
    map->num_scopes = map->num_src;
    map->scopes = (mapper_device *) malloc(sizeof(mapper_device) * map->num_scopes);

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
        map->scopes = realloc(map->scopes, sizeof(mapper_device) * scope_count);
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
        map->process_loc = MAPPER_LOC_SRC;
    else
        map->process_loc = MAPPER_LOC_DST;

    if (local_dst && (local_src == map->num_src)) {
        // all reference signals are local
        lmap->is_local_only = 1;
        map->dst->link = map->src[0]->link;
    }
}

static void check_link(mapper_router rtr, mapper_link link)
{
    /* We could remove link the link here if it has no associated maps, however
     * under normal usage it is likely that users will add new maps after
     * deleting the old ones. If we remove the link immediately it will have to
     * be re-established in this scenario, so we will allow the house-keeping
     * routines to clean up empty links after the link ping timeout period. */
}

void mapper_router_remove_link(mapper_router rtr, mapper_link link)
{
    int i, j;
    // check if any maps use this link
    mapper_router_sig rs = rtr->sigs;
    while (rs) {
        for (i = 0; i < rs->num_slots; i++) {
            if (!rs->slots[i])
                continue;
            mapper_map map = rs->slots[i]->map;
            if (map->dst->link == link) {
                mapper_router_remove_map(rtr, map);
                continue;
            }
            for (j = 0; j < map->num_src; j++) {
                if (map->src[j]->link == link) {
                    mapper_router_remove_map(rtr, map);
                    break;
                }
            }
        }
        rs = rs->next;
    }
}

void mapper_router_remove_sig(mapper_router rtr, mapper_router_sig rs)
{
    if (rtr && rs) {
        // No maps remaining – we can remove the router_sig also
        mapper_router_sig *rstemp = &rtr->sigs;
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

static void free_slot_memory(mapper_slot slot)
{
    int i;
    if (!slot->local)
        return;
    // TODO: use router_sig for holding memory of local slots for effiency
//    if (!slot->local->rsig) {
        if (slot->local->hist) {
            for (i = 0; i < slot->num_inst; i++) {
                free(slot->local->hist[i].val);
                free(slot->local->hist[i].time);
            }
            free(slot->local->hist);
        }
//    }
    free(slot->local);
}

int mapper_router_remove_map(mapper_router rtr, mapper_map map)
{
    // do not free local names since they point to signal's copy
    int i, j;
    if (!map || !map->local)
        return 1;

    // remove map and slots from router_sig lists if necessary
    if (map->dst->local->rsig) {
        mapper_router_sig rs = map->dst->local->rsig;
        for (i = 0; i < rs->num_slots; i++) {
            if (rs->slots[i] == map->dst) {
                rs->slots[i] = 0;
                break;
            }
        }
    }
    else if (map->status >= STATUS_READY && map->dst->link) {
        --map->dst->link->num_maps[0];
        check_link(rtr, map->dst->link);
    }
    free_slot_memory(map->dst);
    for (i = 0; i < map->num_src; i++) {
        if (map->src[i]->local->rsig) {
            mapper_router_sig rs = map->src[i]->local->rsig;
            for (j = 0; j < rs->num_slots; j++) {
                if (rs->slots[j] == map->src[i]) {
                    rs->slots[j] = 0;
                }
            }
        }
        else if (map->status >= STATUS_READY && map->src[i]->link) {
            --map->src[i]->link->num_maps[1];
            check_link(rtr, map->src[i]->link);
        }
        free_slot_memory(map->src[i]);
    }

    // one more case: if map is local only need to decrement num_maps in local map
    if (map->local->is_local_only) {
        mapper_link link = mapper_device_get_link_by_remote_device(rtr->dev,
                                                                   rtr->dev);
        if (link)
            --link->num_maps[0];
    }

    // free buffers associated with user-defined expression variables
    if (map->local->expr_var) {
        for (i = 0; i < map->local->num_var_inst; i++) {
            if (map->local->num_expr_var) {
                for (j = 0; j < map->local->num_expr_var; j++) {
                    free(map->local->expr_var[i][j].val);
                    free(map->local->expr_var[i][j].time);
                }
            }
            free(map->local->expr_var[i]);
        }
        free(map->local->expr_var);
    }
    if (map->local->expr)
        mapper_expr_free(map->local->expr);

    free(map->local);
    return 0;
}

int mapper_router_loop_check(mapper_router rtr,
                             mapper_signal local_sig,
                             int num_remotes,
                             const char **remotes)
{
    mapper_router_sig rs = rtr->sigs;
    while (rs && rs->sig != local_sig)
        rs = rs->next;
    if (!rs)
        return 0;
    int i, j;
    for (i = 0; i < rs->num_slots; i++) {
        if (!rs->slots[i] || rs->slots[i]->dir == MAPPER_DIR_IN)
            continue;
        mapper_slot slot = rs->slots[i];
        mapper_map map = slot->map;

        // check destination
        for (j = 0; j < num_remotes; j++) {
            if (!mapper_slot_match_full_name(map->dst, remotes[j]))
                return 1;
        }
    }
    return 0;
}

mapper_map mapper_router_map_out(mapper_router rtr, mapper_signal local_src,
                                 int num_src, const char **src_names,
                                 const char *dst_name)
{
    // find associated router_sig
    mapper_router_sig rs = rtr->sigs;
    while (rs && rs->sig != local_src)
        rs = rs->next;
    if (!rs)
        return 0;

    // find associated map
    int i, j;
    for (i = 0; i < rs->num_slots; i++) {
        if (!rs->slots[i] || rs->slots[i]->dir == MAPPER_DIR_IN)
            continue;
        mapper_slot slot = rs->slots[i];
        mapper_map map = slot->map;

        // check destination
        if (mapper_slot_match_full_name(map->dst, dst_name))
            continue;

        // check sources
        int found = 1;
        for (j = 0; j < map->num_src; j++) {
            if (map->src[j]->local->rsig == rs)
                continue;

            if (mapper_slot_match_full_name(map->src[j], src_names[j])) {
                found = 0;
                break;
            }
        }
        if (found)
            return map;
    }
    return 0;
}

mapper_map mapper_router_map_in(mapper_router rtr, mapper_signal local_dst,
                                int num_src, const char **src_names)
{
    // find associated router_sig
    mapper_router_sig rs = rtr->sigs;
    while (rs && rs->sig != local_dst)
        rs = rs->next;
    if (!rs)
        return 0;

    // find associated map
    int i, j;
    for (i = 0; i < rs->num_slots; i++) {
        if (!rs->slots[i] || rs->slots[i]->dir == MAPPER_DIR_OUT)
            continue;
        mapper_map map = rs->slots[i]->map;

        // check sources
        int found = 1;
        for (j = 0; j < num_src; j++) {
            if (mapper_slot_match_full_name(map->src[j], src_names[j])) {
                found = 0;
                break;
            }
        }
        if (found)
            return map;
    }
    return 0;
}

mapper_map mapper_router_map_by_id(mapper_router rtr, mapper_signal local_sig,
                                   mapper_id id, mapper_direction dir)
{
    int i;
    mapper_router_sig rs = rtr->sigs;
    while (rs && rs->sig != local_sig)
        rs = rs->next;
    if (!rs)
        return 0;

    for (i = 0; i < rs->num_slots; i++) {
        if (!rs->slots[i] || (dir && rs->slots[i]->dir != dir))
            continue;
        if (rs->slots[i]->map->obj.id == id)
            return rs->slots[i]->map;
    }
    return 0;
}

mapper_slot mapper_router_slot(mapper_router rtr, mapper_signal signal,
                               int slot_id)
{
    // only interested in incoming slots
    mapper_router_sig rs = rtr->sigs;
    while (rs && rs->sig != signal)
        rs = rs->next;
    if (!rs)
        return NULL; // no associated router_sig

    int i, j;
    mapper_map map;
    for (i = 0; i < rs->num_slots; i++) {
        if (!rs->slots[i] || rs->slots[i]->dir == MAPPER_DIR_OUT)
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
