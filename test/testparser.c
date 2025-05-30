#include "../src/bitflags.h"
#include "../src/expression.h"
#include "../src/mpr_time.h"
#include "../src/value.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <time.h>
#include <mapper/mapper.h>

#include <string.h>
#ifdef WIN32
#include <io.h>
#else
#include <sys/time.h>
#include <unistd.h>
#endif
#include <assert.h>

#define MAX_STR_LEN 256
#define MAX_NUM_SRC 3
#define MAX_NUM_DST 1
#define MAX_SRC_ARRAY_LEN 3
#define MAX_DST_ARRAY_LEN 6
#define MAX_VARS 8

int verbose = 1;
char str[MAX_STR_LEN];
mpr_expr e;
int iterations = 20000;
int expression_count = 1;
int token_count = 0;
int update_count;
int start_index = -1;
int stop_index = -1;

int src_int[MAX_SRC_ARRAY_LEN], dst_int[MAX_DST_ARRAY_LEN], expect_int[MAX_DST_ARRAY_LEN];
float src_flt[MAX_SRC_ARRAY_LEN], dst_flt[MAX_DST_ARRAY_LEN], expect_flt[MAX_DST_ARRAY_LEN];
double src_dbl[MAX_SRC_ARRAY_LEN], dst_dbl[MAX_DST_ARRAY_LEN], expect_dbl[MAX_DST_ARRAY_LEN];
mpr_type expect_type[MAX_DST_ARRAY_LEN];
double then, now;
double total_elapsed_time = 0;

mpr_time time_in = {0, 0}, time_out = {0, 0};

/* evaluation buffer */
mpr_expr_eval_buffer eval_buff = 0;

/* signal_history structures */
mpr_value inh[MAX_SRC_ARRAY_LEN], outh, user_vars[MAX_VARS];
mpr_type src_types[MAX_NUM_SRC], dst_type;
unsigned int src_lens[MAX_NUM_SRC], n_sources, dst_len;

#ifdef WIN32
#include <windows.h>
void usleep(__int64 usec)
{
    HANDLE timer;
    LARGE_INTEGER ft;

    /* Convert to 100 nanosecond interval, negative value indicates relative time */
    ft.QuadPart = -(10*usec);

    timer = CreateWaitableTimer(NULL, TRUE, NULL);
    SetWaitableTimer(timer, &ft, 0, NULL, NULL, 0);
    WaitForSingleObject(timer, INFINITE);
    CloseHandle(timer);
}
#endif

static void eprintf(const char *format, ...)
{
    va_list args;
    if (!verbose)
        return;
    va_start(args, format);
    vprintf(format, args);
    va_end(args);
}

typedef struct _expr_var {
    char *name;
    mpr_type datatype;
    uint8_t vec_len;
    uint8_t flags;
} expr_var_t, *expr_var;

typedef struct _estack
{
    void *tokens;
    uint8_t offset;
    uint8_t num_tokens;
    uint8_t vec_len;
} estack_t, *estack;

struct _mpr_expr
{
    estack stack;
    expr_var_t *vars;
    uint16_t *src_mlen;
    uint16_t max_src_mlen;
    uint16_t dst_mlen;
    uint8_t num_vars;
    int8_t inst_ctl;
    int8_t mute_ctl;
    int8_t num_src;
    int8_t own_stack;
};

/*! A helper function to seed the random number generator. */
static void seed_srand()
{
    unsigned int s;
    double d;
    mpr_time t;

#ifndef WIN32
    FILE *f = fopen("/dev/urandom", "rb");
    if (f) {
        if (1 == fread(&s, 4, 1, f)) {
            srand(s);
            fclose(f);
            return;
        }
        fclose(f);
    }
#endif

    mpr_time_set(&t, MPR_NOW);
    d = mpr_time_as_dbl(t);
    s = (unsigned int)((d - (unsigned long)d) * 100000);
    srand(s);
}

int random_int()
{
    return rand() * (rand() % 2 ? 1 : -1);
}

/* value returned by rand() is guaranteed to be at least 15 bits so we will
 * construct a 32-bit field using concatenation */
float random_flt()
{
    uint32_t buffer = 0;
    float *f = (float*)&buffer;
    do {
        int i;
        for (i = 0; i < 4; i++) {
            int random = rand();
            buffer = (buffer << 8) | (random & 0xFF);
        }
    } while (*f != *f); /* exclude NaN */
    return *f;
}

/* value returned by rand() is guaranteed to be at least 15 bits so we will
 * construct a 64-bit field using concatenation */
double random_dbl()
{
    uint64_t buffer = 0;
    double *d = (double*)&buffer;
    do {
        int i;
        for (i = 0; i < 8; i++) {
            int random = rand();
            buffer = (buffer << 8) | (random & 0xFF);
        }
    } while (*d != *d);
    return *d;
}

#define TYPE_ERROR 1
#define VALUE_ERROR 2

int check_result(mpr_bitflags updated, mpr_value value, int check)
{
    int i, error = 0, error_idx = -1, len = mpr_value_get_vlen(value);
    mpr_type type = mpr_value_get_type(value);
    if (!updated || len < 1)
        return 1;

    switch (dst_type) {
        case MPR_INT32:
            memcpy(dst_int, mpr_value_get_value(outh, 0, 0), sizeof(int) * len);
            break;
        case MPR_FLT:
            memcpy(dst_flt, mpr_value_get_value(outh, 0, 0), sizeof(float) * len);
            break;
        default:
            memcpy(dst_dbl, mpr_value_get_value(outh, 0, 0), sizeof(double) * len);
            break;
    }
    memcpy(&time_out, mpr_value_get_time(outh, 0, 0), sizeof(mpr_time));

    eprintf("Got: ");
    if (len > 1)
        eprintf("[");

    for (i = 0; i < len; i++) {
        if (!mpr_bitflags_get(updated, i)) {
            eprintf("NULL, ");
            if (expect_type[i] != MPR_NULL)
                error_idx = i;
        }
        else {
            switch (type) {
                case MPR_INT32:
                    eprintf("%d, ", dst_int[i]);
                    if (expect_type[i] != MPR_INT32)
                        error = TYPE_ERROR;
                    else if (check && dst_int[i] != expect_int[i])
                        error = VALUE_ERROR;
                    else
                        break;
                    error_idx = i;
                    break;
                case MPR_FLT:
                    eprintf("%g, ", dst_flt[i]);
                    if (expect_type[i] != MPR_FLT)
                        error = TYPE_ERROR;
                    else if (check && dst_flt[i] != expect_flt[i] && expect_flt[i] == expect_flt[i])
                        error = VALUE_ERROR;
                    else
                        break;
                    error_idx = i;
                    break;
                case MPR_DBL:
                    eprintf("%g, ", dst_dbl[i]);
                    if (expect_type[i] != MPR_DBL)
                        error = TYPE_ERROR;
                    else if (check && dst_dbl[i] != expect_dbl[i] && expect_dbl[i] == expect_dbl[i])
                        error = VALUE_ERROR;
                    else
                        break;
                    error_idx = i;
                    break;
                default:
                    eprintf("\nTYPE ERROR\n");
                    return 1;
            }
        }
    }

    if (len > 1)
        eprintf("\b\b]");
    else
        eprintf("\b\b");
    if (!check) {
        eprintf("... not checked\n");
        return 0;
    }
    if (error) {
        eprintf("... error at index %d ", error_idx);
        if (error == TYPE_ERROR) {
            switch (expect_type[error_idx]) {
                case MPR_NULL:  eprintf("(expected NULL)\n");   break;
                case MPR_INT32: eprintf("(expected int)\n");    break;
                case MPR_FLT:   eprintf("(expected float)\n");  break;
                case MPR_DBL:   eprintf("(expected double)\n"); break;
            }
        }
        else {
            switch (type) {
                case MPR_INT32: eprintf("(expected %d)\n", expect_int[error_idx]);  break;
                case MPR_FLT:   eprintf("(expected %g)\n", expect_flt[error_idx]);  break;
                case MPR_DBL:   eprintf("(expected %g)\n", expect_dbl[error_idx]);  break;
            }
        }
        return 1;
    }
    eprintf("... OK\n");
    return 0;
}

void set_expr_str(const char *expr_str)
{
    snprintf(str, MAX_STR_LEN, "%s", expr_str);
}

void setup_test_multisource(int _n_sources, mpr_type *_src_types, int *_src_lens,
                            mpr_type _dst_type, int _dst_len)
{
    int i;
    assert(_n_sources <= MAX_NUM_SRC);
    n_sources = _n_sources;
    for (i = 0; i < _n_sources; i++) {
        src_types[i] = _src_types[i];
        assert(_src_lens[i] <= MAX_SRC_ARRAY_LEN);
        src_lens[i] = _src_lens[i];
    }
    dst_len = _dst_len;
    dst_type = _dst_type;

    /* default expected types, can override in test before running parse_and_eval() */
    for (i = 0; i < _dst_len; i++)
        expect_type[i] = _dst_type;
}

void setup_test(mpr_type in_type, int in_len, mpr_type out_type, int out_len)
{
    setup_test_multisource(1, &in_type, &in_len, out_type, out_len);
}

#define PARSE_SUCCESS   0x00
#define PARSE_FAILURE   0x01
#define EVAL_SUCCESS    0x00
#define EVAL_FAILURE    0x02

int parse_and_eval(int expectation, int max_tok, int check, int exp_updates)
{
    /* clear output arrays */
    int i, j, result = 0, mlen, status;
    mpr_bitflags updated_values = 0;

    if (start_index >= 0 && (expression_count < start_index || expression_count > stop_index)) {
        ++expression_count;
        return 0;
    }
    if (verbose) {
        printf("***************** Expression %d *****************\n", expression_count++);
        printf("Parsing string '%s'\n", str);
    }
    else {
        printf("\rExpression %d", expression_count++);
        fflush(stdout);
    }
    e = mpr_expr_new_from_str(str, n_sources, src_types, src_lens, 1, &dst_type, &dst_len);
    if (!e) {
        eprintf("Parser FAILED (expression %d)\n", expression_count - 1);
        if (!(PARSE_FAILURE & expectation))
            result = 1;
        goto fail;
    }
    else if (PARSE_FAILURE & expectation) {
        eprintf("Error: expected parser to FAIL\n");
        result = 1;
        goto free;
    }
    mpr_expr_realloc_eval_buffer(e, eval_buff);
    mpr_time_set(&time_in, MPR_NOW);
    for (i = 0; i < n_sources; i++) {
        mlen = mpr_expr_get_src_mlen(e, i);
        mpr_value_realloc(inh[i], src_lens[i], src_types[i], mlen, 1, 1);
        mpr_value_reset_inst(inh[i], 0, time_in);
        switch (src_types[i]) {
            case MPR_INT32:
                mpr_value_set_next(inh[i], 0, src_int, &time_in);
                break;
            case MPR_FLT:
                mpr_value_set_next(inh[i], 0, src_flt, &time_in);
                break;
            case MPR_DBL:
                mpr_value_set_next(inh[i], 0, src_dbl, &time_in);
                break;
            default:
                assert(0);
        }
    }
    mlen = mpr_expr_get_dst_mlen(e, 0);
    mpr_value_realloc(outh, dst_len, dst_type, mlen, 1, 1);
    mpr_value_reset_inst(outh, 0, time_in);

    if (mpr_expr_get_num_vars(e) > MAX_VARS) {
        eprintf("Maximum variables exceeded.\n");
        if (!(PARSE_FAILURE & expectation))
            result = 1;
        goto fail;
    }

    /* reallocate variable value histories */
    for (i = 0; i < e->num_vars; i++) {
        int vlen = mpr_expr_get_var_vlen(e, i);
        mpr_type type = mpr_expr_get_var_type(e, i);
        mpr_value_realloc(user_vars[i], vlen, type, 1, 1, 0);
        mpr_value_reset_inst(user_vars[i], 0, time_in);
        mpr_value_incr_idx(user_vars[i], 0);
    }

    eprintf("Parser returned %d tokens...", e->stack->num_tokens);
    if (max_tok && e->stack->num_tokens > max_tok) {
        eprintf(" (expected %d)\n", max_tok);
        result = 1;
        goto free;
    }
    else {
        eprintf(" OK\n");
    }

#ifdef DEBUG
    if (verbose) {
        mpr_expr_print(e);
    }
#endif

    token_count += e->stack->num_tokens;

    update_count = 0;
    then = mpr_get_current_time();

    updated_values = mpr_bitflags_new(MAX_DST_ARRAY_LEN);

    eprintf("Try evaluation once... ");
    status = mpr_expr_eval(e, eval_buff, inh, user_vars, outh, &time_in, 0);

    if (!status) {
        eprintf("FAILED.\n");
        if (!(EVAL_FAILURE & expectation))
            result = 1;
        goto free;
    }
    else {
        eprintf("OK\n");
        if (status & EXPR_UPDATE) {
            if (EVAL_FAILURE & expectation) {
                eprintf("Error: expected evaluator FAIL\n");
                result = 1;
                goto free;
            }
            ++update_count;
            mpr_bitflags_clear(updated_values);
            mpr_bitflags_cpy(updated_values, mpr_value_get_elements_known(outh, 0));
        }
    }

    eprintf("Calculate expression %i more times... ", iterations-1);
    fflush(stdout);
    i = iterations-1;
    while (i--) {
        /* update timestamp */
        mpr_time_set(&time_in, MPR_NOW);
        /* copy src values */
        for (j = 0; j < n_sources; j++) {
            switch (mpr_value_get_type(inh[j])) {
                case MPR_INT32:
                    mpr_value_set_next(inh[j], 0, src_int, &time_in);
                    break;
                case MPR_FLT:
                    mpr_value_set_next(inh[j], 0, src_flt, &time_in);
                    break;
                case MPR_DBL:
                    mpr_value_set_next(inh[j], 0, src_dbl, &time_in);
                    break;
                default:
                    assert(0);
            }
        }
        status = mpr_expr_eval(e, eval_buff, inh, user_vars, outh, &time_in, 0);
        if (status & EXPR_UPDATE) {
            ++update_count;
            mpr_bitflags_clear(updated_values);
            mpr_bitflags_cpy(updated_values, mpr_value_get_elements_known(outh, 0));
        }
        /* sleep here stops compiler from optimizing loop away */
        usleep(1);
    }
    now = mpr_get_current_time();
    total_elapsed_time += now-then;

    if (0 == result)
        eprintf("OK\n");

    if (check_result(updated_values, outh, check))
        result = 1;

    eprintf("Recv'd %d updates... ", update_count);
    if (exp_updates >= 0 && exp_updates != update_count) {
        eprintf("error: expected %d\n", exp_updates);
        result = 1;
    }
    else if (exp_updates >= 0)
        eprintf("OK\n");
    else
        eprintf("not checked\n");

    eprintf("Elapsed time: %g seconds.\n", now-then);

free:
    if (updated_values)
        free(updated_values);
    mpr_expr_free(e);

fail:
    return result;
}

int run_tests()
{
    int i;
    mpr_type types[3] = {MPR_INT32, MPR_FLT, MPR_DBL};
    int lens[3] = {2, 3, 2};

    /* 1) Complex string */
    /* TODO: ensure successive constant multiplications are optimized */
    /* TODO: ensure split successive constant additions are optimized */
    set_expr_str("y=26*2/2+log10(pi)+2.*pow(2,1*(3+7*.1)*1.1+x{0}[0]-x)*3*4+cos(2.)");
    setup_test(MPR_FLT, 1, MPR_FLT, 1);
    expect_flt[0] = 26*2/2+log10f(M_PI)+2.f*powf(2,1*(3+7*.1f)*1.1f+src_flt[0]-src_flt[0])*3*4+cosf(2.0f);
    if (parse_and_eval(PARSE_SUCCESS | EVAL_SUCCESS, 0, 1, iterations))
        return 1;

    /* 2) Building vectors, conditionals */
    set_expr_str("y=(x>1)?[1,2,3]:[2,4,6]");
    setup_test(MPR_FLT, 3, MPR_INT32, 3);
    expect_int[0] = src_flt[0] > 1 ? 1 : 2;
    expect_int[1] = src_flt[1] > 1 ? 2 : 4;
    expect_int[2] = src_flt[2] > 1 ? 3 : 6;
    if (parse_and_eval(PARSE_SUCCESS | EVAL_SUCCESS, 0, 1, iterations))
        return 1;

    /* 3) Conditionals with shortened syntax; sign() */
    set_expr_str("y=(sign(x)==-1)?x:123");
    setup_test(MPR_FLT, 1, MPR_INT32, 1);
    expect_int[0] = src_flt[0] < 0 ? (int)src_flt[0] : 123;
    if (parse_and_eval(PARSE_SUCCESS | EVAL_SUCCESS, 0, 1, iterations))
        return 1;

    /* 4) Conditional that should be optimized */
    set_expr_str("y=1?2:123");
    setup_test(MPR_FLT, 1, MPR_INT32, 1);
    expect_int[0] = 2;
    if (parse_and_eval(PARSE_SUCCESS | EVAL_SUCCESS, 2, 1, iterations))
        return 1;

    /* 5) Building vectors with variables, operations inside vector-builder */
    set_expr_str("y=[x*-2+1,0]");
    setup_test(MPR_INT32, 2, MPR_DBL, 3);
    expect_dbl[0] = (double)src_int[0] * -2 + 1;
    expect_dbl[1] = (double)src_int[1] * -2 + 1;
    expect_dbl[2] = 0.0;
    if (parse_and_eval(PARSE_SUCCESS | EVAL_SUCCESS, 0, 1, iterations))
        return 1;

    /* 6) Building vectors with variables, operations inside vector-builder */
    set_expr_str("y=[-99.4, -x*1.1+x]");
    setup_test(MPR_INT32, 2, MPR_DBL, 3);
    expect_dbl[0] = -99.4f;
    expect_dbl[1] = -((double)src_int[0]);
    expect_dbl[1] *= 1.1f;
    expect_dbl[1] += ((double)src_int[0]);
    expect_dbl[2] = -((double)src_int[1]);
    expect_dbl[2] *= 1.1f;
    expect_dbl[2] += ((double)src_int[1]);
    if (parse_and_eval(PARSE_SUCCESS | EVAL_SUCCESS, 0, 1, iterations))
        return 1;

    /* 7) Indexing vectors by range */
    set_expr_str("y=x[1:2]+100");
    setup_test(MPR_DBL, 3, MPR_FLT, 2);
    expect_flt[0] = (float)(src_dbl[1] + 100);
    expect_flt[1] = (float)(src_dbl[2] + 100);
    if (parse_and_eval(PARSE_SUCCESS | EVAL_SUCCESS, 0, 1, iterations))
        return 1;

    /* 8) Typical linear scaling expression with vectors */
    set_expr_str("y=x*[0.1,3.7,-.1112]+[2,1.3,9000]");
    setup_test(MPR_FLT, 3, MPR_FLT, 3);
    expect_flt[0] = src_flt[0] * 0.1f;
    expect_flt[0] += 2.f;
    expect_flt[1] = src_flt[1] * 3.7f;
    expect_flt[1] += 1.3f;
    expect_flt[2] = src_flt[2] * -0.1112f;
    expect_flt[2] += 9000.f;
    if (parse_and_eval(PARSE_SUCCESS | EVAL_SUCCESS, 0, 1, iterations))
        return 1;

    /* 9) Check type and vector length promotion of operation sequences */
    set_expr_str("y=1+2*3-4*x");
    setup_test(MPR_FLT, 2, MPR_FLT, 2);
    expect_flt[0] = 1. + 2. * 3. - 4. * src_flt[0];
    expect_flt[1] = 1. + 2. * 3. - 4. * src_flt[1];
    if (parse_and_eval(PARSE_SUCCESS | EVAL_SUCCESS, 0, 1, iterations))
        return 1;

    /* 10) Swizzling, more pre-computation */
    set_expr_str("y=[x[2],x[0]]*0+1+12");
    setup_test(MPR_FLT, 3, MPR_FLT, 2);
    expect_flt[0] = src_flt[2] * 0. + 1. + 12.;
    expect_flt[1] = src_flt[0] * 0. + 1. + 12.;
    if (parse_and_eval(PARSE_SUCCESS | EVAL_SUCCESS, 0, 1, iterations))
        return 1;

    /* 11) Logical negation */
    set_expr_str("y=!(x[1]*0)");
    setup_test(MPR_DBL, 3, MPR_INT32, 1);
    expect_int[0] = (int)!(src_dbl[1] && 0);
    if (parse_and_eval(PARSE_SUCCESS | EVAL_SUCCESS, 0, 1, iterations))
        return 1;

    /* 12) any() */
    set_expr_str("y=(x-1).any()");
    setup_test(MPR_DBL, 3, MPR_INT32, 1);
    expect_int[0] =    ((int)src_dbl[0] - 1) ? 1 : 0
                     | ((int)src_dbl[1] - 1) ? 1 : 0
                     | ((int)src_dbl[2] - 1) ? 1 : 0;
    if (parse_and_eval(PARSE_SUCCESS | EVAL_SUCCESS, 0, 1, iterations))
        return 1;

    /* 13) all() */
    set_expr_str("y=x[2]*(x-1).all()");
    setup_test(MPR_DBL, 3, MPR_INT32, 1);
    expect_int[0] = (int)src_dbl[2] * (  (((int)src_dbl[0] - 1) ? 1 : 0)
                                       & (((int)src_dbl[1] - 1) ? 1 : 0)
                                       & (((int)src_dbl[2] - 1) ? 1 : 0));
    if (parse_and_eval(PARSE_SUCCESS | EVAL_SUCCESS, 0, 1, iterations))
        return 1;

    /* 14) pi and e, extra spaces */
    set_expr_str("y=x + pi -     e");
    setup_test(MPR_DBL, 1, MPR_FLT, 1);
    expect_flt[0] = (float)(src_dbl[0] + M_PI - M_E);
    if (parse_and_eval(PARSE_SUCCESS | EVAL_SUCCESS, 0, 1, iterations))
        return 1;

    /* 15) Bad vector notation */
    set_expr_str("y=(x-2)[1]");
    setup_test(MPR_INT32, 1, MPR_INT32, 1);
    if (parse_and_eval(PARSE_FAILURE, 0, 1, iterations))
        return 1;

    /* 16) Negative vector index */
    set_expr_str("y=x[-3]");
    setup_test(MPR_INT32, 3, MPR_INT32, 1);
    expect_int[0] = src_int[0];
    if (parse_and_eval(PARSE_SUCCESS | EVAL_SUCCESS, 0, 1, iterations))
        return 1;

    /* 17) Vector length mismatch */
    set_expr_str("y=x[1:2]");
    setup_test(MPR_INT32, 3, MPR_INT32, 1);
    expect_int[0] = src_int[1];
    if (parse_and_eval(PARSE_SUCCESS | EVAL_SUCCESS, 0, 1, iterations))
        return 1;

    /* 18) Unnecessary vector notation */
    set_expr_str("y=x+[1]");
    setup_test(MPR_INT32, 1, MPR_INT32, 1);
    expect_int[0] = src_int[0] + 1;
    if (parse_and_eval(PARSE_SUCCESS | EVAL_SUCCESS, 4, 1, iterations))
        return 1;

    /* 19) Invalid history index */
    set_expr_str("y=x{-101}");
    setup_test(MPR_INT32, 1, MPR_INT32, 1);
    if (parse_and_eval(PARSE_FAILURE, 0, 1, iterations))
        return 1;

    /* 20) Invalid history index */
    set_expr_str("y=x-y{-101}");
    setup_test(MPR_INT32, 1, MPR_INT32, 1);
    if (parse_and_eval(PARSE_FAILURE, 0, 1, iterations))
        return 1;

    /* 21) Scientific notation */
    set_expr_str("y=x[1]*1.23e-20");
    setup_test(MPR_INT32, 2, MPR_DBL, 1);
    expect_dbl[0] = (double)src_int[1] * 1.23e-20;
    if (parse_and_eval(PARSE_SUCCESS | EVAL_SUCCESS, 0, 1, iterations))
        return 1;

    /* 22) Vector assignment */
    set_expr_str("y[1]=x[1]");
    setup_test(MPR_DBL, 3, MPR_INT32, 3);
    expect_type[0] = MPR_NULL;
    expect_int[1] = (int)src_dbl[1];
    expect_type[2] = MPR_NULL;
    if (parse_and_eval(PARSE_SUCCESS | EVAL_SUCCESS, 0, 1, iterations))
        return 1;

    /* 23) Vector assignment */
    set_expr_str("y[1:2]=[x[1],10]");
    setup_test(MPR_DBL, 3, MPR_INT32, 3);
    expect_type[0] = MPR_NULL;
    expect_int[1] = (int)src_dbl[1];
    expect_int[2] = 10;
    if (parse_and_eval(PARSE_SUCCESS | EVAL_SUCCESS, 0, 1, iterations))
        return 1;

    /* 24) Output vector swizzling */
    set_expr_str("[y[0],y[2]]=x[1:2]");
    setup_test(MPR_FLT, 3, MPR_DBL, 3);
    expect_dbl[0] = (double)src_flt[1];
    expect_type[1] = MPR_NULL;
    expect_dbl[2] = (double)src_flt[2];
    if (parse_and_eval(PARSE_SUCCESS | EVAL_SUCCESS, 0, 1, iterations))
        return 1;

    /* 25) Multiple expressions */
    set_expr_str("y[0]=x*100-23.5; y[2]=100-x*6.7");
    setup_test(MPR_INT32, 1, MPR_FLT, 3);
    expect_flt[0] = src_int[0] * 100.f;
    expect_flt[0] -= 23.5f;
    expect_type[1] = MPR_NULL;
    expect_flt[2] = src_int[0] * 6.7f;
    expect_flt[2] = 100.f - expect_flt[2];
    if (parse_and_eval(PARSE_SUCCESS | EVAL_SUCCESS, 0, 1, iterations))
        return 1;

    /* 26) Error check: separating sub-expressions with commas */
    set_expr_str("foo=1,  y=y{-1}+foo");
    setup_test(MPR_INT32, 1, MPR_FLT, 1);
    if (parse_and_eval(PARSE_FAILURE, 0, 1, iterations))
        return 1;

    /* 27) Initialize filters */
    set_expr_str("y=x+y{-1}; y{-1}=100");
    setup_test(MPR_INT32, 1, MPR_INT32, 1);
    expect_int[0] = src_int[0] * iterations + 100;
    if (parse_and_eval(PARSE_SUCCESS | EVAL_SUCCESS, 0, 1, iterations))
        return 1;

    /* 28) Initialize filters + vector index */
    set_expr_str("y=x+y{-1}; y[1]{-1}=100");
    setup_test(MPR_INT32, 2, MPR_INT32, 2);
    expect_int[0] = src_int[0] * iterations;
    expect_int[1] = src_int[1] * iterations + 100;
    if (parse_and_eval(PARSE_SUCCESS | EVAL_SUCCESS, 0, 1, iterations))
        return 1;

    /* 29) Initialize filters + vector index */
    set_expr_str("y=x+y{-1}; y{-1}=[100,101]");
    setup_test(MPR_INT32, 2, MPR_INT32, 2);
    expect_int[0] = src_int[0] * iterations + 100;
    expect_int[1] = src_int[1] * iterations + 101;
    if (parse_and_eval(PARSE_SUCCESS | EVAL_SUCCESS, 0, 1, iterations))
        return 1;

    /* 30) Initialize filters */
    set_expr_str("y=x+y{-1}; y[0]{-1}=100; y[2]{-1}=200");
    setup_test(MPR_INT32, 3, MPR_INT32, 3);
    expect_int[0] = src_int[0] * iterations + 100;
    expect_int[1] = src_int[1] * iterations;
    expect_int[2] = src_int[2] * iterations + 200;
    if (parse_and_eval(PARSE_SUCCESS | EVAL_SUCCESS, 0, 1, iterations))
        return 1;

    /* 31) Initialize filters */
    set_expr_str("y=x+y{-1}-y{-2}; y{-1}=[100,101]; y{-2}=[102,103]");
    setup_test(MPR_INT32, 2, MPR_INT32, 2);
    switch (iterations % 6) {
        case 0:
            expect_int[0] = 100;
            expect_int[1] = 101;
            break;
        case 1:
            expect_int[0] = src_int[0] + 100 - 102;
            expect_int[1] = src_int[1] + 101 - 103;
            break;
        case 2:
            expect_int[0] = src_int[0] * 2 - 102;
            expect_int[1] = src_int[1] * 2 - 103;
            break;
        case 3:
            expect_int[0] = src_int[0] * 2 - 100;
            expect_int[1] = src_int[1] * 2 - 101;
            break;
        case 4:
            expect_int[0] = src_int[0] - 100 + 102;
            expect_int[1] = src_int[1] - 101 + 103;
            break;
        case 5:
            expect_int[0] = 102;
            expect_int[1] = 103;
            break;
    }
    if (parse_and_eval(PARSE_SUCCESS, 0, 1, iterations))
        return 1;

    /* 32) Only initialize */
    set_expr_str("y{-1}=100");
    setup_test(MPR_INT32, 3, MPR_INT32, 1);
    if (parse_and_eval(PARSE_FAILURE, 0, 1, iterations))
        return 1;

    /* 33) Bad syntax */
    set_expr_str(" ");
    setup_test(MPR_INT32, 1, MPR_FLT, 3);
    if (parse_and_eval(PARSE_FAILURE, 0, 1, iterations))
        return 1;

    /* 34) Bad syntax */
    set_expr_str(" ");
    setup_test(MPR_INT32, 1, MPR_FLT, 3);
    if (parse_and_eval(PARSE_FAILURE, 0, 1, iterations))
        return 1;

    /* 35) Bad syntax */
    set_expr_str("y");
    setup_test(MPR_INT32, 1, MPR_FLT, 3);
    if (parse_and_eval(PARSE_FAILURE, 0, 1, iterations))
        return 1;

    /* 36) Bad syntax */
    set_expr_str("y=");
    setup_test(MPR_INT32, 1, MPR_FLT, 3);
    if (parse_and_eval(PARSE_FAILURE, 0, 1, iterations))
        return 1;

    /* 37) Bad syntax */
    set_expr_str("=x");
    setup_test(MPR_INT32, 1, MPR_FLT, 3);
    if (parse_and_eval(PARSE_FAILURE, 0, 1, iterations))
        return 1;

    /* 38) sin */
    set_expr_str("sin(x)");
    setup_test(MPR_INT32, 1, MPR_FLT, 3);
    if (parse_and_eval(PARSE_FAILURE, 0, 1, iterations))
        return 1;

    /* 39) Variable declaration */
    set_expr_str("y=x+var; var=[3.5,0]");
    setup_test(MPR_INT32, 2, MPR_FLT, 2);
    expect_flt[0] = src_int[0] + 3.5f;
    expect_flt[1] = src_int[1];
    if (parse_and_eval(PARSE_SUCCESS | EVAL_SUCCESS, 0, 1, iterations))
        return 1;

    /* 40) Variable declaration */
    set_expr_str("ema=ema*0.9+x*0.1; y=ema*2; ema{-1}=90");
    setup_test(MPR_INT32, 1, MPR_FLT, 1);
    expect_flt[0] = 90.f;
    for (i = 0; i < iterations; i++) {
        float temp = src_int[0];
        temp *= 0.1f;
        expect_flt[0] *= 0.9f;
        expect_flt[0] += temp;
    }
    expect_flt[0] *= 2.f;
    if (parse_and_eval(PARSE_SUCCESS | EVAL_SUCCESS, 0, 1, iterations))
        return 1;

    /* 41) Multiple variable declaration */
    set_expr_str("a=1.1; b=2.2; c=3.3; y=x+a-b*c");
    setup_test(MPR_INT32, 1, MPR_FLT, 1);
    expect_flt[0] = (float)src_int[0];
    expect_flt[0] += 1.1;
    expect_flt[0] -= 2.2 * 3.3;
    if (parse_and_eval(PARSE_SUCCESS | EVAL_SUCCESS, 0, 1, iterations))
        return 1;

    /* 42) Malformed variable declaration */
    set_expr_str("y=x + myvariable * 10");
    setup_test(MPR_INT32, 1, MPR_FLT, 1);
    if (parse_and_eval(PARSE_FAILURE, 0, 1, iterations))
        return 1;

    /* 43) Vector functions mean() and sum() */
    set_expr_str("y=x.mean()==(x.sum()/3)");
    setup_test(MPR_FLT, 3, MPR_INT32, 1);
    expect_int[0] = 1;
    if (parse_and_eval(PARSE_SUCCESS | EVAL_SUCCESS, 0, 1, iterations))
        return 1;

    /* 44) Overloaded vector functions max() and min() */
    set_expr_str("y=x.max()-x.min()*max(x[0],1)");
    setup_test(MPR_FLT, 3, MPR_INT32, 1);
    expect_int[0] = (  ((src_flt[0] > src_flt[1]) ?
                        (src_flt[0] > src_flt[2] ? src_flt[0] : src_flt[2]) :
                        (src_flt[1] > src_flt[2] ? src_flt[1] : src_flt[2]))
                     - ((src_flt[0] < src_flt[1]) ?
                        (src_flt[0] < src_flt[2] ? src_flt[0] : src_flt[2]) :
                        (src_flt[1] < src_flt[2] ? src_flt[1] : src_flt[2]))
                     * (src_flt[0] > 1 ? src_flt[0] : 1));
    if (parse_and_eval(PARSE_SUCCESS | EVAL_SUCCESS, 0, 1, iterations))
        return 1;

    /* 45) Vector function: norm() */
    set_expr_str("y=x.norm();");
    setup_test(MPR_INT32, 2, MPR_FLT, 1);
    expect_flt[0] = sqrtf(powf((float)src_int[0], 2) + powf((float)src_int[1], 2));
    if (parse_and_eval(PARSE_SUCCESS | EVAL_SUCCESS, 0, 1, iterations))
        return 1;

    /* 46) Optimization: operations by zero */
    set_expr_str("y=0*sin(x)*200+1.1");
    setup_test(MPR_INT32, 1, MPR_FLT, 1);
    expect_flt[0] = 1.1;
    if (parse_and_eval(PARSE_SUCCESS | EVAL_SUCCESS, 2, 1, iterations))
        return 1;

    /* 47) Optimization: operations by one */
    set_expr_str("y=x*1");
    setup_test(MPR_INT32, 1, MPR_FLT, 1);
    expect_flt[0] = (float)src_int[0];
    if (parse_and_eval(PARSE_SUCCESS | EVAL_SUCCESS, 2, 1, iterations))
        return 1;

    /* 48) Error check: division by zero */
    set_expr_str("y=x/0");
    setup_test(MPR_INT32, 1, MPR_FLT, 1);
    if (parse_and_eval(PARSE_FAILURE, 0, 1, iterations))
        return 1;

    /* 49) Multiple Inputs */
    set_expr_str("y=x+x$1[1:2]+x$2");
    setup_test_multisource(3, types, lens, MPR_FLT, 2);
    expect_flt[0] = (float)((double)src_int[0] + (double)src_flt[1] + src_dbl[0]);
    expect_flt[1] = (float)((double)src_int[1] + (double)src_flt[2] + src_dbl[1]);
    if (parse_and_eval(PARSE_SUCCESS | EVAL_SUCCESS, 0, 1, iterations))
        return 1;

    /* 50) Functions with memory: ema() */
    set_expr_str("y=x-ema(x,0.1)+2");
    setup_test(MPR_INT32, 1, MPR_FLT, 1);
    expect_flt[0] = 0;
    for (i = 0; i < iterations; i++) {
        expect_flt[0] += ((float)src_int[0] - expect_flt[0]) * 0.1f;
    }
    expect_flt[0] = (float)src_int[0] - expect_flt[0] + 2.f;
    if (parse_and_eval(PARSE_SUCCESS | EVAL_SUCCESS, 0, 1, iterations))
        return 1;

    /* 51) Functions with memory: schmitt() */
    set_expr_str("y=y{-1}+(schmitt(y{-1},20,80)?-1:1)");
    setup_test(MPR_INT32, 3, MPR_FLT, 3);
    if (iterations < 80) {
        expect_flt[0] = expect_flt[1] = expect_flt[2] = iterations;
    }
    else {
        int cycles = (iterations-80) / 60;
        int remainder = (iterations-80) % 60;
        float ans = (cycles % 2) ? 20 + remainder : 80 - remainder;
        expect_flt[0] = expect_flt[1] = expect_flt[2] = ans;
    }
    if (parse_and_eval(PARSE_SUCCESS | EVAL_SUCCESS, 0, 1, iterations))
        return 1;

    /* 52) Multiple output assignment */
    set_expr_str("y=x-10000; y=max(min(y,1),0)");
    setup_test(MPR_FLT, 1, MPR_FLT, 1);
    expect_flt[0] = src_flt[0] - 10000;
    expect_flt[0] = expect_flt[0] < 0 ? 0 : expect_flt[0];
    expect_flt[0] = expect_flt[0] > 0 ? 1 : expect_flt[0];
    if (parse_and_eval(PARSE_SUCCESS | EVAL_SUCCESS, 0, 1, iterations))
        return 1;

    /* 53) Access timetags */
    set_expr_str("y=t_x");
    setup_test(MPR_INT32, 1, MPR_FLT, 1);
    if (parse_and_eval(PARSE_SUCCESS | EVAL_SUCCESS, 0, 0, iterations))
        return 1;
    if (start_index < 0 || start_index == 53) {
        if (dst_flt[0] != (float)mpr_time_as_dbl(time_in)) {
            eprintf("... error: expected %g\n", mpr_time_as_dbl(time_in));
            return 1;
        }
        else
            eprintf("... OK\n");
    }

    /* 54) Access timetags from past samples */
    set_expr_str("y=t_x-t_y{-1}");
    setup_test(MPR_INT32, 1, MPR_DBL, 1);
    if (parse_and_eval(PARSE_SUCCESS | EVAL_SUCCESS, 0, 0, iterations))
        return 1;
    if (start_index < 0 || start_index == 54) {
        /* results may vary depending on machine but we can perform a sanity check */
        if (dst_dbl[0] < 0.0 || dst_dbl[0] > 0.1) {
            eprintf("... error: expected value between %g and %g\n", 0.0, 0.1);
            return 1;
        }
        else
            eprintf("... OK\n");
    }

    /* 55) Moving average of inter-sample period */
    /* Tricky - we need to init t_y{-1} to t_x or the first calculated
     * difference will be enormous! */
    set_expr_str("t_y{-1}=t_x;"
                 "period=t_x-t_y{-1};"
                 "y=y{-1}*0.9+period*0.1;");
    setup_test(MPR_INT32, 1, MPR_DBL, 1);
    if (parse_and_eval(PARSE_SUCCESS | EVAL_SUCCESS, 0, 0, iterations))
        return 1;
    if (start_index < 0 || start_index == 55) {
        /* results may vary depending on machine but we can perform a sanity check */
        if (dst_dbl[0] < 0. || dst_dbl[0] > 0.003) {
            eprintf("... error: expected value between %g and %g\n", 0.0, 0.003);
            return 1;
        }
        else
            eprintf("... OK\n");
    }

    /* 56) Moving average of inter-sample jitter */
    /* Tricky - we need to init t_y{-1} to t_x or the first calculated
     * difference will be enormous! */
    set_expr_str("t_y{-1}=t_x;"
                 "interval=t_x-t_y{-1};"
                 "sr=sr*0.9+interval*0.1;"
                 "y=y{-1}*0.9+abs(interval-sr)*0.1;");
    setup_test(MPR_INT32, 1, MPR_DBL, 1);
    if (parse_and_eval(PARSE_SUCCESS | EVAL_SUCCESS, 0, 0, iterations))
        return 1;
    if (start_index < 0 || start_index == 56) {
        /* results may vary depending on machine but we can perform a sanity check */
        if (dst_dbl[0] < 0. || dst_dbl[0] > 0.002) {
            eprintf("... error: expected value between %g and %g\n", 0.0, 0.002);
            return 1;
        }
        else
            eprintf("... OK\n");
    }

    /* 57) Expression for limiting output rate
     * Leaving the initial t_y{-1} initialized to 0 simply means the first update will cause output */
    set_expr_str("diff=t_x-t_y{-1};"
                 "alive=diff>0.1;"
                 "y=x;");
    setup_test(MPR_INT32, 1, MPR_INT32, 1);
    expect_int[0] = src_int[0];
    if (parse_and_eval(PARSE_SUCCESS | EVAL_SUCCESS, 0, 1, -1))
        return 1;

    /* 58) Expression for limiting rate with smoothed output.
     * Leaving the initial t_y{-1} initialized to 0 simply means the first update will cause output */
    set_expr_str("a=x%100;"
                 "count{-1}=1;"
                 "alive=(t_x-t_y{-1})>0.1;"
                 "y=(accum+a)/count;"
                 "accum=!alive*accum+a;"
                 "count=alive?1:count+1;");
    setup_test(MPR_INT32, 1, MPR_INT32, 1);
    expect_int[0] = (src_int[0] % 100);
    if (parse_and_eval(PARSE_SUCCESS | EVAL_SUCCESS, 0, 1, -1))
        return 1;

    /* 59) Manipulate timetag directly. This functionality may be used in the
     *     future to schedule delays, however it currently will not affect
     *     message timing. Disabled for now. */
    set_expr_str("y=x[0]{0}; t_y=t_x+10");
    setup_test(MPR_INT32, 1, MPR_INT32, 1);
    expect_int[0] = src_int[0];
    if (parse_and_eval(PARSE_FAILURE, 0, 1, iterations))
        return 1;
/*     mpr_time time = *(mpr_time*)mpr_value_get_time(outh, 0, 0);
     if (mpr_time_as_dbl(time) != mpr_time_as_dbl(time_in) + 10) {
         eprintf("Expected timestamp {%"PRIu32", %"PRIu32"} but got "
                 "{%"PRIu32", %"PRIu32"}\n", time_in.sec+10, time_in.frac,
                 time.sec, time.frac);
         return 1;
     }
 */

    /* 60) Faulty timetag syntax */
    set_expr_str("y=t_x-y;");
    setup_test(MPR_INT32, 1, MPR_INT32, 1);
    if (parse_and_eval(PARSE_FAILURE, 0, 1, iterations))
        return 1;

    /* 61) Instance management */
    set_expr_str("count{-1}=0;alive=count>=5;y=x;count=(count+1)%10;");
    setup_test(MPR_INT32, 1, MPR_INT32, 1);
    expect_int[0] = src_int[0];
    if (parse_and_eval(PARSE_SUCCESS | EVAL_SUCCESS, 0, 1, -1))
        return 1;
    if (start_index < 0 || start_index == 61) {
        if (abs(iterations / 2 - update_count) > 4) {
            eprintf("error: expected approximately %d updates\n", iterations / 2);
            return 1;
        }
    }

    /* 62) Filter unchanged values */
    set_expr_str("muted=(x==x{-1});y=x;");
    setup_test(MPR_INT32, 1, MPR_INT32, 1);
    expect_int[0] = src_int[0];
    if (parse_and_eval(PARSE_SUCCESS | EVAL_SUCCESS, 0, 1, 1))
        return 1;

    /* 63) Buddy logic */
    set_expr_str("alive=(t_x$0>t_y{-1})&&(t_x$1>t_y{-1});y=x$0+x$1[1:2];");
    /* types[] and lens[] are already defined */
    setup_test_multisource(2, types, lens, MPR_FLT, 2);
    expect_flt[0] = src_int[0] + src_flt[1];
    expect_flt[1] = src_int[1] + src_flt[2];
    if (parse_and_eval(PARSE_SUCCESS | EVAL_SUCCESS, 0, 1, iterations))
        return 1;

    /* 64) Variable delays */
    set_expr_str("y=x{abs(x%10)-10,10}");
    setup_test(MPR_INT32, 1, MPR_INT32, 1);
    if (parse_and_eval(PARSE_SUCCESS | EVAL_SUCCESS, 0, 0, iterations))
        return 1;

    /* 65) Variable delay with missing maximum */
    set_expr_str("y=x{abs(x%10)-10}");
    setup_test(MPR_INT32, 1, MPR_INT32, 1);
    if (parse_and_eval(PARSE_FAILURE, 0, 0, iterations))
        return 1;

    /* 66) Calling delay() function explicitly */
    set_expr_str("y=delay(x, abs(x%10)-10), 10)");
    setup_test(MPR_INT32, 1, MPR_INT32, 1);
    if (parse_and_eval(PARSE_FAILURE, 0, 0, iterations))
        return 1;

    /* 67) Fractional delays */
    set_expr_str("ratio{-1}=0;y=x{-10+ratio, 10};ratio=(ratio+0.01)%5;");
    setup_test(MPR_INT32, 1, MPR_INT32, 1);
    if (parse_and_eval(PARSE_SUCCESS | EVAL_SUCCESS, 0, 0, iterations))
        return 1;

    /* 68) Pooled instance functions: any() and all() */
    set_expr_str("y=(x-1).instance.any() + (x+1).instance.all();");
    setup_test(MPR_INT32, 1, MPR_INT32, 1);
    expect_int[0] = 2;
    if (parse_and_eval(PARSE_SUCCESS | EVAL_SUCCESS, 0, 1, iterations))
        return 1;

    /* 69) Pooled instance functions: sum(), count() and mean() */
    set_expr_str("y=(x.instance.sum()/x.instance.count())==x.instance.mean();");
    setup_test(MPR_INT32, 3, MPR_INT32, 3);
    expect_int[0] = 1;
    expect_int[1] = 1;
    expect_int[2] = 1;
    if (parse_and_eval(PARSE_SUCCESS | EVAL_SUCCESS, 0, 1, iterations))
        return 1;

    /* 70) Pooled instance functions: max(), min(), and size() */
    set_expr_str("y=(x.instance.max()-x.instance.min())==x.instance.size();");
    setup_test(MPR_INT32, 1, MPR_INT32, 1);
    expect_int[0] = 1;
    if (parse_and_eval(PARSE_SUCCESS | EVAL_SUCCESS, 0, 1, iterations))
        return 1;

    /* 71) Pooled instance function: center() */
    set_expr_str("y=x.instance.center()==(x.instance.max()+x.instance.min())*0.5;");
    setup_test(MPR_INT32, 2, MPR_INT32, 2);
    expect_int[0] = 1;
    expect_int[1] = 1;
    if (parse_and_eval(PARSE_SUCCESS | EVAL_SUCCESS, 0, 1, iterations))
        return 1;

    /* 72) Pooled instance mean length of centered vectors */
    set_expr_str("m=x.instance.mean(); y=(x-m).norm().instance.mean()");
    setup_test(MPR_FLT, 2, MPR_FLT, 1);
    if (parse_and_eval(PARSE_SUCCESS | EVAL_SUCCESS, 0, 0, iterations))
        return 1;

    /* 73) Pooled instance mean linear displacement */
    set_expr_str("y=(x-x{-1}).instance.mean()");
    setup_test(MPR_INT32, 1, MPR_INT32, 1);
    if (parse_and_eval(PARSE_SUCCESS | EVAL_SUCCESS, 0, 0, iterations-1))
        return 1;

    /* 74) Dot product of two vectors */
    set_expr_str("y=dot(x, x$1);");
    lens[0] = 3;
    setup_test_multisource(2, types, lens, MPR_FLT, 1);
    expect_flt[0] = src_int[0] * src_flt[0];
    expect_flt[0] += src_int[1] * src_flt[1];
    expect_flt[0] += src_int[2] * src_flt[2];
    if (parse_and_eval(PARSE_SUCCESS | EVAL_SUCCESS, 0, 1, iterations))
        return 1;

    /* 75) 2D Vector angle */
    set_expr_str("y=angle([-1,-1], [1,0]);");
    setup_test(MPR_FLT, 2, MPR_FLT, 1);
    expect_flt[0] = M_PI * 0.75f;
    if (parse_and_eval(PARSE_SUCCESS | EVAL_SUCCESS, 0, 1, iterations))
        return 1;

    /* 76) Pooled instance mean angular displacement */
    set_expr_str("c0{-1}=x.instance.center();"
                 "c1=x.instance.center();"
                 "y=angle(x{-1}-c0,x-c1).instance.mean();"
                 "c0=c1;");
    setup_test(MPR_FLT, 2, MPR_FLT, 1);
    expect_flt[0] = 0.f;
    if (parse_and_eval(PARSE_SUCCESS | EVAL_SUCCESS, 0, 1, iterations-1))
        return 1;

    /* 77) Integer divide-by-zero */
    set_expr_str("foo=1; y=x/foo; foo=!foo;");
    setup_test(MPR_INT32, 1, MPR_INT32, 1);
    /* we expect only half of the evaluation attempts to succeed (i.e. when 'foo' == 1) */
    if (parse_and_eval(PARSE_SUCCESS | EVAL_SUCCESS, 0, 0, (iterations + 1) / 2))
        return 1;

    /* 78) Optimization: Vector squashing (10 tokens instead of 12) */
    set_expr_str("y=x*[3,3,x[1]]+[x[0],1,1];");
    setup_test(MPR_FLT, 3, MPR_FLT, 3);
    expect_flt[0] = src_flt[0] * 3.0f;
    expect_flt[0] += src_flt[0];
    expect_flt[1] = src_flt[1] * 3.0f;
    expect_flt[1] += 1.0f;
    expect_flt[2] = src_flt[2] * src_flt[1];
    expect_flt[2] += 1.0f;
    if (parse_and_eval(PARSE_SUCCESS | EVAL_SUCCESS, 10, 1, iterations))
        return 1;

    /* 79) Wrapping vectors, vector variables (6 tokens instead of 11) */
    set_expr_str("y=x*[3,3,3]+[1.23,4.56];");
    setup_test(MPR_FLT, 3, MPR_FLT, 3);
    expect_flt[0] = src_flt[0] * 3.0f;
    expect_flt[0] += 1.23f;
    expect_flt[1] = src_flt[1] * 3.0f;
    expect_flt[1] += 4.56f;
    expect_flt[2] = src_flt[2] * 3.0f;
    expect_flt[2] += 1.23f;
    if (parse_and_eval(PARSE_SUCCESS | EVAL_SUCCESS, 6, 1, iterations))
        return 1;

    /* 80) Just instance count */
    set_expr_str("y=x.instance.count();");
    setup_test(MPR_FLT, 3, MPR_FLT, 2);
    expect_flt[0] = 1;
    expect_flt[1] = 1;
    if (parse_and_eval(PARSE_SUCCESS | EVAL_SUCCESS, 2, 1, iterations))
        return 1;

    /* 81) instance.reduce() */
    set_expr_str("y = 10 + x[1:2].instance.reduce(a, b -> a + b);");
    setup_test(MPR_FLT, 3, MPR_FLT, 1);
    expect_flt[0] = src_flt[1] + 10;
    expect_flt[1] = src_flt[2] + 10;
    if (parse_and_eval(PARSE_SUCCESS | EVAL_SUCCESS, 11, 1, iterations))
        return 1;

    /* 82) Reducing a constant - syntax error */
    set_expr_str("y=(1*0).instance.mean();");
    setup_test(MPR_FLT, 3, MPR_FLT, 2);
    expect_flt[0] = 1;
    expect_flt[1] = 1;
    if (parse_and_eval(PARSE_FAILURE, 0, 1, iterations))
        return 1;

    /* 83) Reducing a user variable */
    set_expr_str("n=(x-100);y=n.vector.sum();");
    setup_test(MPR_FLT, 3, MPR_FLT, 2);
    expect_flt[0] = src_flt[0] - 100.f + src_flt[1] - 100.f + src_flt[2] - 100.f;
    expect_flt[1] = expect_flt[0];
    if (parse_and_eval(PARSE_SUCCESS | EVAL_SUCCESS, 0, 1, iterations))
        return 1;

    /* 84) History mean() - windowed running mean */
    set_expr_str("y=x.history(5).mean();");
    setup_test(MPR_FLT, 3, MPR_FLT, 2);
    expect_flt[0] = expect_flt[1] = 0.f;
    for (i = 0; i < iterations && i < 5; i++) {
        expect_flt[0] += src_flt[0];
        expect_flt[1] += src_flt[1];
    }
    expect_flt[0] /= 5.f;
    expect_flt[1] /= 5.f;
    if (parse_and_eval(PARSE_SUCCESS | EVAL_SUCCESS, 0, 1, iterations))
        return 1;

    /* 85) Reducing a user variable over history */
    set_expr_str("n=(x-100);y=n.history(5).mean();");
    setup_test(MPR_FLT, 3, MPR_FLT, 2);
    expect_flt[0] = src_flt[0] - 100;
    expect_flt[1] = src_flt[1] - 100;
    if (parse_and_eval(PARSE_FAILURE, 0, 1, iterations))
        return 1;

    /* 86) history.reduce() with accumulator initialisation */
    set_expr_str("y=x.history(5).reduce(x, a = 100 -> x + a);");
    setup_test(MPR_FLT, 3, MPR_FLT, 2);
    expect_flt[0] = expect_flt[1] = 100;
    for (i = 0; i < iterations && i < 5; i++) {
        expect_flt[0] += src_flt[0];
        expect_flt[1] += src_flt[1];
    }
    if (parse_and_eval(PARSE_SUCCESS | EVAL_SUCCESS, 9, 1, iterations))
        return 1;

    /* 87) vector.mean() */
    set_expr_str("y=x.vector.mean();");
    setup_test(MPR_FLT, 3, MPR_FLT, 2);
    expect_flt[0] = (src_flt[0] + src_flt[1] + src_flt[2]) / 3;
    expect_flt[1] = expect_flt[0];
    if (parse_and_eval(PARSE_SUCCESS | EVAL_SUCCESS, 0, 1, iterations))
        return 1;

    /* 88) vector.reduce() */
    set_expr_str("y=x.vector.reduce(x,a -> x+a);");
    setup_test(MPR_FLT, 3, MPR_FLT, 2);
    expect_flt[0] = src_flt[0] + src_flt[1] + src_flt[2];
    expect_flt[1] = expect_flt[0];
    if (parse_and_eval(PARSE_SUCCESS | EVAL_SUCCESS, 0, 1, iterations))
        return 1;

    /* 89) signal.mean() */
    set_expr_str("y=x.signal.mean();");
    types[0] = MPR_INT32;
    types[1] = MPR_FLT;
    types[2] = MPR_DBL;
    lens[0] = 2;
    lens[1] = 3;
    lens[2] = 1;
    setup_test_multisource(3, types, lens, MPR_FLT, 2);
    expect_flt[0] = (float)(((double)src_int[0] + (double)src_flt[0] + src_dbl[0]) / 3.);
    expect_flt[1] = (float)(((double)src_int[1] + (double)src_flt[1] + src_dbl[0]) / 3.);
    if (parse_and_eval(PARSE_SUCCESS | EVAL_SUCCESS, 0, 1, iterations))
        return 1;

    /* 90) signal.reduce() */
    set_expr_str("y=x.signal.reduce(x,a->x+a);");
    types[0] = MPR_INT32;
    types[1] = MPR_FLT;
    types[2] = MPR_DBL;
    lens[0] = 2;
    lens[1] = 3;
    lens[2] = 1;
    setup_test_multisource(3, types, lens, MPR_FLT, 2);
    expect_flt[0] = (float)((double)src_int[0] + (double)src_flt[0] + src_dbl[0]);
    expect_flt[1] = (float)((double)src_int[1] + (double)src_flt[1] + src_dbl[0]);
    if (parse_and_eval(PARSE_SUCCESS | EVAL_SUCCESS, 0, 1, iterations))
        return 1;

    /* 91) Nested reduce(): sum of last 3 samples of all input signals with extra input reference */
    set_expr_str("y=(x+1).signal.reduce(a,b->b+a.history(3).reduce(c,d->c+d)+a);");
    types[0] = MPR_INT32;
    types[1] = MPR_FLT;
    types[2] = MPR_DBL;
    lens[0] = 2;
    lens[1] = 3;
    lens[2] = 1;
    setup_test_multisource(3, types, lens, MPR_FLT, 1);
    expect_flt[0] = (float)(((double)src_int[0] + (double)src_flt[0] + src_dbl[0] + 3.0) * 4.0);
    if (parse_and_eval(PARSE_SUCCESS | EVAL_SUCCESS, 0, 1, iterations))
        return 1;

    /* 92) mean() nested with reduce() using sequential dot syntax - not currently allowed */
    // TODO: should we allow this syntax?
    set_expr_str("y=x.vector.reduce(x, a -> x + a).signal.mean();");
    types[0] = MPR_INT32;
    types[1] = MPR_FLT;
    types[2] = MPR_DBL;
    lens[0] = 2;
    lens[1] = 3;
    lens[2] = 1;
    setup_test_multisource(3, types, lens, MPR_FLT, 1);
    if (parse_and_eval(PARSE_FAILURE, 0, 1, iterations))
        return 1;

    /* 93) vector.reduce() on specific signal */
    set_expr_str("y=x$1.vector.reduce(x,a=1 -> x*a) == x$1.vector.product();");
    types[0] = MPR_INT32;
    types[1] = MPR_FLT;
    types[2] = MPR_DBL;
    lens[0] = 2;
    lens[1] = 3;
    lens[2] = 1;
    setup_test_multisource(3, types, lens, MPR_FLT, 1);
    expect_flt[0] = 1.f;
    if (parse_and_eval(PARSE_SUCCESS | EVAL_SUCCESS, 0, 1, iterations))
        return 1;

    /* 94) instance.count() on specific signal */
    set_expr_str("y=x$1.instance.count();");
    types[0] = MPR_INT32;
    types[1] = MPR_FLT;
    types[2] = MPR_DBL;
    lens[0] = 2;
    lens[1] = 3;
    lens[2] = 1;
    setup_test_multisource(3, types, lens, MPR_FLT, 1);
    expect_flt[0] = 1;
    if (parse_and_eval(PARSE_SUCCESS | EVAL_SUCCESS, 0, 1, iterations))
        return 1;

    /* 95) vector.reduce() with vector subset */
    set_expr_str("y=x[1:2].vector.reduce(x,a -> x+a);");
    setup_test(MPR_FLT, 3, MPR_FLT, 2);
    expect_flt[0] = src_flt[1] + src_flt[2];
    expect_flt[1] = expect_flt[0];
    if (parse_and_eval(PARSE_SUCCESS | EVAL_SUCCESS, 0, 1, iterations))
        return 1;

    /* 96) signal.reduce() nested with instance count() */
    set_expr_str("y=x.signal.reduce(x, a -> x.instance.count() + a);");
    types[0] = MPR_INT32;
    types[1] = MPR_FLT;
    types[2] = MPR_DBL;
    lens[0] = 2;
    lens[1] = 3;
    lens[2] = 1;
    setup_test_multisource(3, types, lens, MPR_FLT, 1);
    expect_flt[0] = 3;
    if (parse_and_eval(PARSE_SUCCESS | EVAL_SUCCESS, 0, 1, iterations))
        return 1;

    /* 97) signal.reduce() nested with instance mean() */
    set_expr_str("y=x.signal.reduce(x, a -> x.instance.mean() + a);");
    types[0] = MPR_INT32;
    types[1] = MPR_FLT;
    types[2] = MPR_DBL;
    lens[0] = 2;
    lens[1] = 3;
    lens[2] = 1;
    setup_test_multisource(3, types, lens, MPR_FLT, 1);
    expect_flt[0] = (float)((double)src_int[0] + (double)src_flt[0] + src_dbl[0]);
    if (parse_and_eval(PARSE_SUCCESS | EVAL_SUCCESS, 0, 1, iterations))
        return 1;

    /* 98) Nested reduce() of same type */
    set_expr_str("y=x.signal.reduce(a, b -> a.signal.reduce(c, d -> c + d) + b);");
    types[0] = MPR_INT32;
    types[1] = MPR_FLT;
    types[2] = MPR_DBL;
    lens[0] = 1;
    lens[1] = 3;
    lens[2] = 2;
    setup_test_multisource(3, types, lens, MPR_FLT, 3);
    if (parse_and_eval(PARSE_FAILURE, 0, 1, iterations))
        return 1;

    /* 99) Nested reduce(): sum of all vector elements of all input signals */
    /* TODO: need to modify tokens to allow variable vector length (per signal) */
    set_expr_str("y=x.signal.reduce(a, b -> a.vector.reduce(c, d -> c + d) + b);");
    types[0] = MPR_INT32;
    types[1] = MPR_FLT;
    types[2] = MPR_DBL;
    lens[0] = 2;
    lens[1] = 3;
    lens[2] = 1;
    setup_test_multisource(3, types, lens, MPR_FLT, 1);
    expect_flt[0] = (  (double)src_int[0] + (double)src_int[1]
                     + (double)src_flt[0] + (double)src_flt[1] + (double)src_flt[2]
                     + src_dbl[0]);
    if (parse_and_eval(PARSE_SUCCESS | EVAL_SUCCESS, 0, 1, iterations))
        return 1;

    /* 100) reduce() nested with mean() */
    set_expr_str("y=x.signal.reduce(x, aLongerName -> x.vector.mean() + aLongerName);");
    types[0] = MPR_INT32;
    types[1] = MPR_FLT;
    types[2] = MPR_DBL;
    lens[0] = 3;
    lens[1] = 2;
    lens[2] = 2;
    setup_test_multisource(3, types, lens, MPR_FLT, 1);
    expect_flt[0] = (  ((double)src_int[0] + (double)src_int[1] + (double)src_int[2]) / 3.
                     + ((double)src_flt[0] + (double)src_flt[1]) / 2.
                     + (src_dbl[0] + src_dbl[1]) / 2.);
    if (parse_and_eval(PARSE_SUCCESS | EVAL_SUCCESS, 0, 1, iterations))
        return 1;

    /* 101) reduce() nested with mean(), accum ref before input ref */
    set_expr_str("y=x.signal.reduce(x, a -> a - x.vector.mean());");
    types[0] = MPR_INT32;
    types[1] = MPR_FLT;
    types[2] = MPR_DBL;
    lens[0] = 2;
    lens[1] = 3;
    lens[2] = 1;
    setup_test_multisource(3, types, lens, MPR_FLT, 1);
    expect_flt[0] = (  ((double)src_int[0] + (double)src_int[1]) / 2.
                     + ((double)src_flt[0] + (double)src_flt[1] + (double)src_flt[2]) / 3.
                     + src_dbl[0]) * -1.;
    if (parse_and_eval(PARSE_SUCCESS | EVAL_SUCCESS, 0, 1, iterations))
        return 1;

    /* 102) Misplaced commas */
    set_expr_str("y=(0.00417,0.00719*x$0+0.0025*x$1+0,0)*[990,750]/2");
    types[0] = MPR_INT32;
    types[1] = MPR_FLT;
    lens[0] = 2;
    lens[1] = 3;
    setup_test_multisource(2, types, lens, MPR_FLT, 1);
    if (parse_and_eval(PARSE_FAILURE, 0, 1, iterations))
        return 1;

    /* 103) sort() */
    set_expr_str("a=[4,2,3,1,5,0]; a = a.sort(1); y=a[round(x)];");
    setup_test(MPR_FLT, 1, MPR_FLT, 1);
    expect_flt[0] = (((int)round(src_flt[0])) % 6 + 6) % 6;
    if (parse_and_eval(PARSE_SUCCESS | EVAL_SUCCESS, 0, 1, iterations))
        return 1;

    /* 104) Call sort() directly on vector */
    set_expr_str("a=[4,2,3,1,5,0].sort(1); y=a[0];");
    setup_test(MPR_FLT, 1, MPR_FLT, 1);
    expect_flt[0] = 0;
    if (parse_and_eval(PARSE_SUCCESS | EVAL_SUCCESS, 0, 1, iterations))
        return 1;

    /* 105) Variable vector index */
    set_expr_str("a=[0,1,2,3,4,5]; y=a[x];");
    setup_test(MPR_INT32, 1, MPR_FLT, 1);
    expect_flt[0] = ((src_int[0]) % 6 + 6) % 6;
    if (parse_and_eval(PARSE_SUCCESS | EVAL_SUCCESS, 0, 1, iterations))
        return 1;

    /* 106) Expression vector index */
    set_expr_str("a=[0,1,2,3,4,5]; y=a[sin(x)>0?0:1];");
    setup_test(MPR_FLT, 1, MPR_FLT, 1);
    expect_flt[0] = sin(src_flt[0]) > 0.f ? 0.f : 1.f;
    if (parse_and_eval(PARSE_SUCCESS | EVAL_SUCCESS, 0, 1, iterations))
        return 1;

    /* 107) Fractional vector index */
    set_expr_str("y=x[1.5];");
    setup_test(MPR_FLT, 3, MPR_FLT, 1);
    expect_flt[0] = (src_flt[1] + src_flt[2]) * 0.5;
    if (parse_and_eval(PARSE_SUCCESS | EVAL_SUCCESS, 0, 1, iterations))
        return 1;

    /* 108) Variable signal index */
    set_expr_str("y=x$(x$1)");
    types[0] = MPR_FLT;
    types[1] = MPR_INT32;
    lens[0] = 1;
    lens[1] = 1;
    setup_test_multisource(2, types, lens, MPR_FLT, 1);
    expect_flt[0] = src_int[0] % 2 ? (float)src_int[0] : src_flt[0];
    if (parse_and_eval(PARSE_SUCCESS | EVAL_SUCCESS, 0, 1, iterations))
        return 1;

    /* 109) Expression signal index */
    set_expr_str("y=x$(sin(x)>0);");
    types[0] = MPR_FLT;
    types[1] = MPR_INT32;
    lens[0] = 1;
    lens[1] = 1;
    setup_test_multisource(2, types, lens, MPR_FLT, 1);
    expect_flt[0] = (sin(src_flt[0]) > 0.f) ? (float)src_int[0] : src_flt[0];
    if (parse_and_eval(PARSE_SUCCESS | EVAL_SUCCESS, 0, 1, iterations))
        return 1;

    /* 110) Fractional signal index */
    set_expr_str("y=x$0.5;");
    setup_test(MPR_FLT, 1, MPR_FLT, 1);
    if (parse_and_eval(PARSE_FAILURE, 0, 1, iterations))
        return 1;

    /* 111) Indexing vectors by range with wrap-around */
    set_expr_str("y=x[2:0]+100");
    setup_test(MPR_DBL, 3, MPR_FLT, 2);
    expect_flt[0] = (float)(src_dbl[2] + 100);
    expect_flt[1] = (float)(src_dbl[0] + 100);
    if (parse_and_eval(PARSE_SUCCESS | EVAL_SUCCESS, 0, 1, iterations))
        return 1;

    /* 112) Reduce with vector initialisation for accumulator */
    set_expr_str("y = x.history(5).reduce(a, b = [1,2,3] -> a + b);");
    setup_test(MPR_FLT, 3, MPR_FLT, 3);
    expect_flt[0] = 1;
    expect_flt[1] = 2;
    expect_flt[2] = 3;
    for (i = 0; i < iterations && i < 5; i++) {
        expect_flt[0] += src_flt[0];
        expect_flt[1] += src_flt[1];
        expect_flt[2] += src_flt[2];
    }
    if (parse_and_eval(PARSE_SUCCESS | EVAL_SUCCESS, 0, 1, iterations))
        return 1;

    /* 113) Reduce with function initialisation for accumulator */
    set_expr_str("y = x$1.history(4).reduce(a, b = sin(x$0 - 10) -> a + b);");
    types[0] = MPR_FLT;
    types[1] = MPR_INT32;
    lens[0] = 1;
    lens[1] = 1;
    setup_test_multisource(2, types, lens, MPR_FLT, 1);
    expect_flt[0] = sin(src_flt[0] - 10.f);
    for (i = 0; i < iterations && i < 4; i++)
        expect_flt[0] += src_int[0];
    if (parse_and_eval(PARSE_SUCCESS | EVAL_SUCCESS, 0, 1, iterations))
        return 1;

    /* 114) Nested reduce with function initialisation for accumulator */
    set_expr_str("y=x$1.history(4).reduce(a,b=sin(x$0-10)->a.vector.reduce(c,d=cos(b)->c-d)+b);");
    types[0] = MPR_FLT;
    types[1] = MPR_INT32;
    lens[0] = 1;
    lens[1] = 1;
    setup_test_multisource(2, types, lens, MPR_FLT, 1);
    expect_flt[0] = sinf(src_flt[0] - 10.f);
    for (i = 0; i < 4; i++) {
        if (i < iterations)
            expect_flt[0] += (float)src_int[0] - cosf(expect_flt[0]);
        else
            expect_flt[0] += 0 - cosf(expect_flt[0]);
    }
    if (parse_and_eval(PARSE_SUCCESS | EVAL_SUCCESS, 0, 1, iterations))
        return 1;

    /* 115) Arpeggiator */
    /* TODO: add timing */
    /* optimisation possibility here: miditohz() could be precomputed on vector p */
    set_expr_str("i{-1}=0;p=[60,61,62,63,64];y=miditohz(p[i]);i=i+1;");
    setup_test(MPR_INT32, 1, MPR_FLT, 1);
    expect_flt[0] = ((iterations - 1) % 5) + 60.f - 69.f;
    expect_flt[0] /= 12.f;
    expect_flt[0] = pow(2.0, expect_flt[0]);
    expect_flt[0] *= 440.f;
    if (parse_and_eval(PARSE_SUCCESS | EVAL_SUCCESS, 0, 1, iterations))
        return 1;

    /* 116) Multiple variable indices */
    set_expr_str("a=0; b=1; c=-1; y=x$(a)[b]{c,2};");
    setup_test(MPR_INT32, 2, MPR_FLT, 1);
    expect_flt[0] = src_int[1];
    if (parse_and_eval(PARSE_SUCCESS | EVAL_SUCCESS, 0, 1, iterations))
        return 1;

    /* 117) Mixed input and output references in history reduce */
    set_expr_str("y=(x-y).history(4).mean();");
    setup_test(MPR_INT32, 1, MPR_FLT, 1);
    if (parse_and_eval(PARSE_FAILURE, 0, 1, iterations))
        return 1;

    /* 118) Turn history into vector */
    set_expr_str("a = x.history(5).concat(5).sort(1); y = a[2];");
    setup_test(MPR_FLT, 1, MPR_FLT, 1);
    expect_flt[0] = (iterations > 3) || (src_flt[0] < 0) ? src_flt[0] : 0.f;
    if (parse_and_eval(PARSE_SUCCESS | EVAL_SUCCESS, 13, 1, iterations))
        return 1;

    /* 119) Turn signals into vector */
    set_expr_str("a = x.signal.concat(10).sort(-1); y = a[0];");
    types[0] = MPR_FLT;
    types[1] = MPR_INT32;
    types[2] = MPR_DBL;
    lens[0] = 1;
    lens[1] = 2;
    lens[2] = 3;
    setup_test_multisource(3, types, lens, MPR_FLT, 1);
    expect_flt[0] = src_flt[0];
    for (i = 0; i < lens[1]; i++) {
        if ((float)src_int[i] > expect_flt[0]) {
            expect_flt[0] = (float)src_int[i];
        }
    }
    for (i = 0; i < lens[2]; i++) {
        if ((float)src_dbl[i] > expect_flt[0]) {
            expect_flt[0] = (float)src_dbl[i];
        }
    }
    if (parse_and_eval(PARSE_SUCCESS | EVAL_SUCCESS, 13, 1, iterations))
        return 1;

    /* 120) Turn instances into vector and return a function of vector size */
    set_expr_str("y = x.instance.concat(5).length() * 10;");
    setup_test(MPR_FLT, 1, MPR_FLT, 1);
    expect_flt[0] = 10.f;
    if (parse_and_eval(PARSE_SUCCESS | EVAL_SUCCESS, 0, 1, iterations))
        return 1;

    /* 121) Turn instances into vector and arpeggiate */
    /* TODO: add timing */
    /* TODO: ensure variable 'i' is not reset to zero for each new instance. */
    set_expr_str("i{-1}=0;p=(x%128).instance.concat(10).sort(1);y=miditohz(p[i]);i=i+1;");
    setup_test(MPR_INT32, 1, MPR_FLT, 1);
    expect_flt[0] = 440.f * powf(2.f, (fmodf((float)src_int[0], 128.f) - 69.f) / 12.f);
    if (parse_and_eval(PARSE_SUCCESS | EVAL_SUCCESS, 0, 1, iterations))
        return 1;

    /* 122) Count instances over time */
    set_expr_str("y = y{-1} + x.instance.count();");
    setup_test(MPR_FLT, 1, MPR_FLT, 1);
    expect_flt[0] = iterations;
    if (parse_and_eval(PARSE_SUCCESS | EVAL_SUCCESS, 9, 1, iterations))
        return 1;

    /* 123) Reduce without lambda operator */
    set_expr_str("y=x.history(5).reduce(x, a = 100, x + a);");
    setup_test(MPR_FLT, 1, MPR_FLT, 1);
    if (parse_and_eval(PARSE_FAILURE, 0, 1, iterations))
        return 1;

    /* 124) Reduce with extra lambda operator */
    set_expr_str("y=x.history(5).reduce(x, a = 100 -> x + a -> x + 1);");
    setup_test(MPR_FLT, 1, MPR_FLT, 1);
    if (parse_and_eval(PARSE_FAILURE, 0, 1, iterations))
        return 1;

    /* 125) Fractional indices for both vector and history */
    set_expr_str("y=x[0.3]{-0.25};");
    setup_test(MPR_FLT, 2, MPR_FLT, 1);
    expect_flt[1] = src_flt[0] * 0.7f;
    expect_flt[1] += src_flt[1] * 0.3f;
    expect_flt[0] = expect_flt[1] * 0.25f;
    expect_flt[0] += expect_flt[1] * 0.75f;
    if (parse_and_eval(PARSE_SUCCESS | EVAL_SUCCESS, 0, 1, iterations))
        return 1;

    /* 126) Fractional indices for both history and vector */
    set_expr_str("y=x{-1.75}[0.6];");
    setup_test(MPR_FLT, 2, MPR_FLT, 1);
    expect_flt[1] = src_flt[0] * (1.f - 0.6f);
    expect_flt[1] += src_flt[1] * 0.6f;
    expect_flt[0] = expect_flt[1] * 0.75f;
    expect_flt[0] += expect_flt[1] * 0.25f;
    if (parse_and_eval(PARSE_SUCCESS | EVAL_SUCCESS, 0, 1, iterations))
        return 1;

    /* 127) Fractional vector index with non-integer length */
    set_expr_str("y=x[0.2:1.1];");
    setup_test(MPR_FLT, 3, MPR_FLT, 2);
    if (parse_and_eval(PARSE_FAILURE, 0, 0, iterations))
        return 1;

    /* 128) Multi-step linear envelope */
    set_expr_str("tstart{-1}=t_x;"                          /* cache the instance start time */
                 "times=[0.1,0.01];"                        /* durations for our envelope */
                 "sumtimes=[0.1,0.11];"                     /* aggregated durations */
                 "amps=[0,x,x*0.9];"                        /* amplitudes for the envelope */
                 "t=t_x-tstart;"                            /* elapsed time since start */
                 "y[1]=t;"                                  /* store elapsed time for final check */
                 "idx=(t/sumtimes >= 1).sum();"             /* calculate integer time index */
                 "t=t-(idx?sumtimes[idx-1]:0);"             /* subtract previous envelope times */
                 "y[0]=amps[(idx<2)?idx+t/times[idx]:2];"); /* interpolate amp vals for envelope */
    setup_test(MPR_FLT, 1, MPR_DBL, 2);
    if (parse_and_eval(PARSE_SUCCESS | EVAL_SUCCESS, 0, 0, iterations))
        return 1;
    if (start_index < 0 || start_index == 128) {
        if (dst_dbl[1] < 0.1) {
            double d = ((double)src_flt[0] * dst_dbl[1] / 0.1);
            if (fabs(dst_dbl[0] - d) > fabs(d * 0.0000001)) {
                eprintf("... error at index 0 (expected %g) (1)\n", d);
                return 1;
            }
        }
        else if (dst_dbl[1] < 0.11) {
            double c = (dst_dbl[1] - 0.1) / 0.01;
            double d = ((double)src_flt[0]) * (1.0 - 0.1 * c);
            if (fabs(dst_dbl[0] - d) > fabs(d * 0.0000001)) {
                eprintf("... error at index 0 (expected %g) (2)\n", d);
                return 1;
            }
        }
        else {
            double d = (double)src_flt[0] * (double)0.9f;
            if (dst_dbl[0] != d) {
                eprintf("... error at index 0 (expected %g) (3)\n", d);
                return 1;
            }
        }
    }

    /* 129) Find vector index by value */
    set_expr_str("v=[60,61,62,63,64]; y=v.index(62) + index(v, 0);");
    setup_test(MPR_FLT, 1, MPR_FLT, 1);
    expect_flt[0] = 1;
    if (parse_and_eval(PARSE_SUCCESS | EVAL_SUCCESS, 0, 0, iterations))
        return 1;

    /* 130) Check assignment vector length */
    i = 0;
    set_expr_str("y{-1}=[0,0,0]; y[0]=x;");
    setup_test(MPR_FLT, 3, MPR_FLT, 3);
    expect_flt[0] = src_flt[0];
    expect_type[0] = MPR_FLT;
    expect_type[1] = MPR_NULL;
    expect_type[2] = MPR_NULL;
    if (parse_and_eval(PARSE_SUCCESS | EVAL_SUCCESS, 0, 1, iterations))
        return 1;

    /* 131) Concatenate source values over time */
    set_expr_str("i{-1}=0; a{-1}=[0,0,0,0,0,0]; a[i]=i; i=i+1; muted=i%6; y=a;");
    setup_test(MPR_FLT, 1, MPR_FLT, MAX_DST_ARRAY_LEN);
    for (i = 0; i < MAX_DST_ARRAY_LEN; i++) {
        expect_flt[i] = floor(iterations / 6) * 6 + i;
        if (expect_flt[i] >= iterations)
            expect_flt[i] -= 6;
    }
    if (parse_and_eval(PARSE_SUCCESS | EVAL_SUCCESS, 0, 1, iterations / MAX_DST_ARRAY_LEN))
        return 1;

    /* 132) Vector median value (precomputed by the optimizer in this case) */
    set_expr_str("y=[0,1,2,3,4,5].median();");
    setup_test(MPR_FLT, 3, MPR_INT32, 2);
    expect_int[0] = expect_int[1] = floor(2.5f);
    if (parse_and_eval(PARSE_SUCCESS | EVAL_SUCCESS, 0, 1, iterations))
        return 1;

    /* 133) Setting vector to uniform; ensure is not precomputed. */
    set_expr_str("y=1+2+uniform(2);");
    setup_test(MPR_FLT, 1, MPR_FLT, 4);
    if (parse_and_eval(PARSE_SUCCESS | EVAL_SUCCESS, 5, 0, iterations))
        return 1;
    if (start_index < 0 || start_index == 133) {
        for (i = 1; i < 4; i++) {
            int j;
            for (j = i - 1; j >= 0; j--) {
                if (dst_flt[i] == dst_flt[j]) {
                    eprintf("... error: duplicate element values at index %d and %d.\n", j, i);
                    return 1;
                }
            }
        }
    }

    /* 134) Use latest source of a convergent map */
    set_expr_str("y=x.signal.newest();");
    types[0] = MPR_FLT;
    types[1] = MPR_INT32;
    lens[0] = 1;
    lens[1] = 2;
    setup_test_multisource(2, types, lens, MPR_FLT, 2);
    expect_flt[0] = expect_flt[1] = src_flt[0];
    if (parse_and_eval(PARSE_SUCCESS | EVAL_SUCCESS, 0, 1, iterations))
        return 1;

    /* 135) Use latest source of a convergent map (shorthand) */
    set_expr_str("y=x$$;");
    types[0] = MPR_FLT;
    types[1] = MPR_INT32;
    lens[0] = 1;
    lens[1] = 2;
    setup_test_multisource(2, types, lens, MPR_FLT, 2);
    expect_flt[0] = expect_flt[1] = src_flt[0];
    if (parse_and_eval(PARSE_SUCCESS | EVAL_SUCCESS, 0, 1, iterations))
        return 1;

    /* 136) Implicit reduce with variable timetag argument instead of value */
    set_expr_str("y=t_x.instance.mean();");
    setup_test(MPR_FLT, 3, MPR_FLT, 1);
    if (parse_and_eval(PARSE_SUCCESS | EVAL_SUCCESS, 9, 0, iterations))
        return 1;
    if (start_index < 0 || start_index == 136) {
        if (dst_flt[0] != (float)mpr_time_as_dbl(time_in)) {
            eprintf("... error: expected %g\n", mpr_time_as_dbl(time_in));
            return 1;
        }
        else
            eprintf("... OK\n");
    }

    /* 137) Vector reverse */
    set_expr_str("y=x+[2,4,6,8].rev();");
    setup_test(MPR_INT32, 3, MPR_INT32, 1);
    expect_int[0] = src_int[0] + 8;
    expect_int[1] = src_int[0] + 6;
    expect_int[2] = src_int[0] + 4;
    if (parse_and_eval(PARSE_SUCCESS | EVAL_SUCCESS, 0, 1, iterations))
        return 1;

    /* 138) Inverse trigonometric functions */
    set_expr_str("a=abs(x)%2-1;y=[sin(asin(a[0])),cos(acos(a[1])),tan(atan(a[2]))];");
    setup_test(MPR_FLT, 3, MPR_FLT, 3);
    expect_flt[0] = sinf(asinf(fmodf(fabsf(src_flt[0]), 2.f) - 1));
    expect_flt[1] = cosf(acosf(fmodf(fabsf(src_flt[1]), 2.f) - 1));
    expect_flt[2] = tanf(atanf(fmodf(fabsf(src_flt[2]), 2.f) - 1));
    if (parse_and_eval(PARSE_SUCCESS | EVAL_SUCCESS, 0, 1, iterations))
        return 1;

    /* 139) Inverse trigonometric functions outside bounds: precomputed */
    set_expr_str("y=[sin(asin(2)),cos(acos(-2)),tan(atan(100))];");
    setup_test(MPR_FLT, 3, MPR_FLT, 3);
    expect_flt[0] = sinf(asinf(src_flt[0]));
    expect_flt[1] = cosf(acosf(src_flt[1]));
    expect_flt[2] = tanf(atanf(src_flt[2]));
    if (parse_and_eval(PARSE_FAILURE, 0, 1, iterations))
        return 1;

    /* 140) Inverse trigonometric functions outside bounds: NOT precomputed */
    set_expr_str("y=sin(asin(x+20));");
    setup_test(MPR_FLT, 3, MPR_FLT, 3);
    if (parse_and_eval(PARSE_SUCCESS | EVAL_FAILURE, 0, 1, iterations))
        return 1;

    /* 141) Divide-by-zero: NOT precomputed */
    set_expr_str("y=x/(x-x);");
    setup_test(MPR_FLT, 3, MPR_FLT, 3);
    if (parse_and_eval(PARSE_SUCCESS | EVAL_FAILURE, 0, 1, iterations))
        return 1;

    /* 142) Round/Ceiling/Floor with integer operand (should call pass() function) */
    set_expr_str("y=round(x)+floor(x)+ceil(x);");
    setup_test(MPR_INT32, 1, MPR_INT32, 1);
    expect_int[0] = src_int[0] * 3;
    if (parse_and_eval(PARSE_SUCCESS | EVAL_SUCCESS, 0, 1, iterations))
        return 1;

    /* 143) get instance index */
    set_expr_str("y=#x + x.instance.reduce(a, b -> a * #a + b);");
    setup_test(MPR_INT32, 1, MPR_INT32, 1);
    expect_int[0] = 0;
    if (parse_and_eval(PARSE_SUCCESS | EVAL_SUCCESS, 0, 1, iterations))
        return 1;

//    /* 137) Signal count() */
//    set_expr_str("y=x / x.signal.count();");
//    types[0] = MPR_FLT;
//    types[1] = MPR_INT32;
//    lens[0] = 1;
//    lens[1] = 2;
//    setup_test_multisource(2, types, lens, MPR_FLT, 2);
//    expect_flt[0] = expect_flt[1] = 2.f;
//    if (parse_and_eval(PARSE_SUCCESS | EVAL_SUCCESS, 0, 1, iterations))
//        return 1;

    /* 138) IDEA: map instance reduce to instanced destination */
    // dst instance should be released when there are zero sources
    // e.g. y = x.instance.mean()

    return 0;
}

int main(int argc, char **argv)
{
    int i, j, result = 0;
    /* process flags for -v verbose, -h help */
    for (i = 1; i < argc; i++) {
        if (argv[i] && argv[i][0] == '-') {
            int len = strlen(argv[i]);
            for (j = 1; j < len; j++) {
                switch (argv[i][j]) {
                    case 'h':
                        eprintf("testparser.c: possible arguments "
                                "-q quiet (suppress output), "
                                "-h help, "
                                "-i index (default all), "
                                "--num_iterations <int> (default %d)\n",
                                iterations);
                        return 1;
                        break;
                    case 'q':
                        verbose = 0;
                        break;
                    case 'i':
                        if (++i < argc)
                            start_index = stop_index = atoi(argv[i]);
                        if (i + 1 < argc && strcmp(argv[i + 1], "...")==0) {
                            stop_index = 1000000;
                            ++i;
                        }
                        break;
                    case '-':
                        if (++j < len && strcmp(argv[i]+j, "num_iterations")==0)
                            if (++i < argc)
                                iterations = atoi(argv[i]);
                        j = len;
                        break;
                    default:
                        break;
                }
            }
        }
    }

    for (i = 0; i < MAX_SRC_ARRAY_LEN; i++)
        inh[i] = mpr_value_new(1, MPR_INT32, 1, 0);
    outh = mpr_value_new(1, MPR_INT32, 1, 0);
    for (i = 0; i < MAX_VARS; i++)
        user_vars[i] = mpr_value_new(1, MPR_INT32, 1, 0);

    eprintf("**********************************\n");
    seed_srand();
    eprintf("Generating random inputs:\n");
    eprintf("  int: [");
    for (i = 0; i < MAX_SRC_ARRAY_LEN; i++) {
        src_int[i] = random_int();
        eprintf("%i, ", src_int[i]);
    }
    eprintf("\b\b]\n");

    eprintf("  flt: [");
    for (i = 0; i < MAX_SRC_ARRAY_LEN; i++) {
        src_flt[i] = random_flt();
        eprintf("%g, ", src_flt[i]);
    }
    eprintf("\b\b]\n");

    eprintf("  dbl: [");
    for (i = 0; i < MAX_SRC_ARRAY_LEN; i++) {
        src_dbl[i] = random_dbl();
        eprintf("%g, ", src_dbl[i]);
    }
    eprintf("\b\b]\n");

    eval_buff = mpr_expr_new_eval_buffer(NULL);
    result = run_tests();
    mpr_expr_free_eval_buffer(eval_buff);

    for (i = 0; i < MAX_SRC_ARRAY_LEN; i++)
        mpr_value_free(inh[i]);
    mpr_value_free(outh);
    for (i = 0; i < MAX_VARS; i++)
        mpr_value_free(user_vars[i]);

    eprintf("**********************************\n");
    printf("\r..................................................Test %s\x1B[0m.",
           result ? "\x1B[31mFAILED" : "\x1B[32mPASSED");
    if (!result)
        printf(" (%f seconds, %d tokens).\n", total_elapsed_time, token_count);
    else
        printf("\n");
    return result;
}
