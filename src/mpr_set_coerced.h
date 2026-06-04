#ifndef __MPR_SET_COERCED_H__
#define __MPR_SET_COERCED_H__

#include <mapper/mapper_types.h>

/*! Helper for setting property value from different data types.
 * \param src_len   Vector length of the value to be copied.
 * \param src_type  Data type of the value to be copied.
 * \param src_val   Pointer to the value to be copied.
 * \param dst_len   Expected vector length of the destination value.
 * \param dst_type  Expected data type of the destination value.
 * \param dst_val   Pointer to the destination value.
 * \return          0 if value was successfully coerced and modified the destination value. */
int mpr_set_coerced(int src_len, mpr_type src_type, const void *src_val,
                    int dst_len, mpr_type dst_type, void *dst_val);

#endif /* __MPR_SET_COERCED_H__ */
