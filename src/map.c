#include <assert.h>
#include <string.h>
#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <stddef.h>
#include <limits.h>

#include "bitflags.h"
#include "device.h"
#include "graph.h"
#include "link.h"
#include "map.h"
#include "mpr_time.h"
#include "network.h"
#include "path.h"
#include "property.h"
#include "table.h"
#include "value.h"

#include <mapper/mapper.h>

#ifdef _MSC_VER
#include <malloc.h>
#endif

#define MAX_LEN           1024
#define METADATA_OK       (MPR_SLOT_DEV_KNOWN | MPR_SLOT_SIG_KNOWN | MPR_SLOT_LINK_KNOWN)

/* Documentation of status bitflags for maps
 * 'expired'           timeout waiting for map handshake or link heartbeat
 * 'staged'            waiting for registration of local devices
 * 'ready'             all metadata known, devices registered
 * 'active'            received 'mapped' from peer
 * 'slot_link_known'   active link to remote device exists or slot is for local signal
 * 'slot_sig_known'    signal vector length and data type are known
 * 'slot_dev_known'    device owning slot signal is registered
 * 'pushed'            map pushed, waiting for sig & link
 */

#define MPR_MAP_STRUCT_ITEMS                                                    \
    mpr_obj_t obj;                  /* Must be first for type punning */        \
    mpr_dev *scopes;                                                            \
    char *expr_str;                                                             \
    int muted;                      /*!< 1 to mute mapping, 0 to unmute */      \
    int num_scopes;                                                             \
    int num_src;                                                                \
    mpr_loc process_loc;            /*!< Processing location. */                \
    int protocol;                   /*!< Data transport protocol. */            \
    int use_inst;                   /*!< 1 if using instances, 0 otherwise. */  \
    int bundle;

/*! A record that describes the properties of a mapping.
 *  @ingroup map */
typedef struct _mpr_map {
    MPR_MAP_STRUCT_ITEMS            /* Must be first */
    mpr_slot *src;
    mpr_slot dst;
} mpr_map_t;

typedef struct _mpr_local_map {
    MPR_MAP_STRUCT_ITEMS            /* Must be first */
    mpr_local_slot *src;
    mpr_local_slot dst;

    mpr_id_map_t id_map;            /*!< Associated mpr_id_map. */

    mpr_expr expr;                  /*!< The mapping expression. */
    mpr_bitflags updated_inst;      /*!< Bitflags to indicate updated instances. */
    mpr_value *vars;                /*!< User variables values. */
    const char **var_names;         /*!< User variables names. */
    const char **old_var_names;     /*!< User variables names. */
    int num_vars;                   /*!< Number of user variables. */
    int num_old_vars;               /*!< Number of user variables. */
    int num_inst;                   /*!< Number of local instances. */

    uint8_t locality;               /* requires 3 bits -----XXX */
    uint8_t one_src;                /* requires 1 bit */
    uint8_t updated;                /* requires 1 bit */
} mpr_local_map_t;

size_t mpr_map_get_struct_size(int is_local)
{
    return is_local ? sizeof(mpr_local_map_t) : sizeof(mpr_map_t);
}

MPR_INLINE static int mpr_max(int a, int b) { return a > b ? a : b; }

MPR_INLINE static int mpr_min(int a, int b) { return a < b ? a : b; }

static int cmp_qry_scopes(const void *ctx, mpr_dev d)
{
    int i;
    mpr_map m = *(mpr_map*)ctx;
    for (i = 0; i < m->num_scopes; i++) {
        if (!m->scopes[i] || mpr_obj_get_id((mpr_obj)m->scopes[i]) == mpr_obj_get_id((mpr_obj)d))
            return 1;
    }
    return 0;
}

static void mpr_local_map_init(mpr_local_map map)
{
    int i, scope_count, local_src = 0, local_dst = 0;
    mpr_sig dst_sig = mpr_slot_get_sig((mpr_slot)map->dst);
    mpr_dev dst_dev = mpr_sig_get_dev(dst_sig);

    mpr_obj_set_is_local((mpr_obj)map, 1);

    map->locality = 0;
    for (i = 0; i < map->num_src; i++) {
        mpr_sig src_sig = mpr_slot_get_sig((mpr_slot)map->src[i]);
        mpr_dev src_dev = mpr_sig_get_dev(src_sig);
        if (mpr_obj_get_is_local((mpr_obj)src_sig)) {
            mpr_link link = mpr_link_new((mpr_local_dev)src_dev, dst_dev);
            mpr_link_add_map(link, (mpr_map)map);
            mpr_local_slot_set_link(map->src[i], link);
            mpr_local_slot_set_link(map->dst, link);
            ++local_src;
            map->locality |= MPR_LOC_SRC;
        }
        else {
            mpr_link link = mpr_link_new((mpr_local_dev)dst_dev, src_dev);
            mpr_link_add_map(link, (mpr_map)map);
            mpr_local_slot_set_link(map->src[i], link);
        }
    }
    if (mpr_slot_get_sig_if_local((mpr_slot)map->dst)) {
        local_dst = 1;
        map->locality |= MPR_LOC_DST;
    }

    /* TODO: configure number of instances available for each slot */
    map->num_inst = 0;
    map->num_old_vars = 0;
    map->old_var_names = NULL;

    /* assign a unique id to this map if we are the destination */
    if (local_dst && !mpr_obj_get_id((mpr_obj)map)) {
        mpr_id id;
        do {
            id = mpr_dev_generate_unique_id(dst_dev);
        } while (mpr_graph_get_obj(map->obj.graph, id, MPR_MAP));
        mpr_obj_set_id((mpr_obj)map, id);
    }

    /* add scopes */
    scope_count = 0;
    map->num_scopes = map->num_src;
    map->scopes = (mpr_dev *) malloc(sizeof(mpr_dev) * map->num_scopes);

    for (i = 0; i < map->num_src; i++) {
        /* check that scope has not already been added */
        int j, found = 0;
        mpr_sig sig = mpr_slot_get_sig((mpr_slot)map->src[i]);
        mpr_dev dev = mpr_sig_get_dev(sig);
        for (j = 0; j < scope_count; j++) {
            if (map->scopes[j] == dev) {
                found = 1;
                break;
            }
        }
        if (!found) {
            map->scopes[scope_count] = dev;
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
        if (mpr_slot_get_link((mpr_slot)map->src[i]) != mpr_slot_get_link((mpr_slot)map->src[0])) {
            map->one_src = 0;
            break;
        }
    }

    if (local_dst && (local_src == map->num_src)) {
        /* all reference signals are local */
        mpr_sig sig = mpr_slot_get_sig((mpr_slot)map->dst);
        mpr_dev dev = mpr_sig_get_dev(sig);
        mpr_link link = mpr_slot_get_link((mpr_slot)map->src[0]);
        /* TODO: revise this hackery */
        map->protocol = mpr_link_get_dev_dir(link, dev) ? MPR_PROTO_TCP : MPR_PROTO_UDP;
        map->locality = MPR_LOC_BOTH;
        map->obj.status |= MPR_MAP_STATUS_PUSHED;
    }

    /* Default to processing at source device unless the maps has heterogeneous sources. */
    map->process_loc = (MPR_LOC_BOTH == map->locality || map->one_src) ? MPR_LOC_SRC : MPR_LOC_DST;

    /* Don't run mpr_local_map_update_status() here since user code may add props before push. */
}

static void relink_props(mpr_map m)
{
    mpr_tbl t = m->obj.props.synced;
    mpr_list q = mpr_graph_new_query(m->obj.graph, 0, MPR_DEV, (void*)cmp_qry_scopes, "v", &m);

#define link(PROP, TYPE, DATA, FLAGS) \
    mpr_tbl_link_value(t, MPR_PROP_##PROP, 1, TYPE, DATA, FLAGS);
    link(BUNDLE,      MPR_INT32, &m->bundle,      MOD_ANY | PROP_SET);
    link(DATA,        MPR_PTR,   &m->obj.data,    MOD_ANY | INDIRECT | LOCAL_ACCESS | PROP_SET);
    link(EXPR,        MPR_STR,   &m->expr_str,    MOD_ANY | INDIRECT | PROP_SET);
    link(ID,          MPR_INT64, &m->obj.id,      MOD_NONE | LOCAL_ACCESS | PROP_SET);
    link(MUTED,       MPR_BOOL,  &m->muted,       MOD_ANY | PROP_SET);
    link(NUM_SIGS_IN, MPR_INT32, &m->num_src,     MOD_NONE | PROP_SET);
    link(PROCESS_LOC, MPR_INT32, &m->process_loc, MOD_ANY | PROP_SET);
    /* do not mark value as set to enable initialization */
    link(PROTOCOL,    MPR_INT32, &m->protocol,    MOD_REMOTE);
    link(SCOPE,       MPR_LIST,  q,               MOD_NONE | PROP_OWNED | PROP_SET);
    link(STATUS,      MPR_INT32, &m->obj.status,  MOD_NONE | LOCAL_ACCESS | PROP_SET);
    /* do not mark value as set to enable initialization */
    link(USE_INST,    MPR_BOOL,  &m->use_inst,    MOD_REMOTE);
    link(VERSION,     MPR_INT32, &m->obj.version, MOD_REMOTE | PROP_SET);
#undef link
}

void mpr_map_init(mpr_map m, int num_src, mpr_sig *src, mpr_sig dst, int is_local)
{
    int i;
    mpr_graph g = m->obj.graph;
    m->obj.props.synced = mpr_tbl_new();
    m->obj.props.staged = mpr_tbl_new();

    m->num_src = num_src;
    m->src = (mpr_slot*)malloc(sizeof(mpr_slot) * num_src);
    for (i = 0; i < num_src; i++) {
        mpr_sig sig;
        if (mpr_obj_get_graph((mpr_obj)src[i]) == g) {
            sig = src[i];
        }
        else if (  !(sig = (mpr_sig)mpr_graph_get_obj(g, mpr_obj_get_id((mpr_obj)src[i]), MPR_SIG))
                 || (mpr_sig_get_dev(sig) != mpr_sig_get_dev(src[i]))) {
            sig = mpr_graph_add_sig(g, mpr_sig_get_name(src[i]),
                                    mpr_dev_get_name(mpr_sig_get_dev(src[i])), 0);
            mpr_sig_copy_props(sig, src[i]);
        }
        m->src[i] = mpr_slot_new(m, sig, MPR_DIR_UNDEFINED, is_local, 1);
        mpr_slot_set_id(m->src[i], i);
    }

    m->dst = mpr_slot_new(m, dst, mpr_obj_get_is_local((mpr_obj)dst) ? MPR_DIR_IN : MPR_DIR_UNDEFINED,
                          is_local, 0);

    relink_props(m);

    mpr_tbl_add_record(m->obj.props.synced, MPR_PROP_IS_LOCAL, NULL, 1,
                       MPR_BOOL, &is_local, LOCAL_ACCESS | MOD_NONE);
    m->obj.status = MPR_STATUS_NEW | MPR_STATUS_STAGED;
    m->protocol = MPR_PROTO_UDP;

    if (is_local)
        mpr_local_map_init((mpr_local_map)m);
}

void mpr_map_memswap(mpr_map map1, mpr_map map2)
{
    size_t size = mpr_map_get_struct_size(mpr_obj_get_is_local((mpr_obj)map1));
    mpr_map tmp = (mpr_map)alloca(size);
    int i;

    assert(map1->obj.is_local == map2->obj.is_local);

    /* swap contents of new and old maps */
    memcpy(tmp, map1, size);
    memcpy(map1, map2, size);
    memcpy(map2, tmp, size);

    /* re-link table */
    relink_props(map1);
    relink_props(map2);

    /* update slot map ptrs */
    mpr_slot_set_map_ptr(map1->dst, map1);
    for (i = 0; i < map1->num_src; i++) {
        mpr_slot_set_map_ptr(map1->src[i], map1);
    }
    mpr_slot_set_map_ptr(map2->dst, map2);
    for (i = 0; i < map2->num_src; i++) {
        mpr_slot_set_map_ptr(map2->src[i], map2);
    }
}

static int compare_sig_names(const void *l, const void *r)
{
    return mpr_sig_compare_names(*(mpr_sig*)l, *(mpr_sig*)r);
}

mpr_map mpr_map_new(int num_src, mpr_sig *src, int num_dst, mpr_sig *dst)
{
    mpr_graph g;
    mpr_map m;
    mpr_obj o;
    mpr_sig *src_sorted;
    mpr_list maps;
    unsigned char i, j, is_local = 0;

    RETURN_ARG_UNLESS(src && *src && dst && *dst, 0);
    RETURN_ARG_UNLESS(num_src > 0 && num_src <= MAX_NUM_MAP_SRC, 0);

    for (i = 0; i < num_src; i++) {
        mpr_dev src_dev = mpr_sig_get_dev(src[i]);
        /* check to make sure a src signal is not included more than once */
        for (j = i + 1; j < num_src; j++) {
            if (src[i] == src[j]) {
                trace("error in mpr_map_new(): multiple use of source signal.\n");
                return 0;
            }
        }
        for (j = 0; j < num_dst; j++) {
            mpr_dev dst_dev = mpr_sig_get_dev(dst[j]);
            if (src[i] == dst[j]) {
                trace("Cannot connect signal '%s:%s' to itself.\n",
                      mpr_dev_get_name(src_dev), mpr_sig_get_name(src[i]));
                return 0;
            }
            if (!mpr_dev_get_is_ready(src_dev) || !mpr_dev_get_is_ready(dst_dev)) {
                /* Only allow this if devices are the same or share a graph. */
                if (mpr_obj_get_graph((mpr_obj)src[i]) == mpr_obj_get_graph((mpr_obj)dst[j]))
                    continue;
                trace("Cannot create map between uninitialized devices unless they share a graph.");
                return 0;
            }
            if (0 == mpr_sig_compare_names(src[i], dst[j])) {
                trace("Cannot connect signal '%s:%s' to itself.\n",
                      mpr_dev_get_name(src_dev), mpr_sig_get_name(src[i]));
                return 0;
            }
        }
        is_local += mpr_obj_get_is_local((mpr_obj)src[i]);
    }
    /* Only 1 destination supported for now */
    RETURN_ARG_UNLESS(1 == num_dst, 0);
    is_local += mpr_obj_get_is_local((mpr_obj)*dst);
    g = mpr_obj_get_graph((mpr_obj)*dst);

    /* check if record of map already exists */
    maps = mpr_sig_get_maps(*dst, MPR_DIR_IN);
    if (maps) {
        for (i = 0; i < num_src; i++) {
            o = mpr_graph_get_obj(g, mpr_obj_get_id((mpr_obj)src[i]), MPR_SIG);
            if (o) {
                mpr_list temp = mpr_sig_get_maps((mpr_sig)o, MPR_DIR_OUT);
                maps = mpr_list_get_isect(maps, temp);
            }
            else {
                mpr_list_free(maps);
                maps = 0;
                break;
            }
        }
        while (maps) {
            if (((mpr_map)*maps)->num_src == num_src) {
                m = (mpr_map)*maps;
                mpr_list_free(maps);
                /* un-release map if it has been released */
#ifdef DEBUG
                if (m->obj.status & (MPR_STATUS_REMOVED | MPR_STATUS_EXPIRED)) {
                    trace("un-releasing map!\n");
                }
#endif
                m->obj.status &= ~(MPR_STATUS_REMOVED | MPR_STATUS_EXPIRED);
                return m;
            }
            maps = mpr_list_get_next(maps);
        }
    }

    m = (mpr_map)mpr_graph_add_obj(g, MPR_MAP, is_local);
    m->bundle = 1;

    /* Sort the source signals by name */
    src_sorted = (mpr_sig*) malloc(num_src * sizeof(mpr_sig));
    memcpy(src_sorted, src, num_src * sizeof(mpr_sig));
    qsort(src_sorted, num_src, sizeof(mpr_sig), compare_sig_names);

    mpr_map_init(m, num_src, src_sorted, *dst, is_local);
    free(src_sorted);

    return m;
}

/* TODO: if map is local handle locally and don't send unmap to network bus */
void mpr_map_release(mpr_map m)
{
    mpr_net_use_bus(mpr_graph_get_net(m->obj.graph));
    mpr_map_send_state(m, -1, MSG_UNMAP, 0);
}

void mpr_map_refresh(mpr_map m)
{
    RETURN_UNLESS(m);
    mpr_net_use_bus(mpr_graph_get_net(m->obj.graph));
    mpr_map_send_state(m, -1, m->obj.is_local ? MSG_MAP_TO : MSG_MAP, 0);
}

static void release_local_inst(mpr_local_map map, mpr_dev scope)
{
    mpr_local_sig dst_sig;
    assert(MPR_LOC_DST & map->locality);
    dst_sig = (mpr_local_sig)mpr_slot_get_sig((mpr_slot)map->dst);
    if (scope) {
        /* release local destination instances with this device as origin */
        mpr_local_sig_release_inst_by_origin(dst_sig, scope);
    }
    else {
        /* release local destination instances with any map src as origin */
        mpr_dev last_dev = 0;
        int i;
        for (i = 0; i < map->num_src; i++) {
            mpr_dev dev = mpr_sig_get_dev(mpr_slot_get_sig((mpr_slot)map->src[i]));
            if (dev != last_dev)
                mpr_local_sig_release_inst_by_origin(dst_sig, dev);
            last_dev = dev;
        }
    }

}

void mpr_map_free(mpr_map map)
{
    int i;
    mpr_link link;

    if (map->obj.is_local) {
        mpr_local_map lmap = (mpr_local_map)map;

        if (lmap->id_map.LID) {
            /* release map-generated instances */
            lo_message msg = mpr_map_build_msg(lmap, 0, 0, 0, &lmap->id_map);
            mpr_time time;
            mpr_time_set(&time, MPR_NOW);
            if (lmap->locality & MPR_LOC_DST) {
                mpr_net_set_bundle_time(mpr_graph_get_net(lmap->obj.graph), time);
                mpr_sig_osc_handler(NULL, lo_message_get_types(msg), lo_message_get_argv(msg),
                                    lo_message_get_argc(msg), msg, (void*)mpr_slot_get_sig(map->dst));
                lo_message_free(msg);
            }
            else {
                mpr_local_dev dev = (mpr_local_dev)mpr_sig_get_dev(mpr_slot_get_sig(map->src[0]));
                mpr_local_slot_send_msg(lmap->dst, msg, time, MPR_PROTO_TCP);
                mpr_local_dev_set_sending(dev);
            }
            for (i = 0; i < map->num_src; i++) {
                mpr_sig sig = mpr_slot_get_sig(map->src[i]);
                if (mpr_obj_get_is_local((mpr_obj)sig)) {
                    mpr_local_dev dev = (mpr_local_dev)mpr_sig_get_dev(sig);
                    mpr_sig_group group = mpr_local_sig_get_group((mpr_local_sig)sig);
                    mpr_id_map id_map = mpr_dev_get_id_map_by_GID(dev, group, lmap->id_map.GID);
                    if (id_map)
                        mpr_dev_remove_id_map(dev, group, id_map);
                }
            }
            {
                mpr_sig sig = mpr_slot_get_sig(map->dst);
                if (mpr_obj_get_is_local((mpr_obj)sig)) {
                    mpr_local_dev dev = (mpr_local_dev)mpr_sig_get_dev(sig);
                    mpr_sig_group group = mpr_local_sig_get_group((mpr_local_sig)sig);
                    mpr_id_map id_map = mpr_dev_get_id_map_by_GID(dev, group, lmap->id_map.GID);
                    if (id_map)
                        mpr_dev_remove_id_map(dev, group, id_map);
                }
            }
        }
        else if (lmap->use_inst && (MPR_LOC_DST & lmap->locality))
            release_local_inst(lmap, NULL);

        /* one more case: if map is local only need to decrement num_maps in local map */
        /* map could still involve multiple local devices and links */
        if (MPR_LOC_BOTH == lmap->locality) {
            mpr_dev dst_dev = mpr_sig_get_dev(mpr_slot_get_sig(map->dst));
            for (i = 0; i < map->num_src; i++) {
                mpr_dev src_dev = mpr_sig_get_dev(mpr_slot_get_sig(map->src[i]));
                mpr_link link = mpr_dev_get_link_by_remote(src_dev, dst_dev);
                if (link)
                    mpr_link_remove_map(link, map);
                if (lmap->one_src)
                    break;
            }
        }

        /* free buffers associated with user-defined expression variables */
        if (lmap->vars) {
            for (i = 0; i < lmap->num_vars; i++) {
                mpr_value_free(lmap->vars[i]);
                free((void*)lmap->var_names[i]);
            }
            free(lmap->vars);
            free(lmap->var_names);
        }
        for (i = 0; i < lmap->num_old_vars; i++) {
            FUNC_IF(free, (void*)lmap->old_var_names[i]);
        }
        FUNC_IF(free, lmap->old_var_names);
        mpr_bitflags_free(lmap->updated_inst);
        FUNC_IF(mpr_expr_free, lmap->expr);
    }

    /* remove map from parent link */
    for (i = 0; i < map->num_src; i++) {
        link = mpr_slot_get_link(map->src[i]);
        if (link)
            mpr_link_remove_map(link, map);
    }
    link = mpr_slot_get_link(map->dst);
    if (link)
        mpr_link_remove_map(link, map);

    /* free slots */
    for (i = 0; i < map->num_src; i++) {
        mpr_slot_free(map->src[i]);
    }
    free(map->src);
    mpr_slot_free(map->dst);

    if (map->num_scopes && map->scopes)
        free(map->scopes);

    mpr_obj_free(&map->obj);
    FUNC_IF(free, map->expr_str);
}

static int cmp_qry_sigs(const void *ctx, mpr_sig s)
{
    mpr_map m = *(mpr_map*)ctx;
    mpr_loc l = *(mpr_loc*)((char*)ctx + sizeof(mpr_map*));
    int *idx = (int*)((char*)ctx + sizeof(mpr_map*) + sizeof(mpr_loc));
    if (l & MPR_LOC_SRC && *idx < m->num_src) {
        if (s == mpr_slot_get_sig(m->src[*idx])) {
            ++(*idx);
            return 2; /* Signal that list scanning should restart. */
        }
    }
    return (l & MPR_LOC_DST) ? (s == mpr_slot_get_sig(m->dst)) : 0;
}

mpr_list mpr_map_get_sigs(mpr_map m, mpr_loc l)
{
    RETURN_ARG_UNLESS(m, 0);
    return mpr_graph_new_query(m->obj.graph, 1, MPR_SIG, (void*)cmp_qry_sigs, "vix", &m, l, 0);
}

int mpr_map_get_sig_idx(mpr_map map, mpr_sig sig)
{
    int i;
    mpr_id id = mpr_obj_get_id((mpr_obj)sig);
    mpr_sig dst = mpr_slot_get_sig(map->dst);
    if (mpr_obj_get_id((mpr_obj)dst) == id)
        return 0;
    for (i = 0; i < map->num_src; i++) {
        mpr_sig src = mpr_slot_get_sig(map->src[i]);
        if (mpr_obj_get_id((mpr_obj)src) == id)
            return i;
    }
    return -1;
}

int mpr_map_get_is_ready(mpr_map m)
{
    return m ? (MPR_STATUS_ACTIVE & m->obj.status) : 0;
}

/* Here we do not edit the "scope" property directly – instead we stage the
 * change with device name arguments and send to the distributed graph. */
static void stage_scope(mpr_map m, mpr_dev d, int flag)
{
    mpr_prop p = MPR_PROP_SCOPE | flag;
    const void *val;
    mpr_type type;
    int len;

    RETURN_UNLESS(m);

    mpr_tbl_get_record_by_idx(m->obj.props.staged, p, NULL, &len, &type, &val, NULL);
    if (0 == len) {
        const char *name = mpr_dev_get_name(d);
        mpr_tbl_add_record(m->obj.props.staged, p, NULL, 1, MPR_STR, name, 1);
        return;
    }
    else {
        const char **new_val;
        assert(MPR_STR == type);
        new_val = (const char**)alloca((len + 1) * sizeof(char*));
        if (1 == len)
            new_val[0] = ((const char*)val);
        else
            memcpy(new_val, val, sizeof(char*) * len);
        new_val[len] = d ? mpr_dev_get_name(d) : "all";
        mpr_tbl_add_record(m->obj.props.staged, p, NULL, len + 1, MPR_STR, new_val, MOD_REMOTE);
    }
}

/* Here we do not edit the "scope" property directly – instead we stage the
 * change with device name arguments and send to the distributed graph. */
void mpr_map_add_scope(mpr_map m, mpr_dev d)
{
    stage_scope(m, d, PROP_ADD);
}

/* Here we do not edit the "scope" property directly – instead we stage the
 * change with device name arguments and send to the distributed graph. */
void mpr_map_remove_scope(mpr_map m, mpr_dev d)
{
    stage_scope(m, d, PROP_REMOVE);
}

void mpr_map_remove_scope_internal(mpr_map map, mpr_dev dev)
{
    int i;
    for (i = 0; i < map->num_scopes; i++) {
        if (map->scopes[i] == dev)
            break;
    }
    if (i < map->num_scopes) {
        /* found - remove scope at index i */
        for (++i; i < map->num_scopes - 1; i++)
            map->scopes[i] = map->scopes[i + 1];
        --map->num_scopes;
        map->scopes = realloc(map->scopes, map->num_scopes * sizeof(mpr_dev));
    }
}

static int add_scope(mpr_map m, const char *name)
{
    int i;
    mpr_dev d = 0;
    RETURN_ARG_UNLESS(m && name, 0);

    if (strcmp(name, "all")==0) {
        for (i = 0; i < m->num_scopes; i++) {
            if (!m->scopes[i])
                return 0;
        }
    }
    else {
        d = mpr_graph_add_dev(m->obj.graph, name, 0, 1);
        for (i = 0; i < m->num_scopes; i++) {
            if (m->scopes[i] && mpr_obj_get_id((mpr_obj)m->scopes[i]) == mpr_obj_get_id((mpr_obj)d))
                return 0;
        }
    }

    /* not found - add a new scope */
    i = ++m->num_scopes;
    m->scopes = realloc(m->scopes, i * sizeof(mpr_dev));
    m->scopes[i-1] = d;
    return 1;
}

static int remove_scope(mpr_map m, const char *name)
{
    int i;
    mpr_dev dev = 0;
    RETURN_ARG_UNLESS(m && name, 0);
    if (strcmp(name, "all")==0)
        name = 0;
    for (i = 0; i < m->num_scopes; i++) {
        if (!m->scopes[i]) {
            if (!name)
                break;
        }
        else if (name && strcmp(mpr_dev_get_name(m->scopes[i]), name) == 0) {
            dev = m->scopes[i];
            break;
        }
    }
    if (!dev)
        return 0;

    /* found - remove scope at index i */
    for (++i; i < m->num_scopes - 1; i++)
        m->scopes[i] = m->scopes[i + 1];
    --m->num_scopes;
    m->scopes = realloc(m->scopes, m->num_scopes * sizeof(mpr_dev));

    if (m->obj.is_local && MPR_LOC_DST & ((mpr_local_map)m)->locality) {
        release_local_inst((mpr_local_map)m, dev);
    }

    return 1;
}

static int update_scope(mpr_map m, mpr_msg_atom a)
{
    int i = 0, j, updated = 0, num = mpr_msg_atom_get_len(a);
    lo_arg **scope_list = mpr_msg_atom_get_values(a);
    if (scope_list && *scope_list) {
        const char *name;
        if (1 == num && strcmp(&scope_list[0]->s, "none")==0)
            num = 0;

        if (mpr_msg_atom_get_prop(a) & PROP_ADD) {
            for (i = 0; i < num; i++)
                updated += add_scope(m, &scope_list[i]->s);
        }
        else if (mpr_msg_atom_get_prop(a) & PROP_REMOVE) {
            for (i = 0; i < num; i++)
                updated += remove_scope(m, &scope_list[i]->s);
        }
        else {
            /* First remove old scopes that are missing */
            while (i < m->num_scopes) {
                int found = 0;
                for (j = 0; j < num; j++) {
                    name = mpr_path_skip_slash(&scope_list[j]->s);
                    if (!m->scopes[i]) {
                        if (strcmp(name, "all") == 0) {
                            found = 1;
                            break;
                        }
                        break;
                    }
                    if (strcmp(name, mpr_dev_get_name(m->scopes[i])) == 0) {
                        found = 1;
                        break;
                    }
                }
                if (!found && m->scopes[i])
                    updated += remove_scope(m, mpr_dev_get_name(m->scopes[i]));
                else
                    ++i;
            }
            /* ...then add any new scopes */
            for (i = 0; i < num; i++)
                updated += add_scope(m, &scope_list[i]->s);
        }
    }
    return updated;
}

/* 1) update all signals for this timestep, mark signal instances as "updated"
 * 1b) call process_sig (immediately?) to update slots.
 * 1c) need to allow for users modifying sigs, maps, etc after updating sig and before processing
 * 2) iterate through all the maps and process, mark map instances as "updated"
 * 2b) if instance of a map has already been updated don't bother doing it again
 * 3) can bundle map output during iteration
 *
 * Plan of change:
 * 1) move id_maps from signals to maps
 * 2) on release of local instance, can reuse instance resource and mark id_map as "to release"
 * 3) map should iterate through active id_maps instead of instances
 * 4) when it comes to "to release" id_map, send release and decref LID
 */

/* only called for outgoing, source-processed maps */
void mpr_map_send(mpr_local_map m, mpr_time time)
{
    int i, status;
    mpr_sig_group group;
    mpr_type manage_inst = 0;
    lo_message msg;
    mpr_local_dev dev;
    mpr_local_slot src_slot;
    mpr_local_sig src_sig;
    mpr_id_map id_map = 0;
    mpr_value src_vals[MAX_NUM_MAP_SRC], dst_val;

    assert(m->obj.is_local);

    RETURN_UNLESS(   m->updated && m->expr && !m->muted
                  && MPR_DIR_OUT == mpr_slot_get_dir((mpr_slot)m->src[0]));

    /* temporary solution: use most multitudinous source signal for id_map
     * permanent solution: move id_maps to map? */
    src_slot = m->src[0];
    src_sig = (mpr_local_sig)mpr_slot_get_sig((mpr_slot)src_slot);
    for (i = 0; i < m->num_src; i++) {
        mpr_local_sig comp = (mpr_local_sig)mpr_slot_get_sig((mpr_slot)m->src[i]);
        if (  mpr_sig_get_num_inst_internal((mpr_sig)comp)
            > mpr_sig_get_num_inst_internal((mpr_sig)src_sig)) {
            src_slot = m->src[i];
            src_sig = comp;
        }
        src_vals[i] = mpr_slot_get_value(m->src[i]);
    }
    group = mpr_local_sig_get_group(src_sig);
    dev = (mpr_local_dev)mpr_sig_get_dev((mpr_sig)src_sig);

    dst_val = mpr_slot_get_value(m->dst);

    if (m->use_inst) {
        if (mpr_sig_get_use_inst((mpr_sig)src_sig) && !mpr_expr_get_manages_inst(m->expr)) {
            manage_inst = MPR_SIG;
        }
        else {
            manage_inst = MPR_MAP;
            id_map = &m->id_map;
        }
    }

    /* TODO: once releases are added to bitflags, find an instance with a value first to check
     * whether EXPR_EVAL_DONE flag is added. If not, go back and handle releases for previous
     * instances */

    for (i = 0; i < m->num_inst; i++) {
        /* Check if this instance has been updated */
        if (!mpr_bitflags_get(m->updated_inst, i))
            continue;
        /* TODO: Check if this instance has enough history to process the expression */
        status = mpr_expr_eval(m->expr, mpr_graph_get_expr_eval_buffer(m->obj.graph),
                               src_vals, m->vars, dst_val, &time, i);
        if (!m->use_inst) {
            /* remove EXPR_RELEASE* event flags */
            status &= (EXPR_UPDATE | EXPR_EVAL_DONE);
        }
        if (!status)
            continue;

        /* if the map doesn't use instances we don't care about id_maps at all */
        /* TODO: in future updates map-managed reinstancing should use a separate device id_map
         * group to translate signal id_maps to the reinstanced versions. Or would it be feasible
         * to add a third ID to the existing id_maps? */
        if (MPR_SIG == manage_inst) {
            /* we need to use the source signal's id_map */
            id_map = mpr_local_sig_get_id_map_by_inst_idx(src_sig, i);
            if (!id_map) {
                trace("error: couldn't find id_map for signal instance idx %d\n", i);
                continue;
            }
        }

        /* send instance release if dst is instanced and either src or map is also instanced. */
        if (status & EXPR_RELEASE_BEFORE_UPDATE) {
            /* build and send release message */
            msg = mpr_map_build_msg(m, 0, 0, 0, id_map);
            mpr_local_slot_send_msg(m->dst, msg, time, m->protocol);

            if (MPR_MAP == manage_inst && id_map->LID) {
                /* need to clear id_maps */
                mpr_id_map tmp = mpr_dev_get_id_map_by_LID(dev, group, MPR_DEFAULT_INST_LID);
                mpr_dev_remove_id_map(dev, group, tmp);
                id_map->LID = 0;
            }
            /* TODO: if signal is managing instances we should retire reinstancing id_map here */
        }

        if (status & EXPR_UPDATE) {
            mpr_time *time = mpr_value_get_time(dst_val, i, 0);
            if (MPR_MAP == manage_inst) {
                if (!id_map->LID) {
                    /* need to (re)create id_map */
                    mpr_id_map tmp = mpr_dev_get_id_map_by_LID(dev, group, MPR_DEFAULT_INST_LID);
                    if (!tmp) {
                        /* add a new id_map to the device */
                        tmp = mpr_dev_add_id_map(dev, group, MPR_DEFAULT_INST_LID, 0, 0);
                    }
                    /* copy the new id_map values into the map's id_map */
                    id_map->LID = MPR_DEFAULT_INST_LID;
                    id_map->GID = tmp->GID;
                }
            }
            /* build and send update message */
            msg = mpr_map_build_msg(m, 0, dst_val, i, id_map);
            mpr_local_slot_send_msg(m->dst, msg, *time, m->protocol);
        }

        /* send instance release if dst is instanced and either src or map is also instanced. */
        if (status & EXPR_RELEASE_AFTER_UPDATE) {
            /* build and send release message */
            msg = mpr_map_build_msg(m, 0, 0, 0, id_map);
            mpr_local_slot_send_msg(m->dst, msg, time, m->protocol);
            if (MPR_MAP == manage_inst && id_map->LID) {
                /* need to clear id_maps */
                mpr_id_map tmp = mpr_dev_get_id_map_by_LID(dev, group, MPR_DEFAULT_INST_LID);
                mpr_dev_remove_id_map(dev, group, tmp);
                id_map->LID = 0;
            }
            /* TODO: if signal is managing instances we should retire reinstancing id_map here */
        }

        if (status & EXPR_EVAL_DONE)
            break;
    }

    mpr_bitflags_clear(m->updated_inst);
    m->updated = 0;
}

/* only called for incoming, destination-processed maps */
/* TODO: merge with mpr_map_send()? */
void mpr_map_receive(mpr_local_map m, mpr_time time)
{
    int i, status, map_manages_inst = 0;
    mpr_local_slot src_slot;
    mpr_sig src_sig;
    mpr_local_sig dst_sig;
    mpr_value src_vals[MAX_NUM_MAP_SRC], dst_val;

    assert(m->obj.is_local);

    RETURN_UNLESS(m->updated && m->expr && !m->muted
                  && MPR_DIR_IN == mpr_slot_get_dir((mpr_slot)m->src[0]));

    /* temporary solution: use most multitudinous source signal for id_map
     * permanent solution: move id_maps to map */
    src_slot = m->src[0];
    src_sig = mpr_slot_get_sig((mpr_slot)src_slot);
    for (i = 0; i < m->num_src; i++) {
        mpr_sig comp = mpr_slot_get_sig((mpr_slot)m->src[i]);
        if (  mpr_sig_get_num_inst_internal(comp)
            > mpr_sig_get_num_inst_internal(src_sig)) {
            src_slot = m->src[i];
            src_sig = comp;
        }
        src_vals[i] = mpr_slot_get_value(m->src[i]);
    }

    dst_sig = (mpr_local_sig)mpr_slot_get_sig((mpr_slot)m->dst);
    dst_val = mpr_slot_get_value(m->dst);

    if (mpr_expr_get_manages_inst(m->expr)) {
        map_manages_inst = 1;
    }

    for (i = 0; i < m->num_inst; i++) {
        void *value;
        if (!mpr_bitflags_get(m->updated_inst, i))
            continue;
        status = mpr_expr_eval(m->expr, mpr_graph_get_expr_eval_buffer(m->obj.graph),
                               src_vals, m->vars, dst_val, &time, i);
        if (!m->use_inst)
            status &= (EXPR_UPDATE | EXPR_EVAL_DONE);

        if (!status)
            continue;

        value = mpr_value_get_value(dst_val, i, 0);

        /* TODO: Also apply to all instances if the map is convergent and other slot signals are instanced */
        if (!m->use_inst) {
            /* apply update to all active destination instances */
            mpr_local_sig_set_inst_value(dst_sig, value, -1, &m->id_map, status, map_manages_inst, time);
        }
        else if (status & EXPR_EVAL_DONE) {
            /* expression reduces across instances, no need to recompute result */
            for (; i < m->num_inst; i++) {
                if (!mpr_bitflags_get(m->updated_inst, i))
                    continue;
                mpr_local_sig_set_inst_value(dst_sig, value, i, &m->id_map, status, map_manages_inst, time);
            }
            break;
        }
        else {
            mpr_local_sig_set_inst_value(dst_sig, value, i, &m->id_map, status, map_manages_inst, time);
        }
    }
    mpr_bitflags_clear(m->updated_inst);
    m->updated = 0;
}

/*! Build a value update message for a given map. */
lo_message mpr_map_build_msg(mpr_local_map m, mpr_local_slot slot, mpr_value val,
                             unsigned int idx, mpr_id_map id_map)
{
    int i;
    NEW_LO_MSG(msg, return 0);

    if (val) {
        mpr_value_add_to_msg(val, idx, msg);
    }
    else if (m->use_inst) {
        /* retrieve length from slot */
        mpr_sig sig = mpr_slot_get_sig((mpr_slot)(slot ? slot : m->dst));
        int len = mpr_sig_get_len(sig);
        for (i = 0; i < len; i++)
            lo_message_add_nil(msg);
    }
    else {
        lo_message_free(msg);
        return 0;
    }
    if (m->use_inst && id_map) {
        lo_message_add_string(msg, "@in");
        lo_message_add_int64(msg, id_map->GID);
    }
    if (slot) {
        /* add slot */
        lo_message_add_string(msg, "@sl");
        lo_message_add_int32(msg, mpr_slot_get_id((mpr_slot)slot));
    }
    return msg;
}

void mpr_map_alloc_values(mpr_local_map m, int quiet)
{
    /* TODO: check if this filters non-local processing.
     * if so we can eliminate process_loc tests below
     * if not we are allocating variable memory when we don't need to */
    int i, j, mlen, num_inst = 0, num_vars;
    mpr_expr e = m->expr;
    mpr_value *vars;
    const char **var_names;
    mpr_sig sig;

    /* If there is no expression or the processing is remote,
     * then no memory needs to be (re)allocated. */
    RETURN_UNLESS(m->expr
                  && (MPR_LOC_BOTH == m->locality
                      || !(  (MPR_DIR_OUT == mpr_slot_get_dir((mpr_slot)m->dst))
                           ^ (MPR_LOC_SRC == m->process_loc))));

    /* HANDLE edge case: if the map is local, then src->dir is OUT and dst->dir is IN */

    /* check if slot values need to be reallocated */
    for (i = 0; i < m->num_src; i++) {
        sig = mpr_slot_get_sig((mpr_slot)m->src[i]);
        mlen = mpr_expr_get_src_mlen(e, i);
        mpr_slot_alloc_values(m->src[i], 0, mlen);
        num_inst = mpr_max(mpr_sig_get_num_inst_internal(sig), num_inst);
    }
    sig = mpr_slot_get_sig((mpr_slot)m->dst);
    num_inst = mpr_max(mpr_sig_get_num_inst_internal(sig), num_inst);
    mlen = mpr_expr_get_dst_mlen(e, 0);

    /* If the dst slot is remote, we need to allocate enough dst slot and variable instances for
     * the most multitudinous source signal. In the case where the dst slot is associated with a
     * local signal the call below will default to using the signal's instance count. */
    mpr_slot_alloc_values(m->dst, num_inst, mlen);

    num_vars = mpr_expr_get_num_vars(e);
    vars = (mpr_value*) malloc(sizeof(mpr_value) * num_vars);
    var_names = malloc(sizeof(char*) * num_vars);
    for (i = 0; i < num_vars; i++) {
        int vlen = mpr_expr_get_var_vlen(e, i);
        int var_num_inst = mpr_expr_get_var_is_instanced(e, i) ? num_inst : 1;
        var_names[i] = strdup(mpr_expr_get_var_name(e, i));
        /* check if var already exists */
        for (j = 0; j < m->num_vars; j++) {
            if (!m->var_names[j] || strcmp(m->var_names[j], var_names[i]))
                continue;
            if (mpr_value_get_vlen(m->vars[i]) != vlen)
                continue;
            /* match found */
            break;
        }
        if (j < m->num_vars) {
            /* copy old variable */
            vars[i] = m->vars[j];
            m->vars[j] = 0;
            mpr_value_realloc(vars[i], vlen, mpr_expr_get_var_type(e, i), 1, var_num_inst, 0);
        }
        else {
            vars[i] = mpr_value_new(vlen, mpr_expr_get_var_type(e, i), 1, var_num_inst);
            /* set position to 0 since we are not currently allowing history on user variables */
        }
        for (j = 0; j < var_num_inst; j++)
            mpr_value_incr_idx(vars[i], j);
    }

    /* free old variables and replace with new */
    if (m->num_vars) {
        if (m->old_var_names)
            m->old_var_names = realloc(m->old_var_names, (m->num_old_vars + m->num_vars) * sizeof(char*));
        else
            m->old_var_names = malloc((m->num_old_vars + m->num_vars) * sizeof(char*));
        for (i = 0; i < m->num_vars; i++) {
            /* store obsolete variable names so they can be removed from the graph */
            for (j = 0; j < num_vars; j++) {
                if (0 == strcmp(m->var_names[i], var_names[j]))
                    break;
            }
            if (j >= num_vars) {
                m->old_var_names[m->num_old_vars + i] = m->var_names[i];
            }
            else {
                FUNC_IF(free, (void*)m->var_names[i]);
                m->old_var_names[m->num_old_vars + i] = 0;
            }
            FUNC_IF(mpr_value_free, m->vars[i]);
        }
        m->num_old_vars += m->num_vars;
    }
    FUNC_IF(free, m->vars);
    FUNC_IF(free, m->var_names);

    m->vars = vars;
    m->var_names = var_names;
    m->num_vars = num_vars;

    /* allocate update bitflags */
    if (m->updated_inst)
        m->updated_inst = mpr_bitflags_realloc(m->updated_inst, num_inst);
    else
        m->updated_inst = mpr_bitflags_new(num_inst);
    m->num_inst = num_inst;

    if (!quiet) {
        /* Inform remote peers of the change */
        mpr_net net = mpr_graph_get_net(m->obj.graph);
        if (MPR_DIR_OUT == mpr_slot_get_dir((mpr_slot)m->dst)) {
            /* Inform remote destination */
            mpr_net_use_mesh(net, mpr_link_get_admin_addr(mpr_slot_get_link((mpr_slot)m->dst)));
            mpr_map_send_state((mpr_map)m, -1, MSG_MAPPED, 0);
        }
        else {
            /* Inform remote sources */
            for (i = 0; i < m->num_src; i++) {
                mpr_net_use_mesh(net, mpr_link_get_admin_addr(mpr_slot_get_link((mpr_slot)m->src[i])));
                i = mpr_map_send_state((mpr_map)m, i, MSG_MAPPED, 0);
            }
        }
    }
}

/* Helper to replace a map's expression only if the given string
 * parses successfully. Returns 0 on success, non-zero on error. */
static int replace_expr_str(mpr_local_map m, const char *expr_str)
{
    unsigned int i, out_mem, src_lens[MAX_NUM_MAP_SRC], dst_lens[1];
    mpr_type src_types[MAX_NUM_MAP_SRC], dst_types[1];
    mpr_expr expr;
    if (m->expr && m->expr_str && strcmp(m->expr_str, expr_str)==0)
        return 1;

    for (i = 0; i < m->num_src; i++) {
        mpr_sig src = mpr_slot_get_sig((mpr_slot)m->src[i]);
        src_types[i] = mpr_sig_get_type(src);
        src_lens[i] = mpr_sig_get_len(src);
    }
    for (i = 0; i < 1; i++) {
        mpr_sig dst = mpr_slot_get_sig((mpr_slot)m->dst);
        dst_types[i] = mpr_sig_get_type(dst);
        dst_lens[i] = mpr_sig_get_len(dst);
    }

    expr = mpr_expr_new_from_str(expr_str, m->num_src, src_types, src_lens,
                                 1, dst_types, dst_lens);
    RETURN_ARG_UNLESS(expr, 1);

    /* reallocate the central evaluation buffer if necessary */
    mpr_expr_realloc_eval_buffer(expr, mpr_graph_get_expr_eval_buffer(m->obj.graph));

    /* expression update may force processing location to change
     * e.g. if expression combines signals from different devices
     * e.g. if expression refers to current/past value of destination */
    out_mem = mpr_expr_get_dst_mlen(expr, 0);
    if (MPR_LOC_BOTH != m->locality && (out_mem > 1 && MPR_LOC_SRC == m->process_loc)) {
        m->process_loc = MPR_LOC_DST;
        if (MPR_LOC_SRC == m->locality) {
            /* copy expression string but do not execute it */
            mpr_tbl_add_record(m->obj.props.synced, MPR_PROP_EXPR, NULL,
                               1, MPR_STR, expr_str, MOD_REMOTE);
            mpr_expr_free(expr);
            return 1;
        }
    }
    FUNC_IF(mpr_expr_free, m->expr);
    m->expr = expr;

    if (m->expr_str == expr_str)
        return 0;
    mpr_tbl_add_record(m->obj.props.synced, MPR_PROP_EXPR, NULL, 1, MPR_STR, expr_str, MOD_REMOTE);
    mpr_tbl_remove_record(m->obj.props.staged, MPR_PROP_EXPR, NULL, 0);
    return 0;
}

MPR_INLINE static int trim_zeros(char *str, int len)
{
    if (!strchr(str, '.'))
        return len;
    while ('0' == str[len-1])
        --len;
    if ('.' == str[len-1])
        --len;
    str[len] = '\0';
    return len;
}

static int snprint_var(const char *varname, char *str, int max_len, int vec_len,
                       mpr_type type, const void *val)
{
    int i, str_len, var_len;
    RETURN_ARG_UNLESS(str, -1);
    snprintf(str, max_len, "%s=", varname);
    str_len = strlen(str);

    if (vec_len > 1)
        str_len += snprintf(str + str_len, max_len - str_len, "[");
    switch (type) {
        case MPR_INT32:
            for (i = 0; i < vec_len; i++) {
                var_len = snprintf(str + str_len, max_len - str_len, "%d", ((int*)val)[i]);
                str_len += trim_zeros(str + str_len, var_len);
                str_len += snprintf(str + str_len, max_len - str_len, ",");
            }
            break;
        case MPR_FLT:
            for (i = 0; i < vec_len; i++) {
                var_len = snprintf(str + str_len, max_len - str_len, "%g", ((float*)val)[i]);
                str_len += trim_zeros(str + str_len, var_len);
                str_len += snprintf(str + str_len, max_len - str_len, ",");
            }
            break;
        case MPR_DBL:
            for (i = 0; i < vec_len; i++) {
                var_len = snprintf(str + str_len, max_len - str_len, "%g", ((double*)val)[i]);
                str_len += trim_zeros(str + str_len, var_len);
                str_len += snprintf(str + str_len, max_len - str_len, ",");
            }
            break;
        default:
            break;
    }
    --str_len;
    if (vec_len > 1)
        str_len += snprintf(str + str_len, max_len - str_len, "];");
    else
        str_len += snprintf(str + str_len, max_len - str_len, ";");
    return str_len;
}

#define INSERT_VAL(VARNAME)                                         \
mpr_value *ev = m->vars;                                            \
for (j = 0; j < m->num_vars; j++) {                                 \
    /* TODO: handle multiple instances */                           \
    k = 0;                                                          \
    if (strcmp(VARNAME, mpr_expr_get_var_name(m->expr, j)))         \
        continue;                                                   \
    if (mpr_value_get_num_samps(ev[j], k) < 0) {                    \
        trace("expr var '%s' is not yet initialised.\n", VARNAME);  \
        goto abort;                                                 \
    }                                                               \
    len += snprint_var(VARNAME, expr + len, MAX_LEN - len,          \
                       mpr_value_get_vlen(ev[j]),                   \
                       mpr_value_get_type(ev[j]),                   \
                       mpr_value_get_value(ev[j], k, 0));           \
    break;                                                          \
}                                                                   \
if (j == m->num_vars) {                                             \
    trace("expr var '%s' is not found.\n", VARNAME);                \
    goto abort;                                                     \
}

static const char *set_linear(mpr_local_map m, const char *e)
{
    /* if e is NULL, try to fill in ranges from map signals */
    int i, j, k, len = 0, val_len;
    char expr[MAX_LEN] = "";
    char *var = "x";

    /* TODO: check for convergent maps */
    mpr_sig src_0 = mpr_slot_get_sig((mpr_slot)m->src[0]);
    mpr_sig dst = mpr_slot_get_sig((mpr_slot)m->dst);
    int dst_len = mpr_sig_get_len(dst);
    int src_0_len = mpr_sig_get_len(src_0);
    int min_len = mpr_min(src_0_len, dst_len);

    if (e) {
        /* how many instances of 'linear' appear in the expression string? */
        int num_inst = 0;
        char *offset = (char*)e;

        trace("generating linear expression from '%s'\n", e);

        while ((offset = strstr(offset, "linear"))) {
            ++num_inst;
            offset += 6;
        }
        /* for now we will only allow one instance of 'linear' function macro */
        /* TODO: enable multiple instances of the 'linear' function macro */
        if (num_inst != 1) {
            trace("found %d instances of the string 'linear'\n", num_inst);
            return NULL;
        }
        /* use a copy in case we fail after tokenisation */
        e = strdup(e);
        offset = (char*)e;
        for (i = 0; i < num_inst; i++) {
            char* arg_str, *args[5];
            /* TODO: copy sections of expression before 'linear(' */
            offset = strstr(offset, "linear") + 6;
            /* remove any spaces */
            while (*offset && *offset == ' ')
                ++offset;
            /* next character must be '(' */
            if (*offset != '(') {
                goto abort;
            }
            ++offset;
            /* TODO: test with arguments containing parentheses */
            arg_str = strtok(offset, ")");
            if (!arg_str) {
                trace("error: no arguments found for 'linear' function\n");
                goto abort;
            }
            for (j = 0; j < 5; j++) {
                args[j] = strtok(arg_str, ",");
                arg_str = NULL;
                if (!args[j])
                    goto abort;
                while (*args[j] == ' ')
                    ++args[j];
            }
            /* we won't check if ranges are numeric since they could be variables but src extrema
             * are allowed to be "?" to indicate calibration or '-' to indicate they should not
             * be changed */
            if (0 == strcmp(args[1], "?") || 0 == strcmp(args[1], "? "))
                len = snprintf(expr, MAX_LEN, "sMin{-1}=x;sMin=min(%s,sMin);", var);
            else if (0 == strcmp(args[1], "-") || 0 == strcmp(args[1], "- ")) {
                /* try to load sMin variable from existing expression */
                if (!m->expr) {
                    trace("can't retrieve previous expr variable\n");
                    len += snprintf(expr + len, MAX_LEN - len, "sMin=0;");
                }
                else {
                    INSERT_VAL("sMin");
                }
            }
            else {
                val_len = snprintf(expr, MAX_LEN, "sMin=%s", args[1]);
                len += trim_zeros(expr, val_len);
                len += snprintf(expr + len, MAX_LEN - len, ";");
            }

            if (0 == strcmp(args[2], "?") || 0 == strcmp(args[2], "? "))
                len += snprintf(expr + len, MAX_LEN - len, "sMax{-1}=x;sMax=max(%s,sMax);", var);
            else if (0 == strcmp(args[2], "-") || 0 == strcmp(args[2], "- ")) {
                /* try to load sMax variable from existing expression */
                if (!m->expr) {
                    /* TODO: try using signal instead */
                    trace("can't retrieve previous expr var, using default\n");
                    /* TODO: test with vector signals */
                    len += snprintf(expr + len, MAX_LEN - len, "sMax=1;");
                }
                else {
                    INSERT_VAL("sMax");
                }
            }
            else {
                val_len = snprintf(expr + len, MAX_LEN - len, "sMax=%s", args[2]);
                len += trim_zeros(expr + len, val_len);
                len += snprintf(expr + len, MAX_LEN - len, ";");
            }

            if (0 == strcmp(args[3], "-") || 0 == strcmp(args[3], "- ")) {
                /* try to load dMin variable from existing expression */
                if (!m->expr) {
                    trace("can't retrieve previous expr variable\n");
                    len += snprintf(expr + len, MAX_LEN - len, "dMin=0;");
                }
                else {
                    INSERT_VAL("dMin");
                }
            }
            else {
                val_len = snprintf(expr + len, MAX_LEN - len, "dMin=%s", args[3]);
                len += trim_zeros(expr + len, val_len);
                len += snprintf(expr + len, MAX_LEN - len, ";");
            }

            if (0 == strcmp(args[4], "-") || 0 == strcmp(args[4], "- ")) {
                /* try to load dMin variable from existing expression */
                if (!m->expr) {
                    trace("can't retrieve previous expr variable\n");
                    len += snprintf(expr + len, MAX_LEN - len, "dMax=1;");
                }
                else {
                    INSERT_VAL("dMax");
                }
            }
            else {
                val_len = snprintf(expr + len, MAX_LEN - len, "dMax=%s", args[4]);
                len += trim_zeros(expr + len, val_len);
                len += snprintf(expr + len, MAX_LEN - len, ";");
            }

            var = args[0];
            /* TODO: copy sections of expression after closing paren ')' */
        }
    }
    else {
        mpr_type val_type;
        const void *val;
        int cont = 1;

        trace("generating default linear expression\n");

#define print_extremum(SIG, PROPERTY, LABEL)                                                \
        if (cont && mpr_obj_get_prop_by_idx((mpr_obj)SIG, PROPERTY, NULL, &val_len,         \
                                            &val_type, &val, NULL))                         \
            len += snprint_var(LABEL, expr + len, MAX_LEN - len, val_len, val_type, val);   \
        else                                                                                \
            len = cont = 0;

        print_extremum(src_0, MPR_PROP_MIN, "sMin");
        print_extremum(src_0, MPR_PROP_MAX, "sMax");
        print_extremum(dst, MPR_PROP_MIN, "dMin");
        print_extremum(dst, MPR_PROP_MAX, "dMax");

#undef print_extremum
    }
    if (!len) {
        /* try linear combination of inputs */
        if (1 == m->num_src) {
            if (dst_len >= src_0_len)
                snprintf(expr, MAX_LEN, "y=x");
            else {
                /* truncate source */
                if (1 == dst_len)
                    snprintf(expr, MAX_LEN, "y=x[0]");
                else
                    snprintf(expr, MAX_LEN, "y=x[0:%i]", dst_len - 1);
            }
        }
        else {
            /* check vector lengths */
            int i, j;
            len = snprintf(expr, MAX_LEN, "y=(");
            for (i = 0; i < m->num_src; i++) {
                mpr_sig src_i = mpr_slot_get_sig((mpr_slot)m->src[i]);
                int src_i_len = mpr_sig_get_len(src_i);
                if (src_i_len > dst_len) {
                    if (1 == dst_len)
                        len += snprintf(expr + len, MAX_LEN - len, "x$%d[0]+", i);
                    else
                        len += snprintf(expr + len, MAX_LEN - len, "x$%d[0:%d]+", i, dst_len - 1);
                }
                else if (src_i_len < dst_len) {
                    len += snprintf(expr + len, MAX_LEN - len, "[x$%d,0", i);
                    for (j = 1; j < dst_len - src_0_len; j++)
                        len += snprintf(expr + len, MAX_LEN - len, ",0");
                    len += snprintf(expr + len, MAX_LEN - len, "]+");
                }
                else
                    len += snprintf(expr + len, MAX_LEN - len, "x$%d+", i);
            }
            --len;
            snprintf(expr + len, MAX_LEN - len, ")/%d", m->num_src);
        }
        FUNC_IF(free, (char*)e);
        return strdup(expr);
    }

    snprintf(expr + len, MAX_LEN - len,
             "sRange=sMax-sMin;"
             "m=(dMax-dMin)/sRange;"
             "b=dMin;"
             "b=(dMin*sMax-dMax*sMin)/sRange;");

    len = strlen(expr);

    if (dst_len >= src_0_len)
        snprintf(expr + len, MAX_LEN - len, "y=m*%s+b;", var);
    else if (min_len == 1)
        snprintf(expr + len, MAX_LEN - len, "y=m*%s[0]+b;", var);
    else
        snprintf(expr + len, MAX_LEN - len, "y=m*%s[0:%i]+b;", var, min_len - 1);

    trace("linear expression %s requires %d chars\n", expr, (int)strlen(expr));
    FUNC_IF(free, (char*)e);
    return strdup(expr);

abort:
    FUNC_IF(free, (char*)e);
    return NULL;
}

static int set_expr(mpr_local_map m, const char *expr_str)
{
    int i, ret = 0;
    const char *new_expr = 0;
    mpr_sig dst_sig = mpr_slot_get_sig((mpr_slot)m->dst);
    RETURN_ARG_UNLESS(m->num_src > 0, 0);

    trace("setting map expression to '%s'\n", expr_str ? expr_str : "default");

    m->id_map.LID = 0;

    if (!(m->process_loc & m->locality)) {
        /* don't need to compile */
        if (expr_str)
            mpr_tbl_add_record(m->obj.props.synced, MPR_PROP_EXPR, NULL,
                               1, MPR_STR, expr_str, MOD_REMOTE);
        if (m->expr) {
            trace("freeing unused expression\n");
            mpr_expr_free(m->expr);
            m->expr = NULL;
        }
        goto done;
    }

    if (!expr_str || strstr(expr_str, "linear")) {
        expr_str = new_expr = set_linear(m, expr_str);
#ifdef DEBUG
        if (expr_str)
            trace("generated expression '%s'\n", expr_str);
#endif
    }
    RETURN_ARG_UNLESS(expr_str, -1);

    if (!replace_expr_str(m, expr_str)) {
        mpr_time now;
        mpr_map_alloc_values(m, 1);

        /* evaluate expression to initialise literals */
        mpr_time_set(&now, MPR_NOW);
        for (i = 0; i < m->num_inst; i++)
            mpr_expr_eval(m->expr, mpr_graph_get_expr_eval_buffer(m->obj.graph), 0,
                          m->vars, mpr_slot_get_value(m->dst), &now, i);
    }
    else {
        if (!m->expr) {
            /* no previous expression, abort map */
            m->obj.status = MPR_STATUS_EXPIRED;
        }
        /* expression unchanged */
        ret = 1;
        goto done;
    }

    /* Special case: if we are the receiver and the new expression evaluates to
     * a constant we can update immediately. */
    /* TODO: should call handler for all instances updated through this map. */
    if (mpr_expr_get_num_src(m->expr) <= 0 && !m->use_inst && mpr_obj_get_is_local((mpr_obj)dst_sig)) {
        /* call handler if it exists */
        mpr_sig_call_handler((mpr_local_sig)dst_sig, MPR_STATUS_UPDATE_REM, 0, 0, 0);
    }

    /* check whether each source slot causes computation */
    for (i = 0; i < m->num_src; i++)
        mpr_slot_set_causes_update((mpr_slot)m->src[i], !mpr_expr_get_src_is_muted(m->expr, i));
done:
    if (new_expr)
        free((char*)new_expr);
    return ret;
}

int mpr_local_map_update_status(mpr_local_map map)
{
    int i, status = METADATA_OK;
    RETURN_ARG_UNLESS(!(map->obj.status & MPR_MAP_STATUS_READY), map->obj.status);

    trace("checking map status...\n");
    trace("checking slots...\n");
    for (i = 0; i < map->num_src; i++) {
        trace("  src[%d]: ", i);
        status &= mpr_slot_get_status(map->src[i]);
    }
    trace("  dst:    ");
    status &= mpr_slot_get_status(map->dst);

    if (status == METADATA_OK) {
        mpr_tbl tbl = mpr_obj_get_prop_tbl((mpr_obj)map);
        mpr_sig sig;
        int use_inst;

        trace("  map metadata OK, status is now READY\n");
        mpr_map_alloc_values(map, 1);
        set_expr(map, map->expr_str);
        map->obj.status |= MPR_MAP_STATUS_READY;

        /* add map to signals */
        sig = mpr_slot_get_sig((mpr_slot)map->dst);
        use_inst = mpr_sig_get_use_inst(sig);
        if (mpr_obj_get_is_local((mpr_obj)sig))
            mpr_local_sig_add_slot((mpr_local_sig)sig, map->dst, MPR_DIR_IN);
        for (i = 0; i < map->num_src; i++) {
            sig = mpr_slot_get_sig((mpr_slot)map->src[i]);
            use_inst |= mpr_sig_get_use_inst(sig);
            if (mpr_obj_get_is_local((mpr_obj)sig))
                mpr_local_sig_add_slot((mpr_local_sig)sig, map->src[i], MPR_DIR_OUT);
        }

        if (!mpr_tbl_get_prop_is_set(tbl, MPR_PROP_USE_INST)) {
            /* default to using instanced maps if any of the contributing signals are instanced */
            map->use_inst = use_inst;
            mpr_tbl_set_prop_is_set(tbl, MPR_PROP_USE_INST);
        }

        if (MPR_LOC_BOTH != map->locality && !mpr_tbl_get_prop_is_set(tbl, MPR_PROP_PROTOCOL)) {
            map->protocol = use_inst ? MPR_PROTO_TCP : MPR_PROTO_UDP;
            mpr_tbl_set_prop_is_set(tbl, MPR_PROP_PROTOCOL);
        }
    }
    return map->obj.status;
}

/* TODO: currently we could send 2 modify messages (process at src; process at dst) at the same time
 * and they would just keep switching back and forth
 * We need a more deterministic way of doing map admin, e.g. always dst?
 * Move mapto handler to mesh only to avoid user spam? */

int mpr_local_map_set_from_msg(mpr_local_map m, mpr_msg msg)
{
    int updated = 0;
    mpr_loc orig_loc = m->process_loc;
    const char *expr_str = mpr_msg_get_prop_as_str(msg, MPR_PROP_EXPR);

    if (MPR_LOC_BOTH == m->locality) {
        m->process_loc = MPR_LOC_SRC;
    }
    else if (!m->one_src) {
        /* Processing must take place at destination if map
         * includes source signals from different devices. */
        m->process_loc = MPR_LOC_DST;
    }
    else if (expr_str && strstr(expr_str, "y{-")) {
        /* Processing must take place at destination if map expression uses destination history */
        m->process_loc = MPR_LOC_DST;
    }
    else {
        /* try to retrieve process location property from message */
        const char *loc_str = mpr_msg_get_prop_as_str(msg, MPR_PROP_PROCESS_LOC);
        if (loc_str) {
            int new_loc = mpr_loc_from_str(loc_str);
            if (MPR_LOC_UNDEFINED != new_loc)
                m->process_loc = new_loc;
        }
        /* processing location must be either SRC or DST */
        if (m->process_loc != MPR_LOC_SRC && m->process_loc != MPR_LOC_DST)
            m->process_loc = orig_loc;
    }

    if ((expr_str || m->process_loc != orig_loc) && m->obj.status & MPR_MAP_STATUS_READY) {
        int e = set_expr(m, expr_str);
        if (-1 == e) {
            /* restore original process location */
            m->process_loc = orig_loc;
        }
        else if (0 == e)
            ++updated;
    }
    else if (expr_str)
        updated += mpr_tbl_add_record(m->obj.props.synced, MPR_PROP_EXPR, NULL,
                                      1, MPR_STR, expr_str, MOD_REMOTE);

    if (orig_loc != m->process_loc)
        ++updated;

    if (m->obj.status & (MPR_STATUS_REMOVED | MPR_STATUS_EXPIRED)) {
        m->obj.status &= ~(MPR_STATUS_REMOVED | MPR_STATUS_EXPIRED);
        ++updated;
    }
    return updated;
}

/* TODO: consider renaming mpr_N_set_from_msg() to mpr_N_update() since this func also updates status */
int mpr_map_set_from_msg(mpr_map m, mpr_msg msg)
{
    int i, j, updated = 0;
    mpr_tbl tbl = m->obj.props.synced;

    if (!msg)
        goto done;

    if (MPR_DIR_OUT == mpr_slot_get_dir(m->dst)) {
        /* check if MPR_PROP_SLOT property is defined */
        mpr_msg_atom a = mpr_msg_get_prop(msg, MPR_PROP_SLOT);
        if (a && mpr_msg_atom_get_len(a) == m->num_src) {
            lo_arg **vals = mpr_msg_atom_get_values(a);
            for (i = 0; i < m->num_src; i++)
                mpr_slot_set_id(m->src[i], (vals[i])->i32);
        }
    }

    /* set destination slot properties */
    updated += mpr_slot_set_from_msg(m->dst, msg);

    /* set source slot properties */
    for (i = 0; i < m->num_src; i++)
        updated += mpr_slot_set_from_msg(m->src[i], msg);

    if (m->obj.is_local) {
        /* need to handle some properties carefully since they impact expression hosting */
        updated += mpr_local_map_set_from_msg((mpr_local_map)m, msg);
    }

    for (i = 0; i < mpr_msg_get_num_atoms(msg); i++) {
        mpr_msg_atom a = mpr_msg_get_atom(msg, i);
        mpr_prop prop = mpr_msg_atom_get_prop(a);
        const mpr_type *types = mpr_msg_atom_get_types(a);
        lo_arg **vals = mpr_msg_atom_get_values(a);

        if (prop & ~0xFFFF) {
            trace("ignoring slot prop '%s'\n", mpr_msg_atom_get_key(a));
            continue;
        }

        switch (MASK_PROP_BITFLAGS(prop)) {
            case MPR_PROP_NUM_SIGS_IN:
            case MPR_PROP_NUM_SIGS_OUT:
                /* these properties will be set by signal args */
                break;
            case MPR_PROP_STATUS:
                if (!m->obj.is_local)
                    updated += mpr_tbl_add_record_from_msg_atom(tbl, a, MOD_REMOTE);
                break;
            case MPR_PROP_PROCESS_LOC: {
                if (!m->obj.is_local) {
                    mpr_loc loc = mpr_loc_from_str(&(vals[0])->s);
                    if (MPR_LOC_UNDEFINED != loc)
                        updated += mpr_tbl_add_record(tbl, MPR_PROP_PROCESS_LOC, NULL, 1,
                                                      MPR_INT32, &loc, MOD_REMOTE);
                }
                break;
            }
            case MPR_PROP_EXPR: {
                if (!m->obj.is_local) {
                    const char *expr_str = &(vals[0])->s;
                    updated += mpr_tbl_add_record(tbl, MPR_PROP_EXPR, NULL, 1, MPR_STR,
                                                  expr_str, MOD_REMOTE);
                }
                break;
            }
            case MPR_PROP_SCOPE:
                if (types && mpr_type_get_is_str(types[0]))
                    updated += update_scope(m, a);
                break;
            case MPR_PROP_SCOPE | PROP_ADD:
                for (j = 0; j < mpr_msg_atom_get_len(a); j++) {
                    if (types && MPR_STR == types[j])
                        updated += add_scope(m, &(vals[j])->s);
                }
                break;
            case MPR_PROP_SCOPE | PROP_REMOVE:
                for (j = 0; j < mpr_msg_atom_get_len(a); j++) {
                    if (types && MPR_STR == types[j])
                        updated += remove_scope(m, &(vals[j])->s);
                }
                break;
            case MPR_PROP_PROTOCOL: {
                mpr_proto pro;
                if (mpr_obj_get_is_local((mpr_obj)m) && MPR_LOC_BOTH == ((mpr_local_map)m)->locality)
                    break;
                pro = mpr_proto_from_str(&(vals[0])->s);
                if (pro != MPR_PROTO_UNDEFINED)
                    updated += mpr_tbl_add_record(tbl, MPR_PROP_PROTOCOL, NULL, 1, MPR_INT32, &pro,
                                                  MOD_REMOTE);
                break;
            }
            case MPR_PROP_USE_INST: {
                int use_inst;
                if (types[0] == 's') {
                    const char *str = &(vals[0])->s;
                    if (!strchr("TFtf", str[0]))
                        break;
                    if (strlen(str) == 1)
                        use_inst = str[0] == 'T' || str[0] == 't';
                    else if (strcmp(str + 1, "rue") == 0)
                        use_inst = 1;
                    else if (strcmp(str + 1, "alse") == 0)
                        use_inst = 0;
                    else
                        break;
                }
                else
                    use_inst = types[0] == 'T';
                if (m->obj.is_local && m->use_inst && !use_inst) {
                    /* TODO: release map instances */
                }
                updated += mpr_tbl_add_record(tbl, MPR_PROP_USE_INST, NULL, 1, MPR_BOOL,
                                              &use_inst, MOD_REMOTE);
                break;
            }
            case MPR_PROP_EXTRA: {
                const char *key = mpr_msg_atom_get_key(a);
                if (strcmp(key, "expression")==0) {
                    if (mpr_type_get_is_str(types[0])) {
                        /* set property type to expr and repeat */
                        mpr_msg_atom_set_prop(a, MPR_PROP_EXPR);
                        --i;
                    }
                }
                else if (strncmp(key, "var@", 4)==0) {
                    if (m->obj.is_local && ((mpr_local_map)m)->expr) {
                        mpr_local_map lm = (mpr_local_map)m;
                        const char *name;
                        int k = 0, l, var_len;
                        for (j = 0; j < lm->num_vars; j++) {
                            /* check if matches existing varname */
                            name = mpr_expr_get_var_name(lm->expr, j);
                            if (strcmp(name, key+4)!=0)
                                continue;
                            /* found variable */
                            ++updated;
                            /* TODO: handle multiple instances */
                            var_len = mpr_value_get_vlen(lm->vars[j]);
                            /* cast if necessary */
                            switch (mpr_value_get_type(lm->vars[j])) {
#define TYPED_CASE(MTYPE, TYPE, MTYPE1, EL1, MTYPE2, EL2)                           \
                                case MTYPE: {                                       \
                                    TYPE *v = mpr_value_get_value(lm->vars[j], k, 0);\
                                    for (k = 0, l = 0; k < var_len; k++, l++) {     \
                                        if (l >= mpr_msg_atom_get_len(a))           \
                                            l = 0;                                  \
                                        switch (types[l]) {                         \
                                            case MTYPE1:                            \
                                                v[k] = (TYPE)vals[l]->EL1;          \
                                                break;                              \
                                            case MTYPE2:                            \
                                                v[k] = (TYPE)vals[l]->EL2;          \
                                                break;                              \
                                            default:                                \
                                                --updated;                          \
                                                k = var_len;                        \
                                        }                                           \
                                    }                                               \
                                    break;                                          \
                                }
                                TYPED_CASE(MPR_INT32, int, MPR_FLT, f, MPR_DBL, d)
                                TYPED_CASE(MPR_FLT, float, MPR_INT32, i, MPR_DBL, d)
                                TYPED_CASE(MPR_DBL, double, MPR_INT32, i, MPR_FLT, f)
#undef TYPED_CASE
                                default:
                                    --updated;
                            }
                            mpr_expr_set_var_updated(lm->expr, j);
                            break;
                        }
                    }
                    if (m->obj.is_local)
                        break;
                    /* otherwise continue to mpr_tbl_add_record_from_msg_atom() below */
                }
            }
            case MPR_PROP_ID:
            case MPR_PROP_MUTED:
            case MPR_PROP_VERSION:
                updated += mpr_tbl_add_record_from_msg_atom(tbl, a, MOD_REMOTE);
                break;
            default:
                break;
        }
    }
done:
    if (m->obj.is_local && !(m->obj.status & MPR_MAP_STATUS_READY)) {
        /* check if mapping is now "ready" */
        mpr_local_map_update_status((mpr_local_map)m);
    }
    trace("updated %d map properties.\n", updated);
    if (updated)
        m->obj.status |= MPR_STATUS_MODIFIED;
    return updated;
}

/* If the "slot_idx" argument is >= 0, we can assume this message will be sent
 * to a peer device rather than a session manager. */
int mpr_map_send_state(mpr_map m, int slot_idx, net_msg_t cmd, int version)
{
    lo_message msg;
    char buffer[256];
    int i, staged;
    mpr_link link;
    mpr_dir dst_dir = mpr_slot_get_dir(m->dst);

    if (MSG_MAPPED == cmd && !(m->obj.status & MPR_MAP_STATUS_READY))
        return slot_idx;
    msg = lo_message_new();
    if (!msg) {
        trace("couldn't allocate lo_message\n");
        return slot_idx;
    }

    /* Update status to indicate map has been pushed */
    m->obj.status |= MPR_MAP_STATUS_PUSHED;

    if (MPR_DIR_IN == dst_dir) {
        /* add mapping destination */
        mpr_sig_get_full_name(mpr_slot_get_sig(m->dst), buffer, 256);
        lo_message_add_string(msg, buffer);
        lo_message_add_string(msg, "<-");
    }

    if (mpr_obj_get_is_local((mpr_obj)m) && ((mpr_local_map)m)->one_src)
        slot_idx = -1;

    /* add mapping sources */
    i = (slot_idx >= 0) ? slot_idx : 0;
    link = mpr_slot_get_link(m->src[i]);
    for (; i < m->num_src; i++) {
        if ((slot_idx >= 0) && link && (link != mpr_slot_get_link(m->src[i])))
            break;
        mpr_sig_get_full_name(mpr_slot_get_sig(m->src[i]), buffer, 256);
        lo_message_add_string(msg, buffer);
    }

    if (MPR_DIR_OUT == dst_dir || !dst_dir) {
        /* add mapping destination */
        mpr_sig_get_full_name(mpr_slot_get_sig(m->dst), buffer, 256);
        lo_message_add_string(msg, "->");
        lo_message_add_string(msg, buffer);
    }

    /* Add unique id */
    if (m->obj.id) {
        lo_message_add_string(msg, mpr_prop_as_str(MPR_PROP_ID, 0));
        lo_message_add_int64(msg, *((int64_t*)&m->obj.id));
    }

    if (MSG_UNMAP == cmd || MSG_UNMAPPED == cmd) {
        lo_message_add_string(msg, mpr_prop_as_str(MPR_PROP_VERSION, 0));
        lo_message_add_int32(msg, version ? version : m->obj.version);
        mpr_net_add_msg(mpr_graph_get_net(m->obj.graph), 0, cmd, msg);
        return i-1;
    }

    /* add other properties */
    staged = (MSG_MAP == cmd) || (MSG_MAP_MOD == cmd);
    mpr_obj_add_props_to_msg((mpr_obj)m, msg);

    /* add slot id */
    if (MPR_DIR_IN == dst_dir && (   MSG_MAP_TO == cmd
                                  || ((!staged && !(m->obj.status & MPR_MAP_STATUS_READY))))) {
        lo_message_add_string(msg, mpr_prop_as_str(MPR_PROP_SLOT, 0));
        i = (slot_idx >= 0) ? slot_idx : 0;
        link = mpr_slot_get_is_local(m->src[i]) ? mpr_slot_get_link(m->src[i]) : 0;
        for (; i < m->num_src; i++) {
            if ((slot_idx >= 0) && link && (link != mpr_slot_get_link(m->src[i])))
                break;
            lo_message_add_int32(msg, mpr_slot_get_id(m->src[i]));
        }
    }

    /* source properties */
    i = (slot_idx >= 0) ? slot_idx : 0;
    link = mpr_slot_get_is_local(m->src[i]) ? mpr_slot_get_link(m->src[i]) : 0;
    for (; i < m->num_src; i++) {
        if ((slot_idx >= 0) && link && (link != mpr_slot_get_link(m->src[i])))
            break;
        if (MSG_MAPPED == cmd || (MPR_DIR_OUT == dst_dir))
            mpr_slot_add_props_to_msg(msg, m->src[i], 0);
    }

    /* destination properties */
    if (MSG_MAPPED == cmd || (MPR_DIR_IN == dst_dir))
        mpr_slot_add_props_to_msg(msg, m->dst, 1);

    /* add public expression variables */
    if (m->obj.is_local && ((mpr_local_map)m)->expr) {
        mpr_local_map lm = (mpr_local_map)m;
        int j, k;
        char varname[32];
        for (j = 0; j < lm->num_vars; j++) {
            /* TODO: handle multiple instances */
            k = 0;
            if (mpr_value_get_num_samps(lm->vars[j], k) >= 0) {
                snprintf(varname, 32, "@var@%s", mpr_expr_get_var_name(lm->expr, j));
                lo_message_add_string(msg, varname);
                mpr_value_add_to_msg(lm->vars[j], k, msg);
            }
            else {
                trace("public expression variable '%s' is not yet initialised.\n",
                      mpr_expr_get_var_name(lm->expr, j));
            }
        }
        if (lm->num_old_vars && lm->old_var_names) {
            for (j = 0; j < lm->num_old_vars; j++) {
                if (lm->old_var_names[j]) {
                    snprintf(varname, 32, "-@var@%s", lm->old_var_names[j]);
                    lo_message_add_string(msg, varname);
                }
            }
        }
    }
    mpr_net_add_msg(mpr_graph_get_net(m->obj.graph), 0, cmd, msg);
    return i-1;
}

void mpr_map_clear_empty_props(mpr_local_map map)
{
    mpr_tbl_clear_empty_records(map->obj.props.synced);
    if (map->old_var_names) {
        int i;
        for (i = 0; i < map->num_old_vars; i++) {
            FUNC_IF(free, (void*)map->old_var_names[i]);
        }
        free(map->old_var_names);
        map->old_var_names = NULL;
    }
    map->num_old_vars = 0;
}

mpr_map mpr_map_new_from_str(const char *expr, ...)
{
    mpr_sig sig, srcs[MAX_NUM_MAP_SRC];
    mpr_sig dst = NULL;
    mpr_map map = NULL;
    int i = 0, j, num_src = 0, in_refs = 0;
    char *new_expr;
    va_list aq;
    RETURN_ARG_UNLESS(expr, 0);
    va_start(aq, expr);
    while (expr[i]) {
        while (expr[i] && expr[i] != '%')
            ++i;
        if (!expr[i])
            break;
        switch (expr[i+1]) {
            case 'y':
                sig = va_arg(aq, void*);
                if (!sig) {
                    trace("Format string '%s' is missing destination signal.\n", expr);
                    goto error;
                }
                if (!dst)
                    dst = sig;
                else if (sig != dst) {
                    trace("Format string '%s' references more than one destination signal.\n", expr);
                    goto error;
                }
                break;
            case 'x':
                sig = va_arg(aq, void*);
                if (!sig) {
                    trace("Format string '%s' is missing source signal.\n", expr);
                    goto error;
                }
                for (j = 0; j < num_src; j++) {
                    if (sig == srcs[j])
                        break;
                }
                if (j == num_src) {
                    if (num_src >= MAX_NUM_MAP_SRC) {
                        trace("Maps cannot have more than %d source signals.\n", MAX_NUM_MAP_SRC);
                        goto error;
                    }
                    srcs[num_src++] = sig;
                }
                ++in_refs;
                break;
            default:
                trace("Illegal format token '%%%c' in mpr_map_new_from_str().\n", expr[i+1]);
                goto error;
        }
        i += 2;
    }
    va_end(aq);

    TRACE_RETURN_UNLESS(dst, NULL, "Map format string '%s' has no output signal!\n", expr);
    TRACE_RETURN_UNLESS(num_src, NULL, "Map format string '%s' has no input signals!\n", expr);

    /* create the map */
    map = mpr_map_new(num_src, srcs, 1, &dst);

    /* edit the expression string in-place */
    i = j = 0;
    new_expr = calloc(1, strlen(expr) + in_refs + 1);
    va_start(aq, expr);
    while (expr[i]) {
        while (expr[i] && expr[i] != '%')
            new_expr[j++] = expr[i++];
        if (!expr[i])
            break;
        sig = va_arg(aq, void*);
        if (expr[i+1] == 'y') {
            /* replace the preceding '%' with a space */
            new_expr[j++] = 'y';
        }
        else {  /* 'x' */
            /* replace "%x" with "x$i" where i is the signal index */
            new_expr[j++] = 'x';
            new_expr[j++] = '$';
            new_expr[j++] = "0123456789"[mpr_map_get_sig_idx(map, sig) % 10];
        }
        i += 2;
    }
    va_end(aq);
    mpr_obj_set_prop((mpr_obj)map, MPR_PROP_EXPR, NULL, 1, MPR_STR, new_expr, 1);
    free(new_expr);
    return map;

error:
    va_end(aq);
    return NULL;
}

static int compare_slot_names(const void *l, const void *r)
{
    return mpr_slot_compare_names(*(mpr_slot*)l, *(mpr_slot*)r);
}

void mpr_map_add_src(mpr_map map, mpr_sig sig, mpr_dir dir, int is_local)
{
    int i;
    ++map->num_src;
    map->src = realloc(map->src, sizeof(mpr_slot) * map->num_src);
    map->src[map->num_src - 1] = mpr_slot_new(map, sig, dir, is_local, 1);

    /* slots should be in alphabetical order */
    qsort(map->src, map->num_src, sizeof(mpr_slot), compare_slot_names);

    /* fix slot ids */
    for (i = 0; i < map->num_src; i++)
        mpr_slot_set_id(map->src[i], i);
}

int mpr_map_compare(mpr_map l, mpr_map r)
{
    int i;
    if (   mpr_obj_get_id((mpr_obj)r) != 0
        || l->num_src != r->num_src
        || mpr_slot_get_sig(l->dst) != mpr_slot_get_sig(r->dst))
        return 0;
    for (i = 0; i < l->num_src; i++) {
        if (mpr_slot_get_sig(l->src[i]) != mpr_slot_get_sig(r->src[i]))
            return 0;
    }
    return 1;
}

int mpr_map_compare_names(mpr_map map, int num_src, const char **srcs, const char *dst)
{
    if (map->num_src != num_src || mpr_slot_match_full_name(map->dst, dst))
        return 0;
    else {
        int i;
        for (i = 0; i < num_src; i++) {
            if (mpr_slot_match_full_name(map->src[i], srcs[i]))
                return 0;
        }
    }
    return 1;
}

int mpr_map_get_has_dev(mpr_map map, mpr_id dev_id, mpr_dir dir)
{
    if (dir == MPR_DIR_BOTH) {
        mpr_sig sig = mpr_slot_get_sig(map->dst);
        mpr_dev dev = mpr_sig_get_dev(sig);
        int i;
        RETURN_ARG_UNLESS(mpr_obj_get_id((mpr_obj)dev) == dev_id, 0);
        for (i = 0; i < map->num_src; i++) {
            sig = mpr_slot_get_sig(map->src[i]);
            dev = mpr_sig_get_dev(sig);
            RETURN_ARG_UNLESS(mpr_obj_get_id((mpr_obj)dev) == dev_id, 0);
        }
        return 1;
    }
    if (dir & MPR_DIR_OUT) {
        int i;
        for (i = 0; i < map->num_src; i++) {
            mpr_sig sig = mpr_slot_get_sig(map->src[i]);
            mpr_dev dev = mpr_sig_get_dev(sig);
            RETURN_ARG_UNLESS(mpr_obj_get_id((mpr_obj)dev) != dev_id, 1);
        }
    }
    if (dir & MPR_DIR_IN) {
        mpr_sig sig = mpr_slot_get_sig(map->dst);
        mpr_dev dev = mpr_sig_get_dev(sig);
        RETURN_ARG_UNLESS(mpr_obj_get_id((mpr_obj)dev) != dev_id, 1);
    }
    return 0;
}

int mpr_map_get_has_link_id(mpr_map map, mpr_id link_id)
{
    int i;
    for (i = 0; i < map->num_src; i++) {
        mpr_link link = mpr_slot_get_link(map->src[i]);
        if (link && mpr_obj_get_id((mpr_obj)link) == link_id)
            return 1;
    }
    return 0;
}

int mpr_map_get_locality(mpr_map map)
{
    return mpr_obj_get_is_local((mpr_obj)map) ? ((mpr_local_map)map)->locality : 0;
}

int mpr_map_get_has_sig(mpr_map map, mpr_sig sig, mpr_dir dir)
{
    if (!dir || (dir & MPR_DIR_OUT)) {
        int i;
        for (i = 0; i < map->num_src; i++) {
            if (mpr_slot_get_sig(map->src[i]) == sig)
                return 1;
        }
    }
    if (!dir || (dir & MPR_DIR_IN)) {
        if (mpr_slot_get_sig(map->dst) == sig)
            return 1;
    }
    return 0;
}

mpr_sig mpr_map_get_dst_sig(mpr_map map)
{
    return mpr_slot_get_sig(map->dst);
}

mpr_slot mpr_map_get_dst_slot(mpr_map map)
{
    return map->dst;
}

mpr_expr mpr_local_map_get_expr(mpr_local_map map)
{
    return map->expr;
}

const char *mpr_map_get_expr_str(mpr_map map)
{
    return map->expr_str;
}

int mpr_local_map_get_has_scope(mpr_local_map map, mpr_id id)
{
    int i;
    id &= 0xFFFFFFFF00000000; /* interested in device hash part only */
    for (i = 0; i < map->num_scopes; i++) {
        if (map->scopes[i] == 0 || mpr_obj_get_id((mpr_obj)map->scopes[i]) == id)
            return 1;
    }
    return 0;
}

int mpr_local_map_get_is_one_src(mpr_local_map map)
{
    return map->one_src;
}

int mpr_local_map_get_num_inst(mpr_local_map map)
{
    return map->num_inst;
}

int mpr_map_get_num_src(mpr_map map)
{
    return map->num_src;
}

mpr_loc mpr_map_get_process_loc(mpr_map map)
{
    return map->process_loc;
}

mpr_loc mpr_local_map_get_process_loc_from_msg(mpr_local_map map, mpr_msg msg)
{
    mpr_loc loc = mpr_map_get_process_loc((mpr_map)map);
    if (!mpr_local_map_get_is_one_src(map)) {
        /* if map has sources from different remote devices, processing must
         * occur at the destination. */
        loc = MPR_LOC_DST;
    }
    else if (msg) {
        const char *str;
        if ((str = mpr_msg_get_prop_as_str(msg, MPR_PROP_PROCESS_LOC))) {
            int new_loc = mpr_loc_from_str(str);
            if (MPR_LOC_UNDEFINED != new_loc)
                loc = new_loc;
        }
        if (   (str = mpr_msg_get_prop_as_str(msg, MPR_PROP_EXPR))
            || (str = mpr_map_get_expr_str((mpr_map)map))) {
            if (strstr(str, "y{-"))
                loc = MPR_LOC_DST;
        }
    }
    return loc;
}

mpr_proto mpr_map_get_protocol(mpr_map map)
{
    if (mpr_obj_get_is_local((mpr_obj)map) && ((mpr_local_map)map)->locality == MPR_LOC_BOTH)
        return MPR_PROTO_UDP; /* TODO: add MPR_PROTO_NONE? */
    return map->protocol;
}

mpr_sig mpr_map_get_src_sig(mpr_map map, int idx)
{
    assert(idx < MAX_NUM_MAP_SRC);
    return mpr_slot_get_sig(map->src[idx]);
}

mpr_slot mpr_map_get_src_slot(mpr_map map, int idx)
{
    assert(idx < MAX_NUM_MAP_SRC);
    return map->src[idx];
}

mpr_slot mpr_map_get_src_slot_by_id(mpr_map map, int id)
{
    int i;
    for (i = 0; i < map->num_src; i++) {
        mpr_slot slot = map->src[i];
        if (mpr_slot_get_id(slot) == id)
            return slot;
    }
    return 0;
}

void mpr_local_map_set_updated(mpr_local_map map, int inst_idx)
{
    if (inst_idx < 0)
        mpr_bitflags_set_all(map->updated_inst);
    else
        mpr_bitflags_set(map->updated_inst, inst_idx);
    map->updated = 1;
}

int mpr_map_get_use_inst(mpr_map map)
{
    return map->use_inst;
}

void mpr_map_status_decr(mpr_map map)
{
    /* MPR_MAP_STATUS_READY acts as a 2 bit timeout */
    if (map->obj.status & MPR_MAP_STATUS_READY)
        map->obj.status &= (map->obj.status >> 1 | 0x3FFF);
    else
        map->obj.status |= MPR_STATUS_EXPIRED;
}

mpr_id_map mpr_local_map_get_id_map(mpr_local_map map)
{
    return &map->id_map;
}
