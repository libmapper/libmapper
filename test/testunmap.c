#include "../src/graph.h"
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
int terminate = 0;
int shared_graph = 0;
int autoconnect = 1;
int done = 0;

mpr_dev src = 0;
mpr_dev dst = 0;
mpr_graph srcgraph = 0;
mpr_graph dstgraph = 0;
mpr_sig sendsig = 0;
mpr_sig recvsig = 0;

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

    sendsig = mpr_sig_new(src, MPR_DIR_OUT, "outsig", 1, MPR_INT32, NULL, &mn, &mx, NULL, NULL, 0);

    eprintf("Output signal 'outsig' registered.\n");
    l = mpr_dev_get_sigs(src, MPR_DIR_OUT);
    eprintf("Number of outputs: %d\n", mpr_list_get_size(l));
    mpr_list_free(l);

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

    recvsig = mpr_sig_new(dst, MPR_DIR_IN, "insig", 1, MPR_FLT, NULL, &mn, &mx, NULL, NULL, 0);
    mpr_sig_set_value(recvsig, 0, 1, MPR_FLT, &mn);

    eprintf("Input signal 'insig' registered.\n");
    l = mpr_dev_get_sigs(dst, MPR_DIR_IN);
    eprintf("Number of inputs: %d\n", mpr_list_get_size(l));
    mpr_list_free(l);

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

int setup_maps()
{
    mpr_map map = mpr_map_new(1, &sendsig, 1, &recvsig);

    mpr_obj_push((mpr_obj)map);

    /* Wait until mapping has been established */
    while (!done && !mpr_map_get_is_ready(map)) {
        mpr_dev_poll(src, 10);
        mpr_dev_poll(dst, 10);
    }

    /* release the map */
    mpr_map_release(map);

    return 0;
}

void wait_ready()
{
    while (!done && !(mpr_dev_get_is_ready(src) && mpr_dev_get_is_ready(dst))) {
        mpr_dev_poll(src, 25);
        mpr_dev_poll(dst, 25);
    }
}

void loop()
{
    int i = 0;
    float dst_val, last_dst_val = -1;
    eprintf("Polling device..\n");
    while ((!terminate || srcgraph->links || dstgraph->links) && !done) {
        eprintf("Updating signal %s to %d\n",
                sendsig && sendsig->name ? sendsig->name : "", i);
        mpr_sig_set_value(sendsig, 0, 1, MPR_INT32, &i);
        sent++;
        mpr_dev_poll(src, 0);
        mpr_dev_poll(dst, 100);
        dst_val = *(float*)mpr_sig_get_value(recvsig, 0, 0);
        if (dst_val != last_dst_val) {
            ++received;
            last_dst_val = dst_val;
        }
        /* test if we can still set value for the destination signal */
        mpr_sig_set_value(recvsig, 0, 1, MPR_FLT, &dst_val);
        i++;

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

    /* process flags for -v verbose, -t terminate, -h help */
    for (i = 1; i < argc; i++) {
        if (argv[i] && argv[i][0] == '-') {
            int len = strlen(argv[i]);
            for (j = 1; j < len; j++) {
                switch (argv[i][j]) {
                    case 'h':
                        printf("testunmap.c: possible arguments "
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

    if (setup_src(g, iface)) {
        eprintf("Done initializing source.\n");
        result = 1;
        goto done;
    }

    wait_ready();

    if (autoconnect && setup_maps()) {
        eprintf("Error initializing maps.\n");
        result = 1;
        goto done;
    }

    loop();

    if ((srcgraph && srcgraph->links) || (dstgraph && dstgraph->links)) {
        eprintf("Link cleanup failed.\n");
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
