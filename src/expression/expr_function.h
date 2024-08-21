#ifndef __MPR_EXPR_FUNCTION_H__
#define __MPR_EXPR_FUNCTION_H__

#include <ctype.h>
#include <math.h>
#include "expr_operator.h"
#include "expr_value.h"

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
UNARY_FUNC(float, hzToMidi, f, 69.f + 12.f * log2f(x / 440.f))
UNARY_FUNC(double, hzToMidi, d, 69. + 12. * log2(x / 440.))
UNARY_FUNC(float, midiToHz, f, 440.f * powf(2.f, (x - 69.f) / 12.f))
UNARY_FUNC(double, midiToHz, d, 440. * pow(2., (x - 69.) / 12.))
UNARY_FUNC(float, uniform, f, (float)rand() / (RAND_MAX + 1.f) * x)
UNARY_FUNC(double, uniform, d, (double)rand() / (RAND_MAX + 1.) * x)
UNARY_FUNC(int, sign, i, x >= 0 ? 1 : -1)
UNARY_FUNC(float, sign, f, x >= 0.f ? 1.f : -1.f)
UNARY_FUNC(double, sign, d, x >= 0. ? 1. : -1.)

#define COMP_VFUNC(NAME, TYPE, OP, CMP, RET, T)     \
static void NAME(evalue val, uint8_t *dim, int inc) \
{                                                   \
    register TYPE ret = 1 - RET;                    \
    int i, len = dim[0];                            \
    for (i = 0; i < len; i++) {                     \
        if (val[i].T OP CMP) {                      \
            ret = RET;                              \
            break;                                  \
        }                                           \
    }                                               \
    val[0].T = ret;                                 \
}
COMP_VFUNC(valli, int, ==, 0, 0, i)
COMP_VFUNC(vallf, float, ==, 0.f, 0, f)
COMP_VFUNC(valld, double, ==, 0., 0, d)
COMP_VFUNC(vanyi, int, !=, 0, 1, i)
COMP_VFUNC(vanyf, float, !=, 0.f, 1, f)
COMP_VFUNC(vanyd, double, !=, 0., 1, d)

#define LEN_VFUNC(NAME, TYPE, T)                    \
static void NAME(evalue val, uint8_t *dim, int inc) \
{                                                   \
    val[0].T = dim[0];                              \
}
LEN_VFUNC(vleni, int, i)
LEN_VFUNC(vlenf, float, f)
LEN_VFUNC(vlend, double, d)

#define SUM_VFUNC(NAME, TYPE, T)                    \
static void NAME(evalue val, uint8_t *dim, int inc) \
{                                                   \
    register TYPE aggregate = 0;                    \
    int i, len = dim[0];                            \
    for (i = 0; i < len; i++)                       \
        aggregate += val[i].T;                      \
    val[0].T = aggregate;                           \
}
SUM_VFUNC(vsumi, int, i)
SUM_VFUNC(vsumf, float, f)
SUM_VFUNC(vsumd, double, d)

#define MEAN_VFUNC(NAME, TYPE, T)                   \
static void NAME(evalue val, uint8_t *dim, int inc) \
{                                                   \
    register TYPE mean = 0;                         \
    int i, len = dim[0];                            \
    for (i = 0; i < len; i++)                       \
        mean += val[i].T;                           \
    val[0].T = mean / len;                          \
}
MEAN_VFUNC(vmeanf, float, f)
MEAN_VFUNC(vmeand, double, d)

#define CENTER_VFUNC(NAME, TYPE, T)                 \
static void NAME(evalue val, uint8_t *dim, int inc) \
{                                                   \
    register TYPE max = val[0].T, min = max;        \
    int i, len = dim[0];                            \
    for (i = 0; i < len; i++) {                     \
        if (val[i].T > max)                         \
            max = val[i].T;                         \
        if (val[i].T < min)                         \
            min = val[i].T;                         \
    }                                               \
    val[0].T = (max + min) * 0.5;                   \
}
CENTER_VFUNC(vcenterf, float, f)
CENTER_VFUNC(vcenterd, double, d)

#define EXTREMA_VFUNC(NAME, OP, TYPE, T)            \
static void NAME(evalue val, uint8_t *dim, int inc) \
{                                                   \
    register TYPE extrema = val[0].T;               \
    int i, len = dim[0];                            \
    for (i = 1; i < len; i++) {                     \
        if (val[i].T OP extrema)                    \
            extrema = val[i].T;                     \
    }                                               \
    val[0].T = extrema;                             \
}
EXTREMA_VFUNC(vmaxi, >, int, i)
EXTREMA_VFUNC(vmini, <, int, i)
EXTREMA_VFUNC(vmaxf, >, float, f)
EXTREMA_VFUNC(vminf, <, float, f)
EXTREMA_VFUNC(vmaxd, >, double, d)
EXTREMA_VFUNC(vmind, <, double, d)

#define INC_SORT_FUNC(TYPE, T)                          \
int inc_sort_func##T (const void * a, const void * b) { \
    return ((*(evalue)a).T > (*(evalue)b).T);           \
}
INC_SORT_FUNC(int, i)
INC_SORT_FUNC(float, f)
INC_SORT_FUNC(double, d)

#define DEC_SORT_FUNC(TYPE, T)                          \
int dec_sort_func##T (const void * a, const void * b) { \
    return ((*(evalue)b).T > (*(evalue)a).T);           \
}
DEC_SORT_FUNC(int, i)
DEC_SORT_FUNC(float, f)
DEC_SORT_FUNC(double, d)

#define SORT_VFUNC(NAME, TYPE, T)                               \
static void NAME(evalue val, uint8_t *dim, int inc)             \
{                                                               \
    evalue dir = val + inc;                                     \
    if (dir[0].T >= 0)                                          \
        qsort(val, dim[0], sizeof(evalue_t), inc_sort_func##T); \
    else                                                        \
        qsort(val, dim[0], sizeof(evalue_t), dec_sort_func##T); \
}
SORT_VFUNC(vsorti, int, i)
SORT_VFUNC(vsortf, float, f)
SORT_VFUNC(vsortd, double, d)

#define MEDIAN_VFUNC(NAME, TYPE, T)                         \
static void NAME(evalue val, uint8_t *dim, int inc)         \
{                                                           \
    register int idx = floor(dim[0] * 0.5);                 \
    register double tmp;                                    \
    qsort(val, dim[0], sizeof(evalue_t), inc_sort_func##T); \
    tmp = (double)val[idx].T;                               \
    if (dim[0] > 2 && !(dim[0] % 2)) {                      \
        tmp += val[--idx].T;                                \
        tmp *= 0.5;                                         \
    }                                                       \
    val[0].T = (TYPE)tmp;                                   \
}
MEDIAN_VFUNC(vmedianf, float, f)
MEDIAN_VFUNC(vmediand, double, d)

#define powd pow
#define sqrtd sqrt
#define acosd acos

#define NORM_VFUNC(NAME, TYPE, T)                   \
static void NAME(evalue val, uint8_t *dim, int inc) \
{                                                   \
    register TYPE tmp = 0;                          \
    int i, len = dim[0];                            \
    for (i = 0; i < len; i++)                       \
        tmp += pow##T(val[i].T, 2);                 \
    val[0].T = sqrt##T(tmp);                        \
}
NORM_VFUNC(vnormf, float, f)
NORM_VFUNC(vnormd, double, d)

#define DOT_VFUNC(NAME, TYPE, T)                    \
static void NAME(evalue a, uint8_t *dim, int inc)   \
{                                                   \
    register TYPE dot = 0;                          \
    evalue b = a + inc;                             \
    int i, len = dim[0];                            \
    for (i = 0; i < len; i++)                       \
        dot += a[i].T * b[i].T;                     \
    a[0].T = dot;                                   \
}
DOT_VFUNC(vdoti, int, i)
DOT_VFUNC(vdotf, float, f)
DOT_VFUNC(vdotd, double, d)

#define INDEX_VFUNC(NAME, TYPE, T)                  \
static void NAME(evalue a, uint8_t *dim, int inc)   \
{                                                   \
    evalue b = a + inc;                             \
    int i, len = dim[0];                            \
    for (i = 0; i < len; i++) {                     \
        if (a[i].T == b[0].T) {                     \
            a[0].T = (TYPE)i;                       \
            return;                                 \
        }                                           \
    }                                               \
    a[0].T = (TYPE)-1;                              \
}
INDEX_VFUNC(vindexi, int, i)
INDEX_VFUNC(vindexf, float, f)
INDEX_VFUNC(vindexd, double, d)

/* TODO: should we handle multidimensional angles as well? Problem with sign...
 * should probably have separate function for signed and unsigned: angle vs. rotation */
/* TODO: quaternion functions */

#define atan2d atan2
#define ANGLE_VFUNC(NAME, TYPE, T)                              \
static void NAME(evalue a, uint8_t *dim, int inc)               \
{                                                               \
    register TYPE theta;                                        \
    evalue b = a + inc;                                         \
    theta = atan2##T(b[1].T, b[0].T) - atan2##T(a[1].T, a[0].T);\
    if (theta > M_PI)                                           \
        theta -= 2 * M_PI;                                      \
    else if (theta < -M_PI)                                     \
        theta += 2 * M_PI;                                      \
    a[0].T = theta;                                             \
}
ANGLE_VFUNC(vanglef, float, f)
ANGLE_VFUNC(vangled, double, d)

#define MAXMIN_VFUNC(NAME, TYPE, T)                 \
static void NAME(evalue max, uint8_t *dim, int inc) \
{                                                   \
    evalue min = max + inc, new = min + inc;        \
    int i, len = dim[0];                            \
    for (i = 0; i < len; i++) {                     \
        if (new[i].T > max[i].T)                    \
            max[i].T = new[i].T;                    \
        if (new[i].T < min[i].T)                    \
            min[i].T = new[i].T;                    \
    }                                               \
}
MAXMIN_VFUNC(vmaxmini, int, i)
MAXMIN_VFUNC(vmaxminf, float, f)
MAXMIN_VFUNC(vmaxmind, double, d)

#define SUMNUM_VFUNC(NAME, TYPE, T)                 \
static void NAME(evalue sum, uint8_t *dim, int inc) \
{                                                   \
    evalue num = sum + inc, new = num + inc;        \
    int i, len = dim[0];                            \
    for (i = 0; i < len; i++) {                     \
        sum[i].T += new[i].T;                       \
        num[i].T += 1;                              \
    }                                               \
}
SUMNUM_VFUNC(vsumnumi, int, i)
SUMNUM_VFUNC(vsumnumf, float, f)
SUMNUM_VFUNC(vsumnumd, double, d)

#define CONCAT_VFUNC(NAME, TYPE, T)                                     \
static void NAME(evalue cat, uint8_t *dim, int inc)                     \
{                                                                       \
    evalue num = cat + inc, new = num + inc;                            \
    uint8_t i, j, newlen = dim[2];                                      \
    for (i = dim[0], j = 0; j < newlen && i < (int)num[0].T; i++, j++)  \
        cat[i].T = new[j].T;                                            \
    dim[0] = i;                                                         \
}
CONCAT_VFUNC(vconcati, int, i)
CONCAT_VFUNC(vconcatf, float, f)
CONCAT_VFUNC(vconcatd, double, d)

#define TYPED_EMA(TYPE, T)                              \
static TYPE ema##T(TYPE memory, TYPE val, TYPE weight)  \
    { return memory + (val - memory) * weight; }
TYPED_EMA(float, f)
TYPED_EMA(double, d)

#define TYPED_SCHMITT(TYPE, T)                                      \
static TYPE schmitt##T(TYPE memory, TYPE val, TYPE low, TYPE high)  \
    { return memory ? val > low : val >= high; }
TYPED_SCHMITT(float, f)
TYPED_SCHMITT(double, d)

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

/* Stub functions because Microsoft's Release-mode compiler doesn't allow to take the address of these. */
static float flt_acos(float x) { return acosf(x); }
static float flt_asin(float x) { return asinf(x); }
static float flt_atan(float x) { return atanf(x); }
static float flt_atan2(float x, float y) { return atan2f(x, y); }
static float flt_ceil(float x) { return ceilf(x); }
static float flt_cos(float x) { return cosf(x); }
static float flt_cosh(float x) { return coshf(x); }
static float flt_exp(float x) { return expf(x); }
static float flt_floor(float x) { return floorf(x); }
static float flt_log(float x) { return logf(x); }
static float flt_log10(float x) { return log10f(x); }
static float flt_log2(float x) { return log2f(x); }
static float flt_pow(float x, float y) { return powf(x, y); }
static float flt_sin(float x) { return sinf(x); }
static float flt_sinh(float x) { return sinhf(x); }
static float flt_sqrt(float x) { return sqrtf(x); }
static float flt_tan(float x) { return tanf(x); }
static float flt_tanh(float x) { return tanh(x); }

static double dbl_acos(double x) { return acos(x); }
static double dbl_asin(double x) { return asin(x); }
static double dbl_atan(double x) { return atan(x); }
static double dbl_atan2(double x, double y) { return atan2(x, y); }
static double dbl_ceil(double x) { return ceil(x); }
static double dbl_cos(double x) { return cos(x); }
static double dbl_cosh(double x) { return cosh(x); }
static double dbl_exp(double x) { return exp(x); }
static double dbl_floor(double x) { return floor(x); }
static double dbl_log(double x) { return log(x); }
static double dbl_log10(double x) { return log10(x); }
static double dbl_log2(double x) { return log2(x); }
static double dbl_pow(double x, double y) { return pow(x, y); }
static double dbl_sin(double x) { return sin(x); }
static double dbl_sinh(double x) { return sinh(x); }
static double dbl_sqrt(double x) { return sqrt(x); }
static double dbl_tan(double x) { return tan(x); }
static double dbl_tanh(double x) { return tanh(x); }

#if _M_ARM64
    /* Needed to work around the fact that the function fabsf on Windows ARM64 is inline-only
     * This lets libmapper compile with optimizations when targeting Windows ARM64 */
    float fabsf2(float a) {
        return (float)fabs(a);
    }
#else
    #define fabsf2 fabsf
#endif

static struct {
    const char *name;
    uint8_t arity;
    uint8_t memory;
    void *fn_int;
    void *fn_flt;
    void *fn_dbl;
} fn_tbl[] = {
    { "abs",      1, 0, (void*)abs,   (void*)fabsf2,    (void*)fabs      },
    { "acos",     1, 0, 0,            (void*)flt_acos,  (void*)dbl_acos  },
    { "acosh",    1, 0, 0,            (void*)acoshf,    (void*)acosh     },
    { "asin",     1, 0, 0,            (void*)flt_asin,  (void*)dbl_asin  },
    { "asinh",    1, 0, 0,            (void*)asinhf,    (void*)asinh     },
    { "atan",     1, 0, 0,            (void*)flt_atan,  (void*)dbl_atan  },
    { "atan2",    2, 0, 0,            (void*)flt_atan2, (void*)dbl_atan2 },
    { "atanh",    1, 0, 0,            (void*)atanhf,    (void*)atanh     },
    { "cbrt",     1, 0, 0,            (void*)cbrtf,     (void*)cbrt      },
    { "ceil",     1, 0, 0,            (void*)flt_ceil,  (void*)dbl_ceil  },
    { "cos",      1, 0, 0,            (void*)flt_cos,   (void*)dbl_cos   },
    { "cosh",     1, 0, 0,            (void*)flt_cosh,  (void*)dbl_cosh  },
    { "ema",      3, 1, 0,            (void*)emaf,      (void*)emad      },
    { "exp",      1, 0, 0,            (void*)flt_exp,   (void*)dbl_exp   },
    { "exp2",     1, 0, 0,            (void*)exp2f,     (void*)exp2      },
    { "floor",    1, 0, 0,            (void*)flt_floor, (void*)dbl_floor },
    { "hypot",    2, 0, 0,            (void*)hypotf,    (void*)hypot     },
    { "hzToMidi", 1, 0, 0,            (void*)hzToMidif, (void*)hzToMidid },
    { "log",      1, 0, 0,            (void*)flt_log,   (void*)dbl_log   },
    { "log10",    1, 0, 0,            (void*)flt_log10, (void*)dbl_log10 },
    { "log2",     1, 0, 0,            (void*)flt_log2,  (void*)dbl_log2  },
    { "logb",     1, 0, 0,            (void*)logbf,     (void*)logb      },
    { "max",      2, 0, (void*)maxi,  (void*)maxf,      (void*)maxd      },
    { "midiToHz", 1, 0, 0,            (void*)midiToHzf, (void*)midiToHzd },
    { "min",      2, 0, (void*)mini,  (void*)minf,      (void*)mind      },
    { "pow",      2, 0, 0,            (void*)flt_pow,   (void*)dbl_pow   },
    { "round",    1, 0, 0,            (void*)roundf,    (void*)round     },
    { "schmitt",  4, 1, 0,            (void*)schmittf,  (void*)schmittd  },
    { "sign",     1, 0, (void*)signi, (void*)signf,     (void*)signd     },
    { "sin",      1, 0, 0,            (void*)flt_sin,   (void*)dbl_sin   },
    { "sinh",     1, 0, 0,            (void*)flt_sinh,  (void*)dbl_sinh  },
    { "sqrt",     1, 0, 0,            (void*)flt_sqrt,  (void*)dbl_sqrt  },
    { "tan",      1, 0, 0,            (void*)flt_tan,   (void*)dbl_tan   },
    { "tanh",     1, 0, 0,            (void*)flt_tanh,  (void*)dbl_tanh  },
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
    VFN_CONCAT,
    /* function names above this line are also found in rfn_table */
    VFN_NORM,
    VFN_SORT,
    VFN_MAXMIN,
    VFN_SUMNUM,
    VFN_ANGLE,
    VFN_DOT,
    VFN_INDEX,
    VFN_LENGTH,
    VFN_MEDIAN,
    N_VFN
} expr_vfn_t;

static struct {
    const char *name;
    uint8_t arity;
    uint8_t reduce; /* TODO: use bitflags */
    uint8_t dot_notation;
    void (*fn_int)(evalue, uint8_t*, int);
    void (*fn_flt)(evalue, uint8_t*, int);
    void (*fn_dbl)(evalue, uint8_t*, int);
} vfn_tbl[] = {
    { "all",    1, 1, 1, valli,    vallf,    valld    },
    { "any",    1, 1, 1, vanyi,    vanyf,    vanyd    },
    { "center", 1, 1, 1, 0,        vcenterf, vcenterd },
    { "max",    1, 1, 1, vmaxi,    vmaxf,    vmaxd    },
    { "mean",   1, 1, 1, 0,        vmeanf,   vmeand   },
    { "min",    1, 1, 1, vmini,    vminf,    vmind    },
    { "sum",    1, 1, 1, vsumi,    vsumf,    vsumd    },
    { "concat", 3, 0, 0, vconcati, vconcatf, vconcatd },
    { "norm",   1, 1, 1, 0,        vnormf,   vnormd   },
    { "sort",   2, 0, 1, vsorti,   vsortf,   vsortd   },
    { "maxmin", 3, 0, 0, vmaxmini, vmaxminf, vmaxmind },
    { "sumnum", 3, 0, 0, vsumnumi, vsumnumf, vsumnumd },
    { "angle",  2, 1, 0, 0,        vanglef,  vangled  },
    { "dot",    2, 1, 0, vdoti,    vdotf,    vdotd    },
    { "index",  2, 1, 1, vindexi,  vindexf,  vindexd  },
    { "length", 1, 1, 1, vleni,    vlenf,    vlend    },
    { "median", 1, 1, 1, 0,        vmedianf, vmediand }
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
    RFN_CONCAT,
    /* function names above this line are also found in vfn_table */
    RFN_COUNT,
    RFN_SIZE,
    RFN_NEWEST,
/*  RFN_MAP, */
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
    { "concat",   3, OP_UNKNOWN,     VFN_CONCAT  }, /* replaced during parsing */
    { "count",    0, OP_ADD,         VFN_UNKNOWN },
    { "size",     0, OP_UNKNOWN,     VFN_MAXMIN  },
    { "newest",   0, OP_UNKNOWN,     VFN_UNKNOWN },
/*  { "map",      1, OP_UNKNOWN,     VFN_UNKNOWN }, */
    { "filter",   1, OP_UNKNOWN,     VFN_UNKNOWN }, /* replaced during parsing */
    { "reduce",   1, OP_UNKNOWN,     VFN_UNKNOWN }, /* replaced during parsing */
    { "history",  1, OP_UNKNOWN,     VFN_UNKNOWN }, /* replaced during parsing */
    { "instance", 0, OP_UNKNOWN,     VFN_UNKNOWN }, /* replaced during parsing */
    { "signal",   0, OP_UNKNOWN,     VFN_UNKNOWN }, /* replaced during parsing */
    { "vector",   0, OP_UNKNOWN,     VFN_UNKNOWN }  /* replaced during parsing */
};

typedef int fn_int_arity0(void);
typedef int fn_int_arity1(int);
typedef int fn_int_arity2(int,int);
typedef int fn_int_arity3(int,int,int);
typedef int fn_int_arity4(int,int,int,int);
typedef float fn_flt_arity0(void);
typedef float fn_flt_arity1(float);
typedef float fn_flt_arity2(float,float);
typedef float fn_flt_arity3(float,float,float);
typedef float fn_flt_arity4(float,float,float,float);
typedef double fn_dbl_arity0(void);
typedef double fn_dbl_arity1(double);
typedef double fn_dbl_arity2(double,double);
typedef double fn_dbl_arity3(double,double,double);
typedef double fn_dbl_arity4(double,double,double,double);
typedef void vfn_template(evalue, uint8_t*, int);

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
            && strncmp_lc(s, LC##_tbl[i].name, len) == 0) {         \
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

enum reduce_type {
    RT_UNKNOWN  = 0x00,
    RT_HISTORY  = 0x01,
    RT_INSTANCE = 0x02,
    RT_SIGNAL   = 0x04,
    RT_VECTOR   = 0x08
};

#define REDUCE_TYPE_MASK 0x0F

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

#endif /* __MPR_EXPR_FUNCTION_H__ */
