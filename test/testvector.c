
#include "../src/mapper_internal.h"
#include <mapper/mapper.h>
#include <stdio.h>
#include <math.h>
#include <lo/lo.h>

#include <unistd.h>
#include <arpa/inet.h>
#include <signal.h>

int automate = 1;

mapper_device source = 0;
mapper_device destination = 0;
mapper_signal sendsig = 0;
mapper_signal recvsig_1 = 0;
mapper_signal recvsig_2 = 0;
mapper_signal recvsig_3 = 0;

int port = 9000;

int sent = 0;
int received = 0;
int done = 0;

/*! Creation of a local source. */
int setup_source()
{
    source = mdev_new("testsend", port, 0);
    if (!source)
        goto error;
    printf("source created.\n");

    float mn=0, mx=1;

    sendsig = mdev_add_output(source, "/outvec", 3, 'f', 0, &mn, &mx);
    if (!sendsig) {
        printf("Could not create source signal.\n");
        goto error;
    }

    printf("Output signal /outvec registered.\n");

    printf("Number of outputs: %d\n", mdev_num_outputs(source));

    return 0;

  error:
    return 1;
}

void cleanup_source()
{
    if (source) {
        if (source->routers) {
            printf("Removing router.. ");
            fflush(stdout);
            mdev_remove_router(source, source->routers);
            printf("ok\n");
        }
        printf("Freeing source.. ");
        fflush(stdout);
        mdev_free(source);
        printf("ok\n");
    }
}

void invec1_handler(mapper_signal sig, void *v)
{
    float *x = (float*)v;
    printf("--> %s = [%f, %f, %f]\n", sig->props.name, x[0], x[1], x[2]);
    received++;
}

void invec2_handler(mapper_signal sig, void *v)
{
    float *x = (float*)v;
    printf("--> %s = [%f, %f]\n", sig->props.name, x[0], x[1]);
    received++;
}

void invec3_handler(mapper_signal sig, void *v)
{
    float *x = (float*)v;
    printf("--> %s = %f\n", sig->props.name, x[0]);
    received++;
}

/*! Creation of a local destination. */
int setup_destination()
{
    destination = mdev_new("testrecv", port, 0);
    if (!destination)
        goto error;
    printf("destination created.\n");

    float mn=0, mx=1;

    recvsig_1 = mdev_add_input(destination, "/invec_1", 3, 'f',
                               0, &mn, &mx, invec1_handler, 0);
    recvsig_2 = mdev_add_input(destination, "/invec_2", 2, 'f',
                               0, &mn, &mx, invec2_handler, 0);
    recvsig_3 = mdev_add_input(destination, "/invec_3", 1, 'f',
                               0, &mn, &mx, invec3_handler, 0);

    if (!recvsig_1 || !recvsig_2) {
        printf("Could not create destination signals.\n");
        goto error;
    }

    printf("Input signal /invec registered.\n");

    printf("Number of inputs: %d\n", mdev_num_inputs(destination));

    return 0;

  error:
    return 1;
}

void cleanup_destination()
{
    if (destination) {
        printf("Freeing destination.. ");
        fflush(stdout);
        mdev_free(destination);
        printf("ok\n");
    }
}



void wait_local_devices()
{
    while (!(mdev_ready(source) && mdev_ready(destination))) {
        mdev_poll(source, 0);
        mdev_poll(destination, 0);

        usleep(50 * 1000);
    }
}

void loop()
{
    printf("-------------------- GO ! --------------------\n");
    int i = 0;

    if (automate) {
        char source_name[1024], destination_name[3][1024];

        printf("%s\n", mdev_name(source));
        printf("%s\n", mdev_name(destination));

        lo_address a = lo_address_new_from_url("osc.udp://224.0.1.3:7570");
        lo_address_set_ttl(a, 1);

        lo_send(a, "/link", "ss", mdev_name(source), mdev_name(destination));

        msig_full_name(sendsig, source_name, 1024);
        msig_full_name(recvsig_1, destination_name[0], 1024);
        msig_full_name(recvsig_2, destination_name[1], 1024);
        msig_full_name(recvsig_3, destination_name[2], 1024);

        lo_send(a, "/connect", "ss", source_name, destination_name[0]);

        /* These should not succeed. */
        lo_send(a, "/connect", "ss", source_name, destination_name[1]);
        lo_send(a, "/connect", "ss", source_name, destination_name[2]);

        lo_address_free(a);
    }

    while (i >= 0 && !done) {
        mdev_poll(source, 0);
        float vec[3];
        vec[0] = (i%10) / 10.0;
        vec[1] = vec[0]*2;
        vec[2] = vec[1]*2;
        msig_update(source->outputs[0], vec);
        printf("source value updated to [%f, %f, %f] -->\n",
               vec[0], vec[1], vec[2]);

        printf("Received %i messages.\n\n", mdev_poll(destination, 100));
        i++;
    }
}

void ctrlc(int sig)
{
    done = 1;
}

int main()
{
    int result = 0;

    signal(SIGINT, ctrlc);

    if (setup_destination()) {
        printf("Error initializing destination.\n");
        result = 1;
        goto done;
    }

    if (setup_source()) {
        printf("Done initializing source.\n");
        result = 1;
        goto done;
    }

    wait_local_devices();

    loop();

  done:
    cleanup_destination();
    cleanup_source();
    return result;
}
