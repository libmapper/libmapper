#include "../src/device.h"
#include <mapper/mapper.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <math.h>
#include <ctype.h>
#include <lo/lo.h>
#ifdef WIN32
#include <io.h>
#else
#include <unistd.h>
#endif
#include <signal.h>
#include <string.h>

int verbose = 1;
int terminate = 0;
int shared_graph = 0;
int iterations = 100;
int autoconnect = 1;
int period = 100;
int automate = 1;

mpr_dev src = 0;
mpr_dev dst = 0;
mpr_sig multisend = 0;
mpr_sig multirecv = 0;
mpr_sig monosend = 0;
mpr_sig monorecv = 0;

int ephemeral = 1;
int test_counter = 0;
int received = 0;
int done = 0;

static void eprintf(const char *format, ...)
{
    va_list args;
    if (!verbose)
        return;
    va_start(args, format);
    vprintf(format, args);
    va_end(args);
}

const char *instance_type_names[] = { "?", "SINGLETON", "INSTANCED", "MIXED" };

/* TODO: add all-singleton convergent, all-instanced convergent */
typedef enum {
    SNGL = 0x01,   /* singleton */
    INST = 0x02,   /* instanced */
    BOTH = 0x03    /* mixed convergent, same device */
} instance_type;

const char *oflw_action_names[] = { "", "steal oldest", "steal newest", "add instance" };

typedef enum {
    NONE = MPR_STEAL_NONE,
    OLD = MPR_STEAL_OLDEST,
    NEW = MPR_STEAL_NEWEST,
    ADD = 3
} oflw_action;

typedef struct _test_config {
    int             test_id;
    instance_type   src_type;
    instance_type   dst_type;
    instance_type   map_type;
    mpr_loc         process_loc;
    oflw_action     oflw_action;
    const char      *expr;
    float           count_mult;
    float           count_mult_shared;
    float           count_mult_ephem;
    float           count_mult_ephem_shared;
    float           count_epsilon;
    int             same_val;
} test_config;

#define EXPR1 "alive=n>=5;y=x;n=(n+1)%10;"
#define EXPR2 "y=x.instance.mean()"

/* TODO: test received values */
/* TODO: these should work with count_epsilon=0.0 */
test_config test_configs[] = {
    /* singleton ––> singleton; shouldn't make a difference if map is instanced */
    {  1, SNGL, SNGL, SNGL, MPR_LOC_SRC, NONE, NULL,  1.0,  1.0,  1.0,  1.0,  0.01,  0 },
    {  2, SNGL, SNGL, SNGL, MPR_LOC_DST, NONE, NULL,  1.0,  1.0,  1.0,  1.0,  0.01,  0 },

    /* singleton ==> singleton; shouldn't make a difference if map is instanced */
    {  3, SNGL, SNGL, INST, MPR_LOC_SRC, NONE, NULL,  1.0,  1.0,  1.0,  1.0,  0.01,  0 },
    {  4, SNGL, SNGL, INST, MPR_LOC_DST, NONE, NULL,  1.0,  1.0,  1.0,  1.0,  0.01,  0 },

    /* singleton ––> instanced; control all active instances */
    {  5, SNGL, INST, SNGL, MPR_LOC_SRC, NONE, NULL,  2.0,  2.0,  2.0,  2.0,  0.01,  1 },
    {  6, SNGL, INST, SNGL, MPR_LOC_DST, NONE, NULL,  2.0,  2.0,  2.0,  2.0,  0.01,  1 },

    /* singleton ==> instanced; control a single instance (default) */
    /* TODO: check that instance is released on map_free() */
    {  7, SNGL, INST, INST, MPR_LOC_SRC, NONE, NULL,  1.0,  1.0,  1.0,  1.0,  0.01,  0 },
    {  8, SNGL, INST, INST, MPR_LOC_DST, NONE, NULL,  1.0,  1.0,  1.0,  1.0,  0.01,  0 },

    /* instanced ––> singleton; any source instance updates destination */
    {  9, INST, SNGL, SNGL, MPR_LOC_SRC, NONE, NULL,  1.0,  1.0,  1.0,  1.0,  0.01,  0 },
    /* ... but when processing @dst only the last instance update will trigger handler */
    { 10, INST, SNGL, SNGL, MPR_LOC_DST, NONE, NULL,  1.0,  1.0,  1.0,  1.0,  0.01,  0 },

    /* instanced ==> singleton; one src instance updates dst (default) */
    /* CHECK: if controlling instance is released, move to next updated inst */
    { 11, INST, SNGL, INST, MPR_LOC_SRC, NONE, NULL,  1.0,  1.0,  1.0,  1.0,  0.01,  0 },
    { 12, INST, SNGL, INST, MPR_LOC_DST, NONE, NULL,  1.0,  1.0,  1.0,  1.0,  0.01,  0 },

    /* instanced ––> instanced; any src instance updates all dst instances */
    /* source signal does not know about active destination instances */
    { 13, INST, INST, SNGL, MPR_LOC_SRC, NONE, NULL,  2.0,  2.0,  2.0,  2.0,  0.01,  1 },
    { 14, INST, INST, SNGL, MPR_LOC_DST, NONE, NULL,  2.0,  2.0,  2.0,  2.0,  0.01,  1 },

    /* instanced ==> instanced; no stealing */
    { 15, INST, INST, INST, MPR_LOC_SRC, NONE, NULL,  4.0,  4.0,  2.61, 2.61, 0.01,  0 },
    { 16, INST, INST, INST, MPR_LOC_DST, NONE, NULL,  4.0,  4.0,  2.26, 2.61, 0.01,  0 },

    /* instanced ==> instanced; steal newest instance */
    { 17, INST, INST, INST, MPR_LOC_SRC, NEW,  NULL,  4.0,  4.0,  2.61, 2.61, 0.01,  0 },
    /* TODO: verify that shared_graph version is behaving properly */
    { 18, INST, INST, INST, MPR_LOC_DST, NEW,  NULL,  4.0,  4.0,  2.26, 2.61, 0.01,  0 },

    /* instanced ==> instanced; steal oldest instance */
    /* TODO: document why multiplier is not 5.0 */
    { 19, INST, INST, INST, MPR_LOC_SRC, OLD,  NULL,  4.0,  4.0,  2.61, 2.61, 0.01,  0 },
    { 20, INST, INST, INST, MPR_LOC_DST, OLD,  NULL,  4.0,  4.0,  2.26, 2.61, 0.01,  0 },

    /* instanced ==> instanced; add instances if needed */
    { 21, INST, INST, INST, MPR_LOC_SRC, ADD,  NULL,  4.0,  4.0,  2.61, 2.61, 0.01,  0 },
    { 22, INST, INST, INST, MPR_LOC_DST, ADD,  NULL,  4.0,  4.0,  2.26, 2.61, 0.01,  0 },

    /* mixed ––> singleton */
    /* for src processing the update count is additive since the destination has only one instance */
    { 23, BOTH, SNGL, SNGL, MPR_LOC_SRC, NONE, NULL,  1.0,  1.0,  1.0,  1.0,  0.01,  0 },
    /* TODO: we should default to dst processing for this configuration */
    { 24, BOTH, SNGL, SNGL, MPR_LOC_DST, NONE, NULL,  1.0,  1.0,  1.0,  1.0,  0.01,  0 },

    /* mixed ==> singleton */
    /* for src processing we expect one update per iteration */
    { 25, BOTH, SNGL, INST, MPR_LOC_SRC, NONE, NULL,  1.0,  1.0,  1.0,  1.0,  0.01,  0 },
    /* for dst processing we expect one update per iteration */
    { 26, BOTH, SNGL, INST, MPR_LOC_DST, NONE, NULL,  1.0,  1.0,  1.0,  1.0,  0.01,  0 },

    /* mixed ––> instanced */
    { 27, BOTH, INST, SNGL, MPR_LOC_SRC, NONE, NULL,  2.0,  2.0,  2.0,  2.0,  0.01,  1 },
    /* each active instance should receive 1 update per iteration */
    { 28, BOTH, INST, SNGL, MPR_LOC_DST, NONE, NULL,  2.0,  2.0,  2.0,  2.0,  0.01,  1 },

    /* mixed ==> instanced */
    { 29, BOTH, INST, INST, MPR_LOC_SRC, NONE, NULL,  4.0,  4.0,  2.61, 2.61, 0.01,  0 },
    { 30, BOTH, INST, INST, MPR_LOC_DST, NONE, NULL,  4.0,  4.0,  2.26, 2.61, 0.01,  0 },

    /* singleton ––> instanced; in-map instance management */
    /* Should we be updating all active destination instances here? */
    { 31, SNGL, INST, SNGL, MPR_LOC_SRC, NONE, EXPR1, 1.0,  1.0,  1.0,  1.0,  0.01,  1 },
    { 32, SNGL, INST, SNGL, MPR_LOC_DST, NONE, EXPR1, 1.0,  1.0,  1.0,  1.0,  0.01,  1 },

    /* singleton ==> instanced; in-map instance management */
    { 33, SNGL, INST, INST, MPR_LOC_SRC, NONE, EXPR1, 0.5,  0.5,  0.5,  0.5,  0.01,  0 },
    { 34, SNGL, INST, INST, MPR_LOC_DST, NONE, EXPR1, 0.5,  0.5,  0.5,  0.5,  0.01,  0 },

    /* instanced ––> singleton; instance reduce expression */
    { 35, INST, SNGL, SNGL, MPR_LOC_SRC, NONE, EXPR2, 1.0,  1.0,  1.0,  1.0,  0.01,  0 },
    { 36, INST, SNGL, SNGL, MPR_LOC_DST, NONE, EXPR2, 1.0,  1.0,  1.0,  1.0,  0.01,  0 },

    /* instanced ==> singleton; instance reduce expression */
    { 37, INST, SNGL, INST, MPR_LOC_SRC, NONE, EXPR2, 1.0,  1.0,  1.0,  1.0,  0.01,  0 },
    { 38, INST, SNGL, INST, MPR_LOC_DST, NONE, EXPR2, 1.0,  1.0,  1.0,  1.0,  0.01,  0 },

    /* instanced ––> instanced; instance reduce expression */
    /* result of expression should update all active destination instances */
    { 39, INST, INST, SNGL, MPR_LOC_SRC, NONE, EXPR2, 2.0,  2.0,  2.0,  2.0,  0.01,  1 },
    { 40, INST, INST, SNGL, MPR_LOC_DST, NONE, EXPR2, 2.0,  2.0,  2.0,  2.0,  0.01,  1 },

    /* instanced ==> instanced; instance reduce expression */
    // TODO: currently each update is a new instance, should be a stream of one map-managed instance
    { 41, INST, INST, INST, MPR_LOC_SRC, NONE, EXPR2, 1.0,  1.0,  1.0,  1.0,  0.01,  1 },
    { 42, INST, INST, INST, MPR_LOC_DST, NONE, EXPR2, 1.0,  1.0,  0.5,  1.0,  0.01,  1 },

    /* work in progress:
     * instanced ––> instanced; in-map instance management (late start, early release, ad hoc)
     * instanced ==> instanced; in-map instance management (late start, early release, ad hoc)
     * mixed ––> instanced; in-map instance management (late start, early release, ad hoc)
     * mixed ==> instanced; in-map instance management (late start, early release, ad hoc)
     */

/*    { 35, INST, INST, INST, MPR_LOC_SRC, NONE, "alive=n>=3;y=x;n=(n+1)%10;", 0.5, 0. },
    { 36, INST, INST, INST, MPR_LOC_DST, NONE, "alive=n>=3;y=x;n=(n+1)%10;", 0.5, 0. },*/

    /* future work:
     * in-map instance reduce ==> instanced dst (ensure dst release when all src are released)
     * src instance pooling (convergent maps)
     * dst instance pooling (divergent maps)
     * src & dst instance pooling (complex maps)
     */
};
const int NUM_TESTS =
    sizeof(test_configs)/sizeof(test_configs[0]);

/*! Creation of a local source. */
int setup_src(mpr_graph g, const char *iface)
{
    float mn=0, mx=10;
    int num_inst = 10;

    src = mpr_dev_new("testinstance_no_cb-send", g);
    if (!src)
        goto error;
    if (iface)
        mpr_graph_set_interface(mpr_obj_get_graph((mpr_obj)src), iface);
    eprintf("source created using interface %s.\n",
            mpr_graph_get_interface(mpr_obj_get_graph((mpr_obj)src)));

    multisend = mpr_sig_new(src, MPR_DIR_OUT, "multisend", 1, MPR_FLT, NULL,
                            &mn, &mx, &num_inst, NULL, 0);
    monosend = mpr_sig_new(src, MPR_DIR_OUT, "monosend", 1, MPR_FLT, NULL,
                           &mn, &mx, NULL, NULL, 0);
    if (!multisend || !monosend)
        goto error;

    eprintf("Output signal added with %i instances.\n",
            mpr_sig_get_num_inst(multisend, MPR_STATUS_ANY));

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

/*! Creation of a local destination. */
int setup_dst(mpr_graph g, const char *iface)
{
    float mn=0;
    int i, num_inst;

    dst = mpr_dev_new("testinstance_no_cb-recv", g);
    if (!dst)
        goto error;
    if (iface)
        mpr_graph_set_interface(mpr_obj_get_graph((mpr_obj)dst), iface);
    eprintf("destination created using interface %s.\n",
            mpr_graph_get_interface(mpr_obj_get_graph((mpr_obj)dst)));

    /* Specify 0 instances since we wish to use specific ids */
    num_inst = 0;
    multirecv = mpr_sig_new(dst, MPR_DIR_IN, "multirecv", 1, MPR_FLT, NULL,
                            &mn, NULL, &num_inst, NULL, 0);
    mpr_obj_set_prop((mpr_obj)multirecv, MPR_PROP_EPHEM, NULL, 1, MPR_INT32, &ephemeral, 1);

    monorecv = mpr_sig_new(dst, MPR_DIR_IN, "monorecv", 1, MPR_FLT, NULL,
                           &mn, NULL, 0, NULL, 0);
    if (!multirecv || !monorecv)
        goto error;

    for (i = 2; i < 10; i += 2) {
        mpr_sig_reserve_inst(multirecv, 1, (mpr_id*)&i, 0);
    }

    eprintf("Input signal added with %i instances.\n",
            mpr_sig_get_num_inst(multirecv, MPR_STATUS_ANY));

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

void print_instance_ids(mpr_sig sig)
{
    int i, n = mpr_sig_get_num_inst(sig, MPR_STATUS_ANY);
    const char *name = mpr_obj_get_prop_as_str((mpr_obj)sig, MPR_PROP_NAME, NULL);
    eprintf("%s: [ ", name);
    for (i=0; i<n; i++) {
        eprintf("%4i, ", (int)mpr_sig_get_inst_id(sig, i, MPR_STATUS_ANY));
    }
    if (i)
        eprintf("\b\b ");
    eprintf("]   ");
}

void print_instance_idx(mpr_sig sig)
{
    int i, n = mpr_sig_get_num_inst(sig, MPR_STATUS_ANY);
    const char *name = mpr_obj_get_prop_as_str((mpr_obj)sig, MPR_PROP_NAME, NULL);
    mpr_sig_inst *si = mpr_local_sig_get_insts((mpr_local_sig)sig);
    eprintf("%s: [ ", name);
    for (i = 0; i < n; i++) {
        eprintf("%4i, ", mpr_sig_get_inst_idx(si[i]));
    }
    if (i)
        eprintf("\b\b ");
    eprintf("]   ");
}

void print_instance_vals(mpr_sig sig)
{
    int i, n = mpr_sig_get_num_inst(sig, MPR_STATUS_ANY);
    const char *name = mpr_obj_get_prop_as_str((mpr_obj)sig, MPR_PROP_NAME, NULL);
    eprintf("%s: [ ", name);
    for (i = 0; i < n; i++) {
        mpr_id id = mpr_sig_get_inst_id(sig, i, MPR_STATUS_ANY);
        float *val = (float*)mpr_sig_get_value(sig, id, 0);
        if (val)
            printf("%4.0f, ", *val);
        else
            printf("––––, ");
    }
    if (i)
        eprintf("\b\b ");
    eprintf("]   ");
}

void print_instance_status(mpr_sig sig)
{
    int i, n = mpr_sig_get_num_inst(sig, MPR_STATUS_ANY);
    const char *name = mpr_obj_get_prop_as_str((mpr_obj)sig, MPR_PROP_NAME, NULL);
    eprintf("%s: [ ", name);
    for (i = 0; i < n; i++) {
        mpr_id id = mpr_sig_get_inst_id(sig, i, MPR_STATUS_ANY);
        eprintf("x%03x, ", mpr_sig_get_inst_status(sig, id));
    }
    if (i)
        eprintf("\b\b ");
    eprintf("]   ");
}

void release_active_instances(mpr_sig sig)
{
    int i = 1, n = mpr_sig_get_num_inst(sig, MPR_STATUS_ACTIVE);
    int ephem = mpr_obj_get_prop_as_int32((mpr_obj)sig, MPR_PROP_EPHEM, NULL);
    mpr_obj_set_prop((mpr_obj)sig, MPR_PROP_EPHEM, NULL, 1, MPR_INT32, &i, 1);
    eprintf("--> Releasing %d active instances for signal %s\n", n,
            mpr_obj_get_prop_as_str((mpr_obj)sig, MPR_PROP_NAME, NULL));
    for (i = 0; i < n; i++)
        mpr_sig_release_inst(sig, mpr_sig_get_inst_id(sig, 0, MPR_STATUS_ACTIVE));
    mpr_obj_set_prop((mpr_obj)sig, MPR_PROP_EPHEM, NULL, 1, MPR_INT32, &ephem, 1);
}

int loop(test_config *config)
{
    int i = 0, j, num_parallel_inst = 5, ret = 0;
    float valf = 0;
    mpr_id inst = 0;
    received = 0;

    eprintf("-------------------- GO ! --------------------\n");

    while (i < iterations && !done) {
        if (config->src_type & INST) {
            /* update instanced source signal */
            inst = i % 10;

            /* try to destroy an instance */
            eprintf("--> Retiring multisend instance %"PR_MPR_ID"\n", inst);
            mpr_sig_release_inst(multisend, inst);

            for (j = 0; j < num_parallel_inst; j++) {
                inst = (inst + 1) % 10;

                /* try to update an instance */
                valf = inst * 1.0f;
                eprintf("--> Updating multisend instance %"PR_MPR_ID" to %f\n", inst, valf);
                mpr_sig_set_value(multisend, inst, 1, MPR_FLT, &valf);
            }
        }
        if (config->src_type & SNGL) {
            /* update singleton source signal */
            eprintf("--> Updating monosend to %d\n", i);
            mpr_sig_set_value(monosend, 0, 1, MPR_INT32, &i);
        }

        mpr_dev_poll(src, 0);
        mpr_dev_poll(dst, period);
        i++;

        if (config->dst_type & SNGL) {
            /* check status */
            int status = mpr_obj_get_status((mpr_obj)monorecv);
            if (status & MPR_STATUS_UPDATE_REM) {
                ++received;
                mpr_obj_reset_status((mpr_obj)monorecv);
            }
        }
        if (config->dst_type & INST) {
            /* check status */
            int num_inst = mpr_sig_get_num_inst(multirecv, MPR_STATUS_ACTIVE);
            float *last_val = 0;
            for (j = 0; j < num_inst; j++) {
                mpr_id id = mpr_sig_get_inst_id(multirecv, j, MPR_STATUS_ACTIVE);
                int status = mpr_sig_get_inst_status(multirecv, id);
                if (status & MPR_STATUS_OVERFLOW) {
                    switch (config->oflw_action) {
                        case ADD:
                            eprintf("OVERFLOW!! ALLOCATING ANOTHER INSTANCE.\n");
                            mpr_sig_reserve_inst(multirecv, 1, 0, 0);
                            break;
                        case OLD:
                            mpr_sig_release_inst(multirecv, mpr_sig_get_oldest_inst_id(multirecv));
                            break;
                        case NEW:
                            mpr_sig_release_inst(multirecv, mpr_sig_get_newest_inst_id(multirecv));
                            break;
                        default:
                            break;
                    }
                }
                if (status & MPR_SIG_REL_UPSTRM) {
                    eprintf("--> destination multirecv instance %i got upstream release\n", (int)id);
                    mpr_sig_release_inst(multirecv, id);
                }
                if (status & MPR_STATUS_UPDATE_REM) {
                    float *val = (float*)mpr_sig_get_value(multirecv, id, 0);
                    if (!val)
                        continue;
                    eprintf("--> destination multirecv instance %i got %f\n", (int)id, *val);
                    ++received;
                    if (last_val) {
                        if (config->same_val) {
                            if (*val != *last_val) {
                                eprintf("Error: instance values should match but do not\n");
                                ret = 1;
                            }
                        }
                        else if (*val == *last_val) {
                            eprintf("Error: instance values match but should not\n");
                            ret = 1;
                        }
                    }
                    last_val = val;
                }
            }
        }

        if (verbose) {
            printf("ID:     ");
            if (config->src_type & SNGL)
                print_instance_ids(monosend);
            if (config->src_type & INST)
                print_instance_ids(multisend);
            if (config->dst_type & SNGL)
                print_instance_ids(monorecv);
            if (config->dst_type & INST)
                print_instance_ids(multirecv);
            printf("\n");

            printf("INDEX:  ");
            if (config->src_type & SNGL)
                print_instance_idx(monosend);
            if (config->src_type & INST)
                print_instance_idx(multisend);
            if (config->dst_type & SNGL)
                print_instance_idx(monorecv);
            if (config->dst_type & INST)
                print_instance_idx(multirecv);
            printf("\n");

            printf("VALUE:  ");
            if (config->src_type & SNGL)
                print_instance_vals(monosend);
            if (config->src_type & INST)
                print_instance_vals(multisend);
            if (config->dst_type & SNGL)
                print_instance_vals(monorecv);
            if (config->dst_type & INST)
                print_instance_vals(multirecv);
            printf("\n");

            printf("STATUS: ");
            if (config->src_type & SNGL)
                print_instance_status(monosend);
            if (config->src_type & INST)
                print_instance_status(multisend);
            if (config->dst_type & SNGL)
                print_instance_status(monorecv);
            if (config->dst_type & INST)
                print_instance_status(multirecv);
            printf("\n");
        }
        else {
            printf("\r  Iteration: %4d, Received: %4d", i, received);
            fflush(stdout);
        }
    }
    if (config->src_type & INST) {
        for (j = 0; j < num_parallel_inst; j++) {
            inst = (inst + 1) % 10;
            eprintf("--> Releasing multisend instance %"PR_MPR_ID"\n", inst);
            mpr_sig_release_inst(multisend, inst);
        }
        mpr_dev_update_maps(src);
        mpr_dev_poll(dst, 100);
    }
    return ret;
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

int run_test(test_config *config)
{
    mpr_sig *src_ptr, *dst_ptr;
    mpr_sig both_src[2];
    int num_src = 1, use_inst, compare_count;
    int result = 0, active_count = 0, reserve_count = 0, count_epsilon;
    mpr_map map;

    both_src[0] = monosend;
    both_src[1] = multisend;

    printf("Configuration %d: %s%s %s> %s%s%s%s%s%s\n",
           config->test_id,
           instance_type_names[config->src_type], config->process_loc == MPR_LOC_SRC ? "*" : " ",
           config->map_type == SNGL ? "––" : "==",
           instance_type_names[config->dst_type], config->process_loc == MPR_LOC_DST ? "*" : " ",
           config->oflw_action ? "; overflow: " : "", oflw_action_names[config->oflw_action],
           config->expr ? "; expression: " : "", config->expr ? config->expr : "");

    switch (config->src_type) {
        case SNGL:
            src_ptr = &monosend;
            break;
        case INST:
            src_ptr = &multisend;
            break;
        case BOTH:
            src_ptr = both_src;
            num_src = 2;
            break;
        default:
            eprintf("unexpected destination signal\n");
            return 1;
    }

    switch (config->dst_type) {
        case SNGL:
            dst_ptr = &monorecv;
            break;
        case INST:
            dst_ptr = &multirecv;
            break;
        default:
            eprintf("unexpected destination signal\n");
            return 1;
    }

    map = mpr_map_new(num_src, src_ptr, 1, dst_ptr);
    mpr_obj_set_prop((mpr_obj)map, MPR_PROP_PROCESS_LOC, NULL, 1, MPR_INT32, &config->process_loc, 1);
    if (config->expr)
        mpr_obj_set_prop((mpr_obj)map, MPR_PROP_EXPR, NULL, 1, MPR_STR, config->expr, 1);

    use_inst = config->map_type == INST;
    mpr_obj_set_prop((mpr_obj)map, MPR_PROP_USE_INST, NULL, 1, MPR_BOOL, &use_inst, 1);
    mpr_obj_push((mpr_obj)map);
    while (!done && !mpr_map_get_is_ready(map)) {
        mpr_dev_poll(src, 100);
        mpr_dev_poll(dst, 100);
    }
    mpr_dev_poll(src, 100);
    mpr_dev_poll(dst, 100);

    /* remove any extra destination instances allocated by previous tests */
    while (5 <= mpr_sig_get_num_inst(multirecv, MPR_STATUS_ANY)) {
        eprintf("removing extra destination instance\n");
        mpr_sig_remove_inst(multirecv, mpr_sig_get_inst_id(multirecv, 4, MPR_STATUS_ANY));
    }

    mpr_dev_poll(src, 100);
    mpr_dev_poll(dst, 100);

    if (INST & config->dst_type && SNGL == config->map_type) {
        /* activate 2 destination instances */
        eprintf("activating 2 destination instances\n");
        mpr_sig_activate_inst(multirecv, 2);
        mpr_sig_activate_inst(multirecv, 6);
    }

    result += loop(config);

    if (ephemeral) {
        if (shared_graph)
            compare_count = ((float)iterations * config->count_mult_ephem_shared);
        else
            compare_count = ((float)iterations * config->count_mult_ephem);
    }
    else {
        if (shared_graph)
            compare_count = ((float)iterations * config->count_mult_shared);
        else
            compare_count = ((float)iterations * config->count_mult);
    }

    release_active_instances(multisend);

    mpr_dev_poll(src, 100);
    mpr_dev_poll(dst, 100);

    /* Warning: assuming that map has not been released by a peer process is not safe! Do not do
     * this in non-test code. Instead you should try to fetch a fresh map object from the graph
     * e.g. using the map id. */
    mpr_map_release(map);

    /* TODO: we shouldn't have to wait here... */
    mpr_dev_poll(src, 100);
    mpr_dev_poll(dst, 100);
    mpr_dev_poll(src, 100);
    mpr_dev_poll(dst, 100);

    mpr_dev_poll(src, 100);
    mpr_dev_poll(dst, 100);
    mpr_dev_poll(src, 100);
    mpr_dev_poll(dst, 100);

    release_active_instances(multirecv);

    if (mpr_local_sig_get_num_id_maps((mpr_local_sig)multisend) > 8) {
        printf("Error: multisend using %d id maps (should be %d)\n",
               mpr_local_sig_get_num_id_maps((mpr_local_sig)multisend), 8);
        ++result;
    }
    if (mpr_local_sig_get_num_id_maps((mpr_local_sig)multirecv) > 8) {
        printf("Error: multirecv using %d id maps (should be %d)\n",
               mpr_local_sig_get_num_id_maps((mpr_local_sig)multirecv), 8);
        ++result;
    }

    active_count = mpr_local_dev_get_num_id_maps((mpr_local_dev)src, 1);
    reserve_count = mpr_local_dev_get_num_id_maps((mpr_local_dev)src, 0);
    if (active_count > 1 || reserve_count > 6) {
        printf("Error: src device using %d active and %d reserve id maps (should be <=1 and <=6)\n",
               active_count, reserve_count);
#ifdef DEBUG
        mpr_local_dev_print_id_maps((mpr_local_dev)src);
#endif
        ++result;
    }

    active_count = mpr_local_dev_get_num_id_maps((mpr_local_dev)dst, 1);
    reserve_count = mpr_local_dev_get_num_id_maps((mpr_local_dev)dst, 0);
    if (   active_count > mpr_sig_get_num_inst(multirecv, MPR_STATUS_ACTIVE) * !ephemeral + 1
        || reserve_count > 10) {
        printf("Error: dst device using %d active and %d reserve id maps (should be <=%d and <10)\n",
               active_count, reserve_count,
               mpr_sig_get_num_inst(multirecv, MPR_STATUS_ACTIVE) * !ephemeral + 1);
#ifdef DEBUG
        mpr_local_dev_print_id_maps((mpr_local_dev)dst);
#endif
        ++result;
    }

    count_epsilon = ceil((float)compare_count * config->count_epsilon);

    eprintf("Received %d of %d +/- %d updates\n", received, compare_count, count_epsilon);

    result += abs(compare_count - received) > count_epsilon;

    eprintf("Configuration %d %s: %s%s %s> %s%s%s%s%s%s\n",
            config->test_id,
            result ? "FAILED" : "PASSED",
            instance_type_names[config->src_type],
            config->process_loc == MPR_LOC_SRC ? "*" : "",
            config->map_type == SNGL ? "––" : "==",
            instance_type_names[config->dst_type],
            config->process_loc == MPR_LOC_DST ? "*" : "",
            config->oflw_action ? "; overflow: " : "",
            oflw_action_names[config->oflw_action],
            config->expr ? "; expression: " : "",
            config->expr ? config->expr : "");

    if (!verbose) {
        if (result)
            printf(" (expected %4d ± %2d) \x1B[31mFAILED\x1B[0m.\n", compare_count, count_epsilon);
        else
            printf(" (expected) ......... \x1B[32mPASSED\x1B[0m.\n");
    }

    return result;
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
                        printf("testinstance_no_cb.c: possible arguments "
                               "-f fast (execute quickly), "
                               "-q quiet (suppress output), "
                               "-t terminate automatically, "
                               "-s shared (use one mpr_graph only), "
                               "-p persistent destination signal instances, "
                               "-h help, "
                               "--iface network interface, "
                               "--config specify a configuration to run (1-%d)\n", NUM_TESTS);
                        return 1;
                        break;
                    case 'f':
                        period = 2;
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
                    case 'p':
                        ephemeral = 0;
                        break;
                    case '-':
                        if (strcmp(argv[i], "--iface")==0 && argc>i+1) {
                            i++;
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
                                printf("config argument must be between 1 and %d\n", NUM_TESTS);
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

    eprintf("Key:\n");
    eprintf("  *\t denotes processing location\n");
    eprintf("  ––>\t singleton (non-instanced) map\n");
    eprintf("  ==>\t instanced map\n");

    i = config_start;
    while (!done && i < config_stop) {
        test_config *config = &test_configs[i];
        if (run_test(config)) {
            result = 1;
            break;
        }
        ++i;
    }

  done:
    cleanup_dst();
    cleanup_src();
    if (g) mpr_graph_free(g);
    printf("..................................................Test %s\x1B[0m.\n",
           result ? "\x1B[31mFAILED" : "\x1B[32mPASSED");
    return result;
}
