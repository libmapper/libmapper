#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "map.h"
#include "expression.h"
#include "expression/expr_buffer.h"
#include "expression/expr_evaluator.h"
#include "expression/expr_function.h"
#include "expression/expr_operator.h"
#include "expression/expr_parser.h"
#include "expression/expr_stack.h"
#include "expression/expr_struct.h"
#include "expression/expr_token.h"
#include "expression/expr_trace.h"
#include "expression/expr_value.h"
#include "expression/expr_variable.h"
#include <mapper/mapper.h>

#ifdef _MSC_VER
#include <malloc.h>
#endif

#define OWN_STACK    0x01
#define MANAGES_INST 0x02
#define REDUCES_INST 0x04

/* Reallocate evaluation stack if necessary. */
void mpr_expr_realloc_eval_buffer(mpr_expr expr, mpr_expr_eval_buffer buff)
{
    ebuffer_realloc(buff, estack_get_eval_buffer_size(expr->stack), expr->stack->vec_len);
}

mpr_expr_eval_buffer mpr_expr_new_eval_buffer(mpr_expr expr)
{
    mpr_expr_eval_buffer buff = ebuffer_new();
    if (expr)
        ebuffer_realloc(buff, estack_get_eval_buffer_size(expr->stack), expr->stack->vec_len);
    return buff;
}

void mpr_expr_free_eval_buffer(mpr_expr_eval_buffer buff)
{
    ebuffer_free(buff);
}

mpr_expr mpr_expr_new(unsigned int num_src, unsigned int num_dst, void *stack)
{
    int i;
    mpr_expr expr = calloc(1, sizeof(struct _mpr_expr));

    expr->num_src = num_src;
    expr->src_mlen = calloc(1, sizeof(uint16_t) * num_src);
    /* default to mlen = 1 */
    for (i = 0; i < num_src; i++)
        expr->src_mlen[i] = 1;
    expr->max_src_mlen = 1;

    expr->dst_mlen = 1;

    expr->inst_ctl = -1;
    expr->mute_ctl = -1;

    if (stack) {
        expr->stack = (estack)stack;

#if TRACE_PARSE
        printf("Created new expression with %d tokens\n", expr->stack->num_tokens);
        mpr_expr_print(expr);
#endif
    }
    else
        expr->stack = estack_new(0);

    return expr;
}

void mpr_expr_cpy_stack_and_vars(mpr_expr expr, void *stack, void *vars, int num_var)
{
    estack_cpy(expr->stack, (estack)stack);
    expr->flags |= OWN_STACK;

    if (num_var) {
        int i;
        expr->num_vars = num_var;
        expr->vars = malloc(sizeof(expr_var_t) * num_var);
        memcpy(expr->vars, vars, sizeof(expr_var_t) * (size_t)num_var);

        /* check for special variables 'alive' and 'muted' */
        for (i = 0; i < num_var; i++) {
            if (strcmp(expr->vars[i].name, "alive") == 0)
                expr->inst_ctl = i;
            else if (strcmp(expr->vars[i].name, "muted") == 0)
                expr->mute_ctl = i;
        }
#if TRACE_PARSE
        printf("Copied %d vars to expression\n", num_var);
        mpr_expr_print(expr);
#endif
    }
}

mpr_expr mpr_expr_new_from_str(const char *str, unsigned int num_src, const mpr_type *src_types,
                               const unsigned int *src_lens, unsigned int num_dst,
                               const mpr_type *dst_types, const unsigned int *dst_lens)
{
    mpr_expr expr;
    int i;

    RETURN_ARG_UNLESS(str && num_src && src_types && src_lens, 0);

    expr = mpr_expr_new(num_src, num_dst, NULL);

    if (expr_parser_build_stack(expr, str, num_src, src_types, src_lens,
                                num_dst, dst_types, dst_lens)) {
        free(expr->stack);
        mpr_expr_free(expr);
        return NULL;
    }

    /* Check for maximum vector length used in stack */
    for (i = 0; i < expr->stack->num_tokens; i++) {
        estack_update_vec_len(expr->stack, expr->stack->tokens[i].gen.vec_len);
    }

    if (estack_get_reduces_inst(expr->stack))
        expr->flags |= REDUCES_INST;

    if (expr->inst_ctl >= 0)
        expr->flags |= MANAGES_INST;

#if TRACE_PARSE
    printf("expression allocated and initialized with %d tokens\n", expr->stack->num_tokens);
#endif
    return expr;
}

void mpr_expr_free(mpr_expr expr)
{
    int i;
    FUNC_IF(free, expr->src_mlen);
    if (expr->flags & OWN_STACK)
        estack_free(expr->stack, 1);
    if (expr->num_vars && expr->vars) {
        for (i = 0; i < expr->num_vars; i++)
            free(expr->vars[i].name);
        free(expr->vars);
    }
    free(expr);
}

void mpr_expr_update_mlen(mpr_expr expr, int idx, unsigned int mlen)
{
    ++mlen;
    if (VAR_Y == idx) {
        if (mlen > expr->dst_mlen)
            expr->dst_mlen = mlen;
    }
    else if (idx >= VAR_X) {
        idx -= VAR_X;
        if (mlen > expr->src_mlen[idx])
            expr->src_mlen[idx] = mlen;
        if (mlen > expr->max_src_mlen)
            expr->max_src_mlen = mlen;
    }
    else if (VAR_X_NEWEST == idx) {
        /* could apply to any input */
        for (idx = 0; idx < expr->num_src; idx++) {
            if (mlen > expr->src_mlen[idx])
                expr->src_mlen[idx] = mlen;
        }
        if (mlen > expr->max_src_mlen)
            expr->max_src_mlen = mlen;
    }
}

#if DEBUG
void mpr_expr_print(mpr_expr expr)
{
#if TRACE_PARSE
    estack_print(NULL, expr->stack, expr->vars, 1);
#endif /* TRACE_PARSE */
}
#endif /* DEBUG */

int mpr_expr_get_src_mlen(mpr_expr expr, int idx)
{
    return expr->src_mlen[idx];
}

int mpr_expr_get_dst_mlen(mpr_expr expr, int idx)
{
    return expr->dst_mlen;
}

int mpr_expr_get_num_vars(mpr_expr expr)
{
    return expr->num_vars;
}

const char *mpr_expr_get_var_name(mpr_expr expr, int idx)
{
    return (idx >= 0 && idx < expr->num_vars) ? expr->vars[idx].name : NULL;
}

int mpr_expr_get_var_vlen(mpr_expr expr, int idx)
{
    return (idx >= 0 && idx < expr->num_vars) ? expr->vars[idx].vec_len : 0;
}

int mpr_expr_get_var_is_instanced(mpr_expr expr, int idx)
{
    return (idx >= 0 && idx < expr->num_vars) ? expr->vars[idx].flags & VAR_INSTANCED : 0;
}

int mpr_expr_get_var_type(mpr_expr expr, int idx)
{
    return (idx >= 0 && idx < expr->num_vars) ? expr->vars[idx].datatype : 0;
}

int mpr_expr_get_src_is_muted(mpr_expr expr, int idx)
{
    int i, found = 0, muted = VAR_MUTED;
    etoken_t *tok = expr->stack->tokens;
    for (i = 0; i < expr->stack->num_tokens; i++) {
        if (tok[i].toktype == TOK_VAR && tok[i].var.idx == idx + VAR_X) {
            found = 1;
            muted &= tok[i].gen.flags;
        }
    }
    return found && muted;
}

int mpr_expr_get_num_src(mpr_expr expr)
{
    return expr ? expr->num_src : 0;
}

int mpr_expr_get_manages_inst(mpr_expr expr)
{
    return (expr->flags & (MANAGES_INST | REDUCES_INST)) != 0;
}

void mpr_expr_set_var_updated(mpr_expr expr, int var_idx)
{
    RETURN_UNLESS(expr && var_idx >= 0 && var_idx < expr->num_vars);
    RETURN_UNLESS(var_idx != expr->inst_ctl && var_idx != expr->mute_ctl);
    expr->vars[var_idx].flags |= VAR_SET_EXTERN;
    /* Reset expression offset to 0 in case other variables are initialised from this one. */
    expr->stack->offset = 0;
    return;
}
