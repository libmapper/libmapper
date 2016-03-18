
#include "../src/mapper_internal.h"
#include <mapper/mapper.h>
#include <stdio.h>
#include <math.h>
#include <string.h>
#include <unistd.h>

#ifdef WIN32
#define usleep(x) Sleep(x/1000)
#endif

#define eprintf(format, ...) do {               \
    if (verbose)                                \
        fprintf(stdout, format, ##__VA_ARGS__); \
} while(0)

mapper_network net = NULL;
mapper_device dev = NULL;

int verbose = 1;

int test_network()
{
    int error = 0, wait;

    net = mapper_network_new(0, 0, 0);
    if (!net) {
        eprintf("Error creating network structure.\n");
        return 1;
    }

    eprintf("Network structure initialized.\n");

    dev = mapper_device_new("tester", 0, net);
    if (!dev) {
        eprintf("Error creating device structure.\n");
        return 1;
    }

    eprintf("Device structure initialized.\n");

    eprintf("Found interface %s has IP %s\n", net->interface_name,
           inet_ntoa(net->interface_ip));

    while (!dev->local->registered) {
        usleep(10000);
        mapper_network_poll(net);
    }

    eprintf("Using port %d.\n", mapper_device_port(dev));
    eprintf("Allocated ordinal %d.\n", dev->local->ordinal.value);

    eprintf("Delaying for 5 seconds..\n");
    wait = 50;
    while (wait-- > 0) {
        usleep(50000);
        mapper_network_poll(net);
        if (!verbose) {
            printf(".");
            fflush(stdout);
        }
    }

    mapper_device_free(dev);
    eprintf("Device structure freed.\n");
    mapper_network_free(net);
    eprintf("Network structure freed.\n");

    return error;
}

int main(int argc, char **argv)
{
    int i, j, result = 0;

    // process flags for -v verbose, -h help
    for (i = 1; i < argc; i++) {
        if (argv[i] && argv[i][0] == '-') {
            int len = strlen(argv[i]);
            for (j = 1; j < len; j++) {
                switch (argv[i][j]) {
                    case 'h':
                        printf("testnetwork.c: possible arguments "
                               "-q quiet (suppress output), "
                               "-h help\n");
                        return 1;
                        break;
                    case 'q':
                        verbose = 0;
                        break;
                    default:
                        break;
                }
            }
        }
    }

    result = test_network();

    if (result) {
        printf("Test FAILED.\n");
        return 1;
    }

    printf("Test PASSED.\n");
    return 0;
}
