#include <mapper/mapper.h>
#include "../src/mpr_time.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <time.h>

#include <math.h>
#ifdef WIN32
#include <io.h>
#else
#include <sys/time.h>
#include <unistd.h>
#endif
#include <signal.h>

int verbose = 1;
int terminate = 0;
int shared_graph = 0;
int done = 0;

int num_devs = 5;
mpr_dev *devices = 0;
mpr_dev dev_1 = 0;
int sent = 0;
int received = 0;

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
        eprintf("handler: Got %f\n", (*(float*)value));
    }
    received++;
}

void add_sigs(mpr_dev dev)
{
    int i;
    float mn=0, mx=1;
    char str[20];

    for (i = 0; i < 10; i++) {
        mn = fmod(rand() * 0.01, 21.f) - 10.f;
        mx = fmod(rand() * 0.01, 21.f) - 10.f;
        sprintf(str, "in%d", i);
        mpr_sig_new(dev, MPR_DIR_IN, str, 1, MPR_FLT, NULL, &mn, &mx, NULL, NULL, 0);
        mn = fmod(rand() * 0.01, 21.f) - 10.f;
        mx = fmod(rand() * 0.01, 21.f) - 10.f;
        sprintf(str, "out%d", i);
        if (i % 2 == 0)
            mpr_sig_new(dev, MPR_DIR_OUT, str, 1, MPR_FLT, NULL, &mn, &mx, NULL, NULL, 0);
        else
            mpr_sig_new(dev, MPR_DIR_OUT, str, 1, MPR_FLT, NULL, &mn, NULL, NULL, NULL, 0);
    }
}

int setup_devs(const char *iface)
{
    int i;

    mpr_graph g = shared_graph ? mpr_graph_new(0) : 0;
    if (g && iface)
        mpr_graph_set_interface(g, iface);
    for (i = 0; i < num_devs; i++) {
        devices[i] = mpr_dev_new("testmany", g);
        if (!devices[i])
            goto error;
        if (!g && iface)
            mpr_graph_set_interface(mpr_obj_get_graph((mpr_obj)devices[i]), iface);
        eprintf("device %d created using interface %s.\n", i,
                mpr_graph_get_interface(mpr_obj_get_graph((mpr_obj)devices[i])));

        /* give each device 10 inputs and 10 outputs */
        add_sigs(devices[i]);

    }
    return 0;

  error:
    return 1;
}

void cleanup_devs(void)
{
    int i;

    eprintf("Freeing devices");
    for (i = 0; i < num_devs; i++) {
        if (devices[i]) {
            mpr_dev_free(devices[i]);
            eprintf(".");
        }
    }
    if (dev_1) {
        mpr_dev_free(dev_1);
        eprintf(".");
    }
    eprintf("\n");
}

int wait_ready(int *cancel)
{
    int i, keep_waiting = 1, highest = 0, result = 0;

    while ( keep_waiting && !*cancel ) {
        int j = 0, k = 0;
        keep_waiting = 0;

        for (i = 0; i < num_devs; i++) {
            mpr_dev_poll(devices[i], 5);
            if (!mpr_dev_get_is_ready(devices[i])) {
                keep_waiting = 1;
            }
        }
        if (j++ >= 1000) {
            printf(".");
            fflush(stdout);
            j = 0;
            ++k;
            if (k >= 50) {
                printf("\33[2K\r");
                fflush(stdout);
                k = 0;
            }
        }
    }

    eprintf("\nRegistered devices:\n");
    for (i = 0; i < num_devs; i++) {
        int ordinal = mpr_obj_get_prop_as_int32((mpr_obj)devices[i], MPR_PROP_ORDINAL, NULL);
        if (ordinal > highest)
            highest = ordinal;
    }

    /* check for duplicate ordinals; print devices in order */
    for (i = 1; i <= highest; i++) {
        int j, count = 0;
        const char *name = 0;
        for (j = 0; j < num_devs; j++) {
            if (i == mpr_obj_get_prop_as_int32((mpr_obj)devices[j], MPR_PROP_ORDINAL, NULL)) {
                name = mpr_obj_get_prop_as_str((mpr_obj)devices[j], MPR_PROP_NAME, NULL);
                ++count;
            }
        }
        if (count && name) {
            eprintf("%s  %s\t\tx %i\n\x1B[0m", count > 1 ? "\x1B[31m" : "\x1B[32m", name, count);
        }
        if (count > 1)
            result = 1;
    }

    /* stop polling device.1 and see if a new device can register with its ordinal */
    for (i = 0; i < num_devs; i++) {
        int ordinal = mpr_obj_get_prop_as_int32((mpr_obj)devices[i], MPR_PROP_ORDINAL, NULL);
        if (1 == ordinal) {
            /* create a map from device.1 */
            int dst_idx = i ? (i - 1) : (i + 1);
            mpr_list list;
            mpr_sig src, dst;
            mpr_map map;

            list = mpr_dev_get_sigs(devices[i], MPR_DIR_OUT);
            src = mpr_list_get_idx(list, 0);
            mpr_list_free(list);
            list = mpr_dev_get_sigs(devices[dst_idx], MPR_DIR_IN);
            dst = mpr_list_get_idx(list, 0);
            mpr_list_free(list);
            map = mpr_map_new(1, &src, 1, &dst);
            mpr_obj_push((mpr_obj)map);

            /* poll a bit more to allow map handshaking */
            while (!mpr_map_get_is_ready(map)) {
                mpr_dev_poll(devices[i], 10);
                mpr_dev_poll(devices[dst_idx], 10);
            }

            /* cache old device.1 so we can free it later */
            eprintf("stalling device with ordinal 1...\n");
            dev_1 = devices[i];

            /* create a new device */
            eprintf("adding another device...\n");
            mpr_graph g = shared_graph ? mpr_obj_get_graph((mpr_obj)dev_1) : 0;
            devices[i] = mpr_dev_new("testmany", g);
            add_sigs(devices[i]);

            /* wait for new device registration */
            while (!done && !mpr_dev_get_is_ready(devices[i])) {
                int j;
                for (j = 0; j < num_devs; j++) {
                    mpr_dev_poll(devices[j], 5);
                }
            }

            /* check the new device's ordinal */
            ordinal = mpr_obj_get_prop_as_int32((mpr_obj)devices[i], MPR_PROP_ORDINAL, NULL);
            if (1 == ordinal) {
                eprintf("error: new device registered with ordinal 1\n");
                result = 1;
            }
            if (ordinal > highest) {
                highest = ordinal;
            }

            break;
        }
    }
    if (i >= num_devs) {
        eprintf("no devices registered with ordinal 0, skipping test 2\n");
    }

    /* check for duplicate ordinals; print devices in order */
    for (i = 1; i <= highest; i++) {
        int j, count = 0;
        const char *name = 0;
        if (i == mpr_obj_get_prop_as_int32((mpr_obj)dev_1, MPR_PROP_ORDINAL, NULL)) {
            name = mpr_obj_get_prop_as_str((mpr_obj)dev_1, MPR_PROP_NAME, NULL);
            ++count;
        }
        for (j = 0; j < num_devs; j++) {
            if (i == mpr_obj_get_prop_as_int32((mpr_obj)devices[j], MPR_PROP_ORDINAL, NULL)) {
                name = mpr_obj_get_prop_as_str((mpr_obj)devices[j], MPR_PROP_NAME, NULL);
                ++count;
            }
        }
        if (count && name) {
            eprintf("%s  %s\t\tx %i\n\x1B[0m", count > 1 ? "\x1B[31m" : "\x1B[32m", name, count);
        }
        if (count > 1)
            result = 1;
    }

    return result;
}

void loop(void)
{
    int i = 0, j;
    eprintf("-------------------- GO ! --------------------\n");

    while (i >= 0 && !done) {
        for (j = 0; j < num_devs; j++) {
            mpr_dev_poll(devices[j], 10);
        }
        i++;
    }
}

void ctrlc(int sig)
{
    done = 1;
}

void segv(int sig)
{
    printf("\x1B[31m(SEGV)\n\x1B[0m");
    exit(1);
}

int main(int argc, char *argv[])
{
    double now = mpr_get_current_time();
    int i, j, T = 1, result = 0;
    char *iface = 0;
    mpr_graph g;
    mpr_list l;

    /* process flags for -v verbose, -t terminate, -h help */
    for (i = 1; i < argc; i++) {
        if (argv[i] && argv[i][0] == '-') {
            int len = strlen(argv[i]);
            for (j = 1; j < len; j++) {
                switch (argv[i][j]) {
                    case 'h':
                        printf("testmany.c: possible arguments "
                               "-q quiet (suppress output), "
                               "-t terminate automatically, "
                               "-s share (use one mpr_graph only), "
                               "-h help, "
                               "--devices number of devices, "
                               "--iface network interface\n");
                        return 1;
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
                        if (strcmp(argv[i], "--devices")==0 && argc>i+1) {
                            i++;
                            num_devs = atoi(argv[i]);
                            if (num_devs < 2)
                                num_devs = 2;
                            j = 1;
                        }
                        else if (strcmp(argv[i], "--iface")==0 && argc>i+1) {
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

    devices = (mpr_dev*)malloc(sizeof(mpr_dev)*num_devs);

    signal(SIGSEGV, segv);
    signal(SIGINT, ctrlc);
    srand( time(NULL) );

    if (setup_devs(iface)) {
        eprintf("Error initializing devices.\n");
        result = 1;
        goto done;
    }

    if (wait_ready(&done)) {
        eprintf("Error registering devices.\n");
        result = 1;
    }

    now = mpr_get_current_time() - now;
    eprintf("Allocated %d devices in %f seconds.\n", num_devs + (dev_1 != NULL), now);
    if (result)
        goto done;

    /* Check graph local devices */
    g = mpr_obj_get_graph((mpr_obj)devices[0]);
    l = mpr_graph_get_list(g, MPR_DEV);
    l = mpr_list_filter(l, MPR_PROP_IS_LOCAL, NULL, 1, MPR_BOOL, &T, MPR_OP_EQ);
    i = mpr_list_get_size(l);
    eprintf("Checking local device count for graph %p... %d\n", g, i);
    mpr_list_free(l);
    if ((shared_graph && i != num_devs + 1) || (!shared_graph && i != 1)) {
        printf("ERROR! Should be %d\n", shared_graph ? num_devs : 1);
        result = 1;
        goto done;
    }

    if (!terminate)
        loop();

  done:
    {
        mpr_graph g = mpr_obj_get_graph(devices[0]);
        cleanup_devs();
        free(devices);
        if (shared_graph) mpr_graph_free(g);
    }
    printf("\r..................................................Test %s\x1B[0m.\n",
           result ? "\x1B[31mFAILED" : "\x1B[32mPASSED");
    return result;
}
