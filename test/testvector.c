
#include "../src/mapper_internal.h"
#include <mapper/mapper.h>
#include <stdio.h>
#include <math.h>
#include <unistd.h>
#include <string.h>
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
mapper_signal sendsig = 0;
mapper_signal recvsig = 0;

int sent = 0;
int received = 0;

int setup_source()
{
    source = mdev_new("testsend", 0, 0);
    if (!source)
        goto error;
    eprintf("source created.\n");

    float mn[]={0,0,0}, mx[]={1,2,3};
    sendsig = mdev_add_output(source, "/outsig", 3, 'f', 0, &mn, &mx);

    eprintf("Output signal /outsig registered.\n");
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
        float *f = value;
        eprintf("handler: Got [%f, %f, %f]\n", f[0], f[1], f[2]);
    }
    received++;
}

int setup_destination()
{
    destination = mdev_new("testrecv", 0, 0);
    if (!destination)
        goto error;
    eprintf("destination created.\n");

    float mn[]={0,0,0}, mx[]={1,1,1};
    recvsig = mdev_add_input(destination, "/insig", 3, 'f', 0,
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

int setup_connections()
{
    int i = 0;
    mapper_monitor mon = mapper_monitor_new(source->admin, 0);

    char src_name[1024], dest_name[1024];
    mapper_monitor_link(mon, mdev_name(source),
                        mdev_name(destination), 0, 0);

    // wait until link has been established
    while (!done && !source->routers) {
        mdev_poll(source, 10);
        mdev_poll(destination, 10);
        if (i++ > 100)
            return 1;
    }

    msig_full_name(sendsig, src_name, 1024);
    msig_full_name(recvsig, dest_name, 1024);
    mapper_monitor_connect(mon, src_name, dest_name, 0, 0);

    // wait until connection has been established
    i = 0;
    while (!done && !source->routers->num_connections) {
        mdev_poll(source, 10);
        mdev_poll(destination, 10);
        if (i++ > 100)
            return 1;
    }

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
    while ((!terminate || i < 50) && !done) {
        mdev_poll(source, 0);
        float v[3];
        v[0] = (float)i;
        v[1] = (float)i+1;
        v[2] = (float)i+2;
        eprintf("Updating signal %s to [%f, %f, %f]\n",
               sendsig->props.name, v[0], v[1], v[2]);
        msig_update(sendsig, v, 1, MAPPER_NOW);
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
                        printf("testvector.c: possible arguments "
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

    if (autoconnect && setup_connections()) {
        eprintf("Error connecting signals.\n");
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
