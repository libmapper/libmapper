#include "../src/mapper_internal.h"
#include <mapper/mapper.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <math.h>
#include <lo/lo.h>
#include <unistd.h>
#include <signal.h>
#include <string.h>

int verbose = 1;
int terminate = 0;
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
    SINGLETON = 0x01,   /* singleton */
    INSTANCED = 0x02,   /* instanced */
    MIXED_SIG = 0x03,   /* mixed convergent, same device */
    MIXED_DEV = 0x07    /* mixed convergent, different device */
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
    float           count_multiplier;
    float           count_epsilon;
} test_config;

/* TODO: test received values */
/* TODO: these should work with count_epsilon=0.0 */
test_config test_configs[] = {
    /* singleton ––> singleton; shouldn't make a difference if map is instanced */
    {  1, SINGLETON, SINGLETON, SINGLETON, MPR_LOC_SRC, NONE, NULL, 1., 0. },
    {  2, SINGLETON, SINGLETON, SINGLETON, MPR_LOC_DST, NONE, NULL, 1., 0. },

    /* singleton ==> singleton; shouldn't make a difference if map is instanced */
    {  3, SINGLETON, SINGLETON, INSTANCED, MPR_LOC_SRC, NONE, NULL, 1., 0. },
    {  4, SINGLETON, SINGLETON, INSTANCED, MPR_LOC_DST, NONE, NULL, 1., 0. },

    /* singleton ––> instanced; control all active instances */
    {  5, SINGLETON, INSTANCED, SINGLETON, MPR_LOC_SRC, NONE, NULL, 3., 0. },
    {  6, SINGLETON, INSTANCED, SINGLETON, MPR_LOC_DST, NONE, NULL, 3., 0. },

    /* singleton ==> instanced; control a single instance (default) */
    /* TODO: check that instance is released on map_free() */
    {  7, SINGLETON, INSTANCED, INSTANCED, MPR_LOC_SRC, NONE, NULL, 1., 0. },
    {  8, SINGLETON, INSTANCED, INSTANCED, MPR_LOC_DST, NONE, NULL, 1., 0. },

    /* instanced ––> singleton; any source instance updates destination */
    {  9, INSTANCED, SINGLETON, SINGLETON, MPR_LOC_SRC, NONE, NULL, 5., 0. },
    /* ... but when processing @dst only the last instance update will trigger handler */
    { 10, INSTANCED, SINGLETON, SINGLETON, MPR_LOC_DST, NONE, NULL, 1., 0. },

    /* instanced ==> singleton; one src instance updates dst (default) */
    /* CHECK: if controlling instance is released, move to next updated inst */
    { 11, INSTANCED, SINGLETON, INSTANCED, MPR_LOC_SRC, NONE, NULL, 1., 0. },
    { 12, INSTANCED, SINGLETON, INSTANCED, MPR_LOC_DST, NONE, NULL, 1., 0. },

    /* instanced ––> instanced; any src instance updates all dst instances */
    /* source signal does not know about active destination instances */
    { 13, INSTANCED, INSTANCED, SINGLETON, MPR_LOC_SRC, NONE, NULL, 15., 0. },
    { 14, INSTANCED, INSTANCED, SINGLETON, MPR_LOC_DST, NONE, NULL, 3., 0. },

    /* instanced ==> instanced; no stealing */
    { 15, INSTANCED, INSTANCED, INSTANCED, MPR_LOC_SRC, NONE, NULL, 4., 0. },
    { 16, INSTANCED, INSTANCED, INSTANCED, MPR_LOC_DST, NONE, NULL, 4., 0. },

    /* instanced ==> instanced; steal newest instance */
    { 17, INSTANCED, INSTANCED, INSTANCED, MPR_LOC_SRC, NEW, NULL, 4.25, 0. },
    { 18, INSTANCED, INSTANCED, INSTANCED, MPR_LOC_DST, NEW, NULL, 4., 0. },

    /* instanced ==> instanced; steal oldest instance */
    { 19, INSTANCED, INSTANCED, INSTANCED, MPR_LOC_SRC, OLD, NULL, 4.6, 0. },
    { 20, INSTANCED, INSTANCED, INSTANCED, MPR_LOC_DST, OLD, NULL, 4.0, 0. },

    /* instanced ==> instanced; add instances if needed */
    { 21, INSTANCED, INSTANCED, INSTANCED, MPR_LOC_SRC, ADD, NULL, 5., 0. },
    { 22, INSTANCED, INSTANCED, INSTANCED, MPR_LOC_DST, ADD, NULL, 5., 0. },

    /* mixed ––> singleton */
    /* for src processing the update count is additive since the destination has only one instance */
    { 23, MIXED_SIG, SINGLETON, SINGLETON, MPR_LOC_SRC, NONE, NULL, 5.0, 0. },
    /* TODO: we should default to dst processing for this configuration */
    { 24, MIXED_SIG, SINGLETON, SINGLETON, MPR_LOC_DST, NONE, NULL, 1.0, 0. },

    /* mixed ==> singleton */
    /* for src processing we expect one update per iteration */
    { 25, MIXED_SIG, SINGLETON, INSTANCED, MPR_LOC_SRC, NONE, NULL, 1.0, 0. },
    /* for dst processing we expect one update per iteration */
    { 26, MIXED_SIG, SINGLETON, INSTANCED, MPR_LOC_DST, NONE, NULL, 1.0, 0. },

    /* mixed ––> instanced */
    /* for src processing the update count is multiplicative: 5 src x 3 dst */
    { 27, MIXED_SIG, INSTANCED, SINGLETON, MPR_LOC_SRC, NONE, NULL, 15.0, 0. },
    /* each active instance should receive 1 update per iteration */
    { 28, MIXED_SIG, INSTANCED, SINGLETON, MPR_LOC_DST, NONE, NULL, 3.0, 0. },

    /* mixed ==> instanced */
    { 29, MIXED_SIG, INSTANCED, INSTANCED, MPR_LOC_SRC, NONE, NULL, 4.0, 0. },
    { 30, MIXED_SIG, INSTANCED, INSTANCED, MPR_LOC_DST, NONE, NULL, 4.0, 0. },

    /* singleton ––> instanced; in-map instance management */
    { 31, SINGLETON, INSTANCED, SINGLETON, MPR_LOC_SRC, NONE, "alive=n>=5;y=x;n=(n+1)%10;", 1.5, 0. },
    { 32, SINGLETON, INSTANCED, SINGLETON, MPR_LOC_DST, NONE, "alive=n>=5;y=x;n=(n+1)%10;", 1.5, 0. },

    /* singleton ==> instanced; in-map instance management */
    { 33, SINGLETON, INSTANCED, INSTANCED, MPR_LOC_SRC, NONE, "alive=n>=5;y=x;n=(n+1)%10;", 0.5, 0. },
    { 34, SINGLETON, INSTANCED, INSTANCED, MPR_LOC_DST, NONE, "alive=n>=5;y=x;n=(n+1)%10;", 0.5, 0. },

    /* work in progress:
     * instanced ––> instanced; in-map instance management (late start, early release, ad hoc)
     * instanced ==> instanced; in-map instance management (late start, early release, ad hoc)
     * mixed ––> instanced; in-map instance management (late start, early release, ad hoc)
     * mixed ==> instanced; in-map instance management (late start, early release, ad hoc)
     */

/*    { 35, INSTANCED, INSTANCED, INSTANCED, MPR_LOC_SRC, NONE, "alive=n>=3;y=x;n=(n+1)%10;", 0.5, 0. },
    { 36, INSTANCED, INSTANCED, INSTANCED, MPR_LOC_DST, NONE, "alive=n>=3;y=x;n=(n+1)%10;", 0.5, 0. },*/

    /* future work:
     * src instance pooling (convergent maps)
     * dst instance pooling (divergent maps)
     * src & dst instance pooling (complex maps)
     */
};
const int NUM_TESTS =
    sizeof(test_configs)/sizeof(test_configs[0]);

/*! Creation of a local source. */
int setup_src()
{
    float mn=0, mx=10;
    int num_inst = 10, stl = MPR_STEAL_OLDEST;

    src = mpr_dev_new("testinstance-send", 0);
    if (!src)
        goto error;

    multisend = mpr_sig_new(src, MPR_DIR_OUT, "multisend", 1, MPR_FLT, NULL,
                            &mn, &mx, &num_inst, NULL, 0);
    monosend = mpr_sig_new(src, MPR_DIR_OUT, "monosend", 1, MPR_FLT, NULL,
                           &mn, &mx, NULL, NULL, 0);
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
        mpr_sig_release_inst(sig, inst);
    }
    else if (val) {
        eprintf("--> destination %s instance %i got %f\n", name, (int)inst, (*(float*)val));
        ++received;
    }
    else {
        eprintf("--> destination %s instance %i got NULL\n", name, (int)inst);
        mpr_sig_release_inst(sig, inst);
    }
}

/*! Creation of a local destination. */
int setup_dst()
{
    float mn=0;
    int i, num_inst;

    dst = mpr_dev_new("testinstance-recv", 0);
    if (!dst)
        goto error;

    /* Specify 0 instances since we wish to use specific ids */
    num_inst = 0;
    multirecv = mpr_sig_new(dst, MPR_DIR_IN, "multirecv", 1, MPR_FLT, NULL,
                            &mn, NULL, &num_inst, handler, MPR_SIG_UPDATE);
    monorecv = mpr_sig_new(dst, MPR_DIR_IN, "monorecv", 1, MPR_FLT, NULL,
                           &mn, NULL, 0, handler, MPR_SIG_UPDATE);
    if (!multirecv)
        goto error;

    for (i = 2; i < 10; i += 2) {
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

void wait_devs()
{
    while (!done && !(mpr_dev_get_is_ready(src) && mpr_dev_get_is_ready(dst))) {
        mpr_dev_poll(src, 25);
        mpr_dev_poll(dst, 25);
    }
}

void print_instance_ids(mpr_sig sig)
{
    int i, n = mpr_sig_get_num_inst(sig, MPR_STATUS_ALL);
    const char *name = mpr_obj_get_prop_as_str((mpr_obj)sig, MPR_PROP_NAME, NULL);
    eprintf("%s: [ ", name);
    for (i=0; i<n; i++) {
        eprintf("%2i, ", (int)mpr_sig_get_inst_id(sig, i, MPR_STATUS_ALL));
    }
    if (i)
        eprintf("\b\b ");
    eprintf("]   ");
}

void print_instance_idx(mpr_sig sig)
{
    int i, n = mpr_sig_get_num_inst(sig, MPR_STATUS_ALL);
    const char *name = mpr_obj_get_prop_as_str((mpr_obj)sig, MPR_PROP_NAME, NULL);
    eprintf("%s: [ ", name);
    for (i = 0; i < n; i++) {
        eprintf("%2i, ", ((mpr_local_sig)sig)->inst[i]->idx);
    }
    if (i)
        eprintf("\b\b ");
    eprintf("]   ");
}

void print_instance_vals(mpr_sig sig)
{
    int i, n = mpr_sig_get_num_inst(sig, MPR_STATUS_ALL);
    const char *name = mpr_obj_get_prop_as_str((mpr_obj)sig, MPR_PROP_NAME, NULL);
    eprintf("%s: [ ", name);
    for (i = 0; i < n; i++) {
        mpr_id id = mpr_sig_get_inst_id(sig, i, MPR_STATUS_ALL);
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

void release_active_instances(mpr_sig sig)
{
    int i, n = mpr_sig_get_num_inst(sig, MPR_STATUS_ACTIVE);
    for (i = 0; i < n; i++)
        mpr_sig_release_inst(sig, mpr_sig_get_inst_id(sig, 0, MPR_STATUS_ACTIVE));
}

void loop(instance_type src_type, instance_type dst_type)
{
    int i = 0, j, num_parallel_inst = 5;
    float valf = 0;
    mpr_id inst;
    received = 0;

    eprintf("-------------------- GO ! --------------------\n");

    while (i < iterations && !done) {
        if (src_type & INSTANCED) {
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
        if (src_type & SINGLETON) {
            /* update singleton source signal */
            eprintf("--> Updating monosend to %d\n", i);
            mpr_sig_set_value(monosend, 0, 1, MPR_INT32, &i);
        }

        mpr_dev_poll(src, 0);
        mpr_dev_poll(dst, period);
        i++;

        if (verbose) {
            printf("ID:    ");
            if (src_type & SINGLETON)
                print_instance_ids(monosend);
            if (src_type & INSTANCED)
                print_instance_ids(multisend);
            if (dst_type & SINGLETON)
                print_instance_ids(monorecv);
            if (dst_type & INSTANCED)
                print_instance_ids(multirecv);
            printf("\n");

            printf("IDX:   ");
            if (src_type & SINGLETON)
                print_instance_idx(monosend);
            if (src_type & INSTANCED)
                print_instance_idx(multisend);
            if (dst_type & SINGLETON)
                print_instance_idx(monorecv);
            if (dst_type & INSTANCED)
                print_instance_idx(multirecv);
            printf("\n");

            printf("VALUE: ");
            if (src_type & SINGLETON)
                print_instance_vals(monosend);
            if (src_type & INSTANCED)
                print_instance_vals(multisend);
            if (dst_type & SINGLETON)
                print_instance_vals(monorecv);
            if (dst_type & INSTANCED)
                print_instance_vals(multirecv);
            printf("\n");
        }
        else {
            printf("\r  Iteration: %4d, Received: %4d", i, received);
            fflush(stdout);
        }
    }
}

void ctrlc(int sig)
{
    done = 1;
}

int run_test(test_config *config)
{
    mpr_sig *src_ptr, *dst_ptr;
    mpr_sig both_src[2];
    int num_src = 1, stl, evt = MPR_SIG_UPDATE, use_inst, compare_count;
    int result = 0, active_count = 0, reserve_count = 0, count_epsilon;
    mpr_map map;
    mpr_id_map *id_map;

    both_src[0] = monosend;
    both_src[1] = multisend;

    printf("Configuration %d: %s%s %s> %s%s%s%s%s%s\n",
           config->test_id,
           instance_type_names[config->src_type],
           config->process_loc == MPR_LOC_SRC ? "*" : "",
           config->map_type == SINGLETON ? "––" : "==",
           instance_type_names[config->dst_type],
           config->process_loc == MPR_LOC_DST ? "*" : "",
           config->oflw_action ? "; overflow: " : "",
           oflw_action_names[config->oflw_action],
           config->expr ? "; expression: " : "",
           config->expr ? config->expr : "");

    switch (config->src_type) {
        case SINGLETON:
            src_ptr = &monosend;
            break;
        case INSTANCED:
            src_ptr = &multisend;
            break;
        case MIXED_SIG:
            src_ptr = both_src;
            num_src = 2;
            break;
        default:
            eprintf("unexpected destination signal\n");
            return 1;
    }

    switch (config->dst_type) {
        case SINGLETON:
            dst_ptr = &monorecv;
            break;
        case INSTANCED:
            dst_ptr = &multirecv;
            break;
        default:
            eprintf("unexpected destination signal\n");
            return 1;
    }

    switch(config->oflw_action) {
        case NEW:
            stl = MPR_STEAL_NEWEST;
            break;
        case OLD:
            stl = MPR_STEAL_OLDEST;
            break;
        case ADD:
            evt = MPR_SIG_UPDATE | MPR_SIG_INST_OFLW | MPR_SIG_REL_UPSTRM;
        default:
            stl = MPR_STEAL_NONE;
    }
    mpr_obj_set_prop((mpr_obj)multirecv, MPR_PROP_STEAL_MODE, NULL, 1, MPR_INT32, &stl, 1);
    mpr_sig_set_cb(multirecv, handler, evt);

    map = mpr_map_new(num_src, src_ptr, 1, dst_ptr);
    mpr_obj_set_prop((mpr_obj)map, MPR_PROP_PROCESS_LOC, NULL, 1, MPR_INT32, &config->process_loc, 1);
    if (config->expr)
        mpr_obj_set_prop((mpr_obj)map, MPR_PROP_EXPR, NULL, 1, MPR_STR, config->expr, 1);

    use_inst = config->map_type == INSTANCED;
    mpr_obj_set_prop((mpr_obj)map, MPR_PROP_USE_INST, NULL, 1, MPR_BOOL, &use_inst, 1);
    mpr_obj_push((mpr_obj)map);
    while (!done && !mpr_map_get_is_ready(map)) {
        mpr_dev_poll(src, 100);
        mpr_dev_poll(dst, 100);
    }
    mpr_dev_poll(src, 100);
    mpr_dev_poll(dst, 100);

    /* remove any extra destination instances allocated by previous tests */
    while (5 <= mpr_sig_get_num_inst(multirecv, MPR_STATUS_ALL)) {
        eprintf("removing extra destination instance\n");
        mpr_sig_remove_inst(multirecv, mpr_sig_get_inst_id(multirecv, 4, MPR_STATUS_ALL));
    }

    mpr_dev_poll(src, 100);
    mpr_dev_poll(dst, 100);

    if (INSTANCED & config->dst_type && SINGLETON == config->map_type) {
        /* activate 3 destination instances */
        eprintf("activating 3 destination instances\n");
        mpr_sig_activate_inst(multirecv, 2);
        mpr_sig_activate_inst(multirecv, 4);
        mpr_sig_activate_inst(multirecv, 6);
    }

    loop(config->src_type, config->dst_type);

    compare_count = ((float)iterations * config->count_multiplier);

    release_active_instances(multisend);

    /* Warning: assuming that map has not been released by a peer process is not safe! Do not do
     * this in non-test code. */
    mpr_map_release(map);

    /* TODO: we shouldn't have to wait here... */
    mpr_dev_poll(src, 100);
    mpr_dev_poll(dst, 100);
    mpr_dev_poll(src, 100);
    mpr_dev_poll(dst, 100);

    release_active_instances(multirecv);

    if (((mpr_local_sig)multisend)->idmap_len > 8) {
        printf("Error: multisend using %d id maps (should be %d)\n",
               ((mpr_local_sig)multisend)->idmap_len, 8);
        ++result;
    }
    if (((mpr_local_sig)multirecv)->idmap_len > 8) {
        printf("Error: multirecv using %d id maps (should be %d)\n",
               ((mpr_local_sig)multirecv)->idmap_len, 8);
        ++result;
    }

    id_map = &((mpr_local_dev)src)->idmaps.active[0];
    while (*id_map) {
        ++active_count;
        id_map = &(*id_map)->next;
    }
    id_map = &((mpr_local_dev)src)->idmaps.reserve;
    while (*id_map) {
        ++reserve_count;
        id_map = &(*id_map)->next;
    }
    if (active_count > 1 || reserve_count > 5) {
        printf("Error: src device using %d active and %d reserve id maps (should be 0 and <=10)\n",
               active_count, reserve_count);
        id_map = &((mpr_local_dev)src)->idmaps.active[0];
        while (*id_map) {
            printf("  LID*%d: %"PR_MPR_ID", GID*%d: %"PR_MPR_ID"\n", (*id_map)->LID_refcount,
                   (*id_map)->LID, (*id_map)->GID_refcount, (*id_map)->GID);
            id_map = &(*id_map)->next;
        }
        ++result;
    }

    active_count = reserve_count = 0;
    id_map = &((mpr_local_dev)dst)->idmaps.active[0];
    while (*id_map) {
        ++active_count;
        id_map = &(*id_map)->next;
    }
    id_map = &((mpr_local_dev)dst)->idmaps.reserve;
    while (*id_map) {
        ++reserve_count;
        id_map = &(*id_map)->next;
    }
    if (active_count > 1 || reserve_count >= 10) {
        printf("Error: dst device using %d active and %d reserve id maps (should be 0 and <10)\n",
               active_count, reserve_count);
        id_map = &((mpr_local_dev)dst)->idmaps.active[0];
        while (*id_map) {
            printf("  LID*%d: %"PR_MPR_ID", GID*%d: %"PR_MPR_ID"\n", (*id_map)->LID_refcount,
                   (*id_map)->LID, (*id_map)->GID_refcount, (*id_map)->GID);
            id_map = &(*id_map)->next;
        }
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
            config->map_type == SINGLETON ? "––" : "==",
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
    int i, j, result = 0;

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
    if (setup_src()) {
        eprintf("Done initializing source.\n");
        result = 1;
        goto done;
    }
    wait_devs();

    eprintf("Key:\n");
    eprintf("  *\t denotes processing location\n");
    eprintf("  ––>\t singleton (non-instanced) map\n");
    eprintf("  ==>\t instanced map\n");

    i = 0;
    while (!done && i < NUM_TESTS) {
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
    printf("..................................................Test %s\x1B[0m.\n",
           result ? "\x1B[31mFAILED" : "\x1B[32mPASSED");
    return result;
}
