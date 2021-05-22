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

/* prototypes */
void mpr_dev_start_servers(mpr_local_dev dev);
static void mpr_dev_remove_idmap(mpr_local_dev dev, int group, mpr_id_map rem);
MPR_INLINE static int _process_outgoing_maps(mpr_local_dev dev);

mpr_time ts = {0,1};

static int cmp_qry_linked(const void *ctx, mpr_dev dev)
{
    int i;
    mpr_dev self = *(mpr_dev*)ctx;
    for (i = 0; i < self->num_linked; i++) {
        if (!self->linked[i] || self->linked[i]->obj.id == dev->obj.id)
            return 1;
    }
    return 0;
}

static int cmp_qry_dev_sigs(const void *context_data, mpr_sig sig)
{
    mpr_id dev_id = *(mpr_id*)context_data;
    int dir = *(int*)((char*)context_data + sizeof(mpr_id));
    return ((dir & sig->dir) && (dev_id == sig->dev->obj.id));
}

void init_dev_prop_tbl(mpr_dev dev)
{
    int mod = dev->is_local ? NON_MODIFIABLE : MODIFIABLE;
    mpr_tbl tbl;
    mpr_list qry;

    dev->obj.props.synced = mpr_tbl_new();
    if (!dev->is_local)
        dev->obj.props.staged = mpr_tbl_new();
    tbl = dev->obj.props.synced;

    /* these properties need to be added in alphabetical order */
    mpr_tbl_link(tbl, PROP(DATA), 1, MPR_PTR, &dev->obj.data,
                 LOCAL_MODIFY | INDIRECT | LOCAL_ACCESS_ONLY);
    mpr_tbl_link(tbl, PROP(ID), 1, MPR_INT64, &dev->obj.id, mod);
    qry = mpr_list_new_query((const void**)&dev->obj.graph->devs, (void*)cmp_qry_linked, "v", &dev);
    mpr_tbl_link(tbl, PROP(LINKED), 1, MPR_LIST, qry, NON_MODIFIABLE | PROP_OWNED);
    mpr_tbl_link(tbl, PROP(NAME), 1, MPR_STR, &dev->name, mod | INDIRECT | LOCAL_ACCESS_ONLY);
    mpr_tbl_link(tbl, PROP(NUM_MAPS_IN), 1, MPR_INT32, &dev->num_maps_in, mod);
    mpr_tbl_link(tbl, PROP(NUM_MAPS_OUT), 1, MPR_INT32, &dev->num_maps_out, mod);
    mpr_tbl_link(tbl, PROP(NUM_SIGS_IN), 1, MPR_INT32, &dev->num_inputs, mod);
    mpr_tbl_link(tbl, PROP(NUM_SIGS_OUT), 1, MPR_INT32, &dev->num_outputs, mod);
    mpr_tbl_link(tbl, PROP(ORDINAL), 1, MPR_INT32, &dev->ordinal, mod);
    if (!dev->is_local) {
        qry = mpr_list_new_query((const void**)&dev->obj.graph->sigs, (void*)cmp_qry_dev_sigs,
                                 "hi", dev->obj.id, MPR_DIR_ANY);
        mpr_tbl_link(tbl, PROP(SIG), 1, MPR_LIST, qry, NON_MODIFIABLE | PROP_OWNED);
    }
    mpr_tbl_link(tbl, PROP(STATUS), 1, MPR_INT32, &dev->status, mod | LOCAL_ACCESS_ONLY);
    mpr_tbl_link(tbl, PROP(SYNCED), 1, MPR_TIME, &dev->synced, mod | LOCAL_ACCESS_ONLY);
    mpr_tbl_link(tbl, PROP(VERSION), 1, MPR_INT32, &dev->obj.version, mod);

    if (dev->is_local)
        mpr_tbl_set(tbl, PROP(LIBVER), NULL, 1, MPR_STR, PACKAGE_VERSION, NON_MODIFIABLE);
    mpr_tbl_set(tbl, PROP(IS_LOCAL), NULL, 1, MPR_BOOL, &dev->is_local,
                LOCAL_ACCESS_ONLY | NON_MODIFIABLE);
}

/*! Allocate and initialize a device. This function is called to create a new
 *  mpr_dev, not to create a representation of remote devices. */
mpr_dev mpr_dev_new(const char *name_prefix, mpr_graph g)
{
    mpr_local_dev dev;
    RETURN_ARG_UNLESS(name_prefix, 0);
    if (name_prefix[0] == '/')
        ++name_prefix;
    TRACE_RETURN_UNLESS(!strchr(name_prefix, '/'), NULL, "error: character '/' "
                        "is not permitted in device name.\n");
    if (!g) {
        g = mpr_graph_new(0);
        g->own = 0;
    }

    dev = (mpr_local_dev)mpr_list_add_item((void**)&g->devs, sizeof(mpr_local_dev_t));
    dev->obj.type = MPR_DEV;
    dev->obj.graph = g;
    dev->is_local = 1;

    init_dev_prop_tbl((mpr_dev)dev);

    dev->prefix = strdup(name_prefix);
    mpr_dev_start_servers(dev);

    if (!g->net.servers[SERVER_UDP] || !g->net.servers[SERVER_TCP]) {
        mpr_dev_free((mpr_dev)dev);
        return NULL;
    }

    g->net.rtr = (mpr_rtr)calloc(1, sizeof(mpr_rtr_t));
    g->net.rtr->dev = dev;

    dev->expr_stack = mpr_expr_stack_new();

    dev->ordinal_allocator.val = 1;
    dev->idmaps.active = (mpr_id_map*) malloc(sizeof(mpr_id_map));
    dev->idmaps.active[0] = 0;
    dev->num_sig_groups = 1;

    mpr_net_add_dev(&g->net, dev);

    dev->status = MPR_STATUS_STAGED;
    return (mpr_dev)dev;
}

/*! Free resources used by a mpr device. */
void mpr_dev_free(mpr_dev dev)
{
    mpr_graph gph;
    mpr_net net;
    mpr_local_dev ldev;
    mpr_list list;
    int i;
    RETURN_UNLESS(dev && dev->is_local);
    if (!dev->obj.graph) {
        free(dev);
        return;
    }
    ldev = (mpr_local_dev)dev;
    gph = dev->obj.graph;
    net = &gph->net;

    /* free any queued graph messages without sending */
    mpr_net_free_msgs(net);

    /* remove OSC handlers associated with this device */
    mpr_net_remove_dev_methods(net, ldev);

    /* also remove any graph handlers registered locally */
    while (gph->callbacks) {
        fptr_list cb = gph->callbacks;
        gph->callbacks = gph->callbacks->next;
        free(cb);
    }

    /* remove subscribers */
    while (ldev->subscribers) {
        mpr_subscriber sub = ldev->subscribers;
        FUNC_IF(lo_address_free, sub->addr);
        ldev->subscribers = sub->next;
        free(sub);
    }

    list = mpr_dev_get_sigs(dev, MPR_DIR_ANY);
    while (list) {
        mpr_local_sig sig = (mpr_local_sig)*list;
        list = mpr_list_get_next(list);
        if (sig->is_local) {
            /* release active instances */
            for (i = 0; i < sig->idmap_len; i++) {
                if (sig->idmaps[i].inst)
                    mpr_sig_release_inst_internal(sig, i);
            }
        }
        mpr_sig_free((mpr_sig)sig);
    }

    if (ldev->registered) {
        /* A registered device must tell the network it is leaving. */
        NEW_LO_MSG(msg, ;)
        if (msg) {
            mpr_net_use_bus(net);
            lo_message_add_string(msg, mpr_dev_get_name(dev));
            mpr_net_add_msg(net, 0, MSG_LOGOUT, msg);
            mpr_net_send(net);
        }
    }

    /* Release links to other devices */
    list = mpr_dev_get_links(dev, MPR_DIR_ANY);
    while (list) {
        mpr_link link = (mpr_link)*list;
        list = mpr_list_get_next(list);
        _process_outgoing_maps(ldev);
        mpr_graph_remove_link(gph, link, MPR_OBJ_REM);
    }

    /* Release device id maps */
    for (i = 0; i < ldev->num_sig_groups; i++) {
        while (ldev->idmaps.active[i]) {
            mpr_id_map map = ldev->idmaps.active[i];
            ldev->idmaps.active[i] = map->next;
            free(map);
        }
    }
    free(ldev->idmaps.active);

    while (ldev->idmaps.reserve) {
        mpr_id_map map = ldev->idmaps.reserve;
        ldev->idmaps.reserve = map->next;
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

    FUNC_IF(lo_server_free, net->servers[SERVER_UDP]);
    FUNC_IF(lo_server_free, net->servers[SERVER_TCP]);
    FUNC_IF(free, dev->prefix);

    mpr_expr_stack_free(ldev->expr_stack);

    mpr_graph_remove_dev(gph, dev, MPR_OBJ_REM, 1);
    if (!gph->own)
        mpr_graph_free(gph);
}

void mpr_dev_on_registered(mpr_local_dev dev)
{
    int i;
    mpr_list qry;
    /* Add unique device id to locally-activated signal instances. */
    mpr_list sigs = mpr_dev_get_sigs((mpr_dev)dev, MPR_DIR_ANY);
    while (sigs) {
        mpr_local_sig sig = (mpr_local_sig)*sigs;
        sigs = mpr_list_get_next(sigs);
        for (i = 0; i < sig->idmap_len; i++) {
            mpr_id_map idmap = sig->idmaps[i].map;
            if (idmap && !(idmap->GID >> 32))
                idmap->GID |= dev->obj.id;
        }
        sig->obj.id |= dev->obj.id;
    }
    qry = mpr_list_new_query((const void**)&dev->obj.graph->sigs, (void*)cmp_qry_dev_sigs,
                             "hi", dev->obj.id, MPR_DIR_ANY);
    mpr_tbl_set(dev->obj.props.synced, PROP(SIG), NULL, 1, MPR_LIST, qry,
                NON_MODIFIABLE | PROP_OWNED);
    dev->registered = 1;
    dev->ordinal = dev->ordinal_allocator.val;
    dev->status = MPR_STATUS_READY;
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
    mpr_local_sig sig = (mpr_local_sig)data;
    mpr_local_dev dev;
    mpr_sig_inst si;
    mpr_rtr rtr = sig->obj.graph->net.rtr;
    int i, val_len = 0, vals, size, all;
    int idmap_idx, inst_idx, slot_idx = -1, map_manages_inst = 0;
    mpr_id GID = 0;
    mpr_id_map idmap;
    mpr_local_map map = 0;
    mpr_local_slot slot = 0;
    float diff;

    TRACE_RETURN_UNLESS(sig && (dev = sig->dev), 0,
                        "error in mpr_dev_handler, cannot retrieve user data\n");
    TRACE_DEV_RETURN_UNLESS(sig->num_inst, 0, "signal '%s' has no instances.\n", sig->name);
    RETURN_ARG_UNLESS(argc, 0);

    /* We need to consider that there may be properties appended to the msg
     * check length and find properties if any */
    while (val_len < argc && types[val_len] != MPR_STR)
        ++val_len;
    i = val_len;
    while (i < argc) {
        /* Parse any attached properties (instance ids, slot number) */
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
        /* retrieve mapping associated with this slot */
        slot = mpr_rtr_get_slot(rtr, sig, slot_idx);
        TRACE_DEV_RETURN_UNLESS(slot, 0, "error in mpr_dev_handler: slot %d not found.\n", slot_idx);
        map = slot->map;
        TRACE_DEV_RETURN_UNLESS(map->status >= MPR_STATUS_READY, 0, "error in mpr_dev_handler: "
                                "mapping not yet ready.\n");
        if (map->expr && !map->is_local_only) {
            vals = check_types(types, val_len, slot->sig->type, slot->sig->len);
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
        idmap_idx = mpr_sig_get_idmap_with_GID(sig, GID, RELEASED_LOCALLY, ts, 0);
        if (idmap_idx < 0) {
            /* No instance found with this map – don't activate instance just to release it again */
            RETURN_ARG_UNLESS(vals && sig->dir == MPR_DIR_IN, 0);

            if (map_manages_inst && vals == slot->sig->len) {
                /* special case: do a dry-run to check whether this map will
                 * cause a release. If so, don't bother stealing an instance. */
                mpr_value *src;
                mpr_value_t v = {0, 0, 1, 0, 1};
                mpr_value_buffer_t b = {0, 0, -1};
                b.samps = argv[0];
                v.inst = &b;
                v.vlen = val_len;
                v.type = slot->sig->type;
                src = alloca(map->num_src * sizeof(mpr_value));
                for (i = 0; i < map->num_src; i++)
                    src[i] = (i == slot->id) ? &v : 0;
                if (mpr_expr_eval(dev->expr_stack, map->expr, src, 0, 0, 0, 0, 0) & EXPR_RELEASE_BEFORE_UPDATE)
                    return 0;
            }

            /* otherwise try to init reserved/stolen instance with device map */
            idmap_idx = mpr_sig_get_idmap_with_GID(sig, GID, 0, ts, 1);
            TRACE_DEV_RETURN_UNLESS(idmap_idx >= 0, 0,
                                    "no instances available for GUID %"PR_MPR_ID" (1)\n", GID);
        }
        else if (sig->idmaps[idmap_idx].status & RELEASED_LOCALLY) {
            /* map was already released locally, we are only interested in release messages */
            if (0 == vals) {
                /* we can clear signal's reference to map */
                idmap = sig->idmaps[idmap_idx].map;
                sig->idmaps[idmap_idx].map = 0;
                mpr_dev_GID_decref(dev, sig->group, idmap);
            }
            return 0;
        }
        TRACE_DEV_RETURN_UNLESS(sig->idmaps[idmap_idx].inst, 0,
                                "error in mpr_dev_handler: missing instance!\n");
    }
    else {
        /* use the first available instance */
        idmap_idx = 0;
        if (!sig->idmaps[0].inst)
            idmap_idx = mpr_sig_get_idmap_with_LID(sig, sig->inst[0]->id, 1, ts, 1);
        RETURN_ARG_UNLESS(idmap_idx >= 0, 0);
    }
    si = sig->idmaps[idmap_idx].inst;
    inst_idx = si->idx;
    diff = mpr_time_get_diff(ts, si->time);
    idmap = sig->idmaps[idmap_idx].map;

    size = mpr_type_get_size(map ? slot->sig->type : sig->type);
    if (vals == 0) {
        if (GID) {
            /* TODO: mark SLOT status as remotely released rather than map */
            sig->idmaps[idmap_idx].status |= RELEASED_REMOTELY;
            mpr_dev_GID_decref(dev, sig->group, idmap);
            if (!sig->use_inst) {
                /* clear signal's reference to idmap */
                mpr_dev_LID_decref(dev, sig->group, idmap);
                sig->idmaps[idmap_idx].map = 0;
                sig->idmaps[idmap_idx].inst->active = 0;
                sig->idmaps[idmap_idx].inst = 0;
                return 0;
            }
        }
        RETURN_ARG_UNLESS(sig->use_inst && (!map || map->use_inst), 0);

        /* Try to release instance, but do not call mpr_rtr_process_sig() here, since we don't
         * know if the local signal instance will actually be released. */
        if (sig->dir == MPR_DIR_IN) {
            int evt = (MPR_SIG_REL_UPSTRM & sig->event_flags) ? MPR_SIG_REL_UPSTRM : MPR_SIG_UPDATE;
            mpr_sig_call_handler(sig, evt, idmap->LID, 0, 0, &ts, diff);
        }
        else if (MPR_SIG_REL_DNSTRM & sig->event_flags)
            mpr_sig_call_handler(sig, MPR_SIG_REL_DNSTRM, idmap->LID, 0, 0, &ts, diff);

        RETURN_ARG_UNLESS(map && MPR_LOC_DST == map->process_loc && sig->dir == MPR_DIR_IN, 0);

        /* Reset memory for corresponding source slot. */
        /* TODO: make a function (reset) */
        mpr_value_reset_inst(&slot->val, inst_idx);
        return 0;
    }
    else if (sig->dir == MPR_DIR_OUT)
        return 0;

    /* Partial vector updates are not allowed in convergent maps since the slot value mirrors the
     * remote signal value. */
    if (map && vals != slot->sig->len) {
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
        all |= !map->use_inst || (!slot->sig->use_inst && map->num_src > 1 && map->num_inst > 1);
    }
    if (all)
        idmap_idx = 0;

    if (map) {
        for (; idmap_idx < sig->idmap_len; idmap_idx++) {
            /* check if map instance is active */
            if ((si = sig->idmaps[idmap_idx].inst)) {
                inst_idx = si->idx;
                /* Setting to local timestamp here */
                /* TODO: jitter mitigation etc. */
                mpr_value_set_samp(&slot->val, inst_idx, argv[0], dev->time);
                set_bitflag(map->updated_inst, inst_idx);
                map->updated = 1;
                dev->receiving = 1;
            }
            if (!all)
                break;
        }
        return 0;
    }

    for (; idmap_idx < sig->idmap_len; idmap_idx++) {
        /* check if instance is active */
        if ((si = sig->idmaps[idmap_idx].inst)) {
            idmap = sig->idmaps[idmap_idx].map;
            for (i = 0; i < sig->len; i++) {
                if (types[i] == MPR_NULL)
                    continue;
                memcpy((char*)si->val + i * size, argv[i], size);
                set_bitflag(si->has_val_flags, i);
            }
            if (!compare_bitflags(si->has_val_flags, sig->vec_known, sig->len))
                si->has_val = 1;
            if (si->has_val) {
                memcpy(&si->time, &ts, sizeof(mpr_time));
                mpr_sig_call_handler(sig, MPR_SIG_UPDATE, idmap->LID, sig->len, si->val, &ts, diff);
                /* Pass this update downstream if signal is an input and was not updated in handler. */
                if (   !(sig->dir & MPR_DIR_OUT)
                    && !get_bitflag(sig->updated_inst, si->idx)) {
                    mpr_rtr_process_sig(rtr, sig, idmap_idx, si->val, ts);
                    /* TODO: ensure update is propagated within this poll cycle */
                }
            }
        }
        if (!all)
            break;
    }
    return 0;
}

mpr_id mpr_dev_get_unused_sig_id(mpr_local_dev dev)
{
    int done = 0;
    mpr_id id;
    while (!done) {
        mpr_list l = mpr_dev_get_sigs((mpr_dev)dev, MPR_DIR_ANY);
        id = mpr_dev_generate_unique_id((mpr_dev)dev);
        /* check if input signal exists with this id */
        done = 1;
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

void mpr_dev_add_sig_methods(mpr_local_dev dev, mpr_local_sig sig)
{
    mpr_net net;
    RETURN_UNLESS(sig && sig->is_local);
    net = &dev->obj.graph->net;
    lo_server_add_method(net->servers[SERVER_UDP], sig->path, NULL, mpr_dev_handler, (void*)sig);
    lo_server_add_method(net->servers[SERVER_TCP], sig->path, NULL, mpr_dev_handler, (void*)sig);
    ++dev->n_output_callbacks;
}

void mpr_dev_remove_sig_methods(mpr_local_dev dev, mpr_local_sig sig)
{
    mpr_net net;
    char *path = 0;
    int len;
    RETURN_UNLESS(sig && sig->is_local);
    net = &dev->obj.graph->net;
    len = (int)strlen(sig->path) + 5;
    path = (char*)realloc(path, len);
    snprintf(path, len, "%s%s", sig->path, "/get");
    lo_server_del_method(net->servers[SERVER_UDP], path, NULL);
    lo_server_del_method(net->servers[SERVER_TCP], path, NULL);
    free(path);
    lo_server_del_method(net->servers[SERVER_UDP], sig->path, NULL);
    lo_server_del_method(net->servers[SERVER_TCP], sig->path, NULL);
    --dev->n_output_callbacks;
}

mpr_list mpr_dev_get_sigs(mpr_dev dev, mpr_dir dir)
{
    mpr_list qry;
    RETURN_ARG_UNLESS(dev && dev->obj.graph->sigs, 0);
    qry = mpr_list_new_query((const void**)&dev->obj.graph->sigs,
                             (void*)cmp_qry_dev_sigs, "hi", dev->obj.id, dir);
    return mpr_list_start(qry);
}

mpr_sig mpr_dev_get_sig_by_name(mpr_dev dev, const char *sig_name)
{
    mpr_list sigs;
    RETURN_ARG_UNLESS(dev && sig_name, 0);
    sigs = mpr_list_from_data(dev->obj.graph->sigs);
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
    mpr_dir dir = *(int*)((char*)context_data + sizeof(mpr_id));
    int i;
    if (dir == MPR_DIR_BOTH) {
        RETURN_ARG_UNLESS(map->dst->sig->dev->obj.id == dev_id, 0);
        for (i = 0; i < map->num_src; i++)
            RETURN_ARG_UNLESS(map->src[i]->sig->dev->obj.id == dev_id, 0);
        return 1;
    }
    if (dir & MPR_DIR_OUT) {
        for (i = 0; i < map->num_src; i++)
            RETURN_ARG_UNLESS(map->src[i]->sig->dev->obj.id != dev_id, 1);
    }
    if (dir & MPR_DIR_IN)
        RETURN_ARG_UNLESS(map->dst->sig->dev->obj.id != dev_id, 1);
    return 0;
}

mpr_list mpr_dev_get_maps(mpr_dev dev, mpr_dir dir)
{
    mpr_list qry;
    RETURN_ARG_UNLESS(dev && dev->obj.graph->maps, 0);
    qry = mpr_list_new_query((const void**)&dev->obj.graph->maps,
                             (void*)cmp_qry_dev_maps, "hi", dev->obj.id, dir);
    return mpr_list_start(qry);
}

static int cmp_qry_dev_links(const void *context_data, mpr_link link)
{
    mpr_id dev_id = *(mpr_id*)context_data;
    mpr_dir dir = *(int*)((char*)context_data + sizeof(mpr_id));
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
    mpr_list qry;
    RETURN_ARG_UNLESS(dev && dev->obj.graph->links, 0);
    qry = mpr_list_new_query((const void**)&dev->obj.graph->links,
                             (void*)cmp_qry_dev_links, "hi", dev->obj.id, dir);
    return mpr_list_start(qry);
}

mpr_link mpr_dev_get_link_by_remote(mpr_local_dev dev, mpr_dev remote)
{
    mpr_list links;
    RETURN_ARG_UNLESS(dev, 0);
    links = mpr_list_from_data(dev->obj.graph->links);
    while (links) {
        mpr_link link = (mpr_link)*links;
        if (link->devs[0] == (mpr_dev)dev && link->devs[1] == remote)
            return link;
        if (link->devs[1] == (mpr_dev)dev && link->devs[0] == remote)
            return link;
        links = mpr_list_get_next(links);
    }
    return 0;
}

/* TODO: handle interrupt-driven updates that omit call to this function */
MPR_INLINE static void _process_incoming_maps(mpr_local_dev dev)
{
    mpr_graph graph;
    mpr_list maps;
    RETURN_UNLESS(dev->receiving);
    graph = dev->obj.graph;
    /* process and send updated maps */
    /* TODO: speed this up! */
    dev->receiving = 0;
    maps = mpr_list_from_data(graph->maps);
    while (maps) {
        mpr_local_map map = *(mpr_local_map*)maps;
        maps = mpr_list_get_next(maps);
        if (map->is_local && map->updated && map->expr && !map->muted)
            mpr_map_receive(map, dev->time);
    }
}

/* TODO: handle interrupt-driven updates that omit call to this function */
MPR_INLINE static int _process_outgoing_maps(mpr_local_dev dev)
{
    int msgs = 0;
    mpr_list list;
    mpr_graph graph;
    RETURN_ARG_UNLESS(dev->sending, 0);

    graph = dev->obj.graph;
    /* process and send updated maps */
    /* TODO: speed this up! */
    list = mpr_list_from_data(graph->maps);
    while (list) {
        mpr_local_map map = *(mpr_local_map*)list;
        list = mpr_list_get_next(list);
        if (map->is_local && map->updated && map->expr && !map->muted)
            mpr_map_send(map, dev->time);
    }
    dev->sending = 0;
    list = mpr_list_from_data(graph->links);
    while (list) {
        msgs += mpr_link_process_bundles((mpr_link)*list, dev->time, 0);
        list = mpr_list_get_next(list);
    }
    return msgs ? 1 : 0;
}

void mpr_dev_update_maps(mpr_dev dev) {
    RETURN_UNLESS(dev && dev->is_local);
    ((mpr_local_dev)dev)->time_is_stale = 1;
    if (!((mpr_local_dev)dev)->polling)
        _process_outgoing_maps((mpr_local_dev)dev);
}

int mpr_dev_poll(mpr_dev dev, int block_ms)
{
    int admin_count = 0, device_count = 0, status[4];
    mpr_net net;
    RETURN_ARG_UNLESS(dev && dev->is_local, 0);
    net = &dev->obj.graph->net;
    mpr_net_poll(net);

    if (!((mpr_local_dev)dev)->registered) {
        if (lo_servers_recv_noblock(&net->servers[SERVER_ADMIN], status, 2, block_ms)) {
            admin_count = (status[0] > 0) + (status[1] > 0);
            net->msgs_recvd |= admin_count;
        }
        ((mpr_local_dev)dev)->bundle_idx = 1;
        return admin_count;
    }

    ((mpr_local_dev)dev)->polling = 1;
    ((mpr_local_dev)dev)->time_is_stale = 1;
    mpr_dev_get_time(dev);
    _process_outgoing_maps((mpr_local_dev)dev);
    ((mpr_local_dev)dev)->polling = 0;

    if (!block_ms) {
        if (lo_servers_recv_noblock(net->servers, status, 4, 0)) {
            admin_count = (status[0] > 0) + (status[1] > 0);
            device_count = (status[2] > 0) + (status[3] > 0);
            net->msgs_recvd |= admin_count;
        }
    }
    else {
        double then = mpr_get_current_time();
        int left_ms = block_ms, elapsed, checked_admin = 0;
        while (left_ms > 0) {
            /* set timeout to a maximum of 100ms */
            if (left_ms > 100)
                left_ms = 100;
            ((mpr_local_dev)dev)->polling = 1;
            if (lo_servers_recv_noblock(net->servers, status, 4, left_ms)) {
                admin_count += (status[0] > 0) + (status[1] > 0);
                device_count += (status[2] > 0) + (status[3] > 0);
            }
            /* check if any signal update bundles need to be sent */
            _process_incoming_maps((mpr_local_dev)dev);
            _process_outgoing_maps((mpr_local_dev)dev);
            ((mpr_local_dev)dev)->polling = 0;

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
    while (device_count < (dev->num_inputs + ((mpr_local_dev)dev)->n_output_callbacks)*1
           && (lo_servers_recv_noblock(&net->servers[SERVER_DEVICE], &status[2], 2, 0)))
        device_count += (status[2] > 0) + (status[3] > 0);

    /* process incoming maps */
    ((mpr_local_dev)dev)->polling = 1;
    _process_incoming_maps((mpr_local_dev)dev);
    ((mpr_local_dev)dev)->polling = 0;

    if (dev->obj.props.synced->dirty && mpr_dev_get_is_ready(dev)
        && ((mpr_local_dev)dev)->subscribers) {
        /* inform device subscribers of changed properties */
        mpr_net_use_subscribers(net, (mpr_local_dev)dev, MPR_DEV);
        mpr_dev_send_state(dev, MSG_DEV);
    }

    net->msgs_recvd |= admin_count;
    return admin_count + device_count;
}

mpr_time mpr_dev_get_time(mpr_dev dev)
{
    RETURN_ARG_UNLESS(dev && dev->is_local, MPR_NOW);
    if (((mpr_local_dev)dev)->time_is_stale)
        mpr_dev_set_time(dev, MPR_NOW);
    return ((mpr_local_dev)dev)->time;
}

void mpr_dev_set_time(mpr_dev dev, mpr_time time)
{
    RETURN_UNLESS(dev && dev->is_local
                  && memcmp(&time, &((mpr_local_dev)dev)->time, sizeof(mpr_time)));
    mpr_time_set(&((mpr_local_dev)dev)->time, time);
    ((mpr_local_dev)dev)->time_is_stale = 0;
    if (!((mpr_local_dev)dev)->polling)
        _process_outgoing_maps((mpr_local_dev)dev);
}

void mpr_dev_reserve_idmap(mpr_local_dev dev)
{
    mpr_id_map map;
    map = (mpr_id_map)calloc(1, sizeof(mpr_id_map_t));
    map->next = dev->idmaps.reserve;
    dev->idmaps.reserve = map;
}

mpr_id_map mpr_dev_add_idmap(mpr_local_dev dev, int group, mpr_id LID, mpr_id GID)
{
    mpr_id_map map;
    if (!dev->idmaps.reserve)
        mpr_dev_reserve_idmap(dev);
    map = dev->idmaps.reserve;
    map->LID = LID;
    map->GID = GID ? GID : mpr_dev_generate_unique_id((mpr_dev)dev);
    map->LID_refcount = 1;
    map->GID_refcount = 0;
    dev->idmaps.reserve = map->next;
    map->next = dev->idmaps.active[group];
    dev->idmaps.active[group] = map;
    return map;
}

static void mpr_dev_remove_idmap(mpr_local_dev dev, int group, mpr_id_map rem)
{
    mpr_id_map *map = &dev->idmaps.active[group];
    while (*map) {
        if ((*map) == rem) {
            *map = (*map)->next;
            rem->next = dev->idmaps.reserve;
            dev->idmaps.reserve = rem;
            break;
        }
        map = &(*map)->next;
    }
}

int mpr_dev_LID_decref(mpr_local_dev dev, int group, mpr_id_map map)
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

int mpr_dev_GID_decref(mpr_local_dev dev, int group, mpr_id_map map)
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

mpr_id_map mpr_dev_get_idmap_by_LID(mpr_local_dev dev, int group, mpr_id LID)
{
    mpr_id_map map = dev->idmaps.active[group];
    while (map) {
        if (map->LID == LID)
            return map;
        map = map->next;
    }
    return 0;
}

mpr_id_map mpr_dev_get_idmap_by_GID(mpr_local_dev dev, int group, mpr_id GID)
{
    mpr_id_map map = dev->idmaps.active[group];
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

void mpr_dev_start_servers(mpr_local_dev dev)
{
    int portnum;
    char port[16], *pport = 0, *url, *host;
    mpr_net net = &dev->obj.graph->net;
    mpr_list sigs;
    RETURN_UNLESS(!net->servers[SERVER_UDP] && !net->servers[SERVER_TCP]);
    while (!(net->servers[SERVER_UDP] = lo_server_new(pport, handler_error)))
        pport = 0;
    snprintf(port, 16, "%d", lo_server_get_port(net->servers[SERVER_UDP]));
    pport = port;
    while (!(net->servers[SERVER_TCP] = lo_server_new_with_proto(pport, LO_TCP, handler_error)))
        pport = 0;

    /* Disable liblo message queueing */
    lo_server_enable_queue(net->servers[SERVER_UDP], 0, 1);
    lo_server_enable_queue(net->servers[SERVER_TCP], 0, 1);

    /* Add bundle handlers */
    lo_server_add_bundle_handlers(net->servers[SERVER_UDP], mpr_dev_bundle_start, NULL, (void*)dev);
    lo_server_add_bundle_handlers(net->servers[SERVER_TCP], mpr_dev_bundle_start, NULL, (void*)dev);

    portnum = lo_server_get_port(net->servers[SERVER_UDP]);
    mpr_tbl_set(dev->obj.props.synced, PROP(PORT), NULL, 1, MPR_INT32, &portnum, NON_MODIFIABLE);

    trace_dev(dev, "bound to UDP port %i\n", portnum);
    trace_dev(dev, "bound to TCP port %i\n", lo_server_get_port(net->servers[SERVER_TCP]));

    url = lo_server_get_url(net->servers[SERVER_UDP]);
    host = lo_url_get_hostname(url);
    mpr_tbl_set(dev->obj.props.synced, PROP(HOST), NULL, 1, MPR_STR, host, NON_MODIFIABLE);
    free(host);
    free(url);

    /* add signal methods */
    sigs = mpr_dev_get_sigs((mpr_dev)dev, MPR_DIR_ANY);
    while (sigs) {
        mpr_local_sig sig = (mpr_local_sig)*sigs;
        sigs = mpr_list_get_next(sigs);
        if (sig->handler) {
            lo_server_add_method(net->servers[SERVER_UDP], sig->path, NULL, mpr_dev_handler, (void*)sig);
            lo_server_add_method(net->servers[SERVER_TCP], sig->path, NULL, mpr_dev_handler, (void*)sig);
        }
    }
}

const char *mpr_dev_get_name(mpr_dev dev)
{
    unsigned int len;
    RETURN_ARG_UNLESS(!dev->is_local || (   ((mpr_local_dev)dev)->registered
                                         && ((mpr_local_dev)dev)->ordinal_allocator.locked), 0);
    if (dev->name)
        return dev->name;
    len = strlen(dev->prefix) + 6;
    dev->name = (char*)malloc(len);
    dev->name[0] = 0;
    snprintf(dev->name, len, "%s.%d", dev->prefix, ((mpr_local_dev)dev)->ordinal_allocator.val);
    return dev->name;
}

int mpr_dev_get_is_ready(mpr_dev dev)
{
    return dev ? dev->status >= MPR_STATUS_READY : MPR_STATUS_UNDEFINED;
}

mpr_id mpr_dev_generate_unique_id(mpr_dev dev)
{
    mpr_id id;
    RETURN_ARG_UNLESS(dev, 0);
    id = ++dev->obj.graph->resource_counter;
    if (dev->is_local && ((mpr_local_dev)dev)->registered)
        id |= dev->obj.id;
    return id;
}

void mpr_dev_send_state(mpr_dev dev, net_msg_t cmd)
{
    mpr_net net = &dev->obj.graph->net;
    NEW_LO_MSG(msg, return);

    /* device name */
    lo_message_add_string(msg, mpr_dev_get_name((mpr_dev)dev));

    /* properties */
    mpr_tbl_add_to_msg(dev->is_local ? dev->obj.props.synced : 0, dev->obj.props.staged, msg);

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

    /* not found - add a new linked device */
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
        const char *name;
        if (num == 1 && strcmp(&link_list[0]->s, "none")==0)
            num = 0;

        /* Remove any old links that are missing */
        for (i = 0; ; i++) {
            int found = 0;
            if (i >= dev->num_linked)
                break;
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
        /* Add any new links */
        for (i = 0; i < num; i++) {
            mpr_dev rem;
            if ((rem = mpr_graph_add_dev(dev->obj.graph, &link_list[i]->s, 0)))
                updated += mpr_dev_add_link(dev, rem);
        }
    }
    return updated;
}

/*! Update information about a device record based on message properties. */
int mpr_dev_set_from_msg(mpr_dev dev, mpr_msg m)
{
    int i, updated = 0;
    RETURN_ARG_UNLESS(m, 0);
    for (i = 0; i < m->num_atoms; i++) {
        mpr_msg_atom a = &m->atoms[i];
        switch (MASK_PROP_BITFLAGS(a->prop)) {
            case PROP(LINKED):
                if (!dev->is_local && mpr_type_get_is_str(a->types[0]))
                    updated += mpr_dev_update_linked(dev, a);
                break;
            default:
                updated += mpr_tbl_set_from_atom(dev->obj.props.synced, a, REMOTE_MODIFY);
                break;
        }
    }
    return updated;
}

static int mpr_dev_send_sigs(mpr_local_dev dev, mpr_dir dir)
{
    mpr_list l = mpr_dev_get_sigs((mpr_dev)dev, dir);
    while (l) {
        mpr_sig_send_state((mpr_sig)*l, MSG_SIG);
        l = mpr_list_get_next(l);
    }
    return 0;
}

static int mpr_dev_send_maps(mpr_local_dev dev, mpr_dir dir)
{
    mpr_list l = mpr_dev_get_maps((mpr_dev)dev, dir);
    while (l) {
        mpr_map_send_state((mpr_map)*l, -1, MSG_MAPPED);
        l = mpr_list_get_next(l);
    }
    return 0;
}

/* Add/renew/remove a subscription. */
void mpr_dev_manage_subscriber(mpr_local_dev dev, lo_address addr, int flags,
                               int timeout_sec, int revision)
{
    mpr_time t;
    mpr_net net;
    mpr_subscriber *s = &dev->subscribers;
    const char *ip = lo_address_get_hostname(addr);
    const char *port = lo_address_get_port(addr);
    RETURN_UNLESS(ip && port);
    mpr_time_set(&t, MPR_NOW);

    while (*s) {
        const char *s_ip = lo_address_get_hostname((*s)->addr);
        const char *s_port = lo_address_get_port((*s)->addr);
        if (strcmp(ip, s_ip)==0 && strcmp(port, s_port)==0) {
            /* subscriber already exists */
            if (!flags || !timeout_sec) {
                /* remove subscription */
                mpr_subscriber temp = *s;
                int prev_flags = temp->flags;
                trace_dev(dev, "removing subscription from %s:%s\n", s_ip, s_port);
                *s = temp->next;
                FUNC_IF(lo_address_free, temp->addr);
                free(temp);
                RETURN_UNLESS(flags && (flags &= ~prev_flags));
            }
            else {
                /* reset timeout */
                int temp = flags;
#ifdef DEBUG
                trace_dev(dev, "renewing subscription from %s:%s for %d seconds"
                          "with flags ", s_ip, s_port, timeout_sec);
                print_subscription_flags(flags);
#endif
                (*s)->lease_exp = t.sec + timeout_sec;
                flags &= ~(*s)->flags;
                (*s)->flags = temp;
            }
            break;
        }
        s = &(*s)->next;
    }

    RETURN_UNLESS(flags);

    if (!(*s) && timeout_sec) {
        /* add new subscriber */
#ifdef DEBUG
        trace_dev(dev, "adding new subscription from %s:%s with flags ", ip, port);
        print_subscription_flags(flags);
#endif
        mpr_subscriber sub = malloc(sizeof(struct _mpr_subscriber));
        sub->addr = lo_address_new(ip, port);
        sub->lease_exp = t.sec + timeout_sec;
        sub->flags = flags;
        sub->next = dev->subscribers;
        dev->subscribers = sub;
    }

    /* bring new subscriber up to date */
    net = &dev->obj.graph->net;
    mpr_net_use_mesh(net, addr);
    mpr_dev_send_state((mpr_dev)dev, MSG_DEV);
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
