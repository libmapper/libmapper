#include "../src/mapper_internal.h"
#include <mapper/mapper.h>
#include <stdio.h>
#include <stdarg.h>
#include <math.h>
#include <lo/lo.h>
#include <unistd.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>

int verbose = 1;
int terminate = 0;
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
float period;

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
int setup_src(const char *iface)
{
    float mn=0, mx=10;

    src = mpr_dev_new("testrate-send", 0);
    if (!src)
        goto error;
    if (iface)
        mpr_graph_set_interface(mpr_obj_get_graph((mpr_obj)src), iface);
    eprintf("source created using interface %s.\n",
            mpr_graph_get_interface(mpr_obj_get_graph((mpr_obj)src)));

    sendsig = mpr_sig_new(src, MPR_DIR_OUT, "outsig", 1, MPR_FLT, "Hz", &mn, &mx, NULL, NULL, 0);

    eprintf("Output signal 'outsig' registered.\n");

    return 0;

  error:
    return 1;
}

void cleanup_src()
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

    if (!val)
        return;

    name = mpr_obj_get_prop_as_str((mpr_obj)sig, MPR_PROP_NAME, NULL);
    eprintf("%s rec'ved %f (period: %f, jitter: %f, diff: %f)\n", name,
            *(float*)val, sig->period, sig->jitter, period - sig->period);
    if (*(float*)val == expected)
        ++received;
    else
        eprintf("  expected %f\n", expected);
}

/*! Creation of a local destination. */
int setup_dst(const char *iface)
{
    float mn=0, mx=1, rate;

    dst = mpr_dev_new("testrate-recv", 0);
    if (!dst)
        goto error;
    if (iface)
        mpr_graph_set_interface(mpr_obj_get_graph((mpr_obj)dst), iface);
    eprintf("destination created using interface %s.\n",
            mpr_graph_get_interface(mpr_obj_get_graph((mpr_obj)dst)));

    recvsig = mpr_sig_new(dst, MPR_DIR_IN, "insig", 1, MPR_FLT, NULL,
                          &mn, &mx, NULL, handler, MPR_SIG_UPDATE);

    /* This signal is expected to be updated at 100 Hz */
    rate = 100.f;
    mpr_obj_set_prop((mpr_obj)recvsig, MPR_PROP_RATE, NULL, 1, MPR_FLT, &rate, 1);

    eprintf("Input signal 'insig' registered.\n");

    return 0;

  error:
    return 1;
}

void cleanup_dst()
{
    if (dst) {
        eprintf("Freeing destination.. ");
        fflush(stdout);
        mpr_dev_free(dst);
        eprintf("ok\n");
    }
}

void wait_local_devs()
{
    while (!done && !(mpr_dev_get_is_ready(src) && mpr_dev_get_is_ready(dst))) {
        mpr_dev_poll(src, 25);
        mpr_dev_poll(dst, 25);
    }
}

int setup_maps()
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

void loop()
{
    int i = 0, thresh = 0;
    float fval;

    while ((!terminate || i < 50) && !done) {
        fval = (float)i;
        mpr_sig_set_value(sendsig, 0, 1, MPR_FLT, &fval);
        eprintf("Sending update (period: %f; jitter: %f)\n",
                sendsig->period, sendsig->jitter);
        period = sendsig->period;
        ++sent;
        expected = fval * 0.1f;
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

void ctrlc(int sig)
{
    done = 1;
}

int main(int argc, char **argv)
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
                        eprintf("testrate.c: possible arguments "
                                "-f fast (execute quickly), "
                                "-q quiet (suppress output), "
                                "-t terminate automatically, "
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
                    case '-':
                        if (strcmp(argv[i], "--iface")==0 && argc>i+1) {
                            i++;
                            iface = argv[i];
                            j = 1;
                        }
                        break;
                    default:
                        break;
                }
            }
        }
    }

    signal(SIGINT, ctrlc);

    if (setup_dst(iface)) {
        eprintf("Error initializing destination.\n");
        result = 1;
        goto done;
    }

    if (setup_src(iface)) {
        eprintf("Error initializing source.\n");
        result = 1;
        goto done;
    }

    wait_local_devs();

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
    printf("\r..................................................Test %s\x1B[0m.\n",
           result ? "\x1B[31mFAILED" : "\x1B[32mPASSED");
    return result;
}
