
#include "../src/mapper_internal.h"
#include <mapper/mapper.h>
#include <stdio.h>
#include <math.h>

#include <lo/lo.h>

#include <unistd.h>

#ifdef WIN32
#define usleep(x) Sleep(x/1000)
#endif

int sent = 0;
int received = 0;

void handler(mapper_signal sig, mapper_db_signal props,
             int instance_id, void *value, int count,
             mapper_timetag_t *timetag)
{
    if (value) {
        printf("handler: Got %f\n", (*(float*)value));
    }
    received++;
}

int test_recv()
{
    mapper_device md = mdev_new("synth", 0, 0);
    if (!md)
        goto error;
    printf("Mapper device created.\n");

    float mn=0, mx=1;
    mapper_signal sig = 
        mdev_add_input(md, "/mapped1", 1, 'f', 0, &mn, &mx, handler, 0);

    printf("Input signal /mapped1 registered.\n");

    printf("Number of inputs: %d\n", mdev_num_inputs(md));

    printf("Waiting for port/ordinal allocation..\n");
    int i;
    for (i = 0; i < 10; i++) {
        mdev_poll(md, 500);
        if (mdev_ready(md))
            break;
        usleep(500 * 1000);
    }
    if (i >= 10) {
        printf("Timed out waiting for signal name.\n");
        goto error;
    }
	
	char port[10];
	sprintf(port, "%i", md->props.port);
	printf("using port = %s\n", port);
	
	lo_address a = lo_address_new("localhost", port);
    if (!a) {
        printf("Error creating lo_address for test.\n");
        return 1;
    }

    printf("Polling device..\n");
    for (i = 0; i < 10; i++) {
        lo_send(a, sig->props.name, "f", (float) i);
		printf("Updating signal %s to %f\n", sig->props.name, (float) i);
        sent++;
        mdev_poll(md, 500);
		//usleep(500 * 1000);
    }

    if (sent != received) {
        printf("Not all sent values were received.\n");
        printf("Sent %d values, but %d received.\n", sent, received);
        goto error;
    }
    if (sent == 0) {
        printf("Unable to send any values.\n");
        goto error;
    }
    printf("Sent and received %d values.\n", sent);

    mdev_free(md);
    lo_address_free(a);
    return 0;

  error:
    if (md)
        mdev_free(md);
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
