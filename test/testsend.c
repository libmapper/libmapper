
#include "../src/mapper_internal.h"
#include <mapper/mapper.h>
#include <stdio.h>
#include <math.h>

#include <unistd.h>

int test_controller()
{
    mapper_device md = mdev_new("tester", 0, 0);
    if (!md)
        goto error;
    printf("Mapper device created.\n");

    while (!mdev_ready(md)) {
        mdev_poll(md, 100);
    }

    float mn=0, mx=1;
    mapper_signal sig = 
        mdev_add_output(md, "/testsig", 1, 'f', 0, &mn, &mx);

    printf("Output signal /testsig registered.\n");

    printf("Number of outputs: %d\n", mdev_num_outputs(md));

    const char *host = "localhost";
    int port = 9000;
    mapper_router rt = mapper_router_new(md, host, port, "DESTINATION", 0);
    mdev_add_router(md, rt);
    printf("Router to %s:%d added.\n", host, port);

    mapper_router_add_connection(rt, sig, "/mapped1", 'f', 1);
    mapper_router_add_connection(rt, sig, "/mapped2", 'f', 1);

    printf("Polling device..\n");
    int i;
    for (i = 0; i < 10; i++) {
        mdev_poll(md, 500);
        printf("Updating signal %s to %f\n",
               sig->props.name, (i * 1.0f));
        msig_update_float(sig, (i * 1.0f));
    }

    mdev_remove_router(md, rt);
    printf("Router removed.\n");

    mdev_free(md);
    return 0;

  error:
    if (md)
        mdev_free(md);
    return 1;
}

int main()
{
    int result = test_controller();
    if (result) {
        printf("Test FAILED.\n");
        return 1;
    }

    printf("Test PASSED.\n");
    return 0;
}
