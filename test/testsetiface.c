#include <mapper/mapper.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <math.h>
#ifdef WIN32
#include <io.h>
#else
#include <unistd.h>
#endif
#include <signal.h>
#include <string.h>

int verbose = 1;
int done = 0;

const char *iface_names[] = { "en0", "obviously wrong", "vEthernet (nat)" };
#define NUM_IFACE_NAMES 3

mpr_dev devs[NUM_IFACE_NAMES];

static void eprintf(const char *format, ...)
{
    va_list args;
    if (!verbose)
        return;
    va_start(args, format);
    vprintf(format, args);
    va_end(args);
}

mpr_dev setup_dev(const char *iface)
{
    mpr_dev dev = mpr_dev_new("testiface", NULL);
    if (!dev)
        goto error;

    eprintf("device created...\n");

    if (iface) {
        eprintf("  trying to set iface to '%s'...\n", iface);
        mpr_graph_set_interface(mpr_obj_get_graph(dev), iface);
    }

    eprintf("  using interface %s.\n", mpr_graph_get_interface(mpr_obj_get_graph(dev)));

    return dev;

  error:
    return NULL;
}

void cleanup_dev(mpr_dev dev)
{
    if (dev) {
        eprintf("Freeing device '%s'.. ", mpr_obj_get_prop_as_str((mpr_obj)dev, MPR_PROP_NAME, NULL));
        fflush(stdout);
        mpr_dev_free(dev);
        eprintf("ok\n");
    }
}

void wait_ready(void)
{
    while (!done) {
        int i, ready = 1;
        for (i = 0; i < NUM_IFACE_NAMES; i++) {
            mpr_dev dev = devs[i];
            if (!dev)
                continue;
            mpr_dev_poll(dev, 25);
            ready &= mpr_dev_get_is_ready(dev);
        }
        if (ready)
            break;
    }
}

void segv(int sig)
{
    printf("\x1B[31m(SEGV)\n\x1B[0m");
    exit(1);
}

void ctrlc(int signal)
{
    done = 1;
}

int main(int argc, char **argv)
{
    int i, j, result = 0;

    /* process flags for -v verbose, -t terminate, -h help */
    for (i = 1; i < argc; i++) {
        if (argv[i] && argv[i][0] == '-') {
            int len = strlen(argv[i]);
            for (j = 1; j < len; j++) {
                switch (argv[i][j]) {
                    case 'h':
                        printf("testiface.c: possible arguments "
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

    signal(SIGSEGV, segv);
    signal(SIGINT, ctrlc);

    for (i = 0; i < NUM_IFACE_NAMES; i++) {
        devs[i] = setup_dev(iface_names[i]);
    }

    wait_ready();

    for (i = 0; i < NUM_IFACE_NAMES; i++)
        cleanup_dev(devs[i]);
    printf("..................................................");
    printf("Test %s\x1B[0m.\n", result ? "\x1B[31mFAILED" : "\x1B[32mPASSED");
    return result;
}
