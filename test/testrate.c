
#include "../src/mapper_internal.h"
#include <mapper/mapper.h>
#include <stdio.h>
#include <math.h>
#include <lo/lo.h>
#include <unistd.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>

#ifdef WIN32
#define usleep(x) Sleep(x/1000)
#endif

#define eprintf(format, ...) do {               \
    if (verbose)                                \
        fprintf(stdout, format, ##__VA_ARGS__); \
} while(0)

int verbose = 1;
int terminate = 0;
int autoconnect = 1;
int done = 0;

mapper_device source = 0;
mapper_device destination = 0;
mapper_signal sendsig = 0;
mapper_signal recvsig = 0;

int port = 9000;

int sent = 0;
int received = 0;

/*! Creation of a local source. */
int setup_source()
{
    source = mdev_new("testsend", port, 0);
    if (!source)
        goto error;
    eprintf("source created.\n");

    float mn=0, mx=10;

    sendsig = mdev_add_output(source, "/outsig", 1, 'f', "Hz", &mn, &mx);

    // This signal will be updated at 100 Hz
    msig_set_rate(sendsig, 100);

    // Check by both methods that the property was set
    mapper_db_signal props = msig_properties(sendsig);
    eprintf("Rate for /outsig is set to: %f\n", props->rate);

    const float *a;
    char t;
    int l;
    if (mapper_db_signal_property_lookup(props, "rate", &t,
                                         (const void**)&a, &l))
    {
        eprintf("Couldn't find `rate' property.\n");
        mdev_free(source);
        mdev_free(destination);
        exit(1);
    }

    if (l!=1) {
        eprintf("Rate property was unexpected length %d\n", l);
        exit(1);
    }
    if (t=='f')
        eprintf("Rate for /outsig is set to: %f\n", a[0]);
    else {
        eprintf("Rate property was unexpected type `%c'\n", t);
        mdev_free(source);
        mdev_free(destination);
        exit(1);
    }

    if (props->rate != a[0]) {
        eprintf("Rate properties don't agree.\n");
        mdev_free(source);
        mdev_free(destination);
        exit(1);
    }

    eprintf("Output signal /outsig registered.\n");

    return 0;

  error:
    return 1;
}

void cleanup_source()
{
    if (source) {
        if (source->routers) {
            eprintf("Removing router.. ");
            fflush(stdout);
            mdev_remove_router(source, source->routers);
            eprintf("ok\n");
        }
        eprintf("Freeing source.. ");
        fflush(stdout);
        mdev_free(source);
        eprintf("ok\n");
    }
}

void insig_handler(mapper_signal sig, mapper_db_signal props,
                   int instance_id, void *value, int count,
                   mapper_timetag_t *timetag)
{
    if (value) {
        eprintf("--> destination %s got %i message vector\n[",
               props->name, count);
        float *v = value;
        for (int i = 0; i < count; i++) {
            for (int j = 0; j < props->length; j++) {
                eprintf(" %.1f ", v[i*props->length+j]);
            }
        }
        eprintf("]\n");
    }
    received++;
}

/*! Creation of a local destination. */
int setup_destination()
{
    destination = mdev_new("testrecv", port, 0);
    if (!destination)
        goto error;
    eprintf("destination created.\n");

    float mn=0, mx=1;

    recvsig = mdev_add_input(destination, "/insig", 1, 'f',
                               0, &mn, &mx, insig_handler, 0);

    // This signal is expected to be updated at 100 Hz
    msig_set_rate(recvsig, 100);

    eprintf("Input signal /insig registered.\n");

    return 0;

  error:
    return 1;
}

void cleanup_destination()
{
    if (destination) {
        eprintf("Freeing destination.. ");
        fflush(stdout);
        mdev_free(destination);
        eprintf("ok\n");
    }
}

void wait_local_devices()
{
    while (!done && !(mdev_ready(source) && mdev_ready(destination))) {
        mdev_poll(source, 0);
        mdev_poll(destination, 0);

        usleep(50 * 1000);
    }
}

int setup_connections()
{
    int i = 0;
    mapper_monitor mon = mapper_monitor_new(source->admin, 0);

    char src_name[1024], dest_name[1024];
    mapper_monitor_link(mon, mdev_name(source),
                        mdev_name(destination), 0, 0);

    while (!done && !source->routers) {
        mdev_poll(source, 10);
        mdev_poll(destination, 10);
        if (i++ > 100)
            return 1;
    }

    msig_full_name(sendsig, src_name, 1024);
    msig_full_name(recvsig, dest_name, 1024);
    mapper_monitor_connect(mon, src_name, dest_name, 0, 0);

    i = 0;
    // wait until connection has been established
    while (!done && !source->routers->num_connections) {
        mdev_poll(source, 10);
        mdev_poll(destination, 10);
        if (i++ > 100)
            return 1;
    }

    mapper_monitor_free(mon);
    return 0;
}

void loop()
{
    int i = 0;

    float phasor[10];
    for (i=0; i<10; i++)
        phasor[i] = i;

    i = 0;
    while ((!terminate || i < 50) && !done) {
        mdev_poll(source, 0);

        // 10 times a second, we provide 10 samples, making a
        // periodically-sampled signal of 100 Hz.
        eprintf("Sending [%g..%g]...\n", phasor[0], phasor[9]);
        sent++;
        msig_update(sendsig, phasor, 10, MAPPER_NOW);
        int r = mdev_poll(destination, 100);
        eprintf("Destination got %d message%s.\n", r, r==1?"":"s");
        i++;

        if (!verbose) {
            printf("\r  Sent: %4i, Received: %4i   ", sent, received);
            fflush(stdout);
        }
    }
}

void ctrlc(int sig)
{
    done = 1;
}

int main(int argc, char **argv)
{
    int i, j, result = 0;

    // process flags for -v verbose, -t terminate, -h help
    for (i = 1; i < argc; i++) {
        if (argv[i] && argv[i][0] == '-') {
            int len = strlen(argv[i]);
            for (j = 1; j < len; j++) {
                switch (argv[i][j]) {
                    case 'h':
                        eprintf("testrate.c: possible arguments "
                                "-q quiet (suppress output), "
                                "-t terminate automatically, "
                                "-h help\n");
                        return 1;
                        break;
                    case 'q':
                        verbose = 0;
                        break;
                    case 't':
                        terminate = 1;
                        break;
                    default:
                        break;
                }
            }
        }
    }

    signal(SIGINT, ctrlc);

    if (setup_destination()) {
        eprintf("Error initializing destination.\n");
        result = 1;
        goto done;
    }

    if (setup_source()) {
        eprintf("Error initializing source.\n");
        result = 1;
        goto done;
    }

    wait_local_devices();

    if (autoconnect && setup_connections()) {
        eprintf("Error connecting signals.\n");
        result = 1;
        goto done;
    }

    loop();

    if (sent != received) {
        eprintf("Not all sent messages were received.\n");
        eprintf("Updated value %d time%s, but received %d of them.\n",
                sent, sent == 1 ? "" : "s", received);
        result = 1;
    }

  done:
    cleanup_destination();
    cleanup_source();
    printf("Test %s.\n", result ? "FAILED" : "PASSED");
    return result;
}
