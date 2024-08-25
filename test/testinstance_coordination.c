#include <mapper/mapper.h>
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

int verbose = 1;
int terminate = 0;
int shared_graph = 0;
int autoconnect = 1;
int done = 0;
int period = 100;
int ephemeral = 0;

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
    int mn=0, mx=1;
    mpr_list l;
    mpr_time t;

    src = mpr_dev_new("testlinear.send", g);
    if (!src)
        goto error;
    if (iface)
        mpr_graph_set_interface(mpr_obj_get_graph(src), iface);
    eprintf("source created using interface %s.\n",
            mpr_graph_get_interface(mpr_obj_get_graph(src)));

    sendsig1 = mpr_sig_new(src, MPR_DIR_OUT, "outsig1", 1, MPR_INT32, NULL,
                           &mn, &mx, &num_inst, NULL, 0);
    sendsig2 = mpr_sig_new(src, MPR_DIR_OUT, "outsig2", 1, MPR_INT32, NULL,
                           &mn, &mx, &num_inst, NULL, 0);

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

void handler(mpr_sig sig, mpr_sig_evt event, mpr_id instance, int length,
             mpr_type type, const void *value, mpr_time t)
{
    if (value) {
        eprintf("handler: %s.%llu got %f\n",
                mpr_obj_get_prop_as_str((mpr_obj)sig, MPR_PROP_NAME, NULL),
                instance, (*(float*)value));
        received++;
    }
}

int setup_dst(mpr_graph g, const char *iface)
{
    float mn=0, mx=1;
    mpr_list l;
    mpr_time t;

    dst = mpr_dev_new("testlinear.recv", g);
    if (!dst)
        goto error;
    if (iface)
        mpr_graph_set_interface(mpr_obj_get_graph(dst), iface);
    eprintf("destination created using interface %s.\n",
            mpr_graph_get_interface(mpr_obj_get_graph(dst)));

    recvsig1 = mpr_sig_new(dst, MPR_DIR_IN, "insig1", 1, MPR_FLT, NULL,
                           &mn, &mx, &num_inst, handler, MPR_SIG_UPDATE);
    mpr_obj_set_prop((mpr_obj)recvsig1, MPR_PROP_EPHEM, NULL, 1, MPR_INT32, &ephemeral, 1);
    recvsig2 = mpr_sig_new(dst, MPR_DIR_IN, "insig2", 1, MPR_FLT, NULL,
                           &mn, &mx, &num_inst, handler, MPR_SIG_UPDATE);
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

    eprintf("map2 initialized with expression '%s'\n",
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
    int i = 0;
    mpr_graph g = mpr_obj_get_graph((mpr_obj)src);
    const char *name1 = mpr_obj_get_prop_as_str((mpr_obj)sendsig1, MPR_PROP_NAME, NULL);
    const char *name2 = mpr_obj_get_prop_as_str((mpr_obj)sendsig2, MPR_PROP_NAME, NULL);
    while ((!terminate || i < 50) && !done) {
        int inst_id = rand() % 10;
        if (i >= 5) {
            eprintf("Updating signal %s.%d to %d\n", name1, inst_id, i);
            mpr_sig_set_value(sendsig1, inst_id, 1, MPR_INT32, &i);
        }
        eprintf("Updating signal %s.%d to %d\n", name2, inst_id, i);
        mpr_sig_set_value(sendsig2, inst_id, 1, MPR_INT32, &i);
        sent++;
        if (shared_graph) {
            mpr_graph_poll(g, 0);
        }
        else {
            mpr_dev_poll(src, 0);
            mpr_dev_poll(dst, period);
        }
        i++;

        if (!verbose) {
            printf("\r  Sent: %4i, Received: %4i, Matched: %4i   ", sent, received, matched);
            fflush(stdout);
        }
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
                        printf("testlinear.c: possible arguments "
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

    if (autoconnect && (!received || (sent * 2 - 5) != received)) {
        eprintf("Mismatch between sent and received/matched messages.\n");
        eprintf("Updated value %d time%s, but received %d and matched %d of them.\n",
                sent, sent == 1 ? "" : "s", received, matched);
        result = 1;
    }

  done:
    cleanup_dst();
    cleanup_src();
    if (g) mpr_graph_free(g);
    printf("....Test %s\x1B[0m.\n",
           result ? "\x1B[31mFAILED" : "\x1B[32mPASSED");
    return result;
}
