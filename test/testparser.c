#include "../src/mapper_internal.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <time.h>
#include <sys/time.h>
#include <string.h>
#include <unistd.h>
#include <assert.h>

#define SRC_ARRAY_LEN 3
#define DST_ARRAY_LEN 6
#define MAX_VARS 8

int verbose = 1;
char str[256];
mpr_expr e;
int iterations = 20000;
int expression_count = 1;
int token_count = 0;
int update_count;

int src_int[SRC_ARRAY_LEN], dst_int[DST_ARRAY_LEN], expect_int[DST_ARRAY_LEN];
float src_flt[SRC_ARRAY_LEN], dst_flt[DST_ARRAY_LEN], expect_flt[DST_ARRAY_LEN];
double src_dbl[SRC_ARRAY_LEN], dst_dbl[DST_ARRAY_LEN], expect_dbl[DST_ARRAY_LEN];
double then, now;
double total_elapsed_time = 0;
mpr_type out_types[DST_ARRAY_LEN];

mpr_time time_in = {0, 0}, time_out = {0, 0};

/* signal_history structures */
mpr_value_t inh[SRC_ARRAY_LEN], outh, user_vars[MAX_VARS], *user_vars_p;
mpr_value inh_p[SRC_ARRAY_LEN];
mpr_type src_types[SRC_ARRAY_LEN], dst_type;
int src_lens[SRC_ARRAY_LEN], n_sources, dst_len;

static void eprintf(const char *format, ...)
{
    va_list args;
    if (!verbose)
        return;
    va_start(args, format);
    vprintf(format, args);
    va_end(args);
}

/*! Internal function to get the current time. */
static double current_time()
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (double) tv.tv_sec + tv.tv_usec / 1000000.0;
}

typedef struct _var {
    char *name;
    mpr_type datatype;
    mpr_type casttype;
    uint8_t vec_len;
    char vec_len_locked;
    char assigned;
    char public;
} mpr_var_t, *mpr_var;

struct _mpr_expr
{
    void *tokens;
    void *start;
    mpr_var vars;
    uint8_t offset;
    uint8_t n_tokens;
    uint8_t stack_size;
    uint8_t vec_size;
    uint8_t *in_mem;
    uint8_t out_mem;
    uint8_t n_vars;
    int8_t inst_ctl;
    int8_t mute_ctl;
};

/*! A helper function to seed the random number generator. */
static void seed_srand()
{
    unsigned int s;
    double d;

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

    d = mpr_get_current_time();
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

int check_result(mpr_type *types, int len, const void *val, int pos, int check)
{
    int i, offset = pos * len, error = -1;
    if (!val || !types || len < 1)
        return 1;

    switch (dst_type) {
        case MPR_INT32:
            memcpy(dst_int, mpr_value_get_samp(&outh, 0), sizeof(int) * len);
            break;
        case MPR_FLT:
            memcpy(dst_flt, mpr_value_get_samp(&outh, 0), sizeof(float) * len);
            break;
        default:
            memcpy(dst_dbl, mpr_value_get_samp(&outh, 0), sizeof(double) * len);
            break;
    }
    memcpy(&time_out, mpr_value_get_time(&outh, 0), sizeof(mpr_time));

    eprintf("Got: ");
    if (len > 1)
        eprintf("[");

    for (i = 0; i < len; i++) {
        switch (types[i]) {
            case MPR_NULL:
                eprintf("NULL, ");
                break;
            case MPR_INT32:
            {
                int *pi = (int*)val;
                eprintf("%d, ", pi[i + offset]);
                if (check && pi[i + offset] != expect_int[i])
                    error = i;
                break;
            }
            case MPR_FLT:
            {
                float *pf = (float*)val;
                eprintf("%g, ", pf[i + offset]);
                if (check && pf[i + offset] != expect_flt[i])
                    error = i;
                break;
            }
            case MPR_DBL:
            {
                double *pd = (double*)val;
                eprintf("%g, ", pd[i + offset]);
                if (check && pd[i + offset] != expect_dbl[i])
                    error = i;
                break;
            }
            default:
                eprintf("\nTYPE ERROR\n");
                return 1;
        }
    }

    if (len > 1)
        eprintf("\b\b]");
    else
        eprintf("\b\b");
    if (!check)
        return 0;
    if (error >= 0) {
        eprintf("... error at index %d ", error);
        switch (types[error]) {
            case MPR_NULL:
                eprintf("(expected NULL)\n");
                break;
            case MPR_INT32:
                eprintf("(expected %d)\n", expect_int[error]);
                break;
            case MPR_FLT:
                eprintf("(expected %g)\n", expect_flt[error]);
                break;
            case MPR_DBL:
                eprintf("(expected %g)\n", expect_dbl[error]);
                break;
        }
        return 1;
    }
    eprintf("... OK\n");
    return 0;
}

void setup_test_multisource(int _n_sources, mpr_type *_src_types, int *_src_lens,
                            mpr_type _dst_type, int _dst_len)
{
    int i;
    n_sources = _n_sources;
    for (i = 0; i < _n_sources; i++) {
        src_types[i] = _src_types[i];
        src_lens[i] = _src_lens[i];
    }
    dst_len = _dst_len;
    dst_type = _dst_type;
}

void setup_test(mpr_type in_type, int in_len, mpr_type out_type, int out_len)
{
    setup_test_multisource(1, &in_type, &in_len, out_type, out_len);
}

#define EXPECT_SUCCESS 0
#define EXPECT_FAILURE 1

int parse_and_eval(int expectation, int max_tokens, int check, int exp_updates)
{
    /* clear output arrays */
    int i, j, result = 0, mlen, status;

    if (verbose) {
        printf("***************** Expression %d *****************\n", expression_count++);
        printf("Parsing string '%s'\n", str);
    }
    else {
        printf("\rExpression %d", expression_count++);
        fflush(stdout);
    }
    e = mpr_expr_new_from_str(str, n_sources, src_types, src_lens, dst_type, dst_len);
    if (!e) {
        eprintf("Parser FAILED (expression %d)\n", expression_count - 1);
        goto fail;
    }
    else if (EXPECT_FAILURE == expectation) {
        eprintf("Error: expected FAILURE\n");
        result = 1;
        goto free;
    }
    mpr_time_set(&time_in, MPR_NOW);
    for (i = 0; i < n_sources; i++) {
        void *v;
        mlen = mpr_expr_get_in_hist_size(e, i);
        mpr_value_realloc(&inh[i], src_lens[i], src_types[i], mlen, 1, 0);
        inh[i].inst[0].pos = 0;
        v = mpr_value_get_samp(&inh[i], 0);
        switch (src_types[i]) {
            case MPR_INT32:
                memcpy(v, src_int, sizeof(int) * src_lens[i]);
                break;
            case MPR_FLT:
                memcpy(v, src_flt, sizeof(float) * src_lens[i]);
                break;
            case MPR_DBL:
                memcpy(v, src_dbl, sizeof(double) * src_lens[i]);
                break;
            default:
                assert(0);
        }
        memcpy(inh[i].inst[0].times, &time_in, sizeof(mpr_time));
    }
    mlen = mpr_expr_get_out_hist_size(e);
    mpr_value_realloc(&outh, dst_len, dst_type, mlen, 1, 1);

    /* mpr_value_realloc will not initialize memory if history size is unchanged
     * so we will explicitly initialise it here. */
    for (i = 0; i < n_sources; i++) {
        int samp_size;
        if (inh[i].mlen <= 1)
            continue;
        samp_size = inh[i].vlen * mpr_type_get_size(inh[i].type);
        memset((char*)inh[i].inst[0].samps + samp_size, 0, (inh[i].mlen - 1) * samp_size);
    }
    memset(outh.inst[0].samps, 0, outh.mlen * outh.vlen * mpr_type_get_size(outh.type));

    if (mpr_expr_get_num_vars(e) > MAX_VARS) {
        eprintf("Maximum variables exceeded.\n");
        goto fail;
    }

    /* reallocate variable value histories */
    for (i = 0; i < e->n_vars; i++) {
        /* eprintf("user_var[%d]: %p\n", i, &user_vars[i]); */
        int vlen = mpr_expr_get_var_vec_len(e, i);
        mpr_value_realloc(&user_vars[i], vlen, MPR_DBL, 1, 1, 0);

        /* mpr_value_realloc will not initialize memory if history size is
         * unchanged so we will explicitly initialise it here. */
        memset(user_vars[i].inst[0].samps, 0, vlen * mpr_type_get_size(MPR_DBL));
    }
    user_vars_p = user_vars;

    eprintf("Parser returned %d tokens...", e->n_tokens);
    if (max_tokens && e->n_tokens > max_tokens) {
        eprintf(" (expected %d)\n", max_tokens);
        result = 1;
        goto free;
    }
    else {
        eprintf(" OK\n");
    }

#ifdef DEBUG
    if (verbose) {
        char str[128];
        snprintf(str, 128, "    ");
        printexpr(str, e);
    }
#endif

    token_count += e->n_tokens;

    update_count = 0;
    then = current_time();

    eprintf("Try evaluation once... ");
    status = mpr_expr_eval(e, inh_p, &user_vars_p, &outh, &time_in, out_types, 0);
    if (!status) {
        eprintf("FAILED.\n");
        result = 1;
        goto free;
    }
    else if (status & MPR_SIG_UPDATE)
        ++update_count;
    eprintf("OK\n");

    eprintf("Calculate expression %i more times... ", iterations-1);
    fflush(stdout);
    i = iterations-1;
    while (i--) {
        /* update timestamp */
        mpr_time_set(&time_in, MPR_NOW);
        /* copy src values */
        for (j = 0; j < n_sources; j++) {
            int samp_size = inh[j].vlen * mpr_type_get_size(inh[j].type);
            inh[j].inst[0].pos = ((inh[j].inst[0].pos + 1) % inh[j].mlen);
            switch (inh[j].type) {
                case MPR_INT32:
                    memcpy(mpr_value_get_samp(&inh[j], 0), src_int, samp_size);
                    break;
                case MPR_FLT:
                    memcpy(mpr_value_get_samp(&inh[j], 0), src_flt, samp_size);
                    break;
                case MPR_DBL:
                    memcpy(mpr_value_get_samp(&inh[j], 0), src_dbl, samp_size);
                    break;
                default:
                    assert(0);
            }
            memcpy(mpr_value_get_time(&inh[j], 0), &time_in, sizeof(mpr_time));
        }
        status = mpr_expr_eval(e, inh_p, &user_vars_p, &outh, &time_in, out_types, 0);
        if (status & MPR_SIG_UPDATE)
            ++update_count;
        /* sleep here stops compiler from optimizing loop away */
        usleep(1);
    }
    now = current_time();
    total_elapsed_time += now-then;

    if (0 == result)
        eprintf("OK\n");

    if (check_result(out_types, outh.vlen, outh.inst[0].samps, outh.inst[0].pos, check))
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
    mpr_expr_free(e);
    return result;

fail:
    if (EXPECT_FAILURE == expectation) {
        eprintf(" (expected)\n");
        return 0;
    }
    return 1;
}

int run_tests()
{
    int i;
    mpr_type types[3] = {MPR_INT32, MPR_FLT, MPR_DBL};
    int lens[3] = {2, 3, 2};

    /* 1) Complex string */
    /* TODO: ensure successive constant multiplications are optimized */
    /* TODO: ensure split successive constant additions are optimized */
    snprintf(str, 256, "y=26*2/2+log10(pi)+2.*pow(2,1*(3+7*.1)*1.1+x{0}[0])*3*4+cos(2.)");
    setup_test(MPR_FLT, 1, MPR_FLT, 1);
    expect_flt[0] = 26*2/2+log10f(M_PI)+2.f*powf(2,1*(3+7*.1f)*1.1f+src_flt[0])*3*4+cosf(2.0f);
    if (parse_and_eval(EXPECT_SUCCESS, 0, 1, iterations))
        return 1;

    /* 2) Building vectors, conditionals */
    snprintf(str, 256, "y=(x>1)?[1,2,3]:[2,4,6]");
    setup_test(MPR_FLT, 3, MPR_INT32, 3);
    expect_int[0] = src_flt[0] > 1 ? 1 : 2;
    expect_int[1] = src_flt[1] > 1 ? 2 : 4;
    expect_int[2] = src_flt[2] > 1 ? 3 : 6;
    if (parse_and_eval(EXPECT_SUCCESS, 0, 1, iterations))
        return 1;

    /* 3) Conditionals with shortened syntax */
    snprintf(str, 256, "y=x?:123");
    setup_test(MPR_FLT, 1, MPR_INT32, 1);
    expect_int[0] = (int)(src_flt[0] ? src_flt[0] : 123);
    if (parse_and_eval(EXPECT_SUCCESS, 0, 1, iterations))
        return 1;

    /* 4) Conditional that should be optimized */
    snprintf(str, 256, "y=1?2:123");
    setup_test(MPR_FLT, 1, MPR_INT32, 1);
    expect_int[0] = 2;
    if (parse_and_eval(EXPECT_SUCCESS, 2, 1, iterations))
        return 1;

    /* 5) Building vectors with variables, operations inside vector-builder */
    snprintf(str, 256, "y=[x*-2+1,0]");
    setup_test(MPR_INT32, 2, MPR_DBL, 3);
    expect_dbl[0] = (double)src_int[0] * -2 + 1;
    expect_dbl[1] = (double)src_int[1] * -2 + 1;
    expect_dbl[2] = 0.0;
    if (parse_and_eval(EXPECT_SUCCESS, 0, 1, iterations))
        return 1;

    /* 6) Building vectors with variables, operations inside vector-builder */
    snprintf(str, 256, "y=[-99.4, -x*1.1+x]");
    setup_test(MPR_INT32, 2, MPR_DBL, 3);
    expect_dbl[0] = -99.4f;
    expect_dbl[1] = -((double)src_int[0]) * 1.1f + ((double)src_int[0]);
    expect_dbl[2] = -((double)src_int[1]) * 1.1f + ((double)src_int[1]);
    if (parse_and_eval(EXPECT_SUCCESS, 0, 1, iterations))
        return 1;

    /* 7) Indexing vectors by range */
    snprintf(str, 256, "y=x[1:2]+100");
    setup_test(MPR_DBL, 3, MPR_FLT, 2);
    expect_flt[0] = (float)(src_dbl[1] + 100);
    expect_flt[1] = (float)(src_dbl[2] + 100);
    if (parse_and_eval(EXPECT_SUCCESS, 0, 1, iterations))
        return 1;

    /* 8) Typical linear scaling expression with vectors */
    snprintf(str, 256, "y=x*[0.1,3.7,-.1112]+[2,1.3,9000]");
    setup_test(MPR_FLT, 3, MPR_FLT, 3);
    expect_flt[0] = src_flt[0] * 0.1f + 2.f;
    expect_flt[1] = src_flt[1] * 3.7f + 1.3f;
    expect_flt[2] = src_flt[2] * -.1112f + 9000.f;
    if (parse_and_eval(EXPECT_SUCCESS, 0, 1, iterations))
        return 1;

    /* 9) Check type and vector length promotion of operation sequences */
    snprintf(str, 256, "y=1+2*3-4*x");
    setup_test(MPR_FLT, 2, MPR_FLT, 2);
    expect_flt[0] = 1. + 2. * 3. - 4. * src_flt[0];
    expect_flt[1] = 1. + 2. * 3. - 4. * src_flt[1];
    if (parse_and_eval(EXPECT_SUCCESS, 0, 1, iterations))
        return 1;

    /* 10) Swizzling, more pre-computation */
    snprintf(str, 256, "y=[x[2],x[0]]*0+1+12");
    setup_test(MPR_FLT, 3, MPR_FLT, 2);
    expect_flt[0] = src_flt[2] * 0. + 1. + 12.;
    expect_flt[1] = src_flt[0] * 0. + 1. + 12.;
    if (parse_and_eval(EXPECT_SUCCESS, 0, 1, iterations))
        return 1;

    /* 11) Logical negation */
    snprintf(str, 256, "y=!(x[1]*0)");
    setup_test(MPR_DBL, 3, MPR_INT32, 1);
    expect_int[0] = (int)!(src_dbl[1] && 0);
    if (parse_and_eval(EXPECT_SUCCESS, 0, 1, iterations))
        return 1;

    /* 12) any() */
    snprintf(str, 256, "y=(x-1).any()");
    setup_test(MPR_DBL, 3, MPR_INT32, 1);
    expect_int[0] =    ((int)src_dbl[0] - 1) ? 1 : 0
                     | ((int)src_dbl[1] - 1) ? 1 : 0
                     | ((int)src_dbl[2] - 1) ? 1 : 0;
    if (parse_and_eval(EXPECT_SUCCESS, 0, 1, iterations))
        return 1;

    /* 13) all() */
    snprintf(str, 256, "y=x[2]*(x-1).all()");
    setup_test(MPR_DBL, 3, MPR_INT32, 1);
    expect_int[0] = (int)src_dbl[2] * (  (((int)src_dbl[0] - 1) ? 1 : 0)
                                       & (((int)src_dbl[1] - 1) ? 1 : 0)
                                       & (((int)src_dbl[2] - 1) ? 1 : 0));
    if (parse_and_eval(EXPECT_SUCCESS, 0, 1, iterations))
        return 1;

    /* 14) pi and e, extra spaces */
    snprintf(str, 256, "y=x + pi -     e");
    setup_test(MPR_DBL, 1, MPR_FLT, 1);
    expect_flt[0] = (float)(src_dbl[0] + M_PI - M_E);
    if (parse_and_eval(EXPECT_SUCCESS, 0, 1, iterations))
        return 1;

    /* 15) Bad vector notation */
    snprintf(str, 256, "y=(x-2)[1]");
    setup_test(MPR_INT32, 1, MPR_INT32, 1);
    if (parse_and_eval(EXPECT_FAILURE, 0, 1, iterations))
        return 1;

    /* 16) Vector index outside bounds */
    snprintf(str, 256, "y=x[3]");
    setup_test(MPR_INT32, 3, MPR_INT32, 1);
    if (parse_and_eval(EXPECT_FAILURE, 0, 1, iterations))
        return 1;

    /* 17) Vector length mismatch */
    snprintf(str, 256, "y=x[1:2]");
    setup_test(MPR_INT32, 3, MPR_INT32, 1);
    expect_int[0] = src_int[1];
    if (parse_and_eval(EXPECT_SUCCESS, 0, 1, iterations))
        return 1;

    /* 18) Unnecessary vector notation */
    snprintf(str, 256, "y=x+[1]");
    setup_test(MPR_INT32, 1, MPR_INT32, 1);
    expect_int[0] = src_int[0] + 1;
    if (parse_and_eval(EXPECT_SUCCESS, 4, 1, iterations))
        return 1;

    /* 19) Invalid history index */
    snprintf(str, 256, "y=x{-101}");
    setup_test(MPR_INT32, 1, MPR_INT32, 1);
    if (parse_and_eval(EXPECT_FAILURE, 0, 1, iterations))
        return 1;

    /* 20) Invalid history index */
    snprintf(str, 256, "y=x-y{-101}");
    setup_test(MPR_INT32, 1, MPR_INT32, 1);
    if (parse_and_eval(EXPECT_FAILURE, 0, 1, iterations))
        return 1;

    /* 21) Scientific notation */
    snprintf(str, 256, "y=x[1]*1.23e-20");
    setup_test(MPR_INT32, 2, MPR_DBL, 1);
    expect_dbl[0] = (double)src_int[1] * 1.23e-20;
    if (parse_and_eval(EXPECT_SUCCESS, 0, 1, iterations))
        return 1;

    /* 22) Vector assignment */
    snprintf(str, 256, "y[1]=x[1]");
    setup_test(MPR_DBL, 3, MPR_INT32, 3);
    expect_int[1] = (int)src_dbl[1];
    if (parse_and_eval(EXPECT_SUCCESS, 0, 1, iterations))
        return 1;

    /* 23) Vector assignment */
    snprintf(str, 256, "y[1:2]=[x[1],10]");
    setup_test(MPR_DBL, 3, MPR_INT32, 3);
    expect_int[1] = (int)src_dbl[1];
    expect_int[2] = 10;
    if (parse_and_eval(EXPECT_SUCCESS, 0, 1, iterations))
        return 1;

    /* 24) Output vector swizzling */
    snprintf(str, 256, "[y[0],y[2]]=x[1:2]");
    setup_test(MPR_FLT, 3, MPR_DBL, 3);
    expect_dbl[0] = (double)src_flt[1];
    expect_dbl[2] = (double)src_flt[2];
    if (parse_and_eval(EXPECT_SUCCESS, 0, 1, iterations))
        return 1;

    /* 25) Multiple expressions */
    snprintf(str, 256, "y[0]=x*100-23.5; y[2]=100-x*6.7");
    setup_test(MPR_INT32, 1, MPR_FLT, 3);
    expect_flt[0] = src_int[0] * 100.f - 23.5f;
    expect_flt[2] = 100.f - src_int[0] * 6.7f;
    if (parse_and_eval(EXPECT_SUCCESS, 0, 1, iterations))
        return 1;

    /* 26) Error check: separating sub-expressions with commas */
    snprintf(str, 256, "foo=1,  y=y{-1}+foo");
    setup_test(MPR_INT32, 1, MPR_FLT, 1);
    if (parse_and_eval(EXPECT_FAILURE, 0, 1, iterations))
        return 1;

    /* 27) Initialize filters */
    snprintf(str, 256, "y=x+y{-1}; y{-1}=100");
    setup_test(MPR_INT32, 1, MPR_INT32, 1);
    expect_int[0] = src_int[0] * iterations + 100;
    if (parse_and_eval(EXPECT_SUCCESS, 0, 1, iterations))
        return 1;

    /* 28) Initialize filters + vector index */
    snprintf(str, 256, "y=x+y{-1}; y[1]{-1}=100");
    setup_test(MPR_INT32, 2, MPR_INT32, 2);
    expect_int[0] = src_int[0] * iterations;
    expect_int[1] = src_int[1] * iterations + 100;
    if (parse_and_eval(EXPECT_SUCCESS, 0, 1, iterations))
        return 1;

    /* 29) Initialize filters + vector index */
    snprintf(str, 256, "y=x+y{-1}; y{-1}=[100,101]");
    setup_test(MPR_INT32, 2, MPR_INT32, 2);
    expect_int[0] = src_int[0] * iterations + 100;
    expect_int[1] = src_int[1] * iterations + 101;
    if (parse_and_eval(EXPECT_SUCCESS, 0, 1, iterations))
        return 1;

    /* 30) Initialize filters */
    snprintf(str, 256, "y=x+y{-1}; y[0]{-1}=100; y[2]{-1}=200");
    setup_test(MPR_INT32, 3, MPR_INT32, 3);
    expect_int[0] = src_int[0] * iterations + 100;
    expect_int[1] = src_int[1] * iterations;
    expect_int[2] = src_int[2] * iterations + 200;
    if (parse_and_eval(EXPECT_SUCCESS, 0, 1, iterations))
        return 1;

    /* 31) Initialize filters */
    snprintf(str, 256, "y=x+y{-1}-y{-2}; y{-1}=[100,101]; y{-2}=[102,103]");
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
    if (parse_and_eval(EXPECT_SUCCESS, 0, 1, iterations))
        return 1;

    /* 32) Only initialize */
    snprintf(str, 256, "y{-1}=100");
    setup_test(MPR_INT32, 3, MPR_INT32, 1);
    if (parse_and_eval(EXPECT_FAILURE, 0, 1, iterations))
        return 1;

    /* 33) Bad syntax */
    snprintf(str, 256, " ");
    setup_test(MPR_INT32, 1, MPR_FLT, 3);
    if (parse_and_eval(EXPECT_FAILURE, 0, 1, iterations))
        return 1;

    /* 34) Bad syntax */
    snprintf(str, 256, " ");
    setup_test(MPR_INT32, 1, MPR_FLT, 3);
    if (parse_and_eval(EXPECT_FAILURE, 0, 1, iterations))
        return 1;

    /* 35) Bad syntax */
    snprintf(str, 256, "y");
    setup_test(MPR_INT32, 1, MPR_FLT, 3);
    if (parse_and_eval(EXPECT_FAILURE, 0, 1, iterations))
        return 1;

    /* 36) Bad syntax */
    snprintf(str, 256, "y=");
    setup_test(MPR_INT32, 1, MPR_FLT, 3);
    if (parse_and_eval(EXPECT_FAILURE, 0, 1, iterations))
        return 1;

    /* 37) Bad syntax */
    snprintf(str, 256, "=x");
    setup_test(MPR_INT32, 1, MPR_FLT, 3);
    if (parse_and_eval(EXPECT_FAILURE, 0, 1, iterations))
        return 1;

    /* 38) sin */
    snprintf(str, 256, "sin(x)");
    setup_test(MPR_INT32, 1, MPR_FLT, 3);
    if (parse_and_eval(EXPECT_FAILURE, 0, 1, iterations))
        return 1;

    /* 39) Variable declaration */
    snprintf(str, 256, "y=x+var; var=[3.5,0]");
    setup_test(MPR_INT32, 2, MPR_FLT, 2);
    expect_flt[0] = src_int[0] + 3.5f;
    expect_flt[1] = src_int[1];
    if (parse_and_eval(EXPECT_SUCCESS, 0, 1, iterations))
        return 1;

    /* 40) Variable declaration */
    snprintf(str, 256, "ema=ema{-1}*0.9+x*0.1; y=ema*2; ema{-1}=90");
    setup_test(MPR_INT32, 1, MPR_FLT, 1);
    expect_flt[0] = 90;
    for (i = 0; i < iterations; i++)
        expect_flt[0] = expect_flt[0] * 0.9f + src_int[0] * 0.1f;
    expect_flt[0] *= 2;
    if (parse_and_eval(EXPECT_SUCCESS, 0, 1, iterations))
        return 1;

    /* 41) Multiple variable declaration */
    snprintf(str, 256, "a=1.1; b=2.2; c=3.3; y=x+a-b*c");
    setup_test(MPR_INT32, 1, MPR_FLT, 1);
    expect_flt[0] = (float)src_int[0] + 1.1 - 2.2 * 3.3;
    if (parse_and_eval(EXPECT_SUCCESS, 0, 1, iterations))
        return 1;

    /* 42) Malformed variable declaration */
    snprintf(str, 256, "y=x + myvariable * 10");
    setup_test(MPR_INT32, 1, MPR_FLT, 1);
    if (parse_and_eval(EXPECT_FAILURE, 0, 1, iterations))
        return 1;

    /* 43) Vector functions mean() and sum() */
    snprintf(str, 256, "y=x.mean()==(x.sum()/3)");
    setup_test(MPR_FLT, 3, MPR_INT32, 1);
    expect_int[0] = 1;
    if (parse_and_eval(EXPECT_SUCCESS, 0, 1, iterations))
        return 1;

    /* 44) Overloaded vector functions max() and min() */
    snprintf(str, 256, "y=x.max()-x.min()*max(x[0],1)");
    setup_test(MPR_FLT, 3, MPR_INT32, 1);
    expect_int[0] = (  ((src_flt[0] > src_flt[1]) ?
                        (src_flt[0] > src_flt[2] ? src_flt[0] : src_flt[2]) :
                        (src_flt[1] > src_flt[2] ? src_flt[1] : src_flt[2]))
                     - ((src_flt[0] < src_flt[1]) ?
                        (src_flt[0] < src_flt[2] ? src_flt[0] : src_flt[2]) :
                        (src_flt[1] < src_flt[2] ? src_flt[1] : src_flt[2]))
                     * (src_flt[0] > 1 ? src_flt[0] : 1));
    if (parse_and_eval(EXPECT_SUCCESS, 0, 1, iterations))
        return 1;

    /* 45) Vector function: norm() */
    snprintf(str, 256, "y=x.norm();");
    setup_test(MPR_INT32, 2, MPR_FLT, 1);
    expect_flt[0] = sqrtf(powf((float)src_int[0], 2) + powf((float)src_int[1], 2));
    if (parse_and_eval(EXPECT_SUCCESS, 0, 1, iterations))
        return 1;

    /* 46) Optimization: operations by zero */
    snprintf(str, 256, "y=0*sin(x)*200+1.1");
    setup_test(MPR_INT32, 1, MPR_FLT, 1);
    expect_flt[0] = 1.1;
    if (parse_and_eval(EXPECT_SUCCESS, 2, 1, iterations))
        return 1;

    /* 47) Optimization: operations by one */
    snprintf(str, 256, "y=x*1");
    setup_test(MPR_INT32, 1, MPR_FLT, 1);
    expect_flt[0] = (float)src_int[0];
    if (parse_and_eval(EXPECT_SUCCESS, 2, 1, iterations))
        return 1;

    /* 48) Error check: division by zero */
    snprintf(str, 256, "y=x/0");
    setup_test(MPR_INT32, 1, MPR_FLT, 1);
    if (parse_and_eval(EXPECT_FAILURE, 0, 1, iterations))
        return 1;

    /* 49) Multiple Inputs */
    snprintf(str, 256, "y=x+x1[1:2]+x2");
    setup_test_multisource(3, types, lens, MPR_FLT, 2);
    expect_flt[0] = (float)((double)src_int[0] + (double)src_flt[1] + src_dbl[0]);
    expect_flt[1] = (float)((double)src_int[1] + (double)src_flt[2] + src_dbl[1]);
    if (parse_and_eval(EXPECT_SUCCESS, 0, 1, iterations))
        return 1;

    /* 50) Functions with memory: ema() */
    snprintf(str, 256, "y=x-ema(x,0.1)+2");
    setup_test(MPR_INT32, 1, MPR_FLT, 1);
    expect_flt[0] = 0;
    for (i = 0; i < iterations; i++)
        expect_flt[0] = expect_flt[0] * 0.9f + src_int[0] * 0.1f;
    expect_flt[0] = src_int[0] - expect_flt[0] + 2.f;
    if (parse_and_eval(EXPECT_SUCCESS, 0, 1, iterations))
        return 1;

    /* 51) Functions with memory: schmitt() */
    snprintf(str, 256, "y=y{-1}+(schmitt(y{-1},20,80)?-1:1)");
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
    if (parse_and_eval(EXPECT_SUCCESS, 0, 1, iterations))
        return 1;

    /* 52) Multiple output assignment */
    snprintf(str, 256, "y=x-10000; y=max(min(y,1),0)");
    setup_test(MPR_FLT, 1, MPR_FLT, 1);
    expect_flt[0] = src_flt[0] - 10000;
    expect_flt[0] = expect_flt[0] < 0 ? 0 : expect_flt[0];
    expect_flt[0] = expect_flt[0] > 0 ? 1 : expect_flt[0];
    if (parse_and_eval(EXPECT_SUCCESS, 0, 1, iterations))
        return 1;

    /* 53) Access timetags */
    snprintf(str, 256, "y=t_x");
    setup_test(MPR_INT32, 1, MPR_DBL, 1);
    parse_and_eval(EXPECT_SUCCESS, 0, 0, iterations);
    if (dst_dbl[0] != mpr_time_as_dbl(time_in))
        eprintf("... error: expected %g\n", mpr_time_as_dbl(time_in));
    else
        eprintf("... OK\n");

    /* 54) Access timetags from past samples */
    snprintf(str, 256, "y=t_x-t_y{-1}");
    setup_test(MPR_INT32, 1, MPR_DBL, 1);
    parse_and_eval(EXPECT_SUCCESS, 0, 0, iterations);
    /* results may vary depending on machine but we can perform a sanity check */
    if (dst_dbl[0] < 0.0 || dst_dbl[0] > 0.001) {
        eprintf("... error: expected value between %g and %g\n", 0.0, 0.001);
        printf("%g < %g... %d\n", dst_dbl[0], 0.0, dst_dbl[0] < 0.0);
        printf("%g > %g... %d\n", dst_dbl[0], 0.0001, dst_dbl[0] > 0.001);
        return 1;
    }
    else
        eprintf("... OK\n");

    /* 55) Moving average of inter-sample period */
    /* Tricky - we need to init y{-1}.tt to x.tt or the first calculated
     * difference will be enormous! */
    snprintf(str, 256,
             "t_y{-1}=t_x;"
             "period=t_x-t_y{-1};"
             "y=y{-1}*0.9+period*0.1;");
    setup_test(MPR_INT32, 1, MPR_DBL, 1);
    parse_and_eval(EXPECT_SUCCESS, 0, 0, iterations);
    /* results may vary depending on machine but we can perform a sanity check */
    if (dst_dbl[0] < 0. || dst_dbl[0] > 0.001) {
        eprintf("... error: expected value between %g and %g\n", 0.0, 0.001);
        return 1;
    }
    else
        eprintf("... OK\n");

    /* 56) Moving average of inter-sample jitter */
    /* Tricky - we need to init y{-1}.tt to x.tt or the first calculated
     * difference will be enormous! */
    snprintf(str, 256,
             "t_y{-1}=t_x;"
             "interval=t_x-t_y{-1};"
             "sr=sr{-1}*0.9+interval*0.1;"
             "y=y{-1}*0.9+abs(interval-sr)*0.1;");
    setup_test(MPR_INT32, 1, MPR_DBL, 1);
    parse_and_eval(EXPECT_SUCCESS, 0, 0, iterations);
    /* results may vary depending on machine but we can perform a sanity check */
    if (dst_dbl[0] < 0. || dst_dbl[0] > 0.0001) {
        eprintf("... error: expected value between %g and %g\n", 0.0, 0.0001);
        printf("%g < %g... %d\n", dst_dbl[0], 0.0, dst_dbl[0] < 0.0);
        printf("%g > %g... %d\n", dst_dbl[0], 0.0001, dst_dbl[0] > 0.0001);
        return 1;
    }
    else
        eprintf("... OK\n");

    /* 57) Expression for limiting output rate */
    snprintf(str, 256,
             "t_y{-1}=t_x;"
             "diff=t_x-t_y{-1};"
             "alive=diff>0.1;"
             "y=x;");
    setup_test(MPR_INT32, 1, MPR_INT32, 1);
    expect_int[0] = src_int[0];
    if (parse_and_eval(EXPECT_SUCCESS, 0, 1, -1))
        return 1;

    /* 58) Expression for limiting rate with smoothed output */
    snprintf(str, 256,
             "t_y{-1}=t_x;"
             "alive=(t_x-t_y{-1})>0.1;"
             "agg=!alive*agg+x;"
             "samps=alive?1:samps+1;"
             "y=agg/samps;");
    setup_test(MPR_INT32, 1, MPR_INT32, 1);
    expect_dbl[0] = (double)src_int[0];
    if (parse_and_eval(EXPECT_SUCCESS, 0, 1, -1))
        return 1;

    /* 59) Manipulate timetag directly. This functionality may be used in the
     *     future to schedule delays, however it currently will not affect
     *     message timing. Disabled for now. */
    snprintf(str, 256, "y=x[0]{0}; t_y=t_x+10");
    setup_test(MPR_INT32, 1, MPR_INT32, 1);
    expect_int[0] = src_int[0];
    if (parse_and_eval(EXPECT_FAILURE, 0, 1, iterations))
        return 1;
/*     mpr_time time = *(mpr_time*)mpr_value_get_time(&outh, 0);
     if (mpr_time_as_dbl(time) != mpr_time_as_dbl(time_in) + 10) {
         eprintf("Expected timestamp {%"PRIu32", %"PRIu32"} but got "
                 "{%"PRIu32", %"PRIu32"}\n", time_in.sec+10, time_in.frac,
                 time.sec, time.frac);
         return 1;
     }
 */

    /* 60) Faulty timetag syntax */
    snprintf(str, 256, "y=t_x-y;");
    setup_test(MPR_INT32, 1, MPR_INT32, 1);
    if (parse_and_eval(EXPECT_FAILURE, 0, 1, iterations))
        return 1;

    /* 61) Instance management */
    snprintf(str, 256, "%s", "count{-1}=0;alive=count>=5;y=x;count=(count+1)%10;");
    setup_test(MPR_INT32, 1, MPR_INT32, 1);
    expect_int[0] = src_int[0];
    if (parse_and_eval(EXPECT_SUCCESS, 0, 1, -1))
        return 1;
    if (abs(iterations / 2 - update_count) > 4) {
        eprintf("error: expected approximately %d updates\n", iterations / 2);
        return 1;
    }

    /* 62) Filter unchanged values */
    snprintf(str, 256, "muted=(x==x{-1});y=x;");
    setup_test(MPR_INT32, 1, MPR_INT32, 1);
    expect_int[0] = src_int[0];
    if (parse_and_eval(EXPECT_SUCCESS, 0, 1, 1))
        return 1;

    /* 63) Buddy logic */
    snprintf(str, 256, "alive=(t_x0>t_y{-1})&&(t_x1>t_y{-1});y=x0+x1[1:2];");
    /* types[] and lens[] are already defined */
    setup_test_multisource(2, types, lens, MPR_FLT, 2);
    expect_flt[0] = src_int[0] + src_flt[1];
    expect_flt[1] = src_int[1] + src_flt[2];
    if (parse_and_eval(EXPECT_SUCCESS, 0, 1, iterations))
        return 1;

    /* 64) Variable delays */
    snprintf(str, 256, "y=x{abs(x%%10)-10,10}");
    setup_test(MPR_INT32, 1, MPR_INT32, 1);
    if (parse_and_eval(EXPECT_SUCCESS, 0, 0, iterations))
        return 1;

    /* 65) Variable delay with missing maximum */
    snprintf(str, 256, "y=x{abs(x%%10)-10}");
    setup_test(MPR_INT32, 1, MPR_INT32, 1);
    if (parse_and_eval(EXPECT_FAILURE, 0, 0, iterations))
        return 1;

    /* 66) Calling delay() function explicity */
    snprintf(str, 256, "y=delay(x, abs(x%%10)-10), 10)");
    setup_test(MPR_INT32, 1, MPR_INT32, 1);
    if (parse_and_eval(EXPECT_FAILURE, 0, 0, iterations))
        return 1;

    /* 67) Fractional delays */
    snprintf(str, 256, "ratio{-1}=0;y=x{-10+ratio, 10};ratio=(ratio+0.01)%%5;");
    setup_test(MPR_INT32, 1, MPR_INT32, 1);
    if (parse_and_eval(EXPECT_SUCCESS, 0, 0, iterations))
        return 1;

    /* 68) Pooled instance functions: any() and all() */
    snprintf(str, 256, "y=(x-1).instances().any() + (x+1).instances().all();");
    setup_test(MPR_INT32, 1, MPR_INT32, 1);
    expect_int[0] = 2;
    if (parse_and_eval(EXPECT_SUCCESS, 0, 1, iterations))
        return 1;

    /* 69) Pooled instance functions: sum(), count() and mean() */
    snprintf(str, 256, "y=(x.instances().sum()/x.instances().count())==x.instances().mean();");
    setup_test(MPR_INT32, 1, MPR_INT32, 1);
    expect_int[0] = 1;
    if (parse_and_eval(EXPECT_SUCCESS, 0, 1, iterations))
        return 1;

    /* 70) Pooled instance functions: max(), min(), and size() */
    snprintf(str, 256, "y=(x.instances().max()-x.instances().min())==x.instances().size();");
    setup_test(MPR_INT32, 1, MPR_INT32, 1);
    expect_int[0] = 1;
    if (parse_and_eval(EXPECT_SUCCESS, 0, 1, iterations))
        return 1;

    /* 71) Pooled instance function: center() */
    snprintf(str, 256, "y=x.instances().center()==(x.instances().max()+x.instances().min())*0.5;");
    setup_test(MPR_INT32, 1, MPR_INT32, 1);
    if (parse_and_eval(EXPECT_SUCCESS, 0, 0, iterations))
        return 1;

    /* 72) Pooled instance mean length of centered vectors */
    snprintf(str, 256, "m=x.instances().mean(); y=(x-m).norm().instances().mean()");
    setup_test(MPR_FLT, 2, MPR_FLT, 1);
    if (parse_and_eval(EXPECT_SUCCESS, 0, 0, iterations))
        return 1;

    /* 73) Pooled instance mean linear displacement */
    snprintf(str, 256, "y=(x-x{-1}).instances().mean()");
    setup_test(MPR_INT32, 1, MPR_INT32, 1);
    if (parse_and_eval(EXPECT_SUCCESS, 0, 0, iterations))
        return 1;

    /* 74) Dot product of two vectors */
    snprintf(str, 256, "y=dot(x, x1);");
    lens[0] = 3;
    setup_test_multisource(2, types, lens, MPR_FLT, 1);
    expect_flt[0] = src_int[0] * src_flt[0] + src_int[1] * src_flt[1] + src_int[2] * src_flt[2];
    if (parse_and_eval(EXPECT_SUCCESS, 0, 1, iterations))
        return 1;

    /* 75) 2D Vector angle */
    snprintf(str, 256, "y=angle([-1,-1], [1,0]);");
    setup_test(MPR_FLT, 2, MPR_FLT, 1);
    expect_flt[0] = M_PI * -0.75f;
    if (parse_and_eval(EXPECT_SUCCESS, 0, 1, iterations))
        return 1;

    /* 76) Pooled instance mean angular displacement */
    snprintf(str, 256, "M=x.instances().mean(); y=angle(x{-1}-M,x-M).instances().mean();");
    setup_test(MPR_FLT, 2, MPR_FLT, 1);
    expect_flt[0] = 0.f;
    if (parse_and_eval(EXPECT_SUCCESS, 0, 1, iterations))
        return 1;

    /* 77) Integer divide-by-zero */
    snprintf(str, 256, "foo=1; y=x/foo; foo=!foo;");
    setup_test(MPR_INT32, 1, MPR_INT32, 1);
    /* we expect only half of the evaluation attempts to succeed (i.e. when 'foo' == 1) */
    if (parse_and_eval(EXPECT_SUCCESS, 0, 0, (iterations + 1) / 2))
        return 1;

    /* 78) Optimization: Vector squashing (8 tokens instead of 12) */
    snprintf(str, 256, "y=x*[3,3,3]+[1,1,2.6];");
    setup_test(MPR_FLT, 3, MPR_FLT, 3);
    expect_flt[0] = src_flt[0] * 3.0f + 1.0f;
    expect_flt[1] = src_flt[1] * 3.0f + 1.0f;
    expect_flt[2] = src_flt[2] * 3.0f + 2.6f;
    if (parse_and_eval(EXPECT_SUCCESS, 8, 1, iterations))
        return 1;

    /* 79) Wrapping vectors */
    snprintf(str, 256, "y=x*[3,3,3]+[1.23,4.56];");
    setup_test(MPR_FLT, 3, MPR_FLT, 3);
    expect_flt[0] = src_flt[0] * 3.0f + 1.23f;
    expect_flt[1] = src_flt[1] * 3.0f + 4.56f;
    expect_flt[2] = src_flt[2] * 3.0f + 1.23f;
    if (parse_and_eval(EXPECT_SUCCESS, 8, 1, iterations))
        return 1;

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
                                "--num_iterations <int> (default %d)\n",
                                iterations);
                        return 1;
                        break;
                    case 'q':
                        verbose = 0;
                        break;
                    case '-':
                        if (++j < len && strcmp(argv[i]+j, "num_iterations")==0)
                            if (++i < argc)
                                iterations = atoi(argv[i]);
                        break;
                    default:
                        break;
                }
            }
        }
    }

    for (i = 0; i < SRC_ARRAY_LEN; i++)
        inh[i].inst = 0;
    outh.inst = 0;

    eprintf("**********************************\n");
    seed_srand();
    eprintf("Generating random inputs:\n");
    eprintf("  int: [");
    for (i = 0; i < SRC_ARRAY_LEN; i++) {
        src_int[i] = random_int();
        eprintf("%i, ", src_int[i]);
    }
    eprintf("\b\b]\n");

    eprintf("  flt: [");
    for (i = 0; i < SRC_ARRAY_LEN; i++) {
        src_flt[i] = random_flt();
        eprintf("%g, ", src_flt[i]);
    }
    eprintf("\b\b]\n");

    eprintf("  dbl: [");
    for (i = 0; i < SRC_ARRAY_LEN; i++) {
        src_dbl[i] = random_dbl();
        eprintf("%g, ", src_dbl[i]);
    }
    eprintf("\b\b]\n");

    for (i = 0; i < SRC_ARRAY_LEN; i++)
        inh_p[i] = &inh[i];

    result = run_tests();

    for (i = 0; i < SRC_ARRAY_LEN; i++)
        mpr_value_free(&inh[i]);
    mpr_value_free(&outh);

    eprintf("**********************************\n");
    printf("\r..................................................Test %s\x1B[0m.",
           result ? "\x1B[31mFAILED" : "\x1B[32mPASSED");
    if (!result)
        printf(" (%f seconds, %d tokens).\n", total_elapsed_time, token_count);
    else
        printf("\n");
    return result;
}
