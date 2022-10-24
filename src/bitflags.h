
#ifndef __MAPPER_BITFLAGS_H__
#define __MAPPER_BITFLAGS_H__

#include "util/mpr_inline.h"

MPR_INLINE static void mpr_bitflags_set(char *bytearray, int idx)
{
    bytearray[idx / 8] |= 1 << (idx % 8);
}

MPR_INLINE static void mpr_bitflags_unset(char *bytearray, int idx)
{
    bytearray[idx / 8] &= (0xFF ^ (1 << (idx % 8)));
}

MPR_INLINE static int mpr_bitflags_get(char *bytearray, int idx)
{
    return bytearray[idx / 8] & 1 << (idx % 8);
}

MPR_INLINE static int mpr_bitflags_compare(char *l, char *r, int num_flags)
{
    return memcmp(l, r, num_flags / 8 + 1);
}

MPR_INLINE static void mpr_bitflags_clear(char *bytearray, int num_flags)
{
    memset(bytearray, 0, num_flags / 8 + 1);
}

#endif /* __MAPPER_BITFLAGS_H__ */
