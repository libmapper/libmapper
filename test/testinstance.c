#include "../src/mapper_internal.h"
#include <mapper/mapper.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <lo/lo.h>
#include <unistd.h>
#include <signal.h>
#include <string.h>

#define eprintf(format, ...) do {               \
    if (verbose)                                \
        fprintf(stdout, format, ##__VA_ARGS__); \
} while(0)

int verbose = 1;
int iterations = 100;
int autoconnect = 1;
int period = 100;
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
    source = mapper_device_new("testinstance-send", 0);
    if (!source)
        goto error;

    float mn=0, mx=10;

    sendsig = mapper_device_add_signal(source, MAPPER_DIR_OUT, 10, "outsig",
                                       1, MAPPER_FLOAT, NULL, &mn, &mx, NULL);
    if (!sendsig)
        goto error;

    eprintf("Output signal added with %i instances.\n",
            mapper_signal_get_num_instances(sendsig));

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

void insig_handler(mapper_signal sig, mapper_id instance, int length,
                   mapper_type type, const void *value, mapper_time t)
{
    const char *name;
    mapper_object_get_prop_by_index((mapper_object)sig, MAPPER_PROP_NAME, NULL,
                                    NULL, NULL, (const void**)&name);
    if (value) {
        eprintf("--> destination %s instance %i got %f\n", name, (int)instance,
                (*(float*)value));
        received++;
    }
    else {
        eprintf("--> destination %s instance %i got NULL\n", name,
                (int)instance);
        mapper_signal_release_instance(sig, instance, MAPPER_NOW);
    }
}

void more_handler(mapper_signal sig, mapper_id instance,
                  mapper_instance_event event, mapper_time_t *t)
{
    if (event & MAPPER_INSTANCE_OVERFLOW) {
        eprintf("OVERFLOW!! ALLOCATING ANOTHER INSTANCE.\n");
        mapper_signal_reserve_instances(sig, 1, 0, 0);
    }
    else if (event & MAPPER_UPSTREAM_RELEASE) {
        eprintf("UPSTREAM RELEASE!! RELEASING LOCAL INSTANCE.\n");
        mapper_signal_release_instance(sig, instance, MAPPER_NOW);
    }
}

/*! Creation of a local destination. */
int setup_destination()
{
    destination = mapper_device_new("testinstance-recv", 0);
    if (!destination)
        goto error;

    float mn=0;//, mx=1;

    // Specify 0 instances since we wish to use specific ids
    recvsig = mapper_device_add_signal(destination, MAPPER_DIR_IN, 0,
                                       "insig", 1, MAPPER_FLOAT, NULL,
                                       &mn, NULL, insig_handler);
    if (!recvsig)
        goto error;

    int i;
    for (i=2; i<10; i+=2) {
        mapper_signal_reserve_instances(recvsig, 1, (mapper_id*)&i, 0);
    }

    eprintf("Input signal added with %i instances.\n",
            mapper_signal_get_num_instances(recvsig));

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
        mapper_device_poll(source, 25);
        mapper_device_poll(destination, 25);
    }
}

void print_instance_ids(mapper_signal sig)
{
    int i, n = mapper_signal_get_num_instances(sig);
    const char *name;
    mapper_object_get_prop_by_index((mapper_object)sig, MAPPER_PROP_NAME, NULL,
                                    NULL, NULL, (const void**)&name);
    eprintf("%s: [ ", name);
    for (i=0; i<n; i++) {
        eprintf("%1i, ", (int)mapper_signal_get_instance_id(sig, i));
    }
    eprintf("\b\b ]   ");
}

void print_instance_vals(mapper_signal sig)
{
    int i, id, n = mapper_signal_get_num_instances(sig);
    const char *name;
    mapper_object_get_prop_by_index((mapper_object)sig, MAPPER_PROP_NAME, NULL,
                                    NULL, NULL, (const void**)&name);
    eprintf("%s: [ ", name);
    for (i=0; i<n; i++) {
        id = mapper_signal_get_instance_id(sig, i);
        float *val = (float*)mapper_signal_get_value(sig, id, 0);
        if (val)
            printf("%1.0f, ", *val);
        else
            printf("–, ");
    }
    eprintf("\b\b ]   ");
}

void map_signals()
{
    mapper_map map = mapper_map_new(1, &sendsig, 1, &recvsig);
    const char *expr = "y{-1}=-10;y=y{-1}+1";
    mapper_object_set_prop((mapper_object)map, MAPPER_PROP_EXPR, NULL, 1,
                           MAPPER_STRING, expr, 1);
    mapper_object_push((mapper_object)map);

    // wait until mapping has been established
    while (!done && !mapper_map_ready(map)) {
        mapper_device_poll(source, 10);
        mapper_device_poll(destination, 10);
    }
}

void loop()
{
    eprintf("-------------------- GO ! --------------------\n");
    int i = 0;
    float value = 0;
    mapper_id instance;

    while (i < iterations && !done) {
        // here we should create, update and destroy some instances
        instance = (rand() % 10);
        switch (rand() % 5) {
            case 0:
                // try to destroy an instance
                eprintf("--> Retiring sender instance %"PR_MAPPER_ID"\n", instance);
                mapper_signal_release_instance(sendsig, instance, MAPPER_NOW);
                break;
            default:
                // try to update an instance
                value = (rand() % 10) * 1.0f;
                mapper_signal_set_value(sendsig, instance, 1, MAPPER_FLOAT,
                                        &value, MAPPER_NOW);
                eprintf("--> sender instance %"PR_MAPPER_ID" updated to %f\n",
                        instance, value);
                sent++;
                break;
        }

        mapper_device_poll(destination, period);
        mapper_device_poll(source, 0);
        i++;

        if (verbose) {
            print_instance_ids(sendsig);
            print_instance_ids(recvsig);
            eprintf("\n");

            print_instance_vals(sendsig);
            print_instance_vals(recvsig);
            printf("\n");
        }
        else {
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
                               "-f fast (execute quickly), "
                               "-q quiet (suppress output), "
                               "-h help\n");
                        return 1;
                        break;
                    case 'f':
                        period = 1;
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
        mapper_signal_release_instance(sendsig, i, MAPPER_NOW);
    sent = received = 0;

    mapper_signal_set_stealing_mode(recvsig, MAPPER_STEAL_OLDEST);
    eprintf("\n**********************************************\n");
    eprintf("************ STEAL OLDEST INSTANCE ***********\n");
    if (!verbose)
        printf("\n");
    loop();

    stats[2] = sent;
    stats[3] = received;
    sent = received = 0;

    for (i=0; i<10; i++)
        mapper_signal_release_instance(sendsig, i, MAPPER_NOW);
    sent = received = 0;

    mapper_signal_set_instance_event_callback(recvsig, more_handler,
                                              MAPPER_INSTANCE_OVERFLOW
                                              | MAPPER_UPSTREAM_RELEASE);
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
    printf("...................Test %s\x1B[0m.\n",
           result ? "\x1B[31mFAILED" : "\x1B[32mPASSED");
    return result;
}
