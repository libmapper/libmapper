#include <lo/lo.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#ifdef _MSC_VER
    #include <windows.h>
    #include <malloc.h>
#endif
#include <assert.h>

#include <stddef.h>

#include "device.h"
#include "graph.h"
#include "map.h"
#include "path.h"
#include "table.h"
#include "thread_data.h"

#include "util/mpr_debug.h"

#include "config.h"
#include <mapper/mapper.h>

#ifdef HAVE_LIBPTHREAD
#include <pthread.h>
static void* device_thread_func(void *data);
#endif

#ifdef HAVE_WIN32_THREADS
static unsigned __stdcall device_thread_func(void *data);
#endif

extern const char* net_msg_strings[NUM_MSG_STRINGS];

#define MPR_DEV_STRUCT_ITEMS                                            \
    mpr_obj_t obj;      /* always first for type punning */             \
    mpr_dev *linked;                                                    \
    char *name;         /*!< The full name for this device, or zero. */ \
    mpr_time synced;    /*!< Timestamp of last sync. */                 \
    int prefix_len;     /*!< Length of the prefix string. */            \
    int ordinal;                                                        \
    int num_inputs;     /*!< Number of associated input signals. */     \
    int num_outputs;    /*!< Number of associated output signals. */    \
    int num_maps_in;    /*!< Number of associated incoming maps. */     \
    int num_maps_out;   /*!< Number of associated outgoing maps. */     \
    int num_linked;     /*!< Number of linked devices. */               \
    int status;                                                         \
    uint8_t subscribed;

/*! A record that keeps information about a device. */
struct _mpr_dev {
    MPR_DEV_STRUCT_ITEMS
} mpr_dev_t;

typedef struct _mpr_subscriber {
    struct _mpr_subscriber *next;
    lo_address addr;
    uint32_t lease_exp;
    int flags;
} mpr_subscriber_t, *mpr_subscriber;

/*! Allocated resources */
typedef struct _mpr_allocated_t {
    double count_time;          /*!< The last time collision count was updated. */
    double hints[8];            /*!< Availability of a range of resource values. */
    unsigned int val;           /*!< The resource to be allocated. */
    int collision_count;        /*!< The number of collisions detected. */
    uint8_t locked;             /*!< Whether or not the value has been locked (allocated). */
    uint8_t online;             /*!< Whether or not we are connected to the
                                 *   distributed allocation network. */
} mpr_allocated_t, *mpr_allocated;

struct _mpr_local_dev {
    MPR_DEV_STRUCT_ITEMS

    lo_server servers[4];

    mpr_allocated_t ordinal_allocator;  /*!< A unique ordinal for this device instance. */
    int registered;                     /*!< Non-zero if this device has been registered. */

    int n_output_callbacks;

    mpr_subscriber subscribers;         /*!< Linked-list of subscribed peers. */

    struct {
        struct _mpr_id_map **active;    /*!< The list of active instance id maps. */
        struct _mpr_id_map *reserve;    /*!< The list of reserve instance id maps. */
    } id_maps;

    mpr_thread_data thread_data;

    mpr_time time;
    int num_sig_groups;
    uint8_t time_is_stale;
    uint8_t polling;
    uint8_t sending;
    uint8_t receiving;
    uint8_t own_graph;
} mpr_local_dev_t;

/* prototypes */
static void mpr_dev_start_servers(mpr_local_dev dev);
static void mpr_dev_remove_id_map(mpr_local_dev dev, int group, mpr_id_map rem);
static int process_outgoing_maps(mpr_local_dev dev);
static int check_registration(mpr_local_dev dev);

mpr_time ts = {0,1};

size_t mpr_dev_get_struct_size()
{
    return sizeof(mpr_dev_t);
}

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

static int cmp_qry_sigs(const void *context_data, mpr_sig sig)
{
    mpr_id dev_id = *(mpr_id*)context_data;
    int dir = *(int*)((char*)context_data + sizeof(mpr_id));
    mpr_dev dev = mpr_sig_get_dev(sig);
    return ((dir & mpr_sig_get_dir(sig)) && (dev_id == dev->obj.id));
}

void mpr_dev_init(mpr_dev dev, int is_local, const char *name, mpr_id id)
{
    int mod = is_local ? MOD_NONE : MOD_ANY;
    mpr_tbl tbl;
    mpr_list qry;

    dev->obj.is_local = is_local;
    dev->status = MPR_STATUS_STAGED;
    if (name) {
        assert(!dev->name);
        dev->name = strdup(name);
    }
    if (id) {
        assert(!dev->obj.id);
        dev->obj.id = id;
    }

    dev->obj.props.synced = mpr_tbl_new();
    if (!is_local)
        dev->obj.props.staged = mpr_tbl_new();
    tbl = dev->obj.props.synced;

    /* these properties need to be added in alphabetical order */
    mpr_tbl_link_value(tbl, PROP(DATA), 1, MPR_PTR, &dev->obj.data,
                       MOD_LOCAL | INDIRECT | LOCAL_ACCESS);
    mpr_tbl_link_value(tbl, PROP(ID), 1, MPR_INT64, &dev->obj.id, mod);
    qry = mpr_graph_new_query(dev->obj.graph, 0, MPR_DEV, (void*)cmp_qry_linked, "v", &dev);
    mpr_tbl_link_value(tbl, PROP(LINKED), 1, MPR_LIST, qry, MOD_NONE | PROP_OWNED);
    mpr_tbl_link_value(tbl, PROP(NAME), 1, MPR_STR, &dev->name, mod | INDIRECT | LOCAL_ACCESS);
    mpr_tbl_link_value(tbl, PROP(NUM_MAPS_IN), 1, MPR_INT32, &dev->num_maps_in, mod);
    mpr_tbl_link_value(tbl, PROP(NUM_MAPS_OUT), 1, MPR_INT32, &dev->num_maps_out, mod);
    mpr_tbl_link_value(tbl, PROP(NUM_SIGS_IN), 1, MPR_INT32, &dev->num_inputs, mod);
    mpr_tbl_link_value(tbl, PROP(NUM_SIGS_OUT), 1, MPR_INT32, &dev->num_outputs, mod);
    mpr_tbl_link_value(tbl, PROP(ORDINAL), 1, MPR_INT32, &dev->ordinal, mod);
    if (!is_local) {
        qry = mpr_graph_new_query(dev->obj.graph, 0, MPR_SIG, (void*)cmp_qry_sigs,
                                  "hi", dev->obj.id, MPR_DIR_ANY);
        mpr_tbl_link_value(tbl, PROP(SIG), 1, MPR_LIST, qry, MOD_NONE | PROP_OWNED);
    }
    mpr_tbl_link_value(tbl, PROP(STATUS), 1, MPR_INT32, &dev->status, mod | LOCAL_ACCESS);
    mpr_tbl_link_value(tbl, PROP(SYNCED), 1, MPR_TIME, &dev->synced, mod | LOCAL_ACCESS);
    mpr_tbl_link_value(tbl, PROP(VERSION), 1, MPR_INT32, &dev->obj.version, mod);

    if (is_local)
        mpr_tbl_add_record(tbl, PROP(LIBVER), NULL, 1, MPR_STR, PACKAGE_VERSION, MOD_NONE);
    mpr_tbl_add_record(tbl, PROP(IS_LOCAL), NULL, 1, MPR_BOOL, &is_local, LOCAL_ACCESS | MOD_NONE);
}

/*! Allocate and initialize a device. This function is called to create a new
 *  mpr_dev, not to create a representation of remote devices. */
mpr_dev mpr_dev_new(const char *name_prefix, mpr_graph graph)
{
    mpr_local_dev dev;
    mpr_graph g;
    RETURN_ARG_UNLESS(name_prefix, 0);
    if (name_prefix[0] == '/')
        ++name_prefix;
    TRACE_RETURN_UNLESS(!strchr(name_prefix, '/'), NULL, "error: character '/' "
                        "is not permitted in device name.\n");
    if (graph) {
        g = graph;
    }
    else {
        g = mpr_graph_new(0);
        mpr_graph_set_owned(g, 0);
    }

    dev = (mpr_local_dev)mpr_graph_add_list_item(g, MPR_DEV, sizeof(mpr_local_dev_t));
    mpr_dev_init((mpr_dev)dev, 1, NULL, 0);

    dev->own_graph = graph ? 0 : 1;
    dev->prefix_len = strlen(name_prefix);
    dev->name = (char*)malloc(dev->prefix_len + 6);
    sprintf(dev->name, "%s.0", name_prefix);
    mpr_dev_start_servers(dev);

    if (!dev->servers[SERVER_UDP] || !dev->servers[SERVER_TCP]) {
        mpr_dev_free((mpr_dev)dev);
        return NULL;
    }

    dev->ordinal_allocator.val = 1;
    dev->id_maps.active = (mpr_id_map*) malloc(sizeof(mpr_id_map));
    dev->id_maps.active[0] = 0;
    dev->num_sig_groups = 1;

    mpr_net_add_dev(mpr_graph_get_net(g), dev);
    return (mpr_dev)dev;
}

/*! Free resources used by a mpr device. */
void mpr_dev_free(mpr_dev dev)
{
    mpr_graph graph;
    mpr_net net;
    mpr_local_dev ldev;
    mpr_list list;
    int i, own_graph;
    RETURN_UNLESS(dev && dev->obj.is_local);
    if (!(graph = dev->obj.graph)) {
        free(dev);
        return;
    }
    ldev = (mpr_local_dev)dev;
    own_graph = ldev->own_graph;
    net = mpr_graph_get_net(graph);

    /* free any queued graph messages without sending */
    mpr_net_free_msgs(net);

    /* remove OSC handlers associated with this device */
    mpr_net_remove_dev(net, ldev);

    /* remove local graph handlers here so they are not called when child objects are freed */
    /* CHANGE: if graph is not owned then its callbacks _should_ be called when device is removed. */
    if (own_graph)
        mpr_graph_free_cbs(graph);

    /* remove subscribers */
    while (ldev->subscribers) {
        mpr_subscriber sub = ldev->subscribers;
        FUNC_IF(lo_address_free, sub->addr);
        ldev->subscribers = sub->next;
        free(sub);
    }

    /* free signals owned by this device */
    list = mpr_dev_get_sigs(dev, MPR_DIR_ANY);
    while (list) {
        mpr_local_sig sig = (mpr_local_sig)*list;
        list = mpr_list_get_next(list);
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
    process_outgoing_maps(ldev);
    list = mpr_dev_get_links(dev, MPR_DIR_UNDEFINED);
    while (list) {
        mpr_link link = (mpr_link)*list;
        list = mpr_list_get_next(list);
        mpr_graph_remove_link(graph, link, MPR_OBJ_REM);
    }

    /* Release device id maps */
    for (i = 0; i < ldev->num_sig_groups; i++) {
        while (ldev->id_maps.active[i]) {
            mpr_id_map id_map = ldev->id_maps.active[i];
            ldev->id_maps.active[i] = id_map->next;
            free(id_map);
        }
    }
    free(ldev->id_maps.active);

    while (ldev->id_maps.reserve) {
        mpr_id_map id_map = ldev->id_maps.reserve;
        ldev->id_maps.reserve = id_map->next;
        free(id_map);
    }

    FUNC_IF(lo_server_free, ldev->servers[SERVER_UDP]);
    FUNC_IF(lo_server_free, ldev->servers[SERVER_TCP]);

    mpr_graph_remove_dev(graph, dev, MPR_OBJ_REM);
    if (own_graph)
        mpr_graph_free(graph);
}

void mpr_dev_free_mem(mpr_dev dev)
{
    FUNC_IF(free, dev->linked);
    FUNC_IF(free, dev->name);
}

static void on_registered(mpr_local_dev dev)
{
    char *name;
    mpr_list qry;

    /* Add unique device id to locally-activated signal instances. */
    mpr_list sigs = mpr_dev_get_sigs((mpr_dev)dev, MPR_DIR_ANY);
    while (sigs) {
        mpr_local_sig sig = (mpr_local_sig)*sigs;
        sigs = mpr_list_get_next(sigs);
        mpr_local_sig_set_dev_id(sig, dev->obj.id);
    }
    qry = mpr_graph_new_query(dev->obj.graph, 0, MPR_SIG, (void*)cmp_qry_sigs,
                              "hi", dev->obj.id, MPR_DIR_ANY);
    mpr_tbl_add_record(dev->obj.props.synced, PROP(SIG), NULL, 1, MPR_LIST, qry,
                       MOD_NONE | PROP_OWNED);
    dev->registered = 1;
    dev->ordinal = dev->ordinal_allocator.val;

    snprintf(dev->name + dev->prefix_len + 1, dev->prefix_len + 6, "%d", dev->ordinal);
    name = strdup(dev->name);
    free(dev->name);
    dev->name = name;

    dev->status = MPR_STATUS_READY;

    mpr_dev_get_name((mpr_dev)dev);

    /* Check if we have any staged maps */
    mpr_graph_cleanup(dev->obj.graph);
}

int mpr_dev_get_is_registered(mpr_dev dev)
{
    return !dev->obj.is_local || ((mpr_local_dev)dev)->registered;
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

void mpr_local_dev_add_server_method(mpr_local_dev dev, const char *path,
                                     lo_method_handler h, void *data)
{
    lo_server_add_method(dev->servers[SERVER_UDP], path, NULL, h, data);
    lo_server_add_method(dev->servers[SERVER_TCP], path, NULL, h, data);
    ++dev->n_output_callbacks;
}

void mpr_local_dev_remove_server_method(mpr_local_dev dev, const char *path)
{
    lo_server_del_method(dev->servers[SERVER_UDP], path, NULL);
    lo_server_del_method(dev->servers[SERVER_TCP], path, NULL);
    --dev->n_output_callbacks;
}

void mpr_dev_remove_sig(mpr_dev dev, mpr_sig sig)
{
    mpr_dir dir = mpr_sig_get_dir(sig);
    if (dir & MPR_DIR_IN)
        --dev->num_inputs;
    if (dir & MPR_DIR_OUT)
        --dev->num_outputs;
}

mpr_list mpr_dev_get_sigs(mpr_dev dev, mpr_dir dir)
{
    RETURN_ARG_UNLESS(dev, 0);
    return mpr_graph_new_query(dev->obj.graph, 1, MPR_SIG, (void*)cmp_qry_sigs,
                               "hi", dev->obj.id, dir);
}

mpr_sig mpr_dev_get_sig_by_name(mpr_dev dev, const char *sig_name)
{
    mpr_list sigs;
    RETURN_ARG_UNLESS(dev && sig_name, 0);
    sigs = mpr_graph_get_list(dev->obj.graph, MPR_SIG);
    while (sigs) {
        mpr_sig sig = (mpr_sig)*sigs;
        if (   mpr_sig_get_dev(sig) == dev
            && strcmp(mpr_sig_get_name(sig), mpr_path_skip_slash(sig_name))==0)
            return sig;
        sigs = mpr_list_get_next(sigs);
    }
    return 0;
}

static int cmp_qry_maps(const void *context_data, mpr_map map)
{
    mpr_id dev_id = *(mpr_id*)context_data;
    mpr_dir dir = *(int*)((char*)context_data + sizeof(mpr_id));
    return mpr_map_get_has_dev(map, dev_id, dir);
}

mpr_list mpr_dev_get_maps(mpr_dev dev, mpr_dir dir)
{
    RETURN_ARG_UNLESS(dev, 0);
    return mpr_graph_new_query(dev->obj.graph, 1, MPR_MAP, (void*)cmp_qry_maps,
                               "hi", dev->obj.id, dir);
}

static int cmp_qry_links(const void *context_data, mpr_link link)
{
    mpr_id dev_id = *(mpr_id*)context_data;
    mpr_dir dir = *(int*)((char*)context_data + sizeof(mpr_id));
    mpr_dev dev = mpr_link_get_dev(link, 0);
    if (dev->obj.id == dev_id) {
        return MPR_DIR_UNDEFINED == dir ? 1 : mpr_link_get_has_maps(link, dir);
    }
    dev = mpr_link_get_dev(link, 1);
    if (dev->obj.id == dev_id) {
        switch (dir) {
            case MPR_DIR_ANY:
            case MPR_DIR_BOTH:  return mpr_link_get_has_maps(link, dir);
            case MPR_DIR_IN:    return mpr_link_get_has_maps(link, MPR_DIR_OUT);
            case MPR_DIR_OUT:   return mpr_link_get_has_maps(link, MPR_DIR_IN);
            default:            return 1;
        }
    }
    return 0;
}

mpr_list mpr_dev_get_links(mpr_dev dev, mpr_dir dir)
{
    RETURN_ARG_UNLESS(dev, 0);
    return mpr_graph_new_query(dev->obj.graph, 1, MPR_LINK, (void*)cmp_qry_links,
                               "hi", dev->obj.id, dir);
}

mpr_link mpr_dev_get_link_by_remote(mpr_dev dev, mpr_dev remote)
{
    mpr_list links;
    RETURN_ARG_UNLESS(dev, 0);
    links = mpr_graph_get_list(dev->obj.graph, MPR_LINK);
    while (links) {
        mpr_link link = (mpr_link)*links;
        if (mpr_link_get_dev(link, 0) == (mpr_dev)dev && mpr_link_get_dev(link, 1) == remote)
            return link;
        if (mpr_link_get_dev(link, 1) == (mpr_dev)dev && mpr_link_get_dev(link, 0) == remote)
            return link;
        links = mpr_list_get_next(links);
    }
    return 0;
}

/* TODO: handle interrupt-driven updates that omit call to this function */
static void process_incoming_maps(mpr_local_dev dev)
{
    mpr_graph graph;
    mpr_list maps;
    RETURN_UNLESS(dev->receiving);
    graph = dev->obj.graph;
    /* process and send updated maps */
    /* TODO: speed this up! */
    dev->receiving = 0;
    maps = mpr_graph_get_list(graph, MPR_MAP);
    while (maps) {
        mpr_local_map map = *(mpr_local_map*)maps;
        maps = mpr_list_get_next(maps);
        mpr_map_receive(map, dev->time);
    }
}

/* TODO: handle interrupt-driven updates that omit call to this function */
static int process_outgoing_maps(mpr_local_dev dev)
{
    int msgs = 0;
    mpr_list list;
    mpr_graph graph;
    RETURN_ARG_UNLESS(dev->sending, 0);

    graph = dev->obj.graph;
    /* process and send updated maps */
    /* TODO: speed this up! */
    list = mpr_graph_get_list(graph, MPR_MAP);
    while (list) {
        mpr_local_map map = *(mpr_local_map*)list;
        list = mpr_list_get_next(list);
        if (mpr_obj_get_is_local((mpr_obj)map))
            mpr_map_send(map, dev->time);
    }
    dev->sending = 0;
    list = mpr_graph_get_list(graph, MPR_LINK);
    while (list) {
        msgs += mpr_link_process_bundles((mpr_link)*list, dev->time);
        list = mpr_list_get_next(list);
    }
    return msgs ? 1 : 0;
}

void mpr_dev_update_maps(mpr_dev dev) {
    RETURN_UNLESS(dev && dev->obj.is_local);
    ((mpr_local_dev)dev)->time_is_stale = 1;
    if (!((mpr_local_dev)dev)->polling)
        process_outgoing_maps((mpr_local_dev)dev);
}

int mpr_dev_poll(mpr_dev dev, int block_ms)
{
    int admin_count = 0, device_count = 0, status[4];
    mpr_local_dev ldev = (mpr_local_dev)dev;
    mpr_net net;
    double then;

    RETURN_ARG_UNLESS(dev && dev->obj.is_local, 0);

    then = mpr_get_current_time();
    net = mpr_graph_get_net(dev->obj.graph);
    mpr_net_poll(net, !ldev->registered && check_registration(ldev));

    if (ldev->registered) {
        ldev->polling = 1;
        ldev->time_is_stale = 1;
        mpr_dev_get_time(dev);
        process_outgoing_maps(ldev);
        ldev->polling = 0;
    }

    if (!block_ms) {
        if (ldev->registered) {
            if (lo_servers_recv_noblock(ldev->servers, status, 4, 0)) {
                admin_count = (status[0] > 0) + (status[1] > 0);
                device_count = (status[2] > 0) + (status[3] > 0);
            }
        }
        else {
            if (lo_servers_recv_noblock(ldev->servers + 2, status, 2, 0)) {
                admin_count = (status[0] > 0) + (status[1] > 0);
            }
            return admin_count;
        }
    }
    else {
        int left_ms = block_ms, elapsed_ms, admin_elapsed_ms = 0;
        while (left_ms > 0) {
            /* set timeout to a maximum of 100ms */
            if (left_ms > 100)
                left_ms = 100;
            if (ldev->registered) {
                ldev->polling = 1;
                if (lo_servers_recv_noblock(ldev->servers, status, 4, left_ms)) {
                    admin_count += (status[0] > 0) + (status[1] > 0);
                    device_count += (status[2] > 0) + (status[3] > 0);
                }

                /* check if any signal update bundles need to be sent */
                process_incoming_maps(ldev);
                process_outgoing_maps(ldev);
                ldev->polling = 0;
            }
            else {
                if (lo_servers_recv_noblock(ldev->servers + 2, status, 2, left_ms)) {
                    admin_count = (status[0] > 0) + (status[1] > 0);
                }
            }

            /* Only run mpr_net_poll() again if more than 100ms have elapsed. */
            elapsed_ms = (mpr_get_current_time() - then) * 1000;
            if ((elapsed_ms - admin_elapsed_ms) > 100) {
                mpr_net_poll(net, 0);
                admin_elapsed_ms = elapsed_ms;
            }

            left_ms = block_ms - elapsed_ms;
        }
    }

    if (!ldev->registered)
        return admin_count;

    /* When done, or if non-blocking, check for remaining messages up to a
     * proportion of the number of input signals. Arbitrarily choosing 1 for
     * now, but perhaps could be a heuristic based on a recent number of
     * messages per channel per poll. */
    while (device_count < (dev->num_inputs + ldev->n_output_callbacks)*1
           && (lo_servers_recv_noblock(ldev->servers, &status[2], 2, 0)))
        device_count += (status[2] > 0) + (status[3] > 0);

    /* process incoming maps */
    ldev->polling = 1;
    process_incoming_maps(ldev);
    ldev->polling = 0;

    if (mpr_tbl_get_is_dirty(dev->obj.props.synced) && mpr_dev_get_is_ready(dev) && ldev->subscribers) {
        /* inform device subscribers of changed properties */
        mpr_net_use_subscribers(net, ldev, MPR_DEV);
        mpr_dev_send_state(dev, MSG_DEV);
    }

    ldev->time_is_stale = 1;
    return admin_count + device_count;
}

#ifdef HAVE_LIBPTHREAD
static void *device_thread_func(void *data)
{
    mpr_thread_data td = (mpr_thread_data)data;
    while (td->is_active) {
        mpr_dev_poll((mpr_dev)td->object, 100);
    }
    td->is_done = 1;
    pthread_exit(NULL);
    return 0;
}
#endif

#ifdef HAVE_WIN32_THREADS
static unsigned __stdcall device_thread_func(void *data)
{
    mpr_thread_data td = (mpr_thread_data)data;
    while (td->is_active) {
        mpr_dev_poll((mpr_dev)td->object, 100);
    }
    td->is_done = 1;
    _endthread();
    return 0;
}
#endif

int mpr_dev_start_polling(mpr_dev dev)
{
    mpr_thread_data td;
    int result = 0;
    RETURN_ARG_UNLESS(dev && dev->obj.is_local, 0);
    if (((mpr_local_dev)dev)->thread_data)
        return 0;

    td = (mpr_thread_data)malloc(sizeof(mpr_thread_data_t));
    td->object = (mpr_obj)dev;
    td->is_active = 1;


#ifdef HAVE_LIBPTHREAD
    result = -pthread_create(&(td->thread), 0, device_thread_func, td);
#else
#ifdef HAVE_WIN32_THREADS
    if (!(td->thread = (HANDLE)_beginthreadex(NULL, 0, &device_thread_func, td, 0, NULL)))
        result = -1;
#else
    printf("error: threading is not available.\n");
#endif /* HAVE_WIN32_THREADS */
#endif /* HAVE_LIBPTHREAD */

    if (result) {
        printf("Device error: couldn't create thread.\n");
        free(td);
    }
    else {
        ((mpr_local_dev)dev)->thread_data = td;
    }
    return result;
}

int mpr_dev_stop_polling(mpr_dev dev)
{
    mpr_thread_data td;
    int result = 0;
    RETURN_ARG_UNLESS(dev && dev->obj.is_local, 0);
    td = ((mpr_local_dev)dev)->thread_data;
    if (!td || !td->is_active)
        return 0;
    td->is_active = 0;

#ifdef HAVE_LIBPTHREAD
    result = pthread_join(td->thread, NULL);
    if (result) {
        printf("Device error: failed to stop thread (pthread_join).\n");
        return -result;
    }
#else
#ifdef HAVE_WIN32_THREADS
    result = WaitForSingleObject(td->thread, INFINITE);
    CloseHandle(td->thread);
    td->thread = NULL;

    if (0 != result) {
        printf("Device error: failed to join thread (WaitForSingleObject).\n");
        return -1;
    }
#else
    printf("error: threading is not available.\n");
#endif /* HAVE_WIN32_THREADS */
#endif /* HAVE_LIBPTHREAD */

    free(((mpr_local_dev)dev)->thread_data);
    ((mpr_local_dev)dev)->thread_data = 0;
    return result;
}

mpr_time mpr_dev_get_time(mpr_dev dev)
{
    RETURN_ARG_UNLESS(dev && dev->obj.is_local, MPR_NOW);
    if (((mpr_local_dev)dev)->time_is_stale)
        mpr_dev_set_time(dev, MPR_NOW);
    return ((mpr_local_dev)dev)->time;
}

void mpr_dev_set_time(mpr_dev dev, mpr_time time)
{
    mpr_local_dev ldev = (mpr_local_dev)dev;
    RETURN_UNLESS(dev && dev->obj.is_local && memcmp(&time, &(ldev)->time, sizeof(mpr_time)));
    mpr_time_set(&ldev->time, time);
    ldev->time_is_stale = 0;
    if (!ldev->polling)
        process_outgoing_maps(ldev);
}

void mpr_dev_reserve_id_map(mpr_local_dev dev)
{
    mpr_id_map id_map = (mpr_id_map)calloc(1, sizeof(mpr_id_map_t));
    id_map->next = dev->id_maps.reserve;
    dev->id_maps.reserve = id_map;
}

int mpr_local_dev_get_num_id_maps(mpr_local_dev dev, int active)
{
    int count = 0;
    mpr_id_map *id_map = active ? &(dev)->id_maps.active[0] : &(dev)->id_maps.reserve;
    while (*id_map) {
        ++count;
        id_map = &(*id_map)->next;
    }
    return count;
}

#ifdef DEBUG
void mpr_local_dev_print_id_maps(mpr_local_dev dev)
{
    printf("ID MAPS for %s:\n", dev->name);
    mpr_id_map *id_maps = &dev->id_maps.active[0];
    while (*id_maps) {
        mpr_id_map id_map = *id_maps;
        printf("  %p: %"PR_MPR_ID" (%d) -> %"PR_MPR_ID" (%d)\n", id_map, id_map->LID,
               id_map->LID_refcount, id_map->GID, id_map->GID_refcount);
        id_maps = &(*id_maps)->next;
    }
}
#endif

mpr_id_map mpr_dev_add_id_map(mpr_local_dev dev, int group, mpr_id LID, mpr_id GID)
{
    mpr_id_map id_map;
    if (!dev->id_maps.reserve)
        mpr_dev_reserve_id_map(dev);
    id_map = dev->id_maps.reserve;
    id_map->LID = LID;
    id_map->GID = GID ? GID : mpr_dev_generate_unique_id((mpr_dev)dev);
    trace_dev(dev, "mpr_dev_add_id_map(%s) %"PR_MPR_ID" -> %"PR_MPR_ID"\n", dev->name, LID,
              id_map->GID);
    id_map->LID_refcount = 1;
    id_map->GID_refcount = 0;
    dev->id_maps.reserve = id_map->next;
    id_map->next = dev->id_maps.active[group];
    dev->id_maps.active[group] = id_map;
#ifdef DEBUG
    mpr_local_dev_print_id_maps(dev);
#endif
    return id_map;
}

static void mpr_dev_remove_id_map(mpr_local_dev dev, int group, mpr_id_map rem)
{
    mpr_id_map *map = &dev->id_maps.active[group];
    trace_dev(dev, "mpr_dev_remove_id_map(%s) %"PR_MPR_ID" -> %"PR_MPR_ID"\n",
              dev->name, rem->LID, rem->GID);
    while (*map) {
        if ((*map) == rem) {
            *map = (*map)->next;
            rem->next = dev->id_maps.reserve;
            dev->id_maps.reserve = rem;
            break;
        }
        map = &(*map)->next;
    }
#ifdef DEBUG
    mpr_local_dev_print_id_maps(dev);
#endif
}

int mpr_dev_LID_decref(mpr_local_dev dev, int group, mpr_id_map id_map)
{
    trace_dev(dev, "mpr_dev_LID_decref(%s) %"PR_MPR_ID" -> %"PR_MPR_ID"\n",
              dev->name, id_map->LID, id_map->GID);
    --id_map->LID_refcount;
    trace_dev(dev, "  refcounts: {LID:%d, GID:%d}\n", id_map->LID_refcount, id_map->GID_refcount);
    if (id_map->LID_refcount <= 0) {
        id_map->LID_refcount = 0;
        if (id_map->GID_refcount <= 0) {
            mpr_dev_remove_id_map(dev, group, id_map);
            return 1;
        }
    }
    return 0;
}

int mpr_dev_GID_decref(mpr_local_dev dev, int group, mpr_id_map id_map)
{
    trace_dev(dev, "mpr_dev_GID_decref(%s) %"PR_MPR_ID" -> %"PR_MPR_ID"\n",
              dev->name, id_map->LID, id_map->GID);
    --id_map->GID_refcount;
    trace_dev(dev, "  refcounts: {LID:%d, GID:%d}\n", id_map->LID_refcount, id_map->GID_refcount);
    if (id_map->GID_refcount <= 0) {
        id_map->GID_refcount = 0;
        if (id_map->LID_refcount <= 0) {
            mpr_dev_remove_id_map(dev, group, id_map);
            return 1;
        }
    }
    return 0;
}

mpr_id_map mpr_dev_get_id_map_by_LID(mpr_local_dev dev, int group, mpr_id LID)
{
    mpr_id_map id_map = dev->id_maps.active[group];
    while (id_map) {
        if (id_map->LID == LID)
            return id_map;
        id_map = id_map->next;
    }
    return 0;
}

mpr_id_map mpr_dev_get_id_map_by_GID(mpr_local_dev dev, int group, mpr_id GID)
{
    mpr_id_map id_map = dev->id_maps.active[group];
    while (id_map) {
        if (id_map->GID == GID)
            return id_map;
        id_map = id_map->next;
    }
    return 0;
}

/* Internal LibLo error handler */
static void handler_error(int num, const char *msg, const char *where)
{
    trace("[libmapper] liblo server error %d in path %s: %s\n", num, where, msg);
}

void mpr_dev_set_net_servers(mpr_local_dev dev, lo_server *servers)
{
    memcpy(dev->servers + 2, servers, sizeof(lo_server) * 2);
}

static void mpr_dev_start_servers(mpr_local_dev dev)
{
    int portnum;
    mpr_net net = mpr_graph_get_net(dev->obj.graph);
    char port[16], *pport = 0, *url, *host;
    if (!dev->servers[SERVER_UDP] && !dev->servers[SERVER_TCP]) {
        while (!(dev->servers[SERVER_UDP] = lo_server_new(pport, handler_error)))
            pport = 0;
        snprintf(port, 16, "%d", lo_server_get_port(dev->servers[SERVER_UDP]));
        pport = port;
        while (!(dev->servers[SERVER_TCP] = lo_server_new_with_proto(pport, LO_TCP, handler_error)))
            pport = 0;

        /* Disable liblo message queueing */
        lo_server_enable_queue(dev->servers[SERVER_UDP], 0, 1);
        lo_server_enable_queue(dev->servers[SERVER_TCP], 0, 1);

        /* Add bundle handlers */
        lo_server_add_bundle_handlers(dev->servers[SERVER_UDP], mpr_net_bundle_start,
                                      NULL, (void*)net);
        lo_server_add_bundle_handlers(dev->servers[SERVER_TCP], mpr_net_bundle_start,
                                      NULL, (void*)net);
    }

    portnum = lo_server_get_port(dev->servers[SERVER_UDP]);
    mpr_tbl_add_record(dev->obj.props.synced, PROP(PORT), NULL, 1, MPR_INT32, &portnum, MOD_NONE);

    trace_dev(dev, "bound to UDP port %i\n", portnum);
    trace_dev(dev, "bound to TCP port %i\n", lo_server_get_port(dev->servers[SERVER_TCP]));

    url = lo_server_get_url(dev->servers[SERVER_UDP]);
    host = lo_url_get_hostname(url);
    mpr_tbl_add_record(dev->obj.props.synced, PROP(HOST), NULL, 1, MPR_STR, host, MOD_NONE);

#ifndef WIN32
    /* For some reason Windows thinks return of lo_address_get_url() should not be freed */
    free(url);
#endif /* WIN32 */

    mpr_dev_set_net_servers(dev, mpr_net_get_servers(net));
}

/*! Probe the network to see if a device's proposed name.ordinal is available. */
void mpr_local_dev_probe_name(mpr_local_dev dev, mpr_net net)
{
    int i;

    /* reset collisions and hints */
    dev->ordinal_allocator.collision_count = 0;
    dev->ordinal_allocator.count_time = mpr_get_current_time();
    for (i = 0; i < 8; i++)
        dev->ordinal_allocator.hints[i] = 0;

    snprintf(dev->name + dev->prefix_len + 1, dev->prefix_len + 6, "%d", dev->ordinal_allocator.val);
    trace_dev(dev, "probing name '%s'\n", dev->name);

    /* Calculate an id from the name and store it in id.val */
    dev->obj.id = mpr_id_from_str(dev->name);

    mpr_net_send_name_probe(net, dev->name);
}

/* Extract the ordinal from a device name in the format: <name>.<ordinal> */
static int extract_ordinal(char *name) {
    int ordinal;
    char *s = name;
    RETURN_ARG_UNLESS(s = strrchr(s, '.'), -1);
    ordinal = atoi(s+1);
    *s = 0;
    return ordinal;
}

void mpr_local_dev_handler_name(mpr_local_dev dev, const char *name,
                                int temp_id, int random_id, int hint)
{
    mpr_net net = mpr_graph_get_net(dev->obj.graph);
    int ordinal, diff;

#ifdef DEBUG
    if (hint)
        { trace_dev(dev, "received name %s %i %i\n", name, temp_id, hint); }
    else
        { trace_dev(dev, "received name %s\n", name); }
#endif

    if (dev->ordinal_allocator.locked) {
        /* extract_ordinal function replaces '.' with NULL */
        ordinal = extract_ordinal((char*)name);
        RETURN_UNLESS(ordinal >= 0);

        /* If device name matches */
        if (strlen(name) == dev->prefix_len && 0 == strncmp(name, dev->name, dev->prefix_len)) {
            /* if id is locked and registered id is within my block, store it */
            diff = ordinal - dev->ordinal_allocator.val - 1;
            if (diff >= 0 && diff < 8)
                dev->ordinal_allocator.hints[diff] = -1;
            if (hint) {
                /* if suggested id is within my block, store timestamp */
                diff = hint - dev->ordinal_allocator.val - 1;
                if (diff >= 0 && diff < 8)
                    dev->ordinal_allocator.hints[diff] = mpr_get_current_time();
            }
        }
    }
    else {
        mpr_id id = mpr_id_from_str(name);
        if (id == dev->obj.id) {
            if (temp_id < random_id) {
                /* Count ordinal collisions. */
                ++dev->ordinal_allocator.collision_count;
                dev->ordinal_allocator.count_time = mpr_get_current_time();
            }
            else if (temp_id == random_id && hint > 0 && hint != dev->ordinal_allocator.val) {
                dev->ordinal_allocator.val = hint;
                mpr_local_dev_probe_name(dev, net);
            }
        }
    }
}

static void send_name_registered(mpr_net net, const char *name, int id, int hint)
{
    NEW_LO_MSG(msg, return);
    mpr_net_use_bus(net);
    lo_message_add_string(msg, name);
    if (id >= 0) {
        lo_message_add_int32(msg, id);
        lo_message_add_int32(msg, hint);
    }
    mpr_net_add_msg(net, NULL, MSG_NAME_REG, msg);
}

void mpr_local_dev_handler_name_probe(mpr_local_dev dev, char *name, int temp_id,
                                     int random_id, mpr_id id)
{
    int i;
    double current_time;
    if (id != dev->obj.id)
        return;

    trace_dev(dev, "name probe match %s %i \n", name, temp_id);
    current_time = mpr_get_current_time();
    if (dev->ordinal_allocator.locked || temp_id > random_id) {
        mpr_net net = mpr_graph_get_net(dev->obj.graph);
        for (i = 0; i < 8; i++) {
            if (   dev->ordinal_allocator.hints[i] >= 0
                && (current_time - dev->ordinal_allocator.hints[i]) > 2.0) {
                /* reserve suggested ordinal */
                dev->ordinal_allocator.hints[i] = current_time;
                break;
            }
        }
        /* Send /registered message with an ordinal hint */
        send_name_registered(net, name, temp_id, dev->ordinal_allocator.val + i + 1);
    }
    else {
        dev->ordinal_allocator.collision_count += 1;
        dev->ordinal_allocator.count_time = current_time;
        if (temp_id == random_id)
            dev->ordinal_allocator.online = 1;
    }
}

const char *mpr_dev_get_name(mpr_dev dev)
{
    return dev->name;
}

int mpr_dev_get_is_ready(mpr_dev dev)
{
    return dev ? dev->status >= MPR_STATUS_READY : 0;
}

mpr_id mpr_dev_generate_unique_id(mpr_dev dev)
{
    mpr_id id;
    RETURN_ARG_UNLESS(dev, 0);
    id = mpr_graph_generate_unique_id(dev->obj.graph);
    if (dev->obj.is_local && ((mpr_local_dev)dev)->registered)
        id |= dev->obj.id;
    return id;
}

void mpr_dev_send_state(mpr_dev dev, net_msg_t cmd)
{
    mpr_net net = mpr_graph_get_net(dev->obj.graph);
    NEW_LO_MSG(msg, return);

    /* device name */
    lo_message_add_string(msg, mpr_dev_get_name((mpr_dev)dev));

    /* properties */
    mpr_obj_add_props_to_msg((mpr_obj)dev, msg);

    if (cmd == MSG_DEV_MOD) {
        char str[1024];
        snprintf(str, 1024, "/%s/modify", dev->name);
        mpr_net_add_msg(net, str, 0, msg);
        mpr_net_send(net);
    }
    else
        mpr_net_add_msg(net, 0, cmd, msg);

    mpr_tbl_set_is_dirty(dev->obj.props.synced, 0);
}

int mpr_dev_add_link(mpr_dev dev1, mpr_dev dev2)
{
    int i, found = 0;
    for (i = 0; i < dev1->num_linked; i++) {
        if (dev1->linked[i] && dev1->linked[i]->obj.id == dev2->obj.id) {
            found = 0x01;
            break;
        }
    }
    if (!found) {
        i = ++dev1->num_linked;
        dev1->linked = realloc(dev1->linked, i * sizeof(mpr_dev));
        dev1->linked[i-1] = dev2;
    }

    for (i = 0; i < dev2->num_linked; i++) {
        if (dev2->linked[i] && dev2->linked[i]->obj.id == dev1->obj.id) {
            found |= 0x10;
            break;
        }
    }
    if (!(found & 0x10)) {
        i = ++dev2->num_linked;
        dev2->linked = realloc(dev2->linked, i * sizeof(mpr_dev));
        dev2->linked[i-1] = dev1;
    }
    return !found;
}

void mpr_dev_remove_link(mpr_dev dev1, mpr_dev dev2)
{
    int i, j;
    for (i = 0; i < dev1->num_linked; i++) {
        if (!dev1->linked[i] || dev1->linked[i]->obj.id != dev2->obj.id)
            continue;
        for (j = i+1; j < dev1->num_linked; j++)
            dev1->linked[j-1] = dev1->linked[j];
        --dev1->num_linked;
        dev1->linked = realloc(dev1->linked, dev1->num_linked * sizeof(mpr_dev));
        mpr_tbl_set_is_dirty(dev1->obj.props.synced, 1);
        break;
    }
    for (i = 0; i < dev2->num_linked; i++) {
        if (!dev2->linked[i] || dev2->linked[i]->obj.id != dev1->obj.id)
            continue;
        for (j = i+1; j < dev2->num_linked; j++)
            dev2->linked[j-1] = dev2->linked[j];
        --dev2->num_linked;
        dev2->linked = realloc(dev2->linked, dev2->num_linked * sizeof(mpr_dev));
        mpr_tbl_set_is_dirty(dev2->obj.props.synced, 1);
        break;
    }
}

static int mpr_dev_update_linked(mpr_dev dev, mpr_msg_atom a)
{
    int i, j, updated = 0, num = mpr_msg_atom_get_len(a);
    lo_arg **link_list = mpr_msg_atom_get_values(a);
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
            if ((rem = mpr_graph_add_dev(dev->obj.graph, &link_list[i]->s, 0, 1)))
                updated += mpr_dev_add_link(dev, rem);
        }
    }
    return updated;
}

/*! Update information about a device record based on message properties. */
int mpr_dev_set_from_msg(mpr_dev dev, mpr_msg m)
{
    int i, num, updated = 0;
    RETURN_ARG_UNLESS(m, 0);
    num = mpr_msg_get_num_atoms(m);
    for (i = 0; i < num; i++) {
        mpr_msg_atom a = mpr_msg_get_atom(m, i);
        switch (MASK_PROP_BITFLAGS(mpr_msg_atom_get_prop(a))) {
            case PROP(LINKED): {
                if (!dev->obj.is_local)
                    updated += mpr_dev_update_linked(dev, a);
                break;
            }
            default:
                updated += mpr_tbl_add_record_from_msg_atom(dev->obj.props.synced, a, MOD_REMOTE);
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

static int mpr_dev_send_maps(mpr_local_dev dev, mpr_dir dir, int msg)
{
    mpr_list maps = mpr_dev_get_maps((mpr_dev)dev, dir);
    while (maps) {
        mpr_map_send_state((mpr_map)*maps, -1, msg);
        maps = mpr_list_get_next(maps);
    }
    return 0;
}

int mpr_dev_get_is_subscribed(mpr_dev dev)
{
    return dev->subscribed != 0;
}

void mpr_dev_set_is_subscribed(mpr_dev dev, int subscribed)
{
    dev->subscribed = (subscribed != 0);
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

    if (timeout_sec >= 0) {
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
                    trace_dev(dev, "renewing subscription from %s:%s for %d seconds with flags ",
                              s_ip, s_port, timeout_sec);
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
    }

    RETURN_UNLESS(flags);

    if (!(*s) && timeout_sec > 0) {
        /* add new subscriber */
#ifdef DEBUG
        trace_dev(dev, "adding new subscription from %s:%s with flags ", ip, port);
        print_subscription_flags(flags);
#endif
        mpr_subscriber sub = malloc(sizeof(mpr_subscriber_t));
        sub->addr = lo_address_new(ip, port);
        sub->lease_exp = t.sec + timeout_sec;
        sub->flags = flags;
        sub->next = dev->subscribers;
        dev->subscribers = sub;
    }

    /* bring new subscriber up to date */
    net = mpr_graph_get_net(dev->obj.graph);
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
        mpr_dev_send_maps(dev, dir, MSG_MAPPED);
        mpr_net_send(net);
    }
}

int mpr_dev_check_synced(mpr_dev dev, mpr_time time)
{
    return !dev->synced.sec || (dev->synced.sec > time.sec);
}

void mpr_dev_set_synced(mpr_dev dev, mpr_time time)
{
    mpr_time_set(&dev->synced, time);
}

int mpr_dev_has_local_link(mpr_dev dev)
{
    int i;
    for (i = 0; i < dev->num_linked; i++) {
        if (dev->linked[i] && dev->linked[i]->obj.is_local)
            return 1;
    }
    return 0;
}

lo_server mpr_local_dev_get_server(mpr_local_dev dev, dev_server_t idx)
{
    return dev->servers[idx];
}

void mpr_local_dev_set_sending(mpr_local_dev dev)
{
    dev->sending = 1;
}

void mpr_local_dev_set_receiving(mpr_local_dev dev)
{
    dev->receiving = 1;
}

int mpr_local_dev_has_subscribers(mpr_local_dev dev)
{
    return dev->subscribers != 0;
}

void mpr_local_dev_send_to_subscribers(mpr_local_dev dev, lo_bundle bundle,
                                       int msg_type, lo_server from)
{
    mpr_subscriber *sub = &dev->subscribers;
    mpr_time t;
    if (*sub)
        mpr_time_set(&t, MPR_NOW);
    while (*sub) {
        if ((*sub)->lease_exp < t.sec || !(*sub)->flags) {
            /* subscription expired, remove from subscriber list */
#ifdef DEBUG
            char *addr = lo_address_get_url((*sub)->addr);
            trace_dev(dev, "removing expired subscription from %s\n", addr);
#ifndef WIN32
            /* For some reason Windows thinks return of lo_address_get_url() should not be freed */
            free(addr);
#endif /* WIN32 */
#endif /* DEBUG */
            mpr_subscriber temp = *sub;
            *sub = temp->next;
            FUNC_IF(lo_address_free, temp->addr);
            free(temp);
            continue;
        }
        if ((*sub)->flags & msg_type)
            lo_send_bundle_from((*sub)->addr, from, bundle);
        sub = &(*sub)->next;
    }
}

void mpr_local_dev_restart_registration(mpr_local_dev dev, int start_ordinal)
{
    dev->registered = 0;
    dev->ordinal_allocator.val = start_ordinal;
}

/*! Algorithm for checking collisions and allocating resources. */
static int check_collisions(mpr_net net, mpr_allocated resource)
{
    int i;
    double current_time, timediff;
    RETURN_ARG_UNLESS(!resource->locked, 0);
    current_time = mpr_get_current_time();
    timediff = current_time - resource->count_time;

    if (!resource->online) {
        if (timediff >= 5.0) {
            /* reprobe with the same value */
            resource->count_time = current_time;
            return 1;
        }
        return 0;
    }
    else if (timediff >= 2.0 && resource->collision_count < 2) {
        resource->locked = 1;
        return 2;
    }
    else if (timediff >= 0.5 && resource->collision_count > 1) {
        for (i = 0; i < 8; i++) {
            if (!resource->hints[i])
                break;
        }
        resource->val += i + (rand() % mpr_net_get_num_devs(net));

        /* Prepare for causing new resource collisions. */
        resource->collision_count = 0;
        resource->count_time = current_time;
        for (i = 0; i < 8; i++)
            resource->hints[i] = 0;

        /* Indicate that we need to re-probe the new value. */
        return 1;
    }
    return 0;
}

static int check_registration(mpr_local_dev dev)
{
    mpr_net net = mpr_graph_get_net(dev->obj.graph);
    if (dev->registered)
        return 1;

    /* If the ordinal has changed, re-probe the new name. */
    if (1 == check_collisions(net, &dev->ordinal_allocator))
        mpr_local_dev_probe_name(dev, net);
    else if (dev->ordinal_allocator.locked) {
        /* If we are ready to register the device, add the message handlers. */
        on_registered(dev);

        /* Send registered msg. */
        send_name_registered(net, dev->name, -1, 0);

        mpr_net_add_dev_methods(net, dev);
        trace_dev(dev, "registered.\n");

        /* Send out any cached maps. */
        mpr_net_use_bus(net);
        mpr_dev_send_maps(dev, MPR_DIR_ANY, MSG_MAP);
        mpr_net_send(net);
        return 1;
    }
    return 0;
}

void mpr_local_dev_handler_logout(mpr_local_dev dev, mpr_dev remote, const char *prefix_str,
                                 int ordinal)
{
    mpr_link link;
    if (!dev->ordinal_allocator.locked)
        return;
    /* Check if we have any links to this device, if so remove them */
    if (remote && (link = mpr_dev_get_link_by_remote((mpr_dev)dev, remote))) {
        /* TODO: release maps, call local handlers and inform subscribers */
        trace_dev(dev, "removing link to removed device '%s'.\n", mpr_dev_get_name(remote));
        mpr_graph_remove_link(dev->obj.graph, link, MPR_OBJ_REM);
    }
    if (0 == strncmp(prefix_str, dev->name, dev->prefix_len)) {
        /* If device name matches and ordinal is within my block, free it */
        int diff = ordinal - dev->ordinal_allocator.val - 1;
        if (diff >= 0 && diff < 8)
            dev->ordinal_allocator.hints[diff] = 0;
    }
}

void mpr_local_dev_add_sig(mpr_local_dev dev, mpr_local_sig sig, mpr_dir dir)
{
    /* TODO: use & instead? */
    if (dir == MPR_DIR_IN)
        ++dev->num_inputs;
    else
        ++dev->num_outputs;

    mpr_obj_increment_version((mpr_obj)dev);
    if (dev->registered) {
        /* Notify subscribers */
        mpr_net_use_subscribers(mpr_graph_get_net(dev->obj.graph), dev,
                                ((dir == MPR_DIR_IN) ? MPR_SIG_IN : MPR_SIG_OUT));
        mpr_sig_send_state((mpr_sig)sig, MSG_SIG);
    }
}
