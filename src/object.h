
#ifndef __MAPPER_OBJECT_H__
#define __MAPPER_OBJECT_H__

#include "util/mpr_inline.h"

/**** Objects ****/
void mpr_obj_increment_version(mpr_obj obj);

MPR_INLINE static int mpr_obj_get_is_local(mpr_obj obj) { return obj->is_local; }

MPR_INLINE static void mpr_obj_set_is_local(mpr_obj obj, int val) { obj->is_local = val; }

#endif /* __MAPPER_OBJECT_H__ */
