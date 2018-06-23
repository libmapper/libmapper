
#include "../src/mapper_internal.h"
#include <mapper/mapper.h>
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

mapper_device dev = 0;
mapper_signal inputs[100];
mapper_signal outputs[100];

void sig_handler(mapper_signal sig, mapper_id inst, int len, mapper_type type,
                 const void *val, mapper_time t)
{
    if (!val)
        return;

    const char *name;
    mapper_object_get_prop_by_index((mapper_object)sig, MAPPER_PROP_NAME, NULL,
                                    NULL, NULL, (const void**)&name);
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
    dev = mapper_device_new("testsignals", 0);
    if (!dev) {
        result = 1;
        goto done;
    }
    while (!mapper_device_ready(dev)) {
        mapper_device_poll(dev, 100);
    }

    eprintf("Adding 200 signals... ");
    fflush(stdout);
    for (i = 0; i < 100; i++) {
        mapper_device_poll(dev, 100);
        snprintf(signame, 32, "in%i", i);
        if (!(inputs[i] = mapper_device_add_signal(dev, MAPPER_DIR_IN, 1,
                                                   signame, 1, MAPPER_FLOAT,
                                                   NULL, NULL, NULL,
                                                   sig_handler))) {
            result = 1;
            goto done;
        }
        snprintf(signame, 32, "out%i", i);
        if (!(outputs[i] = mapper_device_add_signal(dev, MAPPER_DIR_OUT, 1,
                                                    signame, 1, MAPPER_FLOAT,
                                                    NULL, NULL, NULL, NULL))) {
            result = 1;
            goto done;
        }
    }
    eprintf("Removing 200 signals...\n");
    for (i = 0; i < 100; i++) {
        mapper_device_remove_signal(dev, inputs[i]);
        mapper_device_remove_signal(dev, outputs[i]);
        mapper_device_poll(dev, period);
    }

  done:
    if (dev)
        mapper_device_free(dev);
    printf("........Test %s\x1B[0m.\n",
           result ? "\x1B[31mFAILED" : "\x1B[32mPASSED");
    return result;
}
