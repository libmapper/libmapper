
#include "../src/mapper_internal.h"
#include <mapper/mapper.h>
#include <stdio.h>
#include <math.h>
#include <unistd.h>
#include <signal.h>
#include <string.h>

#define eprintf(format, ...) do {               \
    if (verbose)                                \
        fprintf(stdout, format, ##__VA_ARGS__); \
} while(0)

int verbose = 1;
int terminate = 0;
int done = 0;

int test_controller()
{
    mapper_device md = mdev_new("tester", 0, 0);
    if (!md)
        goto error;
    eprintf("Mapper device created.\n");

    while (!mdev_ready(md)) {
        mdev_poll(md, 100);
    }

    float mn=0, mx=1;
    mapper_signal sig = 
        mdev_add_output(md, "/testsig", 1, 'f', 0, &mn, &mx);

    eprintf("Output signal /testsig registered.\n");

    eprintf("Number of outputs: %d\n", mdev_num_outputs(md));

    const char *host = "localhost";
    int admin_port = 9000, data_port = 9001;
    mapper_link l = mapper_router_add_link(md->router, host, admin_port,
                                           data_port, "DESTINATION");
    eprintf("Link to %s:%d added.\n", host, data_port);

    mapper_router_add_connection(md->router, l, sig, "/mapped1",
                                 'f', 1, DI_OUTGOING);
    mapper_router_add_connection(md->router, l, sig, "/mapped2",
                                 'f', 1, DI_OUTGOING);

    eprintf("Polling device..\n");
    int i = 0;
    while ((!terminate || i < 50) && !done) {
        mdev_poll(md, 100);
        eprintf("Updating signal %s to %f\n",
                sig->props.name, (i * 1.0f));
        msig_update_float(sig, (i * 1.0f));
        i++;

        if (!verbose) {
            printf("\r  Sent: %4i", i);
            fflush(stdout);
        }
    }

    mapper_router_remove_link(md->router, l);
    eprintf("Link removed.\n");

    mdev_free(md);
    return 0;

  error:
    if (md)
        mdev_free(md);
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
                        eprintf("testsend.c: possible arguments"
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

    if (test_controller()) {
        result = 1;
        goto done;
    }

done:
    printf("                   Test %s.\n", result ? "FAILED" : "PASSED");
    return result;
}
