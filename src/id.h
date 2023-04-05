
#ifndef __MPR_ID_H__
#define __MPR_ID_H__
#define __MPR_TYPES_H__

#include <stdint.h> /* portable: uint64_t   MSVC: __int64 */

/*! This data structure must be large enough to hold a system pointer or a uint64_t */
typedef uint64_t mpr_id;

#include <string.h>
#include <zlib.h>
#include "util/mpr_inline.h"

MPR_INLINE static mpr_id mpr_id_from_str(const char *str)
    { return (mpr_id) crc32(0L, (const Bytef *)str, strlen(str)) << 32; }

#endif /* __MPR_ID_H__ */
