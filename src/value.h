
#ifndef __MPR_VALUE_H__
#define __MPR_VALUE_H__

#include <string.h>
#include "util/mpr_inline.h"
#include "mpr_type.h"

/*! A structure that stores the current and historical values of a signal. The
 *  size of the history array is determined by the needs of mapping expressions.
 *  @ingroup signals */

typedef struct _mpr_value *mpr_value;

mpr_value mpr_value_new(unsigned int vlen, mpr_type type, unsigned int mlen, unsigned int num_inst);

void mpr_value_realloc(mpr_value val, unsigned int vec_len, mpr_type type,
                       unsigned int mem_len, unsigned int num_inst, int reset);

void mpr_value_reset_inst(mpr_value v, unsigned int inst_idx, mpr_time t);

int mpr_value_remove_inst(mpr_value v, unsigned int inst_idx);

void* mpr_value_get_value(mpr_value v, unsigned int inst_idx, int hist_idx);

void mpr_value_set_next(mpr_value v, unsigned int inst_idx, const void *s, mpr_time *t);

int mpr_value_set_element(mpr_value v, unsigned int inst_idx, int el_idx, void *new);

int mpr_value_set_next_coerced(mpr_value v, unsigned int inst_idx, unsigned int len,
                               mpr_type type, const void *s, mpr_time *t);

mpr_time* mpr_value_get_time(mpr_value v, unsigned int inst_idx, int hist_idx);

void mpr_value_set_time(mpr_value v, mpr_time t, unsigned int inst_idx, int hist_idx);

void mpr_value_incr_idx(mpr_value v, unsigned int inst_idx);

void mpr_value_decr_idx(mpr_value v, unsigned int inst_idx);

unsigned int mpr_value_get_num_samps(mpr_value v, unsigned int inst_idx);

void mpr_value_free(mpr_value v);

unsigned int mpr_value_get_vlen(mpr_value v);
unsigned int mpr_value_get_mlen(mpr_value v);
unsigned int mpr_value_get_num_inst(mpr_value v);
unsigned int mpr_value_get_num_active_inst(mpr_value v);
mpr_type mpr_value_get_type(mpr_value v);

int mpr_value_cmp(mpr_value v, unsigned int inst_idx, int hist_idx, const void *ptr);

#ifdef DEBUG
void mpr_value_print(mpr_value v);
int mpr_value_print_inst(mpr_value v, unsigned int inst_idx);
int mpr_value_print_inst_hist(mpr_value v, unsigned int inst_idx);
#endif

#endif /* __MPR_VALUE_H__ */
