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
mpr_sig multisend = 0;
mpr_sig multirecv = 0;
mpr_sig monosend = 0;

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

    multisend = mpr_sig_new(src, MPR_DIR_OUT, "multisend", 1, MPR_FLT,
                            NULL, &mn, &mx, &num_inst, NULL, 0);
    monosend = mpr_sig_new(src, MPR_DIR_OUT, "monosend", 1, MPR_FLT,
                           NULL, &mn, &mx, NULL, NULL, 0);
    if (!multisend || !monosend)
        goto error;
    mpr_obj_set_prop((mpr_obj)multisend, MPR_PROP_STEAL_MODE, NULL, 1,
                     MPR_INT32, &stl, 1);

    eprintf("Output signal added with %i instances.\n",
            mpr_sig_get_num_inst(multisend, MPR_STATUS_ALL));

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

    float mn=0;

    // Specify 0 instances since we wish to use specific ids
    int num_inst = 0;
    multirecv = mpr_sig_new(dst, MPR_DIR_IN, "multirecv", 1, MPR_FLT, NULL,
                            &mn, NULL, &num_inst, handler, MPR_SIG_UPDATE);
    if (!multirecv)
        goto error;

    int i;
    for (i=2; i<10; i+=2) {
        mpr_sig_reserve_inst(multirecv, 1, (mpr_id*)&i, 0);
    }

    eprintf("Input signal added with %i instances.\n",
            mpr_sig_get_num_inst(multirecv, MPR_STATUS_ALL));

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
    if (i)
        eprintf("\b\b ");
    eprintf("]   ");
}

void print_instance_vals(mpr_sig sig)
{
    int i, n = mpr_sig_get_num_inst(sig, MPR_STATUS_ACTIVE);
    const char *name = mpr_obj_get_prop_as_str((mpr_obj)sig, MPR_PROP_NAME, NULL);
    eprintf("%s: [ ", name);
    for (i=0; i<n; i++) {
        mpr_id id = mpr_sig_get_inst_id(sig, i, MPR_STATUS_ACTIVE);
        float *val = (float*)mpr_sig_get_value(sig, id, 0);
        if (val)
            printf("%2.0f, ", *val);
        else
            printf("––, ");
    }
    if (i)
        eprintf("\b\b ");
    eprintf("]   ");
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
                eprintf("--> Retiring multisend instance %"PR_MPR_ID"\n", inst);
                mpr_sig_release_inst(multisend, inst, MPR_NOW);
                break;
            default:
                // try to update an instance
                value = (rand() % 10) * 1.0f;
                eprintf("--> Updating multisend instance %"PR_MPR_ID" to %f\n",
                        inst, value);
                mpr_sig_set_value(multisend, inst, 1, MPR_FLT, &value, MPR_NOW);
                sent++;
                break;
        }
        // also update mono-instance signal
        eprintf("--> Updating monosend to %f\n", value);
        mpr_sig_set_value(monosend, 0, 1, MPR_INT32, &i, MPR_NOW);

        mpr_dev_poll(dst, period);
        mpr_dev_poll(src, 0);
        i++;

        if (verbose) {
            printf("monosend: n/a ");
            print_instance_ids(multisend);
            print_instance_ids(multirecv);
            eprintf("\n");

            printf("monosend: %2.0f  ", *(float*)mpr_sig_get_value(monosend, 0, 0));
            print_instance_vals(multisend);
            print_instance_vals(multirecv);
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
    int i, j, result = 0, stats[10];

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

    // create multi-instance -> multi-instance map
    mpr_map map = mpr_map_new(1, &multisend, 1, &multirecv);
    const char *expr = "y{-1}=-10;y=y{-1}+1";
    mpr_obj_set_prop((mpr_obj)map, MPR_PROP_EXPR, NULL, 1, MPR_STR, expr, 1);
    mpr_obj_push((mpr_obj)map);

    // wait until mapping has been established
    while (!done && !mpr_map_get_is_ready(map)) {
        mpr_dev_poll(src, 10);
        mpr_dev_poll(dst, 10);
    }

    eprintf("\n**********************************************\n");
    eprintf("************ NO INSTANCE STEALING ************\n");
    loop();

    stats[0] = sent;
    stats[1] = received;

    j = mpr_sig_get_num_inst(multisend, MPR_STATUS_ALL);
    for (i = 0; i < j; i++) {
        mpr_sig_release_inst(multisend,
                             mpr_sig_get_inst_id(multisend, i, MPR_STATUS_ALL),
                             MPR_NOW);
    }
    sent = received = 0;

    eprintf("\n**********************************************\n");
    eprintf("************ STEAL OLDEST INSTANCE ***********\n");
    int stl = MPR_STEAL_OLDEST;
    mpr_obj_set_prop((mpr_obj)multirecv, MPR_PROP_STEAL_MODE, NULL, 1, MPR_INT32,
                     &stl, 1);
    if (!verbose)
        printf("\n");
    loop();

    stats[2] = sent;
    stats[3] = received;

    j = mpr_sig_get_num_inst(multisend, MPR_STATUS_ALL);
    for (i = 0; i < j; i++) {
        mpr_sig_release_inst(multisend,
                             mpr_sig_get_inst_id(multisend, i, MPR_STATUS_ALL),
                             MPR_NOW);
    }
    sent = received = 0;

    eprintf("\n**********************************************\n");
    eprintf("*********** CALLBACK -> ADD INSTANCE *********\n");
    stl = MPR_STEAL_NONE;
    mpr_obj_set_prop((mpr_obj)multirecv, MPR_PROP_STEAL_MODE, NULL, 1, MPR_INT32,
                     &stl, 1);
    mpr_sig_set_cb(multirecv, handler,
                   MPR_SIG_UPDATE | MPR_SIG_INST_OFLW | MPR_SIG_REL_UPSTRM);
    if (!verbose)
        printf("\n");
    loop();

    stats[4] = sent;
    stats[5] = received;

    j = mpr_sig_get_num_inst(multisend, MPR_STATUS_ALL);
    for (i = 0; i < j; i++) {
        mpr_sig_release_inst(multisend,
                             mpr_sig_get_inst_id(multisend, i, MPR_STATUS_ALL),
                             MPR_NOW);
    }
    sent = received = 0;

    // allow time for change to take effect
    // TODO: ensure map release also releases instances properly so this is not necessary
    mpr_dev_poll(src, 10);
    mpr_dev_poll(dst, 10);

    eprintf("\n**********************************************\n");
    eprintf("****** MIXED INSTANCING -> SRC PROCESSING ****\n");

    // create [multi-instance, mono-instance] -> multiinstance map
    mpr_map_release(map);
    // both source signals belong to the same device
    mpr_sig srcs[2] = {multisend, monosend};
    map = mpr_map_new(2, srcs, 1, &multirecv);
    expr = "y = x0 * x1";
    mpr_obj_set_prop((mpr_obj)map, MPR_PROP_EXPR, NULL, 1, MPR_STR, expr, 1);
    mpr_obj_push((mpr_obj)map);

    // wait until mapping has been established
    while (!done && !mpr_map_get_is_ready(map)) {
        mpr_dev_poll(src, 10);
        mpr_dev_poll(dst, 10);
    }

    if (!verbose)
        printf("\n");
    loop();

    stats[6] = sent;
    stats[7] = received;

    j = mpr_sig_get_num_inst(multisend, MPR_STATUS_ALL);
    for (i = 0; i < j; i++) {
        mpr_sig_release_inst(multisend,
                             mpr_sig_get_inst_id(multisend, i, MPR_STATUS_ALL),
                             MPR_NOW);
    }
    sent = received = 0;

    eprintf("\n**********************************************\n");
    eprintf("****** MIXED INSTANCING -> DST PROCESSING ****\n");

    // move processing to destination device
    mpr_loc loc = MPR_LOC_DST;
    mpr_obj_set_prop((mpr_obj)map, MPR_PROP_PROCESS_LOC, NULL, 1, MPR_INT32,
                     &loc, 1);
    mpr_obj_push((mpr_obj)map);

    // allow time for change to take effect
    mpr_dev_poll(src, 10);
    mpr_dev_poll(dst, 10);

    if (!verbose)
        printf("\n");
    loop();

    stats[8] = sent;
    stats[9] = received;
    sent = received = 0;

    eprintf("NO STEALING: sent %i updates, received %i updates (mismatch is OK).\n",
            stats[0], stats[1]);
    eprintf("STEAL OLDEST: sent %i updates, received %i updates (mismatch is OK).\n",
            stats[2], stats[3]);
    eprintf("ADD INSTANCE: sent %i updates, received %i updates.\n",
            stats[4], stats[5]);
    eprintf("SRC-SIDE MIXED INSTANCING: sent %i updates, received %i updates.\n",
            stats[6], stats[7]);
    eprintf("DST-SIDE MIXED INSTANCING: sent %i updates, received %i updates.\n",
            stats[8], stats[9]);

    result = (stats[4] != stats[5]);

  done:
    cleanup_dst();
    cleanup_src();
    printf("...................Test %s\x1B[0m.\n",
           result ? "\x1B[31mFAILED" : "\x1B[32mPASSED");
    return result;
}
