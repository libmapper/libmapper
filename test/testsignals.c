
#include "../src/mapper_internal.h"
#include <mapper/mapper.h>
#include <stdio.h>
#include <math.h>
#include <lo/lo.h>

#include <unistd.h>
#include <signal.h>

#define num_inputs 100
#define num_outputs 100

#define eprintf(format, ...) do {               \
    if (verbose)                                \
        fprintf(stdout, format, ##__VA_ARGS__); \
} while(0)

int wait_ms = 10000;
int done = 0;
int verbose = 1;
int terminate = 0;

mapper_device dev = 0;
mapper_signal inputs[num_inputs];
mapper_signal outputs[num_outputs];

void sig_handler(mapper_signal sig, mapper_id instance, const void *value,
                 int count, mapper_timetag_t *timetag)
{
    if (value) {
        eprintf("--> destination got %s", sig->name);
        float *v = (float*)value;
        for (int i = 0; i < sig->length; i++) {
            eprintf(" %f", v[i]);
        }
        eprintf("\n");
    }
}

void loop()
{
    if (terminate) {
        while (wait_ms > 0 && !done) {
            eprintf("\rWaiting for %d ms.", wait_ms);
            fflush(stdout);
            mapper_device_poll(dev, 100);
            wait_ms -= 100;
        }
    }
    else {
        while (!done) {
            mapper_device_poll(dev, 100);
        }
    }
}

void ctrlc(int sig)
{
    done = 1;
}

int main(int argc, char ** argv)
{
    int i, j, result = 0;

    // process flags for -v verbose, -t terminate, -h help
    for (i = 1; i < argc; i++) {
        if (argv[i] && argv[i][0] == '-') {
            int len = strlen(argv[i]);
            for (j = 1; j < len; j++) {
                switch (argv[i][j]) {
                    case 'h':
                        printf("test.c: possible arguments "
                               "-q quiet (suppress output), "
                               "-t terminate automatically, "
                               "-h help\n");
                        return 1;
                        break;
                    case 'q':
                        verbose = 0;
                        break;
                    case 't':
                        terminate = 1;
                        break;
                    default:
                        break;
                }
            }
        }
    }

    signal(SIGINT, ctrlc);

    char signame[32];

    eprintf("Creating device... ");
    fflush(stdout);
    dev = mapper_device_new("testsignals", 0, 0);
    if (!dev) {
        result = 1;
        goto done;
    }
    while (!mapper_device_ready(dev)) {
        mapper_device_poll(dev, 100);
    }

    eprintf("Adding %d signals...\n", num_inputs + num_outputs);
    fflush(stdout);
    int max = num_inputs > num_outputs ? num_inputs : num_outputs;
    for (i = 0; i < max && !done; i++) {
        mapper_device_poll(dev, 100);
        if (i < num_inputs) {
            snprintf(signame, 32, "in%i", i);
            if (!(inputs[i] = mapper_device_add_input_signal(dev, signame, 1, 'f', 0,
                                                             0, 0, sig_handler, 0))) {
                result = 1;
                goto done;
            }
        }
        if (i < num_outputs) {
            snprintf(signame, 32, "out%i", i);
            if (!(outputs[i] = mapper_device_add_output_signal(dev, signame, 1, 'f',
                                                               0, 0, 0))) {
                result = 1;
                goto done;
            }
        }
    }

    loop();

    eprintf("\rRemoving %d signals...\n", num_inputs + num_outputs);
    for (i = 0; i < max && !done; i++) {
        if (i < num_inputs)
            mapper_device_remove_signal(dev, inputs[i]);
        if (i < num_outputs)
            mapper_device_remove_signal(dev, outputs[i]);
        mapper_device_poll(dev, 100);
    }

  done:
    if (dev)
        mapper_device_free(dev);
    eprintf("Test %s.\n", result ? "FAILED" : "PASSED");
    return result;
}
