#include <lo/lo.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>
#include <assert.h>
#include <sys/time.h>
#include <stddef.h>

#include "mapper_internal.h"
#include "types_internal.h"
#include "config.h"
#include <mapper/mapper.h>

#ifdef HAVE_PTHREAD
#include <pthread.h>
#endif

extern const char* net_msg_strings[NUM_MSG_STRINGS];

#define DEV_SERVER_FUNC(FUNC, ...)                      \
{                                                       \
    lo_server_ ## FUNC(net->server.udp, __VA_ARGS__);   \
    lo_server_ ## FUNC(net->server.tcp, __VA_ARGS__);   \
}

// prototypes
void mpr_dev_start_servers(mpr_dev dev);
static void mpr_dev_remove_idmap(mpr_dev dev, int group, mpr_id_map rem);
static inline int mpr_dev_process_outputs_internal(mpr_dev dev);

mpr_time ts = {0,1};

static int cmp_qry_linked(const void *ctx, mpr_dev dev)
{
    mpr_dev self = *(mpr_dev*)ctx;
    for (int i = 0; i < self->num_linked; i++) {
        if (!self->linked[i] || self->linked[i]->obj.id == dev->obj.id)
            return 1;
    }
    return 0;
}

static int cmp_qry_dev_sigs(const void *context_data, mpr_sig sig)
{
    mpr_id dev_id = *(mpr_id*)context_data;
    int dir = *(int*)(context_data + sizeof(mpr_id));
    return ((dir & sig->dir) && (dev_id == sig->dev->obj.id));
}

void init_dev_prop_tbl(mpr_dev dev)
{
    dev->obj.props.mask = 0;
    dev->obj.props.synced = mpr_tbl_new();
    if (!dev->loc)
        dev->obj.props.staged = mpr_tbl_new();
    int mod = dev->loc ? NON_MODIFIABLE : MODIFIABLE;
    mpr_tbl tbl = dev->obj.props.synced;

    // these properties need to be added in alphabetical order
    mpr_tbl_link(tbl, PROP(DATA), 1, MPR_PTR, &dev->obj.data,
                 LOCAL_MODIFY | INDIRECT | LOCAL_ACCESS_ONLY);
    mpr_tbl_link(tbl, PROP(ID), 1, MPR_INT64, &dev->obj.id, mod);
    mpr_list qry = mpr_list_new_query((const void**)&dev->obj.graph->devs,
                                      cmp_qry_linked, "v", &dev);
    mpr_tbl_link(tbl, PROP(LINKED), 1, MPR_LIST, qry, NON_MODIFIABLE | PROP_OWNED);
    mpr_tbl_link(tbl, PROP(NAME), 1, MPR_STR, &dev->name, mod | INDIRECT | LOCAL_ACCESS_ONLY);
    mpr_tbl_link(tbl, PROP(NUM_MAPS_IN), 1, MPR_INT32, &dev->num_maps_in, mod);
    mpr_tbl_link(tbl, PROP(NUM_MAPS_OUT), 1, MPR_INT32, &dev->num_maps_out, mod);
    mpr_tbl_link(tbl, PROP(NUM_SIGS_IN), 1, MPR_INT32, &dev->num_inputs, mod);
    mpr_tbl_link(tbl, PROP(NUM_SIGS_OUT), 1, MPR_INT32, &dev->num_outputs, mod);
    mpr_tbl_link(tbl, PROP(ORDINAL), 1, MPR_INT32, &dev->ordinal, mod);
    if (!dev->loc) {
        qry = mpr_list_new_query((const void**)&dev->obj.graph->sigs,
                                 cmp_qry_dev_sigs, "hi", dev->obj.id, MPR_DIR_ANY);
        mpr_tbl_link(tbl, PROP(SIG), 1, MPR_LIST, qry, NON_MODIFIABLE | PROP_OWNED);
    }
    mpr_tbl_link(tbl, PROP(STATUS), 1, MPR_INT32, &dev->status, mod | LOCAL_ACCESS_ONLY);
    mpr_tbl_link(tbl, PROP(SYNCED), 1, MPR_TIME, &dev->synced, mod | LOCAL_ACCESS_ONLY);
    mpr_tbl_link(tbl, PROP(VERSION), 1, MPR_INT32, &dev->obj.version, mod);

    if (dev->loc)
        mpr_tbl_set(tbl, PROP(LIBVER), NULL, 1, MPR_STR, PACKAGE_VERSION, NON_MODIFIABLE);
    mpr_tbl_set(tbl, PROP(IS_LOCAL), NULL, 1, MPR_BOOL, &dev->loc,
                LOCAL_ACCESS_ONLY | NON_MODIFIABLE);
}

/*! Allocate and initialize a device. This function is called to create a new
 *  mpr_dev, not to create a representation of remote devices. */
mpr_dev mpr_dev_new(const char *name_prefix, mpr_graph g)
{
    RETURN_UNLESS(name_prefix, 0);
    if (name_prefix[0] == '/')
        ++name_prefix;
    TRACE_RETURN_UNLESS(!strchr(name_prefix, '/'), NULL, "error: character '/' "
                        "is not permitted in device name.\n");
    if (!g) {
        g = mpr_graph_new(0);
        g->own = 0;
    }

    mpr_dev dev = (mpr_dev)mpr_list_add_item((void**)&g->devs, sizeof(mpr_dev_t));
    dev->obj.type = MPR_DEV;
    dev->obj.graph = g;
    dev->loc = (mpr_local_dev)calloc(1, sizeof(mpr_local_dev_t));

    init_dev_prop_tbl(dev);

    dev->prefix = strdup(name_prefix);
    mpr_dev_start_servers(dev);

    if (!g->net.server.udp || !g->net.server.tcp) {
        mpr_dev_free(dev);
        return NULL;
    }

    g->net.rtr = (mpr_rtr)calloc(1, sizeof(mpr_rtr_t));
    g->net.rtr->dev = dev;

    dev->loc->ordinal.val = 1;
    dev->loc->idmaps.active = (mpr_id_map*) malloc(sizeof(mpr_id_map));
    dev->loc->idmaps.active[0] = 0;
    dev->loc->num_sig_groups = 1;

    mpr_net_add_dev(&g->net, dev);

    dev->status = MPR_STATUS_STAGED;
    return dev;
}

//! Free resources used by a mpr device.
void mpr_dev_free(mpr_dev dev)
{
    RETURN_UNLESS(dev && dev->loc);
    if (!dev->obj.graph) {
        free(dev);
        return;
    }
    mpr_graph gph = dev->obj.graph;
    mpr_net net = &gph->net;

    // free any queued graph messages without sending
    mpr_net_free_msgs(net);

    // remove OSC handlers associated with this device
    mpr_net_remove_dev_methods(net, dev);

    // remove subscribers
    mpr_subscriber sub;
    while (dev->loc->subscribers) {
        sub = dev->loc->subscribers;
        FUNC_IF(lo_address_free, sub->addr);
        dev->loc->subscribers = sub->next;
        free(sub);
    }

    int i;
    mpr_list sigs = mpr_dev_get_sigs(dev, MPR_DIR_ANY);
    while (sigs) {
        mpr_sig sig = (mpr_sig)*sigs;
        sigs = mpr_list_get_next(sigs);
        if (sig->loc) {
            // release active instances
            for (i = 0; i < sig->loc->idmap_len; i++) {
                if (sig->loc->idmaps[i].inst)
                    mpr_sig_release_inst_internal(sig, i);
            }
        }
        mpr_sig_free(sig);
    }

    if (dev->loc->registered) {
        // A registered device must tell the network it is leaving.
        NEW_LO_MSG(msg, ;)
        if (msg) {
            mpr_net_use_bus(net);
            lo_message_add_string(msg, mpr_dev_get_name(dev));
            mpr_net_add_msg(net, 0, MSG_LOGOUT, msg);
            mpr_net_send(net);
        }
    }

    // Release links to other devices
    mpr_list links = mpr_dev_get_links(dev, MPR_DIR_ANY);
    while (links) {
        mpr_link link = (mpr_link)*links;
        links = mpr_list_get_next(links);
        mpr_dev_process_outputs_internal(dev);
        mpr_graph_remove_link(gph, link, MPR_OBJ_REM);
    }

    // Release device id maps
    mpr_id_map map;
    for (i = 0; i < dev->loc->num_sig_groups; i++) {
        while (dev->loc->idmaps.active[i]) {
            map = dev->loc->idmaps.active[i];
            dev->loc->idmaps.active[i] = map->next;
            free(map);
        }
    }
    free(dev->loc->idmaps.active);

    while (dev->loc->idmaps.reserve) {
        map = dev->loc->idmaps.reserve;
        dev->loc->idmaps.reserve = map->next;
        free(map);
    }

    if (net->rtr) {
        while (net->rtr->sigs) {
            mpr_rtr_sig rs = net->rtr->sigs;
            net->rtr->sigs = net->rtr->sigs->next;
            free(rs);
        }
        free(net->rtr);
    }

    FUNC_IF(lo_server_free, net->server.udp);
    FUNC_IF(lo_server_free, net->server.tcp);
    FUNC_IF(free, dev->prefix);
    free(dev->loc);

    mpr_graph_remove_dev(gph, dev, MPR_OBJ_REM, 1);
    if (!gph->own)
        mpr_graph_free(gph);
}

void mpr_dev_on_registered(mpr_dev dev)
{
    int i;
    /* Add unique device id to locally-activated signal instances. */
    mpr_list sigs = mpr_dev_get_sigs(dev, MPR_DIR_ANY);
    while (sigs) {
        mpr_sig sig = (mpr_sig)*sigs;
        sigs = mpr_list_get_next(sigs);
        if (sig->loc) {
            for (i = 0; i < sig->loc->idmap_len; i++) {
                mpr_id_map idmap = sig->loc->idmaps[i].map;
                if (idmap && !(idmap->GID >> 32))
                    idmap->GID |= dev->obj.id;
            }
            sig->obj.id |= dev->obj.id;
        }
    }
    mpr_list qry = mpr_list_new_query((const void**)&dev->obj.graph->sigs,
                                      cmp_qry_dev_sigs, "hi", dev->obj.id, MPR_DIR_ANY);
    mpr_tbl_set(dev->obj.props.synced, PROP(SIG), NULL, 1, MPR_LIST, qry,
                NON_MODIFIABLE | PROP_OWNED);
    dev->loc->registered = 1;
    dev->ordinal = dev->loc->ordinal.val;
    dev->status = MPR_STATUS_READY;
}

static inline int check_types(const mpr_type *types, int len, mpr_type type,
                              int vector_len)
{
    int i, vals = 0;
    RETURN_UNLESS(len >= vector_len, -1);
    for (i = 0; i < len; i++) {
        if (types[i] == type)
            ++vals;
        else if (types[i] != MPR_NULL)
            return -1;
    }
    return vals;
}

int mpr_dev_bundle_start(lo_timetag t, void *data)
{
    mpr_time_set(&ts, t);
    return 0;
}

/* Notes:
 * - Incoming signal values may be scalars or vectors, but much match the
 *   length of the target signal or mapping slot.
 * - Vectors are of homogeneous type (MPR_INT32, MPR_FLT or MPR_DBL) however
 *   individual elements may have no value (type MPR_NULL)
 * - A vector consisting completely of nulls indicates a signal instance release
 *   TODO: use more specific message for release?
 * - Updates to a specific signal instance are indicated using the label
 *   "@instance" followed by a 64bit integer which uniquely identifies this
 *   instance within the network of libmapper devices
 * - Updates to specific "slots" of a convergent (i.e. multi-source) mapping
 *   are indicated using the label "@slot" followed by a single integer slot #
 * - Instance creation and release may also be triggered by expression
 *   evaluation. Refer to the document "Using Instanced Signals with Libmapper"
 *   for more information.
 */
int mpr_dev_handler(const char *path, const char *types, lo_arg **argv, int argc,
                    lo_message msg, void *data)
{
    mpr_sig sig = (mpr_sig)data;
    mpr_dev dev;
    mpr_rtr rtr = sig->obj.graph->net.rtr;
    int i, val_len = 0, vals;
    int idmap_idx, slot_idx = -1, map_manages_inst = 0;
    mpr_id GID = 0;
    mpr_id_map idmap;
    mpr_map map = 0;
    mpr_slot slot = 0;

    TRACE_RETURN_UNLESS(sig && (dev = sig->dev), 0, "error in mpr_dev_handler, "
                        "cannot retrieve user data\n");
    TRACE_DEV_RETURN_UNLESS(sig->num_inst, 0, "signal '%s' has no instances.\n", sig->name);
    RETURN_UNLESS(argc, 0);

    // We need to consider that there may be properties appended to the msg
    // check length and find properties if any
    while (val_len < argc && types[val_len] != MPR_STR)
        ++val_len;
    i = val_len;
    while (i < argc) {
        // Parse any attached properties (instance ids, slot number)
        TRACE_DEV_RETURN_UNLESS(types[i] == MPR_STR, 0, "error in "
                                "mpr_dev_handler: unexpected argument type.\n")
        if ((strcmp(&argv[i]->s, "@in") == 0) && argc >= i + 2) {
            TRACE_DEV_RETURN_UNLESS(types[i+1] == MPR_INT64, 0, "error in "
                                    "mpr_dev_handler: bad arguments for 'instance' prop.\n")
            GID = argv[i+1]->i64;
            i += 2;
        }
        else if ((strcmp(&argv[i]->s, "@sl") == 0) && argc >= i + 2) {
            TRACE_DEV_RETURN_UNLESS(types[i+1] == MPR_INT32, 0, "error in "
                                    "mpr_dev_handler: bad arguments for 'slot' prop.\n")
            slot_idx = argv[i+1]->i32;
            i += 2;
        }
        else {
#ifdef DEBUG
            trace_dev(dev, "error in mpr_dev_handler: unknown property name '%s'.\n", &argv[i]->s);
#endif
            return 0;
        }
    }

    if (slot_idx >= 0) {
        // retrieve mapping associated with this slot
        slot = mpr_rtr_get_slot(rtr, sig, slot_idx);
        TRACE_DEV_RETURN_UNLESS(slot, 0, "error in mpr_dev_handler: slot %d not found.\n", slot_idx);
        map = slot->map;
        TRACE_DEV_RETURN_UNLESS(map->status >= MPR_STATUS_READY, 0, "error in mpr_dev_handler: "
                                "mapping not yet ready.\n");
        TRACE_DEV_RETURN_UNLESS(map->loc->expr, 0, "error in mpr_dev_handler: missing expression.\n");
        if (map->process_loc == MPR_LOC_DST) {
            vals = check_types(types, val_len, slot->sig->type, slot->sig->len);
            map_manages_inst = mpr_expr_get_manages_inst(map->loc->expr);
        }
        else {
            // value has already been processed at source device
            map = 0;
            vals = check_types(types, val_len, sig->type, sig->len);
        }
    }
    else
        vals = check_types(types, val_len, sig->type, sig->len);
    RETURN_UNLESS(vals >= 0, 0);

    // TODO: optionally discard out-of-order messages
    // requires timebase sync for many-to-one mappings or local updates
    //    if (sig->discard_out_of_order && out_of_order(si->time, t))
    //        return 0;

    if (GID) {
        idmap_idx = mpr_sig_get_idmap_with_GID(sig, GID, RELEASED_LOCALLY, ts, 0);
        if (idmap_idx < 0) {
            // no instance found with this map
            // Don't activate instance just to release it again
            RETURN_UNLESS(vals, 0);

            if (map_manages_inst && vals == slot->sig->len) {
                /* special case: do a dry-run to check whether this map will
                 * cause a release. If so, don't bother stealing an instance. */
                mpr_value_buffer_t b = {argv[0], 0, -1};
                mpr_value_t v = {&b, val_len, 1, slot->sig->type, 1};
                mpr_value src[map->num_src];
                for (i = 0; i < map->num_src; i++)
                    src[i] = (i == slot->obj.id) ? &v : 0;
                int status = mpr_expr_eval(map->loc->expr, src, 0, 0, &ts, 0, 0);
                if (status & EXPR_RELEASE_BEFORE_UPDATE)
                    return 0;
            }

            // otherwise try to init reserved/stolen instance with device map
            idmap_idx = mpr_sig_get_idmap_with_GID(sig, GID, 0, ts, 1);
            TRACE_DEV_RETURN_UNLESS(idmap_idx >= 0, 0, "no instances available"
                                    " for GUID %"PR_MPR_ID" (1)\n", GID);
        }
        else if (sig->loc->idmaps[idmap_idx].status & RELEASED_LOCALLY) {
            /* map was already released locally, we are only interested in release messages */
            if (0 == vals) {
                // we can clear signal's reference to map
                idmap = sig->loc->idmaps[idmap_idx].map;
                sig->loc->idmaps[idmap_idx].map = 0;
                mpr_dev_GID_decref(dev, sig->loc->group, idmap);
            }
            return 0;
        }
        TRACE_DEV_RETURN_UNLESS(sig->loc->idmaps[idmap_idx].inst, 0, "error in mpr_dev_handler: "
                                "missing instance!\n");
    }
    else {
        // use the first available instance
        idmap_idx = 0;
        if (!sig->loc->idmaps[0].inst)
            idmap_idx = mpr_sig_get_idmap_with_LID(sig, sig->loc->inst[0]->id, 1, ts, 1);
        RETURN_UNLESS(idmap_idx >= 0, 0);
    }
    mpr_sig_inst si = sig->loc->idmaps[idmap_idx].inst;
    int inst_idx = si->idx;
    float diff = mpr_time_get_diff(ts, si->time);
    idmap = sig->loc->idmaps[idmap_idx].map;

    int size = mpr_type_get_size(slot ? slot->sig->type : sig->type);

    if (vals == 0) {
        if (GID) {
            // TODO: mark SLOT status as remotely released rather than map
            sig->loc->idmaps[idmap_idx].status |= RELEASED_REMOTELY;
            mpr_dev_GID_decref(dev, sig->loc->group, idmap);
            if (!sig->use_inst) {
                // clear signal's reference to idmap
                mpr_dev_LID_decref(dev, sig->loc->group, idmap);
                sig->loc->idmaps[idmap_idx].map = 0;
                sig->loc->idmaps[idmap_idx].inst->active = 0;
                sig->loc->idmaps[idmap_idx].inst = 0;
                return 0;
            }
        }
        RETURN_UNLESS(sig->use_inst && (!map || map->use_inst), 0);

        /* Try to release instance, but do not call mpr_rtr_process_sig() here, since we don't
         * know if the local signal instance will actually be released. */
        int evt = MPR_SIG_REL_UPSTRM & sig->loc->event_flags ? MPR_SIG_REL_UPSTRM : MPR_SIG_UPDATE;
        mpr_sig_call_handler(sig, evt, idmap->LID, 0, 0, &ts, diff);

        RETURN_UNLESS(map && MPR_LOC_DST == map->process_loc, 0);

        /* Reset memory for corresponding source slot. */
        mpr_local_slot lslot = slot->loc;
        // TODO: make a function (reset)
        mpr_value_reset_inst(&lslot->val, inst_idx);
        return 0;
    }

    /* Partial vector updates are not allowed in convergent maps since the slot value mirrors the
     * remote signal value. */
    if (map && vals != slot->sig->len) {
#ifdef DEBUG
        trace_dev(dev, "error in mpr_dev_handler: partial vector update "
                  "applied to convergent mapping slot.");
#endif
        return 0;
    }

    int all = !GID;
    if (map) {
        /* Or if this signal slot is non-instanced but the map has other instanced
         * sources we will need to update all of the map instances. */
        all |= !map->use_inst || (!slot->sig->use_inst && map->num_src > 1 && map->loc->num_inst > 1);
    }
    if (all)
        idmap_idx = 0;

    if (map) {
        for (; idmap_idx < sig->loc->idmap_len; idmap_idx++) {
            // check if map instance is active
            if ((si = sig->loc->idmaps[idmap_idx].inst)) {
                inst_idx = si->idx;
                idmap = sig->loc->idmaps[idmap_idx].map;
                mpr_local_slot lslot = slot->loc;
                mpr_value_set_sample(&lslot->val, inst_idx, argv[0], ts);
                set_bitflag(map->loc->updated_inst, inst_idx);
                map->loc->updated = 1;
            }
            if (!all)
                break;
        }
        return 0;
    }

    for (; idmap_idx < sig->loc->idmap_len; idmap_idx++) {
        // check if map instance is active
        if ((si = sig->loc->idmaps[idmap_idx].inst)) {
            inst_idx = si->idx;
            idmap = sig->loc->idmaps[idmap_idx].map;
            for (i = 0; i < sig->len; i++) {
                if (types[i] == MPR_NULL)
                    continue;
                memcpy(si->val + i * size, argv[i], size);
                set_bitflag(si->has_val_flags, i);
            }
            if (!compare_bitflags(si->has_val_flags, sig->loc->vec_known, sig->len))
                si->has_val = 1;
            if (si->has_val) {
                memcpy(&si->time, &ts, sizeof(mpr_time));
                mpr_sig_call_handler(sig, MPR_SIG_UPDATE, idmap->LID, sig->len, si->val, &ts, diff);
                // Pass this update downstream if signal is an input and was not updated in handler.
                if (   !(sig->dir & MPR_DIR_OUT)
                    && !get_bitflag(sig->loc->updated_inst, si->idx)) {
                    mpr_rtr_process_sig(rtr, sig, idmap_idx, si->val, ts);
                    // TODO: ensure update is propagated within this poll cycle
                }
            }
        }
        if (!all)
            break;
    }
    return 0;
}

mpr_id mpr_dev_get_unused_sig_id(mpr_dev dev)
{
    int done = 0;
    mpr_id id;
    while (!done) {
        done = 1;
        id = mpr_dev_generate_unique_id(dev);
        // check if input signal exists with this id
        mpr_list l = mpr_dev_get_sigs(dev, MPR_DIR_ANY);
        while (l) {
            if ((*l)->id == id) {
                done = 0;
                mpr_list_free(l);
                break;
            }
            l = mpr_list_get_next(l);
        }
    }
    return id;
}

void mpr_dev_add_sig_methods(mpr_dev dev, mpr_sig sig)
{
    RETURN_UNLESS(sig && sig->loc);
    mpr_net net = &dev->obj.graph->net;
    DEV_SERVER_FUNC(add_method, sig->path, NULL, mpr_dev_handler, (void*)sig);
    ++dev->loc->n_output_callbacks;
}

void mpr_dev_remove_sig_methods(mpr_dev dev, mpr_sig sig)
{
    RETURN_UNLESS(sig && sig->loc);
    mpr_net net = &dev->obj.graph->net;
    char *path = 0;
    int len = (int)strlen(sig->path) + 5;
    path = (char*)realloc(path, len);
    snprintf(path, len, "%s%s", sig->path, "/get");
    DEV_SERVER_FUNC(del_method, path, NULL);
    free(path);
    DEV_SERVER_FUNC(del_method, sig->path, NULL);
    --dev->loc->n_output_callbacks;
}

mpr_list mpr_dev_get_sigs(mpr_dev dev, mpr_dir dir)
{
    RETURN_UNLESS(dev && dev->obj.graph->sigs, 0);
    mpr_list qry = mpr_list_new_query((const void**)&dev->obj.graph->sigs,
                                      cmp_qry_dev_sigs, "hi", dev->obj.id, dir);
    return mpr_list_start(qry);
}

mpr_sig mpr_dev_get_sig_by_name(mpr_dev dev, const char *sig_name)
{
    RETURN_UNLESS(dev, 0);
    mpr_list sigs = mpr_list_from_data(dev->obj.graph->sigs);
    while (sigs) {
        mpr_sig sig = (mpr_sig)*sigs;
        if ((sig->dev == dev) && strcmp(sig->name, skip_slash(sig_name))==0)
            return sig;
        sigs = mpr_list_get_next(sigs);
    }
    return 0;
}

static int cmp_qry_dev_maps(const void *context_data, mpr_map map)
{
    mpr_id dev_id = *(mpr_id*)context_data;
    mpr_dir dir = *(int*)(context_data + sizeof(mpr_id));
    int i;
    if (dir == MPR_DIR_BOTH) {
        RETURN_UNLESS(map->dst->sig->dev->obj.id == dev_id, 0);
        for (i = 0; i < map->num_src; i++)
            RETURN_UNLESS(map->src[i]->sig->dev->obj.id == dev_id, 0);
        return 1;
    }
    if (dir & MPR_DIR_OUT) {
        for (i = 0; i < map->num_src; i++)
            RETURN_UNLESS(map->src[i]->sig->dev->obj.id != dev_id, 1);
    }
    if (dir & MPR_DIR_IN)
        RETURN_UNLESS(map->dst->sig->dev->obj.id != dev_id, 1);
    return 0;
}

mpr_list mpr_dev_get_maps(mpr_dev dev, mpr_dir dir)
{
    RETURN_UNLESS(dev && dev->obj.graph->maps, 0);
    mpr_list qry = mpr_list_new_query((const void**)&dev->obj.graph->maps,
                                      cmp_qry_dev_maps, "hi", dev->obj.id, dir);
    return mpr_list_start(qry);
}

static int cmp_qry_dev_links(const void *context_data, mpr_link link)
{
    mpr_id dev_id = *(mpr_id*)context_data;
    mpr_dir dir = *(int*)(context_data + sizeof(mpr_id));
    if (link->devs[0]->obj.id == dev_id) {
        switch (dir) {
            case MPR_DIR_BOTH:  return link->num_maps[0] && link->num_maps[1];
            case MPR_DIR_IN:    return link->num_maps[1];
            case MPR_DIR_OUT:   return link->num_maps[0];
            default:            return 1;
        }
    }
    else if (link->devs[1]->obj.id == dev_id) {
        switch (dir) {
            case MPR_DIR_BOTH:  return link->num_maps[0] && link->num_maps[1];
            case MPR_DIR_IN:    return link->num_maps[0];
            case MPR_DIR_OUT:   return link->num_maps[1];
            default:            return 1;
        }
    }
    return 0;
}

mpr_list mpr_dev_get_links(mpr_dev dev, mpr_dir dir)
{
    RETURN_UNLESS(dev && dev->obj.graph->links, 0);
    mpr_list qry = mpr_list_new_query((const void**)&dev->obj.graph->links,
                                      cmp_qry_dev_links, "hi", dev->obj.id, dir);
    return mpr_list_start(qry);
}

mpr_link mpr_dev_get_link_by_remote(mpr_dev dev, mpr_dev remote)
{
    RETURN_UNLESS(dev, 0);
    mpr_list links = mpr_list_from_data(dev->obj.graph->links);
    while (links) {
        mpr_link link = (mpr_link)*links;
        if (link->devs[0] == dev && link->devs[1] == remote)
            return link;
        if (link->devs[1] == dev && link->devs[0] == remote)
            return link;
        links = mpr_list_get_next(links);
    }
    return 0;
}

// TODO: handle interrupt-driven updates that omit call to this function
static inline void mpr_dev_process_inputs_internal(mpr_dev dev)
{
//    RETURN_UNLESS(dev->loc->updated, 0);
    mpr_graph graph = dev->obj.graph;
    // process and send updated maps
    // TODO: speed this up!
    mpr_list maps = mpr_list_from_data(graph->maps);
    while (maps) {
        mpr_map map = *(mpr_map*)maps;
        maps = mpr_list_get_next(maps);
        mpr_map_receive(map, dev->loc->time);
    }
//    mpr_list links = mpr_list_from_data(graph->links);
//    int msgs = 0;
//    while (links) {
//        msgs += mpr_link_process_bundles((mpr_link)*links, dev->loc->time, idx);
//        links = mpr_list_get_next(links);
//    }
//    dev->loc->updated = 0;
//    return msgs ? 1 : 0;
}

// TODO: handle interrupt-driven updates that omit call to this function
static inline int mpr_dev_process_outputs_internal(mpr_dev dev)
{
//    RETURN_UNLESS(dev->loc->updated, 0);
    mpr_graph graph = dev->obj.graph;
    // process and send updated maps
    // TODO: speed this up!
    mpr_list maps = mpr_list_from_data(graph->maps);
    while (maps) {
        mpr_map map = *(mpr_map*)maps;
        maps = mpr_list_get_next(maps);
        mpr_map_send(map, dev->loc->time);
    }
    mpr_list links = mpr_list_from_data(graph->links);
    int msgs = 0;
    while (links) {
        msgs += mpr_link_process_bundles((mpr_link)*links, dev->loc->time, 0);
        links = mpr_list_get_next(links);
    }
    dev->loc->updated = 0;
    return msgs ? 1 : 0;
}

void mpr_dev_process_outputs(mpr_dev dev) {
    RETURN_UNLESS(dev && dev->loc);
    dev->loc->time_is_stale = 1;
    if (!dev->loc->polling)
        mpr_dev_process_outputs_internal(dev);
}

int mpr_dev_poll(mpr_dev dev, int block_ms)
{
    RETURN_UNLESS(dev && dev->loc, 0);

    int admin_count = 0, device_count = 0, status[4];
    mpr_net net = &dev->obj.graph->net;
    mpr_net_poll(net);

    if (!dev->loc->registered) {
        if (lo_servers_recv_noblock(net->server.admin, status, 2, block_ms)) {
            admin_count = (status[0] > 0) + (status[1] > 0);
            net->msgs_recvd |= admin_count;
        }
        dev->loc->bundle_idx = 1;
        return admin_count;
    }

    dev->loc->polling = 1;
    dev->loc->time_is_stale = 1;
    mpr_dev_process_outputs_internal(dev);
    dev->loc->polling = 0;

    if (!block_ms) {
        if (lo_servers_recv_noblock(net->server.all, status, 4, 0)) {
            admin_count = (status[0] > 0) + (status[1] > 0);
            device_count = (status[2] > 0) + (status[3] > 0);
            net->msgs_recvd |= admin_count;
        }
    }
    else {
        double then = mpr_get_current_time();
        int left_ms = block_ms, elapsed, checked_admin = 0;
        while (left_ms > 0) {
            // set timeout to a maximum of 100ms
            if (left_ms > 100)
                left_ms = 100;
            dev->loc->polling = 1;
            if (lo_servers_recv_noblock(net->server.all, status, 4, left_ms)) {
                admin_count += (status[0] > 0) + (status[1] > 0);
                device_count += (status[2] > 0) + (status[3] > 0);
            }
            // check if any signal update bundles need to be sent
            mpr_dev_process_inputs_internal(dev);
            mpr_dev_process_outputs_internal(dev);
            dev->loc->polling = 0;

            elapsed = (mpr_get_current_time() - then) * 1000;
            if ((elapsed - checked_admin) > 100) {
                mpr_net_poll(net);
                checked_admin = elapsed;
            }
            left_ms = block_ms - elapsed;
        }
    }

    /* When done, or if non-blocking, check for remaining messages up to a
     * proportion of the number of input signals. Arbitrarily choosing 1 for
     * now, but perhaps could be a heuristic based on a recent number of
     * messages per channel per poll. */
    while (device_count < (dev->num_inputs + dev->loc->n_output_callbacks)*1
           && (lo_servers_recv_noblock(net->server.dev, &status[2], 2, 0)))
        device_count += (status[2] > 0) + (status[3] > 0);

    // process incoming maps
    dev->loc->polling = 1;
    mpr_dev_process_inputs_internal(dev);
    dev->loc->polling = 0;

    if (dev->obj.props.synced->dirty && mpr_dev_get_is_ready(dev) && dev->loc->subscribers) {
        // inform device subscribers of changed properties
        mpr_net_use_subscribers(net, dev, MPR_DEV);
        mpr_dev_send_state(dev, MSG_DEV);
    }

    net->msgs_recvd |= admin_count;
    return admin_count + device_count;
}

mpr_time mpr_dev_get_time(mpr_dev dev)
{
    RETURN_UNLESS(dev && dev->loc, MPR_NOW);
    if (dev->loc->time_is_stale)
        mpr_dev_set_time(dev, MPR_NOW);
    return dev->loc->time;
}

void mpr_dev_set_time(mpr_dev dev, mpr_time time)
{
    RETURN_UNLESS(dev && dev->loc && memcmp(&time, &dev->loc->time, sizeof(mpr_time)));
    mpr_time_set(&dev->loc->time, time);
    dev->loc->time_is_stale = 0;
    if (!dev->loc->polling)
        mpr_dev_process_outputs_internal(dev);
}

void mpr_dev_reserve_idmap(mpr_dev dev)
{
    mpr_id_map map;
    map = (mpr_id_map)calloc(1, sizeof(mpr_id_map_t));
    map->next = dev->loc->idmaps.reserve;
    dev->loc->idmaps.reserve = map;
}

mpr_id_map mpr_dev_add_idmap(mpr_dev dev, int group, mpr_id LID, mpr_id GID)
{
    if (!dev->loc->idmaps.reserve)
        mpr_dev_reserve_idmap(dev);

    mpr_id_map map = dev->loc->idmaps.reserve;
    map->LID = LID;
    map->GID = GID;
    map->LID_refcount = 1;
    map->GID_refcount = 0;
    dev->loc->idmaps.reserve = map->next;
    map->next = dev->loc->idmaps.active[group];
    dev->loc->idmaps.active[group] = map;
    return map;
}

static void mpr_dev_remove_idmap(mpr_dev dev, int group, mpr_id_map rem)
{
    mpr_id_map *map = &dev->loc->idmaps.active[group];
    while (*map) {
        if ((*map) == rem) {
            *map = (*map)->next;
            rem->next = dev->loc->idmaps.reserve;
            dev->loc->idmaps.reserve = rem;
            break;
        }
        map = &(*map)->next;
    }
}

int mpr_dev_LID_decref(mpr_dev dev, int group, mpr_id_map map)
{
    --map->LID_refcount;
    if (map->LID_refcount <= 0) {
        map->LID_refcount = 0;
        if (map->GID_refcount <= 0) {
            mpr_dev_remove_idmap(dev, group, map);
            return 1;
        }
    }
    return 0;
}

int mpr_dev_GID_decref(mpr_dev dev, int group, mpr_id_map map)
{
    --map->GID_refcount;
    if (map->GID_refcount <= 0) {
        map->GID_refcount = 0;
        if (map->LID_refcount <= 0) {
            mpr_dev_remove_idmap(dev, group, map);
            return 1;
        }
    }
    return 0;
}

mpr_id_map mpr_dev_get_idmap_by_LID(mpr_dev dev, int group, mpr_id LID)
{
    mpr_id_map map = dev->loc->idmaps.active[group];
    while (map) {
        if (map->LID == LID)
            return map;
        map = map->next;
    }
    return 0;
}

mpr_id_map mpr_dev_get_idmap_by_GID(mpr_dev dev, int group, mpr_id GID)
{
    mpr_id_map map = dev->loc->idmaps.active[group];
    while (map) {
        if (map->GID == GID)
            return map;
        map = map->next;
    }
    return 0;
}

/* Internal LibLo error handler */
static void handler_error(int num, const char *msg, const char *where)
{
    trace_net("[libmapper] liblo server error %d in path %s: %s\n", num, where, msg);
}

void mpr_dev_start_servers(mpr_dev dev)
{
    mpr_net net = &dev->obj.graph->net;
    RETURN_UNLESS(!net->server.udp && !net->server.tcp);

    char port[16], *pport = 0;
    while (!(net->server.udp = lo_server_new(pport, handler_error)))
        pport = 0;
    snprintf(port, 16, "%d", lo_server_get_port(net->server.udp));
    pport = port;
    while (!(net->server.tcp = lo_server_new_with_proto(pport, LO_TCP, handler_error)))
        pport = 0;

    // Disable liblo message queueing
    DEV_SERVER_FUNC(enable_queue, 0, 1);

    // Add bundle handlers
    DEV_SERVER_FUNC(add_bundle_handlers, mpr_dev_bundle_start, NULL, (void*)dev);

    int portnum = lo_server_get_port(net->server.udp);
    mpr_tbl_set(dev->obj.props.synced, PROP(PORT), NULL, 1, MPR_INT32, &portnum,
                NON_MODIFIABLE);

    trace_dev(dev, "bound to UDP port %i\n", portnum);
    trace_dev(dev, "bound to TCP port %i\n", lo_server_get_port(net->server.tcp));

    char *url = lo_server_get_url(net->server.udp);
    char *host = lo_url_get_hostname(url);
    mpr_tbl_set(dev->obj.props.synced, PROP(HOST), NULL, 1, MPR_STR, host,
                NON_MODIFIABLE);
    free(host);
    free(url);

    // add signal methods
    mpr_list sigs = mpr_dev_get_sigs(dev, MPR_DIR_ANY);
    while (sigs) {
        mpr_sig sig = (mpr_sig)*sigs;
        sigs = mpr_list_get_next(sigs);
        if (sig->loc->handler)
            DEV_SERVER_FUNC(add_method, sig->path, NULL, mpr_dev_handler,
                            (void*)sig);
    }
}

const char *mpr_dev_get_name(mpr_dev dev)
{
    RETURN_UNLESS(!dev->loc || (dev->loc->registered && dev->loc->ordinal.locked), 0);
    if (dev->name)
        return dev->name;
    unsigned int len = strlen(dev->prefix) + 6;
    dev->name = (char*)malloc(len);
    dev->name[0] = 0;
    snprintf(dev->name, len, "%s.%d", dev->prefix, dev->loc->ordinal.val);
    return dev->name;
}

int mpr_dev_get_is_ready(mpr_dev dev)
{
    return dev ? dev->status >= MPR_STATUS_READY : MPR_STATUS_UNDEFINED;
}

mpr_id mpr_dev_generate_unique_id(mpr_dev dev)
{
    RETURN_UNLESS(dev, 0);
    mpr_id id = ++dev->obj.graph->resource_counter;
    if (dev->loc && dev->loc->registered)
        id |= dev->obj.id;
    return id;
}

void mpr_dev_send_state(mpr_dev dev, net_msg_t cmd)
{
    RETURN_UNLESS(dev);
    NEW_LO_MSG(msg, return);

    /* device name */
    lo_message_add_string(msg, mpr_dev_get_name(dev));

    /* properties */
    mpr_tbl_add_to_msg(dev->loc ? dev->obj.props.synced : 0, dev->obj.props.staged, msg);

    mpr_net net = &dev->obj.graph->net;
    if (cmd == MSG_DEV_MOD) {
        char str[1024];
        snprintf(str, 1024, "/%s/modify", dev->name);
        mpr_net_add_msg(net, str, 0, msg);
        mpr_net_send(net);
    }
    else
        mpr_net_add_msg(net, 0, cmd, msg);

    dev->obj.props.synced->dirty = 0;
}

int mpr_dev_add_link(mpr_dev dev, mpr_dev rem)
{
    int i;
    for (i = 0; i < dev->num_linked; i++) {
        if (dev->linked[i] && dev->linked[i]->obj.id == rem->obj.id)
            return 0;
    }

    // not found - add a new linked device
    i = ++dev->num_linked;
    dev->linked = realloc(dev->linked, i * sizeof(mpr_dev));
    dev->linked[i-1] = rem;
    return 1;
}

void mpr_dev_remove_link(mpr_dev dev, mpr_dev rem)
{
    int i, j;
    for (i = 0; i < dev->num_linked; i++) {
        if (!dev->linked[i] || dev->linked[i]->obj.id != rem->obj.id)
            continue;
        for (j = i+1; j < dev->num_linked; j++)
            dev->linked[j-1] = dev->linked[j];
        --dev->num_linked;
        dev->linked = realloc(dev->linked, dev->num_linked * sizeof(mpr_dev));
        dev->obj.props.synced->dirty = 1;
        return;
    }
}

static int mpr_dev_update_linked(mpr_dev dev, mpr_msg_atom a)
{
    int i, j, updated = 0, num = a->len;
    lo_arg **link_list = a->vals;
    if (link_list && *link_list) {
        if (num == 1 && strcmp(&link_list[0]->s, "none")==0)
            num = 0;
        const char *name;

        // Remove any old links that are missing
        for (i = 0; ; i++) {
            if (i >= dev->num_linked)
                break;
            int found = 0;
            for (j = 0; j < num; j++) {
                name = &link_list[j]->s;
                name = name[0] == '/' ? name + 1 : name;
                if (0 == strcmp(name, dev->linked[i]->name)) {
                    found = 1;
                    break;
                }
            }
            if (!found) {
                for (j = i+1; j < dev->num_linked; j++)
                    dev->linked[j-1] = dev->linked[j];
                --dev->num_linked;
                ++updated;
            }
        }
        if (updated)
            dev->linked = realloc(dev->linked, dev->num_linked * sizeof(mpr_dev));
        // Add any new links
        mpr_dev rem;
        for (i = 0; i < num; i++) {
            if ((rem = mpr_graph_add_dev(dev->obj.graph, &link_list[i]->s, 0)))
                updated += mpr_dev_add_link(dev, rem);
        }
    }
    return updated;
}

/*! Update information about a device record based on message properties. */
int mpr_dev_set_from_msg(mpr_dev dev, mpr_msg m)
{
    RETURN_UNLESS(m, 0);
    int i, updated = 0;
    mpr_msg_atom a;
    for (i = 0; i < m->num_atoms; i++) {
        a = &m->atoms[i];
        switch (MASK_PROP_BITFLAGS(a->prop)) {
            case PROP(LINKED):
                if (mpr_type_get_is_str(a->types[0]))
                    updated += mpr_dev_update_linked(dev, a);
                break;
            default:
                updated += mpr_tbl_set_from_atom(dev->obj.props.synced, a, REMOTE_MODIFY);
                break;
        }
    }
    return updated;
}

static int mpr_dev_send_sigs(mpr_dev dev, mpr_dir dir)
{
    mpr_list l = mpr_dev_get_sigs(dev, dir);
    while (l) {
        mpr_sig_send_state((mpr_sig)*l, MSG_SIG);
        l = mpr_list_get_next(l);
    }
    return 0;
}

static int mpr_dev_send_maps(mpr_dev dev, mpr_dir dir)
{
    mpr_list l = mpr_dev_get_maps(dev, dir);
    while (l) {
        mpr_map_send_state((mpr_map)*l, -1, MSG_MAPPED);
        l = mpr_list_get_next(l);
    }
    return 0;
}

// Add/renew/remove a subscription.
void mpr_dev_manage_subscriber(mpr_dev dev, lo_address addr, int flags,
                               int timeout_sec, int revision)
{
    mpr_subscriber *s = &dev->loc->subscribers;
    const char *ip = lo_address_get_hostname(addr);
    const char *port = lo_address_get_port(addr);
    RETURN_UNLESS(ip && port);

    mpr_time t;
    mpr_time_set(&t, MPR_NOW);

    while (*s) {
        const char *s_ip = lo_address_get_hostname((*s)->addr);
        const char *s_port = lo_address_get_port((*s)->addr);
        if (strcmp(ip, s_ip)==0 && strcmp(port, s_port)==0) {
            // subscriber already exists
            if (!flags || !timeout_sec) {
                trace_dev(dev, "removing subscription from %s:%s\n", s_ip, s_port);
                // remove subscription
                mpr_subscriber temp = *s;
                int prev_flags = temp->flags;
                *s = temp->next;
                FUNC_IF(lo_address_free, temp->addr);
                free(temp);
                RETURN_UNLESS(flags && (flags &= ~prev_flags));
            }
            else {
                // reset timeout
#ifdef DEBUG
                trace_dev(dev, "renewing subscription from %s:%s for %d seconds"
                          "with flags ", s_ip, s_port, timeout_sec);
                print_subscription_flags(flags);
#endif
                (*s)->lease_exp = t.sec + timeout_sec;
                int temp = flags;
                flags &= ~(*s)->flags;
                (*s)->flags = temp;
            }
            break;
        }
        s = &(*s)->next;
    }

    RETURN_UNLESS(flags);

    if (!(*s) && timeout_sec) {
        // add new subscriber
#ifdef DEBUG
        trace_dev(dev, "adding new subscription from %s:%s with flags ", ip, port);
        print_subscription_flags(flags);
#endif
        mpr_subscriber sub = malloc(sizeof(struct _mpr_subscriber));
        sub->addr = lo_address_new(ip, port);
        sub->lease_exp = t.sec + timeout_sec;
        sub->flags = flags;
        sub->next = dev->loc->subscribers;
        dev->loc->subscribers = sub;
    }

    // bring new subscriber up to date
    mpr_net net = &dev->obj.graph->net;
    mpr_net_use_mesh(net, addr);
    mpr_dev_send_state(dev, MSG_DEV);
    mpr_net_send(net);

    if (flags & MPR_SIG) {
        mpr_dir dir = 0;
        if (flags & MPR_SIG_IN)
            dir |= MPR_DIR_IN;
        if (flags & MPR_SIG_OUT)
            dir |= MPR_DIR_OUT;
        mpr_net_use_mesh(net, addr);
        mpr_dev_send_sigs(dev, dir);
        mpr_net_send(net);
    }
    if (flags & MPR_MAP) {
        mpr_dir dir = 0;
        if (flags & MPR_MAP_IN)
            dir |= MPR_DIR_IN;
        if (flags & MPR_MAP_OUT)
            dir |= MPR_DIR_OUT;
        mpr_net_use_mesh(net, addr);
        mpr_dev_send_maps(dev, dir);
        mpr_net_send(net);
    }
}
