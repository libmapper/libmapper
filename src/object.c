#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "device.h"
#include "list.h"
#include "map.h"
#include "network.h"
#include "mpr_signal.h"
#include "property.h"
#include "slot.h"
#include <mapper/mapper.h>

void mpr_obj_init(mpr_obj o, mpr_graph g, mpr_type t)
{
    o->graph = g;
    o->type = t;
    o->status = MPR_STATUS_NEW;
}

void mpr_obj_free(mpr_obj o)
{
    FUNC_IF(mpr_tbl_free, o->props.staged);
    FUNC_IF(mpr_tbl_free, o->props.synced);
}

mpr_graph mpr_obj_get_graph(mpr_obj o)
{
    return o ? o->graph : 0;
}

mpr_type mpr_obj_get_type(mpr_obj o)
{
    return o ? o->type : 0;
}

void mpr_obj_increment_version(mpr_obj o)
{
    RETURN_UNLESS(o);
    if (o->is_local) {
        ++o->version;
        mpr_tbl_set_is_dirty(o->props.synced, 1);
    }
    else if (o->props.staged)
        mpr_tbl_set_is_dirty(o->props.staged, 1);
    o->status |= MPR_STATUS_MODIFIED;
}

int mpr_obj_get_status(mpr_obj obj)
{
    return obj->status;
}

void mpr_obj_reset_status(mpr_obj obj)
{
    obj->status &= (  MPR_STATUS_EXPIRED
                    | MPR_STATUS_STAGED
                    | MPR_STATUS_ACTIVE
                    | MPR_STATUS_HAS_VALUE);
}

int mpr_obj_get_num_props(mpr_obj o, int staged)
{
    int len = 0;
    if (o) {
        if (o->props.synced)
            len += mpr_tbl_get_num_records(o->props.synced);
        if (staged && o->props.staged)
            len += mpr_tbl_get_num_records(o->props.staged);
    }
    return len;
}

mpr_prop mpr_obj_get_prop_by_key(mpr_obj o, const char *s, int *l, mpr_type *t,
                                 const void **v, int *p)
{
    RETURN_ARG_UNLESS(o && s, 0);
    return mpr_tbl_get_record_by_key(o->props.synced, s, l, t, v, p);
}

mpr_prop mpr_obj_get_prop_by_idx(mpr_obj o, int p, const char **k, int *l,
                                 mpr_type *t, const void **v, int *pub)
{
    RETURN_ARG_UNLESS(o, 0);
    return mpr_tbl_get_record_by_idx(o->props.synced, p, k, l, t, v, pub);
}

int mpr_obj_get_prop_as_int32(mpr_obj o, mpr_prop p, const char *s)
{
    const void *val;
    mpr_type type;
    int len;
    RETURN_ARG_UNLESS(o, 0);
    if (s)
        p = mpr_tbl_get_record_by_key(o->props.synced, s, &len, &type, &val, NULL);
    else
        p = mpr_tbl_get_record_by_idx(o->props.synced, p, NULL, &len, &type, &val, NULL);
    RETURN_ARG_UNLESS(val, 0);

    switch (type) {
        case MPR_BOOL:
        case MPR_INT32: return      *(int*)val;
        case MPR_INT64: return (int)*(int64_t*)val;
        case MPR_FLT:   return (int)*(float*)val;
        case MPR_DBL:   return (int)*(double*)val;
        case MPR_TYPE:  return (int)*(mpr_type*)val;
        default:        return 0;
    }
}

int64_t mpr_obj_get_prop_as_int64(mpr_obj o, mpr_prop p, const char *s)
{
    const void *val;
    mpr_type type;
    int len;
    RETURN_ARG_UNLESS(o, 0);
    if (s)
        p = mpr_tbl_get_record_by_key(o->props.synced, s, &len, &type, &val, NULL);
    else
        p = mpr_tbl_get_record_by_idx(o->props.synced, p, NULL, &len, &type, &val, NULL);
    RETURN_ARG_UNLESS(val, 0);

    switch (type) {
        case MPR_BOOL:
        case MPR_INT32: return (int64_t)*(int*)val;
        case MPR_INT64: return          *(int64_t*)val;
        case MPR_FLT:   return (int64_t)*(float*)val;
        case MPR_DBL:   return (int64_t)*(double*)val;
        case MPR_TYPE:  return (int64_t)*(mpr_type*)val;
        default:        return 0;
    }
}

float mpr_obj_get_prop_as_flt(mpr_obj o, mpr_prop p, const char *s)
{
    const void *val;
    mpr_type type;
    int len;
    RETURN_ARG_UNLESS(o, 0);
    if (s)
        p = mpr_tbl_get_record_by_key(o->props.synced, s, &len, &type, &val, NULL);
    else
        p = mpr_tbl_get_record_by_idx(o->props.synced, p, NULL, &len, &type, &val, NULL);
    RETURN_ARG_UNLESS(val, 0);

    switch (type) {
        case MPR_BOOL:
        case MPR_INT32: return (float)*(int*)val;
        case MPR_INT64: return (float)*(int64_t*)val;
        case MPR_FLT:   return        *(float*)val;
        case MPR_DBL:   return (float)*(double*)val;
        case MPR_TYPE:  return (float)*(mpr_type*)val;
        default:        return 0;
    }
}

const char *mpr_obj_get_prop_as_str(mpr_obj o, mpr_prop p, const char *s)
{
    const void *val;
    mpr_type type;
    int len;
    RETURN_ARG_UNLESS(o, 0);
    if (s)
        p = mpr_tbl_get_record_by_key(o->props.synced, s, &len, &type, &val, NULL);
    else
        p = mpr_tbl_get_record_by_idx(o->props.synced, p, NULL, &len, &type, &val, NULL);
    RETURN_ARG_UNLESS(val && MPR_STR == type && 1 == len, 0);
    return (const char*)val;
}

const void *mpr_obj_get_prop_as_ptr(mpr_obj o, mpr_prop p, const char *s)
{
    const void *val;
    mpr_type type;
    int len;
    RETURN_ARG_UNLESS(o, 0);
    if (s)
        p = mpr_tbl_get_record_by_key(o->props.synced, s, &len, &type, &val, NULL);
    else
        p = mpr_tbl_get_record_by_idx(o->props.synced, p, NULL, &len, &type, &val, NULL);
    RETURN_ARG_UNLESS(val && MPR_PTR == type && 1 == len, 0);
    return val;
}

mpr_obj mpr_obj_get_prop_as_obj(mpr_obj o, mpr_prop p, const char *s)
{
    const void *val;
    mpr_type type;
    int len;
    RETURN_ARG_UNLESS(o, 0);
    if (s)
        p = mpr_tbl_get_record_by_key(o->props.synced, s, &len, &type, &val, NULL);
    else
        p = mpr_tbl_get_record_by_idx(o->props.synced, p, NULL, &len, &type, &val, NULL);
    RETURN_ARG_UNLESS(val && MPR_OBJ >= type && 1 == len, 0);
    return (mpr_obj)val;
}

mpr_list mpr_obj_get_prop_as_list(mpr_obj o, mpr_prop p, const char *s)
{
    const void *val;
    mpr_type type;
    int len;
    RETURN_ARG_UNLESS(o, 0);
    if (s)
        p = mpr_tbl_get_record_by_key(o->props.synced, s, &len, &type, &val, NULL);
    else
        p = mpr_tbl_get_record_by_idx(o->props.synced, p, NULL, &len, &type, &val, NULL);
    RETURN_ARG_UNLESS(val && MPR_LIST == type && 1 == len, 0);
    return (mpr_list)val;
}

mpr_prop mpr_obj_set_prop(mpr_obj o, mpr_prop p, const char *s, int len,
                          mpr_type type, const void *val, int publish)
{
    int flags, updated;
    mpr_tbl tbl;
    RETURN_ARG_UNLESS(o, 0);
    /* TODO: ensure ID property can't be changed by user code */
    if (MPR_PROP_UNKNOWN == p || !MASK_PROP_BITFLAGS(p)) {
        if (!s || '@' == s[0])
            return MPR_PROP_UNKNOWN;
        p = mpr_prop_from_str(s);
    }

    if (!o->props.staged || !publish) {
        tbl = o->props.synced;
        flags = MOD_LOCAL;
    }
    else {
        tbl = o->props.staged;
        flags = MOD_REMOTE;
    }
    if (!publish)
        flags |= LOCAL_ACCESS;
    updated = mpr_tbl_add_record(tbl, p, s, len, type, val, flags);
    if (updated)
        mpr_obj_increment_version(o);
    return updated ? p : MPR_PROP_UNKNOWN;
}

int mpr_obj_remove_prop(mpr_obj o, mpr_prop p, const char *s)
{
    int updated = 0, local;
    RETURN_ARG_UNLESS(o, 0);

    /* check if object represents local resource */
    local = o->props.staged ? 0 : 1;
    if (MPR_PROP_UNKNOWN == p)
        p = mpr_prop_from_str(s);
    if (MPR_PROP_DATA == p || local)
        updated = mpr_tbl_remove_record(o->props.synced, p, s, MOD_LOCAL);
    else if (MPR_PROP_EXTRA == p)
        updated = mpr_tbl_add_record(o->props.staged, p | PROP_REMOVE, s, 0, 0, 0, MOD_REMOTE);
    else
        trace("Cannot remove static property [%d] '%s'\n", p, s ? s : mpr_prop_as_str(p, 1));
    if (updated)
        mpr_obj_increment_version(o);
    return updated ? 1 : 0;
}

void mpr_obj_push(mpr_obj o)
{
    mpr_net n;
    RETURN_UNLESS(o);
    n = mpr_graph_get_net(o->graph);

    if (MPR_DEV == o->type) {
        mpr_dev d = (mpr_dev)o;
        if (o->is_local) {
            RETURN_UNLESS(mpr_dev_get_is_registered(d));
            mpr_net_use_subscribers(n, (mpr_local_dev)d, o->type);
            mpr_dev_send_state(d, MSG_DEV);
        }
        else {
            mpr_net_use_bus(n);
            mpr_dev_send_state(d, MSG_DEV_MOD);
        }
    }
    else if (o->type & MPR_SIG) {
        mpr_sig s = (mpr_sig)o;
        if (o->is_local) {
            mpr_type type = ((MPR_DIR_OUT == mpr_sig_get_dir(s)) ? MPR_SIG_OUT : MPR_SIG_IN);
            mpr_dev d = mpr_sig_get_dev(s);
            RETURN_UNLESS(mpr_dev_get_is_registered(d));
            mpr_net_use_subscribers(n, (mpr_local_dev)d, type);
            mpr_sig_send_state(s, MSG_SIG);
        }
        else {
            mpr_net_use_bus(n);
            mpr_sig_send_state(s, MSG_SIG_MOD);
        }
    }
    else if (o->type & MPR_MAP) {
        int status = o->status;
        mpr_map m = (mpr_map)o;
        mpr_net_use_bus(n);
        if (status & MPR_STATUS_ACTIVE)
            mpr_map_send_state(m, -1, MSG_MAP_MOD);
        else if (o->is_local) {
            status = mpr_local_map_update_status((mpr_local_map)m);
            if (status & MPR_SLOT_DEV_KNOWN)
                mpr_map_send_state(m, -1, MSG_MAP);
            else
                printf("didn't send /map message\n");
        }
        else
            mpr_map_send_state(m, -1, MSG_MAP);
    }
    else {
        trace("mpr_obj_push(): unknown object type %d\n", o->type);
        return;
    }

    /* clear the staged properties */
    FUNC_IF(mpr_tbl_clear, o->props.staged);
}

void mpr_obj_print(mpr_obj o, int staged)
{
    int i = 0, len, num_props;
    mpr_prop p;
    const char *key;
    mpr_type type;
    const void *val;

    RETURN_UNLESS(o && o->props.synced);

    switch (o->type) {
        case MPR_GRAPH:
            mpr_graph_print((mpr_graph)o);
            break;
        case MPR_DEV:
            printf("DEVICE: ");
            mpr_prop_print(1, MPR_DEV, o);
            break;
        case MPR_SIG:
            printf("SIGNAL: ");
            mpr_prop_print(1, MPR_SIG, o);
            break;
        case MPR_MAP:
            printf("MAP: ");
            mpr_prop_print(1, MPR_MAP, o);
            break;
        default:
            trace("mpr_obj_print(): unknown object type %d.", o->type);
            return;
    }

    num_props = mpr_obj_get_num_props(o, 0);
    for (i = 0; i < num_props; i++) {
        p = mpr_tbl_get_record_by_idx(o->props.synced, i, &key, &len, &type, &val, 0);
        die_unless(val != 0 || MPR_LIST == type, "returned zero value\n");

        /* already printed this */
        if (MPR_PROP_NAME == p)
            continue;
        /* don't print device signals as metadata */
        if (MPR_DEV == o->type && MPR_PROP_SIG == p) {
            if (val && MPR_LIST == type)
                mpr_list_free((mpr_list)val);
            continue;
        }

        printf(", %s=", key);

        /* handle pretty-printing a few enum values */
        if (1 == len && MPR_INT32 == type) {
            switch(p) {
                case MPR_PROP_DIR:
                    printf("%s", MPR_DIR_OUT == *(int*)val ? "output" : "input");
                    break;
                case MPR_PROP_PROCESS_LOC:
                    printf("%s", mpr_loc_as_str(*(int*)val));
                    break;
                case MPR_PROP_PROTOCOL:
                    printf("%s", mpr_proto_as_str(*(int*)val));
                    break;
                default:
                    mpr_prop_print(len, type, val);
            }
        }
        else
            mpr_prop_print(len, type, val);

        if (!staged || !o->props.staged)
            continue;

        /* check if staged values exist */
        if (MPR_PROP_EXTRA == p)
            p = mpr_tbl_get_record_by_key(o->props.staged, key, &len, &type, &val, 0);
        else
            p = mpr_tbl_get_record_by_idx(o->props.staged, p, NULL, &len, &type, &val, 0);
        if (MPR_PROP_UNKNOWN != p) {
            printf(" (staged: ");
            mpr_prop_print(len, type, val);
            printf(")");
        }
    }
    if (MPR_MAP == o->type) {
        /* also print slot props */
        mpr_map map = (mpr_map)o;
        for (i = 0; i < mpr_map_get_num_src(map); i++)
            mpr_slot_print(mpr_map_get_src_slot(map, i), 0);
        mpr_slot_print(mpr_map_get_dst_slot(map), 1);
    }
    printf("\n");
}
