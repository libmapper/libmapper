
#include <../src/mapper_internal.h>
#include <math.h>

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
    outp = mapper_expr_evaluate(e, &inp);

    printf("Evaluate with x=%f: %f (expected: %f)\n",
           inp.f, outp.f,
           26*2/2+log10f(M_PI)+2.f*powf(2,1*(3+7*.1f)*1.1f+inp.f)*3*4+cosf(2.0f));

    mapper_expr_free(e);

    return 0;
}
