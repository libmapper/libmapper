#include <string.h>
#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#include <zlib.h>
#include <sys/time.h>

#include "mapper_internal.h"

#define AUTOSUB_INTERVAL 60
extern const char* net_msg_strings[NUM_MSG_STRINGS];

#ifdef DEBUG
void print_subscription_flags(int flags)
{
    printf("[");
    if (!flags) {
        printf("none]\n");
        return;
    }
    if (flags & MPR_DEV)
        printf("devices, ");
    if (flags & MPR_SIG_IN) {
        if (flags & MPR_SIG_OUT)
            printf("signals, ");
        else
            printf("input signals, ");
    }
    else if (flags & MPR_SIG_OUT)
        printf("output signals, ");
    if (flags & MPR_MAP_IN) {
        if (flags & MPR_MAP_OUT)
            printf("maps, ");
        else
            printf("incoming maps, ");
    }
    else if (flags & MPR_MAP_OUT)
        printf("outgoing maps, ");
    printf("\b\b]\n");
}
#endif

static void _on_dev_autosub(mpr_graph g, mpr_obj o, mpr_graph_evt e, const void *v)
{
    /* New subscriptions are handled in network.c as response to "sync" msg */
    if (MPR_OBJ_REM == e)
        mpr_graph_subscribe(g, (mpr_dev)o, 0, 0);
}

static void set_net_dst(mpr_graph g, mpr_dev d)
{
    /* TODO: look up device info, maybe send directly */
    mpr_net_use_bus(&g->net);
}

static void send_subscribe_msg(mpr_graph g, mpr_dev d, int flags, int timeout)
{
    char cmd[1024];
    NEW_LO_MSG(msg, return);
    snprintf(cmd, 1024, "/%s/subscribe", d->name);

    set_net_dst(g, d);
    if (MPR_OBJ == flags)
        lo_message_add_string(msg, "all");
    else {
        if (flags & MPR_DEV)
            lo_message_add_string(msg, "device");
        if (MPR_SIG == (flags & MPR_SIG))
            lo_message_add_string(msg, "signals");
        else {
            if (flags & MPR_SIG_IN)
                lo_message_add_string(msg, "inputs");
            else if (flags & MPR_SIG_OUT)
                lo_message_add_string(msg, "outputs");
        }
        if (MPR_MAP == (flags & MPR_MAP))
            lo_message_add_string(msg, "maps");
        else {
            if (flags & MPR_MAP_IN)
                lo_message_add_string(msg, "incoming_maps");
            else if (flags & MPR_MAP_OUT)
                lo_message_add_string(msg, "outgoing_maps");
        }
    }
    lo_message_add_string(msg, "@lease");
    lo_message_add_int32(msg, timeout);

    lo_message_add_string(msg, "@version");
    lo_message_add_int32(msg, d->obj.version);

    mpr_net_add_msg(&g->net, cmd, 0, msg);
    mpr_net_send(&g->net);
}

static void _autosubscribe(mpr_graph g, int flags)
{
    if (!g->autosub && flags) {
        /* update flags for existing subscriptions */
        mpr_subscription s = g->subscriptions;
        mpr_time t;
        NEW_LO_MSG(msg, ;);
        mpr_time_set(&t, MPR_NOW);
        while (s) {
            trace_graph("adjusting flags for existing autorenewing subscription to %s.\n",
                        mpr_dev_get_name(s->dev));
            if (flags & ~s->flags) {
                send_subscribe_msg(g, s->dev, flags, AUTOSUB_INTERVAL);
                /* leave 10-second buffer for subscription renewal */
                s->lease_expiration_sec = (t.sec + AUTOSUB_INTERVAL - 10);
            }
            s->flags = flags;
            s = s->next;
        }
        if (msg) {
            trace_graph("pinging all devices.\n");
            mpr_net_use_bus(&g->net);
            mpr_net_add_msg(&g->net, 0, MSG_WHO, msg);
        }
        mpr_graph_add_cb(g, _on_dev_autosub, MPR_DEV, g);
    }
    else if (g->autosub && !flags) {
        mpr_graph_remove_cb(g, _on_dev_autosub, g);
        while (g->subscriptions)
            mpr_graph_subscribe(g, g->subscriptions->dev, 0, 0);
    }
#ifdef DEBUG
    trace_graph("setting autosubscribe flags to ");
    print_subscription_flags(flags);
#endif
    g->autosub = flags;
}

void mpr_graph_cleanup(mpr_graph g)
{
    int staged = 0;
    mpr_list maps;
    if (!g->staged_maps)
        return;
    /* check for maps that were staged but never completed */
    maps = mpr_list_from_data(g->maps);
    while (maps) {
        mpr_map map = (mpr_map)*maps;
        maps = mpr_list_get_next(maps);
        if (map->status <= MPR_STATUS_STAGED) {
            if (map->status <= MPR_STATUS_EXPIRED) {
                if (map->is_local)
                    mpr_rtr_remove_map(g->net.rtr, (mpr_local_map)map);
                mpr_graph_remove_map(g, map, MPR_OBJ_REM);
            }
            else {
                --map->status;
                ++staged;
            }
        }
    }
    g->staged_maps = staged;
}

mpr_graph mpr_graph_new(int subscribe_flags)
{
    mpr_tbl tbl;
    mpr_graph g = (mpr_graph) calloc(1, sizeof(mpr_graph_t));
    RETURN_ARG_UNLESS(g, NULL);

    g->obj.type = MPR_GRAPH;
    g->net.graph = g->obj.graph = g;
    g->obj.id = 0;
    g->own = 1;
    mpr_net_init(&g->net, 0, 0, 0);
    if (subscribe_flags)
        _autosubscribe(g, subscribe_flags);

    /* TODO: consider whether graph objects should sync properties over the network. */
    tbl = g->obj.props.synced = mpr_tbl_new();
    mpr_tbl_link(tbl, PROP(DATA), 1, MPR_PTR, &g->obj.data,
                 LOCAL_MODIFY | INDIRECT | LOCAL_ACCESS_ONLY);
    mpr_tbl_set(tbl, PROP(LIBVER), NULL, 1, MPR_STR, PACKAGE_VERSION, NON_MODIFIABLE);
    /* TODO: add object queries as properties. */

    return g;
}

void mpr_graph_free(mpr_graph g)
{
    mpr_list list;
    RETURN_UNLESS(g);

    /* remove callbacks now so they won't be called when removing devices */
    while (g->callbacks) {
        fptr_list cb = g->callbacks;
        g->callbacks = g->callbacks->next;
        free(cb);
    }

    /* unsubscribe from and remove any autorenewing subscriptions */
    while (g->subscriptions)
        mpr_graph_subscribe(g, g->subscriptions->dev, 0, 0);

    /* Remove all non-local maps */
    list = mpr_list_from_data(g->maps);
    while (list) {
        mpr_map map = (mpr_map)*list;
        list = mpr_list_get_next(list);
        if (!map->is_local)
            mpr_graph_remove_map(g, map, MPR_OBJ_REM);
    }

    /* Remove all non-local devices and signals from the graph except for
     * those referenced by local maps. */
    list = mpr_list_from_data(g->devs);
    while (list) {
        mpr_dev dev = (mpr_dev)*list;
        int no_local_dev_maps = 1;
        mpr_list sigs;
        list = mpr_list_get_next(list);
        if (dev->is_local)
            continue;

        sigs = mpr_dev_get_sigs(dev, MPR_DIR_ANY);
        while (sigs) {
            int no_local_sig_maps = 1;
            mpr_sig sig = (mpr_sig)*sigs;
            mpr_list maps = mpr_sig_get_maps(sig, MPR_DIR_ANY);
            while (maps) {
                if (((mpr_map)*maps)->is_local) {
                    no_local_dev_maps = no_local_sig_maps = 0;
                    mpr_list_free(maps);
                    break;
                }
                maps = mpr_list_get_next(maps);
            }
            sigs = mpr_list_get_next(sigs);
            if (no_local_sig_maps)
                mpr_graph_remove_sig(g, sig, MPR_OBJ_REM);
        }
        if (no_local_dev_maps)
            mpr_graph_remove_dev(g, dev, MPR_OBJ_REM, 1);
    }

    mpr_net_free(&g->net);
    FUNC_IF(mpr_tbl_free, g->obj.props.synced);
    free(g);
}

/**** Generic records ****/

static mpr_obj _obj_by_id(mpr_graph g, void *obj_list, mpr_id id)
{
    mpr_list objs = mpr_list_from_data(obj_list);
    while (objs) {
        if (id == (*objs)->id)
            return *objs;
        objs = mpr_list_get_next(objs);
    }
    return NULL;
}

mpr_obj mpr_graph_get_obj(mpr_graph g, mpr_type type, mpr_id id)
{
    if (type & MPR_DEV)
        return _obj_by_id(g, g->devs, id);
    if (type & MPR_SIG)
        return _obj_by_id(g, g->sigs, id);
    if (type & MPR_MAP)
        return _obj_by_id(g, g->maps, id);
    return 0;
}

/* TODO: support queries over multiple object types. */
mpr_list mpr_graph_get_objs(mpr_graph g, int types)
{
    if (types & MPR_DEV)
        return mpr_list_from_data(g->devs);
    if (types & MPR_SIG)
        return mpr_list_from_data(g->sigs);
    if (types & MPR_MAP)
        return mpr_list_from_data(g->maps);
    return 0;
}

int mpr_graph_add_cb(mpr_graph g, mpr_graph_handler *h, int types, const void *user)
{
    fptr_list cb = g->callbacks;
    while (cb) {
        if (cb->f == (void*)h && cb->ctx == user) {
            cb->types |= types;
            return 0;
        }
        cb = cb->next;
    }

    cb = (fptr_list)malloc(sizeof(struct _fptr_list));
    cb->f = (void*)h;
    cb->types = types;
    cb->ctx = (void*)user;
    cb->next = g->callbacks;
    g->callbacks = cb;
    return 1;
}

void mpr_graph_call_cbs(mpr_graph g, mpr_obj o, mpr_type t, mpr_graph_evt e)
{
    fptr_list cb = g->callbacks, temp;
    while (cb) {
        temp = cb->next;
        if (cb->types & t)
            ((mpr_graph_handler*)cb->f)(g, o, e, cb->ctx);
        cb = temp;
    }
}

void *mpr_graph_remove_cb(mpr_graph g, mpr_graph_handler *h, const void *user)
{
    fptr_list cb = g->callbacks;
    fptr_list prevcb = 0;
    void *ctx;
    while (cb) {
        if (cb->f == (void*)h && cb->ctx == user)
            break;
        prevcb = cb;
        cb = cb->next;
    }
    RETURN_ARG_UNLESS(cb, 0);
    if (prevcb)
        prevcb->next = cb->next;
    else
        g->callbacks = cb->next;
    ctx = cb->ctx;
    free(cb);
    return ctx;
}

static void _remove_by_qry(mpr_graph g, mpr_list l, mpr_graph_evt e)
{
    mpr_obj o;
    while (l) {
        o = *l;
        l = mpr_list_get_next(l);
        switch ((int)o->type) {
            case MPR_LINK:  mpr_graph_remove_link(g, (mpr_link)o, e);   break;
            case MPR_SIG:   mpr_graph_remove_sig(g, (mpr_sig)o, e);     break;
            case MPR_MAP:   mpr_graph_remove_map(g, (mpr_map)o, e);     break;
            default:                                                    break;
        }
    }
}

/**** Device records ****/

mpr_dev mpr_graph_add_dev(mpr_graph g, const char *name, mpr_msg msg)
{
    const char *no_slash = skip_slash(name);
    mpr_dev dev = mpr_graph_get_dev_by_name(g, no_slash);
    int rc = 0, updated = 0;

    if (!dev) {
        trace_graph("adding device '%s'.\n", name);
        dev = (mpr_dev)mpr_list_add_item((void**)&g->devs, sizeof(*dev));
        dev->name = strdup(no_slash);
        dev->obj.id = crc32(0L, (const Bytef *)no_slash, strlen(no_slash));
        dev->obj.id <<= 32;
        dev->obj.type = MPR_DEV;
        dev->obj.graph = g;
        dev->is_local = 0;
        init_dev_prop_tbl(dev);
        rc = 1;
    }

    if (dev) {
        updated = mpr_dev_set_from_msg(dev, msg);
        if (!rc)
            trace_graph("updated %d props for device '%s'.\n", updated, name);
        mpr_time_set(&dev->synced, MPR_NOW);

        if (rc || updated)
            mpr_graph_call_cbs(g, (mpr_obj)dev, MPR_DEV, rc ? MPR_OBJ_NEW : MPR_OBJ_MOD);
    }
    return dev;
}

/* Internal function called by /logout protocol handler */
void mpr_graph_remove_dev(mpr_graph g, mpr_dev d, mpr_graph_evt e, int quiet)
{
    mpr_list maps;
    RETURN_UNLESS(d);
    _remove_by_qry(g, mpr_dev_get_maps(d, MPR_DIR_ANY), e);

    /* remove matching maps scopes */
    maps = mpr_graph_get_objs(g, MPR_MAP);
    while (maps) {
        mpr_map_remove_scope((mpr_map)*maps, d);
        maps = mpr_list_get_next(maps);
    }

    _remove_by_qry(g, mpr_dev_get_links(d, MPR_DIR_ANY), e);
    _remove_by_qry(g, mpr_dev_get_sigs(d, MPR_DIR_ANY), e);

    mpr_list_remove_item((void**)&g->devs, d);

    if (!quiet)
        mpr_graph_call_cbs(g, (mpr_obj)d, MPR_DEV, e);

    FUNC_IF(mpr_tbl_free, d->obj.props.synced);
    FUNC_IF(mpr_tbl_free, d->obj.props.staged);
    FUNC_IF(free, d->name);
    mpr_list_free_item(d);
}

mpr_dev mpr_graph_get_dev_by_name(mpr_graph g, const char *name)
{
    const char *no_slash = skip_slash(name);
    mpr_list devs = mpr_list_from_data(g->devs);
    while (devs) {
        mpr_dev dev = (mpr_dev)*devs;
        devs = mpr_list_get_next(devs);
        if (dev->name && (0 == strcmp(dev->name, no_slash)))
            return dev;
    }
    return 0;
}

static void _check_dev_status(mpr_graph g, uint32_t time_sec)
{
    mpr_list devs = mpr_list_from_data(g->devs);
    time_sec -= TIMEOUT_SEC;
    while (devs) {
        mpr_dev dev = (mpr_dev)*devs;
        devs = mpr_list_get_next(devs);
        /* check if device has "checked in" recently – could be /sync ping or any sent metadata */
        if (dev->synced.sec && (dev->synced.sec < time_sec)) {
            /* remove subscription */
            mpr_graph_subscribe(g, dev, 0, 0);
            mpr_graph_remove_dev(g, dev, MPR_OBJ_EXP, 0);
        }
    }
}

/**** Signals ****/

mpr_sig mpr_graph_add_sig(mpr_graph g, const char *name, const char *dev_name, mpr_msg msg)
{
    mpr_sig sig = 0;
    int rc = 0, updated = 0;

    mpr_dev dev = mpr_graph_get_dev_by_name(g, dev_name);
    if (dev) {
        sig = mpr_dev_get_sig_by_name(dev, name);
        if (sig && sig->is_local)
            return sig;
    }
    else
        dev = mpr_graph_add_dev(g, dev_name, 0);

    if (!sig) {
        trace_graph("adding signal '%s:%s'.\n", dev_name, name);
        sig = (mpr_sig)mpr_list_add_item((void**)&g->sigs, sizeof(mpr_sig_t));

        /* also add device record if necessary */
        sig->dev = dev;
        sig->obj.graph = g;
        sig->is_local = 0;

        mpr_sig_init(sig, MPR_DIR_UNDEFINED, name, 0, 0, 0, 0, 0, 0);
        rc = 1;
    }

    if (sig) {
        updated = mpr_sig_set_from_msg(sig, msg);
        if (!rc)
            trace_graph("updated %d props for signal '%s:%s'.\n", updated, dev_name, name);

        if (rc || updated)
            mpr_graph_call_cbs(g, (mpr_obj)sig, MPR_SIG, rc ? MPR_OBJ_NEW : MPR_OBJ_MOD);
    }
    return sig;
}

void mpr_graph_remove_sig(mpr_graph g, mpr_sig s, mpr_graph_evt e)
{
    RETURN_UNLESS(s);

    /* remove any stored maps using this signal */
    _remove_by_qry(g, mpr_sig_get_maps(s, MPR_DIR_ANY), e);

    mpr_list_remove_item((void**)&g->sigs, s);
    mpr_graph_call_cbs(g, (mpr_obj)s, MPR_SIG, e);

    if (s->dir & MPR_DIR_IN)
        --s->dev->num_inputs;
    if (s->dir & MPR_DIR_OUT)
        --s->dev->num_outputs;

    mpr_sig_free_internal(s);
    mpr_list_free_item(s);
}

/**** Link records ****/

mpr_link mpr_graph_add_link(mpr_graph g, mpr_dev dev1, mpr_dev dev2)
{
    mpr_link link;
    RETURN_ARG_UNLESS(dev1 && dev2, 0);
    link = mpr_dev_get_link_by_remote((mpr_local_dev)dev1, dev2);
    if (link)
        return link;

    link = (mpr_link)mpr_list_add_item((void**)&g->links, sizeof(mpr_link_t));
    if (dev2->is_local) {
        link->devs[LOCAL_DEV] = dev2;
        link->devs[REMOTE_DEV] = dev1;
    }
    else {
        link->devs[LOCAL_DEV] = dev1;
        link->devs[REMOTE_DEV] = dev2;
    }
    link->obj.type = MPR_LINK;
    link->obj.graph = g;
    mpr_link_init(link);
    return link;
}

void mpr_graph_remove_link(mpr_graph g, mpr_link l, mpr_graph_evt e)
{
    RETURN_UNLESS(l);
    _remove_by_qry(g, mpr_link_get_maps(l), e);
    mpr_list_remove_item((void**)&g->links, l);
    mpr_link_free(l);
    mpr_list_free_item(l);
}

/**** Map records ****/

static int _compare_slot_names(const void *l, const void *r)
{
    int result = strcmp((*(mpr_slot*)l)->sig->dev->name, (*(mpr_slot*)r)->sig->dev->name);
    if (0 == result)
        return strcmp((*(mpr_slot*)l)->sig->name, (*(mpr_slot*)r)->sig->name);
    return result;
}

static mpr_sig add_sig_from_whole_name(mpr_graph g, const char* name)
{
    char *devnamep, *signame, devname[256];
    int devnamelen = mpr_parse_names(name, &devnamep, &signame);
    if (!devnamelen || devnamelen >= 256) {
        trace_graph("error extracting device name\n");
        return 0;
    }
    strncpy(devname, devnamep, devnamelen);
    devname[devnamelen] = 0;
    return mpr_graph_add_sig(g, signame, devname, 0);
}

mpr_map mpr_graph_get_map_by_names(mpr_graph g, int num_src, const char **srcs, const char *dst)
{
    mpr_map map = 0;
    mpr_list maps = mpr_list_from_data(g->maps);
    while (maps) {
        map = (mpr_map)*maps;
        if (map->num_src != num_src)
            map = 0;
        else if (mpr_slot_match_full_name(map->dst, dst))
            map = 0;
        else {
            int i;
            for (i = 0; i < num_src; i++) {
                if (mpr_slot_match_full_name(map->src[i], srcs[i])) {
                    map = 0;
                    break;
                }
            }
        }
        if (map)
            break;
        maps = mpr_list_get_next(maps);
    }
    return map;
}

mpr_map mpr_graph_add_map(mpr_graph g, mpr_id id, int num_src, const char **src_names,
                          const char *dst_name)
{
    mpr_map map = 0;
    unsigned char rc = 0, updated = 0, i, j, is_local = 0;
    if (num_src > MAX_NUM_MAP_SRC) {
        trace_graph("error: maximum mapping sources exceeded.\n");
        return 0;
    }

    /* We could be part of larger "convergent" mapping, so we will retrieve
     * record by mapping id instead of names. */
    if (id) {
        map = (mpr_map)_obj_by_id(g, (mpr_obj)g->maps, id);
        if (!map && _obj_by_id(g, (mpr_obj)g->maps, 0)) {
            /* may have staged map stored locally */
            map = mpr_graph_get_map_by_names(g, num_src, src_names, dst_name);
        }
    }

    if (!map) {
        mpr_sig *src_sigs, dst_sig;
#ifdef DEBUG
        trace_graph("adding map [");
        for (i = 0; i < num_src; i++)
            printf("%s, ", src_names[i]);
        printf("\b\b] -> [%s].\n", dst_name);
#endif
        /* add signals first in case signal handlers trigger map queries */
        dst_sig = add_sig_from_whole_name(g, dst_name);
        RETURN_ARG_UNLESS(dst_sig, 0);
        src_sigs = alloca(num_src * sizeof(mpr_sig));
        for (i = 0; i < num_src; i++) {
            src_sigs[i] = add_sig_from_whole_name(g, src_names[i]);
            RETURN_ARG_UNLESS(src_sigs[i], 0);
            mpr_graph_add_link(g, dst_sig->dev, src_sigs[i]->dev);
            is_local += src_sigs[i]->is_local;
        }
        is_local += dst_sig->is_local;

        map = (mpr_map)mpr_list_add_item((void**)&g->maps,
                                         is_local ? sizeof(mpr_local_map_t) : sizeof(mpr_map_t));
        map->obj.type = MPR_MAP;
        map->obj.graph = g;
        map->obj.id = id;
        map->num_src = num_src;
        map->is_local = 0;
        map->src = (mpr_slot*)malloc(sizeof(mpr_slot) * num_src);
        for (i = 0; i < num_src; i++)
            map->src[i] = mpr_slot_new(map, src_sigs[i], is_local, 1);
        map->dst = mpr_slot_new(map, dst_sig, is_local, 0);
        mpr_map_init(map);
        ++g->staged_maps;
        rc = 1;
    }
    else {
        int changed = 0;
        /* may need to add sources to existing map */
        for (i = 0; i < num_src; i++) {
            mpr_sig src_sig = add_sig_from_whole_name(g, src_names[i]);
            is_local += src_sig->is_local;
            /* TODO: check if we might need to 'upgrade' existing map to local */
            RETURN_ARG_UNLESS(src_sig, 0);
            for (j = 0; j < map->num_src; j++) {
                if (map->src[j]->sig == src_sig)
                    break;
            }
            if (j == map->num_src) {
                ++changed;
                ++map->num_src;
                map->src = realloc(map->src, sizeof(mpr_slot) * map->num_src);
                map->src[j] = mpr_slot_new(map, src_sig, is_local, 1);
                ++updated;
            }
        }
        if (changed) {
            mpr_list maps;
            /* slots should be in alphabetical order */
            qsort(map->src, map->num_src, sizeof(mpr_slot), _compare_slot_names);
            /* fix slot ids */
            for (i = 0; i < num_src; i++)
                map->src[i]->id = i;
            /* check again if this mirrors a staged map */
            maps = mpr_list_from_data(g->maps);
            while (maps) {
                mpr_map map2 = (mpr_map)*maps;
                maps = mpr_list_get_next(maps);
                if (map2->obj.id != 0 || map->num_src != map2->num_src
                    || map->dst->sig != map2->dst->sig)
                    continue;
                for (i = 0; i < map->num_src; i++) {
                    if (map->src[i]->sig != map2->src[i]->sig) {
                        map2 = NULL;
                        break;
                    }
                }
                if (map2) {
                    mpr_graph_remove_map(g, map2, 0);
                    break;
                }
            }
        }
    }

    if (map) {
#ifdef DEBUG
        if (!rc) {
            trace_graph("updated %d props for map [", updated);
            for (i = 0; i < map->num_src; i++) {
                printf("%s:%s, ", map->src[i]->sig->dev->name, map->src[i]->sig->name);
            }
            printf("\b\b] -> [%s:%s]\n", map->dst->sig->dev->name, map->dst->sig->name);
        }
#endif
        RETURN_ARG_UNLESS(map->status >= MPR_STATUS_ACTIVE, map);
        if (rc || updated)
            mpr_graph_call_cbs(g, (mpr_obj)map, MPR_MAP, rc ? MPR_OBJ_NEW : MPR_OBJ_MOD);
    }
    return map;
}

void mpr_graph_remove_map(mpr_graph g, mpr_map m, mpr_graph_evt e)
{
    RETURN_UNLESS(m);
    mpr_list_remove_item((void**)&g->maps, m);
    mpr_graph_call_cbs(g, (mpr_obj)m, MPR_MAP, e);
    mpr_map_free(m);
    mpr_list_free_item(m);
}

void mpr_graph_print(mpr_graph g)
{
    mpr_list devs = mpr_list_from_data(g->devs);
    mpr_list sigs = mpr_list_from_data(g->sigs);
    mpr_list maps;
    printf("-------------------------------\n");
    printf("Registered devices (%d) and signals (%d):\n",
           mpr_list_get_size(devs), mpr_list_get_size(sigs));
    mpr_list_free(sigs);
    while (devs) {
        printf(" └─ ");
        mpr_obj_print(*devs, 0);
        sigs = mpr_dev_get_sigs((mpr_dev)*devs, MPR_DIR_ANY);
        while (sigs) {
            mpr_sig sig = (mpr_sig)*sigs;
            sigs = mpr_list_get_next(sigs);
            printf("    %s ", sigs ? "├─" : "└─");
            mpr_obj_print((mpr_obj)sig, 0);
        }
        devs = mpr_list_get_next(devs);
    }

    printf("-------------------------------\n");
    maps = mpr_list_from_data(g->maps);
    printf("Registered maps (%d):\n", mpr_list_get_size(maps));
    while (maps) {
        mpr_map map = (mpr_map)*maps;
        printf(" └─ ");
        mpr_obj_print((mpr_obj)map, 0);
        sigs = mpr_map_get_sigs(map, MPR_LOC_SRC);
        while (sigs) {
            mpr_sig sig = (mpr_sig)*sigs;
            sigs = mpr_list_get_next(sigs);
            printf("    ├─ SRC ");
            mpr_obj_print((mpr_obj)sig, 0);
        }
        sigs = mpr_map_get_sigs(map, MPR_LOC_DST);
        while (sigs) {
            mpr_sig sig = (mpr_sig)*sigs;
            sigs = mpr_list_get_next(sigs);
            printf("    └─ DST ");
            mpr_obj_print((mpr_obj)sig, 0);
        }
        maps = mpr_list_get_next(maps);
    }

    printf("-------------------------------\n");
}

static void renew_subscriptions(mpr_graph g, uint32_t time_sec)
{
    /* check if any subscriptions need to be renewed */
    mpr_subscription s = g->subscriptions;
    while (s) {
        if (s->lease_expiration_sec <= time_sec) {
            trace_graph("Automatically renewing subscription to %s for %d secs.\n",
                        mpr_dev_get_name(s->dev), AUTOSUB_INTERVAL);
            send_subscribe_msg(g, s->dev, s->flags, AUTOSUB_INTERVAL);
            /* leave 10-second buffer for subscription renewal */
            s->lease_expiration_sec = (time_sec + AUTOSUB_INTERVAL - 10);
        }
        s = s->next;
    }
}

int mpr_graph_poll(mpr_graph g, int block_ms)
{
    mpr_net n = &g->net;
    int count = 0, status[2], left_ms, elapsed, checked_admin = 0;
    mpr_time t;
    double then;

    mpr_net_poll(n);
    mpr_time_set(&t, MPR_NOW);
    renew_subscriptions(g, t.sec);
    _check_dev_status(g, t.sec);

    if (!block_ms) {
        if (lo_servers_recv_noblock(&n->servers[SERVER_ADMIN], status, 2, 0)) {
            count = (status[0] > 0) + (status[1] > 0);
            n->msgs_recvd |= count;
        }
        return count;
    }

    then = mpr_get_current_time();
    left_ms = block_ms;
    while (left_ms > 0) {
        if (left_ms > 100)
            left_ms = 100;

        if (lo_servers_recv_noblock(&n->servers[SERVER_ADMIN], status, 2, left_ms))
            count += (status[0] > 0) + (status[1] > 0);

        elapsed = (mpr_get_current_time() - then) * 1000;
        if ((elapsed - checked_admin) > 100) {
            mpr_time_set(&t, MPR_NOW);
            renew_subscriptions(g, t.sec);
            mpr_net_poll(n);
            _check_dev_status(g, t.sec);
            checked_admin = elapsed;
        }

        left_ms = block_ms - elapsed;
    }

    n->msgs_recvd |= count;
    return count;
}

static mpr_subscription _get_subscription(mpr_graph g, mpr_dev d)
{
    mpr_subscription s = g->subscriptions;
    while (s) {
        if (s->dev == d)
            return s;
        s = s->next;
    }
    return 0;
}

void mpr_graph_subscribe(mpr_graph g, mpr_dev d, int flags, int timeout)
{
    if (!d) {
        _autosubscribe(g, flags);
        return;
    }
    else if (d->is_local) {
        /* don't bother subscribing to local device */
        trace_graph("aborting subscription, device is local.\n");
        return;
    }
    if (0 == flags || 0 == timeout) {
        mpr_subscription *s = &g->subscriptions, temp;
        while (*s) {
            if ((*s)->dev == d) {
                /* remove from subscriber list */
                (*s)->dev->subscribed = 0;
                temp = *s;
                *s = temp->next;
                free(temp);
                send_subscribe_msg(g, d, 0, 0);
                return;
            }
            s = &(*s)->next;
        }
    }
    else if (-1 == timeout) {
        mpr_time t;
#ifdef DEBUG
        trace_graph("adding %d-second autorenewing subscription to device '%s' with flags ",
                    AUTOSUB_INTERVAL, mpr_dev_get_name(d));
        print_subscription_flags(flags);
#endif
        /* special case: autorenew subscription lease */
        /* first check if subscription already exists */
        mpr_subscription s = _get_subscription(g, d);

        if (!s) {
            /* store subscription record */
            s = malloc(sizeof(struct _mpr_subscription));
            s->flags = 0;
            s->dev = d;
            s->dev->obj.version = -1;
            s->next = g->subscriptions;
            g->subscriptions = s;
        }
        d->subscribed = 1;
        if (s->flags == flags)
            return;

        s->dev->obj.version = -1;
        s->flags = flags;

        mpr_time_set(&t, MPR_NOW);
        /* leave 10-second buffer for subscription lease */
        s->lease_expiration_sec = (t.sec + AUTOSUB_INTERVAL - 10);

        timeout = AUTOSUB_INTERVAL;
    }
#ifdef DEBUG
    else {
        trace_graph("adding temporary %d-second subscription to device '%s' with flags ",
                    timeout, mpr_dev_get_name(d));
        print_subscription_flags(flags);
    }
#endif

    send_subscribe_msg(g, d, flags, timeout);
}

void mpr_graph_unsubscribe(mpr_graph g, mpr_dev d)
{
    if (!d)
        _autosubscribe(g, 0);
    mpr_graph_subscribe(g, d, 0, 0);
}

int mpr_graph_subscribed_by_dev(mpr_graph g, const char *name)
{
    mpr_dev dev = mpr_graph_get_dev_by_name(g, name);
    if (dev) {
        mpr_subscription s = _get_subscription(g, dev);
        return s ? s->flags : 0;
    }
    return 0;
}

int mpr_graph_subscribed_by_sig(mpr_graph g, const char *name)
{
    mpr_dev dev;
    char *devnamep, *signame, devname[256];
    int devnamelen = mpr_parse_names(name, &devnamep, &signame);
    if (!devnamelen || devnamelen >= 256) {
        trace_graph("error extracting device name\n");
        return 0;
    }
    strncpy(devname, devnamep, devnamelen);
    devname[devnamelen] = 0;
    dev = mpr_graph_get_dev_by_name(g, devname);
    if (dev) {
        mpr_subscription s = _get_subscription(g, dev);
        return s ? s->flags : 0;
    }
    return 0;
}

void mpr_graph_set_interface(mpr_graph g, const char *iface)
{
    mpr_net_init(&g->net, iface, 0, 0);
}

const char *mpr_graph_get_interface(mpr_graph g)
{
    return g->net.iface.name;
}

void mpr_graph_set_address(mpr_graph g, const char *group, int port)
{
    mpr_net_init(&g->net, g->net.iface.name, group, port);
}

const char *mpr_graph_get_address(mpr_graph g)
{
    if (!g->net.addr.url)
        g->net.addr.url = lo_address_get_url(g->net.addr.bus);
    return g->net.addr.url;
}
