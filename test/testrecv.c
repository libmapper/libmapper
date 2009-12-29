
#include "../src/mapper_internal.h"
#include <mapper/mapper.h>
#include <stdio.h>
#include <math.h>

#include <unistd.h>
#include <arpa/inet.h>

void handler(mapper_device mdev, mapper_signal_value_t *v)
{
    printf("handler: Got %f\n", (*v).f);
}

int test_recv()
{
    mapper_device md = mdev_new("synth", 9000);
    if (!md) goto error;
    printf("Mapper device created.\n");

    mapper_signal sig =
        msig_float(1, "/mapped1", 0, INFINITY, INFINITY, 0, handler, 0);

    mdev_register_input(md, sig);

    printf("Input signal /mapped1 registered.\n");

    printf("Number of inputs: %d\n", mdev_num_inputs(md));

    printf("Polling device..\n");
    int i;
    for (i=0; i<10; i++) {
        mdev_poll(md, 500);
    }

    mdev_free(md);
    return 0;

  error:
    if (md) mdev_free(md);
    return 1;
}

int main()
{
    int result = test_recv();
    if (result) {
        printf("Test FAILED.\n");
        return 1;
    }

    printf("Test PASSED.\n");
    return 0;
}
