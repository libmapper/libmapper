
#ifndef __MPR_EXPRESSION_H__
#define __MPR_EXPRESSION_H__

typedef struct _mpr_expr *mpr_expr;
typedef struct _ebuffer *mpr_expr_eval_buffer;

#include "bitflags.h"
#include "mpr_time.h"
#include "value.h"

#define EXPR_RELEASE_BEFORE_UPDATE 0x02
#define EXPR_RELEASE_AFTER_UPDATE  0x04
#define EXPR_MUTED_UPDATE          0x08
#define EXPR_UPDATE                0x10
#define EXPR_EVAL_DONE             0x20

mpr_expr mpr_expr_new(unsigned int num_src, unsigned int num_dst, void *stack);

mpr_expr mpr_expr_new_from_str(const char *str, unsigned int num_src, const mpr_type *src_types,
                               const unsigned int *src_lens, unsigned int num_dst,
                               const mpr_type *dst_types, const unsigned int *dst_lens);

void mpr_expr_free(mpr_expr expr);

int mpr_expr_get_src_mlen(mpr_expr expr, int idx);

int mpr_expr_get_dst_mlen(mpr_expr expr, int idx);

int mpr_expr_get_num_vars(mpr_expr expr);

int mpr_expr_get_var_vlen(mpr_expr expr, int idx);

int mpr_expr_get_var_type(mpr_expr expr, int idx);

int mpr_expr_get_var_is_instanced(mpr_expr expr, int idx);

int mpr_expr_get_src_is_muted(mpr_expr expr, int idx);

const char *mpr_expr_get_var_name(mpr_expr expr, int idx);

int mpr_expr_get_manages_inst(mpr_expr expr);

void mpr_expr_set_var_updated(mpr_expr expr, int var_idx);

/*! Evaluate the given inputs using the compiled expression.
 *  \param buff         A preallocated expression evaluation buffer.
 *  \param expr         The expression to use.
 *  \param srcs         An array of `mpr_value` structures for sources.
 *  \param expr_vars    An array of `mpr_value` structures for user variables.
 *  \param result       A `mpr_value` structure for receiving the evaluation result.
 *  \param time         The timestamp to associate with this evaluation.
 *  \param types        An array of `mpr_type` for storing the output type per vector element
 *  \param inst_idx     Index of the instance being updated.
 *  \result             0 if the expression evaluation caused no change, or a bitflags consisting of
 *                      `EXPR_UPDATE` or `EXPR_MUTED_UPDATE` (if an update was generated),
 *                      `EXPR_RELEASE_BEFORE_UPDATE` or `EXPR_RELEASE_AFTER_UPDATE` (if the
 *                      expression generated an instance release), `EXPR_EVAL_DONE` (if the
 *                      expression will not generate different results for different source
 *                      instances because all instances are reduced using e.g.
 *                      `y=x.instance.mean()`). */
int mpr_expr_eval(mpr_expr expr, mpr_expr_eval_buffer buff, mpr_value *srcs, mpr_value *expr_vars,
                  mpr_value result, mpr_time *time, mpr_bitflags has_value, int inst_idx);

int mpr_expr_get_num_src(mpr_expr expr);

mpr_expr_eval_buffer mpr_expr_new_eval_buffer(mpr_expr expr);
void mpr_expr_realloc_eval_buffer(mpr_expr expr, mpr_expr_eval_buffer buff);
void mpr_expr_free_eval_buffer(mpr_expr_eval_buffer eval_buff);

void mpr_expr_update_mlen(mpr_expr expr, int idx, unsigned int mlen);

void mpr_expr_cpy_stack_and_vars(mpr_expr expr, void *stack, void *vars, int num_var);

#if DEBUG
void mpr_expr_print(mpr_expr expr);
#endif /* DEBUG */

#endif /* __MPR_EXPRESSION_H__ */
