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

int current_expr_idx = -1;
int sent = 0;
int received = 0;
int matched = 0;

mpr_time t_last = {0, 0};
double expected = 0;

// to add:
// - start time in the future

const char *expr[] =
{
    /* schedule next periodic event from a repeating pattern (implicit start time) */
//    "period = 1; p=[1,.5,.5] * period; y = 1; next = next + p[i++];",
    "period = 1; p=[1,.5,.5] * period; y = i; next = next + p[i]; i = i + 1;",

    /* schedule next periodic event using a sinusoid */
    "period = 1; y = 1; next = next + (sin(next) + 1.1) * period;",

    /* schedule next periodic event using a ramp */
    "period = 1; y = a; a = (a + 0.1) % 1; next = next + a",

    /* schedule next periodic event (implicit start time) */
    "period = 1; y = 1; next = next + period;",

    /* schedule next periodic event (explicit start time) */
    "period = 1; y = 1; next = now + period - ((now - 10000) % period);",

    /* schedule next periodic event in the past */
    "period = 1; y = 1; next = now - 1;",

    /* schedule next periodic event from current time */
    "period = 1; y = 1; next = now + period;",
};
const int num_expr = sizeof(expr)/sizeof(expr[0]);

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
    mpr_list l;

    src = mpr_dev_new("test_map_timed-send", g);
    if (!src)
        goto error;
    if (iface)
        mpr_graph_set_interface(mpr_obj_get_graph(src), iface);
    eprintf("source created using interface %s.\n",
            mpr_graph_get_interface(mpr_obj_get_graph(src)));

    sendsig = mpr_sig_new(src, MPR_DIR_OUT, "outsig", 1, MPR_FLT, NULL, NULL, NULL, NULL, NULL, 0);

    eprintf("Output signal 'outsig' registered.\n");
    l = mpr_dev_get_sigs(src, MPR_DIR_OUT);
    eprintf("Number of outputs: %d\n", mpr_list_get_size(l));
    mpr_list_free(l);
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

void handler(mpr_sig sig, mpr_sig_evt event, mpr_id instance, int length,
             mpr_type type, const void *value, mpr_time t)
{
    eprintf("handler\n");
    /* check elapsed time */
    mpr_time_sub(&t, t_last);
//    double elapsed = mpr_time_as_dbl(t);
//    if (elapsed == expected)
        ++received;
    eprintf("  received: %d\n", received);
    t_last = t;
}

int setup_dst(mpr_graph g, const char *iface)
{
    mpr_list l;

    dst = mpr_dev_new("test_map_timed-recv", g);
    if (!dst)
        goto error;
    if (iface)
        mpr_graph_set_interface(mpr_obj_get_graph(dst), iface);
    eprintf("destination created using interface %s.\n",
            mpr_graph_get_interface(mpr_obj_get_graph(dst)));

    recvsig = mpr_sig_new(dst, MPR_DIR_IN, "insig", 2, MPR_FLT, NULL,
                          NULL, NULL, NULL, handler, MPR_SIG_UPDATE);

    eprintf("Input signal 'insig' registered.\n");
    l = mpr_dev_get_sigs(dst, MPR_DIR_IN);
    eprintf("Number of inputs: %d\n", mpr_list_get_size(l));
    mpr_list_free(l);
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

mpr_map setup_map(int expr_idx, mpr_loc loc)
{
    double period_sec;

//    if a map expression does not reference sources, default process location should be destination

    if (!map) {
        eprintf("creating map\n");
        map = mpr_map_new(1, &sendsig, 1, &recvsig);
    }

//    /* this can't work yet because we don't support sourceless maps */
//    mpr_map_new_from_string(expr, recvsig);

    if (expr_idx != current_expr_idx) {
        eprintf("setting map expression to '%s'\n", expr[expr_idx]);
        mpr_obj_set_prop(map, MPR_PROP_EXPR, NULL, 1, MPR_STR, expr[expr_idx], 1);
        current_expr_idx = expr_idx;
    }

    eprintf("setting map process location to '%s'\n", MPR_LOC_SRC == loc ? "source" : "destination");
    mpr_obj_set_prop(map, MPR_PROP_PROCESS_LOC, NULL, 1, MPR_INT32, &loc, 1);

    /* also set period variable according to program flags */
    period_sec = period * 0.1;
    eprintf("setting period variable to %g\n", period_sec);
    mpr_obj_set_prop((mpr_obj)map, MPR_PROP_EXTRA, "@var@period", 1, MPR_DBL, &period_sec, 1);

    mpr_obj_push(map);

    /* wait until mapping has been established */
    while (!done && !mpr_map_get_is_ready(map)) {
        mpr_dev_poll(src, 10);
        mpr_dev_poll(dst, 10);
    }

    return map;
}

void loop(void)
{
    received = 0;
    mpr_dev_start_polling(dst, 10);

    eprintf("Polling device..\n");
    while ((!terminate || received < 50) && !done) {

        mpr_dev_poll(src, 10);

        if (!verbose) {
            printf("\r  Received: %4i   ", received);
            fflush(stdout);
        }
    }

    mpr_dev_stop_polling(dst);
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
                        printf("test_map_timed.c: possible arguments "
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

    if (wait_ready()) {
        eprintf("Device registration aborted.\n");
        result = 1;
        goto done;
    }

    for (i = 0; i < num_expr; i++) {
        mpr_loc loc;
        eprintf("Loading map expression '%s'...\n", expr[i]);
        for (loc = MPR_LOC_SRC; loc <= MPR_LOC_DST; loc++) {
            if (done)
                goto done;
            eprintf("  Trying %s processing...\n", MPR_LOC_SRC == loc ? "source" : "destination");
            mpr_map map = setup_map(i, loc);
            if (!map) {
                eprintf("    Error setting up map\n");
                result = 1;
                goto done;
            }
            loop();

            goto done;
        }
    }

    if (autoconnect && (!received || received != matched)) {
        eprintf("Mismatch between sent and received/matched messages.\n");
        eprintf("Received %d values and matched %d of them.\n", received, matched);
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
