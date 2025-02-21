
#ifndef __MPR_BITFLAGS_H__
#define __MPR_BITFLAGS_H__

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "util/mpr_inline.h"

typedef char *mpr_bitflags;

/* A mpr_bitflags object consists of a char array with at least num_flags bits.
 * The bit 0 is used to indicate whether all bits are set.
 * Bits 1–7 are used to store the length of the bitflag array.
 * On allocation any extra bits are set to 1 for efficient comparison. */

MPR_INLINE static mpr_bitflags mpr_bitflags_new(unsigned int num_flags)
{
    mpr_bitflags bitflags;
    if (!num_flags)
        return 0;
    /* allocate an extra byte for length prefix */
    unsigned int num_bytes = (num_flags - 1) / 8 + 2;
    assert(num_flags < 128);
    bitflags = calloc(1, num_bytes);
    if (num_flags % 8) {
        /* set extraneous bits to one */
        bitflags[num_bytes - 1] |= 255 << (num_flags % 8);
    }
    bitflags[0] = num_flags << 1;
    return bitflags;
}

MPR_INLINE static void mpr_bitflags_free(mpr_bitflags bitflags)
{
    if (bitflags)
        free(bitflags);
}

MPR_INLINE static mpr_bitflags mpr_bitflags_realloc(mpr_bitflags bitflags,
                                                    unsigned int new_num_flags)
{
    unsigned int old_num_flags = bitflags[0] >> 1;
    unsigned int old_num_bytes = (old_num_flags - 1) / 8 + 2;

    if (new_num_flags < old_num_flags) {
        char all_set = bitflags[0] & 0x01;
        unsigned int new_num_bytes = (new_num_flags - 1) / 8 + 2;
        if (new_num_bytes < old_num_bytes)
            bitflags = realloc(bitflags, new_num_bytes);
        bitflags[0] = (new_num_flags << 1) | all_set;
    }
    else if (new_num_flags > (bitflags[0] >> 1)) {
        mpr_bitflags new_bitflags = mpr_bitflags_new(new_num_flags);
        int i = old_num_bytes - 1;
        new_bitflags[i] |= (bitflags[i] & (255 >> (8 - (old_num_flags % 8))));
        while (--i)
            new_bitflags[i] = bitflags[i];
        /* leave all_set flag at zero since new flags have not been set */
        free(bitflags);
        bitflags = new_bitflags;
    }
    return bitflags;
}

MPR_INLINE static void mpr_bitflags_set(mpr_bitflags bitflags, unsigned int idx)
{
    bitflags[idx / 8 + 1] |= (1 << (idx % 8));
}

MPR_INLINE static void mpr_bitflags_set_all(mpr_bitflags bitflags)
{
    memset(bitflags + 1, 255, ((bitflags[0] >> 1) - 1) / 8 + 1);
    bitflags[0] |= 0x01;
}

MPR_INLINE static int mpr_bitflags_get_all(mpr_bitflags bitflags)
{
    if (bitflags[0] & 0x01)
        return 1;
    else {
        unsigned int i, num_bytes = ((bitflags[0] >> 1) - 1) / 8 + 2;
        for (i = 1; i < num_bytes; i++) {
            if (bitflags[i] != (char)0xFF)
                return 0;
        }
        bitflags[0] |= 0x01;
        return 1;
    }
}

MPR_INLINE static void mpr_bitflags_unset(mpr_bitflags bitflags, unsigned int idx)
{
    bitflags[idx / 8 + 1] &= (0xFF ^ (1 << (idx % 8)));
    bitflags[0] &= ~0x01;
}

MPR_INLINE static int mpr_bitflags_get(mpr_bitflags bitflags, unsigned int idx)
{
    return bitflags[idx / 8 + 1] & (1 << (idx % 8));
}

MPR_INLINE static int mpr_bitflags_compare(mpr_bitflags l, mpr_bitflags r)
{
    return (l[0] != r[0]) || memcmp(l, r, ((l[0] >> 1) - 1) / 8 + 2);
}

MPR_INLINE static void mpr_bitflags_clear(mpr_bitflags bitflags)
{
    unsigned int num_flags = bitflags[0] >> 1;
    unsigned int num_bytes = (num_flags - 1) / 8 + 1;
    memset(bitflags + 1, 0, num_bytes);
    if (num_flags % 8) {
        /* set extraneous bits to one */
        bitflags[num_bytes] |= 255 << (num_flags % 8);
    }
    bitflags[0] &= ~0x01;
}

MPR_INLINE static void mpr_bitflags_cpy(mpr_bitflags dst, mpr_bitflags src)
{
    /* TODO: check whether sizes match? */
    memcpy(dst, src, ((src[0] >> 1) - 1) / 8 + 2);
}

MPR_INLINE static void mpr_bitflags_print(mpr_bitflags bitflags)
{
    unsigned int i, num_flags = bitflags[0] >> 1;
    printf("%d:[", num_flags);
    for (i = 0; i < num_flags; i++)
        printf("%d", mpr_bitflags_get(bitflags, i) ? 1 : 0);
    num_flags = (ceil(num_flags / 8.f) * 8.f);
    printf("][");
    for (; i < num_flags; i++)
        printf("%d", mpr_bitflags_get(bitflags, i) ? 1 : 0);
    printf("]");
    if (bitflags[0] & 0x01)
        printf("*");
}

#endif /* __MPR_BITFLAGS_H__ */
