#include "../src/mpr_internal.h"
#include <mpr/mpr.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <lo/lo.h>
#include <unistd.h>
#include <signal.h>
#include <string.h>

#define eprintf(format, ...) do {               \
    if (verbose)                                \
        fprintf(stdout, format, ##__VA_ARGS__); \
} while(0)

int verbose = 1;
int iterations = 100;
int autoconnect = 1;
int period = 100;
int automate = 1;

mpr_dev src = 0;
mpr_dev dst = 0;
mpr_sig sendsig = 0;
mpr_sig recvsig = 0;

int sent = 0;
int received = 0;
int done = 0;

/*! Creation of a local source. */
int setup_src()
{
    src = mpr_dev_new("testinstance-send", 0);
    if (!src)
        goto error;

    float mn=0, mx=10;
    int num_inst = 10, stl = MPR_STEAL_OLDEST;

    sendsig = mpr_sig_new(src, MPR_DIR_OUT, "outsig", 1, MPR_FLT, NULL,
                          &mn, &mx, &num_inst, NULL, 0);
    if (!sendsig)
        goto error;
    mpr_obj_set_prop((mpr_obj)sendsig, MPR_PROP_STEAL_MODE, NULL, 1, MPR_INT32,
                     &stl, 1);

    eprintf("Output signal added with %i instances.\n",
            mpr_sig_get_num_inst(sendsig, MPR_STATUS_ALL));

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

void handler(mpr_sig sig, mpr_sig_evt e, mpr_id inst, int len, mpr_type type,
             const void *val, mpr_time t)
{
    const char *name = mpr_obj_get_prop_as_str((mpr_obj)sig, MPR_PROP_NAME, NULL);

    if (e & MPR_SIG_INST_OFLW) {
        eprintf("OVERFLOW!! ALLOCATING ANOTHER INSTANCE.\n");
        mpr_sig_reserve_inst(sig, 1, 0, 0);
    }
    else if (e & MPR_SIG_REL_UPSTRM) {
        eprintf("UPSTREAM RELEASE!! RELEASING LOCAL INSTANCE.\n");
        mpr_sig_release_inst(sig, inst, MPR_NOW);
    }
    else if (val) {
        eprintf("--> destination %s instance %i got %f\n", name, (int)inst,
                (*(float*)val));
        received++;
    }
    else {
        eprintf("--> destination %s instance %i got NULL\n", name, (int)inst);
        mpr_sig_release_inst(sig, inst, MPR_NOW);
    }
}

/*! Creation of a local destination. */
int setup_dst()
{
    dst = mpr_dev_new("testinstance-recv", 0);
    if (!dst)
        goto error;

    float mn=0;//, mx=1;

    // Specify 0 instances since we wish to use specific ids
    int num_inst = 0;
    recvsig = mpr_sig_new(dst, MPR_DIR_IN, "insig", 1, MPR_FLT, NULL, &mn,
                          NULL, &num_inst, handler, MPR_SIG_UPDATE);
    if (!recvsig)
        goto error;

    int i;
    for (i=2; i<10; i+=2) {
        mpr_sig_reserve_inst(recvsig, 1, (mpr_id*)&i, 0);
    }

    eprintf("Input signal added with %i instances.\n",
            mpr_sig_get_num_inst(recvsig, MPR_STATUS_ALL));

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

void wait_local_devs()
{
    while (!done && !(mpr_dev_get_is_ready(src) && mpr_dev_get_is_ready(dst))) {
        mpr_dev_poll(src, 25);
        mpr_dev_poll(dst, 25);
    }
}

void print_instance_ids(mpr_sig sig)
{
    int i, n = mpr_sig_get_num_inst(sig, MPR_STATUS_ACTIVE);
    const char *name = mpr_obj_get_prop_as_str((mpr_obj)sig, MPR_PROP_NAME, NULL);
    eprintf("%s: [ ", name);
    for (i=0; i<n; i++) {
        eprintf("%2i, ", (int)mpr_sig_get_inst_id(sig, i, MPR_STATUS_ACTIVE));
    }
    eprintf("\b\b ]   ");
}

void print_instance_vals(mpr_sig sig)
{
    int i, id, n = mpr_sig_get_num_inst(sig, MPR_STATUS_ACTIVE);
    const char *name = mpr_obj_get_prop_as_str((mpr_obj)sig, MPR_PROP_NAME, NULL);
    eprintf("%s: [ ", name);
    for (i=0; i<n; i++) {
        id = mpr_sig_get_inst_id(sig, i, MPR_STATUS_ACTIVE);
        float *val = (float*)mpr_sig_get_value(sig, id, 0);
        if (val)
            printf("%2.0f, ", *val);
        else
            printf("––, ");
    }
    eprintf("\b\b ]   ");
}

void map_sigs()
{
    mpr_map map = mpr_map_new(1, &sendsig, 1, &recvsig);
    const char *expr = "y{-1}=-10;y=y{-1}+1";
    mpr_obj_set_prop((mpr_obj)map, MPR_PROP_EXPR, NULL, 1, MPR_STR, expr, 1);
    mpr_obj_push((mpr_obj)map);

    // wait until mapping has been established
    while (!done && !mpr_map_get_is_ready(map)) {
        mpr_dev_poll(src, 10);
        mpr_dev_poll(dst, 10);
    }
}

void loop()
{
    eprintf("-------------------- GO ! --------------------\n");
    int i = 0;
    float value = 0;
    mpr_id inst;

    while (i < iterations && !done) {
        // here we should create, update and destroy some instances
        inst = (rand() % 10) + 5;
        switch (rand() % 5) {
            case 0:
                // try to destroy an instance
                eprintf("--> Retiring sender instance %"PR_MPR_ID"\n", inst);
                mpr_sig_release_inst(sendsig, inst, MPR_NOW);
                break;
            default:
                // try to update an instance
                value = (rand() % 10) * 1.0f;
                mpr_sig_set_value(sendsig, inst, 1, MPR_FLT, &value, MPR_NOW);
                eprintf("--> sender instance %"PR_MPR_ID" updated to %f\n",
                        inst, value);
                sent++;
                break;
        }

        mpr_dev_poll(dst, period);
        mpr_dev_poll(src, 0);
        i++;

        if (verbose) {
            print_instance_ids(sendsig);
            print_instance_ids(recvsig);
            eprintf("\n");

            print_instance_vals(sendsig);
            print_instance_vals(recvsig);
            printf("\n");
        }
        else {
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
    int i, j, result = 0, stats[6];

    // process flags for -v verbose, -t terminate, -h help
    for (i = 1; i < argc; i++) {
        if (argv[i] && argv[i][0] == '-') {
            int len = strlen(argv[i]);
            for (j = 1; j < len; j++) {
                switch (argv[i][j]) {
                    case 'h':
                        printf("testinstance.c: possible arguments "
                               "-f fast (execute quickly), "
                               "-q quiet (suppress output), "
                               "-h help\n");
                        return 1;
                        break;
                    case 'f':
                        period = 1;
                        break;
                    case 'q':
                        verbose = 0;
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

    if (setup_src()) {
        eprintf("Done initializing source.\n");
        result = 1;
        goto done;
    }

    wait_local_devs();

    if (automate)
        map_sigs();

    eprintf("\n**********************************************\n");
    eprintf("************ NO INSTANCE STEALING ************\n");
    loop();

    stats[0] = sent;
    stats[1] = received;

    j = mpr_sig_get_num_inst(sendsig, MPR_STATUS_ALL);
    for (i = 0; i < j; i++) {
        mpr_sig_release_inst(sendsig, mpr_sig_get_inst_id(sendsig, i, MPR_STATUS_ALL), MPR_NOW);
    }
    sent = received = 0;

    eprintf("\n**********************************************\n");
    eprintf("************ STEAL OLDEST INSTANCE ***********\n");
    int stl = MPR_STEAL_OLDEST;
    mpr_obj_set_prop((mpr_obj)recvsig, MPR_PROP_STEAL_MODE, NULL, 1, MPR_INT32,
                     &stl, 1);
    if (!verbose)
        printf("\n");
    loop();

    stats[2] = sent;
    stats[3] = received;
    sent = received = 0;

    j = mpr_sig_get_num_inst(sendsig, MPR_STATUS_ALL);
    for (i = 0; i < j; i++) {
        mpr_sig_release_inst(sendsig, mpr_sig_get_inst_id(sendsig, i, MPR_STATUS_ALL), MPR_NOW);
    }
    sent = received = 0;

    eprintf("\n**********************************************\n");
    eprintf("*********** CALLBACK -> ADD INSTANCE *********\n");
    stl = MPR_STEAL_NONE;
    mpr_obj_set_prop((mpr_obj)recvsig, MPR_PROP_STEAL_MODE, NULL, 1, MPR_INT32,
                     &stl, 1);
    mpr_sig_set_cb(recvsig, handler,
                   MPR_SIG_UPDATE | MPR_SIG_INST_OFLW | MPR_SIG_REL_UPSTRM);
    if (!verbose)
        printf("\n");
    loop();

    stats[4] = sent;
    stats[5] = received;

    eprintf("NO STEALING: sent %i updates, received %i updates (mismatch is OK).\n",
            stats[0], stats[1]);
    eprintf("STEAL OLDEST: sent %i updates, received %i updates (mismatch is OK).\n",
            stats[2], stats[3]);
    eprintf("ADD INSTANCE: sent %i updates, received %i updates.\n",
            stats[4], stats[5]);

    result = (stats[4] != stats[5]);

  done:
    cleanup_dst();
    cleanup_src();
    printf("...................Test %s\x1B[0m.\n",
           result ? "\x1B[31mFAILED" : "\x1B[32mPASSED");
    return result;
}
