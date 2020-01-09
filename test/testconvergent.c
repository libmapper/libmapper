#include <mpr/mpr.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>

#define eprintf(format, ...) do {               \
    if (verbose)                                \
        fprintf(stdout, format, ##__VA_ARGS__); \
} while(0)

int num_sources = 3;
int verbose = 1;
int terminate = 0;
int autoconnect = 1;
int done = 0;
int period = 100;

mpr_dev *srcs = 0;
mpr_dev dst = 0;
mpr_sig *sendsigs = 0;
mpr_sig recvsig = 0;

int sent = 0;
int received = 0;

int setup_srcs()
{
    int i, mni=0, mxi=1;

    srcs = (mpr_dev*)calloc(1, num_sources * sizeof(mpr_dev));
    sendsigs = (mpr_sig*)calloc(1, num_sources * sizeof(mpr_sig));

    for (i = 0; i < num_sources; i++) {
        srcs[i] = mpr_dev_new("testconvergent-send", 0);
        if (!srcs[i])
            goto error;
        sendsigs[i] = mpr_sig_new(srcs[i], MPR_DIR_OUT, "sendsig", 1,
                                  MPR_INT32, NULL, &mni, &mxi, NULL, NULL, 0);
        if (!sendsigs[i])
            goto error;
        eprintf("source %d created.\n", i);
    }
    return 0;

error:
    for (i = 0; i < num_sources; i++) {
        if (srcs[i])
            mpr_dev_free(srcs[i]);
    }
    return 1;
}

void cleanup_src()
{
    for (int i = 0; i < num_sources; i++) {
        if (srcs[i]) {
            eprintf("Freeing source %d... ", i);
            fflush(stdout);
            mpr_dev_free(srcs[i]);
            eprintf("ok\n");
        }
    }
    free(srcs);
    free(sendsigs);
}

void handler(mpr_sig sig, mpr_sig_evt evt, mpr_id instance, int length,
             mpr_type type, const void *value, mpr_time t)
{
    if (value) {
        eprintf("handler: Got %f\n", (*(float*)value));
    }
    else {
        eprintf("handler: Got NULL\n");
    }
    received++;
}

int setup_dst()
{
    dst = mpr_dev_new("testconvergent-recv", 0);
    if (!dst)
        goto error;
    eprintf("destination created.\n");

    float mn=0, mx=1;
    recvsig = mpr_sig_new(dst, MPR_DIR_IN, "recvsig", 1, MPR_FLT, NULL,
                          &mn, &mx, NULL, handler, MPR_SIG_UPDATE);

    eprintf("Input signal 'insig' registered.\n");
    mpr_list l = mpr_dev_get_sigs(dst, MPR_DIR_IN);
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
    mpr_map map = mpr_map_new(num_sources, sendsigs, 1, &recvsig);
    if (!map) {
        eprintf("Failed to create map\n");
        return 1;
    }

    // build expression string
    int i, offset = 2, len = num_sources * 4 + 4;
    char expr[len];
    snprintf(expr, 3, "y=");
    for (i = 0; i < num_sources; i++) {
        if (i == 0) {
            snprintf(expr + offset, len - offset, "-x%d",
                     mpr_map_get_sig_idx(map, sendsigs[i]));
            offset += 3;
        }
        else {
            snprintf(expr + offset, len - offset, "-_x%d",
                     mpr_map_get_sig_idx(map, sendsigs[i]));
            offset += 4;
        }
    }
    mpr_obj_set_prop(map, MPR_PROP_EXPR, NULL, 1, MPR_STR, expr, 1);
    mpr_obj_push(map);

    // wait until mappings have been established
    while (!done && !mpr_map_get_is_ready(map)) {
        for (i = 0; i < num_sources; i++)
            mpr_dev_poll(srcs[i], 10);
        mpr_dev_poll(dst, 10);
    }

    return 0;
}

void wait_ready(int *cancel)
{
    int i, keep_waiting = 1;
    while (keep_waiting && !*cancel) {
        keep_waiting = 0;

        for (i = 0; i < num_sources; i++) {
            mpr_dev_poll(srcs[i], 50);
            if (!mpr_dev_get_is_ready(srcs[i])) {
                keep_waiting = 1;
            }
        }
        mpr_dev_poll(dst, 50);
        if (!mpr_dev_get_is_ready(dst))
            keep_waiting = 1;
    }
}

void loop()
{
    eprintf("Polling device..\n");
    int i = 0, j;

    while ((!terminate || i < 50) && !done) {
        for (j = 0; j < num_sources; j++) {
            mpr_dev_poll(srcs[j], 0);
            eprintf("Updating source %d = %i\n", j, i);
            mpr_sig_set_value(sendsigs[j], 0, 1, MPR_INT32, &i, MPR_NOW);
        }
        sent++;
        mpr_dev_poll(dst, period);
        i++;

        if (!verbose) {
            printf("\r  Sent: %4i, Received: %4i   ", sent, received);
            fflush(stdout);
        }
    }
}

void ctrlc(int sig)
{
    done = 1;
}

int main(int argc, char **argv)
{
    int i, j, result = 0;

    // process flags for -v verbose, -t terminate, -h help
    for (i = 1; i < argc; i++) {
        if (argv[i] && argv[i][0] == '-') {
            int len = strlen(argv[i]);
            for (j = 1; j < len; j++) {
                switch (argv[i][j]) {
                    case 'h':
                        printf("testcombiner.c: possible arguments "
                               "-q quiet (suppress output), "
                               "-t terminate automatically, "
                               "-f fast (execute quickly), "
                               "-h help\n");
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
                        if (strcmp(argv[i], "--sources")==0 && argc>i+1) {
                            i++;
                            num_sources = atoi(argv[i]);
                            if (num_sources <= 0)
                                num_sources = 1;
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

    if (setup_dst()) {
        eprintf("Error initializing destination.\n");
        result = 1;
        goto done;
    }

    if (setup_srcs()) {
        eprintf("Done initializing %d sources.\n", num_sources);
        result = 1;
        goto done;
    }

    wait_ready(&done);

    if (autoconnect && setup_maps()) {
        eprintf("Error setting map.\n");
        result = 1;
        goto done;
    }

    loop();

    if (sent != received) {
        eprintf("Not all sent messages were received.\n");
        eprintf("Updated value %d time%s, but received %d of them.\n",
                sent, sent == 1 ? "" : "s", received);
        result = 1;
    }

done:
    cleanup_dst();
    cleanup_src();
    printf("...................Test %s\x1B[0m.\n",
           result ? "\x1B[31mFAILED" : "\x1B[32mPASSED");
    return result;
}

