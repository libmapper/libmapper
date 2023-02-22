
#ifndef __MPR_OBJECT_H__
#define __MPR_OBJECT_H__
#define __MPR_TYPES_H__

#include <stdint.h> /* portable: uint64_t   MSVC: __int64 */

typedef struct _mpr_obj *mpr_obj;

/*! This data structure must be large enough to hold a system pointer or a uint64_t */
typedef uint64_t mpr_id;

typedef struct _mpr_dict {
    struct _mpr_tbl *synced;
    struct _mpr_tbl *staged;
} mpr_dict_t, *mpr_dict;

#include "mpr_type.h"

typedef struct _mpr_obj
{
    struct _mpr_graph *graph;       /*!< Pointer back to the graph. */
    mpr_id id;                      /*!< Unique id for this object. */
    void *data;                     /*!< User context pointer. */
    struct _mpr_dict props;         /*!< Properties associated with this signal. */
    int is_local;
    int version;                    /*!< Version number. */
    mpr_type type;                  /*!< Object type. */
} mpr_obj_t;

#include "graph.h"
#include "util/mpr_inline.h"
#include "table.h"

void mpr_obj_init(mpr_obj obj, mpr_graph graph, mpr_type type);

void mpr_obj_free(mpr_obj obj);

void mpr_obj_increment_version(mpr_obj obj);

mpr_id mpr_obj_get_id(mpr_obj obj);

void mpr_obj_set_id(mpr_obj obj, mpr_id id);

MPR_INLINE static int mpr_obj_get_is_local(mpr_obj obj) { return obj->is_local; }

MPR_INLINE static void mpr_obj_set_is_local(mpr_obj obj, int val) { obj->is_local = val; }

int mpr_obj_get_version(mpr_obj obj);

void mpr_obj_set_version(mpr_obj obj, int val);

void mpr_obj_clear_empty_props(mpr_obj obj);

mpr_tbl mpr_obj_get_prop_tbl(mpr_obj obj);

#endif /* __MPR_OBJECT_H__ */
