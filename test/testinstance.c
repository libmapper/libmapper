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

const char *sig_type_names[] = { "SINGLETON", "INSTANCED", "MIXED" };

typedef enum {
    SINGLETON,   // singleton
    INSTANCED,   // instanced
    MIXED    // mixed convergent
} sig_type;

const char *overflow_action_names[] = { "none", "steal newest", "steal oldest",
                                        "add instance" };

typedef enum {
    NONE = MPR_STEAL_NONE,
    NEW = MPR_STEAL_NEWEST,
    OLD = MPR_STEAL_OLDEST,
    ADD
} oflw_action;

typedef struct _test_config {
    sig_type    src_type;
    sig_type    dst_type;
    mpr_loc     process_loc;
    oflw_action overflow_action;
    int         use_inst;
    const char  *expr;
    float       count_multiplier;
    float       count_epsilon;
} test_config;

test_config test_configs[] = {
    // singleton -> instanced; control a single instance (default?)
    // TODO: should work with epsilon=0.0
    { SINGLETON, INSTANCED, MPR_LOC_SRC, NONE, 1, "alive=1;y=x;", 1., 0.01 }, // OK
    { SINGLETON, INSTANCED, MPR_LOC_DST, NONE, 1, "alive=1;y=x;", 1., 0.01 }, // OK

    // singleton -> instanced; control all active instances
//    { SINGLETON, INSTANCED, MPR_LOC_SRC, NONE, 0, "y=x;", 3., 0. }, // FAIL
//    { SINGLETON, INSTANCED, MPR_LOC_DST, NONE, 0, "y=x;", 3., 0. }, // FAIL

    // instanced -> singleton; default
    // CHECK: should be low received count
    // CHECK: if controlling instance is released, move to next updated inst
//    { INSTANCED, SINGLETON, MPR_LOC_SRC, NONE, 1, NULL, 1., 0. }, // FAIL
//    { INSTANCED, SINGLETON, MPR_LOC_DST, NONE, 1, NULL, 1., 0. }, // FAIL

    // instanced -> instanced; no stealing
    // TODO: test what happens if map->uses_inst=0
    // TODO: try to reduce epsilons to zero (then eliminate them)
    { INSTANCED, INSTANCED, MPR_LOC_SRC, NONE, 1, "y{-1}=-10;y=y{-1}+1", 4., 0.01 }, // OK
    { INSTANCED, INSTANCED, MPR_LOC_DST, NONE, 1, "y{-1}=-10;y=y{-1}+1", 4., 0.01 }, // OK

    // instanced -> instanced; steal newest instance
    { INSTANCED, INSTANCED, MPR_LOC_SRC, NEW, 1, "y{-1}=-10;y=y{-1}+1", 4., 0.01 }, // OK
    { INSTANCED, INSTANCED, MPR_LOC_DST, NEW, 1, "y{-1}=-10;y=y{-1}+1", 4., 0.01 }, // OK

    // instanced -> instanced; steal oldest instance
    { INSTANCED, INSTANCED, MPR_LOC_SRC, OLD, 1, "y{-1}=-10;y=y{-1}+1", 4., 0.01 }, // OK
    { INSTANCED, INSTANCED, MPR_LOC_DST, OLD, 1, "y{-1}=-10;y=y{-1}+1", 4., 0.01 }, // OK

    // instanced -> instanced; add instances if needed
//    { INSTANCED, INSTANCED, MPR_LOC_SRC, ADD, 1, "y{-1}=-10;y=y{-1}+1", 5., 0.1 }, // FAIL
//    { INSTANCED, INSTANCED, MPR_LOC_DST, ADD, 1, "y{-1}=-10;y=y{-1}+1", 5., 0.1 }, // FAIL

    // mixed convergent
    { MIXED,     INSTANCED, MPR_LOC_SRC, NONE, 1, "y = x0+x1", 8.0, 0.01 }, // OK
//    { MIXED,     INSTANCED, MPR_LOC_DST, NONE, 1, "y = x0+x1", 8.0, 0. }, // FAIL

    // singleton -> instanced; in-map instance management
    // NOT RECEIVING RELEASE AT DESTINATION (probably not sent?)
//    { SINGLETON, INSTANCED, MPR_LOC_SRC, NONE, 1, "alive=count>=5;y=x;count=(count+1)%10;", 0.5, 0.01 }, // PROBLEM
    { SINGLETON, INSTANCED, MPR_LOC_DST, NONE, 1, "alive=count>=5;y=x;count=(count+1)%10;", 0.5, 0.02 }, // OK

    // instanced -> instanced; in-map instance management
//    { INSTANCED, INSTANCED, MPR_LOC_SRC, NONE, 1, "alive=count>=5;y=x;count=(count+1)%10;", 0.5, 0.5 }, // FAIL
//    { INSTANCED, INSTANCED, MPR_LOC_DST, NONE, 1, "alive=count>=5;y=x;count=(count+1)%10;", 0.5, 0.5 }, // FAIL

    // TODO: src instance pooling (convergent)
    // TODO: dst instance pooling (divergent)
};
const int NUM_TESTS =
    sizeof(test_configs)/sizeof(test_configs[0]);

/*! Creation of a local source. */
int setup_src()
{
    src = mpr_dev_new("testinstance-send", 0);
    if (!src)
        goto error;

    float mn=0, mx=10;
    int num_inst = 10, stl = MPR_STEAL_OLDEST;

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
        mpr_sig_release_inst(sig, inst, MPR_NOW);
    }
    else if (val) {
        eprintf("--> destination %s instance %i got %f\n", name, (int)inst,
                (*(float*)val));
        ++received;
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
    monorecv = mpr_sig_new(dst, MPR_DIR_IN, "monorecv", 1, MPR_FLT, NULL,
                           &mn, NULL, 0, handler, MPR_SIG_UPDATE);
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

void wait_devs()
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

void release_active_instances(mpr_sig sig)
{
    int i, n = mpr_sig_get_num_inst(sig, MPR_STATUS_ACTIVE);
    for (i = 0; i < n; i++) {
        mpr_sig_release_inst(sig, mpr_sig_get_inst_id(sig, 0, MPR_STATUS_ACTIVE), MPR_NOW);
    }
}

void loop()
{
    eprintf("-------------------- GO ! --------------------\n");
    int i = 0, j, vali = 0, num_parallel_inst = 5;
    float valf = 0;
    mpr_id inst;
    received = 0;

    while (i < iterations && !done) {
        // here we should create, update and destroy some instances
        inst = i % 10;

        // try to destroy an instance
        eprintf("--> Retiring multisend instance %"PR_MPR_ID"\n", inst);
        mpr_sig_release_inst(multisend, inst, MPR_NOW);

        for (j = 0; j < num_parallel_inst; j++) {
            inst = (inst + 1) % 10;

            // try to update an instance
            valf = (rand() % 10) * 1.0f;
            eprintf("--> Updating multisend instance %"PR_MPR_ID" to %f\n", inst, valf);
            mpr_sig_set_value(multisend, inst, 1, MPR_FLT, &valf, MPR_NOW);
        }
        // also update mono-instance signal
        vali += (rand() % 5) - 2;
        eprintf("--> Updating monosend to %d\n", vali);
        mpr_sig_set_value(monosend, 0, 1, MPR_INT32, &vali, MPR_NOW);

        mpr_dev_poll(dst, period);
        mpr_dev_poll(src, 0);
        i++;

        if (verbose) {
            printf("ID:    monosend: n/a ");
            print_instance_ids(multisend);
            print_instance_ids(multirecv);
            printf("\n");

            printf("VALUE: monosend: %2.0f  ", *(float*)mpr_sig_get_value(monosend, 0, 0));
            print_instance_vals(multisend);
            print_instance_vals(multirecv);
            printf("\n");
        }
        else {
            printf("\r  Iteration: %d, Received: %d", i, received);
            fflush(stdout);
        }
    }
}

void ctrlc(int sig)
{
    done = 1;
}

int run_test(int test_num, test_config *config)
{
    mpr_sig *src_ptr, *dst_ptr;
    mpr_sig both_src[] = {monosend, multisend};

    if (verbose) {
        printf("Test %d: %s -> %s; processing location: %s; overflow action: %s\n",
               test_num, sig_type_names[config->src_type],
               sig_type_names[config->dst_type],
               config->process_loc == MPR_LOC_SRC ? "SRC" : "DST",
               overflow_action_names[config->overflow_action]);
    }
    else
        printf("Test %d:\n", test_num);

    int num_src = 1;
    switch (config->src_type) {
        case SINGLETON:
            src_ptr = &monosend;
            break;
        case INSTANCED:
            src_ptr = &multisend;
            break;
        case MIXED:
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
    mpr_map map = mpr_map_new(num_src, src_ptr, 1, dst_ptr);
    mpr_obj_set_prop((mpr_obj)map, MPR_PROP_PROCESS_LOC, NULL, 1, MPR_INT32,
                     &config->process_loc, 1);
    if (config->expr)
        mpr_obj_set_prop((mpr_obj)map, MPR_PROP_EXPR, NULL, 1, MPR_STR,
                         config->expr, 1);

    mpr_obj_set_prop((mpr_obj)map, MPR_PROP_USE_INST, NULL, 1, MPR_BOOL,
                     &config->use_inst, 1);
    mpr_obj_push((mpr_obj)map);
    while (!done && !mpr_map_get_is_ready(map)) {
        mpr_dev_poll(src, 100);
        mpr_dev_poll(dst, 100);
    }
    mpr_dev_poll(src, 100);
    mpr_dev_poll(dst, 100);

    // remove any extra destination instances allocated by previous tests
    int status = MPR_STATUS_ACTIVE | MPR_STATUS_RESERVED;
    while (5 <= mpr_sig_get_num_inst(multirecv, status)) {
        mpr_sig_remove_inst(multirecv, mpr_sig_get_inst_id(multirecv, 4, status), MPR_NOW);
    }

    if (!config->use_inst) {
        // activate 3 destination instances
        mpr_sig_activate_inst(multirecv, 2, MPR_NOW);
        mpr_sig_activate_inst(multirecv, 4, MPR_NOW);
        mpr_sig_activate_inst(multirecv, 6, MPR_NOW);
    }

    loop();

    int compare_count = ((float)iterations * config->count_multiplier);
    release_active_instances(multisend);
    release_active_instances(multirecv);

    mpr_map_release(map);
    mpr_dev_poll(src, 100);
    mpr_dev_poll(dst, 100);
    mpr_dev_poll(src, 100);
    mpr_dev_poll(dst, 100);

    int count_epsilon = (float)compare_count * config->count_epsilon;

    eprintf("Received %d of %d +/- %d updates\n",
            received, compare_count, count_epsilon);

    int result = abs(compare_count - received) > count_epsilon;

    eprintf("Test %d %s: %s -> %s, %s processing\n", test_num,
            result ? "FAILED" : "PASSED", sig_type_names[config->src_type],
            sig_type_names[config->dst_type],
            config->process_loc == MPR_LOC_SRC ? "SRC" : "DST");
    return result;
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

    i = 0;
    while (!done && i < NUM_TESTS) {
        test_config *config = &test_configs[i];
        if (run_test(i, config))
            return 1;
        ++i;
        printf("\n");
    }

  done:
    cleanup_dst();
    cleanup_src();
    printf("...................Test %s\x1B[0m.\n",
           result ? "\x1B[31mFAILED" : "\x1B[32mPASSED");
    return result;
}
