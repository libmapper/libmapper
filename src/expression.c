#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <float.h>

#include "map.h"
#include "expression.h"
#include "expression_function.h"
#include "expression_operator.h"
#include "expression_parser.h"
#include "expression_token.h"
#include "expression_trace.h"
#include "expression_value.h"
#include "expression_variable.h"
#include <mapper/mapper.h>

#ifdef _MSC_VER
#include <malloc.h>
#endif

static int expr_get_eval_buffer_size(mpr_expr expr);

struct _mpr_expr
{
    expr_tok_t *tokens;
    expr_var_t *vars;
    uint8_t offset;
    uint8_t num_tokens;
    uint8_t stack_size;
    uint8_t vec_len;
    uint16_t *src_mlen;
    uint16_t max_src_mlen;
    uint16_t dst_mlen;
    uint8_t num_vars;
    int8_t inst_ctl;
    int8_t mute_ctl;
    int8_t num_src;
    int8_t own_tokens;
};

/* could we use mpr_value here instead, with stack idx instead of history idx?
 * pro: vectors, commonality with I/O
 * con: timetags wasted
 * option: create version with unallocated timetags */
struct _mpr_expr_eval_buffer
{
    expr_value vals;
    mpr_type *types;
    uint8_t *lens;
    unsigned int size;
    unsigned int len;
};

mpr_expr_eval_buffer mpr_expr_new_eval_buffer(void)
{
    return (mpr_expr_eval_buffer) calloc(1, sizeof(struct _mpr_expr_eval_buffer));
}

void mpr_expr_realloc_eval_buffer(mpr_expr expr, mpr_expr_eval_buffer buff)
{
    /* Reallocate evaluation stack if necessary. */
    int size = expr->num_tokens * expr->vec_len;
    if (buff->size < size) {
        buff->size = size;
        if (buff->vals)
            buff->vals = realloc(buff->vals, size * sizeof(expr_value_t));
        else
            buff->vals = malloc(size * sizeof(expr_value_t));
    }
    if (buff->len < expr->num_tokens) {
        buff->len = expr->num_tokens;
        if (buff->types)
            buff->types = realloc(buff->types, buff->len * sizeof(mpr_type));
        else
            buff->types = malloc(buff->len * sizeof(mpr_type));
        if (buff->lens)
            buff->lens = realloc(buff->lens, buff->len * sizeof(uint8_t));
        else
            buff->lens = malloc(buff->len * sizeof(uint8_t));
    }
}

void mpr_expr_free_eval_buffer(mpr_expr_eval_buffer buff)
{
    FUNC_IF(free, buff->vals);
    FUNC_IF(free, buff->types);
    FUNC_IF(free, buff->lens);
    free(buff);
}

mpr_expr mpr_expr_new(unsigned int num_src, unsigned int num_dst,
                      void *tokens, unsigned int num_tok, unsigned int vec_len)
{
    int i;
    mpr_expr expr = calloc(1, sizeof(struct _mpr_expr));

    expr->num_src = num_src;
    expr->src_mlen = calloc(1, sizeof(uint16_t) * num_src);
    /* default to mlen = 1 */
    for (i = 0; i < num_src; i++)
        expr->src_mlen[i] = 1;
    expr->max_src_mlen = 1;

//    expr->num_dst = num_dst;
    expr->dst_mlen = 1;

    expr->vec_len = vec_len;
    expr->inst_ctl = -1;
    expr->mute_ctl = -1;

    if (tokens && num_tok) {
        expr->num_tokens = num_tok;
        expr->tokens = tokens;

#if TRACE_PARSE
        printf("Created new expression with %d tokens\n", num_tok);
        mpr_expr_print(expr);
#endif
    }

    return expr;
}

void mpr_expr_cpy_tokens(mpr_expr expr, void *tokens, unsigned int num_tok)
{
    expr->num_tokens = num_tok;
    expr->tokens = malloc(sizeof(expr_tok_t) * (size_t)num_tok);
    memcpy(expr->tokens, tokens, sizeof(expr_tok_t) * (size_t)num_tok);
    expr->own_tokens = 1;

#if TRACE_PARSE
    printf("Copied %d tokens to expression\n", num_tok);
    mpr_expr_print(expr);
#endif
}

void mpr_expr_cpy_vars(mpr_expr expr, void *vars, int num_var)
{
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
    }

#if TRACE_PARSE
    printf("Copied %d vars to expression\n", num_var);
    mpr_expr_print(expr);
#endif
}

mpr_expr mpr_expr_new_from_str(const char *str, unsigned int num_src, const mpr_type *src_types,
                               const unsigned int *src_lens, unsigned int num_dst,
                               const mpr_type *dst_types, const unsigned int *dst_lens)
{
    mpr_expr expr;
    int i;

    RETURN_ARG_UNLESS(str && num_src && src_types && src_lens, 0);

    expr = mpr_expr_new(num_src, num_dst, NULL, 0, 0);

    if (expr_parser_build_stack(expr, str, num_src, src_types, src_lens,
                                num_dst, dst_types, dst_lens)) {
        mpr_expr_free(expr);
        return NULL;
    }

    /* Check for maximum vector length used in stack */
    for (i = 0; i < expr->num_tokens; i++) {
        mpr_expr_update_vlen(expr, expr->tokens[i].gen.vec_len);
    }

    expr->stack_size = expr_get_eval_buffer_size(expr);

#if TRACE_PARSE
    printf("expression allocated and initialized with %d tokens\n", expr->num_tokens);
#endif
    return expr;
}

void mpr_expr_free(mpr_expr expr)
{
    int i;
    FUNC_IF(free, expr->src_mlen);
    if (expr->own_tokens) {
        for (i = 0; i < expr->num_tokens; i++)
            expr_tok_free(&expr->tokens[i]);
        FUNC_IF(free, expr->tokens);
    }
    if (expr->num_vars && expr->vars) {
        for (i = 0; i < expr->num_vars; i++)
            free(expr->vars[i].name);
        free(expr->vars);
    }
    free(expr);
}

void mpr_expr_update_vlen(mpr_expr expr, unsigned int vlen)
{
    if (vlen > expr->vec_len)
        expr->vec_len = vlen;
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
    print_stack(NULL, expr->tokens, expr->num_tokens - 1, expr->vars, 1);
#endif /* TRACE_PARSE */
}
#endif /* DEBUG */

static int expr_get_eval_buffer_size(mpr_expr expr)
{
    int i = 0, sp = 0, eval_buffer_len = 0;
    expr_tok_t *tok = expr->tokens;
    while (i < expr->num_tokens && tok->toktype != TOK_END) {
        switch (tok->toktype) {
            case TOK_LITERAL:
            case TOK_VAR:
            case TOK_TT:
            case TOK_COPY_FROM:
            case TOK_OP:
            case TOK_FN:
            case TOK_VFN:
            case TOK_VECTORIZE:
            case TOK_MOVE:
                /* keep one stack element for the result */
                ++sp;
                /* continue... */
                /* consume operands/arguments but keep one stack element for the result */
                sp -= expr_tok_get_arity(tok);
                break;

            case TOK_ASSIGN:
            case TOK_ASSIGN_CONST:
            case TOK_ASSIGN_TT:
                --sp;
                /* continue... */
            case TOK_ASSIGN_USE:
                sp -= expr_tok_get_arity(tok);
                break;

            case TOK_LOOP_START:
                /* may need to cache instance */
            case TOK_SP_ADD:
                sp += expr_tok_get_arity(tok);
                break;
            case TOK_LOOP_END:
                /* may need to uncache instance */
                sp -= expr_tok_get_arity(tok);
                break;
            default:
                return -1;
        }
        if (sp > eval_buffer_len)
            eval_buffer_len = sp;
        ++tok;
        ++i;
    }
    return eval_buffer_len;
}

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
    expr_tok_t *tok = expr->tokens;
    for (i = 0; i < expr->num_tokens; i++) {
        if (tok[i].toktype == TOK_VAR && tok[i].var.idx == idx + VAR_X) {
            found = 1;
            muted &= tok[i].gen.flags;
        }
    }
    return found && muted;
}

int mpr_expr_get_num_input_slots(mpr_expr expr)
{
    return expr ? expr->num_src : 0;
}

int mpr_expr_get_manages_inst(mpr_expr expr)
{
    return expr ? expr->inst_ctl >= 0 : 0;
}

void mpr_expr_set_var_updated(mpr_expr expr, int var_idx)
{
    RETURN_UNLESS(expr && var_idx >= 0 && var_idx < expr->num_vars);
    RETURN_UNLESS(var_idx != expr->inst_ctl && var_idx != expr->mute_ctl);
    expr->vars[var_idx].flags |= VAR_SET_EXTERN;
    /* Reset expression offset to 0 in case other variables are initialised from this one. */
    expr->offset = 0;
    return;
}

#define UNARY_OP_CASE(OP, SYM, T)               \
    case OP:                                    \
        for (i = sp; i < sp + lens[dp]; i++)    \
            vals[i].T SYM vals[i].T;            \
        break;

#define BINARY_OP_CASE(OP, SYM, T)                                  \
    case OP:                                                        \
        for (i = 0, j = sp; i < lens[dp]; i++, j++)                 \
            vals[j].T = vals[j].T SYM vals[sp + vlen + i % rlen].T; \
        break;

#define CONDITIONAL_CASES(T)                                        \
    case OP_IF_ELSE:                                                \
        for (i = 0, j = sp; i < lens[dp]; i++, j++) {               \
            if (!vals[j].T)                                         \
                vals[j].T = vals[sp + vlen + i % rlen].T;           \
        }                                                           \
        break;                                                      \
    case OP_IF_THEN_ELSE:                                           \
        for (i = 0, j = sp; i < lens[dp]; i++, j++) {               \
            if (vals[j].T)                                          \
                vals[j].T = vals[sp + vlen + i % rlen].T;           \
            else                                                    \
                vals[j].T = vals[sp + 2 * vlen + i % lens[dp + 2]].T;\
        }                                                           \
        break;

#define OP_CASES_META(EL)                                   \
    BINARY_OP_CASE(OP_ADD, +, EL);                          \
    BINARY_OP_CASE(OP_SUBTRACT, -, EL);                     \
    BINARY_OP_CASE(OP_MULTIPLY, *, EL);                     \
    BINARY_OP_CASE(OP_IS_EQUAL, ==, EL);                    \
    BINARY_OP_CASE(OP_IS_NOT_EQUAL, !=, EL);                \
    BINARY_OP_CASE(OP_IS_LESS_THAN, <, EL);                 \
    BINARY_OP_CASE(OP_IS_LESS_THAN_OR_EQUAL, <=, EL);       \
    BINARY_OP_CASE(OP_IS_GREATER_THAN, >, EL);              \
    BINARY_OP_CASE(OP_IS_GREATER_THAN_OR_EQUAL, >=, EL);    \
    BINARY_OP_CASE(OP_LOGICAL_AND, &&, EL);                 \
    BINARY_OP_CASE(OP_LOGICAL_OR, ||, EL);                  \
    UNARY_OP_CASE(OP_LOGICAL_NOT, =!, EL);                  \
    CONDITIONAL_CASES(EL);

MPR_INLINE static int _max(int a, int b)
{
    return a > b ? a : b;
}

#define SET_STACK_PTR(ADDEND)           \
    dp = ADDEND;                        \
    assert(dp >= 0 || dp < buff_size);  \
    sp = dp * vlen;

#define INCR_STACK_PTR(ADDEND)          \
    dp += ADDEND;                       \
    assert(dp >= 0 || dp < buff_size);  \
    sp = dp * vlen;

#define SET_TYPE(TYPE)  \
    types[dp] = TYPE;

#define SET_LEN(LEN)    \
    lens[dp] = LEN;

int mpr_expr_eval(mpr_expr expr, mpr_expr_eval_buffer buff, mpr_value *v_in, mpr_value *v_vars,
                  mpr_value v_out, mpr_time *time, mpr_bitflags has_value, int inst_idx)
{
#if TRACE_EVAL
    printf("evaluating expression...\n");
#endif
    expr_tok_t *tok, *end;
    int status = 1 | EXPR_EVAL_DONE, cache = 0, vlen;
    int i, j, sp, dp = -1;
    /* Note: signal, history, and vector reduce are currently limited to 255 items here */
    uint8_t alive = 1, muted = 0, can_advance = 1, hist_offset = 0, sig_offset = 0, vec_offset = 0;
    mpr_value x = NULL;

    expr_value vals = buff->vals;
    uint8_t *lens = buff->lens;
    mpr_type *types = buff->types;
    unsigned int buff_size = buff->size;

    if (!expr) {
#if TRACE_EVAL
        printf(" no expression to evaluate!\n");
#endif
        return 0;
    }

    sp = -expr->vec_len;
    vlen = expr->vec_len;
    tok = expr->tokens;
    end = expr->tokens + expr->num_tokens;
    if (v_out && mpr_value_get_num_samps(v_out, inst_idx) > 0) {
        tok += expr->offset;
    }

    if (v_vars) {
        if (expr->inst_ctl >= 0) {
            /* recover instance state */
            mpr_value v = v_vars[expr->inst_ctl];
            int *vi = mpr_value_get_value(v, inst_idx, 0);
            alive = (0 != vi[0]);
        }
        if (expr->mute_ctl >= 0) {
            /* recover mute state */
            mpr_value v = v_vars[expr->mute_ctl];
            int *vi = mpr_value_get_value(v, inst_idx, 0);
            muted = (0 != vi[0]);
        }
    }

    if (v_out) {
        /* init has_value */
        if (has_value)
            mpr_bitflags_clear(has_value, mpr_value_get_vlen(v_out));
        /* Increment index position of output data structure. */
        mpr_value_incr_idx(v_out, inst_idx);
    }

    /* choose one input to represent active instances
     * for now we will choose the input with the highest instance count
     * TODO: consider alternatives */
    if (v_in) {
        x = v_in[0];
        for (i = 1; i < expr->num_src; i++) {
            if (mpr_value_get_num_inst(v_in[i]) > mpr_value_get_num_inst(x))
                x = v_in[i];
        }
    }

#if TRACE_EVAL
    printf("instruction\targuments\t\tresult\n");

#endif

    while (tok < end) {
  repeat:
#if TRACE_EVAL
        printf(" %2ld: ", (tok - expr->tokens));
        print_token(tok, expr->vars, 0);
        printf("\r\t\t\t\t\t");
#endif
        switch (tok->toktype) {
        case TOK_LITERAL:
        case TOK_VLITERAL:
            INCR_STACK_PTR(1);
            SET_TYPE(tok->gen.datatype);
            SET_LEN(tok->gen.vec_len);
            /* TODO: remove vector building? */
            switch (types[dp]) {
#define TYPED_CASE(MTYPE, T)                                            \
                case MTYPE:                                             \
                    if (TOK_LITERAL == tok->toktype) {                  \
                        for (i = sp; i < sp + lens[dp]; i++)            \
                            vals[i].T = tok->lit.val.T;                 \
                    }                                                   \
                    else {                                              \
                        for (i = sp, j = 0; i < sp + lens[dp]; i++, j++)\
                            vals[i].T = tok->lit.val.T##p[j];           \
                    }                                                   \
                    break;
                TYPED_CASE(MPR_INT32, i)
                TYPED_CASE(MPR_FLT, f)
                TYPED_CASE(MPR_DBL, d)
#undef TYPED_CASE
                default:
                    goto error;
            }
#if TRACE_EVAL
            expr_value_print(vals + sp, types[dp], lens[dp], dp);
#endif
            break;
        case TOK_VAR: {
            mpr_value v;
            int hidx = -hist_offset, vidx, idxp = dp;
            float hwt = 0.f, vwt = 0.f;

            if (tok->var.idx == VAR_Y) {
                RETURN_ARG_UNLESS(v_out, status);
#if TRACE_EVAL
                printf("\n\t\tvar.y");
#endif
                v = v_out;
                can_advance = 0;
            }
            else if (tok->var.idx >= VAR_X_NEWEST) {
                RETURN_ARG_UNLESS(v_in, status);
                if (tok->var.idx == VAR_X_NEWEST) {
                    /* Find most recently-updated source signal */
                    int newest_idx = 0;
                    for (i = 1; i < expr->num_src; i++) {
#if TRACE_EVAL
                        mpr_time_print(*mpr_value_get_time(v_in[newest_idx], inst_idx, 0));
                        printf(" : ");
                        mpr_time_print(*mpr_value_get_time(v_in[i], inst_idx, 0));
                        printf("\n");
#endif
                        if (mpr_time_cmp(*mpr_value_get_time(v_in[newest_idx], inst_idx, 0),
                                         *mpr_value_get_time(v_in[i], inst_idx, 0)) < 0) {
                            newest_idx = i;
                        }
                    }
                    v = v_in[newest_idx];
#if TRACE_EVAL
                    printf("\n\t\tvar.x$%d", newest_idx);
#endif
                }
                else if (!(tok->gen.flags & VAR_SIG_IDX)) {
                    v = v_in[tok->var.idx - VAR_X + sig_offset];
#if TRACE_EVAL
                    printf("\n\t\tvar.x$%d", tok->var.idx - VAR_X + sig_offset);
#endif
                }
                else {
                    assert(idxp >= 0);
                    if (MPR_INT32 == types[idxp]) {
                        int sidx = vals[sp].i % expr->num_src;
                        if (sidx < 0)
                            sidx += expr->num_src;
#if TRACE_EVAL
                        printf("\n\t\tvar.x$(%d)", sidx);
#endif
                        v = v_in[sidx];
                        --idxp;
                    }
                    else
                        goto error;
                }
                can_advance = 0;

                /* If no instance idx is cached this means that this expression contains a
                 * non-reducing reference to the variable x, and that mpr_expr_eval() should be
                 * called again for each instance. Thus we remove the EVAL_DONE flag from status. */
                if (!cache)
                    status &= ~EXPR_EVAL_DONE;
            }
            else if (v_vars) {
#if TRACE_EVAL
                if (expr->vars)
                    printf("\n\t\tvar.%s", expr->vars[tok->var.idx].name);
                else
                    printf("\n\t\tvars.%d", tok->var.idx);
#endif
                v = v_vars[tok->var.idx];
                can_advance = 0;
            }
            else
                goto error;

            if (mpr_value_get_num_samps(v, inst_idx) <= 0) {
#if TRACE_EVAL
                printf("\r\t\t\t\t\tno values! exiting...\n");
#endif
                return 0;
            }

            if (tok->gen.flags & VAR_HIST_IDX) {
                double intpart;
                assert(idxp >= 0);
                i = idxp * vlen;
                switch (types[idxp]) {
                    case MPR_INT32: hidx = vals[i].i;                                       break;
                    case MPR_FLT:   hwt = -modf(vals[i].f, &intpart); hidx = (int)intpart;  break;
                    case MPR_DBL:   hwt = -modf(vals[i].d, &intpart); hidx = (int)intpart;  break;
                    default:        goto error;
                }
                --idxp;
            }
#if TRACE_EVAL
            if (hwt)
                printf("{%g}", hidx + -hwt);
            else
                printf("{%d}", hidx);
#endif

            if (tok->gen.flags & VAR_VEC_IDX) {
                double intpart;
                assert(idxp >= 0);
                i = idxp * vlen;
                switch (types[idxp]) {
                    case MPR_INT32: vidx = vals[i].i;                                       break;
                    case MPR_FLT:   vwt = modf(vals[i].f, &intpart); vidx = (int)intpart;   break;
                    case MPR_DBL:   vwt = modf(vals[i].d, &intpart); vidx = (int)intpart;   break;
                    default:        goto error;
                }
                if (vwt < 0) {
                    --vidx;
                    vwt *= -1;
                }
                else if (vwt)
                    vwt = 1 - vwt;
                --idxp;
            }
            else
                vidx = tok->var.vec_idx + vec_offset;
#if TRACE_EVAL
            if (vwt)
                printf("[%g]\r\t\t\t\t\t", vidx + 1 - vwt);
            else
                printf("[%d]\r\t\t\t\t\t", vidx);
#endif

            /* STUB: instance indexing will go here */
            /* if (tok->gen.flags & VAR_INST_IDX) {
                ...
            } */

            SET_STACK_PTR(idxp + 1);
            SET_TYPE(mpr_value_get_type(v));
            SET_LEN(tok->gen.vec_len ? tok->gen.vec_len : mpr_value_get_vlen(v));

            switch (mpr_value_get_type(v)) {
#define COPY_TYPED(MTYPE, TYPE, T)                                                  \
                case MTYPE: {                                                       \
                    int j, k, vlen = mpr_value_get_vlen(v);                         \
                    TYPE *a = (TYPE*)mpr_value_get_value(v, inst_idx, hidx);        \
                    if (vwt) {                                                      \
                        register TYPE temp;                                         \
                        register float ivwt = 1 - vwt;                              \
                        for (j = 0, k = sp; j < lens[dp]; j++, k++) {               \
                            int vec_idx = (j + vidx) % vlen;                        \
                            if (vec_idx < 0) vec_idx += vlen;                       \
                            temp = a[vec_idx] * vwt;                                \
                            vec_idx = (vec_idx + 1) % vlen;                         \
                            temp += a[vec_idx] * ivwt;                              \
                            vals[k].T = temp;                                       \
                        }                                                           \
                    }                                                               \
                    else {                                                          \
                        for (j = 0, k = sp; j < lens[dp]; j++, k++) {               \
                            int vec_idx = (j + vidx) % vlen;                        \
                            if (vec_idx < 0) vec_idx += vlen;                       \
                            vals[k].T = a[vec_idx];                                 \
                        }                                                           \
                    }                                                               \
                    if (hwt) {                                                      \
                        register float ihwt = 1 - hwt;                              \
                        a = (TYPE*)mpr_value_get_value(v, inst_idx, hidx - 1);      \
                        if (vwt) {                                                  \
                            register TYPE temp;                                     \
                            register float ivwt = 1 - vwt;                          \
                            for (j = 0, k = sp; j < lens[dp]; j++, k++) {           \
                                int vec_idx = (j + vidx) % vlen;                    \
                                if (vec_idx < 0) vec_idx += vlen;                   \
                                temp = a[vec_idx] * vwt;                            \
                                vec_idx = (vec_idx + 1) % vlen;                     \
                                temp += a[vec_idx] * ivwt;                          \
                                vals[k].T = vals[k].T * hwt + temp * ihwt;          \
                            }                                                       \
                        }                                                           \
                        else {                                                      \
                            for (j = 0, k = sp; j < lens[dp]; j++, k++) {           \
                                int vec_idx = (j + vidx) % vlen;                    \
                                if (vec_idx < 0) vec_idx += vlen;                   \
                                vals[j].T = vals[j].T * hwt + a[vec_idx] * (1 - hwt);\
                            }                                                       \
                        }                                                           \
                    }                                                               \
                    break;                                                          \
                }
                COPY_TYPED(MPR_INT32, int, i)
                COPY_TYPED(MPR_FLT, float, f)
                COPY_TYPED(MPR_DBL, double, d)
#undef COPY_TYPED
                default:
                    goto error;
            }
#if TRACE_EVAL
            expr_value_print(vals + sp, types[dp], lens[dp], dp);
#endif
            break;
        }
        case TOK_VAR_NUM_INST: {
            INCR_STACK_PTR(1);
            SET_TYPE(MPR_INT32);
            SET_LEN(tok->gen.vec_len);
            if (tok->var.idx == VAR_Y) {
                RETURN_ARG_UNLESS(v_out, status);
                vals[sp].i = mpr_value_get_num_active_inst(v_out);
            }
            else if (tok->var.idx >= VAR_X) {
                RETURN_ARG_UNLESS(v_in, status);
                vals[sp].i = mpr_value_get_num_active_inst(v_in[tok->var.idx - VAR_X]);
            }
            else if (v_vars)
                vals[sp].i = mpr_value_get_num_active_inst(v_vars[tok->var.idx]);
            else
                goto error;
            for (i = 1; i < tok->gen.vec_len; i++)
                vals[sp + i].i = vals[sp].i;
            can_advance = 0;
#if TRACE_EVAL
            expr_value_print(vals + sp, types[dp], lens[dp], dp);
#endif
            break;
        }
        case TOK_TT: {
            int hidx = 0;
            double hwt = 0.0;
            double t_d;
            if (!(tok->gen.flags & VAR_HIST_IDX)) {
                INCR_STACK_PTR(1);
            }
            SET_LEN(tok->gen.vec_len);
#if TRACE_EVAL
            if (tok->var.idx == VAR_Y)
                printf("\n\t\ttt.y");
            else if (tok->var.idx >= VAR_X)
                printf("\n\t\ttt.x$%d", tok->var.idx - VAR_X);
            else if (v_vars)
                printf("\n\t\ttt.%s", expr->vars[tok->var.idx].name);

            if (tok->gen.flags & VAR_HIST_IDX) {
                switch (types[dp]) {
                    case MPR_INT32: printf("{N=%d}", vals[sp].i);   break;
                    case MPR_FLT:   printf("{N=%g}", vals[sp].f);   break;
                    case MPR_DBL:   printf("{N=%g}", vals[sp].d);   break;
                    default:                                        goto error;
                }
            }
            printf("\r\t\t\t\t\t");
#endif
            if (tok->gen.flags & VAR_HIST_IDX) {
                switch (types[dp]) {
                    case MPR_INT32: hidx = vals[sp].i;                                      break;
                    case MPR_FLT:   hidx = (int)vals[sp].f; hwt = fabsf(vals[sp].f - hidx); break;
                    case MPR_DBL:   hidx = (int)vals[sp].d; hwt = fabs(vals[sp].d - hidx);  break;
                    default:        goto error;
                }
            }
            if (tok->var.idx == VAR_Y) {
                mpr_time *t;
                RETURN_ARG_UNLESS(v_out, status);
                t = mpr_value_get_time(v_out, inst_idx, hidx);
                t_d = mpr_time_as_dbl(*t);
                if (hwt) {
                    t = mpr_value_get_time(v_out, inst_idx, hidx - 1);
                    t_d = t_d * hwt + mpr_time_as_dbl(*t) * (1 - hwt);
                }
            }
            else if (tok->var.idx >= VAR_X) {
                mpr_value v;
                mpr_time *t;
                RETURN_ARG_UNLESS(v_in, status);
                v = v_in[tok->var.idx - VAR_X];
                t = mpr_value_get_time(v, inst_idx, hidx);
                t_d = mpr_time_as_dbl(*t);
                if (hwt) {
                    t = mpr_value_get_time(v, inst_idx, hidx);
                    t_d = t_d * hwt + mpr_time_as_dbl(*t) * (1 - hwt);
                }
            }
            else if (v_vars) {
                mpr_value v = v_vars[tok->var.idx];
                mpr_time *t = mpr_value_get_time(v, inst_idx, 0);
                t_d = mpr_time_as_dbl(*t);
            }
            else
                goto error;
            for (i = sp; i < sp + tok->gen.vec_len; i++)
                vals[i].d = t_d;
            SET_TYPE(tok->gen.datatype);
            can_advance = 0;
#if TRACE_EVAL
            expr_value_print(vals + sp, types[dp], lens[dp], dp);
#endif
            break;
        }
        case TOK_OP: {
            uint8_t max_len, rlen, arity = op_tbl[tok->op.idx].arity;
            INCR_STACK_PTR(1 - arity);
            /* first copy vals[sp] elements if necessary */
            max_len = lens[dp];
            for (i = 1; i < arity; i++)
                max_len = _max(max_len, lens[dp + i]);
            for (i = 0; i < arity; i++) {
                int diff = max_len - lens[dp];
                while (diff > 0) {
                    int min_diff = lens[dp] > diff ? diff : lens[dp];
                    expr_value_cpy(&vals[sp + lens[dp]], &vals[sp], min_diff);
                    lens[dp] += min_diff;
                    diff -= min_diff;
                }
            }
            rlen = lens[dp + 1];
            switch (types[dp]) {
                case MPR_INT32: {
                    switch (tok->op.idx) {
                        OP_CASES_META(i);
                        case OP_DIVIDE:
                            /* Check for divide-by-zero */
                            for (i = 0, j = 0; i < max_len; i++, j = (j + 1) % rlen) {
                                if (vals[sp + vlen + j].i)
                                    vals[sp + i].i /= vals[sp + vlen + j].i;
                                else {
#if TRACE_EVAL
                                    printf("... integer divide-by-zero detected, skipping assignment.\n");
#endif
                                    /* skip to after this assignment */
                                    while (tok < end && !((++tok)->toktype & TOK_ASSIGN)) {}
                                    while (tok < end && (tok)->toktype & TOK_ASSIGN) {
                                        if (tok->gen.flags & CLEAR_STACK) {
                                            dp = -1;
                                            sp = dp * vlen;
                                        }
                                        ++tok;
                                    }
                                    if (tok >= end)
                                        return 0;
                                    else
                                        goto repeat;
                                }
                            }
                            break;
                        BINARY_OP_CASE(OP_MODULO, %, i);
                        BINARY_OP_CASE(OP_LEFT_BIT_SHIFT, <<, i);
                        BINARY_OP_CASE(OP_RIGHT_BIT_SHIFT, >>, i);
                        BINARY_OP_CASE(OP_BITWISE_AND, &, i);
                        BINARY_OP_CASE(OP_BITWISE_OR, |, i);
                        BINARY_OP_CASE(OP_BITWISE_XOR, ^, i);
                        default: goto error;
                    }
                    break;
                }
                case MPR_FLT: {
                    switch (tok->op.idx) {
                        OP_CASES_META(f);
                        BINARY_OP_CASE(OP_DIVIDE, /, f);
                        case OP_MODULO:
                            for (i = 0; i < max_len; i++)
                                vals[sp + i].f = fmodf(vals[sp + i].f, vals[sp + vlen + i % rlen].f);
                            break;
                        default: goto error;
                    }
                    break;
                }
                case MPR_DBL: {
                    switch (tok->op.idx) {
                        OP_CASES_META(d);
                        BINARY_OP_CASE(OP_DIVIDE, /, d);
                        case OP_MODULO:
                            for (i = 0; i < max_len; i++)
                                vals[sp + i].d = fmod(vals[sp + i].d, vals[sp + vlen + i % rlen].d);
                            break;
                        default: goto error;
                    }
                    break;
                }
                default:
                    goto error;
            }
            SET_TYPE(tok->gen.datatype);
#if TRACE_EVAL
            expr_value_print(vals + sp, types[dp], lens[dp], dp);
#endif
            break;
        }
        case TOK_FN: {
            int diff;
            uint8_t max_len, llen, rlen, arity = fn_tbl[tok->fn.idx].arity;
            INCR_STACK_PTR(1 - arity);
            /* TODO: use preprocessor macro or inline func here */
            /* first copy vals[sp] elements if necessary */
            max_len = lens[dp];
            for (i = 1; i < arity; i++)
                max_len = _max(max_len, lens[dp + i]);
            diff = max_len - lens[dp];
            while (diff > 0) {
                int min_diff = lens[dp] > diff ? diff : lens[dp];
                expr_value_cpy(&vals[sp + lens[dp]], &vals[sp], min_diff);
                lens[dp] += min_diff;
                diff -= min_diff;
            }
            llen = lens[dp];
            rlen = lens[dp + 1];
            SET_TYPE(tok->gen.datatype);
            switch (types[dp]) {
#define TYPED_CASE(MTYPE, FN, T)                                                        \
            case MTYPE:                                                                 \
                switch (arity) {                                                        \
                case 0:                                                                 \
                    for (i = 0; i < llen; i++)                                          \
                        vals[sp + i].T = ((FN##_arity0*)fn_tbl[tok->fn.idx].FN)();      \
                    break;                                                              \
                case 1:                                                                 \
                    for (i = 0; i < llen; i++)                                          \
                        vals[sp + i].T = (((FN##_arity1*)fn_tbl[tok->fn.idx].FN)        \
                                          (vals[sp + i].T));                            \
                    break;                                                              \
                case 2:                                                                 \
                    for (i = 0; i < llen; i++)                                          \
                        vals[sp + i].T = (((FN##_arity2*)fn_tbl[tok->fn.idx].FN)        \
                                          (vals[sp + i].T, vals[sp + vlen + i % rlen].T));\
                    break;                                                              \
                case 3:                                                                 \
                    for (i = 0; i < llen; i++)                                          \
                        vals[sp + i].T = (((FN##_arity3*)fn_tbl[tok->fn.idx].FN)        \
                                          (vals[sp + i].T, vals[sp + vlen + i % rlen].T,\
                                           vals[sp + 2 * vlen + i % lens[dp + 2]].T));  \
                    break;                                                              \
                case 4:                                                                 \
                    for (i = 0; i < llen; i++)                                          \
                        vals[sp + i].T = (((FN##_arity4*)fn_tbl[tok->fn.idx].FN)        \
                                          (vals[sp + i].T, vals[sp + vlen + i % rlen].T,\
                                           vals[sp + 2 * vlen + i % lens[dp + 2]].T,    \
                                           vals[sp + 3 * vlen + i % lens[dp + 3]].T));  \
                    break;                                                              \
                default: goto error;                                                    \
                }                                                                       \
                break;
            TYPED_CASE(MPR_INT32, fn_int, i)
            TYPED_CASE(MPR_FLT, fn_flt, f)
            TYPED_CASE(MPR_DBL, fn_dbl, d)
#undef TYPED_CASE
            default:
                goto error;
            }
            if (tok->fn.idx > FN_DEL_IDX)
                can_advance = 0;
#if TRACE_EVAL
            expr_value_print(vals + sp, types[dp], lens[dp], dp);
#endif
            break;
        }
        case TOK_VFN: {
            uint8_t arity = vfn_tbl[tok->fn.idx].arity;
            INCR_STACK_PTR(1 - arity);
            if (VFN_CONCAT != tok->fn.idx
                && (arity > 1 || VFN_DOT == tok->fn.idx)) {
                int max_len = tok->gen.vec_len;
                for (i = 0; i < arity; i++)
                    max_len = max_len > lens[dp + i] ? max_len : lens[dp + i];
                for (i = 0; i < arity; i++) {
                    /* we need to ensure the vector lengths are equal */
                    while (lens[dp + i] < max_len) {
                        int diff = max_len - lens[dp + i];
                        diff = diff < lens[dp + i] ? diff : lens[dp + i];
                        expr_value_cpy(&vals[sp + lens[dp + i]], &vals[sp], diff);
                        lens[dp + i] += diff;
                    }
                    sp += vlen;
                }
                sp = dp * vlen;
            }
            SET_TYPE(tok->gen.datatype);
            switch (types[dp]) {
#define TYPED_CASE(MTYPE, FN)                                                               \
                case MTYPE:                                                                 \
                    (((vfn_template*)vfn_tbl[tok->fn.idx].FN)(vals + sp, lens + dp, vlen)); \
                    break;
                TYPED_CASE(MPR_INT32, fn_int)
                TYPED_CASE(MPR_FLT, fn_flt)
                TYPED_CASE(MPR_DBL, fn_dbl)
#undef TYPED_CASE
                default:
                    break;
            }

            if (vfn_tbl[tok->fn.idx].reduce) {
                for (i = 1; i < tok->gen.vec_len; i++)
                    vals[sp + i].d = vals[sp].d;
            }
            if (vfn_tbl[tok->fn.idx].reduce)
                SET_LEN(tok->gen.vec_len);
#if TRACE_EVAL
            expr_value_print(vals + sp, types[dp], lens[dp], dp);
            if (   VFN_MAXMIN == tok->fn.idx
                || VFN_SUMNUM == tok->fn.idx
                || VFN_CONCAT == tok->fn.idx) {
                printf("\t\t\t\t\t");
                expr_value_print(vals + sp + vlen, types[dp + 1], lens[dp + 1], dp + 1);
            }
#endif
            }
            break;
        case TOK_LOOP_START:
#if TRACE_EVAL
            switch (tok->con.flags & REDUCE_TYPE_MASK) {
                case RT_HISTORY:
                    printf("History idx = -%d.\n", tok->con.reduce_start);
                    break;
                case RT_INSTANCE:
                    printf("Instance idx = %d.\n", inst_idx);
                    break;
                case RT_SIGNAL:
                    printf("Signal idx = 0.\n");
                    break;
                case RT_VECTOR:
                    printf("Vector idx = 0.\n");
                    break;
                default:
                    goto error;
            }
#endif
            switch (tok->con.flags & REDUCE_TYPE_MASK) {
                case RT_HISTORY:
                    /* Set history start sample */
                    hist_offset = tok->con.reduce_start;
                    break;
                case RT_INSTANCE:
                    /* cache previous instance idx */
                    INCR_STACK_PTR(1);
                    vals[sp].i = inst_idx;
                    ++cache;
#if TRACE_EVAL
                    printf("Caching instance idx %d on the eval stack: ", inst_idx);
                    expr_value_print(vals + sp, MPR_INT32, 1, dp);
#endif
                    if (x) {
                        /* find first active instance idx */
                        for (i = 0; i < mpr_value_get_num_inst(x); i++) {
                            if (mpr_value_get_num_samps(x, i) >= expr->max_src_mlen)
                                break;
                        }
                        if (i >= mpr_value_get_num_inst(x))
                            return status;
                        inst_idx = i;
                    }
                    break;
                case RT_VECTOR:
                    /* Set vector start index */
                    vec_offset = tok->con.reduce_start;
                    break;
                default:
                    break;
            }
            break;
        case TOK_SP_ADD:
            INCR_STACK_PTR(tok->lit.val.i);
#if TRACE_EVAL
            printf("\n");
#endif
            break;
        case TOK_LOOP_END:
            switch (tok->con.flags & REDUCE_TYPE_MASK) {
                case RT_HISTORY:
                    if (hist_offset > tok->con.reduce_stop) {
                        --hist_offset;
#if TRACE_EVAL
                        printf("History idx = -%d\n", hist_offset);
#endif
                        tok -= tok->con.branch_offset;
                        goto repeat;
                    }
                    else {
                        hist_offset = 0;
#if TRACE_EVAL
                        printf("History loop done.\n");
#endif
                    }
                    break;
                case RT_INSTANCE:
                    /* increment instance idx */
                    if (x) {
                        for (i = inst_idx + 1; i < mpr_value_get_num_inst(x); i++) {
                            if (mpr_value_get_num_samps(x, i) >= expr->max_src_mlen)
                                break;
                        }
                    }
                    if (x && i < mpr_value_get_num_inst(x)) {
#if TRACE_EVAL
                        printf("Instance idx = %d\n", i);
#endif
                        inst_idx = i;
                        tok -= tok->con.branch_offset;
                        goto repeat;
                    }
                    else {
#if TRACE_EVAL
                        printf("Instance loop done; restoring instance idx from offset %d: ",
                               tok->con.cache_offset * -1);
                        expr_value_print(vals + sp - tok->con.cache_offset * vlen, MPR_INT32, 1,
                                         dp - tok->con.cache_offset);
#endif
                        inst_idx = vals[sp - tok->con.cache_offset * vlen].i;
                        if (x && inst_idx >= mpr_value_get_num_inst(x))
                            goto error;
                        if (tok->con.cache_offset > 0) {
                            int dp_temp = dp - tok->con.cache_offset;
                            for (dp_temp = dp - tok->con.cache_offset; dp_temp < dp; dp_temp++) {
                                int sp_temp = dp_temp * vlen;
                                expr_value_cpy(vals + sp_temp, vals + sp_temp + vlen, vlen);
                                lens[dp_temp] = lens[dp_temp + 1];
                                types[dp_temp] = types[dp_temp + 1];
                            }
                            INCR_STACK_PTR(-1);
                        }
                        --cache;
                    }
                    break;
                case RT_SIGNAL:
                    ++sig_offset;
                    if (sig_offset < expr->num_src) {
#if TRACE_EVAL
                        printf("Signal idx = %d\n", sig_offset);
#endif
                        tok -= tok->con.branch_offset;
                        goto repeat;
                    }
                    else {
                        sig_offset = 0;
#if TRACE_EVAL
                        printf("Signal loop done.\n");
#endif
                    }
                    break;
                case RT_VECTOR:
                    RETURN_ARG_UNLESS(v_in, status);
                    ++vec_offset;
                    if (USE_VAR_LEN & tok->con.flags) {
                        if (vec_offset < mpr_value_get_vlen(v_in[sig_offset])) {
#if TRACE_EVAL
                            printf("Vector idx = %d of %d\n", vec_offset,
                                   mpr_value_get_vlen(v_in[sig_offset]));
#endif
                            tok -= tok->con.branch_offset;
                            goto repeat;
                        }
                        else {
                            vec_offset = 0;
#if TRACE_EVAL
                            printf("Vector loop done.\n");
#endif
                        }
                        break;
                    }
                    if (vec_offset < tok->con.reduce_stop) {
#if TRACE_EVAL
                        printf("Vector idx = %d of %d\n", vec_offset, tok->con.reduce_stop);
#endif
                        tok -= tok->con.branch_offset;
                        goto repeat;
                    }
                    else {
                        vec_offset = 0;
#if TRACE_EVAL
                        printf("Vector loop done.\n");
#endif
                    }
                    break;
                default:
                    goto error;
            }
            break;
        case TOK_COPY_FROM: {
            int dp_from = dp - tok->con.cache_offset;
            int sp_from = dp_from * vlen;
            assert(dp_from >= 0 && dp_from < buff_size);
            INCR_STACK_PTR(1);
            SET_TYPE(tok->gen.datatype);
            SET_LEN(tok->gen.vec_len);
            if (lens[dp] < lens[dp_from])
                expr_value_cpy(&vals[sp], &vals[sp_from + vec_offset], tok->gen.vec_len);
            else
                expr_value_cpy(&vals[sp], &vals[sp_from], tok->gen.vec_len);
#if TRACE_EVAL
            expr_value_print(vals + sp, types[dp], lens[dp], dp);
#endif
            break;
        }
        case TOK_MOVE: {
            int dp_from = dp;
            int sp_from = sp;
            INCR_STACK_PTR(-tok->con.cache_offset);
            expr_value_cpy(&vals[sp], &vals[sp_from], vlen);
            SET_TYPE(types[dp_from]);
            SET_LEN(lens[dp_from]);
#if TRACE_EVAL
            expr_value_print(vals + sp, types[dp], lens[dp], dp);
#endif
            break;
        }
        case TOK_VECTORIZE:
            /* don't need to copy vector elements from first token */
            INCR_STACK_PTR(1 - tok->fn.arity);
            for (i = 1, j = lens[dp]; i < tok->fn.arity; i++, j += lens[dp + i]) {
                expr_value_cpy(&vals[sp + j], &vals[sp + i * vlen], lens[dp + i]);
            }
            SET_TYPE(tok->gen.datatype);
            SET_LEN(j);
#if TRACE_EVAL
            expr_value_print(vals + sp, types[dp], lens[dp], dp);
#endif
            break;
        case TOK_ASSIGN:
        case TOK_ASSIGN_USE:
            if (VAR_Y == tok->var.idx)
                can_advance = 0;
        case TOK_ASSIGN_CONST: {
            mpr_value v;
            /* currently only history and vector indices are supported for assignment */
            int idxp, hidx = tok->gen.flags & VAR_HIST_IDX, vidx = tok->gen.flags & VAR_VEC_IDX;
            int num_flags = NUM_VAR_IDXS(tok->gen.flags);
            if (num_flags) {
                INCR_STACK_PTR(-num_flags);
            }
            idxp = dp + 1;

            if (VAR_Y == tok->var.idx) {
                if (!alive)
                    goto assign_done;
                status |= muted ? EXPR_MUTED_UPDATE : EXPR_UPDATE;
                can_advance = 0;
                if (!v_out)
                    return status;
                v = v_out;
            }
            else if (tok->var.idx >= 0 && tok->var.idx < N_USER_VARS) {
                uint8_t flags = expr->vars[tok->var.idx].flags;
                if (flags & VAR_SET_EXTERN) {
#if TRACE_EVAL
                    printf("skipping assignment to %s (set externally)\n",
                           expr->vars[tok->var.idx].name);
#endif
                    goto assign_done;
                }
                if (!v_vars)
                    goto error;
                /* passed the address of an array of mpr_value structs */
                v = v_vars[tok->var.idx];
            }
            else
                goto error;

            if (vidx) {
                switch (types[idxp]) {
                    case MPR_INT32: vidx = vals[sp + vlen].i;       break;
                    case MPR_FLT:   vidx = (int)vals[sp + vlen].f;  break;
                    case MPR_DBL:   vidx = (int)vals[sp + vlen].d;  break;
                    default:
                        printf("error: illegal type %d/'%c'\n", types[idxp], types[idxp]);
                        goto error;
                }
                ++idxp;
            }
            else
                vidx = tok->var.vec_idx;
            while (vidx < 0)
                vidx += mpr_value_get_vlen(v);
            vidx = vidx % (int)mpr_value_get_vlen(v);
            if (hidx) {
                if (MPR_INT32 != types[idxp])
                    goto error;
                hidx = vals[idxp * vlen].i;
                if (hidx > 0 || hidx < -mpr_value_get_mlen(v_out))
                    goto error;
                /* TODO: enable full history assignment with user variables */
                if (VAR_Y != tok->var.idx)
                    hidx = 0;
                ++idxp;
            }
#if TRACE_EVAL
            if (VAR_Y == tok->var.idx)
                printf("\n\t\tvar.y");
            else
                printf("\n\t\tvar.%s", expr->vars[tok->var.idx].name);
            printf("{%s%d}", tok->gen.flags & VAR_HIST_IDX ? "N=" : "", hidx);
            printf("[%s%d]", tok->gen.flags & VAR_VEC_IDX ? "N=" : "", vidx);
            printf(" (%c x %u)\n", types[dp], tok->gen.vec_len);
#endif

            /* Copy time from input */
            if (time)
                mpr_value_set_time(v, *time, inst_idx, hidx);

            switch (mpr_value_get_type(v)) {
#define TYPED_CASE(MTYPE, TYPE, T)                                                              \
                case MTYPE: {                                                                   \
                    TYPE *a = (TYPE*)mpr_value_get_value(v, inst_idx, hidx);                    \
                    for (i = vidx, j = tok->var.offset; i < tok->gen.vec_len + vidx; i++, j++) {\
                        if (j >= lens[dp]) j = 0;                                               \
                        a[i] = vals[sp + j].T;                                                  \
                    }                                                                           \
                    break;                                                                      \
                }
                TYPED_CASE(MPR_INT32, int, i);
                TYPED_CASE(MPR_FLT, float, f);
                TYPED_CASE(MPR_DBL, double, d);
#undef TYPED_CASE
                default:
                    goto error;
            }

#if TRACE_EVAL
            printf("\n");
#if DEBUG
            mpr_value_print_inst_hist(v, inst_idx % mpr_value_get_num_inst(v));
#endif /* DEBUG */
#endif /* TRACE_EVAL */

            if (tok->var.idx == VAR_Y) {
                if (has_value) {
                    for (i = 0, j = vidx; i < tok->gen.vec_len; i++, j++) {
                        if (j >= mpr_value_get_vlen(v)) j = 0;
                        mpr_bitflags_set(has_value, j);
                    }
                }
            }
            else if (tok->var.idx == expr->inst_ctl) {
                if (alive && vals[sp].i == 0) {
                    if (status & EXPR_UPDATE)
                        status |= EXPR_RELEASE_AFTER_UPDATE;
                    else
                        status |= EXPR_RELEASE_BEFORE_UPDATE;
                }
                alive = vals[sp].i != 0;
                can_advance = 0;
            }
            else if (tok->var.idx == expr->mute_ctl) {
                muted = vals[sp].i != 0;
                can_advance = 0;
            }

        assign_done:
            /* If assignment was constant or history initialization, move expr
             * start token pointer so we don't evaluate this section again. */

            if (can_advance || tok->gen.flags & VAR_HIST_IDX) {
#if TRACE_EVAL
                printf("\n     move start\t%ld\n", tok - expr->tokens + 1);
#endif
                expr->offset = tok - expr->tokens + 1;
            }
            else
                can_advance = 0;

            if (tok->gen.flags & CLEAR_STACK)
                dp = -1;
            sp = dp * vlen;
            break;
        }
        case TOK_ASSIGN_TT: {
            int hidx;
            mpr_time t;
            if (tok->var.idx != VAR_Y || !(tok->gen.flags & VAR_HIST_IDX))
                goto error;
#if TRACE_EVAL
            printf("\n\t\ttt.y{%d}\n", tok->gen.flags & VAR_HIST_IDX ? vals[sp - vlen].i : 0);
#endif
            if (!v_out)
                return status;
            assert(types[dp] == MPR_DBL && types[dp - 1] == MPR_INT32);
            hidx = vals[sp - vlen].i;
            mpr_time_set_dbl(&t, vals[sp].d);
            mpr_value_set_time(v_out, t, inst_idx, hidx);
            /* If assignment was constant or history initialization, move expr
             * start token pointer so we don't evaluate this section again. */
            if (1) {
#if TRACE_EVAL
                printf("     move start\t%ld\n", tok - expr->tokens + 1);
#endif
                expr->offset = tok - expr->tokens + 1;
            }
            else
                can_advance = 0;
            if (tok->gen.flags & CLEAR_STACK)
                dp = -1;
            else {
                INCR_STACK_PTR(-1);
            }
            break;
        }
        default: goto error;
        }
        if (tok->gen.casttype) {
            assert(dp >= 0);
#if TRACE_EVAL
            printf("     cast\tvals[%d] %c->%c\t\t", dp, types[dp], tok->gen.casttype);
#endif
            /* need to cast to a different type */
            switch (types[dp]) {
#define TYPED_CASE(MTYPE0, T0, MTYPE1, TYPE1, T1, MTYPE2, TYPE2, T2)\
                case MTYPE0:                                        \
                    switch (tok->gen.casttype) {                    \
                        case MTYPE1:                                \
                            for (i = sp; i < sp + lens[dp]; i++)    \
                                vals[i].T1 = (TYPE1)vals[i].T0;     \
                            break;                                  \
                        case MTYPE2:                                \
                            for (i = sp; i < sp + lens[dp]; i++)    \
                                vals[i].T2 = (TYPE2)vals[i].T0;     \
                            break;                                  \
                        default:                                    \
                            break;                                  \
                    }                                               \
                    break;
                TYPED_CASE(MPR_INT32, i, MPR_FLT, float, f, MPR_DBL, double, d)
                TYPED_CASE(MPR_FLT, f, MPR_INT32, int, i, MPR_DBL, double, d)
                TYPED_CASE(MPR_DBL, d, MPR_INT32, int, i, MPR_FLT, float, f)
#undef TYPED_CASE
            }
            SET_TYPE(tok->gen.casttype);
#if TRACE_EVAL
            expr_value_print(vals + sp, types[dp], lens[dp], dp);
#endif
        }
        ++tok;
    }

    RETURN_ARG_UNLESS(v_out, status);

    if (!has_value) {
        /* Internal evaluation during parsing doesn't contain assignment token,
         * so we need to copy to output here. */
        void *v = mpr_value_get_value(v_out, inst_idx, 0);
        switch (mpr_value_get_type(v_out)) {
#define TYPED_CASE(MTYPE, TYPE, T)                                          \
            case MTYPE:                                                     \
                for (i = 0, j = sp; i < mpr_value_get_vlen(v_out); i++, j++)\
                    ((TYPE*)v)[i] = vals[j].T;                              \
                break;
            TYPED_CASE(MPR_INT32, int, i)
            TYPED_CASE(MPR_FLT, float, f)
            TYPED_CASE(MPR_DBL, double, d)
#undef TYPED_CASE
            default:
                goto error;
        }
        return status;
    }

    /* Undo position increment if nothing was updated. */
    if (!(status & (EXPR_UPDATE | EXPR_MUTED_UPDATE))) {
        mpr_value_decr_idx(v_out, inst_idx);
        return status;
    }

    return status;

  error:
#if TRACE_EVAL
    trace("Unexpected token in expression.");
#endif
    return 0;
}
