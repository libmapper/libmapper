
#ifndef __MAPPER_OBJECT_H__
#define __MAPPER_OBJECT_H__

#include "util/mpr_inline.h"

void mpr_obj_init(mpr_obj obj, mpr_graph graph, mpr_type type);

void mpr_obj_free(mpr_obj obj);

void mpr_obj_increment_version(mpr_obj obj);

mpr_id mpr_obj_get_id(mpr_obj obj);

void mpr_obj_set_id(mpr_obj obj, mpr_id id);

MPR_INLINE static int mpr_obj_get_is_local(mpr_obj obj) { return obj->is_local; }

MPR_INLINE static void mpr_obj_set_is_local(mpr_obj obj, int val) { obj->is_local = val; }

int mpr_obj_get_version(mpr_obj obj);

void mpr_obj_set_version(mpr_obj obj, int val);

void mpr_obj_clear_empty(mpr_obj obj);

mpr_tbl mpr_obj_get_prop_tbl(mpr_obj obj);

#endif /* __MAPPER_OBJECT_H__ */
