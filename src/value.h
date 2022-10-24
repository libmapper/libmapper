
#ifndef __MAPPER_VALUE_H__
#define __MAPPER_VALUE_H__

#include "types_internal.h"
#include "util/mpr_inline.h"
#include "mpr_type.h"
#include <mapper/mapper.h>

void mpr_value_realloc(mpr_value val, unsigned int vec_len, mpr_type type,
                       unsigned int mem_len, unsigned int num_inst, int is_output);

void mpr_value_reset_inst(mpr_value v, int idx);

int mpr_value_remove_inst(mpr_value v, int idx);

void mpr_value_set_samp(mpr_value v, int idx, void *s, mpr_time t);

/*! Helper to find the pointer to the current value in a mpr_value_t. */
MPR_INLINE static void* mpr_value_get_samp(mpr_value v, int idx)
{
    mpr_value_buffer b = &v->inst[idx % v->num_inst];
    return (char*)b->samps + b->pos * v->vlen * mpr_type_get_size(v->type);
}

MPR_INLINE static void* mpr_value_get_samp_hist(mpr_value v, int inst_idx, int hist_idx)
{
    mpr_value_buffer b = &v->inst[inst_idx % v->num_inst];
    int idx = (b->pos + v->mlen + hist_idx) % v->mlen;
    if (idx < 0)
        idx += v->mlen;
    return (char*)b->samps + idx * v->vlen * mpr_type_get_size(v->type);
}

/*! Helper to find the pointer to the current time in a mpr_value_t. */
MPR_INLINE static mpr_time* mpr_value_get_time(mpr_value v, int idx)
{
    mpr_value_buffer b = &v->inst[idx % v->num_inst];
    return &b->times[b->pos];
}

MPR_INLINE static mpr_time* mpr_value_get_time_hist(mpr_value v, int inst_idx, int hist_idx)
{
    mpr_value_buffer b = &v->inst[inst_idx % v->num_inst];
    int idx = (b->pos + v->mlen + hist_idx) % v->mlen;
    if (idx < 0)
        idx += v->mlen;
    return &b->times[idx];
}

void mpr_value_free(mpr_value v);

#ifdef DEBUG
void mpr_value_print(mpr_value v, int inst_idx);
void mpr_value_print_hist(mpr_value v, int inst_idx);
#endif

#endif /* __MAPPER_VALUE_H__ */
