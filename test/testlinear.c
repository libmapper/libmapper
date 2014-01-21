
#include "../src/mapper_internal.h"
#include <mapper/mapper.h>
#include <stdio.h>
#include <math.h>

#include <unistd.h>

#ifdef WIN32
#define usleep(x) Sleep(x/1000)
#endif

mapper_device source = 0;
mapper_device destination = 0;
mapper_signal sendsig = 0;
mapper_signal recvsig = 0;

int sent = 0;
int received = 0;

int setup_source()
{
    source = mdev_new("testsend", 0, 0);
    if (!source)
        goto error;
    printf("source created.\n");

    float mn=0, mx=1;

    sendsig = mdev_add_output(source, "/outsig", 1, 'f', 0, &mn, &mx);

    printf("Output signal /outsig registered.\n");
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
        printf("handler: Got %f\n", (*(float*)value));
    }
    received++;
}

int setup_destination()
{
    destination = mdev_new("testrecv", 0, 0);
    if (!destination)
        goto error;
    printf("destination created.\n");

    float mn=0, mx=1;

    recvsig = mdev_add_input(destination, "/insig", 1, 'f', 0,
                             &mn, &mx, insig_handler, 0);

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

int setup_connection()
{
    float src_min = 0.;
    float src_max = 1.;
    float dest_min = -10.;
    float dest_max = 10.;

    mapper_monitor mon = mapper_monitor_new(source->admin, 0);

    char src_name[1024], dest_name[1024];
    mapper_monitor_link(mon, mdev_name(source),
                        mdev_name(destination), 0, 0);

    // Wait until link has been established
    while (!source->routers) {
        mdev_poll(source, 10);
        mdev_poll(destination, 10);
    }

    msig_full_name(sendsig, src_name, 1024);
    msig_full_name(recvsig, dest_name, 1024);

    mapper_connection_range_t range;
    range.src_min = &src_min;
    range.src_max = &src_max;
    range.dest_min = &dest_min;
    range.dest_max = &dest_max;
    range.known = CONNECTION_RANGE_KNOWN;

    mapper_db_connection_t props;
    props.range = range;
    props.src_length = 1;
    props.dest_length = 1;
    props.src_type = 'f';
    props.dest_type = 'f';
    props.mode = MO_LINEAR;

    mapper_monitor_connect(mon, src_name, dest_name, &props,
                           CONNECTION_RANGE_KNOWN | CONNECTION_MODE);

    // Wait until connection has been established
    while (!source->routers->n_connections) {
        mdev_poll(source, 10);
        mdev_poll(destination, 10);
    }

    mapper_monitor_free(mon);

    return 0;
}

void wait_ready()
{
    while (!(mdev_ready(source) && mdev_ready(destination))) {
        mdev_poll(source, 0);
        mdev_poll(destination, 0);
        usleep(500 * 1000);
    }
}

void loop()
{
    printf("Polling device..\n");
    int i;
    for (i = 0; i < 10; i++) {
        mdev_poll(source, 0);
        printf("Updating signal %s to %f\n",
               sendsig->props.name, (i * 1.0f));
        msig_update_float(sendsig, (i * 1.0f));
        sent++;
        usleep(250 * 1000);
        mdev_poll(destination, 0);
    }
}

int main()
{
    int result = 0;

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

    wait_ready();

    if (setup_connection()) {
        printf("Error initializing router.\n");
        result = 1;
        goto done;
    }

    loop();

    if (sent != received) {
        printf("Not all sent messages were received.\n");
        printf("Updated value %d time%s, but received %d of them.\n",
               sent, sent == 1 ? "" : "s", received);
        result = 1;
    }

  done:
    cleanup_destination();
    cleanup_source();
    printf("Test %s.\n", result ? "FAILED" : "PASSED");
    return result;
}
