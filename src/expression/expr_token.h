#ifndef __MPR_EXPR_TOKEN_H__
#define __MPR_EXPR_TOKEN_H__

#include <float.h>
#include <limits.h>
#include "expr_constant.h"
#include "expr_function.h"
#include "expr_trace.h"
#include "expr_variable.h"
#include "../util/mpr_inline.h"

#define TOKEN_SIZE sizeof(etoken_t)

#define CLEAR_STACK     0x0010
#define TYPE_LOCKED     0x0020
#define VAR_MUTED       0x0040
#define USE_VAR_LEN     0x0040 /* reuse */
#define VEC_LEN_LOCKED  0x0080

enum etoken_type {
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
    enum etoken_type toktype;
    mpr_type datatype;
    mpr_type casttype;
    uint8_t vec_len;
    uint8_t flags;
};

struct literal_type {
    enum etoken_type toktype;
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
    enum etoken_type toktype;
    mpr_type datatype;
    mpr_type casttype;
    uint8_t vec_len;
    uint8_t flags;
    /* end of generic_type */
    expr_op_t idx;
};

struct variable_type {
    enum etoken_type toktype;
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
    enum etoken_type toktype;
    mpr_type datatype;
    mpr_type casttype;
    uint8_t vec_len;
    uint8_t flags;
    /* end of generic_type */
    int8_t idx;
    uint8_t arity;          /* used by TOK_FN, TOK_VFN, TOK_VECTORIZE */
};

struct control_type {
    enum etoken_type toktype;
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
    enum etoken_type toktype;
    struct generic_type gen;
    struct literal_type lit;
    struct operator_type op;
    struct variable_type var;
    struct function_type fn;
    struct control_type con;
} etoken_t, *etoken;

static int etoken_get_is_0(etoken tok)
{
    switch (tok->gen.datatype) {
        case MPR_INT32:     return tok->lit.val.i == 0;
        case MPR_FLT:       return tok->lit.val.f == 0.f;
        case MPR_DBL:       return tok->lit.val.d == 0.0;
    }
    return 0;
}

static int etoken_get_is_1(etoken tok)
{
    switch (tok->gen.datatype) {
        case MPR_INT32:     return tok->lit.val.i == 1;
        case MPR_FLT:       return tok->lit.val.f == 1.f;
        case MPR_DBL:       return tok->lit.val.d == 1.0;
    }
    return 0;
}

static int etoken_get_arity(etoken tok)
{
    switch (tok->toktype) {
        case TOK_VAR:
        case TOK_TT:
        case TOK_ASSIGN:
        case TOK_ASSIGN_CONST:
        case TOK_ASSIGN_USE:
        case TOK_ASSIGN_TT:     return NUM_VAR_IDXS(tok->gen.flags);
        case TOK_OP:            return op_tbl[tok->op.idx].arity;
        case TOK_FN:            return fn_tbl[tok->fn.idx].arity;
        case TOK_RFN:           return rfn_tbl[tok->fn.idx].arity;
        case TOK_VFN:           return vfn_tbl[tok->fn.idx].arity;
        case TOK_VECTORIZE:     return tok->fn.arity;
        case TOK_MOVE:          return tok->con.cache_offset + 1;
        case TOK_SP_ADD:        return tok->lit.val.i;
        case TOK_LOOP_START:
        case TOK_LOOP_END:
                                return tok->con.flags & RT_INSTANCE ? 1 : 0;
        default:                return 0;
    }
    return 0;
}

MPR_INLINE static void etoken_cpy(etoken dst, etoken src)
{
    memcpy(dst, src, TOKEN_SIZE);
}

MPR_INLINE static void etoken_set_op(etoken tok, expr_op_t op)
{
    tok->toktype = TOK_OP;
    tok->op.idx = op;
}

MPR_INLINE static void etoken_set_int32(etoken tok, int val)
{
    tok->toktype = TOK_LITERAL;
    tok->gen.datatype = MPR_INT32;
    tok->lit.val.i = val;
}

MPR_INLINE static void etoken_set_flt(etoken tok, float val)
{
    tok->toktype = TOK_LITERAL;
    tok->gen.datatype = MPR_FLT;
    tok->lit.val.f = val;
}

MPR_INLINE static void etoken_set_dbl(etoken tok, double val)
{
    tok->toktype = TOK_LITERAL;
    tok->gen.datatype = MPR_DBL;
    tok->lit.val.d = val;
}

MPR_INLINE static void etoken_free(etoken tok)
{
    if (TOK_VLITERAL == tok->toktype && tok->lit.val.ip)
        free(tok->lit.val.ip);
}

static mpr_type etoken_cmp_datatype(etoken tok, mpr_type type)
{
    mpr_type type2 = tok->gen.casttype ? tok->gen.casttype : tok->gen.datatype;
    if (tok->toktype >= TOK_LOOP_START)
        return type;
    /* return the higher datatype, 'd' < 'f' < i' */
    return type < type2 ? type : type2;
}

int etoken_squash(etoken l, etoken r)
{
    if (l->gen.flags & VEC_LEN_LOCKED)
        return 0;
    if (TOK_LITERAL == l->toktype) {
        void *tmp;
        mpr_type type = etoken_cmp_datatype(l, r->lit.datatype);
        switch (type) {
            case MPR_INT32:
                tmp = malloc(2 * sizeof(int));
                ((int*)tmp)[0] = l->lit.val.i;
                ((int*)tmp)[1] = r->lit.val.i;
                break;
            case MPR_FLT:
                tmp = malloc(2 * sizeof(float));
                switch (l->lit.datatype) {
                    case MPR_INT32: ((float*)tmp)[0] = (float)l->lit.val.i;     break;
                    default:        ((float*)tmp)[0] =        l->lit.val.f;     break;
                }
                switch (r->lit.datatype) {
                    case MPR_INT32: ((float*)tmp)[1] = (float)r->lit.val.i;     break;
                    default:        ((float*)tmp)[1] =        r->lit.val.f;     break;
                }
                break;
            default:
                tmp = malloc(2 * sizeof(double));
                switch (l->lit.datatype) {
                    case MPR_INT32: ((double*)tmp)[0] = (double)l->lit.val.i;   break;
                    case MPR_FLT:   ((double*)tmp)[0] = (double)l->lit.val.f;   break;
                    default:        ((double*)tmp)[0] =         l->lit.val.d;   break;
                }
                switch (r->lit.datatype) {
                    case MPR_INT32: ((double*)tmp)[1] = (double)r->lit.val.i;   break;
                    case MPR_FLT:   ((double*)tmp)[1] = (double)r->lit.val.f;   break;
                    default:        ((double*)tmp)[1] =         r->lit.val.d;   break;
                }
                break;
        }
        l->toktype = TOK_VLITERAL;
        l->lit.val.ip = tmp;
        l->lit.datatype = type;
        l->lit.vec_len = 2;
        return 1;
    }
    else if (TOK_VLITERAL == l->toktype) {
        int i, vec_len = l->lit.vec_len;
        void *tmp = 0;
        mpr_type type = etoken_cmp_datatype(l, r->lit.datatype);
        ++l->lit.vec_len;
        switch (type) {
            case MPR_INT32:
                /* both vector and new scalar are type MPR_INT32 */
                tmp = malloc(l->lit.vec_len * sizeof(int));
                for (i = 0; i < vec_len; i++)
                    ((int*)tmp)[i] = l->lit.val.ip[i];
                ((int*)tmp)[vec_len] = r->lit.val.i;
                break;
            case MPR_FLT:
                tmp = malloc(l->lit.vec_len * sizeof(float));
                for (i = 0; i < vec_len; i++) {
                    switch (l->lit.datatype) {
                        case MPR_INT32: ((float*)tmp)[i] = (float)l->lit.val.ip[i];     break;
                        default:        ((float*)tmp)[i] =        l->lit.val.fp[i];     break;
                    }
                }
                switch (r->lit.datatype) {
                    case MPR_INT32:     ((float*)tmp)[vec_len] = (float)r->lit.val.i;   break;
                    default:            ((float*)tmp)[vec_len] =        r->lit.val.f;   break;
                }
                break;
            case MPR_DBL:
                tmp = malloc(l->lit.vec_len * sizeof(double));
                for (i = 0; i < vec_len; i++) {
                    switch (l->lit.datatype) {
                        case MPR_INT32: ((double*)tmp)[i] = (double)l->lit.val.ip[i];   break;
                        case MPR_FLT:   ((double*)tmp)[i] = (double)l->lit.val.fp[i];   break;
                        default:        ((double*)tmp)[i] =         l->lit.val.dp[i];   break;
                    }
                }
                switch (r->lit.datatype) {
                    case MPR_INT32:     ((double*)tmp)[vec_len] = (double)r->lit.val.i; break;
                    case MPR_FLT:       ((double*)tmp)[vec_len] = (double)r->lit.val.f; break;
                    default:            ((double*)tmp)[vec_len] =         r->lit.val.d; break;
                }
                break;
        }
        free(l->lit.val.ip);
        l->lit.val.ip = tmp;
        l->lit.datatype = type;
        return 1;
    }
    return 0;
}

int etoken_replace_special_constants(etoken tok)
{
    if (TOK_LITERAL == tok->toktype && (tok->gen.flags & CONST_SPECIAL)) {
        switch (tok->gen.flags & CONST_SPECIAL) {
            case CONST_MAXVAL:
                switch (tok->lit.datatype) {
                    case MPR_INT32: tok->lit.val.i = INT_MAX;   break;
                    case MPR_FLT:   tok->lit.val.f = FLT_MAX; 	break;
                    case MPR_DBL:   tok->lit.val.d = DBL_MAX;   break;
                    default:                                    goto error;
                }
                break;
            case CONST_MINVAL:
                switch (tok->lit.datatype) {
                    case MPR_INT32: tok->lit.val.i = INT_MIN;   break;
                    case MPR_FLT:   tok->lit.val.f = -FLT_MAX;  break;
                    case MPR_DBL:   tok->lit.val.d = -DBL_MAX;  break;
                    default:                                    goto error;
                }
                break;
            case CONST_PI:
                switch (tok->lit.datatype) {
                    case MPR_FLT:   tok->lit.val.f = M_PI;      break;
                    case MPR_DBL:   tok->lit.val.d = M_PI;      break;
                    default:                                    goto error;
                }
                break;
            case CONST_E:
                switch (tok->lit.datatype) {
                    case MPR_FLT:   tok->lit.val.f = M_E;       break;
                    case MPR_DBL:   tok->lit.val.d = M_E;       break;
                    default:                                    goto error;
                }
                break;
            default:
                return 1;
        }
        tok->gen.flags &= ~CONST_SPECIAL;
    }
    return 0;

error:
#if TRACE_PARSE
    printf("Illegal type found when replacing special constants.\n");
#endif
    return -1;
}

#if TRACE_PARSE || TRACE_EVAL
static void etoken_print(etoken tok, expr_var_t *vars, int show_locks)
{
    int i, d = 0, l = 128;
    char s[128];
    char *dims[] = {"unknown", "history", "instance", 0, "signal", 0, 0, 0, "vector"};
    switch (tok->toktype) {
        case TOK_LITERAL:
            d = snprintf(s, l, "LITERAL\t");
            switch (tok->gen.flags & CONST_SPECIAL) {
                case CONST_MAXVAL:  snprintf(s + d, l - d, "max");                      break;
                case CONST_MINVAL:  snprintf(s + d, l - d, "min");                      break;
                case CONST_PI:      snprintf(s + d, l - d, "pi");                       break;
                case CONST_E:       snprintf(s + d, l - d, "e");                        break;
                default:
                    switch (tok->gen.datatype) {
                        case MPR_FLT:   snprintf(s + d, l - d, "%g", tok->lit.val.f);   break;
                        case MPR_DBL:   snprintf(s + d, l - d, "%g", tok->lit.val.d);   break;
                        case MPR_INT32: snprintf(s + d, l - d, "%d", tok->lit.val.i);   break;
                    }                                                                   break;
            }
                                                                                        break;
        case TOK_VLITERAL:
            d = snprintf(s, l, "VLITERAL\t[");
            switch (tok->gen.datatype) {
                case MPR_FLT:
                    for (i = 0; i < tok->lit.vec_len; i++)
                        d += snprintf(s + d, l - d, "%g,", tok->lit.val.fp[i]);
                    break;
                case MPR_DBL:
                    for (i = 0; i < tok->lit.vec_len; i++)
                        d += snprintf(s + d, l - d, "%g,", tok->lit.val.dp[i]);
                    break;
                case MPR_INT32:
                    for (i = 0; i < tok->lit.vec_len; i++)
                        d += snprintf(s + d, l - d, "%d,", tok->lit.val.ip[i]);
                    break;
            }
            --d;
            snprintf(s + d, l - d, "]");
            break;
        case TOK_OP:            snprintf(s, l, "OP\t\t%s", op_tbl[tok->op.idx].name);   break;
        case TOK_OPEN_CURLY:    snprintf(s, l, "{\t");                                  break;
        case TOK_OPEN_PAREN:    snprintf(s, l, "(\t\tarity %d", tok->fn.arity);         break;
        case TOK_OPEN_SQUARE:   snprintf(s, l, "[");                                    break;
        case TOK_CLOSE_CURLY:   snprintf(s, l, "}");                                    break;
        case TOK_CLOSE_PAREN:   snprintf(s, l, ")");                                    break;
        case TOK_CLOSE_SQUARE:  snprintf(s, l, "]");                                    break;

        case TOK_ASSIGN:
        case TOK_ASSIGN_CONST:
        case TOK_ASSIGN_USE:
        case TOK_ASSIGN_TT:
            d = snprintf(s, l, "ASSIGN%s\t", TOK_ASSIGN_USE == tok->toktype ? "_USE" : "");
        case TOK_VAR:
        case TOK_TT: {
            if (TOK_VAR == tok->toktype || TOK_TT == tok->toktype)
                d += snprintf(s + d, l - d, "LOAD\t");
            if (TOK_TT == tok->toktype || TOK_ASSIGN_TT == tok->toktype)
                d += snprintf(s + d, l - d, "tt");
            else
                d += snprintf(s + d, l - d, "var");


            if (tok->var.idx == VAR_Y)
                d += snprintf(s + d, l - d, ".y");
            else if (tok->var.idx == VAR_X_NEWEST)
                d += snprintf(s + d, l - d, ".x$$");
            else if (tok->var.idx >= VAR_X) {
                d += snprintf(s + d, l - d, ".x");
                if (tok->gen.flags & VAR_SIG_IDX)
                    d += snprintf(s + d, l - d, "$N");
                else
                    d += snprintf(s + d, l - d, "$%d", tok->var.idx - VAR_X);
            }
            else
                d += snprintf(s + d, l - d, "%d%c.%s%s", tok->var.idx,
                              vars ? vars[tok->var.idx].datatype : '?',
                              vars ? vars[tok->var.idx].name : "?",
                              vars ? (vars[tok->var.idx].flags & VAR_INSTANCED) ? ".N" : ".0" : ".?");

            if (tok->gen.flags & VAR_HIST_IDX)
                d += snprintf(s + d, l - d, "{N}");

            if (TOK_TT == tok->toktype)
                break;

            if (tok->gen.flags & VAR_VEC_IDX)
                d += snprintf(s + d, l - d, "[N");
            else
                d += snprintf(s + d, l - d, "[%u", tok->var.vec_idx);
            if (tok->var.idx >= VAR_Y)
                d += snprintf(s + d, l - d, "]");
            else
                d += snprintf(s + d, l - d, "/%u]", vars ? vars[tok->var.idx].vec_len : 0);

            if (tok->toktype & TOK_ASSIGN)
                snprintf(s + d, l - d, "<%d>", tok->var.offset);
            break;
        }
        case TOK_VAR_NUM_INST:
            if (tok->var.idx == VAR_Y)
                snprintf(s, l, "NUM_INST\tvar.y");
            else if (tok->var.idx == VAR_X_NEWEST)
                snprintf(s, l, "NUM_INST\tvar.x$$");
            else if (tok->var.idx >= VAR_X)
                snprintf(s, l, "NUM_INST\tvar.x$%d", tok->var.idx - VAR_X);
            else
                snprintf(s, l, "NUM_INST\tvar.%s%s", vars ? vars[tok->var.idx].name : "?",
                         vars ? (vars[tok->var.idx].flags & VAR_INSTANCED) ? ".N" : ".0" : ".?");
            break;
        case TOK_FN:        snprintf(s, l, "FN\t\t%s()", fn_tbl[tok->fn.idx].name);     break;
        case TOK_COMMA:     snprintf(s, l, ",");                                        break;
        case TOK_COLON:     snprintf(s, l, ":");                                        break;
        case TOK_VECTORIZE: snprintf(s, l, "VECT(%d)", tok->fn.arity);                  break;
        case TOK_NEGATE:    snprintf(s, l, "-");                                        break;
        case TOK_VFN:
        case TOK_VFN_DOT:   snprintf(s, l, "VFN\t%s()", vfn_tbl[tok->fn.idx].name);     break;
        case TOK_RFN:
            if (RFN_HISTORY == tok->fn.idx)
                snprintf(s, l, "RFN\thistory(%d:%d)", tok->con.reduce_start, tok->con.reduce_stop);
            else if (RFN_VECTOR == tok->fn.idx) {
                if (tok->con.flags & USE_VAR_LEN)
                    snprintf(s, l, "RFN\tvector(%d:len)", tok->con.reduce_start);
                else
                    snprintf(s, l, "RFN\tvector(%d:%d)", tok->con.reduce_start, tok->con.reduce_stop);
            }
            else
                snprintf(s, l, "RFN\t%s()", rfn_tbl[tok->fn.idx].name);
            break;
        case TOK_LAMBDA:
            snprintf(s, l, "LAMBDA");                                                   break;
        case TOK_COPY_FROM:
            snprintf(s, l, "COPY\t%d", tok->con.cache_offset * -1);                     break;
        case TOK_MOVE:
            snprintf(s, l, "MOVE\t-%d", tok->con.cache_offset);                         break;
        case TOK_LOOP_START:
            snprintf(s, l, "LOOP_START\t%s", dims[tok->con.flags & REDUCE_TYPE_MASK]);  break;
        case TOK_REDUCING:
            if (tok->con.flags & USE_VAR_LEN)
                snprintf(s, l, "REDUCE\t%s[%d:len]", dims[tok->con.flags & REDUCE_TYPE_MASK],
                         tok->con.reduce_start);
            else
                snprintf(s, l, "REDUCE\t%s[%d:%d]", dims[tok->con.flags & REDUCE_TYPE_MASK],
                         tok->con.reduce_start, tok->con.reduce_stop);
            break;
        case TOK_LOOP_END:
            switch (tok->con.flags & REDUCE_TYPE_MASK) {
                case RT_HISTORY:
                    snprintf(s, l, "LOOP_END\thistory[%d:%d]<%d,%d>", -tok->con.reduce_start,
                             -tok->con.reduce_stop, tok->con.branch_offset, tok->con.cache_offset);
                    break;
                case RT_VECTOR:
                    if (tok->con.flags & USE_VAR_LEN)
                        snprintf(s, l, "LOOP_END\tvector[%d:len]<%d,%d>", tok->con.reduce_start,
                                 tok->con.branch_offset, tok->con.cache_offset);
                    else
                        snprintf(s, l, "LOOP_END\tvector[%d:%d]<%d,%d>", tok->con.reduce_start,
                                 tok->con.reduce_stop, tok->con.branch_offset, tok->con.cache_offset);
                    break;
                default:
                    snprintf(s, l, "LOOP_END\t%s<%d,%d>", dims[tok->con.flags & REDUCE_TYPE_MASK],
                             tok->con.branch_offset, tok->con.cache_offset);
            }
            break;
        case TOK_SP_ADD:            snprintf(s, l, "SP_ADD\t%d", tok->lit.val.i);       break;
        case TOK_SEMICOLON:         snprintf(s, l, "semicolon");                        break;
        case TOK_END:               printf("END\n");                                    return;
        default:                    printf("(unknown token)\n");                        return;
    }
    printf("%s", s);
    /* indent */
    l = strlen(s);
    if (show_locks) {
        printf("\r\t\t\t\t\t%c%u", tok->gen.datatype, tok->gen.vec_len);
        if (tok->gen.casttype)
            printf("->%c", tok->gen.casttype);
        else
            printf("   ");
        if (tok->gen.flags & VEC_LEN_LOCKED)
            printf(" vlock");
        if (tok->gen.flags & TYPE_LOCKED)
            printf(" tlock");
        if (TOK_ASSIGN & tok->toktype && tok->gen.flags & CLEAR_STACK)
            printf(" clear");
    }
}
#endif /* TRACE_PARSE || TRACE_EVAL */

#endif /* __MPR_EXPR_TOKEN_H__ */
