#include <mapper/mapper.h>
#include <stdio.h>
#include <math.h>
#include <lo/lo.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <string.h>

#define eprintf(format, ...) do {               \
    if (verbose)                                \
        fprintf(stdout, format, ##__VA_ARGS__); \
} while(0)

mapper_device src = 0;
mapper_device dst = 0;
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

int period = 100;

/*! Creation of a local source. */
int setup_src()
{
    src = mapper_device_new("test-send", 0);
    if (!src)
        goto error;
    eprintf("source created.\n");

    float mnf[]={3.2,2,0}, mxf[]={-2,13,100};
    double mnd=0, mxd=10;

    sendsig_1 = mapper_device_add_signal(src, MAPPER_DIR_OUT, 1, "outsig_1",
                                         1, MAPPER_DOUBLE, "Hz", &mnd, &mxd, NULL);
    sendsig_2 = mapper_device_add_signal(src, MAPPER_DIR_OUT, 1, "outsig_2",
                                         1, MAPPER_FLOAT, "mm", mnf, mxf, NULL);
    sendsig_3 = mapper_device_add_signal(src, MAPPER_DIR_OUT, 1, "outsig_3",
                                         3, MAPPER_FLOAT, NULL, mnf, mxf, NULL);
    sendsig_4 = mapper_device_add_signal(src, MAPPER_DIR_OUT, 1, "outsig_4",
                                         1, MAPPER_FLOAT, NULL, mnf, mxf, NULL);

    eprintf("Output signal 'outsig' registered.\n");

    // Make sure we can add and remove outputs without crashing.
    mapper_device_remove_signal(src,
                                mapper_device_add_signal(src, MAPPER_DIR_OUT, 1,
                                                         "outsig_5", 1, MAPPER_FLOAT,
                                                         NULL, &mnf, &mxf, NULL));

    eprintf("Number of outputs: %d\n",
            mapper_device_get_num_signals(src, MAPPER_DIR_OUT));

    return 0;

  error:
    return 1;
}

void cleanup_src()
{
    if (src) {
        eprintf("Freeing source.. ");
        fflush(stdout);
        mapper_device_free(src);
        eprintf("ok\n");
    }
}

void handler(mapper_signal sig, mapper_id instance, int length,
             mapper_type type, const void *value, mapper_time t)
{
    if (value) {
        const char *name;
        mapper_object_get_prop_by_index(sig, MAPPER_PROP_NAME, NULL, NULL, NULL,
                                        (const void**)&name);
        eprintf("--> destination got %s", name);

        switch (type) {
            case MAPPER_FLOAT: {
                float *v = (float*)value;
                for (int i = 0; i < length; i++) {
                    eprintf(" %f", v[i]);
                }
                break;
            }
            case MAPPER_DOUBLE: {
                double *v = (double*)value;
                for (int i = 0; i < length; i++) {
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
int setup_dst()
{
    dst = mapper_device_new("test-recv", 0);
    if (!dst)
        goto error;
    eprintf("destination created.\n");

    float mnf[]={0,0,0}, mxf[]={1,1,1};
    double mnd=0, mxd=1;

    recvsig_1 = mapper_device_add_signal(dst, MAPPER_DIR_IN, 1, "insig_1", 1,
                                         MAPPER_FLOAT, NULL, mnf, mxf, handler);
    recvsig_2 = mapper_device_add_signal(dst, MAPPER_DIR_IN, 1, "insig_2", 1,
                                         MAPPER_DOUBLE, NULL, &mnd, &mxd, handler);
    recvsig_3 = mapper_device_add_signal(dst, MAPPER_DIR_IN, 1, "insig_3", 3,
                                         MAPPER_FLOAT, NULL, mnf, mxf, handler);
    recvsig_4 = mapper_device_add_signal(dst, MAPPER_DIR_IN, 1, "insig_4", 1,
                                         MAPPER_FLOAT, NULL, mnf, mxf, handler);

    eprintf("Input signal 'insig' registered.\n");

    // Make sure we can add and remove inputs and inputs within crashing.
    mapper_device_remove_signal(dst,
                                mapper_device_add_signal(dst, MAPPER_DIR_IN, 1,
                                                         "insig_5", 1, MAPPER_FLOAT,
                                                         NULL, &mnf, &mxf, NULL));

    eprintf("Number of inputs: %d\n",
            mapper_device_get_num_signals(dst, MAPPER_DIR_IN));

    return 0;

  error:
    return 1;
}

void cleanup_dst()
{
    if (dst) {
        eprintf("Freeing destination.. ");
        fflush(stdout);
        mapper_device_free(dst);
        eprintf("ok\n");
    }
}



void wait_local_devices()
{
    while (!done && !(mapper_device_ready(src) && mapper_device_ready(dst))) {
        mapper_device_poll(src, 25);
        mapper_device_poll(dst, 25);
    }
}

void loop()
{
    eprintf("-------------------- GO ! --------------------\n");
    int i = 0, recvd;

    if (!done && autoconnect) {
        mapper_map maps[4];
        maps[0] = mapper_map_new(1, &sendsig_1, 1, &recvsig_1);
        maps[1] = mapper_map_new(1, &sendsig_2, 1, &recvsig_2);
        maps[2] = mapper_map_new(1, &sendsig_3, 1, &recvsig_3);
        maps[3] = mapper_map_new(1, &sendsig_3, 1, &recvsig_4);

        for (i = 0; i < 4; i++) {
            mapper_object_push(maps[i]);
        }

        // wait until all maps has been established
        int num_maps = 0;
        while (!done && num_maps < 4) {
            mapper_device_poll(src, 10);
            mapper_device_poll(dst, 10);
            num_maps = 0;
            for (i = 0; i < 4; i++) {
                num_maps += (mapper_map_ready(maps[i]));
            }
        }
    }

    i = 0;
    float val[3];

    while ((!terminate || i < 50) && !done) {
        mapper_device_poll(src, 0);

        val[0] = val[1] = val[2] = (i % 10) * 1.0f;
        mapper_signal_set_value(sendsig_1, 0, 1, MAPPER_FLOAT, val, MAPPER_NOW);
        eprintf("outsig_1 value updated to %d -->\n", i % 10);

        mapper_signal_set_value(sendsig_2, 0, 1, MAPPER_FLOAT, val, MAPPER_NOW);
        eprintf("outsig_2 value updated to %d -->\n", i % 10);

        mapper_signal_set_value(sendsig_3, 0, 3, MAPPER_FLOAT, val, MAPPER_NOW);
        eprintf("outsig_3 value updated to [%f,%f,%f] -->\n",
               val[0], val[1], val[2]);

        mapper_signal_set_value(sendsig_4, 0, 1, MAPPER_FLOAT, val, MAPPER_NOW);
        eprintf("outsig_4 value updated to %d -->\n", i % 10);

        eprintf("Sent %i messages.\n", 4);
        sent += 4;
        recvd = mapper_device_poll(dst, period);
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
                               "-f fast (execute quickly), "
                               "-h help\n");
                        return 1;
                        break;
                    case 'q':
                        verbose = 0;
                        break;
                    case 'f':
                        period = 1;
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

    if (setup_dst()) {
        eprintf("Error initializing destination.\n");
        result = 1;
        goto done;
    }

    if (setup_src()) {
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
    cleanup_dst();
    cleanup_src();
    printf("...................Test %s\x1B[0m.\n",
           result ? "\x1B[31mFAILED" : "\x1B[32mPASSED");
    return result;
}
