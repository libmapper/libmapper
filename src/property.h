
#ifndef __MPR_PROPERTY_H__
#define __MPR_PROPERTY_H__

#include "mpr_type.h"

/*! Helper for printing typed values.
 *  \param len          The vector length of the value.
 *  \param type         The value type.
 *  \param val          A pointer to the property value to print. */
void mpr_prop_print(int len, mpr_type type, const void *val);

mpr_prop mpr_prop_from_str(const char *str);

const char *mpr_prop_as_str(mpr_prop prop, int skip_slash);

int mpr_prop_get_len(mpr_prop p);

int mpr_prop_get_protocol_type(mpr_prop p);

const char *mpr_dir_as_str(mpr_dir dir);

mpr_dir mpr_dir_from_str(const char *string);

const char *mpr_loc_as_str(mpr_loc loc);

mpr_loc mpr_loc_from_str(const char *string);

const char *mpr_proto_as_str(mpr_proto pro);

mpr_proto mpr_proto_from_str(const char *string);

const char *mpr_steal_type_as_str(mpr_steal_type stl);

#endif /* __MPR_PROPERTY_H__ */
