#include "../src/operations.h"
#include "../src/expression.h"
#include "../src/mapper_internal.h"
#include <mapper/mapper.h>
#include <stdio.h>
#include <math.h>

#include <lo/lo.h>

#include <unistd.h>
#include <arpa/inet.h>

int sent=0;
int received=0;

void handler(mapper_device mdev, mapper_signal_value_t *v)
{
    printf("handler: Got %f\n", (*v).f);
    received++;
}

int test_recv()
{
    lo_address a = lo_address_new("localhost", "9000");
    if (!a) { printf("Error creating lo_address for test.\n"); return 1; }

    mapper_device md = mdev_new("synth", 9000);
    if (!md) goto error;
    printf("Mapper device created.\n");

    mapper_signal sig =
        msig_float(1, "/mapped1", 0, INFINITY, INFINITY, 0, handler, 0);

    mdev_register_input(md, sig);

    printf("Input signal /mapped1 registered.\n");

    printf("Number of inputs: %d\n", mdev_num_inputs(md));

    printf("Waiting for port/ordinal allocation..\n");
    int i;
    char signame[1024];
    for (i=0; i<10; i++) {
        mdev_poll(md, 500);
        if (msig_full_name(sig, signame, 1024))
            break;
        usleep(500*1000);
    }
    if (i>=10) {
        printf("Timed out waiting for signal name.\n");
        goto error;
    }

    printf("Polling device..\n");
    for (i=0; i<10; i++) {
        lo_send(a, signame, "f", (float)i);
        sent++;
        mdev_poll(md, 500);
    }

    if (sent!=received) {
        printf("Not all sent values were received.\n");
        printf("Sent %d values, but %d received.\n",
               sent, received);
        goto error;
    }
    if (sent==0) {
        printf("Unable to send any values.\n");
        goto error;
    }
    printf("Sent and received %d values.\n", sent);

    mdev_free(md);
    lo_address_free(a);
    return 0;

  error:
    if (md) mdev_free(md);
    lo_address_free(a);
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
