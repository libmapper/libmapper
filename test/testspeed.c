#include "../src/mapper_internal.h"
#include <mapper/mapper.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <time.h>
#include <sys/time.h>
#include <lo/lo.h>
#include <unistd.h>
#include <signal.h>
#include <string.h>

#define eprintf(format, ...) do {               \
    if (verbose)                                \
        fprintf(stdout, format, ##__VA_ARGS__); \
    else                                        \
        fprintf(stdout, ".");                   \
    fflush(stdout);                             \
} while(0)

int verbose = 1;

mapper_device source = 0;
mapper_device destination = 0;
mapper_signal sendsig = 0;
mapper_signal recvsig = 0;

int numTrials = 10;
int trial = 0;
int numModes = 2;
int mode = 0;
int use_instance = 1;
int iterations = 100000;
int counter = 0;
int received = 0;
int done = 0;

double times[100];
float value;

void switch_modes();
void print_results();

/*! Internal function to get the current time. */
static double current_time()
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (double) tv.tv_sec + tv.tv_usec / 1000000.0;
}

/*! Creation of a local source. */
int setup_source()
{
    source = mapper_device_new("testSpeedSend", 0, 0);
    if (!source)
        goto error;
    eprintf("source created.\n");

    sendsig = mapper_device_add_output(source, "/outsig", 1, 'f', 0, 0, 0);
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

void insig_handler(mapper_signal sig, mapper_id instance, const void *value,
                   int count, mapper_timetag_t *timetag)
{
    if (value) {
        counter = (counter+1)%10;
        if (++received >= iterations)
            switch_modes();
        if (use_instance) {
            mapper_signal_instance_update(sendsig, counter, value, 1,
                                          MAPPER_NOW);
        }
        else
            mapper_signal_update(sendsig, value, 1, MAPPER_NOW);
    }
    else
        eprintf("--> destination %s instance %ld got NULL\n",
                sig->name, (long)instance);
}

/*! Creation of a local destination. */
int setup_destination()
{
    destination = mapper_device_new("testSpeedRecv", 0, 0);
    if (!destination)
        goto error;
    eprintf("destination created.\n");

    recvsig = mapper_device_add_input(destination, "/insig", 1, 'f',
                                      0, 0, 0, insig_handler, 0);
    if (!recvsig)
        goto error;
    mapper_signal_reserve_instances(recvsig, 9, 0, 0);

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
        mapper_device_poll(source, 25);
        mapper_device_poll(destination, 25);
    }
}

void map_signals()
{
    mapper_map map = mapper_map_new(1, &sendsig, recvsig);
    mapper_map_set_mode(map, MAPPER_MODE_EXPRESSION);
    mapper_map_set_expression(map, "y=y{-1}+1");
    mapper_map_sync(map);

    // wait until mapping has been established
    while (!done && (mapper_map_status(map) < MAPPER_ACTIVE)) {
        mapper_device_poll(source, 10);
        mapper_device_poll(destination, 10);
    }
}

void ctrlc(int sig)
{
    done = 1;
}

void switch_modes()
{
    int i;
    // possible modes: bypass/expression/calibrate, boundary actions, instances, instance-stealing
    eprintf("MODE %i TRIAL %i COMPLETED...\n", mode, trial);
    received = 0;
    times[mode*numTrials+trial] = current_time() - times[mode*numTrials+trial];
    if (++trial >= numTrials) {
        eprintf("SWITCHING MODES...\n");
        trial = 0;
        mode++;
    }
    if (mode >= numModes) {
        done = 1;
        return;
    }

    switch (mode)
    {
        case 0:
            use_instance = 1;
            break;
        case 1:
            use_instance = 0;
            for (i=1; i<10; i++) {
                mapper_signal_instance_release(sendsig, i, MAPPER_NOW);
            }
            break;
    }

    times[mode*numTrials+trial] = current_time();
}

void print_results()
{
    int i, j;
    printf("\n*****************************************************\n");
    printf("\nRESULTS OF SPEED TEST:\n");
    for (i=0; i<numModes; i++) {
        printf("MODE %i\n", i);
        float bestTime = times[i*numTrials];
        for (j=0; j<numTrials; j++) {
            printf("trial %i: %i messages processed in %f seconds\n", j, iterations, times[i*numTrials+j]);
            if (times[i*numTrials+j] < bestTime)
                bestTime = times[i*numTrials+j];
        }
        printf("\nbest trial: %i messages in %f seconds\n", iterations, bestTime);
    }
    printf("\n*****************************************************\n");
}

int main(int argc, char **argv)
{
    int i, j, result = 0;

    // process flags for -v verbose, -h help
    for (i = 1; i < argc; i++) {
        if (argv[i] && argv[i][0] == '-') {
            int len = strlen(argv[i]);
            for (j = 1; j < len; j++) {
                switch (argv[i][j]) {
                    case 'h':
                        printf("testspeed.c: possible arguments "
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

    value = (float)rand();

    signal(SIGINT, ctrlc);

    if (setup_destination()) {
        printf("Error initializing destination.\n");
        result = 1;
        goto done;
    }

    if (setup_source()) {
        eprintf("Error initializing source.\n");
        result = 1;
        goto done;
    }

    wait_local_devices();

    map_signals();

    // start things off
    eprintf("STARTING TEST...\n");
    times[0] = current_time();
    mapper_signal_instance_update(sendsig, counter++, &value, 0, MAPPER_NOW);
    while (!done) {
        mapper_device_poll(destination, 0);
        mapper_device_poll(source, 0);
    }
    goto done;

  done:
    cleanup_destination();
    cleanup_source();
    if (verbose)
        print_results();
    printf("Test %s.\n", result ? "FAILED" : "PASSED");
    return result;
}
