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
#include "list.h"
#include "map.h"
#include "mpr_signal.h"
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

#define MAX_LEN 1024
#define METADATA_OK             0x1C

#define MPR_MAP_STRUCT_ITEMS                                                    \
    mpr_obj_t obj;                  /* Must be first */                         \
    mpr_dev *scopes;                                                            \
    char *expr_str;                                                             \
    int muted;                      /*!< 1 to mute mapping, 0 to unmute */      \
    int num_scopes;                                                             \
    int num_src;                                                                \
    mpr_loc process_loc;                                                        \
    int status;                                                                 \
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

    mpr_id_map id_map;              /*!< Associated mpr_id_map. */

    mpr_expr expr;                  /*!< The mapping expression. */
    char *updated_inst;             /*!< Bitflags to indicate updated instances. */
    mpr_value_t *vars;              /*!< User variables values. */
    const char **var_names;         /*!< User variables names. */
    int num_vars;                   /*!< Number of user variables. */
    int num_inst;                   /*!< Number of local instances. */

    uint8_t locality;
    uint8_t one_src;
    uint8_t updated;
} mpr_local_map_t;

MPR_INLINE static int mpr_max(int a, int b) { return a > b ? a : b; }

MPR_INLINE static int mpr_min(int a, int b) { return a < b ? a : b; }

static int _cmp_qry_scopes(const void *ctx, mpr_dev d)
{
    int i;
    mpr_map m = *(mpr_map*)ctx;
    for (i = 0; i < m->num_scopes; i++) {
        if (!m->scopes[i] || mpr_obj_get_id((mpr_obj)m->scopes[i]) == mpr_obj_get_id((mpr_obj)d))
            return 1;
    }
    return 0;
}

void mpr_local_map_init(mpr_local_map map)
{
    int i, scope_count, local_src = 0, local_dst = 0;
    mpr_sig dst_sig = mpr_slot_get_sig((mpr_slot)map->dst);
    mpr_dev dst_dev = mpr_sig_get_dev(dst_sig);

    mpr_obj_set_is_local((mpr_obj)map, 1);

    map->locality = 0;
    for (i = 0; i < map->num_src; i++) {
        mpr_sig src_sig = mpr_slot_get_sig((mpr_slot)map->src[i]);
        mpr_dev src_dev = mpr_sig_get_dev(src_sig);
        if (mpr_obj_get_is_local((mpr_obj)mpr_slot_get_sig((mpr_slot)map->src[i]))) {
            mpr_link link = mpr_link_new((mpr_local_dev)src_dev, dst_dev);
            mpr_local_slot_set_link(map->src[i], link);
            mpr_local_slot_set_link(map->dst, link);
            ++local_src;
            map->locality |= MPR_LOC_SRC;
        }
        else
            mpr_local_slot_set_link(map->src[i], mpr_link_new((mpr_local_dev)dst_dev, src_dev));
    }
    if (mpr_slot_get_sig_if_local((mpr_slot)map->dst)) {
        local_dst = 1;
        map->locality |= MPR_LOC_DST;
    }

    /* TODO: configure number of instances available for each slot */
    map->num_inst = 0;

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
    }

    /* default to processing at source device unless heterogeneous sources */
    map->process_loc = (MPR_LOC_BOTH == map->locality || map->one_src) ? MPR_LOC_SRC : MPR_LOC_DST;
}

void mpr_map_init(mpr_map m, int num_src, mpr_sig *src, mpr_sig dst, int is_local)
{
    int i;
    mpr_graph g = m->obj.graph;
    mpr_tbl t = m->obj.props.synced = mpr_tbl_new();
    mpr_list q = mpr_graph_new_query(m->obj.graph, 0, MPR_DEV, (void*)_cmp_qry_scopes, "v", &m);
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

    /* these properties need to be added in alphabetical order */
    mpr_tbl_link_value(t, PROP(BUNDLE), 1, MPR_INT32, &m->bundle, MODIFIABLE);
    mpr_tbl_link_value(t, PROP(DATA), 1, MPR_PTR, &m->obj.data,
                       MODIFIABLE | INDIRECT | LOCAL_ACCESS_ONLY);
    mpr_tbl_link_value(t, PROP(EXPR), 1, MPR_STR, &m->expr_str, MODIFIABLE | INDIRECT);
    mpr_tbl_link_value(t, PROP(ID), 1, MPR_INT64, &m->obj.id, NON_MODIFIABLE | LOCAL_ACCESS_ONLY);
    mpr_tbl_link_value(t, PROP(MUTED), 1, MPR_BOOL, &m->muted, MODIFIABLE);
    mpr_tbl_link_value(t, PROP(NUM_SIGS_IN), 1, MPR_INT32, &m->num_src, NON_MODIFIABLE);
    mpr_tbl_link_value(t, PROP(PROCESS_LOC), 1, MPR_INT32, &m->process_loc, MODIFIABLE);
    mpr_tbl_link_value_no_default(t, PROP(PROTOCOL), 1, MPR_INT32, &m->protocol, REMOTE_MODIFY);
    mpr_tbl_link_value(t, PROP(SCOPE), 1, MPR_LIST, q, NON_MODIFIABLE | PROP_OWNED);
    mpr_tbl_link_value(t, PROP(STATUS), 1, MPR_INT32, &m->status, NON_MODIFIABLE);
    mpr_tbl_link_value_no_default(t, PROP(USE_INST), 1, MPR_BOOL, &m->use_inst, REMOTE_MODIFY);
    mpr_tbl_link_value(t, PROP(VERSION), 1, MPR_INT32, &m->obj.version, REMOTE_MODIFY);

    mpr_tbl_add_record(t, PROP(IS_LOCAL), NULL, 1, MPR_BOOL, &is_local,
                       LOCAL_ACCESS_ONLY | NON_MODIFIABLE);
    m->status = MPR_STATUS_STAGED;

    if (is_local)
        mpr_local_map_init((mpr_local_map)m);
}

int _compare_sig_names(const void *l, const void *r)
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
                return m;
            }
            maps = mpr_list_get_next(maps);
        }
    }

    m = (mpr_map)mpr_graph_add_list_item(g, MPR_MAP, is_local ? sizeof(mpr_local_map_t) : sizeof(mpr_map_t));
    m->obj.is_local = 0;
    m->bundle = 1;

    /* Sort the source signals by name */
    src_sorted = (mpr_sig*) malloc(num_src * sizeof(mpr_sig));
    memcpy(src_sorted, src, num_src * sizeof(mpr_sig));
    qsort(src_sorted, num_src, sizeof(mpr_sig), _compare_sig_names);

    /* we need to give the map a temporary id – this may be overwritten later */
    if (mpr_obj_get_is_local((mpr_obj)*dst))
        mpr_obj_set_id((mpr_obj)m, mpr_dev_generate_unique_id(mpr_sig_get_dev(*dst)));

    mpr_map_init(m, num_src, src_sorted, *dst, is_local);
    m->protocol = MPR_PROTO_UDP;
    free(src_sorted);
    return m;
}

/* TODO: if map is local handle locally and don't send unmap to network bus */
void mpr_map_release(mpr_map m)
{
    mpr_net_use_bus(mpr_graph_get_net(m->obj.graph));
    mpr_map_send_state(m, -1, MSG_UNMAP);
}

void mpr_map_refresh(mpr_map m)
{
    RETURN_UNLESS(m);
    mpr_net_use_bus(mpr_graph_get_net(m->obj.graph));
    mpr_map_send_state(m, -1, m->obj.is_local ? MSG_MAP_TO : MSG_MAP);
}

void mpr_map_free(mpr_map map)
{
    int i;
    mpr_link link;

    if (map->obj.is_local) {
        mpr_local_map lmap = (mpr_local_map)map;

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

        if (lmap->id_map) {
            /* release map-generated instances */
            lo_message msg = mpr_map_build_msg(lmap, 0, 0, 0, lmap->id_map);
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
                mpr_local_slot_send_msg(lmap->dst, msg, time, MPR_PROTO_TCP,
                                        mpr_local_dev_get_bundle_idx(dev));
                mpr_local_dev_set_sending(dev);
            }

            /* TODO: is this the same as (MPR_LOC_SRC | lmap->locality)? */
            if (MPR_DIR_OUT == mpr_slot_get_dir(map->dst) || MPR_LOC_BOTH == lmap->locality) {
                /* TODO: link main id_map from graph? */
                mpr_sig sig;
                for (i = 0; i < map->num_src; i++) {
                    sig = mpr_slot_get_sig(map->src[i]);
                    if (mpr_obj_get_is_local((mpr_obj)sig)) {
                        mpr_dev_LID_decref((mpr_local_dev)mpr_sig_get_dev(sig), 0, lmap->id_map);
                        break;
                    }
                }
            }
        }

        /* free buffers associated with user-defined expression variables */
        if (lmap->vars) {
            for (i = 0; i < lmap->num_vars; i++) {
                mpr_value_free(&lmap->vars[i]);
                free((void*)lmap->var_names[i]);
            }
            free(lmap->vars);
            free(lmap->var_names);
        }
        FUNC_IF(free, lmap->updated_inst);
        FUNC_IF(mpr_expr_free, lmap->expr);
    }

    for (i = 0; i < map->num_src; i++) {
        link = mpr_slot_get_link(map->src[i]);
        if (link)
            mpr_link_remove_map(link, map);
        mpr_slot_free(map->src[i]);
    }
    free(map->src);

    link = mpr_slot_get_link(map->dst);
    if (link)
        mpr_link_remove_map(link, map);
    mpr_slot_free(map->dst);

    if (map->num_scopes && map->scopes)
        free(map->scopes);

    mpr_obj_free(&map->obj);
    FUNC_IF(free, map->expr_str);
}

static int _cmp_qry_sigs(const void *ctx, mpr_sig s)
{
    mpr_map m = *(mpr_map*)ctx;
    mpr_loc l = *(mpr_loc*)((char*)ctx + sizeof(mpr_map*));
    int i;
    if (l & MPR_LOC_SRC) {
        for (i = 0; i < m->num_src; i++) {
            if (s == mpr_slot_get_sig(m->src[i]))
                return 1;
        }
    }
    return (l & MPR_LOC_DST) ? (s == mpr_slot_get_sig(m->dst)) : 0;
}

mpr_list mpr_map_get_sigs(mpr_map m, mpr_loc l)
{
    RETURN_ARG_UNLESS(m, 0);
    return mpr_graph_new_query(m->obj.graph, 1, MPR_SIG, (void*)_cmp_qry_sigs, "vi", &m, l);
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
    return m ? (MPR_STATUS_ACTIVE == m->status) : 0;
}

/* Here we do not edit the "scope" property directly – instead we stage the
 * change with device name arguments and send to the distributed graph. */
void stage_scope(mpr_map m, mpr_dev d, int flag)
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
        mpr_tbl_add_record(m->obj.props.staged, p, NULL, len + 1, MPR_STR, new_val, REMOTE_MODIFY);
    }
}

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

static int _add_scope(mpr_map m, const char *name)
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

static int _remove_scope(mpr_map m, const char *name)
{
    int i;
    RETURN_ARG_UNLESS(m && name, 0);
    if (strcmp(name, "all")==0)
        name = 0;
    for (i = 0; i < m->num_scopes; i++) {
        if (!m->scopes[i]) {
            if (!name)
                break;
        }
        else if (name && strcmp(mpr_dev_get_name(m->scopes[i]), name) == 0)
            break;
    }
    if (i == m->num_scopes)
        return 0;
    /* found - remove scope at index i */
    for (++i; i < m->num_scopes - 1; i++)
        m->scopes[i] = m->scopes[i + 1];
    --m->num_scopes;
    m->scopes = realloc(m->scopes, m->num_scopes * sizeof(mpr_dev));
    return 1;
}

static int _update_scope(mpr_map m, mpr_msg_atom a)
{
    int i = 0, j, updated = 0, num = mpr_msg_atom_get_len(a);
    lo_arg **scope_list = mpr_msg_atom_get_values(a);
    if (scope_list && *scope_list) {
        const char *name;
        if (1 == num && strcmp(&scope_list[0]->s, "none")==0)
            num = 0;

        if (mpr_msg_atom_get_prop(a) & PROP_ADD) {
            for (i = 0; i < num; i++)
                updated += _add_scope(m, &scope_list[i]->s);
        }
        else if (mpr_msg_atom_get_prop(a) & PROP_REMOVE) {
            for (i = 0; i < num; i++)
                updated += _remove_scope(m, &scope_list[i]->s);
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
                    updated += _remove_scope(m, mpr_dev_get_name(m->scopes[i]));
                else
                    ++i;
            }
            /* ...then add any new scopes */
            for (i = 0; i < num; i++)
                updated += _add_scope(m, &scope_list[i]->s);
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
 * 1) move id_maps to maps
 * 2) on release of local instance, can reuse instance resource and mark id_map as "to release"
 * 3) map should iterate through active id_maps instead of instances
 * 4) when it comes to "to release" id_map, send release and decref LID
 */

/* only called for outgoing maps */
void mpr_map_send(mpr_local_map m, mpr_time time)
{
    int i, status, map_manages_inst = 0;
    lo_message msg;
    mpr_local_dev dev;
    uint8_t bundle_idx;
    mpr_local_slot src_slot;
    mpr_local_sig src_sig;
    mpr_sig dst_sig;
    mpr_id_map id_map = 0;
    mpr_value *src_vals, dst_val;
    char *types;

    assert(m->obj.is_local);

    RETURN_UNLESS(   m->updated && m->expr && !m->muted
                  && MPR_DIR_OUT == mpr_slot_get_dir((mpr_slot)m->src[0]));

    /* temporary solution: use most multitudinous source signal for id_map
     * permanent solution: move id_maps to map? */
    src_slot = m->src[0];
    src_sig = (mpr_local_sig)mpr_slot_get_sig((mpr_slot)src_slot);
    src_vals = alloca(m->num_src * sizeof(mpr_value));
    for (i = 0; i < m->num_src; i++) {
        mpr_local_sig comp = (mpr_local_sig)mpr_slot_get_sig((mpr_slot)m->src[i]);
        if (  mpr_sig_get_num_inst((mpr_sig)comp, MPR_STATUS_ANY)
            > mpr_sig_get_num_inst((mpr_sig)src_sig, MPR_STATUS_ANY)) {
            src_slot = m->src[i];
            src_sig = comp;
        }
        src_vals[i] = mpr_slot_get_value(m->src[i]);
    }

    dev = (mpr_local_dev)mpr_sig_get_dev((mpr_sig)src_sig);
    bundle_idx = mpr_local_dev_get_bundle_idx(dev);

    dst_sig = mpr_slot_get_sig((mpr_slot)m->dst);
    dst_val = mpr_slot_get_value(m->dst);

    if (m->use_inst && !mpr_sig_get_use_inst((mpr_sig)src_sig)) {
        map_manages_inst = 1;
        id_map = m->id_map;
    }

    types = alloca(mpr_sig_get_len(dst_sig) * sizeof(char));

    for (i = 0; i < m->num_inst; i++) {
        /* Check if this instance has been updated */
        if (!mpr_bitflags_get(m->updated_inst, i))
            continue;
        /* TODO: Check if this instance has enough history to process the expression */
        status = mpr_expr_eval(mpr_graph_get_expr_stack(m->obj.graph), m->expr, src_vals,
                               &m->vars, dst_val, &time, types, i);
        if (!status)
            continue;

        if (mpr_sig_get_use_inst((mpr_sig)src_sig) && !map_manages_inst) {
            mpr_local_sig_get_inst_by_idx(src_sig, i, &id_map);
            if (!id_map) {
                trace("error: couldn't find id_map for signal instance idx %d\n", i);
                continue;
            }
        }

        /* send instance release if dst is instanced and either src or map is also instanced. */
        if (id_map && status & EXPR_RELEASE_BEFORE_UPDATE && m->use_inst) {
            msg = mpr_map_build_msg(m, 0, 0, 0, id_map);
            mpr_local_slot_send_msg(m->dst, msg, time, m->protocol, bundle_idx);
            if (map_manages_inst) {
                mpr_dev_LID_decref(dev, 0, id_map);
                id_map = m->id_map = 0;
            }
        }
        if (status & EXPR_UPDATE) {
            /* send instance update */
            mpr_value val = mpr_slot_get_value(m->dst);
            void *result = mpr_value_get_samp(val, i);
            if (map_manages_inst && !id_map) {
                /* create an id_map and store it in the map */
                id_map = m->id_map = mpr_dev_add_id_map(dev, 0, 0, 0);
            }
            msg = mpr_map_build_msg(m, src_slot, result, types, id_map);
            mpr_local_slot_send_msg(m->dst, msg, *(mpr_time*)mpr_value_get_time(val, i),
                                    m->protocol, bundle_idx);
        }
        /* send instance release if dst is instanced and either src or map is also instanced. */
        if (id_map && status & EXPR_RELEASE_AFTER_UPDATE && m->use_inst) {
            msg = mpr_map_build_msg(m, 0, 0, 0, id_map);
            mpr_local_slot_send_msg(m->dst, msg, time, m->protocol, bundle_idx);
            if (map_manages_inst) {
                mpr_dev_LID_decref(dev, 0, id_map);
                id_map = m->id_map = 0;
            }
        }
        if ((status & EXPR_EVAL_DONE) && !m->use_inst)
            break;
    }
    mpr_bitflags_clear(m->updated_inst, m->num_inst);
    m->updated = 0;
}

/* only called for incoming maps */
/* TODO: merge with mpr_map_send()? */
void mpr_map_receive(mpr_local_map m, mpr_time time)
{
    int i, status, map_manages_inst = 0;
    mpr_local_slot src_slot;
    mpr_sig src_sig;
    mpr_local_sig dst_sig;
    mpr_value src_vals[MAX_NUM_MAP_SRC], dst_val;
    mpr_id_map id_map = 0;
    char *types;

    assert(m->obj.is_local);

    RETURN_UNLESS(m->updated && m->expr && !m->muted);

    /* temporary solution: use most multitudinous source signal for id_map
     * permanent solution: move id_maps to map */
    src_slot = m->src[0];
    src_sig = mpr_slot_get_sig((mpr_slot)src_slot);
    for (i = 0; i < m->num_src; i++) {
        mpr_sig comp = mpr_slot_get_sig((mpr_slot)m->src[i]);
        if (  mpr_sig_get_num_inst(comp, MPR_STATUS_ANY)
            > mpr_sig_get_num_inst(src_sig, MPR_STATUS_ANY)) {
            src_slot = m->src[i];
            src_sig = comp;
        }
        src_vals[i] = mpr_slot_get_value(m->src[i]);
    }

    dst_sig = (mpr_local_sig)mpr_slot_get_sig((mpr_slot)m->dst);
    dst_val = mpr_slot_get_value(m->dst);

    if (!mpr_sig_get_use_inst(src_sig)) {
        if (mpr_expr_get_manages_inst(m->expr)) {
            map_manages_inst = 1;
            id_map = m->id_map;
        }
        else
            id_map = 0;
    }
    types = alloca(mpr_sig_get_len((mpr_sig)dst_sig) * sizeof(char));

    for (i = 0; i < m->num_inst; i++) {
        if (!mpr_bitflags_get(m->updated_inst, i))
            continue;
        status = mpr_expr_eval(mpr_graph_get_expr_stack(m->obj.graph), m->expr, src_vals,
                               &m->vars, dst_val, &time, types, i);
        if (!status)
            continue;

        mpr_local_sig_set_inst_value(dst_sig, dst_val, i, id_map, status, map_manages_inst, time);

        if ((status & EXPR_EVAL_DONE) && !m->use_inst)
            break;
    }
    mpr_bitflags_clear(m->updated_inst, m->num_inst);
    m->updated = 0;
}

/*! Build a value update message for a given map. */
lo_message mpr_map_build_msg(mpr_local_map m, mpr_local_slot slot, const void *val,
                             mpr_type *types, mpr_id_map id_map)
{
    int i, len = 0;
    NEW_LO_MSG(msg, return 0);
    if (MPR_LOC_SRC == m->process_loc) {
        mpr_sig sig = mpr_slot_get_sig((mpr_slot)m->dst);
        len = mpr_sig_get_len(sig);
    }
    else if (slot) {
        mpr_sig sig = mpr_slot_get_sig((mpr_slot)slot);
        len = mpr_sig_get_len(sig);
    }

    if (val && types) {
        /* value of vector elements can be <type> or NULL */
        for (i = 0; i < len; i++) {
            switch (types[i]) {
            case MPR_INT32: lo_message_add_int32(msg, ((int*)val)[i]);     break;
            case MPR_FLT:   lo_message_add_float(msg, ((float*)val)[i]);   break;
            case MPR_DBL:   lo_message_add_double(msg, ((double*)val)[i]); break;
            case MPR_NULL:  lo_message_add_nil(msg);                       break;
            default:                                                       break;
            }
        }
    }
    else if (m->use_inst) {
        for (i = 0; i < len; i++)
            lo_message_add_nil(msg);
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
    int i, j, hist_size, num_inst = 0, num_vars;
    mpr_expr e = m->expr;
    mpr_value_t *vars;
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
        hist_size = mpr_expr_get_in_hist_size(e, i);
        mpr_slot_alloc_values(m->src[i], mpr_slot_get_num_inst((mpr_slot)m->src[i]), hist_size);
        num_inst = mpr_max(mpr_sig_get_num_inst(sig, MPR_STATUS_ANY), num_inst);
    }
    sig = mpr_slot_get_sig((mpr_slot)m->dst);
    num_inst = mpr_max(mpr_sig_get_num_inst(sig, MPR_STATUS_ANY), num_inst);
    hist_size = mpr_expr_get_out_hist_size(e);

    /* If the dst slot is remote, we need to allocate enough dst slot and variable instances for
     * the most multitudinous source signal. In the case where the dst slot is associated with a
     * local signal the call below will default to using the signal's instance count. */
    mpr_slot_alloc_values(m->dst, num_inst, hist_size);

    num_vars = mpr_expr_get_num_vars(e);
    vars = calloc(1, sizeof(mpr_value_t) * num_vars);
    var_names = malloc(sizeof(char*) * num_vars);
    for (i = 0; i < num_vars; i++) {
        int vlen = mpr_expr_get_var_vec_len(e, i);
        int var_num_inst = mpr_expr_get_var_is_instanced(e, i) ? num_inst : 1;
        var_names[i] = strdup(mpr_expr_get_var_name(e, i));
        /* check if var already exists */
        for (j = 0; j < m->num_vars; j++) {
            if (!m->var_names[j] || strcmp(m->var_names[j], var_names[i]))
                continue;
            if (m->vars[i].vlen != vlen)
                continue;
            /* match found */
            break;
        }
        if (j < m->num_vars) {
            /* copy old variable memory */
            memcpy(&vars[i], &m->vars[j], sizeof(mpr_value_t));
            m->vars[j].inst = 0;
        }
        mpr_value_realloc(&vars[i], vlen, mpr_expr_get_var_type(e, i), 1, var_num_inst, 0);
        /* set position to 0 since we are not currently allowing history on user variables */
        for (j = 0; j < var_num_inst; j++)
            vars[i].inst[j].pos = 0;
    }

    /* free old variables and replace with new */
    for (i = 0; i < m->num_vars; i++) {
        mpr_value_free(&m->vars[i]);
        FUNC_IF(free, (void*)m->var_names[i]);
    }
    FUNC_IF(free, m->vars);
    FUNC_IF(free, m->var_names);

    m->vars = vars;
    m->var_names = var_names;
    m->num_vars = num_vars;
    m->num_inst = num_inst;

    /* allocate update bitflags */
    if (m->updated_inst)
        m->updated_inst = realloc(m->updated_inst, num_inst / 8 + 1);
    else
        m->updated_inst = calloc(1, num_inst / 8 + 1);

    if (!quiet) {
        /* Inform remote peers of the change */
        mpr_net net = mpr_graph_get_net(m->obj.graph);
        if (MPR_DIR_OUT == mpr_slot_get_dir((mpr_slot)m->dst)) {
            /* Inform remote destination */
            mpr_net_use_mesh(net, mpr_link_get_admin_addr(mpr_slot_get_link((mpr_slot)m->dst)));
            mpr_map_send_state((mpr_map)m, -1, MSG_MAPPED);
        }
        else {
            /* Inform remote sources */
            for (i = 0; i < m->num_src; i++) {
                mpr_net_use_mesh(net, mpr_link_get_admin_addr(mpr_slot_get_link((mpr_slot)m->src[i])));
                i = mpr_map_send_state((mpr_map)m, i, MSG_MAPPED);
            }
        }
    }
}

/* Helper to replace a map's expression only if the given string
 * parses successfully. Returns 0 on success, non-zero on error. */
static int _replace_expr_str(mpr_local_map m, const char *expr_str)
{
    int i, out_mem, src_lens[MAX_NUM_MAP_SRC];
    char src_types[MAX_NUM_MAP_SRC];
    mpr_sig dst;
    mpr_expr expr;
    if (m->expr && m->expr_str && strcmp(m->expr_str, expr_str)==0)
        return 1;

    for (i = 0; i < m->num_src; i++) {
        mpr_sig src = mpr_slot_get_sig((mpr_slot)m->src[i]);
        src_types[i] = mpr_sig_get_type(src);
        src_lens[i] = mpr_sig_get_len(src);
    }
    dst = mpr_slot_get_sig((mpr_slot)m->dst);
    expr = mpr_expr_new_from_str(mpr_graph_get_expr_stack(m->obj.graph), expr_str, m->num_src,
                                 src_types, src_lens, mpr_sig_get_type(dst), mpr_sig_get_len(dst));
    RETURN_ARG_UNLESS(expr, 1);

    /* expression update may force processing location to change
     * e.g. if expression combines signals from different devices
     * e.g. if expression refers to current/past value of destination */
    out_mem = mpr_expr_get_out_hist_size(expr);
    if (MPR_LOC_BOTH != m->locality && (out_mem > 1 && MPR_LOC_SRC == m->process_loc)) {
        m->process_loc = MPR_LOC_DST;
        if (!mpr_obj_get_is_local((mpr_obj)dst)) {
            /* copy expression string but do not execute it */
            mpr_tbl_add_record(m->obj.props.synced, PROP(EXPR), NULL, 1, MPR_STR, expr_str,
                               REMOTE_MODIFY);
            mpr_expr_free(expr);
            return 1;
        }
    }
    FUNC_IF(mpr_expr_free, m->expr);
    m->expr = expr;

    if (m->expr_str == expr_str)
        return 0;
    mpr_tbl_add_record(m->obj.props.synced, PROP(EXPR), NULL, 1, MPR_STR, expr_str, REMOTE_MODIFY);
    mpr_tbl_remove_record(m->obj.props.staged, PROP(EXPR), NULL, 0);
    return 0;
}

MPR_INLINE static int _trim_zeros(char *str, int len)
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

static int _snprint_var(const char *varname, char *str, int max_len, int vec_len,
                        mpr_type type, const void *val)
{
    int i, str_len, var_len;
    RETURN_ARG_UNLESS(str, -1);
    snprintf(str, max_len, "%s=", varname);
    str_len = strlen(str);

    if (vec_len > 1)
        str_len += snprintf(str+str_len, max_len-str_len, "[");
    switch (type) {
        case MPR_INT32:
            for (i = 0; i < vec_len; i++) {
                var_len = snprintf(str+str_len, max_len-str_len, "%d", ((int*)val)[i]);
                str_len += _trim_zeros(str+str_len, var_len);
                str_len += snprintf(str+str_len, max_len-str_len, ",");
            }
            break;
        case MPR_FLT:
            for (i = 0; i < vec_len; i++) {
                var_len = snprintf(str+str_len, max_len-str_len, "%g", ((float*)val)[i]);
                str_len += _trim_zeros(str+str_len, var_len);
                str_len += snprintf(str+str_len, max_len-str_len, ",");
            }
            break;
        case MPR_DBL:
            for (i = 0; i < vec_len; i++) {
                var_len = snprintf(str+str_len, max_len-str_len, "%g", ((double*)val)[i]);
                str_len += _trim_zeros(str+str_len, var_len);
                str_len += snprintf(str+str_len, max_len-str_len, ",");
            }
            break;
        default:
            break;
    }
    --str_len;
    if (vec_len > 1)
        str_len += snprintf(str+str_len, max_len-str_len, "];");
    else
        str_len += snprintf(str+str_len, max_len-str_len, ";");
    return str_len;
}

#define INSERT_VAL(VARNAME)                                         \
mpr_value_t *ev = m->vars;                                          \
for (j = 0; j < m->num_vars; j++) {                                 \
    /* TODO: handle multiple instances */                           \
    k = 0;                                                          \
    if (strcmp(VARNAME, mpr_expr_get_var_name(m->expr, j)))         \
        continue;                                                   \
    if (ev[j].inst[k].pos < 0) {                                    \
        trace("expr var '%s' is not yet initialised.\n", VARNAME);  \
        goto abort;                                                 \
    }                                                               \
    len += _snprint_var(VARNAME, expr+len, MAX_LEN-len, ev[j].vlen, \
                        ev[j].type, mpr_value_get_samp(&ev[j], k)); \
    break;                                                          \
}                                                                   \
if (j == m->num_vars) {                                             \
    trace("expr var '%s' is not found.\n", VARNAME);                \
    goto abort;                                                     \
}

static const char *_set_linear(mpr_local_map m, const char *e)
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
                    len += snprintf(expr + len, MAX_LEN-len, "sMin=0;");
                }
                else {
                    INSERT_VAL("sMin");
                }
            }
            else {
                val_len = snprintf(expr, MAX_LEN, "sMin=%s", args[1]);
                len += _trim_zeros(expr, val_len);
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
                len += _trim_zeros(expr + len, val_len);
                len += snprintf(expr + len, MAX_LEN - len, ";");
            }

            if (0 == strcmp(args[3], "-") || 0 == strcmp(args[3], "- ")) {
                /* try to load dMin variable from existing expression */
                if (!m->expr) {
                    trace("can't retrieve previous expr variable\n");
                    len += snprintf(expr+len, MAX_LEN-len, "dMin=0;");
                }
                else {
                    INSERT_VAL("dMin");
                }
            }
            else {
                val_len = snprintf(expr + len, MAX_LEN - len, "dMin=%s", args[3]);
                len += _trim_zeros(expr + len, val_len);
                len += snprintf(expr + len, MAX_LEN - len, ";");
            }

            if (0 == strcmp(args[4], "-") || 0 == strcmp(args[4], "- ")) {
                /* try to load dMin variable from existing expression */
                if (!m->expr) {
                    trace("can't retrieve previous expr variable\n");
                    len += snprintf(expr+len, MAX_LEN-len, "dMax=1;");
                }
                else {
                    INSERT_VAL("dMax");
                }
            }
            else {
                val_len = snprintf(expr + len, MAX_LEN - len, "dMax=%s", args[4]);
                len += _trim_zeros(expr + len, val_len);
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
#define print_extremum(SIG, PROPERTY, LABEL)                                            \
        if (cont && mpr_obj_get_prop_by_idx((mpr_obj)SIG, PROPERTY, NULL, &val_len,     \
                                            &val_type, &val, NULL))                     \
            len += _snprint_var(LABEL, expr+len, MAX_LEN-len, val_len, val_type, val);  \
        else                                                                            \
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
                    snprintf(expr, MAX_LEN, "y=x[0:%i]", dst_len-1);
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
                        len += snprintf(expr+len, MAX_LEN-len, "x$%d[0]+", i);
                    else
                        len += snprintf(expr+len, MAX_LEN-len, "x$%d[0:%d]+", i, dst_len-1);
                }
                else if (src_i_len < dst_len) {
                    len += snprintf(expr+len, MAX_LEN-len, "[x$%d,0", i);
                    for (j = 1; j < dst_len - src_0_len; j++)
                        len += snprintf(expr+len, MAX_LEN-len, ",0");
                    len += snprintf(expr+len, MAX_LEN-len, "]+");
                }
                else
                    len += snprintf(expr+len, MAX_LEN-len, "x$%d+", i);
            }
            --len;
            snprintf(expr+len, MAX_LEN-len, ")/%d", m->num_src);
        }
        FUNC_IF(free, (char*)e);
        return strdup(expr);
    }

    snprintf(expr+len, MAX_LEN-len,
             "sRange=sMax-sMin;"
             "m=sRange?((dMax-dMin)/sRange):0;"
             "b=sRange?(dMin*sMax-dMax*sMin)/sRange:dMin;");

    len = strlen(expr);

    if (dst_len >= src_0_len)
        snprintf(expr+len, MAX_LEN-len, "y=m*%s+b;", var);
    else if (min_len == 1)
        snprintf(expr+len, MAX_LEN-len, "y=m*%s[0]+b;", var);
    else
        snprintf(expr+len, MAX_LEN-len, "y=m*%s[0:%i]+b;", var, min_len-1);

    trace("linear expression %s requires %d chars\n", expr, (int)strlen(expr));
    FUNC_IF(free, (char*)e);
    return strdup(expr);

abort:
    FUNC_IF(free, (char*)e);
    return NULL;
}

static int _set_expr(mpr_local_map m, const char *expr)
{
    int i, should_compile = 0, ret = 0;
    const char *new_expr = 0;
    mpr_sig dst_sig = mpr_slot_get_sig((mpr_slot)m->dst);
    RETURN_ARG_UNLESS(m->num_src > 0, 0);

    /* deal with instances activated by the previous expression */
    if (m->id_map) {
        mpr_sig sig = mpr_slot_get_sig((mpr_slot)m->src[0]);
        if (mpr_obj_get_is_local((mpr_obj)sig))
            mpr_dev_LID_decref((mpr_local_dev)mpr_sig_get_dev(sig), 0, m->id_map);
        if (MPR_LOC_BOTH != m->locality) {
            sig = mpr_slot_get_sig((mpr_slot)m->dst);
            if (mpr_obj_get_is_local((mpr_obj)sig))
                mpr_dev_LID_decref((mpr_local_dev)mpr_sig_get_dev(sig), 0, m->id_map);
        }
    }

    if (MPR_LOC_BOTH == m->locality)
        should_compile = 1;
    else if (MPR_LOC_DST == m->process_loc) {
        /* check if destination is local */
        if (mpr_obj_get_is_local((mpr_obj)dst_sig))
            should_compile = 1;
    }
    else {
        for (i = 0; i < m->num_src; i++) {
            if (mpr_slot_get_sig_if_local((mpr_slot)m->src[i]))
                should_compile = 1;
        }
    }

    if (!should_compile) {
        if (expr)
            mpr_tbl_add_record(m->obj.props.synced, PROP(EXPR), NULL, 1, MPR_STR, expr, REMOTE_MODIFY);
        goto done;
    }
    if (!expr || strstr(expr, "linear"))
        expr = new_expr = _set_linear(m, expr);
    RETURN_ARG_UNLESS(expr, -1);

    if (!_replace_expr_str(m, expr)) {
        mpr_time now;
        char *types = alloca(mpr_sig_get_len(dst_sig) * sizeof(char));
        mpr_map_alloc_values(m, 1);
        /* evaluate expression to intialise literals */
        mpr_time_set(&now, MPR_NOW);
        for (i = 0; i < m->num_inst; i++)
            mpr_expr_eval(mpr_graph_get_expr_stack(m->obj.graph), m->expr, 0, &m->vars,
                          mpr_slot_get_value(m->dst), &now, types, i);
    }
    else {
        mpr_sig src_sig = mpr_slot_get_sig((mpr_slot)m->src[0]);
        if (!m->expr && (   (MPR_LOC_DST == m->process_loc && mpr_obj_get_is_local((mpr_obj)dst_sig))
                         || (MPR_LOC_SRC == m->process_loc && mpr_obj_get_is_local((mpr_obj)src_sig)))) {
            /* no previous expression, abort map */
            m->status = MPR_STATUS_EXPIRED;
        }
        /* expression unchanged */
        ret = 1;
        goto done;
    }

    /* Special case: if we are the receiver and the new expression evaluates to
     * a constant we can update immediately. */
    /* TODO: should call handler for all instances updated through this map. */
    if (   mpr_expr_get_num_input_slots(m->expr) <= 0 && !m->use_inst
        && mpr_obj_get_is_local((mpr_obj)dst_sig)) {
        /* call handler if it exists */
        mpr_time now;
        mpr_time_set(&now, MPR_NOW);
        mpr_sig_call_handler((mpr_local_sig)dst_sig, MPR_SIG_UPDATE, 0, -1,
                             mpr_slot_get_value(m->dst), now, 0);
    }

    /* check whether each source slot causes computation */
    for (i = 0; i < m->num_src; i++)
        mpr_slot_set_causes_update((mpr_slot)m->src[i], !mpr_expr_get_src_is_muted(m->expr, i));
done:
    if (new_expr)
        free((char*)new_expr);
    return ret;
}

static void _check_status(mpr_local_map map)
{
    int i, mask = ~METADATA_OK;
    RETURN_UNLESS((map->status & MPR_STATUS_READY) != MPR_STATUS_READY);

    trace("checking map status\n");
    map->status |= METADATA_OK;
    for (i = 0; i < map->num_src; i++) {
        trace("  src[%d]: ", i);
        map->status &= (mpr_slot_check_status(map->src[i]) | mask);
    }
    trace("  dst:    ");
    map->status &= (mpr_slot_check_status(map->dst) | mask);

    if ((map->status & METADATA_OK) == METADATA_OK) {
        mpr_tbl tbl = mpr_obj_get_prop_tbl((mpr_obj)map);
        mpr_sig sig;
        int use_inst;

        trace("  map metadata OK, setting status to READY\n");
        mpr_map_alloc_values(map, 1);
        map->status = MPR_STATUS_READY;

        /* update in/out counts for link */
        if (MPR_LOC_BOTH == map->locality) {
            mpr_link link = mpr_slot_get_link((mpr_slot)map->dst);
            if (link)
                mpr_link_add_map(link, 0);
        }
        else {
            mpr_link last = 0, link = mpr_slot_get_link((mpr_slot)map->dst);
            if (link) {
                mpr_link_add_map(link, 0);
            }
            for (i = 0; i < map->num_src; i++) {
                link = mpr_slot_get_link((mpr_slot)map->src[i]);
                if (link && link != last) {
                    mpr_link_add_map(link, 1);
                    last = link;
                }
            }
        }
        _set_expr(map, map->expr_str);

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
        }

        if (MPR_LOC_BOTH != map->locality && !mpr_tbl_get_prop_is_set(tbl, MPR_PROP_PROTOCOL))
            map->protocol = use_inst ? MPR_PROTO_TCP : MPR_PROTO_UDP;
    }
    return;
}

/* if 'override' flag is not set, only remote properties can be set */
int mpr_map_set_from_msg(mpr_map m, mpr_msg msg, int override)
{
    int i, j, updated = 0, should_compile = 0;
    mpr_tbl tbl;
    mpr_msg_atom a;
    if (!msg)
        goto done;

    if (MPR_DIR_OUT == mpr_slot_get_dir(m->dst)) {
        /* check if MPR_PROP_SLOT property is defined */
        a = mpr_msg_get_prop(msg, PROP(SLOT));
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

    tbl = m->obj.props.synced;
    for (i = 0; i < mpr_msg_get_num_atoms(msg); i++) {
        const mpr_type *types;
        lo_arg **vals;
        a = mpr_msg_get_atom(msg, i);
        types = mpr_msg_atom_get_types(a);
        vals = mpr_msg_atom_get_values(a);
        switch (MASK_PROP_BITFLAGS(mpr_msg_atom_get_prop(a))) {
            case PROP(NUM_SIGS_IN):
            case PROP(NUM_SIGS_OUT):
                /* these properties will be set by signal args */
                break;
            case PROP(STATUS):
                if (m->obj.is_local)
                    break;
                updated += mpr_tbl_add_record_from_msg_atom(tbl, a, REMOTE_MODIFY);
                break;
            case PROP(PROCESS_LOC): {
                mpr_loc loc;
                if (m->obj.is_local && MPR_LOC_BOTH == ((mpr_local_map)m)->locality)
                    break;
                loc = mpr_loc_from_str(&(vals[0])->s);
                if (loc == m->process_loc)
                    break;
                if (MPR_LOC_UNDEFINED == loc) {
                    trace("map process location is undefined!\n");
                    break;
                }
                if (m->obj.is_local) {
                    mpr_local_map lm = (mpr_local_map)m;
                    if (MPR_LOC_ANY == loc) {
                        /* no effect */
                        break;
                    }
                    if (!lm->one_src) {
                        /* Processing must take place at destination if map
                         * includes source signals from different devices. */
                        loc = MPR_LOC_DST;
                    }
                    if (MPR_LOC_DST == loc) {
                        /* check if destination is local */
                        if (mpr_slot_get_sig_if_local((mpr_slot)lm->dst))
                            should_compile = 1;
                    }
                    else {
                        for (j = 0; j < m->num_src; j++) {
                            if (mpr_slot_get_sig_if_local((mpr_slot)lm->src[j]))
                                should_compile = 1;
                        }
                    }
                    if (should_compile) {
                        if (-1 == _set_expr(lm, m->expr_str)) {
                            /* do not change process location */
                            break;
                        }
                        ++updated;
                    }
                    updated += mpr_tbl_add_record(tbl, PROP(PROCESS_LOC), NULL, 1,
                                                  MPR_INT32, &loc, REMOTE_MODIFY);
                }
                else
                    updated += mpr_tbl_add_record(tbl, PROP(PROCESS_LOC), NULL, 1,
                                                  MPR_INT32, &loc, REMOTE_MODIFY);
                break;
            }
            case PROP(EXPR): {
                const char *expr_str = &(vals[0])->s;
                mpr_loc orig_loc = m->process_loc;
                if (m->obj.is_local && (m->status & MPR_STATUS_READY) == MPR_STATUS_READY) {
                    mpr_local_map lm = (mpr_local_map)m;
                    if (MPR_LOC_BOTH == lm->locality)
                        should_compile = 1;
                    else {
                        if (strstr(expr_str, "y{-"))
                            lm->process_loc = MPR_LOC_DST;
                        if (MPR_LOC_DST == lm->process_loc) {
                            /* check if destination is local */
                            if (mpr_slot_get_sig_if_local((mpr_slot)lm->dst))
                                should_compile = 1;
                        }
                        else {
                            for (j = 0; j < m->num_src; j++) {
                                if (mpr_slot_get_sig_if_local((mpr_slot)lm->src[j]))
                                    should_compile = 1;
                            }
                        }
                    }
                    if (should_compile) {
                        int e = _set_expr(lm, expr_str);
                        if (-1 == e) {
                            /* restore original process location */
                            lm->process_loc = orig_loc;
                            break;
                        }
                        else if (0 == e)
                            ++updated;
                    }
                    else {
                        updated += mpr_tbl_add_record(tbl, PROP(EXPR), NULL, 1, MPR_STR,
                                                      expr_str, REMOTE_MODIFY);
                    }
                }
                else {
                    updated += mpr_tbl_add_record(tbl, PROP(EXPR), NULL, 1, MPR_STR,
                                                  expr_str, REMOTE_MODIFY);
                }
                if (orig_loc != m->process_loc) {
                    mpr_loc loc = m->process_loc;
                    m->process_loc = orig_loc;
                    updated += mpr_tbl_add_record(tbl, PROP(PROCESS_LOC), NULL, 1,
                                                  MPR_INT32, &loc, REMOTE_MODIFY);
                }
                if (!m->obj.is_local) {
                    /* remove any cached expression variables from table */
                    mpr_tbl_remove_record(tbl, MPR_PROP_EXTRA, "var@*", REMOTE_MODIFY);
                }
                break;
            }
            case PROP(SCOPE):
                if (types && mpr_type_get_is_str(types[0]))
                    updated += _update_scope(m, a);
                break;
            case PROP(SCOPE) | PROP_ADD:
                for (j = 0; j < mpr_msg_atom_get_len(a); j++) {
                    if (types && MPR_STR == types[j])
                        updated += _add_scope(m, &(vals[j])->s);
                }
                break;
            case PROP(SCOPE) | PROP_REMOVE:
                for (j = 0; j < mpr_msg_atom_get_len(a); j++) {
                    if (types && MPR_STR == types[j])
                        updated += _remove_scope(m, &(vals[j])->s);
                }
                break;
            case PROP(PROTOCOL): {
                mpr_proto pro;
                if (mpr_obj_get_is_local((mpr_obj)m) && MPR_LOC_BOTH == ((mpr_local_map)m)->locality)
                    break;
                pro = mpr_protocol_from_str(&(vals[0])->s);
                updated += mpr_tbl_add_record(tbl, PROP(PROTOCOL), NULL, 1, MPR_INT32,
                                              &pro, REMOTE_MODIFY);
                break;
            }
            case PROP(USE_INST): {
                int use_inst = types[0] == 'T';
                if (m->obj.is_local && m->use_inst && !use_inst) {
                    /* TODO: release map instances */
                }
                updated += mpr_tbl_add_record(tbl, PROP(USE_INST), NULL, 1, MPR_BOOL,
                                              &use_inst, REMOTE_MODIFY);
                break;
            }
            case PROP(EXTRA): {
                const char *key = mpr_msg_atom_get_key(a);
                if (strcmp(key, "expression")==0) {
                    if (mpr_type_get_is_str(types[0])) {
                        /* set property type to expr and repeat */
                        mpr_msg_atom_set_prop(a, PROP(EXPR));
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
                            var_len = lm->vars[j].vlen;
                            /* cast if necessary */
                            switch (lm->vars[j].type) {
#define TYPED_CASE(MTYPE, TYPE, MTYPE1, EL1, MTYPE2, EL2)                           \
                                case MTYPE: {                                       \
                                    TYPE *v = mpr_value_get_samp(&lm->vars[j], k);  \
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
                            mpr_expr_var_updated(lm->expr, j);
                            break;
                        }
                    }
                    if (m->obj.is_local)
                        break;
                    /* otherwise continue to mpr_tbl_add_record_from_msg_atom() below */
                }
            }
            case PROP(ID):
            case PROP(MUTED):
            case PROP(VERSION):
                updated += mpr_tbl_add_record_from_msg_atom(tbl, a, REMOTE_MODIFY);
                break;
            default:
                break;
        }
    }
done:
    if (m->obj.is_local && m->status < MPR_STATUS_READY) {
        /* check if mapping is now "ready" */
        _check_status((mpr_local_map)m);
    }
    return updated;
}

/* If the "slot_idx" argument is >= 0, we can assume this message will be sent
 * to a peer device rather than a session manager. */
int mpr_map_send_state(mpr_map m, int slot_idx, net_msg_t cmd)
{
    lo_message msg;
    char buffer[256];
    int i, staged;
    mpr_link link;
    mpr_dir dst_dir = mpr_slot_get_dir(m->dst);

    if (MSG_MAPPED == cmd && m->status < MPR_STATUS_READY)
        return slot_idx;
    msg = lo_message_new();
    if (!msg) {
        trace("couldn't allocate lo_message\n");
        return slot_idx;
    }
    if (MPR_DIR_IN == dst_dir) {
        /* add mapping destination */
        mpr_sig_get_full_name(mpr_slot_get_sig(m->dst), buffer, 256);
        lo_message_add_string(msg, buffer);
        lo_message_add_string(msg, "<-");
    }

    /* TODO: verify that this works! */
    if (mpr_obj_get_is_local((mpr_obj)m) && ((mpr_local_map)m)->one_src)
        slot_idx = -1;

    /* add mapping sources */
    i = (slot_idx >= 0) ? slot_idx : 0;
    /* TODO: need to check that this works (non-local slots should not have links? */
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
        lo_message_add_string(msg, mpr_prop_as_str(PROP(ID), 0));
        lo_message_add_int64(msg, *((int64_t*)&m->obj.id));
    }

    if (MSG_UNMAP == cmd || MSG_UNMAPPED == cmd) {
        mpr_net_add_msg(mpr_graph_get_net(m->obj.graph), 0, cmd, msg);
        return i-1;
    }

    /* add other properties */
    staged = (MSG_MAP == cmd) || (MSG_MAP_MOD == cmd);
    mpr_obj_add_props_to_msg((mpr_obj)m, msg);

    /* add slot id */
    if (MPR_DIR_IN == dst_dir && m->status <= MPR_STATUS_READY && !staged) {
        lo_message_add_string(msg, mpr_prop_as_str(PROP(SLOT), 0));
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
        int j, k, l;
        char varname[32];
        for (j = 0; j < lm->num_vars; j++) {
            /* TODO: handle multiple instances */
            k = 0;
            if (lm->vars[j].inst[k].pos >= 0) {
                snprintf(varname, 32, "@var@%s", mpr_expr_get_var_name(lm->expr, j));
                lo_message_add_string(msg, varname);
                switch (lm->vars[j].type) {
                    case MPR_INT32: {
                        int *v = mpr_value_get_samp(&lm->vars[j], k);
                        for (l = 0; l < lm->vars[j].vlen; l++)
                            lo_message_add_int32(msg, v[l]);
                        break;
                    }
                    case MPR_FLT: {
                        float *v = mpr_value_get_samp(&lm->vars[j], k);
                        for (l = 0; l < lm->vars[j].vlen; l++)
                            lo_message_add_float(msg, v[l]);
                        break;
                    }
                    case MPR_DBL: {
                        double *v = mpr_value_get_samp(&lm->vars[j], k);
                        for (l = 0; l < lm->vars[j].vlen; l++)
                            lo_message_add_double(msg, v[l]);
                        break;
                    }
                    default:
                        break;
                }
            }
            else {
                trace("public expression variable '%s' is not yet initialised.\n",
                      mpr_expr_get_var_name(lm->expr, j));
            }
        }
    }
    mpr_net_add_msg(mpr_graph_get_net(m->obj.graph), 0, cmd, msg);
    return i-1;
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
                    trace("Format string '%s' is missing output signal.\n", expr);
                    goto error;
                }
                if (!dst)
                    dst = sig;
                else if (sig != dst) {
                    trace("Format string '%s' references more than one output signal.\n", expr);
                    goto error;
                }
                break;
            case 'x':
                sig = va_arg(aq, void*);
                if (!sig) {
                    trace("Format string '%s' is missing input signal.\n", expr);
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

int _compare_slot_names(const void *l, const void *r)
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
    qsort(map->src, map->num_src, sizeof(mpr_slot), _compare_slot_names);

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

int mpr_map_get_status(mpr_map map)
{
    return map->status;
}

void mpr_map_set_status(mpr_map map, int status)
{
    map->status = status;
}

size_t mpr_map_get_struct_size(int is_local)
{
    return is_local ? sizeof(mpr_local_map_t) : sizeof(mpr_map_t);
}

void mpr_local_map_set_updated(mpr_local_map map, int inst_idx)
{
    mpr_bitflags_set(map->updated_inst, inst_idx);
    map->updated = 1;
}

int mpr_map_get_use_inst(mpr_map map)
{
    return map->use_inst;
}

void mpr_map_status_decr(mpr_map map)
{
    --map->status;
}

int mpr_map_waiting(mpr_map map)
{
    int i;
    mpr_sig sig = mpr_slot_get_sig(map->dst);
    mpr_dev dev = mpr_sig_get_dev(sig);
    /* Only proceed if all signals are registered (remote or registered local signals) */
    RETURN_ARG_UNLESS(mpr_dev_get_is_registered(dev), 1);
    for (i = 0; i < map->num_src; i++) {
        sig = mpr_slot_get_sig(map->src[i]);
        dev = mpr_sig_get_dev(sig);
        RETURN_ARG_UNLESS(mpr_dev_get_is_registered(dev), 1);
    }
    return 0;
}
