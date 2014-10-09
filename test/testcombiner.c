
#include "../src/mapper_internal.h"
#include <mapper/mapper.h>
#include <stdio.h>
#include <math.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>

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
mapper_signal sendsig[2] = {0, 0};
mapper_signal recvsig = 0;

int sent = 0;
int received = 0;

int setup_source()
{
    source = mdev_new("testsend", 0, 0);
    if (!source)
        goto error;
    eprintf("source created.\n");

    float mn=0, mx=1;
    sendsig[0] = mdev_add_output(source, "/outsig0", 1, 'f', 0, &mn, &mx);
    sendsig[1] = mdev_add_output(source, "/outsig1", 1, 'f', 0, &mn, &mx);

    eprintf("Output signals registered.\n");
    eprintf("Number of outputs: %d\n", mdev_num_outputs(source));
    return 0;

  error:
    return 1;
}

void cleanup_source()
{
    if (source) {
        eprintf("Freeing source.. ");
        fflush(stdout);
        mdev_free(source);
        eprintf("ok\n");
    }
}

void insig_handler(mapper_signal sig, mapper_db_signal props,
                   int instance_id, void *value, int count,
                   mapper_timetag_t *timetag)
{
    if (value) {
        eprintf("handler: Got %f\n", (*(float*)value));
    }
    received++;
}

int setup_destination()
{
    destination = mdev_new("testrecv", 0, 0);
    if (!destination)
        goto error;
    eprintf("destination created.\n");

    float mn=0, mx=1;
    recvsig = mdev_add_input(destination, "/insig", 1, 'f', 0,
                             &mn, &mx, insig_handler, 0);

    eprintf("Input signal /insig registered.\n");
    eprintf("Number of inputs: %d\n", mdev_num_inputs(destination));
    return 0;

  error:
    return 1;
}

void cleanup_destination()
{
    if (destination) {
        eprintf("Freeing destination.. ");
        fflush(stdout);
        mdev_free(destination);
        eprintf("ok\n");
    }
}

int setup_connection()
{
    mapper_monitor mon = mapper_monitor_new(source->admin, 0);

    char src_name[1024], dest_name[1024];
    mapper_monitor_link(mon, mdev_name(source),
                        mdev_name(destination), 0, 0);

    // wait unitl link has been established
    while (!done && !source->router->links) {
        mdev_poll(source, 10);
        mdev_poll(destination, 10);
    }

    msig_full_name(recvsig, dest_name, 1024);
    
    mapper_db_connection_t props;
    props.mode = MO_BYPASS;
//    props.cause_update = 1;

    for (int i = 0; i < 2; i++) {
        msig_full_name(sendsig[i], src_name, 1024);
        props.slot = i;
        props.cause_update = i;
        mapper_monitor_connect(mon, src_name, dest_name, &props,
                               CONNECTION_MODE | CONNECTION_SLOT | CONNECTION_CAUSE_UPDATE);
    }

    // wait until connections have been established
    while (!done && !source->router->links->num_connections_out) {
        mdev_poll(source, 10);
        mdev_poll(destination, 10);
    }

    // add combiner
    mapper_db_combiner_t combiner;
    combiner.num_slots = 2;
    combiner.mode = MO_EXPRESSION;
    combiner.expression = "y=x0+x1";
    int flags = COMBINER_MODE | COMBINER_NUM_SLOTS | COMBINER_EXPRESSION;
    mapper_monitor_set_signal_combiner(mon, dest_name, &combiner, flags);

    mapper_monitor_free(mon);

    return 0;
}

void wait_ready()
{
    while (!done && !(mdev_ready(source) && mdev_ready(destination))) {
        mdev_poll(source, 0);
        mdev_poll(destination, 0);
        usleep(500 * 1000);
    }
}

void loop()
{
    eprintf("Polling device..\n");
    int i = 0;
    float value = 0.;
    mapper_timetag_t timetag;
    while ((!terminate || i < 5) && !done) {
        value = i;
        mdev_poll(source, 0);
        eprintf("Updating signals: %s = %f, %s = %f\n",
                sendsig[0]->props.name, value,
                sendsig[1]->props.name, value * 2.f);
        mdev_now(source, &timetag);
        mdev_start_queue(source, timetag);
        msig_update(sendsig[0], &value, 1, timetag);
        value *= 2;
        msig_update(sendsig[1], &value, 1, timetag);
        mdev_send_queue(source, timetag);
        sent++;
        mdev_poll(destination, 100);
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
    int i, j, result = 0;

    // process flags for -v verbose, -t terminate, -h help
    for (i = 1; i < argc; i++) {
        if (argv[i] && argv[i][0] == '-') {
            int len = strlen(argv[i]);
            for (j = 1; j < len; j++) {
                switch (argv[i][j]) {
                    case 'h':
                        printf("testcombiner.c: possible arguments "
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

    if (autoconnect && setup_connection()) {
        eprintf("Error setting connection.\n");
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
