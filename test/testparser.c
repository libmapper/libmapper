
#include <../src/mapper_internal.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

int main()
{
    const char str[] = "y=26*2/2+log10(pi)+2.*pow(2,1*(3+7*.1)*1.1+x{-6*2+12}[0])*3*4+cos(2.)";
    mapper_expr e = mapper_expr_new_from_string(str, 1, 1, 1);
    printf("Parsing %s\n", str);
    if (!e) { printf("Test FAILED.\n"); return 1; }
#ifdef DEBUG
    printexpr("Parser returned: ", e);
#endif
    printf("\n");

    mapper_signal_value_t inp, outp;
    inp.f = 3.0;

    // create signal_history structures
    mapper_signal_history_t inh, outh;
    inh.value = calloc(1, sizeof(mapper_signal_value_t));
    inh.timetag = calloc(1, sizeof(mapper_timetag_t));
    outh.value = calloc(1, sizeof(mapper_signal_value_t));
    outh.timetag = calloc(1, sizeof(mapper_signal_value_t));
    inh.size = outh.size = 1;
    inh.position = outh.position = -1;

    inh.value = &inp.f;
    inh.position = 0;

    outp = mapper_expr_evaluate(e, &inp, &inh, &outh, 0);

    printf("Evaluate with x=%f: %f (expected: %f)\n",
           inp.f, outp.f,
           26*2/2+log10f(M_PI)+2.f*powf(2,1*(3+7*.1f)*1.1f+inp.f)*3*4+cosf(2.0f));

    mapper_expr_free(e);
    free(inh.value);
    free(inh.timetag);
    free(outh.value);
    free(outh.timetag);

    return 0;
}
