#include <mapper/mapper.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <math.h>
#include <string.h>
#ifdef WIN32
#include <io.h>
#else
#include <unistd.h>
#endif
#include <signal.h>

#define MAX_NUM_MAP_SRC 8

int num_sources = 3;
int verbose = 1;
int terminate = 0;
int shared_graph = 0;
int autoconnect = 1;
int done = 0;
int period = 100;
int config, num_configs = 4;

mpr_dev *srcs = 0;
mpr_dev dst = 0;
mpr_sig *sendsigs = 0;
mpr_sig recvsig = 0;
mpr_map map = 0;

int sent = 0;
int received = 0;
int matched = 0;

float expected[3];

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
    uint8_t i;
    int min = 0, max = 1;
    char tmpname[16];

    if (g && iface) mpr_graph_set_interface(g, iface);
    srcs = (mpr_dev*)calloc(1, num_sources * sizeof(mpr_dev));
    sendsigs = (mpr_sig*)calloc(1, num_sources * sizeof(mpr_sig));

    for (i = 0; i < num_sources; i++) {
        srcs[i] = mpr_dev_new("testconvergent-send", g);
        if (!srcs[i])
            goto error;
        if (!g && iface)
            mpr_graph_set_interface(mpr_obj_get_graph(srcs[i]), iface);
        eprintf("sources[%d] created using interface %s.\n", i,
                mpr_graph_get_interface(mpr_obj_get_graph(srcs[i])));
        snprintf(tmpname, 16, "sendsig%d", i);
        sendsigs[i] = mpr_sig_new(srcs[i], MPR_DIR_OUT, tmpname, 1, MPR_INT32,
                                  NULL, &min, &max, NULL, NULL, 0);
        if (!sendsigs[i])
            goto error;
        eprintf("source %d created.\n", i);
    }
    return 0;

error:
    for (i = 0; i < num_sources; i++) {
        if (srcs[i])
            mpr_dev_free(srcs[i]);
    }
    return 1;
}

void cleanup_src(void)
{
    int i;
    for (i = 0; i < num_sources; i++) {
        if (srcs[i]) {
            eprintf("Freeing source %d... ", i);
            fflush(stdout);
            mpr_dev_free(srcs[i]);
            eprintf("ok\n");
        }
    }
    free(srcs);
    free(sendsigs);
}

void handler(mpr_sig sig, mpr_sig_evt evt, mpr_id instance, int length,
             mpr_type type, const void *value, mpr_time t)
{
    if (value) {
        float *fvalue = (float*)value;
        int i, ok = 1;
        assert(3 == length);
        for (i = 0; i < 3; i++) {
            if (fvalue[i] != expected[i])
                ok = 0;
        }
        eprintf("handler: Got [%f, %f, %f] ...%s\n", fvalue[0],
                fvalue[1], fvalue[2], ok ? "OK" : "Error");
        if (!ok)
            eprintf("  (expected [%f, %f, %f])\n", expected[0], expected[1], expected[2]);
        matched += ok;
        ++received;
    }
    else {
        eprintf("handler: Got NULL\n");
    }
}

int setup_dst(mpr_graph g, const char *iface)
{
    float mn = 0, mx = 1;
    mpr_list l;

    dst = mpr_dev_new("testconvergent-recv", g);
    if (!dst)
        goto error;
    if (iface)
        mpr_graph_set_interface(mpr_obj_get_graph(dst), iface);
    eprintf("destination created using interface %s.\n",
            mpr_graph_get_interface(mpr_obj_get_graph(dst)));

    recvsig = mpr_sig_new(dst, MPR_DIR_IN, "recvsig", 3, MPR_FLT, NULL,
                          NULL, NULL, NULL, handler, MPR_SIG_UPDATE);
    mpr_obj_set_prop((mpr_obj)recvsig, MPR_PROP_MIN, NULL, 1, MPR_FLT, &mn, 1);
    mpr_obj_set_prop((mpr_obj)recvsig, MPR_PROP_MAX, NULL, 1, MPR_FLT, &mx, 1);

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
    int i, j = 10;
    mpr_graph g2 = NULL;
    mpr_list sigs1, sigs2;

    if (map) {
        mpr_map_release(map);
        for (i = 0; i < num_sources; i++)
            mpr_dev_poll(srcs[i], 100);
        mpr_dev_poll(dst, 100);

        // poll some more here to ensure previous map was cleaned up
        for (i = 0; i < num_sources; i++)
            mpr_dev_poll(srcs[i], 100);
        mpr_dev_poll(dst, 100);
    }

    switch (config) {
        case 0: {
            int offset = 2, len = num_sources * 5 + 5;
            char *expr;

            eprintf("Configuration 0: combination function and muted sources\n");

            if (!(map = mpr_map_new(num_sources, sendsigs, 1, &recvsig))) {
                eprintf("Failed to create map\n");
                return 1;
            }

            /* build expression string with combination function and muted sources */

            expr = (char*)malloc(len * sizeof(char));
            snprintf(expr, 3, "y=");
            for (i = 0; i < num_sources; i++) {
                if (i == 0) {
                    /* set the first source to trigger evaluation */
                    snprintf(expr + offset, len - offset, "-x$%d",
                             mpr_map_get_sig_idx(map, sendsigs[i]));
                    offset += 4;
                }
                else {
                    /* mute the remaining sources so they don't trigger evaluation */
                    snprintf(expr + offset, len - offset, "-_x$%d",
                             mpr_map_get_sig_idx(map, sendsigs[i]));
                    offset += 5;
                }
            }
            mpr_obj_set_prop(map, MPR_PROP_EXPR, NULL, 1, MPR_STR, expr, 1);
            free(expr);
            mpr_obj_push(map);
            break;
        }
        case 1:
            eprintf("Configuration 1: combination function and buddy logic\n");

            if (!(map = mpr_map_new(num_sources, sendsigs, 1, &recvsig))) {
                eprintf("Failed to create map\n");
                return 1;
            }

            /* build expression string with combination function and buddy logic */
            mpr_obj_set_prop(map, MPR_PROP_EXPR, NULL, 1, MPR_STR,
                             "alive=(t_x$0>t_y{-1})&&(t_x$1>t_y{-1})&&(t_x$2>t_y{-1});"
                             "y=x$0+x$1+x$2;", 1);
            mpr_obj_push(map);
            break;
        case 2:
            eprintf("Configuration 2: format string and signal arguments\n");
            /* create/modify map with format string and signal arguments */
            if (!(map = mpr_map_new_from_str("%y=%x-_%x+_%x", recvsig, sendsigs[0],
                                             sendsigs[1], sendsigs[2]))) {
                eprintf("Failed to create map\n");
                return 1;
            }
            mpr_obj_push(map);
            break;
        case 3: {
            eprintf("Configuration 3: create the map using a 3rd party graph\n");
            /* create the map using a 3rd party graph */
            g2 = mpr_graph_new(MPR_OBJ);
            mpr_sig *sendsigs2 = calloc(1, sizeof(mpr_sig) * num_sources);
            mpr_sig recvsig2;

            eprintf("waiting for graph sync... ");
            while (!done) {
                int synced = 1;
                mpr_id sig_id;

                for (i = 0; i < num_sources; i++)
                    mpr_dev_poll(srcs[i], 100);
                mpr_dev_poll(dst, 100);
                mpr_graph_poll(g2, 100);

                /* Check if all signals are known to the graph */
                sig_id = mpr_obj_get_prop_as_int64(recvsig, MPR_PROP_ID, NULL);
                recvsig2 = mpr_graph_get_obj(g2, sig_id, MPR_SIG);
                if (!recvsig2) {
                    synced = 0;
                }
                else {
                    for (i = 0; i < num_sources; i++) {
                        sig_id = mpr_obj_get_prop_as_int64(sendsigs[i], MPR_PROP_ID, NULL);
                        sendsigs2[i] = mpr_graph_get_obj(g2, sig_id, MPR_SIG);
                        if (!sendsigs2[i]) {
                            synced = 0;
                            break;
                        }
                    }
                }
                if (synced)
                    break;
            }
            if (done)
                return 1;
            eprintf("synced!\n");
            map = mpr_map_new_from_str("%y=[%x,_%x+1,_%x+2]", recvsig2, sendsigs2[0],
                                       sendsigs2[1], sendsigs2[2]);
            free(sendsigs2);
            if (!map) {
                eprintf("Failed to create map\n");
                return 1;
            }
            mpr_obj_push(map);
            break;
        }
    }

    /* wait until mappings have been established */
    while (!done && (!mpr_map_get_is_ready(map) || j > 0)) {
        for (i = 0; i < num_sources; i++)
            mpr_dev_poll(srcs[i], 10);
        mpr_dev_poll(dst, 10);
        if (g2)
            mpr_graph_poll(g2, 10);
        --j;
    }

    /* Check if mpr_list_get_idx returns the correct items of ordered list of map signals */
    sigs1 = mpr_map_get_sigs(map, MPR_LOC_SRC);
    sigs2 = mpr_map_get_sigs(map, MPR_LOC_SRC);
    for (i = 0; i < num_sources; i++) {
        mpr_sig src1 = (mpr_sig)mpr_list_get_idx(sigs1, i);
        mpr_sig src2 = *(mpr_sig*)sigs2;
        eprintf("comparing src.%d '%s' : '%s'... %s\n", i,
                mpr_obj_get_prop_as_str(src1, MPR_PROP_NAME, NULL),
                mpr_obj_get_prop_as_str(src2, MPR_PROP_NAME, NULL),
                src1 == src2 ? "OK" : "ERROR");
        if (src1 != src2) {
            mpr_list_free(sigs1);
            mpr_list_free(sigs2);
            return 1;
        }
        sigs2 = mpr_list_get_next(sigs2);
    }
    /* Need to free list sigs1 since it was not iterated. */
    mpr_list_free(sigs1);

    if (g2) {
        mpr_graph_free(g2);
        map = NULL;
    }
    return 0;
}

int wait_ready(int *cancel)
{
    int i, ready = 0;
    while (!ready && !*cancel) {
        ready = 1;

        for (i = 0; i < num_sources; i++) {
            mpr_dev_poll(srcs[i], 50);
            if (!mpr_dev_get_is_ready(srcs[i])) {
                ready = 0;
            }
        }
        mpr_dev_poll(dst, 50);
        if (!mpr_dev_get_is_ready(dst))
            ready = 0;
    }
    return *cancel;
}

void loop(void)
{
    int i = 0, j;
    eprintf("Polling device..\n");

    while ((!terminate || i < 50) && !done) {
        switch (config) {
            case 0:
                expected[0] = expected[1] = expected[2] = i * -3.f;
                break;
            case 1:
                expected[0] = expected[1] = expected[2] = i * 3.f;
                break;
            case 2:
                expected[0] = expected[1] = expected[2] = i;
                break;
            case 3:
                expected[0] = i;
                expected[1] = i + 1;
                expected[2] = i + 2;
                break;
        }

        for (j = num_sources - 1; j >= 0; j--) {
            eprintf("Updating source %d = %i\n", j, i);
            mpr_sig_set_value(sendsigs[j], 0, 1, MPR_INT32, &i);
            mpr_dev_poll(srcs[j], 0);
        }

        mpr_dev_poll(dst, period);

        sent++;
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

void ctrlc(int sig)
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
                        printf("testconvergent.c: possible arguments "
                               "-q quiet (suppress output), "
                               "-t terminate automatically, "
                               "-f fast (execute quickly), "
                               "-s share (use one mpr_graph only), "
                               "-h help, "
                               "--iface network interface\n");
                        return 1;
                        break;
                    case 'f':
                        period = -1;
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
                        if (strcmp(argv[i], "--sources")==0 && argc>i+1) {
                            i++;
                            num_sources = atoi(argv[i]);
                            if (num_sources <= 0)
                                num_sources = 1;
                            else if (num_sources > MAX_NUM_MAP_SRC)
                                num_sources = MAX_NUM_MAP_SRC;
                            j = 1;
                        }
                        else if (strcmp(argv[i], "--iface")==0 && argc>i+1) {
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

    if (setup_srcs(g, iface)) {
        eprintf("Done initializing %d sources.\n", num_sources);
        result = 1;
        goto done;
    }

    if (wait_ready(&done)) {
        eprintf("Device registration aborted.\n");
        result = 1;
        goto done;
    }

    if (autoconnect) {
        for (i = num_configs - 1; i >= 0; i--) {
            config = i;
            if (setup_maps()) {
                eprintf("Error setting map (1).\n");
                result = 1;
                goto done;
            }
            loop();
        }
    }
    else
        loop();

    if (sent != received || sent != matched) {
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

