
#ifndef __MPR_BITFLAGS_H__
#define __MPR_BITFLAGS_H__

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "util/mpr_inline.h"

typedef char *mpr_bitflags;

/* A mpr_bitflags object consists of a char array with at least num_flags bits. On allocation any
 * extra bits are set to 1 for efficient comparison. */

MPR_INLINE static char *mpr_bitflags_new(unsigned int num_flags)
{
    if (!num_flags)
        return 0;
    unsigned int num_bytes = (num_flags - 1) / 8 + 1;
    char* bitflags = calloc(1, num_bytes);
    if (num_flags % 8) {
        /* set extraneous bits to one */
        bitflags[num_bytes - 1] |= 255 << (num_flags % 8);
    }
    return bitflags;
}

MPR_INLINE static void mpr_bitflags_free(mpr_bitflags bitflags)
{
    if (bitflags)
        free(bitflags);
}

MPR_INLINE static char *mpr_bitflags_realloc(mpr_bitflags bitflags, unsigned int old_num_flags,
                                             unsigned int new_num_flags)
{
    if (!bitflags)
        return mpr_bitflags_new(new_num_flags);
    if (new_num_flags < old_num_flags)
        return realloc(bitflags, (new_num_flags - 1) / 8 + 1);
    if (new_num_flags > old_num_flags) {
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

MPR_INLINE static int mpr_bitflags_get_all(mpr_bitflags bitflags, unsigned int num_flags)
{
    unsigned int i, num_bytes = (num_flags - 1) / 8 + 1;
    for (i = 0; i < num_bytes; i++) {
        if (bitflags[i] != (char)0xFF)
            return 0;
    }
    return 1;
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
    unsigned int num_bytes = (num_flags - 1) / 8 + 1;
    memset(bitflags, 0, num_bytes);
    if (num_flags % 8) {
        /* set extraneous bits to one */
        bitflags[num_bytes - 1] |= 255 << (num_flags % 8);
    }
}

MPR_INLINE static void mpr_bitflags_cpy(mpr_bitflags dst, mpr_bitflags src,
                                        unsigned int num_flags)
{
    memcpy(dst, src, (num_flags - 1) / 8 + 1);
}

MPR_INLINE static void mpr_bitflags_print(mpr_bitflags bitflags, unsigned int num_flags)
{
    unsigned int i;
    printf("[");
    for (i = 0; i < num_flags; i++)
        printf("%d", mpr_bitflags_get(bitflags, i) ? 1 : 0);
    printf("]");
}

#endif /* __MPR_BITFLAGS_H__ */
