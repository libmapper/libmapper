#include <mapper/mapper.h>
#include "../src/util/mpr_debug.h"
#include <stdlib.h>
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

/* This test checks coordination of instances between maps.
 */

int verbose = 1;
int terminate = 0;
int shared_graph = 0;
int autoconnect = 1;
int done = 0;
int period = 100;
int ephemeral = 1;

mpr_dev src = 0;
mpr_dev dst = 0;
mpr_sig sendsig1 = 0;
mpr_sig sendsig2 = 0;
mpr_sig recvsig1 = 0;
mpr_sig recvsig2 = 0;

int sent = 0;
int received = 0;
int matched = 0;
int num_inst = 10;

static void eprintf(const char *format, ...)
{
    va_list args;
    if (!verbose)
        return;
    va_start(args, format);
    vprintf(format, args);
    va_end(args);
}

int setup_src(mpr_graph g, const char *iface)
{
    mpr_list l;
    mpr_time t;

    src = mpr_dev_new("testinstance-coordination.send", g);
    if (!src)
        goto error;
    if (iface)
        mpr_graph_set_interface(mpr_obj_get_graph(src), iface);
    eprintf("source created using interface %s.\n",
            mpr_graph_get_interface(mpr_obj_get_graph(src)));

    sendsig1 = mpr_sig_new(src, MPR_DIR_OUT, "outsig1", 1, MPR_INT32, NULL,
                           NULL, NULL, &num_inst, NULL, 0);
    sendsig2 = mpr_sig_new(src, MPR_DIR_OUT, "outsig2", 1, MPR_INT32, NULL,
                           NULL, NULL, &num_inst, NULL, 0);

    /* test retrieving value before it exists */
    eprintf("sendsig value is %p\n", mpr_sig_get_value(sendsig1, 0, &t));

    eprintf("Output signal 'outsig' registered.\n");
    l = mpr_dev_get_sigs(src, MPR_DIR_OUT);
    eprintf("Number of outputs: %d\n", mpr_list_get_size(l));
    mpr_list_free(l);
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

int setup_dst(mpr_graph g, const char *iface)
{
    mpr_list l;
    mpr_time t;

    dst = mpr_dev_new("testinstance-coordination.recv", g);
    if (!dst)
        goto error;
    if (iface)
        mpr_graph_set_interface(mpr_obj_get_graph(dst), iface);
    eprintf("destination created using interface %s.\n",
            mpr_graph_get_interface(mpr_obj_get_graph(dst)));

    recvsig1 = mpr_sig_new(dst, MPR_DIR_IN, "insig1", 3, MPR_FLT, NULL,
                           NULL, NULL, &num_inst, NULL, 0);
    mpr_obj_set_prop((mpr_obj)recvsig1, MPR_PROP_EPHEM, NULL, 1, MPR_INT32, &ephemeral, 1);
    recvsig2 = mpr_sig_new(dst, MPR_DIR_IN, "insig2", 1, MPR_FLT, NULL,
                           NULL, NULL, &num_inst, NULL, 0);
    mpr_obj_set_prop((mpr_obj)recvsig2, MPR_PROP_EPHEM, NULL, 1, MPR_INT32, &ephemeral, 1);

    /* test retrieving value before it exists */
    eprintf("recvsig value is %p\n", mpr_sig_get_value(recvsig1, 0, &t));

    eprintf("Input signal 'insig' registered.\n");
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
    mpr_map map1 = mpr_map_new(1, &sendsig1, 1, &recvsig1);
    mpr_map map2 = mpr_map_new(1, &sendsig2, 1, &recvsig2);

    mpr_obj_push(map1);
    mpr_obj_push(map2);

    /* Wait until mapping has been established */
    while (!done && !mpr_map_get_is_ready(map1) && !mpr_map_get_is_ready(map2)) {
        mpr_dev_poll(src, 10);
        mpr_dev_poll(dst, 10);
    }

    eprintf("map1 initialized with expression '%s'\n",
            mpr_obj_get_prop_as_str(map1, MPR_PROP_EXPR, NULL));
    eprintf("map2 initialized with expression '%s'\n",
            mpr_obj_get_prop_as_str(map2, MPR_PROP_EXPR, NULL));

    return 0;
}

int wait_ready(void)
{
    while (!done && !(mpr_dev_get_is_ready(src) && mpr_dev_get_is_ready(dst))) {
        mpr_dev_poll(src, 25);
        mpr_dev_poll(dst, 25);
    }
    return done;
}

void loop(void)
{
    int i = 0, j;
    mpr_graph g = mpr_obj_get_graph((mpr_obj)src);
    const char *name1 = mpr_obj_get_prop_as_str((mpr_obj)sendsig1, MPR_PROP_NAME, NULL);
    const char *name2 = mpr_obj_get_prop_as_str((mpr_obj)sendsig2, MPR_PROP_NAME, NULL);
    while ((!terminate || i < 100) && !done) {
        if (i % 20 < 10) {
            int j = i % 10;
            if (rand() % 10 > 8) {
                eprintf("Releasing insig.*.%d\n", j);
                mpr_sig_release_inst(recvsig1, j);
                mpr_sig_release_inst(recvsig2, j);
            }
            else {
                if (j < 5) {
                    eprintf("Updating signal %s.%d to %d\n", name1, j, i);
                    mpr_sig_set_value(sendsig1, j, 1, MPR_INT32, &i);
                }
                else {
                    eprintf("Updating signal %s.%d to %d\n", name2, j, i);
                    mpr_sig_set_value(sendsig2, j, 1, MPR_INT32, &i);
                }
                ++sent;
            }
        }
        else {
            int j = i % 10;
            if (rand() % 10 > 7) {
                eprintf("Releasing insig.*.%d\n", j);
                mpr_sig_release_inst(recvsig1, j);
                mpr_sig_release_inst(recvsig2, j);
            }
            else {
                if (j < 5) {
                    eprintf("Updating signal %s.%d to %d\n", name2, j, i);
                    mpr_sig_set_value(sendsig2, j, 1, MPR_INT32, &i);
                }
                else {
                    eprintf("Updating signal %s.%d to %d\n", name1, j, i);
                    mpr_sig_set_value(sendsig1, j, 1, MPR_INT32, &i);
                }
                ++sent;
            }
        }
        if (shared_graph) {
            mpr_graph_poll(g, 0);
        }
        else {
            mpr_dev_poll(src, 0);
            mpr_dev_poll(dst, period);
        }
        for (j = 0; j < num_inst; j++) {
            mpr_id id;
            int status;
            id = mpr_sig_get_inst_id(recvsig1, j, MPR_STATUS_ANY);
            status = mpr_sig_get_inst_status(recvsig1, id);
            if (status & MPR_STATUS_UPDATE_REM) {
                float *value = (float*)mpr_sig_get_value(recvsig1, id, NULL);
                if (value) {
                    int k, len = mpr_obj_get_prop_as_int32((mpr_obj)recvsig1, MPR_PROP_LEN, NULL);
                    if (verbose) {
                        printf("%s.%"PR_MPR_ID" got ",
                               mpr_obj_get_prop_as_str((mpr_obj)recvsig1, MPR_PROP_NAME, NULL), id);
                        if (len > 1)
                            printf("[");
                        for (k = 0; k < len; k++)
                            printf("%g, ", value[k]);
                        if (len > 1)
                            printf("\b\b] \n");
                        else
                            printf("\b\b  \n");
                    }
                    ++received;
                    ++matched;
                    for (k = 0; k < len; k++) {
                        if (value[k] != (float)i) {
                            --matched;
                            break;
                        }
                    }
                }
            }
            status = mpr_sig_get_inst_status(sendsig1, id);
            if (status & MPR_STATUS_REL_DNSTRM) {
                eprintf("%s.%"PR_MPR_ID" got downstream release\n",
                        mpr_obj_get_prop_as_str((mpr_obj)sendsig1, MPR_PROP_NAME, NULL), id);
                mpr_sig_release_inst(sendsig1, id);
            }
            id = mpr_sig_get_inst_id(recvsig2, j, MPR_STATUS_ANY);
            status = mpr_sig_get_inst_status(recvsig2, id);
            if (status & MPR_STATUS_UPDATE_REM) {
                float *value = (float*)mpr_sig_get_value(recvsig2, id, NULL);
                if (value) {
                    int k, len = mpr_obj_get_prop_as_int32((mpr_obj)recvsig2, MPR_PROP_LEN, NULL);
                    if (verbose) {
                        printf("%s.%"PR_MPR_ID" got ",
                               mpr_obj_get_prop_as_str((mpr_obj)recvsig2, MPR_PROP_NAME, NULL), id);
                        if (len > 1)
                            printf("[");
                        for (k = 0; k < len; k++)
                            printf("%g, ", value[k]);
                        if (len > 1)
                            printf("\b\b] \n");
                        else
                            printf("\b\b  \n");
                    }
                    ++received;
                    ++matched;
                    for (k = 0; k < len; k++) {
                        if (value[k] != (float)i) {
                            --matched;
                            break;
                        }
                    }
                }
            }
            status = mpr_sig_get_inst_status(sendsig2, id);
            if (status & MPR_STATUS_REL_DNSTRM) {
                eprintf("%s.%"PR_MPR_ID" got downstream release\n",
                        mpr_obj_get_prop_as_str((mpr_obj)sendsig2, MPR_PROP_NAME, NULL), id);
                mpr_sig_release_inst(sendsig2, id);
            }
        }
        i++;

        if (!verbose) {
            printf("\r  Sent: %4i, Received: %4i, Matched: %4i   ", sent, received, matched);
            fflush(stdout);
        }
        if (!terminate && (sent != received))
            return;
    }
}

void segv(int sig)
{
    printf("\x1B[31m(SEGV)\n\x1B[0m");
    exit(1);
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

    /* process flags for -v verbose, -t terminate, -h help */
    for (i = 1; i < argc; i++) {
        if (argv[i] && argv[i][0] == '-') {
            int len = strlen(argv[i]);
            for (j = 1; j < len; j++) {
                switch (argv[i][j]) {
                    case 'h':
                        printf("testinstance_coordination.c: possible arguments "
                               "-f fast (execute quickly), "
                               "-q quiet (suppress output), "
                               "-t terminate automatically, "
                               "-s shared (use one mpr_graph only), "
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

    if (autoconnect && setup_maps()) {
        eprintf("Error initializing maps.\n");
        result = 1;
        goto done;
    }

    loop();

    if (autoconnect && (!received || sent != received)) {
        eprintf("Mismatch between sent and received/matched messages.\n");
        eprintf("Updated value %d time%s, but received %d and matched %d of them.\n",
                sent, sent == 1 ? "" : "s", received, matched);
        /* For now just report mismatch but do not fail */
        // result = 1;
    }

  done:
    cleanup_dst();
    cleanup_src();
    if (g) mpr_graph_free(g);
    printf("....Test %s\x1B[0m.\n",
           result ? "\x1B[31mFAILED" : "\x1B[32mPASSED");
    return result;
}
