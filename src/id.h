
#ifndef __MPR_ID_H__
#define __MPR_ID_H__
#define __MPR_TYPES_H__

#include <stdint.h> /* portable: uint64_t   MSVC: __int64 */

/*! This data structure must be large enough to hold a system pointer or a uint64_t */
typedef uint64_t mpr_id;

#endif /* __MPR_ID_H__ */
