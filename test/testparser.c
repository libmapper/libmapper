
#include <../src/mapper_internal.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <sys/time.h>
#include <string.h>

#define DEST_ARRAY_LEN 6
#define MAX_VARS 3

#define eprintf(format, ...) do {               \
    if (verbose)                                \
        fprintf(stdout, format, ##__VA_ARGS__); \
} while(0)

int verbose = 1;
char str[256];
mapper_expr e;
int result = 0;
int iterations = 1000000;
int token_count = 0;

int src_int[] = {1, 2, 3}, dest_int[DEST_ARRAY_LEN];
float src_float[] = {1.0f, 2.0f, 3.0f}, dest_float[DEST_ARRAY_LEN];
double src_double[] = {1.0, 2.0, 3.0}, dest_double[DEST_ARRAY_LEN];
double then, now;
double total_elapsed_time = 0;
char typestring[3];

mapper_timetag_t tt_in = {0, 0}, tt_out = {0, 0};

// signal_history structures
mapper_signal_history_t inh, outh, user_vars[MAX_VARS], *user_vars_p;

/*! Internal function to get the current time. */
static double get_current_time()
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (double) tv.tv_sec + tv.tv_usec / 1000000.0;
}

typedef struct _variable {
    char *name;
    int vector_index;
    int vector_length;
    char datatype;
    char casttype;
    char history_size;
    char vector_length_locked;
    char assigned;
} mapper_variable_t, *mapper_variable;

struct _mapper_expr
{
    void *tokens;
    void *start;
    int length;
    int vector_size;
    int input_history_size;
    int output_history_size;
    mapper_variable variables;
    int num_variables;
    int constant_output;
};

/* TODO:
 multiplication by 0
 addition/subtraction of 0
 division by 0
 */

void print_value(char *types, int length, const void *value, int position)
{
    if (!value || !types || length < 1)
        return;

    if (length > 1)
        printf("[");

    int i, offset = position * length;
    for (i = 0; i < length; i++) {
        switch (types[i]) {
            case 'N':
                printf("NULL, ");
                break;
            case 'f':
            {
                float *pf = (float*)value;
                printf("%g, ", pf[i + offset]);
                break;
            }
            case 'i':
            {
                int *pi = (int*)value;
                printf("%d, ", pi[i + offset]);
                break;
            }
            case 'd':
            {
                double *pd = (double*)value;
                printf("%g, ", pd[i + offset]);
                break;
            }
            default:
                printf("\nTYPE ERROR\n");
                return;
        }
    }

    if (length > 1)
        printf("\b\b]");
    else
        printf("\b\b");
}

void setup_test(char in_type, int in_size, int in_length, void *in_value,
                char out_type, int out_size, int out_length, void *out_value)
{
    inh.type = in_type;
    inh.size = in_size;
    inh.length = in_length;
    inh.value = in_value;
    inh.position = 0;
    inh.timetag = &tt_in;

    outh.type = out_type;
    outh.size = out_size;
    outh.length = out_length;
    outh.value = out_value;
    outh.position = -1;
    outh.timetag = &tt_out;
}

#define EXPECT_SUCCESS 0
#define EXPECT_FAILURE 1

int parse_and_eval(int expectation)
{
    // clear output arrays
    int i;
    for (i = 0; i < DEST_ARRAY_LEN; i++) {
        dest_int[i] = 0;
        dest_float[i] = 0.0f;
        dest_double[i] = 0.0;
    }

    eprintf("**********************************\n");
    eprintf("Parsing string '%s'\n", str);
    if (!(e = mapper_expr_new_from_string(str, inh.type, outh.type,
                                          inh.length, outh.length))) {
        eprintf("Parser FAILED.\n");
        goto fail;
    }
    inh.size = mapper_expr_input_history_size(e);
    outh.size = mapper_expr_output_history_size(e);

    if (mapper_expr_num_variables(e) > MAX_VARS) {
        eprintf("Maximum variables exceeded.\n");
        goto fail;
    }

    // reallocate variable value histories
    for (i = 0; i < e->num_variables; i++) {
        eprintf("user_var[%d]: %p\n", i, &user_vars[i]);
        mhist_realloc(&user_vars[i], e->variables[i].history_size,
                      sizeof(double), 0);
    }
    user_vars_p = user_vars;

#ifdef DEBUG
    if (verbose) {
        char str[128];
        snprintf(str, 128, "Parser returned %d tokens:", e->length);
        printexpr(str, e);
    }
#endif

    token_count += e->length;

    eprintf("Try evaluation once...\n");
    if (!mapper_expr_evaluate(e, &inh, &user_vars_p, &outh, typestring)) {
        eprintf("Evaluation FAILED.\n");
        goto fail;
    }

    then = get_current_time();
    eprintf("Calculate expression %i times... ", iterations);
    i = iterations-1;
    while (i--) {
        mapper_expr_evaluate(e, &inh, &user_vars_p, &outh, typestring);
    }
    now = get_current_time();
    eprintf("%g seconds.\n", now-then);
    total_elapsed_time += now-then;

    if (verbose) {
        printf("Got:      ");
        print_value(typestring, outh.length, outh.value, outh.position);
        printf(" \n");
    }
    else
        printf(".");

    mapper_expr_free(e);

    return expectation != EXPECT_SUCCESS;

fail:
    return expectation != EXPECT_FAILURE;
}

int run_tests()
{
    int result = 0;

    /* Complex string */
    snprintf(str, 256, "y=26*2/2+log10(pi)+2.*pow(2,1*(3+7*.1)*1.1+x{0}[0])*3*4+cos(2.)");
    setup_test('f', 1, 1, src_float, 'f', 1, 1, dest_float);
    result += parse_and_eval(EXPECT_SUCCESS);
    eprintf("Expected: %g\n", 26*2/2+log10f(M_PI)+2.f
            *powf(2,1*(3+7*.1f)*1.1f+src_float[0])*3*4+cosf(2.0f));

    /* Building vectors, conditionals */
    snprintf(str, 256, "y=(x>1)?[1,2,3]:[2,4,6]");
    setup_test('f', 1, 3, src_float, 'i', 1, 3, dest_int);
    result += parse_and_eval(EXPECT_SUCCESS);
    eprintf("Expected: [%i, %i, %i]\n", src_float[0]>1?1:2, src_float[1]>1?2:4,
           src_float[2]>1?3:6);

    /* Conditionals with shortened syntax */
    snprintf(str, 256, "y=x?:123");
    setup_test('f', 1, 1, src_float, 'i', 1, 1, dest_int);
    result += parse_and_eval(EXPECT_SUCCESS);
    eprintf("Expected: %i\n", (int)src_float[0]?:123);

    /* Conditional that should be optimized */
    snprintf(str, 256, "y=1?2:123");
    setup_test('f', 1, 1, src_float, 'i', 1, 1, dest_int);
    result += parse_and_eval(EXPECT_SUCCESS);
    eprintf("Expected: 2\n");

    /* Building vectors with variables, operations inside vector-builder */
    snprintf(str, 256, "y=[x*-2+1,0]");
    setup_test('i', 1, 2, src_int, 'd', 1, 3, dest_double);
    result += parse_and_eval(EXPECT_SUCCESS);
    eprintf("Expected: [%g, %g, %g]\n", (double)src_int[0]*-2+1,
            (double)src_int[1]*-2+1, 0.0);

    /* Building vectors with variables, operations inside vector-builder */
    snprintf(str, 256, "y=[-99.4, -x*1.1+x]");
    setup_test('i', 1, 2, src_int, 'd', 1, 3, dest_double);
    result += parse_and_eval(EXPECT_SUCCESS);
    eprintf("Expected: [%g, %g, %g]\n", -99.4,
            (double)(-src_int[0]*1.1+src_int[0]),
            (double)(-src_int[1]*1.1+src_int[1]));

    /* Indexing vectors by range */
    snprintf(str, 256, "y=x[1:2]+100");
    setup_test('d', 1, 3, src_double, 'f', 1, 2, dest_float);
    result += parse_and_eval(EXPECT_SUCCESS);
    eprintf("Expected: [%g, %g]\n", (float)src_double[1]+100,
           (float)src_double[2]+100);

    /* Typical linear scaling expression with vectors */
    snprintf(str, 256, "y=x*[0.1,3.7,-.1112]+[2,1.3,9000]");
    setup_test('f', 1, 3, src_float, 'f', 1, 3, dest_float);
    result += parse_and_eval(EXPECT_SUCCESS);
    eprintf("Expected: [%g, %g, %g]\n", src_float[0]*0.1f+2.f,
            src_float[1]*3.7f+1.3f, src_float[2]*-.1112f+9000.f);

    /* Check type and vector length promotion of operation sequences */
    snprintf(str, 256, "y=1+2*3-4*x");
    setup_test('f', 1, 2, src_float, 'f', 1, 2, dest_float);
    result += parse_and_eval(EXPECT_SUCCESS);
    eprintf("Expected: [%g, %g]\n", 1.f+2.f*3.f-4.f*src_float[0],
            1.f+2.f*3.f-4.f*src_float[1]);

    /* Swizzling, more pre-computation */
    snprintf(str, 256, "y=[x[2],x[0]]*0+1+12");
    setup_test('f', 1, 3, src_float, 'f', 1, 2, dest_float);
    result += parse_and_eval(EXPECT_SUCCESS);
    eprintf("Expected: [%g, %g]\n", src_float[2]*0.f+1.f+12.f,
            src_float[0]*0.f+1.f+12.f);

    /* Logical negation */
    snprintf(str, 256, "y=!(x[1]*0)");
    setup_test('d', 1, 3, src_double, 'i', 1, 1, dest_int);
    result += parse_and_eval(EXPECT_SUCCESS);
    eprintf("Expected: %i\n", (int)!(src_double[1]*0));

    /* any() */
    snprintf(str, 256, "y=any(x-1)");
    setup_test('d', 1, 3, src_double, 'i', 1, 1, dest_int);
    result += parse_and_eval(EXPECT_SUCCESS);
    eprintf("Expected: %i\n", ((int)src_double[0]-1)?1:0
           | ((int)src_double[1]-1)?1:0
           | ((int)src_double[2]-1)?1:0);

    /* all() */
    snprintf(str, 256, "y=x[2]*all(x-1)");
    setup_test('d', 1, 3, src_double, 'i', 1, 1, dest_int);
    result += parse_and_eval(EXPECT_SUCCESS);
    int temp = ((int)src_double[0]-1)?1:0 & ((int)src_double[1]-1)?1:0
                & ((int)src_double[2]-1)?1:0;
    eprintf("Expected: %i\n", (int)src_double[2] * temp);

    /* pi and e, extra spaces */
    snprintf(str, 256, "y=x + pi -     e");
    setup_test('d', 1, 1, src_double, 'f', 1, 1, dest_float);
    result += parse_and_eval(EXPECT_SUCCESS);
    eprintf("Expected: %g\n", (float)(src_double[0]+M_PI-M_E));

    /* bad vector notation */
    snprintf(str, 256, "y=(x-2)[1]");
    setup_test('i', 1, 1, src_int, 'i', 1, 1, dest_int);
    result += parse_and_eval(EXPECT_FAILURE);
    eprintf("Expected: FAILURE\n");

    /* vector index outside bounds */
    snprintf(str, 256, "y=x[3]");
    setup_test('i', 1, 3, src_int, 'i', 1, 1, dest_int);
    result += parse_and_eval(EXPECT_FAILURE);
    eprintf("Expected: FAILURE\n");

    /* vector length mismatch */
    snprintf(str, 256, "y=x[1:2]");
    setup_test('i', 1, 3, src_int, 'i', 1, 1, dest_int);
    result += parse_and_eval(EXPECT_FAILURE);
    eprintf("Expected: FAILURE\n");

    /* unnecessary vector notation */
    snprintf(str, 256, "y=x+[1]");
    setup_test('i', 1, 1, src_int, 'i', 1, 1, dest_int);
    result += parse_and_eval(EXPECT_SUCCESS);
    eprintf("Expected: %i\n", src_int[0]+1);

    /* invalid history index */
    snprintf(str, 256, "y=x{-101}");
    setup_test('i', 1, 1, src_int, 'i', 1, 1, dest_int);
    result += parse_and_eval(EXPECT_FAILURE);
    eprintf("Expected: FAILURE\n");

    /* invalid history index */
    snprintf(str, 256, "y=x-y{-101}");
    setup_test('i', 1, 1, src_int, 'i', 1, 1, dest_int);
    result += parse_and_eval(EXPECT_FAILURE);
    eprintf("Expected: FAILURE\n");

    /* scientific notation */
    snprintf(str, 256, "y=x[1]*1.23e-20");
    setup_test('i', 1, 2, src_int, 'd', 1, 1, dest_double);
    result += parse_and_eval(EXPECT_SUCCESS);
    eprintf("Expected: %g\n", (double)src_int[1] * 1.23e-20);

    /* Vector assignment */
    snprintf(str, 256, "y[1]=x[1]");
    setup_test('d', 1, 3, src_double, 'i', 1, 3, dest_int);
    result += parse_and_eval(EXPECT_SUCCESS);
    eprintf("Expected: [NULL, %i, NULL]\n", (int)src_double[1]);

    /* Vector assignment */
    snprintf(str, 256, "y[1:2]=[x[1],10]");
    setup_test('d', 1, 3, src_double, 'i', 1, 3, dest_int);
    result += parse_and_eval(EXPECT_SUCCESS);
    eprintf("Expected: [NULL, %i, %i]\n", (int)src_double[1], 10);

    /* Output vector swizzling */
    snprintf(str, 256, "[y[0],y[2]]=x[1:2]");
    setup_test('f', 1, 3, src_float, 'd', 1, 3, dest_double);
    result += parse_and_eval(EXPECT_SUCCESS);
    eprintf("Expected: [%g, NULL, %g]\n", (double)src_float[1],
            (double)src_float[2]);

    /* Multiple expressions */
    snprintf(str, 256, "y[0]=x*100-23.5; y[2]=100-x*6.7");
    setup_test('i', 1, 1, src_int, 'f', 1, 3, dest_float);
    result += parse_and_eval(EXPECT_FAILURE);
    eprintf("Expected: FAILURE\n");

    /* Error check: separating sub-expressions with commas */
    snprintf(str, 256, "foo=1,  y=y{-1}+foo");
    setup_test('i', 1, 1, src_int, 'f', 1, 1, dest_float);
    result += parse_and_eval(EXPECT_FAILURE);
    eprintf("Expected: FAILURE\n");

    /* Initialize filters */
    snprintf(str, 256, "y=x+y{-1}; y{-1}=100");
    setup_test('i', 1, 1, src_int, 'i', 2, 1, dest_int);
    result += parse_and_eval(EXPECT_SUCCESS);
    eprintf("Expected: %i\n", src_int[0]*iterations + 100);

    /* Initialize filters + vector index */
    snprintf(str, 256, "y=x+y{-1}; y[1]{-1}=100");
    setup_test('i', 1, 2, src_int, 'i', 2, 2, dest_int);
    result += parse_and_eval(EXPECT_SUCCESS);
    eprintf("Expected: [%i, %i]\n", src_int[0]*iterations,
            src_int[1]*iterations + 100);

    /* Initialize filters + vector index */
    snprintf(str, 256, "y=x+y{-1}; y{-1}=[100,101]");
    setup_test('i', 1, 2, src_int, 'i', 2, 2, dest_int);
    result += parse_and_eval(EXPECT_SUCCESS);
    eprintf("Expected: [%i, %i]\n", src_int[0]*iterations + 100,
            src_int[1]*iterations + 101);

    /* Initialize filters */
    snprintf(str, 256, "y=x+y{-1}; y[0]{-1}=100; y[2]{-1}=200");
    setup_test('i', 1, 3, src_int, 'i', 2, 3, dest_int);
    result += parse_and_eval(EXPECT_SUCCESS);
    eprintf("Expected: [%i, %i, %i]\n", src_int[0]*iterations + 100,
            src_int[1]*iterations, src_int[2]*iterations + 200);

    /* Initialize filters */
    snprintf(str, 256, "y=x+y{-1}-y{-2}; y{-1}=[100,101]; y{-2}=[100,101]");
    setup_test('i', 1, 2, src_int, 'i', 3, 2, dest_int);
    result += parse_and_eval(EXPECT_SUCCESS);
    eprintf("Expected: [1, 2]\n");

    /* Only initialize */
    snprintf(str, 256, "y{-1}=100");
    setup_test('i', 1, 3, src_int, 'i', 1, 1, dest_int);
    result += parse_and_eval(EXPECT_FAILURE);
    eprintf("Expected: FAILURE\n");

    /* Some examples of bad syntax */
    strcpy(str, "");
    setup_test('i', 1, 1, src_int, 'f', 1, 3, dest_float);
    result += parse_and_eval(EXPECT_FAILURE);
    eprintf("Expected: FAILURE\n");
    snprintf(str, 256, " ");
    setup_test('i', 1, 1, src_int, 'f', 1, 3, dest_float);
    result += parse_and_eval(EXPECT_FAILURE);
    eprintf("Expected: FAILURE\n");
    snprintf(str, 256, "y");
    setup_test('i', 1, 1, src_int, 'f', 1, 3, dest_float);
    result += parse_and_eval(EXPECT_FAILURE);
    eprintf("Expected: FAILURE\n");
    snprintf(str, 256, "y=");
    setup_test('i', 1, 1, src_int, 'f', 1, 3, dest_float);
    result += parse_and_eval(EXPECT_FAILURE);
    eprintf("Expected: FAILURE\n");
    snprintf(str, 256, "=x");
    setup_test('i', 1, 1, src_int, 'f', 1, 3, dest_float);
    result += parse_and_eval(EXPECT_FAILURE);
    eprintf("Expected: FAILURE\n");
    snprintf(str, 256, "sin(x)");
    setup_test('i', 1, 1, src_int, 'f', 1, 3, dest_float);
    result += parse_and_eval(EXPECT_FAILURE);
    eprintf("Expected: FAILURE\n");

    /* Variable declaration */
    snprintf(str, 256, "var=3.5; y=x+var");
    setup_test('i', 1, 1, src_int, 'f', 1, 1, dest_float);
    result += parse_and_eval(EXPECT_SUCCESS);
    eprintf("Expected: %g\n", (float)src_int[0] + 3.5);

    /* Variable declaration */
    snprintf(str, 256, "ema=ema{-1}*0.9+x*0.1; y=ema*2; ema{-1}=90");
    setup_test('i', 1, 1, src_int, 'f', 1, 1, dest_float);
    result += parse_and_eval(EXPECT_SUCCESS);
    eprintf("Expected: 2\n");

    /* Use of uninitialized variable */
    snprintf(str, 256, "y=x + myvariable * 10");
    setup_test('i', 1, 1, src_int, 'f', 1, 1, dest_float);
    result += parse_and_eval(EXPECT_FAILURE);
    eprintf("Expected: FAILURE\n");

    /* Vector functions mean() and sum() */
    snprintf(str, 256, "y=mean(x)==(sum(x)/3)");
    setup_test('f', 1, 3, src_float, 'i', 1, 1, dest_int);
    result += parse_and_eval(EXPECT_SUCCESS);
    eprintf("Expected: %i\n", 1);

    /* Overloaded vector functions max() and min() */
    snprintf(str, 256, "y=max(x)-min(x)");
    setup_test('f', 1, 3, src_float, 'i', 1, 1, dest_int);
    result += parse_and_eval(EXPECT_SUCCESS);
    eprintf("Expected: %i\n",
            ((src_float[0]>src_float[1])?
             (src_float[0]>src_float[2]?(int)src_float[0]:(int)src_float[2]):
             (src_float[1]>src_float[2]?(int)src_float[1]:(int)src_float[2])) -
            ((src_float[0]<src_float[1])?
             (src_float[0]<src_float[2]?(int)src_float[0]:(int)src_float[2]):
             (src_float[1]<src_float[2]?(int)src_float[1]:(int)src_float[2])));

    /* Optimization: operations by zero */
    snprintf(str, 256, "y=0*sin(x)*200+1.1");
    setup_test('i', 1, 1, src_int, 'f', 1, 1, dest_float);
    result += parse_and_eval(EXPECT_SUCCESS);
    eprintf("Expected: 1.1\n");

    /* Optimization: operations by one */
    snprintf(str, 256, "y=x*1");
    setup_test('i', 1, 1, src_int, 'f', 1, 1, dest_float);
    result += parse_and_eval(EXPECT_SUCCESS);
    eprintf("Expected: 1\n");

    /* Error check: division by zero */
    snprintf(str, 256, "y=x/0");
    setup_test('i', 1, 1, src_int, 'f', 1, 1, dest_float);
    result += parse_and_eval(EXPECT_FAILURE);
    eprintf("Expected: FAILURE\n");

    return result;
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
                                "--num_iterations <int>\n");
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

    result = run_tests();
    eprintf("**********************************\n");
    printf("Test %s ", result ? "FAILED" : "PASSED");
    if (!result)
        printf("in %f seconds using %d tokens in total.\n",
               total_elapsed_time, token_count);
    else
        printf("\n");
}
