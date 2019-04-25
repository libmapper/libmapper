
#include "../src/mpr_internal.h"
#include <mpr/mpr.h>
#include <stdio.h>
#include <math.h>
#include <lo/lo.h>

#include <unistd.h>
#include <signal.h>

#define eprintf(format, ...) do {               \
    if (verbose)                                \
        fprintf(stdout, format, ##__VA_ARGS__); \
} while(0)

int verbose = 1;
int period = 100;

mpr_dev dev = 0;
mpr_sig inputs[100];
mpr_sig outputs[100];

void handler(mpr_sig sig, mpr_sig_evt event, mpr_id inst, int len,
             mpr_type type, const void *val, mpr_time t)
{
    if (!val)
        return;

    const char *name = mpr_obj_get_prop_as_str((mpr_obj)sig, MPR_PROP_NAME, NULL);
    eprintf("--> destination got %s", name);
    float *v = (float*)val;
    for (int i = 0; i < sig->len; i++) {
        eprintf(" %f", v[i]);
    }
    eprintf("\n");
}

int main(int argc, char ** argv)
{
    int i, j, result = 0;

    // process flags
    for (i = 1; i < argc; i++) {
        if (argv[i] && argv[i][0] == '-') {
            int len = strlen(argv[i]);
            for (j = 1; j < len; j++) {
                switch (argv[i][j]) {
                    case 'h':
                        printf("test.c: possible arguments "
                               "-q quiet (suppress output), "
                               "-f fast (execute quickly), "
                               "-h help\n");
                        return 1;
                        break;
                    case 'q':
                        verbose = 0;
                        break;
                    case 'f':
                        period = 1;
                        break;
                    default:
                        break;
                }
            }
        }
    }

    char signame[32];

    eprintf("Creating device... ");
    fflush(stdout);
    dev = mpr_dev_new("testsignals", 0);
    if (!dev) {
        result = 1;
        goto done;
    }
    while (!mpr_dev_get_is_ready(dev)) {
        mpr_dev_poll(dev, 100);
    }

    eprintf("Adding 200 signals... ");
    fflush(stdout);
    for (i = 0; i < 100; i++) {
        mpr_dev_poll(dev, 100);
        snprintf(signame, 32, "in%i", i);
        if (!(inputs[i] = mpr_sig_new(dev, MPR_DIR_IN, signame, 1, MPR_FLT, NULL,
                                      NULL, NULL, NULL, handler, MPR_SIG_UPDATE))) {
            result = 1;
            goto done;
        }
        snprintf(signame, 32, "out%i", i);
        if (!(outputs[i] = mpr_sig_new(dev, MPR_DIR_OUT, signame, 1, MPR_FLT,
                                       NULL, NULL, NULL, NULL, NULL, 0))) {
            result = 1;
            goto done;
        }
    }
    eprintf("Removing 200 signals...\n");
    for (i = 0; i < 100; i++) {
        mpr_sig_free(inputs[i]);
        mpr_sig_free(outputs[i]);
        mpr_dev_poll(dev, period);
    }

  done:
    if (dev)
        mpr_dev_free(dev);
    printf("........Test %s\x1B[0m.\n",
           result ? "\x1B[31mFAILED" : "\x1B[32mPASSED");
    return result;
}
