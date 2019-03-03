
#include "../src/mapper_internal.h"
#include <mapper/mapper.h>
#include <stdio.h>
#include <math.h>
#include <lo/lo.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <string.h>

#define eprintf(format, ...) do {               \
    if (verbose)                                \
        fprintf(stdout, format, ##__VA_ARGS__); \
} while(0)

mapper_device devices[2];
mapper_signal inputs[4];

int sent = 0;
int received = 0;
int done = 0;

int verbose = 1;
int terminate = 0;
int autoconnect = 1;

void insig_handler(mapper_signal sig, mapper_id instance, const void *value,
                   int count, mapper_timetag_t *timetag)
{
    if (value) {
        eprintf("--> received %s", mapper_signal_name(sig));
        char type = mapper_signal_type(sig);
        int length = mapper_signal_length(sig);
        if (type == 'f') {
            float *v = (float*)value;
            for (int i = 0; i < length; i++) {
                eprintf(" %f", v[i]);
            }
        }
        else if (type == 'd') {
            double *v = (double*)value;
            for (int i = 0; i < length; i++) {
                eprintf(" %f", v[i]);
            }
        }
        eprintf("\n");
    }
    received++;
}

int setup_devices()
{
    devices[0] = mapper_device_new("testmapinput", 0, 0);
    devices[1] = mapper_device_new("testmapinput", 0, 0);
    if (!devices[0] || !devices[1])
        goto error;
    eprintf("devices created.\n");

    float mnf1[]={0,0,0}, mxf1[]={1,1,1};
    float mnf2[]={3.2,2,0}, mxf2[]={-2,13,100};
    double mnd=0, mxd=10;

    inputs[0] = mapper_device_add_input_signal(devices[0], "insig_1", 1, 'f', 0,
                                               mnf1, mxf1, insig_handler, 0);
    inputs[1] = mapper_device_add_input_signal(devices[0], "insig_2", 1, 'd', 0,
                                               &mnd, &mxd, insig_handler, 0);
    inputs[2] = mapper_device_add_input_signal(devices[1], "insig_3", 3, 'f', 0,
                                               mnf1, mxf1, insig_handler, 0);
    inputs[3] = mapper_device_add_input_signal(devices[1], "insig_4", 1, 'f', 0,
                                               mnf2, mxf2, insig_handler, 0);

    /* In this test inputs[2] will never get its full vector value from
     * external updates – for the handler to be called we will need to
     * initialize its value. */
    mapper_signal_update(inputs[2], mnf2, 1, MAPPER_NOW);

    return 0;

  error:
    return 1;
}

void cleanup_devices()
{
    for (int i = 0; i < 2; i++) {
        if (devices[i]) {
            eprintf("Freeing device.. ");
            fflush(stdout);
            mapper_device_free(devices[i]);
            eprintf("ok\n");
        }
    }
}

void wait_local_devices()
{
    while (!done && !(mapper_device_ready(devices[0])
                      && mapper_device_ready(devices[1]))) {
        mapper_device_poll(devices[0], 25);
        mapper_device_poll(devices[1], 25);
    }
}

void loop()
{
    eprintf("-------------------- GO ! --------------------\n");
    int i = 0, recvd;

    if (autoconnect) {
        mapper_map maps[2];
        // map input to another input on same device
        maps[0] = mapper_map_new(1, &inputs[0], 1, &inputs[1]);
        mapper_map_push(maps[0]);

        // map input to an input on another device
        maps[1] = mapper_map_new(1, &inputs[1], 1, &inputs[2]);
        mapper_map_push(maps[1]);

        // wait until mapping has been established
        int ready = 0;
        while (!done && !ready) {
            mapper_device_poll(devices[0], 100);
            mapper_device_poll(devices[1], 100);
            ready = mapper_map_ready(maps[0]) & mapper_map_ready(maps[1]);
        }
    }

    i = 0;
    while ((!terminate || i < 50) && !done) {
        mapper_signal_update_float(inputs[0], ((i % 10) * 1.0f));
        eprintf("insig_1 value updated to %d -->\n", i % 10);
        sent += 1;

        recvd = mapper_device_poll(devices[0], 50);
        recvd += mapper_device_poll(devices[1], 50);
        eprintf("Received %i messages.\n\n", recvd);
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

int main(int argc, char ** argv)
{
    int i, j, result = 0;

    // process flags for -v verbose, -t terminate, -h help
    for (i = 1; i < argc; i++) {
        if (argv[i] && argv[i][0] == '-') {
            int len = strlen(argv[i]);
            for (j = 1; j < len; j++) {
                switch (argv[i][j]) {
                    case 'h':
                        printf("testmapinput.c: possible arguments "
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

    if (setup_devices()) {
        eprintf("Error initializing devices.\n");
        result = 1;
        goto done;
    }

    wait_local_devices();

    loop();

    if (autoconnect && received != sent * 2) {
        eprintf("sent: %d, recvd: %d\n", sent, received);
        result = 1;
    }

  done:
    cleanup_devices();

    printf("Test %s.\n", result ? "FAILED" : "PASSED");
    return result;
}
