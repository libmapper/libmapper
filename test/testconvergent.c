
#include "../src/mapper_internal.h"
#include <mapper/mapper.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>

#define eprintf(format, ...) do {               \
    if (verbose)                                \
        fprintf(stdout, format, ##__VA_ARGS__); \
} while(0)

int verbose = 1;
int terminate = 0;
int autoconnect = 1;
int done = 0;
int num_sources = 3;

mapper_device *sources = 0;
mapper_device destination = 0;
mapper_signal *sendsigs = 0;
mapper_signal recvsig = 0;

int sent = 0;
int received = 0;

int setup_sources()
{
    int i, mni=0, mxi=1;
    sources = (mapper_device*)calloc(1, num_sources * sizeof(mapper_device));
    sendsigs = (mapper_signal*)calloc(1, num_sources * sizeof(mapper_signal));

    for (i = 0; i < num_sources; i++) {
        sources[i] = mapper_device_new("testconvergent-send", 0, 0);
        if (!sources[i])
            goto error;
        sendsigs[i] = mapper_device_add_output_signal(sources[i], "sendsig", 1,
                                                      'i', 0, &mni, &mxi);
        if (!sendsigs[i])
            goto error;
        eprintf("source %d created.\n", i);
    }
    return 0;

  error:
    for (i = 0; i < num_sources; i++) {
        if (sources[i])
            mapper_device_free(sources[i]);
    }
    return 1;
}

void cleanup_source()
{
    for (int i = 0; i < num_sources; i++) {
        if (sources[i]) {
            eprintf("Freeing source %d... ", i);
            fflush(stdout);
            mapper_device_free(sources[i]);
            eprintf("ok\n");
        }
    }
    free(sources);
    free(sendsigs);
}

void insig_handler(mapper_signal sig, mapper_id instance, const void *value,
                   int count, mapper_timetag_t *timetag)
{
    if (value) {
        eprintf("handler: Got %f\n", (*(float*)value));
    }
    else {
        eprintf("handler: Got NULL\n");
    }
    received++;
}

int setup_destination()
{
    destination = mapper_device_new("testconvergent-recv", 0, 0);
    if (!destination)
        goto error;
    eprintf("destination created.\n");

    float mn=0, mx=1;
    recvsig = mapper_device_add_input_signal(destination, "recvsig", 1, 'f', 0,
                                             &mn, &mx, insig_handler, 0);

    eprintf("Input signal 'insig' registered.\n");
    eprintf("Number of inputs: %d\n",
            mapper_device_num_signals(destination, MAPPER_DIR_INCOMING));
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

int setup_maps()
{
    mapper_map map = mapper_map_new(num_sources, sendsigs, 1, &recvsig);
    if (!map) {
        eprintf("Failed to create map\n");
        return 1;
    }
    mapper_map_set_mode(map, MAPPER_MODE_EXPRESSION);

    // build expression string
    int i, offset = 2, len = num_sources * 3 + 3;
    char expr[len];
    snprintf(expr, 3, "y=");
    for (i = 0; i < num_sources; i++) {
        mapper_slot slot = mapper_map_slot_by_signal(map, sendsigs[i]);
        snprintf(expr + offset, len - offset, "-x%d", mapper_slot_index(slot));
        if (i > 0)
            mapper_slot_set_causes_update(slot, 0);
        offset += 3;
    }
    mapper_map_set_expression(map, expr);
    mapper_map_push(map);

    // wait until mappings have been established
    while (!done && !mapper_map_ready(map)) {
        for (i = 0; i < num_sources; i++)
            mapper_device_poll(sources[i], 10);
        mapper_device_poll(destination, 10);
    }

    return 0;
}

void wait_ready(int *cancel)
{
    int i, keep_waiting = 1;
    while (keep_waiting && !*cancel) {
        keep_waiting = 0;

        for (i = 0; i < num_sources; i++) {
            mapper_device_poll(sources[i], 50);
            if (!mapper_device_ready(sources[i])) {
                keep_waiting = 1;
            }
        }
        mapper_device_poll(destination, 50);
        if (!mapper_device_ready(destination))
            keep_waiting = 1;
    }
}

void loop()
{
    eprintf("Polling device..\n");
    int i = 0, j;
    while ((!terminate || i < 50) && !done) {
        for (j = 0; j < num_sources; j++) {
            mapper_device_poll(sources[j], 0);
            eprintf("Updating signal %s/%s = %i\n", sources[j]->name,
                    sendsigs[j]->name, i);
            mapper_signal_update_int(sendsigs[j], i);
        }
        sent++;
        mapper_device_poll(destination, 100);
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
                        printf("testcombiner.c: possible arguments "
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
                    case '-':
                        if (strcmp(argv[i], "--sources")==0 && argc>i+1) {
                            i++;
                            num_sources = atoi(argv[i]);
                            if (num_sources <= 0)
                                num_sources = 1;
                            j = 1;
                        }
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

    if (setup_sources()) {
        eprintf("Done initializing %d sources.\n", num_sources);
        result = 1;
        goto done;
    }

    wait_ready(&done);

    if (autoconnect && setup_maps()) {
        eprintf("Error setting map.\n");
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
