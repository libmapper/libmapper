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
int polltime = 100;

mapper_device src = 0;
mapper_device dst = 0;
mapper_signal sendsig = 0;
mapper_signal recvsig = 0;

int sent = 0;
int received = 0;

float period;

/*! Creation of a local source. */
int setup_src()
{
    src = mapper_device_new("testrate-send", 0);
    if (!src)
        goto error;
    eprintf("source created.\n");

    float mn=0, mx=10;

    sendsig = mapper_device_add_signal(src, MAPPER_DIR_OUT, 1, "outsig", 1,
                                       MAPPER_FLOAT, "Hz", &mn, &mx, NULL);

    eprintf("Output signal 'outsig' registered.\n");

    return 0;

  error:
    return 1;
}

void cleanup_src()
{
    if (src) {
        eprintf("Freeing source.. ");
        fflush(stdout);
        mapper_device_free(src);
        eprintf("ok\n");
    }
}

void handler(mapper_signal sig, mapper_id instance, int len, mapper_type type,
             const void *val, mapper_time t)
{
    ++received;
    if (!val)
        return;

    const char *name;
    mapper_object_get_prop_by_index((mapper_object)sig, MAPPER_PROP_NAME, NULL,
                                    NULL, NULL, (const void**)&name);
    eprintf("Rec'ved (period: %f, jitter: %f, diff:%f)\n", sig->period,
            sig->jitter, period - sig->period);
}

/*! Creation of a local destination. */
int setup_dst()
{
    dst = mapper_device_new("testrate-recv", 0);
    if (!dst)
        goto error;
    eprintf("destination created.\n");

    float mn=0, mx=1;

    recvsig = mapper_device_add_signal(dst, MAPPER_DIR_IN, 1, "insig", 1,
                                       MAPPER_FLOAT, NULL, &mn, &mx, handler);

    // This signal is expected to be updated at 100 Hz
    float rate = 100.f;
    mapper_object_set_prop((mapper_object)recvsig, MAPPER_PROP_RATE, NULL, 1,
                           MAPPER_FLOAT, &rate, 1);

    eprintf("Input signal 'insig' registered.\n");

    return 0;

  error:
    return 1;
}

void cleanup_dst()
{
    if (dst) {
        eprintf("Freeing destination.. ");
        fflush(stdout);
        mapper_device_free(dst);
        eprintf("ok\n");
    }
}

void wait_local_devices()
{
    while (!done && !(mapper_device_ready(src) && mapper_device_ready(dst))) {
        mapper_device_poll(src, 25);
        mapper_device_poll(dst, 25);
    }
}

int setup_maps()
{
    int i = 0;
    mapper_map map = mapper_map_new(1, &sendsig, 1, &recvsig);
    mapper_object_push((mapper_object)map);

    i = 0;
    // wait until mapping has been established
    while (!done && !mapper_map_ready(map)) {
        mapper_device_poll(src, 10);
        mapper_device_poll(dst, 10);
        if (i++ > 100)
            return 1;
    }

    return 0;
}

void loop()
{
    int i = 0, thresh = 0;

    float phasor[10];
    for (i=0; i<10; i++)
        phasor[i] = i;

    i = 0;
    while ((!terminate || i < 50) && !done) {
        mapper_device_poll(src, 0);

        // 10 times a second, we provide 10 samples, making a
        // periodically-sampled signal of 100 Hz.
        if (rand() % 200 > thresh) {
            mapper_signal_set_value(sendsig, 0, 10, MAPPER_FLOAT, phasor, MAPPER_NOW);
            eprintf("Sending (period: %f; jitter: %f)\n", sendsig->period,
                    sendsig->jitter);
            period = sendsig->period;
            ++sent;
        }

        mapper_device_poll(dst, polltime);
        ++i;

        if (!verbose) {
            printf("\r  Sent: %4i, Received: %4i   ", sent, received);
            fflush(stdout);
        }

        ++thresh;
        if (thresh > 100)
            thresh = -100;
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
                                "-f fast (execute quickly), "
                                "-q quiet (suppress output), "
                                "-t terminate automatically, "
                                "-h help\n");
                        return 1;
                        break;
                    case 'f':
                        polltime = 1;
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

    if (setup_dst()) {
        eprintf("Error initializing destination.\n");
        result = 1;
        goto done;
    }

    if (setup_src()) {
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
    cleanup_dst();
    cleanup_src();
    printf("\r..................................................Test %s\x1B[0m.\n",
           result ? "\x1B[31mFAILED" : "\x1B[32mPASSED");
    return result;
}
