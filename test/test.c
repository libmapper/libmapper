
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
mapper_signal recvsig = 0;

int recvport = 9000;
int sendport = 9000;

int sent = 0;
int received = 0;
int done = 0;

/*! Creation of a local source. */
int setup_source()
{
    source = mdev_new("testsend", sendport);
    if (!source)
        goto error;
    printf("source created.\n");

    sendsig = msig_float(1, "/outsig", 0, 0, 1, 0, 0, 0);

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

void insig_handler(mapper_signal sig, mapper_signal_value_t *v)
{
    printf("--> destination got %s %f\n\n", sig->props.name, (*v).f);
    received++;
}

/*! Creation of a local destination. */
int setup_destination()
{
    destination = mdev_new("testrecv", recvport);
    if (!destination)
        goto error;
    printf("destination created.\n");

    recvsig = msig_float(1, "/insig", 0, 0, 1, 0, insig_handler, 0);

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



void wait_local_devices()
{
    while (!(mdev_ready(source) && mdev_ready(destination))) {
        mdev_poll(source, 0);
        mdev_poll(destination, 0);

        usleep(500 * 1000);
    }

    mapper_db_dump();
}

void loop()
{
    printf("-------------------- GO ! --------------------\n");
    int i = 0;

    if (automate) {
        char source_name[1024], destination_name[1024];

        printf("%s\n", mdev_name(source));
        printf("%s\n", mdev_name(destination));

        lo_address a = lo_address_new_from_url("osc.udp://224.0.1.3:7570");
        lo_address_set_ttl(a, 1);

        lo_send(a, "/link", "ss", mdev_name(source), mdev_name(destination));

        msig_full_name(sendsig, source_name, 1024);
        msig_full_name(recvsig, destination_name, 1024);

        lo_send(a, "/connect", "ss", source_name, destination_name);

        lo_address_free(a);
    }

    while (i >= 0 && !done) {
        mdev_poll(source, 0);
        msig_update_scalar(source->outputs[0],
                           (mval) ((i % 10) * 1.0f));
        printf("source value updated to %d -->\n", i % 10);

        usleep(500 * 1000);
        mdev_poll(destination, 0);

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
