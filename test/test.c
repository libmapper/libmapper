
#include "../src/mapper_internal.h"
#include <mapper/mapper.h>
#include <stdio.h>
#include <math.h>
#include <lo/lo.h>
#include <stdlib.h>
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

int verbose = 1;
int terminate = 0;
int autoconnect = 1;

/*! Creation of a local source. */
int setup_source()
{
    source = mdev_new("testsend", 0, 0);
    if (!source)
        goto error;
    eprintf("source created.\n");

    float mnf[]={3.2,2,0}, mxf[]={-2,13,100};
    double mnd=0, mxd=10;

    sendsig_1 = mdev_add_output(source, "/outsig_1", 1, 'd', "Hz", &mnd, &mxd);
    sendsig_2 = mdev_add_output(source, "/outsig_2", 1, 'f', "mm", mnf, mxf);
    sendsig_3 = mdev_add_output(source, "/outsig_3", 3, 'f', 0, mnf, mxf);
    sendsig_4 = mdev_add_output(source, "/outsig_4", 1, 'f', 0, mnf, mxf);

    eprintf("Output signal /outsig registered.\n");

    // Make sure we can add and remove outputs without crashing.
    mdev_remove_output(source, mdev_add_output(source, "/outsig_5", 1,
                                               'f', 0, &mnf, &mxf));

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
        eprintf("--> destination got %s", props->name);
        if (props->type == 'f') {
            float *v = value;
            for (int i = 0; i < props->length; i++) {
                eprintf(" %f", v[i]);
            }
        }
        else if (props->type == 'd') {
            double *v = value;
            for (int i = 0; i < props->length; i++) {
                eprintf(" %f", v[i]);
            }
        }
        eprintf("\n");
    }
    received++;
}

/*! Creation of a local destination. */
int setup_destination()
{
    destination = mdev_new("testrecv", 0, 0);
    if (!destination)
        goto error;
    eprintf("destination created.\n");

    float mnf[]={0,0,0}, mxf[]={1,1,1};
    double mnd=0, mxd=1;

    recvsig_1 = mdev_add_input(destination, "/insig_1", 1, 'f',
                               0, mnf, mxf, insig_handler, 0);
    recvsig_2 = mdev_add_input(destination, "/insig_2", 1, 'd',
                               0, &mnd, &mxd, insig_handler, 0);
    recvsig_3 = mdev_add_input(destination, "/insig_3", 3, 'f',
                               0, mnf, mxf, insig_handler, 0);
    recvsig_4 = mdev_add_input(destination, "/insig_4", 1, 'f',
                               0, mnf, mxf, insig_handler, 0);

    eprintf("Input signal /insig registered.\n");

    // Make sure we can add and remove inputs and inputs within crashing.
    mdev_remove_input(destination,
                      mdev_add_input(destination, "/insig_5", 1,
                                     'f', 0, &mnf, &mxf, 0, 0));

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

void loop()
{
    eprintf("-------------------- GO ! --------------------\n");
    int i = 0, recvd;

    if (autoconnect) {
        mapper_monitor mon = mapper_monitor_new(source->admin, 0);

        char src_name[1024], dest_name[1024];
        mapper_monitor_link(mon, mdev_name(source),
                            mdev_name(destination), 0, 0);

        while (!source->routers) {
            mdev_poll(source, 10);
            mdev_poll(destination, 10);
        }

        msig_full_name(sendsig_1, src_name, 1024);
        msig_full_name(recvsig_1, dest_name, 1024);
        mapper_monitor_connect(mon, src_name, dest_name, 0, 0);

        msig_full_name(sendsig_2, src_name, 1024);
        msig_full_name(recvsig_2, dest_name, 1024);
        mapper_monitor_connect(mon, src_name, dest_name, 0, 0);

        msig_full_name(sendsig_3, src_name, 1024);
        msig_full_name(recvsig_3, dest_name, 1024);
        mapper_monitor_connect(mon, src_name, dest_name, 0, 0);

        msig_full_name(recvsig_4, dest_name, 1024);
        mapper_monitor_connect(mon, src_name, dest_name, 0, 0);

        // wait until connection has been established
        while (!done && !source->routers->num_connections) {
            mdev_poll(source, 10);
            mdev_poll(destination, 10);
        }

        mapper_monitor_free(mon);
    }

    i = 0;
    float val[3];

    while ((!terminate || i < 50) && !done) {
        mdev_poll(source, 0);
        msig_update_double(sendsig_1, ((i % 10) * 1.0f));
        eprintf("/outsig_1 value updated to %d -->\n", i % 10);

        msig_update_float(sendsig_2, ((i % 10) * 1.0f));
        eprintf("/outsig_2 value updated to %d -->\n", i % 10);

        val[0] = val[1] = val[2] = (i % 10) * 1.0f;
        msig_update(sendsig_3, val, 1, MAPPER_NOW);
        eprintf("/outsig_3 value updated to [%f,%f,%f] -->\n",
               val[0], val[1], val[2]);

        msig_update_float(sendsig_4, ((i % 10) * 1.0f));
        eprintf("/outsig_4 value updated to %d -->\n", i % 10);

        eprintf("Sent %i messages.\n", 4);
        sent += 4;
        recvd = mdev_poll(destination, 100);
        eprintf("Received %i messages.\n\n", recvd);
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

int main(int argc, char ** argv)
{
    int i, j, result = 0;

    // process flags for -v verbose, -t terminate, -h help
    for (i = 1; i < argc; i++) {
        if (argv[i] && argv[i][0] == '-') {
            int len = strlen(argv[i]);
            for (j = 1; j < len; j++) {
                switch (argv[i][j]) {
                    case 'h':
                        printf("test.c: possible arguments "
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

    loop();

    if (autoconnect && received != sent) {
        eprintf("sent: %d, recvd: %d\n", sent, received);
        result = 1;
    }

  done:
    cleanup_destination();
    cleanup_source();
    printf("Test %s.\n", result ? "FAILED" : "PASSED");
    return result;
}
