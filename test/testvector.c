
#include "../src/mapper_internal.h"
#include <mapper/mapper.h>
#include <stdio.h>
#include <math.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>

#define eprintf(format, ...) do {               \
    if (verbose)                                \
        fprintf(stdout, format, ##__VA_ARGS__); \
} while(0)

int verbose = 1;
int terminate = 0;
int autoconnect = 1;
int done = 0;
int period = 100;

mapper_device src = 0;
mapper_device dst = 0;
mapper_signal sendsig = 0;
mapper_signal recvsig = 0;

int sent = 0;
int received = 0;

int setup_src()
{
    src = mapper_device_new("testvector-send", 0);
    if (!src)
        goto error;
    eprintf("source created.\n");

    float mn[]={0,0,0}, mx[]={1,2,3};
    sendsig = mapper_device_add_signal(src, MAPPER_DIR_OUT, 1, "outsig", 3,
                                       MAPPER_FLOAT, NULL, &mn, &mx, NULL);

    eprintf("Output signal 'outsig' registered.\n");
    eprintf("Number of outputs: %d\n",
            mapper_device_get_num_signals(src, MAPPER_DIR_OUT));
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

void handler(mapper_signal sig, mapper_id instance, int length,
                   mapper_type type, const void *value, mapper_time t)
{
    if (value) {
        float *f = (float*)value;
        eprintf("handler: Got [%f, %f, %f]\n", f[0], f[1], f[2]);
    }
    received++;
}

int setup_dst()
{
    dst = mapper_device_new("testvector-recv", 0);
    if (!dst)
        goto error;
    eprintf("destination created.\n");

    float mn[]={0,0,0}, mx[]={1,1,1};
    recvsig = mapper_device_add_signal(dst, MAPPER_DIR_IN, 1, "insig", 3,
                                       MAPPER_FLOAT, NULL, &mn, &mx, handler);

    eprintf("Input signal 'insig' registered.\n");
    eprintf("Number of inputs: %d\n",
            mapper_device_get_num_signals(dst, MAPPER_DIR_IN));
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

int setup_maps()
{
    int i = 0;

    mapper_map map = mapper_map_new(1, &sendsig, 1, &recvsig);
    mapper_object_push((mapper_object)map);

    // wait until mapping has been established
    i = 0;
    while (!done && !mapper_map_ready(map)) {
        mapper_device_poll(src, 10);
        mapper_device_poll(dst, 10);
        if (i++ > 100)
            return 1;
    }

    return 0;
}

void wait_ready()
{
    while (!done && !(mapper_device_ready(src) && mapper_device_ready(dst))) {
        mapper_device_poll(src, 25);
        mapper_device_poll(dst, 25);
    }
}

void loop()
{
    eprintf("Polling device..\n");
    int i = 0;
    while ((!terminate || i < 50) && !done) {
        mapper_device_poll(src, 0);
        float v[3];
        v[0] = (float)i;
        v[1] = (float)i+1;
        v[2] = (float)i+2;
        eprintf("Updating signal %s to [%f, %f, %f]\n",
               sendsig->name, v[0], v[1], v[2]);
        mapper_signal_set_value(sendsig, 0, 3, MAPPER_FLOAT, v, MAPPER_NOW);
        sent++;
        mapper_device_poll(dst, period);
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
                        printf("testvector.c: possible arguments "
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

    if (setup_dst()) {
        eprintf("Error initializing destination.\n");
        result = 1;
        goto done;
    }

    if (setup_src()) {
        eprintf("Done initializing source.\n");
        result = 1;
        goto done;
    }

    wait_ready();

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
    printf("...................Test %s\x1B[0m.\n",
           result ? "\x1B[31mFAILED" : "\x1B[32mPASSED");
    return result;
}
