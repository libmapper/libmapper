#include "../src/mapper_internal.h"
#include <mapper/mapper.h>
#include <stdio.h>
#include <stdarg.h>
#include <math.h>
#include <lo/lo.h>

#include <unistd.h>
#include <signal.h>

#define num_inputs 100
#define num_outputs 100

int done = 0;
int verbose = 1;
int period = 100;
int terminate = 0;
int wait_ms = 10000;

mpr_dev dev = 0;
mpr_sig inputs[num_inputs];
mpr_sig outputs[num_outputs];

static void eprintf(const char *format, ...)
{
    va_list args;
    if (!verbose)
        return;
    va_start(args, format);
    vprintf(format, args);
    va_end(args);
}

void handler(mpr_sig sig, mpr_sig_evt event, mpr_id inst, int len,
             mpr_type type, const void *val, mpr_time t)
{
    const char *name;
    float *fval;
    int i;

    if (!val)
        return;

    name = mpr_obj_get_prop_as_str((mpr_obj)sig, MPR_PROP_NAME, NULL);
    eprintf("--> destination got %s", name);
    fval = (float*)val;
    for (i = 0; i < sig->len; i++) {
        eprintf(" %f", fval[i]);
    }
    eprintf("\n");
}

void loop()
{
    if (terminate) {
        while (wait_ms > 0 && !done) {
            printf("\rWaiting for %d ms.", wait_ms);
            fflush(stdout);
            mpr_dev_poll(dev, 100);
            wait_ms -= 100;
        }
    }
    else {
        while (!done) {
            mpr_dev_poll(dev, 100);
        }
    }
}

void ctrlc(int sig)
{
    done = 1;
}

int main(int argc, char ** argv)
{
    int i, j, result = 0, max;
    char signame[32];
    char *iface = 0;

    /* process flags */
    for (i = 1; i < argc; i++) {
        if (argv[i] && argv[i][0] == '-') {
            int len = strlen(argv[i]);
            for (j = 1; j < len; j++) {
                switch (argv[i][j]) {
                    case 'h':
                        printf("test.c: possible arguments "
                               "-q quiet (suppress output), "
                               "-f fast (execute quickly), "
                               "-h help, "
                               "--iface network interface\n");
                        return 1;
                        break;
                    case 'q':
                        verbose = 0;
                        break;
                    case 't':
                        terminate = 1;
                        break;
                    case 'f':
                        period = 1;
                        break;
                    case '-':
                        if (strcmp(argv[i], "--iface")==0 && argc>i+1) {
                            i++;
                            iface = argv[i];
                            j = 1;
                        }
                        break;
                    default:
                        break;
                }
            }
        }
    }

    signal(SIGINT, ctrlc);

    eprintf("Creating device... ");
    fflush(stdout);
    dev = mpr_dev_new("testsignals", 0);
    if (!dev) {
        result = 1;
        goto done;
    }
    if (iface)
        mpr_graph_set_interface(mpr_obj_get_graph((mpr_obj)dev), iface);
    eprintf("device created using interface %s.\n",
            mpr_graph_get_interface(mpr_obj_get_graph((mpr_obj)dev)));
    while (!mpr_dev_get_is_ready(dev)) {
        mpr_dev_poll(dev, 100);
    }

    eprintf("Adding %d signals... ", num_inputs + num_outputs);
    fflush(stdout);
    max = num_inputs > num_outputs ? num_inputs : num_outputs;
    for (i = 0; i < max && !done; i++) {
        mpr_dev_poll(dev, 100);
        if (i < num_inputs) {
            snprintf(signame, 32, "in%i", i);
            if (!(inputs[i] = mpr_sig_new(dev, MPR_DIR_IN, signame, 1, MPR_FLT,
                                          NULL, NULL, NULL, NULL, handler,
                                          MPR_SIG_UPDATE))) {
                result = 1;
                goto done;
            }
        }
        if (i < num_outputs) {
            snprintf(signame, 32, "out%i", i);
            if (!(outputs[i] = mpr_sig_new(dev, MPR_DIR_OUT, signame, 1, MPR_FLT,
                                           NULL, NULL, NULL, NULL, NULL, 0))) {
                result = 1;
                goto done;
            }
        }
    }

    loop();

    eprintf("Removing %d signals...\n", num_inputs + num_outputs);
    for (i = 0; i < 100 && !done; i++) {
        if (i < num_inputs)
            mpr_sig_free(inputs[i]);
        if (i < num_outputs)
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
