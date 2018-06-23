
#include "../src/mapper_internal.h"
#include <mapper/mapper.h>
#include <stdio.h>
#include <math.h>
#include <string.h>
#include <unistd.h>

#define eprintf(format, ...) do {               \
    if (verbose)                                \
        fprintf(stdout, format, ##__VA_ARGS__); \
} while(0)

mapper_graph graph = NULL;
mapper_device dev = NULL;

int verbose = 1;

int test_network()
{
    int error = 0, wait;

    graph = mapper_graph_new(0);
    if (!graph) {
        eprintf("Error creating graph structure.\n");
        return 1;
    }

    mapper_graph_set_interface(graph, "lo0");
    mapper_graph_set_multicast_addr(graph, "224.0.1.4", 7777);

    eprintf("Graph structure initialized.\n");

    dev = mapper_device_new("testnetwork", graph);
    if (!dev) {
        eprintf("Error creating device structure.\n");
        return 1;
    }

    eprintf("Device structure initialized.\n");

    eprintf("Found interface %s has IP %s\n", graph->net.iface.name,
           inet_ntoa(graph->net.iface.addr));

    while (!dev->local->registered) {
        mapper_device_poll(dev, 100);
    }

    int len;
    mapper_type type;
    const void *val;
    mapper_object_get_prop_by_index((mapper_object)dev, MAPPER_PROP_PORT, NULL,
                                    &len, &type, &val);
    if (1 != len || MAPPER_INT32 != type) {
        eprintf("Error retrieving port.\n");
        return 1;
    }
    eprintf("Using port %d.\n", *(int*)val);
    eprintf("Allocated ordinal %d.\n", dev->local->ordinal.val);

    eprintf("Delaying for 5 seconds..\n");
    wait = 50;
    while (wait-- > 0) {
        mapper_device_poll(dev, 100);
        if (!verbose) {
            printf(".");
            fflush(stdout);
        }
    }

    mapper_device_free(dev);
    eprintf("Device structure freed.\n");
    mapper_graph_free(graph);
    eprintf("Graph structure freed.\n");

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
                        printf("testgraph.c: possible arguments "
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
    printf("\r..................................................Test %s\x1B[0m.\n",
           result ? "\x1B[31mFAILED" : "\x1B[32mPASSED");
    return result;
}
