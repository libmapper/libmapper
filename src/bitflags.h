
#ifndef __MPR_BITFLAGS_H__
#define __MPR_BITFLAGS_H__

#include <stdlib.h>
#include <string.h>
#include "util/mpr_inline.h"

typedef char *mpr_bitflags;

MPR_INLINE static char *mpr_bitflags_new(unsigned int num_flags)
{
    return calloc(1, (num_flags - 1) / 8 + 1);
}

MPR_INLINE static void mpr_bitflags_free(mpr_bitflags bitflags)
{
    if (bitflags)
        free(bitflags);
}

MPR_INLINE static char *mpr_bitflags_realloc(mpr_bitflags bitflags, unsigned int old_num_flags,
                                             unsigned int new_num_flags)
{
    if (new_num_flags < old_num_flags)
        return realloc(bitflags, (new_num_flags - 1) / 8 + 1);
    else if (new_num_flags > old_num_flags) {
        char *bitflags2 = mpr_bitflags_new(new_num_flags);
        memcpy(bitflags2, bitflags, (old_num_flags - 1) / 8 + 1);
        free(bitflags);
        return bitflags2;
    }
    else
        return bitflags;
}

MPR_INLINE static void mpr_bitflags_set(mpr_bitflags bitflags, unsigned int idx)
{
    bitflags[idx / 8] |= (1 << (idx % 8));
}

MPR_INLINE static void mpr_bitflags_set_all(mpr_bitflags bitflags, unsigned int num_flags)
{
    memset(bitflags, 255, (num_flags - 1) / 8 + 1);
}

MPR_INLINE static void mpr_bitflags_unset(mpr_bitflags bitflags, unsigned int idx)
{
    bitflags[idx / 8] &= (0xFF ^ (1 << (idx % 8)));
}

MPR_INLINE static int mpr_bitflags_get(mpr_bitflags bitflags, unsigned int idx)
{
    return bitflags[idx / 8] & (1 << (idx % 8));
}

MPR_INLINE static int mpr_bitflags_compare(mpr_bitflags l, mpr_bitflags r, unsigned int num_flags)
{
    return memcmp(l, r, (num_flags - 1) / 8 + 1);
}

MPR_INLINE static void mpr_bitflags_clear(mpr_bitflags bitflags, unsigned int num_flags)
{
    memset(bitflags, 0, (num_flags - 1) / 8 + 1);
}

MPR_INLINE static void mpr_bitflags_cpy(mpr_bitflags dst, mpr_bitflags src,
                                        unsigned int num_flags)
{
    memcpy(dst, src, (num_flags - 1) / 8 + 1);
}

#endif /* __MPR_BITFLAGS_H__ */
