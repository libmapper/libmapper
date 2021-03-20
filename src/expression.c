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
#define PARSE_ERROR(ret, ...) { trace(__VA_ARGS__); return ret; }

typedef union _mpr_val {
    float f;
    double d;
    int i;
} mpr_expr_val_t, *mpr_expr_val;

#define EXTREMA_FUNC(NAME, TYPE, OP)    \
    static TYPE NAME(TYPE x, TYPE y) { return (x OP y) ? x : y; }
EXTREMA_FUNC(maxi, int, >);
EXTREMA_FUNC(mini, int, <);
EXTREMA_FUNC(maxf, float, >);
EXTREMA_FUNC(minf, float, <);
EXTREMA_FUNC(maxd, double, >);
EXTREMA_FUNC(mind, double, <);

#define UNARY_FUNC(TYPE, NAME, SUFFIX, CALC)    \
    static TYPE NAME##SUFFIX(TYPE x) { return CALC; }
#define FLOAT_OR_DOUBLE_UNARY_FUNC(NAME, CALC)  \
    UNARY_FUNC(float, NAME, f, CALC)            \
    UNARY_FUNC(double, NAME, d, CALC)
FLOAT_OR_DOUBLE_UNARY_FUNC(midiToHz, 440. * pow(2.0, (x - 69) / 12.0));
FLOAT_OR_DOUBLE_UNARY_FUNC(hzToMidi, 69. + 12. * log2(x / 440.));
FLOAT_OR_DOUBLE_UNARY_FUNC(uniform, rand() / (RAND_MAX + 1.0) * x);
UNARY_FUNC(int, sign, i, x >= 0 ? 1 : -1);
FLOAT_OR_DOUBLE_UNARY_FUNC(sign, x >= 0 ? 1.0 : -1.0);

#define TEST_VEC_TYPED(NAME, TYPE, OP, CMP, RET, EL)\
static void NAME(mpr_expr_val val, int from, int to)\
{                                                   \
    register TYPE ret = 1 - RET;                    \
    for (int i = 0; i < from; i++) {                \
        if (val[i].EL OP CMP) {                     \
            ret = RET;                              \
            break;                                  \
        }                                           \
    }                                               \
    for (int i = 0; i < to; i++)                    \
        val[i].EL = ret;                            \
}
TEST_VEC_TYPED(valli, int, ==, 0, 0, i);
TEST_VEC_TYPED(vallf, float, ==, 0.f, 0, f);
TEST_VEC_TYPED(valld, double, ==, 0., 0, d);
TEST_VEC_TYPED(vanyi, int, !=, 0, 1, i);
TEST_VEC_TYPED(vanyf, float, !=, 0.f, 1, f);
TEST_VEC_TYPED(vanyd, double, !=, 0., 1, d);

#define SUM_VFUNC(NAME, TYPE, EL)                   \
static void NAME(mpr_expr_val val, int from, int to)\
{                                                   \
    register TYPE aggregate = 0;                    \
    for (int i = 0; i < from; i++)                  \
        aggregate += val[i].EL;                     \
    for (int i = 0; i < to; i++)                    \
        val[i].EL = aggregate;                      \
}
SUM_VFUNC(vsumi, int, i);
SUM_VFUNC(vsumf, float, f);
SUM_VFUNC(vsumd, double, d);

#define MEAN_VFUNC(NAME, TYPE, EL)                  \
static void NAME(mpr_expr_val val, int from, int to)\
{                                                   \
    register TYPE mean = 0;                         \
    for (int i = 0; i < from; i++)                  \
        mean += val[i].EL;                          \
    mean /= from;                                   \
    for (int i = 0; i < to; i++)                    \
        val[i].EL = mean;                           \
}
MEAN_VFUNC(vmeanf, float, f);
MEAN_VFUNC(vmeand, double, d);

#define CENTER_VFUNC(NAME, TYPE, EL)                \
static void NAME(mpr_expr_val val, int from, int to)\
{                                                   \
    register TYPE max = val[0].EL, min = max;       \
    for (int i = 0; i < from; i++) {                \
        if (val[i].EL > max)                        \
            max = val[i].EL;                        \
        if (val[i].EL < min)                        \
            min = val[i].EL;                        \
    }                                               \
    register TYPE center = (max + min) * 0.5;       \
    for (int i = 0; i < to; i++)                    \
        val[i].EL = center;                         \
}
CENTER_VFUNC(vcenterf, float, f);
CENTER_VFUNC(vcenterd, double, d);

#define EXTREMA_VFUNC(NAME, OP, TYPE, EL)           \
static void NAME(mpr_expr_val val, int from, int to)\
{                                                   \
    register TYPE extrema = val[0].EL;              \
    for (int i = 1; i < from; i++) {                \
        if (val[i].EL OP extrema)                   \
            extrema = val[i].EL;                    \
    }                                               \
    for (int i = 0; i < to; i++)                    \
        val[i].EL = extrema;                        \
}
EXTREMA_VFUNC(vmaxi, >, int, i);
EXTREMA_VFUNC(vmini, <, int, i);
EXTREMA_VFUNC(vmaxf, >, float, f);
EXTREMA_VFUNC(vminf, <, float, f);
EXTREMA_VFUNC(vmaxd, >, double, d);
EXTREMA_VFUNC(vmind, <, double, d);

#define powd pow
#define sqrtd sqrt
#define acosd acos

#define NORM_FUNC(TYPE, EL)                             \
static inline TYPE norm##EL(mpr_expr_val val, int len)  \
{                                                       \
    register TYPE tmp = 0;                              \
    for (int i = 0; i < len; i++)                       \
        tmp += pow##EL(val[i].EL, 2);                   \
    return sqrt##EL(tmp);                               \
}
NORM_FUNC(float, f);
NORM_FUNC(double, d);

#define NORM_VFUNC(TYPE, EL)                                \
static void vnorm##EL(mpr_expr_val val, int from, int to)   \
{                                                           \
    register TYPE norm = norm##EL(val, from);               \
    for (int i = 0; i < to; i++)                            \
        val[i].EL = norm;                                   \
}
NORM_VFUNC(float, f);
NORM_VFUNC(double, d);

#define DOT_FUNC(TYPE, EL)                                  \
static TYPE dot##EL(mpr_expr_val a, mpr_expr_val b, int len)\
{                                                           \
    register TYPE dot = 0;                                  \
    for (int i = 0; i < len; i++)                           \
        dot += a[i].EL * b[i].EL;                           \
    return dot;                                             \
}
DOT_FUNC(int, i);
DOT_FUNC(float, f);
DOT_FUNC(double, d);

#define DOT_VFUNC(TYPE, EL)                                             \
static void vdot##EL(mpr_expr_val a, mpr_expr_val b, int from, int to)  \
{                                                                       \
    register TYPE dot = dot##EL(a, b, from);                            \
    for (int i = 0; i < to; i++)                                        \
        a[i].EL = dot;                                                  \
}
DOT_VFUNC(int, i);
DOT_VFUNC(float, f);
DOT_VFUNC(double, d);

// TODO: should we handle mutidimensional angles as well?
// should probably have separate function for signed and unsigned: angle vs. rotation
// TODO: quaternion functions

#define atan2d atan2
#define ANGLE_VFUNC(TYPE, T)                                                        \
static void vangle##T(mpr_expr_val a, mpr_expr_val b, int from, int to)             \
{                                                                                   \
    register TYPE theta;                                                            \
    theta = atan2##T(a[1].T, a[0].T) - atan2##T(b[1].T, b[0].T);                    \
    for (int i = 0 ; i < to; i++)                                                   \
        a[i].T = theta;                                                             \
}
ANGLE_VFUNC(float, f);
ANGLE_VFUNC(double, d);

#define MAXMIN_VFUNC(NAME, TYPE, EL)                \
static void NAME(mpr_expr_val max, mpr_expr_val min,\
                 mpr_expr_val val, int from, int to)\
{                                                   \
    for (int i = 0; i < from; i++) {                \
        if (val[i].EL > max[i].EL)                  \
            max[i].EL = val[i].EL;                  \
        if (val[i].EL < min[i].EL)                  \
            min[i].EL = val[i].EL;                  \
    }                                               \
}
MAXMIN_VFUNC(vmaxmini, int, i);
MAXMIN_VFUNC(vmaxminf, float, f);
MAXMIN_VFUNC(vmaxmind, double, d);

#define SUMNUM_VFUNC(NAME, TYPE, EL)                \
static void NAME(mpr_expr_val sum, mpr_expr_val num,\
                 mpr_expr_val val, int from, int to)\
{                                                   \
    for (int i = 0; i < from; i++) {                \
        sum[i].EL += val[i].EL;                     \
        num[i].EL += 1;                             \
    }                                               \
}
SUMNUM_VFUNC(vsumnumi, int, i);
SUMNUM_VFUNC(vsumnumf, float, f);
SUMNUM_VFUNC(vsumnumd, double, d);

#define TYPED_EMA(TYPE, SUFFIX)                             \
static TYPE ema##SUFFIX(TYPE memory, TYPE val, TYPE weight) \
    { return val * weight + memory * (1 - weight); }
TYPED_EMA(float, f);
TYPED_EMA(double, d);

#define TYPED_SCHMITT(TYPE, SUFFIX)                                     \
static TYPE schmitt##SUFFIX(TYPE memory, TYPE val, TYPE low, TYPE high) \
    { return memory ? val > low : val >= high; }
TYPED_SCHMITT(float, f);
TYPED_SCHMITT(double, d);

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
    OP_IF_THEN_ELSE,
} expr_op_t;

#define NONE        0x0
#define GET_ZERO    0x1
#define GET_ONE     0x2
#define GET_OPER    0x4
#define BAD_EXPR    0x8

static struct {
    const char *name;
    char arity;
    char precedence;
    uint16_t optimize_const_ops;
} op_tbl[] = {
/*                     left==0  | right==0     | left==1      | right==1     */
    { "!",      1, 11, GET_ONE  | GET_ONE  <<4 | GET_ZERO <<8 | GET_ZERO <<12 },
    { "*",      2, 10, GET_ZERO | GET_ZERO <<4 | GET_OPER <<8 | GET_OPER <<12 },
    { "/",      2, 10, GET_ZERO | BAD_EXPR <<4 | NONE     <<8 | GET_OPER <<12 },
    { "%",      2, 10, GET_ZERO | GET_OPER <<4 | GET_ONE  <<8 | GET_OPER <<12 },
    { "+",      2, 9,  GET_OPER | GET_OPER <<4 | NONE     <<8 | NONE     <<12 },
    { "-",      2, 9,  NONE     | GET_OPER <<4 | NONE     <<8 | NONE     <<12 },
    { "<<",     2, 8,  GET_ZERO | GET_OPER <<4 | NONE     <<8 | NONE     <<12 },
    { ">>",     2, 8,  GET_ZERO | GET_OPER <<4 | NONE     <<8 | NONE     <<12 },
    { ">",      2, 7,  NONE     | NONE     <<4 | NONE     <<8 | NONE     <<12 },
    { ">=",     2, 7,  NONE     | NONE     <<4 | NONE     <<8 | NONE     <<12 },
    { "<",      2, 7,  NONE     | NONE     <<4 | NONE     <<8 | NONE     <<12 },
    { "<=",     2, 7,  NONE     | NONE     <<4 | NONE     <<8 | NONE     <<12 },
    { "==",     2, 6,  NONE     | NONE     <<4 | NONE     <<8 | NONE     <<12 },
    { "!=",     2, 6,  NONE     | NONE     <<4 | NONE     <<8 | NONE     <<12 },
    { "&",      2, 5,  GET_ZERO | GET_ZERO <<4 | NONE     <<8 | NONE     <<12 },
    { "^",      2, 4,  GET_OPER | GET_OPER <<4 | NONE     <<8 | NONE     <<12 },
    { "|",      2, 3,  GET_OPER | GET_OPER <<4 | GET_ONE  <<8 | GET_ONE  <<12 },
    { "&&",     2, 2,  GET_ZERO | GET_ZERO <<4 | NONE     <<8 | NONE     <<12 },
    { "||",     2, 1,  GET_OPER | GET_OPER <<4 | GET_ONE  <<8 | GET_ONE  <<12 },
    // TODO: handle optimization of ternary operator
    { "IFTHEN",      2, 0, NONE | NONE     <<4 | NONE     <<8 | NONE     <<12 },
    { "IFELSE",      2, 0, NONE | NONE     <<4 | NONE     <<8 | NONE     <<12 },
    { "IFTHENELSE",  3, 0, NONE | NONE     <<4 | NONE     <<8 | NONE     <<12 },
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
    const char arity;
    const char memory;
    void *fn_int;
    void *fn_flt;
    void *fn_dbl;
} fn_tbl[] = {
    { "abs",        1,  0,  abs,    fabsf,      fabs        },
    { "acos",       1,  0,  0,      acosf,      acos        },
    { "acosh",      1,  0,  0,      acoshf,     acosh       },
    { "asin",       1,  0,  0,      asinf,      asin        },
    { "asinh",      1,  0,  0,      asinhf,     asinh       },
    { "atan",       1,  0,  0,      atanf,      atan        },
    { "atan2",      2,  0,  0,      atan2f,     atan2       },
    { "atanh",      1,  0,  0,      atanhf,     atanh       },
    { "cbrt",       1,  0,  0,      cbrtf,      cbrt        },
    { "ceil",       1,  0,  0,      ceilf,      ceil        },
    { "cos",        1,  0,  0,      cosf,       cos         },
    { "cosh",       1,  0,  0,      coshf,      cosh        },
    { "ema",        3,  1,  0,      emaf,       emad        },
    { "exp",        1,  0,  0,      expf,       exp         },
    { "exp2",       1,  0,  0,      exp2f,      exp2        },
    { "floor",      1,  0,  0,      floorf,     floor       },
    { "hypot",      2,  0,  0,      hypotf,     hypot       },
    { "hzToMidi",   1,  0,  0,      hzToMidif,  hzToMidid   },
    { "log",        1,  0,  0,      logf,       log         },
    { "log10",      1,  0,  0,      log10f,     log10       },
    { "log2",       1,  0,  0,      log2f,      log2        },
    { "logb",       1,  0,  0,      logbf,      logb        },
    { "max",        2,  0,  maxi,   maxf,       maxd        },
    { "midiToHz",   1,  0,  0,      midiToHzf,  midiToHzd   },
    { "min",        2,  0,  mini,   minf,       mind        },
    { "pow",        2,  0,  0,      powf,       pow         },
    { "round",      1,  0,  0,      roundf,     round       },
    { "schmitt",    4,  1,  0,      schmittf,   schmittd    },
    { "sign",       1,  0,  signi,  signf,      signd       },
    { "sin",        1,  0,  0,      sinf,       sin         },
    { "sinh",       1,  0,  0,      sinhf,      sinh        },
    { "sqrt",       1,  0,  0,      sqrtf,      sqrt        },
    { "tan",        1,  0,  0,      tanf,       tan         },
    { "tanh",       1,  0,  0,      tanhf,      tanh        },
    { "trunc",      1,  0,  0,      truncf,     trunc       },
    /* place functions which should never be precomputed below this point */
    { "delay",      1,  0,  (void*)1,      0,          0,          },
    { "uniform",    1,  0,  0,      uniformf,   uniformd    },
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
    // function names above this line are also found in pfn_table
    VFN_NORM,
    VFN_MAXMIN,
    VFN_SUMNUM,
    FN_ANGLE,
    FN_DOT,
    N_VFN
} expr_vfn_t;

static struct {
    const char *name;
    uint8_t arity;
    uint8_t reduce; // TODO: use bitflags
    uint8_t dot_notation;
    void *fn_int;
    void *fn_flt;
    void *fn_dbl;
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
    // function names above this line are also found in vfn_table
    PFN_COUNT,
    PFN_POOL,
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
    { "pool",     0, OP_UNKNOWN,     VFN_UNKNOWN }, // replaced during parsing
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
typedef void vfn_int_arity1(mpr_expr_val, int, int);
typedef void vfn_int_arity2(mpr_expr_val, mpr_expr_val, int, int);
typedef void vfn_int_arity3(mpr_expr_val, mpr_expr_val, mpr_expr_val, int, int);
typedef void vfn_flt_arity1(mpr_expr_val, int, int);
typedef void vfn_flt_arity2(mpr_expr_val, mpr_expr_val, int, int);
typedef void vfn_flt_arity3(mpr_expr_val, mpr_expr_val, mpr_expr_val, int, int);
typedef void vfn_dbl_arity1(mpr_expr_val, int, int);
typedef void vfn_dbl_arity2(mpr_expr_val, mpr_expr_val, int, int);
typedef void vfn_dbl_arity3(mpr_expr_val, mpr_expr_val, mpr_expr_val, int, int);

#define CONST_MINVAL    0x0001
#define CONST_MAXVAL    0x0002
#define CONST_PI        0x0003
#define CONST_E         0x0004

typedef struct _token {
    union {
        float f;
        int i;
        double d;
        expr_var_t var;
        expr_op_t op;
        expr_fn_t fn;
        expr_vfn_t vfn; // vector function
        expr_pfn_t pfn; // pooled instance function
    };
    enum {
        TOK_CONST           = 0x000001,
        TOK_NEGATE          = 0x000002,
        TOK_FN              = 0x000004,
        TOK_VFN             = 0x000008,
        TOK_VFN_DOT         = 0x000010,
        TOK_PFN             = 0x000020,
        TOK_OPEN_PAREN      = 0x000040,
        TOK_MUTED           = 0x000080,
        TOK_PUBLIC          = 0x000100,
        TOK_OPEN_SQUARE     = 0x000200,
        TOK_OPEN_CURLY      = 0x000400,
        TOK_CLOSE_PAREN     = 0x000800,
        TOK_CLOSE_SQUARE    = 0x001000,
        TOK_CLOSE_CURLY     = 0x002000,
        TOK_VAR             = 0x004000,
        TOK_OP              = 0x008000,
        TOK_COMMA           = 0x010000,
        TOK_COLON           = 0x020000,
        TOK_SEMICOLON       = 0x040000,
        TOK_VECTORIZE       = 0x080000,
        TOK_POOL            = 0x100000,
        TOK_ASSIGN          = 0x200000,
        TOK_ASSIGN_USE,
        TOK_ASSIGN_CONST,
        TOK_ASSIGN_TT,
        TOK_TT              = 0x400000,
        TOK_CACHE_INIT_INST,
        TOK_BRANCH_NEXT_INST,
        TOK_SP_ADD,
        TOK_END             = 0x800000,
    } toktype;
    union {
        mpr_type casttype;
        uint8_t offset;
        uint8_t inst_cache_pos;
    };
    mpr_type datatype;
    uint8_t vec_len;
//    union {
        uint8_t vec_idx;
        uint8_t arity;
//    };
    int8_t hist;          // TODO switch to bitflags
    char vec_len_locked;  // TODO switch to bitflags
    char muted;           // TODO switch to bitflags
    char clear_stack;     // TODO switch to bitflags
    uint8_t const_flags;  // TODO add to bitflags
} mpr_token_t, *mpr_token;

typedef struct _var {
    char *name;
    mpr_type datatype;
    mpr_type casttype;
    uint8_t vec_len;
    char vec_len_locked;
    char assigned;
    char public;
} mpr_var_t, *mpr_var;

static int strncmp_lc(const char *a, const char *b, int len)
{
    int i;
    for (i = 0; i < len; i++) {
        int diff = tolower(a[i]) - tolower(b[i]);
        RETURN_UNLESS(0 == diff, diff);
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
FN_LOOKUP(fn, FN, 0);
FN_LOOKUP(vfn, VFN, 0);
FN_LOOKUP(pfn, PFN, 1);

static void var_lookup(mpr_token_t *tok, const char *s, int len)
{
    if ('t' == *s && '_' == *(s+1)) {
        tok->toktype = TOK_TT;
        s += 2;
        len -= 2;
        tok->var = VAR_X;
    }
    else
        tok->toktype = TOK_VAR;
    if (*s == 'y' && 1 == len)
        tok->var = VAR_Y;
    else if ('x' == *s) {
        if (1 == len)
            tok->var = VAR_X;
        else {
            int i, ordinal = 1;
            for (i = 1; i < len; i++) {
                if (!isdigit(*(s+i))) {
                    ordinal = 0;
                    break;
                }
            }
            tok->var = ordinal ? VAR_X + atoi(s+1) : VAR_UNKNOWN;
        }
    }
    else
        tok->var = VAR_UNKNOWN;
}

static int const_lookup(mpr_token_t *tok, const char *s, int len)
{
    if (len == 2 && 'p' == *s && 'i' == *(s+1))
        tok->const_flags = CONST_PI;
    else if (len == 1 && *s == 'e')
        tok->const_flags = CONST_E;
    else
        return 1;
    tok->toktype = TOK_CONST;
    tok->datatype = MPR_FLT;
    return 0;
}

static int const_tok_is_zero(mpr_token_t tok)
{
    switch (tok.datatype) {
        case MPR_INT32:     return tok.i == 0;
        case MPR_FLT:       return tok.f == 0.f;
        case MPR_DBL:       return tok.d == 0.0;
    }
    return 0;
}

static int const_tok_is_one(mpr_token_t tok)
{
    switch (tok.datatype) {
        case MPR_INT32:     return tok.i == 1;
        case MPR_FLT:       return tok.f == 1.f;
        case MPR_DBL:       return tok.d == 1.0;
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
        case TOK_ASSIGN_TT: return tok.hist ? 1 : 0;
        case TOK_OP:        return op_tbl[tok.op].arity;
        case TOK_FN:        return fn_tbl[tok.fn].arity;
        case TOK_PFN:       return pfn_tbl[tok.pfn].arity;
        case TOK_VFN:       return vfn_tbl[tok.vfn].arity;
        case TOK_VECTORIZE: return tok.arity;
        default:            return 0;
    }
    return 0;
}

#define SET_TOK_OPTYPE(TYPE)    \
    tok->toktype = TOK_OP;      \
    tok->op = TYPE;

static int expr_lex(const char *str, int idx, mpr_token_t *tok)
{
    tok->datatype = MPR_INT32;
    tok->casttype = 0;
    tok->vec_len = 1;
    tok->vec_idx = 0;
    tok->vec_len_locked = 0;
    tok->clear_stack = 0;
    tok->const_flags = 0;
    int n=idx, i=idx;
    char c = str[idx];
    int integer_found = 0;

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
            tok->i = n;
            tok->toktype = TOK_CONST;
            tok->datatype = MPR_INT32;
            return idx;
        }
    }

    switch (c) {
    case '.':
        c = str[++idx];
        if (!isdigit(c) && c!='e') {
            if (integer_found) {
                tok->toktype = TOK_CONST;
                tok->f = (float)n;
                tok->datatype = MPR_FLT;
                return idx;
            }
            while (c && (isalpha(c) || c == '.'))
                c = str[++idx];
            ++i;
            if ((tok->vfn = vfn_lookup(str+i, idx-i)) != VFN_UNKNOWN)
                tok->toktype = TOK_VFN_DOT;
            else if ((tok->pfn = pfn_lookup(str+i, idx-i)) != PFN_UNKNOWN)
                tok->toktype = TOK_PFN;
            else
                break;
            return idx+2;
        }
        do {
            c = str[++idx];
        } while (c && isdigit(c));
        if (c!='e') {
            tok->f = atof(str+i);
            tok->toktype = TOK_CONST;
            tok->datatype = MPR_FLT;
            return idx;
        }
        // continue to next case 'e'
    case 'e':
        if (!integer_found) {
            while (c && (isalpha(c) || isdigit(c) || c == '_'))
                c = str[++idx];
            if ((tok->fn = fn_lookup(str+i, idx-i)) != FN_UNKNOWN)
                tok->toktype = TOK_FN;
            else if ((tok->vfn = vfn_lookup(str+i, idx-i)) != VFN_UNKNOWN)
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
        tok->toktype = TOK_CONST;
        tok->datatype = MPR_DBL;
        tok->d = atof(str+i);
        return idx;
    case '+':
        SET_TOK_OPTYPE(OP_ADD);
        return ++idx;
    case '-':
        // could be either subtraction or negation
        i = idx - 1;
        // back up one character
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
        // could be '=', '=='
        c = str[++idx];
        if (c == '=') {
            SET_TOK_OPTYPE(OP_IS_EQUAL);
            ++idx;
        }
        else
            tok->toktype = TOK_ASSIGN;
        return idx;
    case '<':
        // could be '<', '<=', '<<'
        SET_TOK_OPTYPE(OP_IS_LESS_THAN);
        c = str[++idx];
        if (c == '=') {
            tok->op = OP_IS_LESS_THAN_OR_EQUAL;
            ++idx;
        }
        else if (c == '<') {
            tok->op = OP_LEFT_BIT_SHIFT;
            ++idx;
        }
        return idx;
    case '>':
        // could be '>', '>=', '>>'
        SET_TOK_OPTYPE(OP_IS_GREATER_THAN);
        c = str[++idx];
        if (c == '=') {
            tok->op = OP_IS_GREATER_THAN_OR_EQUAL;
            ++idx;
        }
        else if (c == '>') {
            tok->op = OP_RIGHT_BIT_SHIFT;
            ++idx;
        }
        return idx;
    case '!':
        // could be '!', '!='
        // TODO: handle factorial case
        SET_TOK_OPTYPE(OP_LOGICAL_NOT);
        c = str[++idx];
        if (c == '=') {
            tok->op = OP_IS_NOT_EQUAL;
            ++idx;
        }
        return idx;
    case '&':
        // could be '&', '&&'
        SET_TOK_OPTYPE(OP_BITWISE_AND);
        c = str[++idx];
        if (c == '&') {
            tok->op = OP_LOGICAL_AND;
            ++idx;
        }
        return idx;
    case '|':
        // could be '|', '||'
        SET_TOK_OPTYPE(OP_BITWISE_OR);
        c = str[++idx];
        if (c == '|') {
            tok->op = OP_LOGICAL_OR;
            ++idx;
        }
        return idx;
    case '^':
        // bitwise XOR
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
        // conditional
        SET_TOK_OPTYPE(OP_IF);
        c = str[++idx];
        if (c == ':') {
            tok->op = OP_IF_ELSE;
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
    case '#':
        tok->toktype = TOK_PUBLIC;
        return ++idx;
    default:
        if (!isalpha(c)) {
            lex_error("unknown character '%c' in lexer\n", c);
            break;
        }
        while (c && (isalpha(c) || isdigit(c) || c == '_'))
            c = str[++idx];
        if ((tok->fn = fn_lookup(str+i, idx-i)) != FN_UNKNOWN)
            tok->toktype = TOK_FN;
        else if ((tok->vfn = vfn_lookup(str+i, idx-i)) != VFN_UNKNOWN)
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
    uint8_t vec_size;
    uint8_t *in_hist_size;
    uint8_t out_hist_size;
    uint8_t n_vars;
    int8_t inst_ctl;
    int8_t mute_ctl;
    int8_t n_ins;
};

void mpr_expr_free(mpr_expr expr)
{
    int i;
    FUNC_IF(free, expr->in_hist_size);
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
    int i, len = 64;
    char s[len];
    switch (t.toktype) {
        case TOK_CONST:
            switch (t.const_flags) {
                case CONST_MAXVAL:  snprintf(s, len, "max%c", t.datatype);      break;
                case CONST_MINVAL:  snprintf(s, len, "min%c", t.datatype);      break;
                case CONST_PI:      snprintf(s, len, "pi%c", t.datatype);       break;
                case CONST_E:       snprintf(s, len, "e%c", t.datatype);        break;
                default:
                    switch (t.datatype) {
                        case MPR_FLT:   snprintf(s, len, "%g", t.f);            break;
                        case MPR_DBL:   snprintf(s, len, "%g", t.d);            break;
                        case MPR_INT32: snprintf(s, len, "%d", t.i);            break;
                    }                                                           break;
            }
                                                                                break;
        case TOK_OP:            snprintf(s, len, "op.%s", op_tbl[t.op].name);   break;
        case TOK_OPEN_CURLY:    snprintf(s, len, "{");                          break;
        case TOK_OPEN_PAREN:    snprintf(s, len, "( arity %d", t.arity);        break;
        case TOK_OPEN_SQUARE:   snprintf(s, len, "[");                          break;
        case TOK_CLOSE_CURLY:   snprintf(s, len, "}");                          break;
        case TOK_CLOSE_PAREN:   snprintf(s, len, ")");                          break;
        case TOK_CLOSE_SQUARE:  snprintf(s, len, "]");                          break;
        case TOK_VAR:
            if (t.var == VAR_Y)
                snprintf(s, len, "var.y%s[%u]", t.hist ? "{N}" : "", t.vec_idx);
            else if (t.var >= VAR_X)
                snprintf(s, len, "var.x%d%s[%u]", t.var-VAR_X, t.hist ? "{N}" : "", t.vec_idx);
            else
                snprintf(s, len, "var.%s%s[%u]", vars ? vars[t.var].name : "?", t.hist ? "{N}" : "",
                         t.vec_idx);
            break;
        case TOK_TT:
            if (t.var == VAR_Y)
                snprintf(s, len, "tt.y%s", t.hist ? "{N}" : "");
            else if (t.var >= VAR_X)
                snprintf(s, len, "tt.x%d%s", t.var-VAR_X, t.hist ? "{N}" : "");
            else
                snprintf(s, len, "tt.%s%s", vars ? vars[t.var].name : "?", t.hist ? "{N}" : "");
            break;
        case TOK_FN:        snprintf(s, len, "fn.%s(arity %d)", fn_tbl[t.fn].name, t.arity); break;
        case TOK_COMMA:     snprintf(s, len, ",");                              break;
        case TOK_COLON:     snprintf(s, len, ":");                              break;
        case TOK_VECTORIZE: snprintf(s, len, "VECT(%d)", t.arity);              break;
        case TOK_NEGATE:    snprintf(s, len, "-");                              break;
        case TOK_VFN:
        case TOK_VFN_DOT:   snprintf(s, len, "vfn.%s(arity %d)", vfn_tbl[t.vfn].name, t.arity); break;
        case TOK_PFN:       snprintf(s, len, "pfn.%s(arity %d)", pfn_tbl[t.pfn].name, t.arity);    break;
        case TOK_POOL:      snprintf(s, len, "pool()");                         break;
        case TOK_ASSIGN:
        case TOK_ASSIGN_CONST:
        case TOK_ASSIGN_USE:
            if (t.var == VAR_Y)
                snprintf(s, len, "ASSIGN_TO:y%s[%u]->[%u]%s", t.hist ? "{N}" : "", t.offset,
                         t.vec_idx, t.toktype == TOK_ASSIGN_CONST ? " (const) " : "");
            else
                snprintf(s, len, "ASSIGN_TO:%s%s[%u]->[%u]%s", vars ? vars[t.var].name : "?",
                         t.hist ? "{N}" : "", t.offset, t.vec_idx,
                         t.toktype == TOK_ASSIGN_CONST ? " (const) " : "");
            break;
        case TOK_ASSIGN_TT:
            snprintf(s, len, "ASSIGN_TO:t_y%s->[%u]", t.hist ? "{N}" : "", t.vec_idx);
            break;
        case TOK_CACHE_INIT_INST:   snprintf(s, len, "<cache instance>");       break;
        case TOK_BRANCH_NEXT_INST:  snprintf(s, len, "<branch next instance %d, inst cache %d>",
                                             t.i, t.inst_cache_pos);            break;
        case TOK_SP_ADD:            snprintf(s, len, "<sp add %d>", t.i);       break;
        case TOK_SEMICOLON:         snprintf(s, len, "semicolon");              break;
        case TOK_END:               printf("END\n");                            return;
        default:                    printf("(unknown token)\n");                return;
    }
    printf("%s", s);
    // indent
    len = strlen(s);
    for (i = len; i < 40; i++)
        printf(" ");
    printf("%c%u", t.datatype, t.vec_len);
    if (t.toktype < TOK_ASSIGN && t.casttype)
        printf("->%c", t.casttype);
    else
        printf("   ");
    if (t.vec_len_locked)
        printf(" locked");
    if (t.clear_stack)
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
                    if (stk[i].var != VAR_Y)
                        break;
                case TOK_ASSIGN:
                case TOK_ASSIGN_USE:
                case TOK_ASSIGN_TT:
                    // look ahead for future assignments
                    for (j = i + 1; j <= sp; j++) {
                        if (stk[j].toktype < TOK_ASSIGN)
                            continue;
                        if (TOK_ASSIGN_CONST == stk[j].toktype && stk[j].var != VAR_Y)
                            break;
                        if (stk[j].hist)
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
                    if (stk[i].var >= VAR_X)
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

#endif // TRACE_PARSE

static mpr_type compare_token_datatype(mpr_token_t tok, mpr_type type)
{
    if (tok.toktype >= TOK_CACHE_INIT_INST)
        return type;
    // return the higher datatype
    if (tok.datatype == MPR_DBL || type == MPR_DBL)
        return MPR_DBL;
    else if (tok.datatype == MPR_FLT || type == MPR_FLT)
        return MPR_FLT;
    else
        return MPR_INT32;
}

static mpr_type promote_token_datatype(mpr_token_t *tok, mpr_type type)
{
    if (tok->toktype >= TOK_CACHE_INIT_INST)
        return type;

    tok->casttype = 0;

    if (tok->datatype == type)
        return type;

    if (tok->toktype >= TOK_ASSIGN) {
        if (tok->var >= VAR_Y) {
            // typecasting is not possible
            return tok->datatype;
        }
        else {
            // user-defined variable, can typecast
            tok->casttype = type;
            return type;
        }
    }

    if (tok->toktype == TOK_CONST) {
        // constants can be cast immediately
        if (tok->datatype == MPR_INT32) {
            if (type == MPR_FLT) {
                tok->f = (float)tok->i;
                tok->datatype = type;
            }
            else if (type == MPR_DBL) {
                tok->d = (double)tok->i;
                tok->datatype = type;
            }
        }
        else if (tok->datatype == MPR_FLT) {
            if (type == MPR_DBL) {
                tok->d = (double)tok->f;
                tok->datatype = type;
            }
            else if (type == MPR_INT32)
                tok->casttype = type;
        }
        else
            tok->casttype = type;
        return type;
    }
    else if (tok->toktype == TOK_VAR || tok->toktype == TOK_PFN) {
        // we need to cast at runtime
        tok->casttype = type;
        return type;
    }
    else {
        if (tok->datatype == MPR_INT32 || type == MPR_DBL) {
            tok->datatype = type;
            return type;
        }
        else {
            tok->casttype = type;
            return tok->datatype;
        }
    }
    return type;
}

static void lock_vec_len(mpr_token_t *stk, int sp)
{
    int i = sp, arity = 1;
    while ((i >= 0) && arity--) {
        stk[i].vec_len_locked = 1;
        switch (stk[i].toktype) {
            case TOK_OP:        arity += op_tbl[stk[i].op].arity;   break;
            case TOK_FN:        arity += fn_tbl[stk[i].fn].arity;   break;
            case TOK_VECTORIZE: arity += stk[i].arity;              break;
            default:                                                break;
        }
        --i;
    }
}

static int replace_special_constants(mpr_token_t *stk, int sp)
{
    while (sp >= 0) {
        if (stk[sp].toktype != TOK_CONST || !stk[sp].const_flags) {
            --sp;
            continue;
        }
        switch (stk[sp].const_flags) {
            case CONST_MAXVAL:
                switch (stk[sp].datatype) {
                    case MPR_INT32: stk[sp].i = INT_MAX;    break;
                    case MPR_FLT:   stk[sp].f = FLT_MAX;    break;
                    case MPR_DBL:   stk[sp].d = DBL_MAX;    break;
                    default:                                goto error;
                }
                break;
            case CONST_MINVAL:
                switch (stk[sp].datatype) {
                    case MPR_INT32: stk[sp].i = INT_MIN;    break;
                    case MPR_FLT:   stk[sp].f = FLT_MIN;    break;
                    case MPR_DBL:   stk[sp].d = DBL_MIN;    break;
                    default:                                goto error;
                }
                break;
            case CONST_PI:
                switch (stk[sp].datatype) {
                    case MPR_FLT:   stk[sp].f = M_PI;       break;
                    case MPR_DBL:   stk[sp].d = M_PI;       break;
                    default:                                goto error;
                }
                break;
            case CONST_E:
                switch (stk[sp].datatype) {
                    case MPR_FLT:   stk[sp].f = M_E;        break;
                    case MPR_DBL:   stk[sp].d = M_E;        break;
                    default:                                goto error;
                }
                break;
            default:
                continue;
        }
        stk[sp].const_flags = 0;
        --sp;
    }
    return 0;
error:
#if TRACE_PARSE
    printf("Illegal type found when replacing special constants.\n");
#endif
    return -1;
}


static int precompute(mpr_token_t *stk, int len, int vec_len)
{
    if (replace_special_constants(stk, len-1))
        return 0;
    struct _mpr_expr e = {0, stk, 0, 0, len, len, vec_len, 0, 0, 0, -1, -1};
    void *s = malloc(mpr_type_get_size(stk[len - 1].datatype) * vec_len);
    mpr_value_buffer_t b = {s, 0, -1};
    mpr_value_t v = {&b, vec_len, 1, stk[len - 1].datatype, 1};
    if (!(mpr_expr_eval(&e, 0, 0, &v, 0, 0, 0) & 1)) {
        free(s);
        return 0;
    }

    int i;
    switch (v.type) {
#define TYPED_CASE(MTYPE, TYPE, EL)         \
        case MTYPE:                         \
            for (i = 0; i < vec_len; i++)   \
                stk[i].EL = ((TYPE*)s)[i];  \
            break;
        TYPED_CASE(MPR_INT32, int, i)
        TYPED_CASE(MPR_FLT, float, f)
        TYPED_CASE(MPR_DBL, double, d)
#undef TYPED_CASE
        default:
            free(s);
            return 0;
        break;
    }
    for (i = 0; i < vec_len; i++) {
        stk[i].toktype = TOK_CONST;
        stk[i].const_flags = 0;
        stk[i].datatype = stk[len - 1].datatype;
    }
    free(s);
    return len - 1;
}

static int check_type_and_len(mpr_token_t *stk, int sp, mpr_var_t *vars, int enable_optimize)
{
    // TODO: enable precomputation of const-only vectors
    int i, arity, can_precompute = 1, optimize = NONE;
    mpr_type type = stk[sp].datatype;
    uint8_t vec_len = stk[sp].vec_len;
    switch (stk[sp].toktype) {
        case TOK_OP:
            if (stk[sp].op == OP_IF)
                PARSE_ERROR(-1, "Ternary operator is missing operand.\n");
            arity = op_tbl[stk[sp].op].arity;
            break;
        case TOK_FN:
            arity = fn_tbl[stk[sp].fn].arity;
            if (stk[sp].fn >= FN_DELAY)
                can_precompute = 0;
            break;
        case TOK_VFN:
            arity = vfn_tbl[stk[sp].vfn].arity;
            break;
        case TOK_VECTORIZE:
            arity = stk[sp].arity;
            can_precompute = 0;
            break;
        case TOK_ASSIGN:
        case TOK_ASSIGN_CONST:
        case TOK_ASSIGN_TT:
        case TOK_ASSIGN_USE:
            arity = stk[sp].hist ? 2 : 1;
            can_precompute = 0;
            break;
        default:
            return sp;
    }
    if (arity) {
        // find operator or function inputs
        i = sp;
        int skip = 0;
        int depth = arity;
        int operand = 0;

        // Walk down stack distance of arity, checking types and vector lengths.
        while (--i >= 0) {
            if (stk[i].toktype >= TOK_CACHE_INIT_INST) {
                can_precompute = enable_optimize = 0;
                continue;
            }

            if (stk[i].toktype == TOK_FN) {
                if (fn_tbl[stk[i].fn].arity)
                    can_precompute = 0;
            }
            else if (stk[i].toktype > TOK_CONST)
                can_precompute = 0;

            if (skip == 0) {
                if (enable_optimize && stk[i].toktype == TOK_CONST && stk[sp].toktype == TOK_OP
                    && depth <= op_tbl[stk[sp].op].arity) {
                    if (const_tok_is_zero(stk[i])) {
                        // mask and bitshift, depth == 1 or 2
                        optimize = (op_tbl[stk[sp].op].optimize_const_ops >> (depth - 1) * 4) & 0xF;
                    }
                    else if (const_tok_is_one(stk[i])) {
                        optimize = (op_tbl[stk[sp].op].optimize_const_ops >> (depth + 1) * 4) & 0xF;
                    }
                    if (optimize == GET_OPER) {
                        if (i == sp - 1) {
                            // optimize immediately without moving other operand
                            return sp - 2;
                        }
                        else {
                            // store position of non-zero operand
                            operand = sp - 1;
                        }
                    }
                }
                type = compare_token_datatype(stk[i], type);
                if (stk[i].toktype == TOK_VFN)
                    stk[i].vec_len = vec_len;
                else if (stk[i].vec_len > vec_len)
                    vec_len = stk[i].vec_len;
                --depth;
                if (depth == 0)
                    break;
            }
            else
                --skip;

            switch (stk[i].toktype) {
                case TOK_OP:         skip += op_tbl[stk[i].op].arity;   break;
                case TOK_FN:         skip += fn_tbl[stk[i].fn].arity;   break;
                case TOK_VFN:
                    skip += vfn_tbl[stk[i].vfn].arity;
                    if (VFN_MAXMIN == stk[i].vfn || VFN_SUMNUM == stk[i].vfn)
                        --skip; // these functions have 2 outputs
                    break;
                case TOK_VECTORIZE:  skip += stk[i].arity;              break;
                case TOK_ASSIGN_USE: ++skip;                            break;
                case TOK_VAR:        skip += stk[i].hist ? 1 : 0;       break;
                default:                                                break;
            }
        }

        if (depth)
            return -1;

        if (enable_optimize && !can_precompute) {
            switch (optimize) {
                case BAD_EXPR:
                    PARSE_ERROR(-1, "Operator '%s' cannot have zero operand.\n",
                                op_tbl[stk[sp].op].name);
                case GET_ZERO:
                case GET_ONE: {
                    // finish walking down compound arity
                    int _arity = 0;
                    while ((_arity += tok_arity(stk[i])) && i >= 0) {
                        --_arity;
                        --i;
                    }
                    stk[i].toktype = TOK_CONST;
                    stk[i].datatype = MPR_INT32;
                    stk[i].i = optimize == GET_ZERO ? 0 : 1;
                    stk[i].vec_len = vec_len;
                    stk[i].vec_len_locked = 0;
                    stk[i].casttype = 0;
                    return i;
                }
                case GET_OPER:
                    // copy tokens for non-zero operand
                    for (; i < operand; i++)
                        memcpy(stk+i, stk+i+1, sizeof(mpr_token_t));
                    // we may need to promote vector length, so do not return yet
                    sp = operand - 1;
                default:
                    break;
            }
        }

        // walk down stack distance of arity again, promoting types and lengths
        // this time we will also touch sub-arguments
        i = sp;
        switch (stk[sp].toktype) {
            case TOK_VECTORIZE:  skip = stk[sp].arity; depth = 0;       break;
            case TOK_ASSIGN_USE: skip = 1;             depth = 0;       break;
            case TOK_VAR: skip = stk[sp].hist ? 1 : 0; depth = 0;       break;
            default:             skip = 0;             depth = arity;   break;
        }
        type = promote_token_datatype(&stk[i], type);
        while (--i >= 0) {
            if (stk[i].toktype >= TOK_CACHE_INIT_INST)
                continue;
            // we will promote types within range of compound arity
            if ((stk[i+1].toktype != TOK_VAR && stk[i+1].toktype != TOK_TT) || !stk[i+1].hist) {
                // don't promote type of history indices
                type = promote_token_datatype(&stk[i], type);
            }

            if (skip <= 0) {
                --depth;
                // also check/promote vector length
                if (!stk[i].vec_len_locked) {
                    if (stk[i].toktype == TOK_VFN && vfn_tbl[stk[i].vfn].reduce) {
                        if (stk[i].vec_len != vec_len)
                            PARSE_ERROR(-1, "Vector length mismatch (1) %u != %u\n",
                                        stk[i].vec_len, vec_len);
                    }
                    else if (stk[i].toktype == TOK_VAR && stk[i].var < VAR_Y) {
                        uint8_t *vec_len_ptr = &vars[stk[i].var].vec_len;
                        *vec_len_ptr = vec_len;
                        stk[i].vec_len = vec_len;
                        stk[i].vec_len_locked = 1;
                    }
                    else
                        stk[i].vec_len = vec_len;
                }
                else if (stk[i].vec_len != vec_len)
                    PARSE_ERROR(-1, "Vector length mismatch (2) %u != %u\n", stk[i].vec_len, vec_len);
            }

            switch (stk[i].toktype) {
                case TOK_OP:
                    if (skip > 0)
                        skip += op_tbl[stk[i].op].arity;
                    else
                        depth += op_tbl[stk[i].op].arity;
                    break;
                case TOK_FN:
                    if (skip > 0)
                        skip += fn_tbl[stk[i].fn].arity;
                    else
                        depth += fn_tbl[stk[i].fn].arity;
                    break;
                case TOK_VFN:
                    skip += vfn_tbl[stk[i].vfn].arity + 1;
                    break;
                case TOK_VECTORIZE:
                    skip = stk[i].arity + 1;
                    break;
                case TOK_ASSIGN_USE:
                    ++skip;
                    ++depth;
                    break;
                case TOK_VAR:
                    if (stk[i].hist) {
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

        if (!stk[sp].vec_len_locked) {
            if (stk[sp].toktype != TOK_VFN)
                stk[sp].vec_len = vec_len;
        }
        else if (stk[sp].vec_len != vec_len)
            PARSE_ERROR(-1, "Vector length mismatch (3) %u != %u\n", stk[sp].vec_len, vec_len);
    }
    // if stack within bounds of arity was only constants, we're ok to compute
    if (enable_optimize && can_precompute)
        return sp - precompute(&stk[sp - arity], arity + 1, vec_len);
    else
        return sp;
}

static int check_assign_type_and_len(mpr_token_t *stk, int sp, mpr_var_t *vars)
{
    int i = sp, optimize = 1;
    uint8_t vec_len = 0;
    expr_var_t var = stk[sp].var;

    while (i >= 0 && (stk[i].toktype & TOK_ASSIGN) && (stk[i].var == var)) {
        vec_len += stk[i].vec_len;
        --i;
    }
    // skip branch instruction if any
    if (i >= 0 && TOK_BRANCH_NEXT_INST == stk[i].toktype) {
        --i;
        optimize = 0;
    }
    if (i < 0)
        PARSE_ERROR(-1, "Malformed expression (1)\n");
    if (stk[i].vec_len != vec_len)
        PARSE_ERROR(-1, "Vector length mismatch (4) %u != %u\n", stk[i].vec_len, vec_len);
    promote_token_datatype(&stk[i], stk[sp].datatype);
    if (check_type_and_len(stk, i, vars, optimize) == -1)
        return -1;
    promote_token_datatype(&stk[i], stk[sp].datatype);

    if (stk[sp].hist == 0 || i == 0)
        return 0;

    // Move assignment expression to beginning of stack
    int j = 0, expr_len = sp - i + (stk[sp].hist ? 2 : 1);
    if (stk[i].toktype == TOK_VECTORIZE)
        expr_len += stk[i].arity;

    mpr_token_t temp[expr_len];
    for (i = sp - expr_len + 1; i <= sp; i++)
        memcpy(&temp[j++], &stk[i], sizeof(mpr_token_t));

    for (i = sp - expr_len; i >= 0; i--)
        memcpy(&stk[i+expr_len], &stk[i], sizeof(mpr_token_t));

    for (i = 0; i < expr_len; i++)
        memcpy(&stk[i], &temp[i], sizeof(mpr_token_t));

    return 0;
}

static int find_var_by_name(mpr_var_t *vars, int n_vars, const char *str, int len)
{
    // check if variable name matches known variable
    int i;
    for (i = 0; i < n_vars; i++) {
        if (strlen(vars[i].name) == len && strncmp(vars[i].name, str, len)==0)
            return i;
    }
    return -1;
}

static int _get_num_input_slots(mpr_expr expr)
{
    // actually need to return highest numbered input slot
    int i, count = -1;
    mpr_token_t *tok = expr->tokens;
    for (i = 0; i < expr->n_tokens; i++) {
        if (tok[i].toktype == TOK_VAR && tok[i].var > count)
            count = tok[i].var;
    }
    return count >= VAR_X ? count - VAR_X + 1 : 0;
}

static int substack_len(mpr_token_t *stk, int sp)
{
    int idx = sp, arity = tok_arity(stk[sp]);
    while (arity > 0 && idx > 0) {
        --idx;
        --arity;
        arity += tok_arity(stk[idx]);
    }
    return sp - idx + 1;
}

static int _eval_stack_size(mpr_token_t *token_stack, int token_stack_len)
{
    int i = 0, sp = 0, eval_stack_len = 0;
    mpr_token_t *tok = token_stack;
    while (i < token_stack_len && tok->toktype != TOK_END) {
        switch (tok->toktype) {
            case TOK_CACHE_INIT_INST:
            case TOK_CONST:
            case TOK_VAR:
            case TOK_TT:                if (!tok->hist) ++sp;               break;
            case TOK_OP:                sp -= op_tbl[tok->op].arity - 1;    break;
            case TOK_FN:                sp -= fn_tbl[tok->fn].arity - 1;    break;
            case TOK_VFN:               sp -= vfn_tbl[tok->vfn].arity - 1;  break;
            case TOK_SP_ADD:            sp += tok->i;                       break;
            case TOK_BRANCH_NEXT_INST:  --sp;                       break;
            case TOK_VECTORIZE:         sp -= tok->arity - 1;       break;
            case TOK_ASSIGN:
            case TOK_ASSIGN_USE:
            case TOK_ASSIGN_CONST:
            case TOK_ASSIGN_TT:
                if (tok->hist)
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
    PARSE_ERROR(0, "%s\n", msg);                                    \
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
    tok.toktype = TOK_CONST;                                        \
    tok.datatype = MPR_INT32;                                       \
    tok.i = x;                                                      \
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
    out_idx = check_type_and_len(out, out_idx, vars, 1);            \
    {FAIL_IF(out_idx < 0, "Malformed expression (3).");}            \
    POP_OPERATOR();                                                 \
}
#define GET_NEXT_TOKEN(x)                                           \
{                                                                   \
    lex_idx = expr_lex(str, lex_idx, &x);                           \
    {FAIL_IF(!lex_idx, "Error in lexer.");}                         \
}

/*! Use Dijkstra's shunting-yard algorithm to parse expression into RPN stack. */
mpr_expr mpr_expr_new_from_str(const char *str, int n_ins, const mpr_type *in_types,
                               const int *in_vec_lens, mpr_type out_type, int out_vec_len)
{
    RETURN_UNLESS(str && n_ins && in_types && in_vec_lens, 0);
    mpr_token_t out[STACK_SIZE];
    mpr_token_t op[STACK_SIZE];
    int i, lex_idx = 0, out_idx = -1, op_idx = -1;
    int oldest_in[n_ins], oldest_out = 0, max_vector = 1;
    for (i = 0; i < n_ins; i++)
        oldest_in[i] = 0;

    int assigning = 0;
    int out_assigned = 0;
    int vectorizing = 0;
    int var_flags = 0;
    int allow_toktype = 0x2FFFFF;
    int is_const = 1;
    int in_vec_len = 0;
    int muted = 0;
    int public = 0;

    mpr_var_t vars[N_USER_VARS];
    int n_vars = 0;
    int inst_ctl = -1;
    int mute_ctl = -1;
    int assign_mask = (TOK_VAR | TOK_OPEN_SQUARE | TOK_COMMA | TOK_CLOSE_SQUARE | TOK_CLOSE_CURLY
                       | TOK_OPEN_CURLY | TOK_PUBLIC | TOK_NEGATE | TOK_CONST);
    int OBJECT_TOKENS = (TOK_VAR | TOK_CONST | TOK_FN | TOK_VFN | TOK_MUTED | TOK_PUBLIC
                         | TOK_NEGATE | TOK_OPEN_PAREN | TOK_OPEN_SQUARE | TOK_OP | TOK_TT);
    mpr_token_t tok;

    // ignoring spaces at start of expression
    while (str[lex_idx] == ' ') ++lex_idx;
    {FAIL_IF(!str[lex_idx], "No expression found.");}

    assigning = 1;
    allow_toktype = TOK_VAR | TOK_TT | TOK_OPEN_SQUARE | TOK_MUTED | TOK_PUBLIC;

    mpr_type var_type = out_type;
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
            {FAIL_IF(tok.toktype < TOK_ASSIGN && !(tok.toktype & allow_toktype & assign_mask),
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
            case TOK_PUBLIC:
                public = 1;
                allow_toktype = TOK_VAR | TOK_TT;
                break;
            case TOK_CONST:
                // push to output stack
                PUSH_TO_OUTPUT(tok);
                allow_toktype = (TOK_OP | TOK_CLOSE_PAREN | TOK_CLOSE_SQUARE | TOK_CLOSE_CURLY
                                 | TOK_COMMA | TOK_COLON | TOK_SEMICOLON);
                break;
            case TOK_VAR:
            case TOK_TT:
                if (tok.var >= VAR_X) {
                    int slot = tok.var-VAR_X;
                    {FAIL_IF(slot >= n_ins, "Input slot index > number of sources.");}
                    tok.datatype = in_types[slot];
                    tok.vec_len = (TOK_VAR == tok.toktype) ? in_vec_lens[slot] : 1;
                    in_vec_len = tok.vec_len;
                    tok.vec_len_locked = 1;
                    is_const = 0;
                }
                else if (tok.var == VAR_Y) {
                    tok.datatype = out_type;
                    tok.vec_len = (TOK_VAR == tok.toktype) ? out_vec_len : 1;
                    tok.vec_len_locked = 1;
                }
                else {
                    // get name of variable
                    int idx = lex_idx - 1;
                    char c = str[idx];
                    while (idx >= 0 && c && (isalpha(c) || isdigit(c))) {
                        if (--idx >= 0)
                            c = str[idx];
                    }

                    int len = lex_idx-idx - 1;
                    int i = find_var_by_name(vars, n_vars, str+idx+1, len);
                    if (i >= 0) {
                        tok.var = i;
                        tok.datatype = vars[i].datatype;
                        tok.vec_len = vars[i].vec_len;
                        if (tok.vec_len)
                            tok.vec_len_locked = 1;
                        if (public)
                            vars[i].public = 1;
                    }
                    else {
                        {FAIL_IF(n_vars >= N_USER_VARS, "Maximum number of variables exceeded.");}
                        // need to store new variable
                        vars[n_vars].name = malloc(lex_idx-idx);
                        snprintf(vars[n_vars].name, lex_idx-idx, "%s", str+idx+1);
                        vars[n_vars].datatype = var_type;
                        vars[n_vars].vec_len = 0;
                        vars[n_vars].assigned = 0;
                        vars[n_vars].public = public;
#if TRACE_PARSE
                        printf("Stored new variable '%s' at index %i\n", vars[n_vars].name, n_vars);
#endif
                        tok.var = n_vars;
                        tok.datatype = var_type;
                        // special case: 'alive' tracks instance lifetime
                        if (strcmp(vars[n_vars].name, "alive")==0) {
                            inst_ctl = n_vars;
                            tok.vec_len = 1;
                            tok.vec_len_locked = 1;
                            tok.datatype = MPR_INT32;
                        }
                        else if (strcmp(vars[n_vars].name, "muted")==0) {
                            mute_ctl = n_vars;
                            tok.vec_len = 1;
                            tok.vec_len_locked = 1;
                            tok.datatype = MPR_INT32;
                        }
                        else
                            tok.vec_len = 0;
                        ++n_vars;
                    }
                }
                tok.hist = 0;
                tok.vec_idx = 0;
                tok.muted = muted;
                // timetag tokens have type double
                if (tok.toktype == TOK_TT)
                    tok.datatype = MPR_DBL;
                PUSH_TO_OUTPUT(tok);
                // variables can have vector and history indices
                var_flags = TOK_OPEN_SQUARE | TOK_OPEN_CURLY;
                allow_toktype = (var_flags | (assigning ? TOK_ASSIGN | TOK_ASSIGN_TT : 0));
                if (TOK_VAR == tok.toktype)
                    allow_toktype |= TOK_VFN_DOT | TOK_PFN;
                if (tok.var != VAR_Y || out_assigned > 1)
                    allow_toktype |= (TOK_OP | TOK_CLOSE_PAREN | TOK_CLOSE_SQUARE | TOK_CLOSE_CURLY
                                      | TOK_COMMA | TOK_COLON | TOK_SEMICOLON);
                muted = 0;
                public = 0;
                break;
            case TOK_FN:
                tok.datatype = fn_tbl[tok.fn].fn_int ? MPR_INT32 : MPR_FLT;
                tok.arity = fn_tbl[tok.fn].arity;
                mpr_token_t newtok;
                if (fn_tbl[tok.fn].memory) {
                    // add assignment token
                    {FAIL_IF(n_vars >= N_USER_VARS, "Maximum number of variables exceeded.");}
                    char varname[6];
                    int varidx = n_vars;
                    do {
                        snprintf(varname, 6, "var%d", varidx++);
                    } while (find_var_by_name(vars, n_vars, varname, 6) >= 0);
                    // need to store new variable
                    vars[n_vars].name = strdup(varname);
                    vars[n_vars].datatype = var_type;
                    vars[n_vars].vec_len = 1;
                    vars[n_vars].assigned = 1;

                    newtok.toktype = TOK_ASSIGN_USE;
                    newtok.clear_stack = 0;
                    newtok.var = n_vars;
                    ++n_vars;
                    newtok.datatype = var_type;
                    newtok.vec_len = 1;
                    newtok.vec_len_locked = 0;
                    newtok.hist = 0;
                    newtok.vec_idx = 0;
                    newtok.offset = 0;
                    is_const = 0;
                    PUSH_TO_OPERATOR(newtok);
                }
                PUSH_TO_OPERATOR(tok);
                if (fn_tbl[tok.fn].arity)
                    allow_toktype = TOK_OPEN_PAREN;
                else {
                    POP_OPERATOR_TO_OUTPUT();
                    allow_toktype = (TOK_OP | TOK_CLOSE_PAREN | TOK_CLOSE_SQUARE | TOK_CLOSE_CURLY
                                     | TOK_COMMA | TOK_COLON | TOK_SEMICOLON);
                }
                if (tok.fn >= FN_DELAY)
                    is_const = 0;
                if (fn_tbl[tok.fn].memory) {
                    newtok.toktype = TOK_VAR;
                    newtok.hist = 0;
                    PUSH_TO_OUTPUT(newtok);
                }
                break;
            case TOK_PFN:
                {FAIL_IF(PFN_POOL != tok.pfn, "Instance reduce functions must start with 'pool()'.");}
                GET_NEXT_TOKEN(tok);
                {FAIL_IF(TOK_PFN != tok.toktype && TOK_VFN_DOT != tok.toktype,
                         "pool() must be followed by a reduce function.");}
                // get compound arity of last token
                int pre, sslen = substack_len(out, out_idx);
                expr_pfn_t pfn = tok.pfn;
                switch (pfn) {
                    case PFN_MEAN: case PFN_CENTER: case PFN_SIZE:  pre = 3; break;
                    default:                                        pre = 2; break;
                }

                {FAIL_IF(out_idx+pre > STACK_SIZE, "Stack size exceeded. (3)");}
                // copy substack to after prefix
                out_idx = out_idx - sslen + 1;
                memcpy(out + out_idx + pre, out + out_idx, sizeof(mpr_token_t) * sslen);
                --out_idx;

                // all instance reduce functions require this token
                tok.toktype = TOK_CACHE_INIT_INST;
                PUSH_TO_OUTPUT(tok);

                switch (pfn) {
                    case PFN_CENTER:
                    case PFN_MAX:
                    case PFN_SIZE:
                        // some reduce functions need init with the value from first iteration
                        tok.toktype = TOK_CONST;
                        tok.const_flags = CONST_MINVAL;
                        PUSH_TO_OUTPUT(tok);
                        if (PFN_MAX == pfn)
                            break;
                        tok.toktype = TOK_CONST;
                        tok.const_flags = CONST_MAXVAL;
                        PUSH_TO_OUTPUT(tok);
                        break;
                    case PFN_MIN:
                        tok.toktype = TOK_CONST;
                        tok.const_flags = CONST_MAXVAL;
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

                // skip to after substack
                if (PFN_COUNT != pfn)
                    out_idx += sslen;

                if (OP_UNKNOWN != pfn_tbl[pfn].op) {
                    tok.toktype = TOK_OP;
                    tok.op = pfn_tbl[pfn].op;
                    // don't use macro here since we don't want to optimize away initialization args
                    PUSH_TO_OUTPUT(tok);
                    out_idx = check_type_and_len(out, out_idx, vars, 0);
                    {FAIL_IF(out_idx < 0, "Malformed expression (11).");}
                }
                if (VFN_UNKNOWN != pfn_tbl[pfn].vfn) {
                    tok.toktype = TOK_VFN;
                    tok.vfn = pfn_tbl[pfn].vfn;
                    if (VFN_MAX == tok.vfn || VFN_MIN == tok.vfn) {
                        // we don't want vector reduce version here
                        tok.toktype = TOK_FN;
                        tok.fn = (VFN_MAX == tok.vfn) ? FN_MAX : FN_MIN;
                        tok.arity = fn_tbl[tok.fn].arity;
                    }
                    else
                        tok.arity = vfn_tbl[tok.vfn].arity;
                    PUSH_TO_OPERATOR(tok);
                    POP_OPERATOR_TO_OUTPUT();
                }

                if (PFN_CENTER == pfn || PFN_MEAN == pfn || PFN_SIZE == pfn) {
                    tok.toktype = TOK_SP_ADD;
                    tok.i = 1;
                    PUSH_TO_OUTPUT(tok);
                }

                // all instance reduce functions require these tokens
                tok.toktype = TOK_BRANCH_NEXT_INST;
                if (PFN_CENTER == pfn || PFN_MEAN == pfn || PFN_SIZE == pfn) {
                    tok.i = -3 - sslen;
                    tok.inst_cache_pos = 2;
                }
                else {
                    tok.i = -2 - sslen;
                    tok.inst_cache_pos = 1;
                }
                tok.arity = 0;
                PUSH_TO_OUTPUT(tok);

                if (PFN_CENTER == pfn) {
                    tok.toktype = TOK_OP;
                    tok.op = OP_ADD;
                    PUSH_TO_OPERATOR(tok);
                    POP_OPERATOR_TO_OUTPUT();
                    tok.toktype = TOK_CONST;
                    tok.const_flags = 0;
                    tok.datatype = MPR_FLT;
                    tok.f = 0.5;
                    PUSH_TO_OUTPUT(tok);
                    tok.toktype = TOK_OP;
                    tok.op = OP_MULTIPLY;
                    PUSH_TO_OPERATOR(tok);
                    POP_OPERATOR_TO_OUTPUT();
                }
                else if (PFN_MEAN == pfn) {
                    tok.toktype = TOK_OP;
                    tok.op = OP_DIVIDE;
                    PUSH_TO_OPERATOR(tok);
                    POP_OPERATOR_TO_OUTPUT();
                }
                else if (PFN_SIZE == pfn) {
                    tok.toktype = TOK_OP;
                    tok.op = OP_SUBTRACT;
                    PUSH_TO_OPERATOR(tok);
                    POP_OPERATOR_TO_OUTPUT();
                }
                allow_toktype = (TOK_OP | TOK_CLOSE_PAREN | TOK_CLOSE_SQUARE | TOK_CLOSE_CURLY
                                 | TOK_COMMA | TOK_COLON | TOK_SEMICOLON);
                break;
            case TOK_VFN:
                tok.toktype = TOK_VFN;
                tok.datatype = vfn_tbl[tok.vfn].fn_int ? MPR_INT32 : MPR_FLT;
                tok.arity = vfn_tbl[tok.vfn].arity;
                tok.vec_len = 1;
                PUSH_TO_OPERATOR(tok);
                allow_toktype = TOK_OPEN_PAREN;
                break;
            case TOK_VFN_DOT:
                tok.toktype = TOK_VFN;
                tok.datatype = vfn_tbl[tok.vfn].fn_int ? MPR_INT32 : MPR_FLT;
                tok.arity = vfn_tbl[tok.vfn].arity;
                tok.vec_len = 1;
                PUSH_TO_OPERATOR(tok);
                POP_OPERATOR_TO_OUTPUT();
                allow_toktype = (TOK_OP | TOK_CLOSE_PAREN | TOK_CLOSE_SQUARE | TOK_CLOSE_CURLY
                                 | TOK_COMMA | TOK_COLON | TOK_SEMICOLON | TOK_PFN);
                break;
            case TOK_OPEN_PAREN:
                if (TOK_FN == op[op_idx].toktype && fn_tbl[op[op_idx].fn].memory)
                    tok.arity = 2;
                else
                    tok.arity = 1;
                PUSH_TO_OPERATOR(tok);
                allow_toktype = OBJECT_TOKENS;
                break;
            case TOK_CLOSE_CURLY:
            case TOK_CLOSE_PAREN:
                // pop from operator stack to output until left parenthesis found
                while (op_idx >= 0 && op[op_idx].toktype != TOK_OPEN_PAREN)
                    POP_OPERATOR_TO_OUTPUT();
                {FAIL_IF(op_idx < 0, "Unmatched parentheses or misplaced comma.");}

                int arity = op[op_idx].arity;
                // remove left parenthesis from operator stack
                POP_OPERATOR();

                allow_toktype = (TOK_OP | TOK_COLON | TOK_SEMICOLON | TOK_COMMA | TOK_CLOSE_PAREN
                                 | TOK_CLOSE_SQUARE | TOK_CLOSE_CURLY);
                if (tok.toktype == TOK_CLOSE_CURLY)
                    allow_toktype |= TOK_OPEN_SQUARE;
                else
                    allow_toktype |= (TOK_VFN_DOT | TOK_PFN);

                if (op_idx < 0)
                    break;

                // if operator stack[sp] is tok_fn or tok_vfn, pop to output
                if (op[op_idx].toktype == TOK_FN) {
                    if (FN_DELAY == op[op_idx].fn) {
                        int buffer_size = 0;
                        switch (arity) {
                            case 2:
                                // max delay should be at the top of output stack
                                {FAIL_IF(out[out_idx].toktype != TOK_CONST, "non-constant max history.");}
                                switch (out[out_idx].datatype) {
#define TYPED_CASE(MTYPE, EL)                                               \
                                    case MTYPE:                             \
                                        buffer_size = (int)out[out_idx].EL; \
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
                                    {FAIL_IF(out[out_idx - 1].toktype != TOK_CONST,
                                             "variable history indices must include maximum value.");}
                                    switch (out[out_idx - 1].datatype) {
#define TYPED_CASE(MTYPE, EL)                                                       \
                                        case MTYPE:                                 \
                                            buffer_size = (int)out[out_idx - 1].EL; \
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
                                    // remove zero delay
                                    memcpy(&out[out_idx - 1], &out[out_idx], sizeof(mpr_token_t));
                                    POP_OUTPUT();
                                    POP_OPERATOR();
                                    break;
                                }
                                if (out[out_idx].var == VAR_Y && buffer_size < oldest_out)
                                    oldest_out = buffer_size;
                                else if (   out[out_idx].var >= VAR_X
                                         && buffer_size < oldest_in[out[out_idx].var-VAR_X]) {
                                    oldest_in[out[out_idx].var-VAR_X] = buffer_size;
                                }
                                // TODO: disable non-const assignment to past values of output
                                out[out_idx].hist = 1;
                                POP_OPERATOR();
                                break;
                            default:
                                {FAIL("Illegal arity for variable delay.");}
                        }
                    }
                    else {
                        if (arity != fn_tbl[op[op_idx].fn].arity) {
                            // check for overloaded functions
                            if (arity != 1)
                                {FAIL("FN arity mismatch.");}
                            if (op[op_idx].fn == FN_MIN) {
                                op[op_idx].toktype = TOK_VFN;
                                op[op_idx].vfn = VFN_MIN;
                            }
                            else if (op[op_idx].fn == FN_MAX) {
                                op[op_idx].toktype = TOK_VFN;
                                op[op_idx].vfn = VFN_MAX;
                            }
                            else
                                {FAIL("FN arity mismatch.");}
                        }
                        POP_OPERATOR_TO_OUTPUT();
                    }

                }
                else if (op[op_idx].toktype == TOK_VFN) {
                    // check arity
                    {FAIL_IF(arity != vfn_tbl[op[op_idx].vfn].arity, "VFN arity mismatch.");}
                    POP_OPERATOR_TO_OUTPUT();
                }
                // special case: if top of stack is tok_assign_use, pop to output
                if (op_idx >= 0 && op[op_idx].toktype == TOK_ASSIGN_USE)
                    POP_OPERATOR_TO_OUTPUT();
                break;
            case TOK_COMMA:
                // pop from operator stack to output until left parenthesis or TOK_VECTORIZE found
                while (op_idx >= 0 && op[op_idx].toktype != TOK_OPEN_PAREN
                       && op[op_idx].toktype != TOK_VECTORIZE) {
                    POP_OPERATOR_TO_OUTPUT();
                }
                {FAIL_IF(op_idx < 0, "Malformed expression (4).");}
                if (op[op_idx].toktype == TOK_VECTORIZE) {
                    ++op[op_idx].vec_idx;
                    switch (out[out_idx].toktype) {
                        case TOK_BRANCH_NEXT_INST:
                            op[op_idx].vec_len += out[out_idx-1].vec_len;
                            ++op[op_idx].arity;
                            break;
                        case TOK_CONST:
                            if (out_idx >= 1 && TOK_CONST == out[out_idx - 1].toktype
                                && out[out_idx].datatype == out[out_idx - 1].datatype
                                && out[out_idx].i == out[out_idx - 1].i) {
                                trace("Squashing vector. (1)\n");
                                ++out[out_idx - 1].vec_len;
                                ++op[op_idx].vec_len;
                                POP_OUTPUT();
                                break;
                            }
                        default:
                            op[op_idx].vec_len += out[out_idx].vec_len;
                            ++op[op_idx].arity;
                    }
                    lock_vec_len(out, out_idx);
                }
                else
                    ++op[op_idx].arity;
                allow_toktype = OBJECT_TOKENS;
                break;
            case TOK_COLON:
                // pop from operator stack to output until conditional found
                while (op_idx >= 0 && (op[op_idx].toktype != TOK_OP ||
                                       op[op_idx].op != OP_IF)) {
                    POP_OPERATOR_TO_OUTPUT();
                }
                {FAIL_IF(op_idx < 0, "Unmatched colon.");}
                op[op_idx].op = OP_IF_THEN_ELSE;
                allow_toktype = OBJECT_TOKENS;
                break;
            case TOK_SEMICOLON:
                // finish popping operators to output, check for unbalanced parentheses
                while (op_idx >= 0 && op[op_idx].toktype < TOK_ASSIGN) {
                    if (op[op_idx].toktype == TOK_OPEN_PAREN)
                        {FAIL("Unmatched parentheses or misplaced comma.");}
                    POP_OPERATOR_TO_OUTPUT();
                }
                int var_idx = op[op_idx].var;
                if (var_idx < VAR_Y) {
                    if (!vars[var_idx].vec_len)
                        vars[var_idx].vec_len = out[out_idx].vec_len;
                    // update and lock vector length of assigned variable
                    op[op_idx].vec_len = vars[var_idx].vec_len;
                    op[op_idx].vec_len_locked = 1;
                }
                // pop assignment operators to output
                while (op_idx >= 0) {
                    if (!op_idx && op[op_idx].toktype < TOK_ASSIGN)
                        {FAIL("Malformed expression (5)");}
                    PUSH_TO_OUTPUT(op[op_idx]);
                    if (out[out_idx].toktype == TOK_ASSIGN_USE
                        && check_assign_type_and_len(out, out_idx, vars) == -1)
                        {FAIL("Malformed expression (6)");}
                    POP_OPERATOR();
                }
                // mark last assignment token to clear eval stack
                out[out_idx].clear_stack = 1;

                // check vector length and type
                if (check_assign_type_and_len(out, out_idx, vars) == -1)
                    {FAIL("Malformed expression (7)");}

                // start another sub-expression
                assigning = 1;
                allow_toktype = TOK_VAR | TOK_TT | TOK_PUBLIC;
                break;
            case TOK_OP:
                // check precedence of operators on stack
                while (op_idx >= 0 && op[op_idx].toktype == TOK_OP
                       && (op_tbl[op[op_idx].op].precedence >=
                           op_tbl[tok.op].precedence)) {
                    POP_OPERATOR_TO_OUTPUT();
                }
                PUSH_TO_OPERATOR(tok);
                allow_toktype = OBJECT_TOKENS;
                break;
            case TOK_OPEN_SQUARE:
                if (var_flags & TOK_OPEN_SQUARE) { // vector index not set
                    GET_NEXT_TOKEN(tok);
                    {FAIL_IF(tok.toktype != TOK_CONST || tok.datatype != MPR_INT32,
                             "Non-integer vector index.");}
                    if (out[out_idx].var == VAR_Y) {
                        {FAIL_IF(tok.i >= out_vec_len,
                                 "Index exceeds output vector length. (1)");}
                    }
                    else
                        {FAIL_IF(tok.i >= in_vec_len, "Index exceeds input vector length. (1)");}
                    out[out_idx].vec_idx = tok.i;
                    out[out_idx].vec_len = 1;
                    out[out_idx].vec_len_locked = 1;
                    GET_NEXT_TOKEN(tok);
                    if (tok.toktype == TOK_COLON) {
                        // index is range A:B
                        GET_NEXT_TOKEN(tok);
                        {FAIL_IF(tok.toktype != TOK_CONST || tok.datatype != MPR_INT32,
                              "Malformed vector index.");}
                        if (out[out_idx].var == VAR_Y) {
                            {FAIL_IF(tok.i >= out_vec_len,
                                     "Index exceeds output vector length. (2)");}
                        }
                        else
                            {FAIL_IF(tok.i >= in_vec_len,
                                     "Index exceeds input vector length. (2)");}
                        {FAIL_IF(tok.i < out[out_idx].vec_idx,
                                 "Malformed vector index.");}
                        out[out_idx].vec_len =
                            tok.i - out[out_idx].vec_idx + 1;
                        GET_NEXT_TOKEN(tok);
                    }
                    {FAIL_IF(tok.toktype != TOK_CLOSE_SQUARE, "Unmatched bracket.");}
                    // vector index set
                    var_flags &= ~TOK_OPEN_SQUARE;
                    allow_toktype = (TOK_OP | TOK_COMMA | TOK_CLOSE_PAREN | TOK_CLOSE_CURLY
                                     | TOK_CLOSE_SQUARE | TOK_COLON | TOK_SEMICOLON
                                     | var_flags | (assigning ? TOK_ASSIGN | TOK_ASSIGN_TT : 0));
                }
                else {
                    {FAIL_IF(vectorizing, "Nested (multidimensional) vectors not allowed.");}
                    tok.toktype = TOK_VECTORIZE;
                    tok.vec_len = 0;
                    tok.arity = 0;
                    PUSH_TO_OPERATOR(tok);
                    vectorizing = 1;
                    allow_toktype = OBJECT_TOKENS & ~TOK_OPEN_SQUARE;
                }
                break;
            case TOK_CLOSE_SQUARE:
                // pop from operator stack to output until TOK_VECTORIZE found
                while (op_idx >= 0 &&
                       op[op_idx].toktype != TOK_VECTORIZE) {
                    POP_OPERATOR_TO_OUTPUT();
                }
                {FAIL_IF(op_idx < 0, "Unmatched brackets or misplaced comma.");}
                if (op[op_idx].vec_len) {
                    op[op_idx].vec_len_locked = 1;

                    switch (out[out_idx].toktype) {
                        case TOK_BRANCH_NEXT_INST:
                            op[op_idx].vec_len += out[out_idx-1].vec_len;
                            ++op[op_idx].arity;
                            break;
                        case TOK_CONST:
                            if (out_idx >= 1 && TOK_CONST == out[out_idx - 1].toktype
                                && out[out_idx].datatype == out[out_idx - 1].datatype
                                && out[out_idx].i == out[out_idx - 1].i) {
                                trace("Squashing vector. (2)\n");
                                ++out[out_idx - 1].vec_len;
                                ++op[op_idx].vec_len;
                                POP_OUTPUT();
                                break;
                            }
                        default:
                            op[op_idx].vec_len += out[out_idx].vec_len;
                            ++op[op_idx].arity;
                    }
                    lock_vec_len(out, out_idx);
                }
                if (op[op_idx].arity > 1)
                    { POP_OPERATOR_TO_OUTPUT(); }
                else {
                    // we do not need vectorizer token if vector length == 1
                    POP_OPERATOR();
                }
                vectorizing = 0;
                allow_toktype = (TOK_OP | TOK_CLOSE_PAREN | TOK_CLOSE_CURLY | TOK_COMMA
                                 | TOK_COLON | TOK_SEMICOLON | TOK_VFN_DOT);
                break;
            case TOK_OPEN_CURLY:
                // push a FN_DELAY to operator stack
                tok.toktype = TOK_FN;
                tok.fn = FN_DELAY;
                tok.arity = 1;
                PUSH_TO_OPERATOR(tok);

                // also push an open parenthesis
                tok.toktype = TOK_OPEN_PAREN;
                PUSH_TO_OPERATOR(tok);

                // move variable from output to operator stack
                PUSH_TO_OPERATOR(out[out_idx]);
                POP_OUTPUT();

                var_flags &= ~TOK_OPEN_CURLY;
                var_flags |= TOK_CLOSE_CURLY;
                allow_toktype = OBJECT_TOKENS;
                break;
            case TOK_NEGATE:
                // push '-1' to output stack, and '*' to operator stack
                tok.toktype = TOK_CONST;
                tok.datatype = MPR_INT32;
                tok.i = -1;
                PUSH_TO_OUTPUT(tok);
                tok.toktype = TOK_OP;
                tok.op = OP_MULTIPLY;
                PUSH_TO_OPERATOR(tok);
                allow_toktype = TOK_CONST | TOK_VAR | TOK_TT | TOK_FN;
                break;
            case TOK_ASSIGN:
                var_flags = 0;
                // assignment to variable
                {FAIL_IF(!assigning, "Misplaced assignment operator.");}
                {FAIL_IF(op_idx >= 0 || out_idx < 0,
                         "Malformed expression left of assignment.");}

                if (out[out_idx].toktype == TOK_VAR) {
                    int var = out[out_idx].var;
                    if (var >= VAR_X)
                        {FAIL("Cannot assign to input variable 'x'.");}
                    else if (var == VAR_Y) {
                        if (out[out_idx].hist == 0)
                            ++out_assigned;
                    }
                    else if (out[out_idx].hist == 0)
                        vars[var].assigned = 1;
                    // nothing extraordinary, continue as normal
                    out[out_idx].toktype = is_const ? TOK_ASSIGN_CONST : TOK_ASSIGN;
                    out[out_idx].offset = 0;
                    PUSH_TO_OPERATOR(out[out_idx]);
                    --out_idx;
                }
                else if (out[out_idx].toktype == TOK_TT) {
                    // assignment to timetag
                    // for now we will only allow assigning to output t_y
                    FAIL_IF(out[out_idx].var != VAR_Y, "Only output timetag is writable.");
                    // disable writing to current timetag for now
                    FAIL_IF(out[out_idx].hist == 0, "Only past samples of output timetag are writable.");
                    out[out_idx].toktype = TOK_ASSIGN_TT;
                    out[out_idx].datatype = MPR_DBL;
                    PUSH_TO_OPERATOR(out[out_idx]);
                    --out_idx;
                }
                else if (out[out_idx].toktype == TOK_VECTORIZE) {
                    // out token is vectorizer
                    --out_idx;
                    {FAIL_IF(out[out_idx].toktype != TOK_VAR,
                             "Illegal tokens left of assignment.");}
                    int var = out[out_idx].var;
                    if (var >= VAR_X)
                        {FAIL("Cannot assign to input variable 'x'.");}
                    else if (var == VAR_Y) {
                        if (out[out_idx].hist == 0)
                            ++out_assigned;
                    }
                    else if (out[out_idx].hist == 0)
                        vars[var].assigned = 1;
                    while (out_idx >= 0) {
                        if (out[out_idx].toktype != TOK_VAR)
                            {FAIL("Illegal tokens left of assignment.");}
                        else if (out[out_idx].var != var)
                            {FAIL("Cannot mix variables in vector assignment.");}
                        out[out_idx].toktype = is_const ? TOK_ASSIGN_CONST : TOK_ASSIGN;
                        PUSH_TO_OPERATOR(out[out_idx]);
                        --out_idx;
                    }
                    int idx = op_idx, vec_count = 0;
                    while (idx >= 0) {
                        op[idx].offset = vec_count;
                        vec_count += op[idx].vec_len;
                        --idx;
                    }
                }
                else
                    {FAIL("Malformed expression left of assignment.");}
                assigning = 0;
                allow_toktype = (TOK_VAR | TOK_CONST | TOK_FN | TOK_VFN | TOK_MUTED | TOK_PUBLIC
                                 | TOK_NEGATE | TOK_OPEN_PAREN | TOK_OPEN_SQUARE | TOK_OP | TOK_TT);
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

    {FAIL_IF(allow_toktype & TOK_CONST || !out_assigned, "Expression has no output assignment.");}

    // check that all used-defined variables were assigned
    for (i = 0; i < n_vars; i++) {
        {FAIL_IF(!vars[i].assigned, "User-defined variable not assigned.");}
    }

    // finish popping operators to output, check for unbalanced parentheses
    while (op_idx >= 0 && op[op_idx].toktype < TOK_ASSIGN) {
        {FAIL_IF(op[op_idx].toktype == TOK_OPEN_PAREN, "Unmatched parentheses or misplaced comma.");}
        POP_OPERATOR_TO_OUTPUT();
    }

    if (op_idx >= 0) {
        int var_idx = op[op_idx].var;
        if (var_idx < VAR_Y) {
            if (!vars[var_idx].vec_len)
                vars[var_idx].vec_len = out[out_idx].vec_len;
            // update and lock vector length of assigned variable
            op[op_idx].vec_len = vars[var_idx].vec_len;
            op[op_idx].vec_len_locked = 1;
        }
    }

    // pop assignment operator(s) to output
    while (op_idx >= 0) {
        {FAIL_IF(!op_idx && op[op_idx].toktype < TOK_ASSIGN, "Malformed expression (8).");}
        PUSH_TO_OUTPUT(op[op_idx]);
        // check vector length and type
        {FAIL_IF(out[out_idx].toktype == TOK_ASSIGN_USE
                 && check_assign_type_and_len(out, out_idx, vars) == -1,
                 "Malformed expression (9).");}
        POP_OPERATOR();
    }

    // mark last assignment token to clear eval stack
    out[out_idx].clear_stack = 1;

    // check vector length and type
    {FAIL_IF(check_assign_type_and_len(out, out_idx, vars) == -1, "Malformed expression (10).");}

    {FAIL_IF(replace_special_constants(out, out_idx), "Error replacing special constants."); }

#if (TRACE_PARSE)
    printstack("--->OUTPUT STACK:  ", out, out_idx, vars, 0);
    printstack("--->OPERATOR STACK:", op, op_idx, vars, 0);
#endif

    // Check for maximum vector length used in stack
    for (i = 0; i < out_idx; i++) {
        if (out[i].vec_len > max_vector)
            max_vector = out[i].vec_len;
    }

    mpr_expr expr = malloc(sizeof(struct _mpr_expr));
    expr->n_tokens = out_idx + 1;
    expr->stack_size = _eval_stack_size(out, out_idx);
    expr->offset = 0;
    expr->inst_ctl = inst_ctl;
    expr->mute_ctl = mute_ctl;

    // copy tokens
    expr->tokens = malloc(sizeof(struct _token) * expr->n_tokens);
    memcpy(expr->tokens, &out, sizeof(struct _token) * expr->n_tokens);
    expr->start = expr->tokens;
    expr->vec_size = max_vector;
    expr->out_hist_size = -oldest_out+1;
    expr->in_hist_size = malloc(sizeof(uint8_t) * n_ins);
    for (i = 0; i < n_ins; i++)
        expr->in_hist_size[i] = -oldest_in[i] + 1;
    if (n_vars) {
        // copy user-defined variables
        expr->vars = malloc(sizeof(mpr_var_t) * n_vars);
        memcpy(expr->vars, vars, sizeof(mpr_var_t) * n_vars);
    }
    else
        expr->vars = NULL;

    expr->n_vars = n_vars;
    // TODO: is this the same as n_ins arg passed to this function?
    expr->n_ins = _get_num_input_slots(expr);
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

int mpr_expr_get_var_is_public(mpr_expr expr, int idx)
{
    return 1;//(idx >= 0 && idx < expr->n_vars) ? expr->vars[idx].public : 0;
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
    int i, found = 0, muted = 1;
    mpr_token_t *tok = expr->tokens;
    for (i = 0; i < expr->n_tokens; i++) {
        if (tok[i].toktype == TOK_VAR && tok[i].var == idx + VAR_X) {
            found = 1;
            muted &= tok[i].muted;
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
#define TYPED_CASE(MTYPE, STR, EL)          \
        case MTYPE:                         \
            for (i = 0; i < vec_len; i++)   \
                printf(STR, stk[i].EL);     \
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

#define UNARY_OP_CASE(OP, SYM, EL)              \
    case OP:                                    \
        for (i = 0; i < tok->vec_len; i++)      \
            stk[sp][i].EL SYM stk[sp][i].EL;    \
        break;

#define BINARY_OP_CASE(OP, SYM, EL)                             \
    case OP:                                                    \
        for (i = 0; i < tok->vec_len; i++)                      \
            stk[sp][i].EL = stk[sp][i].EL SYM stk[sp + 1][i].EL;\
        break;

#define CONDITIONAL_CASES(EL)                       \
    case OP_IF_ELSE:                                \
        for (i = 0; i < tok->vec_len; i++) {        \
            if (!stk[sp][i].EL)                     \
                stk[sp][i].EL = stk[sp + 1][i].EL;  \
        }                                           \
        break;                                      \
    case OP_IF_THEN_ELSE:                           \
        for (i = 0; i < tok->vec_len; i++) {        \
            if (stk[sp][i].EL)                      \
                stk[sp][i].EL = stk[sp + 1][i].EL;  \
            else                                    \
                stk[sp][i].EL = stk[sp + 2][i].EL;  \
        }                                           \
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
    UNARY_OP_CASE(OP_LOGICAL_NOT, =!, EL)                   \
    CONDITIONAL_CASES(EL)

#define COPY_TYPED(MTYPE, TYPE, EL)                         \
    case MTYPE:                                             \
        for (i = 0; i < tok->vec_len; i++)                  \
            stk[sp][i].EL = ((TYPE*)a)[i+tok->vec_idx];     \
        break;

#define WEIGHTED_ADD(MTYPE, TYPE, EL)                                                           \
    case MTYPE:                                                                                 \
        for (i = 0; i < tok->vec_len; i++)                                                      \
            stk[sp][i].EL = stk[sp][i].EL * weight + ((TYPE*)a)[i+tok->vec_idx] * (1 - weight); \
        break;

#define COPY_TO_STACK(VAL)                                                  \
    if (!tok->hist)                                                         \
        ++sp;                                                               \
    else {                                                                  \
        switch (last_type) {                                                \
            case MPR_INT32:                                                 \
                hidx = stk[sp][0].i;                                        \
                break;                                                      \
            case MPR_FLT:                                                   \
                hidx = (int)stk[sp][0].f;                                   \
                weight = fabsf(stk[sp][0].f - hidx);                        \
                break;                                                      \
            case MPR_DBL:                                                   \
                hidx = (int)stk[sp][0].d;                                   \
                weight = fabs(stk[sp][0].d - hidx);                         \
                break;                                                      \
            default:                                                        \
                goto error;                                                 \
        }                                                                   \
    }                                                                       \
    dims[sp] = tok->vec_len;                                                \
    void *a = mpr_value_get_samp_hist(VAL, inst_idx % VAL->num_inst, hidx); \
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

int mpr_expr_eval(mpr_expr expr, mpr_value *v_in, mpr_value *v_vars,
                  mpr_value v_out, mpr_time *time, mpr_type *types, int inst_idx)
{
    if (!expr) {
#if TRACE_EVAL
        printf(" no expression to evaluate!\n");
#endif
        return 0;
    }

    mpr_token_t *tok = expr->start, *end = expr->start + expr->n_tokens;
    int status = 1 | EXPR_EVAL_DONE, alive = 1, muted = 0;   // TODO: make bitflags
    int cache = 0;
    mpr_value_buffer b_out = v_out ? &v_out->inst[inst_idx] : 0;
    if (v_out && b_out->pos >= 0) {
        tok += expr->offset;
    }

    if (v_vars) {
        if (expr->inst_ctl >= 0) {
            // recover instance state
            mpr_value v = *v_vars + expr->inst_ctl;
            int *vi = v->inst[inst_idx].samps;
            alive = (0 != vi[0]);
        }
        if (expr->mute_ctl >= 0) {
            // recover mute state
            mpr_value v = *v_vars + expr->mute_ctl;
            int *vi = v->inst[inst_idx].samps;
            muted = (0 != vi[0]);
        }
    }

    // could we use mpr_value here instead, with stack idx instead of history idx?
    // pro: vectors, commonality with I/O
    // con: timetags wasted
    // option: create version with unallocated timetags
    mpr_expr_val_t stk[expr->stack_size][expr->vec_size];
    int dims[expr->stack_size];

    int i, j, k, sp = -1, can_advance = 1;
    mpr_type last_type = 0;

    if (v_out) {
        // init types
        if (types)
            memset(types, MPR_NULL, v_out->vlen);
        /* Increment index position of output data structure. */
        b_out->pos = (b_out->pos + 1) % v_out->mlen;
    }

    // choose one input to represent active instances
    // or now we will choose the input with the highest instance count
    // TODO: consider alternatives
    mpr_value x = NULL;
    if (v_in) {
        x = v_in[0];
        for (i = 1; i < expr->n_ins; i++) {
            if (v_in[i]->num_inst > x->num_inst)
                x = v_in[i];
        }
    }

    for (i = 0; i < expr->n_vars; i++)
        expr->vars[i].assigned = 0;

    while (tok < end) {
  repeat:
        switch (tok->toktype) {
        case TOK_CONST:
            ++sp;
            dims[sp] = tok->vec_len;
            switch (tok->datatype) {
#define TYPED_CASE(MTYPE, EL)                           \
                case MTYPE:                             \
                    for (i = 0; i < tok->vec_len; i++)  \
                        stk[sp][i].EL = tok->EL;        \
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
            print_stack_vec(stk[sp], tok->datatype, tok->vec_len);
            printf("\n");
#endif
            break;
        case TOK_VAR: {
            int hidx = 0;
            float weight = 0.f;
#if TRACE_EVAL
            int mlen = 0;
            if (tok->var == VAR_Y) {
                printf("loading variable y");
                mlen = v_out->mlen;
            }
            else if (tok->var >= VAR_X) {
                printf("loading variable x%d", tok->var-VAR_X);
                mlen = v_in ? v_in[tok->var-VAR_X]->mlen : 0;
            }
            else if (expr->vars)
                printf("loading variable %s", expr->vars[tok->var].name);
            else
                printf("loading variable vars[%d]", tok->var);

            if (tok->hist) {
                switch (last_type) {
                    case MPR_INT32:
                        printf("{N=%d}", mlen ? stk[sp][0].i % mlen : stk[sp][0].i);
                        break;
                    case MPR_FLT:
                        printf("{N=%g}", mlen ? fmodf(stk[sp][0].f, (float)mlen) : stk[sp][0].f);
                        break;
                    case MPR_DBL:
                        printf("{N=%g}", mlen ? fmod(stk[sp][0].d, (double)mlen) : stk[sp][0].d);
                        break;
                    default:
                        goto error;
                }
            }
            printf("[%u] ", tok->vec_idx);
#endif
            if (tok->var == VAR_Y) {
                if (!v_out)
                    return status;
                COPY_TO_STACK(v_out);
            }
            else if (tok->var >= VAR_X) {
                if (!v_in)
                    return status;
                mpr_value v = v_in[tok->var-VAR_X];
                COPY_TO_STACK(v);
                if (!cache)
                    status &= ~EXPR_EVAL_DONE;
            }
            else if (v_vars) {
                if (!tok->hist)
                    ++sp;
                dims[sp] = tok->vec_len;
                mpr_value v = *v_vars + tok->var;
                switch (v->type) {
                    case MPR_INT32: {
                        int *vi = v->inst[inst_idx].samps;
                        for (i = 0; i < tok->vec_len; i++)
                            stk[sp][i].i = vi[i + tok->vec_idx];
                        break;
                    }
                    case MPR_FLT: {
                        float *vf = v->inst[inst_idx].samps;
                        for (i = 0; i < tok->vec_len; i++)
                            stk[sp][i].f = vf[i + tok->vec_idx];
                        break;
                    }
                    case MPR_DBL: {
                        double *vd = v->inst[inst_idx].samps;
                        for (i = 0; i < tok->vec_len; i++)
                            stk[sp][i].d = vd[i + tok->vec_idx];
                        break;
                    }
                }
            }
            else
                goto error;
#if TRACE_EVAL
            print_stack_vec(stk[sp], tok->datatype, tok->vec_len);
            printf(" \n");
#endif
            break;
        }
        case TOK_TT: {
            int hidx = 0;
            double weight = 0.0;
            double t_d;
            if (!tok->hist)
                ++sp;
            dims[sp] = tok->vec_len;
            mpr_value_buffer b;
#if TRACE_EVAL
            if (tok->var == VAR_Y)
                printf("loading timetag t_y");
            else if (tok->var >= VAR_X)
                printf("loading timetag t_x%d", tok->var-VAR_X);
            else if (v_vars)
                printf("loading timetag t_%s", expr->vars[tok->var].name);

            if (tok->hist) {
                switch (last_type) {
                    case MPR_INT32: printf("{N=%d}", stk[sp][0].i); break;
                    case MPR_FLT:   printf("{N=%g}", stk[sp][0].f); break;
                    case MPR_DBL:   printf("{N=%g}", stk[sp][0].d); break;
                    default:                                        goto error;
                }
            }
#endif
            if (tok->hist) {
                switch (last_type) {
                    case MPR_INT32:
                        hidx = stk[sp][0].i;
                        break;
                    case MPR_FLT:
                        hidx = (int)stk[sp][0].f;
                        weight = fabsf(stk[sp][0].f - hidx);
                        break;
                    case MPR_DBL:
                        hidx = (int)stk[sp][0].d;
                        weight = fabs(stk[sp][0].d - hidx);
                        break;
                    default:
                        goto error;
                }
            }
            if (tok->var == VAR_Y) {
                if (!v_out)
                    return status;
                b = b_out;
                int idx = (b->pos + v_out->mlen + hidx) % v_out->mlen;
                t_d = mpr_time_as_dbl(b->times[idx]);
                if (weight)
                    t_d = t_d * weight + ((b->pos + v_out->mlen + hidx - 1) % v_out->mlen) * (1 - weight);
            }
            else if (tok->var >= VAR_X) {
                if (!v_in)
                    return status;
                mpr_value v = v_in[tok->var-VAR_X];
                b = &v->inst[inst_idx % v->num_inst];
                int idx = (b->pos + v->mlen + hidx) % v->mlen;
                t_d = mpr_time_as_dbl(b->times[idx]);
                if (weight)
                    t_d = t_d * weight + ((b->pos + v->mlen + hidx - 1) % v->mlen) * (1 - weight);
            }
            else if (v_vars) {
                mpr_value v = *v_vars + tok->var;
                b = &v->inst[inst_idx];
                t_d = mpr_time_as_dbl(b->times[0]);
            }
            else
                goto error;
#if TRACE_EVAL
            printf(" as double %g\n", t_d);
#endif
            for (i = 0; i < tok->vec_len; i++)
                stk[sp][i].d = t_d;
            break;
        }
        case TOK_OP:
            sp -= (op_tbl[tok->op].arity - 1);
            dims[sp] = tok->vec_len;
#if TRACE_EVAL
            if (tok->op == OP_IF_THEN_ELSE || tok->op == OP_IF_ELSE) {
                printf("IF ");
                print_stack_vec(stk[sp], tok->datatype, tok->vec_len);
                printf(" THEN ");
                if (tok->op == OP_IF_ELSE) {
                    print_stack_vec(stk[sp], tok->datatype, tok->vec_len);
                    printf(" ELSE ");
                    print_stack_vec(stk[sp + 1], tok->datatype, tok->vec_len);
                }
                else {
                    print_stack_vec(stk[sp + 1], tok->datatype, tok->vec_len);
                    printf(" ELSE ");
                    print_stack_vec(stk[sp + 2], tok->datatype, tok->vec_len);
                }
            }
            else {
                print_stack_vec(stk[sp], tok->datatype, tok->vec_len);
                printf(" %s%c ", op_tbl[tok->op].name, tok->datatype);
                print_stack_vec(stk[sp + 1], tok->datatype, tok->vec_len);
            }
#endif
            switch (tok->datatype) {
                case MPR_INT32: {
                    switch (tok->op) {
                        OP_CASES_META(i);
                        case OP_DIVIDE:
                            // need to check for divide-by-zero
                            for (i = 0; i < tok->vec_len; i++) {
                                if (stk[sp + 1][i].i)
                                    stk[sp][i].i /= stk[sp + 1][i].i;
                                else {
                                    // skip to after this assignment
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
                    switch (tok->op) {
                        OP_CASES_META(f);
                        BINARY_OP_CASE(OP_DIVIDE, /, f);
                        case OP_MODULO:
                            for (i = 0; i < tok->vec_len; i++)
                                stk[sp][i].f = fmodf(stk[sp][i].f, stk[sp + 1][i].f);
                            break;
                        default: goto error;
                    }
                    break;
                }
                case MPR_DBL: {
                    switch (tok->op) {
                        OP_CASES_META(d);
                        BINARY_OP_CASE(OP_DIVIDE, /, d);
                        case OP_MODULO:
                            for (i = 0; i < tok->vec_len; i++)
                                stk[sp][i].d = fmod(stk[sp][i].d, stk[sp + 1][i].d);
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
            print_stack_vec(stk[sp], tok->datatype, tok->vec_len);
            printf(" \n");
#endif
            break;
        case TOK_FN:
            sp -= (fn_tbl[tok->fn].arity - 1);
            dims[sp] = tok->vec_len;
#if TRACE_EVAL
            printf("%s%c(", fn_tbl[tok->fn].name, tok->datatype);
            for (i = 0; i < fn_tbl[tok->fn].arity; i++) {
                print_stack_vec(stk[sp + i], tok->datatype, tok->vec_len);
                printf(", ");
            }
            printf("%s)", fn_tbl[tok->fn].arity ? "\b\b" : "");
#endif
            switch (tok->datatype) {
#define TYPED_CASE(MTYPE, FN, EL)                                               \
            case MTYPE:                                                         \
                switch (fn_tbl[tok->fn].arity) {                                \
                case 0:                                                         \
                    for (i = 0; i < tok->vec_len; i++)                          \
                        stk[sp][i].EL = ((FN##_arity0*)fn_tbl[tok->fn].FN)();   \
                    break;                                                      \
                case 1:                                                         \
                    for (i = 0; i < tok->vec_len; i++)                          \
                        stk[sp][i].EL = (((FN##_arity1*)fn_tbl[tok->fn].FN)     \
                                         (stk[sp][i].EL));                      \
                    break;                                                      \
                case 2:                                                         \
                    for (i = 0; i < tok->vec_len; i++)                          \
                        stk[sp][i].EL = (((FN##_arity2*)fn_tbl[tok->fn].FN)     \
                                         (stk[sp][i].EL, stk[sp + 1][i].EL));   \
                    break;                                                      \
                case 3:                                                         \
                    for (i = 0; i < tok->vec_len; i++)                          \
                        stk[sp][i].EL = (((FN##_arity3*)fn_tbl[tok->fn].FN)     \
                                         (stk[sp][i].EL, stk[sp + 1][i].EL,     \
                                          stk[sp + 2][i].EL));                  \
                    break;                                                      \
                case 4:                                                         \
                    for (i = 0; i < tok->vec_len; i++)                          \
                        stk[sp][i].EL = (((FN##_arity4*)fn_tbl[tok->fn].FN)     \
                                         (stk[sp][i].EL, stk[sp + 1][i].EL,     \
                                          stk[sp + 2][i].EL, stk[sp + 3][i].EL));\
                    break;                                                      \
                default: goto error;                                            \
                }                                                               \
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
            print_stack_vec(stk[sp], tok->datatype, tok->vec_len);
            printf(" \n");
#endif
            break;
        case TOK_VFN:
            sp -= (vfn_tbl[tok->vfn].arity - 1);
#if TRACE_EVAL
            printf("%s%c(", vfn_tbl[tok->vfn].name, tok->datatype);
            for (i = 0; i < vfn_tbl[tok->vfn].arity; i++) {
                print_stack_vec(stk[sp + i], tok->datatype, dims[sp + i]);
                printf(", ");
            }
            printf("\b\b)");
#endif
            switch (tok->datatype) {
#define TYPED_CASE(MTYPE, FN, EL)                                                   \
            case MTYPE:                                                             \
                switch (vfn_tbl[tok->vfn].arity) {                                  \
                    case 1:                                                         \
                        (((v##FN##_arity1*)vfn_tbl[tok->vfn].FN)                    \
                         (stk[sp], dims[sp], tok->vec_len));                        \
                        break;                                                      \
                    case 2:                                                         \
                        (((v##FN##_arity2*)vfn_tbl[tok->vfn].FN)                    \
                         (stk[sp], stk[sp + 1], dims[sp], tok->vec_len));           \
                        break;                                                      \
                    case 3:                                                             \
                        (((v##FN##_arity3*)vfn_tbl[tok->vfn].FN)                        \
                         (stk[sp], stk[sp + 1], stk[sp + 2], dims[sp], tok->vec_len));  \
                        break;                                                          \
                    default: goto error;                                                \
                }                                                                   \
                break;
            TYPED_CASE(MPR_INT32, fn_int, i)
            TYPED_CASE(MPR_FLT, fn_flt, f)
            TYPED_CASE(MPR_DBL, fn_dbl, d)
#undef TYPED_CASE
            default:
                break;
            }
            dims[sp] = tok->vec_len;
#if TRACE_EVAL
            printf(" = ");
            if (VFN_MAXMIN == tok->vfn || VFN_SUMNUM == tok->vfn) {
                printf("[");
                print_stack_vec(stk[sp], tok->datatype, tok->vec_len);
                printf(", ");
                print_stack_vec(stk[sp + 1], tok->datatype, tok->vec_len);
                printf("]\n");
            }
            else {
                print_stack_vec(stk[sp], tok->datatype, tok->vec_len);
                printf(" \n");
            }
#endif
            break;
        case TOK_CACHE_INIT_INST:
#if TRACE_EVAL
            printf("Caching instance idx %d on the eval stack.\n", inst_idx);
#endif
            // cache previous instance idx
            ++sp;
            stk[sp][0].i = inst_idx;
            ++cache;

            if (x) {
                // find first active instance idx
                for (i = 0; i < x->num_inst; i++) {
                    if (x->inst[i].pos >= 0)
                        break;
                }
                if (i >= x->num_inst)
                    goto error;
                inst_idx = i;
            }
#if TRACE_EVAL
            printf("Starting instance loop with idx %d.\n", inst_idx);
#endif
            break;
        case TOK_SP_ADD:
#if TRACE_EVAL
            printf("Adding %d to eval stack pointer.\n", tok->i);
#endif
            sp += tok->i;
            break;
        case TOK_BRANCH_NEXT_INST:
            // increment instance idx
            if (x) {
                for (i = inst_idx + 1; i < x->num_inst; i++) {
                    if (x->inst[i].pos >= 0)
                        break;
                }
            }
            if (x && i < x->num_inst) {
#if TRACE_EVAL
                printf("Incrementing instance idx to %d and jumping %d\n", i, tok->i);
#endif
                inst_idx = i;
                tok += tok->i;
            }
            else {
                inst_idx = stk[sp - tok->inst_cache_pos][0].i;
                if (x && inst_idx >= x->num_inst)
                    goto error;
                memcpy(stk[sp - tok->inst_cache_pos], stk[sp - tok->inst_cache_pos + 1],
                       sizeof(mpr_expr_val_t) * expr->vec_size * tok->inst_cache_pos);
                memcpy(dims + sp - tok->inst_cache_pos, dims + sp - tok->inst_cache_pos + 1,
                       sizeof(int) * tok->inst_cache_pos);
                --sp;
                --cache;
#if TRACE_EVAL
                printf("Instance loop done; retrieved cached instance idx %d.\n", inst_idx);
#endif
            }
            break;
        case TOK_VECTORIZE:
            // don't need to copy vector elements from first token
            sp -= tok->arity - 1;
            k = dims[sp];
            switch (tok->datatype) {
#define TYPED_CASE(MTYPE, EL)                                       \
                case MTYPE:                                         \
                    for (i = 1; i < tok->arity; i++) {              \
                        for (j = 0; j < dims[sp + i]; j++)          \
                            stk[sp][k++].EL = stk[sp + i][j].EL;    \
                    }                                               \
                    break;
                TYPED_CASE(MPR_INT32, i)
                TYPED_CASE(MPR_FLT, f)
                TYPED_CASE(MPR_DBL, d)
#undef TYPED_CASE
                default:
                    goto error;
            }
            dims[sp] = tok->vec_len;
#if TRACE_EVAL
            printf("built %u-element vector: ", tok->vec_len);
            print_stack_vec(stk[sp], tok->datatype, tok->vec_len);
            printf(" \n");
#endif
            break;
        case TOK_ASSIGN:
        case TOK_ASSIGN_USE:
            can_advance = 0;
        case TOK_ASSIGN_CONST: {
            int hidx = tok->hist;
            if (tok->hist) {
                // TODO: disallow assignment interpolation & verify parser does this check also
                hidx = stk[sp - 1][0].i;
                // var{-1} is the current sample, so we allow hidx range of 0 -> -mlen inclusive
                if (hidx > 0 || hidx < -v_out->mlen)
                    goto error;
            }
#if TRACE_EVAL
            if (VAR_Y == tok->var)
                printf("assigning values to y{%d}[%u] (%s x %u)\n", hidx, tok->vec_idx,
                       type_name(tok->datatype), tok->vec_len);
            else
                printf("assigning values to %s{%d}[%u] (%s x %u)\n", expr->vars[tok->var].name,
                       hidx, tok->vec_idx, type_name(tok->datatype), tok->vec_len);
#endif
            if (tok->var == VAR_Y) {
                if (!alive)
                    break;
                status |= muted ? EXPR_MUTED_UPDATE : EXPR_UPDATE;
                can_advance = 0;
                if (!v_out)
                    return status;

                int idx = (b_out->pos + v_out->mlen + hidx) % v_out->mlen;
                void *v = b_out->samps + idx * v_out->vlen * mpr_type_get_size(v_out->type);

                switch (v_out->type) {
#define TYPED_CASE(MTYPE, TYPE, EL)                                                     \
                    case MTYPE:                                                         \
                        for (i = 0; i < tok->vec_len; i++)                              \
                            ((TYPE*)v)[i + tok->vec_idx] = stk[sp][i + tok->offset].EL; \
                        break;
                    TYPED_CASE(MPR_INT32, int, i)
                    TYPED_CASE(MPR_FLT, float, f)
                    TYPED_CASE(MPR_DBL, double, d)
#undef TYPED_CASE
                    default:
                        goto error;
                }

#if TRACE_EVAL
                v_out->inst[inst_idx].full = 1;
                mpr_value_print_hist(v_out, inst_idx);
#endif

                if (types) {
                    for (i = tok->vec_idx; i < tok->vec_idx + tok->vec_len; i++)
                        types[i] = tok->datatype;
                }
                // Also copy time from input
                if (time) {
                    mpr_time *tvar = &b_out->times[idx];
                    memcpy(tvar, time, sizeof(mpr_time));
                }
            }
            else if (tok->var >= 0 && tok->var < N_USER_VARS) {
                if (!v_vars)
                    goto error;
                // var{-1} is the current sample, so we allow hidx of 0 or -1
                if (hidx < -1)
                    goto error;
                // passed the address of an array of mpr_value structs
                mpr_value v = *v_vars + tok->var;
                mpr_value_buffer b = &v->inst[inst_idx];

                switch (v->type) {
                    case MPR_INT32: {
                        int *vi = b->samps;
                        for (i = 0; i < tok->vec_len; i++)
                            vi[i + tok->vec_idx] = stk[sp][i + tok->offset].i;
                        break;
                    }
                    case MPR_FLT: {
                        float *vf = b->samps;
                        for (i = 0; i < tok->vec_len; i++)
                            vf[i + tok->vec_idx] = stk[sp][i + tok->offset].f;
                        break;
                    }
                    case MPR_DBL: {
                        double *vd = b->samps;
                        for (i = 0; i < tok->vec_len; i++)
                            vd[i + tok->vec_idx] = stk[sp][i + tok->offset].d;
                        break;
                    }
                }

                // Also copy time from input
                if (time)
                    memcpy(b->times, time, sizeof(mpr_time));

                expr->vars[tok->var].assigned = 1;

                if (tok->var == expr->inst_ctl) {
                    if (alive && stk[sp][0].i == 0) {
                        if (status & EXPR_UPDATE)
                            status |= EXPR_RELEASE_AFTER_UPDATE;
                        else
                            status |= EXPR_RELEASE_BEFORE_UPDATE;
                    }
                    alive = stk[sp][0].i != 0;
                    break;
                }
                else if (tok->var == expr->mute_ctl) {
                    muted = stk[sp][0].i != 0;
                    break;
                }
            }
            else
                goto error;

            /* If assignment was constant or history initialization, move expr
             * start token pointer so we don't evaluate this section again. */
            if (tok->hist || can_advance) {
#if TRACE_EVAL
                printf("moving expr offset to %ld\n", tok - expr->start + 1);
#endif
                expr->offset = tok - expr->start + 1;
            }
            else
                can_advance = 0;
            if (tok->clear_stack)
                sp = -1;
            else if (tok->hist)
                --sp;

            break;
        }
        case TOK_ASSIGN_TT:
            if (tok->var != VAR_Y || tok->hist == 0)
                goto error;
#if TRACE_EVAL
            printf("assigning timetag to t_y{%d}\n", tok->hist ? stk[sp - 1][0].i : 0);
#endif
            if (!v_out)
                return status;
            int idx = (b_out->pos + v_out->mlen + (tok->hist ? stk[sp - 1][0].i : 0)) % v_out->mlen;
            if (idx < 0)
                idx = v_out->mlen + idx;
            mpr_time_set_dbl(&b_out->times[idx], stk[sp][0].d);
            /* If assignment was constant or history initialization, move expr
             * start token pointer so we don't evaluate this section again. */
            if (tok->hist || can_advance) {
#if TRACE_EVAL
                printf("moving expr offset to %ld\n", tok - expr->start + 1);
#endif
                expr->offset = tok - expr->start + 1;
            }
            else
                can_advance = 0;
            if (tok->clear_stack)
                sp = -1;
            else if (tok->hist)
                --sp;
            break;
        default: goto error;
        }
        if (tok->casttype && tok->toktype < TOK_ASSIGN) {
#if TRACE_EVAL
            printf("casting sp=%d from %s (%c) to %s (%c)\n", sp, type_name(tok->datatype),
                   tok->datatype, type_name(tok->casttype), tok->casttype);
            print_stack_vec(stk[sp], tok->datatype, tok->vec_len);
#endif
            // need to cast to a different type
            switch (tok->datatype) {
#define TYPED_CASE(MTYPE, EL, MTYPE1, TYPE1, EL1, MTYPE2, TYPE2, EL2)       \
                case MTYPE:                                                 \
                    switch (tok->casttype) {                                \
                        case MTYPE1:                                        \
                            for (i = 0; i < tok->vec_len; i++)              \
                                stk[sp][i].EL1 = (TYPE1)stk[sp][i].EL;      \
                            break;                                          \
                        case MTYPE2:                                        \
                            for (i = 0; i < tok->vec_len; i++)              \
                                stk[sp][i].EL2 = (TYPE2)stk[sp][i].EL;      \
                            break;                                          \
                        default:                                            \
                            goto error;                                     \
                    }                                                       \
                    break;
                TYPED_CASE(MPR_INT32, i, MPR_FLT, float, f, MPR_DBL, double, d)
                TYPED_CASE(MPR_FLT, f, MPR_INT32, int, i, MPR_DBL, double, d)
                TYPED_CASE(MPR_DBL, d, MPR_INT32, int, i, MPR_FLT, float, f)
#undef TYPED_CASE
            }
#if TRACE_EVAL
            printf(" -> ");
            print_stack_vec(stk[sp], tok->casttype, tok->vec_len);
            printf("\n");
#endif
            last_type = tok->casttype;
        }
        else
            last_type = tok->datatype;
        ++tok;
    }

    RETURN_UNLESS(v_out, status);

    if (!types) {
        /* Internal evaluation during parsing doesn't contain assignment token,
         * so we need to copy to output here. */

        /* Increment index position of output data structure. */
        b_out->pos = (b_out->pos + 1) % v_out->mlen;
        void *v = mpr_value_get_samp(v_out, inst_idx);
        switch (v_out->type) {
#define TYPED_CASE(MTYPE, TYPE, EL)                 \
            case MTYPE:                             \
                for (i = 0; i < v_out->vlen; i++)   \
                    ((TYPE*)v)[i] = stk[sp][i].EL;  \
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
