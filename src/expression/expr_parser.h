#ifndef __MPR_EXPR_PARSER_H__
#define __MPR_EXPR_PARSER_H__

#include <assert.h>
#include "expr_lexer.h"
#include "expr_stack.h"

#define STACK_SIZE 64

/* Macros to help express stack operations in parser. */
#if TRACE_PARSE
#define FAIL(msg) {     \
    trace("%s\n", msg); \
    goto error;         \
}
#else
#define FAIL(msg) {     \
    goto error;         \
}
#endif /* TRACE_PARSE */

#define FAIL_IF(condition, msg) \
    if (condition) {FAIL(msg)}

#define GET_NEXT_TOKEN(x)                   \
{                                           \
    x.toktype = TOK_UNKNOWN;                \
    lex_idx = expr_lex(str, lex_idx, &x);   \
    {FAIL_IF(!lex_idx, "Error in lexer.");} \
}

#define ADD_TO_VECTOR()                                         \
{                                                               \
    switch (estack_peek(out, ESTACK_TOP)->toktype) {            \
        case TOK_LOOP_END:                                      \
            (   (estack_peek(op, ESTACK_TOP))->gen.vec_len      \
             += (estack_peek(out, ESTACK_TOP-1))->gen.vec_len); \
            ++(estack_peek(op, ESTACK_TOP))->fn.arity;          \
            break;                                              \
        case TOK_LITERAL:                                       \
            if ((estack_peek(op, ESTACK_TOP))->fn.arity         \
                && estack_squash(out)) {                        \
                ++(estack_peek(op, ESTACK_TOP))->gen.vec_len;   \
                break;                                          \
            }                                                   \
            /* else continue to default... */                   \
        default:                                                \
            (   (estack_peek(op, ESTACK_TOP))->gen.vec_len      \
             += (estack_peek(out, ESTACK_TOP))->gen.vec_len);   \
            ++(estack_peek(op, ESTACK_TOP))->fn.arity;          \
    }                                                           \
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
                       | TOK_OPEN_PAREN | TOK_OPEN_SQUARE | TOK_OP_UNARY | TOK_TT | TOK_HASH)
#define JOIN_TOKENS (TOK_OP | TOK_OP_UNARY | TOK_CLOSE_PAREN | TOK_CLOSE_SQUARE | TOK_CLOSE_CURLY \
                     | TOK_COMMA | TOK_COLON | TOK_SEMICOLON | TOK_FN_DOT)

/* some vector functions require additional variable load/write tokens for function memory */
int push_vfn_token(estack op, estack out, etoken tok, expr_var_t *vars, int *p_num_var, int move)
{
    etoken_t newtok;
    int i, memory = vfn_tbl[tok->fn.idx].memory, num_var = *p_num_var;
    *p_num_var += memory;

    for (i = 0; i < memory; i++) {
        /* add assignment token */
        newtok.toktype = (i == 0) ? TOK_ASSIGN_USE : TOK_ASSIGN;
        newtok.var.idx = num_var;
#if TRACE_PARSE
        printf("vfn memory: adding assign(vars[%d]) to op stack\n", num_var);
#endif
        newtok.gen.datatype = vars[num_var].datatype = tok->gen.datatype;
        vars[num_var].name = NULL;
        vars[num_var].flags = VAR_ASSIGNED;
        newtok.gen.casttype = 0;
        newtok.gen.vec_len = 1;
        newtok.gen.flags = 0;
        newtok.var.vec_idx = 0;
        newtok.var.offset = 0;
        estack_push(op, &newtok);
        ++num_var;
    }
    if (memory > 1) {
        newtok.toktype = TOK_SP_ADD;
        newtok.lit.val.i = memory - 1;
        estack_push(op, &newtok);
    }
    if (vfn_tbl[tok->fn.idx].len) {
        tok->gen.vec_len = vfn_tbl[tok->fn.idx].len;
        tok->gen.flags |= VEC_LEN_LOCKED;
    }
    else
        tok->gen.vec_len = 1;

    estack_push(op, tok);

    /* if necessary, temporarily move tokens from output stack to operator stack */
    for (i = 0; i < move; i++)
        estack_push(op, estack_pop(out));

    num_var -= memory;
    for (i = 0; i < memory; i++) {
        /* add load token */
#if TRACE_PARSE
        printf("vfn memory: adding load(vars[%d]) to out stack\n", num_var - i);
#endif
        newtok.toktype = TOK_VAR;
        newtok.var.idx = num_var + i;
        newtok.gen.flags = 0;
        estack_push(out, &newtok);
    }

    /* move tokens back */
    for (i = 0; i < move; i++)
        estack_push(out, estack_pop(op));

    return memory;
}

/*! Use Dijkstra's shunting-yard algorithm to parse expression into RPN stack. */
int expr_parser_build_stack(mpr_expr expr, const char *str,
                            int num_src, const mpr_type *src_types, const unsigned int *src_lens,
                            int num_dst, const mpr_type *dst_types, const unsigned int *dst_lens)
{
    estack out = estack_new(STACK_SIZE), op = estack_new(STACK_SIZE);
    expr_var_t vars[N_USER_VARS];
    int i, lex_idx = 0, allow_toktype = 0x2FFFFF;;

    /* TODO: use bitflags instead? */
    uint8_t assigning = 0, is_const = 1, out_assigned = 0, muted = 0, vectorizing = 0;
    uint8_t lambda_allowed = 0, reduce_types = 0;
    uint8_t decorating_var = 0;
    uint8_t vec_len_ctx = 0;

    temp_var_cache temp_vars = NULL;
    /* TODO: optimise these vars */
    int num_var = 0;
    etoken_t tok;
    tok.toktype = TOK_UNKNOWN;
    mpr_type type_hi = MPR_INT32, type_lo = MPR_DBL;

    /* ignoring spaces at start of expression */
    while (str[lex_idx] == ' ') ++lex_idx;
    {FAIL_IF(!str[lex_idx], "No expression found.");}

    assigning = 1;
    allow_toktype = TOK_VAR | TOK_TT | TOK_OPEN_SQUARE | TOK_MUTED | TOK_OP_UNARY;

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
        if (src_lens[i] > out->vec_len)
            out->vec_len = src_lens[i];
    }

#if TRACE_PARSE
    printf("parsing expression '%s'\n", str);
#endif

    while (str[lex_idx]) {
        /* check last token */
        switch (tok.toktype) {
            case TOK_VAR:
            case TOK_CLOSE_PAREN:
            case TOK_CLOSE_CURLY:
            case TOK_CLOSE_SQUARE:
                decorating_var = 1;
                break;
            default:
                decorating_var = 0;
        }
        GET_NEXT_TOKEN(tok);
    again:
        /* TODO: streamline handling assigning and lambda_allowed flags */
        if (TOK_LAMBDA == tok.toktype) {
            if (!lambda_allowed) {
#if TRACE_PARSE
                printf("Illegal token sequence (1)\n");
#endif
                goto error;
            }
        }
        else if (!(tok.toktype & allow_toktype)) {
            /* TODO: generalize this special exception for VFN_DIFF */
            if ((TOK_OP & allow_toktype) && (TOK_VFN == tok.toktype) && (VFN_DIFF == tok.fn.idx)) {
                /* continue parsing */
            }
            else {
#if TRACE_PARSE
                printf("Illegal token sequence (2)\n");
#endif
                goto error;
            }
        }
        switch (tok.toktype) {
            case TOK_MUTED:
                muted = 1;
                allow_toktype = TOK_VAR | TOK_TT;
                break;
            case TOK_LITERAL:
                /* push to output stack */
                estack_push(out, &tok);
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
                    while (i < out->num_tokens) {
                        if (out->tokens[i].toktype <= TOK_MOVE)
                            offset += 1 - etoken_get_arity(&out->tokens[i]);
                        ++i;
                    }
                    tok.toktype = TOK_COPY_FROM;
                    tok.con.cache_offset = offset;

                    i = out->num_tokens - 1 - offset;
                    {FAIL_IF(i < 0, "Compilation error (1)");}
                    if (reduce_types & RT_VECTOR)
                        tok.con.vec_len = 1;
                    else
                        tok.con.vec_len = out->tokens[i].gen.vec_len;
                    tok.con.datatype = (  out->tokens[i].gen.casttype
                                        ? out->tokens[i].gen.casttype
                                        : out->tokens[i].gen.datatype);
                    tok.con.flags |= (TYPE_LOCKED | VEC_LEN_LOCKED);

                    /* TODO: handle timetags */
                    is_const = 0;
                    estack_push(out, &tok);

                    allow_toktype = (TOK_VFN_DOT | TOK_RFN | TOK_OP | TOK_CLOSE_PAREN
                                     | TOK_CLOSE_SQUARE | TOK_CLOSE_CURLY | TOK_COLON);
                    if (!(reduce_types & RT_VECTOR))
                        allow_toktype |= TOK_OPEN_SQUARE;
                    if (!(reduce_types & RT_HISTORY))
                        allow_toktype |= TOK_OPEN_CURLY;
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
                    while (pos < out->num_tokens) {
                        if (TOK_SP_ADD == out->tokens[pos].toktype)
                            stack_offset += out->tokens[pos].lit.val.i;
                        else if (TOK_LOOP_START == out->tokens[pos].toktype
                                 && out->tokens[pos].con.flags & RT_INSTANCE)
                            ++stack_offset;
                        else if (TOK_LOOP_END == out->tokens[pos].toktype
                                 && out->tokens[pos].con.flags & RT_INSTANCE)
                            --stack_offset;
                        else if (out->tokens[pos].toktype < TOK_LAMBDA)
                            stack_offset += 1 - etoken_get_arity(&out->tokens[pos]);
                        ++pos;
                    }

                    etoken_cpy(&tok, estack_peek(out, ESTACK_TOP - stack_offset));
                    tok.toktype = TOK_COPY_FROM;
                    tok.con.cache_offset = stack_offset;
                    estack_push(out, &tok);
                    allow_toktype = (TOK_VFN_DOT | TOK_RFN | TOK_OP | TOK_CLOSE_PAREN
                                     | TOK_CLOSE_SQUARE | TOK_CLOSE_CURLY | TOK_COLON);
                    break;
                }

                if (VAR_X_NEWEST == tok.var.idx) {
                    tok.gen.datatype = type_lo;
                    tok.gen.casttype = type_hi;
                    tok.gen.vec_len = out->vec_len;
                    tok.gen.flags |= (TYPE_LOCKED | VEC_LEN_LOCKED);
                    is_const = 0;
                }
                else if (tok.var.idx >= VAR_X) {
                    int slot = tok.var.idx - VAR_X;
                    {FAIL_IF(slot >= num_src, "Input slot index > number of sources.");}
                    tok.gen.datatype = src_types[slot];
                    tok.gen.vec_len = src_lens[slot];
                    tok.gen.flags |= (TYPE_LOCKED | VEC_LEN_LOCKED);
                    is_const = 0;
                }
                else if (VAR_Y == tok.var.idx) {
                    tok.gen.datatype = dst_types[0];
                    tok.gen.vec_len = dst_lens[0];
                    tok.gen.flags |= (TYPE_LOCKED | VEC_LEN_LOCKED);
                }
                else if (VAR_NOW == tok.var.idx || VAR_NEXT == tok.var.idx) {
                    tok.gen.datatype = MPR_DBL;
                    tok.gen.vec_len = 1;
                    tok.gen.flags |= (TYPE_LOCKED | VEC_LEN_LOCKED);
                }
                else {
                    if (TOK_TT == tok.toktype) {
                        varname += 2;
                        len -= 2;
                    }
                    i = expr_var_find_by_name(vars, num_var, varname, len);
                    if (i >= 0) {
                        tok.var.idx = i;
                        tok.gen.datatype = vars[i].datatype;
                        tok.gen.vec_len = vars[i].vec_len;
                        if (tok.gen.vec_len)
                            tok.gen.flags |= VEC_LEN_LOCKED;
                    }
                    else {
                        {FAIL_IF(num_var >= N_USER_VARS, "Maximum number of variables exceeded. (2)");}
                        /* need to store new variable */
                        expr_var_set(&vars[num_var], varname, len, type_hi, 0, VAR_INSTANCED);
#if TRACE_PARSE
                        printf("Stored new variable '%s' at index %i\n", vars[num_var].name, num_var);
#endif
                        tok.var.idx = num_var;

                        if (TOK_VAR == tok.toktype) {
                            i = op->num_tokens - 1;
                            while (i >= 0) {
                                etoken t = estack_peek(op, i);
                                if (TOK_FN == t->toktype && FN_VEC_IDX == t->fn.idx) {
                                    tok.var.datatype = MPR_INT32;
                                    vars[num_var].datatype = MPR_INT32;
                                    break;
                                }
                                --i;
                            }
                            if (i < 0) {
                                tok.var.datatype = type_hi;
                            }
                        }
                        else {
                            /* use double type for timestamps */
                            tok.var.datatype = MPR_DBL;
                        }

                        /* special case: 'alive' controls instance lifetime */
                        /* special case: 'muted' controls mute state */
                        // TODO: switch to variable property e.g. `y.alive`
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
                if (tok.toktype == TOK_TT) {
                    tok.gen.datatype = MPR_DBL;
                    tok.gen.vec_len = 1;
                    tok.gen.flags |= VEC_LEN_LOCKED;
                }
                estack_push(out, &tok);

                /* variables can have vector and history indices */
                allow_toktype = TOK_OPEN_SQUARE | TOK_OPEN_CURLY | TOK_RFN;
                if (VAR_X == tok.var.idx)
                    allow_toktype |= TOK_DOLLAR;
                if (assigning)
                    allow_toktype |= TOK_ASSIGN | TOK_ASSIGN_TT;
                if (TOK_VAR == tok.toktype)
                    allow_toktype |= TOK_VFN_DOT;
                if (tok.var.idx != VAR_Y || out_assigned > 1)
                    allow_toktype |= JOIN_TOKENS;
                muted = 0;
                break;
            }
            case TOK_FN:
            case TOK_FN_DOT: {
                etoken_t newtok;
                int dot = TOK_FN_DOT == tok.toktype;
                tok.gen.datatype = fn_tbl[tok.fn.idx].fn_int ? MPR_INT32 : MPR_FLT;
                tok.fn.arity = fn_tbl[tok.fn.idx].arity;
                tok.toktype = TOK_FN;
                estack_push(op, &tok);

                GET_NEXT_TOKEN(newtok);
                {FAIL_IF(newtok.toktype != TOK_OPEN_PAREN, "missing open parenthesis. (1)");}
                if (dot && tok.fn.arity > 1)
                    newtok.fn.arity = 2;
                else
                    newtok.fn.arity = 1;
                newtok.fn.idx = tok.fn.idx;
                estack_push(op, &newtok);

                switch (tok.fn.arity - dot) {
                    case -1:
                        {FAIL("Dot function syntax requires arity > 0");}
                    case 0:
                        allow_toktype = TOK_CLOSE_PAREN;
                        break;
                    default:
                        allow_toktype = OBJECT_TOKENS;
                        break;
                }
                if (tok.fn.idx >= FN_DEL_IDX)
                    is_const = 0;
                break;
            }
            case TOK_VFN: {
                int memory = vfn_tbl[tok.fn.idx].memory;
                if (memory) {
                    {FAIL_IF((num_var + memory) >= N_USER_VARS,
                             "Maximum number of variables exceeded. (3)");}
                    is_const = 0;
                }
                tok.toktype = TOK_VFN;
                tok.gen.datatype = type_hi;
                tok.fn.arity = vfn_tbl[tok.fn.idx].arity;

                push_vfn_token(op, out, &tok, vars, &num_var, 0);

                allow_toktype = TOK_OPEN_PAREN;
                break;
            }
            case TOK_OP_UNARY:
                {FAIL_IF(   !op->num_tokens
                         && tok.op.idx != OP_LOGICAL_NOT
                         && tok.op.idx != OP_INCREMENT_PRE
                         && tok.op.idx != OP_DECREMENT_PRE, "illegal unary operator at expr start");}
                tok.toktype = TOK_OP;
                /* do not break */
            case TOK_OP: {
                if (OP_PRIME == tok.op.idx) {
                    tok.toktype = TOK_VFN;
                    tok.fn.idx = VFN_DIFF2;
                    /* omit break and continue to case TOK_VFN_DOT */
                }
                else if (OP_INCREMENT_PRE == tok.op.idx || OP_DECREMENT_PRE == tok.op.idx) {
                    if (decorating_var) {
                        /* increment/decrement postfix operator: i++, i-- */
                        tok.op.idx += 2;
                        estack_push(op, &tok);
                        allow_toktype = JOIN_TOKENS;
                    }
                    else {
                        /* increment/decrement prefix operator: ++i, --i */
                        etoken t = estack_peek(op, ESTACK_TOP);
                        {FAIL_IF(t && TOK_OP == t->toktype,
                                 "bad syntax for prefix increment/decrement operator");}
                        estack_push(op, &tok);
                        allow_toktype = TOK_VAR;
                    }
                    break;
                }
                else {
                    /* check precedence of operators on stack */
                    while (op->num_tokens) {
                        etoken op_top = estack_peek(op, ESTACK_TOP);
                        if (   op_top->toktype != TOK_OP
                            || (op_tbl[op_top->op.idx].precedence < op_tbl[tok.op.idx].precedence))
                            break;
                        estack_push(out, estack_pop(op));
                        {FAIL_IF(!estack_check_type(out, vars, 1), "Malformed expression (2)");}
                    }
                    estack_push(op, &tok);
                    allow_toktype = OBJECT_TOKENS & ~TOK_OP;
                    if (op_tbl[tok.op.idx].arity <= 1)
                        allow_toktype &= ~TOK_NEGATE;
                    break;
                }
            }
            case TOK_VFN_DOT: {
                /* First argument subexpression is already on the output stack, but any memory
                 * tokens need to precede it. */
                etoken t = estack_peek(op, ESTACK_TOP);
                if (t->toktype != TOK_RFN || t->fn.idx < RFN_HISTORY) {
                    if (VFN_DIFF2 != tok.fn.idx) {
                        etoken_t newtok;
                        GET_NEXT_TOKEN(newtok);
                        {FAIL_IF(newtok.toktype != TOK_OPEN_PAREN, "missing open parenthesis. (2)");}
                    }
                    int memory = vfn_tbl[tok.fn.idx].memory;
                    if (memory) {
                        {FAIL_IF((num_var + memory) >= N_USER_VARS,
                                 "Maximum number of variables exceeded. (4)");}
                        is_const = 0;
                    }
                    tok.toktype = TOK_VFN;
                    /* borrow datatype from existing argument on output stack */
                    t = estack_peek(out, ESTACK_TOP);
                    tok.gen.datatype = t->gen.datatype;
                    if (MPR_INT32 == tok.gen.datatype && !vfn_tbl[tok.fn.idx].fn_int)
                        tok.gen.datatype = MPR_FLT;
                    tok.fn.arity = vfn_tbl[tok.fn.idx].arity;
                    tok.gen.vec_len = 1;

                    /* the first argument for this VFN is already on the output stack */
                    push_vfn_token(op, out, &tok, vars, &num_var,
                                   estack_get_substack_len(out, ESTACK_TOP));

                    tok.toktype = TOK_OPEN_PAREN;
                    tok.fn.arity = vfn_tbl[tok.fn.idx].arity;
                    estack_push(op, &tok);

                    if ((vfn_tbl[tok.fn.idx].arity - vfn_tbl[tok.fn.idx].memory) > 1) {
                        /* we are expecting more arguments */
                        allow_toktype = OBJECT_TOKENS;
                    }
                    else {
                        allow_toktype = TOK_CLOSE_PAREN;
                        if (VFN_DIFF2 == tok.fn.idx) {
                            /* prime notation doesn't use parentheses so we fake it here */
                            tok.toktype = TOK_CLOSE_PAREN;
                        }
                        else {
                            GET_NEXT_TOKEN(tok);
                            if (tok.toktype != TOK_CLOSE_PAREN) {
                                t = estack_peek(op, ESTACK_TOP - 1);
                                if (VFN_MAX == t->fn.idx || VFN_MIN == t->fn.idx) {
                                    /* overloaded function, TOK_FN_DOT version expects argument */
                                    t->toktype = TOK_FN;
                                    t->fn.idx = (VFN_MAX == t->fn.idx) ? FN_MAX : FN_MIN;
                                    t->fn.arity = 2;
                                    /* also adjust arity of OPEN_PAREN token on op stack */
                                    estack_peek(op, ESTACK_TOP)->fn.arity = 2;
                                    allow_toktype = OBJECT_TOKENS;
                                }
                                else
                                    {FAIL("missing close parenthesis. (1)");}
                            }
                        }
#if TRACE_PARSE
        estack_print("OUTPUT STACK", out, vars, 0);
        estack_print("OPERATOR STACK", op, vars, 0);
#endif
                        goto again;
                    }
                    break;
                }
                else {
                    /* omit break and continue to case TOK_RFN */
                }
            }
            case TOK_RFN: {
                int pre, sslen;
                expr_rfn_t rfn;
                etoken_t newtok;
                uint8_t rt, idx;
                if (tok.fn.idx >= RFN_HISTORY) {
                    rt = _reduce_type_from_fn_idx(tok.fn.idx);
                    /* fail if input is another reduce function */
                    {FAIL_IF(TOK_LOOP_END == (estack_peek(out, ESTACK_TOP))->toktype,
                             "Reduce functions may be nested but not chained.");}
                    /* fail if same-type reduction already on the operator stack (nested) */
                    {FAIL_IF(estack_get_reduce_types(op) & rt,
                             "Syntax error: nested reduce functions of the same type.");}
                    tok.fn.arity = rfn_tbl[tok.fn.idx].arity;
                    tok.gen.datatype = MPR_INT32;
                    estack_push(op, &tok);
                    allow_toktype = TOK_RFN | TOK_VFN_DOT;
                    /* get compound arity of last token */
                    sslen = estack_get_substack_len(out, ESTACK_TOP);
                    switch (rt) {
                        case RT_HISTORY: {
                            int y_ref = 0, x_ref = 0, lit_val, len = sslen;
                            /* History requires an integer argument */
                            /* TODO: allow variable + maximum instead e.g. history(n, 10) */
                            /* TODO: allow range instead e.g. history(-5:-2) */
                            GET_NEXT_TOKEN(tok);
                            {FAIL_IF(tok.toktype != TOK_OPEN_PAREN, "missing open parenthesis. (3)");}
                            GET_NEXT_TOKEN(tok);
                            {FAIL_IF(tok.toktype != TOK_LITERAL || tok.lit.datatype != MPR_INT32,
                                     "'history' must be followed by integer argument.");}
                            lit_val = abs(tok.lit.val.i);

                            for (i = 0; i < len; i++) {
                                int idx = out->num_tokens - 1 - i;
                                etoken t = estack_peek(out, idx);
                                while (TOK_COPY_FROM == t->toktype) {
                                    idx -= t->con.cache_offset + 1;
                                    assert(idx >= 0);
                                    t = estack_peek(out, idx);
                                }
                                len += etoken_get_arity(t);
                                if (TOK_VAR != t->toktype && TOK_TT != t->toktype)
                                    continue;
                                {FAIL_IF(t->gen.flags & VAR_HIST_IDX,
                                         "History indexes not allowed within history reduce function.");}
                                if (VAR_Y == t->var.idx) {
                                    y_ref = 1;
                                    break;
                                }
                                else if (t->var.idx >= VAR_X_NEWEST)
                                    x_ref = 1;
                            }
                            /* TODO: reduce prefix could include BOTH x and y */
                            if (y_ref && x_ref)
                                {FAIL("mixed history reduce is ambiguous.");}
                            else if (y_ref) {
                                etoken t = estack_peek(op, ESTACK_TOP);
                                t->con.reduce_start = lit_val;
                                t->con.reduce_stop = 1;
                            }
                            else if (x_ref) {
                                etoken t = estack_peek(op, ESTACK_TOP);
                                t->con.reduce_start = lit_val - 1;
                                t->con.reduce_stop = 0;
                            }
                            else
                                {FAIL("history reduce requires reference to 'x' or 'y'.");}

                            for (i = 0; i < sslen; i++) {
                                etoken t_op, t_out = estack_peek(out, ESTACK_TOP - i);
                                if (TOK_VAR != t_out->toktype)
                                    continue;
                                t_op = estack_peek(op, ESTACK_TOP);
                                mpr_expr_update_mlen(expr, t_out->var.idx, t_op->con.reduce_start);
                            }
                            GET_NEXT_TOKEN(tok);
                            {FAIL_IF(tok.toktype != TOK_CLOSE_PAREN, "missing close parenthesis. (2)");}
                            break;
                        }
                        case RT_INSTANCE: {
                            /* TODO: fail if var has instance index (once they are implemented) */
                            int v_ref = 0, len = sslen;
                            for (i = 0; i < len; i++) {
                                int idx = out->num_tokens - 1 - i;
                                etoken t = estack_peek(out, idx);
                                while (TOK_COPY_FROM == t->toktype) {
                                    idx -= t->con.cache_offset + 1;
                                    assert(idx > 0);
                                    t = estack_peek(out, idx);
                                }
                                len += etoken_get_arity(t);
                                if (t->toktype != TOK_VAR && t->toktype != TOK_TT)
                                    continue;
                                if (t->var.idx >= VAR_Y) {
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
                            etoken t;
                            for (i = 0; i < sslen; i++) {
                                etoken t = estack_peek(out, ESTACK_TOP - i);
                                if (   (TOK_VAR != t->toktype && TOK_TT != t->toktype)
                                    || t->var.idx < VAR_Y)
                                    continue;
                                {FAIL_IF(t->var.idx == VAR_Y,
                                         "Cannot call signal reduce function on output.");}
                                {FAIL_IF(t->var.idx > VAR_X,
                                         "Signal indexes not allowed within signal reduce function.");}
                                if (VAR_X == t->var.idx) {
                                    x_ref = 1;
                                    if (TOK_VAR == t->toktype) {
                                        /* promote vector length */
                                        t->var.vec_len = out->vec_len;
                                    }
                                }
                            }
                            {FAIL_IF(!x_ref, "signal reduce requires reference to input 'x'.");}
                            if (type_hi == type_lo) /* homogeneous types, no casting necessary */
                                break;
                            t = estack_peek(out, ESTACK_TOP);
                            if (TOK_VAR == t->toktype)
                                t->var.datatype = type_hi;
                            for (i = sslen - 1; i >= 0; i--) {
                                etoken t = estack_peek(out, ESTACK_TOP - i);
                                if (t->toktype != TOK_VAR || t->var.idx < VAR_Y)
                                    continue;
                                /* promote datatype and casttype */
                                t->var.datatype = type_lo;
                                t->var.casttype = type_hi;
                                {FAIL_IF(!estack_check_type(out, vars, 1), "Malformed expression (3).");}
                            }
                            {FAIL_IF(!estack_check_type(out, vars, 0), "Malformed expression (4)");}
                            break;
                        }
                        case RT_VECTOR: {
                            uint8_t vec_len = 0;
                            etoken t;
                            /* Fail if variables in substack have vector idx other than zero */
                            /* TODO: use start variable or expr instead */
                            for (i = 0; i < sslen; i++) {
                                etoken t = estack_peek(out, ESTACK_TOP - i);
                                if (t->toktype != TOK_VAR && t->toktype != TOK_COPY_FROM)
                                    continue;
                                {FAIL_IF(TOK_VAR == t->toktype && t->var.vec_idx && t->var.vec_len == 1,
                                         "Vector indexes not allowed within vector reduce function.");}
                                if (t->gen.vec_len > vec_len)
                                    vec_len = t->gen.vec_len;
                                /* Set token vec_len to 1 since we will be iterating over elements */
                                t->gen.vec_len = 1;
                                t->gen.flags |= VEC_LEN_LOCKED;
                            }
                            t = estack_peek(op, ESTACK_TOP);
                            t->con.reduce_start = 0;
                            t->con.reduce_stop = vec_len;
                            /* check if we are currently reducing over signals */
                            if (RT_SIGNAL & estack_get_reduce_types(op))
                                t->con.flags |= USE_VAR_LEN;

                            break;
                        }
                        default:
                            {FAIL("unhandled reduce function identifier.");}
                    }
                    break;
                }
                else {
                    GET_NEXT_TOKEN(newtok);
                    {FAIL_IF(newtok.toktype != TOK_OPEN_PAREN, "missing open parenthesis. (4)");}
                }
                assert(op->num_tokens);
                etoken_cpy(&newtok, estack_peek(op, ESTACK_TOP));
                rt = _reduce_type_from_fn_idx((estack_peek(op, ESTACK_TOP))->fn.idx);
                /* fail unless reduction already on the stack */
                {FAIL_IF(RT_UNKNOWN == rt, "Syntax error: missing reduce function prefix.");}
                newtok.con.flags |= rt;
                estack_pop(op);
                /* TODO: check if there is possible conflict here between vfn and rfn */
                rfn = tok.fn.idx;
                if (RFN_COUNT == rfn) {
                    int idx = out->num_tokens - 1;
                    etoken t = estack_peek(out, idx);
                    {FAIL_IF(rt != RT_INSTANCE, "count() requires 'instance' prefix");}
                    while (TOK_COPY_FROM == t->toktype) {
                        idx -= t->con.cache_offset + 1;
                        assert(idx > 0);
                        t = estack_peek(out, idx);
                    }
                    if (TOK_VAR == t->toktype) {
                        /* Special case: count() can be represented by single token */
                        etoken t = estack_peek(out, ESTACK_TOP);
                        if (idx != out->num_tokens - 1)
                            estack_cpy_tok(out, ESTACK_TOP, idx);
                        t->toktype = TOK_VAR_NUM_INST;
                        t->gen.datatype = MPR_INT32;
                        allow_toktype = JOIN_TOKENS;
                        GET_NEXT_TOKEN(newtok);
                        {FAIL_IF(newtok.toktype != TOK_CLOSE_PAREN, "missing close parenthesis. (3)");}
                        break;
                    }
                }
                else if (RFN_NEWEST == rfn) {
                    etoken t = estack_peek(out, ESTACK_TOP);
                    {FAIL_IF(rt != RT_SIGNAL, "newest() requires 'signal' prefix");}
                    t->var.idx = VAR_X_NEWEST;
                    if (!(t->gen.flags & TYPE_LOCKED))
                        t->gen.datatype = type_lo;
                    if (t->gen.datatype != type_hi)
                        t->gen.casttype = type_hi;
                    if (!(t->gen.flags & VEC_LEN_LOCKED))
                        t->gen.vec_len = out->vec_len;
                    t->gen.flags |= (TYPE_LOCKED | VEC_LEN_LOCKED);
                    is_const = 0;
                    allow_toktype = JOIN_TOKENS;
                    GET_NEXT_TOKEN(newtok);
                    {FAIL_IF(newtok.toktype != TOK_CLOSE_PAREN, "missing close parenthesis. (4)");}
                    break;
                }

                /* get compound arity of last token */
                sslen = estack_get_substack_len(out, ESTACK_TOP);
                switch (rfn) {
                    case RFN_MEAN: case RFN_CENTER: case RFN_SIZE:  pre = 3; break;
                    default:                                        pre = 2; break;
                }

                {FAIL_IF(out->num_tokens + pre > STACK_SIZE, "Stack size exceeded. (3)");}

                /* find source token(s) for reduce input */
                idx = out->num_tokens - 1;
                while (TOK_COPY_FROM == out->tokens[idx].toktype) {
                    /* TODO: rename 'cache_offset' variable or switch struct */
                    idx -= out->tokens[idx].con.cache_offset + 1;
                    assert(idx > 0 && idx < out->num_tokens);
                }

                if (RFN_REDUCE == rfn) {
                    /* TODO: reduce types should be cached in stack instead of global */
                    reduce_types |= newtok.con.flags & REDUCE_TYPE_MASK;
                    newtok.toktype = TOK_REDUCING;
                    estack_push(op, &newtok);
                    tok.toktype = TOK_OPEN_PAREN;
                    tok.fn.arity = 0;
                    estack_push(op, &tok);
                }

                if (   TOK_COPY_FROM == (estack_peek(out, ESTACK_TOP))->toktype
                    && TOK_VAR != (estack_peek(out, idx))->toktype) {
                    /* make a new copy of this substack */
                    sslen = estack_get_substack_len(out, idx);
                    {FAIL_IF(out->num_tokens + sslen + pre > STACK_SIZE, "Stack size exceeded. (3)");}

                    /* Copy destacked reduce input substack */
                    for (i = 0; i < sslen; i++)
                        estack_push(op, estack_peek(out, idx - i));
                    /* discard copy token */
                    estack_pop(out);
                }
                else {
                    int ar = RFN_CENTER == rfn || RFN_MEAN == rfn || RFN_SIZE == rfn ? 2 : 1;
                    if (RT_INSTANCE == rt) {
                        /* instance loops cache the starting instance idx */
                        ++ar;
                    }

                    /* Destack reduce input substack */
                    for (i = 0; i < sslen; i++) {
                        etoken t = estack_pop(out);
                        if (TOK_COPY_FROM == t->toktype)
                            t->con.cache_offset += ar;
                        estack_push(op, t);
                    }
                }

                /* all instance reduce functions require this token */
                etoken_cpy(&tok, &newtok);
                tok.toktype = TOK_LOOP_START;
                if (RFN_REDUCE == rfn)
                    estack_push(op, &tok);
                else
                    estack_push(out, &tok);

                if (RFN_REDUCE == rfn) {
                    temp_var_cache var_cache;
                    char *temp, *in_name, *accum_name;
                    int len;
                    {FAIL_IF(num_var >= N_USER_VARS, "Maximum number of variables exceeded. (1)");}
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
                        etoken_set_int32(&tok, 0);
                        tok.lit.vec_len = 1;
                        estack_push(out, &tok);

                        /* Restack reduce input */
                        ++sslen;
                        for (i = 0; i < sslen; i++) {
                            etoken out_top = estack_push(out, estack_pop(op));
                            {FAIL_IF(!out_top, "Stack size exceeded.");}
                            if (TOK_LOOP_START == out_top->toktype)
                                var_cache->loop_start_pos = (out->num_tokens - 1);
                        }
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
                    etoken_t newtok, *t = estack_peek(op, ESTACK_TOP);
                    GET_NEXT_TOKEN(newtok);
                    {FAIL_IF(TOK_LITERAL != newtok.toktype || MPR_INT32 != newtok.gen.datatype,
                             "concat() requires an integer argument");}
                    {FAIL_IF(newtok.lit.val.i <= 1 || newtok.lit.val.i > 64,
                             "concat() max size must be between 2 and 64.");}

                    estack_update_vec_len(out, newtok.lit.val.i);
                    tok.gen.vec_len = 0;

                    if (t->gen.casttype)
                        tok.gen.datatype = t->gen.casttype;
                    else
                        tok.gen.datatype = t->gen.datatype;

                    for (i = 0; i < sslen; i++) {
                        t = estack_peek(op, ESTACK_TOP - i);
                        if (TOK_VAR == t->toktype)
                            t->gen.vec_len = 0;
                    }

                    /* Push token for building vector */
                    estack_push_int(out, 0, 0, VEC_LEN_LOCKED);

                    /* Push token for maximum vector length */
                    estack_push_int(out, newtok.lit.val.i, 1, 0);

                    tok.gen.flags |= VEC_LEN_LOCKED;
                }

                switch (rfn) {
                    case RFN_CENTER:
                    case RFN_MAX:
                    case RFN_SIZE:
                        /* some reduce functions need init with the value from first iteration */
                        tok.toktype = TOK_LITERAL;
                        tok.gen.flags = CONST_MINVAL;
                        estack_push(out, &tok);
                        if (RFN_MAX == rfn)
                            break;
                        tok.toktype = TOK_LITERAL;
                        tok.gen.flags = CONST_MAXVAL;
                        estack_push(out, &tok);
                        break;
                    case RFN_MIN:
                        tok.toktype = TOK_LITERAL;
                        tok.gen.flags = CONST_MAXVAL;
                        estack_push(out, &tok);
                        break;
                    case RFN_ALL:
                    case RFN_ANY:
                    case RFN_COUNT:
                    case RFN_MEAN:
                    case RFN_SUM:
                    case RFN_PRODUCT:
                        estack_push_int(out, RFN_ALL == rfn || RFN_PRODUCT == rfn, 1, 0);
                        if (RFN_COUNT == rfn || RFN_MEAN == rfn)
                            estack_push_int(out, RFN_COUNT == rfn, 1, 0);
                        break;
                    default:
                        break;
                }

                /* Restack reduce input */
                for (i = 0; i < sslen; i++) {
                    estack_push(out, estack_pop(op));
                }
                {FAIL_IF(!op->num_tokens, "Malformed expression (5).");}

                if (TOK_COPY_FROM == (estack_peek(out, ESTACK_TOP))->toktype) {
                    /* TODO: simplified reduce functions do not need separate cache for input */
                }

                if (OP_UNKNOWN != rfn_tbl[rfn].op) {
                    etoken_set_op(&tok, rfn_tbl[rfn].op);
                    /* don't use macro here since we don't want to optimize away initialization args */
                    estack_push(out, &tok);
                    {FAIL_IF(!estack_check_type(out, vars, 0), "Malformed expression (6).");}
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
                    estack_push(out, &tok);
                    {FAIL_IF(!estack_check_type(out, vars, 1), "Malformed expression (7)");}
                }
                /* copy type from last token */
                newtok.gen.datatype = (estack_peek(out, ESTACK_TOP))->gen.datatype;

                if (RFN_CENTER == rfn || RFN_MEAN == rfn || RFN_SIZE == rfn || RFN_CONCAT == rfn) {
                    tok.toktype = TOK_SP_ADD;
                    tok.lit.val.i = 1;
                    estack_push(out, &tok);
                }

                /* all instance reduce functions require these tokens */
                etoken_cpy(&tok, &newtok);
                tok.toktype = TOK_LOOP_END;
                if (RFN_CENTER == rfn || RFN_MEAN == rfn || RFN_SIZE == rfn || RFN_CONCAT == rfn) {
                    tok.con.branch_offset = 2 + sslen;
                    tok.con.cache_offset = 2;
                }
                else {
                    tok.con.branch_offset = 1 + sslen;
                    tok.con.cache_offset = 1;
                }
                estack_push(out, &tok);

                if (RFN_CENTER == rfn) {
                    etoken_set_op(&tok, OP_ADD);
                    estack_push(op, &tok);

                    estack_push(out, estack_pop(op));
                    etoken_set_flt(&tok, 0.5f);
                    tok.gen.flags &= ~CONST_SPECIAL;
                    estack_push(out, &tok);

                    etoken_set_op(&tok, OP_MULTIPLY);
                    estack_push(out, &tok);
                    {FAIL_IF(!estack_check_type(out, vars, 0), "Malformed expression (8)");}
                }
                else if (RFN_MEAN == rfn) {
                    etoken_set_op(&tok, OP_DIVIDE);
                    estack_push(out, &tok);
                    {FAIL_IF(!estack_check_type(out, vars, 0), "Malformed expression (9)");}
                }
                else if (RFN_SIZE == rfn) {
                    etoken_set_op(&tok, OP_SUBTRACT);
                    estack_push(out, &tok);
                    {FAIL_IF(!estack_check_type(out, vars, 0), "Malformed expression (10)");}
                }
                else if (RFN_CONCAT == rfn) {
                    tok.toktype = TOK_SP_ADD;
                    tok.lit.val.i = -1;
                    estack_push(out, &tok);
                }
                GET_NEXT_TOKEN(tok);
                {FAIL_IF(tok.toktype != TOK_CLOSE_PAREN, "missing close parenthesis. (5)");}
                allow_toktype = JOIN_TOKENS;
                if (RFN_CONCAT == rfn) {
                    /* Allow chaining another dot function after concat() */
                    allow_toktype |= TOK_VFN_DOT;
                }
                break;
            }
            case TOK_LAMBDA: {
                /* Pop from operator stack to output until left parenthesis found. This should
                 * finish stacking the accumulator initialization tokens and re-stack the reduce
                 * function input stack */
                while (op->num_tokens && (estack_peek(op, ESTACK_TOP))->toktype != TOK_OPEN_PAREN) {
                    estack_push(out, estack_pop(op));
                    etoken out_top = estack_check_type(out, vars, 1);
                    {FAIL_IF(!out_top, "Malformed expression (11)");}
                    if (TOK_LOOP_START == out_top->toktype)
                        temp_vars->loop_start_pos = (out->num_tokens - 1);
                }
                {FAIL_IF(!op->num_tokens, "Unmatched parentheses. (1)");}
                /* Don't pop the left parenthesis yet */

                lambda_allowed = 0;
                allow_toktype = OBJECT_TOKENS;
                break;
            }
            case TOK_OPEN_PAREN: {
                etoken t = estack_peek(op, ESTACK_TOP);
                tok.fn.arity = 1;
                if (TOK_VFN == t->toktype)
                    tok.fn.arity += vfn_tbl[t->fn.idx].memory;
                tok.fn.idx = (TOK_FN == t->toktype || TOK_VFN == t->toktype) ? t->fn.idx : FN_UNKNOWN;
                estack_push(op, &tok);
                allow_toktype = OBJECT_TOKENS;
                break;
            }
            case TOK_CLOSE_CURLY:
            case TOK_CLOSE_PAREN:
            case TOK_CLOSE_SQUARE: {
                int arity;
                etoken op_top;
                /* pop from operator stack to output until left parenthesis found */
                while (op->num_tokens) {
                    op_top = estack_peek(op, ESTACK_TOP);
                    if (op_top->toktype == TOK_OPEN_PAREN || op_top->toktype == TOK_VECTORIZE)
                        break;
                    estack_push(out, estack_pop(op));
                    {FAIL_IF(!estack_check_type(out, vars, 1), "Malformed expression (12).");}
                }
                {FAIL_IF(!op->num_tokens, "Unmatched parentheses, brackets, or misplaced comma. (1)");}

                if (TOK_VECTORIZE == op_top->toktype) {
                    op_top->gen.flags |= VEC_LEN_LOCKED;
                    ADD_TO_VECTOR();
                    estack_lock_vec_len(out);
                    if (op_top->fn.arity > 1) {
                        estack_push(out, estack_pop(op));
                        {FAIL_IF(!estack_check_type(out, vars, 1), "Malformed expression (13)");}
                    }
                    else {
                        /* we do not need vectorizer token if vector length == 1 */
                        estack_pop(op);
                    }
                    vectorizing = 0;
                    allow_toktype = (TOK_OP | TOK_CLOSE_PAREN | TOK_CLOSE_CURLY | TOK_COMMA
                                     | TOK_COLON | TOK_SEMICOLON | TOK_VFN_DOT);
                    if (assigning)
                        allow_toktype |= (TOK_ASSIGN | TOK_ASSIGN_TT);
                    break;
                }

                arity = estack_peek(op, ESTACK_TOP)->fn.arity;
                /* remove left parenthesis from operator stack */
                estack_pop(op);

                allow_toktype = JOIN_TOKENS | TOK_VFN_DOT | TOK_RFN;
                if (assigning)
                    allow_toktype |= (TOK_ASSIGN | TOK_ASSIGN_TT);

                if (!op->num_tokens)
                    break;

                /* if the top of the operator stack is tok_fn or tok_vfn, pop to output */
                op_top = estack_peek(op, ESTACK_TOP);
                if (TOK_FN == op_top->toktype) {
                    if (FN_SIG_IDX == op_top->fn.idx) {
                        etoken top = estack_peek(out, ESTACK_TOP);
                        etoken top_m1 = estack_peek(out, ESTACK_TOP - 1);
                        {FAIL_IF(TOK_VAR != top->toktype || VAR_X != top->var.idx,
                                 "Signal index used on incompatible token.");}
                        {FAIL_IF(top->gen.flags & VAR_SIG_IDX, "Signal index already set.");}
                        if (TOK_LITERAL == top_m1->toktype) {
                            /* Optimize by storing signal idx in variable token */
                            int sig_idx;
                            {FAIL_IF(MPR_INT32 != top_m1->gen.datatype,
                                     "Signal index must be an integer.");}
                            sig_idx = top_m1->lit.val.i % num_src;
                            if (sig_idx < 0)
                                sig_idx += num_src;
                            top->var.idx = sig_idx + VAR_X;
                            estack_cpy_tok(out, ESTACK_TOP - 1, ESTACK_TOP);
                            estack_pop(out);
                        }
                        else {
                            top->gen.flags |= VAR_SIG_IDX;
                            /* signal indices must be integers */
                            if (top_m1->gen.datatype != MPR_INT32)
                                top_m1->gen.casttype = MPR_INT32;
                            if (type_hi != type_lo) {
                                /* heterogeneous types, need to cast */
                                top->var.datatype = type_lo;
                                top->var.casttype = type_hi;
                            }
                        }
                        estack_pop(op);

                        /* signal index set */
                        if (!(top->gen.flags & VAR_VEC_IDX) && !top->var.vec_idx)
                            allow_toktype |= TOK_OPEN_SQUARE;
                        if (!(top->gen.flags & VAR_HIST_IDX))
                            allow_toktype |= TOK_OPEN_CURLY;
                    }
                    else if (FN_DEL_IDX == op_top->fn.idx) {
                        int buffer_size = 0;
                        etoken t = estack_peek(out, ESTACK_TOP);
                        switch (arity) {
                            case 2:
                                /* max delay should be at the top of the output stack */
                                {FAIL_IF(t->toktype != TOK_LITERAL, "non-constant max history.");}
                                switch (t->gen.datatype) {
#define TYPED_CASE(MTYPE, T)                                            \
                                    case MTYPE:                         \
                                        buffer_size = (int)t->lit.val.T;\
                                        break;
                                    TYPED_CASE(MPR_INT32, i)
                                    TYPED_CASE(MPR_FLT, f)
                                    TYPED_CASE(MPR_DBL, d)
#undef TYPED_CASE
                                    default:
                                        break;
                                }
                                {FAIL_IF(buffer_size < 0, "negative history buffer size detected.");}
                                estack_pop(out);
                                t = estack_peek(out, ESTACK_TOP);
                                buffer_size = buffer_size * -1;
                            case 1:
                                /* variable should be at the top of the output stack */
                                {FAIL_IF(t->toktype != TOK_VAR && t->toktype != TOK_TT,
                                         "delay on non-variable token.");}
                                t = estack_peek(out, ESTACK_TOP - 1);
                                if (!buffer_size) {
                                    {FAIL_IF(t->toktype != TOK_LITERAL,
                                             "variable history indices must include maximum value.");}
                                    switch (t->gen.datatype) {
#define TYPED_CASE(MTYPE, T)                                                        \
                                        case MTYPE:                                 \
                                            buffer_size = -(int)ceil(t->lit.val.T); \
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
                                    estack_cpy_tok(out, ESTACK_TOP - 1, ESTACK_TOP);
                                    estack_pop(out);
                                    estack_pop(op);
                                    break;
                                }
                                t = estack_peek(out, ESTACK_TOP);
                                mpr_expr_update_mlen(expr, t->var.idx, buffer_size);
                                /* TODO: disable non-const assignment to past values of output */
                                t->gen.flags |= VAR_HIST_IDX;
                                if (assigning) {
                                    t = estack_peek(out, ESTACK_TOP - 1);
                                    t->gen.flags |= (TYPE_LOCKED | VEC_LEN_LOCKED);
                                }
                                estack_pop(op);
                                break;
                            default:
                                {FAIL("Illegal arity for variable delay.");}
                        }

                        t = estack_peek(out, ESTACK_TOP);
                        if (!(t->gen.flags & VAR_SIG_IDX) && VAR_X == t->var.idx)
                            allow_toktype |= TOK_DOLLAR;
                        if (!(t->gen.flags & VAR_VEC_IDX) && !t->var.vec_idx)
                            allow_toktype |= TOK_OPEN_SQUARE;
                    }
                    else if (FN_VEC_IDX == op_top->fn.idx) {
                        etoken top = estack_peek(out, ESTACK_TOP);
                        etoken top_m1 = estack_peek(out, ESTACK_TOP - 1);

                        {FAIL_IF(arity != 1, "vector index arity != 1.");}
                        {FAIL_IF(top->toktype != TOK_VAR, "Missing variable for vector indexing");}

                        top->gen.flags |= VAR_VEC_IDX;
                        estack_pop(op);
                        if (TOK_LITERAL == top_m1->toktype && TOK_VAR == top->gen.toktype) {
                            if (top->var.idx == VAR_Y || top->var.idx >= VAR_X
                                || vars[top->var.idx].vec_len) {
                                if (MPR_INT32 == top_m1->gen.datatype) {
                                    /* Optimize by storing vector idx in variable token */
                                    int vec_len, vec_idx;
                                    if (VAR_Y == top->var.idx)
                                        vec_len = dst_lens[0];
                                    else if (VAR_X <= top->var.idx)
                                        vec_len = src_lens[top->var.idx - VAR_X];
                                    else
                                        vec_len = vars[top->var.idx].vec_len;
                                    vec_idx = top_m1->lit.val.i % vec_len;
                                    if (vec_idx < 0)
                                        vec_idx += vec_len;
                                    top->var.vec_idx = vec_idx;
                                    top->gen.flags &= ~VAR_VEC_IDX;
                                    estack_cpy_tok(out, ESTACK_TOP - 1, ESTACK_TOP);
                                    estack_pop(out);
                                    top = top_m1;
                                }
                            }
                            else if (VAR_X_NEWEST != top->var.idx) {
                                int vec_len = vars[top->var.idx].vec_len;
                                int vec_idx = 0;
                                switch (top_m1->gen.datatype) {
                                    case MPR_INT32: vec_idx = top_m1->lit.val.i;       break;
                                    case MPR_FLT:   vec_idx = ceil(top_m1->lit.val.f); break;
                                    case MPR_DBL:   vec_idx = ceil(top_m1->lit.val.d); break;
                                }
                                {FAIL_IF((vec_idx + 1) > MPR_MAX_VECTOR_LEN,
                                         "vector index exceeds maximum vector length");}
                                if (vec_idx >= vec_len) {
                                    vars[top->var.idx].vec_len = vec_idx + 1;
                                }
                            }
                        }
                        /* also set var vec_len to 1 */
                        /* TODO: consider vector indices */
                        top->var.vec_len = 1;
                        top->gen.flags |= VEC_LEN_LOCKED;

                        /* vector index set */
                        if (!(top->gen.flags & VAR_SIG_IDX) && VAR_X == top->var.idx)
                            allow_toktype |= TOK_DOLLAR;
                        if (!(top->gen.flags & VAR_HIST_IDX))
                            allow_toktype |= TOK_OPEN_CURLY;
                        break;
                    }
                    else {
                        if (arity != fn_tbl[op_top->fn.idx].arity) {
                            /* check for overloaded functions */
                            if (arity != 1)
                                {FAIL("Function arity mismatch (1).");}
                            if (op_top->fn.idx == FN_MIN) {
                                op_top->toktype = TOK_VFN;
                                op_top->fn.idx = VFN_MIN;
                            }
                            else if (op_top->fn.idx == FN_MAX) {
                                op_top->toktype = TOK_VFN;
                                op_top->fn.idx = VFN_MAX;
                            }
                            else
                                {FAIL("Function arity mismatch (2).");}
                        }
                        estack_push(out, estack_pop(op));
                        {FAIL_IF(!estack_check_type(out, vars, 1), "Malformed expression (14)");}
                    }

                }
                else if (TOK_VFN == op_top->toktype) {
                    /* check arity */
                    etoken t = estack_pop(op);
                    {FAIL_IF(arity != vfn_tbl[t->fn.idx].arity, "VFN arity mismatch.");}
                    estack_push(out, t);
                    {FAIL_IF(!estack_check_type(out, vars, 1), "Malformed expression (15)");}

                    if (vfn_tbl[t->fn.idx].memory) {
                        int memory = vfn_tbl[t->fn.idx].memory;
                        t = estack_peek(out, ESTACK_TOP);
                        if (TOK_SP_ADD == (estack_peek(op, ESTACK_TOP))->toktype)
                            estack_push(out, estack_pop(op));
                        /* move assignment tokens */
                        for (; memory > 0; memory--) {
                            etoken tmp = estack_pop(op);
                            {FAIL_IF(tmp->toktype != TOK_ASSIGN && tmp->toktype != TOK_ASSIGN_USE,
                                     "VFN missing memory assignment tokens.");}
                            /* copy datatype and vector length from vfn token */
                            tmp->gen.datatype = t->gen.datatype;
                            tmp->gen.vec_len = t->gen.vec_len;
                            estack_push(out, tmp);
                        }
                    }
                }
                else if (TOK_REDUCING == op_top->toktype) {
                    int cache_pos;
                    etoken t;

                    /* remove the cached reduce variables */
                    temp_var_cache var_cache = temp_vars;
                    temp_vars = var_cache->next;

                    cache_pos = var_cache->loop_start_pos;
                    t = estack_peek(out, cache_pos);
                    {FAIL_IF(t->toktype != TOK_LOOP_START, "Compilation error (2)");}

                    free((char*)var_cache->in_name);
                    free((char*)var_cache->accum_name);
                    free(var_cache);

                    /* push move token to output */
                    tok.toktype = TOK_MOVE;
                    if (t->con.flags & RT_INSTANCE)
                        tok.con.cache_offset = 3;
                    else
                        tok.con.cache_offset = 2;
                    if (t->gen.casttype)
                        tok.con.datatype = t->gen.casttype;
                    else
                        tok.con.datatype = t->gen.datatype;
                    estack_push(out, &tok);

                    /* push branch token to output */
                    t = estack_pop(op);
                    tok.toktype = TOK_LOOP_END;
                    tok.con.flags |= t->con.flags;
                    tok.con.branch_offset = out->num_tokens - 1 - cache_pos;
                    tok.con.cache_offset = -1;
                    tok.con.reduce_start = t->con.reduce_start;
                    tok.con.reduce_stop = t->con.reduce_stop;
                    estack_push(out, &tok);
                    reduce_types &= ~(tok.con.flags & REDUCE_TYPE_MASK);
                }
                /* special case: if top of stack is tok_assign_use, pop to output */
                if (op->num_tokens && (estack_peek(op, ESTACK_TOP))->toktype == TOK_ASSIGN_USE) {
                    estack_push(out, estack_pop(op));
                    {FAIL_IF(!estack_check_type(out, vars, 1), "Malformed expression (16).");}
                }
                break;
            }
            case TOK_COMMA: {
                etoken t;
                /* pop from operator stack to output until left parenthesis or TOK_VECTORIZE found */
                while ((t = estack_peek(op, ESTACK_TOP)) && t->toktype != TOK_OPEN_PAREN
                       && t->toktype != TOK_VECTORIZE) {
                    estack_push(out, estack_pop(op));
                    {FAIL_IF(!estack_check_type(out, vars, 1), "Malformed expression (17)");}
                }
                {FAIL_IF(!(t = estack_peek(op, ESTACK_TOP)), "Malformed expression (18).");}
                if (TOK_VECTORIZE == t->toktype) {
                    ADD_TO_VECTOR();
                }
                else {
                    /* check if paren is attached to a function */
                    {FAIL_IF(FN_UNKNOWN == t->fn.idx, "Misplaced comma.");}
                    ++t->fn.arity;
                }
                allow_toktype = OBJECT_TOKENS;
                break;
            }
            case TOK_COLON: {
                etoken op_top;
                /* pop from operator stack to output until conditional found */
                while (   (op_top = estack_peek(op, ESTACK_TOP))
                       && (op_top->toktype != TOK_OP || op_top->op.idx != OP_IF)
                       && (op_top->toktype != TOK_FN || op_top->fn.idx != FN_VEC_IDX)) {
                    estack_push(out, estack_pop(op));
                    {FAIL_IF(!estack_check_type(out, vars, 1), "Malformed expression (19)");}
                }
                {FAIL_IF(!(op_top = estack_peek(op, ESTACK_TOP)), "Unmatched colon.");}

                if (op_top->toktype == TOK_FN) {
                    etoken out_top;
                    /* index is range A:B */

                    /* Pop TOK_FN from operator stack */
                    estack_pop(op);

                    /* Pop parenthesis from output stack, top should now be variable */
                    estack_pop(out);
                    {FAIL_IF(TOK_VAR != (estack_peek(out, ESTACK_TOP))->toktype,
                             "Variable not found for colon indexing.");}

                    /* Push variable back to operator stack */
                    op_top = estack_push(op, estack_pop(out));
                    {FAIL_IF(!op_top, "Stack size exceeded.");}

                    /* Check if left index is an integer */
                    out_top = estack_peek(out, ESTACK_TOP);
                    {FAIL_IF(TOK_LITERAL != out_top->toktype || MPR_INT32 != out_top->gen.datatype,
                             "Non-integer left vector index used with colon.");}
                    op_top->var.vec_idx = out_top->lit.val.i;
                    op_top->var.vec_idx = out_top->lit.val.i;
                    estack_pop(out);
                    estack_push(out, estack_pop(op));
                    {FAIL_IF(!(out_top = estack_check_type(out, vars, 1)), "Malformed expression (20)");}

                    /* Get right index and verify that it is an integer */
                    GET_NEXT_TOKEN(tok);
                    {FAIL_IF(tok.toktype != TOK_LITERAL || tok.gen.datatype != MPR_INT32,
                             "Non-integer right vector index used with colon.");}
                    out_top->var.vec_len = tok.lit.val.i - out_top->var.vec_idx + 1;
                    if (tok.lit.val.i < out_top->var.vec_idx)
                        out_top->var.vec_len += vec_len_ctx;
                    out_top->gen.flags |= VEC_LEN_LOCKED;
                    GET_NEXT_TOKEN(tok);
                    {FAIL_IF(tok.toktype != TOK_CLOSE_SQUARE, "Unmatched bracket.");}
                    /* vector index set */
                    allow_toktype = (JOIN_TOKENS | TOK_VFN_DOT | TOK_RFN);
                    if (assigning)
                        allow_toktype |= TOK_ASSIGN | TOK_ASSIGN_TT;
                    break;
                }
                op_top->op.idx = OP_IF_THEN_ELSE;
                allow_toktype = OBJECT_TOKENS;
                break;
            }
            case TOK_SEMICOLON: {
                int var_idx;
                etoken op_top, out_top = estack_peek(out, ESTACK_TOP);

                /* finish popping operators to output, check for unbalanced parentheses */
                while (   op->num_tokens
                       && (op_top = estack_peek(op, ESTACK_TOP)) && op_top->toktype < TOK_ASSIGN) {
                    if (op_top->toktype == TOK_OPEN_PAREN)
                        {FAIL("Unmatched parentheses or misplaced comma. (2)");}
                    out_top = estack_push(out, estack_pop(op));
                    {FAIL_IF(!estack_check_type(out, vars, 1), "Malformed expression (21)");}
                }

                if ((op_top = estack_peek(op, ESTACK_TOP))) {
                    var_idx = op_top->var.idx;
                    if (var_idx < N_USER_VARS) {
                        if (!vars[var_idx].vec_len) {
                            int temp = out->num_tokens - 1, num_idx = NUM_VAR_IDXS(op_top->gen.flags);
                            etoken t;
                            for (i = 0; i < num_idx && temp > 0; i++)
                                temp -= estack_get_substack_len(out, temp);
                            t = estack_peek(out, temp);
                            vars[var_idx].vec_len = t->gen.vec_len;
                            if (   !(vars[var_idx].flags & TYPE_LOCKED)
                                && vars[var_idx].datatype > t->gen.datatype) {
                                vars[var_idx].datatype = t->gen.datatype;
                            }
                        }
                        /* update and lock vector length of assigned variable */
                        if (!(op_top->gen.flags & VEC_LEN_LOCKED))
                            op_top->gen.vec_len = vars[var_idx].vec_len;
                        op_top->gen.datatype = vars[var_idx].datatype;
                        op_top->gen.flags |= VEC_LEN_LOCKED;
                        if (is_const)
                            vars[var_idx].flags &= ~VAR_INSTANCED;
                    }
                    /* pop assignment operators to output */
                    while (op->num_tokens && (op_top = estack_peek(op, ESTACK_TOP))) {
                        if (op_top->toktype == TOK_OPEN_PAREN)
                            {FAIL("Unmatched parentheses or misplaced comma. (4)");}
                        if ((op->num_tokens == 1) && op_top->toktype < TOK_ASSIGN)
                            {FAIL("Malformed expression (22)");}
                        estack_push(out, estack_pop(op));
                        if (   TOK_ASSIGN_USE == (estack_peek(out, ESTACK_TOP))->toktype
                            && estack_check_assign_type_and_len(out, vars) == -1)
                            {FAIL("Malformed expression (23)");}
                        if (!is_const && TOK_ASSIGN_CONST == estack_peek(out, ESTACK_TOP)->toktype)
                            estack_peek(out, ESTACK_TOP)->toktype = TOK_ASSIGN;
                    }
                    /* mark last assignment token to clear eval stack */
                    (estack_peek(out, ESTACK_TOP))->gen.flags |= CLEAR_STACK;

                    /* check vector length and type */
                    if (estack_check_assign_type_and_len(out, vars) == -1)
                        {FAIL("Malformed expression (24)");}
                }
                else {
                    /* only allow increment/decrement postfix operator here */
                    if (   TOK_OP == out_top->toktype
                        && OP_INCREMENT_PRE <= out_top->op.idx
                        && OP_DECREMENT_POST >= out_top->op.idx) {
                        /* check if substack is entire stack */
                        int substack_len = estack_get_substack_len(out, ESTACK_TOP);
                        if (substack_len == out->num_tokens) {
                            out_top->gen.flags |= (TYPE_LOCKED | CLEAR_STACK);
                        }
                        else {
                            etoken t = estack_peek(out, ESTACK_TOP - substack_len);
                            switch (t->toktype) {
                                case TOK_OP:
                                    if (t->op.idx < OP_INCREMENT_PRE || t->op.idx > OP_DECREMENT_POST)
                                        {FAIL("misplaced increment or decrement operator");}
                                case TOK_ASSIGN:
                                case TOK_ASSIGN_TT:
                                case TOK_ASSIGN_CONST:
                                    out_top->gen.flags |= (TYPE_LOCKED | CLEAR_STACK);
                                    break;
                                default:
                                    {FAIL("Unmatched parentheses or misplaced comma. (3)");}
                            }
                        }
                    }
                    else {
                        {FAIL("Unmatched parentheses or misplaced comma. (3)");}
                    }
                }

                /* start another sub-expression */
                assigning = is_const = 1;
                allow_toktype = TOK_VAR | TOK_TT;
                break;
            }
            case TOK_DOLLAR: {
                etoken out_top = estack_peek(out, ESTACK_TOP);
                {FAIL_IF(TOK_VAR != out_top->toktype, "Signal index on non-variable type.");}
                {FAIL_IF(VAR_X != out_top->var.idx || out_top->gen.flags & VAR_SIG_IDX,
                         "Signal index on non-input type or index already set.");}

                GET_NEXT_TOKEN(tok);
                {FAIL_IF(TOK_OPEN_PAREN != tok.toktype,
                         "Signal index token must be followed by an integer or use parentheses.");}

                /* push a FN_SIG_IDX to operator stack */
                tok.toktype = TOK_FN;
                tok.fn.idx = FN_SIG_IDX;
                tok.fn.arity = 1;
                estack_push(op, &tok);

                /* also push an open parenthesis */
                tok.toktype = TOK_OPEN_PAREN;
                estack_push(op, &tok);

                /* move variable from output to operator stack */
                estack_push(op, estack_pop(out));

                /* sig_idx should come last on output stack (first on operator stack) so we
                 * don't need to move any other tokens */

                allow_toktype = OBJECT_TOKENS;
                break;
            }
            case TOK_OPEN_SQUARE:
                if (decorating_var) { /* vector index */
                    etoken t;
                    {FAIL_IF(TOK_VAR != (estack_peek(out, ESTACK_TOP))->toktype,
                             "error: vector index on non-variable type. (1)");}

                    /* push a FN_VEC_IDX to operator stack */
                    tok.toktype = TOK_FN;
                    tok.fn.idx = FN_VEC_IDX;
                    tok.fn.arity = 1;
                    estack_push(op, &tok);

                    /* also push an open parenthesis */
                    tok.toktype = TOK_OPEN_PAREN;
                    estack_push(op, &tok);

                    /* move variable from output to operator stack */
                    t = estack_push(op, estack_pop(out));
                    {FAIL_IF(!t, "Stack size exceeded.");}

                    if (t->gen.flags & VAR_SIG_IDX) {
                        /* Move sig_idx substack from output to operator */
                        for (i = estack_get_substack_len(out, ESTACK_TOP); i > 0; i--) {
                            t = estack_push(op, estack_pop(out));
                            {FAIL_IF(!t, "Stack size exceeded.");}
                        }
                    }

                    if (t->gen.flags & VAR_HIST_IDX) {
                        /* Move hist_idx substack from output to operator */
                        for (i = estack_get_substack_len(out, ESTACK_TOP); i > 0; i--)
                            estack_push(op, estack_pop(out));
                    }

                    allow_toktype = OBJECT_TOKENS;
                    break;
                }
                else {
                    {FAIL_IF(vectorizing, "Nested (multidimensional) vectors not allowed.");}
                    tok.toktype = TOK_VECTORIZE;
                    tok.gen.vec_len = 0;
                    tok.fn.arity = 0;
                    estack_push(op, &tok);
                    vectorizing = 1;
                    allow_toktype = OBJECT_TOKENS & ~TOK_OPEN_SQUARE;
                }
                break;
            case TOK_OPEN_CURLY: {
                uint8_t flags;
                etoken out_top = estack_peek(out, ESTACK_TOP);
                {FAIL_IF(TOK_VAR != out_top->toktype && TOK_TT != out_top->toktype,
                         "error: history index on non-variable type.");}
                flags = out_top->gen.flags;

                /* push a FN_DEL_IDX to operator stack */
                tok.toktype = TOK_FN;
                tok.fn.idx = FN_DEL_IDX;
                tok.fn.arity = 1;
                estack_push(op, &tok);

                /* also push an open parenthesis */
                tok.toktype = TOK_OPEN_PAREN;
                estack_push(op, &tok);

                /* move variable from output to operator stack */
                estack_push(op, estack_pop(out));

                if (flags & VAR_SIG_IDX) {
                    /* Move sig_idx substack from output to operator */
                    for (i = estack_get_substack_len(out, ESTACK_TOP); i > 0; i--)
                        estack_push(op, estack_pop(out));
                }

                allow_toktype = OBJECT_TOKENS;
                break;
            }
            case TOK_HASH: {
                etoken out_top = estack_peek(out, ESTACK_TOP);
                if (out_top && TOK_VAR == out_top->toktype) {
                    /* we are specifying signal instance index */
                    // fail for now
                    {FAIL_IF(1, "Specifying instance index is not currently supported.")}
                }
                else {
                    /* we are retrieving signal instance index */
                    GET_NEXT_TOKEN(tok);
                    {FAIL_IF(tok.toktype != TOK_VAR, "Misplaced instance index query.");}
                    tok.toktype = TOK_VAR_INST_IDX;
                    estack_push(out, &tok);
                    allow_toktype = JOIN_TOKENS | TOK_DOLLAR;
                }
                break;
            }
            case TOK_NEGATE:
                /* push '-1' to output stack, and '*' to operator stack */
                etoken_set_int32(&tok, -1);
                estack_push(out, &tok);

                etoken_set_op(&tok, OP_MULTIPLY);
                estack_push(op, &tok);

                allow_toktype = OBJECT_TOKENS & ~TOK_NEGATE;
                break;
            case TOK_ASSIGN:
            case TOK_ASSIGN_OP: {
                etoken out_top = estack_peek(out, ESTACK_TOP);
                /* assignment to variable */
                {FAIL_IF(!assigning, "Misplaced assignment operator.");}
                {FAIL_IF(op->num_tokens || !out->num_tokens,
                         "Malformed expression left of assignment.");}

                if (out_top->toktype == TOK_VAR) {
                    int var = out_top->var.idx;
                    if (var >= VAR_X_NEWEST)
                        {FAIL("Cannot assign to input variable 'x' (2)");}
                    if (out_top->gen.flags & VAR_HIST_IDX) {
                        /* unlike variable lookup, history assignment index must be an integer */
                        int idx = 1;
                        etoken t;
                        if (out_top->gen.flags & VAR_SIG_IDX)
                            idx += estack_get_substack_len(out, ESTACK_TOP - 1);
                        t = estack_peek(out, ESTACK_TOP - idx);
                        if (MPR_INT32 != t->gen.datatype)
                            t->gen.casttype = MPR_INT32;
                        if (VAR_Y != var)
                            vars[var].flags |= VAR_ASSIGNED;
                    }
                    else if (VAR_Y == var)
                        ++out_assigned;
                    else
                        vars[var].flags |= VAR_ASSIGNED;
                    i = estack_get_substack_len(out, ESTACK_TOP);
                    /* nothing extraordinary, continue as normal */
                    if (TOK_ASSIGN_OP == tok.toktype) {
                        out_top->toktype = TOK_ASSIGN_OP;
                        out_top->var.op_idx = tok.var.op_idx;
                    }
                    else {
                        out_top->toktype = is_const ? TOK_ASSIGN_CONST : TOK_ASSIGN;
                    }
                    out_top->var.offset = 0;
                    while (i > 0) {
                        estack_push(op, estack_pop(out));
                        --i;
                    }
                }
                else if (out_top->toktype == TOK_TT) {
                    /* assignment to timetag */
                    /* for now we will only allow assigning to output t_y */
                    /* TODO: enable writing timetags on user-defined variables */
                    {FAIL_IF(out_top->var.idx > VAR_Y, "cannot assign timetag on input variable");}
                    /* disable writing to current timetag for now */
                    {FAIL_IF(out_top->var.idx == VAR_Y && !(out_top->gen.flags & VAR_HIST_IDX),
                             "Only past samples of output timetag are writable.");}
                    i = estack_get_substack_len(out, ESTACK_TOP);
                    if (TOK_ASSIGN_OP == tok.toktype) {
                        out_top->toktype = TOK_ASSIGN_TT_OP;
                        out_top->var.op_idx = tok.var.op_idx;
                    }
                    else {
                        out_top->toktype = TOK_ASSIGN_TT;
                    }
                    out_top->gen.datatype = MPR_DBL;
                    while (i > 0) {
                        estack_push(op, estack_pop(out));
                        --i;
                    }
                }
                else if (out_top->toktype == TOK_VECTORIZE) {
                    int var, j, arity = out_top->fn.arity;

                    /* out token is vectorizer */
                    estack_pop(out);
                    out_top = estack_peek(out, ESTACK_TOP);
                    {FAIL_IF(out_top->toktype != TOK_VAR, "Bad token left of assignment. (1)");}
                    var = out_top->var.idx;
                    if (var >= VAR_X_NEWEST)
                        {FAIL("Cannot assign to input variable 'x' (3)");}
                    else if (!(out_top->gen.flags & VAR_HIST_IDX)) {
                        if (var == VAR_Y)
                            ++out_assigned;
                        else
                            vars[var].flags |= VAR_ASSIGNED;
                    }

                    for (i = 0; i < arity; i++) {
                        out_top = estack_peek(out, ESTACK_TOP);
                        if (out_top->toktype != TOK_VAR)
                            {FAIL("Bad token left of assignment. (2)");}
                        else if (out_top->var.idx != var)
                            {FAIL("Cannot mix variables in vector assignment.");}
                        j = estack_get_substack_len(out, ESTACK_TOP);
                        if (TOK_ASSIGN_OP == tok.toktype) {
                            out_top->toktype = TOK_ASSIGN_OP;
                            out_top->var.op_idx = tok.var.op_idx;
                        }
                        else {
                            out_top->toktype = is_const ? TOK_ASSIGN_CONST : TOK_ASSIGN;
                        }
                        while (j-- > 0)
                            estack_push(op, estack_pop(out));
                    }

                    i = 0;
                    j = op->num_tokens - 1;
                    while (j >= 0 && arity > 0) {
                        etoken t = estack_peek(op, j);
                        if (t->toktype & TOK_ASSIGN) {
                            t->var.offset = i;
                            i += t->gen.vec_len;
                        }
                        --j;
                    }
                }
                else
                    {FAIL("Malformed expression left of assignment.");}
                assigning = 0;
                allow_toktype = OBJECT_TOKENS;
                break;
            }
            default:
                {FAIL("Unknown token type.");}
                break;
        }
#if TRACE_PARSE
        estack_print("OUTPUT STACK", out, vars, 0);
        estack_print("OPERATOR STACK", op, vars, 0);
#endif
    }

    {FAIL_IF(allow_toktype & TOK_LITERAL || !out_assigned, "Expression has no output assignment.");}

    if (TOK_SEMICOLON != tok.toktype) {
#if TRACE_PARSE
        printf("adding missing semicolon\n");
#endif
        tok.toktype = TOK_SEMICOLON;
        goto again;
    }

    {FAIL_IF(op->num_tokens, "Unmerged tokens on operator stack");}

#if TRACE_PARSE
    printf("expanding assign-op and increment/decrement macro tokens\n");
#endif
    for (i = 0; i < out->num_tokens; i++) {
        etoken t = estack_peek(out, i);
        if (TOK_ASSIGN_OP == t->toktype || TOK_ASSIGN_TT_OP == t->toktype) {
            /* include variable indexes/indexing subexpressions but not the value to be assigned */
            int j, assign_len = 1, arg_substack_len, var_substack_len = estack_get_substack_len(out, i) - 1;
            etoken_t newtok;

            /* check if there are additional assignment tokens (cf. assignment swizzling) */
            while ((i + 1) < out->num_tokens && estack_peek(out, i + 1)->toktype & TOK_ASSIGN) {
                ++assign_len;
                ++var_substack_len;
                ++i;
            }

            arg_substack_len = estack_get_substack_len(out, i - var_substack_len);

            /* current order of tokens on the expression stack:
             *   [i]        top of augmented assignment operator subexpression of length N
             *   [i-N]      top of argument subexpression of length M
             *   [i-N-M]    top of preceding subexpression (if any)
             *
             * need to be expanded to:
             *   [i]        top of augmented assignment operator subexpression of length N
             *   [i-N]      operator token
             *   [i-N-1]    top of argument subexpression of length M
             *   [i-N-M-1]  top of variable subexpression of length N
             *   [i-2N-M-1] top of preceding subexpression (if any)
             */

            /* 1) prepare operator token: copy op and datatype from assignment token */
            newtok.toktype = TOK_OP;
            newtok.op.idx = t->var.op_idx;
            newtok.gen.datatype = t->gen.datatype;
            newtok.gen.casttype = 0;
            newtok.gen.vec_len = t->gen.vec_len;
            newtok.gen.flags = 0;

            /* 2) convert assign*_op tokens to assign* */
            j = i;
            while (j >= 0) {
                etoken a = estack_peek(out, j);
                if (!(TOK_ASSIGN & a->toktype))
                    break;
                if (TOK_ASSIGN_OP == a->toktype)
                    a->toktype = TOK_ASSIGN;
                else if (TOK_ASSIGN_TT_OP == a->toktype)
                    a->toktype = TOK_ASSIGN_TT;
                --j;
            }

            /* 3) insert op token between arg and var sub-expressions */
            estack_insert(out, i - var_substack_len + 1, 1, &newtok);

            /* 4) insert assignment substack before arg substack */
            /* we have inserted a token but i still points to the old assignment token position */
            estack_insert(out, i - var_substack_len - arg_substack_len + 1, var_substack_len,
                          estack_peek(out, i - var_substack_len + 2));

            /* 5) convert to var/tt tokens */
            /* we have inserted a token but i still points to the old assignment token position */
            for (j = 0; j < var_substack_len; j++) {
                etoken a = estack_peek(out, i - arg_substack_len - j);
                if (TOK_ASSIGN == a->toktype)
                    a->toktype = TOK_VAR;
                else if (TOK_ASSIGN_TT == a->toktype)
                    a->toktype = TOK_TT;
            }

            /* 6) if there are more than 1 assignment tokens we also need a TOK_VECTORIZE */
            if (assign_len > 1) {
                newtok.toktype = TOK_VECTORIZE;
                newtok.fn.arity = assign_len;
                estack_insert(out, i - arg_substack_len + 1, 1, &newtok);
            }

            /* start scan again */
            i = 0;
        }
        if (TOK_OP == t->toktype && (t->op.idx >= OP_INCREMENT_PRE && t->op.idx <= OP_DECREMENT_POST)) {
            int substack_len,
                inc = t->op.idx == OP_INCREMENT_PRE || t->op.idx == OP_INCREMENT_POST,
                pre = t->op.idx == OP_INCREMENT_PRE || t->op.idx == OP_DECREMENT_PRE,
                var_idx = i - 1,
                flags = t->gen.flags;
#if TRACE_PARSE
            printf("expanding %sfix %screment operator\n", pre ? "pre" : "post", inc ? "in" : "de");
#endif
            etoken_t newtok, *vartok = estack_peek(out, var_idx);
            {FAIL_IF(TOK_VAR != vartok->toktype, "misplaced increment or decrement operator");}
            {FAIL_IF(vartok->var.idx >= VAR_X_NEWEST, "cannot assign to variable 'x'");}
            {FAIL_IF(vartok->gen.flags & VAR_HIST_IDX, "cannot assign to historical value");}
            substack_len = estack_get_substack_len(out, var_idx);

            /* copy datatype from variable */
            newtok.gen.datatype = t->gen.datatype;
            newtok.gen.casttype = 0;
            newtok.gen.vec_len = t->gen.vec_len;
            newtok.gen.flags = 0;

            /* convert OP to literal 1 */
            t->toktype = TOK_LITERAL;
            t->gen.vec_len = 1;
            t->gen.flags = VEC_LEN_LOCKED;
            switch(t->gen.datatype) {
                case MPR_INT32: t->lit.val.i = 1; break;
                case MPR_FLT:   t->lit.val.f = 1; break;
                case MPR_DBL:   t->lit.val.d = 1; break;
            }

            /* The postfix increment/decrement operators require copying the unaltered variable
             * value to the evaluation stack before incrementing/decrementing so it can be used
             * in further calculations. An exception is made if the variable being incremented
             * is not otherwise assigned, e.g. "a++;" */
            if (!pre) {
                /* check if the variable is assigned */
                int top = out->num_tokens - 1, copy_tokens = 1;
                while (top > 0) {
                    top -= estack_get_substack_len(out, top);
                    if (top == i) {
                        copy_tokens = 0;
                        break;
                    }
                    if (top < i)
                        break;
                }
                if (copy_tokens) {
                    /* need to insert copy token after variable */
                    /* newtok.toktype = TOK_COPY_FROM;
                     * newtok.con.cache_offset = 0;
                     * estack_insert(out, i++, 1, &newtok);
                     */

                    /* above code does not guarantee that variable token will not be cast to another
                     * datatype before copy */
                    /* for now we will copy variable substack instead of using copy */
                    estack_insert(out, i, substack_len, estack_peek(out, var_idx - substack_len + 1));
                    i += substack_len;
                }
            }

            /* add operator */
            newtok.toktype = TOK_OP;
            newtok.op.idx = inc ? OP_ADD : OP_SUBTRACT;
            newtok.gen.vec_len = 1;
            estack_insert(out, ++i, 1, &newtok);

            /* add var substack and convert to assign_use */
            t = estack_insert(out, ++i, substack_len, estack_peek(out, var_idx - substack_len + 1));
            {FAIL_IF(t->toktype != TOK_VAR, "error!");}
            t->toktype = pre ? TOK_ASSIGN_USE : TOK_ASSIGN;
            t->gen.flags |= flags;
            if (t->var.idx < VAR_Y) {
                vars[t->var.idx].flags |= VAR_ASSIGNED;
            }
            /* start scan again */
            i = 0;
        }
    }

#if TRACE_PARSE
        estack_print("OUTPUT STACK", out, vars, 0);
        estack_print("OPERATOR STACK", op, vars, 0);
#endif

    /* run checks on VAR_NOW and VAR_NEXT */
    for (i = 0; i < out->num_tokens; i++) {
        etoken t = estack_peek(out, i);
        {FAIL_IF(   (TOK_VAR == t->toktype || TOK_ASSIGN == t->toktype)
                 && (VAR_NOW == t->var.idx || VAR_NEXT == t->var.idx),
                 "Illegal variable index.");}
        {FAIL_IF(TOK_TT == t->toktype && VAR_NOW == t->var.idx && NUM_VAR_IDXS(t->gen.flags),
                 "'now' timestamp does not accept indices.");}
        {FAIL_IF(TOK_ASSIGN_TT == t->toktype && VAR_NOW == t->var.idx,
                 "Cannot assign to 'now' timestamp.");}
    }

    /* check that all used-defined variables were assigned */
    for (i = 0; i < num_var; i++) {
        {FAIL_IF(!(vars[i].flags & VAR_ASSIGNED), "User-defined variable not assigned.");}
    }

    /* check vector length and type */
    {FAIL_IF(estack_check_assign_type_and_len(out, vars) == -1, "Malformed expression (27).");}

    /* replace special constants with their typed values */
    {FAIL_IF(estack_replace_special_constants(out), "Error replacing special constants."); }

    estack_find_init_offset(out);

#if TRACE_PARSE
    estack_print("OUTPUT STACK", out, vars, 0);
    estack_print("OPERATOR STACK", op, vars, 0);
#endif

    /* free the operator stack */
    estack_free(op, 1);

    /* copy user-defined variables and the output stack to parent mpr_expr and free stack */
    mpr_expr_cpy_stack_and_vars(expr, out, vars, num_var);
    estack_free(out, 0);

    return 0;

error:
    estack_free(out, 1);
    estack_free(op, 1);
    while (--num_var >= 0)
        expr_var_free(&vars[num_var]);
    while (temp_vars) {
        temp_var_cache tmp = temp_vars->next;
        free((char*)temp_vars->in_name);
        free((char*)temp_vars->accum_name);
        free(temp_vars);
        temp_vars = tmp;
    }
    return 1;
}

#endif /* __MPR_EXPR_PARSER_H__ */
