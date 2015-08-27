#include "../src/mapper_internal.h"
#include <mapper/mapper.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <lo/lo.h>
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
int iterations = 100;
int autoconnect = 1;

int automate = 1;

mapper_device source = 0;
mapper_device destination = 0;
mapper_signal sendsig = 0;
mapper_signal recvsig = 0;

int sent = 0;
int received = 0;
int done = 0;

/*! Creation of a local source. */
int setup_source()
{
    source = mapper_device_new("testInstanceSend", 0, 0);
    if (!source)
        goto error;
    eprintf("source created.\n");

    float mn=0, mx=10;

    sendsig = mapper_device_add_output(source, "/outsig", 1, 'f', 0, &mn, &mx);
    if (!sendsig)
        goto error;
    mapper_signal_reserve_instances(sendsig, 9, 0, 0);

    eprintf("Output signal registered.\n");
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

void insig_handler(mapper_signal sig, int instance_id, const void *value,
                   int count, mapper_timetag_t *timetag)
{
    if (value) {
        eprintf("--> destination %s instance %ld got %f\n",
                mapper_signal_name(sig), (long)instance_id, (*(float*)value));
        received++;
    }
    else {
        eprintf("--> destination %s instance %ld got NULL\n",
                mapper_signal_name(sig), (long)instance_id);
        mapper_signal_instance_release(sig, instance_id, MAPPER_NOW);
    }
}

void more_handler(mapper_signal sig, int instance_id, int event,
                  mapper_timetag_t *timetag)
{
    if (event & MAPPER_INSTANCE_OVERFLOW) {
        eprintf("OVERFLOW!! ALLOCATING ANOTHER INSTANCE.\n");
        mapper_signal_reserve_instances(sig, 1, 0, 0);
    }
    else if (event & MAPPER_UPSTREAM_RELEASE) {
        eprintf("UPSTREAM RELEASE!! RELEASING LOCAL INSTANCE.\n");
        mapper_signal_instance_release(sig, instance_id, MAPPER_NOW);
    }
}

/*! Creation of a local destination. */
int setup_destination()
{
    destination = mapper_device_new("testInstanceRecv", 0, 0);
    if (!destination)
        goto error;
    eprintf("destination created.\n");

    float mn=0;//, mx=1;

    recvsig = mapper_device_add_input(destination, "/insig", 1, 'f',
                                      0, &mn, 0, insig_handler, 0);
    if (!recvsig)
        goto error;

    // remove the default instance "0"
    mapper_signal_remove_instance(recvsig, 0);
    int i;
    for (i=100; i<104; i++) {
        mapper_signal_reserve_instances(recvsig, 1, &i, 0);
    }

    eprintf("Input signal registered.\n");
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

void wait_local_devices()
{
    while (!done && !(mapper_device_ready(source)
                      && mapper_device_ready(destination))) {
        mapper_device_poll(source, 0);
        mapper_device_poll(destination, 0);

        usleep(50 * 1000);
    }
}

void print_instance_ids(mapper_signal sig)
{
    int i, n = mapper_signal_num_active_instances(sig);
    eprintf("active %s: [", mapper_signal_name(sig));
    for (i=0; i<n; i++)
        eprintf(" %ld", (long)mapper_signal_active_instance_id(sig, i));
    eprintf(" ]   ");
}

void map_signals()
{
    mapper_map map = mapper_map_new(1, &sendsig, recvsig);
    mapper_map_set_mode(map, MAPPER_MODE_EXPRESSION);
    mapper_map_set_expression(map, "foo=1;  y=y{-1}+foo");
    mapper_map_sync(map);

    // wait until mapping has been established
    while (!done && (mapper_map_status(map) < MAPPER_ACTIVE)) {
        mapper_device_poll(source, 10);
        mapper_device_poll(destination, 10);
    }
}

void loop()
{
    eprintf("-------------------- GO ! --------------------\n");
    int i = 0, j = 0;
    float value = 0;

    while (i < iterations && !done) {
        // here we should create, update and destroy some instances
        switch (rand() % 5) {
            case 0:
                // try to destroy an instance
                j = rand() % 10;
                eprintf("--> Retiring sender instance %i\n", j);
                mapper_signal_instance_release(sendsig, j, MAPPER_NOW);
                break;
            default:
                j = rand() % 10;
                // try to update an instance
                value = (rand() % 10) * 1.0f;
                mapper_signal_instance_update(sendsig, j, &value, 0, MAPPER_NOW);
                eprintf("--> sender instance %d updated to %f\n", j, value);
                sent++;
                break;
        }

        print_instance_ids(sendsig);
        print_instance_ids(recvsig);
        eprintf("\n");

        mapper_device_poll(destination, 100);
        mapper_device_poll(source, 0);
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
    int i, j, result = 0, stats[6];

    // process flags for -v verbose, -t terminate, -h help
    for (i = 1; i < argc; i++) {
        if (argv[i] && argv[i][0] == '-') {
            int len = strlen(argv[i]);
            for (j = 1; j < len; j++) {
                switch (argv[i][j]) {
                    case 'h':
                        printf("testinstance.c: possible arguments "
                               "-q quiet (suppress output), "
                               "-h help\n");
                        return 1;
                        break;
                    case 'q':
                        verbose = 0;
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

    wait_local_devices();

    if (automate)
        map_signals();

    eprintf("\n**********************************************\n");
    eprintf("************ NO INSTANCE STEALING ************\n");
    loop();

    stats[0] = sent;
    stats[1] = received;

    for (i=0; i<10; i++)
        mapper_signal_instance_release(sendsig, i, MAPPER_NOW);
    sent = received = 0;

    mapper_signal_set_instance_allocation_mode(recvsig, MAPPER_STEAL_OLDEST);
    eprintf("\n**********************************************\n");
    eprintf("************ STEAL OLDEST INSTANCE ***********\n");
    if (!verbose)
        printf("\n");
    loop();

    stats[2] = sent;
    stats[3] = received;
    sent = received = 0;

    for (i=0; i<10; i++)
        mapper_signal_instance_release(sendsig, i, MAPPER_NOW);
    sent = received = 0;

    mapper_signal_set_instance_event_callback(recvsig, more_handler,
                                              MAPPER_INSTANCE_OVERFLOW
                                              | MAPPER_UPSTREAM_RELEASE, 0);
    eprintf("\n**********************************************\n");
    eprintf("*********** CALLBACK -> ADD INSTANCE *********\n");
    if (!verbose)
        printf("\n");
    loop();

    stats[4] = sent;
    stats[5] = received;

    eprintf("NO STEALING: sent %i updates, received %i updates (mismatch is OK).\n",
            stats[0], stats[1]);
    eprintf("STEAL OLDEST: sent %i updates, received %i updates (mismatch is OK).\n",
            stats[2], stats[3]);
    eprintf("ADD INSTANCE: sent %i updates, received %i updates.\n",
            stats[4], stats[5]);

    result = (stats[4] != stats[5]);

  done:
    cleanup_destination();
    cleanup_source();
    printf("Test %s.\n", result ? "FAILED" : "PASSED");
    return result;
}
