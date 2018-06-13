
#include "../src/mapper_internal.h"
#include <mapper/mapper.h>
#include <stdio.h>
#include <math.h>
#include <lo/lo.h>

#include <unistd.h>
#include <signal.h>

mapper_device dev = 0;
mapper_signal inputs[100];
mapper_signal outputs[100];

void sig_handler(mapper_signal sig, mapper_id instance, const void *value,
                 int count, mapper_timetag_t *timetag)
{
    if (value) {
        printf("--> destination got %s", sig->name);
        float *v = (float*)value;
        for (int i = 0; i < sig->length; i++) {
            printf(" %f", v[i]);
        }
        printf("\n");
    }
}

int main(int argc, char ** argv)
{
    int i, result = 0;
    char signame[32];

    printf("Creating device... ");
    fflush(stdout);
    dev = mapper_device_new("testsignals", 0, 0);
    if (!dev) {
        result = 1;
        goto done;
    }
    while (!mapper_device_ready(dev)) {
        mapper_device_poll(dev, 100);
    }

    printf("Adding 200 signals... ");
    fflush(stdout);
    for (i = 0; i < 100; i++) {
        mapper_device_poll(dev, 100);
        snprintf(signame, 32, "in%i", i);
        if (!(inputs[i] = mapper_device_add_input_signal(dev, signame, 1,
                                                         MAPPER_FLOAT, 0, 0, 0,
                                                         sig_handler, 0))) {
            result = 1;
            goto done;
        }
        snprintf(signame, 32, "out%i", i);
        if (!(outputs[i] = mapper_device_add_output_signal(dev, signame, 1,
                                                           MAPPER_FLOAT,
                                                           0, 0, 0))) {
            result = 1;
            goto done;
        }
    }
    printf("Removing 200 signals...\n");
    for (i = 0; i < 100; i++) {
        mapper_device_remove_signal(dev, inputs[i]);
        mapper_device_remove_signal(dev, outputs[i]);
        mapper_device_poll(dev, 100);
    }

  done:
    if (dev)
        mapper_device_free(dev);
    printf("Test %s.\n", result ? "FAILED" : "PASSED");
    return result;
}
