#ifndef __MPR_EXPRESSION_PARSER_H__
#define __MPR_EXPRESSION_PARSER_H__

#include <assert.h>
#include "expression_lexer.h"

#define STACK_SIZE 64

/* TODO: move most of this logic into expression_token.h */
static mpr_type promote_token(expr_tok_t *tokens, int sp, mpr_type type, int vec_len,
                              expr_var_t *vars)
{
    expr_tok_t *tok;

    /* don't promote type of variable indices */
    if (tokens[sp].gen.datatype == type && tokens[sp].gen.casttype == MPR_INT32)
        return type;

    while (TOK_COPY_FROM == tokens[sp].toktype) {
        int offset = tokens[sp].con.cache_offset + 1;
        tokens[sp].gen.datatype = type;
        if (vec_len && !(tokens[sp].gen.flags & VEC_LEN_LOCKED))
            tokens[sp].gen.vec_len = vec_len;
        while (offset > 0 && sp > 0) {
            --sp;
            if (TOK_LOOP_START == tokens[sp].toktype || TOK_SP_ADD == tokens[sp].toktype)
                offset -= expr_tok_get_arity(&tokens[sp]);
            else if (TOK_LOOP_END == tokens[sp].toktype)
                offset += expr_tok_get_arity(&tokens[sp]);
            else if (tokens[sp].toktype <= TOK_MOVE)
                offset += expr_tok_get_arity(&tokens[sp]) - 1;
        }
        assert(sp >= 0);
    }
    tok = &tokens[sp];

    if (tok->toktype > TOK_MOVE && type != tok->gen.datatype) {
        if (tok->toktype == TOK_LOOP_END)
            tok->gen.casttype = type;
        else
            tok->gen.datatype = type;
        return type;
    }

    tok->gen.casttype = 0;

    if (vec_len && !(tok->gen.flags & VEC_LEN_LOCKED))
        tok->gen.vec_len = vec_len;

    if (tok->gen.datatype == type)
        return type;

    if (tok->toktype >= TOK_ASSIGN) {
        if (tok->var.idx >= VAR_Y) {
            /* typecasting is not possible */
            return tok->var.datatype;
        }
        else {
            /* user-defined variable, can typecast */
            tok->var.casttype = type;
            return type;
        }
    }

    if (TOK_LITERAL == tok->toktype) {
        if (tok->gen.flags & TYPE_LOCKED)
            return tok->var.datatype;
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
        return type;
    }
    else if (TOK_VLITERAL == tok->toktype) {
        int i;
        if (tok->gen.flags & TYPE_LOCKED)
            return tok->var.datatype;
        /* constants can be cast immediately */
        if (MPR_INT32 == tok->lit.datatype) {
            if (MPR_FLT == type) {
                float *tmp = malloc((int)tok->lit.vec_len * sizeof(float));
                for (i = 0; i < tok->lit.vec_len; i++)
                    tmp[i] = (float)tok->lit.val.ip[i];
                free(tok->lit.val.ip);
                tok->lit.val.fp = tmp;
                tok->lit.datatype = type;
            }
            else if (MPR_DBL == type) {
                double *tmp = malloc((int)tok->lit.vec_len * sizeof(double));
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
                for (i = 0; i < tok->lit.vec_len; i++)
                    tmp[i] = (double)tok->lit.val.fp[i];
                free(tok->lit.val.fp);
                tok->lit.val.dp = tmp;
                tok->lit.datatype = type;
            }
        }
        else
            tok->lit.casttype = type;
        return type;
    }
    else if (TOK_VAR == tok->toktype || TOK_VAR_NUM_INST == tok->toktype || TOK_RFN == tok->toktype) {
        /* we need to cast at runtime */
        tok->gen.casttype = type;
        return type;
    }
    else {
        if (!(tok->gen.flags & TYPE_LOCKED) && (MPR_INT32 == tok->gen.datatype || MPR_DBL == type)) {
            tok->gen.datatype = type;
            return type;
        }
        else {
            tok->gen.casttype = type;
            return tok->gen.datatype;
        }
    }
    return type;
}

static void lock_vec_len(expr_tok_t *tokens, int sp)
{
    int i = sp, arity = 1;
    while ((i >= 0) && arity--) {
        tokens[i].gen.flags |= VEC_LEN_LOCKED;
        switch (tokens[i].toktype) {
            case TOK_OP:        arity += op_tbl[tokens[i].op.idx].arity; break;
            case TOK_FN:        arity += fn_tbl[tokens[i].fn.idx].arity; break;
            case TOK_VECTORIZE: arity += tokens[i].fn.arity;             break;
            default:                                                     break;
        }
        --i;
    }
}

static int replace_special_constants(expr_tok_t *tokens, int sp)
{
    while (sp >= 0) {
        if (tokens[sp].toktype != TOK_LITERAL || !(tokens[sp].gen.flags & CONST_SPECIAL)) {
            --sp;
            continue;
        }
        switch (tokens[sp].gen.flags & CONST_SPECIAL) {
            case CONST_MAXVAL:
                switch (tokens[sp].lit.datatype) {
                    case MPR_INT32: tokens[sp].lit.val.i = INT_MAX; break;
                    case MPR_FLT:   tokens[sp].lit.val.f = FLT_MAX; break;
                    case MPR_DBL:   tokens[sp].lit.val.d = DBL_MAX; break;
                    default:                                        goto error;
                }
                break;
            case CONST_MINVAL:
                switch (tokens[sp].lit.datatype) {
                    case MPR_INT32: tokens[sp].lit.val.i = INT_MIN;  break;
                    case MPR_FLT:   tokens[sp].lit.val.f = -FLT_MAX; break;
                    case MPR_DBL:   tokens[sp].lit.val.d = -DBL_MAX; break;
                    default:                                         goto error;
                }
                break;
            case CONST_PI:
                switch (tokens[sp].lit.datatype) {
                    case MPR_FLT:   tokens[sp].lit.val.f = M_PI;    break;
                    case MPR_DBL:   tokens[sp].lit.val.d = M_PI;    break;
                    default:                                        goto error;
                }
                break;
            case CONST_E:
                switch (tokens[sp].lit.datatype) {
                    case MPR_FLT:   tokens[sp].lit.val.f = M_E;     break;
                    case MPR_DBL:   tokens[sp].lit.val.d = M_E;     break;
                    default:                                        goto error;
                }
                break;
            default:
                continue;
        }
        tokens[sp].gen.flags &= ~CONST_SPECIAL;
        --sp;
    }
    return 0;
error:
#if TRACE_PARSE
    printf("Illegal type found when replacing special constants.\n");
#endif
    return -1;
}

static int precompute(mpr_expr_eval_buffer buff, expr_tok_t *tokens, int len, int vec_len)
{
    mpr_type type = tokens[len - 1].gen.datatype;
    mpr_expr expr;
    mpr_value val;

    if (replace_special_constants(tokens, len - 1))
        return 0;

    expr = mpr_expr_new(0, 0, tokens, len, vec_len);
    mpr_expr_realloc_eval_buffer(expr, buff);
    val = mpr_value_new(vec_len, type, 1, 1);

    if (!(mpr_expr_eval(expr, buff, 0, 0, val, 0, 0, 0) & 1)) {
        mpr_value_free(val);
        mpr_expr_free(expr);
        return 0;
    }

    /* TODO: should we also do this for TOK_RFN? */
    if (tokens[len - 1].toktype == TOK_VFN && vfn_tbl[tokens[len - 1].fn.idx].reduce) {
        vec_len = 1;
    }

    switch (type) {
#define TYPED_CASE(MTYPE, TYPE, T)                                      \
        case MTYPE: {                                                   \
            TYPE *a = (TYPE*)mpr_value_get_value(val, 0, 0);            \
            if (TOK_VLITERAL == tokens[0].toktype)                      \
                free(tokens[0].lit.val.ip);                             \
            if (vec_len > 1) {                                          \
                int i;                                                  \
                tokens[0].toktype = TOK_VLITERAL;                       \
                tokens[0].lit.val.T##p = malloc(vec_len * sizeof(TYPE));\
                for (i = 0; i < vec_len; i++)                           \
                    tokens[0].lit.val.T##p[i] = ((TYPE*)a)[i];          \
            }                                                           \
            else {                                                      \
                tokens[0].toktype = TOK_LITERAL;                        \
                tokens[0].lit.val.T = ((TYPE*)a)[0];                    \
            }                                                           \
            break;                                                      \
        }
        TYPED_CASE(MPR_INT32, int, i)
        TYPED_CASE(MPR_FLT, float, f)
        TYPED_CASE(MPR_DBL, double, d)
#undef TYPED_CASE
        default:
            mpr_value_free(val);
            mpr_expr_free(expr);
            return 0;
    }
    tokens[0].gen.flags &= ~CONST_SPECIAL;
    tokens[0].gen.datatype = type;
    tokens[0].gen.vec_len = vec_len;
    mpr_value_free(val);
    mpr_expr_free(expr);
    return len - 1;
}

static int check_type(mpr_expr_eval_buffer buff, expr_tok_t *tokens, int sp, expr_var_t *vars,
                      int enable_optimize)
{
    /* TODO: enable precomputation of const-only vectors */
    int i, arity, can_precompute = 1, optimize = NONE;
    mpr_type type = tokens[sp].gen.datatype;
    uint8_t vec_len = tokens[sp].gen.vec_len;
    switch (tokens[sp].toktype) {
        case TOK_OP:
            if (tokens[sp].op.idx == OP_IF) {
                trace("Ternary operator is missing operand.\n");
                return -1;
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
                return sp;
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
        case TOK_LOOP_END:
        case TOK_COPY_FROM:
        case TOK_MOVE:
            arity = 1;
            break;
        default:
            return sp;
    }
    if (arity) {
        /* find operator or function inputs */
        uint8_t skip = 0;
        uint8_t depth = arity;
        uint8_t operand = 0;
        uint8_t vec_reduce = 0;
        i = sp;

        /* Walk down stack distance of arity, checking types. */
        while (--i >= 0) {
            if (tokens[i].toktype >= TOK_LOOP_START) {
                can_precompute = enable_optimize = 0;
                continue;
            }

            if (tokens[i].toktype == TOK_FN) {
                if (fn_tbl[tokens[i].fn.idx].arity)
                    can_precompute = 0;
            }
            else if (tokens[i].toktype > TOK_VLITERAL)
                can_precompute = 0;

            if (skip == 0) {
                int j;
                if (enable_optimize && tokens[i].toktype == TOK_LITERAL && tokens[sp].toktype == TOK_OP
                    && depth <= op_tbl[tokens[sp].op.idx].arity) {
                    if (const_tok_is_zero(&tokens[i])) {
                        /* mask and bitshift, depth == 1 or 2 */
                        optimize = (op_tbl[tokens[sp].op.idx].optimize_const_ops >> (depth - 1) * 4) & 0xF;
                    }
                    else if (const_tok_equals_one(&tokens[i])) {
                        optimize = (op_tbl[tokens[sp].op.idx].optimize_const_ops >> (depth + 1) * 4) & 0xF;
                    }
                    if (optimize == GET_OPER) {
                        if (i == sp - 1) {
                            /* optimize immediately without moving other operand */
                            return sp - 2;
                        }
                        else {
                            /* store position of non-zero operand */
                            operand = sp - 1;
                        }
                    }
                }
                j = i;
                do {
                    type = expr_tok_cmp_datatype(&tokens[j], type);
                    if (tokens[j].gen.vec_len > vec_len)
                        vec_len = tokens[j].gen.vec_len;
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
                                offset += expr_tok_get_arity(&tokens[j]) - 1;
                            type = expr_tok_cmp_datatype(&tokens[j], type);
                            if (vec_reduce <= 0 && tokens[j].gen.vec_len > vec_len)
                                vec_len = tokens[j].gen.vec_len;
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

            skip += expr_tok_get_arity(&tokens[i]);
            if (TOK_VFN == tokens[i].toktype && (   VFN_MAXMIN == tokens[i].fn.idx
                                                 || VFN_SUMNUM == tokens[i].fn.idx
                                                 || VFN_CONCAT == tokens[i].fn.idx)) {
                /* these functions have 2 outputs */
                --skip;
            }
        }

        if (depth)
            return -1;

        if (enable_optimize && !can_precompute) {
            switch (optimize) {
                case BAD_EXPR:
                    trace("Operator '%s' cannot have zero operand.\n", op_tbl[tokens[sp].op.idx].name);
                    return -1;
                case GET_ZERO:
                case GET_ONE: {
                    /* finish walking down compound arity */
                    int _arity = 0;
                    while ((_arity += expr_tok_get_arity(&tokens[i])) && i >= 0) {
                        --_arity;
                        --i;
                    }
                    expr_tok_set_int32(&tokens[i], (optimize == GET_ZERO) ? 0 : 1);
                    /* clear locks and casttype */
                    tokens[i].gen.flags &= ~(VEC_LEN_LOCKED | TYPE_LOCKED);
                    tokens[i].gen.casttype = 0;
                    return i;
                }
                case GET_OPER:
                    /* copy tokens for non-zero operand */
                    for (; i < operand; i++)
                        memcpy(tokens + i, tokens + i + 1, TOKEN_SIZE);
                    return i;
                default:
                    break;
            }
        }

        /* walk down stack distance of arity again, promoting types
         * this time we will also touch sub-arguments */
        i = sp;
        switch (tokens[sp].toktype) {
            case TOK_VECTORIZE:  skip = tokens[sp].fn.arity;                depth = 0;      break;
            case TOK_ASSIGN_USE: skip = 1;                                  depth = 0;      break;
            case TOK_VAR:        skip = NUM_VAR_IDXS(tokens[sp].gen.flags); depth = 0;      break;
            default:             skip = 0;                                  depth = arity;  break;
        }
        promote_token(tokens, i, type, 0, 0);
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
                    promote_token(tokens, j, type, vec_reduce ? 0 : vec_len, 0);
                    --depth;
                    if (!vec_reduce && !(tokens[j].gen.flags & VEC_LEN_LOCKED)) {
                        tokens[j].var.vec_len = vec_len;
                        if (TOK_VAR == tokens[j].toktype && tokens[j].var.idx < N_USER_VARS)
                            vars[tokens[j].var.idx].vec_len = vec_len;
                    }
                }
                else {
                    promote_token(tokens, j, type, 0, 0);
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
                            offset += expr_tok_get_arity(&tokens[j]) - 1;
                        promote_token(tokens, j, type, 0, 0);
                    }
                    assert(j >= 0);
                }
            } while (TOK_COPY_FROM == tokens[j].toktype);


            if (TOK_VECTORIZE == tokens[i].toktype || TOK_VFN == tokens[i].toktype) {
                skip += expr_tok_get_arity(&tokens[i]) + 1;
            }
            else if (skip > 0) {
                skip += expr_tok_get_arity(&tokens[i]);
            }
            else {
                depth += expr_tok_get_arity(&tokens[i]);
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

    /* if stack within bounds of arity was only constants, we're ok to compute */
    if (enable_optimize && can_precompute) {
#if TRACE_PARSE
        printf("precomputing tokens[%d:%d]\n", sp - arity, arity + 1);
#endif
        return sp - precompute(buff, &tokens[sp - arity], arity + 1, vec_len);
    }
    else
        return sp;
}

static int substack_len(expr_tok_t *tokens, int sp)
{
    int idx = sp, arity = 0;
    do {
        if (tokens[idx].toktype < TOK_LOOP_END)
            --arity;
        arity += expr_tok_get_arity(&tokens[idx]);
        if (TOK_ASSIGN & tokens[idx].toktype)
            ++arity;
        --idx;
    } while (arity >= 0 && idx >= 0);
    return sp - idx;
}

static int check_assign_type_and_len(mpr_expr_eval_buffer buff, expr_tok_t *tokens, int sp,
                                     expr_var_t *vars)
{
    int i = sp, j, optimize = 1, expr_len = 0, vec_len = 0;
    int8_t var = tokens[sp].var.idx;

    while (i >= 0 && (tokens[i].toktype & TOK_ASSIGN) && (tokens[i].var.idx == var)) {
        int num_var_idx = NUM_VAR_IDXS(tokens[i].gen.flags);
        --i;
        for (j = 0; j < num_var_idx; j++)
            i -= substack_len(tokens, i - j);
    }

    j = i;
    while (j < sp && !(tokens[j].toktype & TOK_ASSIGN))
        ++j;

    expr_len = sp - j;
    expr_len += substack_len(tokens, j);

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

    promote_token(tokens, i, tokens[sp].gen.datatype, vec_len, vars);
    if (check_type(buff, tokens, i, vars, optimize) == -1)
        return -1;
    promote_token(tokens, i, tokens[sp].gen.datatype, 0, vars);

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
        expr_tok_t *temp = malloc(expr_len * TOKEN_SIZE);
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

/* Macros to help express stack operations in parser. */
#define FAIL(msg) {     \
    trace("%s\n", msg); \
    goto error;         \
}

#define FAIL_IF(condition, msg) \
    if (condition) {FAIL(msg)}

#define PUSH_TO_OUTPUT(x)                                           \
{                                                                   \
    {FAIL_IF(++out_idx >= STACK_SIZE, "Stack size exceeded. (1)");} \
    if (x.toktype == TOK_ASSIGN_CONST && !is_const)                 \
        x.toktype = TOK_ASSIGN;                                     \
    memcpy(out + out_idx, &x, TOKEN_SIZE);                          \
}

#define PUSH_INT_TO_OUTPUT(x)   \
{                               \
    expr_tok_t t;               \
    expr_tok_set_int32(&t, x);  \
    t.gen.casttype = 0;         \
    t.gen.vec_len = 1;          \
    t.gen.flags = 0;            \
    PUSH_TO_OUTPUT(t);          \
}

#define POP_OUTPUT() ( out_idx-- )

#define PUSH_TO_OPERATOR(x)                                         \
{                                                                   \
    {FAIL_IF(++op_idx >= STACK_SIZE, "Stack size exceeded. (2)");}  \
    memcpy(op + op_idx, &x, TOKEN_SIZE);                            \
}

#define POP_OPERATOR() ( op_idx-- )

#define POP_OPERATOR_TO_OUTPUT()                            \
{                                                           \
    PUSH_TO_OUTPUT(op[op_idx]);                             \
    out_idx = check_type(buff, out, out_idx, vars, 1);      \
    {FAIL_IF(out_idx < 0, "Malformed expression (2).");}    \
    POP_OPERATOR();                                         \
}

#define POP_OUTPUT_TO_OPERATOR()    \
{                                   \
    PUSH_TO_OPERATOR(out[out_idx]); \
    POP_OUTPUT();                   \
}

#define GET_NEXT_TOKEN(x)                   \
{                                           \
    x.toktype = TOK_UNKNOWN;                \
    lex_idx = expr_lex(str, lex_idx, &x);   \
    {FAIL_IF(!lex_idx, "Error in lexer.");} \
}

#define ADD_TO_VECTOR()                                                 \
{                                                                       \
    switch (out[out_idx].toktype) {                                     \
        case TOK_LOOP_END:                                              \
            op[op_idx].gen.vec_len += out[out_idx-1].gen.vec_len;       \
            ++op[op_idx].fn.arity;                                      \
            break;                                                      \
        case TOK_LITERAL:                                               \
            if (op[op_idx].fn.arity && squash_to_vector(out, out_idx)) {\
                ++op[op_idx].gen.vec_len;                               \
                POP_OUTPUT();                                           \
                break;                                                  \
            }                                                           \
            /* else continue to default... */                           \
        default:                                                        \
            op[op_idx].gen.vec_len += out[out_idx].gen.vec_len;         \
            ++op[op_idx].fn.arity;                                      \
    }                                                                   \
}

int squash_to_vector(expr_tok_t *tokens, int idx)
{
    expr_tok_t *a = tokens + idx, *b = a - 1;
    if (idx < 1 || b->gen.flags & VEC_LEN_LOCKED)
        return 0;
    if (TOK_LITERAL == b->toktype) {
        int i;
        void *tmp;
        mpr_type type = expr_tok_cmp_datatype(a, b->lit.datatype);
        switch (type) {
            case MPR_INT32:
                tmp = malloc(2 * sizeof(int));
                ((int*)tmp)[0] = b->lit.val.i;
                ((int*)tmp)[1] = a->lit.val.i;
                break;
            case MPR_FLT:
                tmp = malloc(2 * sizeof(float));
                for (i = 0; i < 2; i++) {
                    switch (b[i].lit.datatype) {
                        case MPR_INT32: ((float*)tmp)[i] = (float)b[i].lit.val.i;   break;
                        default:        ((float*)tmp)[i] = b[i].lit.val.f;          break;
                    }
                }
                break;
            default:
                tmp = malloc(2 * sizeof(double));
                for (i = 0; i < 2; i++) {
                    switch (b[i].lit.datatype) {
                        case MPR_INT32: ((double*)tmp)[i] = (double)b[i].lit.val.i; break;
                        case MPR_FLT:   ((double*)tmp)[i] = (double)b[i].lit.val.f; break;
                        default:        ((double*)tmp)[i] = b[i].lit.val.d;         break;
                    }
                }
                break;
        }
        b->toktype = TOK_VLITERAL;
        b->gen.flags &= ~VEC_LEN_LOCKED;
        b->lit.val.ip = tmp;
        b->lit.datatype = type;
        b->lit.vec_len = 2;
        return 1;
    }
    else if (TOK_VLITERAL == b->toktype && !(b->gen.flags & VEC_LEN_LOCKED)) {
        int i, vec_len = b->lit.vec_len;
        void *tmp = 0;
        mpr_type type = expr_tok_cmp_datatype(a, b->lit.datatype);
        ++b->lit.vec_len;
        switch (type) {
            case MPR_INT32:
                /* both vector and new scalar are type MPR_INT32 */
                tmp = malloc(b->lit.vec_len * sizeof(int));
                for (i = 0; i < vec_len; i++)
                    ((int*)tmp)[i] = b->lit.val.ip[i];
                ((int*)tmp)[vec_len] = a->lit.val.i;
                break;
            case MPR_FLT:
                tmp = malloc(b->lit.vec_len * sizeof(float));
                for (i = 0; i < vec_len; i++) {
                    switch (b->lit.datatype) {
                        case MPR_INT32: ((float*)tmp)[i] = (float)b->lit.val.ip[i];     break;
                        default:        ((float*)tmp)[i] = b->lit.val.fp[i];            break;
                    }
                }
                switch (a->lit.datatype) {
                    case MPR_INT32:     ((float*)tmp)[vec_len] = (float)a->lit.val.i;   break;
                    default:            ((float*)tmp)[vec_len] = a->lit.val.f;          break;
                }
                break;
            case MPR_DBL:
                tmp = malloc(b->lit.vec_len * sizeof(double));
                for (i = 0; i < vec_len; i++) {
                    switch (b->lit.datatype) {
                        case MPR_INT32: ((double*)tmp)[i] = (double)b->lit.val.ip[i];   break;
                        case MPR_FLT:   ((double*)tmp)[i] = (double)b->lit.val.fp[i];   break;
                        default:        ((double*)tmp)[i] = b->lit.val.dp[i];           break;
                    }
                }
                switch (a->lit.datatype) {
                    case MPR_INT32:     ((double*)tmp)[vec_len] = (double)a->lit.val.i; break;
                    case MPR_FLT:       ((double*)tmp)[vec_len] = (double)a->lit.val.f; break;
                    default:            ((double*)tmp)[vec_len] = a->lit.val.d;         break;
                }
                break;
        }
        if (tmp && tmp != b->lit.val.ip) {
            free(b->lit.val.ip);
            b->lit.val.ip = tmp;
        }
        b->lit.datatype = type;
        return 1;
    }
    return 0;
}

typedef struct _temp_var_cache {
    const char *in_name;
    const char *accum_name;
    struct _temp_var_cache *next;
    uint16_t scope_start;
    uint8_t loop_start_pos;
} temp_var_cache_t, *temp_var_cache;

#define ASSIGN_MASK (TOK_VAR | TOK_OPEN_SQUARE | TOK_COMMA | TOK_CLOSE_SQUARE | TOK_CLOSE_CURLY \
                     | TOK_OPEN_CURLY | TOK_NEGATE | TOK_LITERAL | TOK_COLON)
#define OBJECT_TOKENS (TOK_VAR | TOK_LITERAL | TOK_FN | TOK_VFN | TOK_MUTED | TOK_NEGATE \
                       | TOK_OPEN_PAREN | TOK_OPEN_SQUARE | TOK_OP | TOK_TT)
#define JOIN_TOKENS (TOK_OP | TOK_CLOSE_PAREN | TOK_CLOSE_SQUARE | TOK_CLOSE_CURLY | TOK_COMMA \
                     | TOK_COLON | TOK_SEMICOLON)

/*! Use Dijkstra's shunting-yard algorithm to parse expression into RPN stack. */
int expr_parser_build_stack(mpr_expr expr, const char *str,
                            int num_src, const mpr_type *src_types, const unsigned int *src_lens,
                            int num_dst, const mpr_type *dst_types, const unsigned int *dst_lens)
{
    expr_tok_t out[STACK_SIZE];
    expr_tok_t op[STACK_SIZE];
    expr_var_t vars[N_USER_VARS];
    int i, lex_idx = 0, out_idx = -1, op_idx = -1;

    mpr_expr_eval_buffer buff;

    /* TODO: use bitflags instead? */
    uint8_t assigning = 0, is_const = 1, out_assigned = 0, muted = 0, vectorizing = 0;
    uint8_t lambda_allowed = 0;
    int var_flags = 0;
    uint8_t reduce_types = 0, max_vec_len = 0;
    int allow_toktype = 0x2FFFFF;
    int vec_len_ctx = 0;

    temp_var_cache temp_vars = NULL;
    /* TODO: optimise these vars */
    int num_var = 0;
    expr_tok_t tok;
    mpr_type type_hi = MPR_INT32, type_lo = MPR_DBL;

    /* allocate a temporary evaluation buffer for opportunistic precomputation */
    buff = mpr_expr_new_eval_buffer();

    /* ignoring spaces at start of expression */
    while (str[lex_idx] == ' ') ++lex_idx;
    {FAIL_IF(!str[lex_idx], "No expression found.");}

    assigning = 1;
    allow_toktype = TOK_VAR | TOK_TT | TOK_OPEN_SQUARE | TOK_MUTED;

    /* Find lowest and highest signal types */
    for (i = 0; i < num_src; i++) {
        if (src_types[i] < type_hi)
            type_hi = src_types[i];
        if (src_types[i] > type_lo)
            type_lo = src_types[i];
    }
    for (i = 0; i < num_dst; i++) {
        if (dst_types[i] < type_hi)
            type_hi = dst_types[i];
        if (dst_types[i] > type_lo)
            type_lo = dst_types[i];
    }

    for (i = 0; i < num_src; i++) {
        if (src_lens[i] > max_vec_len)
            max_vec_len = src_lens[i];
    }

    memset(out, 0, TOKEN_SIZE * STACK_SIZE);
    memset(op, 0, TOKEN_SIZE * STACK_SIZE);

#if TRACE_PARSE
    printf("parsing expression '%s'\n", str);
#endif

    while (str[lex_idx]) {
        GET_NEXT_TOKEN(tok);
        /* TODO: streamline handling assigning and lambda_allowed flags */
        if (TOK_LAMBDA == tok.toktype) {
            if (!lambda_allowed) {
#if TRACE_PARSE
                printf("Illegal token sequence (1): ");
                print_token(&tok, vars, 1);
#endif
                goto error;
            }
        }
        else if (!(tok.toktype & allow_toktype)) {
#if TRACE_PARSE
            printf("Illegal token sequence: ");
            print_token(&tok, vars, 1);
#endif
            goto error;
        }
        switch (tok.toktype) {
            case TOK_OPEN_CURLY:
            case TOK_OPEN_SQUARE:
            case TOK_DOLLAR:
                if (!(var_flags & tok.toktype))
                    var_flags = 0;
                break;
            default:
                if (!(var_flags & VAR_IDXS))
                    var_flags = 0;
                break;
        }
        switch (tok.toktype) {
            case TOK_MUTED:
                muted = 1;
                allow_toktype = TOK_VAR | TOK_TT;
                break;
            case TOK_LITERAL:
                /* push to output stack */
                PUSH_TO_OUTPUT(tok);
                allow_toktype = JOIN_TOKENS;
                break;
            case TOK_VAR:
            case TOK_TT: {
                /* get name of variable */
                int len;
                const char *varname = _get_var_str_and_len(str, lex_idx - 1, &len);
                /* first check if we have a variable scoped to local reduce function */
                temp_var_cache var_cache_list = temp_vars, found_in = 0, found_accum = 0;
                while (var_cache_list) {
                    /* break after finding match in case of nested duplicates */
                    // TODO: deal with timetag references
                    if (strncmp(var_cache_list->in_name, varname, len) == 0) {
                        found_in = var_cache_list;
                        break;
                    }
                    else if (strncmp(var_cache_list->accum_name, varname, len) == 0) {
                        found_accum = var_cache_list;
                        break;
                    }
                    var_cache_list = var_cache_list->next;
                }
                if (found_in) {
                    int offset = -1;
#if TRACE_PARSE
                    printf("found reference to local input variable '%s'\n", found_in->in_name);
#endif
                    {FAIL_IF(!found_in->loop_start_pos,
                             "local input variable used before lambda token.");}
                    i = found_in->loop_start_pos + 1;
                    while (i <= out_idx) {
                        if (out[i].toktype <= TOK_MOVE)
                            offset += 1 - expr_tok_get_arity(&out[i]);
                        ++i;
                    }
                    tok.toktype = TOK_COPY_FROM;
                    tok.con.cache_offset = offset;

                    i = out_idx - offset;
                    {FAIL_IF(i < 0, "Compilation error (1)");}
                    if (reduce_types & RT_VECTOR)
                        tok.con.vec_len = 1;
                    else
                        tok.con.vec_len = out[i].gen.vec_len;
                    tok.con.datatype = out[i].gen.casttype ? out[i].gen.casttype : out[i].gen.datatype;

                    /* TODO: handle timetags */
                    is_const = 0;
                    PUSH_TO_OUTPUT(tok);

                    var_flags = 0;
                    if (!(reduce_types & RT_VECTOR))
                        var_flags |= TOK_OPEN_SQUARE;
                    if (!(reduce_types & RT_HISTORY))
                        var_flags |= TOK_OPEN_CURLY;
                    allow_toktype = (var_flags | TOK_VFN_DOT | TOK_RFN | TOK_OP | TOK_CLOSE_PAREN
                                     | TOK_CLOSE_SQUARE | TOK_CLOSE_CURLY | TOK_COLON);
                    break;
                }
                if (found_accum) {
                    int pos = found_accum->loop_start_pos, stack_offset = 0;
#if TRACE_PARSE
                    printf("found reference to local accumulator variable '%s'\n",
                           found_accum->accum_name);
#endif
                    {FAIL_IF(!found_accum->loop_start_pos,
                             "local input variable used before lambda token.");}
                    while (pos <= out_idx) {
                        if (TOK_SP_ADD == out[pos].toktype)
                            stack_offset += out[pos].lit.val.i;
                        else if (TOK_LOOP_START == out[pos].toktype
                                 && out[pos].con.flags & RT_INSTANCE)
                            ++stack_offset;
                        else if (TOK_LOOP_END == out[pos].toktype
                                 && out[pos].con.flags & RT_INSTANCE)
                            --stack_offset;
                        else if (out[pos].toktype < TOK_LAMBDA)
                            stack_offset += 1 - expr_tok_get_arity(&out[pos]);
                        ++pos;
                    }

                    memcpy(&tok, &out[out_idx - stack_offset], TOKEN_SIZE);
                    tok.toktype = TOK_COPY_FROM;
                    tok.con.cache_offset = stack_offset;
                    PUSH_TO_OUTPUT(tok);
                    allow_toktype = (TOK_VFN_DOT | TOK_RFN | TOK_OP | TOK_CLOSE_PAREN
                                     | TOK_CLOSE_SQUARE | TOK_CLOSE_CURLY | TOK_COLON);
                    break;
                }

                if (tok.var.idx == VAR_X_NEWEST) {
                    tok.gen.datatype = type_lo;
                    tok.gen.casttype = type_hi;
                    tok.gen.vec_len = max_vec_len;
                    tok.gen.flags |= (TYPE_LOCKED | VEC_LEN_LOCKED);
                    is_const = 0;
                }
                else if (tok.var.idx >= VAR_X) {
                    int slot = tok.var.idx - VAR_X;
                    {FAIL_IF(slot >= num_src, "Input slot index > number of sources.");}
                    tok.gen.datatype = src_types[slot];
                    tok.gen.vec_len = (TOK_VAR == tok.toktype) ? src_lens[slot] : 1;
                    tok.gen.flags |= (TYPE_LOCKED | VEC_LEN_LOCKED);
                    is_const = 0;
                }
                else if (tok.var.idx == VAR_Y) {
                    tok.gen.datatype = dst_types[0];
                    tok.gen.vec_len = (TOK_VAR == tok.toktype) ? dst_lens[0] : 1;
                    tok.gen.flags |= (TYPE_LOCKED | VEC_LEN_LOCKED);
                }
                else {
                    if (TOK_TT == tok.toktype) {
                        varname += 2;
                        len -= 2;
                    }
                    i = find_var_by_name(vars, num_var, varname, len);
                    if (i >= 0) {
                        tok.var.idx = i;
                        tok.gen.datatype = vars[i].datatype;
                        tok.gen.vec_len = vars[i].vec_len;
                        if (tok.gen.vec_len)
                            tok.gen.flags |= VEC_LEN_LOCKED;
                    }
                    else {
                        {FAIL_IF(num_var >= N_USER_VARS, "Maximum number of variables exceeded.");}
                        /* need to store new variable */
                        expr_var_set(&vars[num_var], varname, len, type_hi, 0, VAR_INSTANCED);
#if TRACE_PARSE
                        printf("Stored new variable '%s' at index %i\n", vars[num_var].name, num_var);
#endif
                        tok.var.idx = num_var;
                        tok.var.datatype = type_hi;
                        /* special case: 'alive' tracks instance lifetime */
                        // TODO: switch to variable property
                        if (   strcmp(vars[num_var].name, "alive") == 0
                            || strcmp(vars[num_var].name, "muted") == 0) {
                            vars[num_var].vec_len = tok.gen.vec_len = 1;
                            vars[num_var].datatype = tok.gen.datatype = MPR_INT32;
                            tok.gen.flags |= (TYPE_LOCKED | VEC_LEN_LOCKED);
                            if (vars[num_var].name[0] == 'a')
                                is_const = 0;
                        }
                        else
                            tok.gen.vec_len = 0;
                        ++num_var;
                    }
                    if (!assigning)
                        is_const = 0;
                }
                vec_len_ctx = tok.gen.vec_len;
                tok.var.vec_idx = 0;
                if (muted)
                    tok.gen.flags |= VAR_MUTED;

                /* timetag tokens have type double */
                if (tok.toktype == TOK_TT)
                    tok.gen.datatype = MPR_DBL;
                PUSH_TO_OUTPUT(tok);

                /* variables can have vector and history indices */
                var_flags = TOK_OPEN_SQUARE | TOK_OPEN_CURLY;
                if (VAR_X == tok.var.idx)
                    var_flags |= TOK_DOLLAR;
                allow_toktype = TOK_RFN | (var_flags | (assigning ? TOK_ASSIGN | TOK_ASSIGN_TT : 0));
                if (TOK_VAR == tok.toktype)
                    allow_toktype |= TOK_VFN_DOT;
                if (tok.var.idx != VAR_Y || out_assigned > 1)
                    allow_toktype |= JOIN_TOKENS;
                muted = 0;
                break;
            }
            case TOK_FN: {
                expr_tok_t newtok;
                tok.gen.datatype = fn_tbl[tok.fn.idx].fn_int ? MPR_INT32 : MPR_FLT;
                tok.fn.arity = fn_tbl[tok.fn.idx].arity;
                if (fn_tbl[tok.fn.idx].memory) {
                    /* add assignment token */
                    char varname[7];
                    uint8_t varidx = num_var;
                    {FAIL_IF(num_var >= N_USER_VARS, "Maximum number of variables exceeded.");}
                    do {
                        snprintf(varname, 7, "var%d", varidx++);
                    } while (find_var_by_name(vars, num_var, varname, 7) >= 0);
                    /* need to store new variable */
                    expr_var_set(&vars[num_var], varname, 0, type_hi, 1, VAR_ASSIGNED);

                    newtok.toktype = TOK_ASSIGN_USE;
                    newtok.var.idx = num_var++;
                    newtok.gen.datatype = type_hi;
                    newtok.gen.casttype = 0;
                    newtok.gen.vec_len = 1;
                    newtok.gen.flags = 0;
                    newtok.var.vec_idx = 0;
                    newtok.var.offset = 0;
                    is_const = 0;
                    PUSH_TO_OPERATOR(newtok);
                }
                PUSH_TO_OPERATOR(tok);
                if (fn_tbl[tok.fn.idx].arity)
                    allow_toktype = TOK_OPEN_PAREN;
                else {
                    POP_OPERATOR_TO_OUTPUT();
                    allow_toktype = JOIN_TOKENS;
                }
                if (tok.fn.idx >= FN_DEL_IDX)
                    is_const = 0;
                if (fn_tbl[tok.fn.idx].memory) {
                    newtok.toktype = TOK_VAR;
                    newtok.gen.flags = 0;
                    PUSH_TO_OUTPUT(newtok);
                }
                break;
            }
            case TOK_VFN:
                tok.toktype = TOK_VFN;
                tok.gen.datatype = vfn_tbl[tok.fn.idx].fn_int ? MPR_INT32 : MPR_FLT;
                tok.fn.arity = vfn_tbl[tok.fn.idx].arity;
                if (VFN_ANGLE == tok.fn.idx) {
                    tok.gen.vec_len = 2;
                    tok.gen.flags |= VEC_LEN_LOCKED;
                }
                else
                    tok.gen.vec_len = 1;
                PUSH_TO_OPERATOR(tok);
                allow_toktype = TOK_OPEN_PAREN;
                break;
            case TOK_VFN_DOT:
                if (op[op_idx].toktype != TOK_RFN || op[op_idx].fn.idx < RFN_HISTORY) {
                    tok.toktype = TOK_VFN;
                    tok.gen.datatype = vfn_tbl[tok.fn.idx].fn_int ? MPR_INT32 : MPR_FLT;
                    tok.fn.arity = vfn_tbl[tok.fn.idx].arity;
                    tok.gen.vec_len = 1;
                    PUSH_TO_OPERATOR(tok);
                    if (tok.fn.arity > 1) {
                        tok.toktype = TOK_OPEN_PAREN;
                        tok.fn.arity = 2;
                        PUSH_TO_OPERATOR(tok);
                        allow_toktype = OBJECT_TOKENS;
                    }
                    else {
                        POP_OPERATOR_TO_OUTPUT();
                        allow_toktype = JOIN_TOKENS | TOK_RFN;
                    }
                    break;
                }
                /* omit break and continue to case TOK_RFN */
            case TOK_RFN: {
                int pre, sslen;
                expr_rfn_t rfn;
                expr_tok_t newtok;
                uint8_t rt, idx;
                if (tok.fn.idx >= RFN_HISTORY) {
                    rt = _reduce_type_from_fn_idx(tok.fn.idx);
                    /* fail if input is another reduce function */
                    {FAIL_IF(TOK_LOOP_END == out[out_idx].toktype,
                             "Reduce functions may be nested but not chained.");}
                    /* fail if same-type reduction already on the operator stack (nested) */
                    for (i = op_idx; i >= 0; i--) {
                        FAIL_IF(TOK_REDUCING == op[i].toktype && rt & op[op_idx].con.flags,
                                "Syntax error: nested reduce functions of the same type.");
                    }
                    tok.fn.arity = rfn_tbl[tok.fn.idx].arity;
                    tok.gen.datatype = MPR_INT32;
                    PUSH_TO_OPERATOR(tok);
                    allow_toktype = TOK_RFN | TOK_VFN_DOT;
                    /* get compound arity of last token */
                    sslen = substack_len(out, out_idx);
                    switch (rt) {
                        case RT_HISTORY: {
                            int y_ref = 0, x_ref = 0, lit_val, len = sslen;
                            /* History requires an integer argument */
                            /* TODO: allow variable + maximum instead e.g. history(n, 10) */
                            /* TODO: allow range instead e.g. history(-5:-2) */
                            GET_NEXT_TOKEN(tok);
                            {FAIL_IF(tok.toktype != TOK_OPEN_PAREN, "missing open parenthesis. (1)");}
                            GET_NEXT_TOKEN(tok);
                            {FAIL_IF(tok.toktype != TOK_LITERAL || tok.lit.datatype != MPR_INT32,
                                     "'history' must be followed by integer argument.");}
                            lit_val = abs(tok.lit.val.i);

                            for (i = 0; i < len; i++) {
                                int idx = out_idx - i;
                                assert(idx >= 0);
                                tok = out[idx];
                                while (TOK_COPY_FROM == tok.toktype) {
                                    idx -= tok.con.cache_offset + 1;
                                    assert(idx > 0 && idx <= out_idx);
                                    tok = out[idx];
                                }
                                len += expr_tok_get_arity(&tok);
                                if (tok.toktype != TOK_VAR)
                                    continue;
                                {FAIL_IF(tok.gen.flags & VAR_HIST_IDX,
                                         "History indexes not allowed within history reduce function.");}
                                if (VAR_Y == tok.var.idx) {
                                    y_ref = 1;
                                    break;
                                }
                                else if (tok.var.idx >= VAR_X_NEWEST)
                                    x_ref = 1;
                            }
                            /* TODO: reduce prefix could include BOTH x and y */
                            if (y_ref && x_ref)
                                {FAIL("mixed history reduce is ambiguous.");}
                            else if (y_ref) {
                                op[op_idx].con.reduce_start = lit_val;
                                op[op_idx].con.reduce_stop = 1;
                            }
                            else if (x_ref) {
                                op[op_idx].con.reduce_start = lit_val - 1;
                                op[op_idx].con.reduce_stop = 0;
                            }
                            else
                                {FAIL("history reduce requires reference to 'x' or 'y'.");}

                            for (i = 0; i < sslen; i++) {
                                tok = out[out_idx - i];
                                if (tok.toktype != TOK_VAR)
                                    continue;
                                mpr_expr_update_mlen(expr, tok.var.idx, op[op_idx].con.reduce_start);
                            }
                            GET_NEXT_TOKEN(tok);
                            {FAIL_IF(tok.toktype != TOK_CLOSE_PAREN, "missing close parenthesis. (1)");}
                            break;
                        }
                        case RT_INSTANCE: {
                            /* TODO: fail if var has instance index (once they are implemented) */
                            int v_ref = 0, len = sslen;
                            for (i = 0; i < len; i++) {
                                int idx = out_idx - i;
                                assert(idx >= 0);
                                tok = out[idx];
                                while (TOK_COPY_FROM == tok.toktype) {
                                    idx -= tok.con.cache_offset + 1;
                                    assert(idx > 0 && idx <= out_idx);
                                    tok = out[idx];
                                }
                                len += expr_tok_get_arity(&tok);
                                if (tok.toktype != TOK_VAR && tok.toktype != TOK_TT)
                                    continue;
                                if (tok.var.idx >= VAR_Y) {
                                    v_ref = 1;
                                    break;
                                }
                            }
                            {FAIL_IF(!v_ref, "instance reduce requires reference to 'x' or 'y'.");}
                            break;
                        }
                        case RT_SIGNAL: {
                            /* Fail if variables in substack have signal idx other than zero */
                            uint8_t x_ref = 0;
                            for (i = 0; i < sslen; i++) {
                                tok = out[out_idx - i];
                                if (tok.toktype != TOK_VAR || tok.var.idx < VAR_Y)
                                    continue;
                                {FAIL_IF(tok.var.idx == VAR_Y,
                                         "Cannot call signal reduce function on output.");}
                                {FAIL_IF(tok.var.idx > VAR_X,
                                         "Signal indexes not allowed within signal reduce function.");}
                                if (VAR_X == tok.var.idx) {
                                    x_ref = 1;
                                    /* promote vector length */
                                    out[out_idx - i].var.vec_len = max_vec_len;
                                }
                            }
                            {FAIL_IF(!x_ref, "signal reduce requires reference to input 'x'.");}
                            if (type_hi == type_lo) /* homogeneous types, no casting necessary */
                                break;
                            out[out_idx].var.datatype = type_hi;
                            for (i = sslen - 1; i >= 0; i--) {
                                tok = out[out_idx - i];
                                if (tok.toktype != TOK_VAR || tok.var.idx < VAR_Y)
                                    continue;
                                /* promote datatype and casttype */
                                out[out_idx - i].var.datatype = type_lo;
                                out[out_idx - i].var.casttype = type_hi;
                                {FAIL_IF(check_type(buff, out, out_idx, vars, 1) < 0,
                                         "Malformed expression (3).");}
                            }
                            out_idx = check_type(buff, out, out_idx, vars, 0);
                            break;
                        }
                        case RT_VECTOR: {
                            uint8_t vec_len = 0;
                            /* Fail if variables in substack have vector idx other than zero */
                            /* TODO: use start variable or expr instead */
                            for (i = 0; i < sslen; i++) {
                                tok = out[out_idx - i];
                                if (tok.toktype != TOK_VAR && tok.toktype != TOK_COPY_FROM)
                                    continue;
                                {FAIL_IF(TOK_VAR == tok.toktype && tok.var.vec_idx
                                         && tok.var.vec_len == 1,
                                         "Vector indexes not allowed within vector reduce function.");}
                                if (out[out_idx - i].gen.vec_len > vec_len)
                                    vec_len = out[out_idx - i].gen.vec_len;
                                /* Set token vec_len to 1 since we will be iterating over elements */
                                out[out_idx - i].gen.vec_len = 1;
                                out[out_idx - i].gen.flags |= VEC_LEN_LOCKED;
                            }
                            op[op_idx].con.reduce_start = 0;
                            op[op_idx].con.reduce_stop = vec_len;
                            /* check if we are currently reducing over signals */
                            for (i = op_idx; i >= 0; i--) {
                                if (TOK_REDUCING == op[i].toktype && RT_SIGNAL & op[i].con.flags) {
                                    op[op_idx].con.flags |= USE_VAR_LEN;
                                    break;
                                }
                            }
                            break;
                        }
                        default:
                            {FAIL("unhandled reduce function identifier.");}
                    }
                    break;
                }
                assert(op_idx >= 0);
                memcpy(&newtok, &op[op_idx], TOKEN_SIZE);
                rt = _reduce_type_from_fn_idx(op[op_idx].fn.idx);
                /* fail unless reduction already on the stack */
                {FAIL_IF(RT_UNKNOWN == rt, "Syntax error: missing reduce function prefix.");}
                newtok.con.flags |= rt;
                POP_OPERATOR();
                /* TODO: check if there is possible conflict here between vfn and rfn */
                rfn = tok.fn.idx;
                if (RFN_COUNT == rfn) {
                    int idx = out_idx;
                    {FAIL_IF(rt != RT_INSTANCE, "count() requires 'instance' prefix");}
                    while (TOK_COPY_FROM == out[idx].toktype) {
                        idx -= out[idx].con.cache_offset + 1;
                        assert(idx > 0 && idx <= out_idx);
                    }
                    if (TOK_VAR == out[idx].toktype) {
                        /* Special case: count() can be represented by single token */
                        if (out_idx != idx)
                            memcpy(&out[out_idx], &out[idx], TOKEN_SIZE);
                        out[out_idx].toktype = TOK_VAR_NUM_INST;
                        out[out_idx].gen.datatype = MPR_INT32;
                        allow_toktype = JOIN_TOKENS;
                        break;
                    }
                }
                else if (RFN_NEWEST == rfn) {
                    {FAIL_IF(rt != RT_SIGNAL, "newest() requires 'signal' prefix'");}
                    out[out_idx].toktype = TOK_VAR;
                    out[out_idx].var.idx = VAR_X_NEWEST;
                    out[out_idx].gen.datatype = type_lo;
                    out[out_idx].gen.casttype = type_hi;
                    out[out_idx].gen.vec_len = max_vec_len;
                    out[out_idx].gen.flags |= (TYPE_LOCKED | VEC_LEN_LOCKED);
                    is_const = 0;
                    allow_toktype = JOIN_TOKENS;
                    break;
                }

                /* get compound arity of last token */
                sslen = substack_len(out, out_idx);
                switch (rfn) {
                    case RFN_MEAN: case RFN_CENTER: case RFN_SIZE:  pre = 3; break;
                    default:                                        pre = 2; break;
                }

                {FAIL_IF(out_idx + pre > STACK_SIZE, "Stack size exceeded. (3)");}

                /* find source token(s) for reduce input */
                idx = out_idx;
                while (TOK_COPY_FROM == out[idx].toktype) {
                    /* TODO: rename 'cache_offset' variable or switch struct */
                    idx -= out[idx].con.cache_offset + 1;
                    assert(idx > 0 && idx <= out_idx);
                }

                if (RFN_REDUCE == rfn) {
                    reduce_types |= newtok.con.flags & REDUCE_TYPE_MASK;
                    newtok.toktype = TOK_REDUCING;
                    PUSH_TO_OPERATOR(newtok);
                    tok.toktype = TOK_OPEN_PAREN;
                    tok.fn.arity = 0;
                    PUSH_TO_OPERATOR(tok);
                }

                if (TOK_COPY_FROM == out[out_idx].toktype && TOK_VAR != out[idx].toktype) {
                    /* make a new copy of this substack */
                    sslen = substack_len(out, idx);
                    {FAIL_IF(out_idx + sslen + pre > STACK_SIZE, "Stack size exceeded. (3)");}

                    /* Copy destacked reduce input substack */
                    for (i = 0; i < sslen; i++)
                        PUSH_TO_OPERATOR(out[idx - i]);
                    /* discard copy token at out[out_idx] */
                    POP_OUTPUT();
                }
                else {
                    int ar = RFN_CENTER == rfn || RFN_MEAN == rfn || RFN_SIZE == rfn ? 2 : 1;
                    if (RT_INSTANCE == rt) {
                        /* instance loops cache the starting instance idx */
                        ++ar;
                    }

                    /* Destack reduce input substack */
                    for (i = 0; i < sslen; i++) {
                        if (TOK_COPY_FROM == out[out_idx].toktype)
                            out[out_idx].con.cache_offset += ar;
                        POP_OUTPUT_TO_OPERATOR();
                    }
                }

                /* all instance reduce functions require this token */
                memcpy(&tok, &newtok, TOKEN_SIZE);
                tok.toktype = TOK_LOOP_START;
                if (RFN_REDUCE == rfn)
                    {PUSH_TO_OPERATOR(tok);}
                else
                    {PUSH_TO_OUTPUT(tok);}

                if (RFN_REDUCE == rfn) {
                    temp_var_cache var_cache;
                    char *temp, *in_name, *accum_name;
                    int len;
                    {FAIL_IF(num_var >= N_USER_VARS, "Maximum number of variables exceeded.");}
                    GET_NEXT_TOKEN(tok);
                    {FAIL_IF(tok.toktype != TOK_OPEN_PAREN, "missing open parenthesis. (3)");}
                    GET_NEXT_TOKEN(tok);
                    {FAIL_IF(tok.toktype != TOK_VAR, "'reduce()' requires variable arguments.");}

                    /* cache variable arg used for representing input */
                    temp = (char*)_get_var_str_and_len(str, lex_idx - 1, &len);
                    in_name = malloc(len + 1);
                    snprintf(in_name, len + 1, "%s", temp);
#if TRACE_PARSE
                    printf("using name '%s' for reduce input\n", in_name);
#endif

                    GET_NEXT_TOKEN(tok);
                    if (tok.toktype != TOK_COMMA) {
                        free(in_name);
                        {FAIL("missing comma.");}
                    }
                    GET_NEXT_TOKEN(tok);
                    if (tok.toktype != TOK_VAR) {
                        free(in_name);
                        {FAIL("'reduce()' requires variable arguments.");}
                    }

                    /* cache variable arg used for representing accumulator */
                    temp = (char*)_get_var_str_and_len(str, lex_idx - 1, &len);
                    accum_name = malloc(len + 1);
                    snprintf(accum_name, len + 1, "%s", temp);
#if TRACE_PARSE
                    printf("using name '%s' for reduce accumulator\n", accum_name);
#endif

                    /* temporarily store variable names so we can look them up later */
                    var_cache = calloc(1, sizeof(temp_var_cache_t));
                    if (temp_vars)
                        var_cache->next = temp_vars;
                    temp_vars = var_cache;
                    var_cache->in_name = in_name;
                    var_cache->accum_name = accum_name;
                    var_cache->scope_start = lex_idx;
                    var_cache->loop_start_pos = 0;

                    GET_NEXT_TOKEN(tok);
                    if (TOK_ASSIGN == tok.toktype) {
                        /* expression contains accumulator variable initialization */
                        lambda_allowed = 1;
                    }
                    else if (TOK_LAMBDA == tok.toktype) {
                        /* default to zero for accumulator initialization */
                        expr_tok_set_int32(&tok, 0);
                        tok.lit.vec_len = 1;
                        PUSH_TO_OUTPUT(tok);

                        /* Restack reduce input */
                        ++sslen;
                        for (i = 0; i < sslen; i++) {
                            PUSH_TO_OUTPUT(op[op_idx]);
                            POP_OPERATOR();
                            if (TOK_LOOP_START == out[out_idx].toktype)
                                var_cache->loop_start_pos = out_idx;
                        }
                        {FAIL_IF(op_idx < 0, "Malformed expression (4).");}
                    }
                    else {
                        free(in_name);
                        free(accum_name);
                        {FAIL("'reduce()' missing lambda operator '->'.");}
                    }
                    allow_toktype = OBJECT_TOKENS;
                    break;
                }
                else if (RFN_CONCAT == rfn) {
                    expr_tok_t newtok;
                    GET_NEXT_TOKEN(newtok);
                    {FAIL_IF(TOK_LITERAL != newtok.toktype || MPR_INT32 != newtok.gen.datatype,
                             "concat() requires an integer argument");}
                    {FAIL_IF(newtok.lit.val.i <= 1 || newtok.lit.val.i > 64,
                             "concat() max size must be between 2 and 64.");}

                    mpr_expr_update_vlen(expr, newtok.lit.val.i);
                    tok.gen.vec_len = 0;

                    if (op[op_idx].gen.casttype)
                        tok.gen.datatype = op[op_idx].gen.casttype;
                    else
                        tok.gen.datatype = op[op_idx].gen.datatype;

                    for (i = 0; i < sslen; i++) {
                        if (TOK_VAR == op[op_idx - i].toktype)
                            op[op_idx - i].gen.vec_len = 0;
                    }

                    /* Push token for building vector */
                    PUSH_INT_TO_OUTPUT(0);
                    out[out_idx].lit.vec_len = 0;
                    out[out_idx].gen.flags |= VEC_LEN_LOCKED;

                    /* Push token for maximum vector length */
                    PUSH_INT_TO_OUTPUT(newtok.lit.val.i);

                    GET_NEXT_TOKEN(newtok);
                    {FAIL_IF(TOK_CLOSE_PAREN != newtok.toktype, "missing right parenthesis.");}
                    tok.gen.flags |= VEC_LEN_LOCKED;
                }

                switch (rfn) {
                    case RFN_CENTER:
                    case RFN_MAX:
                    case RFN_SIZE:
                        /* some reduce functions need init with the value from first iteration */
                        tok.toktype = TOK_LITERAL;
                        tok.gen.flags = CONST_MINVAL;
                        PUSH_TO_OUTPUT(tok);
                        if (RFN_MAX == rfn)
                            break;
                        tok.toktype = TOK_LITERAL;
                        tok.gen.flags = CONST_MAXVAL;
                        PUSH_TO_OUTPUT(tok);
                        break;
                    case RFN_MIN:
                        tok.toktype = TOK_LITERAL;
                        tok.gen.flags = CONST_MAXVAL;
                        PUSH_TO_OUTPUT(tok);
                        break;
                    case RFN_ALL:
                    case RFN_ANY:
                    case RFN_COUNT:
                    case RFN_MEAN:
                    case RFN_SUM:
                        PUSH_INT_TO_OUTPUT(RFN_ALL == rfn);
                        if (RFN_COUNT == rfn || RFN_MEAN == rfn)
                            PUSH_INT_TO_OUTPUT(RFN_COUNT == rfn);
                        break;
                    default:
                        break;
                }

                /* Restack reduce input */
                for (i = 0; i < sslen; i++) {
                    PUSH_TO_OUTPUT(op[op_idx]);
                    POP_OPERATOR();
                }
                {FAIL_IF(op_idx < 0, "Malformed expression (5).");}

                if (TOK_COPY_FROM == out[out_idx].toktype) {
                    /* TODO: simplified reduce functions do not need separate cache for input */
                }

                if (OP_UNKNOWN != rfn_tbl[rfn].op) {
                    expr_tok_set_op(&tok, rfn_tbl[rfn].op);
                    /* don't use macro here since we don't want to optimize away initialization args */
                    PUSH_TO_OUTPUT(tok);
                    out_idx = check_type(buff, out, out_idx, vars, 0);
                    {FAIL_IF(out_idx < 0, "Malformed expression (6).");}
                }
                if (VFN_UNKNOWN != rfn_tbl[rfn].vfn) {
                    tok.toktype = TOK_VFN;
                    tok.fn.idx = rfn_tbl[rfn].vfn;
                    if (VFN_MAX == tok.fn.idx || VFN_MIN == tok.fn.idx) {
                        /* we don't want vector reduce version here */
                        tok.toktype = TOK_FN;
                        tok.fn.idx = (VFN_MAX == tok.fn.idx) ? FN_MAX : FN_MIN;
                        tok.fn.arity = fn_tbl[tok.fn.idx].arity;
                    }
                    else
                        tok.fn.arity = vfn_tbl[tok.fn.idx].arity;
                    PUSH_TO_OPERATOR(tok);
                    POP_OPERATOR_TO_OUTPUT();
                }
                /* copy type from last token */
                newtok.gen.datatype = out[out_idx].gen.datatype;

                if (RFN_CENTER == rfn || RFN_MEAN == rfn || RFN_SIZE == rfn || RFN_CONCAT == rfn) {
                    tok.toktype = TOK_SP_ADD;
                    tok.lit.val.i = 1;
                    PUSH_TO_OUTPUT(tok);
                }

                /* all instance reduce functions require these tokens */
                memcpy(&tok, &newtok, TOKEN_SIZE);
                tok.toktype = TOK_LOOP_END;
                if (RFN_CENTER == rfn || RFN_MEAN == rfn || RFN_SIZE == rfn || RFN_CONCAT == rfn) {
                    tok.con.branch_offset = 2 + sslen;
                    tok.con.cache_offset = 2;
                }
                else {
                    tok.con.branch_offset = 1 + sslen;
                    tok.con.cache_offset = 1;
                }
                PUSH_TO_OUTPUT(tok);

                if (RFN_CENTER == rfn) {
                    expr_tok_set_op(&tok, OP_ADD);
                    PUSH_TO_OPERATOR(tok);

                    POP_OPERATOR_TO_OUTPUT();
                    expr_tok_set_flt(&tok, 0.5f);
                    tok.gen.flags &= ~CONST_SPECIAL;
                    PUSH_TO_OUTPUT(tok);

                    expr_tok_set_op(&tok, OP_MULTIPLY);
                    PUSH_TO_OPERATOR(tok);
                    POP_OPERATOR_TO_OUTPUT();
                }
                else if (RFN_MEAN == rfn) {
                    expr_tok_set_op(&tok, OP_DIVIDE);
                    PUSH_TO_OPERATOR(tok);
                    POP_OPERATOR_TO_OUTPUT();
                }
                else if (RFN_SIZE == rfn) {
                    expr_tok_set_op(&tok, OP_SUBTRACT);
                    PUSH_TO_OPERATOR(tok);
                    POP_OPERATOR_TO_OUTPUT();
                }
                else if (RFN_CONCAT == rfn) {
                    tok.toktype = TOK_SP_ADD;
                    tok.lit.val.i = -1;
                    PUSH_TO_OUTPUT(tok);
                }
                allow_toktype = JOIN_TOKENS;
                if (RFN_CONCAT == rfn) {
                    /* Allow chaining another dot function after concat() */
                    allow_toktype |= TOK_VFN_DOT;
                }
                break;
            }
            case TOK_LAMBDA:
                /* Pop from operator stack to output until left parenthesis found. This should
                 * finish stacking the accumulator initialization tokens and re-stack the reduce
                 * function input stack */
                while (op_idx >= 0 && op[op_idx].toktype != TOK_OPEN_PAREN) {
                    POP_OPERATOR_TO_OUTPUT();
                    if (TOK_LOOP_START == out[out_idx].toktype)
                        temp_vars->loop_start_pos = out_idx;
                }
                {FAIL_IF(op_idx < 0, "Unmatched parentheses. (1)");}
                /* Don't pop the left parenthesis yet */

                lambda_allowed = 0;
                allow_toktype = OBJECT_TOKENS;
                break;
            case TOK_OPEN_PAREN:
                if (TOK_FN == op[op_idx].toktype && fn_tbl[op[op_idx].fn.idx].memory)
                    tok.fn.arity = 2;
                else
                    tok.fn.arity = 1;
                tok.fn.idx = (   TOK_FN == op[op_idx].toktype
                              || TOK_VFN == op[op_idx].toktype) ? op[op_idx].fn.idx : FN_UNKNOWN;
                PUSH_TO_OPERATOR(tok);
                allow_toktype = OBJECT_TOKENS;
                break;
            case TOK_CLOSE_CURLY:
            case TOK_CLOSE_PAREN:
            case TOK_CLOSE_SQUARE: {
                int arity;
                /* pop from operator stack to output until left parenthesis found */
                while (op_idx >= 0 && op[op_idx].toktype != TOK_OPEN_PAREN
                       && op[op_idx].toktype != TOK_VECTORIZE)
                    POP_OPERATOR_TO_OUTPUT();
                {FAIL_IF(op_idx < 0, "Unmatched parentheses, brackets, or misplaced comma. (1)");}

                if (TOK_VECTORIZE == op[op_idx].toktype) {
                    op[op_idx].gen.flags |= VEC_LEN_LOCKED;
                    ADD_TO_VECTOR();
                    lock_vec_len(out, out_idx);
                    if (op[op_idx].fn.arity > 1)
                        { POP_OPERATOR_TO_OUTPUT(); }
                    else {
                        /* we do not need vectorizer token if vector length == 1 */
                        POP_OPERATOR();
                    }
                    vectorizing = 0;
                    allow_toktype = (TOK_OP | TOK_CLOSE_PAREN | TOK_CLOSE_CURLY | TOK_COMMA
                                     | TOK_COLON | TOK_SEMICOLON | TOK_VFN_DOT);
                    if (assigning)
                        allow_toktype |= (TOK_ASSIGN | TOK_ASSIGN_TT);
                    break;
                }

                arity = op[op_idx].fn.arity;
                /* remove left parenthesis from operator stack */
                POP_OPERATOR();

                allow_toktype = JOIN_TOKENS | TOK_VFN_DOT | TOK_RFN;
                if (assigning)
                    allow_toktype |= (TOK_ASSIGN | TOK_ASSIGN_TT);

                if (op_idx < 0)
                    break;

                /* if operator stack[sp] is tok_fn or tok_vfn, pop to output */
                if (op[op_idx].toktype == TOK_FN) {
                    if (FN_SIG_IDX == op[op_idx].fn.idx) {
                        {FAIL_IF(TOK_VAR != out[out_idx].toktype || VAR_X != out[out_idx].var.idx,
                                 "Signal index used on incompatible token.");}
                        if (TOK_LITERAL == out[out_idx-1].toktype) {
                            /* Optimize by storing signal idx in variable token */
                            int sig_idx;
                            {FAIL_IF(MPR_INT32 != out[out_idx-1].gen.datatype,
                                     "Signal index must be an integer.");}
                            sig_idx = out[out_idx-1].lit.val.i % num_src;
                            if (sig_idx < 0)
                                sig_idx += num_src;
                            out[out_idx].var.idx = sig_idx + VAR_X;
                            out[out_idx].gen.flags &= ~VAR_SIG_IDX;
                            memcpy(out + out_idx - 1, out + out_idx, TOKEN_SIZE);
                            POP_OUTPUT();
                        }
                        else if (type_hi != type_lo) {
                            /* heterogeneous types, need to cast */
                            out[out_idx].var.datatype = type_lo;
                            out[out_idx].var.casttype = type_hi;
                        }
                        POP_OPERATOR();

                        /* signal indices must be integers */
                        if (out[out_idx-1].gen.datatype != MPR_INT32)
                            out[out_idx-1].gen.casttype = MPR_INT32;

                        /* signal index set */
                        /* recreate var_flags from variable token */
                        tok = out[out_idx];
                        var_flags = tok.gen.flags & VAR_IDXS;
                        if (!(tok.gen.flags & VAR_VEC_IDX) && !tok.var.vec_idx)
                            var_flags |= TOK_OPEN_SQUARE;
                        if (!(tok.gen.flags & VAR_HIST_IDX))
                            var_flags |= TOK_OPEN_CURLY;
                        allow_toktype |= (var_flags & ~VAR_IDXS);
                    }
                    else if (FN_DEL_IDX == op[op_idx].fn.idx) {
                        int buffer_size = 0;
                        switch (arity) {
                            case 2:
                                /* max delay should be at the top of the output stack */
                                {FAIL_IF(out[out_idx].toktype != TOK_LITERAL,
                                         "non-constant max history.");}
                                switch (out[out_idx].gen.datatype) {
#define TYPED_CASE(MTYPE, T)                                                        \
                                    case MTYPE:                                     \
                                        buffer_size = (int)out[out_idx].lit.val.T;  \
                                        break;
                                    TYPED_CASE(MPR_INT32, i)
                                    TYPED_CASE(MPR_FLT, f)
                                    TYPED_CASE(MPR_DBL, d)
#undef TYPED_CASE
                                    default:
                                        break;
                                }
                                {FAIL_IF(buffer_size < 0, "negative history buffer size detected.");}
                                POP_OUTPUT();
                                buffer_size = buffer_size * -1;
                            case 1:
                                /* variable should be at the top of the output stack */
                                {FAIL_IF(out[out_idx].toktype != TOK_VAR && out[out_idx].toktype != TOK_TT,
                                         "delay on non-variable token.");}
                                i = out_idx - 1;
                                if (!buffer_size) {
                                    {FAIL_IF(out[i].toktype != TOK_LITERAL,
                                             "variable history indices must include maximum value.");}
                                    switch (out[i].gen.datatype) {
#define TYPED_CASE(MTYPE, T)                                                            \
                                        case MTYPE:                                     \
                                            buffer_size = -(int)ceil(out[i].lit.val.T); \
                                            break;
                                        TYPED_CASE(MPR_INT32, i)
                                        TYPED_CASE(MPR_FLT, f)
                                        TYPED_CASE(MPR_DBL, d)
#undef TYPED_CASE
                                        default:
                                            break;
                                    }
                                    {FAIL_IF(buffer_size < 0 || buffer_size > MAX_HIST_SIZE,
                                             "Illegal history index.");}
                                }
                                if (!buffer_size) {
#if TRACE_PARSE
                                    printf("Removing zero delay\n");
#endif
                                    /* remove zero delay */
                                    memcpy(&out[i], &out[i + 1], TOKEN_SIZE * (out_idx - i));
                                    POP_OUTPUT();
                                    POP_OPERATOR();
                                    break;
                                }
                                mpr_expr_update_mlen(expr, out[out_idx].var.idx, buffer_size);
                                /* TODO: disable non-const assignment to past values of output */
                                out[out_idx].gen.flags |= VAR_HIST_IDX;
                                if (assigning)
                                    out[i].gen.flags |= (TYPE_LOCKED | VEC_LEN_LOCKED);
                                POP_OPERATOR();
                                break;
                            default:
                                {FAIL("Illegal arity for variable delay.");}
                        }
                        /* recreate var_flags from variable token */
                        tok = out[out_idx];
                        var_flags = tok.gen.flags & VAR_IDXS;
                        if (!(tok.gen.flags & VAR_SIG_IDX) && VAR_X == tok.var.idx)
                            var_flags |= TOK_DOLLAR;
                        if (!(tok.gen.flags & VAR_VEC_IDX) && !tok.var.vec_idx)
                            var_flags |= TOK_OPEN_SQUARE;

                        allow_toktype |= (var_flags & ~VAR_IDXS);
                    }
                    else if (FN_VEC_IDX == op[op_idx].fn.idx) {
                        {FAIL_IF(arity != 1, "vector index arity != 1.");}
                        {FAIL_IF(out[out_idx].toktype != TOK_VAR,
                                 "Missing variable for vector indexing");}
                        tok = out[out_idx];
                        out[out_idx].gen.flags |= VAR_VEC_IDX;
                        POP_OPERATOR();
                        if (TOK_LITERAL == out[out_idx-1].toktype) {
                            if (   TOK_VAR == tok.gen.toktype
                                && (tok.var.idx >= VAR_Y || vars[tok.var.idx].vec_len)
                                && MPR_INT32 == out[out_idx-1].gen.datatype) {
                                /* Optimize by storing vector idx in variable token */
                                int vec_len, vec_idx;
                                if (VAR_Y == tok.var.idx)
                                    vec_len = dst_lens[0];
                                else if (VAR_X <= tok.var.idx)
                                    vec_len = src_lens[tok.var.idx - VAR_X];
                                else
                                    vec_len = vars[tok.var.idx].vec_len;
                                vec_idx = out[out_idx-1].lit.val.i % vec_len;
                                if (vec_idx < 0)
                                    vec_idx += vec_len;
                                out[out_idx].var.vec_idx = vec_idx;
                                out[out_idx].gen.flags &= ~VAR_VEC_IDX;
                                memcpy(out + out_idx - 1, out + out_idx, TOKEN_SIZE);
                                POP_OUTPUT();
                            }
                        }
                        /* also set var vec_len to 1 */
                        /* TODO: consider vector indices */
                        out[out_idx].var.vec_len = 1;

                        /* vector index set */
                        /* recreate var_flags from variable token */
                        tok = out[out_idx];
                        var_flags = tok.gen.flags & VAR_IDXS;
                        if (!(tok.gen.flags & VAR_SIG_IDX) && VAR_X == tok.var.idx)
                            var_flags |= TOK_DOLLAR;
                        if (!(tok.gen.flags & VAR_HIST_IDX))
                            var_flags |= TOK_OPEN_CURLY;

                        allow_toktype |= (var_flags & ~VAR_IDXS);
                        break;
                    }
                    else {
                        if (arity != fn_tbl[op[op_idx].fn.idx].arity) {
                            /* check for overloaded functions */
                            if (arity != 1)
                                {FAIL("Function arity mismatch.");}
                            if (op[op_idx].fn.idx == FN_MIN) {
                                op[op_idx].toktype = TOK_VFN;
                                op[op_idx].fn.idx = VFN_MIN;
                            }
                            else if (op[op_idx].fn.idx == FN_MAX) {
                                op[op_idx].toktype = TOK_VFN;
                                op[op_idx].fn.idx = VFN_MAX;
                            }
                            else
                                {FAIL("Function arity mismatch.");}
                        }
                        POP_OPERATOR_TO_OUTPUT();
                    }

                }
                else if (TOK_VFN == op[op_idx].toktype) {
                    /* check arity */
                    {FAIL_IF(arity != vfn_tbl[op[op_idx].fn.idx].arity, "VFN arity mismatch.");}
                    POP_OPERATOR_TO_OUTPUT();
                }
                else if (TOK_REDUCING == op[op_idx].toktype) {
                    int cache_pos;
                    /* remove the cached reduce variables */
                    temp_var_cache var_cache = temp_vars;
                    temp_vars = var_cache->next;

                    cache_pos = var_cache->loop_start_pos;
                    {FAIL_IF(out[cache_pos].toktype != TOK_LOOP_START, "Compilation error (2)");}

                    free((char*)var_cache->in_name);
                    free((char*)var_cache->accum_name);
                    free(var_cache);

                    /* push move token to output */
                    tok.toktype = TOK_MOVE;
                    if (out[cache_pos].con.flags & RT_INSTANCE)
                        tok.con.cache_offset = 3;
                    else
                        tok.con.cache_offset = 2;
                    if (out[out_idx].gen.casttype)
                        tok.con.datatype = out[out_idx].gen.casttype;
                    else
                        tok.con.datatype = out[out_idx].gen.datatype;
                    PUSH_TO_OUTPUT(tok);
                    /* push branch token to output */
                    tok.toktype = TOK_LOOP_END;
                    tok.con.flags |= op[op_idx].con.flags;
                    tok.con.branch_offset = out_idx - cache_pos;
                    tok.con.cache_offset = -1;
                    tok.con.reduce_start = op[op_idx].con.reduce_start;
                    tok.con.reduce_stop = op[op_idx].con.reduce_stop;
                    PUSH_TO_OUTPUT(tok);
                    reduce_types &= ~(tok.con.flags & REDUCE_TYPE_MASK);
                    POP_OPERATOR();
                }
                /* special case: if top of stack is tok_assign_use, pop to output */
                if (op_idx >= 0 && op[op_idx].toktype == TOK_ASSIGN_USE)
                    POP_OPERATOR_TO_OUTPUT();
                break;
            }
            case TOK_COMMA:
                /* pop from operator stack to output until left parenthesis or TOK_VECTORIZE found */
                while (op_idx >= 0 && op[op_idx].toktype != TOK_OPEN_PAREN
                       && op[op_idx].toktype != TOK_VECTORIZE) {
                    POP_OPERATOR_TO_OUTPUT();
                }
                {FAIL_IF(op_idx < 0, "Malformed expression (7).");}
                if (TOK_VECTORIZE == op[op_idx].toktype) {
                    ADD_TO_VECTOR();
                }
                else {
                    /* check if paren is attached to a function */
                    {FAIL_IF(FN_UNKNOWN == op[op_idx].fn.idx, "Misplaced comma.");}
                    ++op[op_idx].fn.arity;
                }
                allow_toktype = OBJECT_TOKENS;
                break;
            case TOK_COLON:
                /* pop from operator stack to output until conditional found */
                while (op_idx >= 0 && (op[op_idx].toktype != TOK_OP || op[op_idx].op.idx != OP_IF)
                       && (op[op_idx].toktype != TOK_FN || op[op_idx].fn.idx != FN_VEC_IDX)) {
                    POP_OPERATOR_TO_OUTPUT();
                }
                {FAIL_IF(op_idx < 0, "Unmatched colon.");}

                if (op[op_idx].toktype == TOK_FN) {
                    /* index is range A:B */

                    /* Pop TOK_FN from operator stack */
                    POP_OPERATOR();

                    /* Pop parenthesis from output stack, top should now be variable */
                    POP_OUTPUT();
                    {FAIL_IF(out[out_idx].toktype != TOK_VAR,
                             "Variable not found for colon indexing.");}

                    /* Push variable back to operator stack */
                    POP_OUTPUT_TO_OPERATOR();

                    /* Check if left index is an integer */
                    {FAIL_IF(out[out_idx].toktype != TOK_LITERAL || out[out_idx].gen.datatype != MPR_INT32,
                             "Non-integer left vector index used with colon.");}
                    op[op_idx].var.vec_idx = out[out_idx].lit.val.i;
                    POP_OUTPUT();
                    POP_OPERATOR_TO_OUTPUT();

                    /* Get right index and verify that it is an integer */
                    GET_NEXT_TOKEN(tok);
                    {FAIL_IF(tok.toktype != TOK_LITERAL || tok.gen.datatype != MPR_INT32,
                             "Non-integer right vector index used with colon.");}
                    out[out_idx].var.vec_len = tok.lit.val.i - out[out_idx].var.vec_idx + 1;
                    if (tok.lit.val.i < out[out_idx].var.vec_idx)
                        out[out_idx].var.vec_len += vec_len_ctx;
                    GET_NEXT_TOKEN(tok);
                    {FAIL_IF(tok.toktype != TOK_CLOSE_SQUARE, "Unmatched bracket.");}
                    /* vector index set */
                    var_flags &= ~VAR_VEC_IDX;
                    allow_toktype = (JOIN_TOKENS | TOK_VFN_DOT | TOK_RFN | (var_flags & ~VAR_IDXS));
                    if (assigning)
                        allow_toktype |= TOK_ASSIGN | TOK_ASSIGN_TT;
                    break;
                }
                op[op_idx].op.idx = OP_IF_THEN_ELSE;
                allow_toktype = OBJECT_TOKENS;
                break;
            case TOK_SEMICOLON: {
                int var_idx;
                /* finish popping operators to output, check for unbalanced parentheses */
                while (op_idx >= 0 && op[op_idx].toktype < TOK_ASSIGN) {
                    if (op[op_idx].toktype == TOK_OPEN_PAREN)
                        {FAIL("Unmatched parentheses or misplaced comma. (2)");}
                    POP_OPERATOR_TO_OUTPUT();
                }
                var_idx = op[op_idx].var.idx;
                if (var_idx < N_USER_VARS) {
                    if (!vars[var_idx].vec_len) {
                        int temp = out_idx, num_idx = NUM_VAR_IDXS(op[op_idx].gen.flags);
                        for (i = 0; i < num_idx && temp > 0; i++)
                            temp -= substack_len(out, temp);
                        vars[var_idx].vec_len = out[temp].gen.vec_len;
                        if (   !(vars[var_idx].flags & TYPE_LOCKED)
                            && vars[var_idx].datatype > out[temp].gen.datatype) {
                            vars[var_idx].datatype = out[temp].gen.datatype;
                        }
                    }
                    /* update and lock vector length of assigned variable */
                    if (!(op[op_idx].gen.flags & VEC_LEN_LOCKED))
                        op[op_idx].gen.vec_len = vars[var_idx].vec_len;
                    op[op_idx].gen.datatype = vars[var_idx].datatype;
                    op[op_idx].gen.flags |= VEC_LEN_LOCKED;
                    if (is_const)
                        vars[var_idx].flags &= ~VAR_INSTANCED;
                }
                /* pop assignment operators to output */
                while (op_idx >= 0) {
                    if (!op_idx && op[op_idx].toktype < TOK_ASSIGN)
                        {FAIL("Malformed expression (8)");}
                    PUSH_TO_OUTPUT(op[op_idx]);
                    if (out[out_idx].toktype == TOK_ASSIGN_USE
                        && check_assign_type_and_len(buff, out, out_idx, vars) == -1)
                        {FAIL("Malformed expression (9)");}
                    POP_OPERATOR();
                }
                /* mark last assignment token to clear eval stack */
                out[out_idx].gen.flags |= CLEAR_STACK;

                /* check vector length and type */
                if (check_assign_type_and_len(buff, out, out_idx, vars) == -1)
                    {FAIL("Malformed expression (10)");}

                /* start another sub-expression */
                assigning = is_const = 1;
                allow_toktype = TOK_VAR | TOK_TT;
                break;
            }
            case TOK_OP:
                /* check precedence of operators on stack */
                while (op_idx >= 0 && op[op_idx].toktype == TOK_OP
                       && (op_tbl[op[op_idx].op.idx].precedence >=
                           op_tbl[tok.op.idx].precedence)) {
                    POP_OPERATOR_TO_OUTPUT();
                }
                PUSH_TO_OPERATOR(tok);
                allow_toktype = OBJECT_TOKENS & ~TOK_OP;
                if (op_tbl[tok.op.idx].arity <= 1)
                    allow_toktype &= ~TOK_NEGATE;
                break;
            case TOK_DOLLAR:
                {FAIL_IF(TOK_VAR != out[out_idx].toktype, "Signal index on non-variable type.");}
                {FAIL_IF(VAR_X != out[out_idx].var.idx || out[out_idx].gen.flags & VAR_SIG_IDX,
                         "Signal index on non-input type or index already set.");}

                out[out_idx].gen.flags |= VAR_SIG_IDX;

                GET_NEXT_TOKEN(tok);
                {FAIL_IF(TOK_OPEN_PAREN != tok.toktype,
                         "Signal index token must be followed by an integer or use parentheses.");}

                /* push a FN_SIG_IDX to operator stack */
                tok.toktype = TOK_FN;
                tok.fn.idx = FN_SIG_IDX;
                tok.fn.arity = 1;
                PUSH_TO_OPERATOR(tok);

                /* also push an open parenthesis */
                tok.toktype = TOK_OPEN_PAREN;
                PUSH_TO_OPERATOR(tok);

                /* move variable from output to operator stack */
                POP_OUTPUT_TO_OPERATOR();

                /* sig_idx should come last on output stack (first on operator stack) so we
                 * don't need to move any other tokens */

                var_flags = (var_flags & ~TOK_DOLLAR) | VAR_SIG_IDX;
                allow_toktype = OBJECT_TOKENS;
                break;
            case TOK_OPEN_SQUARE:
                if (var_flags & TOK_OPEN_SQUARE) { /* vector index not set */
                    {FAIL_IF(TOK_VAR != out[out_idx].toktype,
                             "error: vector index on non-variable type. (1)");}

                    /* push a FN_VEC_IDX to operator stack */
                    tok.toktype = TOK_FN;
                    tok.fn.idx = FN_VEC_IDX;
                    tok.fn.arity = 1;
                    PUSH_TO_OPERATOR(tok);

                    /* also push an open parenthesis */
                    tok.toktype = TOK_OPEN_PAREN;
                    PUSH_TO_OPERATOR(tok);

                    /* move variable from output to operator stack */
                    POP_OUTPUT_TO_OPERATOR();

                    if (op[op_idx].gen.flags & VAR_SIG_IDX) {
                        /* Move sig_idx substack from output to operator */
                        for (i = substack_len(out, out_idx); i > 0; i--)
                            POP_OUTPUT_TO_OPERATOR();
                    }

                    if (op[op_idx].gen.flags & VAR_HIST_IDX) {
                        /* Move hist_idx substack from output to operator */
                        for (i = substack_len(out, out_idx); i > 0; i--)
                            POP_OUTPUT_TO_OPERATOR();
                    }

                    var_flags = (var_flags & ~TOK_OPEN_SQUARE) | VAR_VEC_IDX;
                    allow_toktype = OBJECT_TOKENS;
                    break;
                }
                else {
                    {FAIL_IF(vectorizing, "Nested (multidimensional) vectors not allowed.");}
                    tok.toktype = TOK_VECTORIZE;
                    tok.gen.vec_len = 0;
                    tok.fn.arity = 0;
                    PUSH_TO_OPERATOR(tok);
                    vectorizing = 1;
                    allow_toktype = OBJECT_TOKENS & ~TOK_OPEN_SQUARE;
                }
                break;
            case TOK_OPEN_CURLY: {
                uint8_t flags;
                {FAIL_IF(TOK_VAR != out[out_idx].toktype && TOK_TT != out[out_idx].toktype,
                         "error: history index on non-variable type.");}
                flags = out[out_idx].gen.flags;

                /* push a FN_DEL_IDX to operator stack */
                tok.toktype = TOK_FN;
                tok.fn.idx = FN_DEL_IDX;
                tok.fn.arity = 1;
                PUSH_TO_OPERATOR(tok);

                /* also push an open parenthesis */
                tok.toktype = TOK_OPEN_PAREN;
                PUSH_TO_OPERATOR(tok);

                /* move variable from output to operator stack */
                POP_OUTPUT_TO_OPERATOR();

                if (flags & VAR_SIG_IDX) {
                    /* Move sig_idx substack from output to operator */
                    for (i = substack_len(out, out_idx); i > 0; i--)
                        POP_OUTPUT_TO_OPERATOR();
                }

                var_flags = (var_flags & ~TOK_OPEN_CURLY) | VAR_HIST_IDX;
                allow_toktype = OBJECT_TOKENS;
                break;
            }
            case TOK_NEGATE:
                /* push '-1' to output stack, and '*' to operator stack */
                expr_tok_set_int32(&tok, -1);
                PUSH_TO_OUTPUT(tok);

                expr_tok_set_op(&tok, OP_MULTIPLY);
                PUSH_TO_OPERATOR(tok);

                allow_toktype = OBJECT_TOKENS & ~TOK_NEGATE;
                break;
            case TOK_ASSIGN:
                var_flags = 0;
                /* assignment to variable */
                {FAIL_IF(!assigning, "Misplaced assignment operator.");}
                {FAIL_IF(op_idx >= 0 || out_idx < 0, "Malformed expression left of assignment.");}

                if (out[out_idx].toktype == TOK_VAR) {
                    int var = out[out_idx].var.idx;
                    if (var >= VAR_X_NEWEST)
                        {FAIL("Cannot assign to input variable 'x'.");}
                    if (out[out_idx].gen.flags & VAR_HIST_IDX) {
                        /* unlike variable lookup, history assignment index must be an integer */
                        i = out_idx - 1;
                        if (out[out_idx].gen.flags & VAR_SIG_IDX)
                            i -= substack_len(out, out_idx - 1);
                        if (MPR_INT32 != out[i].gen.datatype)
                            out[i].gen.casttype = MPR_INT32;
                        if (VAR_Y != var)
                            vars[var].flags |= VAR_ASSIGNED;
                    }
                    else if (VAR_Y == var)
                        ++out_assigned;
                    else
                        vars[var].flags |= VAR_ASSIGNED;
                    i = substack_len(out, out_idx);
                    /* nothing extraordinary, continue as normal */
                    out[out_idx].toktype = is_const ? TOK_ASSIGN_CONST : TOK_ASSIGN;
                    out[out_idx].var.offset = 0;
                    while (i > 0) {
                        POP_OUTPUT_TO_OPERATOR();
                        --i;
                    }
                }
                else if (out[out_idx].toktype == TOK_TT) {
                    /* assignment to timetag */
                    /* for now we will only allow assigning to output t_y */
                    /* TODO: enable writing timetags on user-defined variables */
                    {FAIL_IF(out[out_idx].var.idx != VAR_Y, "Only output timetag is writable.");}
                    /* disable writing to current timetag for now */
                    {FAIL_IF(!(out[out_idx].gen.flags & VAR_HIST_IDX),
                             "Only past samples of output timetag are writable.");}
                    out[out_idx].toktype = TOK_ASSIGN_TT;
                    out[out_idx].gen.datatype = MPR_DBL;
                    POP_OUTPUT_TO_OPERATOR();
                }
                else if (out[out_idx].toktype == TOK_VECTORIZE) {
                    int var, j, arity = out[out_idx].fn.arity;

                    /* out token is vectorizer */
                    --out_idx;
                    {FAIL_IF(out[out_idx].toktype != TOK_VAR, "Bad token left of assignment. (1)");}
                    var = out[out_idx].var.idx;
                    if (var >= VAR_X_NEWEST)
                        {FAIL("Cannot assign to input variable 'x'.");}
                    else if (!(out[out_idx].gen.flags & VAR_HIST_IDX)) {
                        if (var == VAR_Y)
                            ++out_assigned;
                        else
                            vars[var].flags |= VAR_ASSIGNED;
                    }

                    for (i = 0; i < arity; i++) {
                        if (out[out_idx].toktype != TOK_VAR)
                            {FAIL("Bad token left of assignment. (2)");}
                        else if (out[out_idx].var.idx != var)
                            {FAIL("Cannot mix variables in vector assignment.");}
                        j = substack_len(out, out_idx);
                        out[out_idx].toktype = is_const ? TOK_ASSIGN_CONST : TOK_ASSIGN;
                        while (j-- > 0)
                            POP_OUTPUT_TO_OPERATOR();
                    }

                    i = 0;
                    j = op_idx;
                    while (j >= 0 && arity > 0) {
                        if (op[j].toktype & TOK_ASSIGN) {
                            op[j].var.offset = i;
                            i += op[j].gen.vec_len;
                        }
                        --j;
                    }
                }
                else
                    {FAIL("Malformed expression left of assignment.");}
                assigning = 0;
                allow_toktype = OBJECT_TOKENS;
                break;
            default:
                {FAIL("Unknown token type.");}
                break;
        }
#if TRACE_PARSE
        print_stack("OUTPUT STACK", out, out_idx, vars, 0);
        print_stack("OPERATOR STACK", op, op_idx, vars, 0);
#endif
    }

    {FAIL_IF(allow_toktype & TOK_LITERAL || !out_assigned, "Expression has no output assignment.");}

    /* check that all used-defined variables were assigned */
    for (i = 0; i < num_var; i++) {
        {FAIL_IF(!(vars[i].flags & VAR_ASSIGNED), "User-defined variable not assigned.");}
    }

    /* finish popping operators to output, check for unbalanced parentheses */
    while (op_idx >= 0 && op[op_idx].toktype < TOK_ASSIGN) {
        {FAIL_IF(op[op_idx].toktype == TOK_OPEN_PAREN, "Unmatched parentheses or misplaced comma. (4)");}
        POP_OPERATOR_TO_OUTPUT();
    }

    if (op_idx >= 0) {
        int var_idx = op[op_idx].var.idx;
        if (var_idx < N_USER_VARS) {
            if (!vars[var_idx].vec_len)
                vars[var_idx].vec_len = out[out_idx].gen.vec_len;
            /* update and lock vector length of assigned variable */
            op[op_idx].gen.vec_len = vars[var_idx].vec_len;
            op[op_idx].gen.flags |= VEC_LEN_LOCKED;
        }
    }

    /* pop assignment operator(s) to output */
    while (op_idx >= 0) {
        {FAIL_IF(!op_idx && op[op_idx].toktype < TOK_ASSIGN, "Malformed expression (11).");}
        PUSH_TO_OUTPUT(op[op_idx]);
        /* check vector length and type */
        {FAIL_IF(out[out_idx].toktype == TOK_ASSIGN_USE
                 && check_assign_type_and_len(buff, out, out_idx, vars) == -1,
                 "Malformed expression (12).");}
        POP_OPERATOR();
    }

    /* mark last assignment token to clear eval stack */
    out[out_idx].gen.flags |= CLEAR_STACK;

    /* promote unlocked variable token vector lengths */
    for (i = 0; i < out_idx; i++) {
        if (TOK_VAR == out[i].toktype && out[i].var.idx < N_USER_VARS
            && !(out[i].gen.flags & VEC_LEN_LOCKED))
            out[i].gen.vec_len = vars[out[i].var.idx].vec_len;
    }

    /* check vector length and type */
    {FAIL_IF(check_assign_type_and_len(buff, out, out_idx, vars) == -1,
             "Malformed expression (13).");}

    {FAIL_IF(replace_special_constants(out, out_idx), "Error replacing special constants."); }

#if TRACE_PARSE
    print_stack("OUTPUT STACK", out, out_idx, vars, 0);
    print_stack("OPERATOR STACK", op, op_idx, vars, 0);
#endif

    /* copy tokens */
    mpr_expr_cpy_tokens(expr, out, out_idx + 1);

    /* copy user-defined variables */
    mpr_expr_cpy_vars(expr, vars, num_var);

    mpr_expr_free_eval_buffer(buff);
    return 0;

error:
    while (out_idx >= 0) {
        expr_tok_free(&out[out_idx]);
        --out_idx;
    }
    while (op_idx-- >= 0) {
        expr_tok_free(&op[op_idx]);
        --op_idx;
    }
    while (--num_var >= 0)
        expr_var_free_mem(&vars[num_var]);
    while (temp_vars) {
        temp_var_cache tmp = temp_vars->next;
        free((char*)temp_vars->in_name);
        free((char*)temp_vars->accum_name);
        free(temp_vars);
        temp_vars = tmp;
    }
    mpr_expr_free_eval_buffer(buff);
    return 1;
}

#endif /* __MPR_EXPRESSION_PARSER_H__ */
