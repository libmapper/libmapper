#ifndef __MPR_EXPR_BUFFER_H__
#define __MPR_EXPR_BUFFER_H__

#include "expr_value.h"

/* could we use mpr_value here instead, with stack idx instead of history idx?
 * pro: vectors, commonality with I/O
 * con: timetags wasted
 * option: create version with unallocated timetags */
typedef struct _ebuffer
{
    evalue vals;
    mpr_type *types;
    uint8_t *lens;
    unsigned int size;
    unsigned int len;
} *ebuffer;

ebuffer ebuffer_new(void)
{
    ebuffer buff = (ebuffer) calloc(1, sizeof(struct _ebuffer));
    return buff;
}

/* Reallocate evaluation stack if necessary. */
void ebuffer_realloc(ebuffer buff, uint8_t num_slots, uint8_t vec_len)
{
    if (buff->len < num_slots) {
        buff->len = num_slots;
        if (buff->types)
            buff->types = realloc(buff->types, buff->len * sizeof(mpr_type));
        else
            buff->types = malloc(buff->len * sizeof(mpr_type));
        if (buff->lens)
            buff->lens = realloc(buff->lens, buff->len * sizeof(uint8_t));
        else
            buff->lens = malloc(buff->len * sizeof(uint8_t));
    }

    /* evaluation buffer size needs to multiplied by vector length */
    if (!vec_len)
        vec_len = 1;
    if (buff->size < num_slots * vec_len) {
        buff->size = num_slots * vec_len;
        if (buff->vals)
            buff->vals = realloc(buff->vals, buff->size * sizeof(evalue_t));
        else
            buff->vals = malloc(buff->size * sizeof(evalue_t));
    }
}

void ebuffer_free(ebuffer buff)
{
    FUNC_IF(free, buff->vals);
    FUNC_IF(free, buff->types);
    FUNC_IF(free, buff->lens);
    free(buff);
}

#endif /* __MPR_EXPR_BUFFER_H__ */
