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
static double get_current_time()
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (double) tv.tv_sec + tv.tv_usec / 1000000.0; 
}

/*! Creation of a local source. */
int setup_source()
{
    source = mdev_new("testSpeedSend", 0, 0);
    if (!source)
        goto error;
    printf("source created.\n");

    sendsig = mdev_add_output(source, "/outsig", 1, 'f', 0, 0, 0);
    if (!sendsig)
        goto error;
    msig_reserve_instances(sendsig, 9, 0, 0);

    printf("Output signal registered.\n");
    printf("Number of outputs: %d\n", mdev_num_outputs(source));

    return 0;

  error:
    return 1;
}

void cleanup_source()
{
    if (source) {
        if (source->routers) {
            printf("Removing router.. ");
            fflush(stdout);
            mdev_remove_router(source, source->routers);
            printf("ok\n");
        }
        printf("Freeing source.. ");
        fflush(stdout);
        mdev_free(source);
        printf("ok\n");
    }
}

void insig_handler(mapper_signal sig, mapper_db_signal props,
                   int instance_id, void *value, int count,
                   mapper_timetag_t *timetag)
{
    if (value) {
        counter = (counter+1)%10;
        if (++received >= iterations)
            switch_modes();
        if (use_instance) {
            msig_update_instance(sendsig, counter, value, 1, MAPPER_NOW);
        }
        else
            msig_update(sendsig, value, 1, MAPPER_NOW);
    }
    else
        printf("--> destination %s instance %ld got NULL\n",
               props->name, (long)instance_id);
}

/*! Creation of a local destination. */
int setup_destination()
{
    destination = mdev_new("testSpeedRecv", 0, 0);
    if (!destination)
        goto error;
    printf("destination created.\n");

    recvsig = mdev_add_input(destination, "/insig", 1, 'f',
                             0, 0, 0, insig_handler, 0);
    if (!recvsig)
        goto error;
    msig_reserve_instances(recvsig, 9, 0, 0);

    printf("Input signal registered.\n");
    printf("Number of inputs: %d\n", mdev_num_inputs(destination));

    return 0;

  error:
    return 1;
}

void cleanup_destination()
{
    if (destination) {
        printf("Freeing destination.. ");
        fflush(stdout);
        mdev_free(destination);
        printf("ok\n");
    }
}

void wait_local_devices()
{
    while (!(mdev_ready(source) && mdev_ready(destination))) {
        mdev_poll(source, 25);
        mdev_poll(destination, 25);
    }
}

void connect_signals()
{
    mapper_monitor mon = mapper_monitor_new(source->admin, 0);

    char src_name[1024], dest_name[1024];
    mapper_monitor_link(mon, mdev_name(source),
                        mdev_name(destination), 0, 0);

    msig_full_name(sendsig, src_name, 1024);
    msig_full_name(recvsig, dest_name, 1024);
    mapper_db_connection_t props;
    props.expression = "y=y{-1}+1";
    props.mode = MO_EXPRESSION;
    mapper_monitor_connect(mon, src_name, dest_name, &props,
                           CONNECTION_MODE | CONNECTION_EXPRESSION);

    // wait until connection has been established
    while (!source->routers || !source->routers->n_connections) {
        mdev_poll(source, 1);
        mdev_poll(destination, 1);
    }

    mapper_monitor_free(mon);
}

void ctrlc(int sig)
{
    done = 1;
}

void switch_modes()
{
    int i;
    // possible modes: bypass/expression/calibrate, boundary actions, instances, instance-stealing
    printf("MODE %i TRIAL %i COMPLETED...\n", mode, trial);
    received = 0;
    times[mode*numTrials+trial] = get_current_time() - times[mode*numTrials+trial];
    if (++trial >= numTrials) {
        printf("SWITCHING MODES...\n");
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
                msig_release_instance(sendsig, i, MAPPER_NOW);
            }
            break;
    }

    times[mode*numTrials+trial] = get_current_time();
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

int main()
{
    int result = 0;

    value = (float)rand();

    signal(SIGINT, ctrlc);

    if (setup_destination()) {
        printf("Error initializing destination.\n");
        result = 1;
        goto done;
    }

    if (setup_source()) {
        printf("Error initializing source.\n");
        result = 1;
        goto done;
    }

    wait_local_devices();

    connect_signals();

    // start things off
    printf("STARTING TEST...\n");
    times[0] = get_current_time();
    msig_update_instance(sendsig, counter++, &value, 0, MAPPER_NOW);
    while (!done) {
        mdev_poll(destination, 0);
        mdev_poll(source, 0);
    }
    goto done;

  done:
    cleanup_destination();
    cleanup_source();
    print_results();
    return result;
}
