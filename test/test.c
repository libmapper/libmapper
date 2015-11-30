
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
    source = mapper_device_new("testsend", 0, 0);
    if (!source)
        goto error;
    eprintf("source created.\n");

    float mnf[]={3.2,2,0}, mxf[]={-2,13,100};
    double mnd=0, mxd=10;

    sendsig_1 = mapper_device_add_output(source, "outsig_1", 1, 'd', "Hz",
                                         &mnd, &mxd);
    sendsig_2 = mapper_device_add_output(source, "outsig_2", 1, 'f', "mm",
                                         mnf, mxf);
    sendsig_3 = mapper_device_add_output(source, "outsig_3", 3, 'f', 0,
                                         mnf, mxf);
    sendsig_4 = mapper_device_add_output(source, "outsig_4", 1, 'f', 0,
                                         mnf, mxf);

    eprintf("Output signal 'outsig' registered.\n");

    // Make sure we can add and remove outputs without crashing.
    mapper_device_remove_signal(source,
                                mapper_device_add_output(source, "outsig_5", 1,
                                                         'f', 0, &mnf, &mxf));

    eprintf("Number of outputs: %d\n",
            mapper_device_num_signals(source, MAPPER_DIR_OUTGOING));

    return 0;

  error:
    return 1;
}

void cleanup_source()
{
    if (source) {
        eprintf("Freeing source.. ");
        fflush(stdout);
        mapper_device_free(source);
        eprintf("ok\n");
    }
}

void insig_handler(mapper_signal sig, mapper_id instance, const void *value,
                   int count, mapper_timetag_t *timetag)
{
    if (value) {
        eprintf("--> destination got %s", mapper_signal_name(sig));
        switch (mapper_signal_type(sig)) {
            case 'f': {
                float *v = (float*)value;
                for (int i = 0; i < mapper_signal_length(sig); i++) {
                    eprintf(" %f", v[i]);
                }
                break;
            }
            case 'd': {
                double *v = (double*)value;
                for (int i = 0; i < mapper_signal_length(sig); i++) {
                    eprintf(" %f", v[i]);
                }
                break;
            }
            default:
                break;
        }
        eprintf("\n");
    }
    received++;
}

/*! Creation of a local destination. */
int setup_destination()
{
    destination = mapper_device_new("testrecv", 0, 0);
    if (!destination)
        goto error;
    eprintf("destination created.\n");

    float mnf[]={0,0,0}, mxf[]={1,1,1};
    double mnd=0, mxd=1;

    recvsig_1 = mapper_device_add_input(destination, "insig_1", 1, 'f', 0,
                                        mnf, mxf, insig_handler, 0);
    recvsig_2 = mapper_device_add_input(destination, "insig_2", 1, 'd', 0,
                                        &mnd, &mxd, insig_handler, 0);
    recvsig_3 = mapper_device_add_input(destination, "insig_3", 3, 'f', 0,
                                        mnf, mxf, insig_handler, 0);
    recvsig_4 = mapper_device_add_input(destination, "insig_4", 1, 'f', 0,
                                        mnf, mxf, insig_handler, 0);

    eprintf("Input signal 'insig' registered.\n");

    // Make sure we can add and remove inputs and inputs within crashing.
    mapper_device_remove_signal(destination,
                                mapper_device_add_input(destination,
                                                        "insig_5", 1, 'f', 0,
                                                        &mnf, &mxf, 0, 0));

    eprintf("Number of inputs: %d\n",
            mapper_device_num_signals(destination, MAPPER_DIR_INCOMING));

    return 0;

  error:
    return 1;
}

void cleanup_destination()
{
    if (destination) {
        eprintf("Freeing destination.. ");
        fflush(stdout);
        mapper_device_free(destination);
        eprintf("ok\n");
    }
}



void wait_local_devices()
{
    while (!done && !(mapper_device_ready(source)
                      && mapper_device_ready(destination))) {
        mapper_device_poll(source, 0);
        mapper_device_poll(destination, 0);

        usleep(50 * 1000);
    }
}

void loop()
{
    eprintf("-------------------- GO ! --------------------\n");
    int i = 0, recvd;

    if (!done && autoconnect) {
        mapper_map maps[4];
        maps[0] = mapper_map_new(1, &sendsig_1, recvsig_1);
        maps[1] = mapper_map_new(1, &sendsig_2, recvsig_2);
        maps[2] = mapper_map_new(1, &sendsig_3, recvsig_3);
        maps[3] = mapper_map_new(1, &sendsig_3, recvsig_4);

        for (i = 0; i < 4; i++) {
            mapper_map_push(maps[i]);
        }

        // wait until all maps has been established
        int num_maps = 0;
        while (!done && num_maps < 4) {
            mapper_device_poll(source, 10);
            mapper_device_poll(destination, 10);
            num_maps = 0;
            for (i = 0; i < 4; i++) {
                num_maps += (mapper_map_ready(maps[i]));
            }
        }
    }

    i = 0;
    float val[3];

    while ((!terminate || i < 50) && !done) {
        mapper_device_poll(source, 100);
        mapper_signal_update_double(sendsig_1, ((i % 10) * 1.0f));
        eprintf("outsig_1 value updated to %d -->\n", i % 10);

        mapper_signal_update_float(sendsig_2, ((i % 10) * 1.0f));
        eprintf("outsig_2 value updated to %d -->\n", i % 10);

        val[0] = val[1] = val[2] = (i % 10) * 1.0f;
        mapper_signal_update(sendsig_3, val, 1, MAPPER_NOW);
        eprintf("outsig_3 value updated to [%f,%f,%f] -->\n",
               val[0], val[1], val[2]);

        mapper_signal_update_float(sendsig_4, ((i % 10) * 1.0f));
        eprintf("outsig_4 value updated to %d -->\n", i % 10);

        eprintf("Sent %i messages.\n", 4);
        sent += 4;
        recvd = mapper_device_poll(destination, 100);
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
