
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
mapper_signal sendsig_1 = 0;
mapper_signal recvsig_1 = 0;
mapper_signal sendsig_2 = 0;
mapper_signal recvsig_2 = 0;
mapper_signal sendsig_3 = 0;
mapper_signal recvsig_3 = 0;
mapper_signal sendsig_4 = 0;
mapper_signal recvsig_4 = 0;

int sent = 0;
int received = 0;
int done = 0;

/*! Creation of a local source. */
int setup_source()
{
    source = mdev_new("testsend", 0, 0);
    if (!source)
        goto error;
    printf("source created.\n");

    float mn=0, mx=10;

    sendsig_1 = mdev_add_output(source, "/outsig_1", 1, 'f', "Hz", &mn, &mx);
    sendsig_2 = mdev_add_output(source, "/outsig_2", 1, 'f', "mm", &mn, &mx);
    sendsig_3 = mdev_add_output(source, "/outsig_3", 3, 'f', 0, &mn, &mx);
    sendsig_4 = mdev_add_output(source, "/outsig_4", 1, 'f', 0, &mn, &mx);

    printf("Output signal /outsig registered.\n");

    // Make sure we can add and remove outputs without crashing.
    mdev_remove_output(source, mdev_add_output(source, "/outsig_5", 1,
                                               'f', 0, &mn, &mx));

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
        printf("--> destination got %s", props->name);
        float *v = value;
        for (int i = 0; i < props->length; i++) {
            printf(" %f", v[i]);
        }
        printf("\n");
    }
    received++;
}

/*! Creation of a local destination. */
int setup_destination()
{
    destination = mdev_new("testrecv", 0, 0);
    if (!destination)
        goto error;
    printf("destination created.\n");

    float mn=0, mx=1;

    recvsig_1 = mdev_add_input(destination, "/insig_1", 1, 'f',
                               0, &mn, &mx, insig_handler, 0);
    recvsig_2 = mdev_add_input(destination, "/insig_2", 1, 'f',
                               0, &mn, &mx, insig_handler, 0);
    recvsig_3 = mdev_add_input(destination, "/insig_3", 3, 'f',
                               0, &mn, &mx, insig_handler, 0);
    recvsig_4 = mdev_add_input(destination, "/insig_4", 1, 'f',
                               0, &mn, &mx, insig_handler, 0);

    printf("Input signal /insig registered.\n");

    // Make sure we can add and remove inputs and inputs within crashing.
    mdev_remove_input(destination,
                      mdev_add_input(destination, "/insig_5", 1,
                                     'f', 0, &mn, &mx, 0, 0));

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

        msig_full_name(sendsig_1, src_name, 1024);
        msig_full_name(recvsig_1, dest_name, 1024);
        mapper_monitor_connect(mon, src_name, dest_name, 0, 0);

        msig_full_name(sendsig_2, src_name, 1024);
        msig_full_name(recvsig_2, dest_name, 1024);
        mapper_monitor_connect(mon, src_name, dest_name, 0, 0);

        mapper_monitor_free(mon);
    }

    while (i >= 0 && !done) {
        mdev_poll(source, 0);
        msig_update_float(source->outputs[0], ((i % 10) * 1.0f));
        msig_update_float(source->outputs[1], ((i % 10) * 1.0f));
        printf("source value updated to %d -->\n", i % 10);

        printf("Received %i messages.\n\n", mdev_poll(destination, 100));
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
