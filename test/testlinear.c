
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
    mapper_admin admin = mapper_admin_new(iface, 0, 0);
    source = mdev_new("testsend", 0, admin);
    if (!source)
        goto error;
    eprintf("source created.\n");

    int mn=0, mx=1;

    sendsig = mdev_add_output(source, "/outsig", 1, 'i', 0, &mn, &mx);

    eprintf("Output signal /outsig registered.\n");
    eprintf("Number of outputs: %d\n", mdev_num_outputs(source));
    return 0;

  error:
    return 1;
}

void cleanup_source()
{
    if (source) {
        mapper_admin admin = source->admin;
        eprintf("Freeing source.. ");
        fflush(stdout);
        mdev_free(source);
        mapper_admin_free(admin);
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

int setup_destination(char *iface)
{
    mapper_admin admin = mapper_admin_new(iface, 0, 0);
    destination = mdev_new("testrecv", 0, admin);
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
        mapper_admin admin = destination->admin;
        eprintf("Freeing destination.. ");
        fflush(stdout);
        mdev_free(destination);
        mapper_admin_free(admin);
        eprintf("ok\n");
    }
}

int setup_connection()
{
    float src_min = 0., src_max = 100., dest_min = -10., dest_max = 10.;

    mapper_monitor mon = mmon_new(source->admin, 0);

    mapper_db_connection_t props;
    mapper_db_connection_slot_t src_slot;
    props.num_sources = 1;
    props.sources = &src_slot;
    src_slot.minimum = &src_min;
    src_slot.maximum = &src_max;
    src_slot.length = 1;
    src_slot.type = 'f';
    src_slot.flags = CONNECTION_RANGE_KNOWN;
    props.destination.minimum = &dest_min;
    props.destination.maximum = &dest_max;
    props.destination.length = 1;
    props.destination.type = 'f';
    props.destination.flags = CONNECTION_RANGE_KNOWN;
    props.mode = MO_LINEAR;
    props.flags = CONNECTION_MODE;

    mapper_db_signal src = &sendsig->props;
    mmon_connect_signals_by_db_record(mon, 1, &src, &recvsig->props, &props);

    // Wait until connection has been established
    while (!done && !mdev_num_connections_out(source)) {
        mdev_poll(source, 10);
        mdev_poll(destination, 10);
    }

    mmon_free(mon);

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
    while ((!terminate || i < 50) && !done) {
        mdev_poll(source, 0);
        eprintf("Updating signal %s to %d\n",
                sendsig->props.name, i);
        msig_update_int(sendsig, i);
        sent++;
        mdev_poll(destination, 100);
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

    if (autoconnect && setup_connection()) {
        eprintf("Error initializing connections.\n");
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
