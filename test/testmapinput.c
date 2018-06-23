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
int period = 50;

void insig_handler(mapper_signal sig, mapper_id instance, int length,
                   mapper_type type, const void *value, mapper_time t)
{
    if (value) {
        const char *name;
        mapper_object_get_prop_by_index((mapper_object)sig, MAPPER_PROP_NAME,
                                        NULL, NULL, NULL, (const void**)&name);
        eprintf("--> received %s", name);

        if (type == MAPPER_FLOAT) {
            float *v = (float*)value;
            for (int i = 0; i < length; i++) {
                eprintf(" %f", v[i]);
            }
        }
        else if (type == MAPPER_DOUBLE) {
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
    devices[0] = mapper_device_new("testmapinput1", 0);
    devices[1] = mapper_device_new("testmapinput2", 0);
    if (!devices[0] || !devices[1])
        goto error;
    eprintf("devices created.\n");

    float mnf1[]={0,0,0}, mxf1[]={1,1,1};
    float mnf2[]={3.2,2,0}, mxf2[]={-2,13,100};
    double mnd=0, mxd=10;

    inputs[0] = mapper_device_add_signal(devices[0], MAPPER_DIR_IN, 1,
                                         "insig_1", 1, MAPPER_FLOAT, NULL,
                                         mnf1, mxf1, insig_handler);
    inputs[1] = mapper_device_add_signal(devices[0], MAPPER_DIR_IN, 1,
                                         "insig_2", 1, MAPPER_DOUBLE, NULL,
                                         &mnd, &mxd, insig_handler);
    inputs[2] = mapper_device_add_signal(devices[1], MAPPER_DIR_IN, 1,
                                         "insig_3", 3, MAPPER_FLOAT, NULL,
                                         mnf1, mxf1, insig_handler);
    inputs[3] = mapper_device_add_signal(devices[1], MAPPER_DIR_IN, 1,
                                         "insig_4", 1, MAPPER_FLOAT, NULL,
                                         mnf2, mxf2, insig_handler);

    /* In this test inputs[2] will never get its full vector value from
     * external updates – for the handler to be called we will need to
     * initialize its value. */
    mapper_signal_set_value(inputs[2], 0, 3, MAPPER_FLOAT, mnf2, MAPPER_NOW);

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
        mapper_object_push((mapper_object)maps[0]);

        // map input to an input on another device
        maps[1] = mapper_map_new(1, &inputs[1], 1, &inputs[2]);
        mapper_object_push((mapper_object)maps[1]);

        // wait until mapping has been established
        int ready = 0;
        while (!done && !ready) {
            mapper_device_poll(devices[0], 100);
            mapper_device_poll(devices[1], 100);
            ready = mapper_map_ready(maps[0]) & mapper_map_ready(maps[1]);
        }
    }

    i = 0;
    float val;
    while ((!terminate || i < 50) && !done) {
        val = (i % 10) * 1.0f;
        mapper_signal_set_value(inputs[0], 0, 1, MAPPER_FLOAT, &val, MAPPER_NOW);
        eprintf("insig_1 value updated to %f -->\n", val);
        sent += 1;

        recvd = mapper_device_poll(devices[0], period);
        recvd += mapper_device_poll(devices[1], period);
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
                               "-f fast (execute quickly), "
                               "-q quiet (suppress output), "
                               "-t terminate automatically, "
                               "-h help\n");
                        return 1;
                        break;
                    case 'f':
                        period = 1;
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
    printf("...................Test %s\x1B[0m.\n",
           result ? "\x1B[31mFAILED" : "\x1B[32mPASSED");
    return result;
}
