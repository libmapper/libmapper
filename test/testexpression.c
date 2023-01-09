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
#include <signal.h>
#include <stdlib.h>

int verbose = 1;
int terminate = 0;
int autoconnect = 1;
int shared_graph = 0;
int done = 0;
int period = 100;

mpr_dev src = 0;
mpr_dev dst = 0;
mpr_sig sendsig = 0;
mpr_sig recvsig = 0;
mpr_map map = 0;

int sent = 0;
int received = 0;
int matched = 0;
int addend[2] = {1, 2};

float expected_val[2];
double expected_time;

static void eprintf(const char *format, ...)
{
    va_list args;
    if (!verbose)
        return;
    va_start(args, format);
    vprintf(format, args);
    va_end(args);
}

int setup_src(mpr_graph g, const char *iface)
{
    float mn=0, mx=1;
    mpr_list l;

    src = mpr_dev_new("testexpression-send", g);
    if (!src)
        goto error;
    if (iface)
        mpr_graph_set_interface(mpr_obj_get_graph(src), iface);
    eprintf("source created using interface %s.\n",
            mpr_graph_get_interface(mpr_obj_get_graph(src)));

    sendsig = mpr_sig_new(src, MPR_DIR_OUT, "outsig", 1, MPR_FLT, NULL, &mn, &mx, NULL, NULL, 0);

    eprintf("Output signal 'outsig' registered.\n");
    l = mpr_dev_get_sigs(src, MPR_DIR_OUT);
    eprintf("Number of outputs: %d\n", mpr_list_get_size(l));
    mpr_list_free(l);
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

void handler(mpr_sig sig, mpr_sig_evt event, mpr_id instance, int length,
             mpr_type type, const void *value, mpr_time t)
{
    if (!value || length != 2)
        return;
    float *fvalue = (float*)value;
    eprintf("handler: Got value [%f, %f], time %f\n", fvalue[0], fvalue[1], mpr_time_as_dbl(t));
    if (fvalue[0] != expected_val[0] || fvalue[1] != expected_val[1])
        eprintf("  error: expected value [%f, %f]\n", expected_val[0], expected_val[1]);
    else
        ++matched;
    ++received;
}

int setup_dst(mpr_graph g, const char *iface)
{
    float mn=0, mx=1;
    mpr_list l;

    dst = mpr_dev_new("testexpression-recv", g);
    if (!dst)
        goto error;
    if (iface)
        mpr_graph_set_interface(mpr_obj_get_graph(dst), iface);
    eprintf("destination created using interface %s.\n",
            mpr_graph_get_interface(mpr_obj_get_graph(dst)));

    recvsig = mpr_sig_new(dst, MPR_DIR_IN, "insig", 2, MPR_FLT, NULL,
                          NULL, NULL, NULL, handler, MPR_SIG_UPDATE);
    mpr_obj_set_prop((mpr_obj)recvsig, MPR_PROP_MIN, NULL, 1, MPR_FLT, &mn, 0);
    mpr_obj_set_prop((mpr_obj)recvsig, MPR_PROP_MAX, NULL, 1, MPR_FLT, &mx, 0);

    eprintf("Input signal 'insig' registered.\n");
    l = mpr_dev_get_sigs(dst, MPR_DIR_IN);
    eprintf("Number of inputs: %d\n", mpr_list_get_size(l));
    mpr_list_free(l);
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

int setup_maps()
{
    map = mpr_map_new(1, &sendsig, 1, &recvsig);
    mpr_obj_set_prop(map, MPR_PROP_EXPR, NULL, 1, MPR_STR, "foo=[1,2];y=x*10+foo", 1);
    mpr_obj_push(map);

    /* wait until mapping has been established */
    while (!done && !mpr_map_get_is_ready(map)) {
        mpr_dev_poll(src, 10);
        mpr_dev_poll(dst, 10);
    }

    return 0;
}

void wait_ready()
{
    while (!done && !(mpr_dev_get_is_ready(src) && mpr_dev_get_is_ready(dst))) {
        mpr_dev_poll(src, 25);
        mpr_dev_poll(dst, 25);
    }
}

void loop()
{
    int i = 0;
    mpr_time t;

    eprintf("Polling device..\n");
    while ((!terminate || i < 50) && !done) {
        float val = i * 1.0f;
        expected_val[0] = val * 10 + addend[0];
        expected_val[1] = val * 10 + addend[1];
        t = mpr_dev_get_time(src);
        expected_time = mpr_time_as_dbl(t);
        eprintf("Updating output signal to %f at time %f\n", (i * 1.0f),
                mpr_time_as_dbl(t));
        mpr_sig_set_value(sendsig, 0, 1, MPR_FLT, &val);
        sent++;
        mpr_dev_poll(src, 0);
        mpr_dev_poll(dst, period);
        i++;

        if (!verbose) {
            printf("\r  Sent: %4i, Received: %4i   ", sent, received);
            fflush(stdout);
        }
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
                        printf("testexpression.c: possible arguments "
                               "-f fast (execute quickly), "
                               "-q quiet (suppress output), "
                               "-t terminate automatically, "
                               "-s shared (use one mpr_graph only), "
                               "-h help, "
                               "--iface network interface\n");
                        return 1;
                        break;
                    case 'f':
                        period = 1;
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
        eprintf("Done initializing source.\n");
        result = 1;
        goto done;
    }

    wait_ready();

    if (autoconnect && setup_maps()) {
        eprintf("Error setting map.\n");
        result = 1;
        goto done;
    }

    loop();

    eprintf("Modifying expression variable");
    addend[0] = 1000;
    addend[1] = 234;
    mpr_obj_set_prop(map, MPR_PROP_EXTRA, "var@foo", 2, MPR_INT32, addend, 1);
    mpr_obj_push(map);
    /* wait for change to take effect */
    mpr_dev_poll(dst, 100);
    mpr_dev_poll(src, 100);

    loop();

    eprintf("Modifying expression to check that variable is overwritten");
    addend[0] = addend[1] = 20;
    mpr_obj_set_prop(map, MPR_PROP_EXPR, NULL, 1, MPR_STR, "foo=[20,20];y=x*10+foo", 1);
    mpr_obj_push(map);
    /* wait for change to take effect */
    mpr_dev_poll(dst, 100);
    mpr_dev_poll(src, 100);

    loop();

    if (autoconnect && (!received || sent != matched)) {
        eprintf("Mismatch between sent and received/matched messages.\n");
        eprintf("Updated value %d time%s, but received %d and matched %d of them.\n",
                sent, sent == 1 ? "" : "s", received, matched);
        result = 1;
    }

  done:
    cleanup_dst();
    cleanup_src();
    if (g) mpr_graph_free(g);
    printf("...................Test %s\x1B[0m.\n",
           result ? "\x1B[31mFAILED" : "\x1B[32mPASSED");
    return result;
}
