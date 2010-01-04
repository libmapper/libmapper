
#include "../src/mapper_internal.h"
#include <mapper/mapper.h>
#include <stdio.h>
#include <math.h>

#include <unistd.h>
#include <arpa/inet.h>

mapper_device sender = 0;
mapper_device receiver = 0;
mapper_router router = 0;
mapper_signal sendsig = 0;
mapper_signal recvsig = 0;

int recvport = 9000;
int sendport = 9001;

int sent = 0;
int received = 0;

int setup_sender()
{
    sender = mdev_new("testsend", sendport);
    if (!sender) goto error;
    printf("Sender created.\n");

    sendsig =
        msig_float(1, "/outsig", 0, INFINITY, INFINITY, 0, 0, 0);

    mdev_register_output(sender, sendsig);

    printf("Output signal /outsig registered.\n");
    printf("Number of outputs: %d\n", mdev_num_outputs(sender));
    return 0;

  error:
    return 1;
}

void cleanup_sender()
{
    if (sender) {
        if (router) {
            printf("Removing router.. ");
            fflush(stdout);
            mdev_remove_router(sender, router);
            printf("ok\n");
        }
        printf("Freeing sender.. ");
        fflush(stdout);
        mdev_free(sender);
        printf("ok\n");
    }
}

void insig_handler(mapper_device mdev, mapper_signal_value_t *v)
{
    printf("handler: Got %f\n", (*v).f);
    received++;
}

int setup_receiver()
{
    receiver = mdev_new("testrecv", recvport);
    if (!receiver) goto error;
    printf("Receiver created.\n");

    recvsig =
        msig_float(1, "/insig", 0, INFINITY, INFINITY, 0, insig_handler, 0);

    mdev_register_input(receiver, recvsig);

    printf("Input signal /insig registered.\n");
    printf("Number of inputs: %d\n", mdev_num_inputs(receiver));
    return 0;

  error:
    return 1;
}

void cleanup_receiver()
{
    if (receiver) {
        printf("Freeing receiver.. ");
        fflush(stdout);
        mdev_free(receiver);
        printf("ok\n");
    }
}

int setup_router()
{
    const char *host = "localhost";
    router = mapper_router_new(host, recvport);
    mdev_add_router(sender, router);
    printf("Router to %s:%d added.\n", host, recvport);

    char signame_in[1024];
    if (!msig_full_name(recvsig, signame_in, 1024)) {
        printf("Could not get receiver signal name.\n");
        return 1;
    }

    char signame_out[1024];
    if (!msig_full_name(sendsig, signame_out, 1024)) {
        printf("Could not get sender signal name.\n");
        return 1;
    }

    printf("Mapping signal %s -> %s\n",
           signame_out, signame_in);
    mapper_router_add_linear_mapping(router, sendsig, signame_in,
                                     (mapper_signal_value_t)10.0f);
    return 0;
}

void wait_ready()
{
    int count = 0;
    while (count++ < 10
           && !(   mdev_ready(sender)
                && mdev_ready(receiver)))
    {
        mdev_poll(sender, 0);
        mdev_poll(receiver, 0);
        usleep(500*1000);
    }
}

void loop()
{
    printf("Polling device..\n");
    int i;
    for (i=0; i<10; i++) {
        mdev_poll(sender, 0);
        printf("Updating signal %s to %f\n", sendsig->name, (i*1.0f));
        msig_update_scalar(sendsig, (mval)(i*1.0f));
        sent ++;
        usleep(250*1000);
        mdev_poll(receiver, 0);
    }
}

int main()
{
    int result=0;

    if (setup_receiver()) {
        printf("Error initializing receiver.\n");
        result = 1;
        goto done;
    }

    if (setup_sender()) {
        printf("Done initializing sender.\n");
        result = 1;
        goto done;
    }

    wait_ready();

    if (setup_router()) {
        printf("Error initializing router.\n");
        result = 1;
        goto done;
    }

    loop();

    if (sent != received) {
        printf("Not all sent messages were received.\n");
        printf("Updated value %d time%s, but received %d of them.\n",
               sent, sent==1?"":"s", received);
        result = 1;
    }

  done:
    cleanup_receiver();
    cleanup_sender();
    printf("Test %s.\n", result?"FAILED":"PASSED");
    return result;
}
