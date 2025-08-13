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

#include "util/mpr_debug.h"

#include "config.h"
#include <mapper/mapper.h>

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

    mpr_allocated_t ordinal_allocator;  /*!< A unique ordinal for this device instance. */
    int registered;                     /*!< Non-zero if this device has been registered. */

    mpr_subscriber subscribers;         /*!< Linked-list of subscribed peers. */

    struct {
        struct _mpr_id_map **active;    /*!< The list of active instance id maps. */
        struct _mpr_id_map *reserve;    /*!< The list of reserve instance id maps. */
    } id_maps;

    mpr_time time;
    int num_sig_groups;
    uint8_t time_is_stale;
    uint8_t polling;
    uint8_t sending;
    uint8_t receiving;
    uint8_t own_graph;
} mpr_local_dev_t;

/* prototypes */
static int process_outgoing_maps(mpr_local_dev dev);
static int check_registration(mpr_local_dev dev);

mpr_time ts = {0,1};

size_t mpr_dev_get_struct_size(int is_local)
{
    return is_local ? sizeof(mpr_local_dev_t) : sizeof(mpr_dev_t);
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
    mpr_tbl tbl;
    mpr_list qry;

    dev->obj.is_local = is_local;
    dev->obj.status = 0;
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
#define link(PROP, TYPE, DATA, FLAGS) \
    mpr_tbl_link_value(tbl, MPR_PROP_##PROP, 1, TYPE, DATA, FLAGS | PROP_SET);
    link(DATA,         MPR_PTR,   &dev->obj.data,     MOD_LOCAL | INDIRECT | LOCAL_ACCESS);
    link(ID,           MPR_INT64, &dev->obj.id,       MOD_NONE);
    qry = mpr_graph_new_query(dev->obj.graph, 0, MPR_DEV, (void*)cmp_qry_linked, "v", &dev);
    link(LINKED,       MPR_LIST,  qry,                MOD_NONE | PROP_OWNED);
    link(NAME,         MPR_STR,   &dev->name,         MOD_NONE | INDIRECT | LOCAL_ACCESS);
    link(NUM_MAPS_IN,  MPR_INT32, &dev->num_maps_in,  MOD_NONE);
    link(NUM_MAPS_OUT, MPR_INT32, &dev->num_maps_out, MOD_NONE);
    link(NUM_SIGS_IN,  MPR_INT32, &dev->num_inputs,   MOD_NONE);
    link(NUM_SIGS_OUT, MPR_INT32, &dev->num_outputs,  MOD_NONE);
    link(ORDINAL,      MPR_INT32, &dev->ordinal,      MOD_NONE);
    if (!is_local) {
        qry = mpr_graph_new_query(dev->obj.graph, 0, MPR_SIG, (void*)cmp_qry_sigs,
                                  "hi", dev->obj.id, MPR_DIR_ANY);
        link(SIG,      MPR_LIST,  qry,                MOD_NONE | PROP_OWNED);
    }
    link(STATUS,       MPR_INT32, &dev->obj.status,   MOD_NONE | LOCAL_ACCESS);
    link(SYNCED,       MPR_TIME,  &dev->synced,       MOD_NONE | LOCAL_ACCESS);
    link(VERSION,      MPR_INT32, &dev->obj.version,  MOD_NONE);
#undef link

    if (is_local)
        mpr_tbl_add_record(tbl, MPR_PROP_LIBVER, NULL, 1, MPR_STR, PACKAGE_VERSION, MOD_NONE);
    mpr_tbl_add_record(tbl, MPR_PROP_IS_LOCAL, NULL, 1, MPR_BOOL, &is_local, LOCAL_ACCESS | MOD_NONE);
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

    dev = (mpr_local_dev)mpr_graph_add_obj(g, MPR_DEV, 1);
    mpr_dev_init((mpr_dev)dev, 1, NULL, 0);

    dev->own_graph = graph ? 0 : 1;
    dev->prefix_len = strlen(name_prefix);
    dev->name = (char*)malloc(dev->prefix_len + 6);
    sprintf(dev->name, "%s.0", name_prefix);

    dev->ordinal_allocator.val = 1;
    dev->ordinal_allocator.count_time = mpr_get_current_time();
    dev->id_maps.active = (mpr_id_map*) malloc(sizeof(mpr_id_map));
    dev->id_maps.active[0] = 0;
    dev->num_sig_groups = 1;

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

    /* remove local graph handlers here so they are not called when child objects are freed */
    /* CHANGE: if graph is not owned then its callbacks _should_ be called when device is removed. */
    if (own_graph) {
        /* free any queued graph messages without sending */
        mpr_net_free_msgs(net);

        mpr_graph_free_cbs(graph);
    }

    /* remove OSC handlers associated with this device */
    mpr_net_remove_dev(net, ldev);

    /* remove subscribers */
    while (ldev->subscribers) {
        mpr_subscriber sub = ldev->subscribers;
        FUNC_IF(lo_address_free, sub->addr);
        ldev->subscribers = sub->next;
        free(sub);
    }

    process_outgoing_maps(ldev);

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
    list = mpr_dev_get_links(dev, MPR_DIR_UNDEFINED);
    while (list) {
        mpr_link link = (mpr_link)*list;
        list = mpr_list_get_next(list);
        mpr_graph_remove_link(graph, link, MPR_STATUS_REMOVED);
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

    dev->obj.status |= MPR_STATUS_REMOVED;
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
    mpr_net net = mpr_graph_get_net(dev->obj.graph);
    mpr_list qry;

    /* Add unique device id to locally-activated signal instances. */
    mpr_list sigs = mpr_dev_get_sigs((mpr_dev)dev, MPR_DIR_ANY);
    while (sigs) {
        mpr_local_sig sig = (mpr_local_sig)*sigs;
        sigs = mpr_list_get_next(sigs);
        mpr_local_sig_set_dev_id(sig, dev->obj.id);
        mpr_local_sig_add_to_net(sig, net);
    }
    qry = mpr_graph_new_query(dev->obj.graph, 0, MPR_SIG, (void*)cmp_qry_sigs,
                              "hi", dev->obj.id, MPR_DIR_ANY);
    mpr_tbl_add_record(dev->obj.props.synced, MPR_PROP_SIG, NULL,
                       1, MPR_LIST, qry, MOD_NONE | PROP_OWNED);
    dev->registered = 1;
    dev->ordinal = dev->ordinal_allocator.val;

    snprintf(dev->name + dev->prefix_len + 1, dev->prefix_len + 6, "%d", dev->ordinal);
    name = strdup(dev->name);
    free(dev->name);
    dev->name = name;

    dev->obj.status &= ~MPR_STATUS_STAGED;
    dev->obj.status |= MPR_STATUS_ACTIVE;

    mpr_dev_get_name((mpr_dev)dev);

    /* Check if we have any staged maps */
    mpr_graph_cleanup(dev->obj.graph);
}

int mpr_dev_get_is_registered(mpr_dev dev)
{
    return !dev->obj.is_local || check_registration((mpr_local_dev)dev);
}

void mpr_local_dev_add_sig(mpr_local_dev dev, mpr_local_sig sig, mpr_dir dir)
{
    /* TODO: use & instead? */
    if (dir == MPR_DIR_IN)
        ++dev->num_inputs;
    else
        ++dev->num_outputs;

    if (dev->registered)
        mpr_local_sig_add_to_net(sig, mpr_graph_get_net(dev->obj.graph));

    mpr_obj_incr_version((mpr_obj)dev);
    dev->obj.status |= MPR_DEV_SIG_CHANGED;
}

void mpr_dev_remove_sig(mpr_dev dev, mpr_sig sig)
{
    mpr_dir dir = mpr_sig_get_dir(sig);
    if (dir & MPR_DIR_IN)
        --dev->num_inputs;
    if (dir & MPR_DIR_OUT)
        --dev->num_outputs;
    if (dev->obj.is_local) {
        mpr_obj_incr_version((mpr_obj)dev);
        dev->obj.status |= MPR_DEV_SIG_CHANGED;
    }
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
void mpr_dev_process_incoming_maps(mpr_local_dev dev)
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
        mpr_map map = (mpr_map)*maps;
        maps = mpr_list_get_next(maps);
        if (mpr_obj_get_is_local((mpr_obj)map))
            mpr_map_receive((mpr_local_map)map, dev->time);
        else {
            /* local maps are always located at the start of the list */
            break;
        }
    }
}

/* TODO: handle interrupt-driven updates that omit call to this function */
static int process_outgoing_maps(mpr_local_dev dev)
{
    int msgs = 0;
    mpr_list list;
    mpr_graph graph;
    RETURN_ARG_UNLESS(dev->sending && !dev->polling, 0);

    dev->polling = 1;
    graph = dev->obj.graph;
    /* process and send updated maps */
    /* TODO: speed this up! */
    list = mpr_graph_get_list(graph, MPR_MAP);
    while (list) {
        mpr_map map = (mpr_map)*list;
        list = mpr_list_get_next(list);
        if (mpr_obj_get_is_local((mpr_obj)map))
            mpr_map_send((mpr_local_map)map, dev->time);
        else {
            /* local maps are always located at the start of the list */
            break;
        }
    }
    dev->sending = 0;
    list = mpr_graph_get_list(graph, MPR_LINK);
    while (list) {
        msgs += mpr_link_process_bundles((mpr_link)*list, dev->time);
        list = mpr_list_get_next(list);
    }
    dev->polling = 0;
    return msgs != 0;
}

void mpr_dev_update_maps(mpr_dev dev) {
    RETURN_UNLESS(dev && dev->obj.is_local);
    if (!((mpr_local_dev)dev)->polling)
        process_outgoing_maps((mpr_local_dev)dev);
    ((mpr_local_dev)dev)->time_is_stale = 1;
}

static int mpr_dev_send_sigs(mpr_local_dev dev, mpr_dir dir, int force)
{
    mpr_list list = mpr_dev_get_sigs((mpr_dev)dev, dir);
    int sent = 0;
    while (list) {
        mpr_sig sig = (mpr_sig)*list;
        if (force || mpr_tbl_get_is_dirty(((mpr_obj)sig)->props.synced)) {
            mpr_sig_send_state(sig, MSG_SIG);
            if (!force)
                mpr_tbl_set_is_dirty(((mpr_obj)sig)->props.synced, 0);
            ++sent;
        }
        list = mpr_list_get_next(list);
    }
    return sent;
}

static int mpr_dev_send_maps(mpr_local_dev dev, mpr_dir dir, int msg)
{
    mpr_list maps = mpr_dev_get_maps((mpr_dev)dev, dir);
    int sent = 0;
    while (maps) {
        mpr_map_send_state((mpr_map)*maps, -1, msg, 0);
        maps = mpr_list_get_next(maps);
        ++sent;
    }
    return sent;
}

int mpr_dev_poll(mpr_dev dev, int block_ms)
{
    return mpr_net_poll(mpr_graph_get_net(dev->obj.graph), block_ms);
}

int mpr_dev_start_polling(mpr_dev dev, int block_ms)
{
    return mpr_net_start_polling(mpr_graph_get_net(dev->obj.graph), block_ms);
}

int mpr_dev_stop_polling(mpr_dev dev)
{
    return mpr_net_stop_polling(mpr_graph_get_net(dev->obj.graph));
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
        printf("  %p: %"PR_MPR_ID" (%d) -> %"PR_MPR_ID"%s (%d)\n", id_map, id_map->LID,
               id_map->LID_refcount, id_map->GID, id_map->indirect ? "*" : "", id_map->GID_refcount);
        id_maps = &(*id_maps)->next;
    }
}
#endif

mpr_id_map mpr_dev_add_id_map(mpr_local_dev dev, int group, mpr_id LID, mpr_id GID, int indirect)
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
    id_map->indirect = indirect;
    dev->id_maps.reserve = id_map->next;
    id_map->next = dev->id_maps.active[group];
    dev->id_maps.active[group] = id_map;
#ifdef DEBUG
    mpr_local_dev_print_id_maps(dev);
#endif
    return id_map;
}

void mpr_dev_remove_id_map(mpr_local_dev dev, int group, mpr_id_map rem)
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
        if (id_map->LID_refcount <= id_map->indirect) {
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
        if (id_map->LID == LID && id_map->LID_refcount > 0) {
            return id_map;
        }
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

/* TODO: rename this function */
mpr_id_map mpr_dev_get_id_map_GID_free(mpr_local_dev dev, int group, mpr_id last_GID)
{
    int searching = last_GID != 0;
    mpr_id_map id_map = dev->id_maps.active[group];
    while (id_map && searching) {
        if (id_map->GID == last_GID)
            searching = 0;
        id_map = id_map->next;
    }
    while (id_map) {
        if (id_map->GID_refcount <= 0)
            return id_map;
        id_map = id_map->next;
    }
    return 0;
}

/*! Probe the network to see if a device's proposed name.ordinal is available. */
void mpr_local_dev_probe_name(mpr_local_dev dev, int start_ordinal, mpr_net net)
{
    int i;

    if (start_ordinal)
        dev->ordinal_allocator.val = start_ordinal;

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
                mpr_local_dev_probe_name(dev, hint, net);
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
    return dev ? dev->name : NULL;
}

int mpr_dev_get_is_ready(mpr_dev dev)
{
    return dev && dev->obj.status & MPR_STATUS_ACTIVE ? 1 : 0;
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
            case MPR_PROP_LINKED: {
                if (!dev->obj.is_local)
                    updated += mpr_dev_update_linked(dev, a);
                break;
            }
            default:
                updated += mpr_tbl_add_record_from_msg_atom(dev->obj.props.synced, a, MOD_REMOTE);
                break;
        }
    }
    if (updated) {
        dev->obj.status |= MPR_STATUS_MODIFIED;
        mpr_obj_incr_version((mpr_obj)dev);
    }
    dev->obj.status |= MPR_STATUS_ACTIVE;
    return updated;
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
        mpr_dev_send_sigs(dev, dir, 1);
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

void mpr_dev_update_subscribers(mpr_local_dev ldev)
{
    mpr_net net = mpr_graph_get_net(ldev->obj.graph);
    if (ldev->subscribers) {
        if (mpr_tbl_get_is_dirty(ldev->obj.props.synced)) {
            /* inform device subscribers of changed properties */
            mpr_net_use_subscribers(net, ldev, MPR_DEV);
            mpr_dev_send_state((mpr_dev)ldev, MSG_DEV);
        }
        if (ldev->obj.status & MPR_DEV_SIG_CHANGED) {
            mpr_net_use_subscribers(net, ldev, MPR_SIG);
            mpr_dev_send_sigs(ldev, MPR_DIR_ANY, 0);
            ldev->obj.status &= ~MPR_DEV_SIG_CHANGED;
        }
        ldev->time_is_stale = 1;
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
        mpr_local_dev_probe_name(dev, 0, net);
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
        mpr_graph_remove_link(dev->obj.graph, link, MPR_STATUS_REMOVED);
    }
    if (0 == strncmp(prefix_str, dev->name, dev->prefix_len)) {
        /* If device name matches and ordinal is within my block, free it */
        int diff = ordinal - dev->ordinal_allocator.val - 1;
        if (diff >= 0 && diff < 8)
            dev->ordinal_allocator.hints[diff] = 0;
    }
}
