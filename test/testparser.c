
#include <../src/mapper_internal.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <sys/time.h>

/*! Internal function to get the current time. */
static double get_current_time()
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (double) tv.tv_sec + tv.tv_usec / 1000000.0;
}

int main()
{
    const char str[] = "y=26*2/2+log10(pi)+2.*pow(2,1*(3+7*.1)*1.1+x{-6*2+12}[0])*3*4+cos(2.)";
    int input_history_size, output_history_size;
    mapper_expr e = mapper_expr_new_from_string(str, 1, 1, 1, &input_history_size, &output_history_size);
    printf("Parsing %s\n", str);
    if (!e) { printf("Test FAILED.\n"); return 1; }
#ifdef DEBUG
    printexpr("Parser returned: ", e);
#endif
    printf("\n");

    float inp = 3.0, outp;

    // create signal_history structures
    mapper_signal_history_t inh, outh;
    inh.type = 'f';
    inh.size = 1;
    inh.length = 1;
    inh.value = &inp;
    inh.timetag = calloc(1, sizeof(mapper_timetag_t));
    inh.position = 0;

    outh.type = 'f';
    outh.size = 1;
    outh.length = 1;
    outh.value = &outp;
    outh.timetag = calloc(1, sizeof(mapper_timetag_t));
    outh.position = -1;

    int iterations = 1000000;
    double then = get_current_time();
    printf("Calculate expression %i times... ", iterations);
    while (iterations--) {
        mapper_expr_evaluate(e, &inh, &outh);
    }
    double now = get_current_time();
    printf("%f seconds.\n", now-then);

    printf("Evaluate with x=%f: %f (expected: %f)\n",
           inp, outp,
           26*2/2+log10f(M_PI)+2.f*powf(2,1*(3+7*.1f)*1.1f+inp)*3*4+cosf(2.0f));

    mapper_expr_free(e);
    free(inh.timetag);
    free(outh.timetag);

    return 0;
}
