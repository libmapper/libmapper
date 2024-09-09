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

int verbose = 1;
int shared_graph = 0;
int num_inst = 10;
int done = 0;

mpr_dev src = 0;
mpr_dev dst = 0;
mpr_graph srcgraph = 0;
mpr_graph dstgraph = 0;
mpr_sig sendsig = 0;
mpr_sig recvsig = 0;
mpr_id map_id;

int src_linked = 0;
int dst_linked = 0;

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

int setup_src(mpr_graph g, const char *iface)
{
    int mn = 0, mx = 1;
    mpr_list l;

    src = mpr_dev_new("testunmap-send", g);
    if (!src)
        goto error;
    srcgraph = mpr_obj_get_graph((mpr_obj)src);
    if (iface)
        mpr_graph_set_interface(srcgraph, iface);
    eprintf("source created using interface %s.\n", mpr_graph_get_interface(srcgraph));

    sendsig = mpr_sig_new(src, MPR_DIR_OUT, "outsig", 1, MPR_INT32, NULL, &mn, &mx, &num_inst, NULL, 0);
    if (!sendsig)
        goto error;

    eprintf("Output signal 'outsig' registered with %d instances.\n", num_inst);
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

    dst = mpr_dev_new("testunmap-recv", g);
    if (!dst)
        goto error;
    dstgraph = mpr_obj_get_graph((mpr_obj)dst);
    if (iface)
        mpr_graph_set_interface(dstgraph, iface);
    eprintf("destination created using interface %s.\n", mpr_graph_get_interface(dstgraph));

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
    mpr_map map = mpr_map_new(1, &sendsig, 1, &recvsig);

    mpr_obj_push((mpr_obj)map);

    /* Wait until mapping has been established */
    while (!done && !mpr_map_get_is_ready(map)) {
        mpr_dev_poll(src, 10);
        mpr_dev_poll(dst, 10);
    }

    map_id = mpr_obj_get_id((mpr_obj)map);

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

void loop(int count)
{
    const char *sig_name = mpr_obj_get_prop_as_str((mpr_obj)sendsig, MPR_PROP_NAME, NULL);
    mpr_list links = 0;
    eprintf("Polling device..\n");
    while (   !done
           && count-- > 0
           && (   (links = mpr_graph_get_list(srcgraph, MPR_LINK))
               || (links = mpr_graph_get_list(dstgraph, MPR_LINK)))) {
        int inst = random() % num_inst;
        mpr_list_free(links);
        eprintf("Updating signal %s.%d to %d\n", sig_name, inst, sent);
        mpr_sig_set_value(sendsig, inst, 1, MPR_INT32, &sent);
        sent++;
        if (!shared_graph)
            mpr_dev_poll(src, 0);
        mpr_dev_poll(dst, 100);

        if (!verbose) {
            printf("\r  Sent: %4i, Received: %4i   ", sent, received);
            fflush(stdout);
        }
    }
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
    mpr_list links;

    /* process flags for -v verbose, -h help */
    for (i = 1; i < argc; i++) {
        if (argv[i] && argv[i][0] == '-') {
            int len = strlen(argv[i]);
            for (j = 1; j < len; j++) {
                switch (argv[i][j]) {
                    case 'h':
                        printf("testunmap.c: possible arguments "
                               "-q quiet (suppress output), "
                               "-s shared (use one mpr_graph only), "
                               "-h help, "
                               "--iface network interface\n");
                        return 1;
                        break;
                    case 'q':
                        verbose = 0;
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

    if (setup_maps()) {
        eprintf("Error initializing map.\n");
        result = 1;
        goto done;
    }

    /* update some instances */
    loop(50);

    num_inst = mpr_sig_get_num_inst(recvsig, MPR_STATUS_ACTIVE);
    eprintf("Destination has %d active instances before map release.\n", num_inst);

    /* remove the map */
    map = (mpr_map)mpr_graph_get_obj(dstgraph, map_id, MPR_MAP);
    if (map) {
        eprintf("Removing map.\n");
        mpr_map_release(map);
    }
    else {
        eprintf("Error retrieving map.\n");
        result = 1;
        goto done;
    }

    /* wait for link cleanup */
    loop(400);

    if (   (srcgraph && (links = mpr_graph_get_list(srcgraph, MPR_LINK)))
        || (dstgraph && (links = mpr_graph_get_list(dstgraph, MPR_LINK)))) {
        eprintf("Link cleanup failed.\n");
        result = 1;
        mpr_list_free(links);
        goto done;
    }

    /* check whether destination instances were released */
    num_inst = mpr_sig_get_num_inst(recvsig, MPR_STATUS_ACTIVE);
    if (num_inst) {
        eprintf("Destination has %d active instance%s (should be 0).\n", num_inst,
                num_inst > 1 ? "s" : "");
        result = 1;
        goto done;
    }

  done:
    cleanup_dst();
    cleanup_src();
    if (g) mpr_graph_free(g);
    printf("...................Test %s\x1B[0m.\n",
           result ? "\x1B[31mFAILED" : "\x1B[32mPASSED");
    return result;
}
