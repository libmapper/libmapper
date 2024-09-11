#include "../src/graph.h"
#include "../src/link.h"
#include <mapper/mapper.h>
#include <stdio.h>
#include <stdarg.h>
#include <math.h>
#ifdef WIN32
#include <io.h>
#else
#include <unistd.h>
#endif
#include <signal.h>
#include <string.h>

#define NUM_SRC 1

int verbose = 1;
int terminate = 0;
int shared_graph = 0;
int num_inst = 10;
int done = 0;

mpr_dev srcs[NUM_SRC];
mpr_dev dst = 0;

mpr_sig sendsigs[NUM_SRC];
mpr_sig recvsig = 0;

mpr_id map_id = 0;

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

int setup_srcs(mpr_graph g, const char *iface)
{
    int i, mn = 0, mx = 1;
    mpr_list l;

    for (i = 0; i < NUM_SRC; i++) {
        srcs[i] = mpr_dev_new("testremap-send", g);
        if (!srcs[i])
            goto error;

        if (iface)
            mpr_graph_set_interface(mpr_obj_get_graph((mpr_obj)srcs[i]), iface);
        eprintf("source created using interface %s.\n",
                mpr_graph_get_interface(mpr_obj_get_graph((mpr_obj)srcs[i])));

        sendsigs[i] = mpr_sig_new(srcs[i], MPR_DIR_OUT, "outsig", 1, MPR_INT32, NULL,
                                  &mn, &mx, &num_inst, NULL, 0);
        if (!sendsigs[i])
            goto error;

        eprintf("Output signal 'outsig' registered with %d instances.\n", num_inst);
        l = mpr_dev_get_sigs(srcs[i], MPR_DIR_OUT);
        eprintf("Number of outputs: %d\n", mpr_list_get_size(l));
        mpr_list_free(l);
    }

    return 0;

  error:
    return 1;
}

void cleanup_src(void)
{
    int i;
    for (i = 0; i < NUM_SRC; i++) {
        if (srcs[i]) {
            eprintf("Freeing source.. ");
            fflush(stdout);
            mpr_dev_free(srcs[i]);
            eprintf("ok\n");
        }
    }
}

void handler(mpr_sig sig, mpr_sig_evt evt, mpr_id id, int len, mpr_type type,
             const void *val, mpr_time t)
{
    if (evt == MPR_STATUS_REL_UPSTRM) {
        eprintf("%s.%llu got release\n",
                mpr_obj_get_prop_as_str((mpr_obj)sig, MPR_PROP_NAME, NULL), id);
        mpr_sig_release_inst(sig, id);
    }
    else if (val) {
        eprintf("%s.%llu got %f\n",
                mpr_obj_get_prop_as_str((mpr_obj)sig, MPR_PROP_NAME, NULL), id, *(float*)val);
        received++;
    }
}

int setup_dst(mpr_graph g, const char *iface)
{
    float mn = 0, mx = 1;
    mpr_list l;

    dst = mpr_dev_new("testremap-recv", g);
    if (!dst)
        goto error;
    if (iface)
        mpr_graph_set_interface(mpr_obj_get_graph((mpr_obj)dst), iface);
    eprintf("destination created using interface %s.\n",
            mpr_graph_get_interface(mpr_obj_get_graph((mpr_obj)dst)));

    recvsig = mpr_sig_new(dst, MPR_DIR_IN, "insig", 1, MPR_FLT, NULL,
                          &mn, &mx, &num_inst, handler, MPR_SIG_ALL);
    if (!recvsig)
        goto error;

    eprintf("Input signal 'insig' registered with %d instances.\n", num_inst);
    l = mpr_dev_get_sigs(dst, MPR_DIR_IN);
    eprintf("Number of inputs: %d\n", mpr_list_get_size(l));
    mpr_list_free(l);

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

int setup_maps(void)
{
    mpr_map map = mpr_map_new(NUM_SRC, sendsigs, 1, &recvsig);
    mpr_obj_push((mpr_obj)map);
    map_id = mpr_obj_get_id((mpr_obj)map);

    /* Wait until mapping has been established */
    while (!done && !mpr_map_get_is_ready(map)) {
        int i;
        for (i = 0; i < NUM_SRC; i++)
            mpr_dev_poll(srcs[i], 10);
        mpr_dev_poll(dst, 10);
    }

    return 0;
}

int wait_ready(void)
{
    eprintf("waiting for %d devices\n", NUM_SRC + 1);
    int ready = 0;
    while (!done && !ready) {
        int i;
        ready = 1;
        for (i = 0; i < NUM_SRC; i++) {
            mpr_dev_poll(srcs[i], 25);
            ready &= mpr_dev_get_is_ready(srcs[i]);
        }
        mpr_dev_poll(dst, 25);
        ready &= mpr_dev_get_is_ready(dst);
    }
    return done;
}

int loop()
{
    int i = 0, j, result = 0;
    eprintf("Polling device..\n");
    while ((!terminate || i < 99) && !done) {
        int inst = random() % num_inst;
        for (j = 0; j < NUM_SRC; j++) {
            eprintf("Updating srcs[%d].%d to %d\n", j, inst, sent);
            mpr_sig_set_value(sendsigs[j], inst, 1, MPR_INT32, &sent);
            if (!shared_graph)
                mpr_dev_poll(srcs[j], 0);
        }
        mpr_dev_poll(dst, 100);
        sent++;

        if (!verbose) {
            printf("\r  Sent: %4i, Received: %4i   ", sent, received);
            fflush(stdout);
        }
        ++i;

        if (i % 50 == 0) {
            mpr_map map = (mpr_map)mpr_graph_get_obj(mpr_obj_get_graph((mpr_obj)dst), map_id, MPR_MAP);
            if (map) {
                eprintf("Removing map.\n");
                mpr_map_release(map);
                map = 0;
            }
            else {
                eprintf("Error retrieving map.\n");
                result = 1;
                break;
            }

            eprintf("Recreating map with same signals.\n");
            map = mpr_map_new(NUM_SRC, sendsigs, 1, &recvsig);
            mpr_obj_push((mpr_obj)map);
            map_id = mpr_obj_get_id((mpr_obj)map);
        }
    }
    return result;
}

void ctrlc(int signal)
{
    done = 1;
}

int main(int argc, char **argv)
{
    int i, j, result = 0;
    char *iface = 0;
    mpr_graph g;
    mpr_map map;

    /* process flags for -v verbose, -h help */
    for (i = 1; i < argc; i++) {
        if (argv[i] && argv[i][0] == '-') {
            int len = strlen(argv[i]);
            for (j = 1; j < len; j++) {
                switch (argv[i][j]) {
                    case 'h':
                        printf("testremap.c: possible arguments "
                               "-q quiet (suppress output), "
                               "-t terminate automatically, "
                               "-s shared (use one mpr_graph only), "
                               "-h help, "
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
                        if (strcmp(argv[i], "--iface")==0 && argc>i+1) {
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

    signal(SIGINT, ctrlc);

    g = shared_graph ? mpr_graph_new(0) : 0;

    if (setup_dst(g, iface)) {
        eprintf("Error initializing destination.\n");
        result = 1;
        goto done;
    }

    if (setup_srcs(g, iface)) {
        eprintf("Done initializing source.\n");
        result = 1;
        goto done;
    }

    if (wait_ready()) {
        eprintf("Device registration aborted.\n");
        result = 1;
        goto done;
    }

    if (setup_maps()) {
        eprintf("Error initializing map.\n");
        result = 1;
        goto done;
    }

    /* update some instances */
    if ((result = loop()))
        goto done;

    /* remove the map */
    map = (mpr_map)mpr_graph_get_obj(mpr_obj_get_graph((mpr_obj)dst), map_id, MPR_MAP);
    if (map) {
        eprintf("Removing map.\n");
        mpr_map_release(map);
    }
    else {
        eprintf("Error retrieving map.\n");
        result = 1;
        goto done;
    }

    for (i = 0; i < 10; i++) {
        if (!shared_graph) {
            for (j = 0; j < NUM_SRC; j++) {
                mpr_dev_poll(srcs[j], 0);
            }
        }
        mpr_dev_poll(dst, 100);
    }

    /* check whether destination instances were released */
    num_inst = mpr_sig_get_num_inst(recvsig, MPR_STATUS_ACTIVE);
    if (num_inst) {
        eprintf("Destination has %d active instance%s (should be 0).\n", num_inst,
                num_inst > 1 ? "s" : "");
        result = 1;
        goto done;
    }

    if (sent != received) {
        eprintf("Mismatch between sent and received messages.\n");
        eprintf("Updated value %d time%s, but received %d update%s.\n",
                sent, sent == 1 ? "" : "s", received, received == 1 ? "" : "s");
        result = 1;
    }

  done:
    cleanup_dst();
    cleanup_src();
    if (g) mpr_graph_free(g);
    printf("...................Test %s\x1B[0m.\n",
           result ? "\x1B[31mFAILED" : "\x1B[32mPASSED");
    return result;
}
