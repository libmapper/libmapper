
#include "../src/mapper_internal.h"

#include <mapper/mapper.h>
#include <stdio.h>
#include <math.h>

#include <unistd.h>
#include <arpa/inet.h>

mapper_device source = 0;
mapper_device destination = 0;
mapper_router router = 0;
mapper_signal sendsig = 0;
mapper_signal recvsig = 0;

int recvport = 9000;
int sendport = 9001;

int sent = 0;
int received = 0;

int setup_source()
{
    source = mdev_new("testsend", sendport, 0);
    if (!source)
        goto error;
    printf("source created.\n");

    float mn=0, mx=1;
    sendsig = msig_float(1, "/outsig", 0, &mn, &mx, 0, 0, 0);

    mdev_register_output(source, sendsig);

    printf("Output signal /outsig registered.\n");
    printf("Number of outputs: %d\n", mdev_num_outputs(source));
    return 0;

  error:
    return 1;
}

void cleanup_source()
{
    if (source) {
        if (router) {
            printf("Removing router.. ");
            fflush(stdout);
            mdev_remove_router(source, router);
            printf("ok\n");
        }
        printf("Freeing source.. ");
        fflush(stdout);
        mdev_free(source);
        printf("ok\n");
    }
}

void insig_handler(mapper_signal sig, mapper_signal_value_t *v)
{
    printf("handler: Got %f\n", (*v).f);
    received++;
}

int setup_destination()
{
    destination = mdev_new("testrecv", recvport, 0);
    if (!destination)
        goto error;
    printf("destination created.\n");

    float mn=0, mx=1;
    recvsig = msig_float(1, "/insig", 0, &mn, &mx, 0, insig_handler, 0);

    mdev_register_input(destination, recvsig);

    printf("Input signal /insig registered.\n");
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

int setup_router()
{
    const char *host = "localhost";
    router = mapper_router_new(source, host, destination->admin->port.value, 
                               mdev_name(destination));
    mdev_add_router(source, router);
    printf("Router to %s:%d added.\n", host, recvport);

    char signame_in[1024];
    if (!msig_full_name(recvsig, signame_in, 1024)) {
        printf("Could not get destination signal name.\n");
        return 1;
    }

    char signame_out[1024];
    if (!msig_full_name(sendsig, signame_out, 1024)) {
        printf("Could not get source signal name.\n");
        return 1;
    }

    printf("Mapping signal %s -> %s\n", signame_out, signame_in);
    mapper_mapping m = mapper_router_add_mapping(router, sendsig,
                                                 recvsig->props.name);
    const char *expr = "y=x*10";
    mapper_mapping_set_expression(m, sendsig, expr);

    return 0;
}

void wait_ready()
{
    while (!(mdev_ready(source) && mdev_ready(destination))) {
        mdev_poll(source, 0);
        mdev_poll(destination, 0);
        usleep(500 * 1000);
    }
}

void loop()
{
    printf("Polling device..\n");
    int i;
    for (i = 0; i < 10; i++) {
        mdev_poll(source, 0);
        printf("Updating signal %s to %f\n",
               sendsig->props.name, (i * 1.0f));
        msig_update_scalar(sendsig, (mval) (i * 1.0f));
        sent++;
        usleep(250 * 1000);
        mdev_poll(destination, 0);
    }
}

int main()
{
    int result = 0;

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
               sent, sent == 1 ? "" : "s", received);
        result = 1;
    }

  done:
    cleanup_destination();
    cleanup_source();
    printf("Test %s.\n", result ? "FAILED" : "PASSED");
    return result;
}
