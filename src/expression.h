
#ifndef __MPR_EXPRESSION_H__
#define __MPR_EXPRESSION_H__

typedef struct _mpr_expr *mpr_expr;
typedef struct _mpr_expr_stack *mpr_expr_stack;

#include "mpr_time.h"
#include "value.h"

#define EXPR_RELEASE_BEFORE_UPDATE 0x02
#define EXPR_RELEASE_AFTER_UPDATE  0x04
#define EXPR_MUTED_UPDATE          0x08
#define EXPR_UPDATE                0x10
#define EXPR_EVAL_DONE             0x20

mpr_expr mpr_expr_new_from_str(mpr_expr_stack eval_stk, const char *str, int num_in,
                               const mpr_type *in_types, const int *in_vec_lens,
                               mpr_type out_type, int out_vec_len);

int mpr_expr_get_in_hist_size(mpr_expr expr, int idx);

int mpr_expr_get_out_hist_size(mpr_expr expr);

int mpr_expr_get_num_vars(mpr_expr expr);

int mpr_expr_get_var_vec_len(mpr_expr expr, int idx);

int mpr_expr_get_var_type(mpr_expr expr, int idx);

int mpr_expr_get_var_is_instanced(mpr_expr expr, int idx);

int mpr_expr_get_src_is_muted(mpr_expr expr, int idx);

const char *mpr_expr_get_var_name(mpr_expr expr, int idx);

int mpr_expr_get_manages_inst(mpr_expr expr);

void mpr_expr_var_updated(mpr_expr expr, int var_idx);

#ifdef DEBUG
void printexpr(const char*, mpr_expr);
#endif

/*! Evaluate the given inputs using the compiled expression.
 *  \param stk          A preallocated expression eval stack.
 *  \param expr         The expression to use.
 *  \param srcs         An array of `mpr_value` structures for sources.
 *  \param expr_vars    An array of `mpr_value` structures for user variables.
 *  \param result       A `mpr_value` structure for receiving the evaluation result.
 *  \param time         The timestamp to associate with this evaluation.
 *  \param types        An array of `mpr_type` for storing the output type per vector element
 *  \param inst_idx     Index of the instance being updated.
 *  \result             0 if the expression evaluation caused no change, or a bitflags consisting of
 *                      `MPR_SIG_UPDATE` (if an update was generated), `MPR_SIG_REL_UPSTRM` (if the
 *                      expression generated an instance release before the update), and
 *                      `MPR_SIG_REL_DNSRTM` (if the expression generated an instance release after
 *                      an update). */
int mpr_expr_eval(mpr_expr_stack stk, mpr_expr expr, mpr_value *srcs, mpr_value *expr_vars,
                  mpr_value result, mpr_time *time, mpr_type *types, int inst_idx);

int mpr_expr_get_num_input_slots(mpr_expr expr);

void mpr_expr_free(mpr_expr expr);

mpr_expr_stack mpr_expr_stack_new(void);

void mpr_expr_stack_free(mpr_expr_stack stk);

#endif /* __MPR_EXPRESSION_H__ */
