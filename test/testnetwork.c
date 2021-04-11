#include "../src/mapper_internal.h"
#include <mapper/mapper.h>
#include <stdio.h>
#include <stdarg.h>
#include <math.h>
#include <string.h>
#include <unistd.h>

mpr_graph graph = NULL;
mpr_dev dev = NULL;

int verbose = 1;

static void eprintf(const char *format, ...)
{
    va_list args;
    if (!verbose)
        return;
    va_start(args, format);
    vprintf(format, args);
    va_end(args);
}

int test_network()
{
    int error = 0, wait, len;
    mpr_type type;
    const void *val;

    graph = mpr_graph_new(0);
    if (!graph) {
        eprintf("Error creating graph structure.\n");
        return 1;
    }

    mpr_graph_set_interface(graph, "lo0");
    mpr_graph_set_address(graph, "224.0.1.4", 7777);

    eprintf("Graph structure initialized.\n");

    dev = mpr_dev_new("testnetwork", graph);
    if (!dev) {
        eprintf("Error creating device structure.\n");
        return 1;
    }

    eprintf("Device structure initialized.\n");

    eprintf("Found interface %s has IP %s\n", graph->net.iface.name,
           inet_ntoa(graph->net.iface.addr));

    while (!((mpr_local_dev)dev)->registered) {
        mpr_dev_poll(dev, 100);
    }

    mpr_obj_get_prop_by_idx((mpr_obj)dev, MPR_PROP_PORT, NULL, &len, &type, &val, 0);
    if (1 != len || MPR_INT32 != type) {
        eprintf("Error retrieving port.\n");
        return 1;
    }
    eprintf("Using port %d.\n", *(int*)val);
    eprintf("Allocated ordinal %d.\n", ((mpr_local_dev)dev)->ordinal_allocator.val);

    eprintf("Delaying for 5 seconds..\n");
    wait = 50;
    while (wait-- > 0) {
        mpr_dev_poll(dev, 100);
        if (!verbose) {
            printf(".");
            fflush(stdout);
        }
    }

    mpr_dev_free(dev);
    eprintf("Device structure freed.\n");
    mpr_graph_free(graph);
    eprintf("Graph structure freed.\n");

    return error;
}

int main(int argc, char **argv)
{
    int i, j, result = 0;

    /* process flags for -v verbose, -h help */
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
