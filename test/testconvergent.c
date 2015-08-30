
#include "../src/mapper_internal.h"
#include <mapper/mapper.h>
#include <stdio.h>
#include <math.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>

#ifdef WIN32
#define usleep(x) Sleep(x/1000)
#endif

#define eprintf(format, ...) do {               \
    if (verbose)                                \
        fprintf(stdout, format, ##__VA_ARGS__); \
} while(0)

#define NUM_SOURCES 2

int verbose = 1;
int terminate = 0;
int autoconnect = 1;
int done = 0;

mapper_device sources[NUM_SOURCES];
mapper_device destination = 0;
mapper_signal sendsig[NUM_SOURCES][2];
mapper_signal recvsig = 0;

int sent = 0;
int received = 0;

int setup_sources()
{
    int i, mni=0, mxi=1;
    float mnf=0, mxf=1;
    for (i = 0; i < NUM_SOURCES; i++)
        sources[i] = 0;
    for (i = 0; i < NUM_SOURCES; i++) {
        sources[i] = mapper_device_new("testsend", 0, 0);
        if (!sources[i])
            goto error;
        eprintf("source %d created.\n", i);
        sendsig[i][0] = mapper_device_add_output(sources[i], "/sendsig1", 1,
                                                 'i', 0, &mni, &mxi);
        sendsig[i][1] = mapper_device_add_output(sources[i], "/sendsig2", 1,
                                                 'f', 0, &mnf, &mxf);
    }
    return 0;

  error:
    for (i = 0; i < NUM_SOURCES; i++) {
        if (sources[i])
            mapper_device_free(sources[i]);
    }
    return 1;
}

void cleanup_source()
{
    for (int i = 0; i < NUM_SOURCES; i++) {
        if (sources[i]) {
            eprintf("Freeing source %d... ", i);
            fflush(stdout);
            mapper_device_free(sources[i]);
            eprintf("ok\n");
        }
    }
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
    destination = mapper_device_new("testrecv", 0, 0);
    if (!destination)
        goto error;
    eprintf("destination created.\n");

    float mn=0, mx=1;
    recvsig = mapper_device_add_input(destination, "/recvsig", 1, 'f', 0,
                                      &mn, &mx, insig_handler, 0);

    eprintf("Input signal /insig registered.\n");
    eprintf("Number of inputs: %d\n", mapper_device_num_signals(destination,
                                                                MAPPER_INCOMING));
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
    mapper_signal all_sources[2] = {sendsig[0][0], sendsig[1][1]};

    mapper_map map = mapper_map_new(2, all_sources, recvsig);
    mapper_map_set_mode(map, MAPPER_MODE_EXPRESSION);

    // build expression string
    mapper_slot slot1 = mapper_map_slot_by_signal(map, sendsig[0][0]);
    mapper_slot slot2 = mapper_map_slot_by_signal(map, sendsig[1][1]);
    char expr[64];
    snprintf(expr, 64, "y=x%d-x%d", mapper_slot_index(slot1), mapper_slot_index(slot2));
    mapper_map_set_expression(map, expr);

    mapper_slot_set_causes_update(slot1, 0);

    mapper_map_sync(map);

    // wait until mappings have been established
    int i;
    while (!done && (mapper_map_status(map) < MAPPER_ACTIVE)) {
        for (i = 0; i < NUM_SOURCES; i++)
            mapper_device_poll(sources[i], 10);
        mapper_device_poll(destination, 10);
        printf("waiting... status:%d\n", mapper_map_status(map));
    }

    return 0;
}

void wait_ready()
{
    int i, ready = 0;
    while (!done && !ready) {
        ready = 1;
        for (i = 0; i < NUM_SOURCES; i++) {
            mapper_device_poll(sources[i], 10);
            if (!mapper_device_ready(sources[i])) {
                ready = 0;
                break;
            }
        }
        mapper_device_poll(destination, 10);
        if (!mapper_device_ready(destination))
            ready = 0;
    }
}

void loop()
{
    eprintf("Polling device..\n");
    int i = 0, j;
    float f = 0.;
    while ((!terminate || i < 50) && !done) {
        for (j = 0; j < NUM_SOURCES; j++) {
            mapper_device_poll(sources[j], 0);
        }

        eprintf("Updating signals: %s = %i, %s = %f\n",
                sendsig[0][0]->name, i,
                sendsig[0][1]->name, i * 2.f);
        mapper_signal_update_int(sendsig[0][0], i);
        mapper_signal_update_int(sendsig[1][0], i);
        f = i * 2;
        mapper_signal_update_float(sendsig[0][1], f);
        mapper_signal_update_float(sendsig[1][1], f);

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
        eprintf("Done initializing %d sources.\n", NUM_SOURCES);
        result = 1;
        goto done;
    }

    wait_ready();

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
