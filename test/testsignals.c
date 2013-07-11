
#include "../src/mapper_internal.h"
#include <mapper/mapper.h>
#include <stdio.h>
#include <math.h>
#include <lo/lo.h>

#include <unistd.h>
#include <signal.h>

mapper_device mdev = 0;

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
    int i;
    char signame[32];

    printf("Creating device...\n");
    mdev = mdev_new("testsignals", 0, 0);
    if (!mdev)
        goto error;

    printf("Adding 200 signals...\n");
    for (i = 0; i < 100; i++) {
        snprintf(signame, 32, "/s%i", i);
        if (!mdev_add_input(mdev, signame, 1, 'f', 0, 0, 0, sig_handler, 0))
            goto error;
        if (!mdev_add_output(mdev, signame, 1, 'f', 0, 0, 0))
            goto error;
    }

    printf("Waiting for 20 seconds...\n");
    for (i = 2000; i > 0; i--)
        mdev_poll(mdev, 10);

    mdev_free(mdev);
    return 0;

  error:
    return 1;
}
