#include <string.h>
#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#ifndef _MSC_VER
#include <sys/time.h>
#else

#endif

#include "graph.h"
#include "link.h"
#include "mpr_time.h"
#include "path.h"
#include "property.h"
#include "slot.h"
#include "table.h"

#include <mapper/mapper.h>

#ifdef _MSC_VER
#include <malloc.h>
#endif

#define AUTOSUB_INTERVAL 60
extern const char* net_msg_strings[NUM_MSG_STRINGS];

/*! Debug tracer */
#if defined(__GNUC__) || defined(WIN32)
    #ifdef DEBUG
        #define trace_graph(graph, ...)                     \
        {                                                   \
            printf("\x1B[31m-- <graph.%p>\x1B[0m ", graph); \
            printf(__VA_ARGS__);                            \
        }
    #else /* !DEBUG */
        #define trace_graph(...) {}
    #endif /* DEBUG */
#else /* !__GNUC__ */
    #define trace_graph(...) {};
#endif /* __GNUC__ */

/*! A list of function and context pointers. */
typedef struct _fptr_list {
    void *f;
    void *ctx;
    struct _fptr_list *next;
    int types;
} *fptr_list;

typedef struct _mpr_subscription {
    struct _mpr_subscription *next;
    mpr_dev dev;
    int flags;
    uint32_t lease_expiration_sec;
} *mpr_subscription;

typedef struct _mpr_graph {
    mpr_obj_t obj;                  /* always first */
    mpr_net net;
    mpr_list devs;                  /*!< List of devices. */
    mpr_list sigs;                  /*!< List of signals. */
    mpr_list maps;                  /*!< List of maps. */
    mpr_list links;                 /*!< List of links. */
    fptr_list callbacks;            /*!< List of object record callbacks. */

    /*! Linked-list of autorenewing device subscriptions. */
    mpr_subscription subscriptions;

    mpr_expr_eval_buffer expr_eval_buff;

    /*! Flags indicating whether information on signals and mappings should
     *  be automatically subscribed to when a new device is seen.*/
    int autosub;

    int own;
    int staged_maps;

    uint32_t resource_counter;
} mpr_graph_t;

static mpr_list *get_list_internal(mpr_graph g, int obj_type)
{
    switch (obj_type) {
        case MPR_DEV:  return &g->devs;
        case MPR_LINK: return &g->links;
        case MPR_MAP:  return &g->maps;
        case MPR_SIG:  return &g->sigs;
        default:       return 0;
    }
}

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

static void on_dev_autosubscribe(mpr_graph g, mpr_obj o, mpr_graph_evt e, const void *v)
{
    /* New subscriptions are handled in network.c as response to "sync" msg */
    if (MPR_STATUS_REMOVED == e)
        mpr_graph_subscribe(g, (mpr_dev)o, 0, 0);
}

static void set_net_dst(mpr_graph g, mpr_dev d)
{
    /* TODO: look up device info, maybe send directly */
    mpr_net_use_bus(g->net);
}

static void send_subscribe_msg(mpr_graph g, mpr_dev d, int flags, int timeout)
{
    char cmd[1024];
    NEW_LO_MSG(msg, return);
    snprintf(cmd, 1024, "/%s/subscribe", mpr_dev_get_name(d)); /* MSG_SUBSCRIBE */

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
    lo_message_add_int32(msg, mpr_obj_get_version((mpr_obj)d));

    mpr_net_add_msg(g->net, cmd, 0, msg);
    mpr_net_send(g->net);
}

static void autosubscribe(mpr_graph g, int flags)
{
    if (!g->autosub && flags) {
        /* update flags for existing subscriptions */
        mpr_subscription s = g->subscriptions;
        mpr_time t;
        NEW_LO_MSG(msg, ;);
        mpr_time_set(&t, MPR_NOW);
        while (s) {
            trace_graph(g, "adjusting flags for existing autorenewing subscription to %s.\n",
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
            trace_graph(g, "pinging all devices.\n");
            mpr_net_use_bus(g->net);
            mpr_net_add_msg(g->net, 0, MSG_WHO, msg);
        }
        mpr_graph_add_cb(g, on_dev_autosubscribe, MPR_DEV, g);
    }
    else if (g->autosub && !flags) {
        mpr_graph_remove_cb(g, on_dev_autosubscribe, g);
        while (g->subscriptions)
            mpr_graph_subscribe(g, g->subscriptions->dev, 0, 0);
    }
#ifdef DEBUG
    trace_graph(g, "setting autosubscribe flags to ");
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
    trace_graph(g, "checking %d staged maps\n", g->staged_maps);
    /* check for maps that were staged but never completed */
    maps = mpr_list_from_data(g->maps);
    while (maps) {
        mpr_map map = (mpr_map)*maps;
        int status = mpr_obj_get_status((mpr_obj)map);
        maps = mpr_list_get_next(maps);
        if (status & MPR_STATUS_ACTIVE || !mpr_obj_get_is_local((mpr_obj)map))
            continue;

#ifdef DEBUG
        trace_graph(g, "  checking map: ");
        mpr_prop_print(1, MPR_MAP, map);
        printf(", status=%d\n", status);
#endif

        if (status & MPR_STATUS_EXPIRED) {
            trace_graph(g, "  removing expired map\n");
            mpr_graph_remove_map(g, map, MPR_STATUS_EXPIRED);
        }
        else {
            if (!(status & MPR_MAP_STATUS_PUSHED)) {
                /* update map status */
                status = mpr_local_map_update_status((mpr_local_map)map);
                if (status & MPR_SLOT_DEV_KNOWN) {
                    /* Try pushing the map to the distributed graph */
                    trace_graph(g, "  pushing staged map to network\n");
                    mpr_obj_push((mpr_obj)map);
                }
            }
            mpr_map_status_decr(map);
            ++staged;
        }
    }
    g->staged_maps = staged;
}

mpr_graph mpr_graph_new(int subscribe_flags)
{
    mpr_tbl tbl;
    mpr_graph g;
    RETURN_ARG_UNLESS(subscribe_flags <= MPR_OBJ, NULL);
    g = (mpr_graph) calloc(1, sizeof(mpr_graph_t));
    RETURN_ARG_UNLESS(g, NULL);

    mpr_obj_init((mpr_obj)g, g, MPR_GRAPH);
    g->obj.id = 0;
    g->own = 1;
    g->net = mpr_net_new(g);
    if (subscribe_flags)
        autosubscribe(g, subscribe_flags);

    /* TODO: consider whether graph objects should sync properties over the network. */
    tbl = g->obj.props.synced = mpr_tbl_new();
    mpr_tbl_link_value(tbl, PROP(DATA), 1, MPR_PTR, &g->obj.data,
                       MOD_LOCAL | INDIRECT | LOCAL_ACCESS);
    mpr_tbl_add_record(tbl, PROP(LIBVER), NULL, 1, MPR_STR, PACKAGE_VERSION, MOD_NONE);
    /* TODO: add object queries as properties. */

    g->expr_eval_buff = mpr_expr_new_eval_buffer(NULL);

    return g;
}

void mpr_graph_free_cbs(mpr_graph g)
{
    while (g->callbacks) {
        fptr_list cb = g->callbacks;
        g->callbacks = g->callbacks->next;
        free(cb);
    }
}

static void mpr_graph_gc(mpr_graph g)
{
    mpr_list list;

    /* check if any signals need to be removed */
    list = mpr_list_from_data(g->sigs);
    while (list) {
        mpr_obj sig = *list;
        list = mpr_list_get_next(list);
        if (sig->is_local && sig->status & MPR_STATUS_REMOVED)
            mpr_graph_remove_sig(g, (mpr_sig)sig, MPR_STATUS_REMOVED);
    }
}

void mpr_graph_free(mpr_graph g)
{
    mpr_list list;
    RETURN_UNLESS(g);

    /* remove callbacks now so they won't be called when removing devices */
    mpr_graph_free_cbs(g);

    mpr_graph_gc(g);

    /* unsubscribe from and remove any autorenewing subscriptions */
    while (g->subscriptions)
        mpr_graph_subscribe(g, g->subscriptions->dev, 0, 0);

    /* Remove all non-local maps */
    list = mpr_list_from_data(g->maps);
    while (list) {
        mpr_obj map = *list;
        list = mpr_list_get_next(list);
        if (!map->is_local) {
            mpr_graph_remove_map(g, (mpr_map)map, MPR_STATUS_REMOVED);
        }
    }

    /* Remove all non-local links */
    list = mpr_list_from_data(g->links);
    while (list) {
        mpr_obj link = *list;
        list = mpr_list_get_next(list);
        if (!link->is_local)
            mpr_graph_remove_link(g, (mpr_link)link, MPR_STATUS_REMOVED);
    }

    /* Remove all non-local devices and signals from the graph except for
     * those referenced by local maps. */
    list = mpr_list_from_data(g->devs);
    while (list) {
        mpr_obj dev = *list;
        int no_local_dev_maps = 1;
        mpr_list sigs;
        list = mpr_list_get_next(list);
        if (dev->is_local)
            continue;

        sigs = mpr_dev_get_sigs((mpr_dev)dev, MPR_DIR_ANY);
        while (sigs) {
            int no_local_sig_maps = 1;
            mpr_obj sig = *sigs;
            mpr_list maps = mpr_sig_get_maps((mpr_sig)sig, MPR_DIR_ANY);
            while (maps) {
                mpr_obj map = *maps;
                if (map->is_local) {
                    no_local_dev_maps = no_local_sig_maps = 0;
                    mpr_list_free(maps);
                    break;
                }
                maps = mpr_list_get_next(maps);
            }
            sigs = mpr_list_get_next(sigs);
            if (no_local_sig_maps)
                mpr_graph_remove_sig(g, (mpr_sig)sig, MPR_STATUS_REMOVED);
        }
        if (no_local_dev_maps)
            mpr_graph_remove_dev(g, (mpr_dev)dev, MPR_STATUS_REMOVED);
    }

    FUNC_IF(mpr_expr_free_eval_buffer, g->expr_eval_buff);
    mpr_net_free(g->net);
    mpr_obj_free(&g->obj);
    free(g);
}

/**** Generic records ****/

static mpr_obj get_obj_by_id(mpr_graph g, void *obj_list, mpr_id id)
{
    mpr_list objs = mpr_list_from_data(obj_list);
    while (objs) {
        if (id == (*objs)->id)
            return *objs;
        objs = mpr_list_get_next(objs);
    }
    return NULL;
}

mpr_obj mpr_graph_get_obj(mpr_graph g, mpr_id id, mpr_type type)
{
    mpr_obj o;
    if ((type & MPR_DEV) && (o = get_obj_by_id(g, g->devs, id)))
        return o;
    if ((type & MPR_SIG) && (o = get_obj_by_id(g, g->sigs, id)))
        return o;
    if ((type & MPR_MAP) && (o = get_obj_by_id(g, g->maps, id)))
        return o;
    return 0;
}

/* TODO: support queries over multiple object types. */
mpr_list mpr_graph_get_list(mpr_graph g, int types)
{
    mpr_list *list = get_list_internal(g, types);
    assert(list);
    return mpr_list_from_data(*list);
}

mpr_list mpr_graph_new_query(mpr_graph g, int start, int obj_type,
                             const void *func, const char *types, ...)
{
    mpr_list qry = 0, *list = get_list_internal(g, obj_type);
    va_list aq;
    RETURN_ARG_UNLESS(list && (!start || *list), 0);
    va_start(aq, types);
    qry = vmpr_list_new_query((const void**)list, func, types, aq);
    va_end(aq);
    return start ? mpr_list_start(qry) : qry;
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

    /* add event to object and graph status */
    mpr_obj_set_status(o, e, 0);
    g->obj.status |= e;

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

static void remove_by_qry(mpr_graph g, mpr_list l, mpr_graph_evt e)
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

static mpr_subscription get_subscription(mpr_graph g, mpr_dev d)
{
    mpr_subscription s = g->subscriptions;
    while (s) {
        if (s->dev == d)
            return s;
        s = s->next;
    }
    return 0;
}

mpr_dev mpr_graph_add_dev(mpr_graph g, const char *name, mpr_msg msg, int force)
{
    const char *no_slash = mpr_path_skip_slash(name);
    mpr_dev dev = mpr_graph_get_dev_by_name(g, no_slash);
    int rc = 0, updated = 0;

    if (!force && !g->autosub) {
        if (dev) {
            mpr_subscription s = get_subscription(g, dev);
            mpr_list l = mpr_dev_get_links(dev, MPR_DIR_UNDEFINED);
            if (l)
                mpr_list_free(l);
            else if (!s || !s->flags)
                return dev;
        }
        else
            return 0;
    }

    if (!dev) {
        mpr_id id = mpr_id_from_str(no_slash);
        dev = (mpr_dev)mpr_list_add_item((void**)&g->devs, mpr_dev_get_struct_size(0), 0);
        mpr_obj_init((mpr_obj)dev, g, MPR_DEV);
        mpr_dev_init(dev, 0, no_slash, id);
#ifdef DEBUG
        trace_graph(g, "added device ");
        mpr_prop_print(1, MPR_DEV, dev);
        printf("\n");
#endif
        rc = 1;

        if (!mpr_dev_get_is_subscribed(dev) && g->autosub)
            mpr_graph_subscribe(g, dev, g->autosub, -1);
    }

    if (dev) {
        updated = mpr_dev_set_from_msg(dev, msg);
#ifdef DEBUG
        if (!rc) {
            trace_graph(g, "updated %d props for device ", updated);
            mpr_prop_print(1, MPR_DEV, dev);
            printf("\n");
        }
#endif
        mpr_dev_set_synced(dev, MPR_NOW);

        if (rc || updated)
            mpr_graph_call_cbs(g, (mpr_obj)dev, MPR_DEV, rc ? MPR_STATUS_NEW : MPR_STATUS_MODIFIED);
    }
    return dev;
}

/* Internal function called by /logout protocol handler */
void mpr_graph_remove_dev(mpr_graph g, mpr_dev d, mpr_graph_evt e)
{
    mpr_list list;
    RETURN_UNLESS(d);
    remove_by_qry(g, mpr_dev_get_maps(d, MPR_DIR_ANY), e);

    /* remove matching device links (linked property) */
    list = mpr_graph_get_list(g, MPR_DEV);
    while (list) {
        if ((mpr_dev)*list != d)
            mpr_dev_remove_link((mpr_dev)*list, d);
        list = mpr_list_get_next(list);
    }

    /* remove matching map scopes */
    list = mpr_graph_get_list(g, MPR_MAP);
    while (list) {
        mpr_map_remove_scope_internal((mpr_map)*list, d);
        list = mpr_list_get_next(list);
    }

    remove_by_qry(g, mpr_dev_get_links(d, MPR_DIR_UNDEFINED), e);
    remove_by_qry(g, mpr_dev_get_sigs(d, MPR_DIR_ANY), e);

    mpr_list_remove_item((void**)&g->devs, d);
    mpr_graph_call_cbs(g, (mpr_obj)d, MPR_DEV, e);

#ifdef DEBUG
    trace_graph(g, "removed device ");
    mpr_prop_print(1, MPR_DEV, d);
    printf("\n");
#endif

    mpr_obj_free((mpr_obj)d);
    mpr_dev_free_mem(d);
    mpr_list_free_item(d);
}

mpr_dev mpr_graph_get_dev_by_name(mpr_graph g, const char *name)
{
    const char *no_slash = mpr_path_skip_slash(name);
    mpr_list devs = mpr_list_from_data(g->devs);
    while (devs) {
        mpr_dev dev = (mpr_dev)*devs;
        name = mpr_dev_get_name(dev);
        if (name && (0 == strcmp(name, no_slash)))
            return dev;
        devs = mpr_list_get_next(devs);
    }
    return 0;
}

/**** Signals ****/

mpr_sig mpr_graph_add_sig(mpr_graph g, const char *name, const char *dev_name, mpr_msg msg)
{
    mpr_sig sig = 0;
    int rc = 0, updated = 0;

    mpr_dev dev = mpr_graph_get_dev_by_name(g, dev_name);
    if (dev) {
        sig = mpr_dev_get_sig_by_name(dev, name);
        if (sig && mpr_obj_get_is_local((mpr_obj)sig))
            return sig;
    }
    else
        dev = mpr_graph_add_dev(g, dev_name, 0, 1);

    if (!sig) {
        int num_inst = 1;
        sig = (mpr_sig)mpr_list_add_item((void**)&g->sigs, mpr_sig_get_struct_size(0), 0);
        mpr_obj_init((mpr_obj)sig, g, MPR_SIG);
        mpr_sig_init(sig, dev, 0, MPR_DIR_UNDEFINED, name, 0, 0, 0, 0, 0, &num_inst);
        rc = 1;
#ifdef DEBUG
        trace_graph(g, "added signal ");
        mpr_prop_print(1, MPR_SIG, sig);
        printf("\n");
#endif
    }

    if (sig) {
        updated = mpr_sig_set_from_msg(sig, msg);
#ifdef DEBUG
        if (!rc) {
            trace_graph(g, "updated %d props for signal ", updated);
            mpr_prop_print(1, MPR_SIG, sig);
            printf("\n");
        }
#endif

        if (rc || updated)
            mpr_graph_call_cbs(g, (mpr_obj)sig, MPR_SIG, rc ? MPR_STATUS_NEW : MPR_STATUS_MODIFIED);
    }
    return sig;
}

void mpr_graph_remove_sig(mpr_graph g, mpr_sig s, mpr_graph_evt e)
{
    RETURN_UNLESS(s);

    /* remove any stored maps using this signal */
    remove_by_qry(g, mpr_sig_get_maps(s, MPR_DIR_ANY), e);

    mpr_list_remove_item((void**)&g->sigs, s);
    mpr_graph_call_cbs(g, (mpr_obj)s, MPR_SIG, e);

#ifdef DEBUG
    trace_graph(g, "removed signal ");
    mpr_prop_print(1, MPR_SIG, s);
    printf("\n");
#endif

    mpr_sig_free_internal(s);
    mpr_list_free_item(s);
}

/**** Link records ****/

mpr_link mpr_graph_add_link(mpr_graph g, mpr_dev dev1, mpr_dev dev2)
{
    mpr_link link;
    RETURN_ARG_UNLESS(dev1 && dev2, 0);
    link = mpr_dev_get_link_by_remote(dev1, dev2);
    if (link)
        return link;

    link = (mpr_link)mpr_list_add_item((void**)&g->links, mpr_link_get_struct_size(), 0);
    mpr_obj_init((mpr_obj)link, g, MPR_LINK);
    if (mpr_obj_get_is_local((mpr_obj)dev2))
        mpr_link_init(link, g, dev2, dev1);
    else
        mpr_link_init(link, g, dev1, dev2);

#ifdef DEBUG
    trace_graph(g, "added link ");
    mpr_prop_print(1, MPR_LINK, link);
    printf("\n");
#endif

    return link;
}

void mpr_graph_remove_link(mpr_graph g, mpr_link l, mpr_graph_evt e)
{
    RETURN_UNLESS(l);
    remove_by_qry(g, mpr_link_get_maps(l), e);
    mpr_list_remove_item((void**)&g->links, l);

#ifdef DEBUG
    trace_graph(g, "removed link ");
    mpr_prop_print(1, MPR_LINK, l);
    printf("\n");
#endif

    mpr_link_free(l);
    mpr_list_free_item(l);
}

/**** Map records ****/

static mpr_sig add_sig_from_whole_name(mpr_graph g, const char* name)
{
    char *devnamep, *signame, devname[256];
    int devnamelen = mpr_path_parse(name, &devnamep, &signame);
    if (!devnamelen || devnamelen >= 256) {
        trace_graph(g, "error extracting device name\n");
        return 0;
    }
    strncpy(devname, devnamep, devnamelen);
    devname[devnamelen] = 0;
    return mpr_graph_add_sig(g, signame, devname, 0);
}

mpr_map mpr_graph_get_map_by_names(mpr_graph g, int num_src, const char **srcs, const char *dst)
{
    mpr_list maps = mpr_list_from_data(g->maps);
    while (maps) {
        mpr_map map = (mpr_map)*maps;
        if (mpr_map_compare_names(map, num_src, srcs, dst))
            return map;
        maps = mpr_list_get_next(maps);
    }
    return 0;
}

mpr_map mpr_graph_add_map(mpr_graph g, mpr_id id, int num_src, const char **src_names,
                          const char *dst_name)
{
    mpr_map map = 0;
    unsigned char i, j, is_local = 0;
    if (num_src > MAX_NUM_MAP_SRC) {
        trace_graph(g, "error: maximum mapping sources exceeded.\n");
        return 0;
    }

    /* We could be part of larger "convergent" mapping, so we will retrieve
     * record by mapping id instead of names. */
    if (id) {
        map = (mpr_map)get_obj_by_id(g, g->maps, id);
        if (!map && get_obj_by_id(g, g->maps, 0)) {
            /* may have staged map stored locally */
            map = mpr_graph_get_map_by_names(g, num_src, src_names, dst_name);
        }
    }

    if (!map) {
        mpr_sig *src_sigs, dst_sig;
        /* add signals first in case signal handlers trigger map queries */
        dst_sig = add_sig_from_whole_name(g, dst_name);
        RETURN_ARG_UNLESS(dst_sig, 0);
        src_sigs = alloca(num_src * sizeof(mpr_sig));
        for (i = 0; i < num_src; i++) {
            src_sigs[i] = add_sig_from_whole_name(g, src_names[i]);
            RETURN_ARG_UNLESS(src_sigs[i], 0);
            mpr_graph_add_link(g, mpr_sig_get_dev(dst_sig), mpr_sig_get_dev(src_sigs[i]));
            is_local += mpr_obj_get_is_local((mpr_obj)src_sigs[i]);
        }
        is_local += mpr_obj_get_is_local((mpr_obj)dst_sig);

        map = (mpr_map)mpr_list_add_item((void**)&g->maps, mpr_map_get_struct_size(is_local),
                                         is_local);
        mpr_obj_init((mpr_obj)map, g, MPR_MAP);
        mpr_map_init(map, num_src, src_sigs, dst_sig, is_local);
        if (id && !mpr_obj_get_id((mpr_obj)map))
            mpr_obj_set_id((mpr_obj)map, id);
#ifdef DEBUG
        trace_graph(g, "added map ");
        mpr_prop_print(1, MPR_MAP, map);
        printf("\n");
#endif
        if (mpr_obj_get_status((mpr_obj)map) & MPR_STATUS_ACTIVE)
            mpr_graph_call_cbs(g, (mpr_obj)map, MPR_MAP, MPR_STATUS_NEW);
    }
    else {
        int changed = 0;
        /* may need to add sources to existing map */
        for (i = 0; i < num_src; i++) {
            int num_src = mpr_map_get_num_src(map);
            mpr_sig src_sig = add_sig_from_whole_name(g, src_names[i]);
            is_local += mpr_obj_get_is_local((mpr_obj)src_sig);
            /* TODO: check if we might need to 'upgrade' existing map to local */
            RETURN_ARG_UNLESS(src_sig, 0);
            for (j = 0; j < num_src; j++) {
                if (mpr_map_get_src_sig(map, j) == src_sig)
                    break;
            }
            if (j == num_src) {
                ++changed;
                trace_graph(g, "adding source %s to map\n", src_names[i]);
                mpr_map_add_src(map, src_sig, MPR_DIR_UNDEFINED, is_local);
            }
        }
        if (changed) {
            /* check again if this mirrors a staged map */
            mpr_list maps = mpr_list_from_data(g->maps);
            while (maps) {
                mpr_map map2 = (mpr_map)*maps;
                maps = mpr_list_get_next(maps);
                if (mpr_map_compare(map, map2)) {
                    mpr_graph_remove_map(g, map2, 0);
                    break;
                }
            }
            if (mpr_obj_get_status((mpr_obj)map) & MPR_STATUS_ACTIVE)
                mpr_graph_call_cbs(g, (mpr_obj)map, MPR_MAP, MPR_STATUS_MODIFIED);
        }
    }
    return map;
}

void mpr_graph_remove_map(mpr_graph g, mpr_map m, mpr_graph_evt e)
{
    RETURN_UNLESS(m);
    mpr_list_remove_item((void**)&g->maps, m);
    if (mpr_obj_get_status((mpr_obj)m) & MPR_STATUS_ACTIVE)
        mpr_graph_call_cbs(g, (mpr_obj)m, MPR_MAP, e);

#ifdef DEBUG
    trace_graph(g, "removed map ");
    mpr_prop_print(1, MPR_MAP, m);
    printf("\n");
#endif

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

/* TODO: consider throttling */
void mpr_graph_housekeeping(mpr_graph g)
{
    mpr_list list = mpr_list_from_data(g->devs);
    mpr_subscription s;
    mpr_time t;
    mpr_time_set(&t, MPR_NOW);

    mpr_graph_gc(g);

    /* check if any known devices have expired */
    t.sec -= TIMEOUT_SEC;
    while (list) {
        mpr_obj dev = *list;
        list = mpr_list_get_next(list);
        /* check if device has "checked in" recently – could be /sync ping or any sent metadata */
        if (!dev->is_local) {
            if (!mpr_dev_check_synced((mpr_dev)dev, t)) {
                /* do nothing if device is linked to local device; will be handled in network.c */
                if (!mpr_dev_has_local_link((mpr_dev)dev)) {
                    /* remove subscription */
                    mpr_graph_subscribe(g, (mpr_dev)dev, 0, 0);
                    mpr_graph_remove_dev(g, (mpr_dev)dev, MPR_STATUS_EXPIRED);
                }
            }
        }
    }

    /* check if any signals need to be removed */
    list = mpr_list_from_data(g->sigs);
    while (list) {
        mpr_obj sig = *list;
        list = mpr_list_get_next(list);
        if (sig->status & MPR_STATUS_REMOVED)
            mpr_graph_remove_sig(g, (mpr_sig)sig, MPR_STATUS_REMOVED);
    }

    /* check if any subscriptions need to be renewed */
    s = g->subscriptions;
    while (s) {
        if (s->lease_expiration_sec <= t.sec) {
            trace_graph(g, "Automatically renewing subscription to %s for %d secs.\n",
                        mpr_dev_get_name(s->dev), AUTOSUB_INTERVAL);
            send_subscribe_msg(g, s->dev, s->flags, AUTOSUB_INTERVAL);
            /* leave 10-second buffer for subscription renewal */
            s->lease_expiration_sec = (t.sec + AUTOSUB_INTERVAL - 10);
        }
        s = s->next;
    }
}

int mpr_graph_poll(mpr_graph g, int block_ms)
{
    return mpr_net_poll(g->net, block_ms);
}

int mpr_graph_start_polling(mpr_graph g, int block_ms)
{
    return mpr_net_start_polling(g->net, block_ms);
}

int mpr_graph_stop_polling(mpr_graph g)
{
    return mpr_net_stop_polling(g->net);
}

void mpr_graph_subscribe(mpr_graph g, mpr_dev d, int flags, int timeout)
{
    RETURN_UNLESS(g && flags <= MPR_OBJ);
    if (!d) {
        autosubscribe(g, flags);
        return;
    }
    else if (mpr_obj_get_is_local((mpr_obj)d)) {
        /* don't bother subscribing to local device */
        trace_graph(g, "aborting subscription, device is local.\n");
        return;
    }
    if (0 == flags || 0 == timeout) {
        mpr_subscription *s = &g->subscriptions, temp;
        while (*s) {
            if ((*s)->dev == d) {
                /* remove from subscriber list */
                mpr_dev_set_is_subscribed((*s)->dev, 0);
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
        trace_graph(g, "adding %d-second autorenewing subscription to device '%s' with flags ",
                    AUTOSUB_INTERVAL, mpr_dev_get_name(d));
        print_subscription_flags(flags);
#endif
        /* special case: autorenew subscription lease */
        /* first check if subscription already exists */
        mpr_subscription s = get_subscription(g, d);

        if (!s) {
            /* store subscription record */
            s = malloc(sizeof(struct _mpr_subscription));
            s->flags = 0;
            s->dev = d;
            mpr_obj_set_version((mpr_obj)s->dev, -1);
            s->next = g->subscriptions;
            g->subscriptions = s;
        }
        mpr_dev_set_is_subscribed(d, 1);
        if (s->flags == flags)
            return;

        mpr_obj_set_version((mpr_obj)s->dev, -1);
        s->flags = flags;

        mpr_time_set(&t, MPR_NOW);
        /* leave 10-second buffer for subscription lease */
        s->lease_expiration_sec = (t.sec + AUTOSUB_INTERVAL - 10);

        timeout = AUTOSUB_INTERVAL;
    }
#ifdef DEBUG
    else {
        trace_graph(g, "adding temporary %d-second subscription to device '%s' with flags ",
                    timeout, mpr_dev_get_name(d));
        print_subscription_flags(flags);
    }
#endif

    send_subscribe_msg(g, d, flags, timeout);
}

void mpr_graph_unsubscribe(mpr_graph g, mpr_dev d)
{
    if (!d)
        autosubscribe(g, 0);
    mpr_graph_subscribe(g, d, 0, 0);
}

int mpr_graph_subscribed_by_sig(mpr_graph g, const char *name)
{
    mpr_dev dev;
    char *devnamep, *signame, devname[256];
    int devnamelen = mpr_path_parse(name, &devnamep, &signame);
    if (!devnamelen || devnamelen >= 256) {
        trace_graph(g, "error extracting device name\n");
        return 0;
    }
    strncpy(devname, devnamep, devnamelen);
    devname[devnamelen] = 0;
    dev = mpr_graph_get_dev_by_name(g, devname);
    if (dev) {
        mpr_subscription s = get_subscription(g, dev);
        return s ? s->flags : 0;
    }
    return 0;
}

int mpr_graph_set_interface(mpr_graph g, const char *iface)
{
    return iface && !mpr_net_init(g->net, iface, 0, 0) && !strcmp(iface, mpr_net_get_interface(g->net));
}

const char *mpr_graph_get_interface(mpr_graph g)
{
    return mpr_net_get_interface(g->net);
}

int mpr_graph_set_address(mpr_graph g, const char *group, int port)
{
    return mpr_net_init(g->net, NULL, group, port);
}

const char *mpr_graph_get_address(mpr_graph g)
{
    return mpr_net_get_address(g->net);
}

void mpr_graph_set_owned(mpr_graph g, int own)
{
    g->own = own;
}

mpr_net mpr_graph_get_net(mpr_graph g)
{
    return g->net;
}

int mpr_graph_get_owned(mpr_graph g)
{
    return g->own;
}

mpr_obj mpr_graph_add_obj(mpr_graph g, int obj_type, int is_local)
{
    mpr_list *list = get_list_internal(g, obj_type);
    mpr_obj obj;
    size_t size;
    RETURN_ARG_UNLESS(list, 0);

    switch (obj_type) {
        case MPR_DEV:   size = mpr_dev_get_struct_size(is_local);   break;
        case MPR_SIG:   size = mpr_sig_get_struct_size(is_local);   break;
        case MPR_MAP:   size = mpr_map_get_struct_size(is_local);   break;
        default:                                                    return 0;
    }

    obj = mpr_list_add_item((void**)list, size, is_local && (MPR_MAP == obj_type));
    mpr_obj_init(obj, g, obj_type);

    if (MPR_MAP == obj_type)
        ++g->staged_maps;

    return obj;
}

int mpr_graph_generate_unique_id(mpr_graph g)
{
    return ++g->resource_counter;
}

void mpr_graph_sync_dev(mpr_graph g, const char *name)
{
    mpr_dev dev = mpr_graph_get_dev_by_name(g, name);
    if (dev) {
        RETURN_UNLESS(!mpr_obj_get_is_local((mpr_obj)dev));
        trace_graph(g, "updating sync record for device '%s'\n", name);
        mpr_dev_set_synced(dev, MPR_NOW);
        if (!mpr_dev_get_is_subscribed(dev) && g->autosub) {
            trace_graph(g, "autosubscribing to device '%s'.\n", name);
            mpr_graph_subscribe(g, dev, g->autosub, -1);
        }
    }
    else if (g->autosub) {
        /* only create device record after requesting more information */
        /* can't use mpr_graph_subscribe() here since device is not yet known */
        char cmd[1024];
        NEW_LO_MSG(msg, return);
        trace_graph(g, "requesting metadata for device '%s'.\n", name);
        snprintf(cmd, 1024, "/%s/subscribe", name);
        lo_message_add_string(msg, "device");
        mpr_net_use_bus(g->net);
        mpr_net_add_msg(g->net, cmd, 0, msg);
        mpr_net_send(g->net);
    }
    else
        trace_graph(g, "ignoring sync from '%s' (autosubscribe = %d)\n", name, g->autosub);
}

int mpr_graph_get_autosub(mpr_graph g)
{
    return g->autosub;
}

mpr_expr_eval_buffer mpr_graph_get_expr_eval_buffer(mpr_graph g)
{
    return g->expr_eval_buff;
}

void mpr_graph_reset_obj_statuses(mpr_graph g)
{
    mpr_list list = mpr_list_from_data(g->devs);
    while (list) {
        mpr_obj_reset_status(*list);
        list = mpr_list_get_next(list);
    }
    list = mpr_list_from_data(g->sigs);
    while (list) {
        mpr_obj_reset_status(*list);
        list = mpr_list_get_next(list);
    }
    list = mpr_list_from_data(g->maps);
    while (list) {
        mpr_obj_reset_status(*list);
        list = mpr_list_get_next(list);
    }
}
