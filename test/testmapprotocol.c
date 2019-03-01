
#include "../src/mapper_internal.h"
#include <mapper/mapper.h>
#include <stdio.h>
#include <math.h>
#include <signal.h>

#include <unistd.h>

#define eprintf(format, ...) do {               \
    if (verbose)                                \
        fprintf(stdout, format, ##__VA_ARGS__); \
} while(0)

mapper_device source = 0;
mapper_device destination = 0;
mapper_signal sendsig = 0;
mapper_signal recvsig = 0;

mapper_map map = 0;

int sent = 0;
int received = 0;
int done = 0;

int verbose = 1;
int terminate = 0;

int setup_source()
{
    source = mapper_device_new("testmapprotocol-send", 0, 0);
    if (!source)
        goto error;
    eprintf("source created.\n");

    float mn=0, mx=1;

    sendsig = mapper_device_add_output_signal(source, "/outsig", 1, 'f', 0,
                                              &mn, &mx);

    eprintf("Output signal /outsig registered.\n");
    eprintf("Number of outputs: %d\n",
            mapper_device_num_signals(source, MAPPER_DIR_OUTGOING));
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

void insig_handler(mapper_signal sig, mapper_id instance_id, const void *value,
                   int count, mapper_timetag timetag)
{
    if (value) {
        eprintf("handler: Got %f\n", (*(float*)value));
    }
    received++;
}

int setup_destination()
{
    destination = mapper_device_new("testmapprotocol-recv", 0, 0);
    if (!destination)
        goto error;
    eprintf("destination created.\n");

    float mn=0, mx=1;

    recvsig = mapper_device_add_input_signal(destination, "/insig", 1, 'f', 0,
                                             &mn, &mx, insig_handler, 0);

    eprintf("Input signal /insig registered.\n");
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

void set_map_protocol(mapper_protocol proto)
{
    if (!map || (mapper_map_protocol(map) == proto))
        return;

    mapper_map_set_protocol(map, proto);
    mapper_map_push(map);

    // wait until change has taken effect
    while (mapper_map_protocol(map) != proto) {
        mapper_device_poll(source, 10);
        mapper_device_poll(destination, 10);
    }
}

int setup_map()
{
    map = mapper_map_new(1, &sendsig, 1, &recvsig);
    mapper_map_push(map);

    // wait until map is established
    while (!mapper_map_ready(map)) {
        mapper_device_poll(destination, 10);
        mapper_device_poll(source, 10);
    }

    return 0;
}

void wait_ready()
{
    while (!(mapper_device_ready(source) && mapper_device_ready(destination))) {
        mapper_device_poll(source, 10);
        mapper_device_poll(destination, 10);
    }
}

void loop()
{
    int i = 0;
    while (!done && i < 50) {
        mapper_device_poll(source, 0);
        eprintf("Updating signal %s to %f\n", mapper_signal_name(sendsig),
                (i * 1.0f));
        mapper_signal_update_float(sendsig, (i * 1.0f));
        sent++;
        mapper_device_poll(destination, 100);
        ++i;

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
                        printf("testmapprotocol.c: possible arguments "
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
        eprintf("Done initializing source.\n");
        result = 1;
        goto done;
    }

    wait_ready();

    if (setup_map()) {
        eprintf("Error initializing map.\n");
        result = 1;
        goto done;
    }

    do {
        set_map_protocol(MAPPER_PROTO_UDP);
        eprintf("SENDING UDP\n");
        loop();

        set_map_protocol(MAPPER_PROTO_TCP);
        eprintf("SENDING TCP\n");
        loop();
    } while (!terminate && !done);

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
