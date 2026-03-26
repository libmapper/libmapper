#include <mapper/mapper.h>
#include <stdio.h>
#include <stdarg.h>
#include <math.h>
#include <ctype.h>
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

int current_config = -1;
int sent = 0;
int received = 0;
int matched = 0;

mpr_time t_last = {0, 0};
double expected = 0;

// to add:
// - start time in the future


/* schedule next periodic event from a repeating pattern (implicit start time) */
#define PATT "period = 1; p=[1,.5,.5] * period; y = 1; next = next + p[++i];"
//#define PATT "period = 1; p=[1,.5,.5] * period; y = i; next = next + p[i]; i = i + 1;"

/* schedule next periodic event using a sinusoid */
#define SINE "period = 1; y = 1; next = next + (sin(next) + 1.1) * period;"

/* schedule next periodic event using a ramp */
#define RAMP "period = 1; y = a; a = (a + 0.1) % 1; next = next + a;"

/* schedule next periodic event (implicit start time) */
#define NEXT "period = 1; y = 1; next = next + period;"

/* schedule next periodic event (explicit start time) */
#define START "period = 1; y = 1; next = now + period - ((now - 10000) % period);"

/* schedule next periodic event in the past */
#define PAST "period = 1; y = 1; next = now - 1;"

/* schedule next periodic event from current time */
#define NOW "period = 1; y = 1; next = now + period;"


typedef struct _test_config
{
    int test_id;
    const char *expr;
    mpr_loc process_loc;
} test_config;

test_config test_configs[] = {
    { 1, PATT, MPR_LOC_SRC },
//    { 2, PATT, MPR_LOC_DST },
//    { 3, SINE, MPR_LOC_SRC },
//    { 4, SINE, MPR_LOC_DST },
//    { 3, RAMP, MPR_LOC_SRC },
//    { 4, RAMP, MPR_LOC_DST },
//    { 3, NEXT, MPR_LOC_SRC },
//    { 4, NEXT, MPR_LOC_DST },
//    { 3, START, MPR_LOC_SRC },
//    { 4, START, MPR_LOC_DST },
//    { 3, PAST, MPR_LOC_SRC },
//    { 4, PAST, MPR_LOC_DST },
//    { 3, NOW, MPR_LOC_SRC },
//    { 4, NOW, MPR_LOC_DST },
};
const int NUM_TESTS = sizeof(test_configs)/sizeof(test_configs[0]);

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

    recvsig = mpr_sig_new(dst, MPR_DIR_IN, "insig", 1, MPR_FLT, NULL,
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

int run_test(test_config *config)
{
    double period_sec;
    mpr_map map;

    printf("Configuration %d: ", config->test_id);
    printf("; processing: %s", config->process_loc == MPR_LOC_SRC ? "SRC" : "DST");
    printf("; expression: %s\n", config->expr);

    map = mpr_map_new(1, &sendsig, 1, &recvsig);

    /* set process location */
    mpr_obj_set_prop((mpr_obj)map, MPR_PROP_PROCESS_LOC, NULL, 1, MPR_INT32, &config->process_loc, 1);

    /* set expression */
    mpr_obj_set_prop((mpr_obj)map, MPR_PROP_EXPR, NULL, 1, MPR_STR, config->expr, 1);

    /* also set period variable according to program flags */
    period_sec = period * 0.1;
    mpr_obj_set_prop((mpr_obj)map, MPR_PROP_EXTRA, "@var@period", 1, MPR_DBL, &period_sec, 1);

    mpr_obj_push(map);

    /* wait until mapping has been established */
    while (!done && !mpr_map_get_is_ready(map)) {
        mpr_dev_poll(src, 10);
        mpr_dev_poll(dst, 10);
    }

    mpr_obj_set_prop((mpr_obj)map, MPR_PROP_EXTRA, "@var@period", 1, MPR_DBL, &period_sec, 1);
    mpr_obj_push(map);

    loop();

    mpr_map_release(map);

    return 0;
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
    int i, j, result = 0, config_start = 0, config_stop = NUM_TESTS;
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
                               "--iface network interface, "
                               "--config specify a configuration to run (1-%d)\n", NUM_TESTS);
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
                            ++i;
                            iface = argv[i];
                            j = len;
                        }
                        else if (strcmp(argv[i], "--config")==0 && argc>i+1) {
                            i++;
                            config_start = atoi(argv[i]);
                            if (config_start > 0 && config_start <= NUM_TESTS) {
                                config_stop = config_start;
                                --config_start;
                            }
                            else {
                                printf("config start argument must be between 1 and %d\n", NUM_TESTS);
                                return 1;
                            }
                            if (i + 1 < argc) {
                                if (strcmp(argv[i + 1], "...")==0) {
                                    config_stop = NUM_TESTS;
                                    ++i;
                                }
                                else if (isdigit(argv[i + 1][0])) {
                                    config_stop = atoi(argv[i + 1]);
                                    if (config_stop <= config_start || config_stop > NUM_TESTS) {
                                        printf("config stop argument must be between config start and %d\n", NUM_TESTS);
                                        return 1;
                                    }
                                    ++i;
                                }
                            }
                        }
                        j = len;
                        break;
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

    i = config_start;
    while (!done && i < config_stop) {
        test_config *config = &test_configs[i];
        if (run_test(config)) {
            result = 1;
            break;
        }
        ++i;
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
