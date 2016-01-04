
#include "../src/mapper_internal.h"
#include <mapper/mapper.h>
#include <stdio.h>
#include <math.h>
#include <stdlib.h>
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
mapper_signal sendsig1 = 0;
mapper_signal recvsig1 = 0;

int port = 9000;

int sent = 0;
int received = 0;

int setup_source()
{
    source = mapper_device_new("testsend", port, 0);
    if (!source)
        goto error;
    eprintf("source created.\n");

    float mn=0, mx=1;

    sendsig = mapper_device_add_output(source, "outsig", 1, 'f', 0, &mn, &mx);
    sendsig1= mapper_device_add_output(source, "outsig1", 1, 'f', 0, &mn, &mx);

    eprintf("Output signal 'outsig' registered.\n");
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
        eprintf("handler: Got %f\n", (*(float*)value));
    }
    received++;
}

int setup_destination()
{
    destination = mapper_device_new("testrecv", port, 0);
    if (!destination)
        goto error;
    eprintf("destination created.\n");

    float mn=0, mx=1;

    recvsig = mapper_device_add_input(destination, "insig", 1, 'f', 0, &mn, &mx,
                                      insig_handler, 0);
	recvsig1= mapper_device_add_input(destination, "insig1", 1, 'f', 0, &mn, &mx,
                                      insig_handler, 0);

    eprintf("Input signal 'insig' registered.\n");
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

int create_maps()
{
    mapper_map maps[2];
    maps[0] = mapper_map_new(1, &sendsig, recvsig);
    mapper_map_push(maps[0]);
    maps[1] = mapper_map_new(1, &sendsig1, recvsig);
    mapper_map_push(maps[1]);

    // wait until mapping has been established
    while (!done && !mapper_map_ready(maps[0]) && !mapper_map_ready(maps[1])) {
        mapper_device_poll(source, 10);
        mapper_device_poll(destination, 10);
    }

    return 0;
}

void wait_ready()
{
    while (!done && !(mapper_device_ready(source)
                      && mapper_device_ready(destination))) {
        mapper_device_poll(source, 0);
        mapper_device_poll(destination, 0);
        usleep(500 * 1000);
    }
}

void loop()
{
    eprintf("Polling device..\n");
	int i = 0;
	float j=1;
	while ((!terminate || i < 50) && !done) {
        j=i;
        mapper_timetag_t now;
        mapper_device_now(source, &now);
        mapper_device_start_queue(source, now);
		mapper_device_poll(source, 0);
        eprintf("Updating signal %s to %f\n", mapper_signal_name(sendsig), j);
        mapper_signal_update(sendsig, &j, 0, now);
		mapper_signal_update(sendsig1, &j, 0, now);
		mapper_device_send_queue(sendsig->device, now);
		sent = sent+2;
        mapper_device_poll(destination, 100);
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
                        eprintf("testqueue.c: possible arguments "
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

    if (autoconnect && create_maps()) {
        eprintf("Error creating maps.\n");
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
