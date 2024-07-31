#ifndef __MPR_EXPRESSION_TOKEN_H__
#define __MPR_EXPRESSION_TOKEN_H__

#include "expression_constant.h"
#include "expression_function.h"
#include "expression_trace.h"
#include "expression_variable.h"

#define TOKEN_SIZE sizeof(mpr_token_t)

#define CLEAR_STACK     0x0010
#define TYPE_LOCKED     0x0020
#define VAR_MUTED       0x0040
#define USE_VAR_LEN     0x0040 /* reuse */
#define VEC_LEN_LOCKED  0x0080

enum token_type {
    TOK_UNKNOWN         = 0x0000000,
    TOK_LITERAL         = 0x0000001,    /* Scalar literal */
    TOK_VLITERAL        = 0x0000002,    /* Vector literal */
    TOK_NEGATE          = 0x0000004,
    TOK_FN              = 0x0000008,    /* Function */
    TOK_VFN             = 0x0000010,    /* Vector function */
    TOK_VFN_DOT         = 0x0000020,    /* Dot vector function */
    TOK_RFN             = 0x0000040,    /* Reduce function */
    TOK_OPEN_PAREN      = 0x0000080,
    TOK_MUTED           = 0x0000100,
    TOK_OPEN_SQUARE     = 0x0000200,
    TOK_OPEN_CURLY      = 0x0000400,
    TOK_CLOSE_PAREN     = 0x0000800,
    TOK_CLOSE_SQUARE    = 0x0001000,
    TOK_CLOSE_CURLY     = 0x0002000,
    TOK_VAR             = 0x0004000,
    TOK_VAR_NUM_INST    = 0x0008000,
    TOK_DOLLAR          = 0x0010000,
    TOK_OP              = 0x0020000,
    TOK_COMMA           = 0x0040000,
    TOK_COLON           = 0x0080000,
    TOK_SEMICOLON       = 0x0100000,
    TOK_VECTORIZE       = 0x0200000,
    TOK_TT              = 0x0400000,    /* NTP Timestamp */
    TOK_ASSIGN          = 0x0800000,
    TOK_ASSIGN_USE,
    TOK_ASSIGN_CONST,                   /* Const assignment (does not require input) */
    TOK_ASSIGN_TT,                      /* Assign to NTP timestamp */
    TOK_COPY_FROM       = 0x1000000,    /* Copy from stack */
    TOK_MOVE,                           /* Move stack */
    TOK_LAMBDA,
    TOK_LOOP_START,
    TOK_LOOP_END,
    TOK_SP_ADD,                         /* Stack pointer offset */
    TOK_REDUCING,
    TOK_END             = 0x2000000
};

struct generic_type {
    enum token_type toktype;
    mpr_type datatype;
    mpr_type casttype;
    uint8_t vec_len;
    uint8_t flags;
};

struct literal_type {
    enum token_type toktype;
    mpr_type datatype;
    mpr_type casttype;
    uint8_t vec_len;
    uint8_t flags;
    /* end of generic_type */
    union {
        float f;
        int i;
        double d;
        float *fp;
        int *ip;
        double *dp;
    } val;
};

struct operator_type {
    enum token_type toktype;
    mpr_type datatype;
    mpr_type casttype;
    uint8_t vec_len;
    uint8_t flags;
    /* end of generic_type */
    expr_op_t idx;
};

struct variable_type {
    enum token_type toktype;
    mpr_type datatype;
    mpr_type casttype;
    uint8_t vec_len;
    uint8_t flags;
    /* end of generic_type */
    int8_t idx;
    uint8_t offset;         /* only used by TOK_ASSIGN* and TOK_COPY_FROM */
    uint8_t vec_idx;        /* only used by TOK_VAR and TOK_ASSIGN */
};

struct function_type {
    enum token_type toktype;
    mpr_type datatype;
    mpr_type casttype;
    uint8_t vec_len;
    uint8_t flags;
    /* end of generic_type */
    int8_t idx;
    uint8_t arity;          /* used by TOK_FN, TOK_VFN, TOK_VECTORIZE */
};

struct control_type {
    enum token_type toktype;
    mpr_type datatype;
    mpr_type casttype;
    uint8_t vec_len;
    uint8_t flags;
    /* end of generic_type */
    int8_t cache_offset;
    uint8_t reduce_start;
    uint8_t reduce_stop;
    uint8_t branch_offset;
};

typedef union _token {
    enum token_type toktype;
    struct generic_type gen;
    struct literal_type lit;
    struct operator_type op;
    struct variable_type var;
    struct function_type fn;
    struct control_type con;
} mpr_token_t, *mpr_token;

static int const_tok_is_zero(mpr_token_t tok)
{
    switch (tok.gen.datatype) {
        case MPR_INT32:     return tok.lit.val.i == 0;
        case MPR_FLT:       return tok.lit.val.f == 0.f;
        case MPR_DBL:       return tok.lit.val.d == 0.0;
    }
    return 0;
}

static int const_tok_equals_one(mpr_token_t tok)
{
    switch (tok.gen.datatype) {
        case MPR_INT32:     return tok.lit.val.i == 1;
        case MPR_FLT:       return tok.lit.val.f == 1.f;
        case MPR_DBL:       return tok.lit.val.d == 1.0;
    }
    return 0;
}

static int tok_arity(mpr_token_t tok)
{
    switch (tok.toktype) {
        case TOK_VAR:
        case TOK_TT:
        case TOK_ASSIGN:
        case TOK_ASSIGN_CONST:
        case TOK_ASSIGN_USE:
        case TOK_ASSIGN_TT:     return NUM_VAR_IDXS(tok.gen.flags);
        case TOK_OP:            return op_tbl[tok.op.idx].arity;
        case TOK_FN:            return fn_tbl[tok.fn.idx].arity;
        case TOK_RFN:           return rfn_tbl[tok.fn.idx].arity;
        case TOK_VFN:           return vfn_tbl[tok.fn.idx].arity;
        case TOK_VECTORIZE:     return tok.fn.arity;
        case TOK_MOVE:          return tok.con.cache_offset + 1;
        case TOK_SP_ADD:        return -tok.lit.val.i;
        case TOK_LOOP_START:    return tok.con.flags & RT_INSTANCE ? 1 : 0;
        default:                return 0;
    }
    return 0;
}

#define SET_TOK_OPTYPE(TYPE)    \
    tok->toktype = TOK_OP;      \
    tok->op.idx = TYPE;

#if TRACE_PARSE || TRACE_EVAL
static void print_token(mpr_token_t *t, mpr_var_t *vars, int show_locks)
{
    int i, d = 0, l = 128;
    char s[128];
    char *dims[] = {"unknown", "history", "instance", 0, "signal", 0, 0, 0, "vector"};
    switch (t->toktype) {
        case TOK_LITERAL:
            d = snprintf(s, l, "LITERAL\t");
            switch (t->gen.flags & CONST_SPECIAL) {
                case CONST_MAXVAL:  snprintf(s + d, l - d, "max");                  break;
                case CONST_MINVAL:  snprintf(s + d, l - d, "min");                  break;
                case CONST_PI:      snprintf(s + d, l - d, "pi");                   break;
                case CONST_E:       snprintf(s + d, l - d, "e");                    break;
                default:
                    switch (t->gen.datatype) {
                        case MPR_FLT:   snprintf(s + d, l - d, "%g", t->lit.val.f); break;
                        case MPR_DBL:   snprintf(s + d, l - d, "%g", t->lit.val.d); break;
                        case MPR_INT32: snprintf(s + d, l - d, "%d", t->lit.val.i); break;
                    }                                                               break;
            }
                                                                                    break;
        case TOK_VLITERAL:
            d = snprintf(s, l, "VLITERAL\t[");
            switch (t->gen.datatype) {
                case MPR_FLT:
                    for (i = 0; i < t->lit.vec_len; i++)
                        d += snprintf(s + d, l - d, "%g,", t->lit.val.fp[i]);
                    break;
                case MPR_DBL:
                    for (i = 0; i < t->lit.vec_len; i++)
                        d += snprintf(s + d, l - d, "%g,", t->lit.val.dp[i]);
                    break;
                case MPR_INT32:
                    for (i = 0; i < t->lit.vec_len; i++)
                        d += snprintf(s + d, l - d, "%d,", t->lit.val.ip[i]);
                    break;
            }
            --d;
            snprintf(s + d, l - d, "]");
            break;
        case TOK_OP:            snprintf(s, l, "OP\t\t%s", op_tbl[t->op.idx].name); break;
        case TOK_OPEN_CURLY:    snprintf(s, l, "{\t");                              break;
        case TOK_OPEN_PAREN:    snprintf(s, l, "(\t\tarity %d", t->fn.arity);       break;
        case TOK_OPEN_SQUARE:   snprintf(s, l, "[");                                break;
        case TOK_CLOSE_CURLY:   snprintf(s, l, "}");                                break;
        case TOK_CLOSE_PAREN:   snprintf(s, l, ")");                                break;
        case TOK_CLOSE_SQUARE:  snprintf(s, l, "]");                                break;

        case TOK_ASSIGN:
        case TOK_ASSIGN_CONST:
        case TOK_ASSIGN_USE:
        case TOK_ASSIGN_TT:
            d = snprintf(s, l, "ASSIGN\t");
        case TOK_VAR:
        case TOK_TT: {
            if (TOK_VAR == t->toktype || TOK_TT == t->toktype)
                d += snprintf(s + d, l - d, "LOAD\t");
            if (TOK_TT == t->toktype || TOK_ASSIGN_TT == t->toktype)
                d += snprintf(s + d, l - d, "tt");
            else
                d += snprintf(s + d, l - d, "var");


            if (t->var.idx == VAR_Y)
                d += snprintf(s + d, l - d, ".y");
            else if (t->var.idx == VAR_X_NEWEST)
                d += snprintf(s + d, l - d, ".x$$");
            else if (t->var.idx >= VAR_X) {
                d += snprintf(s + d, l - d, ".x");
                if (t->gen.flags & VAR_SIG_IDX)
                    d += snprintf(s + d, l - d, "$N");
                else
                    d += snprintf(s + d, l - d, "$%d", t->var.idx - VAR_X);
            }
            else
                d += snprintf(s + d, l - d, "%d%c.%s%s", t->var.idx,
                              vars ? vars[t->var.idx].datatype : '?',
                              vars ? vars[t->var.idx].name : "?",
                              vars ? (vars[t->var.idx].flags & VAR_INSTANCED) ? ".N" : ".0" : ".?");

            if (t->gen.flags & VAR_HIST_IDX)
                d += snprintf(s + d, l - d, "{N}");

            if (TOK_TT == t->toktype)
                break;

            if (t->gen.flags & VAR_VEC_IDX)
                d += snprintf(s + d, l - d, "[N");
            else
                d += snprintf(s + d, l - d, "[%u", t->var.vec_idx);
            if (t->var.idx >= VAR_Y)
                d += snprintf(s + d, l - d, "]");
            else
                d += snprintf(s + d, l - d, "/%u]", vars ? vars[t->var.idx].vec_len : 0);

            if (t->toktype & TOK_ASSIGN)
                snprintf(s + d, l - d, "<%d>", t->var.offset);
            break;
        }
        case TOK_VAR_NUM_INST:
            if (t->var.idx == VAR_Y)
                snprintf(s, l, "NUM_INST\tvar.y");
            else if (t->var.idx == VAR_X_NEWEST)
                snprintf(s, l, "NUM_INST\tvar.x$$");
            else if (t->var.idx >= VAR_X)
                snprintf(s, l, "NUM_INST\tvar.x$%d", t->var.idx - VAR_X);
            else
                snprintf(s, l, "NUM_INST\tvar.%s%s", vars ? vars[t->var.idx].name : "?",
                         vars ? (vars[t->var.idx].flags & VAR_INSTANCED) ? ".N" : ".0" : ".?");
            break;
        case TOK_FN:        snprintf(s, l, "FN\t\t%s()", fn_tbl[t->fn.idx].name);   break;
        case TOK_COMMA:     snprintf(s, l, ",");                                    break;
        case TOK_COLON:     snprintf(s, l, ":");                                    break;
        case TOK_VECTORIZE: snprintf(s, l, "VECT(%d)", t->fn.arity);                break;
        case TOK_NEGATE:    snprintf(s, l, "-");                                    break;
        case TOK_VFN:
        case TOK_VFN_DOT:   snprintf(s, l, "VFN\t%s()", vfn_tbl[t->fn.idx].name);   break;
        case TOK_RFN:
            if (RFN_HISTORY == t->fn.idx)
                snprintf(s, l, "RFN\thistory(%d:%d)", t->con.reduce_start, t->con.reduce_stop);
            else if (RFN_VECTOR == t->fn.idx) {
                if (t->con.flags & USE_VAR_LEN)
                    snprintf(s, l, "RFN\tvector(%d:len)", t->con.reduce_start);
                else
                    snprintf(s, l, "RFN\tvector(%d:%d)", t->con.reduce_start, t->con.reduce_stop);
            }
            else
                snprintf(s, l, "RFN\t%s()", rfn_tbl[t->fn.idx].name);
            break;
        case TOK_LAMBDA:
            snprintf(s, l, "LAMBDA");                                               break;
        case TOK_COPY_FROM:
            snprintf(s, l, "COPY\t%d", t->con.cache_offset * -1);                   break;
        case TOK_MOVE:
            snprintf(s, l, "MOVE\t-%d", t->con.cache_offset);                       break;
        case TOK_LOOP_START:
            snprintf(s, l, "LOOP_START\t%s", dims[t->con.flags & REDUCE_TYPE_MASK]); break;
        case TOK_REDUCING:
            if (t->con.flags & USE_VAR_LEN)
                snprintf(s, l, "REDUCE\t%s[%d:len]", dims[t->con.flags & REDUCE_TYPE_MASK],
                         t->con.reduce_start);
            else
                snprintf(s, l, "REDUCE\t%s[%d:%d]", dims[t->con.flags & REDUCE_TYPE_MASK],
                         t->con.reduce_start, t->con.reduce_stop);
            break;
        case TOK_LOOP_END:
            switch (t->con.flags & REDUCE_TYPE_MASK) {
                case RT_HISTORY:
                    snprintf(s, l, "LOOP_END\thistory[%d:%d]<%d,%d>", -t->con.reduce_start,
                             -t->con.reduce_stop, t->con.branch_offset, t->con.cache_offset);
                    break;
                case RT_VECTOR:
                    if (t->con.flags & USE_VAR_LEN)
                        snprintf(s, l, "LOOP_END\tvector[%d:len]<%d,%d>", t->con.reduce_start,
                                 t->con.branch_offset, t->con.cache_offset);
                    else
                        snprintf(s, l, "LOOP_END\tvector[%d:%d]<%d,%d>", t->con.reduce_start,
                                 t->con.reduce_stop, t->con.branch_offset, t->con.cache_offset);
                    break;
                default:
                    snprintf(s, l, "LOOP_END\t%s<%d,%d>", dims[t->con.flags & REDUCE_TYPE_MASK],
                             t->con.branch_offset, t->con.cache_offset);
            }
            break;
        case TOK_SP_ADD:            snprintf(s, l, "SP_ADD\t%d", t->lit.val.i);     break;
        case TOK_SEMICOLON:         snprintf(s, l, "semicolon");                    break;
        case TOK_END:               printf("END\n");                                return;
        default:                    printf("(unknown token)\n");                    return;
    }
    printf("%s", s);
    /* indent */
    l = strlen(s);
    if (show_locks) {
        printf("\r\t\t\t\t\t%c%u", t->gen.datatype, t->gen.vec_len);
        if (t->gen.casttype)
            printf("->%c", t->gen.casttype);
        else
            printf("   ");
        if (t->gen.flags & VEC_LEN_LOCKED)
            printf(" vlock");
        if (t->gen.flags & TYPE_LOCKED)
            printf(" tlock");
        if (TOK_ASSIGN & t->toktype && t->gen.flags & CLEAR_STACK)
            printf(" clear");
    }
}
#endif /* TRACE_PARSE || TRACE_EVAL */

static mpr_type compare_token_datatype(mpr_token_t tok, mpr_type type)
{
    mpr_type type2 = tok.gen.casttype ? tok.gen.casttype : tok.gen.datatype;
    if (tok.toktype >= TOK_LOOP_START)
        return type;
    /* return the higher datatype, 'd' < 'f' < i' */
    return type < type2 ? type : type2;
}

#endif /* __MPR_EXPRESSION_TOKEN_H__ */
