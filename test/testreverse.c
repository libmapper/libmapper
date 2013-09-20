#include "../src/mapper_internal.h"
#include <mapper/mapper.h>
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
mapper_signal sendsig;
mapper_signal recvsig;

int sent = 0;
int received = 0;
int done = 0;

void insig_handler(mapper_signal sig, mapper_db_signal props,
                   int instance_id, void *value, int count,
                   mapper_timetag_t *timetag)
{
    if (value) {
        if (props->type == 'f')
            printf("--> %s got %f\n", props->is_output ?
                   "source" : "destination", (*(float*)value));
        else if (props->type == 'i')
            printf("--> %s got %i\n", props->is_output ?
                   "source" : "destination", (*(int*)value));
    }
    else {
        printf("--> %s got NIL\n", props->is_output ?
               "source" : "destination");
    }
    received++;
}

/*! Creation of a local source. */
int setup_source()
{
    source = mdev_new("testreverse-send", 0, 0);
    if (!source)
        goto error;
    printf("source created.\n");

    float mn=0, mx=10;

    sendsig = mdev_add_output(source, "/outsig", 1, 'i', 0, &mn, &mx);
    msig_set_callback(sendsig, insig_handler, 0);

    printf("Output signals registered.\n");
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

/*! Creation of a local destination. */
int setup_destination()
{
    destination = mdev_new("testreverse-recv", 0, 0);
    if (!destination)
        goto error;
    printf("destination created.\n");

    float mn=0, mx=1;

    recvsig = mdev_add_input(destination, "/insig", 1,
                             'f', 0, &mn, &mx, insig_handler, 0);

    printf("Input signal /insig registered.\n");
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

void loop()
{
    printf("-------------------- GO ! --------------------\n");
    int i = 0;

    if (automate) {
        mapper_monitor mon = mapper_monitor_new(source->admin, 0);

        char src_name[1024], dest_name[1024];
        mapper_monitor_link(mon, mdev_name(source),
                            mdev_name(destination), 0, 0);

        msig_full_name(sendsig, src_name, 1024);
        msig_full_name(recvsig, dest_name, 1024);
        mapper_db_connection_t props;
        props.mode = MO_REVERSE;
        mapper_monitor_connect(mon, src_name, dest_name, &props,
                               CONNECTION_MODE);

        // wait until connection has been established
        while (!destination->receivers ||
               !destination->receivers->n_connections) {
            mdev_poll(source, 1);
            mdev_poll(destination, 1);
        }

        mapper_monitor_free(mon);
    }

    while (i >= 0 && !done) {
        msig_update_float(recvsig, ((i % 10) * 1.0f));
        printf("\ndestination value updated to %f -->\n", (i % 10) * 1.0f);
        mdev_poll(destination, 1);
        mdev_poll(source, 100);
        i++;
    }
}

void ctrlc(int sig)
{
    done = 1;
}

int main()
{
    int result = 0;

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

    loop();

  done:
    cleanup_destination();
    cleanup_source();
    return result;
}
