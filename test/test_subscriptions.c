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

#define NUM_DEVICES 2

int verbose = 1;
int terminate = 0;
int autoconnect = 1;
int done = 0;
int period = 100;

mpr_graph graph;
mpr_dev devices[NUM_DEVICES];
mpr_id device_ids[NUM_DEVICES];

mpr_time *timetags;

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
    if (value) {
        eprintf("handler: %s.%d got %f\n",
                mpr_obj_get_prop_as_str((mpr_obj)sig, MPR_PROP_NAME, NULL),
                instance, (*(float*)value));
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

void poll_for(int wait_time)
{
    int i, registered = 0;
    while (!done && (wait_time > 0 || !registered)) {
        registered = 1;
        for (i = 0; i < NUM_DEVICES; i++) {
            mpr_dev_poll(devices[i], 10);
            if (!mpr_dev_get_is_ready(devices[i]))
                registered = 0;
        }
        mpr_graph_poll(graph, 10);
        wait_time -= (NUM_DEVICES + 1) * 10;
    }
}

int main(int argc, char **argv)
{
    int i, j, result = 0;
    char *iface = 0;
    mpr_obj obj;

    /* process flags for -v verbose, -t terminate, -h help */
    for (i = 1; i < argc; i++) {
        if (argv[i] && argv[i][0] == '-') {
            int len = strlen(argv[i]);
            for (j = 1; j < len; j++) {
                switch (argv[i][j]) {
                    case 'h':
                        printf("testsubscriptions.c: possible arguments "
                               "-f fast (execute quickly), "
                               "-q quiet (suppress output), "
                               "-t terminate automatically, "
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

    graph = mpr_graph_new(0);
    if (iface)
        mpr_graph_set_interface(graph, iface);

    for (i = 0; i < NUM_DEVICES; i++) {
        devices[i] = mpr_dev_new("testsubscription", NULL);
        if (iface)
            mpr_graph_set_interface(mpr_obj_get_graph((mpr_obj)devices[i]), iface);
        mpr_sig_new(devices[i], MPR_DIR_IN, "signal", 1, MPR_FLT, NULL, NULL, NULL, NULL, NULL, 0);
    }

    /* wait for device registration */
    poll_for(0);

    /* copy device ids */
    for (i = 0; i < NUM_DEVICES; i++) {
        device_ids[i] = mpr_obj_get_prop_as_int64((mpr_obj)devices[i], MPR_PROP_ID, NULL);
    }

    /* subscribe to device0 */
    mpr_graph_subscribe(graph, devices[0], MPR_DEV, -1);
    poll_for(500);

    /* test 1: */
    /* graph should know about devices[0] */
    obj = mpr_graph_get_obj(graph, device_ids[0], MPR_OBJ);
    if (!obj || (0 == mpr_obj_get_prop_as_int32(obj, MPR_PROP_VERSION, NULL))) {
        eprintf("Test 1 FAILED: graph does not contain device0 record after subscribe\n");
        result = 1;
        goto done;
    }
    eprintf("Test 1 PASSED\n");

    /* test 2: */
    /* graph should not know about devices[1] */
    obj = mpr_graph_get_obj(graph, device_ids[1], MPR_OBJ);
    if (obj) {
        eprintf("Test 2 FAILED: graph contains device1 record without subscription\n");
        result = 1;
        goto done;
    }
    eprintf("Test 2 PASSED\n");


    /* remove subscription to device0 */
    mpr_graph_unsubscribe(graph, devices[0]);
    poll_for(100);

    /* subscription should have expired by now */
    /* modify some metadata */
    mpr_obj_set_prop((mpr_obj)devices[0], MPR_PROP_EXTRA, "foo", 1, MPR_STR, "bar", 1);
    poll_for(100);

    /* test 3: graph should not know about property 'foo' */
    obj = mpr_graph_get_obj(graph, device_ids[0], MPR_OBJ);
    if (MPR_PROP_UNKNOWN != mpr_obj_get_prop_by_key(obj, "foo", NULL, NULL, NULL, NULL)) {
        eprintf("Test 3 FAILED: graph contains new device metadata without subscription\n");
        result = 1;
        goto done;
    }
    eprintf("Test 3 PASSED\n");

    /* turn on autosubscribe */
    mpr_graph_subscribe(graph, NULL, MPR_DEV, -1);
    poll_for(100);

    /* test 4: graph should now know about property 'foo' */
    obj = mpr_graph_get_obj(graph, device_ids[0], MPR_OBJ);
    if (MPR_PROP_UNKNOWN == mpr_obj_get_prop_by_key(obj, "foo", NULL, NULL, NULL, NULL)) {
        eprintf("Test 4 FAILED: graph missing new device metadata after autosubscribe\n");
        result = 1;
        goto done;
    }
    eprintf("Test 4 PASSED\n");

    /* test 5: graph should now know about device1 */
    obj = mpr_graph_get_obj(graph, device_ids[1], MPR_OBJ);
    if (!obj || (0 == mpr_obj_get_prop_as_int32(obj, MPR_PROP_VERSION, NULL))) {
        eprintf("Test 5 FAILED: graph does not contain device1 record after autosubscribe\n");
        result = 1;
        goto done;
    }
    eprintf("Test 5 PASSED\n");

    /* remove subscription to devices[0]0 */
    mpr_graph_unsubscribe(graph, devices[0]);

    /* modify some metadata */
    mpr_obj_remove_prop((mpr_obj)devices[0], MPR_PROP_EXTRA, "foo");
    poll_for(100);

    /* test 6: graph should still know about property 'foo' */
    obj = mpr_graph_get_obj(graph, device_ids[0], MPR_OBJ);
    if (MPR_PROP_UNKNOWN == mpr_obj_get_prop_by_key(obj, "foo", NULL, NULL, NULL, NULL)) {
        eprintf("Test 6 FAILED: graph contains new device metadata after unsubscribe\n");
        result = 1;
        goto done;
    }
    eprintf("Test 6 PASSED\n");

  done:
    for (i = 0; i < NUM_DEVICES; i++)
        mpr_dev_free(devices[i]);
    mpr_graph_free(graph);
    printf("..................................................Test %s\x1B[0m.\n",
           result ? "\x1B[31mFAILED" : "\x1B[32mPASSED");
    return result;
}
