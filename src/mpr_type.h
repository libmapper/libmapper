
#ifndef __MPR_TYPE_H__
#define __MPR_TYPE_H__

#include <mapper/mapper_constants.h>
#include "mpr_time.h"
#include "util/mpr_debug.h"
#include "util/mpr_inline.h"

typedef char mpr_type;

/*! Helper to find size of signal value types. */
MPR_INLINE static int mpr_type_get_size(mpr_type type)
{
    if (type <= MPR_LIST)   return sizeof(void*);
    switch (type) {
        case MPR_INT32:
        case MPR_BOOL:
        case 'T':
        case 'F':           return sizeof(int);
        case MPR_FLT:       return sizeof(float);
        case MPR_DBL:       return sizeof(double);
        case MPR_PTR:       return sizeof(void*);
        case MPR_STR:       return sizeof(char*);
        case MPR_INT64:     return sizeof(int64_t);
        case MPR_TIME:      return sizeof(mpr_time);
        case MPR_TYPE:      return sizeof(mpr_type);
        default:
            die_unless(0, "Unknown type '%c' in mpr_type_get_size().\n", type);
            return 0;
    }
}

/*! Helper to check if type is a number. */
MPR_INLINE static int mpr_type_get_is_num(mpr_type type)
{
    switch (type) {
        case MPR_INT32:
        case MPR_FLT:
        case MPR_DBL:
            return 1;
        default:    return 0;
    }
}

/*! Helper to check if type is a boolean. */
MPR_INLINE static int mpr_type_get_is_bool(mpr_type type)
{
    return 'T' == type || 'F' == type;
}

/*! Helper to check if type is a string. */
MPR_INLINE static int mpr_type_get_is_str(mpr_type type)
{
    return MPR_STR == type;
}

/*! Helper to check if type is a string or void* */
MPR_INLINE static int mpr_type_get_is_ptr(mpr_type type)
{
    return MPR_PTR == type || MPR_STR == type;
}

#endif /* __MPR_TYPE_H__ */
