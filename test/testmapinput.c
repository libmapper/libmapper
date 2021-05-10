#include <mapper/mapper.h>
#include <stdio.h>
#include <stdarg.h>
#include <math.h>
#include <lo/lo.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <string.h>

mpr_dev devices[2];
mpr_sig inputs[4];

int sent = 0;
int received = 0;
int done = 0;

int verbose = 1;
int terminate = 0;
int autoconnect = 1;
int period = 50;

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
        int i;
        const char *name = mpr_obj_get_prop_as_str((mpr_obj)sig, MPR_PROP_NAME, NULL);
        eprintf("--> received %s", name);

        if (type == MPR_FLT) {
            float *v = (float*)value;
            for (i = 0; i < length; i++) {
                eprintf(" %f", v[i]);
            }
        }
        else if (type == MPR_DBL) {
            double *v = (double*)value;
            for (i = 0; i < length; i++) {
                eprintf(" %f", v[i]);
            }
        }
        eprintf("\n");
    }
    received++;
}

int setup_devs(const char *iface)
{
    float mnf1[] = {0, 0, 0}, mxf1[] = {1, 1, 1};
    float mnf2[] = {3.2, 2, 0}, mxf2[] = {-2, 13, 100};
    double mnd = 0, mxd = 10;

    devices[0] = mpr_dev_new("testmapinput", 0);
    devices[1] = mpr_dev_new("testmapinput", 0);
    if (!devices[0] || !devices[1])
        goto error;
    if (iface) {
        mpr_graph_set_interface(mpr_obj_get_graph(devices[0]), iface);
        mpr_graph_set_interface(mpr_obj_get_graph(devices[1]), iface);
    }
    eprintf("devices created using interfaces %s and %s.\n",
            mpr_graph_get_interface(mpr_obj_get_graph(devices[0])),
            mpr_graph_get_interface(mpr_obj_get_graph(devices[0])));

    inputs[0] = mpr_sig_new(devices[0], MPR_DIR_IN, "insig_1", 1, MPR_FLT,
                            NULL, mnf1, mxf1, NULL, handler, MPR_SIG_UPDATE);
    inputs[1] = mpr_sig_new(devices[0], MPR_DIR_IN, "insig_2", 1, MPR_DBL,
                            NULL, &mnd, &mxd, NULL, handler, MPR_SIG_UPDATE);
    inputs[2] = mpr_sig_new(devices[1], MPR_DIR_IN, "insig_3", 3, MPR_FLT,
                            NULL, mnf1, mxf1, NULL, handler, MPR_SIG_UPDATE);
    inputs[3] = mpr_sig_new(devices[1], MPR_DIR_IN, "insig_4", 1, MPR_FLT,
                            NULL, mnf2, mxf2, NULL, handler, MPR_SIG_UPDATE);

    /* In this test inputs[2] will never get its full vector value from
     * external updates – for the handler to be called we will need to
     * initialize its value. */
    mpr_sig_set_value(inputs[2], 0, 3, MPR_FLT, mnf2);

    return 0;

  error:
    return 1;
}

void cleanup_devs()
{
    int i;
    for (i = 0; i < 2; i++) {
        if (devices[i]) {
            eprintf("Freeing device.. ");
            fflush(stdout);
            mpr_dev_free(devices[i]);
            eprintf("ok\n");
        }
    }
}

void wait_local_devs()
{
    while (!done && !(mpr_dev_get_is_ready(devices[0]) && mpr_dev_get_is_ready(devices[1]))) {
        mpr_dev_poll(devices[0], 25);
        mpr_dev_poll(devices[1], 25);
    }
}

void loop()
{
    int i = 0, recvd, ready = 0;
    float val;

    eprintf("-------------------- GO ! --------------------\n");

    if (autoconnect) {
        mpr_map maps[2];
        /* map input to another input on same device */
        maps[0] = mpr_map_new(1, &inputs[0], 1, &inputs[1]);
        mpr_obj_push((mpr_obj)maps[0]);

        /* map input to an input on another device */
        maps[1] = mpr_map_new(1, &inputs[1], 1, &inputs[2]);
        mpr_obj_push((mpr_obj)maps[1]);

        /* wait until mapping has been established */
        while (!done && !ready) {
            mpr_dev_poll(devices[0], 100);
            mpr_dev_poll(devices[1], 100);
            ready = mpr_map_get_is_ready(maps[0]) & mpr_map_get_is_ready(maps[1]);
        }
    }

    i = 0;
    while ((!terminate || i < 50) && !done) {
        val = (i % 10) * 1.0f;
        mpr_sig_set_value(inputs[0], 0, 1, MPR_FLT, &val);
        eprintf("insig_1 value updated to %f -->\n", val);
        sent += 1;

        recvd = mpr_dev_poll(devices[0], period);
        recvd += mpr_dev_poll(devices[1], period);
        eprintf("Received %i messages.\n\n", recvd);
        i++;

        if (!verbose) {
            printf("\r  Sent: %4i, Received: %4i   ", sent, received);
            fflush(stdout);
        }
    }
    mpr_dev_poll(devices[0], period);
    mpr_dev_poll(devices[1], period);
}

void ctrlc(int sig)
{
    done = 1;
}

int main(int argc, char ** argv)
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
                        printf("testmapinput.c: possible arguments "
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

    if (setup_devs(iface)) {
        eprintf("Error initializing devices.\n");
        result = 1;
        goto done;
    }

    wait_local_devs();

    loop();

    if (autoconnect && received != sent * 2) {
        eprintf("sent: %d, recvd: %d\n", sent, received);
        result = 1;
    }

  done:
    cleanup_devs();
    printf("...................Test %s\x1B[0m.\n",
           result ? "\x1B[31mFAILED" : "\x1B[32mPASSED");
    return result;
}
