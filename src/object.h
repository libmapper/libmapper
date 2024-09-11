
#ifndef __MPR_OBJECT_H__
#define __MPR_OBJECT_H__
#define __MPR_TYPES_H__

typedef struct _mpr_obj *mpr_obj;

typedef struct _mpr_dict {
    struct _mpr_tbl *synced;
    struct _mpr_tbl *staged;
} mpr_dict_t, *mpr_dict;

#include "id.h"
#include "mpr_type.h"

typedef struct _mpr_obj
{
    struct _mpr_graph *graph;       /*!< Pointer back to the graph. */
    mpr_id id;                      /*!< Unique id for this object. */
    void *data;                     /*!< User context pointer. */
    struct _mpr_dict props;         /*!< Properties associated with this signal. */
    int is_local;
    int version;                    /*!< Version number. */
    uint16_t status;
    mpr_type type;                  /*!< Object type. */
} mpr_obj_t;

#include "graph.h"
#include "util/mpr_inline.h"
#include "table.h"

void mpr_obj_init(mpr_obj obj, mpr_graph graph, mpr_type type);

void mpr_obj_free(mpr_obj obj);

void mpr_obj_incr_version(mpr_obj obj);

MPR_INLINE static mpr_id mpr_obj_get_id(mpr_obj obj)
    { return obj->id; }

MPR_INLINE static void mpr_obj_set_id(mpr_obj obj, mpr_id id)
    { obj->id = id; }

MPR_INLINE static void mpr_obj_set_status(mpr_obj obj, int add, int remove)
    { obj->status = (obj->status | add) & ~remove; }

MPR_INLINE static int mpr_obj_get_is_local(mpr_obj obj)
    { return obj->is_local; }

MPR_INLINE static void mpr_obj_set_is_local(mpr_obj obj, int val)
    { obj->is_local = val; }

MPR_INLINE static int mpr_obj_get_version(mpr_obj obj)
    { return obj->version; }

MPR_INLINE static void mpr_obj_set_version(mpr_obj obj, int ver)
    { obj->version = ver; }

MPR_INLINE static void mpr_obj_clear_empty_props(mpr_obj obj)
    { mpr_tbl_clear_empty_records(obj->props.synced); }

MPR_INLINE static mpr_tbl mpr_obj_get_prop_tbl(mpr_obj obj)
    { return obj->props.synced; }

MPR_INLINE static void mpr_obj_add_props_to_msg(mpr_obj obj, lo_message msg)
    { mpr_tbl_add_to_msg(obj->is_local ? obj->props.synced : 0, obj->props.staged, msg); }

#endif /* __MPR_OBJECT_H__ */
