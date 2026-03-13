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
mpr_sig multisend1 = 0;
mpr_sig multisend2 = 0;
mpr_sig multirecv = 0;
mpr_sig monosend1 = 0;
mpr_sig monosend2 = 0;
mpr_sig monorecv = 0;

int ephemeral = 1;
int count_received = 0;
int count_expected = 0;
int count_epsilon = 0;
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

const char *instance_type_names[] = { "", "SINGLETON", "INSTANCED" };

/* TODO: add all-singleton convergent, all-instanced convergent */
typedef enum {
    NONE = 0x00,
    SNGL = 0x01,   /* singleton */
    INST = 0x02    /* instanced */
    // TODO: convergent with different source devices?
} instance_type;

const char *oflw_action_names[] = { "no action", "steal oldest", "steal newest", "add instance" };

typedef enum {
    NO_ACTION = MPR_STEAL_NONE,
    STEAL_OLD = MPR_STEAL_OLDEST,
    STEAL_NEW = MPR_STEAL_NEWEST,
    ADD_INST  = 3
} oflw_action;

typedef struct _test_config {
    int             test_id;
    instance_type   src1_type;
    instance_type   src2_type;
    instance_type   dst_type;
    instance_type   map_type;
    mpr_loc         process_loc;
    oflw_action     oflw_action;
    const char      *expr;
    float           count_mult_persistent;
    float           count_mult_persistent_shared;
    float           count_mult_ephemeral;
    float           count_mult_ephemeral_shared;
    float           count_epsilon;
    int             same_val;
} test_config;

#define EXPR1 "alive=(x%10)>=5;y=x;"
#define EXPR2 "y=x.instance.mean()"

// TODO: randomize order of configurations

/* TODO: test received values */
/* TODO: these should work with count_epsilon=0.0 */
test_config test_configs[] = {
    /* #, SRC1, SRC2, DST,  MAP,  PROCESS_LOC, STEALING,  EXPR,  -tp   -tps, -t    -ts   */

    /* singleton ––> singleton; shouldn't make a difference if map is instanced */
    {  1, SNGL, NONE, SNGL, SNGL, MPR_LOC_SRC, NO_ACTION, NULL,  1.0,  1.0,  1.0,  1.0,  0.0,   0 },
    {  2, SNGL, NONE, SNGL, SNGL, MPR_LOC_DST, NO_ACTION, NULL,  1.0,  1.0,  1.0,  1.0,  0.0,   0 },

    /* singleton ==> singleton; shouldn't make a difference if map is instanced */
    {  3, SNGL, NONE, SNGL, INST, MPR_LOC_SRC, NO_ACTION, NULL,  1.0,  1.0,  1.0,  1.0,  0.0,   0 },
    {  4, SNGL, NONE, SNGL, INST, MPR_LOC_DST, NO_ACTION, NULL,  1.0,  1.0,  1.0,  1.0,  0.0,   0 },

    /* singleton ––> instanced; control all active instances */
    {  5, SNGL, NONE, INST, SNGL, MPR_LOC_SRC, NO_ACTION, NULL,  2.0,  2.0,  2.0,  2.0,  0.0,   1 },
    {  6, SNGL, NONE, INST, SNGL, MPR_LOC_DST, NO_ACTION, NULL,  2.0,  2.0,  2.0,  2.0,  0.0,   1 },

    /* singleton ==> instanced; control a single instance (default) */
    /* TODO: check that instance is released on map_free() */
    {  7, SNGL, NONE, INST, INST, MPR_LOC_SRC, NO_ACTION, NULL,  1.0,  1.0,  1.0,  1.0,  0.0,   0 },
    {  8, SNGL, NONE, INST, INST, MPR_LOC_DST, NO_ACTION, NULL,  1.0,  1.0,  1.0,  1.0,  0.0,   0 },

    /* instanced ––> singleton; any source instance updates destination */
    {  9, INST, NONE, SNGL, SNGL, MPR_LOC_SRC, NO_ACTION, NULL,  5.0,  5.0,  5.0,  5.0,  0.0,   0 },
    /* ... but when processing @dst only the last instance update will trigger handler */
    { 10, INST, NONE, SNGL, SNGL, MPR_LOC_DST, NO_ACTION, NULL,  1.0,  5.0,  1.0,  5.0,  0.0,   0 },

    /* instanced ==> singleton; one src instance updates dst (default) */
    /* CHECK: if controlling instance is released, move to next updated inst */
    { 11, INST, NONE, SNGL, INST, MPR_LOC_SRC, NO_ACTION, NULL,  1.0,  1.0,  1.0,  1.0,  0.0,   0 },
    { 12, INST, NONE, SNGL, INST, MPR_LOC_DST, NO_ACTION, NULL,  1.0,  1.0,  1.0,  1.0,  0.0,   0 },

    /* instanced ––> instanced; any src instance updates all dst instances */
    /* source signal does not know about active destination instances */
    { 13, INST, NONE, INST, SNGL, MPR_LOC_SRC, NO_ACTION, NULL, 10.0, 10.0, 10.0, 10.0,  0.0,   1 },
    { 14, INST, NONE, INST, SNGL, MPR_LOC_DST, NO_ACTION, NULL,  2.0, 10.0,  2.0, 10.0,  0.0,   1 },

    /* instanced ==> instanced; no stealing */
    { 15, INST, NONE, INST, INST, MPR_LOC_SRC, NO_ACTION, NULL,  4.0,  4.0,  2.0,  2.0,  0.0,   0 },
    { 16, INST, NONE, INST, INST, MPR_LOC_DST, NO_ACTION, NULL,  4.0,  4.0,  2.0,  2.0,  0.0,   0 },

    /* instanced ==> instanced; steal newest instance */
    { 17, INST, NONE, INST, INST, MPR_LOC_SRC, STEAL_NEW, NULL,  4.0,  4.0,  2.72, 2.72, 0.04,  0 },
    /* TODO: verify that shared_graph version is behaving properly */
    { 18, INST, NONE, INST, INST, MPR_LOC_DST, STEAL_NEW, NULL,  4.0,  4.0,  2.0,  2.72, 0.04,  0 },

    /* instanced ==> instanced; steal oldest instance */
    /* TODO: document why multiplier is not 5.0 */
    { 19, INST, NONE, INST, INST, MPR_LOC_SRC, STEAL_OLD, NULL,  4.0,  4.0,  4.6,  4.6,  0.0,   0 },
    { 20, INST, NONE, INST, INST, MPR_LOC_DST, STEAL_OLD, NULL,  4.0,  4.0,  4.0,  4.6,  0.0,   0 },

    /* instanced ==> instanced; add instances if needed */
    { 21, INST, NONE, INST, INST, MPR_LOC_SRC, ADD_INST,  NULL,  5.0,  5.0,  5.0,  5.0,  0.0,   0 },
    { 22, INST, NONE, INST, INST, MPR_LOC_DST, ADD_INST,  NULL,  5.0,  5.0,  5.0,  5.0,  0.0,   0 },

    /* singleton convergent ––> singleton */
    { 23, SNGL, SNGL, SNGL, SNGL, MPR_LOC_SRC, NO_ACTION, NULL,  1.0,  1.0,  1.0,  1.0,  0.0,   0 },
    { 24, SNGL, SNGL, SNGL, SNGL, MPR_LOC_DST, NO_ACTION, NULL,  1.0,  1.0,  1.0,  1.0,  0.0,   0 },

    /* singleton convergent ==> singleton */
    { 25, SNGL, SNGL, SNGL, INST, MPR_LOC_SRC, NO_ACTION, NULL,  1.0,  1.0,  1.0,  1.0,  0.0,   0 },
    { 26, SNGL, SNGL, SNGL, INST, MPR_LOC_DST, NO_ACTION, NULL,  1.0,  1.0,  1.0,  1.0,  0.0,   0 },

    /* singleton convergent ––> instanced */
    { 27, SNGL, SNGL, INST, SNGL, MPR_LOC_SRC, NO_ACTION, NULL,  2.0,  2.0,  2.0,  2.0,  0.0,   1 },
    { 28, SNGL, SNGL, INST, SNGL, MPR_LOC_DST, NO_ACTION, NULL,  2.0,  2.0,  2.0,  2.0,  0.0,   1 },

    /* singleton convergent ==> instanced */
    { 29, SNGL, SNGL, INST, SNGL, MPR_LOC_SRC, NO_ACTION, NULL,  2.0,  2.0,  2.0,  2.0,  0.0,   1 },
    { 30, SNGL, SNGL, INST, SNGL, MPR_LOC_DST, NO_ACTION, NULL,  2.0,  2.0,  2.0,  2.0,  0.0,   1 },

    /* instanced convergent ––> singleton */
    { 31, INST, INST, SNGL, SNGL, MPR_LOC_SRC, NO_ACTION, NULL,  5.0,  5.0,  5.0,  5.0,  0.0,   0 },
    { 32, INST, INST, SNGL, SNGL, MPR_LOC_DST, NO_ACTION, NULL,  1.0,  5.0,  1.0,  5.0,  0.0,   0 },

    /* instanced convergent ==> singleton */
    { 33, INST, INST, SNGL, INST, MPR_LOC_SRC, NO_ACTION, NULL,  1.0,  1.0,  1.0,  1.0,  0.0,   0 },
    { 34, INST, INST, SNGL, INST, MPR_LOC_DST, NO_ACTION, NULL,  1.0,  1.0,  1.0,  1.0,  0.0,   0 },

    /* instanced convergent ––> instanced */
    { 35, INST, INST, INST, SNGL, MPR_LOC_SRC, NO_ACTION, NULL, 10.0, 10.0, 10.0, 10.0,  0.0,   1 },
    { 36, INST, INST, INST, SNGL, MPR_LOC_DST, NO_ACTION, NULL,  2.0, 10.0,  2.0, 10.0,  0.0,   1 },

    /* instanced convergent ==> instanced */
    { 37, INST, INST, INST, INST, MPR_LOC_SRC, NO_ACTION, NULL,  4.0,  4.0,  2.0,  2.0,  0.0,   0 },
    { 38, INST, INST, INST, INST, MPR_LOC_DST, NO_ACTION, NULL,  4.0,  4.0,  2.0,  2.0,  0.0,   0 },

    /* mixed convergent ––> singleton */
    /* for src processing the update count is additive since the destination has only one instance */
    { 39, SNGL, INST, SNGL, SNGL, MPR_LOC_SRC, NO_ACTION, NULL,  5.0,  5.0,  5.0,  5.0,  0.0,   0 },
    /* TODO: we should default to dst processing for this configuration */
    { 40, SNGL, INST, SNGL, SNGL, MPR_LOC_DST, NO_ACTION, NULL,  1.0,  5.0,  1.0,  5.0,  0.0,   0 },

    /* mixed convergent ==> singleton */
    /* for src processing we expect one update per iteration */
    { 41, SNGL, INST, SNGL, INST, MPR_LOC_SRC, NO_ACTION, NULL,  1.0,  1.0,  1.0,  1.0,  0.0,   0 },
    /* for dst processing we expect one update per iteration */
    { 42, SNGL, INST, SNGL, INST, MPR_LOC_DST, NO_ACTION, NULL,  1.0,  1.0,  1.0,  1.0,  0.0,   0 },

    /* mixed convergent ––> instanced */
    /* for src processing the update count is multiplicative: 5 src x 3 dst */
    { 43, SNGL, INST, INST, SNGL, MPR_LOC_SRC, NO_ACTION, NULL, 10.0, 10.0, 10.0, 10.0,  0.0,   1 },
    /* each active instance should receive 1 update per iteration */
    { 44, SNGL, INST, INST, SNGL, MPR_LOC_DST, NO_ACTION, NULL,  2.0, 10.0,  2.0, 10.0,  0.0,   1 },

    /* mixed convergent ==> instanced */
    { 45, SNGL, INST, INST, INST, MPR_LOC_SRC, NO_ACTION, NULL,  4.0,  4.0,  2.0,  2.0,  0.0,   0 },
    { 46, SNGL, INST, INST, INST, MPR_LOC_DST, NO_ACTION, NULL,  4.0,  4.0,  2.0,  2.0,  0.0,   0 },

    /* singleton ––> instanced; in-map instance management */
    /* each active instance receives 1 update per iteration when the expression is 'alive' */
    { 47, SNGL, NONE, INST, SNGL, MPR_LOC_SRC, NO_ACTION, EXPR1, 1.0,  1.0,  1.0,  1.0,  0.0,   1 },
    { 48, SNGL, NONE, INST, SNGL, MPR_LOC_DST, NO_ACTION, EXPR1, 1.0,  1.0,  1.0,  1.0,  0.0,   1 },

    /* singleton ==> instanced; in-map instance management */
    { 49, SNGL, NONE, INST, INST, MPR_LOC_SRC, NO_ACTION, EXPR1, 0.5,  0.5,  0.5,  0.5,  0.0,   0 },
    { 50, SNGL, NONE, INST, INST, MPR_LOC_DST, NO_ACTION, EXPR1, 0.5,  0.5,  0.5,  0.5,  0.0,   0 },

    /* instanced ––> singleton; instance reduce expression */
    { 51, INST, NONE, SNGL, SNGL, MPR_LOC_SRC, NO_ACTION, EXPR2, 1.0,  1.0,  1.0,  1.0,  0.0,   0 },
    { 52, INST, NONE, SNGL, SNGL, MPR_LOC_DST, NO_ACTION, EXPR2, 1.0,  1.0,  1.0,  1.0,  0.0,   0 },

    /* instanced ==> singleton; instance reduce expression */
    { 53, INST, NONE, SNGL, INST, MPR_LOC_SRC, NO_ACTION, EXPR2, 1.0,  1.0,  1.0,  1.0,  0.0,   0 },
    /* to calculate the correct value we would need to cache all src instances at dst */
    { 54, INST, NONE, SNGL, INST, MPR_LOC_DST, NO_ACTION, EXPR2, 1.0,  1.0,  1.0,  1.0,  0.0,   0 },

    /* instanced ––> instanced; instance reduce expression */
    /* result of expression should update all active destination instances */
    { 55, INST, NONE, INST, SNGL, MPR_LOC_SRC, NO_ACTION, EXPR2, 2.0,  2.0,  2.0,  2.0,  0.0,   1 },
    { 56, INST, NONE, INST, SNGL, MPR_LOC_DST, NO_ACTION, EXPR2, 2.0,  2.0,  2.0,  2.0,  0.0,   1 },

    /* instanced ==> instanced; instance reduce expression */
    // TODO: currently each update is a new instance, should be a stream of one map-managed instance
    // TODO: ensure all src instances are released periodically; check that dst is also released
    // or at least check for update-triggered release at end of test?
    { 57, INST, NONE, INST, INST, MPR_LOC_SRC, NO_ACTION, EXPR2, 1.0,  1.0,  1.0,  1.0,  0.0,   1 },
    { 58, INST, NONE, INST, INST, MPR_LOC_DST, NO_ACTION, EXPR2, 1.0,  1.0,  1.0,  1.0,  0.0,   1 },

    /* work in progress:
     * instanced ––> instanced; in-map instance management (late start, early release, ad hoc)
     * instanced ==> instanced; in-map instance management (late start, early release, ad hoc)
     * mixed ––> instanced; in-map instance management (late start, early release, ad hoc)
     * mixed ==> instanced; in-map instance management (late start, early release, ad hoc)
     */

/*    { 59, INST, NONE, INST, INST, MPR_LOC_SRC, NONE, "alive=n>=3;y=x;n=(n+1)%10;", 0.5, 0. },
    { 60, INST, NONE, INST, INST, MPR_LOC_DST, NONE, "alive=n>=3;y=x;n=(n+1)%10;", 0.5, 0. },*/

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
    int num_inst = 10, stl = MPR_STEAL_OLDEST;

    src = mpr_dev_new("testinstance-send", g);
    if (!src)
        goto error;
    if (iface)
        mpr_graph_set_interface(mpr_obj_get_graph((mpr_obj)src), iface);
    eprintf("source created using interface %s.\n",
            mpr_graph_get_interface(mpr_obj_get_graph((mpr_obj)src)));

    monosend1 = mpr_sig_new(src, MPR_DIR_OUT, "monosend1", 1, MPR_FLT, NULL,
                            &mn, &mx, NULL, NULL, 0);
    monosend2 = mpr_sig_new(src, MPR_DIR_OUT, "monosend2", 1, MPR_FLT, NULL,
                            &mn, &mx, NULL, NULL, 0);
    multisend1 = mpr_sig_new(src, MPR_DIR_OUT, "multisend1", 1, MPR_FLT, NULL,
                             &mn, &mx, &num_inst, NULL, 0);
    multisend2 = mpr_sig_new(src, MPR_DIR_OUT, "multisend2", 1, MPR_FLT, NULL,
                             &mn, &mx, &num_inst, NULL, 0);
    if (!(monosend1 && monosend2 && multisend1 && multisend2))
        goto error;

    mpr_obj_set_prop((mpr_obj)multisend1, MPR_PROP_STEAL_MODE, NULL, 1, MPR_INT32, &stl, 1);
    mpr_obj_set_prop((mpr_obj)multisend2, MPR_PROP_STEAL_MODE, NULL, 1, MPR_INT32, &stl, 1);

    if (verbose) {
        mpr_list sigs = mpr_dev_get_sigs(src, MPR_DIR_ANY);
        while (sigs) {
            mpr_sig sig = (mpr_sig)*sigs;
            eprintf("Output signal '%s' added with %i instances.\n",
                    mpr_obj_get_prop_as_str((mpr_obj)sig, MPR_PROP_NAME, NULL),
                    mpr_sig_get_num_inst(sig, MPR_STATUS_ANY));
            sigs = mpr_list_get_next(sigs);
        }
    }

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

void handler(mpr_sig sig, mpr_sig_evt e, mpr_id inst, int len, mpr_type type,
             const void *val, mpr_time t)
{
    const char *name = mpr_obj_get_prop_as_str((mpr_obj)sig, MPR_PROP_NAME, NULL);

    if (e & MPR_SIG_INST_OFLW) {
        eprintf("OVERFLOW!! ALLOCATING ANOTHER INSTANCE.\n");
        mpr_sig_reserve_inst(sig, 1, 0, 0);
    }
    else if (e & MPR_SIG_REL_UPSTRM) {
        eprintf("--> destination %s instance %i got upstream release\n", name, (int)inst);
        mpr_sig_release_inst(sig, inst);
    }
    else if (val) {
        eprintf("--> destination %s instance %i got %f\n", name, (int)inst, (*(float*)val));
        ++count_received;
    }
    else {
        eprintf("--> destination %s instance %i got NULL\n", name, (int)inst);
        mpr_sig_release_inst(sig, inst);
    }
}

/*! Creation of a local destination. */
int setup_dst(mpr_graph g, const char *iface)
{
    float mn=0;
    int i, num_inst;

    dst = mpr_dev_new("testinstance-recv", g);
    if (!dst)
        goto error;
    if (iface)
        mpr_graph_set_interface(mpr_obj_get_graph((mpr_obj)dst), iface);
    eprintf("destination created using interface %s.\n",
            mpr_graph_get_interface(mpr_obj_get_graph((mpr_obj)dst)));

    /* Specify 0 instances since we wish to use specific ids */
    num_inst = 0;
    multirecv = mpr_sig_new(dst, MPR_DIR_IN, "multirecv", 1, MPR_FLT, NULL,
                            &mn, NULL, &num_inst, handler, MPR_SIG_ALL);
    mpr_obj_set_prop((mpr_obj)multirecv, MPR_PROP_EPHEM, NULL, 1, MPR_INT32, &ephemeral, 1);
    monorecv = mpr_sig_new(dst, MPR_DIR_IN, "monorecv", 1, MPR_FLT, NULL,
                           &mn, NULL, 0, handler, MPR_SIG_UPDATE);
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
        mpr_id id;
        mpr_sig_get_inst_id(sig, i, MPR_STATUS_ANY, &id);
        eprintf("%4i, ", (int)id);
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
        mpr_id id;
        mpr_sig_get_inst_id(sig, i, MPR_STATUS_ANY, &id);
        float *val = (float*)mpr_sig_get_value(sig, id, 0);
        if (val)
            printf("%4.1f, ", *val);
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
        mpr_id id;
        mpr_sig_get_inst_id(sig, i, MPR_STATUS_ANY, &id);
        eprintf("x%03x, ", mpr_sig_get_inst_status(sig, id, 0));
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
    for (i = 0; i < n; i++) {
        mpr_id id;
        if (mpr_sig_get_inst_id(sig, 0, MPR_STATUS_ACTIVE, &id))
            mpr_sig_release_inst(sig, id);
    }
    mpr_obj_set_prop((mpr_obj)sig, MPR_PROP_EPHEM, NULL, 1, MPR_INT32, &ephem, 1);
}

int loop(test_config *config)
{
    int i = 0, j, num_parallel_inst = 5, ret = 0;
    float valf = 0;
    mpr_id inst = 0;
    count_received = 0;

    eprintf("-------------------- GO ! --------------------\n");

    while (i < iterations && !done) {
        if (INST == config->src1_type) {
            /* update instanced source signal */
            inst = i % 10;

            /* try to destroy an instance */
            eprintf("--> Retiring multisend1 instance %"PR_MPR_ID"\n", inst);
            mpr_sig_release_inst(multisend1, inst);

            for (j = 0; j < num_parallel_inst; j++) {
                inst = (inst + 1) % 10;

                /* try to update an instance */
                valf = inst * 1.0f;
                eprintf("--> Updating multisend1 instance %"PR_MPR_ID" to %f\n", inst, valf);
                mpr_sig_set_value(multisend1, inst, 1, MPR_FLT, &valf);
            }
        }
        else if (SNGL == config->src1_type) {
            /* update singleton source signal */
            eprintf("--> Updating monosend1 to %d\n", i);
            mpr_sig_set_value(monosend1, 0, 1, MPR_INT32, &i);
        }
        if (INST == config->src2_type) {
            /* update instanced source signal */
            inst = i % 10;

            /* try to destroy an instance */
            eprintf("--> Retiring multisend2 instance %"PR_MPR_ID"\n", inst);
            mpr_sig_release_inst(multisend2, inst);

            for (j = 0; j < num_parallel_inst; j++) {
                inst = (inst + 1) % 10;

                /* try to update an instance */
                valf = inst * 1.0f;
                eprintf("--> Updating multisend2 instance %"PR_MPR_ID" to %f\n", inst, valf);
                mpr_sig_set_value(multisend2, inst, 1, MPR_FLT, &valf);
            }
        }
        else if (SNGL == config->src2_type) {
            /* update singleton source signal */
            eprintf("--> Updating monosend2 to %d\n", i);
            mpr_sig_set_value(monosend2, 0, 1, MPR_INT32, &i);
        }

        mpr_dev_poll(src, 0);
        mpr_dev_poll(dst, period);
        i++;

        if (INST == config->dst_type) {
            /* check values */
            int num_inst = mpr_sig_get_num_inst(multirecv, MPR_STATUS_ACTIVE);
            /* if dst signal is not ephemeral there may be values from a previous configuration */
            if (num_inst > 1 && (ephemeral || count_received > i)) {
                mpr_id id;
                mpr_sig_get_inst_id(multirecv, 0, MPR_STATUS_ACTIVE, &id);
                float *val0 = (float*)mpr_sig_get_value(multirecv, id, 0);
                if (val0) {
                    for (j = 1; j < num_inst; j++) {
                        float *valj;
                        mpr_sig_get_inst_id(multirecv, j, MPR_STATUS_ACTIVE, &id);
                        valj = (float*)mpr_sig_get_value(multirecv, id, 0);
                        if (!valj)
                            continue;
                        if (config->same_val) {
                            if (*val0 != *valj) {
                                eprintf("Error: instance values should match but do not\n");
                                ret = 1;
                            }
                        }
                        else if (*val0 == *valj) {
                            eprintf("Error: instance values match but should not\n");
                            ret = 1;
                        }
                    }
                }
            }
        }

        if (verbose) {
            printf("ID:     ");
            if (SNGL == config->src1_type)
                print_instance_ids(monosend1);
            if (INST == config->src1_type)
                print_instance_ids(multisend1);
            if (SNGL == config->src2_type)
                print_instance_ids(monosend2);
            if (INST == config->src2_type)
                print_instance_ids(multisend2);
            if (SNGL == config->dst_type)
                print_instance_ids(monorecv);
            if (INST == config->dst_type)
                print_instance_ids(multirecv);
            printf("\n");

            printf("VALUE:  ");
            if (SNGL == config->src1_type)
                print_instance_vals(monosend1);
            if (INST == config->src1_type)
                print_instance_vals(multisend1);
            if (SNGL == config->src2_type)
                print_instance_vals(monosend2);
            if (INST == config->src2_type)
                print_instance_vals(multisend2);
            if (SNGL == config->dst_type)
                print_instance_vals(monorecv);
            if (INST == config->dst_type)
                print_instance_vals(multirecv);
            printf("\n");

            printf("STATUS: ");
            if (SNGL == config->src1_type)
                print_instance_status(monosend1);
            if (INST == config->src1_type)
                print_instance_status(multisend1);
            if (SNGL == config->src2_type)
                print_instance_status(monosend2);
            if (INST == config->src2_type)
                print_instance_status(multisend2);
            if (SNGL == config->dst_type)
                print_instance_status(monorecv);
            if (INST == config->dst_type)
                print_instance_status(multirecv);
            printf("\n");
        }
        else {
            printf("\r  Iteration: %4d, Received: %4d/%4d (+/-%d)", i, count_received,
                   count_expected, count_epsilon);
            fflush(stdout);
        }
    }
    if (INST == config->src1_type) {
        for (j = 0; j < num_parallel_inst; j++) {
            inst = (inst + 1) % 10;
            eprintf("--> Releasing multisend1 instance %"PR_MPR_ID"\n", inst);
            mpr_sig_release_inst(multisend1, inst);
        }
        mpr_dev_update_maps(src);
        mpr_dev_poll(dst, 100);
    }
    if (INST == config->src2_type) {
        for (j = 0; j < num_parallel_inst; j++) {
            inst = (inst + 1) % 10;
            eprintf("--> Releasing multisend2 instance %"PR_MPR_ID"\n", inst);
            mpr_sig_release_inst(multisend2, inst);
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
    mpr_sig srcs[2], *dst_ptr;
    int num_src = 1, stl, evt = MPR_SIG_UPDATE | MPR_SIG_REL_UPSTRM, use_inst;
    int result = 0, active_count = 0, reserve_count = 0;
    mpr_map map;

    printf("Configuration %d: ", config->test_id);
    if (config->src2_type)
        printf("[");
    printf("%s", instance_type_names[config->src1_type]);
    if (config->src2_type)
        printf(",%s]", instance_type_names[config->src2_type]);
    if (config->process_loc == MPR_LOC_SRC)
        printf("*");
    printf(" %s> ", config->map_type == SNGL ? "––" : "==");
    printf("%s", instance_type_names[config->dst_type]);
    if (config->process_loc == MPR_LOC_DST)
        printf("*");
    printf("; overflow: %s", oflw_action_names[config->oflw_action]);
    printf("; expression: %s\n", config->expr ? config->expr : "default");

    switch (config->src1_type) {
        case SNGL:
            srcs[0] = monosend1;
            break;
        case INST:
            srcs[0] = multisend1;
            break;
        default:
            eprintf("unexpected source signal\n");
            return 1;
    }

    switch (config->src2_type) {
        case SNGL:
            srcs[1] = monosend2;
            num_src = 2;
            break;
        case INST:
            srcs[1] = multisend2;
            num_src = 2;
            break;
        case NONE:
            srcs[1] = 0;
            num_src = 1;
            break;
        default:
            eprintf("unexpected source signal\n");
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

    switch(config->oflw_action) {
        case STEAL_NEW:
            stl = MPR_STEAL_NEWEST;
            break;
        case STEAL_OLD:
            stl = MPR_STEAL_OLDEST;
            break;
        case ADD_INST:
            evt = MPR_SIG_UPDATE | MPR_SIG_INST_OFLW | MPR_SIG_REL_UPSTRM;
        default:
            stl = MPR_STEAL_NONE;
    }
    mpr_obj_set_prop((mpr_obj)multirecv, MPR_PROP_STEAL_MODE, NULL, 1, MPR_INT32, &stl, 1);
    mpr_sig_set_cb(multirecv, handler, evt);

    map = mpr_map_new(num_src, srcs, 1, dst_ptr);
    mpr_obj_set_prop((mpr_obj)map, MPR_PROP_PROCESS_LOC, NULL, 1, MPR_INT32, &config->process_loc, 1);
    if (config->expr)
        mpr_obj_set_prop((mpr_obj)map, MPR_PROP_EXPR, NULL, 1, MPR_STR, config->expr, 1);

    use_inst = config->map_type == INST;
    mpr_obj_set_prop((mpr_obj)map, MPR_PROP_USE_INST, NULL, 1, MPR_BOOL, &use_inst, 1);
    mpr_obj_push((mpr_obj)map);
    while (!done && !mpr_map_get_is_ready(map)) {
        mpr_dev_poll(src, 10);
        mpr_dev_poll(dst, 10);
    }
    mpr_dev_poll(src, 10);
    mpr_dev_poll(dst, 10);

    /* remove any extra destination instances allocated by previous tests */
    while (5 <= mpr_sig_get_num_inst(multirecv, MPR_STATUS_ANY)) {
        mpr_id id;
        eprintf("removing extra destination instance\n");
        if (mpr_sig_get_inst_id(multirecv, 4, MPR_STATUS_ANY, &id))
            mpr_sig_remove_inst(multirecv, id);
    }

    mpr_dev_poll(src, 10);
    mpr_dev_poll(dst, 10);

    if (INST == config->dst_type) {
        /* activate 2 destination instances */
        eprintf("activating 2 destination instances\n");
        mpr_sig_activate_inst(multirecv, 2);
        mpr_sig_activate_inst(multirecv, 6);
    }

    if (ephemeral) {
        if (shared_graph)
            count_expected = ((float)iterations * config->count_mult_ephemeral_shared);
        else
            count_expected = ((float)iterations * config->count_mult_ephemeral);
    }
    else {
        if (shared_graph)
            count_expected = ((float)iterations * config->count_mult_persistent_shared);
        else
            count_expected = ((float)iterations * config->count_mult_persistent);
    }

    result += loop(config);

    release_active_instances(multisend1);
    release_active_instances(multisend2);

    mpr_dev_poll(src, 10);
    mpr_dev_poll(dst, 10);

    /* Warning: assuming that map has not been released by a peer process is not safe! Do not do
     * this in non-test code. Instead you should try to fetch a fresh map object from the graph
     * e.g. using the map id. */
    mpr_map_release(map);

    /* TODO: we shouldn't have to wait here... */
    mpr_dev_poll(src, 10);
    mpr_dev_poll(dst, 10);
    mpr_dev_poll(src, 10);
    mpr_dev_poll(dst, 10);
    mpr_dev_poll(src, 10);
    mpr_dev_poll(dst, 10);

    release_active_instances(multirecv);

    if (mpr_local_sig_get_num_id_maps((mpr_local_sig)multisend1) > 8) {
        printf("Error: multisend1 using %d id maps (should be %d)\n",
               mpr_local_sig_get_num_id_maps((mpr_local_sig)multisend1), 8);
        ++result;
    }
    if (mpr_local_sig_get_num_id_maps((mpr_local_sig)multisend2) > 8) {
        printf("Error: multisend2 using %d id maps (should be %d)\n",
               mpr_local_sig_get_num_id_maps((mpr_local_sig)multisend2), 8);
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
        printf("Error: dst device using %d active and %d reserve id maps (should be <=%d and <=10)\n",
               active_count, reserve_count,
               active_count > mpr_sig_get_num_inst(multirecv, MPR_STATUS_ACTIVE) * !ephemeral + 1);
#ifdef DEBUG
        mpr_local_dev_print_id_maps((mpr_local_dev)dst);
#endif
        ++result;
    }

    count_epsilon = ceil((float)count_expected * config->count_epsilon);

    eprintf("Received %d of %d +/- %d updates\n", count_received, count_expected, count_epsilon);

    result += abs(count_expected - count_received) > count_epsilon;

    eprintf("Configuration %d %s\n", config->test_id, result ? "FAILED" : "PASSED");

    if (!verbose) {
        printf(" ........ \x1B[32m%s\x1B[0m.\n", result ? "FAILED" : "PASSED");
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
                        printf("testinstance.c: possible arguments "
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
                        period = 1;
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
