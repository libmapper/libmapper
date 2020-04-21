#include <../src/mpr_internal.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <sys/time.h>
#include <string.h>
#include <unistd.h>

#define SRC_ARRAY_LEN 3
#define DST_ARRAY_LEN 6
#define MAX_VARS 3

#define eprintf(format, ...) do {               \
    if (verbose)                                \
        fprintf(stdout, format, ##__VA_ARGS__); \
} while(0)

int verbose = 1;
char str[256];
mpr_expr e;
int iterations = 20000;
int expression_count = 1;
int token_count = 0;

int src_int[SRC_ARRAY_LEN], dst_int[DST_ARRAY_LEN], tmp_int[DST_ARRAY_LEN];
float src_flt[SRC_ARRAY_LEN], dst_flt[DST_ARRAY_LEN], tmp_flt[DST_ARRAY_LEN];
double src_dbl[SRC_ARRAY_LEN], dst_dbl[DST_ARRAY_LEN], tmp_dbl[DST_ARRAY_LEN];
double then, now;
double total_elapsed_time = 0;
mpr_type types[SRC_ARRAY_LEN];

mpr_time time_in = {0, 0}, time_out = {0, 0};

// signal_history structures
mpr_hist_t inh[SRC_ARRAY_LEN], outh, user_vars[MAX_VARS], *user_vars_p;
mpr_hist inh_p[SRC_ARRAY_LEN];
mpr_type src_types[SRC_ARRAY_LEN];
int src_lens[SRC_ARRAY_LEN], n_sources;

/*! Internal function to get the current time. */
static double current_time()
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (double) tv.tv_sec + tv.tv_usec / 1000000.0;
}

typedef struct _var {
    char *name;
    int vec_len;
    mpr_type datatype;
    mpr_type casttype;
    char vec_len_locked;
    char assigned;
    char public;
} mpr_var_t, *mpr_var;

struct _mpr_expr
{
    void *tokens;
    void *start;
    mpr_var vars;
    int offset;
    int len;
    int vec_size;
    int in_mem;
    int out_mem;
    int n_vars;
};

/*! A helper function to seed the random number generator. */
static void seed_srand()
{
    unsigned int s;

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

    double d = mpr_get_current_time();
    s = (unsigned int)((d-(unsigned long)d)*100000);
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
    uint32_t buffer;
    float *f = (float*)&buffer;
    do {
        for (int i = 0; i < 4; i++) {
            int random = rand();
            buffer = (buffer << 8) | (random & 0xFF);
        }
    } while (*f != *f); // exclude NaN
    return *f;
}

/* value returned by rand() is guaranteed to be at least 15 bits so we will
 * construct a 64-bit field using concatenation */
double random_dbl()
{
    uint64_t buffer;
    double *d = (double*)&buffer;
    do {
        for (int i = 0; i < 8; i++) {
            int random = rand();
            buffer = (buffer << 8) | (random & 0xFF);
        }
    } while (*d != *d);
    return *d;
}

int check_result(mpr_type *types, int len, const void *val, int pos, int check)
{
    if (!val || !types || len < 1)
        return 1;

    eprintf("Got: ");
    if (len > 1)
        eprintf("[");

    int i, offset = pos * len, error = -1;
    for (i = 0; i < len; i++) {
        switch (types[i]) {
            case MPR_NULL:
                eprintf("NULL, ");
                break;
            case MPR_INT32:
            {
                int *pi = (int*)val;
                eprintf("%d, ", pi[i + offset]);
                if (check && pi[i + offset] != tmp_int[i])
                    error = i;
                break;
            }
            case MPR_FLT:
            {
                float *pf = (float*)val;
                eprintf("%g, ", pf[i + offset]);
                if (check && pf[i + offset] != tmp_flt[i])
                    error = i;
                break;
            }
            case MPR_DBL:
            {
                double *pd = (double*)val;
                eprintf("%g, ", pd[i + offset]);
                if (check && pd[i + offset] != tmp_dbl[i])
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
                eprintf("(expected %d)\n", tmp_int[error]);
                break;
            case MPR_FLT:
                eprintf("(expected %g)\n", tmp_flt[error]);
                break;
            case MPR_DBL:
                eprintf("(expected %g)\n", tmp_dbl[error]);
                break;
        }
        return 1;
    }
    eprintf("... OK\n");
    return 0;
}

void setup_test_multisource(int _n_sources, mpr_type *in_types, int *in_lens,
                            mpr_type out_type, int out_len)
{
    n_sources = _n_sources;
    int i;
    for (i = 0; i < _n_sources; i++) {
        src_types[i] = in_types[i];
        src_lens[i] = in_lens[i];
        inh[i].type = in_types[i];
        inh[i].mem = 3;
        inh[i].len = in_lens[i];
        switch (in_types[i]) {
            case MPR_INT32:
                inh[i].val = src_int;
                break;
            case MPR_FLT:
                inh[i].val = src_flt;
                break;
            default:
                inh[i].val = src_dbl;
                break;
        }
        inh[i].pos = 0;
        inh[i].time = &time_in;
    }

    outh.type = out_type;
    outh.mem = 3;
    outh.len = out_len;
    switch (out_type) {
        case MPR_INT32:
            outh.val = dst_int;
            break;
        case MPR_FLT:
            outh.val = dst_flt;
            break;
        default:
            outh.val = dst_dbl;
            break;
    }
    outh.pos = -1;
    outh.time = &time_out;
}

void setup_test(mpr_type in_type, int in_len, mpr_type out_type, int out_len)
{
    setup_test_multisource(1, &in_type, &in_len, out_type, out_len);
}

#define EXPECT_SUCCESS 0
#define EXPECT_FAILURE 1

int parse_and_eval(int expectation, int max_tokens, int check)
{
    // clear output arrays
    int i, result = 0;

    if (verbose) {
        printf("***************** Expression %d *****************\n",
               expression_count++);
        printf("Parsing string '%s'\n", str);
    }
    else {
        printf("\rExpression %d", expression_count++);
        fflush(stdout);
    }
    e = mpr_expr_new_from_str(str, n_sources, src_types, src_lens,
                              outh.type, outh.len);
    if (!e) {
        eprintf("Parser FAILED");
        goto fail;
    }
    else if (EXPECT_FAILURE == expectation) {
        eprintf("Error: expected FAILURE\n");
        result = 1;
        goto free;
    }
    for (i = 0; i < n_sources; i++) {
        inh[i].mem = mpr_expr_get_in_hist_size(e, i);
        mpr_hist_realloc(&inh[i], inh[i].mem,
                         inh[i].len * mpr_type_get_size(inh[i].type), 0);
    }
    outh.mem = mpr_expr_get_out_hist_size(e);
    mpr_hist_realloc(&outh, outh.mem, outh.len * mpr_type_get_size(outh.type), 1);

    /* mpr_hist_realloc will not initialize memory if history size is unchanged
     * so we will explicitly initialise it here. */
    memset(outh.val, 0, outh.mem * outh.len * mpr_type_get_size(outh.type));

    if (mpr_expr_get_num_vars(e) > MAX_VARS) {
        eprintf("Maximum variables exceeded.\n");
        goto fail;
    }

    // reallocate variable value histories
    for (i = 0; i < e->n_vars; i++) {
        // eprintf("user_var[%d]: %p\n", i, &user_vars[i]);
        int sample_size = sizeof(double) * mpr_expr_get_var_vec_len(e, i);
        mpr_hist_realloc(&user_vars[i], 1, sample_size, 0);

        /* mpr_hist_realloc will not initialize memory if history size is
         * unchanged so we will explicitly initialise it here. */
        memset(user_vars[i].val, 0, sample_size);
    }
    user_vars_p = user_vars;

    eprintf("Parser returned %d tokens...", e->len);
    if (max_tokens && e->len > max_tokens) {
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

    token_count += e->len;

    eprintf("Try evaluation once... ");
    mpr_time_set(&time_in, MPR_NOW);
    if (!mpr_expr_eval(e, inh_p, &user_vars_p, &outh, &time_in, types)) {
        eprintf("FAILED.\n");
        result = 1;
        goto free;
    }
    eprintf("OK\n");

    then = current_time();
    eprintf("Calculate expression %i more times... ", iterations-1);
    fflush(stdout);
    i = iterations-1;
    while (i--) {
        mpr_time_set(&time_in, MPR_NOW);
        mpr_expr_eval(e, inh_p, &user_vars_p, &outh, &time_in, types);
        // sleep here stops compiler from optimizing loop away
        usleep(1);
    }
    now = current_time();
    eprintf("%g seconds.\n", now-then);
    total_elapsed_time += now-then;

    if (check_result(types, outh.len, outh.val, outh.pos, check))
        result = 1;

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
    /* 1) Complex string */
    snprintf(str, 256, "y=26*2/2+log10(pi)+2.*pow(2,1*(3+7*.1)*1.1+x{0}[0])*3*4+cos(2.)");
    setup_test(MPR_FLT, 1, MPR_FLT, 1);
    tmp_flt[0] = 26*2/2+log10(M_PI)+2.*pow(2,1*(3+7*.1)*1.1+src_flt[0])*3*4+cos(2.0);
    if (parse_and_eval(EXPECT_SUCCESS, 0, 1))
        return 1;

    /* 2) Building vectors, conditionals */
    snprintf(str, 256, "y=(x>1)?[1,2,3]:[2,4,6]");
    setup_test(MPR_FLT, 3, MPR_INT32, 3);
    tmp_int[0] = src_flt[0] > 1 ? 1 : 2;
    tmp_int[1] = src_flt[1] > 1 ? 2 : 4;
    tmp_int[2] = src_flt[2] > 1 ? 3 : 6;
    if (parse_and_eval(EXPECT_SUCCESS, 0, 1))
        return 1;

    /* 3) Conditionals with shortened syntax */
    snprintf(str, 256, "y=x?:123");
    setup_test(MPR_FLT, 1, MPR_INT32, 1);
    tmp_int[0] = (int)(src_flt[0] ? src_flt[0] : 123);
    if (parse_and_eval(EXPECT_SUCCESS, 0, 1))
        return 1;

    /* 4) Conditional that should be optimized */
    snprintf(str, 256, "y=1?2:123");
    setup_test(MPR_FLT, 1, MPR_INT32, 1);
    tmp_int[0] = 2;
    if (parse_and_eval(EXPECT_SUCCESS, 2, 1))
        return 1;

    /* 5) Building vectors with variables, operations inside vector-builder */
    snprintf(str, 256, "y=[x*-2+1,0]");
    setup_test(MPR_INT32, 2, MPR_DBL, 3);
    tmp_dbl[0] = (double)src_int[0] * -2 + 1;
    tmp_dbl[1] = (double)src_int[1] * -2 + 1;
    tmp_dbl[2] = 0.0;
    if (parse_and_eval(EXPECT_SUCCESS, 0, 1))
        return 1;

    /* 6) Building vectors with variables, operations inside vector-builder */
    snprintf(str, 256, "y=[-99.4, -x*1.1+x]");
    setup_test(MPR_INT32, 2, MPR_DBL, 3);
    tmp_dbl[0] = atof("-99.4f");
    tmp_dbl[1] = (double)(-src_int[0] * 1.1 + src_int[0]);
    tmp_dbl[2] = (double)(-src_int[1] * 1.1 + src_int[1]);
    if (parse_and_eval(EXPECT_SUCCESS, 0, 1))
        return 1;

    /* 7) Indexing vectors by range */
    snprintf(str, 256, "y=x[1:2]+100");
    setup_test(MPR_DBL, 3, MPR_FLT, 2);
    tmp_flt[0] = (float)src_dbl[1] + 100;
    tmp_flt[1] = (float)src_dbl[2] + 100;
    if (parse_and_eval(EXPECT_SUCCESS, 0, 1))
        return 1;

    /* 8) Typical linear scaling expression with vectors */
    snprintf(str, 256, "y=x*[0.1,3.7,-.1112]+[2,1.3,9000]");
    setup_test(MPR_FLT, 3, MPR_FLT, 3);
    tmp_flt[0] = src_flt[0] * 0.1 + 2.;
    tmp_flt[1] = src_flt[1] * 3.7 + 1.3;
    tmp_flt[2] = src_flt[2] * -.1112 + 9000.;
    if (parse_and_eval(EXPECT_SUCCESS, 0, 1))
        return 1;

    /* 9) Check type and vector length promotion of operation sequences */
    snprintf(str, 256, "y=1+2*3-4*x");
    setup_test(MPR_FLT, 2, MPR_FLT, 2);
    tmp_flt[0] = 1. + 2. * 3. - 4. * src_flt[0];
    tmp_flt[1] = 1. + 2. * 3. - 4. * src_flt[1];
    if (parse_and_eval(EXPECT_SUCCESS, 0, 1))
        return 1;

    /* 10) Swizzling, more pre-computation */
    snprintf(str, 256, "y=[x[2],x[0]]*0+1+12");
    setup_test(MPR_FLT, 3, MPR_FLT, 2);
    tmp_flt[0] = src_flt[2] * 0. + 1. + 12.;
    tmp_flt[1] = src_flt[0] * 0. + 1. + 12.;
    if (parse_and_eval(EXPECT_SUCCESS, 0, 1))
        return 1;

    /* 11) Logical negation */
    snprintf(str, 256, "y=!(x[1]*0)");
    setup_test(MPR_DBL, 3, MPR_INT32, 1);
    tmp_int[0] = (int)!(src_dbl[1] && 0);
    if (parse_and_eval(EXPECT_SUCCESS, 0, 1))
        return 1;

    /* 12) any() */
    snprintf(str, 256, "y=any(x-1)");
    setup_test(MPR_DBL, 3, MPR_INT32, 1);
    tmp_int[0] =    ((int)src_dbl[0] - 1) ? 1 : 0
                  | ((int)src_dbl[1] - 1) ? 1 : 0
                  | ((int)src_dbl[2] - 1) ? 1 : 0;
    if (parse_and_eval(EXPECT_SUCCESS, 0, 1))
        return 1;

    /* 13) all() */
    snprintf(str, 256, "y=x[2]*all(x-1)");
    setup_test(MPR_DBL, 3, MPR_INT32, 1);
    tmp_int[0] = (int)src_dbl[2] * (  ((int)src_dbl[0] - 1) ? 1 : 0
                                    & ((int)src_dbl[1] - 1) ? 1 : 0
                                    & ((int)src_dbl[2] - 1) ? 1 : 0);
    if (parse_and_eval(EXPECT_SUCCESS, 0, 1))
        return 1;

    /* 14) pi and e, extra spaces */
    snprintf(str, 256, "y=x + pi -     e");
    setup_test(MPR_DBL, 1, MPR_FLT, 1);
    tmp_flt[0] = (float)(src_dbl[0] + M_PI - M_E);
    if (parse_and_eval(EXPECT_SUCCESS, 0, 1))
        return 1;

    /* 15) Bad vector notation */
    snprintf(str, 256, "y=(x-2)[1]");
    setup_test(MPR_INT32, 1, MPR_INT32, 1);
    if (parse_and_eval(EXPECT_FAILURE, 0, 1))
        return 1;

    /* 16) Vector index outside bounds */
    snprintf(str, 256, "y=x[3]");
    setup_test(MPR_INT32, 3, MPR_INT32, 1);
    if (parse_and_eval(EXPECT_FAILURE, 0, 1))
        return 1;

    /* 17) Vector length mismatch */
    snprintf(str, 256, "y=x[1:2]");
    setup_test(MPR_INT32, 3, MPR_INT32, 1);
    if (parse_and_eval(EXPECT_FAILURE, 0, 1))
        return 1;

    /* 18) Unnecessary vector notation */
    snprintf(str, 256, "y=x+[1]");
    setup_test(MPR_INT32, 1, MPR_INT32, 1);
    tmp_int[0] = src_int[0] + 1;
    if (parse_and_eval(EXPECT_SUCCESS, 4, 1))
        return 1;

    /* 19) Invalid history index */
    snprintf(str, 256, "y=x{-101}");
    setup_test(MPR_INT32, 1, MPR_INT32, 1);
    if (parse_and_eval(EXPECT_FAILURE, 0, 1))
        return 1;

    /* 20) Invalid history index */
    snprintf(str, 256, "y=x-y{-101}");
    setup_test(MPR_INT32, 1, MPR_INT32, 1);
    if (parse_and_eval(EXPECT_FAILURE, 0, 1))
        return 1;

    /* 21) Scientific notation */
    snprintf(str, 256, "y=x[1]*1.23e-20");
    setup_test(MPR_INT32, 2, MPR_DBL, 1);
    tmp_dbl[0] = (double)src_int[1] * 1.23e-20;
    if (parse_and_eval(EXPECT_SUCCESS, 0, 1))
        return 1;

    /* 22) Vector assignment */
    snprintf(str, 256, "y[1]=x[1]");
    setup_test(MPR_DBL, 3, MPR_INT32, 3);
    tmp_int[1] = (int)src_dbl[1];
    if (parse_and_eval(EXPECT_SUCCESS, 0, 1))
        return 1;

    /* 23) Vector assignment */
    snprintf(str, 256, "y[1:2]=[x[1],10]");
    setup_test(MPR_DBL, 3, MPR_INT32, 3);
    tmp_int[1] = (int)src_dbl[1];
    tmp_int[2] = 10;
    if (parse_and_eval(EXPECT_SUCCESS, 0, 1))
        return 1;

    /* 24) Output vector swizzling */
    snprintf(str, 256, "[y[0],y[2]]=x[1:2]");
    setup_test(MPR_FLT, 3, MPR_DBL, 3);
    tmp_dbl[0] = (double)src_flt[1];
    tmp_dbl[2] = (double)src_flt[2];
    if (parse_and_eval(EXPECT_SUCCESS, 0, 1))
        return 1;

    /* 25) Multiple expressions */
    snprintf(str, 256, "y[0]=x*100-23.5; y[2]=100-x*6.7");
    setup_test(MPR_INT32, 1, MPR_FLT, 3);
    tmp_flt[0] = (float)((double)src_int[0] * 100 - 23.5);
    tmp_flt[2] = (float)(100 - (double)src_int[0] * 6.7);
    if (parse_and_eval(EXPECT_SUCCESS, 0, 1))
        return 1;

    /* 26) Error check: separating sub-expressions with commas */
    snprintf(str, 256, "foo=1,  y=y{-1}+foo");
    setup_test(MPR_INT32, 1, MPR_FLT, 1);
    if (parse_and_eval(EXPECT_FAILURE, 0, 1))
        return 1;

    /* 27) Initialize filters */
    snprintf(str, 256, "y=x+y{-1}; y{-1}=100");
    setup_test(MPR_INT32, 1, MPR_INT32, 1);
    tmp_int[0] = src_int[0] * iterations + 100;
    if (parse_and_eval(EXPECT_SUCCESS, 0, 1))
        return 1;

    /* 28) Initialize filters + vector index */
    snprintf(str, 256, "y=x+y{-1}; y[1]{-1}=100");
    setup_test(MPR_INT32, 2, MPR_INT32, 2);
    tmp_int[0] = src_int[0] * iterations;
    tmp_int[1] = src_int[1] * iterations + 100;
    if (parse_and_eval(EXPECT_SUCCESS, 0, 1))
        return 1;

    /* 29) Initialize filters + vector index */
    snprintf(str, 256, "y=x+y{-1}; y{-1}=[100,101]");
    setup_test(MPR_INT32, 2, MPR_INT32, 2);
    tmp_int[0] = src_int[0] * iterations + 100;
    tmp_int[1] = src_int[1] * iterations + 101;
    if (parse_and_eval(EXPECT_SUCCESS, 0, 1))
        return 1;

    /* 30) Initialize filters */
    snprintf(str, 256, "y=x+y{-1}; y[0]{-1}=100; y[2]{-1}=200");
    setup_test(MPR_INT32, 3, MPR_INT32, 3);
    tmp_int[0] = src_int[0] * iterations + 100;
    tmp_int[1] = src_int[1] * iterations;
    tmp_int[2] = src_int[2] * iterations + 200;
    if (parse_and_eval(EXPECT_SUCCESS, 0, 1))
        return 1;

    /* 31) Initialize filters */
    snprintf(str, 256, "y=x+y{-1}-y{-2}; y{-1}=[100,101]; y{-2}=[102,103]");
    setup_test(MPR_INT32, 2, MPR_INT32, 2);
    switch (iterations % 6) {
        case 0:
            tmp_int[0] = 100;
            tmp_int[1] = 101;
            break;
        case 1:
            tmp_int[0] = src_int[0] + 100 - 102;
            tmp_int[1] = src_int[1] + 101 - 103;
            break;
        case 2:
            tmp_int[0] = src_int[0] * 2 - 102;
            tmp_int[1] = src_int[1] * 2 - 103;
            break;
        case 3:
            tmp_int[0] = src_int[0] * 2 - 100;
            tmp_int[1] = src_int[1] * 2 - 101;
            break;
        case 4:
            tmp_int[0] = src_int[0] - 100 + 102;
            tmp_int[1] = src_int[1] - 101 + 103;
            break;
        case 5:
            tmp_int[0] = 102;
            tmp_int[1] = 103;
            break;
    }
    if (parse_and_eval(EXPECT_SUCCESS, 0, 1))
        return 1;

    /* 32) Only initialize */
    snprintf(str, 256, "y{-1}=100");
    setup_test(MPR_INT32, 3, MPR_INT32, 1);
    if (parse_and_eval(EXPECT_FAILURE, 0, 1))
        return 1;

    /* 33) Bad syntax */
    snprintf(str, 256, " ");
    setup_test(MPR_INT32, 1, MPR_FLT, 3);
    if (parse_and_eval(EXPECT_FAILURE, 0, 1))
        return 1;

    /* 34) Bad syntax */
    snprintf(str, 256, " ");
    setup_test(MPR_INT32, 1, MPR_FLT, 3);
    if (parse_and_eval(EXPECT_FAILURE, 0, 1))
        return 1;

    /* 35) Bad syntax */
    snprintf(str, 256, "y");
    setup_test(MPR_INT32, 1, MPR_FLT, 3);
    if (parse_and_eval(EXPECT_FAILURE, 0, 1))
        return 1;

    /* 36) Bad syntax */
    snprintf(str, 256, "y=");
    setup_test(MPR_INT32, 1, MPR_FLT, 3);
    if (parse_and_eval(EXPECT_FAILURE, 0, 1))
        return 1;

    /* 37) Bad syntax */
    snprintf(str, 256, "=x");
    setup_test(MPR_INT32, 1, MPR_FLT, 3);
    if (parse_and_eval(EXPECT_FAILURE, 0, 1))
        return 1;

    /* 38) sin */
    snprintf(str, 256, "sin(x)");
    setup_test(MPR_INT32, 1, MPR_FLT, 3);
    if (parse_and_eval(EXPECT_FAILURE, 0, 1))
        return 1;

    /* 39) Variable declaration */
    snprintf(str, 256, "y=x+var; var=[3.5,0]");
    setup_test(MPR_INT32, 2, MPR_FLT, 2);
    tmp_flt[0] = (float)((double)src_int[0] + 3.5);
    tmp_flt[1] = (float)((double)src_int[1]);
    if (parse_and_eval(EXPECT_SUCCESS, 0, 1))
        return 1;

    /* 40) Variable declaration */
    snprintf(str, 256, "ema=ema{-1}*0.9+x*0.1; y=ema*2; ema{-1}=90");
    setup_test(MPR_INT32, 1, MPR_FLT, 1);
    tmp_flt[0] = ((90 - src_int[0]) * powf(M_E, iterations * -0.1053605) + src_int[0]) * 2;
    if (parse_and_eval(EXPECT_SUCCESS, 0, 1))
        return 1;

    /* 41) Multiple variable declaration */
    snprintf(str, 256, "a=1.1; b=2.2; c=3.3; y=x+a-b*c");
    setup_test(MPR_INT32, 1, MPR_FLT, 1);
    tmp_flt[0] = (float)((double)src_int[0] + 1.1 - 2.2 * 3.3);
    if (parse_and_eval(EXPECT_SUCCESS, 0, 1))
        return 1;

    /* 42) Malformed variable declaration */
    snprintf(str, 256, "y=x + myvariable * 10");
    setup_test(MPR_INT32, 1, MPR_FLT, 1);
    if (parse_and_eval(EXPECT_FAILURE, 0, 1))
        return 1;

    /* 43) Vector functions mean() and sum() */
    snprintf(str, 256, "y=mean(x)==(sum(x)/3)");
    setup_test(MPR_FLT, 3, MPR_INT32, 1);
    tmp_int[0] = 1;
    if (parse_and_eval(EXPECT_SUCCESS, 0, 1))
        return 1;

    /* 44) Overloaded vector functions max() and min() */
    snprintf(str, 256, "y=max(x)-min(x)*max(x[0],1)");
    setup_test(MPR_FLT, 3, MPR_INT32, 1);
    tmp_int[0] = (  ((src_flt[0] > src_flt[1]) ?
                     (src_flt[0] > src_flt[2] ? src_flt[0] : src_flt[2]) :
                     (src_flt[1] > src_flt[2] ? src_flt[1] : src_flt[2]))
                  - ((src_flt[0] < src_flt[1]) ?
                     (src_flt[0] < src_flt[2] ? src_flt[0] : src_flt[2]) :
                     (src_flt[1] < src_flt[2] ? src_flt[1] : src_flt[2]))
                  * (src_flt[0] > 1 ? src_flt[0] : 1));
    if (parse_and_eval(EXPECT_SUCCESS, 0, 1))
        return 1;

    /* 45) Optimization: operations by zero */
    snprintf(str, 256, "y=0*sin(x)*200+1.1");
    setup_test(MPR_INT32, 1, MPR_FLT, 1);
    tmp_flt[0] = 1.1;
    if (parse_and_eval(EXPECT_SUCCESS, 2, 1))
        return 1;

    /* 46) Optimization: operations by one */
    snprintf(str, 256, "y=x*1");
    setup_test(MPR_INT32, 1, MPR_FLT, 1);
    tmp_flt[0] = (float)src_int[0];
    if (parse_and_eval(EXPECT_SUCCESS, 2, 1))
        return 1;

    /* 47) Error check: division by zero */
    snprintf(str, 256, "y=x/0");
    setup_test(MPR_INT32, 1, MPR_FLT, 1);
    if (parse_and_eval(EXPECT_FAILURE, 0, 1))
        return 1;

    /* 48) Multiple Inputs */
    snprintf(str, 256, "y=x+x1[1:2]+x2");
    mpr_type types[] = {MPR_INT32, MPR_FLT, MPR_DBL};
    int lens[] = {2, 3, 2};
    setup_test_multisource(3, types, lens, MPR_FLT, 2);
    tmp_flt[0] = (float)((double)src_int[0] + (double)src_flt[1] + src_dbl[0]);
    tmp_flt[1] = (float)((double)src_int[1] + (double)src_flt[2] + src_dbl[1]);
    if (parse_and_eval(EXPECT_SUCCESS, 0, 1))
        return 1;

    /* 49) Functions with memory: ema() */
    snprintf(str, 256, "y=x-ema(x,0.1)+2");
    setup_test(MPR_INT32, 1, MPR_FLT, 1);
    // result should approach 2 as iterations increases
    tmp_flt[0] = src_int[0] * powf(M_E, iterations * -0.1053605) + 2;
    parse_and_eval(EXPECT_SUCCESS, 0, 0);
    dst_flt[0] = ((float*)outh.val)[outh.pos * outh.len];
    if (fabsf(dst_flt[0] - tmp_flt[0]) > 0.001) {
        eprintf("... error: expected approximately %g\n", tmp_flt[0]);
        return 1;
    }
    else
        eprintf("... OK\n");

    /* 50) Functions with memory: schmitt() */
    snprintf(str, 256, "y=y{-1}+(schmitt(y{-1},20,80)?-1:1)");
    setup_test(MPR_INT32, 3, MPR_FLT, 3);
    if (iterations < 80) {
        tmp_flt[0] = tmp_flt[1] = tmp_flt[2] = iterations;
    }
    else {
        int cycles = (iterations-80) / 60;
        int remainder = (iterations-80) % 60;
        float ans = (cycles % 2) ? 20 + remainder : 80 - remainder;
        tmp_flt[0] = tmp_flt[1] = tmp_flt[2] = ans;
    }
    if (parse_and_eval(EXPECT_SUCCESS, 0, 1))
        return 1;

    /* 51) Multiple output assignment */
    snprintf(str, 256, "y=x-10000; y=max(min(y,1),0)");
    setup_test(MPR_FLT, 1, MPR_FLT, 1);
    tmp_flt[0] = src_flt[0] - 10000;
    tmp_flt[0] = tmp_flt[0] < 0 ? 0 : tmp_flt[0];
    tmp_flt[0] = tmp_flt[0] > 0 ? 1 : tmp_flt[0];
    if (parse_and_eval(EXPECT_SUCCESS, 0, 1))
        return 1;

    /* 52) Access timetags */
    snprintf(str, 256, "y=t_x");
    setup_test(MPR_INT32, 1, MPR_DBL, 1);
    parse_and_eval(EXPECT_SUCCESS, 0, 0);
    dst_dbl[0] = ((double*)outh.val)[outh.pos * outh.len];
    if (dst_dbl[0] != mpr_time_as_dbl(time_in)) {
        eprintf("... error: expected %g\n", mpr_time_as_dbl(time_in));
        return 1;
    }
    else
        eprintf("... OK\n");

    /* 53) Access timetags from past samples */
    snprintf(str, 256, "y=t_x-t_y{-1}");
    setup_test(MPR_INT32, 1, MPR_DBL, 1);
    parse_and_eval(EXPECT_SUCCESS, 0, 0);
    // results may vary depending on machine but we can perform a sanity check
    dst_dbl[0] = ((double*)outh.val)[outh.pos * outh.len];
    if (dst_dbl[0] < 0.0 || dst_dbl[0] > 0.0001) {
        eprintf("... error: expected value between %g and %g\n", 0.0, 0.0001);
        printf("%g < %g... %d\n", dst_dbl[0], 0.0, dst_dbl[0] < 0.0);
        printf("%g > %g... %d\n", dst_dbl[0], 0.0001, dst_dbl[0] > 0.0010);
        return 1;
    }
    else
        eprintf("... OK\n");

    /* 54) Moving average of inter-sample period */
    /* Tricky - we need to init y{-1}.tt to x.tt or the first calculated
     * difference will be enormous! */
    snprintf(str, 256,
             "t_y{-1}=t_x;"
             "period=t_x-t_y{-1};"
             "y=y{-1}*0.9+period*0.1;");
    setup_test(MPR_INT32, 1, MPR_DBL, 1);
    parse_and_eval(EXPECT_SUCCESS, 0, 0);
    // results may vary depending on machine but we can perform a sanity check
    dst_dbl[0] = ((double*)outh.val)[outh.pos * outh.len];
    if (dst_dbl[0] < 0. || dst_dbl[0] > 0.0001) {
        eprintf("... error: expected value between %g and %g\n", 0.0, 0.0001);
        return 1;
    }
    else
        eprintf("... OK\n");

    /* 55) Moving average of inter-sample jitter */
    /* Tricky - we need to init y{-1}.tt to x.tt or the first calculated
     * difference will be enormous! */
    snprintf(str, 256,
             "t_y{-1}=t_x;"
             "interval=t_x-t_y{-1};"
             "sr=sr{-1}*0.9+interval*0.1;"
             "y=y{-1}*0.9+abs(interval-sr)*0.1;");
    setup_test(MPR_INT32, 1, MPR_DBL, 1);
    parse_and_eval(EXPECT_SUCCESS, 0, 0);
    // results may vary depending on machine but we can perform a sanity check
    dst_dbl[0] = ((double*)outh.val)[outh.pos * outh.len];
    if (dst_dbl[0] < 0. || dst_dbl[0] > 0.0001) {
        eprintf("... error: expected value between %g and %g\n", 0.0, 0.0001);
        printf("%g < %g... %d\n", dst_dbl[0], 0.0, dst_dbl[0] < 0.0);
        printf("%g > %g... %d\n", dst_dbl[0], 0.0001, dst_dbl[0] > 0.0001);
        return 1;
    }
    else
        eprintf("... OK\n");

    /* 56) Expression for limiting output rate */
    snprintf(str, 256,
             "t_y{-1}=t_x;"
             "diff=t_x-t_y{-1};"
             "y=(diff>0.1)?x;");
    setup_test(MPR_INT32, 1, MPR_INT32, 1);
    tmp_int[0] = src_int[0];
    if (parse_and_eval(EXPECT_SUCCESS, 0, 1))
        return 1;

    /* 57) Expression for limiting rate with smoothed output */
    snprintf(str, 256,
             "t_y{-1}=t_x;"
             "output=(t_x-t_y{-1})>0.1;"
             "y=output?agg/samps;"
             "agg=!output*agg+x;"
             "samps=output?1:samps+1");
    setup_test(MPR_INT32, 1, MPR_INT32, 1);
    tmp_dbl[0] = (double)src_int[0];
    if (parse_and_eval(EXPECT_SUCCESS, 0, 1))
        return 1;

    /* 58) Manipulate timetag directly. This functionality may be used in the
     *     future to schedule delays, however it currently will not affect
     *     message timing. */
    snprintf(str, 256, "y=x[0]{0}; t_y=t_x+10");
    setup_test(MPR_INT32, 1, MPR_INT32, 1);
    tmp_int[0] = src_int[0];
    if (parse_and_eval(EXPECT_SUCCESS, 0, 1))
        return 1;
    mpr_time time = outh.time[(int)outh.pos];
    if (mpr_time_as_dbl(time) != mpr_time_as_dbl(time_in) + 10) {
        eprintf("Expected timestamp {%"PRIu32", %"PRIu32"} but got "
                "{%"PRIu32", %"PRIu32"}\n", time_in.sec+10, time_in.frac,
                time.sec, time.frac);
        return 1;
    }

    /* 59) Faulty timetag syntax */
    snprintf(str, 256, "y=t_x-y;");
    setup_test(MPR_INT32, 1, MPR_INT32, 1);
    if (parse_and_eval(EXPECT_FAILURE, 0, 1))
        return 1;

    return 0;
}

int main(int argc, char **argv)
{
    int i, j, result = 0;
    // process flags for -v verbose, -h help
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

    eprintf("**********************************\n");
    seed_srand();
    eprintf("Generating random inputs:\n");
    eprintf("  int: [");
    for (int i = 0; i < SRC_ARRAY_LEN; i++) {
        src_int[i] = random_int();
        eprintf("%i, ", src_int[i]);
    }
    eprintf("\b\b]\n");

    // mpr_time t;
    eprintf("  flt: [");
    for (int i = 0; i < SRC_ARRAY_LEN; i++) {
        src_flt[i] = random_flt();
        eprintf("%g, ", src_flt[i]);
    }
    eprintf("\b\b]\n");

    eprintf("  dbl: [");
    for (int i = 0; i < SRC_ARRAY_LEN; i++) {
        src_dbl[i] = random_dbl();
        eprintf("%g, ", src_dbl[i]);
    }
    eprintf("\b\b]\n");

    for (int i = 0; i < SRC_ARRAY_LEN; i++)
        inh_p[i] = &inh[i];

    result = run_tests();
    eprintf("**********************************\n");
    printf("\r..................................................Test %s\x1B[0m.",
           result ? "\x1B[31mFAILED" : "\x1B[32mPASSED");
    if (!result)
        printf(" (%f seconds, %d tokens).\n", total_elapsed_time, token_count);
    else
        printf(".\n");
    return result;
}
