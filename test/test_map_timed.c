#include <mapper/mapper.h>
#include <stdio.h>
#include <ctype.h>
#include <string.h>
#include <signal.h>
#include <stdlib.h>

int verbose = 1;
int terminate = 0;
int autoconnect = 1;
int shared_graph = 0;
int done = 0;
int period = 100;

// TODO: increase
int num_inst = 1;

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

//if map is instanced, map-produced instance updates should cause activation right?
//if map is not instanced, only already-active instances would be updated

/* schedule next periodic event from current time */
#define NOW                 \
    "period = %g;"          \
    "y = a++;"              \
    "next = now + period;"

/* schedule next periodic event from current time (explicit phase)
 * we "round up" when calculating num periods since start if closer than 0.999 */
#define NOW_W_START                                                         \
    "period = %g;"                                                          \
    "start = 10;"                                                           \
    "y = 1;"                                                                \
    "next = (floor((now - start + 0.001) / period) + 1) * period + start;"

/* better to schedule by incrementing the `next` timestamp for drift-free timing */
/* schedule next periodic event (implicit phase) */
#define NEXT            \
    "period = %g;"      \
    "y = 1;"            \
    "next += period;"

/* schedule next periodic event (explicit phase)
 * we "round up" when calculating num periods since start if closer than 0.999 */
#define NEXT_W_START                                                \
    "period = %g;"                                                  \
    "start = 10;"                                                   \
    "y = _x;"                                                       \
    "next = (floor((next - start + 0.001) / period) + 1) * period + start;"

/* if we track num_periods it is much cheaper to calculate `(n++)*period+start`
 * also have access to beat number which could be useful
 * this version has no division at "runtime" */
#define START_NO_DIV                        \
    "start{-1} = 10;"                       \
    "period{-1} = %g;"                      \
    "i{-1} = floor((now - start) / period);"\
    "y = 1; next = (i++) * period + start;"

/* schedule next periodic event from a repeating pattern (implicit start time) */
#define PATT                    \
    "period = %g;"              \
    "p = [1,.5,.5] * period;"   \
    "y = 1; next += p[i++];"

/* schedule next periodic event using a ramp */
#define RAMP                \
    "period = %g;"          \
    "y = a;"                \
    "a += period * 0.1;"    \
    "next += a %% period;"

/* schedule next periodic event using a sinusoid */
#define SINE                                    \
    "start{-1} = now;"                          \
    "period = %g; y = 1;"                       \
    "next += (sin(now - start) + 1.1) * period;"

/* schedule next periodic event in the past */
#define PAST                \
    "period = %g;"          \
    "y = 1;"                \
    "next = now - 1;"

/* 'start' time is in the future */
#define FUTURE                                          \
    "start{-1} = 0; period{-1} = %g;"                   \
    "i{-1} = floor((now - start) / period) + 50; y = 1;"\
    "next = (i++) * period + start;"

#define UPSAMPLE            \
    "period = %g;"          \
    "y = ema(_x, 0.1);"     \
    "next += period * 0.67;"

#define DOWNSAMPLE                  \
    "period = %g;"                  \
    "y = ema(_x, 0.5);"             \
    "next = next{-1} + period * 2;"

#define QUANTIZE                                    \
    "period = ema((_t_x-_t_x{-1}) ?: %f * 25, 0.2);"\
    "y = period;"                                   \
    "next += period;"

#define RANDOM  \
    "p = %g; r = uniform(p) + p * 0.5; y = r; next += r;"

//"new{-1}=0; period = ema(new?0.1:(t_x')

/* TODO: need unmodified t_x to estimate timebase offset
 * or direct access to timebase offset estimation, e.g. `periodic(period, x - t0_x)` */

/* note: since the expression string is being processed by `snprintf()` below, the modulus operator
 * `%` needs to be escaped using `%%` */
#define SYNC                                                \
    "period = %g;"                                          \
    "y = 1;"                                                \
    "next = now + period - ((t_now - t_x - x) %% period);"

/* the periodic function (syntactic sugar) */
#define FN_PERIODIC                         \
    "period = %g;"                          \
    "start = 10;"                           \
    "y = 1;"                                \
    "next = periodic(period, start);"

/* the periodic function with start time in the future */
#define FN_PERIODIC_FUTURE                  \
    "period{-1} = %g;"                      \
    "start{-1} = now + period * 50;"        \
    "y = 1;"                                \
    "next = periodic(period, start);"

typedef struct _test_config
{
    int test_id;
    const char *expr;
    mpr_loc process_loc;
    double time_mult_min;
    double time_mult_max;
} test_config;

test_config test_configs[] = {
    {  1, NOW,                  MPR_LOC_SRC, 0.95, 1.15 },
    {  2, NOW,                  MPR_LOC_DST, 0.95, 1.20 },
    {  3, NOW_W_START,          MPR_LOC_SRC, 0.95, 1.05 },
    {  4, NOW_W_START,          MPR_LOC_DST, 0.95, 1.05 },
    {  5, NEXT,                 MPR_LOC_SRC, 0.95, 1.05 },
    {  6, NEXT,                 MPR_LOC_DST, 0.95, 1.05 },
    {  7, NEXT_W_START,         MPR_LOC_SRC, 0.95, 1.05 },
    {  8, NEXT_W_START,         MPR_LOC_DST, 0.95, 1.05 },
    {  9, START_NO_DIV,         MPR_LOC_SRC, 0.95, 1.05 },
    { 10, START_NO_DIV,         MPR_LOC_DST, 0.95, 1.05 },
    { 11, PATT,                 MPR_LOC_SRC, 0.60, 0.70 },
    { 12, PATT,                 MPR_LOC_DST, 0.60, 0.70 },
    { 13, RAMP,                 MPR_LOC_SRC, 0.44, 0.52 },
    { 14, RAMP,                 MPR_LOC_DST, 0.44, 0.52 },
    { 15, SINE,                 MPR_LOC_SRC, 0.85, 2.00 },
    { 16, SINE,                 MPR_LOC_DST, 0.85, 2.00 },
    { 17, PAST,                 MPR_LOC_SRC, 0.00, 0.05 },
    { 18, PAST,                 MPR_LOC_DST, 0.00, 0.05 },
    { 19, FUTURE,               MPR_LOC_SRC, 1.95, 2.05 },
    { 20, FUTURE,               MPR_LOC_DST, 1.95, 2.05 },
    { 21, UPSAMPLE,             MPR_LOC_SRC, 0.65, 0.70 },
    { 22, UPSAMPLE,             MPR_LOC_DST, 0.65, 0.70 },
    { 23, DOWNSAMPLE,           MPR_LOC_SRC, 1.95, 2.05 },
    { 24, DOWNSAMPLE,           MPR_LOC_DST, 1.95, 2.05 },
    { 25, QUANTIZE,             MPR_LOC_SRC, 1.65, 1.90 },
    { 26, QUANTIZE,             MPR_LOC_DST, 1.65, 1.90 },
    { 27, RANDOM,               MPR_LOC_SRC, 0.75, 1.25 },
    { 28, RANDOM,               MPR_LOC_DST, 0.75, 1.25 },
    { 29, FN_PERIODIC,          MPR_LOC_SRC, 0.95, 1.15 },
    { 30, FN_PERIODIC,          MPR_LOC_DST, 0.95, 1.15 },
    { 31, FN_PERIODIC_FUTURE,   MPR_LOC_SRC, 1.95, 2.05 },
    { 32, FN_PERIODIC_FUTURE,   MPR_LOC_DST, 1.85, 2.05 },
//    { 33, SYNC_LOCAL,           MPR_LOC_SRC, 2.0,  2.0  },
//    { 34, SYNC_LOCAL,           MPR_LOC_DST, 2.0,  2.0  },
//    { 35, SYNC_REMOTE,          MPR_LOC_SRC, 2.0,  2.0  },
//    { 36, SYNC_REMOTE,          MPR_LOC_DST, 2.0,  2.0  },

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

    sendsig = mpr_sig_new(src, MPR_DIR_OUT, "outsig", 1, MPR_FLT,
                          NULL, NULL, NULL, &num_inst, NULL, 0);

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
    if (verbose) {
        eprintf("handler inst %d, evt %d", instance, event);
        if (value)
            printf(", val %g\n", *(float*)value);
        else
            printf("\n");
    }
    ++received;
    eprintf("  received: %d (%gms)\n", received, (mpr_time_as_dbl(t) - mpr_time_as_dbl(t_last)) * 1000);
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
                          NULL, NULL, &num_inst, handler, MPR_STATUS_UPDATE_REM);

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

void loop()
{
    received = 0;
    mpr_dev_start_polling(dst, 100);

    eprintf("Polling device..\n");
    while ((!terminate || received < 50) && !done) {

        ++sent;
        if (sent % 3)
            mpr_sig_set_value(sendsig, 0, 1, MPR_INT32, &sent);

        mpr_dev_poll(src, period);

        if (!verbose) {
            printf("\r  Received: %4i", received);
            fflush(stdout);
        }
    }

    mpr_dev_stop_polling(dst);
}

int run_test(test_config *config)
{
    double period_sec = period * 0.001;
    mpr_time t_start;
    int result = 0;
//    int zero = 0;
    mpr_map map;
    char expr[256];

    snprintf(expr, 256, config->expr, period_sec);

    printf("Configuration %d: ", config->test_id);
    printf("PROC: %s", config->process_loc == MPR_LOC_SRC ? "src" : "dst");
    printf("; EXPR: \"%s\"\n", expr);

    map = mpr_map_new(1, &sendsig, 1, &recvsig);

    /* set process location */
    mpr_obj_set_prop((mpr_obj)map, MPR_PROP_PROCESS_LOC, NULL, 1, MPR_INT32, &config->process_loc, 1);

    /* set expression */
    mpr_obj_set_prop((mpr_obj)map, MPR_PROP_EXPR, NULL, 1, MPR_STR, expr, 1);

    // TODO: ensure can set this with an int, etc
//    mpr_obj_set_prop((mpr_obj)map, MPR_PROP_USE_INST, NULL, 1, MPR_BOOL, &zero, 1);

    mpr_obj_push(map);

    /* wait until mapping has been established */
    while (!done && !mpr_map_get_is_ready(map)) {
        mpr_dev_poll(src, 10);
        mpr_dev_poll(dst, 10);
    }

    /* wait for changes to take effect */
    do {
        int ready = 1;
        mpr_dev_poll(src, 10);
        mpr_dev_poll(dst, 10);
        mpr_dev_poll(src, 10);
        mpr_dev_poll(dst, 10);

        eprintf("\r  checking process location...");
        if (   !shared_graph
            && mpr_obj_get_prop_as_int32((mpr_obj)map, MPR_PROP_PROCESS_LOC, 0) != config->process_loc)
            ready = 0;
        else {
            eprintf("\r  checking expression...      ");
            if (strcmp(mpr_obj_get_prop_as_str((mpr_obj)map, MPR_PROP_EXPR, 0), expr))
                ready = 0;
        }
        if (ready) {
            eprintf("\r  configuration ready         \n");
            break;
        }
    } while (!done);

    /* activate a destination instance */
//    mpr_sig_activate_inst(recvsig, 0);

    mpr_time_set(&t_start, MPR_NOW);
    t_last = t_start;

    loop();

    if (terminate) {
        /* check result timing */
        mpr_time t_end;
        double min_expected_sec;
        double max_expected_sec;
        double elapsed_sec;

        mpr_time_set(&t_end, MPR_NOW);

        elapsed_sec = mpr_time_as_dbl(t_end) - mpr_time_as_dbl(t_start);
        min_expected_sec = 50 * period_sec * config->time_mult_min;
        max_expected_sec = 50 * period_sec * config->time_mult_max;

        printf(" in %.2fs (expected %.2fs–%.2fs)", elapsed_sec, min_expected_sec, max_expected_sec);

        result = elapsed_sec < min_expected_sec || elapsed_sec > (max_expected_sec + 0.1);
    }

    printf(" ..... %s\x1B[0m.\n", result ? "\x1B[31mFAILED" : "\x1B[32mPASSED");
    return result;
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
                               "-q quiet (suppress output), "
                               "-t terminate automatically, "
                               "-f fast (execute quickly), "
                               "-s shared (use one mpr_graph only), "
                               "-h help, "
                               "--iface network interface, "
                               "--config specify a configuration to run (1-%d)\n", NUM_TESTS);
                        return 1;
                        break;
                    case 'f':
                        period = 50;
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

    if (autoconnect && result) {
        result = 1;
    }

  done:
    cleanup_dst();
    cleanup_src();
    if (g) mpr_graph_free(g);
    printf("..................................................Test %s\x1B[0m.\n",
           result ? "\x1B[31mFAILED" : "\x1B[32mPASSED");
    return result;
}
