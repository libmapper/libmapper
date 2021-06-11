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
    uint8_t *dims;
    int size;
};

mpr_expr_stack mpr_expr_stack_new() {
    mpr_expr_stack stk = malloc(sizeof(struct _mpr_expr_stack));
    stk->stk = 0;
    stk->dims = 0;
    stk->size = 0;
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
        if (stk->dims)
            stk->dims = realloc(stk->dims, stk->size * sizeof(uint8_t));
        else
            stk->dims = malloc(stk->size * sizeof(uint8_t));
    }
}

void mpr_expr_stack_free(mpr_expr_stack stk) {
    if (stk->stk)
        free(stk->stk);
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
    FN_DELAY,
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
    { "delay",    1, 0, (void*)1,     0,                0,               },
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
    /* function names above this line are also found in pfn_table */
    VFN_NORM,
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
    { "maxmin", 3, 0, 0, vmaxmini, vmaxminf, vmaxmind },
    { "sumnum", 3, 0, 0, vsumnumi, vsumnumf, vsumnumd },
    { "angle",  2, 1, 0, 0,        vanglef,  vangled  },
    { "dot",    2, 1, 0, vdoti,    vdotf,    vdotd    },
};

typedef enum {
    PFN_UNKNOWN = -1,
    PFN_ALL = 0,
    PFN_ANY,
    PFN_CENTER,
    PFN_MAX,
    PFN_MEAN,
    PFN_MIN,
    PFN_SUM,
    /* function names above this line are also found in vfn_table */
    PFN_COUNT,
    PFN_INSTANCES,
    PFN_SIZE,
    N_PFN
} expr_pfn_t;

static struct {
    const char *name;
    unsigned int arity;
    expr_op_t op;
    expr_vfn_t vfn;
} pfn_tbl[] = {
    { "all",      2, OP_LOGICAL_AND, VFN_UNKNOWN },
    { "any",      2, OP_LOGICAL_OR,  VFN_UNKNOWN },
    { "center",   0, OP_UNKNOWN,     VFN_MAXMIN  },
    { "max",      2, OP_UNKNOWN,     VFN_MAX     },
    { "mean",     3, OP_UNKNOWN,     VFN_SUMNUM  },
    { "min",      2, OP_UNKNOWN,     VFN_MIN     },
    { "sum",      2, OP_ADD,         VFN_UNKNOWN },
    { "count",    0, OP_ADD,         VFN_UNKNOWN },
    { "instances",0, OP_UNKNOWN,     VFN_UNKNOWN }, /* replaced during parsing */
    { "size",     0, OP_UNKNOWN,     VFN_MAXMIN  },
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

#define CONST_MINVAL    0x0001
#define CONST_MAXVAL    0x0002
#define CONST_PI        0x0003
#define CONST_E         0x0004
#define CONST_SPECIAL   0x0007
#define CLEAR_STACK     0x0008
#define TYPE_LOCKED     0x0010
#define VAR_MUTED       0x0020
#define VAR_DELAY       0x0040
#define VEC_LEN_LOCKED  0x0080

enum toktype {
    TOK_LITERAL         = 0x0000001,
    TOK_VLITERAL        = 0x0000002,
    TOK_NEGATE          = 0x0000004,
    TOK_FN              = 0x0000008,
    TOK_VFN             = 0x0000010,
    TOK_VFN_DOT         = 0x0000020,
    TOK_PFN             = 0x0000040,
    TOK_OPEN_PAREN      = 0x0000080,
    TOK_MUTED           = 0x0000100,
    TOK_OPEN_SQUARE     = 0x0000200,
    TOK_OPEN_CURLY      = 0x0000400,
    TOK_CLOSE_PAREN     = 0x0000800,
    TOK_CLOSE_SQUARE    = 0x0001000,
    TOK_CLOSE_CURLY     = 0x0002000,
    TOK_VAR             = 0x0004000,
    TOK_VAR_NUM_INST    = 0x0008000,
    TOK_OP              = 0x0010000,
    TOK_COMMA           = 0x0020000,
    TOK_COLON           = 0x0040000,
    TOK_SEMICOLON       = 0x0080000,
    TOK_VECTORIZE       = 0x0100000,
    TOK_INSTANCES       = 0x0200000,
    TOK_ASSIGN          = 0x0400000,
    TOK_ASSIGN_USE,
    TOK_ASSIGN_CONST,
    TOK_ASSIGN_TT,
    TOK_TT              = 0x0800000,
    TOK_CACHE_INIT_INST,
    TOK_BRANCH_NEXT_INST,
    TOK_SP_ADD,
    TOK_END             = 0x1000000
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
    expr_op_t idx;
};

struct variable_type {
    enum toktype toktype;
    mpr_type datatype;
    mpr_type casttype;
    uint8_t vec_len;
    uint8_t flags;
    expr_var_t idx;
    uint8_t offset;         /* only used by TOK_ASSIGN* */
    uint8_t vec_idx;        /* only used by TOK_VAR and TOK_ASSIGN* */
};

struct function_type {
    enum toktype toktype;
    mpr_type datatype;
    mpr_type casttype;
    uint8_t vec_len;
    uint8_t flags;
    int idx;
    uint8_t arity;          /* used by TOK_FN, TOK_VFN, TOK_VECTORIZE */
    uint8_t inst_cache_pos; /* only used by TOK_BRANCH* */
};

typedef union _token {
    enum toktype toktype;
    struct generic_type gen;
    struct literal_type lit;
    struct operator_type op;
    struct variable_type var;
    struct function_type fn;
} mpr_token_t, *mpr_token;

#define VAR_ASSIGNED    0x0001
#define VAR_INSTANCED   0x0002
#define VAR_LEN_LOCKED  0x0004

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

#define FN_LOOKUP(LC, UC, CLOSE)                                \
static expr_##LC##_t LC##_lookup(const char *s, int len)        \
{                                                               \
    int i, j;                                                   \
    for (i=0; i<N_##UC; i++) {                                  \
        if (LC##_tbl[i].name && strlen(LC##_tbl[i].name)==len   \
            && strncmp_lc(s, LC##_tbl[i].name, len)==0) {       \
            /* check for parentheses */                         \
            j = strlen(LC##_tbl[i].name);                       \
            if (s[j] != '(')                                    \
                return UC##_UNKNOWN;                            \
            else if (CLOSE && s[j] && s[j+1] != ')')            \
                return UC##_UNKNOWN;                            \
            return i;                                           \
        }                                                       \
    }                                                           \
    return UC##_UNKNOWN;                                        \
}
FN_LOOKUP(fn, FN, 0)
FN_LOOKUP(vfn, VFN, 0)
FN_LOOKUP(pfn, PFN, 1)

static void var_lookup(mpr_token_t *tok, const char *s, int len)
{
    if ('t' == *s && '_' == *(s+1)) {
        tok->toktype = TOK_TT;
        s += 2;
        len -= 2;
        tok->var.idx = VAR_X;
    }
    else
        tok->toktype = TOK_VAR;
    if (*s == 'y' && 1 == len)
        tok->var.idx = VAR_Y;
    else if ('x' == *s) {
        if (1 == len)
            tok->var.idx = VAR_X;
        else {
            int i, ordinal = 1;
            for (i = 1; i < len; i++) {
                if (!isdigit(*(s+i))) {
                    ordinal = 0;
                    break;
                }
            }
            tok->var.idx = ordinal ? VAR_X + atoi(s+1) : VAR_UNKNOWN;
        }
    }
    else
        tok->var.idx = VAR_UNKNOWN;
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

static int const_tok_is_one(mpr_token_t tok)
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
        case TOK_ASSIGN_TT: return tok.gen.flags & VAR_DELAY ? 1 : 0;
        case TOK_OP:        return op_tbl[tok.op.idx].arity;
        case TOK_FN:        return fn_tbl[tok.fn.idx].arity;
        case TOK_PFN:       return pfn_tbl[tok.fn.idx].arity;
        case TOK_VFN:       return vfn_tbl[tok.fn.idx].arity;
        case TOK_VECTORIZE: return tok.fn.arity;
        default:            return 0;
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
            while (c && (isalpha(c) || c == '.'))
                c = str[++idx];
            ++i;
            if ((tok->fn.idx = vfn_lookup(str+i, idx-i)) != VFN_UNKNOWN)
                tok->toktype = TOK_VFN_DOT;
            else if ((tok->fn.idx = pfn_lookup(str+i, idx-i)) != PFN_UNKNOWN)
                tok->toktype = TOK_PFN;
            else
                break;
            return idx+2;
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
                var_lookup(tok, str+i, idx-i);
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
        /* could be either subtraction or negation */
        i = idx - 1;
        /* back up one character */
        while (i && strchr(" \t\r\n", str[i]))
           --i;
        if (isalpha(str[i]) || isdigit(str[i]) || strchr(")]}", str[i])) {
            SET_TOK_OPTYPE(OP_SUBTRACT);
        }
        else
            tok->toktype = TOK_NEGATE;
        return ++idx;
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
            var_lookup(tok, str+i, idx-i);
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
    uint8_t *in_hist_size;
    uint8_t out_hist_size;
    uint8_t n_vars;
    int8_t inst_ctl;
    int8_t mute_ctl;
    int8_t n_ins;
    uint8_t max_in_hist_size;
};

void mpr_expr_free(mpr_expr expr)
{
    int i;
    FUNC_IF(free, expr->in_hist_size);
    for (i = 0; i < expr->n_tokens; i++) {
        if (TOK_VLITERAL == expr->tokens[i].toktype && expr->tokens[i].lit.val.ip)
            free(expr->tokens[i].lit.val.ip);
    }
    FUNC_IF(free, expr->tokens);
    if (expr->n_vars && expr->vars) {
        for (i = 0; i < expr->n_vars; i++)
            free(expr->vars[i].name);
        free(expr->vars);
    }
    free(expr);
}

#ifdef TRACE_PARSE

static void printtoken(mpr_token_t t, mpr_var_t *vars)
{
    int i, len = 128, offset = 0, delay = t.gen.flags & VAR_DELAY;
    char s[128];
    switch (t.toktype) {
        case TOK_LITERAL:
            switch (t.gen.flags & CONST_SPECIAL) {
                case CONST_MAXVAL:  snprintf(s, len, "max%c", t.lit.datatype);  break;
                case CONST_MINVAL:  snprintf(s, len, "min%c", t.lit.datatype);  break;
                case CONST_PI:      snprintf(s, len, "pi%c", t.lit.datatype);   break;
                case CONST_E:       snprintf(s, len, "e%c", t.lit.datatype);    break;
                default:
                    switch (t.gen.datatype) {
                        case MPR_FLT:   snprintf(s, len, "%g", t.lit.val.f);    break;
                        case MPR_DBL:   snprintf(s, len, "%g", t.lit.val.d);    break;
                        case MPR_INT32: snprintf(s, len, "%d", t.lit.val.i);    break;
                    }                                                           break;
            }
                                                                                break;
        case TOK_VLITERAL:
            offset = snprintf(s, len, "[");
            switch (t.gen.datatype) {
                case MPR_FLT:
                    for (i = 0; i < t.lit.vec_len; i++)
                        offset += snprintf(s + offset, len - offset, "%g,", t.lit.val.fp[i]);
                    break;
                case MPR_DBL:
                    for (i = 0; i < t.lit.vec_len; i++)
                        offset += snprintf(s + offset, len - offset, "%g,", t.lit.val.dp[i]);
                    break;
                case MPR_INT32:
                    for (i = 0; i < t.lit.vec_len; i++)
                        offset += snprintf(s + offset, len - offset, "%d,", t.lit.val.ip[i]);
                    break;
            }
            --offset;
            offset += snprintf(s + offset, len - offset, "]");
            break;
        case TOK_OP:            snprintf(s, len, "op.%s", op_tbl[t.op.idx].name); break;
        case TOK_OPEN_CURLY:    snprintf(s, len, "{");                          break;
        case TOK_OPEN_PAREN:    snprintf(s, len, "( arity %d", t.fn.arity);     break;
        case TOK_OPEN_SQUARE:   snprintf(s, len, "[");                          break;
        case TOK_CLOSE_CURLY:   snprintf(s, len, "}");                          break;
        case TOK_CLOSE_PAREN:   snprintf(s, len, ")");                          break;
        case TOK_CLOSE_SQUARE:  snprintf(s, len, "]");                          break;
        case TOK_VAR:
            if (t.var.idx == VAR_Y)
                snprintf(s, len, "var.y%s[%u]", delay ? "{N}" : "", t.var.vec_idx);
            else if (t.var.idx >= VAR_X)
                snprintf(s, len, "var.x%d%s[%u]", t.var.idx - VAR_X, delay ? "{N}" : "", t.var.vec_idx);
            else
                snprintf(s, len, "var.%s%s%s[%u/%u]", vars ? vars[t.var.idx].name : "?",
                         vars ? (vars[t.var.idx].flags & VAR_INSTANCED) ? ".N" : ".0" : ".?",
                         delay ? "{N}" : "", t.var.vec_idx, vars ? vars[t.var.idx].vec_len : 0);
            break;
        case TOK_VAR_NUM_INST:
            if (t.var.idx == VAR_Y)
                snprintf(s, len, "var.y.count()");
            else if (t.var.idx >= VAR_X)
                snprintf(s, len, "var.x%d.count()", t.var.idx - VAR_X);
            else
                snprintf(s, len, "var.%s%s.count()", vars ? vars[t.var.idx].name : "?",
                         vars ? (vars[t.var.idx].flags & VAR_INSTANCED) ? ".N" : ".0" : ".?");
            break;
        case TOK_TT:
            if (t.var.idx == VAR_Y)
                snprintf(s, len, "tt.y%s", delay ? "{N}" : "");
            else if (t.var.idx >= VAR_X)
                snprintf(s, len, "tt.x%d%s", t.var.idx - VAR_X, delay ? "{N}" : "");
            else
                snprintf(s, len, "tt.%s%s", vars ? vars[t.var.idx].name : "?", delay ? "{N}" : "");
            break;
        case TOK_FN:        snprintf(s, len, "fn.%s(arity %d)", fn_tbl[t.fn.idx].name, t.fn.arity); break;
        case TOK_COMMA:     snprintf(s, len, ",");                              break;
        case TOK_COLON:     snprintf(s, len, ":");                              break;
        case TOK_VECTORIZE: snprintf(s, len, "VECT(%d)", t.fn.arity);           break;
        case TOK_NEGATE:    snprintf(s, len, "-");                              break;
        case TOK_VFN:
        case TOK_VFN_DOT:   snprintf(s, len, "vfn.%s(arity %d)", vfn_tbl[t.fn.idx].name, t.fn.arity); break;
        case TOK_PFN:       snprintf(s, len, "pfn.%s(arity %d)", pfn_tbl[t.fn.idx].name, t.fn.arity); break;
        case TOK_INSTANCES: snprintf(s, len, "instances()");                    break;
        case TOK_ASSIGN:
        case TOK_ASSIGN_CONST:
        case TOK_ASSIGN_USE:
            if (t.var.idx == VAR_Y)
                snprintf(s, len, "ASSIGN_TO:y%s[%u]->[%u]%s", delay ? "{N}" : "", t.var.offset,
                         t.var.vec_idx, t.toktype == TOK_ASSIGN_CONST ? " (const) " : "");
            else
                snprintf(s, len, "ASSIGN_TO:%s%s%s[%u]->[%u]%s", vars ? vars[t.var.idx].name : "?",
                         vars ? (vars[t.var.idx].flags & VAR_INSTANCED) ? ".N" : ".0" : ".?",
                         delay ? "{N}" : "", t.var.offset, t.var.vec_idx,
                         t.toktype == TOK_ASSIGN_CONST ? " (const) " : "");
            break;
        case TOK_ASSIGN_TT:
            snprintf(s, len, "ASSIGN_TO:t_y%s->[%u]", delay ? "{N}" : "", t.var.vec_idx);
            break;
        case TOK_CACHE_INIT_INST:   snprintf(s, len, "<cache instance>");       break;
        case TOK_BRANCH_NEXT_INST:  snprintf(s, len, "<branch next instance %d, inst cache %d>",
                                             t.lit.val.i, t.fn.inst_cache_pos); break;
        case TOK_SP_ADD:            snprintf(s, len, "<sp add %d>", t.lit.val.i); break;
        case TOK_SEMICOLON:         snprintf(s, len, "semicolon");              break;
        case TOK_END:               printf("END\n");                            return;
        default:                    printf("(unknown token)\n");                return;
    }
    printf("%s", s);
    /* indent */
    len = strlen(s);
    for (i = len; i < 40; i++)
        printf(" ");
    printf("%c%u", t.gen.datatype, t.gen.vec_len);
    if (t.toktype != TOK_LITERAL && t.toktype < TOK_ASSIGN && t.gen.casttype)
        printf("->%c", t.gen.casttype);
    else
        printf("   ");
    if (t.gen.flags & VEC_LEN_LOCKED)
        printf(" vec_lock");
    if (t.gen.flags & TYPE_LOCKED)
        printf(" type_lock");
    if (t.gen.flags & CLEAR_STACK)
        printf(" clear");
    printf("\n");
}

static void printstack(const char *s, mpr_token_t *stk, int sp, mpr_var_t *vars, int show_init_line)
{
    int i, j, indent = 0, can_advance = 1;
    printf("%s ", s);
    if (s)
        indent = strlen(s) + 1;
    if (sp < 0) {
        printf("EMPTY\n");
        return;
    }
    for (i = 0; i <= sp; i++) {
        if (i != 0) {
            for (j = 0; j < indent; j++)
                printf(" ");
        }
        printtoken(stk[i], vars);
        if (show_init_line && can_advance) {
            switch (stk[i].toktype) {
                case TOK_ASSIGN_CONST:
                    if (stk[i].var.idx != VAR_Y)
                        break;
                case TOK_ASSIGN:
                case TOK_ASSIGN_USE:
                case TOK_ASSIGN_TT:
                    /* look ahead for future assignments */
                    for (j = i + 1; j <= sp; j++) {
                        if (stk[j].toktype < TOK_ASSIGN)
                            continue;
                        if (TOK_ASSIGN_CONST == stk[j].toktype && stk[j].var.idx != VAR_Y)
                            break;
                        if (stk[j].gen.flags & VAR_DELAY)
                            break;
                        for (j = 0; j < indent; j++)
                            printf(" ");
                        printf("--- <INITIALISATION DONE> ---\n");
                        can_advance = 0;
                        break;
                    }
                    break;
                case TOK_PFN:
                case TOK_VAR:
                    if (stk[i].var.idx >= VAR_X)
                        can_advance = 0;
                    break;
                default:
                    break;
            }
        }
    }
    if (!i)
        printf("\n");
}

void printexpr(const char *s, mpr_expr e)
{
    printstack(s, e->tokens, e->n_tokens - 1, e->vars, 1);
}

#endif /* TRACE_PARSE */

static mpr_type compare_token_datatype(mpr_token_t tok, mpr_type type)
{
    if (tok.toktype >= TOK_CACHE_INIT_INST)
        return type;
    /* return the higher datatype */
    if (tok.gen.datatype == MPR_DBL || type == MPR_DBL)
        return MPR_DBL;
    else if (tok.gen.datatype == MPR_FLT || type == MPR_FLT)
        return MPR_FLT;
    else
        return MPR_INT32;
}

static mpr_type promote_token_datatype(mpr_token_t *tok, mpr_type type)
{
    if (tok->toktype >= TOK_CACHE_INIT_INST)
        return type;

    tok->gen.casttype = 0;

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
                float *tmp = malloc(tok->lit.vec_len * sizeof(float));
                for (i = 0; i < tok->lit.vec_len; i++)
                    tmp[i] = (float)tok->lit.val.ip[i];
                free(tok->lit.val.ip);
                tok->lit.val.fp = tmp;
                tok->lit.datatype = type;
            }
            else if (MPR_DBL == type) {
                double *tmp = malloc(tok->lit.vec_len * sizeof(double));
                for (i = 0; i < tok->lit.vec_len; i++)
                    tmp[i] = (double)tok->lit.val.ip[i];
                free(tok->lit.val.ip);
                tok->lit.val.dp = tmp;
                tok->lit.datatype = type;
            }
        }
        else if (MPR_FLT == tok->lit.datatype) {
            if (MPR_DBL == type) {
                double *tmp = malloc(tok->lit.vec_len * sizeof(double));
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
    else if (TOK_VAR == tok->toktype || TOK_VAR_NUM_INST == tok->toktype || TOK_PFN == tok->toktype) {
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
                    case MPR_FLT:   stk[sp].lit.val.f = FLT_MIN;    break;
                    case MPR_DBL:   stk[sp].lit.val.d = DBL_MIN;    break;
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

    s = b.samps = malloc(mpr_type_get_size(stk[len - 1].gen.datatype) * vec_len);

    v.inst = &b;
    v.vlen = vec_len;
    v.type = stk[len - 1].gen.datatype;

    expr_stack_realloc(eval_stk, len * vec_len);

    if (!(mpr_expr_eval(eval_stk, &e, 0, 0, &v, 0, 0, 0) & 1)) {
        free(s);
        return 0;
    }

    /* free token vector memory if necessary */
    for (i = 0; i < len; i++) {
        if (TOK_VLITERAL == stk[i].toktype && stk[i].lit.val.ip)
            free(stk[i].lit.val.ip);
    }

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
            if (stk[sp].fn.idx >= FN_DELAY)
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
            arity = stk[sp].gen.flags & VAR_DELAY ? 2 : 1;
            can_precompute = 0;
            break;
        default:
            return sp;
    }
    if (arity) {
        /* find operator or function inputs */
        int skip = 0;
        int depth = arity;
        int operand = 0;
        i = sp;

        /* Walk down stack distance of arity, checking types. */
        while (--i >= 0) {
            if (stk[i].toktype >= TOK_CACHE_INIT_INST) {
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
                if (enable_optimize && stk[i].toktype == TOK_LITERAL && stk[sp].toktype == TOK_OP
                    && depth <= op_tbl[stk[sp].op.idx].arity) {
                    if (const_tok_is_zero(stk[i])) {
                        /* mask and bitshift, depth == 1 or 2 */
                        optimize = (op_tbl[stk[sp].op.idx].optimize_const_ops >> (depth - 1) * 4) & 0xF;
                    }
                    else if (const_tok_is_one(stk[i])) {
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
                type = compare_token_datatype(stk[i], type);
                if (stk[i].gen.vec_len > vec_len)
                    vec_len = stk[i].gen.vec_len;
                --depth;
                if (depth == 0)
                    break;
            }
            else
                --skip;

            switch (stk[i].toktype) {
                case TOK_OP:         skip += op_tbl[stk[i].op.idx].arity;           break;
                case TOK_FN:         skip += fn_tbl[stk[i].fn.idx].arity;           break;
                case TOK_VFN:
                    skip += vfn_tbl[stk[i].fn.idx].arity;
                    if (VFN_MAXMIN == stk[i].fn.idx || VFN_SUMNUM == stk[i].fn.idx)
                        --skip; /* these functions have 2 outputs */
                    break;
                case TOK_VECTORIZE:  skip += stk[i].fn.arity;                       break;
                case TOK_ASSIGN_USE: ++skip;                                        break;
                case TOK_VAR:        skip += stk[i].gen.flags & VAR_DELAY ? 1 : 0;  break;
                default:                                                            break;
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
            case TOK_VECTORIZE:  skip = stk[sp].fn.arity;                       depth = 0;   break;
            case TOK_ASSIGN_USE: skip = 1;                                      depth = 0;   break;
            case TOK_VAR:        skip = stk[sp].gen.flags & VAR_DELAY ? 1 : 0;  depth = 0;   break;
            default:             skip = 0;                                  depth = arity;   break;
        }
        type = promote_token_datatype(&stk[i], type);
        while (--i >= 0) {
            if (stk[i].toktype >= TOK_CACHE_INIT_INST)
                continue;
            /* we will promote types within range of compound arity */
            if ((stk[i+1].toktype != TOK_VAR && stk[i+1].toktype != TOK_TT)
                || !(stk[i+1].gen.flags & VAR_DELAY)) {
                /* don't promote type of history indices */
                type = promote_token_datatype(&stk[i], type);
            }

            if (skip <= 0) {
                --depth;
                if (!(stk[i].gen.flags & VEC_LEN_LOCKED)) {
                    stk[i].var.vec_len = vec_len;
                    if (TOK_VAR == stk[i].toktype && stk[i].var.idx < N_USER_VARS)
                        vars[stk[i].var.idx].vec_len = vec_len;
                }
            }

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
                case TOK_VAR:
                    if (stk[i].gen.flags & VAR_DELAY) {
                        if (skip > 0)
                            ++skip;
                        else
                            ++depth;
                    }
                    break;
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
        if (stk[sp].toktype != TOK_VFN)
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
        if (TOK_BRANCH_NEXT_INST != stk[idx].toktype && TOK_SP_ADD != stk[idx].toktype)
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
    int i = sp, optimize = 1, expr_len = 0;
    uint8_t vec_len = 0;
    expr_var_t var = stk[sp].var.idx;

    while (i >= 0 && (stk[i].toktype & TOK_ASSIGN) && (stk[i].var.idx == var)) {
        vec_len += stk[i].gen.vec_len;
        --i;
        ++expr_len;
    }
    expr_len += substack_len(stk, i + 1) - 1;

    if (expr_len > sp + 1) {
        trace("Malformed expression (1)\n");
        return -1;
    }
    promote_token_datatype(&stk[i], stk[sp].gen.datatype);
    if (check_type(eval_stk, stk, i, vars, optimize) == -1)
        return -1;
    promote_token_datatype(&stk[i], stk[sp].gen.datatype);

    if (stk[sp].var.idx < N_USER_VARS) {
        /* Check if this expression assignment is instance-reducing */
        int reducing = 1, skipping = 0;
        for (i = 0; i < expr_len; i++) {
            switch (stk[sp - i].toktype) {
                case TOK_BRANCH_NEXT_INST:
                    skipping = 1;
                    reducing *= 2;
                    break;
                case TOK_CACHE_INIT_INST:
                    skipping = 0;
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

    if (!(stk[sp].gen.flags & VAR_DELAY))
        return 0;

    /* Need to move assignment statements to beginning of stack. */

    if (expr_len == sp + 1) {
        /* This statement is already at the start of the expression stack. */
        return 0;
    }

    for (i = sp - expr_len; i > 0; i--) {
        if (stk[i].toktype & TOK_ASSIGN && (stk[i].gen.flags & VAR_DELAY)) {
            ++i;
            break;
        }
    }

    if (i != (sp - expr_len + 1)) {
        /* This expression statement needs to be moved. */
        mpr_token_t *temp = alloca(expr_len * sizeof(mpr_token_t));
        memcpy(temp, stk + sp - expr_len + 1, expr_len * sizeof(mpr_token_t));
        memcpy(stk + i + expr_len, stk + i, (sp - expr_len - i + 1) * sizeof(mpr_token_t));
        memcpy(stk + i, temp, expr_len * sizeof(mpr_token_t));
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

static int _get_num_input_slots(mpr_expr expr)
{
    /* actually need to return highest numbered input slot */
    int i, count = -1;
    mpr_token_t *tok = expr->tokens;
    for (i = 0; i < expr->n_tokens; i++) {
        if (tok[i].toktype == TOK_VAR && tok[i].var.idx > count)
            count = tok[i].var.idx;
    }
    return count >= VAR_X ? count - VAR_X + 1 : 0;
}

static int _eval_stack_size(mpr_token_t *token_stack, int token_stack_len)
{
    int i = 0, sp = 0, eval_stack_len = 0;
    mpr_token_t *tok = token_stack;
    while (i < token_stack_len && tok->toktype != TOK_END) {
        switch (tok->toktype) {
            case TOK_CACHE_INIT_INST:
            case TOK_LITERAL:
            case TOK_VAR:
            case TOK_TT:                if (!(tok->gen.flags & VAR_DELAY)) ++sp; break;
            case TOK_OP:                sp -= op_tbl[tok->op.idx].arity - 1;    break;
            case TOK_FN:                sp -= fn_tbl[tok->fn.idx].arity - 1;    break;
            case TOK_VFN:               sp -= vfn_tbl[tok->fn.idx].arity - 1;   break;
            case TOK_SP_ADD:            sp += tok->lit.val.i;                   break;
            case TOK_BRANCH_NEXT_INST:  --sp;                                   break;
            case TOK_VECTORIZE:         sp -= tok->fn.arity - 1;                break;
            case TOK_ASSIGN:
            case TOK_ASSIGN_USE:
            case TOK_ASSIGN_CONST:
            case TOK_ASSIGN_TT:
                if (tok->gen.flags & VAR_DELAY)
                    --sp;
                if (tok->toktype != TOK_ASSIGN_USE)
                    --sp;
                break;
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
#define FAIL(msg) {                                                 \
    while (--n_vars >= 0)                                           \
        free(vars[n_vars].name);                                    \
    trace("%s\n", msg);                                             \
    return 0;                                                       \
}
#define FAIL_IF(condition, msg)                                     \
    if (condition) {FAIL(msg)}
#define PUSH_TO_OUTPUT(x)                                           \
{                                                                   \
    {FAIL_IF(++out_idx >= STACK_SIZE, "Stack size exceeded. (1)");} \
    if (x.toktype == TOK_ASSIGN_CONST && !is_const)                 \
        x.toktype = TOK_ASSIGN;                                     \
    memcpy(out + out_idx, &x, sizeof(mpr_token_t));                 \
}
#define PUSH_INT_TO_OUTPUT(x)                                       \
{                                                                   \
    tok.toktype = TOK_LITERAL;                                      \
    tok.lit.datatype = MPR_INT32;                                   \
    tok.lit.val.i = x;                                              \
    PUSH_TO_OUTPUT(tok);                                            \
}
#define POP_OUTPUT() ( out_idx-- )
#define PUSH_TO_OPERATOR(x)                                         \
{                                                                   \
    {FAIL_IF(++op_idx >= STACK_SIZE, "Stack size exceeded. (2)");}  \
    memcpy(op + op_idx, &x, sizeof(mpr_token_t));                   \
}
#define POP_OPERATOR() ( op_idx-- )
#define POP_OPERATOR_TO_OUTPUT()                                    \
{                                                                   \
    PUSH_TO_OUTPUT(op[op_idx]);                                     \
    out_idx = check_type(eval_stk, out, out_idx, vars, 1);          \
    {FAIL_IF(out_idx < 0, "Malformed expression (3).");}            \
    POP_OPERATOR();                                                 \
}
#define GET_NEXT_TOKEN(x)                                           \
{                                                                   \
    lex_idx = expr_lex(str, lex_idx, &x);                           \
    {FAIL_IF(!lex_idx, "Error in lexer.");}                         \
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

#define ASSIGN_MASK (TOK_VAR | TOK_OPEN_SQUARE | TOK_COMMA | TOK_CLOSE_SQUARE | TOK_CLOSE_CURLY \
                     | TOK_OPEN_CURLY | TOK_NEGATE | TOK_LITERAL)
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
    int allow_toktype = 0x2FFFFF;
    int in_vec_len = 0;

    mpr_var_t vars[N_USER_VARS];
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

#if TRACE_PARSE
    printf("parsing expression '%s'\n", str);
#endif

    while (str[lex_idx]) {
        GET_NEXT_TOKEN(tok);
        if (assigning) {
            {FAIL_IF(tok.toktype < TOK_ASSIGN && !(tok.toktype & allow_toktype & ASSIGN_MASK),
                     "Illegal token sequence. (1)");}
        }
        else if (!(tok.toktype & allow_toktype))
            {FAIL("Illegal token sequence. (2)");}
        switch (tok.toktype) {
            case TOK_OPEN_CURLY:
            case TOK_OPEN_SQUARE:
                if (!(var_flags & tok.toktype))
                    var_flags = 0;
                break;
            default:
                if (!(var_flags & (TOK_CLOSE_CURLY | TOK_CLOSE_SQUARE)))
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
            case TOK_TT:
                if (tok.var.idx >= VAR_X) {
                    int slot = tok.var.idx - VAR_X;
                    {FAIL_IF(slot >= n_ins, "Input slot index > number of sources.");}
                    tok.gen.datatype = in_types[slot];
                    tok.gen.vec_len = (TOK_VAR == tok.toktype) ? in_vec_lens[slot] : 1;
                    in_vec_len = tok.gen.vec_len;
                    tok.gen.flags |= VEC_LEN_LOCKED;
                    is_const = 0;
                }
                else if (tok.var.idx == VAR_Y) {
                    tok.gen.datatype = out_type;
                    tok.gen.vec_len = (TOK_VAR == tok.toktype) ? out_vec_len : 1;
                    tok.gen.flags |= VEC_LEN_LOCKED;
                }
                else {
                    /* get name of variable */
                    int len, idx = lex_idx - 1;
                    char c = str[idx];
                    while (idx >= 0 && c && (isalpha(c) || isdigit(c) || '_' == c)) {
                        if (--idx >= 0)
                            c = str[idx];
                    }

                    len = lex_idx - idx - 1;
                    i = find_var_by_name(vars, n_vars, str+idx+1, len);
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
                        vars[n_vars].name = malloc(lex_idx - idx);
                        snprintf(vars[n_vars].name, lex_idx - idx, "%s", str+idx+1);
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
                }
                tok.var.vec_idx = 0;
                if (muted)
                    tok.gen.flags |= VAR_MUTED;
                /* timetag tokens have type double */
                if (tok.toktype == TOK_TT)
                    tok.gen.datatype = MPR_DBL;
                PUSH_TO_OUTPUT(tok);
                /* variables can have vector and history indices */
                var_flags = TOK_OPEN_SQUARE | TOK_OPEN_CURLY;
                allow_toktype = (var_flags | (assigning ? TOK_ASSIGN | TOK_ASSIGN_TT : 0));
                if (TOK_VAR == tok.toktype)
                    allow_toktype |= TOK_VFN_DOT | TOK_PFN;
                if (tok.var.idx != VAR_Y || out_assigned > 1)
                    allow_toktype |= (TOK_OP | TOK_CLOSE_PAREN | TOK_CLOSE_SQUARE | TOK_CLOSE_CURLY
                                      | TOK_COMMA | TOK_COLON | TOK_SEMICOLON);
                muted = 0;
                break;
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
                if (tok.fn.idx >= FN_DELAY)
                    is_const = 0;
                if (fn_tbl[tok.fn.idx].memory) {
                    newtok.toktype = TOK_VAR;
                    newtok.gen.flags = 0;
                    PUSH_TO_OUTPUT(newtok);
                }
                break;
            }
            case TOK_PFN: {
                int pre, sslen;
                expr_pfn_t pfn;
                {FAIL_IF(PFN_INSTANCES != tok.fn.idx,
                         "Instance reduce functions must start with 'instances()'.");}
                GET_NEXT_TOKEN(tok);
                {FAIL_IF(TOK_PFN != tok.toktype && TOK_VFN_DOT != tok.toktype,
                         "instances() must be followed by a reduce function.");}
                pfn = tok.fn.idx;
                if (PFN_COUNT == pfn && TOK_VAR == out[out_idx].toktype) {
                    /* Special case: count() can be represented by single token */
                    out[out_idx].toktype = TOK_VAR_NUM_INST;
                    out[out_idx].gen.datatype = MPR_INT32;
                    break;
                }

                /* get compound arity of last token */
                sslen = substack_len(out, out_idx);
                switch (pfn) {
                    case PFN_MEAN: case PFN_CENTER: case PFN_SIZE:  pre = 3; break;
                    default:                                        pre = 2; break;
                }

                {FAIL_IF(out_idx + pre > STACK_SIZE, "Stack size exceeded. (3)");}
                /* copy substack to after prefix */
                out_idx = out_idx - sslen + 1;
                memcpy(out + out_idx + pre, out + out_idx, sizeof(mpr_token_t) * sslen);
                --out_idx;

                /* all instance reduce functions require this token */
                tok.toktype = TOK_CACHE_INIT_INST;
                PUSH_TO_OUTPUT(tok);

                switch (pfn) {
                    case PFN_CENTER:
                    case PFN_MAX:
                    case PFN_SIZE:
                        /* some reduce functions need init with the value from first iteration */
                        tok.toktype = TOK_LITERAL;
                        tok.gen.flags = CONST_MINVAL;
                        PUSH_TO_OUTPUT(tok);
                        if (PFN_MAX == pfn)
                            break;
                        tok.toktype = TOK_LITERAL;
                        tok.gen.flags = CONST_MAXVAL;
                        PUSH_TO_OUTPUT(tok);
                        break;
                    case PFN_MIN:
                        tok.toktype = TOK_LITERAL;
                        tok.gen.flags = CONST_MAXVAL;
                        PUSH_TO_OUTPUT(tok);
                        break;
                    case PFN_ALL:
                    case PFN_ANY:
                    case PFN_COUNT:
                    case PFN_MEAN:
                    case PFN_SUM:
                        PUSH_INT_TO_OUTPUT(PFN_ALL == pfn);
                        if (PFN_COUNT == pfn || PFN_MEAN == pfn)
                            PUSH_INT_TO_OUTPUT(PFN_COUNT == pfn);
                        break;
                    default:
                        break;
                }

                /* skip to after substack */
                if (PFN_COUNT != pfn)
                    out_idx += sslen;

                if (OP_UNKNOWN != pfn_tbl[pfn].op) {
                    tok.toktype = TOK_OP;
                    tok.op.idx = pfn_tbl[pfn].op;
                    /* don't use macro here since we don't want to optimize away initialization args */
                    PUSH_TO_OUTPUT(tok);
                    out_idx = check_type(eval_stk, out, out_idx, vars, 0);
                    {FAIL_IF(out_idx < 0, "Malformed expression (11).");}
                }
                if (VFN_UNKNOWN != pfn_tbl[pfn].vfn) {
                    tok.toktype = TOK_VFN;
                    tok.fn.idx = pfn_tbl[pfn].vfn;
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

                if (PFN_CENTER == pfn || PFN_MEAN == pfn || PFN_SIZE == pfn) {
                    tok.toktype = TOK_SP_ADD;
                    tok.lit.val.i = 1;
                    PUSH_TO_OUTPUT(tok);
                }

                /* all instance reduce functions require these tokens */
                tok.toktype = TOK_BRANCH_NEXT_INST;
                if (PFN_CENTER == pfn || PFN_MEAN == pfn || PFN_SIZE == pfn) {
                    tok.lit.val.i = -3 - sslen;
                    tok.fn.inst_cache_pos = 2;
                }
                else {
                    tok.lit.val.i = -2 - sslen;
                    tok.fn.inst_cache_pos = 1;
                }
                tok.fn.arity = 0;
                PUSH_TO_OUTPUT(tok);

                if (PFN_CENTER == pfn) {
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
                else if (PFN_MEAN == pfn) {
                    tok.toktype = TOK_OP;
                    tok.op.idx = OP_DIVIDE;
                    PUSH_TO_OPERATOR(tok);
                    POP_OPERATOR_TO_OUTPUT();
                }
                else if (PFN_SIZE == pfn) {
                    tok.toktype = TOK_OP;
                    tok.op.idx = OP_SUBTRACT;
                    PUSH_TO_OPERATOR(tok);
                    POP_OPERATOR_TO_OUTPUT();
                }
                allow_toktype = (TOK_OP | TOK_CLOSE_PAREN | TOK_CLOSE_SQUARE | TOK_CLOSE_CURLY
                                 | TOK_COMMA | TOK_COLON | TOK_SEMICOLON);
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
                tok.toktype = TOK_VFN;
                tok.gen.datatype = vfn_tbl[tok.fn.idx].fn_int ? MPR_INT32 : MPR_FLT;
                tok.fn.arity = vfn_tbl[tok.fn.idx].arity;
                tok.gen.vec_len = 1;
                PUSH_TO_OPERATOR(tok);
                POP_OPERATOR_TO_OUTPUT();
                allow_toktype = (TOK_OP | TOK_CLOSE_PAREN | TOK_CLOSE_SQUARE | TOK_CLOSE_CURLY
                                 | TOK_COMMA | TOK_COLON | TOK_SEMICOLON | TOK_PFN);
                break;
            case TOK_OPEN_PAREN:
                if (TOK_FN == op[op_idx].toktype && fn_tbl[op[op_idx].fn.idx].memory)
                    tok.fn.arity = 2;
                else
                    tok.fn.arity = 1;
                PUSH_TO_OPERATOR(tok);
                allow_toktype = OBJECT_TOKENS;
                break;
            case TOK_CLOSE_CURLY:
            case TOK_CLOSE_PAREN: {
                int arity;
                /* pop from operator stack to output until left parenthesis found */
                while (op_idx >= 0 && op[op_idx].toktype != TOK_OPEN_PAREN)
                    POP_OPERATOR_TO_OUTPUT();
                {FAIL_IF(op_idx < 0, "Unmatched parentheses or misplaced comma.");}

                arity = op[op_idx].fn.arity;
                /* remove left parenthesis from operator stack */
                POP_OPERATOR();

                allow_toktype = (TOK_OP | TOK_COLON | TOK_SEMICOLON | TOK_COMMA | TOK_CLOSE_PAREN
                                 | TOK_CLOSE_SQUARE | TOK_CLOSE_CURLY | TOK_VFN_DOT | TOK_PFN);
                if (tok.toktype == TOK_CLOSE_CURLY)
                    allow_toktype |= TOK_OPEN_SQUARE;

                if (op_idx < 0)
                    break;

                /* if operator stack[sp] is tok_fn or tok_vfn, pop to output */
                if (op[op_idx].toktype == TOK_FN) {
                    if (FN_DELAY == op[op_idx].fn.idx) {
                        int buffer_size = 0;
                        switch (arity) {
                            case 2:
                                /* max delay should be at the top of output stack */
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
                                {FAIL_IF(out[out_idx].toktype != TOK_VAR && out[out_idx].toktype != TOK_TT,
                                         "delay on non-variable token.");}
                                if (!buffer_size) {
                                    {FAIL_IF(out[out_idx - 1].toktype != TOK_LITERAL,
                                             "variable history indices must include maximum value.");}
                                    switch (out[out_idx - 1].gen.datatype) {
#define TYPED_CASE(MTYPE, T)                                                                \
                                        case MTYPE:                                         \
                                            buffer_size = (int)out[out_idx - 1].lit.val.T;  \
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
                                    memcpy(&out[out_idx - 1], &out[out_idx], sizeof(mpr_token_t));
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
                                out[out_idx].gen.flags |= VAR_DELAY;
                                if (assigning)
                                    out[out_idx - 1].gen.flags |= (TYPE_LOCKED | VEC_LEN_LOCKED);
                                POP_OPERATOR();
                                break;
                            default:
                                {FAIL("Illegal arity for variable delay.");}
                        }
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
                else if (op[op_idx].toktype == TOK_VFN) {
                    /* check arity */
                    {FAIL_IF(arity != vfn_tbl[op[op_idx].fn.idx].arity, "VFN arity mismatch.");}
                    POP_OPERATOR_TO_OUTPUT();
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
                if (op[op_idx].toktype == TOK_VECTORIZE) {
                    switch (out[out_idx].toktype) {
                        case TOK_BRANCH_NEXT_INST:
                            op[op_idx].gen.vec_len += out[out_idx-1].gen.vec_len;
                            ++op[op_idx].fn.arity;
                            break;
                        case TOK_LITERAL:
                            if (vectorizing && _squash_to_vector(out, out_idx)) {
                                POP_OUTPUT();
                                break;
                            }
                        default:
                            op[op_idx].gen.vec_len += out[out_idx].gen.vec_len;
                            ++op[op_idx].fn.arity;
                    }
                }
                else
                    ++op[op_idx].fn.arity;
                allow_toktype = OBJECT_TOKENS;
                break;
            case TOK_COLON:
                /* pop from operator stack to output until conditional found */
                while (op_idx >= 0 && (op[op_idx].toktype != TOK_OP || op[op_idx].op.idx != OP_IF)) {
                    POP_OPERATOR_TO_OUTPUT();
                }
                {FAIL_IF(op_idx < 0, "Unmatched colon.");}
                op[op_idx].op.idx = OP_IF_THEN_ELSE;
                allow_toktype = OBJECT_TOKENS;
                break;
            case TOK_SEMICOLON: {
                int var_idx;
                /* finish popping operators to output, check for unbalanced parentheses */
                while (op_idx >= 0 && op[op_idx].toktype < TOK_ASSIGN) {
                    if (op[op_idx].toktype == TOK_OPEN_PAREN)
                        {FAIL("Unmatched parentheses or misplaced comma.");}
                    POP_OPERATOR_TO_OUTPUT();
                }
                var_idx = op[op_idx].var.idx;
                if (var_idx < N_USER_VARS) {
                    if (!vars[var_idx].vec_len)
                        vars[var_idx].vec_len = out[out_idx].gen.vec_len;
                    /* update and lock vector length of assigned variable */
                    op[op_idx].gen.vec_len = vars[var_idx].vec_len;
                    op[op_idx].gen.flags |= VEC_LEN_LOCKED;
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
                assigning = 1;
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
            case TOK_OPEN_SQUARE:
                if (var_flags & TOK_OPEN_SQUARE) { /* vector index not set */
                    GET_NEXT_TOKEN(tok);
                    {FAIL_IF(tok.toktype != TOK_LITERAL || tok.gen.datatype != MPR_INT32,
                             "Non-integer vector index.");}
                    if (out[out_idx].var.idx == VAR_Y)
                        {FAIL_IF(tok.lit.val.i >= out_vec_len, "Index exceeds output vector length. (1)");}
                    else
                        {FAIL_IF(tok.lit.val.i >= in_vec_len, "Index exceeds input vector length. (1)");}
                    out[out_idx].var.vec_idx = tok.lit.val.i;
                    out[out_idx].gen.vec_len = 1;
                    out[out_idx].gen.flags |= VEC_LEN_LOCKED;
                    GET_NEXT_TOKEN(tok);
                    if (tok.toktype == TOK_COLON) {
                        /* index is range A:B */
                        GET_NEXT_TOKEN(tok);
                        {FAIL_IF(tok.toktype != TOK_LITERAL || tok.gen.datatype != MPR_INT32,
                              "Malformed vector index.");}
                        if (out[out_idx].var.idx == VAR_Y)
                            {FAIL_IF(tok.lit.val.i >= out_vec_len,
                                     "Index exceeds output vector length. (2)");}
                        else
                            {FAIL_IF(tok.lit.val.i >= in_vec_len,
                                     "Index exceeds input vector length. (2)");}
                        {FAIL_IF(tok.lit.val.i < out[out_idx].var.vec_idx, "Malformed vector index.");}
                        out[out_idx].gen.vec_len = tok.lit.val.i - out[out_idx].var.vec_idx + 1;
                        GET_NEXT_TOKEN(tok);
                    }
                    {FAIL_IF(tok.toktype != TOK_CLOSE_SQUARE, "Unmatched bracket.");}
                    /* vector index set */
                    var_flags &= ~TOK_OPEN_SQUARE;
                    allow_toktype = (TOK_OP | TOK_COMMA | TOK_CLOSE_PAREN | TOK_CLOSE_CURLY
                                     | TOK_CLOSE_SQUARE | TOK_COLON | TOK_SEMICOLON
                                     | var_flags | (assigning ? TOK_ASSIGN | TOK_ASSIGN_TT : 0));
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
            case TOK_CLOSE_SQUARE:
                /* pop from operator stack to output until TOK_VECTORIZE found */
                while (op_idx >= 0 && op[op_idx].toktype != TOK_VECTORIZE) {
                    POP_OPERATOR_TO_OUTPUT();
                }
                {FAIL_IF(op_idx < 0, "Unmatched brackets or misplaced comma.");}
                if (op[op_idx].gen.vec_len) {
                    op[op_idx].gen.flags |= VEC_LEN_LOCKED;

                    switch (out[out_idx].toktype) {
                        case TOK_BRANCH_NEXT_INST:
                            op[op_idx].gen.vec_len += out[out_idx-1].gen.vec_len;
                            ++op[op_idx].fn.arity;
                            break;
                        case TOK_LITERAL:
                            if (vectorizing && _squash_to_vector(out, out_idx)) {
                                POP_OUTPUT();
                                break;
                            }
                        default:
                            op[op_idx].gen.vec_len += out[out_idx].gen.vec_len;
                            ++op[op_idx].fn.arity;
                    }
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
                break;
            case TOK_OPEN_CURLY:
                /* push a FN_DELAY to operator stack */
                tok.toktype = TOK_FN;
                tok.fn.idx = FN_DELAY;
                tok.fn.arity = 1;
                PUSH_TO_OPERATOR(tok);

                /* also push an open parenthesis */
                tok.toktype = TOK_OPEN_PAREN;
                PUSH_TO_OPERATOR(tok);

                /* move variable from output to operator stack */
                PUSH_TO_OPERATOR(out[out_idx]);
                POP_OUTPUT();

                var_flags &= ~TOK_OPEN_CURLY;
                var_flags |= TOK_CLOSE_CURLY;
                allow_toktype = OBJECT_TOKENS;
                break;
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
                    if (!(out[out_idx].gen.flags & VAR_DELAY)) {
                        if (var == VAR_Y)
                            ++out_assigned;
                        else
                            vars[var].flags |= VAR_ASSIGNED;
                    }
                    /* nothing extraordinary, continue as normal */
                    out[out_idx].toktype = is_const ? TOK_ASSIGN_CONST : TOK_ASSIGN;
                    out[out_idx].var.offset = 0;
                    PUSH_TO_OPERATOR(out[out_idx]);
                    --out_idx;
                }
                else if (out[out_idx].toktype == TOK_TT) {
                    /* assignment to timetag */
                    /* for now we will only allow assigning to output t_y */
                    FAIL_IF(out[out_idx].var.idx != VAR_Y, "Only output timetag is writable.");
                    /* disable writing to current timetag for now */
                    FAIL_IF(!(out[out_idx].gen.flags & VAR_DELAY),
                            "Only past samples of output timetag are writable.");
                    out[out_idx].toktype = TOK_ASSIGN_TT;
                    out[out_idx].gen.datatype = MPR_DBL;
                    PUSH_TO_OPERATOR(out[out_idx]);
                    --out_idx;
                }
                else if (out[out_idx].toktype == TOK_VECTORIZE) {
                    int var, idx, vec_count = 0;;
                    /* out token is vectorizer */
                    --out_idx;
                    {FAIL_IF(out[out_idx].toktype != TOK_VAR,
                             "Illegal tokens left of assignment.");}
                    var = out[out_idx].var.idx;
                    if (var >= VAR_X)
                        {FAIL("Cannot assign to input variable 'x'.");}
                    else if (var == VAR_Y) {
                        if (!(out[out_idx].gen.flags & VAR_DELAY))
                            ++out_assigned;
                    }
                    else if (!(out[out_idx].gen.flags & VAR_DELAY))
                        vars[var].flags |= VAR_ASSIGNED;
                    while (out_idx >= 0) {
                        if (out[out_idx].toktype != TOK_VAR)
                            {FAIL("Illegal tokens left of assignment.");}
                        else if (out[out_idx].var.idx != var)
                            {FAIL("Cannot mix variables in vector assignment.");}
                        out[out_idx].toktype = is_const ? TOK_ASSIGN_CONST : TOK_ASSIGN;
                        PUSH_TO_OPERATOR(out[out_idx]);
                        --out_idx;
                    }
                    idx = op_idx;
                    while (idx >= 0) {
                        op[idx].var.offset = vec_count;
                        vec_count += op[idx].gen.vec_len;
                        --idx;
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
#if (TRACE_PARSE)
        printstack("OUTPUT STACK:  ", out, out_idx, vars, 0);
        printstack("OPERATOR STACK:", op, op_idx, vars, 0);
#endif
    }

    {FAIL_IF(allow_toktype & TOK_LITERAL || !out_assigned, "Expression has no output assignment.");}

    /* check that all used-defined variables were assigned */
    for (i = 0; i < n_vars; i++) {
        {FAIL_IF(!(vars[i].flags & VAR_ASSIGNED), "User-defined variable not assigned.");}
    }

    /* finish popping operators to output, check for unbalanced parentheses */
    while (op_idx >= 0 && op[op_idx].toktype < TOK_ASSIGN) {
        {FAIL_IF(op[op_idx].toktype == TOK_OPEN_PAREN, "Unmatched parentheses or misplaced comma.");}
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

#if (TRACE_PARSE)
    printstack("--->OUTPUT STACK:  ", out, out_idx, vars, 0);
    printstack("--->OPERATOR STACK:", op, op_idx, vars, 0);
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
    expr->tokens = malloc(sizeof(union _token) * expr->n_tokens);
    memcpy(expr->tokens, &out, sizeof(union _token) * expr->n_tokens);
    expr->start = expr->tokens;
    expr->vec_len = max_vector;
    expr->out_hist_size = -oldest_out+1;
    expr->in_hist_size = malloc(sizeof(uint8_t) * n_ins);
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
    expr->n_ins = _get_num_input_slots(expr);

    expr_stack_realloc(eval_stk, expr->stack_size * expr->vec_len);

#if TRACE_PARSE
    printf("expression allocated and initialized\n");
#endif
    return expr;
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
        printf("\b\b]");
    else
        printf("\b\b");
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

#define COPY_TYPED(MTYPE, TYPE, T)                          \
    case MTYPE:                                             \
        for (i = 0, j = sp; i < tok->gen.vec_len; i++, j++) \
            stk[j].T = ((TYPE*)a)[i + tok->var.vec_idx];    \
        break;

#define WEIGHTED_ADD(MTYPE, TYPE, T)                                                        \
    case MTYPE:                                                                             \
        for (i = 0, j = sp; i < tok->gen.vec_len; i++, j++)                                 \
            stk[j].T = stk[j].T * weight + ((TYPE*)a)[i + tok->var.vec_idx] * (1 - weight); \
        break;

#define COPY_TO_STACK(VAL)                                                  \
    if (!(tok->gen.flags & VAR_DELAY)) {                                    \
        sp += vlen;                                                         \
        ++dp;                                                               \
    }                                                                       \
    else {                                                                  \
        switch (last_type) {                                                \
            case MPR_INT32:                                                 \
                hidx = stk[sp].i;                                           \
                break;                                                      \
            case MPR_FLT:                                                   \
                hidx = (int)stk[sp].f;                                      \
                weight = fabsf(stk[sp].f - hidx);                           \
                break;                                                      \
            case MPR_DBL:                                                   \
                hidx = (int)stk[sp].d;                                      \
                weight = fabs(stk[sp].d - hidx);                            \
                break;                                                      \
            default:                                                        \
                goto error;                                                 \
        }                                                                   \
    }                                                                       \
    dims[dp] = tok->gen.vec_len;                                            \
    a = mpr_value_get_samp_hist(VAL, inst_idx % VAL->num_inst, hidx);       \
    switch (VAL->type) {                                                    \
        COPY_TYPED(MPR_INT32, int, i)                                       \
        COPY_TYPED(MPR_FLT, float, f)                                       \
        COPY_TYPED(MPR_DBL, double, d)                                      \
        default:                                                            \
            goto error;                                                     \
    }                                                                       \
    if (weight) {                                                           \
        a = mpr_value_get_samp_hist(VAL, inst_idx % VAL->num_inst, hidx-1); \
        switch (VAL->type) {                                                \
            WEIGHTED_ADD(MPR_INT32, int, i)                                 \
            WEIGHTED_ADD(MPR_FLT, float, f)                                 \
            WEIGHTED_ADD(MPR_DBL, double, d)                                \
            default:                                                        \
                goto error;                                                 \
        }                                                                   \
    }                                                                       \

MPR_INLINE static int _max(int a, int b)
{
    return a > b ? a : b;
}

int mpr_expr_eval(mpr_expr_stack expr_stk, mpr_expr expr, mpr_value *v_in, mpr_value *v_vars,
                  mpr_value v_out, mpr_time *time, mpr_type *types, int inst_idx)
{
#if TRACE_EVAL
    printf("evaluating expression...\n");
#endif
    mpr_token_t *tok, *end;
    int status = 1 | EXPR_EVAL_DONE, cache = 0, vlen;
    int i, j, sp, dp = -1;
    uint8_t alive = 1, muted = 0, can_advance = 1;
    mpr_type last_type = 0;
    mpr_value_buffer b_out;
    mpr_value x = NULL;

    mpr_expr_val stk = expr_stk->stk;
    uint8_t *dims = expr_stk->dims;

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
    b_out = v_out ? &v_out->inst[inst_idx] : 0;
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
        /* init types */
        if (types)
            memset(types, MPR_NULL, v_out->vlen);
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
            dims[dp] = tok->gen.vec_len;
                /* TODO: remove vector building? */
            switch (tok->gen.datatype) {
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
            printf("loading constant ");
            print_stack_vec(stk + sp, tok->gen.datatype, tok->gen.vec_len);
            printf("\n");
#endif
            break;
        case TOK_VAR: {
            int hidx = 0;
            float weight = 0.f;
#if TRACE_EVAL
            int mlen = 0;
            if (tok->var.idx == VAR_Y) {
                printf("loading variable y");
                mlen = v_out->mlen;
            }
            else if (tok->var.idx >= VAR_X) {
                printf("loading variable x%d", tok->var.idx - VAR_X);
                mlen = v_in ? v_in[tok->var.idx - VAR_X]->mlen : 0;
            }
            else if (expr->vars)
                printf("loading variable %s", expr->vars[tok->var.idx].name);
            else
                printf("loading variable vars[%d]", tok->var.idx);

            if (tok->gen.flags & VAR_DELAY) {
                switch (last_type) {
                    case MPR_INT32:
                        printf("{N=%d}", mlen ? stk[sp].i % mlen : stk[sp].i);
                        break;
                    case MPR_FLT:
                        printf("{N=%g}", mlen ? fmodf(stk[sp].f, (float)mlen) : stk[sp].f);
                        break;
                    case MPR_DBL:
                        printf("{N=%g}", mlen ? fmod(stk[sp].d, (double)mlen) : stk[sp].d);
                        break;
                    default:
                        goto error;
                }
            }
            printf("[%u] ", tok->var.vec_idx);
#endif
            if (tok->var.idx == VAR_Y) {
                void *a;
                if (!v_out)
                    return status;
                COPY_TO_STACK(v_out);
            }
            else if (tok->var.idx >= VAR_X) {
                mpr_value v;
                void *a;
                if (!v_in)
                    return status;
                v = v_in[tok->var.idx - VAR_X];
                COPY_TO_STACK(v);
                if (!cache)
                    status &= ~EXPR_EVAL_DONE;
            }
            else if (v_vars) {
                mpr_value v = *v_vars + tok->var.idx;
                int _inst_idx = expr->vars[tok->var.idx].flags & VAR_INSTANCED ? inst_idx : 0;
                if (!(tok->gen.flags & VAR_DELAY)) {
                    sp += vlen;
                    ++dp;
                }
                dims[dp] = tok->gen.vec_len;
                switch (v->type) {
#define TYPED_CASE(MTYPE, TYPE, T)                                          \
                    case MTYPE: {                                           \
                        TYPE *vt = v->inst[_inst_idx].samps;                \
                        for (i = 0, j = sp; i < tok->gen.vec_len; i++, j++) \
                            stk[j].T = vt[i + tok->var.vec_idx];            \
                        break;                                              \
                    }
                    TYPED_CASE(MPR_INT32, int, i)
                    TYPED_CASE(MPR_FLT, float, f)
                    TYPED_CASE(MPR_DBL, double, d)
#undef TYPED_CASE
                }
            }
            else
                goto error;
#if TRACE_EVAL
            print_stack_vec(stk + sp, tok->gen.datatype, tok->gen.vec_len);
            printf(" \n");
#endif
            break;
        }
        case TOK_VAR_NUM_INST: {
            ++dp;
            sp += vlen;
            dims[dp] = tok->gen.vec_len;
#if TRACE_EVAL
            if (tok->var.idx == VAR_Y)
                printf("loading y.count%c()", tok->gen.datatype);
            else if (tok->var.idx >= VAR_X)
                printf("loading x%d.count%c()", tok->var.idx - VAR_X, tok->gen.datatype);
            else if (v_vars)
                printf("loading vars[%d].count%c()", tok->var.idx, tok->gen.datatype);
#endif
            if (tok->var.idx == VAR_Y)
                stk[sp].i = v_out->num_active_inst;
            else if (tok->var.idx >= VAR_X) {
                if (!v_in)
                    return status;
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
            print_stack_vec(stk + sp, tok->gen.datatype, dims[dp]);
            printf(" \n");
#endif
            break;
        }
        case TOK_TT: {
            int hidx = 0;
            double weight = 0.0;
            double t_d;
            if (!(tok->gen.flags & VAR_DELAY)) {
                sp += vlen;
                ++dp;
            }
            dims[dp] = tok->gen.vec_len;
#if TRACE_EVAL
            if (tok->var.idx == VAR_Y)
                printf("loading timetag t_y");
            else if (tok->var.idx >= VAR_X)
                printf("loading timetag t_x%d", tok->var.idx - VAR_X);
            else if (v_vars)
                printf("loading timetag t_%s", expr->vars[tok->var.idx].name);

            if (tok->gen.flags & VAR_DELAY) {
                switch (last_type) {
                    case MPR_INT32: printf("{N=%d}", stk[sp].i);    break;
                    case MPR_FLT:   printf("{N=%g}", stk[sp].f);    break;
                    case MPR_DBL:   printf("{N=%g}", stk[sp].d);    break;
                    default:                                        goto error;
                }
            }
#endif
            if (tok->gen.flags & VAR_DELAY) {
                switch (last_type) {
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
            break;
        }
        case TOK_OP: {
            int maxlen, diff;
            unsigned int rdim;
            dp -= (op_tbl[tok->op.idx].arity - 1);
            sp = dp * vlen;
#if TRACE_EVAL
            if (tok->op.idx == OP_IF_THEN_ELSE || tok->op.idx == OP_IF_ELSE) {
                printf("IF ");
                print_stack_vec(stk + sp, tok->gen.datatype, dims[dp]);
                printf(" THEN ");
                if (tok->op.idx == OP_IF_ELSE) {
                    print_stack_vec(stk + sp, tok->gen.datatype, dims[dp]);
                    printf(" ELSE ");
                    print_stack_vec(stk + sp + vlen, tok->gen.datatype, dims[dp + 1]);
                }
                else {
                    print_stack_vec(stk + sp + vlen, tok->gen.datatype, dims[dp + 1]);
                    printf(" ELSE ");
                    print_stack_vec(stk + sp + 2 * vlen, tok->gen.datatype, dims[dp + 2]);
                }
            }
            else {
                print_stack_vec(stk + sp, tok->gen.datatype, dims[dp]);
                printf(" %s%c ", op_tbl[tok->op.idx].name, tok->gen.datatype);
                print_stack_vec(stk + sp + vlen, tok->gen.datatype, dims[dp + 1]);
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
            switch (tok->gen.datatype) {
                case MPR_INT32: {
                    switch (tok->op.idx) {
                        OP_CASES_META(i);
                        case OP_DIVIDE:
                            /* need to check for divide-by-zero */
                            for (i = 0, j = 0; i < tok->gen.vec_len; i++, j = (j + 1) % rdim) {
                                if (stk[sp + vlen + j].i)
                                    stk[sp + i].i /= stk[sp + vlen + j].i;
                                else {
                                    /* skip to after this assignment */
                                    while (tok < end && !((++tok)->toktype & TOK_ASSIGN)) {}
                                    while (tok < end && (++tok)->toktype & TOK_ASSIGN) {}
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
                                stk[sp + i].f = fmodf(stk[sp + i].f,
                                                      stk[sp + vlen + i % rdim].f);
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
                                stk[sp + i].d = fmod(stk[sp + i].d,
                                                     stk[sp + vlen + i % rdim].d);
                            break;
                        default: goto error;
                    }
                    break;
                }
                default:
                    goto error;
            }

#if TRACE_EVAL
            printf(" = ");
            print_stack_vec(stk + sp, tok->gen.datatype, dims[dp]);
            printf(" \n");
#endif
            break;
        }
        case TOK_FN: {
            int maxlen, diff;
            unsigned int ldim, rdim;
            dp -= (fn_tbl[tok->fn.idx].arity - 1);
            sp = dp * vlen;
#if TRACE_EVAL
            printf("%s%c(", fn_tbl[tok->fn.idx].name, tok->gen.datatype);
            for (i = 0; i < fn_tbl[tok->fn.idx].arity; i++) {
                print_stack_vec(stk + sp + vlen, tok->gen.datatype, dims[dp + i]);
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
            switch (tok->gen.datatype) {
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
#if TRACE_EVAL
            printf(" = ");
            print_stack_vec(stk + sp, tok->gen.datatype, dims[dp]);
            printf(" \n");
#endif
            break;
        }
        case TOK_VFN:
            dp -= (vfn_tbl[tok->fn.idx].arity - 1);
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
#if TRACE_EVAL
            printf("%s%c(", vfn_tbl[tok->fn.idx].name, tok->gen.datatype);
            for (i = 0; i < vfn_tbl[tok->fn.idx].arity; i++) {
                print_stack_vec(stk + sp + i * vlen, tok->gen.datatype, dims[dp + i]);
                printf(", ");
            }
            printf("\b\b)");
#endif
            switch (tok->gen.datatype) {
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
#if TRACE_EVAL
            printf(" = ");
            if (VFN_MAXMIN == tok->fn.idx || VFN_SUMNUM == tok->fn.idx) {
                printf("[");
                print_stack_vec(stk + sp, tok->gen.datatype, dims[dp]);
                printf(", ");
                print_stack_vec(stk + sp + vlen, tok->gen.datatype, dims[dp + 1]);
                printf("]\n");
            }
            else {
                print_stack_vec(stk + sp, tok->gen.datatype, dims[dp]);
                printf(" \n");
            }
#endif
            break;
        case TOK_CACHE_INIT_INST:
#if TRACE_EVAL
            printf("Caching instance idx %d on the eval stack.\n", inst_idx);
#endif
            /* cache previous instance idx */
            ++dp;
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
#if TRACE_EVAL
            printf("Starting instance loop with idx %d.\n", inst_idx);
#endif
            break;
        case TOK_SP_ADD:
#if TRACE_EVAL
            printf("Adding %d to eval stack pointer.\n", tok->lit.val.i);
#endif
            dp += tok->lit.val.i;
            sp = dp * vlen;
            break;
        case TOK_BRANCH_NEXT_INST:
            /* increment instance idx */
            if (x) {
                for (i = inst_idx + 1; i < x->num_inst; i++) {
                    if (x->inst[i].full || x->inst[i].pos >= (expr->max_in_hist_size - 1))
                        break;
                }
            }
            if (x && i < x->num_inst) {
#if TRACE_EVAL
                printf("Incrementing instance idx to %d and jumping %d\n", i, tok->lit.val.i);
#endif
                inst_idx = i;
                tok += tok->lit.val.i;
            }
            else {
                inst_idx = stk[sp - tok->fn.inst_cache_pos * vlen].i;
                if (x && inst_idx >= x->num_inst)
                    goto error;
                memcpy(stk + sp - tok->fn.inst_cache_pos * vlen,
                       stk + sp - (tok->fn.inst_cache_pos - 1) * vlen,
                       sizeof(mpr_expr_val_t) * vlen * tok->fn.inst_cache_pos);
                memcpy(dims + dp - tok->fn.inst_cache_pos, dims + dp - tok->fn.inst_cache_pos + 1,
                       sizeof(int) * tok->fn.inst_cache_pos);
                sp -= vlen;
                --dp;
                --cache;
#if TRACE_EVAL
                printf("Instance loop done; retrieved cached instance idx %d.\n", inst_idx);
#endif
            }
            break;
        case TOK_VECTORIZE:
            /* don't need to copy vector elements from first token */
            dp -= tok->fn.arity - 1;
            sp = dp * vlen;
            j = dims[dp];
            for (i = 1; i < tok->fn.arity; i++) {
                memcpy(&stk[sp + j], &stk[sp + i * vlen], dims[dp + i] * sizeof(mpr_expr_val_t));
                j += dims[dp + i];
            }
            dims[dp] = j;
#if TRACE_EVAL
            printf("built %u-element vector: ", dims[dp]);
            print_stack_vec(stk + sp, tok->gen.datatype, dims[dp]);
            printf(" \n");
#endif
            break;
        case TOK_ASSIGN:
        case TOK_ASSIGN_USE:
            can_advance = 0;
        case TOK_ASSIGN_CONST: {
            int hidx = tok->gen.flags & VAR_DELAY;
            if (hidx) {
                /* TODO: disallow assignment interpolation & verify parser does this check also */
                hidx = stk[sp - vlen].i;
                /* var{-1} is the current sample, so we allow hidx range of 0 -> -mlen inclusive */
                if (hidx > 0 || hidx < -v_out->mlen)
                    goto error;
            }
#if TRACE_EVAL
            if (VAR_Y == tok->var.idx)
                printf("assigning values to y{%d}[%u] (%s x %u)\n", hidx, tok->var.vec_idx,
                       type_name(tok->gen.datatype), tok->gen.vec_len);
            else
                printf("assigning values to %s{%d}[%u] (%s x %u)\n", expr->vars[tok->var.idx].name,
                       hidx, tok->var.vec_idx, type_name(tok->gen.datatype), tok->gen.vec_len);
#endif
            if (tok->var.idx == VAR_Y) {
                int idx;
                void *v;
                if (!alive)
                    goto assign_done;

                status |= muted ? EXPR_MUTED_UPDATE : EXPR_UPDATE;
                can_advance = 0;
                if (!v_out)
                    return status;

                idx = (b_out->pos + v_out->mlen + hidx) % v_out->mlen;
                v = (char*)b_out->samps + idx * v_out->vlen * mpr_type_get_size(v_out->type);

                switch (v_out->type) {
#define TYPED_CASE(MTYPE, TYPE, T)                                                          \
                    case MTYPE:                                                             \
                        for (i = 0, j = tok->var.offset; i < tok->gen.vec_len; i++, j++) {  \
                            if (j >= dims[dp]) j = 0;                                       \
                            ((TYPE*)v)[i + tok->var.vec_idx] = stk[sp + j].T;               \
                        }                                                                   \
                        break;
                    TYPED_CASE(MPR_INT32, int, i)
                    TYPED_CASE(MPR_FLT, float, f)
                    TYPED_CASE(MPR_DBL, double, d)
#undef TYPED_CASE
                    default:
                        goto error;
                }

                if (types) {
                    for (i = tok->var.vec_idx; i < tok->var.vec_idx + tok->gen.vec_len; i++)
                        types[i] = tok->gen.datatype;
                }
                /* Also copy time from input */
                if (time) {
                    mpr_time *tvar = &b_out->times[idx];
                    memcpy(tvar, time, sizeof(mpr_time));
                }
#if TRACE_EVAL
                mpr_value_print_hist(v_out, inst_idx);
#endif
            }
            else if (tok->var.idx >= 0 && tok->var.idx < N_USER_VARS) {
                mpr_value v;
                mpr_value_buffer b;
                if (!v_vars)
                    goto error;
                /* var{-1} is the current sample, so we allow hidx of 0 or -1 */
                if (hidx < -1)
                    goto error;
                /* passed the address of an array of mpr_value structs */
                v = *v_vars + tok->var.idx;
                b = &v->inst[inst_idx];

                switch (v->type) {
#define TYPED_CASE(MTYPE, TYPE, T)                                                          \
                    case MTYPE: {                                                           \
                        TYPE *vi = b->samps;                                                \
                        for (i = 0, j = tok->var.offset; i < tok->gen.vec_len; i++, j++) {  \
                            vi[i + tok->var.vec_idx] = stk[sp + j].T;                       \
                            if (j >= dims[dp]) j = 0;                                       \
                        }                                                                   \
                        break;                                                              \
                    }
                    TYPED_CASE(MPR_INT32, int, i)
                    TYPED_CASE(MPR_FLT, float, f)
                    TYPED_CASE(MPR_DBL, double, d)
#undef TYPED_CASE
                }

                /* Also copy time from input */
                if (time)
                    memcpy(b->times, time, sizeof(mpr_time));

#if TRACE_EVAL
                mpr_value_print_hist(v, inst_idx);
#endif

                if (tok->var.idx == expr->inst_ctl) {
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
            }
            else
                goto error;

        assign_done:
            /* If assignment was constant or history initialization, move expr
             * start token pointer so we don't evaluate this section again. */
            if (can_advance || tok->gen.flags & VAR_DELAY) {
#if TRACE_EVAL
                printf("moving expr offset to %ld\n", tok - expr->start + 1);
#endif
                expr->offset = tok - expr->start + 1;
            }
            else
                can_advance = 0;

            if (tok->gen.flags & CLEAR_STACK)
                dp = -1;
            else if (tok->gen.flags & VAR_DELAY)
                --dp;
            sp = dp * vlen;
            break;
        }
        case TOK_ASSIGN_TT: {
            int idx, hist;
            if (tok->var.idx != VAR_Y || !(tok->gen.flags & VAR_DELAY))
                goto error;
#if TRACE_EVAL
            printf("assigning timetag to t_y{%d}\n",
                   tok->gen.flags & VAR_DELAY ? stk[sp - vlen].i : 0);
#endif
            if (!v_out)
                return status;
            hist = tok->gen.flags & VAR_DELAY;
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
            else if (hist)
                --dp;
            sp = dp * vlen;
            break;
        }
        default: goto error;
        }
        if (tok->gen.casttype && tok->toktype > TOK_VLITERAL && tok->toktype < TOK_ASSIGN) {
#if TRACE_EVAL
            printf("casting sp=%d from %s (%c) to %s (%c)\n", dp, type_name(tok->gen.datatype),
                   tok->gen.datatype, type_name(tok->gen.casttype), tok->gen.casttype);
            print_stack_vec(stk + sp, tok->gen.datatype, dims[dp]);
#endif
            /* need to cast to a different type */
            switch (tok->gen.datatype) {
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
                            goto error;                             \
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
            last_type = tok->gen.casttype;
        }
        else
            last_type = tok->gen.datatype;
        ++tok;
    }

    RETURN_ARG_UNLESS(v_out, status);

    if (!types) {
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
    trace("Unexpected token in expression.");
    return 0;
}
