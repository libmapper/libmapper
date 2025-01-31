#include <mapper/mapper.h>
#include <stdio.h>
#include <stdarg.h>
#include <math.h>
#include <lo/lo.h>
#include <stdlib.h>
#ifdef WIN32
#include <io.h>
#else
#include <unistd.h>
#endif
#include <signal.h>
#include <string.h>

mpr_dev dev = 0;
mpr_graph graph;

int done = 0;

int verbose = 1;
int terminate = 0;
int shared_graph = 0;

const char *tags[] = {"session1", "session2", "session3"};

static void eprintf(const char *format, ...)
{
    va_list args;
    if (!verbose)
        return;
    va_start(args, format);
    vprintf(format, args);
    va_end(args);
}

/*! Creation of a local device. */
int setup_dev(mpr_graph g, const char *iface)
{
    double mnd=0, mxd=10;
    mpr_sig sig;
    mpr_list l;

    dev = mpr_dev_new("testlist", g);
    if (!dev)
        goto error;
    if (iface)
        mpr_graph_set_interface(mpr_obj_get_graph(dev), iface);
    eprintf("Device created using interface %s.\n",
            mpr_graph_get_interface(mpr_obj_get_graph(dev)));

    sig = mpr_sig_new(dev, MPR_DIR_IN, "sig1", 1, MPR_DBL, "Hz", &mnd, &mxd, NULL, NULL, 0);

    sig = mpr_sig_new(dev, MPR_DIR_OUT, "sig2", 1, MPR_DBL, "Hz", &mnd, &mxd, NULL, NULL, 0);
    mpr_obj_set_prop((mpr_obj)sig, MPR_PROP_EXTRA, "session", 1, MPR_STR, tags[0], 1);

    sig = mpr_sig_new(dev, MPR_DIR_IN, "sig3", 1, MPR_DBL, "Hz", &mnd, &mxd, NULL, NULL, 0);
    mpr_obj_set_prop((mpr_obj)sig, MPR_PROP_EXTRA, "session", 1, MPR_STR, tags[1], 1);

    sig = mpr_sig_new(dev, MPR_DIR_OUT, "sig4", 1, MPR_DBL, "Hz", &mnd, &mxd, NULL, NULL, 0);
    mpr_obj_set_prop((mpr_obj)sig, MPR_PROP_EXTRA, "session", 2, MPR_STR, tags, 1);

    sig = mpr_sig_new(dev, MPR_DIR_IN, "sig5", 1, MPR_DBL, "Hz", &mnd, &mxd, NULL, NULL, 0);
    mpr_obj_set_prop((mpr_obj)sig, MPR_PROP_EXTRA, "session", 3, MPR_STR, tags, 1);

    eprintf("Signals registered.\n");

    l = mpr_dev_get_sigs(dev, MPR_DIR_ANY);
    eprintf("Number of signals: %d\n", mpr_list_get_size(l));
    mpr_list_free(l);

    return 0;

  error:
    return 1;
}

void cleanup_dev(void)
{
    if (dev) {
        eprintf("Freeing device.. ");
        fflush(stdout);
        mpr_dev_free(dev);
        eprintf("ok\n");
    }
}

int wait_for_registration_and_sync(void)
{
    int i = 10;
    while (!done && !mpr_dev_get_is_ready(dev)) {
        mpr_dev_poll(dev, 10);
        mpr_graph_poll(graph, 10);
    }
    /* wait some more for graph sync */
    while (!done && i-- > 0) {
        mpr_dev_poll(dev, 10);
        mpr_graph_poll(graph, 10);
    }
    return done;
}

int filter(mpr_graph graph, mpr_prop prop, const char *key, int len, mpr_type type, void *value,
           mpr_op op, int expected)
{
    mpr_list list = mpr_graph_get_list(graph, MPR_SIG);
    int size;

    /* also filter by device to exclude other libmapper-enabled programs that may be running */
    list = mpr_list_filter(list, MPR_PROP_DEV, NULL, 1, MPR_DEV, dev, MPR_OP_EQ);

    /* filter using function args */
    list = mpr_list_filter(list, prop, key, len, type, value, op);
    size = mpr_list_get_size(list);

    if (verbose) {
        printf("found %d matching result%s... ", size, size == 1 ? "" : "s");
        if (size != expected)
            printf("ERROR! Expected %d\n", expected);
        else
            printf("OK\n");
        while (list) {
            printf("  ");
            mpr_obj_print(*list, 1);
            printf("\n");
            list = mpr_list_get_next(list);
        }
    }

    if (list)
        mpr_list_free(list);

    return size != expected;
}

void segv(int sig)
{
    printf("\x1B[31m(SEGV)\n\x1B[0m");
    exit(1);
}

void ctrlc(int sig)
{
    done = 1;
}

int main(int argc, char ** argv)
{
    int i, j, result = 0;
    char *iface = 0;

    /* process flags for -v verbose, -t terminate, -h help */
    for (i = 1; i < argc; i++) {
        if (argv[i] && argv[i][0] == '-') {
            int len = strlen(argv[i]);
            for (j = 1; j < len; j++) {
                switch (argv[i][j]) {
                    case 'h':
                        printf("testlist.c: possible arguments "
                               "-q quiet (suppress output), "
                               "-s shared (use one mpr_graph only), "
                               "-h help, "
                               "--iface network interface\n");
                        return 1;
                        break;
                    case 'q':
                        verbose = 0;
                        break;
                    case 's':
                        shared_graph = 1;
                        break;
                    case '-':
                        if (strcmp(argv[i], "--iface")==0 && argc>i+1) {
                            i++;
                            iface = argv[i];
                            j = len;
                        }
                        break;
                    default:
                        break;
                }
            }
        }
    }

    signal(SIGSEGV, segv);
    signal(SIGINT, ctrlc);

    graph = mpr_graph_new(MPR_OBJ);

    if (setup_dev(shared_graph ? graph : NULL, iface)) {
        eprintf("Error initializing device.\n");
        result = 1;
        goto done;
    }

    if (wait_for_registration_and_sync()) {
        eprintf("Device registration aborted.\n");
        result = 1;
        goto done;
    }

    result = (   filter(graph, 0, "session", 0, 0,       0,              MPR_OP_NEX,             1)
              || filter(graph, 0, "session", 0, 0,       0,              MPR_OP_EX,              4)
              || filter(graph, 0, "session", 1, MPR_STR, (void*)tags[0], MPR_OP_EQ,              1)
              || filter(graph, 0, "session", 1, MPR_STR, (void*)tags[0], MPR_OP_EQ | MPR_OP_ANY, 3));

    /* TODO: add this test once vector filter matching is implemented */
    /* result |= filter(graph, 0, "session", 2, MPR_STR, (void*)tags,    MPR_OP_EQ,              1); */

  done:
    cleanup_dev();
    if (graph)
        mpr_graph_free(graph);
    printf("..................................................");
    printf("Test %s\x1B[0m.\n", result ? "\x1B[31mFAILED" : "\x1B[32mPASSED");
    return result;
}
