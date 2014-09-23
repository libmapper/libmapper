
#include "../src/mapper_internal.h"
#include <mapper/mapper.h>
#include <stdio.h>
#include <math.h>
#include <lo/lo.h>

#include <unistd.h>
#include <signal.h>

mapper_device mdev = 0;
mapper_signal inputs[100];
mapper_signal outputs[100];

void sig_handler(mapper_signal sig, mapper_db_signal props,
                 int instance_id, void *value, int count,
                 mapper_timetag_t *timetag)
{
    if (value) {
        printf("--> destination got %s", props->name);
        float *v = value;
        for (int i = 0; i < props->length; i++) {
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
    mdev = mdev_new("testsignals", 0, 0);
    if (!mdev) {
        result = 1;
        goto done;
    }

    printf("Adding signals... ");
    fflush(stdout);
    for (i = 0; i < 100; i++) {
        mdev_poll(mdev, 100);
        snprintf(signame, 32, "/s%i", i);
        if (!(inputs[i] = mdev_add_input(mdev, signame, 1, 'f', 0, 0,
                                         0, sig_handler, 0))) {
            result = 1;
            goto done;
        }
        if (!(outputs[i] = mdev_add_output(mdev, signame, 1, 'f', 0, 0, 0))) {
            result = 1;
            goto done;
        }
    }
    printf("Removing 200 signals...\n");
    for (i = 0; i < 100; i++) {
        mdev_remove_input(mdev, inputs[i]);
        mdev_remove_output(mdev, outputs[i]);
        mdev_poll(mdev, 100);
    }

  done:
    if (mdev)
        mdev_free(mdev);
    printf("Test %s.\n", result ? "FAILED" : "PASSED");
    return result;
}
