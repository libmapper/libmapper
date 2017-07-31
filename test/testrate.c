
#include "../src/mapper_internal.h"
#include <mapper/mapper.h>
#include <stdio.h>
#include <math.h>
#include <lo/lo.h>
#include <unistd.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>

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
    source = mapper_device_new("testrate-send", port, 0);
    if (!source)
        goto error;
    eprintf("source created.\n");

    float mn=0, mx=10;

    sendsig = mapper_device_add_output_signal(source, "outsig", 1, 'f', "Hz",
                                              &mn, &mx);

    // This signal will be updated at 100 Hz
    mapper_signal_set_rate(sendsig, 100);

    // Check by both methods that the property was set
    eprintf("Rate for 'outsig' is set to: %f\n", sendsig->rate);

    const float *a;
    char t;
    int l;
    if (mapper_signal_property(sendsig, "rate", &l, &t, (const void**)&a))
    {
        eprintf("Couldn't find `rate' property.\n");
        mapper_device_free(source);
        mapper_device_free(destination);
        exit(1);
    }

    if (l!=1) {
        eprintf("Rate property was unexpected length %d\n", l);
        exit(1);
    }
    if (t=='f')
        eprintf("Rate for 'outsig' is set to: %f\n", a[0]);
    else {
        eprintf("Rate property was unexpected type `%c'\n", t);
        mapper_device_free(source);
        mapper_device_free(destination);
        exit(1);
    }

    if (sendsig->rate != a[0]) {
        eprintf("Rate properties don't agree.\n");
        mapper_device_free(source);
        mapper_device_free(destination);
        exit(1);
    }

    eprintf("Output signal 'outsig' registered.\n");

    return 0;

  error:
    return 1;
}

void cleanup_source()
{
    if (source) {
        eprintf("Freeing source.. ");
        fflush(stdout);
        mapper_device_free(source);
        eprintf("ok\n");
    }
}

void insig_handler(mapper_signal sig, mapper_id instance, const void *value,
                   int count, mapper_timetag_t *timetag)
{
    if (value) {
        eprintf("--> destination %s got %i message vector\n[", sig->name, count);
        float *v = (float*)value;
        for (int i = 0; i < count; i++) {
            for (int j = 0; j < sig->length; j++) {
                eprintf(" %.1f ", v[i*sig->length+j]);
            }
        }
        eprintf("]\n");
    }
    received++;
}

/*! Creation of a local destination. */
int setup_destination()
{
    destination = mapper_device_new("testrate-recv", port, 0);
    if (!destination)
        goto error;
    eprintf("destination created.\n");

    float mn=0, mx=1;

    recvsig = mapper_device_add_input_signal(destination, "insig", 1, 'f', 0,
                                             &mn, &mx, insig_handler, 0);

    // This signal is expected to be updated at 100 Hz
    mapper_signal_set_rate(recvsig, 100);

    eprintf("Input signal 'insig' registered.\n");

    return 0;

  error:
    return 1;
}

void cleanup_destination()
{
    if (destination) {
        eprintf("Freeing destination.. ");
        fflush(stdout);
        mapper_device_free(destination);
        eprintf("ok\n");
    }
}

void wait_local_devices()
{
    while (!done && !(mapper_device_ready(source)
                      && mapper_device_ready(destination))) {
        mapper_device_poll(source, 25);
        mapper_device_poll(destination, 25);
    }
}

int setup_maps()
{
    int i = 0;
    mapper_map map = mapper_map_new(1, &sendsig, 1, &recvsig);
    mapper_map_push(map);

    i = 0;
    // wait until mapping has been established
    while (!done && !mapper_map_ready(map)) {
        mapper_device_poll(source, 10);
        mapper_device_poll(destination, 10);
        if (i++ > 100)
            return 1;
    }

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
        mapper_device_poll(source, 0);

        // 10 times a second, we provide 10 samples, making a
        // periodically-sampled signal of 100 Hz.
        eprintf("Sending [%g..%g]...\n", phasor[0], phasor[9]);
        sent++;
        mapper_signal_update(sendsig, phasor, 10, MAPPER_NOW);
        int r = mapper_device_poll(destination, 100);
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

    if (autoconnect && setup_maps()) {
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
