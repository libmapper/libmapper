#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "mapper_internal.h"

#define MAX_HIST_SIZE 100
#define STACK_SIZE 128
#define N_USER_VARS 16
#ifdef DEBUG
    #define TRACING 0 /* Set non-zero to see trace during parse & eval. */
#else
    #define TRACING 0
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

#define TEST_VEC(OP, CMP, RET, EL)  \
    for (int i = 0; i < len; i++) { \
        if (val[i].EL OP CMP)       \
            return RET;             \
    }                               \
    return 1 - RET;
#define TEST_VEC_TYPED(NAME, TYPE, OP, CMP, RET, EL)    \
    static TYPE NAME##EL(mpr_expr_val val, int len) {   \
        TEST_VEC(OP, CMP, RET, EL)                      \
    }
TEST_VEC_TYPED(all, int, ==, 0, 0, i);
TEST_VEC_TYPED(any, int, !=, 0, 1, i);
TEST_VEC_TYPED(all, float, ==, 0.f, 0, f);
TEST_VEC_TYPED(any, float, !=, 0.f, 1, f);
TEST_VEC_TYPED(all, double, ==, 0., 0, d);
TEST_VEC_TYPED(any, double, !=, 0., 1, d);

#define SUM_FUNC(TYPE, EL)                          \
    static TYPE sum##EL(mpr_expr_val val, int len)  \
    {                                               \
        TYPE aggregate = 0;                         \
        for (int i = 0; i < len; i++)               \
            aggregate += val[i].EL;                 \
        return aggregate;                           \
    }
SUM_FUNC(int, i);
SUM_FUNC(float, f);
SUM_FUNC(double, d);

static float meanf(mpr_expr_val val, int len)
{
    return sumf(val, len) / (float)len;
}

static double meand(mpr_expr_val val, int len)
{
    return sumd(val, len) / (double)len;
}

#define EXTREMA_VFUNC(NAME, OP, TYPE, EL)   \
static TYPE NAME(mpr_expr_val val, int len) \
{                                           \
    TYPE extrema = val[0].EL;               \
    for (int i = 1; i < len; i++) {         \
        if (val[i].EL OP extrema)           \
            extrema = val[i].EL;            \
    }                                       \
    return extrema;                         \
}
EXTREMA_VFUNC(vmaxi, >, int, i);
EXTREMA_VFUNC(vmini, <, int, i);
EXTREMA_VFUNC(vmaxf, >, float, f);
EXTREMA_VFUNC(vminf, <, float, f);
EXTREMA_VFUNC(vmaxd, >, double, d);
EXTREMA_VFUNC(vmind, <, double, d);

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

static void icount(mpr_expr_val t, mpr_value s)
{
    int i, count = 0;
    for (i = 0; i < s->num_inst; i++)
        count += s->inst[i].pos >= 0;
    for (i = 0; i < s->vlen; i++)
    t[i].i = count;
}

#define SUM_IFUNC(TYPE, EL)                             \
static void isum##EL(mpr_expr_val t, mpr_value s)       \
{                                                       \
    int i, j;                                           \
    for (j = 0; j < s->vlen; j++)                       \
        t[j].EL = 0;                                    \
    for (i = 0; i < s->num_inst; i++) {                 \
        if (s->inst[i].pos < 0)                         \
            continue;                                   \
        TYPE *samp = (TYPE*)mpr_value_get_samp(s, i);   \
        for (j = 0; j < s->vlen; j++)                   \
            t[j].EL += samp[j];                         \
    }                                                   \
}
SUM_IFUNC(int, i);
SUM_IFUNC(float, f);
SUM_IFUNC(double, d);

#define TYPED_INST_MEAN(TYPE, EL)                           \
static void imean##EL(mpr_expr_val t, mpr_value s)          \
{                                                           \
    int i, j, count = 0;                                    \
    for (j = 0; j < s->vlen; j++)                           \
        t[j].EL = 0;                                        \
    for (i = 0; i < s->num_inst; i++) {                     \
        if (s->inst[i].pos < 0)                             \
            continue;                                       \
        if (MPR_INT32 == s->type) {                         \
            int *samp = (int*)mpr_value_get_samp(s, i);     \
            for (j = 0; j < s->vlen; j++)                   \
                t[j].EL += (TYPE)samp[j];                   \
            ++count;                                        \
        }                                                   \
        else {                                              \
            TYPE *samp = (TYPE*)mpr_value_get_samp(s, i);   \
            for (j = 0; j < s->vlen; j++)                   \
                t[j].EL += samp[j];                         \
            ++count;                                        \
        }                                                   \
    }                                                       \
    if (count > 1) {                                        \
        for (j = 0; j < s->vlen; j++)                       \
            t[i].EL /= (TYPE)count;                         \
    }                                                       \
}
TYPED_INST_MEAN(float, f);
TYPED_INST_MEAN(double, d);

#define EXTREMA_IFUNC(NAME, TYPE, EL, OP)               \
static void NAME(mpr_expr_val t, mpr_value s)           \
{                                                       \
    int i, j, count = 0;                                \
    for (i = 0; i < s->num_inst; i++) {                 \
        if (s->inst[i].pos < 0)                         \
            continue;                                   \
        TYPE *samp = (TYPE*)mpr_value_get_samp(s, i);   \
        for (j = 0; j < s->vlen; j++) {                 \
            if (!count || samp[j] OP t[j].EL) {         \
                t[j].EL = samp[j];                      \
                count = 1;                              \
            }                                           \
        }                                               \
    }                                                   \
}
EXTREMA_IFUNC(imaxi, int, i, >);
EXTREMA_IFUNC(imaxf, float, f, >);
EXTREMA_IFUNC(imaxd, double, d, >);
EXTREMA_IFUNC(imini, int, i, <);
EXTREMA_IFUNC(iminf, float, f, <);
EXTREMA_IFUNC(imind, double, d, <);

typedef enum {
    VAR_UNKNOWN=-1,
    VAR_Y=N_USER_VARS,
    VAR_X,
    N_VARS
} expr_var_t;

typedef enum {
    FN_UNKNOWN=-1,
    FN_ABS=0,
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
    FN_POOL,
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
    { "pool",       0,  0,  0,      0,          0           },
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
    VFN_UNKNOWN=-1,
    VFN_ALL=0,
    VFN_ANY,
    VFN_MEAN,
    VFN_SUM,
    VFN_MAX,
    VFN_MIN,
    N_VFN
} expr_vfn_t;

static struct {
    const char *name;
    unsigned int arity;
    void *fn_int;
    void *fn_flt;
    void *fn_dbl;
} vfn_tbl[] = {
    { "all",    1,      alli,       allf,       alld        },
    { "any",    1,      anyi,       anyf,       anyd        },
    { "mean",   1,      0,          meanf,      meand       },
    { "sum",    1,      sumi,       sumf,       sumd        },
    { "max",    1,      vmaxi,      vmaxf,      vmaxd       },
    { "min",    1,      vmini,      vminf,      vmind       },
};

typedef enum {
    IFN_UNKNOWN=-1,
    IFN_COUNT=0,
    IFN_MEAN,
    IFN_SUM,
    IFN_MAX,
    IFN_MIN,
    N_IFN
} expr_ifn_t;

static struct {
    const char *name;
    unsigned int arity;
    void *fn_int;
    void *fn_flt;
    void *fn_dbl;
} ifn_tbl[] = {
    { "inst.count", 0,  icount, 0,          0       },
    { "inst.mean",  0,  0,      imeanf,     imeand  },
    { "inst.sum",   0,  isumi,  isumf,      isumd   },
    { "inst.max",   0,  imaxi,  imaxf,      imaxd   },
    { "inst.min",   0,  imini,  iminf,      imind   },
};

typedef enum {
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
    uint16_t optimize_const_operands;
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
typedef int vfn_int_arity1(mpr_expr_val, int);
typedef float vfn_flt_arity1(mpr_expr_val, int);
typedef double vfn_dbl_arity1(mpr_expr_val, int);
typedef int ifn_int_arity1(mpr_expr_val, mpr_value);
typedef float ifn_flt_arity1(mpr_expr_val, mpr_value);
typedef double ifn_dbl_arity1(mpr_expr_val, mpr_value);

typedef struct _token {
    union {
        float f;
        int i;
        double d;
        expr_var_t var;
        expr_op_t op;
        expr_fn_t fn;
        expr_vfn_t vfn; // vector function
    };
    enum {
        TOK_CONST           = 0x000001,
        TOK_NEGATE          = 0x000002,
        TOK_FN              = 0x000004,
        TOK_VFN             = 0x000008,
        TOK_IFN             = 0x000010,
        TOK_OPEN_PAREN      = 0x000020,
        TOK_MUTED           = 0x000040,
        TOK_PUBLIC          = 0x000080,
        TOK_OPEN_SQUARE     = 0x000100,
        TOK_OPEN_CURLY      = 0x000200,
        TOK_CLOSE_PAREN     = 0x000400,
        TOK_CLOSE_SQUARE    = 0x000800,
        TOK_CLOSE_CURLY     = 0x001000,
        TOK_VAR             = 0x002000,
        TOK_OP              = 0x004000,
        TOK_COMMA           = 0x008000,
        TOK_COLON           = 0x010000,
        TOK_SEMICOLON       = 0x020000,
        TOK_VECTORIZE       = 0x040000,
        TOK_POOL            = 0x080000,
        TOK_ASSIGN          = 0x100000,
        TOK_ASSIGN_USE,
        TOK_ASSIGN_CONST,
        TOK_ASSIGN_TT,
        TOK_TT              = 0x200000,
        TOK_END             = 0x400000,
    } toktype;
    union {
        mpr_type casttype;
        uint8_t offset;
    };
    mpr_type datatype;
    uint8_t vec_len;
//    union {
        uint8_t vec_idx;
    union {
        uint8_t arity;
        expr_ifn_t ifn; // instance function
    };
    int8_t hist;          // TODO switch to bitflags
    char vec_len_locked;  // TODO switch to bitflags
    char muted;           // TODO switch to bitflags
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

#define FN_LOOKUP(LC, UC)                                   \
static expr_##LC##_t LC##_lookup(const char *s, int len)    \
{                                                           \
    int i, j;                                               \
    for (i=0; i<N_##UC; i++) {                              \
        if (   strlen(LC##_tbl[i].name)==len                \
            && strncmp_lc(s, LC##_tbl[i].name, len)==0) {   \
            /* check for parenthesis */                     \
            j = strlen(LC##_tbl[i].name);                   \
            while (s[j]) {                                  \
                if (s[j] == '(')                            \
                    return i;                               \
                else if (s[j] != ' ')                       \
                    return UC##_UNKNOWN;                    \
                ++j;                                        \
            }                                               \
            break;                                          \
        }                                                   \
    }                                                       \
    return UC##_UNKNOWN;                                    \
}

FN_LOOKUP(fn, FN);
FN_LOOKUP(vfn, VFN);
FN_LOOKUP(ifn, IFN);

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
        tok->d = M_PI;
    else if (len == 1 && *s == 'e')
        tok->d = M_E;
    else
        return 1;
    s += 2;
    len -= 2;
    tok->toktype = TOK_CONST;
    tok->datatype = MPR_DBL;
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
        case TOK_OP:        return op_tbl[tok.op].arity;
        case TOK_FN:        return fn_tbl[tok.fn].arity;
        case TOK_IFN:       return ifn_tbl[tok.ifn].arity;
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
        if (!isdigit(c) && c!='e' && integer_found) {
            tok->toktype = TOK_CONST;
            tok->f = (float)n;
            tok->datatype = MPR_FLT;
            return idx;
        }
        if (!isdigit(c) && c!='e')
            break;
        do {
            c = str[++idx];
        } while (c && isdigit(c));
        if (c!='e') {
            tok->d = atof(str+i);
            tok->toktype = TOK_CONST;
            tok->datatype = MPR_DBL;
            return idx;
        }
    case 'e':
        if (!integer_found) {
            while (c && (isalpha(c) || isdigit(c) || c == '_' || c == '.'))
                c = str[++idx];
            if ((tok->fn = fn_lookup(str+i, idx-i)) != FN_UNKNOWN)
                tok->toktype = TOK_FN;
            else if ((tok->vfn = vfn_lookup(str+i, idx-i)) != VFN_UNKNOWN)
                tok->toktype = TOK_VFN;
            else if ((tok->ifn = ifn_lookup(str+i, idx-i)) != IFN_UNKNOWN)
                tok->toktype = TOK_IFN;
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
        i = idx-1;
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
        while (c && (isalpha(c) || isdigit(c) || c == '_' || c == '.'))
            c = str[++idx];
        if ((tok->fn = fn_lookup(str+i, idx-i)) != FN_UNKNOWN)
            tok->toktype = TOK_FN;
        else if ((tok->vfn = vfn_lookup(str+i, idx-i)) != VFN_UNKNOWN)
            tok->toktype = TOK_VFN;
        else if ((tok->ifn = ifn_lookup(str+i, idx-i)) != IFN_UNKNOWN)
            tok->toktype = TOK_IFN;
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
    uint8_t len;
    uint8_t vec_size;
    uint8_t *in_hist_size;
    uint8_t out_hist_size;
    uint8_t n_vars;
    int8_t inst_ctl;
    int8_t mute_ctl;
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

#ifdef DEBUG

static void printtoken(mpr_token_t t, mpr_var_t *vars)
{
    int i, len = 64;
    char s[len];
    switch (t.toktype) {
        case TOK_CONST:
            switch (t.datatype) {
                case MPR_FLT:   snprintf(s, len, "%f", t.f);             break;
                case MPR_DBL:   snprintf(s, len, "%f", t.d);             break;
                case MPR_INT32: snprintf(s, len, "%d", t.i);             break;
            }                                                           break;
        case TOK_OP:            snprintf(s, len, "op.%s", op_tbl[t.op].name); break;
        case TOK_OPEN_CURLY:    snprintf(s, len, "{");                   break;
        case TOK_OPEN_PAREN:    snprintf(s, len, "(");                   break;
        case TOK_OPEN_SQUARE:   snprintf(s, len, "[");                   break;
        case TOK_CLOSE_CURLY:   snprintf(s, len, "}");                   break;
        case TOK_CLOSE_PAREN:   snprintf(s, len, ")");                   break;
        case TOK_CLOSE_SQUARE:  snprintf(s, len, "]");                   break;
        case TOK_VAR:
            if (t.var == VAR_Y)
                snprintf(s, len, "var.y%s[%u]", t.hist ? "{N}" : "", t.vec_idx);
            else if (t.var >= VAR_X)
                snprintf(s, len, "var.x%d%s[%u]", t.var-VAR_X, t.hist ? "{N}" : "", t.vec_idx);
            else
                snprintf(s, len, "var.%s%s[%u]", vars[t.var].name, t.hist ? "{N}" : "", t.vec_idx);
            break;
        case TOK_TT:
            if (t.var == VAR_Y)
                snprintf(s, len, "tt.y%s", t.hist ? "{N}" : "");
            else if (t.var >= VAR_X)
                snprintf(s, len, "tt.x%d%s", t.var-VAR_X, t.hist ? "{N}" : "");
            else
                snprintf(s, len, "tt.%s%s", vars[t.var].name, t.hist ? "{N}" : "");
            break;
        case TOK_FN:        snprintf(s, len, "fn.%s(arity %d)", fn_tbl[t.fn].name, t.arity); break;
        case TOK_COMMA:     snprintf(s, len, ",");                       break;
        case TOK_COLON:     snprintf(s, len, ":");                       break;
        case TOK_VECTORIZE: snprintf(s, len, "VECT(%d)", t.arity);       break;
        case TOK_NEGATE:    snprintf(s, len, "-");                       break;
        case TOK_VFN:       snprintf(s, len, "vfn.%s()", vfn_tbl[t.vfn].name); break;
        case TOK_POOL:
            if (t.var == VAR_UNKNOWN)
                snprintf(s, len, "pool(?)");
            else if (t.var == VAR_Y)
                snprintf(s, len, "pool(y)");
            else if (t.var >= VAR_X)
                snprintf(s, len, "pool(x%d)", t.var-VAR_X);
            else
                snprintf(s, len, "pool(%s)", vars[t.var].name);
            break;
        case TOK_IFN:       snprintf(s, len, "ifn.%s()", ifn_tbl[t.ifn].name+5); break;
        case TOK_ASSIGN:
        case TOK_ASSIGN_CONST:
        case TOK_ASSIGN_USE:
            if (t.var == VAR_Y)
                snprintf(s, len, "ASSIGN_TO:y%s[%u]->[%u]%s", t.hist ? "{N}" : "", t.offset,
                         t.vec_idx, t.toktype == TOK_ASSIGN_CONST ? " (const) " : "");
            else
                snprintf(s, len, "ASSIGN_TO:%s%s[%u]->[%u]%s", vars[t.var].name,
                         t.hist ? "{N}" : "", t.offset, t.vec_idx,
                         t.toktype == TOK_ASSIGN_CONST ? " (const) " : "");
            break;
        case TOK_ASSIGN_TT:
            snprintf(s, len, "ASSIGN_TO:t_y%s->[%u]", t.hist ? "{N}" : "", t.vec_idx);
            break;
        case TOK_END:       printf("END\n");                return;
        default:            printf("(unknown token)\n");    return;
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
    printf("\n");
}

static void printstack(const char *s, mpr_token_t *stk, int top,
                       mpr_var_t *vars, int show_init_line)
{
    int i, j, indent = 0, can_advance = 1;
    printf("%s ", s);
    if (s)
        indent = strlen(s) + 1;
    if (top < 0) {
        printf("EMPTY\n");
        return;
    }
    for (i=0; i<=top; i++) {
        if (i != 0) {
            for (j=0; j<indent; j++)
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
                    for (j=i+1; j<=top; j++) {
                        if (stk[j].toktype < TOK_ASSIGN)
                            continue;
                        if (TOK_ASSIGN_CONST == stk[j].toktype && stk[j].var != VAR_Y)
                            break;
                        if (stk[j].hist)
                            break;
                        for (j=0; j<indent; j++)
                            printf(" ");
                        printf("--- <INITIALISATION DONE> ---\n");
                        can_advance = 0;
                        break;
                    }
                    break;
                case TOK_IFN:
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
    printstack(s, e->tokens, e->len-1, e->vars, 1);
}

#endif // DEBUG

static mpr_type compare_token_datatype(mpr_token_t tok, mpr_type type)
{
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
    else if (tok->toktype == TOK_VAR || tok->toktype == TOK_IFN) {
        // we need to cast at runtime
        tok->casttype = type;
        return type;
    }
    else if (tok->toktype == TOK_POOL) {
        // keep variable type
        return tok->datatype;
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

static void lock_vec_len(mpr_token_t *stk, int top)
{
    int i=top, arity=1;
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

static int precompute(mpr_token_t *stk, int len, int vec_len)
{
    struct _mpr_expr e = {0, stk, 0, 0, len, vec_len, 0, 0, 0, -1, -1};
    void *s = malloc(mpr_type_get_size(stk[len-1].datatype) * vec_len);
    mpr_value_buffer_t b = {s, 0, -1};
    mpr_value_t v = {&b, vec_len, 1, stk[len-1].datatype, 1};
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
        stk[i].datatype = stk[len-1].datatype;
    }
    free(s);
    return len-1;
}

static int check_type_and_len(mpr_token_t *stk, int top, mpr_var_t *vars)
{
    // TODO: enable precomputation of const-only vectors
    int i, arity, can_precompute = 1, optimize = NONE;
    mpr_type type = stk[top].datatype;
    uint8_t vec_len = stk[top].vec_len;
    switch (stk[top].toktype) {
        case TOK_OP:
            if (stk[top].op == OP_IF)
                PARSE_ERROR(-1, "Ternary operator is missing operand.\n");
            arity = op_tbl[stk[top].op].arity;
            break;
        case TOK_FN:
            arity = fn_tbl[stk[top].fn].arity;
            if (stk[top].fn >= FN_DELAY)
                can_precompute = 0;
            break;
        case TOK_IFN:
            arity = ifn_tbl[stk[top].ifn].arity;
            can_precompute = 0;
            break;
        case TOK_VFN:
            arity = vfn_tbl[stk[top].vfn].arity;
            break;
        case TOK_VECTORIZE:
            arity = stk[top].arity;
            can_precompute = 0;
            break;
        case TOK_ASSIGN:
        case TOK_ASSIGN_CONST:
        case TOK_ASSIGN_TT:
        case TOK_ASSIGN_USE:
            arity = stk[top].hist ? 2 : 1;
            can_precompute = 0;
            break;
        default:
            return top;
    }
    if (arity) {
        // find operator or function inputs
        i = top;
        int skip = 0;
        int depth = arity;
        int operand = 0;
        // last arg of op or func is at top-1
        type = compare_token_datatype(stk[top-1], type);
        if (stk[top-1].vec_len > vec_len)
            vec_len = stk[top-1].vec_len;

        // Walk down stack distance of arity, checking types and vector lengths.
        while (--i >= 0) {
            if (stk[i].toktype == TOK_FN && fn_tbl[stk[i].fn].arity)
                can_precompute = 0;
            else if (stk[i].toktype != TOK_CONST)
                can_precompute = 0;

            if (skip == 0) {
                if (stk[i].toktype == TOK_CONST && stk[top].toktype == TOK_OP
                    && depth <= op_tbl[stk[top].op].arity) {
                    if (const_tok_is_zero(stk[i])) {
                        // mask and bitshift, depth == 1 or 2
                        optimize = (op_tbl[stk[top].op].optimize_const_operands>>(depth-1)*4) & 0xF;
                    }
                    else if (const_tok_is_one(stk[i])) {
                        optimize = (op_tbl[stk[top].op].optimize_const_operands>>(depth+1)*4) & 0xF;
                    }
                    if (optimize == GET_OPER) {
                        if (i == top-1) {
                            // optimize immediately without moving other operand
                            return top-2;
                        }
                        else {
                            // store position of non-zero operand
                            operand = top-1;
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
                case TOK_IFN:        skip += ifn_tbl[stk[i].ifn].arity;  break;
                case TOK_VFN:        skip += vfn_tbl[stk[i].vfn].arity;  break;
                case TOK_VECTORIZE:  skip += stk[i].arity;              break;
                case TOK_ASSIGN_USE: ++skip;                            break;
                case TOK_VAR:        skip += stk[i].hist ? 1 : 0;       break;
                default:                                                break;
            }
        }

        if (depth)
            return -1;

        if (!can_precompute) {
            switch (optimize) {
                case BAD_EXPR:
                    PARSE_ERROR(-1, "Operator '%s' cannot have zero operand.\n",
                                op_tbl[stk[top].op].name);
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
                    top = operand-1;
                default:
                    break;
            }
        }

        // walk down stack distance of arity again, promoting types and lengths
        i = top;
        switch (stk[top].toktype) {
            case TOK_VECTORIZE:  skip = stk[top].arity; depth = 0;      break;
            case TOK_VFN:
            case TOK_ASSIGN_USE: skip = 1;              depth = 0;      break;
            case TOK_VAR: skip = stk[top].hist ? 1 : 0; depth = 0;      break;
            default:             skip = 0;              depth = arity;  break;
        }
        type = promote_token_datatype(&stk[i], type);
        while (--i >= 0) {
            // we will promote types within range of compound arity
            if ((stk[i+1].toktype != TOK_VAR && stk[i+1].toktype != TOK_TT
                 && stk[i+1].toktype != TOK_IFN)
                || !stk[i+1].hist) {
                // don't promote type of history indices
                type = promote_token_datatype(&stk[i], type);
            }

            if (skip <= 0) {
                // also check/promote vector length
                if (!stk[i].vec_len_locked) {
                    if (stk[i].toktype == TOK_VFN) {
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
                    skip = 2;
                    break;
                case TOK_VECTORIZE:
                    skip = stk[i].arity + 1;
                    break;
                case TOK_ASSIGN_USE:
                    ++skip;
                    ++depth;
                    break;
                case TOK_IFN:
                case TOK_VAR:
                    if (stk[i].hist) {
                        ++skip;
                        ++depth;
                    }
                    break;
                default:
                    break;
            }

            if (skip > 0)
                --skip;
            else
                --depth;
            if (depth <= 0 && skip <= 0)
                break;
        }

        if (!stk[top].vec_len_locked) {
            if (stk[top].toktype != TOK_VFN)
                stk[top].vec_len = vec_len;
        }
        else if (stk[top].vec_len != vec_len)
            PARSE_ERROR(-1, "Vector length mismatch (3) %u != %u\n", stk[top].vec_len, vec_len);
    }
    // if stack within bounds of arity was only constants, we're ok to compute
    if (can_precompute)
        return top - precompute(&stk[top-arity], arity+1, vec_len);
    else
        return top;
}

static int check_assign_type_and_len(mpr_token_t *stk, int top, mpr_var_t *vars)
{
    int i = top;
    uint8_t vec_len = 0;
    expr_var_t var = stk[top].var;

    while (i >= 0 && (stk[i].toktype & TOK_ASSIGN) && (stk[i].var == var)) {
        vec_len += stk[i].vec_len;
        --i;
    }
    if (i < 0)
        PARSE_ERROR(-1, "Malformed expression (1)\n");
    if (stk[i].vec_len != vec_len)
        PARSE_ERROR(-1, "Vector length mismatch (4) %u != %u\n",
                    stk[i].vec_len, vec_len);
    promote_token_datatype(&stk[i], stk[top].datatype);
    if (check_type_and_len(stk, i, vars) == -1)
        return -1;
    promote_token_datatype(&stk[i], stk[top].datatype);

    if (stk[top].hist == 0 || i == 0)
        return 0;

    // Move assignment expression to beginning of stack
    int j = 0, expr_len = top - i + (stk[top].hist ? 2 : 1);
    if (stk[i].toktype == TOK_VECTORIZE)
        expr_len += stk[i].arity;

    mpr_token_t temp[expr_len];
    for (i = top-expr_len+1; i <= top; i++)
        memcpy(&temp[j++], &stk[i], sizeof(mpr_token_t));

    for (i = top-expr_len; i >= 0; i--)
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
    {FAIL_IF(++out_idx >= STACK_SIZE, "Stack size exceeded.");}     \
    if (x.toktype == TOK_ASSIGN_CONST && !is_const)                 \
        x.toktype = TOK_ASSIGN;                                     \
    memcpy(out + out_idx, &x, sizeof(mpr_token_t));                 \
}
#define POP_OUTPUT() ( out_idx-- )
#define PUSH_TO_OPERATOR(x)                                         \
{                                                                   \
    {FAIL_IF(++op_idx >= STACK_SIZE, "Stack size exceeded.");}      \
    memcpy(op + op_idx, &x, sizeof(mpr_token_t));                   \
}
#define POP_OPERATOR() ( op_idx-- )
#define POP_OPERATOR_TO_OUTPUT()                                    \
{                                                                   \
    PUSH_TO_OUTPUT(op[op_idx]);                                     \
    out_idx = check_type_and_len(out, out_idx, vars);               \
    {FAIL_IF(out_idx < 0, "Malformed expression (3).");}            \
    POP_OPERATOR();                                                 \
}
#define GET_NEXT_TOKEN(x)                                           \
{                                                                   \
    lex_idx = expr_lex(str, lex_idx, &x);                           \
    {FAIL_IF(!lex_idx, "Error in lexer.");}                         \
}

/*! Use Dijkstra's shunting-yard algorithm to parse expression into RPN stack. */
mpr_expr mpr_expr_new_from_str(const char *str, int n_ins,
                               const mpr_type *in_types, const int *in_vec_lens,
                               mpr_type out_type, int out_vec_len)
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
    int OBJECT_TOKENS = (TOK_VAR | TOK_CONST | TOK_FN | TOK_IFN | TOK_VFN | TOK_MUTED | TOK_PUBLIC
                         | TOK_NEGATE | TOK_OPEN_PAREN | TOK_OPEN_SQUARE | TOK_OP | TOK_TT);
    mpr_token_t tok;

    // ignoring spaces at start of expression
    while (str[lex_idx] == ' ') ++lex_idx;
    {FAIL_IF(!str[lex_idx], "No expression found.");}

    assigning = 1;
    allow_toktype = TOK_VAR | TOK_TT | TOK_OPEN_SQUARE | TOK_MUTED | TOK_PUBLIC;

#if TRACING
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
                    int idx = lex_idx-1;
                    char c = str[idx];
                    while (idx >= 0 && c && (isalpha(c) || isdigit(c))) {
                        if (--idx >= 0)
                            c = str[idx];
                    }

                    int len = lex_idx-idx-1;
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
                        vars[n_vars].datatype = MPR_DBL;
                        vars[n_vars].vec_len = 0;
                        vars[n_vars].assigned = 0;
                        vars[n_vars].public = public;
#if TRACING
                        printf("Stored new variable '%s' at index %i\n", vars[n_vars].name, n_vars);
#endif
                        tok.var = n_vars;
                        tok.datatype = MPR_DBL;
                        // special case: 'alive' tracks instance lifetime
                        if (strcmp(vars[n_vars].name, "alive")==0) {
                            inst_ctl = n_vars;
                            tok.vec_len = 1;
                            tok.vec_len_locked = 1;
                        }
                        else if (strcmp(vars[n_vars].name, "muted")==0) {
                            mute_ctl = n_vars;
                            tok.vec_len = 1;
                            tok.vec_len_locked = 1;
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
                if (tok.var != VAR_Y || out_assigned > 1)
                    allow_toktype |= (TOK_OP | TOK_CLOSE_PAREN | TOK_CLOSE_SQUARE | TOK_CLOSE_CURLY
                                      | TOK_COMMA | TOK_COLON | TOK_SEMICOLON);
                muted = 0;
                public = 0;
                break;
            case TOK_FN:
                if (tok.fn == FN_POOL) {
                    {FAIL_IF(op_idx < 2 || op[op_idx].toktype != TOK_OPEN_PAREN,
                             "Error: pool() can only appear inside an instance function.")}
                    {FAIL_IF(op[op_idx-1].toktype < TOK_FN || op[op_idx-1].toktype > TOK_IFN,
                             "Error: pool() can only appear inside an instance function.")}
                    if (op[op_idx-1].toktype != TOK_IFN) {
                        // upgrade previous token to instance function
                        op[op_idx-1].toktype = TOK_IFN;
                        switch (op[op_idx-1].fn) {
                            case VFN_MEAN:
#if TRACING
                                printf("Upgrading vector mean() to instanced mean()\n");
#endif
                                op[op_idx-1].ifn = (int)IFN_MEAN;
                                break;
//                            case FN_MAX:
//                                out[out_idx-1].ifn = (int)IFN_MAX;
//                                break;
//                            case FN_MIN:
//                                out[out_idx-1].ifn = (int)IFN_MIN;
//                                break;
//                            case VFN_SUM:
//                                out[out_idx-1].ifn = (int)IFN_SUM;
//                                break;
                            default:
                                {FAIL("Error: unknown instance function");}
                        }
                    }
                    tok.toktype = TOK_POOL;
                    tok.var = VAR_UNKNOWN;
                    GET_NEXT_TOKEN(tok);
                    {FAIL_IF(tok.toktype != TOK_OPEN_PAREN, "Error processing pool().");}
                    GET_NEXT_TOKEN(tok);
                    {FAIL_IF(tok.toktype != TOK_VAR || tok.var < VAR_X, "Error processing pool().");}
                    int slot = tok.var-VAR_X;
                    {FAIL_IF(slot >= n_ins, "Input slot index > number of sources.");}
                    tok.toktype = TOK_POOL;
                    tok.datatype = in_types[slot];
                    tok.vec_len = in_vec_lens[slot];
                    tok.vec_len_locked = 1;
                    PUSH_TO_OUTPUT(tok);
                    GET_NEXT_TOKEN(tok);
                    {FAIL_IF(tok.toktype != TOK_CLOSE_PAREN, "Error processing pool().");}
                    allow_toktype = TOK_CLOSE_PAREN;
                    break;
                }
                tok.datatype = fn_tbl[tok.fn].fn_int ? MPR_INT32 : MPR_DBL;
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
                    vars[n_vars].datatype = MPR_DBL;
                    vars[n_vars].vec_len = 1;
                    vars[n_vars].assigned = 1;

                    newtok.toktype = TOK_ASSIGN_USE;
                    newtok.var = n_vars;
                    ++n_vars;
                    newtok.datatype = MPR_DBL;
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
            case TOK_IFN:
                tok.var = VAR_UNKNOWN;
                PUSH_TO_OPERATOR(tok);
                allow_toktype = TOK_OPEN_PAREN;
                break;
            case TOK_VFN:
                tok.datatype = vfn_tbl[tok.vfn].fn_int ? MPR_INT32 : MPR_FLT;
                PUSH_TO_OPERATOR(tok);
                allow_toktype = TOK_OPEN_PAREN;
                break;
            case TOK_OPEN_PAREN:
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

                if (op_idx < 0)
                    break;

                // if operator stack[top] is tok_fn or tok_vfn, pop to output
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
                                    {FAIL_IF(out[out_idx-1].toktype != TOK_CONST,
                                             "variable history indices must include maximum value.");}
                                    switch (out[out_idx-1].datatype) {
#define TYPED_CASE(MTYPE, EL)                                                   \
                                        case MTYPE:                             \
                                            buffer_size = (int)out[out_idx-1].EL; \
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
                                    memcpy(&out[out_idx-1], &out[out_idx], sizeof(mpr_token_t));
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
                        // check for overloaded functions
                        if (arity == 1) {
                            if (op[op_idx].fn == FN_MIN) {
                                op[op_idx].toktype = TOK_VFN;
                                op[op_idx].vfn = VFN_MIN;
                            }
                            else if (op[op_idx].fn == FN_MAX) {
                                op[op_idx].toktype = TOK_VFN;
                                op[op_idx].vfn = VFN_MAX;
                            }
                        }
                        POP_OPERATOR_TO_OUTPUT();
                    }
                }
                else if (op[op_idx].toktype == TOK_VFN)
                    {POP_OPERATOR_TO_OUTPUT();}
                else if (op[op_idx].toktype == TOK_IFN) {
                    {FAIL_IF(out[out_idx].toktype != TOK_VAR || out[out_idx].var < VAR_X,
                             "Only input signals can be used as arguments to instance functions");}
                    // convert variable token to instance function
                    out[out_idx].toktype = TOK_IFN;
                    out[out_idx].ifn = op[op_idx].ifn;
                    if (out[out_idx].ifn == IFN_COUNT)
                        out[out_idx].datatype = MPR_INT32;
                    else if (MPR_INT32 == out[out_idx].datatype && !ifn_tbl[out[out_idx].ifn].fn_int)
                        out[out_idx].datatype = MPR_FLT;
                    POP_OPERATOR();
                    break;
                }
                // special case: if stack[top] is tok_assign, pop to output
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
                    op[op_idx].vec_len += out[out_idx].vec_len;
                    lock_vec_len(out, out_idx);
                }
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
                    ++op[op_idx].arity;
                    op[op_idx].vec_len += out[out_idx].vec_len;
                    lock_vec_len(out, out_idx);
                    POP_OPERATOR_TO_OUTPUT();
                }
                else {
                    // we do not need vectorizer token if vector length == 1
                    POP_OPERATOR();
                }
                vectorizing = 0;
                allow_toktype = (TOK_OP | TOK_CLOSE_PAREN | TOK_CLOSE_CURLY | TOK_COMMA
                                 | TOK_COLON | TOK_SEMICOLON);
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
                allow_toktype = TOK_CONST | TOK_VAR | TOK_TT | TOK_FN | TOK_VFN | TOK_IFN;
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
                allow_toktype = 0x2FFFFF;
                break;
            default:
                {FAIL("Unknown token type.");}
                break;
        }
#if (TRACING && DEBUG)
        printstack("OUTPUT STACK:  ", out, out_idx, vars, 0);
        printstack("OPERATOR STACK:", op, op_idx, vars, 0);
#endif
    }

    {FAIL_IF(allow_toktype & TOK_CONST || !out_assigned,
             "Expression has no output assignment.");}

    // check that all used-defined variables were assigned
    for (i = 0; i < n_vars; i++) {
        {FAIL_IF(!vars[i].assigned, "User-defined variable not assigned.");}
    }

    // finish popping operators to output, check for unbalanced parentheses
    while (op_idx >= 0 && op[op_idx].toktype < TOK_ASSIGN) {
        {FAIL_IF(op[op_idx].toktype == TOK_OPEN_PAREN,
                 "Unmatched parentheses or misplaced comma.");}
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
        {FAIL_IF(!op_idx && op[op_idx].toktype < TOK_ASSIGN,
                 "Malformed expression (8).");}
        PUSH_TO_OUTPUT(op[op_idx]);
        // check vector length and type
        {FAIL_IF(out[out_idx].toktype == TOK_ASSIGN_USE
                 && check_assign_type_and_len(out, out_idx, vars) == -1,
                 "Malformed expression (9).");}
        POP_OPERATOR();
    }

    // check vector length and type
    {FAIL_IF(check_assign_type_and_len(out, out_idx, vars) == -1,
             "Malformed expression (10).");}

#if (TRACING && DEBUG)
    printstack("--->OUTPUT STACK:  ", out, out_idx, vars, 0);
    printstack("--->OPERATOR STACK:", op, op_idx, vars, 0);
#endif

    // Check for maximum vector length used in stack
    for (i = 0; i < out_idx; i++) {
        if (out[i].vec_len > max_vector)
            max_vector = out[i].vec_len;
    }

    mpr_expr expr = malloc(sizeof(struct _mpr_expr));
    expr->len = out_idx + 1;
    expr->offset = 0;
    expr->inst_ctl = inst_ctl;
    expr->mute_ctl = mute_ctl;

    // copy tokens
    expr->tokens = malloc(sizeof(struct _token) * expr->len);
    memcpy(expr->tokens, &out, sizeof(struct _token) * expr->len);
    expr->start = expr->tokens;
    expr->vec_size = max_vector;
    expr->out_hist_size = -oldest_out+1;
    expr->in_hist_size = malloc(sizeof(uint8_t) * n_ins);
    for (i = 0; i < n_ins; i++)
        expr->in_hist_size[i] = -oldest_in[i] + 1;
    if (n_vars) {
        // copy user-defined variables
        expr->vars = malloc(sizeof(mpr_var_t)*n_vars);
        memcpy(expr->vars, vars, sizeof(mpr_var_t)*n_vars);
    }
    else
        expr->vars = NULL;

    expr->n_vars = n_vars;
#if TRACING
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

int mpr_expr_get_src_is_muted(mpr_expr expr, int idx)
{
    int i, found = 0, muted = 1;
    mpr_token_t *tok = expr->tokens;
    for (i = 0; i < expr->len; i++) {
        if (tok[i].toktype == TOK_VAR && tok[i].var == idx + VAR_X) {
            found = 1;
            muted &= tok[i].muted;
        }
    }
    return found && muted;
}

int mpr_expr_get_num_input_slots(mpr_expr expr)
{
    // actually need to return highest numbered input slot
    int i, count = -1;
    mpr_token_t *tok = expr->tokens;
    for (i = 0; i < expr->len; i++) {
        if (tok[i].toktype == TOK_VAR && tok[i].var > count)
            count = tok[i].var;
    }
    return count >= VAR_X ? count - VAR_X + 1 : 0;
}

int mpr_expr_get_manages_inst(mpr_expr expr)
{
    return expr ? expr->inst_ctl >= 0 : 0;
}

#if TRACING
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
        TYPED_CASE(MPR_FLT, "%f, ", f)
        TYPED_CASE(MPR_DBL, "%f, ", d)
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

#if TRACING
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
            stk[top][i].EL SYM stk[top][i].EL;  \
        break;

#define BINARY_OP_CASE(OP, SYM, EL)                                 \
    case OP:                                                        \
        for (i = 0; i < tok->vec_len; i++)                          \
            stk[top][i].EL = stk[top][i].EL SYM stk[top+1][i].EL;   \
        break;

#define CONDITIONAL_CASES(EL)                       \
    case OP_IF_ELSE:                                \
        for (i = 0; i < tok->vec_len; i++) {        \
            if (!stk[top][i].EL)                    \
                stk[top][i].EL = stk[top+1][i].EL;  \
        }                                           \
        break;                                      \
    case OP_IF_THEN_ELSE:                           \
        for (i = 0; i < tok->vec_len; i++) {        \
            if (stk[top][i].EL)                     \
                stk[top][i].EL = stk[top+1][i].EL;  \
            else                                    \
                stk[top][i].EL = stk[top+2][i].EL;  \
        }                                           \
        break;

#define OP_CASES_META(EL)                                   \
    BINARY_OP_CASE(OP_ADD, +, EL);                          \
    BINARY_OP_CASE(OP_SUBTRACT, -, EL);                     \
    BINARY_OP_CASE(OP_MULTIPLY, *, EL);                     \
    BINARY_OP_CASE(OP_DIVIDE, /, EL);                       \
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
            stk[top][i].EL = ((TYPE*)a)[i+tok->vec_idx];    \
        break;
#define WEIGHTED_ADD(MTYPE, TYPE, EL)                                                             \
    case MTYPE:                                                                                   \
        for (i = 0; i < tok->vec_len; i++)                                                        \
            stk[top][i].EL = stk[top][i].EL * weight + ((TYPE*)a)[i+tok->vec_idx] * (1 - weight); \
        break;
#define COPY_TO_STACK(SRC)                                                  \
    if (!tok->hist)                                                         \
        ++top;                                                              \
    else {                                                                  \
        switch (last_type) {                                                \
            case MPR_INT32:                                                 \
                hidx = stk[top][0].i;                                       \
                break;                                                      \
            case MPR_FLT:                                                   \
                hidx = (int)stk[top][0].f;                                  \
                weight = fabsf(stk[top][0].f - hidx);                       \
                break;                                                      \
            case MPR_DBL:                                                   \
                hidx = (int)stk[top][0].d;                                  \
                weight = fabs(stk[top][0].d - hidx);                        \
                break;                                                      \
            default:                                                        \
                goto error;                                                 \
        }                                                                   \
    }                                                                       \
    dims[top] = tok->vec_len;                                               \
    mpr_value_buffer b = &SRC->inst[inst_idx % SRC->num_inst];              \
    int idx = ((b->pos + SRC->mlen + hidx) % SRC->mlen);                    \
    if (idx < 0) idx = SRC->mlen + idx;                                     \
    void *a = (b->samps + idx * SRC->vlen * mpr_type_get_size(SRC->type));  \
    switch (SRC->type) {                                                    \
        COPY_TYPED(MPR_INT32, int, i)                                       \
        COPY_TYPED(MPR_FLT, float, f)                                       \
        COPY_TYPED(MPR_DBL, double, d)                                      \
        default:                                                            \
            goto error;                                                     \
    }                                                                       \
    if (weight) {                                                           \
        --idx;                \
        if (idx < 0) idx = SRC->mlen + idx;                                 \
        a = (b->samps + idx * SRC->vlen * mpr_type_get_size(SRC->type));    \
        switch (SRC->type) {                                                \
            WEIGHTED_ADD(MPR_INT32, int, i)                                 \
            WEIGHTED_ADD(MPR_FLT, float, f)                                 \
            WEIGHTED_ADD(MPR_DBL, double, d)                                \
            default:                                                        \
                goto error;                                                 \
        }                                                                   \
    }                                                                       \

int mpr_expr_eval(mpr_expr expr, mpr_value *v_in, mpr_value *v_vars,
                  mpr_value v_out, mpr_time *t, mpr_type *types, int inst_idx)
{
    if (!expr) {
#if TRACING
        printf(" no expression to evaluate!\n");
#endif
        return 0;
    }

    mpr_token_t *tok = expr->start;
    int len = expr->len;
    int status = 1, alive = 1, muted = 0;
    mpr_value_buffer b_out = v_out ? &v_out->inst[inst_idx] : 0;
    if (v_out && b_out->pos >= 0) {
        tok += expr->offset;
        len -= expr->offset;
    }

    if (v_vars) {
        if (expr->inst_ctl >= 0) {
            // recover instance state
            mpr_value v = *v_vars + expr->inst_ctl;
            double *d = v->inst[inst_idx].samps;
            alive = (0 != d[0]);
        }
        if (expr->mute_ctl >= 0) {
            // recover mute state
            mpr_value v = *v_vars + expr->mute_ctl;
            double *d = v->inst[inst_idx].samps;
            muted = (0 != d[0]);
        }
    }

    mpr_expr_val_t stk[len][expr->vec_size];
    int dims[len];

    int i, j, k, top = -1, count = 0, can_advance = 1, pool = VAR_UNKNOWN;
    mpr_type last_type = 0;

    if (v_out) {
        // init types
        if (types)
            memset(types, MPR_NULL, v_out->vlen);
        /* Increment index position of output data structure. */
        b_out->pos = (b_out->pos + 1) % v_out->mlen;
    }

    for (i = 0; i < expr->n_vars; i++)
        expr->vars[i].assigned = 0;

    while (count < len && tok->toktype != TOK_END) {
        switch (tok->toktype) {
        case TOK_POOL: {
            // we only allow pooling inputs for now
            if (tok->var < VAR_X)
                goto error;
            pool = tok->var;
#if TRACING
            printf("pooling variable x%d\n", pool-VAR_X);
#ifdef DEBUG
            mpr_value_print(v_in[pool-VAR_X], -1);
#endif
#endif
            break;
        }
        case TOK_CONST:
            ++top;
            dims[top] = tok->vec_len;
            switch (tok->datatype) {
#define TYPED_CASE(MTYPE, EL)                           \
                case MTYPE:                             \
                    for (i = 0; i < tok->vec_len; i++)  \
                        stk[top][i].EL = tok->EL;       \
                    break;
                TYPED_CASE(MPR_INT32, i)
                TYPED_CASE(MPR_FLT, f)
                TYPED_CASE(MPR_DBL, d)
#undef TYPED_CASE
                default:
                    goto error;
            }
#if TRACING
            printf("loading constant ");
            print_stack_vec(stk[top], tok->datatype, tok->vec_len);
            printf("\n");
#endif
            break;
        case TOK_VAR: {
            int hidx = 0;
            float weight = 0.f;
#if TRACING
            int mlen = 0;
            if (tok->var == VAR_Y) {
                printf("loading variable y");
                mlen = v_out->mlen;
            }
            else if (tok->var >= VAR_X) {
                printf("loading variable x%d", tok->var-VAR_X);
                mlen = v_in ? v_in[tok->var-VAR_X]->mlen : 0;
            }

            if (tok->hist) {
                switch (last_type) {
                    case MPR_INT32:
                        printf("{N=%d}", mlen ? stk[top][0].i % mlen : stk[top][0].i);
                        break;
                    case MPR_FLT:
                        printf("{N=%f}", mlen ? fmodf(stk[top][0].f, (float)mlen) : stk[top][0].f);
                        break;
                    case MPR_DBL:
                        printf("{N=%f}", mlen ? fmod(stk[top][0].d, (double)mlen) : stk[top][0].d);
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
            }
            else if (v_vars) {
                // TODO: allow other data types?
                if (!tok->hist)
                    ++top;
                dims[top] = tok->vec_len;
                mpr_value v = *v_vars + tok->var;
                double *d = v->inst[inst_idx].samps;
                for (i = 0; i < tok->vec_len; i++)
                    stk[top][i].d = d[i+tok->vec_idx];
            }
            else
                goto error;
#if TRACING
            print_stack_vec(stk[top], tok->datatype, tok->vec_len);
            printf(" \n");
#endif
            break;
        }
        case TOK_TT: {
            int hidx = 0;
            double weight = 0.0;
            double t_d;
            if (!tok->hist)
                ++top;
            dims[top] = tok->vec_len;
            mpr_value_buffer b;
#if TRACING
            if (tok->var == VAR_Y)
                printf("loading timetag t_y");
            else if (tok->var >= VAR_X)
                printf("loading timetag t_x%d", tok->var-VAR_X);
            else if (v_vars)
                printf("loading timetag t_%s", expr->vars[tok->var].name);

            if (tok->hist) {
                switch (last_type) {
                    case MPR_INT32:
                        printf("{N=%d}", stk[top][0].i);
                        break;
                    case MPR_FLT:
                        printf("{N=%f}", stk[top][0].f);
                        break;
                    case MPR_DBL:
                        printf("{N=%f}", stk[top][0].d);
                        break;
                    default:
                        goto error;
                }
            }
#endif
            if (tok->hist) {
                switch (last_type) {
                    case MPR_INT32:
                        hidx = stk[top][0].i;
                        break;
                    case MPR_FLT:
                        hidx = (int)stk[top][0].f;
                        weight = fabsf(stk[top][0].f - hidx);
                        break;
                    case MPR_DBL:
                        hidx = (int)stk[top][0].d;
                        weight = fabs(stk[top][0].d - hidx);
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
#if TRACING
            printf(" as double %f\n", t_d);
#endif
            for (i = 0; i < tok->vec_len; i++)
                stk[top][i].d = t_d;
            break;
        }
        case TOK_OP:
            top -= op_tbl[tok->op].arity-1;
            dims[top] = tok->vec_len;
#if TRACING
            if (tok->op == OP_IF_THEN_ELSE || tok->op == OP_IF_ELSE) {
                printf("IF ");
                print_stack_vec(stk[top], tok->datatype, tok->vec_len);
                printf(" THEN ");
                if (tok->op == OP_IF_ELSE) {
                    print_stack_vec(stk[top], tok->datatype, tok->vec_len);
                    printf(" ELSE ");
                    print_stack_vec(stk[top+1], tok->datatype, tok->vec_len);
                }
                else {
                    print_stack_vec(stk[top+1], tok->datatype, tok->vec_len);
                    printf(" ELSE ");
                    print_stack_vec(stk[top+2], tok->datatype, tok->vec_len);
                }
            }
            else {
                print_stack_vec(stk[top], tok->datatype, tok->vec_len);
                printf(" %s%c ", op_tbl[tok->op].name, tok->datatype);
                print_stack_vec(stk[top+1], tok->datatype, tok->vec_len);
            }
#endif
            switch (tok->datatype) {
                case MPR_INT32: {
                    switch (tok->op) {
                            OP_CASES_META(i);
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
                        case OP_MODULO:
                            for (i = 0; i < tok->vec_len; i++)
                                stk[top][i].f = fmodf(stk[top][i].f, stk[top+1][i].f);
                            break;
                        default: goto error;
                    }
                    break;
                }
                case MPR_DBL: {
                    switch (tok->op) {
                        OP_CASES_META(d);
                        case OP_MODULO:
                            for (i = 0; i < tok->vec_len; i++)
                                stk[top][i].d = fmod(stk[top][i].d, stk[top+1][i].d);
                            break;
                        default: goto error;
                    }
                    break;
                }
                default:
                    goto error;
            }
#if TRACING
            printf(" = ");
            print_stack_vec(stk[top], tok->datatype, tok->vec_len);
            printf(" \n");
#endif
            break;
        case TOK_FN:
            top -= fn_tbl[tok->fn].arity-1;
            dims[top] = tok->vec_len;
#if TRACING
            printf("%s%c(", fn_tbl[tok->fn].name, tok->datatype);
            for (i = 0; i < fn_tbl[tok->fn].arity; i++) {
                print_stack_vec(stk[top+i], tok->datatype, tok->vec_len);
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
                        stk[top][i].EL = ((FN##_arity0*)fn_tbl[tok->fn].FN)();  \
                    break;                                                      \
                case 1:                                                         \
                    for (i = 0; i < tok->vec_len; i++)                          \
                        stk[top][i].EL = (((FN##_arity1*)fn_tbl[tok->fn].FN)    \
                                          (stk[top][i].EL));                    \
                    break;                                                      \
                case 2:                                                         \
                    for (i = 0; i < tok->vec_len; i++)                          \
                        stk[top][i].EL = (((FN##_arity2*)fn_tbl[tok->fn].FN)    \
                                          (stk[top][i].EL, stk[top+1][i].EL));  \
                    break;                                                      \
                case 3:                                                         \
                    for (i = 0; i < tok->vec_len; i++)                          \
                        stk[top][i].EL = (((FN##_arity3*)fn_tbl[tok->fn].FN)    \
                                          (stk[top][i].EL, stk[top+1][i].EL,    \
                                           stk[top+2][i].EL));                  \
                    break;                                                      \
                case 4:                                                         \
                    for (i = 0; i < tok->vec_len; i++)                          \
                        stk[top][i].EL = (((FN##_arity4*)fn_tbl[tok->fn].FN)    \
                                          (stk[top][i].EL, stk[top+1][i].EL,    \
                                           stk[top+2][i].EL, stk[top+3][i].EL));\
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
#if TRACING
            printf(" = ");
            print_stack_vec(stk[top], tok->datatype, tok->vec_len);
            printf(" \n");
#endif
            break;
        case TOK_VFN:
            top -= vfn_tbl[tok->vfn].arity-1;
#if TRACING
            printf("%s%c(", vfn_tbl[tok->vfn].name, tok->datatype);
            for (i = 0; i < vfn_tbl[tok->vfn].arity; i++) {
                print_stack_vec(stk[top], tok->datatype, dims[top]);
                printf(", ");
            }
            printf("\b\b)");
#endif
            switch (tok->datatype) {
#define TYPED_CASE(MTYPE, FN, EL)                                                   \
            case MTYPE:                                                             \
                switch (vfn_tbl[tok->vfn].arity) {                                  \
                    case 1:                                                         \
                        stk[top][0].EL = (((v##FN##_arity1*)vfn_tbl[tok->vfn].FN)   \
                                          (stk[top], dims[top]));                   \
                        for (i = 1; i < tok->vec_len; i++)                          \
                            stk[top][i].EL = stk[top][0].EL;                        \
                        break;                                                      \
                    default: goto error;                                            \
                }                                                                   \
                break;
            TYPED_CASE(MPR_INT32, fn_int, i)
            TYPED_CASE(MPR_FLT, fn_flt, f)
            TYPED_CASE(MPR_DBL, fn_dbl, d)
#undef TYPED_CASE
            default:
                break;
            }
            dims[top] = tok->vec_len;
#if TRACING
            printf(" = ");
            print_stack_vec(stk[top], tok->datatype, tok->vec_len);
            printf(" \n");
#endif
            break;
        case TOK_IFN:
            if (tok->var < VAR_X)
                goto error;
//            top -= ifn_tbl[tok->ifn].arity-1;
#if TRACING
            printf("%s(x%d)", ifn_tbl[tok->ifn].name, tok->var - VAR_X);
#endif
            mpr_value v = v_in[tok->var - VAR_X];
            switch (tok->datatype) {
#define TYPED_CASE(MTYPE, FN, EL)                                               \
            case MTYPE:                                                         \
                switch (ifn_tbl[tok->ifn].arity) {                              \
                    case 0:                                                     \
                        (((i##FN##_arity1*)ifn_tbl[tok->ifn].FN)(stk[top], v)); \
                        break;                                                  \
                    default: goto error;                                        \
                }                                                               \
                break;
            TYPED_CASE(MPR_INT32, fn_int, i)
            TYPED_CASE(MPR_FLT, fn_flt, f)
            TYPED_CASE(MPR_DBL, fn_dbl, d)
#undef TYPED_CASE
            default:
                break;
            }
            dims[top] = tok->vec_len;
#if TRACING
            printf(" = ");
            print_stack_vec(stk[top], tok->datatype, tok->vec_len);
            printf(" \n");
#endif
            break;
        case TOK_VECTORIZE:
            // don't need to copy vector elements from first token
            top -= tok->arity-1;
            k = dims[top];
            switch (tok->datatype) {
#define TYPED_CASE(MTYPE, EL)                                       \
                case MTYPE:                                         \
                    for (i = 1; i < tok->arity; i++) {              \
                        for (j = 0; j < dims[top+1]; j++)           \
                            stk[top][k++].EL = stk[top+i][j].EL;    \
                    }                                               \
                    break;
                TYPED_CASE(MPR_INT32, i)
                TYPED_CASE(MPR_FLT, f)
                TYPED_CASE(MPR_DBL, d)
#undef TYPED_CASE
                default:
                    goto error;
            }
            dims[top] = tok->vec_len;
#if TRACING
            printf("built %u-element vector: ", tok->vec_len);
            print_stack_vec(stk[top], tok->datatype, tok->vec_len);
            printf(" \n");
#endif
            break;
        case TOK_ASSIGN:
        case TOK_ASSIGN_USE:
            can_advance = 0;
        case TOK_ASSIGN_CONST:
#if TRACING
            if (VAR_Y == tok->var)
                printf("assigning values to y{%d}[%u] (%s x %u)\n",
                       tok->hist ? stk[top-1][0].i : 0, tok->vec_idx, type_name(tok->datatype),
                       tok->vec_len);
            else
                printf("assigning values to %s{%d}[%u] (%s x %u)\n", expr->vars[tok->var].name,
                       tok->hist ? stk[top-1][0].i : 0, tok->vec_idx, type_name(tok->datatype),
                       tok->vec_len);
#endif
            if (tok->var == VAR_Y) {
                if (!alive)
                    break;
                status |=  muted ? EXPR_MUTED_UPDATE : EXPR_UPDATE;
                can_advance = 0;
                if (!v_out)
                    return status;
                int idx = (b_out->pos + v_out->mlen + (tok->hist ? stk[top-1][0].i : 0)) % v_out->mlen;
                if (idx < 0)
                    idx = v_out->mlen + idx;
                void *v = (b_out->samps + idx * v_out->vlen * mpr_type_get_size(v_out->type));

                switch (v_out->type) {
#define TYPED_CASE(MTYPE, TYPE, EL)                             \
                    case MTYPE:                                 \
                        for (i = 0; i < tok->vec_len; i++)      \
                            ((TYPE*)v)[i + tok->vec_idx]        \
                                = stk[top][i + tok->offset].EL; \
                        break;
                    TYPED_CASE(MPR_INT32, int, i)
                    TYPED_CASE(MPR_FLT, float, f)
                    TYPED_CASE(MPR_DBL, double, d)
#undef TYPED_CASE
                    default:
                        goto error;
                }

                if (types) {
                    for (i = tok->vec_idx; i < tok->vec_idx + tok->vec_len; i++)
                        types[i] = tok->datatype;
                }

                // Also copy time from input
                if (t) {
                    mpr_time *tvar = &b_out->times[idx];
                    memcpy(tvar, t, sizeof(mpr_time));
                }
            }
            else if (tok->var >= 0 && tok->var < N_USER_VARS) {
                if (!v_vars)
                    goto error;
                // passed the address of an array of mpr_value structs
                mpr_value v = *v_vars + tok->var;
                mpr_value_buffer b = &v->inst[inst_idx];

                double *d = b->samps;
                for (i = 0; i < tok->vec_len; i++)
                    d[i + tok->vec_idx] = stk[top][i + tok->offset].d;

                // Also copy time from input
                if (t)
                    memcpy(b->times, t, sizeof(mpr_time));

                expr->vars[tok->var].assigned = 1;

                if (tok->var == expr->inst_ctl) {
                    if (alive && stk[top][0].d == 0) {
                        if (status & EXPR_UPDATE)
                            status |= EXPR_RELEASE_AFTER_UPDATE;
                        else
                            status |= EXPR_RELEASE_BEFORE_UPDATE;
                    }
                    alive = stk[top][0].d != 0;
                    break;
                }
                else if (tok->var == expr->mute_ctl) {
                    muted = stk[top][0].d != 0;
                    break;
                }
            }
            else
                goto error;

            /* If assignment was constant or history initialization, move expr
             * start token pointer so we don't evaluate this section again. */
            if (tok->hist || can_advance) {
#if TRACING
                printf("moving expr offset to %ld\n", tok - expr->start + 1);
#endif
                expr->offset = tok - expr->start + 1;
            }
            else
                can_advance = 0;
            break;
        case TOK_ASSIGN_TT:
            if (tok->var != VAR_Y || tok->hist == 0)
                goto error;
#if TRACING
            printf("assigning timetag to t_y{%d}\n", tok->hist ? stk[top-1][0].i : 0);
#endif
            if (!v_out)
                return status;
            int idx = (b_out->pos + v_out->mlen + (tok->hist ? stk[top-1][0].i : 0)) % v_out->mlen;
            if (idx < 0)
                idx = v_out->mlen + idx;
            mpr_time_set_dbl(&b_out->times[idx], stk[top][0].d);
            /* If assignment was constant or history initialization, move expr
             * start token pointer so we don't evaluate this section again. */
            if (tok->hist || can_advance) {
#if TRACING
                printf("moving expr offset to %ld\n", tok - expr->start + 1);
#endif
                expr->offset = tok - expr->start + 1;
            }
            else
                can_advance = 0;
            break;
        default: goto error;
        }
        if (tok->casttype && tok->toktype < TOK_ASSIGN) {
#if TRACING
            printf("casting from %s to %s (%c)\n", type_name(tok->datatype),
                   type_name(tok->casttype), tok->casttype);
#endif
            // need to cast to a different type
            switch (tok->datatype) {
#define TYPED_CASE(MTYPE, EL, MTYPE1, TYPE1, EL1, MTYPE2, TYPE2, EL2)       \
                case MTYPE:                                                 \
                    switch (tok->casttype) {                                \
                        case MTYPE1:                                        \
                            for (i = 0; i < tok->vec_len; i++)              \
                                stk[top][i].EL1 = (TYPE1)stk[top][i].EL;    \
                            break;                                          \
                        case MTYPE2:                                        \
                            for (i = 0; i < tok->vec_len; i++)              \
                                stk[top][i].EL2 = (TYPE2)stk[top][i].EL;    \
                            break;                                          \
                        default:                                            \
                            goto error;                                     \
                    }                                                       \
                    break;
                TYPED_CASE(MPR_INT32, i, MPR_FLT, float, f, MPR_DBL, double, d)
                TYPED_CASE(MPR_FLT, f, MPR_INT32, int, i, MPR_DBL, double, d)
                TYPED_CASE(MPR_DBL, d, MPR_INT32, int, i, MPR_FLT, float, f)
#undef TYPED_CASE
                default:
                    goto error;
            }
        }
        last_type = tok->datatype;
        ++tok;
        ++count;
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
                    ((TYPE*)v)[i] = stk[top][i].EL; \
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
