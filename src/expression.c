#include <assert.h>
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <float.h>
#include "mapper_internal.h"

#define MAX_HIST_SIZE 100
#define STACK_SIZE 64
#define N_USER_VARS 16
#ifdef DEBUG
    #define TRACE_PARSE 0 /* Set non-zero to see trace during parse. */
    #define TRACE_EVAL 0 /* Set non-zero to see trace during evaluation. */
#else
    #define TRACE_PARSE 0 /* Set non-zero to see trace during parse. */
    #define TRACE_EVAL 0 /* Set non-zero to see trace during evaluation. */
#endif

#define lex_error trace

typedef union _mpr_expr_val {
    float f;
    double d;
    int i;
} mpr_expr_val_t, *mpr_expr_val;

/* could we use mpr_value here instead, with stack idx instead of history idx?
 * pro: vectors, commonality with I/O
 * con: timetags wasted
 * option: create version with unallocated timetags */
struct _mpr_expr_stack {
    mpr_expr_val stk;
    mpr_type *types;
    uint8_t *dims;
    int size;
};

mpr_expr_stack mpr_expr_stack_new() {
    mpr_expr_stack stk = calloc(1, sizeof(struct _mpr_expr_stack));
    return stk;
}

static void expr_stack_realloc(mpr_expr_stack stk, int num_samps) {
    /* Reallocate evaluation stack if necessary. */
    if (num_samps > stk->size) {
        stk->size = num_samps;
        if (stk->stk)
            stk->stk = realloc(stk->stk, stk->size * sizeof(mpr_expr_val_t));
        else
            stk->stk = malloc(stk->size * sizeof(mpr_expr_val_t));
        if (stk->types)
            stk->types = realloc(stk->types, stk->size * sizeof(mpr_type));
        else
            stk->types = malloc(stk->size * sizeof(mpr_type));
        if (stk->dims)
            stk->dims = realloc(stk->dims, stk->size * sizeof(uint8_t));
        else
            stk->dims = malloc(stk->size * sizeof(uint8_t));
    }
}

void mpr_expr_stack_free(mpr_expr_stack stk) {
    if (stk->stk)
        free(stk->stk);
    if (stk->types)
        free(stk->types);
    if (stk->dims)
        free(stk->dims);
    free(stk);
}

#define EXTREMA_FUNC(NAME, TYPE, OP)    \
    static TYPE NAME(TYPE x, TYPE y) { return (x OP y) ? x : y; }
EXTREMA_FUNC(maxi, int, >)
EXTREMA_FUNC(mini, int, <)
EXTREMA_FUNC(maxf, float, >)
EXTREMA_FUNC(minf, float, <)
EXTREMA_FUNC(maxd, double, >)
EXTREMA_FUNC(mind, double, <)

#define UNARY_FUNC(TYPE, NAME, SUFFIX, CALC)    \
    static TYPE NAME##SUFFIX(TYPE x) { return CALC; }
#define FLOAT_OR_DOUBLE_UNARY_FUNC(NAME, CALC)  \
    UNARY_FUNC(float, NAME, f, CALC)            \
    UNARY_FUNC(double, NAME, d, CALC)
FLOAT_OR_DOUBLE_UNARY_FUNC(midiToHz, 440. * pow(2.0, (x - 69) / 12.0))
FLOAT_OR_DOUBLE_UNARY_FUNC(hzToMidi, 69. + 12. * log2(x / 440.))
FLOAT_OR_DOUBLE_UNARY_FUNC(uniform, rand() / (RAND_MAX + 1.0) * x)
UNARY_FUNC(int, sign, i, x >= 0 ? 1 : -1)
FLOAT_OR_DOUBLE_UNARY_FUNC(sign, x >= 0 ? 1.0 : -1.0)

#define TEST_VEC_TYPED(NAME, TYPE, OP, CMP, RET, T)                 \
static void NAME(mpr_expr_val stk, uint8_t *dim, int idx, int inc)  \
{                                                                   \
    register TYPE ret = 1 - RET;                                    \
    mpr_expr_val val = stk + idx * inc;                             \
    int i, len = dim[idx];                                          \
    for (i = 0; i < len; i++) {                                     \
        if (val[i].T OP CMP) {                                      \
            ret = RET;                                              \
            break;                                                  \
        }                                                           \
    }                                                               \
    val[0].T = ret;                                                 \
}
TEST_VEC_TYPED(valli, int, ==, 0, 0, i)
TEST_VEC_TYPED(vallf, float, ==, 0.f, 0, f)
TEST_VEC_TYPED(valld, double, ==, 0., 0, d)
TEST_VEC_TYPED(vanyi, int, !=, 0, 1, i)
TEST_VEC_TYPED(vanyf, float, !=, 0.f, 1, f)
TEST_VEC_TYPED(vanyd, double, !=, 0., 1, d)

#define SUM_VFUNC(NAME, TYPE, T)                                    \
static void NAME(mpr_expr_val stk, uint8_t *dim, int idx, int inc)  \
{                                                                   \
    register TYPE aggregate = 0;                                    \
    mpr_expr_val val = stk + idx * inc;                             \
    int i, len = dim[idx];                                          \
    for (i = 0; i < len; i++)                                       \
        aggregate += val[i].T;                                      \
    val[0].T = aggregate;                                           \
}
SUM_VFUNC(vsumi, int, i)
SUM_VFUNC(vsumf, float, f)
SUM_VFUNC(vsumd, double, d)

#define MEAN_VFUNC(NAME, TYPE, T)                                   \
static void NAME(mpr_expr_val stk, uint8_t *dim, int idx, int inc)  \
{                                                                   \
    register TYPE mean = 0;                                         \
    mpr_expr_val val = stk + idx * inc;                             \
    int i, len = dim[idx];                                          \
    for (i = 0; i < len; i++)                                       \
        mean += val[i].T;                                           \
    val[0].T = mean / len;                                          \
}
MEAN_VFUNC(vmeanf, float, f)
MEAN_VFUNC(vmeand, double, d)

#define CENTER_VFUNC(NAME, TYPE, T)                                 \
static void NAME(mpr_expr_val stk, uint8_t *dim, int idx, int inc)  \
{                                                                   \
    mpr_expr_val val = stk + idx * inc;                             \
    register TYPE max = val[0].T, min = max;                        \
    int i, len = dim[idx];                                          \
    for (i = 0; i < len; i++) {                                     \
        if (val[i].T > max)                                         \
            max = val[i].T;                                         \
        if (val[i].T < min)                                         \
            min = val[i].T;                                         \
    }                                                               \
    val[0].T = (max + min) * 0.5;                                   \
}
CENTER_VFUNC(vcenterf, float, f)
CENTER_VFUNC(vcenterd, double, d)

#define EXTREMA_VFUNC(NAME, OP, TYPE, T)                            \
static void NAME(mpr_expr_val stk, uint8_t *dim, int idx, int inc)  \
{                                                                   \
    mpr_expr_val val = stk + idx * inc;                             \
    register TYPE extrema = val[0].T;                               \
    int i, len = dim[idx];                                          \
    for (i = 1; i < len; i++) {                                     \
        if (val[i].T OP extrema)                                    \
            extrema = val[i].T;                                     \
    }                                                               \
    val[0].T = extrema;                                             \
}
EXTREMA_VFUNC(vmaxi, >, int, i)
EXTREMA_VFUNC(vmini, <, int, i)
EXTREMA_VFUNC(vmaxf, >, float, f)
EXTREMA_VFUNC(vminf, <, float, f)
EXTREMA_VFUNC(vmaxd, >, double, d)
EXTREMA_VFUNC(vmind, <, double, d)

#define INC_SORT_FUNC(TYPE, T)                              \
int inc_sort_func##T (const void * a, const void * b) {     \
    return ((*(mpr_expr_val)a).T - (*(mpr_expr_val)b).T);   \
}
INC_SORT_FUNC(int, i)
INC_SORT_FUNC(float, f)
INC_SORT_FUNC(double, d)

#define DEC_SORT_FUNC(TYPE, T)                              \
int dec_sort_func##T (const void * a, const void * b) {     \
    return ((*(mpr_expr_val)b).T - (*(mpr_expr_val)a).T);   \
}
DEC_SORT_FUNC(int, i)
DEC_SORT_FUNC(float, f)
DEC_SORT_FUNC(double, d)

#define SORT_VFUNC(NAME, TYPE, T)                                   \
static void NAME(mpr_expr_val stk, uint8_t *dim, int idx, int inc)  \
{                                                                   \
    mpr_expr_val val = stk + idx * inc, dir = val + inc;            \
    int len = dim[idx];                                             \
    if (dir[0].T >= 0)                                              \
        qsort(val, len, sizeof(mpr_expr_val_t), inc_sort_func##T);  \
    else                                                            \
        qsort(val, len, sizeof(mpr_expr_val_t), dec_sort_func##T);  \
}
SORT_VFUNC(vsorti, int, i)
SORT_VFUNC(vsortf, float, f)
SORT_VFUNC(vsortd, double, d)

#define powd pow
#define sqrtd sqrt
#define acosd acos

#define NORM_VFUNC(NAME, TYPE, T)                                   \
static void NAME(mpr_expr_val stk, uint8_t *dim, int idx, int inc)  \
{                                                                   \
    mpr_expr_val val = stk + idx * inc;                             \
    register TYPE tmp = 0;                                          \
    int i, len = dim[idx];                                          \
    for (i = 0; i < len; i++)                                       \
        tmp += pow##T(val[i].T, 2);                                 \
    val[0].T = sqrt##T(tmp);                                        \
}
NORM_VFUNC(vnormf, float, f)
NORM_VFUNC(vnormd, double, d)

#define DOT_VFUNC(NAME, TYPE, T)                                    \
static void NAME(mpr_expr_val stk, uint8_t *dim, int idx, int inc)  \
{                                                                   \
    register TYPE dot = 0;                                          \
    mpr_expr_val a = stk + idx * inc, b = a + inc;                  \
    int i, len = dim[idx];                                          \
    for (i = 0; i < len; i++)                                       \
        dot += a[i].T * b[i].T;                                     \
    a[0].T = dot;                                                   \
}
DOT_VFUNC(vdoti, int, i)
DOT_VFUNC(vdotf, float, f)
DOT_VFUNC(vdotd, double, d)

/* TODO: should we handle multidimensional angles as well? Problem with sign...
 * should probably have separate function for signed and unsigned: angle vs. rotation */
/* TODO: quaternion functions */

#define atan2d atan2
#define ANGLE_VFUNC(NAME, TYPE, T)                                  \
static void NAME(mpr_expr_val stk, uint8_t *dim, int idx, int inc)  \
{                                                                   \
    register TYPE theta;                                            \
    mpr_expr_val a = stk + idx * inc, b = a + inc;                  \
    theta = atan2##T(b[1].T, b[0].T) - atan2##T(a[1].T, a[0].T);    \
    if (theta > M_PI)                                               \
        theta -= 2 * M_PI;                                          \
    else if (theta < -M_PI)                                         \
        theta += 2 * M_PI;                                          \
    a[0].T = theta;                                                 \
}
ANGLE_VFUNC(vanglef, float, f)
ANGLE_VFUNC(vangled, double, d)

#define MAXMIN_VFUNC(NAME, TYPE, T)                                 \
static void NAME(mpr_expr_val stk, uint8_t *dim, int idx, int inc)  \
{                                                                   \
    mpr_expr_val max = stk+idx*inc, min = max+inc, new = min+inc;   \
    int i, len = dim[idx];                                          \
    for (i = 0; i < len; i++) {                                     \
        if (new[i].T > max[i].T)                                    \
            max[i].T = new[i].T;                                    \
        if (new[i].T < min[i].T)                                    \
            min[i].T = new[i].T;                                    \
    }                                                               \
}
MAXMIN_VFUNC(vmaxmini, int, i)
MAXMIN_VFUNC(vmaxminf, float, f)
MAXMIN_VFUNC(vmaxmind, double, d)

#define SUMNUM_VFUNC(NAME, TYPE, T)                                 \
static void NAME(mpr_expr_val stk, uint8_t *dim, int idx, int inc)  \
{                                                                   \
    mpr_expr_val sum = stk+idx*inc, num = sum+inc, new = num+inc;   \
    int i, len = dim[idx];                                          \
    for (i = 0; i < len; i++) {                                     \
        sum[i].T += new[i].T;                                       \
        num[i].T += 1;                                              \
    }                                                               \
}
SUMNUM_VFUNC(vsumnumi, int, i)
SUMNUM_VFUNC(vsumnumf, float, f)
SUMNUM_VFUNC(vsumnumd, double, d)

#define TYPED_EMA(TYPE, T)                              \
static TYPE ema##T(TYPE memory, TYPE val, TYPE weight)  \
    { return val * weight + memory * (1 - weight); }
TYPED_EMA(float, f)
TYPED_EMA(double, d)

#define TYPED_SCHMITT(TYPE, T)                                      \
static TYPE schmitt##T(TYPE memory, TYPE val, TYPE low, TYPE high)  \
    { return memory ? val > low : val >= high; }
TYPED_SCHMITT(float, f)
TYPED_SCHMITT(double, d)

typedef enum {
    VAR_UNKNOWN = -1,
    VAR_Y = N_USER_VARS,
    VAR_X,
    N_VARS
} expr_var_t;

typedef enum {
    OP_UNKNOWN = -1,
    OP_LOGICAL_NOT,
    OP_MULTIPLY,
    OP_DIVIDE,
    OP_MODULO,
    OP_ADD,
    OP_SUBTRACT,
    OP_LEFT_BIT_SHIFT,
    OP_RIGHT_BIT_SHIFT,
    OP_IS_GREATER_THAN,
    OP_IS_GREATER_THAN_OR_EQUAL,
    OP_IS_LESS_THAN,
    OP_IS_LESS_THAN_OR_EQUAL,
    OP_IS_EQUAL,
    OP_IS_NOT_EQUAL,
    OP_BITWISE_AND,
    OP_BITWISE_XOR,
    OP_BITWISE_OR,
    OP_LOGICAL_AND,
    OP_LOGICAL_OR,
    OP_IF,
    OP_IF_ELSE,
    OP_IF_THEN_ELSE
} expr_op_t;

#define NONE        0x0
#define GET_ZERO    0x1
#define GET_ONE     0x2
#define GET_OPER    0x4
#define BAD_EXPR    0x8

static struct {
    const char *name;
    uint8_t arity;
    uint8_t precedence;
    uint16_t optimize_const_ops;
} op_tbl[] = {
/*                         left==0  | right==0     | left==1      | right==1     */
    { "!",          1, 11, GET_ONE  | GET_ONE  <<4 | GET_ZERO <<8 | GET_ZERO <<12 },
    { "*",          2, 10, GET_ZERO | GET_ZERO <<4 | GET_OPER <<8 | GET_OPER <<12 },
    { "/",          2, 10, GET_ZERO | BAD_EXPR <<4 | NONE     <<8 | GET_OPER <<12 },
    { "%",          2, 10, GET_ZERO | GET_OPER <<4 | GET_ONE  <<8 | GET_OPER <<12 },
    { "+",          2, 9,  GET_OPER | GET_OPER <<4 | NONE     <<8 | NONE     <<12 },
    { "-",          2, 9,  NONE     | GET_OPER <<4 | NONE     <<8 | NONE     <<12 },
    { "<<",         2, 8,  GET_ZERO | GET_OPER <<4 | NONE     <<8 | NONE     <<12 },
    { ">>",         2, 8,  GET_ZERO | GET_OPER <<4 | NONE     <<8 | NONE     <<12 },
    { ">",          2, 7,  NONE     | NONE     <<4 | NONE     <<8 | NONE     <<12 },
    { ">=",         2, 7,  NONE     | NONE     <<4 | NONE     <<8 | NONE     <<12 },
    { "<",          2, 7,  NONE     | NONE     <<4 | NONE     <<8 | NONE     <<12 },
    { "<=",         2, 7,  NONE     | NONE     <<4 | NONE     <<8 | NONE     <<12 },
    { "==",         2, 6,  NONE     | NONE     <<4 | NONE     <<8 | NONE     <<12 },
    { "!=",         2, 6,  NONE     | NONE     <<4 | NONE     <<8 | NONE     <<12 },
    { "&",          2, 5,  GET_ZERO | GET_ZERO <<4 | NONE     <<8 | NONE     <<12 },
    { "^",          2, 4,  GET_OPER | GET_OPER <<4 | NONE     <<8 | NONE     <<12 },
    { "|",          2, 3,  GET_OPER | GET_OPER <<4 | GET_ONE  <<8 | GET_ONE  <<12 },
    { "&&",         2, 2,  GET_ZERO | GET_ZERO <<4 | NONE     <<8 | NONE     <<12 },
    { "||",         2, 1,  GET_OPER | GET_OPER <<4 | GET_ONE  <<8 | GET_ONE  <<12 },
    /* TODO: handle optimization of ternary operator */
    { "IFTHEN",     2, 0,  NONE     | NONE     <<4 | NONE     <<8 | NONE     <<12 },
    { "IFELSE",     2, 0,  NONE     | NONE     <<4 | NONE     <<8 | NONE     <<12 },
    { "IFTHENELSE", 3, 0,  NONE     | NONE     <<4 | NONE     <<8 | NONE     <<12 },
};

typedef enum {
    FN_UNKNOWN = -1,
    FN_ABS = 0,
    FN_ACOS,
    FN_ACOSH,
    FN_ASIN,
    FN_ASINH,
    FN_ATAN,
    FN_ATAN2,
    FN_ATANH,
    FN_CBRT,
    FN_CEIL,
    FN_COS,
    FN_COSH,
    FN_EMA,
    FN_EXP,
    FN_EXP2,
    FN_FLOOR,
    FN_HYPOT,
    FN_HZTOMIDI,
    FN_LOG,
    FN_LOG10,
    FN_LOG2,
    FN_LOGB,
    FN_MAX,
    FN_MIDITOHZ,
    FN_MIN,
    FN_POW,
    FN_ROUND,
    FN_SCHMITT,
    FN_SIGN,
    FN_SIN,
    FN_SINH,
    FN_SQRT,
    FN_TAN,
    FN_TANH,
    FN_TRUNC,
    /* place functions which should never be precomputed below this point */
    FN_DEL_IDX,
    FN_SIG_IDX,
    FN_VEC_IDX,
    FN_UNIFORM,
    N_FN
} expr_fn_t;

static struct {
    const char *name;
    uint8_t arity;
    uint8_t memory;
    void *fn_int;
    void *fn_flt;
    void *fn_dbl;
} fn_tbl[] = {
    { "abs",      1, 0, (void*)abs,   (void*)fabsf,     (void*)fabs      },
    { "acos",     1, 0, 0,            (void*)acosf,     (void*)acos      },
    { "acosh",    1, 0, 0,            (void*)acoshf,    (void*)acosh     },
    { "asin",     1, 0, 0,            (void*)asinf,     (void*)asin      },
    { "asinh",    1, 0, 0,            (void*)asinhf,    (void*)asinh     },
    { "atan",     1, 0, 0,            (void*)atanf,     (void*)atan      },
    { "atan2",    2, 0, 0,            (void*)atan2f,    (void*)atan2     },
    { "atanh",    1, 0, 0,            (void*)atanhf,    (void*)atanh     },
    { "cbrt",     1, 0, 0,            (void*)cbrtf,     (void*)cbrt      },
    { "ceil",     1, 0, 0,            (void*)ceilf,     (void*)ceil      },
    { "cos",      1, 0, 0,            (void*)cosf,      (void*)cos       },
    { "cosh",     1, 0, 0,            (void*)coshf,     (void*)cosh      },
    { "ema",      3, 1, 0,            (void*)emaf,      (void*)emad      },
    { "exp",      1, 0, 0,            (void*)expf,      (void*)exp       },
    { "exp2",     1, 0, 0,            (void*)exp2f,     (void*)exp2      },
    { "floor",    1, 0, 0,            (void*)floorf,    (void*)floor     },
    { "hypot",    2, 0, 0,            (void*)hypotf,    (void*)hypot     },
    { "hzToMidi", 1, 0, 0,            (void*)hzToMidif, (void*)hzToMidid },
    { "log",      1, 0, 0,            (void*)logf,      (void*)log       },
    { "log10",    1, 0, 0,            (void*)log10f,    (void*)log10     },
    { "log2",     1, 0, 0,            (void*)log2f,     (void*)log2      },
    { "logb",     1, 0, 0,            (void*)logbf,     (void*)logb      },
    { "max",      2, 0, (void*)maxi,  (void*)maxf,      (void*)maxd      },
    { "midiToHz", 1, 0, 0,            (void*)midiToHzf, (void*)midiToHzd },
    { "min",      2, 0, (void*)mini,  (void*)minf,      (void*)mind      },
    { "pow",      2, 0, 0,            (void*)powf,      (void*)pow       },
    { "round",    1, 0, 0,            (void*)roundf,    (void*)round     },
    { "schmitt",  4, 1, 0,            (void*)schmittf,  (void*)schmittd  },
    { "sign",     1, 0, (void*)signi, (void*)signf,     (void*)signd     },
    { "sin",      1, 0, 0,            (void*)sinf,      (void*)sin       },
    { "sinh",     1, 0, 0,            (void*)sinhf,     (void*)sinh      },
    { "sqrt",     1, 0, 0,            (void*)sqrtf,     (void*)sqrt      },
    { "tan",      1, 0, 0,            (void*)tanf,      (void*)tan       },
    { "tanh",     1, 0, 0,            (void*)tanhf,     (void*)tanh      },
    { "trunc",    1, 0, 0,            (void*)truncf,    (void*)trunc     },
    /* place functions which should never be precomputed below this point */
    { "delay",    1, 0, (void*)1,     0,                0                },
    { "sig_idx",  1, 0, (void*)1,     0,                0                },
    { "vec_idx",  1, 0, (void*)1,     0,                0                },
    { "uniform",  1, 0, 0,            (void*)uniformf,  (void*)uniformd  },
};

typedef enum {
    VFN_UNKNOWN = -1,
    VFN_ALL = 0,
    VFN_ANY,
    VFN_CENTER,
    VFN_MAX,
    VFN_MEAN,
    VFN_MIN,
    VFN_SUM,
    /* function names above this line are also found in rfn_table */
    VFN_NORM,
    VFN_SORT,
    VFN_MAXMIN,
    VFN_SUMNUM,
    VFN_ANGLE,
    VFN_DOT,
    N_VFN
} expr_vfn_t;

static struct {
    const char *name;
    uint8_t arity;
    uint8_t reduce; /* TODO: use bitflags */
    uint8_t dot_notation;
    void (*fn_int)(mpr_expr_val, uint8_t*, int, int);
    void (*fn_flt)(mpr_expr_val, uint8_t*, int, int);
    void (*fn_dbl)(mpr_expr_val, uint8_t*, int, int);
} vfn_tbl[] = {
    { "all",    1, 1, 1, valli,    vallf,    valld    },
    { "any",    1, 1, 1, vanyi,    vanyf,    vanyd    },
    { "center", 1, 1, 1, 0,        vcenterf, vcenterd },
    { "max",    1, 1, 1, vmaxi,    vmaxf,    vmaxd    },
    { "mean",   1, 1, 1, 0,        vmeanf,   vmeand   },
    { "min",    1, 1, 1, vmini,    vminf,    vmind    },
    { "sum",    1, 1, 1, vsumi,    vsumf,    vsumd    },
    { "norm",   1, 1, 1, 0,        vnormf,   vnormd   },
    { "sort",   2, 0, 1, vsorti,   vsortf,   vsortd   },
    { "maxmin", 3, 0, 0, vmaxmini, vmaxminf, vmaxmind },
    { "sumnum", 3, 0, 0, vsumnumi, vsumnumf, vsumnumd },
    { "angle",  2, 1, 0, 0,        vanglef,  vangled  },
    { "dot",    2, 1, 0, vdoti,    vdotf,    vdotd    }
};

typedef enum {
    RFN_UNKNOWN = -1,
    RFN_ALL = 0,
    RFN_ANY,
    RFN_CENTER,
    RFN_MAX,
    RFN_MEAN,
    RFN_MIN,
    RFN_SUM,
    /* function names above this line are also found in vfn_table */
    RFN_COUNT,
    RFN_SIZE,
    RFN_MAP,
    RFN_FILTER,
    RFN_REDUCE,
    RFN_HISTORY,
    RFN_INSTANCE,
    RFN_SIGNAL,
    RFN_VECTOR,
    N_RFN
} expr_rfn_t;

static struct {
    const char *name;
    uint8_t arity;
    expr_op_t op;
    expr_vfn_t vfn;
} rfn_tbl[] = {
    { "all",      2, OP_LOGICAL_AND, VFN_UNKNOWN },
    { "any",      2, OP_LOGICAL_OR,  VFN_UNKNOWN },
    { "center",   0, OP_UNKNOWN,     VFN_MAXMIN  },
    { "max",      2, OP_UNKNOWN,     VFN_MAX     },
    { "mean",     3, OP_UNKNOWN,     VFN_SUMNUM  },
    { "min",      2, OP_UNKNOWN,     VFN_MIN     },
    { "sum",      2, OP_ADD,         VFN_UNKNOWN },
    { "count",    0, OP_ADD,         VFN_UNKNOWN },
    { "size",     0, OP_UNKNOWN,     VFN_MAXMIN  },
    { "map",      1, OP_UNKNOWN,     VFN_UNKNOWN }, /* replaced during parsing */
    { "filter",   1, OP_UNKNOWN,     VFN_UNKNOWN }, /* replaced during parsing */
    { "reduce",   1, OP_UNKNOWN,     VFN_UNKNOWN }, /* replaced during parsing */
    { "history",  1, OP_UNKNOWN,     VFN_UNKNOWN }, /* replaced during parsing */
    { "instance", 0, OP_UNKNOWN,     VFN_UNKNOWN }, /* replaced during parsing */
    { "signal",   0, OP_UNKNOWN,     VFN_UNKNOWN }, /* replaced during parsing */
    { "vector",   0, OP_UNKNOWN,     VFN_UNKNOWN }  /* replaced during parsing */
};

typedef int fn_int_arity0();
typedef int fn_int_arity1(int);
typedef int fn_int_arity2(int,int);
typedef int fn_int_arity3(int,int,int);
typedef int fn_int_arity4(int,int,int,int);
typedef float fn_flt_arity0();
typedef float fn_flt_arity1(float);
typedef float fn_flt_arity2(float,float);
typedef float fn_flt_arity3(float,float,float);
typedef float fn_flt_arity4(float,float,float,float);
typedef double fn_dbl_arity0();
typedef double fn_dbl_arity1(double);
typedef double fn_dbl_arity2(double,double);
typedef double fn_dbl_arity3(double,double,double);
typedef double fn_dbl_arity4(double,double,double,double);
typedef void vfn_template(mpr_expr_val, uint8_t*, int, int);

/* Const special flags */
#define CONST_MINVAL    0x0001
#define CONST_MAXVAL    0x0002
#define CONST_PI        0x0003
#define CONST_E         0x0004
#define CONST_SPECIAL   0x0007

/* Variables can have multiple dimensions, each of which may be indexed separately in an expression:
 *      input signals (in the case of VAR_X only)
 *      historic samples
 *      vector elements
 *      signal instances (not currently indexable)
 * Bitflags are used to keep track of which indices are provided, therefore the indices or
 * sub-expressions that compute the indices need to be presented in the same order. On the output
 * stack they are stored in the order: INST_IDX, VEC_IDX, HIST_IDX, SIG_IDX. On the operator stack
 * (during parsing) this order is reversed.
 * Input signal and vector indices can also be specified using an index stored directly in the
 * token. In this case the token flags are not set. */

#define VAR_SIG_IDX     0x0001
#define VAR_HIST_IDX    0x0002
#define VAR_VEC_IDX     0x0004
#define VAR_INST_IDX    0x0008

#define CLEAR_STACK     0x0010
#define TYPE_LOCKED     0x0020
#define VAR_MUTED       0x0040
#define USE_VAR_LEN     0x0040 /* reuse */
#define VEC_LEN_LOCKED  0x0080

#define VAR_IDXS (VAR_HIST_IDX | VAR_VEC_IDX | VAR_SIG_IDX | VAR_INST_IDX)

uint8_t var_idx_nums[] = {0, 1, 1, 2, 1, 2, 2, 3, 1, 2, 2, 3, 2, 3, 3, 4};
#define NUM_VAR_IDXS(X) (var_idx_nums[X & VAR_IDXS])

enum toktype {
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
    TOK_ASSIGN          = 0x0400000,
    TOK_ASSIGN_USE,
    TOK_ASSIGN_CONST,                   /* Const assignment (does not require input) */
    TOK_ASSIGN_TT,                      /* Assign to NTP timestamp */
    TOK_TT              = 0x0800000,    /* NTP Timestamp */
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
    enum toktype toktype;
    mpr_type datatype;
    mpr_type casttype;
    uint8_t vec_len;
    uint8_t flags;
};

struct literal_type {
    enum toktype toktype;
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
    enum toktype toktype;
    mpr_type datatype;
    mpr_type casttype;
    uint8_t vec_len;
    uint8_t flags;
    /* end of generic_type */
    expr_op_t idx;
};

struct variable_type {
    enum toktype toktype;
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
    enum toktype toktype;
    mpr_type datatype;
    mpr_type casttype;
    uint8_t vec_len;
    uint8_t flags;
    /* end of generic_type */
    int8_t idx;
    uint8_t arity;          /* used by TOK_FN, TOK_VFN, TOK_VECTORIZE */
};

enum reduce_type {
    RT_UNKNOWN  = 0x00,
    RT_HISTORY  = 0x01,
    RT_INSTANCE = 0x02,
    RT_SIGNAL   = 0x04,
    RT_VECTOR   = 0x08
};

#define REDUCE_TYPE_MASK 0x0F

struct control_type {
    enum toktype toktype;
    mpr_type datatype;
    mpr_type casttype;
    uint8_t vec_len;
    uint8_t flags;
    /* end of generic_type */
    uint8_t cache_offset;
    uint8_t reduce_start;
    uint8_t reduce_stop;
    uint8_t branch_offset;
};

typedef union _token {
    enum toktype toktype;
    struct generic_type gen;
    struct literal_type lit;
    struct operator_type op;
    struct variable_type var;
    struct function_type fn;
    struct control_type con;
} mpr_token_t, *mpr_token;

#define VAR_ASSIGNED    0x0001
#define VAR_INSTANCED   0x0002
#define VAR_LEN_LOCKED  0x0004
#define VAR_SET_EXTERN  0x0008

typedef struct _var {
    char *name;
    mpr_type datatype;
    mpr_type casttype;
    uint8_t vec_len;
    uint8_t flags;
} mpr_var_t, *mpr_var;

static int strncmp_lc(const char *a, const char *b, int len)
{
    int i;
    for (i = 0; i < len; i++) {
        int diff = tolower(a[i]) - tolower(b[i]);
        RETURN_ARG_UNLESS(0 == diff, diff);
    }
    return 0;
}

#define FN_LOOKUP(LC, UC, CLOSE)                                    \
static expr_##LC##_t LC##_lookup(const char *s, int len)            \
{                                                                   \
    int i, j;                                                       \
    for (i = 0; i < N_##UC; i++) {                                  \
        if (LC##_tbl[i].name && strlen(LC##_tbl[i].name) == len     \
            && strncmp_lc(s, LC##_tbl[i].name, len)==0) {           \
            j = strlen(LC##_tbl[i].name);                           \
            if (CLOSE && i > RFN_HISTORY)                           \
                return s[j] == '.' ? i : UC##_UNKNOWN;              \
            /* check for parentheses */                             \
            if (s[j] != '(')                                        \
                return UC##_UNKNOWN;                                \
            else if (CLOSE && i > RFN_HISTORY && s[j + 1] != ')')   \
                return UC##_UNKNOWN;                                \
            return i;                                               \
        }                                                           \
    }                                                               \
    return UC##_UNKNOWN;                                            \
}
FN_LOOKUP(fn, FN, 0)
FN_LOOKUP(vfn, VFN, 0)
FN_LOOKUP(rfn, RFN, 1)

static int var_lookup(mpr_token_t *tok, const char *s, int len)
{
    if ('t' == *s && '_' == *(s+1)) {
        tok->toktype = TOK_TT;
        s += 2;
        len -= 2;
    }
    else
        tok->toktype = TOK_VAR;
    tok->var.idx = VAR_UNKNOWN;
    if (1 != len)
        return 0;
    if (*s == 'y')
        tok->var.idx = VAR_Y;
    else if ('x' == *s) {
        if ('$' == *(s+1)) {
            if (isdigit(*(s+2))) {
                /* literal input signal index */
                int num_digits = 1;
                while (isdigit(*s+1+num_digits))
                    ++num_digits;
                tok->var.idx = VAR_X + atoi(s+2);
                return num_digits + 1;
            }
            else
                tok->var.idx = VAR_X;
        }
        else
            tok->var.idx = VAR_X;
    }
    return 0;
}

static int const_lookup(mpr_token_t *tok, const char *s, int len)
{
    if (len == 2 && 'p' == *s && 'i' == *(s+1))
        tok->gen.flags |= CONST_PI;
    else if (len == 1 && *s == 'e')
        tok->gen.flags |= CONST_E;
    else
        return 1;
    tok->toktype = TOK_LITERAL;
    tok->gen.datatype = MPR_FLT;
    return 0;
}

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

static int expr_lex(const char *str, int idx, mpr_token_t *tok)
{
    int n=idx, i=idx;
    char c = str[idx];
    int integer_found = 0;
    tok->gen.datatype = MPR_INT32;
    tok->gen.casttype = 0;
    tok->gen.vec_len = 1;
    tok->var.vec_idx = 0;
    tok->gen.flags = 0;

    if (c==0) {
        tok->toktype = TOK_END;
        return idx;
    }

  again:

    i = idx;
    if (isdigit(c)) {
        do {
            c = str[++idx];
        } while (c && isdigit(c));
        n = atoi(str+i);
        integer_found = 1;
        if (c!='.' && c!='e') {
            tok->lit.val.i = n;
            tok->toktype = TOK_LITERAL;
            tok->lit.datatype = MPR_INT32;
            return idx;
        }
    }

    switch (c) {
    case '.':
        c = str[++idx];
        if (!isdigit(c) && c!='e') {
            if (integer_found) {
                tok->toktype = TOK_LITERAL;
                tok->lit.val.f = (float)n;
                tok->lit.datatype = MPR_FLT;
                return idx;
            }
            while (c && (isalpha(c)))
                c = str[++idx];
            ++i;
            if ((tok->fn.idx = vfn_lookup(str+i, idx-i)) != VFN_UNKNOWN) {
                tok->toktype = TOK_VFN_DOT;
                return idx + ((vfn_tbl[tok->fn.idx].arity == 1) ? 2 : 1);
            }
            else if ((tok->fn.idx = rfn_lookup(str+i, idx-i)) != RFN_UNKNOWN) {
                tok->toktype = TOK_RFN;
                /* skip over '()' for reduce functions but not reduce types other than 'history' */
                return tok->fn.idx >= RFN_FILTER ? idx : idx + 2;
            }
            else
                break;
        }
        do {
            c = str[++idx];
        } while (c && isdigit(c));
        if (c!='e') {
            tok->lit.val.f = atof(str+i);
            tok->toktype = TOK_LITERAL;
            tok->lit.datatype = MPR_FLT;
            return idx;
        }
        /* continue to next case 'e' */
    case 'e':
        if (!integer_found) {
            while (c && (isalpha(c) || isdigit(c) || c == '_'))
                c = str[++idx];
            if ((tok->fn.idx = fn_lookup(str+i, idx-i)) != FN_UNKNOWN)
                tok->toktype = TOK_FN;
            else if ((tok->fn.idx = vfn_lookup(str+i, idx-i)) != VFN_UNKNOWN)
                tok->toktype = TOK_VFN;
            else if (const_lookup(tok, str+i, idx-i))
                idx += var_lookup(tok, str+i, idx-i);
            return idx;
        }
        c = str[++idx];
        if (c!='-' && c!='+' && !isdigit(c)) {
            lex_error("Incomplete scientific notation `%s'.\n", str+i);
            break;
        }
        if (c=='-' || c=='+')
            c = str[++idx];
        while (c && isdigit(c))
            c = str[++idx];
        tok->toktype = TOK_LITERAL;
        tok->lit.datatype = MPR_DBL;
        tok->lit.val.d = atof(str+i);
        return idx;
    case '+':
        SET_TOK_OPTYPE(OP_ADD);
        return ++idx;
    case '-':
        /* could be either subtraction, negation, or lambda */
        c = str[++idx];
        if (c == '>') {
            tok->toktype = TOK_LAMBDA;
            return idx + 1;
        }
        i = idx - 2;
        /* back up one character */
        while (i && strchr(" \t\r\n", str[i]))
           --i;
        if (isalpha(str[i]) || isdigit(str[i]) || strchr(")]}", str[i])) {
            SET_TOK_OPTYPE(OP_SUBTRACT);
        }
        else
            tok->toktype = TOK_NEGATE;
        return idx;
    case '/':
        SET_TOK_OPTYPE(OP_DIVIDE);
        return ++idx;
    case '*':
        SET_TOK_OPTYPE(OP_MULTIPLY);
        return ++idx;
    case '%':
        SET_TOK_OPTYPE(OP_MODULO);
        return ++idx;
    case '=':
        /* could be '=', '==' */
        c = str[++idx];
        if (c == '=') {
            SET_TOK_OPTYPE(OP_IS_EQUAL);
            ++idx;
        }
        else
            tok->toktype = TOK_ASSIGN;
        return idx;
    case '<':
        /* could be '<', '<=', '<<' */
        SET_TOK_OPTYPE(OP_IS_LESS_THAN);
        c = str[++idx];
        if (c == '=') {
            tok->op.idx = OP_IS_LESS_THAN_OR_EQUAL;
            ++idx;
        }
        else if (c == '<') {
            tok->op.idx = OP_LEFT_BIT_SHIFT;
            ++idx;
        }
        return idx;
    case '>':
        /* could be '>', '>=', '>>' */
        SET_TOK_OPTYPE(OP_IS_GREATER_THAN);
        c = str[++idx];
        if (c == '=') {
            tok->op.idx = OP_IS_GREATER_THAN_OR_EQUAL;
            ++idx;
        }
        else if (c == '>') {
            tok->op.idx = OP_RIGHT_BIT_SHIFT;
            ++idx;
        }
        return idx;
    case '!':
        /* could be '!', '!=' */
        /* TODO: handle factorial case */
        SET_TOK_OPTYPE(OP_LOGICAL_NOT);
        c = str[++idx];
        if (c == '=') {
            tok->op.idx = OP_IS_NOT_EQUAL;
            ++idx;
        }
        return idx;
    case '&':
        /* could be '&', '&&' */
        SET_TOK_OPTYPE(OP_BITWISE_AND);
        c = str[++idx];
        if (c == '&') {
            tok->op.idx = OP_LOGICAL_AND;
            ++idx;
        }
        return idx;
    case '|':
        /* could be '|', '||' */
        SET_TOK_OPTYPE(OP_BITWISE_OR);
        c = str[++idx];
        if (c == '|') {
            tok->op.idx = OP_LOGICAL_OR;
            ++idx;
        }
        return idx;
    case '^':
        /* bitwise XOR */
        SET_TOK_OPTYPE(OP_BITWISE_XOR);
        return ++idx;
    case '(':
        tok->toktype = TOK_OPEN_PAREN;
        return ++idx;
    case ')':
        tok->toktype = TOK_CLOSE_PAREN;
        return ++idx;
    case '[':
        tok->toktype = TOK_OPEN_SQUARE;
        return ++idx;
    case ']':
        tok->toktype = TOK_CLOSE_SQUARE;
        return ++idx;
    case '{':
        tok->toktype = TOK_OPEN_CURLY;
        return ++idx;
    case '}':
        tok->toktype = TOK_CLOSE_CURLY;
        return ++idx;
    case '$':
        tok->toktype = TOK_DOLLAR;
        return ++idx;
    case ' ':
    case '\t':
    case '\r':
    case '\n':
        c = str[++idx];
        goto again;
    case ',':
        tok->toktype = TOK_COMMA;
        return ++idx;
    case '?':
        /* conditional */
        SET_TOK_OPTYPE(OP_IF);
        c = str[++idx];
        if (c == ':') {
            tok->op.idx = OP_IF_ELSE;
            ++idx;
        }
        return idx;
    case ':':
        tok->toktype = TOK_COLON;
        return ++idx;
    case ';':
        tok->toktype = TOK_SEMICOLON;
        return ++idx;
    case '_':
        tok->toktype = TOK_MUTED;
        return ++idx;
    default:
        if (!isalpha(c)) {
            lex_error("unknown character '%c' in lexer\n", c);
            break;
        }
        while (c && (isalpha(c) || isdigit(c) || c == '_'))
            c = str[++idx];
        if ((tok->fn.idx = fn_lookup(str+i, idx-i)) != FN_UNKNOWN)
            tok->toktype = TOK_FN;
        else if ((tok->fn.idx = vfn_lookup(str+i, idx-i)) != VFN_UNKNOWN)
            tok->toktype = TOK_VFN;
        else if (const_lookup(tok, str+i, idx-i))
            idx += var_lookup(tok, str+i, idx-i);
        return idx;
    }
    return 1;
}

struct _mpr_expr
{
    mpr_token tokens;
    mpr_token start;
    mpr_var vars;
    uint8_t offset;
    uint8_t n_tokens;
    uint8_t stack_size;
    uint8_t vec_len;
    uint16_t *in_hist_size;
    uint16_t out_hist_size;
    uint8_t n_vars;
    int8_t inst_ctl;
    int8_t mute_ctl;
    int8_t n_ins;
    uint16_t max_in_hist_size;
};

static void free_stack_vliterals(mpr_token_t *stk, int top)
{
    while (top >= 0) {
        if (TOK_VLITERAL == stk[top].toktype && stk[top].lit.val.ip)
            free(stk[top].lit.val.ip);
        --top;
    }
}

void mpr_expr_free(mpr_expr expr)
{
    int i;
    FUNC_IF(free, expr->in_hist_size);
    free_stack_vliterals(expr->tokens, expr->n_tokens - 1);
    FUNC_IF(free, expr->tokens);
    if (expr->n_vars && expr->vars) {
        for (i = 0; i < expr->n_vars; i++)
            free(expr->vars[i].name);
        free(expr->vars);
    }
    free(expr);
}

#ifdef TRACE_PARSE

static void printtoken(mpr_token_t *t, mpr_var_t *vars)
{
    int i, len = 128, l = 0, offset = 0;
    char s[128];
    char *dims[] = {"unknown", "history", "instance", 0, "signal", 0, 0, 0, "vector"};
    switch (t->toktype) {
        case TOK_LITERAL:
            switch (t->gen.flags & CONST_SPECIAL) {
                case CONST_MAXVAL:  snprintf(s, len, "lit.max");                    break;
                case CONST_MINVAL:  snprintf(s, len, "lit.min");                    break;
                case CONST_PI:      snprintf(s, len, "lit.pi");                     break;
                case CONST_E:       snprintf(s, len, "lit.e");                      break;
                default:
                    switch (t->gen.datatype) {
                        case MPR_FLT:   snprintf(s, len, "lit.%g", t->lit.val.f);   break;
                        case MPR_DBL:   snprintf(s, len, "lit.%g", t->lit.val.d);   break;
                        case MPR_INT32: snprintf(s, len, "lit.%d", t->lit.val.i);   break;
                    }                                                               break;
            }
                                                                                    break;
        case TOK_VLITERAL:
            offset = snprintf(s, len, "[");
            switch (t->gen.datatype) {
                case MPR_FLT:
                    for (i = 0; i < t->lit.vec_len; i++)
                        offset += snprintf(s + offset, len - offset, "%g,", t->lit.val.fp[i]);
                    break;
                case MPR_DBL:
                    for (i = 0; i < t->lit.vec_len; i++)
                        offset += snprintf(s + offset, len - offset, "%g,", t->lit.val.dp[i]);
                    break;
                case MPR_INT32:
                    for (i = 0; i < t->lit.vec_len; i++)
                        offset += snprintf(s + offset, len - offset, "%d,", t->lit.val.ip[i]);
                    break;
            }
            --offset;
            snprintf(s + offset, len - offset, "]");
            break;
        case TOK_OP:            snprintf(s, len, "op.%s", op_tbl[t->op.idx].name);  break;
        case TOK_OPEN_CURLY:    snprintf(s, len, "{");                              break;
        case TOK_OPEN_PAREN:    snprintf(s, len, "( arity %d", t->fn.arity);        break;
        case TOK_OPEN_SQUARE:   snprintf(s, len, "[");                              break;
        case TOK_CLOSE_CURLY:   snprintf(s, len, "}");                              break;
        case TOK_CLOSE_PAREN:   snprintf(s, len, ")");                              break;
        case TOK_CLOSE_SQUARE:  snprintf(s, len, "]");                              break;

        case TOK_ASSIGN:
        case TOK_ASSIGN_CONST:
        case TOK_ASSIGN_USE:
        case TOK_ASSIGN_TT:
            l = snprintf(s, len, "ASSIGN_TO:");
        case TOK_VAR:
        case TOK_TT: {
            if (TOK_TT == t->toktype || TOK_ASSIGN_TT == t->toktype)
                l += snprintf(s + l, len - l, "tt.");
            else
                l += snprintf(s + l, len - l, "var.");


            if (t->var.idx == VAR_Y)
                l += snprintf(s + l, len - l, "y");
            else if (t->var.idx >= VAR_X) {
                l += snprintf(s + l, len - l, "x");
                if (t->gen.flags & VAR_SIG_IDX)
                    l += snprintf(s + l, len - l, "$N");
                else
                    l += snprintf(s + l, len - l, "$%d", t->var.idx - VAR_X);
            }
            else
                l += snprintf(s + l, len - l, "%s%s", vars ? vars[t->var.idx].name : "?",
                              vars ? (vars[t->var.idx].flags & VAR_INSTANCED) ? ".N" : ".0" : ".?");

            if (t->gen.flags & VAR_HIST_IDX)
                l += snprintf(s + l, len - l, "{N}");

            if (TOK_TT == t->toktype)
                break;

            if (t->gen.flags & VAR_VEC_IDX)
                l += snprintf(s + l, len - l, "[N");
            else
                l += snprintf(s + l, len - l, "[%u", t->var.vec_idx);
            if (t->var.idx >= VAR_Y)
                l += snprintf(s + l, len - l, "]");
            else
                l += snprintf(s + l, len - l, "/%u]", vars ? vars[t->var.idx].vec_len : 0);

            if (t->toktype & TOK_ASSIGN)
                snprintf(s + l, len - l, " offset:%d", t->var.offset);
            break;
        }
        case TOK_VAR_NUM_INST:
            if (t->var.idx == VAR_Y)
                snprintf(s, len, "var.y.count()");
            else if (t->var.idx >= VAR_X)
                snprintf(s, len, "var.x$%d.count()", t->var.idx - VAR_X);
            else
                snprintf(s, len, "var.%s%s.count()", vars ? vars[t->var.idx].name : "?",
                         vars ? (vars[t->var.idx].flags & VAR_INSTANCED) ? ".N" : ".0" : ".?");
            break;
        case TOK_FN:        snprintf(s, len, "fn.%s(arity %d)", fn_tbl[t->fn.idx].name, t->fn.arity); break;
        case TOK_COMMA:     snprintf(s, len, ",");                              break;
        case TOK_COLON:     snprintf(s, len, ":");                              break;
        case TOK_VECTORIZE: snprintf(s, len, "VECT(%d)", t->fn.arity);          break;
        case TOK_NEGATE:    snprintf(s, len, "-");                              break;
        case TOK_VFN:
        case TOK_VFN_DOT:   snprintf(s, len, "vfn.%s(arity %d)",
                                     vfn_tbl[t->fn.idx].name, t->fn.arity);     break;
        case TOK_RFN:
            if (RFN_HISTORY == t->fn.idx)
                snprintf(s, len, "rfn.history(%d:%d)", t->con.reduce_start, t->con.reduce_stop);
            else if (RFN_VECTOR == t->fn.idx) {
                if (t->con.flags & USE_VAR_LEN)
                    snprintf(s, len, "rfn.vector(%d:len)", t->con.reduce_start);
                else
                    snprintf(s, len, "rfn.vector(%d:%d)", t->con.reduce_start, t->con.reduce_stop);
            }
            else
                snprintf(s, len, "rfn.%s()", rfn_tbl[t->fn.idx].name);
            break;
        case TOK_LAMBDA:
            snprintf(s, len, "LAMBDA(%d)", t->fn.arity);                        break;
        case TOK_COPY_FROM:
            snprintf(s, len, "COPY_FROM(-%d)", t->con.cache_offset);            break;
        case TOK_MOVE:
            snprintf(s, len, "MOVE(-%d)", t->con.cache_offset);                 break;
        case TOK_LOOP_START:
            snprintf(s, len, "<%s loop start>", dims[t->con.flags & REDUCE_TYPE_MASK]); break;
        case TOK_REDUCING:
            if (t->con.flags & USE_VAR_LEN)
                snprintf(s, len, "<reducing %s[%d:len]>", dims[t->con.flags & REDUCE_TYPE_MASK],
                         t->con.reduce_start);
            else
                snprintf(s, len, "<reducing %s[%d:%d]>", dims[t->con.flags & REDUCE_TYPE_MASK],
                         t->con.reduce_start, t->con.reduce_stop);
            break;
        case TOK_LOOP_END:
            switch (t->con.flags & REDUCE_TYPE_MASK) {
                case RT_HISTORY:
                    snprintf(s, len, "<history[%d:%d] loop end, offsets=[%d,%d]>",
                             -t->con.reduce_start, -t->con.reduce_stop, t->con.branch_offset,
                             t->con.cache_offset);
                    break;
                case RT_VECTOR:
                    if (t->con.flags & USE_VAR_LEN)
                        snprintf(s, len, "<vector[%d:len] loop end, offsets=[%d,%d]>",
                                 t->con.reduce_start, t->con.branch_offset, t->con.cache_offset);
                    else
                        snprintf(s, len, "<vector[%d:%d] loop end, offsets=[%d,%d]>",
                                 t->con.reduce_start, t->con.reduce_stop, t->con.branch_offset,
                                 t->con.cache_offset);
                    break;
                default:
                    snprintf(s, len, "<%s loop end, offsets=[-%d, -%d]>",
                             dims[t->con.flags & REDUCE_TYPE_MASK], t->con.branch_offset,
                             t->con.cache_offset);
            }
            break;
        case TOK_SP_ADD:            snprintf(s, len, "<sp add %d>", t->lit.val.i);  break;
        case TOK_SEMICOLON:         snprintf(s, len, "semicolon");                  break;
        case TOK_END:               printf("END\n");                                return;
        default:                    printf("(unknown token)\n");                    return;
    }
    printf("%s", s);
    /* indent */
    len = strlen(s);
    for (i = len; i < 40; i++)
        printf(" ");
    printf("%c%u", t->gen.datatype, t->gen.vec_len);
    if (t->gen.casttype)
        printf("->%c", t->gen.casttype);
    else
        printf("   ");
    if (t->gen.flags & VEC_LEN_LOCKED)
        printf(" vec_lock");
    if (t->gen.flags & TYPE_LOCKED)
        printf(" type_lock");
    if (TOK_ASSIGN & t->toktype && t->gen.flags & CLEAR_STACK)
        printf(" clear");
    printf("\n");
}

static void printstack(const char *s, mpr_token_t *stk, int sp, mpr_var_t *vars, int show_init_line)
{
    int i, j, indent = 0, can_advance = 1;
    if (s)
        printf("%s:\n", s);
    if (sp < 0) {
        printf("  --- <EMPTY> ---\n");
        return;
    }
    for (i = 0; i <= sp; i++) {
        if (show_init_line && can_advance) {
            switch (stk[i].toktype) {
                case TOK_ASSIGN_CONST:
                case TOK_ASSIGN:
                case TOK_ASSIGN_USE:
                case TOK_ASSIGN_TT:
                    /* look ahead for future assignments */
                    for (j = i + 1; j <= sp; j++) {
                        if (stk[j].toktype < TOK_ASSIGN)
                            continue;
                        if (TOK_ASSIGN_CONST == stk[j].toktype && stk[j].var.idx != VAR_Y)
                            break;
                        if (stk[j].gen.flags & VAR_HIST_IDX)
                            break;
                        for (j = 0; j < indent; j++)
                            printf(" ");
                        can_advance = 0;
                        break;
                    }
                    break;
                case TOK_RFN:
                case TOK_VAR:
                    if (stk[i].var.idx >= VAR_X)
                        can_advance = 0;
                    break;
                default:
                    break;
            }
            if (i && !can_advance)
                printf("  --- <INITIALISATION DONE> ---\n");
        }
        printf("  %2d: ", i);
        printtoken(&stk[i], vars);
    }
}

void printexpr(const char *s, mpr_expr e)
{
    printstack(s, e->tokens, e->n_tokens - 1, e->vars, 1);
}

#endif /* TRACE_PARSE */

static mpr_type compare_token_datatype(mpr_token_t tok, mpr_type type)
{
    mpr_type type2 = tok.gen.casttype ? tok.gen.casttype : tok.gen.datatype;
    if (tok.toktype >= TOK_LOOP_START)
        return type;
    /* return the higher datatype, 'd' < 'f' < i' */
    return type < type2 ? type : type2;
}

static mpr_type promote_token(mpr_token_t *stk, int sp, mpr_type type, int vec_len, mpr_var_t *vars)
{
    mpr_token_t *tok;

    /* don't promote type of variable indices */
    if (stk[sp].gen.datatype == type && stk[sp].gen.casttype == MPR_INT32)
        return type;

    while (TOK_COPY_FROM == stk[sp].toktype) {
        int offset = stk[sp].con.cache_offset + 1;
        stk[sp].gen.datatype = type;
        if (vec_len && !(stk[sp].gen.flags & VEC_LEN_LOCKED))
            stk[sp].gen.vec_len = vec_len;
        while (offset > 0 && sp > 0) {
            --sp;
            /* TODO: merge TOK_SP_ADD into tok_arity fn */
            if (TOK_SP_ADD == stk[sp].toktype)
                offset -= stk[sp].lit.val.i;
            else if (TOK_LOOP_START == stk[sp].toktype && stk[sp].con.flags & RT_INSTANCE)
                --offset;
            else if (TOK_LOOP_END == stk[sp].toktype && stk[sp].con.flags & RT_INSTANCE)
                ++offset;
            else if (stk[sp].toktype <= TOK_MOVE)
                offset += tok_arity(stk[sp]) - 1;
        }
        assert(sp > 0);
    }
    tok = &stk[sp];

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
        if (MPR_INT32 == tok->gen.datatype || MPR_DBL == type) {
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

static void lock_vec_len(mpr_token_t *stk, int sp)
{
    int i = sp, arity = 1;
    while ((i >= 0) && arity--) {
        stk[i].gen.flags |= VEC_LEN_LOCKED;
        switch (stk[i].toktype) {
            case TOK_OP:        arity += op_tbl[stk[i].op.idx].arity;   break;
            case TOK_FN:        arity += fn_tbl[stk[i].fn.idx].arity;   break;
            case TOK_VECTORIZE: arity += stk[i].fn.arity;               break;
            default:                                                    break;
        }
        --i;
    }
}

static int replace_special_constants(mpr_token_t *stk, int sp)
{
    while (sp >= 0) {
        if (stk[sp].toktype != TOK_LITERAL || !(stk[sp].gen.flags & CONST_SPECIAL)) {
            --sp;
            continue;
        }
        switch (stk[sp].gen.flags & CONST_SPECIAL) {
            case CONST_MAXVAL:
                switch (stk[sp].lit.datatype) {
                    case MPR_INT32: stk[sp].lit.val.i = INT_MAX;    break;
                    case MPR_FLT:   stk[sp].lit.val.f = FLT_MAX;    break;
                    case MPR_DBL:   stk[sp].lit.val.d = DBL_MAX;    break;
                    default:                                        goto error;
                }
                break;
            case CONST_MINVAL:
                switch (stk[sp].lit.datatype) {
                    case MPR_INT32: stk[sp].lit.val.i = INT_MIN;    break;
                    case MPR_FLT:   stk[sp].lit.val.f = -FLT_MAX;   break;
                    case MPR_DBL:   stk[sp].lit.val.d = -DBL_MAX;   break;
                    default:                                        goto error;
                }
                break;
            case CONST_PI:
                switch (stk[sp].lit.datatype) {
                    case MPR_FLT:   stk[sp].lit.val.f = M_PI;       break;
                    case MPR_DBL:   stk[sp].lit.val.d = M_PI;       break;
                    default:                                        goto error;
                }
                break;
            case CONST_E:
                switch (stk[sp].lit.datatype) {
                    case MPR_FLT:   stk[sp].lit.val.f = M_E;        break;
                    case MPR_DBL:   stk[sp].lit.val.d = M_E;        break;
                    default:                                        goto error;
                }
                break;
            default:
                continue;
        }
        stk[sp].gen.flags &= ~CONST_SPECIAL;
        --sp;
    }
    return 0;
error:
#if TRACE_PARSE
    printf("Illegal type found when replacing special constants.\n");
#endif
    return -1;
}

static int precompute(mpr_expr_stack eval_stk, mpr_token_t *stk, int len, int vec_len)
{
    int i;
    struct _mpr_expr e = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, -1, -1};
    mpr_value_t v = {0, 0, 1, 1, 0, 1};
    mpr_value_buffer_t b = {0, 0, -1};
    void *s;

    if (replace_special_constants(stk, len-1))
        return 0;
    e.start = stk;
    e.n_tokens = e.stack_size = len;
    e.vec_len = vec_len;

    v.inst = &b;
    v.vlen = vec_len;
    v.type = stk[len - 1].gen.datatype;
    s = b.samps = malloc(mpr_type_get_size(v.type) * vec_len);

    expr_stack_realloc(eval_stk, len * vec_len);

    if (!(mpr_expr_eval(eval_stk, &e, 0, 0, &v, 0, 0, 0) & 1)) {
        free(s);
        return 0;
    }

    /* free token vector memory if necessary */
    free_stack_vliterals(stk, len - 1);

    switch (v.type) {
#define TYPED_CASE(MTYPE, TYPE, T)                                      \
        case MTYPE:                                                     \
            if (vec_len > 1) {                                          \
                stk[0].toktype = TOK_VLITERAL;                          \
                stk[0].lit.val.T##p = malloc(vec_len * sizeof(TYPE));   \
                for (i = 0; i < vec_len; i++)                           \
                    stk[0].lit.val.T##p[i] = ((TYPE*)s)[i];             \
            }                                                           \
            else {                                                      \
                stk[0].toktype = TOK_LITERAL;                           \
                stk[0].lit.val.T = ((TYPE*)s)[0];                       \
            }                                                           \
            break;
        TYPED_CASE(MPR_INT32, int, i)
        TYPED_CASE(MPR_FLT, float, f)
        TYPED_CASE(MPR_DBL, double, d)
#undef TYPED_CASE
        default:
            free(s);
            return 0;
    }
    stk[0].gen.flags &= ~CONST_SPECIAL;
    stk[0].gen.datatype = v.type;
    free(s);
    return len - 1;
}

static int check_type(mpr_expr_stack eval_stk, mpr_token_t *stk, int sp, mpr_var_t *vars,
                      int enable_optimize)
{
    /* TODO: enable precomputation of const-only vectors */
    int i, arity, can_precompute = 1, optimize = NONE;
    mpr_type type = stk[sp].gen.datatype;
    uint8_t vec_len = stk[sp].gen.vec_len;
    switch (stk[sp].toktype) {
        case TOK_OP:
            if (stk[sp].op.idx == OP_IF) {
                trace("Ternary operator is missing operand.\n");
                return -1;
            }
            arity = op_tbl[stk[sp].op.idx].arity;
            break;
        case TOK_FN:
            arity = fn_tbl[stk[sp].fn.idx].arity;
            if (stk[sp].fn.idx >= FN_DEL_IDX)
                can_precompute = 0;
            break;
        case TOK_VFN:
            arity = vfn_tbl[stk[sp].fn.idx].arity;
            break;
        case TOK_VECTORIZE:
            arity = stk[sp].fn.arity;
            can_precompute = 0;
            break;
        case TOK_ASSIGN:
        case TOK_ASSIGN_CONST:
        case TOK_ASSIGN_TT:
        case TOK_ASSIGN_USE:
            arity = NUM_VAR_IDXS(stk[sp].gen.flags) + 1;
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
            if (stk[i].toktype >= TOK_LOOP_START) {
                can_precompute = enable_optimize = 0;
                continue;
            }

            if (stk[i].toktype == TOK_FN) {
                if (fn_tbl[stk[i].fn.idx].arity)
                    can_precompute = 0;
            }
            else if (stk[i].toktype > TOK_VLITERAL)
                can_precompute = 0;

            if (skip == 0) {
                int j;
                if (enable_optimize && stk[i].toktype == TOK_LITERAL && stk[sp].toktype == TOK_OP
                    && depth <= op_tbl[stk[sp].op.idx].arity) {
                    if (const_tok_is_zero(stk[i])) {
                        /* mask and bitshift, depth == 1 or 2 */
                        optimize = (op_tbl[stk[sp].op.idx].optimize_const_ops >> (depth - 1) * 4) & 0xF;
                    }
                    else if (const_tok_equals_one(stk[i])) {
                        optimize = (op_tbl[stk[sp].op.idx].optimize_const_ops >> (depth + 1) * 4) & 0xF;
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
                    type = compare_token_datatype(stk[j], type);
                    if (stk[j].gen.vec_len > vec_len)
                        vec_len = stk[j].gen.vec_len;
                    if (TOK_COPY_FROM == stk[j].toktype) {
                        uint8_t offset = stk[j].con.cache_offset + 1;
                        uint8_t vec_reduce = 0;
                        while (offset > 0 && j > 0) {
                            --j;
                            if (TOK_SP_ADD == stk[j].toktype)
                                offset -= stk[j].lit.val.i;
                            else if (TOK_LOOP_START == stk[j].toktype) {
                                if (stk[j].con.flags & RT_INSTANCE)
                                    --offset;
                                else if (stk[j].con.flags & RT_VECTOR)
                                    ++vec_reduce;
                            }
                            else if (TOK_LOOP_END == stk[j].toktype) {
                                if (stk[j].con.flags & RT_INSTANCE)
                                    ++offset;
                                else if (stk[j].con.flags & RT_VECTOR)
                                    --vec_reduce;
                            }
                            else if (stk[j].toktype <= TOK_MOVE)
                                offset += tok_arity(stk[j]) - 1;
                            type = compare_token_datatype(stk[j], type);
                            if (vec_reduce <= 0 && stk[j].gen.vec_len > vec_len)
                                vec_len = stk[j].gen.vec_len;
                        }
                        assert(j > 0);
                    }
                } while (TOK_COPY_FROM == stk[j].toktype);
                --depth;
                if (depth == 0)
                    break;
            }
            else
                --skip;

            switch (stk[i].toktype) {
                case TOK_OP:         skip += op_tbl[stk[i].op.idx].arity;       break;
                case TOK_FN:         skip += fn_tbl[stk[i].fn.idx].arity;       break;
                case TOK_VFN:
                    skip += vfn_tbl[stk[i].fn.idx].arity;
                    if (VFN_MAXMIN == stk[i].fn.idx || VFN_SUMNUM == stk[i].fn.idx)
                        --skip; /* these functions have 2 outputs */
                    break;
                case TOK_VECTORIZE:  skip += stk[i].fn.arity;                   break;
                case TOK_ASSIGN_USE: ++skip;                                    break;
                case TOK_VAR:        skip += NUM_VAR_IDXS(stk[i].gen.flags);    break;
                default:                                                        break;
            }
        }

        if (depth)
            return -1;

        if (enable_optimize && !can_precompute) {
            switch (optimize) {
                case BAD_EXPR:
                    trace("Operator '%s' cannot have zero operand.\n", op_tbl[stk[sp].op.idx].name);
                    return -1;
                case GET_ZERO:
                case GET_ONE: {
                    /* finish walking down compound arity */
                    int _arity = 0;
                    while ((_arity += tok_arity(stk[i])) && i >= 0) {
                        --_arity;
                        --i;
                    }
                    stk[i].toktype = TOK_LITERAL;
                    stk[i].gen.datatype = MPR_INT32;
                    stk[i].lit.val.i = optimize == GET_ZERO ? 0 : 1;
                    stk[i].gen.casttype = 0;
                    return i;
                }
                case GET_OPER:
                    /* copy tokens for non-zero operand */
                    for (; i < operand; i++)
                        memcpy(stk + i, stk + i + 1, sizeof(mpr_token_t));
                    return i;
                default:
                    break;
            }
        }

        /* walk down stack distance of arity again, promoting types
         * this time we will also touch sub-arguments */
        i = sp;
        switch (stk[sp].toktype) {
            case TOK_VECTORIZE:  skip = stk[sp].fn.arity;                   depth = 0;      break;
            case TOK_ASSIGN_USE: skip = 1;                                  depth = 0;      break;
            case TOK_VAR:        skip = NUM_VAR_IDXS(stk[sp].gen.flags);    depth = 0;      break;
            default:             skip = 0;                                  depth = arity;  break;
        }
        promote_token(stk, i, type, 0, 0);
        while (--i >= 0) {
            int j = i;
            if (TOK_LOOP_END == stk[i].toktype && stk[i].con.flags & RT_VECTOR)
                vec_reduce = 1;
            else if (TOK_LOOP_START == stk[i].toktype && stk[i].con.flags & RT_VECTOR)
                vec_reduce = 0;
            if (stk[i].toktype >= TOK_LOOP_START)
                continue;

            /* promote types within range of compound arity */
            do {
                if (skip <= 0) {
                    promote_token(stk, j, type, vec_reduce ? 0 : vec_len, 0);
                    --depth;
                    if (!vec_reduce && !(stk[j].gen.flags & VEC_LEN_LOCKED)) {
                        stk[j].var.vec_len = vec_len;
                        if (TOK_VAR == stk[j].toktype && stk[j].var.idx < N_USER_VARS)
                            vars[stk[j].var.idx].vec_len = vec_len;
                    }
                }
                else {
                    promote_token(stk, j, type, 0, 0);
                }

                if (TOK_COPY_FROM == stk[j].toktype) {
                    int offset = stk[j].con.cache_offset + 1;
                    while (offset > 0 && j > 0) {
                        --j;
                        if (TOK_SP_ADD == stk[j].toktype)
                            offset -= stk[j].lit.val.i;
                        else if (TOK_LOOP_START == stk[j].toktype && stk[j].con.flags & RT_INSTANCE)
                            --offset;
                        else if (TOK_LOOP_END == stk[j].toktype && stk[j].con.flags & RT_INSTANCE)
                            ++offset;
                        else if (stk[j].toktype <= TOK_MOVE)
                            offset += tok_arity(stk[j]) - 1;
                        promote_token(stk, j, type, 0, 0);
                    }
                    assert(j > 0);
                }
            } while (TOK_COPY_FROM == stk[j].toktype);

            switch (stk[i].toktype) {
                case TOK_OP:
                    if (skip > 0)
                        skip += op_tbl[stk[i].op.idx].arity;
                    else
                        depth += op_tbl[stk[i].op.idx].arity;
                    break;
                case TOK_FN:
                    if (skip > 0)
                        skip += fn_tbl[stk[i].fn.idx].arity;
                    else
                        depth += fn_tbl[stk[i].fn.idx].arity;
                    break;
                case TOK_VFN:
                    skip += vfn_tbl[stk[i].fn.idx].arity + 1;
                    break;
                case TOK_VECTORIZE:
                    skip = stk[i].fn.arity + 1;
                    break;
                case TOK_ASSIGN_USE:
                    ++skip;
                    ++depth;
                    break;
                case TOK_VAR: {
                    int num = NUM_VAR_IDXS(stk[i].gen.flags);
                    if (skip > 0)
                        skip += num;
                    else
                        depth += num;
                    break;
                }
                default:
                    break;
            }

            if (skip > 0)
                --skip;
            if (depth <= 0 && skip <= 0)
                break;
        }
    }

    if (!(stk[sp].gen.flags & VEC_LEN_LOCKED)) {
        if (stk[sp].toktype != TOK_VFN || VFN_SORT == stk[sp].fn.idx)
            stk[sp].gen.vec_len = vec_len;
    }

    /* if stack within bounds of arity was only constants, we're ok to compute */
    if (enable_optimize && can_precompute) {
        int len = precompute(eval_stk, &stk[sp - arity], arity + 1, vec_len);
        return sp - len;
    }
    else
        return sp;
}

static int substack_len(mpr_token_t *stk, int sp)
{
    int idx = sp, arity = 0;
    do {
        if (stk[idx].toktype < TOK_LOOP_END)
            --arity;
        arity += tok_arity(stk[idx]);
        if (TOK_ASSIGN & stk[idx].toktype)
            ++arity;
        --idx;
    } while (arity >= 0 && idx >= 0);
    return sp - idx;
}

static int check_assign_type_and_len(mpr_expr_stack eval_stk, mpr_token_t *stk, int sp,
                                     mpr_var_t *vars)
{
    int i = sp, j, optimize = 1, expr_len = 0;
    uint8_t vec_len = 0;
    int8_t var = stk[sp].var.idx;

    while (i >= 0 && (stk[i].toktype & TOK_ASSIGN) && (stk[i].var.idx == var)) {
        int num_var_idx = NUM_VAR_IDXS(stk[i].gen.flags);
        vec_len += stk[i].gen.vec_len;
        --i;
        for (j = 0; j < num_var_idx; j++)
            i -= substack_len(stk, i - j);
    }

    j = i;
    while (j < sp && !(stk[j].toktype & TOK_ASSIGN))
        ++j;

    expr_len = sp - j;
    expr_len += substack_len(stk, j);

    if (expr_len > sp + 1) {
        trace("Malformed expression (1)\n");
        return -1;
    }
    promote_token(stk, i, stk[sp].gen.datatype, 0, vars);
    if (check_type(eval_stk, stk, i, vars, optimize) == -1)
        return -1;
    promote_token(stk, i, stk[sp].gen.datatype, 0, vars);

    if (stk[sp].var.idx < N_USER_VARS) {
        /* Check if this expression assignment is instance-reducing */
        int reducing = 1, skipping = 0;
        for (i = 0; i < expr_len; i++) {
            switch (stk[sp - i].toktype) {
                case TOK_LOOP_START:
                    skipping = 0;
                    break;
                case TOK_LOOP_END:
                    skipping = 1;
                    reducing *= 2;
                    break;
                case TOK_VAR:
                    if (!skipping && stk[sp - i].var.idx >= VAR_X)
                        reducing = 0;
                    break;
                default:
                    break;
            }
        }
        if (reducing > 1 && (vars[stk[sp].var.idx].flags & VAR_INSTANCED))
            vars[stk[sp].var.idx].flags &= ~VAR_INSTANCED;
    }

    if (!(stk[sp].gen.flags & VAR_HIST_IDX))
        return 0;

    /* Need to move assignment statements to beginning of stack. */

    if (expr_len == sp + 1) {
        /* This statement is already at the start of the expression stack. */
        return 0;
    }

    for (i = sp - expr_len; i > 0; i--) {
        if (stk[i].toktype & TOK_ASSIGN && !(stk[i].gen.flags & VAR_HIST_IDX))
            break;
    }

    if (i > 0) {
        /* This expression statement needs to be moved. */
        mpr_token_t *temp = alloca(expr_len * sizeof(mpr_token_t));
        memcpy(temp, stk + sp - expr_len + 1, expr_len * sizeof(mpr_token_t));
        memcpy(stk + expr_len, stk, (sp - expr_len + 1) * sizeof(mpr_token_t));
        memcpy(stk, temp, expr_len * sizeof(mpr_token_t));
    }

    return 0;
}

static int find_var_by_name(mpr_var_t *vars, int n_vars, const char *str, int len)
{
    /* check if variable name matches known variable */
    int i;
    for (i = 0; i < n_vars; i++) {
        if (strlen(vars[i].name) == len && strncmp(vars[i].name, str, len)==0)
            return i;
    }
    return -1;
}

static int _eval_stack_size(mpr_token_t *token_stack, int token_stack_len)
{
    int i = 0, sp = 0, eval_stack_len = 0;
    mpr_token_t *tok = token_stack;
    while (i < token_stack_len && tok->toktype != TOK_END) {
        switch (tok->toktype) {
            case TOK_LOOP_START:
            case TOK_LITERAL:           ++sp;                                   break;
            case TOK_VAR:
            case TOK_TT:                sp -= NUM_VAR_IDXS(tok->gen.flags) - 1; break;
            case TOK_OP:                sp -= op_tbl[tok->op.idx].arity - 1;    break;
            case TOK_FN:                sp -= fn_tbl[tok->fn.idx].arity - 1;    break;
            case TOK_VFN:               sp -= vfn_tbl[tok->fn.idx].arity - 1;   break;
            case TOK_SP_ADD:            sp += tok->lit.val.i;                   break;
            case TOK_LOOP_END:          --sp;                                   break;
            case TOK_VECTORIZE:         sp -= tok->fn.arity - 1;                break;
            case TOK_ASSIGN:
            case TOK_ASSIGN_USE:
            case TOK_ASSIGN_CONST:
            case TOK_ASSIGN_TT:
                sp -= NUM_VAR_IDXS(tok->gen.flags);
                if (tok->toktype != TOK_ASSIGN_USE)
                    --sp;
                break;
            case TOK_COPY_FROM:         ++sp;                                   break;
            case TOK_MOVE:              sp -= tok->con.cache_offset;            break;
            default:
                return -1;
        }
        if (sp > eval_stack_len)
            eval_stack_len = sp;
        ++tok;
        ++i;
    }
    return eval_stack_len;
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
    memcpy(out + out_idx, &x, sizeof(mpr_token_t));                 \
}

#define PUSH_INT_TO_OUTPUT(x)       \
{                                   \
    tok.toktype = TOK_LITERAL;      \
    tok.lit.datatype = MPR_INT32;   \
    tok.lit.val.i = x;              \
    tok.lit.flags &= ~CONST_SPECIAL;\
    PUSH_TO_OUTPUT(tok);            \
}

#define POP_OUTPUT() ( out_idx-- )

#define PUSH_TO_OPERATOR(x)                                         \
{                                                                   \
    {FAIL_IF(++op_idx >= STACK_SIZE, "Stack size exceeded. (2)");}  \
    memcpy(op + op_idx, &x, sizeof(mpr_token_t));                   \
}

#define POP_OPERATOR() ( op_idx-- )

#define POP_OPERATOR_TO_OUTPUT()                            \
{                                                           \
    PUSH_TO_OUTPUT(op[op_idx]);                             \
    out_idx = check_type(eval_stk, out, out_idx, vars, 1);  \
    {FAIL_IF(out_idx < 0, "Malformed expression (3).");}    \
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

#define ADD_TO_VECTOR()                                             \
{                                                                   \
    switch (out[out_idx].toktype) {                                 \
        case TOK_LOOP_END:                                          \
            op[op_idx].gen.vec_len += out[out_idx-1].gen.vec_len;   \
            ++op[op_idx].fn.arity;                                  \
            break;                                                  \
        case TOK_LITERAL:                                           \
            if (vectorizing && _squash_to_vector(out, out_idx)) {   \
                POP_OUTPUT();                                       \
                break;                                              \
            }                                                       \
        default:                                                    \
            op[op_idx].gen.vec_len += out[out_idx].gen.vec_len;     \
            ++op[op_idx].fn.arity;                                  \
    }                                                               \
}

int _squash_to_vector(mpr_token_t *stk, int idx)
{
    mpr_token_t *a = stk + idx, *b = a - 1;
    if (idx < 1 || b->gen.flags & VEC_LEN_LOCKED)
        return 0;
    if (TOK_LITERAL == b->toktype) {

        int i;
        void *tmp;
        mpr_type type = compare_token_datatype(*a, b->lit.datatype);
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
        mpr_type type = compare_token_datatype(*a, b->lit.datatype);
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

MPR_INLINE static int _reduce_type_from_fn_idx(int fn)
{
    switch (fn) {
        case RFN_HISTORY:   return RT_HISTORY;
        case RFN_INSTANCE:  return RT_INSTANCE;
        case RFN_SIGNAL:    return RT_SIGNAL;
        case RFN_VECTOR:    return RT_VECTOR;
        default:            return RT_UNKNOWN;
    }
}

static const char* _get_var_str_and_len(const char* str, int last_char, int *len)
{
    int idx = last_char;
    char c = str[idx];
    while (idx >= 0 && c && (isalpha(c) || isdigit(c) || '_' == c)) {
        if (--idx >= 0)
            c = str[idx];
    }
    *len = last_char - idx;
    return str + idx + 1;
}

typedef struct _temp_var_cache {
    const char *in_name;
    const char *accum_name;
    struct _temp_var_cache *next;
    int scope_start;
    uint8_t in_pos;
    uint8_t accum_pos;
} temp_var_cache_t, *temp_var_cache;

#define ASSIGN_MASK (TOK_VAR | TOK_OPEN_SQUARE | TOK_COMMA | TOK_CLOSE_SQUARE | TOK_CLOSE_CURLY \
                     | TOK_OPEN_CURLY | TOK_NEGATE | TOK_LITERAL | TOK_COLON)
#define OBJECT_TOKENS (TOK_VAR | TOK_LITERAL | TOK_FN | TOK_VFN | TOK_MUTED | TOK_NEGATE \
                       | TOK_OPEN_PAREN | TOK_OPEN_SQUARE | TOK_OP | TOK_TT)

/*! Use Dijkstra's shunting-yard algorithm to parse expression into RPN stack. */
mpr_expr mpr_expr_new_from_str(mpr_expr_stack eval_stk, const char *str, int n_ins,
                               const mpr_type *in_types, const int *in_vec_lens, mpr_type out_type,
                               int out_vec_len)
{
    mpr_token_t out[STACK_SIZE];
    mpr_token_t op[STACK_SIZE];
    int i, lex_idx = 0, out_idx = -1, op_idx = -1;
    int oldest_in[MAX_NUM_MAP_SRC], oldest_out = 0, max_vector = 1;

    /* TODO: use bitflags instead? */
    uint8_t assigning = 0, is_const = 1, out_assigned = 0, muted = 0, vectorizing = 0;
    int var_flags = 0;
    uint8_t reduce_types = 0;
    int allow_toktype = 0x2FFFFF;
    int vec_len_ctx = 0;

    mpr_var_t vars[N_USER_VARS];
    temp_var_cache temp_vars = NULL;
    /* TODO: optimise these vars */
    int n_vars = 0;
    int inst_ctl = -1;
    int mute_ctl = -1;
    mpr_token_t tok;
    mpr_type var_type;
    mpr_expr expr;

    RETURN_ARG_UNLESS(str && n_ins && in_types && in_vec_lens, 0);
    for (i = 0; i < n_ins; i++)
        oldest_in[i] = 0;

    /* ignoring spaces at start of expression */
    while (str[lex_idx] == ' ') ++lex_idx;
    {FAIL_IF(!str[lex_idx], "No expression found.");}

    assigning = 1;
    allow_toktype = TOK_VAR | TOK_TT | TOK_OPEN_SQUARE | TOK_MUTED;

    var_type = out_type;
    for (i = 0; i < n_ins; i++) {
        if (var_type == in_types[i])
            continue;
        if (MPR_INT32 == var_type || MPR_DBL == in_types[i])
            var_type = in_types[i];
    }

    memset(out, 0, sizeof(mpr_token_t) * STACK_SIZE);
    memset(op, 0, sizeof(mpr_token_t) * STACK_SIZE);

#if TRACE_PARSE
    printf("parsing expression '%s'\n", str);
#endif

    while (str[lex_idx]) {
#if TRACE_PARSE
        printf("lexing string '%s'\n", str + lex_idx);
#endif
        GET_NEXT_TOKEN(tok);
        if (!(tok.toktype & allow_toktype)) {
            FAIL("Illegal token sequence. (2)");
#if TRACE_PARSE
            printtoken(&tok, vars);
#endif
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
                allow_toktype = (TOK_OP | TOK_CLOSE_PAREN | TOK_CLOSE_SQUARE | TOK_CLOSE_CURLY
                                 | TOK_COMMA | TOK_COLON | TOK_SEMICOLON);
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
                    if (strncmp(var_cache_list->in_name, varname, len)==0) {
                        found_in = var_cache_list;
                        break;
                    }
                    else if (strncmp(var_cache_list->accum_name, varname, len)==0) {
                        found_accum = var_cache_list;
                        break;
                    }
                    var_cache_list = var_cache_list->next;
                }
                if (found_in) {
#if TRACE_PARSE
                    printf("found reference to local input variable '%s'\n", found_in->in_name);
#endif
                    int offset = -1;
                    i = found_in->accum_pos + 1;
                    while (i <= out_idx) {
                        if (out[i].toktype <= TOK_MOVE)
                            offset += 1 - tok_arity(out[i]);
                        ++i;
                    }
                    tok.toktype = TOK_COPY_FROM;
                    tok.con.cache_offset = offset;

                    i = found_in->in_pos;
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
#if TRACE_PARSE
                    printf("found reference to local accumulator variable '%s'\n",
                           found_accum->accum_name);
#endif
                    int pos = found_accum->accum_pos, stack_offset = -1;
                    while (pos <= out_idx) {
                        if (TOK_COPY_FROM == out[pos].toktype
                            && out[pos].con.cache_offset == stack_offset) {
                            stack_offset = 0;
                        }
                        if (TOK_SP_ADD == out[pos].toktype)
                            stack_offset += out[pos].lit.val.i;
                        else if (TOK_LOOP_START == out[pos].toktype
                                 && out[pos].con.flags & RT_INSTANCE)
                            ++stack_offset;
                        else if (TOK_LOOP_END == out[pos].toktype
                                 && out[pos].con.flags & RT_INSTANCE)
                            --stack_offset;
                        else if (out[pos].toktype < TOK_LAMBDA)
                            stack_offset += 1 - tok_arity(out[pos]);
                        ++pos;
                    }

                    memcpy(&tok, &out[out_idx - stack_offset], sizeof(mpr_token_t));
                    tok.toktype = TOK_COPY_FROM;
                    tok.con.cache_offset = stack_offset;
                    PUSH_TO_OUTPUT(tok);
                    allow_toktype = (TOK_VFN_DOT | TOK_RFN | TOK_OP | TOK_CLOSE_PAREN
                                     | TOK_CLOSE_SQUARE | TOK_CLOSE_CURLY | TOK_COLON);
                    break;
                }

                if (tok.var.idx >= VAR_X) {
                    int slot = tok.var.idx - VAR_X;
                    {FAIL_IF(slot >= n_ins, "Input slot index > number of sources.");}
                    tok.gen.datatype = in_types[slot];
                    tok.gen.vec_len = (TOK_VAR == tok.toktype) ? in_vec_lens[slot] : 1;
                    tok.gen.flags |= VEC_LEN_LOCKED;
                    is_const = 0;
                }
                else if (tok.var.idx == VAR_Y) {
                    tok.gen.datatype = out_type;
                    tok.gen.vec_len = (TOK_VAR == tok.toktype) ? out_vec_len : 1;
                    tok.gen.flags |= VEC_LEN_LOCKED;
                }
                else {
                    i = find_var_by_name(vars, n_vars, varname, len);
                    if (i >= 0) {
                        tok.var.idx = i;
                        tok.gen.datatype = vars[i].datatype;
                        tok.gen.vec_len = vars[i].vec_len;
                        if (tok.gen.vec_len)
                            tok.gen.flags |= VEC_LEN_LOCKED;
                    }
                    else {
                        {FAIL_IF(n_vars >= N_USER_VARS, "Maximum number of variables exceeded.");}
                        /* need to store new variable */
                        vars[n_vars].name = malloc(len + 1);
                        snprintf(vars[n_vars].name, len + 1, "%s", varname);
                        vars[n_vars].datatype = var_type;
                        vars[n_vars].vec_len = 0;
                        vars[n_vars].flags = VAR_INSTANCED;
#if TRACE_PARSE
                        printf("Stored new variable '%s' at index %i\n", vars[n_vars].name, n_vars);
#endif
                        tok.var.idx = n_vars;
                        tok.var.datatype = var_type;
                        /* special case: 'alive' tracks instance lifetime */
                        if (strcmp(vars[n_vars].name, "alive")==0) {
                            inst_ctl = n_vars;
                            is_const = 0;
                            tok.gen.vec_len = 1;
                            tok.gen.flags |= VEC_LEN_LOCKED;
                            tok.gen.datatype = MPR_INT32;
                        }
                        else if (strcmp(vars[n_vars].name, "muted")==0) {
                            mute_ctl = n_vars;
                            tok.gen.vec_len = 1;
                            tok.gen.flags |= VEC_LEN_LOCKED;
                            tok.gen.datatype = MPR_INT32;
                        }
                        else
                            tok.gen.vec_len = 0;
                        ++n_vars;
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
                allow_toktype = (var_flags | (assigning ? TOK_ASSIGN | TOK_ASSIGN_TT : 0));
                if (TOK_VAR == tok.toktype)
                    allow_toktype |= TOK_VFN_DOT | TOK_RFN;
                if (tok.var.idx != VAR_Y || out_assigned > 1)
                    allow_toktype |= (TOK_OP | TOK_CLOSE_PAREN | TOK_CLOSE_SQUARE | TOK_CLOSE_CURLY
                                      | TOK_COMMA | TOK_COLON | TOK_SEMICOLON);
                muted = 0;
                break;
            }
            case TOK_FN: {
                mpr_token_t newtok;
                tok.gen.datatype = fn_tbl[tok.fn.idx].fn_int ? MPR_INT32 : MPR_FLT;
                tok.fn.arity = fn_tbl[tok.fn.idx].arity;
                if (fn_tbl[tok.fn.idx].memory) {
                    /* add assignment token */
                    char varname[6];
                    int varidx = n_vars;
                    {FAIL_IF(n_vars >= N_USER_VARS, "Maximum number of variables exceeded.");}
                    do {
                        snprintf(varname, 6, "var%d", varidx++);
                    } while (find_var_by_name(vars, n_vars, varname, 6) >= 0);
                    /* need to store new variable */
                    vars[n_vars].name = strdup(varname);
                    vars[n_vars].datatype = var_type;
                    vars[n_vars].vec_len = 1;
                    vars[n_vars].flags = VAR_ASSIGNED;

                    newtok.toktype = TOK_ASSIGN_USE;
                    newtok.var.idx = n_vars;
                    ++n_vars;
                    newtok.gen.datatype = var_type;
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
                    allow_toktype = (TOK_OP | TOK_CLOSE_PAREN | TOK_CLOSE_SQUARE | TOK_CLOSE_CURLY
                                     | TOK_COMMA | TOK_COLON | TOK_SEMICOLON);
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
                        allow_toktype |= (TOK_OP | TOK_CLOSE_PAREN | TOK_CLOSE_SQUARE | TOK_CLOSE_CURLY
                                          | TOK_COMMA | TOK_COLON | TOK_SEMICOLON | TOK_RFN);
                    }
                    break;
                }
                /* omit break and continue to case TOK_RFN */
            case TOK_RFN: {
                int pre, sslen;
                expr_rfn_t rfn;
                mpr_token_t newtok;
                uint8_t rt;
                uint8_t idx;
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
                            {FAIL_IF(tok.toktype != TOK_OPEN_PAREN, "missing parenthesis.");}
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
                                len += tok_arity(tok);
                                if (tok.toktype != TOK_VAR)
                                    continue;
                                {FAIL_IF(tok.gen.flags & VAR_HIST_IDX,
                                         "History indexes not allowed within history reduce function.");}
                                if (VAR_Y == tok.var.idx) {
                                    y_ref = 1;
                                    break;
                                }
                                else if (tok.var.idx >= VAR_X)
                                    x_ref = 1;
                            }
                            if (y_ref) {
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
                                if (tok.var.idx == VAR_Y) {
                                    if (-op[op_idx].con.reduce_start < oldest_out)
                                        oldest_out = -op[op_idx].con.reduce_start;
                                }
                                else if (tok.var.idx <= VAR_X) {
                                    if (-op[op_idx].con.reduce_start < oldest_in[out[out_idx].var.idx - VAR_X])
                                        oldest_in[out[out_idx].var.idx - VAR_X] = -op[op_idx].con.reduce_start;
                                }
                            }
                            GET_NEXT_TOKEN(tok);
                            {FAIL_IF(tok.toktype != TOK_CLOSE_PAREN, "missing parenthesis.");}
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
                                len += tok_arity(tok);
                                if (tok.toktype != TOK_VAR)
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
                            mpr_type hi = 0, lo = 0;
                            uint8_t x_ref = 0;
                            uint8_t max_vec_len = in_vec_lens[0];
                            for (i = 1; i < n_ins; i++) {
                                if (in_vec_lens[i] > max_vec_len)
                                    max_vec_len = in_vec_lens[i];
                            }
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
                                    /* upgrade vector length to maximum */
                                    out[out_idx - i].var.vec_len = max_vec_len;
                                }
                            }
                            {FAIL_IF(!x_ref, "signal reduce requires reference to input 'x'.");}
                            /* Also find lowest and highest input signal types */
                            for (i = 0; i < n_ins; i++) {
                                if (!hi || in_types[i] < hi)
                                    hi = in_types[i];
                                if (!lo || in_types[i] > lo)
                                    lo = in_types[i];
                            }
                            if (hi == lo) /* homogeneous types, no casting necessary */
                                break;
                            out[out_idx].var.datatype = hi;
                            for (i = sslen - 1; i >= 0; i--) {
                                tok = out[out_idx - i];
                                if (tok.toktype != TOK_VAR || tok.var.idx < VAR_Y)
                                    continue;
                                /* promote datatype and casttype */
                                out[out_idx - i].var.datatype = lo;
                                out[out_idx - i].var.casttype = hi;
                                {FAIL_IF(check_type(eval_stk, out, out_idx, vars, 1) < 0,
                                         "Malformed expression (12).");}
                            }
                            out_idx = check_type(eval_stk, out, out_idx, vars, 0);
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
                                /* Set token dim to 1 since we will be iterating over elements */
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
                memcpy(&newtok, &op[op_idx], sizeof(mpr_token_t));
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
                            memcpy(&out[out_idx], &out[idx], sizeof(mpr_token_t));
                        out[out_idx].toktype = TOK_VAR_NUM_INST;
                        out[out_idx].gen.datatype = MPR_INT32;
                        allow_toktype = (TOK_OP | TOK_CLOSE_PAREN | TOK_CLOSE_SQUARE | TOK_COMMA
                                         | TOK_COLON | TOK_SEMICOLON);
                        break;
                    }
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
                if (TOK_COPY_FROM == out[out_idx].toktype && TOK_VAR != out[idx].toktype) {
                    /* make a new copy of this substack */
                    sslen = substack_len(out, idx);
                    {FAIL_IF(out_idx + sslen + pre > STACK_SIZE, "Stack size exceeded. (3)");}
                    /* deliberately overwrite out[out_idx] */
                    memcpy(&out[out_idx + pre], &out[idx - sslen + 1], sslen * sizeof(mpr_token_t));
                }
                else {
                    int ar = RFN_CENTER == rfn || RFN_MEAN == rfn || RFN_SIZE == rfn ? 2 : 1;
                    if (RT_INSTANCE == rt) {
                        /* instance loops cache the starting instance idx */
                        ++ar;
                    }
                    /* copy substack to after prefix */
                    out_idx = out_idx - sslen + 1;
                    memcpy(out + out_idx + pre, out + out_idx, sizeof(mpr_token_t) * sslen);
                    /* adjust any copy tokens */
                    for (i = 0; i < sslen; i++) {
                        if (TOK_COPY_FROM == out[out_idx + pre + i].toktype)
                            out[out_idx + pre + i].con.cache_offset += ar;
                    }
                }
                --out_idx;

                /* all instance reduce functions require this token */
                memcpy(&tok, &newtok, sizeof(mpr_token_t));
                tok.toktype = TOK_LOOP_START;
                PUSH_TO_OUTPUT(tok);

                if (RFN_REDUCE == rfn) {
                    temp_var_cache var_cache;
                    char *temp, *in_name, *accum_name;
                    int len;
                    {FAIL_IF(n_vars >= N_USER_VARS, "Maximum number of variables exceeded.");}
                    GET_NEXT_TOKEN(tok);
                    {FAIL_IF(tok.toktype != TOK_OPEN_PAREN, "missing parenthesis.");}
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
                    GET_NEXT_TOKEN(tok);
                    if (TOK_ASSIGN == tok.toktype) {
                        /* assignment to local accumulator variable*/
                        GET_NEXT_TOKEN(tok);
                        /* TODO: allow vectorizer, variable, function tokens */
                        if (TOK_LITERAL != tok.toktype) {
                            free(in_name);
                            free(accum_name);
                            {FAIL("'reduce()' accumulator initialization only accepts constant.");}
                        }
                        PUSH_TO_OUTPUT(tok);
                        GET_NEXT_TOKEN(tok);
                    }

                    if (TOK_LAMBDA != tok.toktype) {
                        free(in_name);
                        free(accum_name);
                        {FAIL("'reduce()' missing lambda operator '->'.");}
                    }

                    if (TOK_LITERAL != out[out_idx].toktype) {
                        /* default to zero for accumulator initialization */
                        tok.toktype = TOK_LITERAL;
                        tok.lit.datatype = MPR_INT32;
                        tok.lit.vec_len = 1;
                        tok.lit.val.i = 0;
                        PUSH_TO_OUTPUT(tok);
                    }

                    /* temporarily store variable names so we can look them up later */
                    var_cache = calloc(1, sizeof(temp_var_cache_t));
                    if (temp_vars)
                        var_cache->next = temp_vars;
                    temp_vars = var_cache;
                    var_cache->in_name = in_name;
                    var_cache->accum_name = accum_name;
                    var_cache->scope_start = lex_idx;
                    var_cache->in_pos = out_idx + sslen;
                    var_cache->accum_pos = out_idx;

                    reduce_types |= newtok.con.flags & REDUCE_TYPE_MASK;
                    newtok.toktype = TOK_REDUCING;
                    PUSH_TO_OPERATOR(newtok);
                    tok.toktype = TOK_OPEN_PAREN;
                    PUSH_TO_OPERATOR(tok);

                    allow_toktype = (TOK_VAR | TOK_LITERAL | TOK_FN | TOK_VFN | TOK_MUTED
                                     | TOK_NEGATE | TOK_OPEN_PAREN | TOK_OPEN_SQUARE | TOK_TT);
                    out_idx += sslen;
                    break;
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

                /* skip to after substack */
                if (RFN_COUNT != rfn) {
                    out_idx += sslen;
                    if (TOK_COPY_FROM == out[out_idx].toktype) {
                        /* TODO: simplified reduce functions do not need separate cache for input */
                    }
                }

                if (OP_UNKNOWN != rfn_tbl[rfn].op) {
                    tok.toktype = TOK_OP;
                    tok.op.idx = rfn_tbl[rfn].op;
                    /* don't use macro here since we don't want to optimize away initialization args */
                    PUSH_TO_OUTPUT(tok);
                    out_idx = check_type(eval_stk, out, out_idx, vars, 0);
                    {FAIL_IF(out_idx < 0, "Malformed expression (11).");}
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

                if (RFN_CENTER == rfn || RFN_MEAN == rfn || RFN_SIZE == rfn) {
                    tok.toktype = TOK_SP_ADD;
                    tok.lit.val.i = 1;
                    PUSH_TO_OUTPUT(tok);
                }

                /* all instance reduce functions require these tokens */
                memcpy(&tok, &newtok, sizeof(mpr_token_t));
                tok.toktype = TOK_LOOP_END;
                if (RFN_CENTER == rfn || RFN_MEAN == rfn || RFN_SIZE == rfn) {
                    tok.con.branch_offset = 3 + sslen;
                    tok.con.cache_offset = 2;
                }
                else {
                    tok.con.branch_offset = 2 + sslen;
                    tok.con.cache_offset = 1;
                }
                PUSH_TO_OUTPUT(tok);

                if (RFN_CENTER == rfn) {
                    tok.toktype = TOK_OP;
                    tok.op.idx = OP_ADD;
                    PUSH_TO_OPERATOR(tok);
                    POP_OPERATOR_TO_OUTPUT();
                    tok.toktype = TOK_LITERAL;
                    tok.gen.flags &= ~CONST_SPECIAL;
                    tok.gen.datatype = MPR_FLT;
                    tok.lit.val.f = 0.5;
                    PUSH_TO_OUTPUT(tok);
                    tok.toktype = TOK_OP;
                    tok.op.idx = OP_MULTIPLY;
                    PUSH_TO_OPERATOR(tok);
                    POP_OPERATOR_TO_OUTPUT();
                }
                else if (RFN_MEAN == rfn) {
                    tok.toktype = TOK_OP;
                    tok.op.idx = OP_DIVIDE;
                    PUSH_TO_OPERATOR(tok);
                    POP_OPERATOR_TO_OUTPUT();
                }
                else if (RFN_SIZE == rfn) {
                    tok.toktype = TOK_OP;
                    tok.op.idx = OP_SUBTRACT;
                    PUSH_TO_OPERATOR(tok);
                    POP_OPERATOR_TO_OUTPUT();
                }
                allow_toktype = (TOK_OP | TOK_CLOSE_PAREN | TOK_CLOSE_SQUARE | TOK_CLOSE_CURLY
                                 | TOK_COMMA | TOK_COLON | TOK_SEMICOLON);
                break;
            }
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
                    if (op[op_idx].gen.vec_len) {
                        op[op_idx].gen.flags |= VEC_LEN_LOCKED;
                        ADD_TO_VECTOR();
                        lock_vec_len(out, out_idx);
                    }
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

                allow_toktype = (TOK_OP | TOK_COLON | TOK_SEMICOLON | TOK_COMMA | TOK_CLOSE_PAREN
                                 | TOK_CLOSE_SQUARE | TOK_CLOSE_CURLY | TOK_VFN_DOT | TOK_RFN);
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
                                     "Vector index must be an integer.");}
                            sig_idx = out[out_idx-1].lit.val.i % n_ins;
                            if (sig_idx < 0)
                                sig_idx += n_ins;
                            out[out_idx].var.idx = sig_idx + VAR_X;
                            out[out_idx].gen.flags &= ~VAR_SIG_IDX;
                            memcpy(out + out_idx - 1, out + out_idx, sizeof(mpr_token_t));
                            POP_OUTPUT();
                        }
                        else {
                            mpr_type hi = 0, lo = 0;
                            /* Find lowest and highest input signal types */
                            for (i = 0; i < n_ins; i++) {
                                if (!hi || in_types[i] < hi)
                                    hi = in_types[i];
                                if (!lo || in_types[i] > lo)
                                    lo = in_types[i];
                            }
                            if (hi != lo) {
                                /* heterogeneous types, need to cast */
                                out[out_idx].var.datatype = lo;
                                out[out_idx].var.casttype = hi;
                            }
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
                                /* History index may be "behind" a vector index literal or expr */
                                if (out[out_idx].gen.flags & VAR_VEC_IDX)
                                    i -= substack_len(out, i);
                                /* History index may be "behind" an instance index literal or inst */
                                if (out[out_idx].gen.flags & VAR_INST_IDX)
                                    i -= substack_len(out, i);
                                {FAIL_IF(i < 0, "error processing vector buffer size.");}

                                if (!buffer_size) {
                                    {FAIL_IF(out[i].toktype != TOK_LITERAL,
                                             "variable history indices must include maximum value.");}
                                    switch (out[i].gen.datatype) {
#define TYPED_CASE(MTYPE, T)                                                    \
                                        case MTYPE:                             \
                                            buffer_size = (int)out[i].lit.val.T;\
                                            break;
                                        TYPED_CASE(MPR_INT32, i)
                                        TYPED_CASE(MPR_FLT, f)
                                        TYPED_CASE(MPR_DBL, d)
#undef TYPED_CASE
                                        default:
                                            break;
                                    }
                                    {FAIL_IF(buffer_size > 0 || abs(buffer_size) > MAX_HIST_SIZE,
                                             "Illegal history index.");}
                                }
                                if (!buffer_size) {
                                    /* remove zero delay */
                                    memcpy(&out[i], &out[i + 1], sizeof(mpr_token_t) * (out_idx - i));
                                    POP_OUTPUT();
                                    POP_OPERATOR();
                                    break;
                                }
                                if (out[out_idx].var.idx == VAR_Y && buffer_size < oldest_out)
                                    oldest_out = buffer_size;
                                else if (   out[out_idx].var.idx >= VAR_X
                                         && buffer_size < oldest_in[out[out_idx].var.idx - VAR_X]) {
                                    oldest_in[out[out_idx].var.idx - VAR_X] = buffer_size;
                                }
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
                            {FAIL_IF(MPR_INT32 != out[out_idx-1].gen.datatype,
                                     "Vector index must be an integer.");}
                            if (   TOK_VAR == tok.gen.toktype
                                && (tok.var.idx >= VAR_Y || vars[tok.var.idx].vec_len)) {
                                /* Optimize by storing vector idx in variable token */
                                int vec_len, vec_idx;
                                if (VAR_Y == tok.var.idx)
                                    vec_len = out_vec_len;
                                else if (VAR_X <= tok.var.idx)
                                    vec_len = in_vec_lens[tok.var.idx - VAR_X];
                                else
                                    vec_len = vars[tok.var.idx].vec_len;
                                vec_idx = out[out_idx-1].lit.val.i % vec_len;
                                if (vec_idx < 0)
                                    vec_idx += vec_len;
                                out[out_idx].var.vec_idx = vec_idx;
                                out[out_idx].gen.flags &= ~VAR_VEC_IDX;
                                memcpy(out + out_idx - 1, out + out_idx, sizeof(mpr_token_t));
                                POP_OUTPUT();
                            }
                        }
                        /* also set var vec_len to 1 */
                        /* TODO: consider vector indices */
                        out[out_idx].var.vec_len = 1;

                        if (out[out_idx].gen.flags & VAR_VEC_IDX) {
                            /* vector indices must be integers: find index and cast if necessary */
                            i = out_idx - 1;
                            if (out[out_idx].gen.flags & VAR_SIG_IDX)
                                i -= substack_len(out, i);
                            if (out[out_idx].gen.flags & VAR_HIST_IDX)
                                i -= substack_len(out, i);
                            if (out[i].gen.datatype != MPR_INT32)
                                out[i].gen.casttype = MPR_INT32;
                        }

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
                    cache_pos = var_cache->accum_pos - 1;
                    {FAIL_IF(out[cache_pos].toktype != TOK_LOOP_START, "Compilation error (2)");}
                    free((char*)var_cache->in_name);
                    free((char*)var_cache->accum_name);
                    free(var_cache);

                    /* push move token to output */
                    tok.toktype = TOK_MOVE;
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
                    tok.con.cache_offset = 1;
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
                {FAIL_IF(op_idx < 0, "Malformed expression (4).");}
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
                    allow_toktype = (TOK_OP | TOK_COMMA | TOK_CLOSE_PAREN | TOK_CLOSE_CURLY
                                     | TOK_CLOSE_SQUARE | TOK_COLON | TOK_SEMICOLON
                                     | TOK_VFN_DOT | TOK_RFN | (var_flags & ~VAR_IDXS)
                                     | (assigning ? TOK_ASSIGN | TOK_ASSIGN_TT : 0));
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
                    }
                    /* update and lock vector length of assigned variable */
                    op[op_idx].gen.vec_len = vars[var_idx].vec_len;
                    op[op_idx].gen.flags |= VEC_LEN_LOCKED;
                    if (is_const)
                        vars[var_idx].flags &= ~VAR_INSTANCED;
                }
                /* pop assignment operators to output */
                while (op_idx >= 0) {
                    if (!op_idx && op[op_idx].toktype < TOK_ASSIGN)
                        {FAIL("Malformed expression (5)");}
                    PUSH_TO_OUTPUT(op[op_idx]);
                    if (out[out_idx].toktype == TOK_ASSIGN_USE
                        && check_assign_type_and_len(eval_stk, out, out_idx, vars) == -1)
                        {FAIL("Malformed expression (6)");}
                    POP_OPERATOR();
                }
                /* mark last assignment token to clear eval stack */
                out[out_idx].gen.flags |= CLEAR_STACK;

                /* check vector length and type */
                if (check_assign_type_and_len(eval_stk, out, out_idx, vars) == -1)
                    {FAIL("Malformed expression (7)");}

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
                allow_toktype = OBJECT_TOKENS;
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
                tok.toktype = TOK_LITERAL;
                tok.gen.datatype = MPR_INT32;
                tok.lit.val.i = -1;
                PUSH_TO_OUTPUT(tok);
                tok.toktype = TOK_OP;
                tok.op.idx = OP_MULTIPLY;
                PUSH_TO_OPERATOR(tok);
                allow_toktype = TOK_LITERAL | TOK_VAR | TOK_TT | TOK_FN;
                break;
            case TOK_ASSIGN:
                var_flags = 0;
                /* assignment to variable */
                {FAIL_IF(!assigning, "Misplaced assignment operator.");}
                {FAIL_IF(op_idx >= 0 || out_idx < 0, "Malformed expression left of assignment.");}

                if (out[out_idx].toktype == TOK_VAR) {
                    int var = out[out_idx].var.idx;
                    if (var >= VAR_X)
                        {FAIL("Cannot assign to input variable 'x'.");}
                    if (out[out_idx].gen.flags & VAR_HIST_IDX) {
                        /* unlike variable lookup, history index must be integer for assignment */
                        i = out_idx - 1;
                        if (out[out_idx].gen.flags & VAR_SIG_IDX)
                            i -= substack_len(out, out_idx - 1);
                        if (MPR_INT32 != out[i].gen.datatype)
                            out[i].gen.casttype = MPR_INT32;
                    }
                    else if (var == VAR_Y)
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
                    FAIL_IF(out[out_idx].var.idx != VAR_Y, "Only output timetag is writable.");
                    /* disable writing to current timetag for now */
                    FAIL_IF(!(out[out_idx].gen.flags & VAR_HIST_IDX),
                            "Only past samples of output timetag are writable.");
                    out[out_idx].toktype = TOK_ASSIGN_TT;
                    out[out_idx].gen.datatype = MPR_DBL;
                    POP_OUTPUT_TO_OPERATOR();
                }
                else if (out[out_idx].toktype == TOK_VECTORIZE) {
                    int var, j, arity = out[out_idx].fn.arity;

                    /* out token is vectorizer */
                    --out_idx;
                    {FAIL_IF(out[out_idx].toktype != TOK_VAR,
                             "Illegal tokens left of assignment. (1)");}
                    var = out[out_idx].var.idx;
                    if (var >= VAR_X)
                        {FAIL("Cannot assign to input variable 'x'.");}
                    else if (!(out[out_idx].gen.flags & VAR_HIST_IDX)) {
                        if (var == VAR_Y)
                            ++out_assigned;
                        else
                            vars[var].flags |= VAR_ASSIGNED;
                    }

                    for (i = 0; i < arity; i++) {
                        if (out[out_idx].toktype != TOK_VAR)
                            {FAIL("Illegal tokens left of assignment. (2)");}
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
                allow_toktype = (TOK_VAR | TOK_LITERAL | TOK_FN | TOK_VFN | TOK_MUTED | TOK_NEGATE
                                 | TOK_OPEN_PAREN | TOK_OPEN_SQUARE | TOK_OP | TOK_TT);
                break;
            default:
                {FAIL("Unknown token type.");}
                break;
        }
#if TRACE_PARSE
        printstack("OUTPUT STACK", out, out_idx, vars, 0);
        printstack("OPERATOR STACK", op, op_idx, vars, 0);
#endif
    }

    {FAIL_IF(allow_toktype & TOK_LITERAL || !out_assigned, "Expression has no output assignment.");}

    /* check that all used-defined variables were assigned */
    for (i = 0; i < n_vars; i++) {
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
        {FAIL_IF(!op_idx && op[op_idx].toktype < TOK_ASSIGN, "Malformed expression (8).");}
        PUSH_TO_OUTPUT(op[op_idx]);
        /* check vector length and type */
        {FAIL_IF(out[out_idx].toktype == TOK_ASSIGN_USE
                 && check_assign_type_and_len(eval_stk, out, out_idx, vars) == -1,
                 "Malformed expression (9).");}
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
    {FAIL_IF(check_assign_type_and_len(eval_stk, out, out_idx, vars) == -1,
             "Malformed expression (10).");}

    {FAIL_IF(replace_special_constants(out, out_idx), "Error replacing special constants."); }

#if TRACE_PARSE
    printstack("OUTPUT STACK", out, out_idx, vars, 0);
    printstack("OPERATOR STACK", op, op_idx, vars, 0);
#endif

    /* Check for maximum vector length used in stack */
    for (i = 0; i < out_idx; i++) {
        if (out[i].gen.vec_len > max_vector)
            max_vector = out[i].gen.vec_len;
    }

    expr = malloc(sizeof(struct _mpr_expr));
    expr->n_tokens = out_idx + 1;
    expr->stack_size = _eval_stack_size(out, out_idx);
    expr->offset = 0;
    expr->inst_ctl = inst_ctl;
    expr->mute_ctl = mute_ctl;

    /* copy tokens */
    expr->tokens = malloc(sizeof(union _token) * (size_t)expr->n_tokens);
    memcpy(expr->tokens, &out, sizeof(union _token) * (size_t)expr->n_tokens);
    expr->start = expr->tokens;
    expr->vec_len = max_vector;
    expr->out_hist_size = -oldest_out + 1;
    expr->in_hist_size = malloc(sizeof(uint16_t) * n_ins);
    expr->max_in_hist_size = 0;
    for (i = 0; i < n_ins; i++) {
        register int hist_size = -oldest_in[i] + 1;
        if (hist_size > expr->max_in_hist_size)
            expr->max_in_hist_size = hist_size;
        expr->in_hist_size[i] = hist_size;
    }
    if (n_vars) {
        /* copy user-defined variables */
        expr->vars = malloc(sizeof(mpr_var_t) * n_vars);
        memcpy(expr->vars, vars, sizeof(mpr_var_t) * n_vars);
    }
    else
        expr->vars = NULL;

    expr->n_vars = n_vars;
    /* TODO: is this the same as n_ins arg passed to this function? */
    expr->n_ins = n_ins;

    expr_stack_realloc(eval_stk, expr->stack_size * expr->vec_len);

#if TRACE_PARSE
    printf("expression allocated and initialized\n");
#endif
    return expr;

error:
    while (--n_vars >= 0)
        free(vars[n_vars].name);
    while (temp_vars) {
        temp_var_cache tmp = temp_vars->next;
        free((char*)temp_vars->in_name);
        free((char*)temp_vars->accum_name);
        free(temp_vars);
        temp_vars = tmp;
    }
    free_stack_vliterals(out, out_idx);
    free_stack_vliterals(op, op_idx);
    return 0;

}

int mpr_expr_get_in_hist_size(mpr_expr expr, int idx)
{
    return expr->in_hist_size[idx];
}

int mpr_expr_get_out_hist_size(mpr_expr expr)
{
    return expr->out_hist_size;
}

int mpr_expr_get_num_vars(mpr_expr expr)
{
    return expr->n_vars;
}

const char *mpr_expr_get_var_name(mpr_expr expr, int idx)
{
    return (idx >= 0 && idx < expr->n_vars) ? expr->vars[idx].name : NULL;
}

int mpr_expr_get_var_vec_len(mpr_expr expr, int idx)
{
    return (idx >= 0 && idx < expr->n_vars) ? expr->vars[idx].vec_len : 0;
}

int mpr_expr_get_var_is_instanced(mpr_expr expr, int idx)
{
    return (idx >= 0 && idx < expr->n_vars) ? expr->vars[idx].flags & VAR_INSTANCED : 0;
}

int mpr_expr_get_var_type(mpr_expr expr, int idx)
{
    return (idx >= 0 && idx < expr->n_vars) ? expr->vars[idx].datatype : 0;
}

int mpr_expr_get_src_is_muted(mpr_expr expr, int idx)
{
    int i, found = 0, muted = VAR_MUTED;
    mpr_token_t *tok = expr->tokens;
    for (i = 0; i < expr->n_tokens; i++) {
        if (tok[i].toktype == TOK_VAR && tok[i].var.idx == idx + VAR_X) {
            found = 1;
            muted &= tok[i].gen.flags;
        }
    }
    return found && muted;
}

int mpr_expr_get_num_input_slots(mpr_expr expr)
{
    return expr ? expr->n_ins : 0;
}

int mpr_expr_get_manages_inst(mpr_expr expr)
{
    return expr ? expr->inst_ctl >= 0 : 0;
}

void mpr_expr_var_updated(mpr_expr expr, int var_idx)
{
    RETURN_UNLESS(expr && var_idx >= 0 && var_idx < expr->n_vars);
    RETURN_UNLESS(var_idx != expr->inst_ctl && var_idx != expr->mute_ctl);
    expr->vars[var_idx].flags |= VAR_SET_EXTERN;
    /* Reset expression offset to 0 in case other variables are initialised from this one. */
    expr->offset = 0;
    return;
}

#if TRACE_EVAL
static void print_stack_vec(mpr_expr_val stk, mpr_type type, int vec_len)
{
    int i;
    if (vec_len > 1)
        printf("[");
    switch (type) {
#define TYPED_CASE(MTYPE, STR, T)           \
        case MTYPE:                         \
            for (i = 0; i < vec_len; i++)   \
                printf(STR, stk[i].T);      \
            break;
        TYPED_CASE(MPR_INT32, "%d, ", i)
        TYPED_CASE(MPR_FLT, "%g, ", f)
        TYPED_CASE(MPR_DBL, "%g, ", d)
#undef TYPED_CASE
        default:
            break;
    }
    if (vec_len > 1)
        printf("\b\b]%c", type);
    else
        printf("\b\b%c", type);
}
#endif

#if TRACE_EVAL
static const char *type_name(const mpr_type type)
{
    switch (type) {
        case MPR_DBL:   return "double";
        case MPR_FLT:   return "float";
        case MPR_INT32: return "int";
        default:        return 0;
    }
}
#endif

#define UNARY_OP_CASE(OP, SYM, T)               \
    case OP:                                    \
        for (i = sp; i < sp + dims[dp]; i++)    \
            stk[i].T SYM stk[i].T;              \
        break;

#define BINARY_OP_CASE(OP, SYM, T)                                  \
    case OP:                                                        \
        for (i = 0, j = sp; i < dims[dp]; i++, j++)                 \
            stk[j].T = stk[j].T SYM stk[sp + vlen + i % rdim].T;    \
        break;

#define CONDITIONAL_CASES(T)                                        \
    case OP_IF_ELSE:                                                \
        for (i = 0, j = sp; i < dims[dp]; i++, j++) {               \
            if (!stk[j].T)                                          \
                stk[j].T = stk[sp + vlen + i % rdim].T;             \
        }                                                           \
        break;                                                      \
    case OP_IF_THEN_ELSE:                                           \
        for (i = 0, j = sp; i < dims[dp]; i++, j++) {               \
            if (stk[j].T)                                           \
                stk[j].T = stk[sp + vlen + i % rdim].T;             \
            else                                                    \
                stk[j].T = stk[sp + 2 * vlen + i % dims[dp + 2]].T; \
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

int mpr_expr_eval(mpr_expr_stack expr_stk, mpr_expr expr, mpr_value *v_in, mpr_value *v_vars,
                  mpr_value v_out, mpr_time *time, mpr_type *out_types, int inst_idx)
{
#if TRACE_EVAL
    printf("evaluating expression...\n");
#endif
    mpr_token_t *tok, *end;
    int status = 1 | EXPR_EVAL_DONE, cache = 0, vlen;
    int i, j, sp, dp = -1;
    uint8_t alive = 1, muted = 0, can_advance = 1, hist_offset = 0, sig_offset = 0, vec_offset = 0;
    mpr_value_buffer b_out;
    mpr_value x = NULL;

    mpr_expr_val stk = expr_stk->stk;
    uint8_t *dims = expr_stk->dims;
    mpr_type *types = expr_stk->types;

    if (!expr) {
#if TRACE_EVAL
        printf(" no expression to evaluate!\n");
#endif
        return 0;
    }

    sp = -expr->vec_len;
    vlen = expr->vec_len;
    tok = expr->start;
    end = expr->start + expr->n_tokens;
    b_out = v_out ? &v_out->inst[inst_idx % v_out->num_inst] : 0;
    if (v_out && b_out->pos >= 0) {
        tok += expr->offset;
    }

    if (v_vars) {
        if (expr->inst_ctl >= 0) {
            /* recover instance state */
            mpr_value v = *v_vars + expr->inst_ctl;
            int *vi = v->inst[inst_idx].samps;
            alive = (0 != vi[0]);
        }
        if (expr->mute_ctl >= 0) {
            /* recover mute state */
            mpr_value v = *v_vars + expr->mute_ctl;
            int *vi = v->inst[inst_idx].samps;
            muted = (0 != vi[0]);
        }
    }

    if (v_out) {
        /* init out_types */
        if (out_types)
            memset(out_types, MPR_NULL, v_out->vlen);
        /* Increment index position of output data structure. */
        b_out->pos = (b_out->pos + 1) % v_out->mlen;
    }

    /* choose one input to represent active instances
     * or now we will choose the input with the highest instance count
     * TODO: consider alternatives */
    if (v_in) {
        x = v_in[0];
        for (i = 1; i < expr->n_ins; i++) {
            if (v_in[i]->num_inst > x->num_inst)
                x = v_in[i];
        }
    }

    while (tok < end) {
  repeat:
        switch (tok->toktype) {
        case TOK_LITERAL:
        case TOK_VLITERAL:
            sp += vlen;
            ++dp;
            assert(dp < expr_stk->size);
            dims[dp] = tok->gen.vec_len;
            types[dp] = tok->gen.datatype;
                /* TODO: remove vector building? */
            switch (types[dp]) {
#define TYPED_CASE(MTYPE, T)                                                    \
                case MTYPE:                                                     \
                    if (TOK_LITERAL == tok->toktype) {                          \
                        for (i = sp; i < sp + tok->gen.vec_len; i++)            \
                            stk[i].T = tok->lit.val.T;                          \
                    }                                                           \
                    else {                                                      \
                        for (i = sp, j = 0; i < sp + tok->gen.vec_len; i++, j++)\
                            stk[i].T = tok->lit.val.T##p[j];                    \
                    }                                                           \
                    break;
                TYPED_CASE(MPR_INT32, i)
                TYPED_CASE(MPR_FLT, f)
                TYPED_CASE(MPR_DBL, d)
#undef TYPED_CASE
                default:
                    goto error;
            }
#if TRACE_EVAL
            printf("loading constant %d|", dp);
            print_stack_vec(stk + sp, types[dp], tok->gen.vec_len);
            printf("\n");
#endif
            break;
        case TOK_VAR: {
            mpr_value v;
            mpr_value_buffer b;
            int hidx = -hist_offset, vidx, idxp = dp;
            float weight = 0.f;

            if (tok->var.idx == VAR_Y) {
                RETURN_ARG_UNLESS(v_out, status);
#if TRACE_EVAL
                printf("loading variable y");
#endif
                v = v_out;
                b = b_out;
                can_advance = 0;
            }
            else if (tok->var.idx >= VAR_X) {
                RETURN_ARG_UNLESS(v_in, status);
                if (!(tok->gen.flags & VAR_SIG_IDX)) {
                    v = v_in[tok->var.idx - VAR_X + sig_offset];
#if TRACE_EVAL
                    printf("loading variable x$%d", tok->var.idx - VAR_X + sig_offset);
#endif
                }
                else {
                    assert(idxp >= 0);
                    if (MPR_INT32 == types[idxp]) {
                        int sidx = stk[sp].i % expr->n_ins;
                        if (sidx < 0)
                            sidx += expr->n_ins;
#if TRACE_EVAL
                        printf("loading variable x$(%d mod %d = %d)", stk[sp].i, expr->n_ins, sidx);
#endif
                        v = v_in[sidx];
                        --idxp;
                    }
                    else
                        goto error;
                }
                b = &v->inst[inst_idx % v->num_inst];
                can_advance = 0;
                if (!cache)
                    status &= ~EXPR_EVAL_DONE;
            }
            else if (v_vars) {
#if TRACE_EVAL
                if (expr->vars)
                    printf("loading variable %s", expr->vars[tok->var.idx].name);
                else
                    printf("loading variable vars.%d", tok->var.idx);
#endif
                v = *v_vars + tok->var.idx;
                if (expr->vars[tok->var.idx].flags & VAR_INSTANCED)
                    b = &v->inst[inst_idx % v->num_inst];
                else
                    b = &v->inst[0];
            }
            else
                goto error;

            if (tok->gen.flags & VAR_HIST_IDX) {
                assert(idxp >= 0);
                i = idxp * vlen;
                switch (types[idxp]) {
                    case MPR_INT32: hidx = stk[i].i;                                        break;
                    case MPR_FLT:   hidx = (int)stk[i].f; weight = fabsf(stk[i].f - hidx);  break;
                    case MPR_DBL:   hidx = (int)stk[i].d; weight = fabs(stk[i].d - hidx);   break;
                    default:        goto error;
                }
                --idxp;
            }
#if TRACE_EVAL
            if (MPR_INT32 == types[idxp])
                printf("{N=%d}", hidx);
            else
                printf("{N=%g}", hidx + weight);
#endif

            if (tok->gen.flags & VAR_VEC_IDX) {
                assert(idxp >= 0);
                if (MPR_INT32 != types[idxp])
                    goto error;
                i = idxp * vlen;
                vidx = stk[i].i;
                --idxp;
            }
            else
                vidx = tok->var.vec_idx + vec_offset;
#if TRACE_EVAL
            printf("[N=%d]", vidx);
#endif

            /* STUB: instance indexing will go here */
            /* if (tok->gen.flags & VAR_INST_IDX) {
                ...
            } */

            dp = idxp + 1;
            assert(dp >= 0 && dp < expr_stk->size);
            sp = dp * vlen;
            dims[dp] = tok->gen.vec_len;
            types[dp] = v->type;

            i = (b->pos + v->mlen + hidx) % v->mlen;
            if (i < 0)
                i += v->mlen;
            switch (v->type) {
#define COPY_TYPED(MTYPE, TYPE, T)                                          \
                case MTYPE: {                                               \
                    TYPE *a = (TYPE*)b->samps + i * v->vlen;                \
                    for (i = 0, j = sp; i < tok->gen.vec_len; i++, j++) {   \
                        int vec_idx = (i + vidx) % v->vlen;                 \
                        if (vec_idx < 0) vec_idx += v->vlen;                \
                        stk[j].T = a[vec_idx];                              \
                    }                                                       \
                    break;                                                  \
                }
                COPY_TYPED(MPR_INT32, int, i)
                COPY_TYPED(MPR_FLT, float, f)
                COPY_TYPED(MPR_DBL, double, d)
#undef COPY_TYPED
                default:
                    goto error;
            }
            if (weight) {
                if (--i < 0)
                    i += v->mlen;
                switch (v->type) {
#define WEIGHTED_ADD(MTYPE, TYPE, T)                                                    \
                    case MTYPE: {                                                       \
                        TYPE *a = (TYPE*)b->samps + i * v->vlen;                        \
                        for (i = 0, j = sp; i < tok->gen.vec_len; i++, j++) {           \
                            int vec_idx = (i + vidx) % v->vlen;                         \
                            if (vec_idx < 0) vec_idx += v->vlen;                        \
                            stk[j].T = stk[j].T * weight + a[vec_idx] * (1 - weight);   \
                        }                                                               \
                        break;                                                          \
                    }
                    WEIGHTED_ADD(MPR_INT32, int, i)
                    WEIGHTED_ADD(MPR_FLT, float, f)
                    WEIGHTED_ADD(MPR_DBL, double, d)
#undef WEIGHTED_ADD
                    default:
                        goto error;
                }
            }
#if TRACE_EVAL
            printf(" %d|", dp);
            print_stack_vec(stk + sp, types[dp], tok->gen.vec_len);
            printf(" \n");
#endif
            break;
        }
        case TOK_VAR_NUM_INST: {
            ++dp;
            assert(dp < expr_stk->size);
            sp += vlen;
            dims[dp] = tok->gen.vec_len;
            types[dp] = MPR_INT32;
#if TRACE_EVAL
            if (tok->var.idx == VAR_Y)
                printf("loading y.count()");
            else if (tok->var.idx >= VAR_X)
                printf("loading x%d.count()", tok->var.idx - VAR_X);
            else if (v_vars)
                printf("loading vars[%d].count()", tok->var.idx);
#endif
            if (tok->var.idx == VAR_Y) {
                RETURN_ARG_UNLESS(v_out, status);
                stk[sp].i = v_out->num_active_inst;
            }
            else if (tok->var.idx >= VAR_X) {
                RETURN_ARG_UNLESS(v_in, status);
                stk[sp].i = v_in[tok->var.idx - VAR_X]->num_active_inst;
            }
            else if (v_vars)
                stk[sp].i = (*v_vars + tok->var.idx)->num_active_inst;
            else
                goto error;
            for (i = 1; i < tok->gen.vec_len; i++)
                stk[sp + i].i = stk[sp].i;
#if TRACE_EVAL
            printf(" = ");
            print_stack_vec(stk + sp, types[dp], dims[dp]);
            printf(" \n");
#endif
            break;
        }
        case TOK_TT: {
            int hidx = 0;
            double weight = 0.0;
            double t_d;
            if (!(tok->gen.flags & VAR_HIST_IDX)) {
                sp += vlen;
                ++dp;
                assert(dp < expr_stk->size);
            }
            assert(dp >= 0);
            dims[dp] = tok->gen.vec_len;
#if TRACE_EVAL
            if (tok->var.idx == VAR_Y)
                printf("loading timetag t_y");
            else if (tok->var.idx >= VAR_X)
                printf("loading timetag t_x%d", tok->var.idx - VAR_X);
            else if (v_vars)
                printf("loading timetag t_%s", expr->vars[tok->var.idx].name);

            if (tok->gen.flags & VAR_HIST_IDX) {
                switch (types[dp]) {
                    case MPR_INT32: printf("{N=%d}", stk[sp].i);    break;
                    case MPR_FLT:   printf("{N=%g}", stk[sp].f);    break;
                    case MPR_DBL:   printf("{N=%g}", stk[sp].d);    break;
                    default:                                        goto error;
                }
            }
#endif
            if (tok->gen.flags & VAR_HIST_IDX) {
                switch (types[dp]) {
                    case MPR_INT32:
                        hidx = stk[sp].i;
                        break;
                    case MPR_FLT:
                        hidx = (int)stk[sp].f;
                        weight = fabsf(stk[sp].f - hidx);
                        break;
                    case MPR_DBL:
                        hidx = (int)stk[sp].d;
                        weight = fabs(stk[sp].d - hidx);
                        break;
                    default:
                        goto error;
                }
            }
            if (tok->var.idx == VAR_Y) {
                int idx;
                mpr_value_buffer b;
                RETURN_ARG_UNLESS(v_out, status);
                b = b_out;
                idx = (b->pos + v_out->mlen + hidx) % v_out->mlen;
                t_d = mpr_time_as_dbl(b->times[idx]);
                if (weight)
                    t_d = t_d * weight + ((b->pos + v_out->mlen + hidx - 1) % v_out->mlen) * (1 - weight);
            }
            else if (tok->var.idx >= VAR_X) {
                mpr_value v;
                mpr_value_buffer b;
                RETURN_ARG_UNLESS(v_in, status);
                v = v_in[tok->var.idx - VAR_X];
                b = &v->inst[inst_idx % v->num_inst];
                /* TODO: ensure buffer overrun is not possible here amd similar */
                t_d = mpr_time_as_dbl(b->times[(b->pos + v->mlen + hidx) % v->mlen]);
                if (weight)
                    t_d = t_d * weight + ((b->pos + v->mlen + hidx - 1) % v->mlen) * (1 - weight);
            }
            else if (v_vars) {
                mpr_value v = *v_vars + tok->var.idx;
                mpr_value_buffer b = &v->inst[inst_idx];
                t_d = mpr_time_as_dbl(b->times[0]);
            }
            else
                goto error;
#if TRACE_EVAL
            printf(" as double %g\n", t_d);
#endif
            for (i = sp; i < sp + tok->gen.vec_len; i++)
                stk[i].d = t_d;
            types[dp] = tok->gen.datatype;
            break;
        }
        case TOK_OP: {
            int maxlen, diff;
            unsigned int rdim;
            dp -= (op_tbl[tok->op.idx].arity - 1);
            assert(dp >= 0);
            sp = dp * vlen;
#if TRACE_EVAL
            if (OP_IF_THEN_ELSE == tok->op.idx || OP_IF_ELSE == tok->op.idx) {
                printf("IF ");
                print_stack_vec(stk + sp, types[dp], dims[dp]);
                printf(" THEN ");
                if (OP_IF_ELSE == tok->op.idx) {
                    print_stack_vec(stk + sp, types[dp], dims[dp]);
                    printf(" ELSE ");
                    print_stack_vec(stk + sp + vlen, types[dp + 1], dims[dp + 1]);
                }
                else {
                    print_stack_vec(stk + sp + vlen, types[dp], dims[dp + 1]);
                    printf(" ELSE ");
                    print_stack_vec(stk + sp + 2 * vlen, types[dp + 2], dims[dp + 2]);
                }
            }
            else {
                printf("%d|", dp);
                print_stack_vec(stk + sp, types[dp], dims[dp]);
                printf(" %s%c ", op_tbl[tok->op.idx].name, types[2]);
                if (2 == op_tbl[tok->op.idx].arity) {
                    printf("%d|", dp + 1);
                    print_stack_vec(stk + sp + vlen, types[dp + 1], dims[dp + 1]);
                }
            }
#endif
            /* first copy stk[sp] elements if necessary */
            maxlen = dims[dp];
            for (i = 1; i < op_tbl[tok->op.idx].arity; i++)
                maxlen = _max(maxlen, dims[dp + i]);
            diff = maxlen - dims[dp];
            while (diff > 0) {
                int mindiff = dims[dp] > diff ? diff : dims[dp];
                memcpy(&stk[sp + dims[dp]], &stk[sp], mindiff * sizeof(mpr_expr_val_t));
                dims[dp] += mindiff;
                diff -= mindiff;
            }
            rdim = dims[dp + 1];
            switch (types[dp]) {
                case MPR_INT32: {
                    switch (tok->op.idx) {
                        OP_CASES_META(i);
                        case OP_DIVIDE:
                            /* Check for divide-by-zero */
                            for (i = 0, j = 0; i < tok->gen.vec_len; i++, j = (j + 1) % rdim) {
                                if (stk[sp + vlen + j].i)
                                    stk[sp + i].i /= stk[sp + vlen + j].i;
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
                            for (i = 0; i < tok->gen.vec_len; i++)
                                stk[sp + i].f = fmodf(stk[sp + i].f, stk[sp + vlen + i % rdim].f);
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
                            for (i = 0; i < tok->gen.vec_len; i++)
                                stk[sp + i].d = fmod(stk[sp + i].d, stk[sp + vlen + i % rdim].d);
                            break;
                        default: goto error;
                    }
                    break;
                }
                default:
                    goto error;
            }
            types[dp] = tok->gen.datatype;
#if TRACE_EVAL
            printf(" = ");
            printf("%d|", dp);
            print_stack_vec(stk + sp, types[dp], dims[dp]);
            printf(" \n");
#endif
            break;
        }
        case TOK_FN: {
            int maxlen, diff;
            unsigned int ldim, rdim;
            dp -= (fn_tbl[tok->fn.idx].arity - 1);
            assert(dp >= 0);
            sp = dp * vlen;
#if TRACE_EVAL
            printf("%s%c(", fn_tbl[tok->fn.idx].name, types[dp]);
            for (i = 0; i < fn_tbl[tok->fn.idx].arity; i++) {
                print_stack_vec(stk + sp + vlen, types[dp + i], dims[dp + i]);
                printf(", ");
            }
            printf("%s)", fn_tbl[tok->fn.idx].arity ? "\b\b" : "");
#endif
            /* TODO: use preprocessor macro or inline func here */
            /* first copy stk[sp] elements if necessary */
            maxlen = dims[dp];
            for (i = 1; i < fn_tbl[tok->fn.idx].arity; i++)
                maxlen = _max(maxlen, dims[dp + i]);
            diff = maxlen - dims[dp];
            while (diff > 0) {
                int mindiff = dims[dp] > diff ? diff : dims[dp];
                memcpy(&stk[sp + dims[dp]], &stk[sp], mindiff * sizeof(mpr_expr_val_t));
                dims[dp] += mindiff;
                diff -= mindiff;
            }
            ldim = dims[dp];
            rdim = dims[dp + 1];
            types[dp] = tok->gen.datatype;
            switch (types[dp]) {
#define TYPED_CASE(MTYPE, FN, T)                                                        \
            case MTYPE:                                                                 \
                switch (fn_tbl[tok->fn.idx].arity) {                                    \
                case 0:                                                                 \
                    for (i = 0; i < ldim; i++)                                          \
                        stk[sp + i].T = ((FN##_arity0*)fn_tbl[tok->fn.idx].FN)();       \
                    break;                                                              \
                case 1:                                                                 \
                    for (i = 0; i < ldim; i++)                                          \
                        stk[sp + i].T = (((FN##_arity1*)fn_tbl[tok->fn.idx].FN)         \
                                        (stk[sp + i].T));                               \
                    break;                                                              \
                case 2:                                                                 \
                    for (i = 0; i < ldim; i++)                                          \
                        stk[sp + i].T = (((FN##_arity2*)fn_tbl[tok->fn.idx].FN)         \
                                         (stk[sp + i].T, stk[sp + vlen + i % rdim].T)); \
                    break;                                                              \
                case 3:                                                                 \
                    for (i = 0; i < ldim; i++)                                          \
                        stk[sp + i].T = (((FN##_arity3*)fn_tbl[tok->fn.idx].FN)         \
                                         (stk[sp + i].T, stk[sp + vlen + i % rdim].T,   \
                                          stk[sp + 2 * vlen + i % dims[dp + 2]].T));    \
                    break;                                                              \
                case 4:                                                                 \
                    for (i = 0; i < ldim; i++)                                          \
                        stk[sp + i].T = (((FN##_arity4*)fn_tbl[tok->fn.idx].FN)         \
                                         (stk[sp + i].T, stk[sp + vlen + i % rdim].T,   \
                                          stk[sp + 2 * vlen + i % dims[dp + 2]].T,      \
                                          stk[sp + 3 * vlen + i % dims[dp + 3]].T));    \
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
            printf(" = ");
            print_stack_vec(stk + sp, types[dp], dims[dp]);
            printf(" \n");
#endif
            break;
        }
        case TOK_VFN:
            dp -= (vfn_tbl[tok->fn.idx].arity - 1);
            assert(dp >= 0);
            sp = dp * vlen;
            if (vfn_tbl[tok->fn.idx].arity > 1 || VFN_DOT == tok->fn.idx) {
                int maxdim = tok->gen.vec_len;
                for (i = 0; i < vfn_tbl[tok->fn.idx].arity; i++)
                    maxdim = maxdim > dims[dp + i] ? maxdim : dims[dp + i];
                for (i = 0; i < vfn_tbl[tok->fn.idx].arity; i++) {
                    /* we need to ensure the vector lengths are equal */
                    while (dims[dp + i] < maxdim) {
                        int diff = maxdim - dims[dp + i];
                        diff = diff < dims[dp + i] ? diff : dims[dp + i];
                        memcpy(&stk[sp + dims[dp + i]], &stk[sp], diff * sizeof(mpr_expr_val_t));
                        dims[dp + i] += diff;
                    }
                    sp += vlen;
                }
                sp = dp * vlen;
            }
            types[dp] = tok->gen.datatype;
#if TRACE_EVAL
            printf("%s%c(", vfn_tbl[tok->fn.idx].name, types[dp]);
            for (i = 0; i < vfn_tbl[tok->fn.idx].arity; i++) {
                printf("%d|", dp + i);
                print_stack_vec(stk + sp + i * vlen, types[dp + 1], dims[dp + i]);
                printf(", ");
            }
            printf("\b\b)");
#endif
            switch (types[dp]) {
#define TYPED_CASE(MTYPE, FN)                                                       \
                case MTYPE:                                                         \
                    (((vfn_template*)vfn_tbl[tok->fn.idx].FN)(stk, dims, dp, vlen));\
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
                    stk[sp + i].d = stk[sp].d;
            }
            dims[dp] = tok->gen.vec_len;
            types[dp] = tok->gen.datatype;
#if TRACE_EVAL
            printf(" = ");
            if (VFN_MAXMIN == tok->fn.idx || VFN_SUMNUM == tok->fn.idx) {
                printf("[");
                printf("%d|", dp);
                print_stack_vec(stk + sp, types[dp], dims[dp]);
                printf(", ");
                printf("%d|", dp + 1);
                print_stack_vec(stk + sp + vlen, types[dp + 1], dims[dp + 1]);
                printf("]\n");
            }
            else {
                printf("%d|", dp);
                print_stack_vec(stk + sp, types[dp], dims[dp]);
                printf(" \n");
            }
#endif
            break;
        case TOK_LOOP_START:
#if TRACE_EVAL
            if (RT_INSTANCE & tok->con.flags)
                printf("Caching instance idx %d on the eval stack.\n", inst_idx);
#endif
            switch (tok->con.flags & REDUCE_TYPE_MASK) {
                case RT_HISTORY:
                    /* Set history start sample */
                    hist_offset = tok->con.reduce_start;
                    break;
                case RT_INSTANCE:
                    /* cache previous instance idx */
                    ++dp;
                    assert(dp < expr_stk->size);
                    sp += vlen;
                    stk[sp].i = inst_idx;
                    ++cache;
                    if (x) {
                        /* find first active instance idx */
                        for (i = 0; i < x->num_inst; i++) {
                            if (x->inst[i].full || x->inst[i].pos >= (expr->max_in_hist_size - 1))
                                break;
                        }
                        if (i >= x->num_inst)
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
#if TRACE_EVAL
            switch (tok->con.flags & REDUCE_TYPE_MASK) {
                case RT_HISTORY:
                    printf("Starting history reduce loop at -%d.\n", tok->con.reduce_start);
                    break;
                case RT_INSTANCE:
                    printf("Starting instance reduce loop with idx %d.\n", inst_idx);
                    break;
                case RT_SIGNAL:
                    printf("Starting input signal reduce loop.\n");
                    break;
                case RT_VECTOR:
                    printf("Starting vector reduce loop.\n");
                    break;
                default:
                    goto error;
            }
#endif
            break;
        case TOK_SP_ADD:
#if TRACE_EVAL
            printf("Adding %d to eval stack pointer.\n", tok->lit.val.i);
#endif
            dp += tok->lit.val.i;
            assert(dp < expr_stk->size);
            sp = dp * vlen;
            break;
        case TOK_LOOP_END:
            switch (tok->con.flags & REDUCE_TYPE_MASK) {
                case RT_HISTORY:
                    if (hist_offset > tok->con.reduce_stop) {
                        --hist_offset;
#if TRACE_EVAL
                        printf("Incrementing history idx to -%d and jumping -%d\n",
                               hist_offset, tok->con.branch_offset);
#endif
                        tok -= tok->con.branch_offset;
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
                        for (i = inst_idx + 1; i < x->num_inst; i++) {
                            if (x->inst[i].full || x->inst[i].pos >= (expr->max_in_hist_size - 1))
                                break;
                        }
                    }
                    if (x && i < x->num_inst) {
#if TRACE_EVAL
                        printf("Incrementing instance idx to %d and jumping -%d\n",
                               i, tok->con.branch_offset);
#endif
                        inst_idx = i;
                        tok -= tok->con.branch_offset;
                    }
                    else {
                        inst_idx = stk[sp - tok->con.cache_offset * vlen].i;
                        if (x && inst_idx >= x->num_inst)
                            goto error;
                        memcpy(stk + sp - tok->con.cache_offset * vlen,
                               stk + sp - (tok->con.cache_offset - 1) * vlen,
                               sizeof(mpr_expr_val_t) * vlen * tok->con.cache_offset);
                        memcpy(dims + dp - tok->con.cache_offset, dims + dp - tok->con.cache_offset + 1,
                               sizeof(uint8_t) * tok->con.cache_offset);
                        memcpy(types + dp - tok->con.cache_offset, types + dp - tok->con.cache_offset + 1,
                               sizeof(mpr_type) * tok->con.cache_offset);
                        sp -= vlen;
                        --dp;
                        assert(dp >= 0);
                        --cache;
#if TRACE_EVAL
                        printf("Instance loop done; restored cached instance idx %d.\n", inst_idx);
#endif
                    }
                    break;
                case RT_SIGNAL:
                    ++sig_offset;
                    if (sig_offset < expr->n_ins) {
#if TRACE_EVAL
                        printf("Incrementing signal idx to %d and jumping -%d\n",
                               sig_offset, tok->con.branch_offset);
#endif
                        tok -= tok->con.branch_offset;
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
                        if (vec_offset < v_in[sig_offset]->vlen) {
#if TRACE_EVAL
                            printf("Incrementing vector idx to %d of %d and jumping -%d\n",
                                   vec_offset, v_in[sig_offset]->vlen, tok->con.branch_offset);
#endif
                            tok -= tok->con.branch_offset;
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
                        printf("Incrementing vector idx to %d of %d and jumping -%d\n",
                               vec_offset, tok->con.reduce_stop, tok->con.branch_offset);
#endif
                        tok -= tok->con.branch_offset;
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
            assert(dp_from >= 0 && dp_from < expr_stk->size);
            ++dp;
            assert(dp < expr_stk->size);
            sp += vlen;
#if TRACE_EVAL
            printf("copying stk[%d] -> stk[%d]: ", dp_from, dp);
#endif
            dims[dp] = tok->gen.vec_len;
            types[dp] = tok->gen.datatype;
            if (dims[dp] < dims[dp_from])
                memcpy(&stk[sp], &stk[sp_from + vec_offset], tok->gen.vec_len * sizeof(mpr_expr_val_t));
            else
                memcpy(&stk[sp], &stk[sp_from], tok->gen.vec_len * sizeof(mpr_expr_val_t));
#if TRACE_EVAL
            print_stack_vec(stk + sp, types[dp], dims[dp]);
            printf(" \n");
#endif
            break;
        }
        case TOK_MOVE: {
            int dp_from = dp;
            int sp_from = sp;
            dp -= tok->con.cache_offset;
            assert(dp >= 0 && dp < expr_stk->size && dp_from >= 0);
            sp = dp * vlen;
#if TRACE_EVAL
            printf("moving eval stack top from %d to %d: ", dp_from, dp);
#endif
            memcpy(&stk[sp], &stk[sp_from], vlen * sizeof(mpr_expr_val_t));
            dims[dp] = dims[dp_from];
            types[dp] = types[dp_from];
#if TRACE_EVAL
            print_stack_vec(stk + sp, types[dp], dims[dp]);
            printf(" \n");
#endif
            break;
        }
        case TOK_VECTORIZE:
            /* don't need to copy vector elements from first token */
            dp -= tok->fn.arity - 1;
            assert(dp >= 0);
            sp = dp * vlen;
            j = dims[dp];
            for (i = 1; i < tok->fn.arity; i++) {
                memcpy(&stk[sp + j], &stk[sp + i * vlen], dims[dp + i] * sizeof(mpr_expr_val_t));
                j += dims[dp + i];
            }
            dims[dp] = j;
            types[dp] = tok->gen.datatype;
#if TRACE_EVAL
            printf("built %u-element vector: ", dims[dp]);
            print_stack_vec(stk + sp, types[dp], dims[dp]);
            printf(" \n");
#endif
            break;
        case TOK_ASSIGN:
        case TOK_ASSIGN_USE:
            if (VAR_Y == tok->var.idx)
                can_advance = 0;
        case TOK_ASSIGN_CONST: {
            mpr_value v;
            mpr_value_buffer b;
            /* currently only history and vector indices are supported for assignment */
            int idxp, hidx = tok->gen.flags & VAR_HIST_IDX, vidx = tok->gen.flags & VAR_VEC_IDX;
            int num_flags = NUM_VAR_IDXS(tok->gen.flags);
            if (num_flags) {
                dp -= num_flags;
                assert(dp >= 0);
                sp = dp * vlen;
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
                b = b_out;
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
                v = *v_vars + tok->var.idx;
                b = &v->inst[flags & VAR_INSTANCED ? inst_idx : 0];
            }
            else
                goto error;

            if (vidx) {
                if (MPR_INT32 != types[idxp])
                    goto error;
                ++idxp;
                vidx = stk[sp + vlen].i;
            }
            else
                vidx = tok->var.vec_idx;
            while (vidx < 0)
                vidx += v->vlen;
            if (hidx) {
                if (MPR_INT32 != types[idxp])
                    goto error;
                hidx = stk[idxp * vlen].i;
                if (hidx > 0 || hidx < -v_out->mlen)
                    goto error;
                /* TODO: enable full history assignment with user variables */
                if (VAR_Y != tok->var.idx)
                    hidx = 0;
                ++idxp;
            }
#if TRACE_EVAL
            if (VAR_Y == tok->var.idx)
                printf("assigning values to y");
            else
                printf("assigning values to %s", expr->vars[tok->var.idx].name);
            printf("{%s%d}", tok->gen.flags & VAR_HIST_IDX ? "N=" : "", hidx);
            printf("[%s%d]", tok->gen.flags & VAR_VEC_IDX ? "N=" : "", vidx);
            printf(" (%s x %u)\n", type_name(types[dp]), tok->gen.vec_len);
#endif
            i = (b->pos + v->mlen + hidx) % v->mlen;
            if (i < 0)
                i += v->mlen;

            /* Copy time from input */
            if (time) {
                mpr_time *tvar = &b->times[i];
                memcpy(tvar, time, sizeof(mpr_time));
            }

            switch (v->type) {
#define TYPED_CASE(MTYPE, TYPE, T)                                                              \
                case MTYPE: {                                                                   \
                    TYPE *a = (TYPE*)b->samps + i * v->vlen;                                    \
                    for (i = vidx, j = tok->var.offset; i < tok->gen.vec_len + vidx; i++, j++) {\
                        if (j >= dims[dp]) j = 0;                                               \
                        a[i] = stk[sp + j].T;                                                   \
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
            mpr_value_print_hist(v, inst_idx % v->num_inst);
#endif

            if (tok->var.idx == VAR_Y) {
                if (out_types) {
                    for (i = 0, j = vidx; i < tok->gen.vec_len; i++, j++) {
                        if (j >= v->vlen) j = 0;
                        out_types[j] = types[dp];
                    }
                }
            }
            else if (tok->var.idx == expr->inst_ctl) {
                if (alive && stk[sp].i == 0) {
                    if (status & EXPR_UPDATE)
                        status |= EXPR_RELEASE_AFTER_UPDATE;
                    else
                        status |= EXPR_RELEASE_BEFORE_UPDATE;
                }
                alive = stk[sp].i != 0;
                can_advance = 0;
            }
            else if (tok->var.idx == expr->mute_ctl) {
                muted = stk[sp].i != 0;
                can_advance = 0;
            }

        assign_done:
            /* If assignment was constant or history initialization, move expr
             * start token pointer so we don't evaluate this section again. */
            if (can_advance || tok->gen.flags & VAR_HIST_IDX) {
#if TRACE_EVAL
                printf("moving expr offset to %ld\n", tok - expr->start + 1);
#endif
                expr->offset = tok - expr->start + 1;
            }
            else
                can_advance = 0;

            if (tok->gen.flags & CLEAR_STACK)
                dp = -1;
            sp = dp * vlen;
            break;
        }
        case TOK_ASSIGN_TT: {
            int idx, hist;
            if (tok->var.idx != VAR_Y || !(tok->gen.flags & VAR_HIST_IDX))
                goto error;
#if TRACE_EVAL
            printf("assigning timetag to t_y{%d}\n",
                   tok->gen.flags & VAR_HIST_IDX ? stk[sp - vlen].i : 0);
#endif
            if (!v_out)
                return status;
            hist = tok->gen.flags & VAR_HIST_IDX;
            idx = (b_out->pos + v_out->mlen + (hist ? stk[sp - vlen].i : 0)) % v_out->mlen;
            if (idx < 0)
                idx = v_out->mlen + idx;
            mpr_time_set_dbl(&b_out->times[idx], stk[sp].d);
            /* If assignment was constant or history initialization, move expr
             * start token pointer so we don't evaluate this section again. */
            if (hist || can_advance) {
#if TRACE_EVAL
                printf("moving expr offset to %ld\n", tok - expr->start + 1);
#endif
                expr->offset = tok - expr->start + 1;
            }
            else
                can_advance = 0;
            if (tok->gen.flags & CLEAR_STACK)
                dp = -1;
            else if (hist) {
                --dp;
                assert(dp >= 0);
            }
            sp = dp * vlen;
            break;
        }
        default: goto error;
        }
        if (tok->gen.casttype) {
            assert(dp >= 0);
#if TRACE_EVAL
            printf("casting sp=%d from %s (%c) to %s (%c)\n", dp, type_name(types[dp]),
                   types[dp], type_name(tok->gen.casttype), tok->gen.casttype);
            print_stack_vec(stk + sp, types[dp], dims[dp]);
#endif
            /* need to cast to a different type */
            switch (types[dp]) {
#define TYPED_CASE(MTYPE0, T0, MTYPE1, TYPE1, T1, MTYPE2, TYPE2, T2)\
                case MTYPE0:                                        \
                    switch (tok->gen.casttype) {                    \
                        case MTYPE1:                                \
                            for (i = sp; i < sp + dims[dp]; i++)    \
                                stk[i].T1 = (TYPE1)stk[i].T0;       \
                            break;                                  \
                        case MTYPE2:                                \
                            for (i = sp; i < sp + dims[dp]; i++)    \
                                stk[i].T2 = (TYPE2)stk[i].T0;       \
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
#if TRACE_EVAL
            printf(" -> ");
            print_stack_vec(stk + sp, tok->gen.casttype, dims[dp]);
            printf("\n");
#endif
            types[dp] = tok->gen.casttype;
        }
        ++tok;
    }

    RETURN_ARG_UNLESS(v_out, status);

    if (!out_types) {
        void *v;
        /* Internal evaluation during parsing doesn't contain assignment token,
         * so we need to copy to output here. */

        /* Increment index position of output data structure. */
        b_out->pos = (b_out->pos + 1) % v_out->mlen;
        v = mpr_value_get_samp(v_out, inst_idx);
        switch (v_out->type) {
#define TYPED_CASE(MTYPE, TYPE, T)                              \
            case MTYPE:                                         \
                for (i = 0, j = sp; i < v_out->vlen; i++, j++)  \
                    ((TYPE*)v)[i] = stk[j].T;                   \
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
        --b_out->pos;
        if (b_out->pos < 0)
            b_out->pos = v_out->mlen - 1;
        return status;
    }

    return status;

  error:
#if TRACE_EVAL
    trace("Unexpected token in expression.");
#endif
    return 0;
}
