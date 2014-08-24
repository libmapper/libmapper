
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
int done = 0;

int sent = 0;
int received = 0;
lo_address a = NULL;

void handler(mapper_signal sig, mapper_db_signal props,
             int instance_id, void *value, int count,
             mapper_timetag_t *timetag)
{
    if (value) {
        eprintf("handler: Got %f\n", (*(float*)value));
    }
    received++;
    if (!verbose) {
        printf("\r  Received: %4i", received);
        fflush(stdout);
    }
}

int test_recv()
{
    mapper_device md = mdev_new("synth", 0, 0);
    if (!md)
        goto error;
    eprintf("Mapper device created.\n");

    float mn=0, mx=1;
    mapper_signal sig = 
        mdev_add_input(md, "/mapped1", 1, 'f', 0, &mn, &mx, handler, 0);

    eprintf("Input signal /mapped1 registered.\n");

    eprintf("Number of inputs: %d\n", mdev_num_inputs(md));

    eprintf("Waiting for port/ordinal allocation..\n");
    int i;
    for (i = 0; i < 10; i++) {
        mdev_poll(md, 500);
        if (mdev_ready(md))
            break;
        usleep(500 * 1000);
    }
    if (i >= 10) {
        eprintf("Timed out waiting for signal name.\n");
        goto error;
    }
	
	char port[10];
	sprintf(port, "%i", md->props.port);
	eprintf("using port = %s\n", port);
	
	a = lo_address_new("localhost", port);
    if (!a) {
        eprintf("Error creating lo_address for test.\n");
        goto error;
    }

    eprintf("Polling device..\n");
    i = 0;
    while ((!terminate || i < 50) && !done) {
        lo_send(a, sig->props.name, "f", (float) i);
		eprintf("Updating signal %s to %f\n", sig->props.name, (float) i);
        sent++;
        mdev_poll(md, 100);
        i++;
    }

    if (sent != received) {
        eprintf("Not all sent values were received.\n");
        eprintf("Sent %d values, but %d received.\n", sent, received);
        goto error;
    }
    if (sent == 0) {
        eprintf("Unable to send any values.\n");
        goto error;
    }
    eprintf("Sent and received %d values.\n", sent);

    mdev_free(md);
    lo_address_free(a);
    return 0;

  error:
    if (md)
        mdev_free(md);
    if (a)
        lo_address_free(a);
    return 1;
}

void ctrlc(int signal)
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
                        eprintf("testrecv.c: possible arguments"
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

    if (test_recv()) {
        result = 1;
        goto done;
    }

done:
    printf("               Test %s.\n", result ? "FAILED" : "PASSED");
    return result;
}
