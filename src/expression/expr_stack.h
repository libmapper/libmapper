#ifndef __MPR_EXPR_STACK_H__
#define __MPR_EXPR_STACK_H__

#include <assert.h>
#include "expr_token.h"

#define ESTACK_TOP -1

typedef struct _estack
{
    etoken_t *tokens;
    uint8_t offset;
    uint8_t num_tokens;
    uint8_t vec_len;
} estack_t, *estack;

#if TRACE_PARSE
static void estack_print(const char *s, estack stk, expr_var_t *vars, int show_init_line);
#endif

estack estack_new(uint8_t num_tokens)
{
    estack stk = calloc(1, sizeof(estack_t));
    if (num_tokens)
        stk->tokens = calloc(1, num_tokens * sizeof(etoken_t));
    return stk;
}

void estack_cpy(estack to, estack from)
{
    to->num_tokens = from->num_tokens;
    to->vec_len = from->vec_len;
    to->offset = from->offset;
    to->tokens = malloc(sizeof(etoken_t) * (size_t)from->num_tokens);
    memcpy(to->tokens, from->tokens, sizeof(etoken_t) * (size_t)from->num_tokens);

#if TRACE_PARSE
    printf("Copied %d tokens to expression\n", from->num_tokens);
    estack_print(NULL, to, NULL, 0);
    printf("Copied vec_len %d\n", to->vec_len);
#endif
}

void estack_free(estack stk, int free_token_mem)
{
    if (free_token_mem) {
        int i;
        for (i = 0; i < stk->num_tokens; i++)
            etoken_free(&stk->tokens[i]);
    }
    FUNC_IF(free, stk->tokens);
    free(stk);
}

static int estack_replace_special_constants(estack stk)
{
    int i = stk->num_tokens;
    while (--i >= 0) {
        if (etoken_replace_special_constants(&stk->tokens[i]))
            return -1;
    }
    return 0;
}

static etoken estack_push(estack stk, etoken tok)
{
    memcpy(stk->tokens + stk->num_tokens, tok, sizeof(etoken_t));
    ++stk->num_tokens;
    return &stk->tokens[stk->num_tokens - 1];
}

static void estack_push_int(estack stk, int val, int vec_len, uint8_t flags)
{
    etoken_t tok;
    etoken_set_int32(&tok, val);
    tok.gen.casttype = 0;
    tok.gen.vec_len = vec_len;
    tok.gen.flags = flags;
    estack_push(stk, &tok);
}

static etoken estack_pop(estack stk)
{
    if (stk->num_tokens) {
        etoken tok = &stk->tokens[stk->num_tokens - 1];
        --stk->num_tokens;
        return tok;
    }
    return 0;
}

static etoken estack_peek(estack stk, int idx)
{
    if (idx < 0)
        idx += stk->num_tokens;
    if (idx >= 0 && idx < stk->num_tokens)
        return &stk->tokens[idx];
    return 0;
}

static int estack_promote_tokens(estack stk, int idx, mpr_type type, int vec_len)
{
    etoken_t *tokens = stk->tokens;
    etoken_t *tok;
    int modified = 0;

    /* don't promote type of variable indices */
    if (tokens[idx].gen.datatype == type && tokens[idx].gen.casttype == MPR_INT32)
        goto done;

    while (TOK_COPY_FROM == tokens[idx].toktype) {
        int offset = tokens[idx].con.cache_offset + 1;
        tokens[idx].gen.datatype = type;
        if (!(tokens[idx].gen.flags & VEC_LEN_LOCKED) && tokens[idx].gen.vec_len < vec_len)
            tokens[idx].gen.vec_len = vec_len;
        while (offset > 0 && idx > 0) {
            --idx;
            if (TOK_LOOP_START == tokens[idx].toktype || TOK_SP_ADD == tokens[idx].toktype)
                offset -= etoken_get_arity(&tokens[idx]);
            else if (TOK_LOOP_END == tokens[idx].toktype)
                offset += etoken_get_arity(&tokens[idx]);
            else if (tokens[idx].toktype <= TOK_MOVE)
                offset += etoken_get_arity(&tokens[idx]) - 1;
        }
        assert(idx >= 0);
    }
    tok = &tokens[idx];

    if (tok->toktype > TOK_MOVE && type != tok->gen.datatype) {
        if (tok->toktype != TOK_LOOP_END) {
            tok->gen.datatype = type;
            modified = 1;
        }
        else if (tok->gen.casttype != type) {
            tok->gen.casttype = type;
            modified = 1;
        }
        goto done;
    }

    tok->gen.casttype = 0;

    if (!(tok->gen.flags & VEC_LEN_LOCKED) && tok->gen.vec_len < vec_len) {
        tok->gen.vec_len = vec_len;
        modified = 1;
    }

    if (tok->gen.datatype == type)
        goto done;

    if (tok->toktype >= TOK_ASSIGN) {
        if (tok->var.idx < VAR_Y) {
            /* user-defined variable, can typecast */
            tok->var.casttype = type;
            modified = 1;
        }
    }
    else if (TOK_LITERAL == tok->toktype) {
        if (!(tok->gen.flags & TYPE_LOCKED)) {
            /* constants can be cast immediately */
            if (MPR_INT32 == tok->lit.datatype) {
                if (MPR_FLT == type) {
                    tok->lit.val.f = (float)tok->lit.val.i;
                    tok->lit.datatype = type;
                }
                else if (MPR_DBL == type) {
                    tok->lit.val.d = (double)tok->lit.val.i;
                    tok->lit.datatype = type;
                }
            }
            else if (MPR_FLT == tok->lit.datatype) {
                if (MPR_DBL == type) {
                    tok->lit.val.d = (double)tok->lit.val.f;
                    tok->lit.datatype = type;
                }
                else if (MPR_INT32 == type)
                    tok->lit.casttype = type;
            }
            else
                tok->lit.casttype = type;
            modified = 1;
        }
    }
    else if (TOK_VLITERAL == tok->toktype) {
        if (!(tok->gen.flags & TYPE_LOCKED)) {
            /* constants can be cast immediately */
            if (MPR_INT32 == tok->lit.datatype) {
                if (MPR_FLT == type) {
                    float *tmp = malloc((int)tok->lit.vec_len * sizeof(float));
                    int i;
                    for (i = 0; i < tok->lit.vec_len; i++)
                        tmp[i] = (float)tok->lit.val.ip[i];
                    free(tok->lit.val.ip);
                    tok->lit.val.fp = tmp;
                    tok->lit.datatype = type;
                }
                else if (MPR_DBL == type) {
                    double *tmp = malloc((int)tok->lit.vec_len * sizeof(double));
                    int i;
                    for (i = 0; i < tok->lit.vec_len; i++)
                        tmp[i] = (double)tok->lit.val.ip[i];
                    free(tok->lit.val.ip);
                    tok->lit.val.dp = tmp;
                    tok->lit.datatype = type;
                }
            }
            else if (MPR_FLT == tok->lit.datatype) {
                if (MPR_DBL == type) {
                    double *tmp = malloc((int)tok->lit.vec_len * sizeof(double));
                    int i;
                    for (i = 0; i < tok->lit.vec_len; i++)
                        tmp[i] = (double)tok->lit.val.fp[i];
                    free(tok->lit.val.fp);
                    tok->lit.val.dp = tmp;
                    tok->lit.datatype = type;
                }
            }
            else
                tok->lit.casttype = type;
            modified = 1;
        }
    }
    else if (TOK_VAR == tok->toktype || TOK_VAR_NUM_INST == tok->toktype || TOK_RFN == tok->toktype) {
        /* we need to cast at runtime */
        tok->gen.casttype = type;
        modified = 1;
    }
    else {
        if (!(tok->gen.flags & TYPE_LOCKED) && (MPR_INT32 == tok->gen.datatype || MPR_DBL == type)) {
            tok->gen.datatype = type;
        }
        else {
            tok->gen.casttype = type;
        }
        modified = 1;
    }
done:
    return modified;
}

static void precompute(estack stk, uint8_t num_tokens_to_compute)
{
    mpr_expr_eval_buffer buff;
    etoken tok = estack_peek(stk, ESTACK_TOP);
    mpr_type type = tok->gen.datatype;
    mpr_expr expr;
    mpr_value val;
    int i, vec_len = tok->gen.vec_len;

    if (estack_replace_special_constants(stk))
        return;

    /* temporarily set the stk 'offset' variable */
    stk->offset = stk->num_tokens - num_tokens_to_compute;
    stk->vec_len = vec_len;
    expr = mpr_expr_new(0, 0, stk);
    buff = mpr_expr_new_eval_buffer(expr);
    val = mpr_value_new(vec_len, type, 1, 1);
    mpr_value_incr_idx(val, 0);

    if (!(mpr_expr_eval(expr, buff, 0, 0, val, 0, 0, 0) & 1))
        goto done;

    /* TODO: should we also do this for TOK_RFN? */
    if (tok->toktype == TOK_VFN && vfn_tbl[tok->fn.idx].reduce) {
        vec_len = 1;
    }
    for (i = 0; i < num_tokens_to_compute; i++) {
        tok = estack_peek(stk, ESTACK_TOP - i);
        if (TOK_VLITERAL == tok->toktype)
            free(tok->lit.val.ip);
    }
    /* tok is now at stk->offset */

    switch (type) {
#define TYPED_CASE(MTYPE, TYPE, T)                                  \
        case MTYPE: {                                               \
            TYPE *a = (TYPE*)mpr_value_get_value(val, 0, 0);        \
            if (vec_len > 1) {                                      \
                int i;                                              \
                tok->toktype = TOK_VLITERAL;                        \
                tok->lit.val.T##p = malloc(vec_len * sizeof(TYPE)); \
                for (i = 0; i < vec_len; i++)                       \
                    tok->lit.val.T##p[i] = ((TYPE*)a)[i];           \
            }                                                       \
            else {                                                  \
                tok->toktype = TOK_LITERAL;                         \
                tok->lit.val.T = ((TYPE*)a)[0];                     \
            }                                                       \
            break;                                                  \
        }
        TYPED_CASE(MPR_INT32, int, i)
        TYPED_CASE(MPR_FLT, float, f)
        TYPED_CASE(MPR_DBL, double, d)
#undef TYPED_CASE
        default:
            goto done;
    }
    tok->gen.flags &= ~CONST_SPECIAL;
    tok->gen.datatype = type;
    tok->gen.vec_len = vec_len;
    stk->num_tokens = stk->offset + 1;
    stk->offset = 0;

done:
    mpr_value_free(val);
    mpr_expr_free(expr);
    mpr_expr_free_eval_buffer(buff);
    stk->offset = 0;
}

static int estack_get_substack_len(estack stk, int start_idx)
{
    int idx, arity = 0;
    etoken_t *tokens = stk->tokens;

    if (start_idx < 0)
        start_idx += stk->num_tokens;
    assert(start_idx >= 0 && start_idx < stk->num_tokens);
    idx = start_idx;

    do {
        if (tokens[idx].toktype < TOK_LOOP_END)
            --arity;
        arity += etoken_get_arity(&tokens[idx]);
        if (TOK_ASSIGN & tokens[idx].toktype)
            ++arity;
        --idx;
    } while (arity >= 0 && idx >= 0);
    return start_idx - idx;
}

static int estack_get_reduce_types(estack stk)
{
    uint8_t i, flags = 0;
    etoken_t *tokens = stk->tokens;
    for (i = 0; i < stk->num_tokens; i++) {
        if (TOK_REDUCING == tokens[i].toktype) {
            flags |= tokens[i].con.flags;
        }
    }
    return flags;
}

static void estack_cpy_tok(estack stk, int dst_idx, int src_idx)
{
    if (src_idx < 0)
        src_idx += stk->num_tokens;
    if (dst_idx < 0)
        dst_idx += stk->num_tokens;
    assert(src_idx >= 0 && src_idx < stk->num_tokens);
    assert(dst_idx >= 0 && dst_idx < stk->num_tokens);

    etoken_cpy(&stk->tokens[dst_idx], &stk->tokens[src_idx]);
}

MPR_INLINE static int estack_squash(estack stk)
{
    int idx = stk->num_tokens - 1;
    etoken_t *tokens = stk->tokens;

    assert(stk->num_tokens >= 2);

    if (etoken_squash(&tokens[idx-1], &tokens[idx])) {
        --stk->num_tokens;
        return 1;
    }
    return 0;
}

static etoken estack_check_type(estack stk, expr_var_t *vars, int enable_optimize)
{
    /* TODO: enable precomputation of const-only vectors */
    int i, sp = stk->num_tokens - 1, arity, can_precompute = 1, optimize = NONE;
    etoken_t *tokens = stk->tokens;
    mpr_type type = tokens[sp].gen.datatype;
    uint8_t vec_len = tokens[sp].gen.vec_len;

    switch (tokens[sp].toktype) {
        case TOK_OP:
            if (tokens[sp].op.idx == OP_IF) {
                trace("Ternary operator is missing operand.\n");
                return 0;
            }
            arity = op_tbl[tokens[sp].op.idx].arity;
            break;
        case TOK_FN:
            arity = fn_tbl[tokens[sp].fn.idx].arity;
            if (tokens[sp].fn.idx >= FN_DEL_IDX)
                can_precompute = 0;
            break;
        case TOK_VFN:
            if (VFN_CONCAT == tokens[sp].fn.idx || VFN_LENGTH == tokens[sp].fn.idx)
                return &tokens[sp];
            arity = vfn_tbl[tokens[sp].fn.idx].arity;
            break;
        case TOK_VECTORIZE:
            arity = tokens[sp].fn.arity;
            can_precompute = 0;
            break;
        case TOK_ASSIGN:
        case TOK_ASSIGN_CONST:
        case TOK_ASSIGN_TT:
        case TOK_ASSIGN_USE:
            arity = NUM_VAR_IDXS(tokens[sp].gen.flags) + 1;
            can_precompute = 0;
            break;
        case TOK_COPY_FROM:
        case TOK_MOVE:
            arity = 1;
            break;
        default:
            return &tokens[sp];
    }

    if (arity) {
        /* find operator or function inputs */
        uint8_t skip = 0;
        uint8_t depth = arity;
        uint8_t operand = 0;
        uint8_t vec_reduce = 0;
        i = sp;

        /* Walk down stk distance of arity, checking types. */
        while (--i >= 0) {
            if (tokens[i].toktype >= TOK_LOOP_START) {
                can_precompute = enable_optimize = 0;
                continue;
            }

            if (tokens[i].toktype == TOK_FN) {
                if (fn_tbl[tokens[i].fn.idx].arity) {
                    can_precompute = 0;
                }
            }
            else if (tokens[i].toktype > TOK_VLITERAL) {
                can_precompute = 0;
            }

            if (skip == 0) {
                int j;
                if (enable_optimize && tokens[i].toktype == TOK_LITERAL && tokens[sp].toktype == TOK_OP
                    && depth <= op_tbl[tokens[sp].op.idx].arity) {
                    if (etoken_get_is_0(&tokens[i])) {
                        /* mask and bitshift, depth == 1 or 2 */
                        optimize = (op_tbl[tokens[sp].op.idx].optimize_const_ops >> (depth - 1) * 4) & 0xF;
                    }
                    else if (etoken_get_is_1(&tokens[i])) {
                        optimize = (op_tbl[tokens[sp].op.idx].optimize_const_ops >> (depth + 1) * 4) & 0xF;
                    }
                    if (optimize == GET_OPER) {
                        if (i == sp - 1) {
                            /* optimize immediately without moving other operand */
                            stk->num_tokens -= 2;
                            return &tokens[sp - 2];
                        }
                        else {
                            /* store position of non-zero operand */
                            operand = sp - 1;
                        }
                    }
                }
                j = i;
                do {
                    type = etoken_cmp_datatype(&tokens[j], type);
                    if (tokens[j].gen.vec_len > vec_len) {
                        vec_len = tokens[j].gen.vec_len;
                    }
                    if (TOK_COPY_FROM == tokens[j].toktype) {
                        uint8_t offset = tokens[j].con.cache_offset + 1;
                        uint8_t vec_reduce = 0;
                        while (offset > 0 && j > 0) {
                            --j;
                            if (TOK_SP_ADD == tokens[j].toktype)
                                offset -= tokens[j].lit.val.i;
                            else if (TOK_LOOP_START == tokens[j].toktype) {
                                if (tokens[j].con.flags & RT_INSTANCE)
                                    --offset;
                                else if (tokens[j].con.flags & RT_VECTOR)
                                    ++vec_reduce;
                            }
                            else if (TOK_LOOP_END == tokens[j].toktype) {
                                if (tokens[j].con.flags & RT_INSTANCE)
                                    ++offset;
                                else if (tokens[j].con.flags & RT_VECTOR)
                                    --vec_reduce;
                            }
                            else if (tokens[j].toktype <= TOK_MOVE)
                                offset += etoken_get_arity(&tokens[j]) - 1;
                            type = etoken_cmp_datatype(&tokens[j], type);
                            if (vec_reduce <= 0 && tokens[j].gen.vec_len > vec_len) {
                                vec_len = tokens[j].gen.vec_len;
                            }
                        }
                        assert(j >= 0);
                    }
                } while (TOK_COPY_FROM == tokens[j].toktype);
                --depth;
                if (depth == 0)
                    break;
            }
            else
                --skip;

            skip += etoken_get_arity(&tokens[i]);
            if (TOK_VFN == tokens[i].toktype && (   VFN_MAXMIN == tokens[i].fn.idx
                                                 || VFN_SUMNUM == tokens[i].fn.idx
                                                 || VFN_CONCAT == tokens[i].fn.idx)) {
                /* these functions have 2 outputs */
                --skip;
            }
        }

        if (depth)
            return 0;

        if (enable_optimize && !can_precompute) {
            switch (optimize) {
                case BAD_EXPR:
                    trace("Operator '%s' cannot have zero operand.\n", op_tbl[tokens[sp].op.idx].name);
                    return 0;
                case GET_ZERO:
                case GET_ONE: {
                    /* finish walking down compound arity */
                    int _arity = 0;
                    while ((_arity += etoken_get_arity(&tokens[i])) && i >= 0) {
                        --_arity;
                        --i;
                    }
                    etoken_set_int32(&tokens[i], (optimize == GET_ZERO) ? 0 : 1);
                    /* clear locks and casttype */
                    tokens[i].gen.flags &= ~(VEC_LEN_LOCKED | TYPE_LOCKED);
                    tokens[i].gen.casttype = 0;
                    stk->num_tokens = i + 1;
                    return &tokens[i];
                }
                case GET_OPER:
                    /* copy tokens for non-zero operand */
                    for (; i < operand; i++)
                        memcpy(tokens + i, tokens + i + 1, TOKEN_SIZE);
                    stk->num_tokens = i + 1;
                    return &tokens[i];
                default:
                    break;
            }
        }

        /* walk down stk distance of arity again, promoting types
         * this time we will also touch sub-arguments */
        i = sp;
        switch (tokens[sp].toktype) {
            case TOK_VECTORIZE:  skip = tokens[sp].fn.arity;                depth = 0;      break;
            case TOK_ASSIGN_USE: skip = 1;                                  depth = 0;      break;
            case TOK_VAR:        skip = NUM_VAR_IDXS(tokens[sp].gen.flags); depth = 0;      break;
            default:             skip = 0;                                  depth = arity;  break;
        }
        estack_promote_tokens(stk, i, type, 0);
        while (--i >= 0) {
            int j = i;
            if (TOK_LOOP_END == tokens[i].toktype && tokens[i].con.flags & RT_VECTOR)
                vec_reduce = 1;
            else if (TOK_LOOP_START == tokens[i].toktype && tokens[i].con.flags & RT_VECTOR)
                vec_reduce = 0;
            if (tokens[i].toktype >= TOK_LOOP_START)
                continue;

            /* promote types within range of compound arity */
            do {
                if (skip <= 0) {
                    estack_promote_tokens(stk, j, type, vec_reduce ? 0 : vec_len);
                    --depth;
                    if (!vec_reduce && !(tokens[j].gen.flags & VEC_LEN_LOCKED)) {
                        tokens[j].var.vec_len = vec_len;
                        if (TOK_VAR == tokens[j].toktype && tokens[j].var.idx < N_USER_VARS)
                            vars[tokens[j].var.idx].vec_len = vec_len;
                    }
                }
                else {
                    estack_promote_tokens(stk, j, type, 0);
                }

                if (TOK_COPY_FROM == tokens[j].toktype) {
                    int offset = tokens[j].con.cache_offset + 1;
                    while (offset > 0 && j > 0) {
                        --j;
                        if (TOK_SP_ADD == tokens[j].toktype)
                            offset -= tokens[j].lit.val.i;
                        else if (TOK_LOOP_START == tokens[j].toktype && tokens[j].con.flags & RT_INSTANCE)
                            --offset;
                        else if (TOK_LOOP_END == tokens[j].toktype && tokens[j].con.flags & RT_INSTANCE)
                            ++offset;
                        else if (tokens[j].toktype <= TOK_MOVE)
                            offset += etoken_get_arity(&tokens[j]) - 1;
                        estack_promote_tokens(stk, j, type, 0);
                    }
                    assert(j >= 0);
                }
            } while (TOK_COPY_FROM == tokens[j].toktype);

            if (TOK_ASSIGN_USE == tokens[i].toktype) {
                skip += etoken_get_arity(&tokens[i]) + 2;
            }
            if (TOK_VECTORIZE == tokens[i].toktype || TOK_VFN == tokens[i].toktype) {
                skip += etoken_get_arity(&tokens[i]) + 1;
            }
            else if (skip > 0) {
                skip += etoken_get_arity(&tokens[i]);
            }
            else {
                depth += etoken_get_arity(&tokens[i]);
            }

            if (skip > 0)
                --skip;
            if (depth <= 0 && skip <= 0)
                break;
        }
    }

    if (!(tokens[sp].gen.flags & VEC_LEN_LOCKED)) {
        if (tokens[sp].toktype != TOK_VFN || VFN_SORT == tokens[sp].fn.idx)
            tokens[sp].gen.vec_len = vec_len;
    }

    /* if stk within bounds of arity was only constants, we're ok to compute */
    if (enable_optimize && can_precompute) {
#if TRACE_PARSE
        printf("precomputing tokens[%d:%d]\n", sp - arity, sp + 1);
#endif
        precompute(stk, arity + 1);
    }
    return &tokens[stk->num_tokens-1];
}

// TODO: merge with _check_type()
static int estack_check_assign_type_and_len(estack stk, expr_var_t *vars)
{
    int sp = stk->num_tokens - 1, i = sp, j, expr_len = 0, vec_len = 0;
    etoken_t *tokens = stk->tokens;
    int8_t var = tokens[sp].var.idx;

    while (i >= 0 && (tokens[i].toktype & TOK_ASSIGN) && (tokens[i].var.idx == var)) {
        int num_var_idx = NUM_VAR_IDXS(tokens[i].gen.flags);
        --i;
        for (j = 0; j < num_var_idx; j++)
            i -= estack_get_substack_len(stk, i - j);
    }

    j = i;
    while (j < sp && !(tokens[j].toktype & TOK_ASSIGN))
        ++j;

    expr_len = sp - j;
    expr_len += estack_get_substack_len(stk, j);

    if (expr_len > sp + 1) {
        trace("Malformed expression (1)\n");
        return -1;
    }

    /* if the subexpr contains uniform(), should pass assignment vec_len rather than 0 */
    for (--j; j > sp - expr_len; j--) {
        if (TOK_FN == tokens[j].toktype && FN_UNIFORM == tokens[j].fn.idx) {
            vec_len = tokens[sp].gen.vec_len;
            break;
        }
    }

    estack_promote_tokens(stk, i, tokens[sp].gen.datatype, vec_len);

    /* cache num_tokens and set stack top to i */
    uint8_t tmp = stk->num_tokens;
    stk->num_tokens = i + 1;

    if (!estack_check_type(stk, vars, 1))
        return -1;

    estack_promote_tokens(stk, i, tokens[sp].gen.datatype, 0);

    /* restore num_tokens */
    stk->num_tokens = tmp;

    if (tokens[sp].var.idx < N_USER_VARS) {
        /* Check if this expression assignment is instance-reducing */
        int reducing = 1, skipping = 0;
        for (i = 0; i < expr_len; i++) {
            switch (tokens[sp - i].toktype) {
                case TOK_LOOP_START:
                    skipping = 0;
                    break;
                case TOK_LOOP_END:
                    skipping = 1;
                    reducing *= 2;
                    break;
                case TOK_VAR:
                    if (!skipping && tokens[sp - i].var.idx >= VAR_X_NEWEST)
                        reducing = 0;
                    break;
                default:
                    break;
            }
        }
        if (reducing > 1 && (vars[tokens[sp].var.idx].flags & VAR_INSTANCED))
            vars[tokens[sp].var.idx].flags &= ~VAR_INSTANCED;
    }

    if (!(tokens[sp].gen.flags & VAR_HIST_IDX))
        return 0;

    if (expr_len == sp + 1) {
        /* This statement is already at the start of the expression stack. */
        return 0;
    }

    /* Need to move assignment statements to beginning of stack. */

    for (i = sp - expr_len; i > 0; i--) {
        if (tokens[i].toktype & TOK_ASSIGN && !(tokens[i].gen.flags & VAR_HIST_IDX))
            break;
    }

    if (i > 0) {
        /* This expression statement needs to be moved. */
        etoken_t *temp = malloc(expr_len * TOKEN_SIZE);
        memcpy(temp, tokens + sp - expr_len + 1, expr_len * TOKEN_SIZE);
        sp = sp - expr_len + 1;
        for (; sp >= 0; sp = sp - expr_len) {
            /* batch copy tokens in blocks of expr_len to avoid memcpy overlap */
            int len = expr_len;
            if (sp < len) {
                len = sp;
            }
            memcpy(tokens + sp - len + expr_len, tokens + sp - len, len * TOKEN_SIZE);
        }
        memcpy(tokens, temp, expr_len * TOKEN_SIZE);
        free(temp);
    }

    return 0;
}

static void estack_update_vec_len(estack stk, unsigned int vec_len)
{
    if (vec_len > stk->vec_len)
        stk->vec_len = vec_len;
}

static void estack_lock_vec_len(estack stk)
{
    int arity = 1, idx = stk->num_tokens - 1;
    while ((idx >= 0) && arity--) {
        etoken tok = &stk->tokens[idx];
        tok->gen.flags |= VEC_LEN_LOCKED;
        // TODO: try etoken_get_arity() instead
        switch (tok->toktype) {
            case TOK_OP:        arity += op_tbl[tok->op.idx].arity; break;
            case TOK_FN:        arity += fn_tbl[tok->fn.idx].arity; break;
            case TOK_VECTORIZE: arity += tok->fn.arity;             break;
            default:                                                break;
        }
        --idx;
    }
}

static int estack_get_eval_buffer_size(estack stk)
{
    int i = 0, sp = 0, eval_buffer_len = 0;
    etoken_t *tok = stk->tokens;
    while (i < stk->num_tokens && tok->toktype != TOK_END) {
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
                sp -= etoken_get_arity(tok);
                break;

            case TOK_ASSIGN:
            case TOK_ASSIGN_CONST:
            case TOK_ASSIGN_TT:
                --sp;
                /* continue... */
            case TOK_ASSIGN_USE:
                sp -= etoken_get_arity(tok);
                break;

            case TOK_LOOP_START:
                /* may need to cache instance */
            case TOK_SP_ADD:
                sp += etoken_get_arity(tok);
                break;
            case TOK_LOOP_END:
                /* may need to uncache instance */
                sp -= etoken_get_arity(tok);
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

static int estack_get_reduces_inst(estack stk)
{
    int i, reducing = 0;
    for (i = 0; i < stk->num_tokens; i++) {
        etoken tok = &stk->tokens[i];
        switch (tok->toktype) {
            case TOK_LOOP_START:
                if (tok->con.flags & RT_INSTANCE)
                    reducing = 1;
                break;
            case TOK_LOOP_END:
                if (tok->con.flags & RT_INSTANCE)
                    reducing = 0;
            case TOK_VAR:
                if (!reducing && tok->var.idx >= VAR_X_NEWEST)
                    return 0;
                break;
            default:
                break;
        }
    }
    return 1;
}

#if TRACE_PARSE
static void estack_print(const char *s, estack stk, expr_var_t *vars, int show_init_line)
{
    int i, j, indent = 0, can_advance = 1;
    etoken_t *tokens = stk->tokens;
    if (s)
        printf("%s:\n", s);
    if (!stk->num_tokens) {
        printf("  --- <EMPTY> ---\n");
        return;
    }
    for (i = 0; i < stk->num_tokens; i++) {
        if (show_init_line && can_advance) {
            switch (tokens[i].toktype) {
                case TOK_ASSIGN_CONST:
                case TOK_ASSIGN:
                case TOK_ASSIGN_USE:
                case TOK_ASSIGN_TT:
                    /* look ahead for future assignments */
                    for (j = i + 1; j < stk->num_tokens; j++) {
                        if (tokens[j].toktype < TOK_ASSIGN)
                            continue;
                        if (TOK_ASSIGN_CONST == tokens[j].toktype && tokens[j].var.idx != VAR_Y)
                            break;
                        if (tokens[j].gen.flags & VAR_HIST_IDX)
                            break;
                        for (j = 0; j < indent; j++)
                            printf(" ");
                        can_advance = 0;
                        break;
                    }
                    break;
                case TOK_RFN:
                case TOK_VAR:
                    if (tokens[i].var.idx >= VAR_X_NEWEST)
                        can_advance = 0;
                    break;
                default:
                    break;
            }
            printf(" %2d: ", i);
            etoken_print(&tokens[i], vars, 1);
            printf("\n");
            if (i && !can_advance)
                printf("  --- <INITIALISATION DONE> ---\n");
        }
        else {
            printf(" %2d: ", i);
            etoken_print(&tokens[i], vars, 1);
            printf("\n");
        }
    }
}
#endif /* TRACE_PARSE */

#endif /* __MPR_EXPR_STACK_H__ */
