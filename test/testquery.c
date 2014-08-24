#include "../src/mapper_internal.h"
#include <mapper/mapper.h>
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
int terminate = 0;
int autoconnect = 1;

mapper_device source = 0;
mapper_device destination = 0;
mapper_signal sendsig[4] = {0, 0, 0, 0};
mapper_signal recvsig[4] = {0, 0, 0, 0};

int sent = 0;
int received = 0;
int done = 0;

void query_response_handler(mapper_signal sig, mapper_db_signal props,
                            int instance_id, void *value, int count,
                            mapper_timetag_t *timetag)
{
    int i;
    if (value) {
        eprintf("--> source got query response: %s ", props->name);
        for (i = 0; i < props->length * count; i++)
            eprintf("%i ", ((int*)value)[i]);
        eprintf("\n");
    }
    else {
        eprintf("--> source got empty query response: %s\n", props->name);
    }

    received++;
}

/*! Creation of a local source. */
int setup_source()
{
    char sig_name[20];
    source = mdev_new("testquery-send", 0, 0);
    if (!source)
        goto error;
    eprintf("source created.\n");

    int mn[]={0,0,0,0}, mx[]={10,10,10,10};

    for (int i = 0; i < 4; i++) {
        snprintf(sig_name, 20, "%s%i", "/outsig_", i);
        sendsig[i] = mdev_add_output(source, sig_name, i+1, 'i', 0, mn, mx);
        msig_set_callback(sendsig[i], query_response_handler, 0);
        msig_update(sendsig[i], mn, 0, MAPPER_NOW);
    }

    eprintf("Output signals registered.\n");
    eprintf("Number of outputs: %d\n", mdev_num_outputs(source));

    return 0;

error:
    return 1;
}

void cleanup_source()
{
    if (source) {
        if (source->routers) {
            eprintf("Removing router.. ");
            fflush(stdout);
            mdev_remove_router(source, source->routers);
            eprintf("ok\n");
        }
        eprintf("Freeing source.. ");
        fflush(stdout);
        mdev_free(source);
        eprintf("ok\n");
    }
}

void insig_handler(mapper_signal sig,mapper_db_signal props,
                   int instance_id, void *value, int count,
                   mapper_timetag_t *timetag)
{
    if (value) {
        eprintf("--> destination got %s %f\n", props->name, (*(float*)value));
    }
    received++;
}

/*! Creation of a local destination. */
int setup_destination()
{
    char sig_name[10];
    destination = mdev_new("testquery-recv", 0, 0);
    if (!destination)
        goto error;
    eprintf("destination created.\n");

    float mn[]={0,0,0,0}, mx[]={1,1,1,1};

    for (int i = 0; i < 4; i++) {
        snprintf(sig_name, 10, "%s%i", "/insig_", i);
        recvsig[i] = mdev_add_input(destination, sig_name, i+1,
                                    'f', 0, mn, mx, insig_handler, 0);
    }

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

void wait_local_devices()
{
    while (!done && !(mdev_ready(source) && mdev_ready(destination))) {
        mdev_poll(source, 0);
        mdev_poll(destination, 0);

        usleep(50 * 1000);
    }
}

int setup_connections()
{
    int i;
    mapper_monitor mon = mapper_monitor_new(source->admin, 0);

    char src_name[1024], dest_name[1024];
    mapper_monitor_link(mon, mdev_name(source),
                        mdev_name(destination), 0, 0);

    i = 0;
    while (!done && !source->routers) {
        mdev_poll(source, 10);
        mdev_poll(destination, 10);
        if (i++ > 100)
            return 1;
    }

    for (int i = 0; i < 4; i++) {
        msig_full_name(sendsig[i], src_name, 1024);
        msig_full_name(recvsig[i], dest_name, 1024);
        mapper_monitor_connect(mon, src_name, dest_name, 0, 0);
    }

    // swap the last two signals to mix up signal vector lengths
    msig_full_name(sendsig[2], src_name, 1024);
    msig_full_name(recvsig[3], dest_name, 1024);
    mapper_monitor_connect(mon, src_name, dest_name, 0, 0);
    msig_full_name(sendsig[3], src_name, 1024);
    msig_full_name(recvsig[2], dest_name, 1024);
    mapper_monitor_connect(mon, src_name, dest_name, 0, 0);

    i = 0;
    // wait until connection has been established
    while (!done && !source->routers->num_connections) {
        mdev_poll(source, 10);
        mdev_poll(destination, 10);
        if (i++ > 100)
            return 1;
    }

    mapper_monitor_free(mon);
    return 0;
}

void loop()
{
    eprintf("-------------------- GO ! --------------------\n");
    int i = 10, j = 0, count;
    float value[] = {0., 0., 0., 0.};

    while ((!terminate || i < 50) && !done) {
        for (j = 0; j < 4; j++)
            value[j] = (i % 10) * 1.0f;
        for (j = 0; j < 4; j++) {
            msig_update(recvsig[j], value, 0, MAPPER_NOW);
        }
        eprintf("\ndestination values updated to %f -->\n", (i % 10) * 1.0f);
        for (j = 0; j < 4; j++) {
            sent += count = msig_query_remotes(sendsig[j], MAPPER_NOW);
            eprintf("Sent %i queries for sendsig[%i]\n", count, j);
        }
        mdev_poll(destination, 50);
        mdev_poll(source, 50);
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
                        eprintf("testquery.c: possible arguments "
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

    wait_local_devices();

    if (autoconnect && setup_connections()) {
        eprintf("Error connecting signals.\n");
        result = 1;
        goto done;
    }

    loop();

    if (sent != received) {
        eprintf("Not all sent queries received responses.\n");
        eprintf("Queried %d time%s, but received %d responses.\n",
                sent, sent == 1 ? "" : "s", received);
        result = 1;
    }

done:
    cleanup_destination();
    cleanup_source();
    printf("Test %s.\n", result ? "FAILED" : "PASSED");
    return result;
}
