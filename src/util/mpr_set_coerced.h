#ifndef __MPR_SET_COERCED_H__
#define __MPR_SET_COERCED_H__

#include <mapper/mapper_types.h>

/* Helper for setting property value from different data types */
int mpr_set_coerced(int src_len, mpr_type src_type, const void *src_val,
                    int dst_len, mpr_type dst_type, void *dst_val);

#endif /* __MPR_SET_COERCED_H__ */
