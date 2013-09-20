#include "../src/mapper_internal.h"
#include <mapper/mapper.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <lo/lo.h>

#include <unistd.h>
#include <signal.h>

#ifdef WIN32
#define usleep(x) Sleep(x/1000)
#endif

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
    source = mdev_new("testInstanceSend", 0, 0);
    if (!source)
        goto error;
    printf("source created.\n");

    float mn=0, mx=10;

    sendsig = mdev_add_output(source, "/outsig", 1, 'f', 0, &mn, &mx);
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
        printf("--> destination %s instance %ld got %f\n",
               props->name, (long)instance_id, (*(float*)value));
        received++;
    }
    else {
        printf("--> destination %s instance %ld got NULL\n",
               props->name, (long)instance_id);
        msig_release_instance(sig, instance_id, MAPPER_NOW);
    }
}

void more_handler(mapper_signal sig, mapper_db_signal props,
                  int instance_id, msig_instance_event_t event,
                  mapper_timetag_t *timetag)
{
    if (event & IN_OVERFLOW) {
        printf("OVERFLOW!! ALLOCATING ANOTHER INSTANCE.\n");
        msig_reserve_instances(sig, 1, 0, 0);
    }
    else if (event & IN_UPSTREAM_RELEASE) {
        printf("UPSTREAM RELEASE!! RELEASING LOCAL INSTANCE.\n");
        msig_release_instance(sig, instance_id, MAPPER_NOW);
    }
}

/*! Creation of a local destination. */
int setup_destination()
{
    destination = mdev_new("testInstanceRecv", 0, 0);
    if (!destination)
        goto error;
    printf("destination created.\n");

    float mn=0;//, mx=1;

    recvsig = mdev_add_input(destination, "/insig", 1, 'f',
                             0, &mn, 0, insig_handler, 0);
    if (!recvsig)
        goto error;

    // remove the default instance "0"
    msig_remove_instance(recvsig, 0);
    int i;
    for (i=100; i<104; i++) {
        msig_reserve_instances(recvsig, 1, &i, 0);
    }

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
        mdev_poll(source, 0);
        mdev_poll(destination, 0);

        usleep(50 * 1000);
    }
}

void print_instance_ids(mapper_signal sig)
{
    int i, n = msig_num_active_instances(sig);
    printf("active %s: [", sig->props.name);
    for (i=0; i<n; i++)
        printf(" %ld", (long)msig_active_instance_id(sig, i));
    printf(" ]   ");
}

void connect_signals()
{
    mapper_monitor mon = mapper_monitor_new(source->admin, 0);

    char src_name[1024], dest_name[1024];
    mapper_monitor_link(mon, mdev_name(source),
                        mdev_name(destination), 0, 0);

    msig_full_name(sendsig, src_name, 1024);
    msig_full_name(recvsig, dest_name, 1024);
    mapper_monitor_connect(mon, src_name, dest_name, 0, 0);

    // wait until connection has been established
    while (!source->routers || !source->routers->n_connections) {
        mdev_poll(source, 1);
        mdev_poll(destination, 1);
    }

    mapper_monitor_free(mon);
}

void loop(int iterations)
{
    printf("-------------------- GO ! --------------------\n");
    int i = 0, j = 0;
    float value = 0;

    while (i >= 0 && iterations-- >= 0 && !done) {
        // here we should create, update and destroy some instances
        switch (rand() % 5) {
            case 0:
                // try to destroy an instance
                j = rand() % 10;
                printf("--> Retiring sender instance %i\n", j);
                msig_release_instance(sendsig, j, MAPPER_NOW);
                break;
            default:
                j = rand() % 10;
                // try to update an instance
                value = (rand() % 10) * 1.0f;
                msig_update_instance(sendsig, j, &value, 0, MAPPER_NOW);
                printf("--> sender instance %d updated to %f\n", j, value);
                sent++;
                break;
        }

        print_instance_ids(sendsig);
        print_instance_ids(recvsig);
        printf("\n");

        mdev_poll(destination, 100);
        mdev_poll(source, 0);
        i++;
        //usleep(1 * 1000);
    }
}

void ctrlc(int sig)
{
    done = 1;
}

int main()
{
    int result = 0;
    int stats[6], i;

    signal(SIGINT, ctrlc);

    if (setup_destination()) {
        printf("Error initializing destination.\n");
        result = 1;
        goto done;
    }

    if (setup_source()) {
        printf("Done initializing source.\n");
        result = 1;
        goto done;
    }

    wait_local_devices();

    if (automate)
        connect_signals();

    printf("\n**********************************************\n");
    printf("************ NO INSTANCE STEALING ************\n");
    loop(100);

    stats[0] = sent;
    stats[1] = received;

    for (i=0; i<10; i++)
        msig_release_instance(sendsig, i, MAPPER_NOW);
    sent = received = 0;

    msig_set_instance_allocation_mode(recvsig, IN_STEAL_OLDEST);
    printf("\n**********************************************\n");
    printf("************ STEAL OLDEST INSTANCE ***********\n");
    loop(100);

    stats[2] = sent;
    stats[3] = received;
    sent = received = 0;

    for (i=0; i<10; i++)
        msig_release_instance(sendsig, i, MAPPER_NOW);
    sent = received = 0;

    msig_set_instance_event_callback(recvsig, more_handler,
                                     IN_OVERFLOW | IN_UPSTREAM_RELEASE, 0);
    printf("\n**********************************************\n");
    printf("*********** CALLBACK -> ADD INSTANCE *********\n");
    loop(100);

    stats[4] = sent;
    stats[5] = received;

    printf("NO STEALING: sent %i updates, received %i updates (mismatch is OK).\n", stats[0], stats[1]);
    printf("STEAL OLDEST: sent %i updates, received %i updates (mismatch is OK).\n", stats[2], stats[3]);
    printf("ADD INSTANCE: sent %i updates, received %i updates.\n", stats[4], stats[5]);

    result = (stats[4] != stats[5]);

  done:
    cleanup_destination();
    cleanup_source();
    printf("Test %s.\n", result ? "FAILED" : "PASSED");
    return result;
}
