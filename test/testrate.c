#include "../src/mpr_signal.h"
#include <mapper/mapper.h>
#include <stdio.h>
#include <stdarg.h>
#include <math.h>
#include <lo/lo.h>
#ifdef WIN32
#include <io.h>
#else
#include <unistd.h>
#endif
#include <signal.h>
#include <stdlib.h>
#include <string.h>

int verbose = 1;
int terminate = 0;
int shared_graph = 0;
int autoconnect = 1;
int done = 0;
int polltime = 100;

mpr_dev src = 0;
mpr_dev dst = 0;
mpr_sig sendsig = 0;
mpr_sig recvsig = 0;

int sent = 0;
int received = 0;

float expected;
float period_send;
float jitter_send;

static void eprintf(const char *format, ...)
{
    va_list args;
    if (!verbose)
        return;
    va_start(args, format);
    vprintf(format, args);
    va_end(args);
}

/*! Creation of a local source. */
int setup_src(mpr_graph g, const char *iface)
{
    src = mpr_dev_new("testrate-send", g);
    if (!src)
        goto error;
    if (iface)
        mpr_graph_set_interface(mpr_obj_get_graph((mpr_obj)src), iface);
    eprintf("source created using interface %s.\n",
            mpr_graph_get_interface(mpr_obj_get_graph((mpr_obj)src)));

    sendsig = mpr_sig_new(src, MPR_DIR_OUT, "outsig", 1, MPR_FLT, "Hz", NULL, NULL, NULL, NULL, 0);

    eprintf("Output signal 'outsig' registered.\n");

    return 0;

  error:
    return 1;
}

void cleanup_src(void)
{
    if (src) {
        eprintf("Freeing source.. ");
        fflush(stdout);
        mpr_dev_free(src);
        eprintf("ok\n");
    }
}

void handler(mpr_sig sig, mpr_sig_evt event, mpr_id instance, int len,
             mpr_type type, const void *val, mpr_time t)
{
    const char *name;
    float period_recv = mpr_obj_get_prop_as_flt((mpr_obj)sig, MPR_PROP_PERIOD, NULL);
    float period_diff = period_recv - period_send;

    if (!val)
        return;

    name = mpr_obj_get_prop_as_str((mpr_obj)sig, MPR_PROP_NAME, NULL);
    if (verbose) {
        float jitter_recv = mpr_obj_get_prop_as_flt((mpr_obj)sig, MPR_PROP_JITTER, NULL);
        printf("%s rec'ved %g (period: %gms, jitter: %gms, diff: %gms)\n", name, *(float*)val,
               period_recv * 1000, jitter_recv * 1000, period_diff * 1000);
    }

    if (*(float*)val == expected && (*(float*)val <= 2.f || fabsf(period_diff) < 0.01))
        ++received;
    else
        eprintf("  expected value %g, period %dms\n", expected, polltime);
}

/*! Creation of a local destination. */
int setup_dst(mpr_graph g, const char *iface)
{
    float rate;

    dst = mpr_dev_new("testrate-recv", g);
    if (!dst)
        goto error;
    if (iface)
        mpr_graph_set_interface(mpr_obj_get_graph((mpr_obj)dst), iface);
    eprintf("destination created using interface %s.\n",
            mpr_graph_get_interface(mpr_obj_get_graph((mpr_obj)dst)));

    recvsig = mpr_sig_new(dst, MPR_DIR_IN, "insig", 1, MPR_FLT, NULL,
                          NULL, NULL, NULL, handler, MPR_SIG_UPDATE);

    /* This signal is expected to be updated at 100 Hz */
    rate = 100.f;
    mpr_obj_set_prop((mpr_obj)recvsig, MPR_PROP_RATE, NULL, 1, MPR_FLT, &rate, 1);

    eprintf("Input signal 'insig' registered.\n");

    return 0;

  error:
    return 1;
}

void cleanup_dst(void)
{
    if (dst) {
        eprintf("Freeing destination.. ");
        fflush(stdout);
        mpr_dev_free(dst);
        eprintf("ok\n");
    }
}

int wait_ready(void)
{
    while (!done && !(mpr_dev_get_is_ready(src) && mpr_dev_get_is_ready(dst))) {
        mpr_dev_poll(src, 25);
        mpr_dev_poll(dst, 25);
    }
    return done;
}

int setup_maps(void)
{
    int i = 0;
    mpr_map map = mpr_map_new(1, &sendsig, 1, &recvsig);

    /* TODO: set up update bundling as map property */

    mpr_obj_push((mpr_obj)map);

    i = 0;
    /* wait until mapping has been established */
    while (!done && !mpr_map_get_is_ready(map)) {
        mpr_dev_poll(src, 10);
        mpr_dev_poll(dst, 10);
        if (i++ > 100)
            return 1;
    }

    return 0;
}

void loop(void)
{
    int i = 0, thresh = 0;
    float fval;

    while ((!terminate || i < 50) && !done) {
        fval = (float)i;
        mpr_sig_set_value(sendsig, 0, 1, MPR_FLT, &fval);
        period_send = mpr_obj_get_prop_as_flt((mpr_obj)sendsig, MPR_PROP_PERIOD, NULL);
        jitter_send = mpr_obj_get_prop_as_flt((mpr_obj)sendsig, MPR_PROP_JITTER, NULL);
        eprintf("Sending update (period: %gms; jitter: %g)\n", period_send * 1000, jitter_send);
        ++sent;
        expected = fval;
        mpr_dev_poll(src, 0);
        mpr_dev_poll(dst, polltime);
        ++i;

        if (!verbose) {
            printf("\r  Sent: %4i, Received: %4i   ", sent, received);
            fflush(stdout);
        }

        ++thresh;
        if (thresh > 100)
            thresh = -100;
    }
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

int main(int argc, char **argv)
{
    int i, j, result = 0;
    char *iface = 0;
    mpr_graph g;

    /* process flags for -v verbose, -t terminate, -h help */
    for (i = 1; i < argc; i++) {
        if (argv[i] && argv[i][0] == '-') {
            int len = strlen(argv[i]);
            for (j = 1; j < len; j++) {
                switch (argv[i][j]) {
                    case 'h':
                        eprintf("testrate.c: possible arguments "
                                "-f fast (execute quickly), "
                                "-q quiet (suppress output), "
                                "-t terminate automatically, "
                                "-s shared (use one mpr_graph only), "
                                "-h help, "
                                "--iface network interface\n");
                        return 1;
                        break;
                    case 'f':
                        polltime = 1;
                        break;
                    case 'q':
                        verbose = 0;
                        break;
                    case 't':
                        terminate = 1;
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

    g = shared_graph ? mpr_graph_new(0) : 0;

    if (setup_dst(g, iface)) {
        eprintf("Error initializing destination.\n");
        result = 1;
        goto done;
    }

    if (setup_src(g, iface)) {
        eprintf("Error initializing source.\n");
        result = 1;
        goto done;
    }

    if (wait_ready()) {
        eprintf("Device registration aborted.\n");
        result = 1;
        goto done;
    }

    if (autoconnect && setup_maps()) {
        eprintf("Error connecting signals.\n");
        result = 1;
        goto done;
    }

    loop();

    if (received != sent) {
        eprintf("Not all sent messages were received.\n");
        eprintf("Updated value %d time%s, but received %d of them.\n",
                sent, sent == 1 ? "" : "s", received);
        result = 1;
    }

  done:
    cleanup_dst();
    cleanup_src();
    if (g) mpr_graph_free(g);
    printf("\r..................................................Test %s\x1B[0m.\n",
           result ? "\x1B[31mFAILED" : "\x1B[32mPASSED");
    return result;
}
