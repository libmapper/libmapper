
#include "../src/mapper_internal.h"
#include <mapper/mapper.h>
#include <stdio.h>
#include <math.h>
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

int verbose = 1;
int terminate = 0;
int autoconnect = 1;
int done = 0;

mapper_device source = 0;
mapper_device destination = 0;
mapper_signal sendsig = 0;
mapper_signal recvsig = 0;

int sent = 0;
int received = 0;

int setup_source(char *iface)
{
    source = mapper_device_new("testsend", 0, 0);
    if (!source)
        goto error;
    eprintf("source created.\n");

    int mn=0, mx=1;
    sendsig = mapper_device_add_output(source, "/outsig", 1, 'i', 0, &mn, &mx);

    eprintf("Output signal /outsig registered.\n");
    eprintf("Number of outputs: %d\n",
            mapper_device_num_signals(source, MAPPER_OUTGOING));
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
        eprintf("handler: Got %f\n", (*(float*)value));
    }
    received++;
}

int setup_destination(char *iface)
{
    destination = mapper_device_new("testrecv", 0, 0);
    if (!destination)
        goto error;
    eprintf("destination created.\n");

    float mn=0, mx=1;
    recvsig = mapper_device_add_input(destination, "/insig", 1, 'f', 0,
                                      &mn, &mx, insig_handler, 0);

    eprintf("Input signal /insig registered.\n");
    eprintf("Number of inputs: %d\n",
            mapper_device_num_signals(destination, MAPPER_INCOMING));
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
    float src_min = 0., src_max = 100., dest_min = -10., dest_max = 10.;

    mapper_map map = mapper_map_new(1, &sendsig, recvsig);
    mapper_map_set_mode(map, MAPPER_MODE_LINEAR);

    mapper_slot slot = mapper_map_source_slot(map, 0);
    mapper_slot_set_minimum(slot, 1, 'f', &src_min);
    mapper_slot_set_maximum(slot, 1, 'f', &src_max);

    slot = mapper_map_destination_slot(map);
    mapper_slot_set_minimum(slot, 1, 'f', &dest_min);
    mapper_slot_set_maximum(slot, 1, 'f', &dest_max);
    mapper_slot_set_bound_min(slot, MAPPER_FOLD);

    mapper_map_sync(map);

    // Wait until mapping has been established
    while (!done && (mapper_map_status(map) < MAPPER_ACTIVE)) {
        mapper_device_poll(source, 10);
        mapper_device_poll(destination, 10);
    }

    return 0;
}

void wait_ready()
{
    while (!done && !(mapper_device_ready(source)
                      && mapper_device_ready(destination))) {
        mapper_device_poll(source, 0);
        mapper_device_poll(destination, 0);
        usleep(500 * 1000);
    }
}

void loop()
{
    eprintf("Polling device..\n");
    int i = 0;
    while ((!terminate || i < 50) && !done) {
        mapper_device_poll(source, 0);
        eprintf("Updating signal %s to %d\n", mapper_signal_name(sendsig), i);
        mapper_signal_update_int(sendsig, i);
        sent++;
        mapper_device_poll(destination, 100);
        i++;

        if (!verbose) {
            printf("\r  Sent: %4i, Received: %4i   ", sent, received);
            fflush(stdout);
        }
    }
}

void ctrlc(int signal)
{
    done = 1;
}

int main(int argc, char **argv)
{
    int i, j, result = 0;
    char *iface = 0;

    // process flags for -v verbose, -t terminate, -h help
    for (i = 1; i < argc; i++) {
        if (argv[i] && argv[i][0] == '-') {
            int len = strlen(argv[i]);
            for (j = 1; j < len; j++) {
                switch (argv[i][j]) {
                    case 'h':
                        printf("testlinear.c: possible arguments "
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
                        if (strcmp(argv[i], "--iface")==0 && argc>i+1) {
                            i++;
                            iface = argv[i];
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

    if (setup_destination(iface)) {
        eprintf("Error initializing destination.\n");
        result = 1;
        goto done;
    }

    if (setup_source(iface)) {
        eprintf("Done initializing source.\n");
        result = 1;
        goto done;
    }

    wait_ready();

    if (autoconnect && setup_maps()) {
        eprintf("Error initializing maps.\n");
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
