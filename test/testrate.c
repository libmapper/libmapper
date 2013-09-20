
#include "../src/mapper_internal.h"
#include <mapper/mapper.h>
#include <stdio.h>
#include <math.h>
#include <lo/lo.h>

#include <unistd.h>
#include <signal.h>
#include <stdlib.h>

#ifdef WIN32
#define usleep(x) Sleep(x/1000)
#endif

int automate = 1;

mapper_device source = 0;
mapper_device destination = 0;
mapper_signal sendsig = 0;
mapper_signal recvsig = 0;

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

    float mn=0, mx=10;

    sendsig = mdev_add_output(source, "/outsig", 1, 'f', "Hz", &mn, &mx);

    // This signal will be updated at 100 Hz
    msig_set_rate(sendsig, 100);

    // Check by both methods that the property was set
    mapper_db_signal props = msig_properties(sendsig);
    printf("Rate for /outsig is set to: %f\n", props->rate);

    const lo_arg *a;
    lo_type t;
    if (mapper_db_signal_property_lookup(props, "rate", &t, &a))
    {
        printf("Couldn't find `rate' property.\n");
        mdev_free(source);
        mdev_free(destination);
        exit(1);
    }

    if (t=='f')
        printf("Rate for /outsig is set to: %f\n", a->f);
    else {
        printf("Rate property was unexpected type `%c'\n", t);
        mdev_free(source);
        mdev_free(destination);
        exit(1);
    }

    if (props->rate != a->f) {
        printf("Rate properties don't agree.\n");
        mdev_free(source);
        mdev_free(destination);
        exit(1);
    }

    printf("Output signal /outsig registered.\n");

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

void insig_handler(mapper_signal sig, mapper_db_signal props,
                   int instance_id, void *value, int count,
                   mapper_timetag_t *timetag)
{
    if (value) {
        printf("--> destination %s got %i message vector\n[",
               props->name, count);
        float *v = value;
        for (int i = 0; i < count; i++) {
            for (int j = 0; j < props->length; j++) {
                printf(" %.1f ", v[i*props->length+j]);
            }
        }
        printf("]\n");
    }
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

    recvsig = mdev_add_input(destination, "/insig", 1, 'f',
                               0, &mn, &mx, insig_handler, 0);

    // This signal is expected to be updated at 100 Hz
    msig_set_rate(recvsig, 100);

    printf("Input signal /insig registered.\n");

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
    int i = 0;

    if (automate) {
        mapper_monitor mon = mapper_monitor_new(source->admin, 0);

        char src_name[1024], dest_name[1024];
        mapper_monitor_link(mon, mdev_name(source),
                            mdev_name(destination), 0, 0);

        msig_full_name(sendsig, src_name, 1024);
        msig_full_name(recvsig, dest_name, 1024);
        mapper_monitor_connect(mon, src_name, dest_name, 0, 0);

        // wait until connection has been established
        while (!source->routers || !source->routers->n_connections) {
            mdev_poll(source, 1);
            mdev_poll(destination, 1);
        }

        mapper_monitor_free(mon);
    }

    float phasor[10];
    for (i=0; i<10; i++)
        phasor[i] = i;

    while (i >= 0 && !done) {
        mdev_poll(source, 0);

        // 10 times a second, we provide 10 samples, making a
        // periodically-sampled signal of 100 Hz.
        printf("Sending [%g..%g]...\n", phasor[0], phasor[9]);

        msig_update(sendsig, phasor, 10, MAPPER_NOW);
        int r = mdev_poll(destination, 100);
        printf("Destination got %d message%s.\n", r, r==1?"":"s");
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
