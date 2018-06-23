#include <mapper/mapper.h>
#include <stdio.h>
#include <math.h>
#include <lo/lo.h>
#include <unistd.h>
#include <signal.h>
#include <string.h>

#define eprintf(format, ...) do {               \
    if (verbose)                                \
        fprintf(stdout, format, ##__VA_ARGS__); \
} while(0)

int verbose = 1;
int terminate = 0;
int autoconnect = 1;
int done = 0;
int period = 100;

mapper_device source = 0;
mapper_device destination = 0;
mapper_signal sendsig;
mapper_signal recvsig;

int sent = 0;
int received = 0;

void insig_handler(mapper_signal sig, mapper_id inst, int length,
                   mapper_type type, const void *val, mapper_time t)
{
    const char *name;
    mapper_object_get_prop_by_index(sig, MAPPER_PROP_NAME, NULL, NULL, NULL,
                                    (const void**)&name);
    int i;
    eprintf("--> %s got ", name);
    if (val) {
        if (type == MAPPER_FLOAT) {
            for (i = 0; i < length; i++)
                eprintf("%f ", ((float*)val)[i]);
            eprintf("\n");
        }
        else if (type == MAPPER_INT32) {
            for (i = 0; i < length; i++)
                eprintf("%i ", ((int*)val)[i]);
            eprintf("\n");
        }
    }
    else {
        for (i = 0; i < length; i++)
            eprintf("NIL ");
        eprintf("\n");
    }
    received++;
}

/*! Creation of a local source. */
int setup_source()
{
    source = mapper_device_new("testreverse-send", 0);
    if (!source)
        goto error;
    eprintf("source created.\n");

    float mn[]={0.f,0.f}, mx[]={10.f,10.f};

    sendsig = mapper_device_add_signal(source, MAPPER_DIR_OUT, 1, "outsig",
                                       2, MAPPER_FLOAT, NULL, mn, mx, NULL);
    mapper_signal_set_callback(sendsig, insig_handler);

    eprintf("Output signals registered.\n");
    eprintf("Number of outputs: %d\n",
            mapper_device_get_num_signals(source, MAPPER_DIR_OUT));

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

/*! Creation of a local destination. */
int setup_destination()
{
    destination = mapper_device_new("testreverse-recv", 0);
    if (!destination)
        goto error;
    eprintf("destination created.\n");

    float mn=0, mx=1;

    recvsig = mapper_device_add_signal(destination, MAPPER_DIR_IN, 1,
                                       "insig", 1, MAPPER_FLOAT, NULL, &mn, &mx,
                                       insig_handler);

    eprintf("Input signal insig registered.\n");
    eprintf("Number of inputs: %d\n",
            mapper_device_get_num_signals(destination, MAPPER_DIR_IN));

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
        mapper_device_poll(source, 25);
        mapper_device_poll(destination, 25);
    }
}

int setup_maps()
{
    int i = 0;

    mapper_map map = mapper_map_new(1, &recvsig, 1, &sendsig);
    mapper_object_push(map);

    i = 0;
    // wait until mapping has been established
    while (!done && !mapper_map_ready(map)) {
        mapper_device_poll(source, 10);
        mapper_device_poll(destination, 10);
        if (i++ > 100)
            return 1;
    }

    return 0;
}

void loop()
{
    eprintf("-------------------- GO ! --------------------\n");
    int i = 0;
    float val[] = {0, 0};
    mapper_signal_set_value(sendsig, 0, 2, MAPPER_FLOAT, val, MAPPER_NOW);

    while ((!terminate || i < 50) && !done) {
        val[0] = i % 10;
        mapper_signal_set_value(recvsig, 0, 1, MAPPER_FLOAT, val, MAPPER_NOW);
        sent++;
        eprintf("\ndestination value updated to %f -->\n", (i % 10) * 1.0f);
        mapper_device_poll(destination, 0);
        mapper_device_poll(source, period);
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
                        eprintf("testreverse.c: possible arguments"
                                "-f fast (execute quickly), "
                                "-q quiet (suppress output), "
                                "-t terminate automatically, "
                                "-h help\n");
                        return 1;
                        break;
                    case 'f':
                        period = 1;
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
        eprintf("Error initializing source.\n");
        result = 1;
        goto done;
    }

    wait_local_devices();

    if (autoconnect && setup_maps()) {
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
    printf("...................Test %s\x1B[0m.\n",
           result ? "\x1B[31mFAILED" : "\x1B[32mPASSED");
    return result;
}
