
#include <../src/mapper_internal.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <sys/time.h>
#include <string.h>

#define eprintf(format, ...) do {               \
    if (verbose)                                \
        fprintf(stdout, format, ##__VA_ARGS__); \
} while(0)

int verbose = 1;
char str[256];
mapper_expr e;
int result = 0;
int iterations = 1000000;

int src_int[] = {1, 2, 3}, dest_int[3];
float src_float[] = {1.0f, 2.0f, 3.0f}, dest_float[3];
double src_double[] = {1.0, 2.0, 3.0}, dest_double[3];
double then, now;

mapper_timetag_t tt_in = {0, 0}, tt_out = {0, 0};

// signal_history structures
mapper_signal_history_t inh, outh;

/*! Internal function to get the current time. */
static double get_current_time()
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (double) tv.tv_sec + tv.tv_usec / 1000000.0;
}

/* Examples:
 unit-delay indexing
 vector length mismatches
 multiplication by 0
 addition/subtraction of 0
 division by 0?
 */

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

int parse_and_eval()
{
    eprintf("**********************************\n");
    eprintf("Parsing string '%s'\n", str);
    if (!(e = mapper_expr_new_from_string(str, inh.type, outh.type,
                                          inh.length, outh.length,
                                          &inh.size, &outh.size))) {
        eprintf("Parser FAILED.\n");
        return 1;
    }

#ifdef DEBUG
    if (verbose)
        printexpr("Parser returned:", e);
#endif

    if (!mapper_expr_evaluate(e, &inh, &outh)) {
        eprintf("Evaluation FAILED.\n");
        return 1;
    }

    then = get_current_time();
    eprintf("Calculate expression %i times... ", iterations);
    int i = iterations;
    while (i--) {
        mapper_expr_evaluate(e, &inh, &outh);
    }
    now = get_current_time();
    eprintf("%f seconds.\n", now-then);

    eprintf("Got:      ");
    if (verbose)
        mapper_prop_pp(outh.type, outh.length, outh.value);
    eprintf(" \n");

    mapper_expr_free(e);

    if (!verbose)
        printf(".");
    return 0;
}

int run_tests()
{
    int result = 0;

    /* Complex string */
    snprintf(str, 256, "y=26*2/2+log10(pi)+2.*pow(2,1*(3+7*.1)*1.1+x{0}[0])*3*4+cos(2.)");
    setup_test('f', 1, 1, src_float, 'f', 1, 1, dest_float);
    result += parse_and_eval();
    eprintf("Expected: %f\n", 26*2/2+log10f(M_PI)+2.f*powf(2,1*(3+7*.1f)*1.1f+src_float[0])*3*4+cosf(2.0f));

    /* Building vectors, conditionals */
    snprintf(str, 256, "y=(x>1)?[1,2,3]:[2,4,6]");
    setup_test('f', 1, 3, src_float, 'i', 1, 3, dest_int);
    result += parse_and_eval();
    eprintf("Expected: [%i, %i, %i]\n", src_float[0]>1?1:2, src_float[1]>1?2:4,
           src_float[2]>1?3:6);

    /* Conditionals with shortened syntax */
    snprintf(str, 256, "y=x?:123");
    setup_test('f', 1, 1, src_float, 'i', 1, 1, dest_int);
    result += parse_and_eval();
    eprintf("Expected: %i\n", (int)src_float[0]?:123);

    /* Building vectors with variables, operations inside vector-builder */
    snprintf(str, 256, "y=[x*-2+1,0]");
    setup_test('i', 1, 2, src_int, 'd', 1, 3, dest_double);
    result += parse_and_eval();
    eprintf("Expected: [%f, %f, %f]\n", (double)src_int[0]*-2+1,
           (double)src_int[1]*-2+1, 0.0);

    /* Building vectors with variables, operations inside vector-builder */
    snprintf(str, 256, "y=[-99.4, -x*1.1+x]");
    setup_test('i', 1, 2, src_int, 'd', 1, 3, dest_double);
    result += parse_and_eval();
    eprintf("Expected: [%f, %f, %f]\n", -99.4,
           (double)(-src_int[0]*1.1+src_int[0]),
           (double)(-src_int[1]*1.1+src_int[1]));

    /* Indexing vectors by range */
    snprintf(str, 256, "y=x[1:2]+100");
    setup_test('d', 1, 3, src_double, 'f', 1, 2, dest_float);
    result += parse_and_eval();
    eprintf("Expected: [%f, %f]\n", (float)src_double[1]+100,
           (float)src_double[2]+100);

    /* Typical linear scaling expression with vectors */
    snprintf(str, 256, "y=x*[0.1,3.7,-.1112]+[2,1.3,9000]");
    setup_test('f', 1, 3, src_float, 'f', 1, 3, dest_float);
    result += parse_and_eval();
    eprintf("Expected: [%f, %f, %f]\n", src_float[0]*0.1f+2.f,
           src_float[1]*3.7f+1.3f, src_float[2]*-.1112f+9000.f);

    /* Check type and vector length promotion of operation sequences */
    snprintf(str, 256, "y=1+2*3-4*x");
    setup_test('f', 1, 2, src_float, 'f', 1, 2, dest_float);
    result += parse_and_eval();
    eprintf("Expected: [%f, %f]\n", 1.f+2.f*3.f-4.f*src_float[0],
           1.f+2.f*3.f-4.f*src_float[1]);

    /* Swizzling, more pre-computation */
    snprintf(str, 256, "y=[x[2],x[0]]*0+1+12");
    setup_test('f', 1, 3, src_float, 'f', 1, 2, dest_float);
    result += parse_and_eval();
    eprintf("Expected: [%f, %f]\n", src_float[2]*0.f+1.f+12.f,
           src_float[0]*0.f+1.f+12.f);

    /* Logical negation */
    snprintf(str, 256, "y=!(x[1]*0)");
    setup_test('d', 1, 3, src_double, 'i', 1, 1, dest_int);
    result += parse_and_eval();
    eprintf("Expected: %i\n", (int)!(src_double[1]*0));

    /* any() */
    snprintf(str, 256, "y=any(x-1)");
    setup_test('d', 1, 3, src_double, 'i', 1, 1, dest_int);
    result += parse_and_eval();
    eprintf("Expected: %i\n", ((int)src_double[0]-1)?1:0
           | ((int)src_double[1]-1)?1:0
           | ((int)src_double[2]-1)?1:0);

    /* all() */
    snprintf(str, 256, "y=x[1:2]*all(x-1)");
    setup_test('d', 1, 3, src_double, 'i', 1, 2, dest_int);
    result += parse_and_eval();
    int temp = ((int)src_double[0]-1)?1:0 & ((int)src_double[1]-1)?1:0
                & ((int)src_double[2]-1)?1:0;
    eprintf("Expected: [%i, %i]\n", (int)src_double[1] * temp,
           (int)src_double[2] * temp);

    /* pi and e, extra spaces */
    snprintf(str, 256, "y=x + pi -     e");
    setup_test('d', 1, 1, src_double, 'f', 1, 1, dest_float);
    result += parse_and_eval();
    eprintf("Expected: %f\n", (float)(src_double[0]+M_PI-M_E));

    /* bad vector notation */
    snprintf(str, 256, "y=(x-2)[1]");
    setup_test('i', 1, 1, src_int, 'i', 1, 1, dest_int);
    result += !parse_and_eval();
    eprintf("Expected: FAILURE\n");

    /* vector index outside bounds */
    snprintf(str, 256, "y=x[3]");
    setup_test('i', 1, 3, src_int, 'i', 1, 1, dest_int);
    result += !parse_and_eval();
    eprintf("Expected: FAILURE\n");

    /* vector length mismatch */
    snprintf(str, 256, "y=x[1:2]");
    setup_test('i', 1, 3, src_int, 'i', 1, 1, dest_int);
    result += !parse_and_eval();
    eprintf("Expected: FAILURE\n");

    /* unnecessary vector notation */
    snprintf(str, 256, "y=x+[1]");
    setup_test('i', 1, 1, src_int, 'i', 1, 1, dest_int);
    result += parse_and_eval();
    eprintf("Expected: %i\n", src_int[0]+1);

    /* invalid history index */
    snprintf(str, 256, "y=x{-101}");
    setup_test('i', 1, 1, src_int, 'i', 1, 1, dest_int);
    result += !parse_and_eval();
    eprintf("Expected: FAILURE\n");

    /* invalid history index */
    snprintf(str, 256, "y=x-y{-101}");
    setup_test('i', 1, 1, src_int, 'i', 1, 1, dest_int);
    result += !parse_and_eval();
    eprintf("Expected: FAILURE\n");

    /* Initialize filters */
//    snprintf(str, 256, "y=x+y{-1}, y{-1}=100");
//    setup_test('i', 1, 1, src_int, 'i', 1, 1, dest_int);
//    result += parse_and_eval();
//    eprintf("Expected: %i\n", src_int[0]*iterations + 100);

    /* TODO: scientific notation */

    /* Vector functions mean() and sum() */
    snprintf(str, 256, "y=mean(x)==(sum(x)/3)");
    setup_test('f', 1, 3, src_float, 'i', 1, 1, dest_int);
    result += parse_and_eval();
    eprintf("Expected: %i\n", 1);

    /* Vector function vmax() and vmin() */
    snprintf(str, 256, "y=vmax(x)-vmin(x)");
    setup_test('f', 1, 3, src_float, 'i', 1, 1, dest_int);
    result += parse_and_eval();
    eprintf("Expected: %i\n",
            ((src_float[0]>src_float[1])?
             (src_float[0]>src_float[2]?(int)src_float[0]:(int)src_float[2]):
             (src_float[1]>src_float[2]?(int)src_float[1]:(int)src_float[2])) -
            ((src_float[0]<src_float[1])?
             (src_float[0]<src_float[2]?(int)src_float[0]:(int)src_float[2]):
             (src_float[1]<src_float[2]?(int)src_float[1]:(int)src_float[2])));

    printf("**********************************\n");
    printf("Failed %d tests\n", result);

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
                                "-h help\n");
                        return 1;
                        break;
                    case 'q':
                        verbose = 0;
                        break;
                    default:
                        break;
                }
            }
        }
    }

    result = run_tests();
    printf("Test %s.\n", result ? "FAILED" : "PASSED");
}
