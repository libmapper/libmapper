#ifndef __MPR_EXPR_PARSER_H__
#define __MPR_EXPR_PARSER_H__

#include <assert.h>
#include "expr_lexer.h"
#include "expr_stack.h"

#define STACK_SIZE 64

/* Macros to help express stack operations in parser. */
#define FAIL(msg) {     \
    trace("%s\n", msg); \
    goto error;         \
}

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
                       | TOK_OPEN_PAREN | TOK_OPEN_SQUARE | TOK_OP | TOK_TT)
#define JOIN_TOKENS (TOK_OP | TOK_CLOSE_PAREN | TOK_CLOSE_SQUARE | TOK_CLOSE_CURLY | TOK_COMMA \
                     | TOK_COLON | TOK_SEMICOLON)

/*! Use Dijkstra's shunting-yard algorithm to parse expression into RPN stack. */
int expr_parser_build_stack(mpr_expr expr, const char *str,
                            int num_src, const mpr_type *src_types, const unsigned int *src_lens,
                            int num_dst, const mpr_type *dst_types, const unsigned int *dst_lens)
{
    estack out = estack_new(STACK_SIZE), op = estack_new(STACK_SIZE);
    expr_var_t vars[N_USER_VARS];
    int i, lex_idx = 0;

    /* TODO: use bitflags instead? */
    uint8_t assigning = 0, is_const = 1, out_assigned = 0, muted = 0, vectorizing = 0;
    uint8_t lambda_allowed = 0, reduce_types = 0;
    int var_flags = 0;
    int allow_toktype = 0x2FFFFF;
    int vec_len_ctx = 0;

    temp_var_cache temp_vars = NULL;
    /* TODO: optimise these vars */
    int num_var = 0;
    etoken_t tok;
    mpr_type type_hi = MPR_INT32, type_lo = MPR_DBL;

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
        if (src_lens[i] > out->vec_len)
            out->vec_len = src_lens[i];
    }

#if TRACE_PARSE
    printf("parsing expression '%s'\n", str);
#endif

    while (str[lex_idx]) {
        GET_NEXT_TOKEN(tok);
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
#if TRACE_PARSE
            printf("Illegal token sequence (2)\n");
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

                if (tok.var.idx == VAR_X_NEWEST) {
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
                    i = expr_var_find_by_name(vars, num_var, varname, len);
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
                estack_push(out, &tok);

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
                etoken_t newtok;
                tok.gen.datatype = fn_tbl[tok.fn.idx].fn_int ? MPR_INT32 : MPR_FLT;
                tok.fn.arity = fn_tbl[tok.fn.idx].arity;
                if (fn_tbl[tok.fn.idx].memory) {
                    /* add assignment token */
                    char varname[7];
                    uint8_t varidx = num_var;
                    {FAIL_IF(num_var >= N_USER_VARS, "Maximum number of variables exceeded.");}
                    do {
                        snprintf(varname, 7, "var%d", varidx++);
                    } while (expr_var_find_by_name(vars, num_var, varname, 7) >= 0);
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
                    estack_push(op, &newtok);
                }
                estack_push(op, &tok);
                if (fn_tbl[tok.fn.idx].arity)
                    allow_toktype = TOK_OPEN_PAREN;
                else {
                    estack_push(out, estack_pop(op));
                    estack_check_type(out, vars, 1);
                    allow_toktype = JOIN_TOKENS;
                }
                if (tok.fn.idx >= FN_DEL_IDX)
                    is_const = 0;
                if (fn_tbl[tok.fn.idx].memory) {
                    newtok.toktype = TOK_VAR;
                    newtok.gen.flags = 0;
                    estack_push(out, &newtok);
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
                estack_push(op, &tok);
                allow_toktype = TOK_OPEN_PAREN;
                break;
            case TOK_VFN_DOT: {
                etoken t = estack_peek(op, ESTACK_TOP);
                if (t->toktype != TOK_RFN || t->fn.idx < RFN_HISTORY) {
                    tok.toktype = TOK_VFN;
                    tok.gen.datatype = vfn_tbl[tok.fn.idx].fn_int ? MPR_INT32 : MPR_FLT;
                    tok.fn.arity = vfn_tbl[tok.fn.idx].arity;
                    tok.gen.vec_len = 1;
                    estack_push(op, &tok);
                    if (tok.fn.arity > 1) {
                        tok.toktype = TOK_OPEN_PAREN;
                        tok.fn.arity = 2;
                        estack_push(op, &tok);
                        allow_toktype = OBJECT_TOKENS;
                    }
                    else {
                        estack_push(out, estack_pop(op));
                        estack_check_type(out, vars, 1);
                        allow_toktype = JOIN_TOKENS | TOK_RFN;
                    }
                    break;
                }
            }
                /* omit break and continue to case TOK_RFN */
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
                            {FAIL_IF(tok.toktype != TOK_OPEN_PAREN, "missing open parenthesis. (1)");}
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
                                if (t->toktype != TOK_VAR)
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
                                etoken t;
                                if (TOK_VAR != (estack_peek(out, ESTACK_TOP - i))->toktype)
                                    continue;
                                t = estack_peek(op, ESTACK_TOP);
                                mpr_expr_update_mlen(expr, tok.var.idx, t->con.reduce_start);
                            }
                            GET_NEXT_TOKEN(tok);
                            {FAIL_IF(tok.toktype != TOK_CLOSE_PAREN, "missing close parenthesis. (1)");}
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
                                if (t->toktype != TOK_VAR || t->var.idx < VAR_Y)
                                    continue;
                                {FAIL_IF(t->var.idx == VAR_Y,
                                         "Cannot call signal reduce function on output.");}
                                {FAIL_IF(t->var.idx > VAR_X,
                                         "Signal indexes not allowed within signal reduce function.");}
                                if (VAR_X == t->var.idx) {
                                    x_ref = 1;
                                    /* promote vector length */
                                    t->var.vec_len = out->vec_len;
                                }
                            }
                            {FAIL_IF(!x_ref, "signal reduce requires reference to input 'x'.");}
                            if (type_hi == type_lo) /* homogeneous types, no casting necessary */
                                break;
                            t = estack_peek(out, ESTACK_TOP);
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
                            estack_check_type(out, vars, 0);
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
                        break;
                    }
                }
                else if (RFN_NEWEST == rfn) {
                    etoken t = estack_peek(out, ESTACK_TOP);
                    {FAIL_IF(rt != RT_SIGNAL, "newest() requires 'signal' prefix'");}
                    t->toktype = TOK_VAR;
                    t->var.idx = VAR_X_NEWEST;
                    t->gen.datatype = type_lo;
                    t->gen.casttype = type_hi;
                    t->gen.vec_len = out->vec_len;
                    t->gen.flags |= (TYPE_LOCKED | VEC_LEN_LOCKED);
                    is_const = 0;
                    allow_toktype = JOIN_TOKENS;
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
                        etoken_set_int32(&tok, 0);
                        tok.lit.vec_len = 1;
                        estack_push(out, &tok);

                        /* Restack reduce input */
                        ++sslen;
                        for (i = 0; i < sslen; i++) {
                            etoken out_top = estack_push(out, estack_pop(op));
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
                        estack_push_int(out, RFN_ALL == rfn, 1, 0);
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
                    estack_check_type(out, vars, 1);
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
                    estack_check_type(out, vars, 0);
                }
                else if (RFN_MEAN == rfn) {
                    etoken_set_op(&tok, OP_DIVIDE);
                    estack_push(out, &tok);
                    estack_check_type(out, vars, 0);
                }
                else if (RFN_SIZE == rfn) {
                    etoken_set_op(&tok, OP_SUBTRACT);
                    estack_push(out, &tok);
                    estack_check_type(out, vars, 0);
                }
                else if (RFN_CONCAT == rfn) {
                    tok.toktype = TOK_SP_ADD;
                    tok.lit.val.i = -1;
                    estack_push(out, &tok);
                }
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
                if (TOK_FN == t->toktype && fn_tbl[t->fn.idx].memory)
                    tok.fn.arity = 2;
                else
                    tok.fn.arity = 1;
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
                    estack_check_type(out, vars, 1);
                }
                {FAIL_IF(!op->num_tokens, "Unmatched parentheses, brackets, or misplaced comma. (1)");}

                if (TOK_VECTORIZE == op_top->toktype) {
                    op_top->gen.flags |= VEC_LEN_LOCKED;
                    ADD_TO_VECTOR();
                    estack_lock_vec_len(out);
                    if (op_top->fn.arity > 1) {
                        estack_push(out, estack_pop(op));
                        estack_check_type(out, vars, 1);
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
                if (TOK_FN == estack_peek(op, ESTACK_TOP)->toktype) {
                    if (FN_SIG_IDX == estack_peek(op, ESTACK_TOP)->fn.idx) {
                        etoken top = estack_peek(out, ESTACK_TOP);
                        etoken top_m1 = estack_peek(out, ESTACK_TOP - 1);
                        {FAIL_IF(TOK_VAR != top->toktype || VAR_X != top->var.idx,
                                 "Signal index used on incompatible token.");}
                        if (TOK_LITERAL == top_m1->toktype) {
                            /* Optimize by storing signal idx in variable token */
                            int sig_idx;
                            {FAIL_IF(MPR_INT32 != top_m1->gen.datatype,
                                     "Signal index must be an integer.");}
                            sig_idx = top_m1->lit.val.i % num_src;
                            if (sig_idx < 0)
                                sig_idx += num_src;
                            top->var.idx = sig_idx + VAR_X;
                            top->gen.flags &= ~VAR_SIG_IDX;
                            estack_cpy_tok(out, ESTACK_TOP - 1, ESTACK_TOP);
                            estack_pop(out);
                        }
                        else if (type_hi != type_lo) {
                            /* heterogeneous types, need to cast */
                            top->var.datatype = type_lo;
                            top->var.casttype = type_hi;
                        }
                        estack_pop(op);

                        /* signal indices must be integers */
                        if (top_m1->gen.datatype != MPR_INT32)
                            top_m1->gen.casttype = MPR_INT32;

                        /* signal index set */
                        /* recreate var_flags from variable token */
                        var_flags = top->gen.flags & VAR_IDXS;
                        if (!(top->gen.flags & VAR_VEC_IDX) && !top->var.vec_idx)
                            var_flags |= TOK_OPEN_SQUARE;
                        if (!(top->gen.flags & VAR_HIST_IDX))
                            var_flags |= TOK_OPEN_CURLY;
                        allow_toktype |= (var_flags & ~VAR_IDXS);
                    }
                    else if (FN_DEL_IDX == (estack_peek(op, ESTACK_TOP))->fn.idx) {
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
                        /* recreate var_flags from variable token */
                        t = estack_peek(out, ESTACK_TOP);
                        var_flags = t->gen.flags & VAR_IDXS;
                        if (!(t->gen.flags & VAR_SIG_IDX) && VAR_X == t->var.idx)
                            var_flags |= TOK_DOLLAR;
                        if (!(t->gen.flags & VAR_VEC_IDX) && !t->var.vec_idx)
                            var_flags |= TOK_OPEN_SQUARE;

                        allow_toktype |= (var_flags & ~VAR_IDXS);
                    }
                    else if (FN_VEC_IDX == (estack_peek(op, ESTACK_TOP))->fn.idx) {
                        etoken top = estack_peek(out, ESTACK_TOP);
                        etoken top_m1 = estack_peek(out, ESTACK_TOP - 1);

                        {FAIL_IF(arity != 1, "vector index arity != 1.");}
                        {FAIL_IF(top->toktype != TOK_VAR, "Missing variable for vector indexing");}

                        top->gen.flags |= VAR_VEC_IDX;
                        estack_pop(op);
                        if (TOK_LITERAL == top_m1->toktype) {
                            if (   TOK_VAR == top->gen.toktype
                                && (top->var.idx >= VAR_Y || vars[top->var.idx].vec_len)
                                && MPR_INT32 == top_m1->gen.datatype) {
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
                        /* also set var vec_len to 1 */
                        /* TODO: consider vector indices */
                        top->var.vec_len = 1;

                        /* vector index set */
                        /* recreate var_flags from variable token */
                        var_flags = top->gen.flags & VAR_IDXS;
                        if (!(top->gen.flags & VAR_SIG_IDX) && VAR_X == top->var.idx)
                            var_flags |= TOK_DOLLAR;
                        if (!(top->gen.flags & VAR_HIST_IDX))
                            var_flags |= TOK_OPEN_CURLY;

                        allow_toktype |= (var_flags & ~VAR_IDXS);
                        break;
                    }
                    else {
                        etoken top = estack_peek(op, ESTACK_TOP);
                        if (arity != fn_tbl[top->fn.idx].arity) {
                            /* check for overloaded functions */
                            if (arity != 1)
                                {FAIL("Function arity mismatch.");}
                            if (top->fn.idx == FN_MIN) {
                                top->toktype = TOK_VFN;
                                top->fn.idx = VFN_MIN;
                            }
                            else if (top->fn.idx == FN_MAX) {
                                top->toktype = TOK_VFN;
                                top->fn.idx = VFN_MAX;
                            }
                            else
                                {FAIL("Function arity mismatch.");}
                        }
                        estack_push(out, estack_pop(op));
                        estack_check_type(out, vars, 1);
                    }

                }
                else if (TOK_VFN == (estack_peek(op, ESTACK_TOP))->toktype) {
                    /* check arity */
                    etoken t = estack_pop(op);
                    {FAIL_IF(arity != vfn_tbl[t->fn.idx].arity, "VFN arity mismatch.");}
                    estack_push(out, t);
                    estack_check_type(out, vars, 1);
                }
                else if (TOK_REDUCING == (estack_peek(op, ESTACK_TOP))->toktype) {
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
                    estack_check_type(out, vars, 1);
                }
                break;
            }
            case TOK_COMMA: {
                etoken t;
                /* pop from operator stack to output until left parenthesis or TOK_VECTORIZE found */
                while ((t = estack_peek(op, ESTACK_TOP)) && t->toktype != TOK_OPEN_PAREN
                       && t->toktype != TOK_VECTORIZE) {
                    estack_push(out, estack_pop(op));
                    estack_check_type(out, vars, 1);
                }
                {FAIL_IF(!(t = estack_peek(op, ESTACK_TOP)), "Malformed expression (7).");}
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
                    estack_check_type(out, vars, 1);
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

                    /* Check if left index is an integer */
                    out_top = estack_peek(out, ESTACK_TOP);
                    {FAIL_IF(TOK_LITERAL != out_top->toktype || MPR_INT32 != out_top->gen.datatype,
                             "Non-integer left vector index used with colon.");}
                    op_top->var.vec_idx = out_top->lit.val.i;
                    op_top->var.vec_idx = out_top->lit.val.i;
                    estack_pop(out);
                    estack_push(out, estack_pop(op));
                    out_top = estack_check_type(out, vars, 1);

                    /* Get right index and verify that it is an integer */
                    GET_NEXT_TOKEN(tok);
                    {FAIL_IF(tok.toktype != TOK_LITERAL || tok.gen.datatype != MPR_INT32,
                             "Non-integer right vector index used with colon.");}
                    out_top->var.vec_len = tok.lit.val.i - out_top->var.vec_idx + 1;
                    if (tok.lit.val.i < out_top->var.vec_idx)
                        out_top->var.vec_len += vec_len_ctx;
                    GET_NEXT_TOKEN(tok);
                    {FAIL_IF(tok.toktype != TOK_CLOSE_SQUARE, "Unmatched bracket.");}
                    /* vector index set */
                    var_flags &= ~VAR_VEC_IDX;
                    allow_toktype = (JOIN_TOKENS | TOK_VFN_DOT | TOK_RFN | (var_flags & ~VAR_IDXS));
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
                etoken op_top;
                /* finish popping operators to output, check for unbalanced parentheses */
                while (   op->num_tokens
                       && (op_top = estack_peek(op, ESTACK_TOP)) && op_top->toktype < TOK_ASSIGN) {
                    if (op_top->toktype == TOK_OPEN_PAREN)
                        {FAIL("Unmatched parentheses or misplaced comma. (2)");}
                    estack_push(out, estack_pop(op));
                    estack_check_type(out, vars, 1);
                }
                {FAIL_IF(!(op_top = estack_peek(op, ESTACK_TOP)),
                         "Unmatched parentheses or misplaced comma. (3)");}
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
                    if ((op->num_tokens == 1) && op_top->toktype < TOK_ASSIGN)
                        {FAIL("Malformed expression (8)");}
                    estack_push(out, estack_pop(op));
                    if (   TOK_ASSIGN_USE == (estack_peek(out, ESTACK_TOP))->toktype
                        && estack_check_assign_type_and_len(out, vars) == -1)
                        {FAIL("Malformed expression (9)");}
                }
                /* mark last assignment token to clear eval stack */
                (estack_peek(out, ESTACK_TOP))->gen.flags |= CLEAR_STACK;

                /* check vector length and type */
                if (estack_check_assign_type_and_len(out, vars) == -1)
                    {FAIL("Malformed expression (10)");}

                /* start another sub-expression */
                assigning = is_const = 1;
                allow_toktype = TOK_VAR | TOK_TT;
                break;
            }
            case TOK_OP: {
                /* check precedence of operators on stack */
                while (op->num_tokens) {
                    etoken op_top = estack_peek(op, ESTACK_TOP);
                    if (   op_top->toktype != TOK_OP
                        || (op_tbl[op_top->op.idx].precedence < op_tbl[tok.op.idx].precedence))
                        break;
                    estack_push(out, estack_pop(op));
                    estack_check_type(out, vars, 1);
                }
                estack_push(op, &tok);
                allow_toktype = OBJECT_TOKENS & ~TOK_OP;
                if (op_tbl[tok.op.idx].arity <= 1)
                    allow_toktype &= ~TOK_NEGATE;
                break;
            }
            case TOK_DOLLAR: {
                etoken out_top = estack_peek(out, ESTACK_TOP);
                {FAIL_IF(TOK_VAR != out_top->toktype, "Signal index on non-variable type.");}
                {FAIL_IF(VAR_X != out_top->var.idx || out_top->gen.flags & VAR_SIG_IDX,
                         "Signal index on non-input type or index already set.");}

                out_top->gen.flags |= VAR_SIG_IDX;

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

                var_flags = (var_flags & ~TOK_DOLLAR) | VAR_SIG_IDX;
                allow_toktype = OBJECT_TOKENS;
                break;
            }
            case TOK_OPEN_SQUARE:
                if (var_flags & TOK_OPEN_SQUARE) { /* vector index not set */
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

                    if (t->gen.flags & VAR_SIG_IDX) {
                        /* Move sig_idx substack from output to operator */
                        for (i = estack_get_substack_len(out, ESTACK_TOP); i > 0; i--)
                            t = estack_push(op, estack_pop(out));
                    }

                    if (t->gen.flags & VAR_HIST_IDX) {
                        /* Move hist_idx substack from output to operator */
                        for (i = estack_get_substack_len(out, ESTACK_TOP); i > 0; i--)
                            estack_push(op, estack_pop(out));
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

                var_flags = (var_flags & ~TOK_OPEN_CURLY) | VAR_HIST_IDX;
                allow_toktype = OBJECT_TOKENS;
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
            case TOK_ASSIGN: {
                etoken out_top = estack_peek(out, ESTACK_TOP);
                var_flags = 0;
                /* assignment to variable */
                {FAIL_IF(!assigning, "Misplaced assignment operator.");}
                {FAIL_IF(op->num_tokens || !out->num_tokens,
                         "Malformed expression left of assignment.");}

                if (out_top->toktype == TOK_VAR) {
                    int var = out_top->var.idx;
                    if (var >= VAR_X_NEWEST)
                        {FAIL("Cannot assign to input variable 'x'.");}
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
                    out_top->toktype = is_const ? TOK_ASSIGN_CONST : TOK_ASSIGN;
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
                    {FAIL_IF(out_top->var.idx != VAR_Y, "Only output timetag is writable.");}
                    /* disable writing to current timetag for now */
                    {FAIL_IF(!(out_top->gen.flags & VAR_HIST_IDX),
                             "Only past samples of output timetag are writable.");}
                    out_top->toktype = TOK_ASSIGN_TT;
                    out_top->gen.datatype = MPR_DBL;
                    estack_push(op, estack_pop(out));
                }
                else if (out_top->toktype == TOK_VECTORIZE) {
                    int var, j, arity = out_top->fn.arity;

                    /* out token is vectorizer */
                    estack_pop(out);
                    out_top = estack_peek(out, ESTACK_TOP);
                    {FAIL_IF(out_top->toktype != TOK_VAR, "Bad token left of assignment. (1)");}
                    var = out_top->var.idx;
                    if (var >= VAR_X_NEWEST)
                        {FAIL("Cannot assign to input variable 'x'.");}
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
                        out_top->toktype = is_const ? TOK_ASSIGN_CONST : TOK_ASSIGN;
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

    /* check that all used-defined variables were assigned */
    for (i = 0; i < num_var; i++) {
        {FAIL_IF(!(vars[i].flags & VAR_ASSIGNED), "User-defined variable not assigned.");}
    }

    /* finish popping operators to output, check for unbalanced parentheses */
    while (op->num_tokens) {
        etoken op_top = estack_peek(op, ESTACK_TOP);
        if (op_top->toktype >= TOK_ASSIGN)
            break;
        {FAIL_IF(op_top->toktype == TOK_OPEN_PAREN, "Unmatched parentheses or misplaced comma. (4)");}
        estack_push(out, estack_pop(op));
        if (!estack_check_type(out, vars, 1))
            goto error;
    }

    if (op->num_tokens) {
        etoken op_top = estack_peek(op, ESTACK_TOP);
        int var_idx = op_top->var.idx;
        if (var_idx < N_USER_VARS) {
            if (!vars[var_idx].vec_len)
                vars[var_idx].vec_len = (estack_peek(out, ESTACK_TOP))->gen.vec_len;
            /* update and lock vector length of assigned variable */
            op_top->gen.vec_len = vars[var_idx].vec_len;
            op_top->gen.flags |= VEC_LEN_LOCKED;
        }
    }

    /* pop assignment operator(s) to output */
    while (op->num_tokens) {
        {FAIL_IF(op->num_tokens == 1 && (estack_peek(op, ESTACK_TOP))->toktype < TOK_ASSIGN,
                 "Malformed expression (11).");}
        estack_push(out, estack_pop(op));
        /* check vector length and type */
        {FAIL_IF(   TOK_ASSIGN_USE == (estack_peek(out, ESTACK_TOP))->toktype
                 && estack_check_assign_type_and_len(out, vars) == -1,
                 "Malformed expression (12).");}
    }

    /* mark last assignment token to clear eval stack */
    (estack_peek(out, ESTACK_TOP))->gen.flags |= CLEAR_STACK;

    /* promote unlocked variable token vector lengths */
    for (i = 0; i < out->num_tokens - 1; i++) {
        etoken t = estack_peek(out, i);
        if (TOK_VAR == t->toktype && t->var.idx < N_USER_VARS
            && !(t->gen.flags & VEC_LEN_LOCKED))
            t->gen.vec_len = vars[t->var.idx].vec_len;
    }

    /* check vector length and type */
    {FAIL_IF(estack_check_assign_type_and_len(out, vars) == -1, "Malformed expression (13).");}

    /* replace special constants with their typed values */
    {FAIL_IF(estack_replace_special_constants(out), "Error replacing special constants."); }

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
