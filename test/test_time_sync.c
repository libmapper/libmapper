//#include "../src/mpr_signal.h"
#include <mapper/mapper.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <math.h>
#ifdef WIN32
#include <io.h>
#else
#include <unistd.h>
#endif
#include <string.h>
#include <signal.h>

#if defined(WIN32) || defined(_MSC_VER)
#define HAVE_WIN32_THREADS 1
#define SLEEP_MS(x) Sleep(x)
#else
#include <pthread.h>
#define SLEEP_MS(x) usleep((x)*1000)
#endif

int verbose = 1;
int terminate = 0;
int shared_graph = 0;
int autoconnect = 1;
int done = 0;
int period = 100;
int sent = 0;
int received = 0;

double offset1, offset2, diff1, diff2;

mpr_dev dev1 = 0;
mpr_dev dev2 = 0;

mpr_sig out1 = 0, in1 = 0;
mpr_sig out2 = 0, in2 = 0;

mpr_time now;

static void eprintf(const char *format, ...)
{
    va_list args;
    if (!verbose)
        return;
    va_start(args, format);
    vprintf(format, args);
    va_end(args);
}

void handler(mpr_sig sig, mpr_sig_evt event, mpr_id instance, int length,
             mpr_type type, const void *value, mpr_time t)
{
    mpr_id id = mpr_obj_get_prop_as_int64((mpr_obj)mpr_sig_get_dev(sig), MPR_PROP_ID, NULL);
    double diff = mpr_time_as_dbl(t) - mpr_time_as_dbl(now), epsilon = 0.005;

    if (value)
        eprintf("handler got value %g at time %f (%f)\n", *(float*)value, mpr_time_as_dbl(t), diff);

    if (id == mpr_obj_get_prop_as_int64((mpr_obj)dev1, MPR_PROP_ID, NULL)) {
        /* time should be offset1 seconds */
        if (fabs(diff - offset1) > epsilon) {
            eprintf("  time diff is %g, should be %f +/-%f\n", diff, offset1, epsilon);
            received = 0;
        }
        else
            ++received;
        diff1 = diff - offset1;
    }
    else if (id == mpr_obj_get_prop_as_int64((mpr_obj)dev2, MPR_PROP_ID, NULL)) {
        /* time should be offset2 seconds */
        if (fabs(diff - offset2) > epsilon) {
            eprintf("  time diff is %g, should be %f +/-%f\n", diff, offset2, epsilon);
            received = 0;
        }
        else
            ++received;
        diff2 = diff - offset2;
    }
    else {
        printf("error: unknown device\n");
        done = 1;
    }
}

int setup_devs(mpr_graph g, const char *iface)
{
    dev1 = mpr_dev_new("test_time_sync", g);
    dev2 = mpr_dev_new("test_time_sync", g);
    if (!dev1 || !dev2)
        goto error;
    if (iface) {
        mpr_graph_set_interface(mpr_obj_get_graph((mpr_obj)dev1), iface);
        mpr_graph_set_interface(mpr_obj_get_graph((mpr_obj)dev2), iface);
    }
    eprintf("devices created using interface %s.\n",
            mpr_graph_get_interface(mpr_obj_get_graph((mpr_obj)dev1)));

    out1 = mpr_sig_new(dev1, MPR_DIR_OUT, "output", 1, MPR_FLT, NULL, NULL, NULL, NULL, NULL, 0);
    out2 = mpr_sig_new(dev2, MPR_DIR_OUT, "output", 1, MPR_FLT, NULL, NULL, NULL, NULL, NULL, 0);

    in1 = mpr_sig_new(dev1, MPR_DIR_IN, "input", 1, MPR_FLT, NULL, NULL, NULL, NULL,
                      handler, MPR_STATUS_ANY);
    in2 = mpr_sig_new(dev2, MPR_DIR_IN, "input", 1, MPR_FLT, NULL, NULL, NULL, NULL,
                      handler, MPR_STATUS_ANY);

    eprintf("Signals registered.\n");
    return 0;

  error:
    return 1;
}

int wait_ready(void)
{
    mpr_time t;
    while (!done && !(mpr_dev_get_is_ready(dev1) && mpr_dev_get_is_ready(dev2))) {
        mpr_time_set(&t, MPR_NOW);
        mpr_time_add_dbl(&t, offset1);
        mpr_dev_set_time(dev1, t);
        mpr_dev_poll(dev1, 25);

        mpr_time_set(&t, MPR_NOW);
        mpr_time_add_dbl(&t, offset2);
        mpr_dev_set_time(dev2, t);
        mpr_dev_poll(dev2, 25);
    }
    return done;
}

int setup_map(void)
{
    int i = 0;

    mpr_map map1 = mpr_map_new(1, &out1, 1, &in2);
    mpr_obj_push((mpr_obj)map1);

    mpr_map map2 = mpr_map_new(1, &out2, 1, &in1);
    mpr_obj_push((mpr_obj)map2);

    /* wait until mapping has been established */
    while (!done && !mpr_map_get_is_ready(map1) && !mpr_map_get_is_ready(map2)) {
        mpr_dev_poll(dev1, 10);
        mpr_dev_poll(dev2, 10);
        if (i++ > 100)
            return 1;
    }
    return 0;
}

enum {
    SYNCING,
    SYNCED
};

void loop(void)
{
    int i = 0, status = SYNCING;
    float sync_time = 0;
    mpr_time start, t;
    mpr_time_set(&start, MPR_NOW);

    mpr_dev_start_polling(dev1, 10);
    mpr_dev_start_polling(dev2, 10);

    eprintf("Polling device..\n");
    while ((!terminate || (i < 400 && received < 50)) && !done) {
        mpr_time_set(&now, MPR_NOW);

        if (i % 2) {
            mpr_time_set(&t, now);
            mpr_time_add_dbl(&t, offset1);
            mpr_dev_set_time(dev1, t);
            eprintf("sending %d from dev1 -> dev2\n", received);
            mpr_sig_set_value(out1, 0, 1, MPR_INT32, &received);
            mpr_dev_update_maps(dev1);
        }
        else {
            mpr_time_set(&t, now);
            mpr_time_add_dbl(&t, offset2);
            mpr_dev_set_time(dev2, t);
            eprintf("sending %d from dev2 -> dev1\n", received);
            mpr_sig_set_value(out2, 0, 1, MPR_INT32, &received);
            mpr_dev_update_maps(dev2);
        }

        ++sent;
        SLEEP_MS(period);
        ++i;

        if (received > 1) {
            if (SYNCING == status) {
                mpr_time_set(&t, MPR_NOW);
                sync_time = mpr_time_as_dbl(t) - mpr_time_as_dbl(start);
                eprintf("\n----------- %2.2f seconds -----------\n", sync_time);
            }
            status = SYNCED;
            if (!verbose) {
                printf("\r  Synced  (%3i)... Offsets: [%+4.3f, %+4.3f]   ", received, diff1, diff2);
                fflush(stdout);
            }
        }
        else {
            status = SYNCING;
            if (!verbose) {
                printf("\r  Syncing (%3i)... Offsets: [%+4.3f, %+4.3f]   ", i, diff1, diff2);
                fflush(stdout);
            }
        }
    }
    if (SYNCED == status)
        printf("\r  Sync achieved in %2.2f seconds ", sync_time);
    else
        printf("\r  Sync not achieved ............");

    mpr_dev_stop_polling(dev1);
    mpr_dev_stop_polling(dev2);
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
                        printf("test_time_sync.c: possible arguments "
                               "-q quiet (suppress output), "
                               "-f fast (execute quickly), "
                               "-t terminate automatically, "
                               "-s shared (use one mpr_graph only), "
                               "-h help, "
                               "--iface network interface\n");
                        return 1;
                        break;
                    case 'q':
                        verbose = 0;
                        break;
                    case 'f':
                        period = 1;
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

    g = shared_graph ? mpr_graph_new(0) : 0;

    signal(SIGINT, ctrlc);

    offset1 = rand() % 1000 * (rand() % 2 ? 1.0 : -1.0);
    offset2 = rand() % 1000 * (rand() % 2 ? 1.0 : -1.0);

    if (setup_devs(g, iface)) {
        eprintf("Error initializing devices.\n");
        result = 1;
        goto done;
    }

    if (wait_ready()) {
        eprintf("Device registration aborted.\n");
        result = 1;
        goto done;
    }

    if (autoconnect && setup_map()) {
        eprintf("Error creating maps.\n");
        result = 1;
        goto done;
    }

    eprintf("offset1: %f\n", offset1);
    eprintf("offset2: %f\n", offset2);

    loop();

    if (received < 50) {
        eprintf("Problem with time offsets.\n");
        eprintf("Updated value %d time%s, received %d messages with correct time offset.\n",
                sent, sent == 1 ? "" : "s", received);
        result = 1;
    }

  done:
    mpr_dev_free(dev1);
    mpr_dev_free(dev2);
    if (g)
        mpr_graph_free(g);
    printf("..................Test %s\x1B[0m.\n",
           result ? "\x1B[31mFAILED" : "\x1B[32mPASSED");
    return result;
}
