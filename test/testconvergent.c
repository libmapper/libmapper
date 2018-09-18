#include <mpr/mpr.h>
#include <stdio.h>
#include <math.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>

#define eprintf(format, ...) do {               \
    if (verbose)                                \
        fprintf(stdout, format, ##__VA_ARGS__); \
} while(0)

#define NUM_SRCS 2

int verbose = 1;
int terminate = 0;
int autoconnect = 1;
int done = 0;
int period = 100;

mpr_dev srcs[NUM_SRCS];
mpr_dev dst = 0;
mpr_sig sendsig[NUM_SRCS][2];
mpr_sig recvsig = 0;

int sent = 0;
int received = 0;

int setup_srcs()
{
    int i, mni=0, mxi=1;
    float mnf=0, mxf=1;
    for (i = 0; i < NUM_SRCS; i++)
        srcs[i] = 0;
    for (i = 0; i < NUM_SRCS; i++) {
        srcs[i] = mpr_dev_new("testconvergent-send", 0);
        if (!srcs[i])
            goto error;
        eprintf("source %d created.\n", i);
        sendsig[i][0] = mpr_sig_new(srcs[i], MPR_DIR_OUT, 1, "sendsig1", 1,
                                    MPR_INT32, NULL, &mni, &mxi, NULL, 0);
        sendsig[i][1] = mpr_sig_new(srcs[i], MPR_DIR_OUT, 1, "sendsig2", 1,
                                    MPR_FLT, NULL, &mnf, &mxf, NULL, 0);
    }
    return 0;

  error:
    for (i = 0; i < NUM_SRCS; i++) {
        if (srcs[i])
            mpr_dev_free(srcs[i]);
    }
    return 1;
}

void cleanup_src()
{
    for (int i = 0; i < NUM_SRCS; i++) {
        if (srcs[i]) {
            eprintf("Freeing source %d... ", i);
            fflush(stdout);
            mpr_dev_free(srcs[i]);
            eprintf("ok\n");
        }
    }
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
    recvsig = mpr_sig_new(dst, MPR_DIR_IN, 1, "recvsig", 1, MPR_FLT, NULL,
                          &mn, &mx, handler, MPR_SIG_UPDATE);

    eprintf("Input signal 'insig' registered.\n");
    eprintf("Number of inputs: %d\n",
            mpr_list_get_count(mpr_dev_get_sigs(dst, MPR_DIR_IN)));
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
    mpr_sig all_srcs[2] = {sendsig[0][0], sendsig[1][1]};

    mpr_map map = mpr_map_new(2, all_srcs, 1, &recvsig);

    // build expression string
    char expr[64];
    snprintf(expr, 64, "y=x%d-_x%d", mpr_map_get_sig_index(map, sendsig[0][0]),
             mpr_map_get_sig_index(map, sendsig[1][1]));
    mpr_obj_set_prop(map, MPR_PROP_EXPR, NULL, 1, MPR_STR, expr, 1);

    mpr_obj_push(map);

    // wait until mappings have been established
    int i;
    while (!done && !mpr_map_ready(map)) {
        for (i = 0; i < NUM_SRCS; i++)
            mpr_dev_poll(srcs[i], 10);
        mpr_dev_poll(dst, 10);
    }

    return 0;
}

void wait_ready()
{
    int i, ready = 0;
    while (!done && !ready) {
        ready = 1;
        for (i = 0; i < NUM_SRCS; i++) {
            mpr_dev_poll(srcs[i], 25);
            if (!mpr_dev_ready(srcs[i])) {
                ready = 0;
                break;
            }
        }
        mpr_dev_poll(dst, 25);
        if (!mpr_dev_ready(dst))
            ready = 0;
    }
}

void loop()
{
    eprintf("Polling device..\n");
    int i = 0, j;
    float f = 0.;

    const char *sig0name, *sig1name;
    mpr_obj_get_prop_by_idx(sendsig[0][0], MPR_PROP_NAME, NULL, NULL, NULL,
                            (const void**)&sig0name, 0);
    mpr_obj_get_prop_by_idx(sendsig[0][1], MPR_PROP_NAME, NULL, NULL, NULL,
                            (const void**)&sig1name, 0);

    while ((!terminate || i < 50) && !done) {
        for (j = 0; j < NUM_SRCS; j++) {
            mpr_dev_poll(srcs[j], 0);
        }

        eprintf("Updating signals: %s = %i, %s = %f\n", sig0name, i, sig1name,
                i * 2.f);
        mpr_sig_set_value(sendsig[0][0], 0, 1, MPR_INT32, &i, MPR_NOW);
        mpr_sig_set_value(sendsig[1][0], 0, 1, MPR_INT32, &i, MPR_NOW);
        f = i * 2;
        mpr_sig_set_value(sendsig[0][1], 0, 1, MPR_FLT, &f, MPR_NOW);
        mpr_sig_set_value(sendsig[1][1], 0, 1, MPR_FLT, &f, MPR_NOW);

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
        eprintf("Done initializing %d sources.\n", NUM_SRCS);
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
