
#include "../src/mapper_internal.h"
#include <mapper/mapper.h>
#include <stdio.h>
#include <math.h>
#include <lo/lo.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <string.h>

#ifdef WIN32
#define usleep(x) Sleep(x/1000)
#endif

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

void insig_handler(mapper_signal sig, mapper_db_signal props,
                   int instance_id, void *value, int count,
                   mapper_timetag_t *timetag)
{
    if (value) {
        eprintf("--> received %s", props->name);
        if (props->type == 'f') {
            float *v = value;
            for (int i = 0; i < props->length; i++) {
                eprintf(" %f", v[i]);
            }
        }
        else if (props->type == 'd') {
            double *v = value;
            for (int i = 0; i < props->length; i++) {
                eprintf(" %f", v[i]);
            }
        }
        eprintf("\n");
    }
    received++;
}

int setup_devices()
{
    devices[0] = mdev_new("test", 0, 0);
    devices[1] = mdev_new("test", 0, 0);
    if (!devices[0] || !devices[1])
        goto error;
    eprintf("devices created.\n");

    float mnf1[]={0,0,0}, mxf1[]={1,1,1};
    float mnf2[]={3.2,2,0}, mxf2[]={-2,13,100};
    double mnd=0, mxd=10;

    inputs[0] = mdev_add_input(devices[0], "/insig_1", 1, 'f',
                               0, mnf1, mxf1, insig_handler, 0);
    inputs[1] = mdev_add_input(devices[0], "/insig_2", 1, 'd',
                               0, &mnd, &mxd, insig_handler, 0);
    inputs[2] = mdev_add_input(devices[1], "/insig_3", 3, 'f',
                               0, mnf1, mxf1, insig_handler, 0);
    inputs[3] = mdev_add_input(devices[1], "/insig_4", 1, 'f',
                               0, mnf2, mxf2, insig_handler, 0);

    /* In this test inputs[2] will never get its full vector value from
     * external updates – for the handler to be called we will need to
     * initialize its value. */
    msig_update(inputs[2], mnf2, 1, MAPPER_NOW);

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
            mdev_free(devices[i]);
            eprintf("ok\n");
        }
    }
}

void wait_local_devices()
{
    while (!done && !(mdev_ready(devices[0]) && mdev_ready(devices[1]))) {
        mdev_poll(devices[0], 0);
        mdev_poll(devices[1], 0);

        usleep(50 * 1000);
    }
}

void loop()
{
    eprintf("-------------------- GO ! --------------------\n");
    int i = 0, recvd;

    if (autoconnect) {
        mapper_monitor mon = mmon_new(devices[0]->admin, 0);
        mapper_db_signal src = msig_properties(inputs[0]);

        // map input to another input on same device
        mmon_update_map(mon, mapper_db_map_new(1, &src, msig_properties(inputs[1])));

        // map input to an input on another device
        src = msig_properties(inputs[1]);
        mmon_update_map(mon, mapper_db_map_new(1, &src, msig_properties(inputs[2])));

        // wait until mapping has been established
        while (!done && mdev_num_outgoing_maps(devices[0]) < 2) {
            mdev_poll(devices[0], 10);
            mdev_poll(devices[1], 10);
        }

        mmon_free(mon);
    }

    i = 0;
    while ((!terminate || i < 50) && !done) {
        msig_update_float(inputs[0], ((i % 10) * 1.0f));
        eprintf("/insig_1 value updated to %d -->\n", i % 10);
        sent += 1;

        recvd = mdev_poll(devices[0], 50);
        recvd += mdev_poll(devices[1], 50);
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
