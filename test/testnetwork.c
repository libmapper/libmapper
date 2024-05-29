#include "../src/device.h"
#include "../src/graph.h"
#include <mapper/mapper.h>
#include <stdio.h>
#include <stdarg.h>
#include <math.h>
#include <string.h>
#ifdef WIN32
#include <io.h>
#else
#include <unistd.h>
#endif

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

int test_network(void)
{
    int error = 0, wait, len, port = 7777;
    mpr_type type;
    const void *val;

    graph = mpr_graph_new(0);
    if (!graph) {
        eprintf("Error creating graph structure.\n");
        return 1;
    }

    mpr_graph_set_interface(graph, "lo0");

    eprintf("trying port %d... ", port);
    while (mpr_graph_set_address(graph, "224.0.1.4", port)) {
        eprintf("error!\ntrying port %d... ", ++port);
    }

    eprintf("Graph structure initialized.\n");

    dev = mpr_dev_new("testnetwork", graph);
    if (!dev) {
        eprintf("Error creating device structure.\n");
        return 1;
    }

    eprintf("Device structure initialized.\n");

    eprintf("Found interface %s has IP %s\n", mpr_graph_get_interface(graph),
            mpr_graph_get_address(graph));

    while (!mpr_dev_get_is_ready(dev)) {
        mpr_dev_poll(dev, 100);
    }

    mpr_obj_get_prop_by_idx((mpr_obj)dev, MPR_PROP_PORT, NULL, &len, &type, &val, 0);
    if (1 != len || MPR_INT32 != type) {
        eprintf("Error retrieving port.\n");
        return 1;
    }
    eprintf("Using port %d.\n", *(int*)val);
    eprintf("Using ordinal %d.\n", mpr_obj_get_prop_as_int32((mpr_obj)dev, MPR_PROP_ORDINAL, NULL));

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
    printf("\r..................................................Test %s\x1B[0m.\n",
           result ? "\x1B[31mFAILED" : "\x1B[32mPASSED");
    return result;
}
