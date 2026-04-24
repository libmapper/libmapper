#include <mapper/mapper.h>
#include <stdio.h>
#include <stdarg.h>
#include <math.h>
#include <string.h>
#ifdef WIN32
#include <io.h>
#else
#include <unistd.h>
#endif
#include <signal.h>
#include <stdlib.h>

int verbose = 1;
int terminate = 0;
int autoconnect = 1;
int shared_graph = 0;
int done = 0;
int period = 100;

mpr_dev src = 0;
mpr_dev dst = 0;
mpr_sig sendsig = 0;
mpr_sig recvsig = 0;
mpr_map src_map = 0;
mpr_map dst_map = 0;

int sent = 0;
int received = 0;
int matched = 0;
int addend[2] = {0, 0};

float expected_val[2];
double expected_time;

static void eprintf(const char *format, ...)
{
    va_list args;
    if (!verbose)
        return;
    va_start(args, format);
    vprintf(format, args);
    va_end(args);
}

/*! A helper function to seed the random number generator. */
static void seed_srand()
{
    unsigned int s;
    double d;
    mpr_time t;

#ifndef WIN32
    FILE *f = fopen("/dev/urandom", "rb");
    if (f) {
        if (1 == fread(&s, 4, 1, f)) {
            srand(s);
            fclose(f);
            return;
        }
        fclose(f);
    }
#endif

    mpr_time_set(&t, MPR_NOW);
    d = mpr_time_as_dbl(t);
    s = (unsigned int)((d - (unsigned long)d) * 100000);
    srand(s);
}

void randomize_addend()
{
    addend[0] = rand() * (rand() % 2 ? 1 : -1);
    addend[1] = rand() * (rand() % 2 ? 1 : -1);
    eprintf("set addend to [%d, %d]\n", addend[0], addend[1]);
}

int setup_src(mpr_graph g, const char *iface)
{
    float mn=0, mx=1;
    mpr_list l;

    src = mpr_dev_new("testexpression-send", g);
    if (!src)
        goto error;
    if (iface)
        mpr_graph_set_interface(mpr_obj_get_graph(src), iface);
    eprintf("source created using interface %s.\n",
            mpr_graph_get_interface(mpr_obj_get_graph(src)));

    sendsig = mpr_sig_new(src, MPR_DIR_OUT, "outsig", 1, MPR_FLT, NULL, &mn, &mx, NULL, NULL, 0);

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
    float *fvalue;
    if (!value || length != 2)
        return;
    fvalue = (float*)value;
    eprintf("handler: Got value [%f, %f], time %f\n", fvalue[0], fvalue[1], mpr_time_as_dbl(t));
    if (fvalue[0] != expected_val[0] || fvalue[1] != expected_val[1])
        eprintf("  error: expected value [%f, %f]\n", expected_val[0], expected_val[1]);
    else
        ++matched;
    ++received;
}

int setup_dst(mpr_graph g, const char *iface)
{
    float mn=0, mx=1;
    mpr_list l;

    dst = mpr_dev_new("testexpression-recv", g);
    if (!dst)
        goto error;
    if (iface)
        mpr_graph_set_interface(mpr_obj_get_graph(dst), iface);
    eprintf("destination created using interface %s.\n",
            mpr_graph_get_interface(mpr_obj_get_graph(dst)));

    recvsig = mpr_sig_new(dst, MPR_DIR_IN, "insig", 2, MPR_FLT, NULL,
                          NULL, NULL, NULL, handler, MPR_SIG_UPDATE);
    mpr_obj_set_prop((mpr_obj)recvsig, MPR_PROP_MIN, NULL, 1, MPR_FLT, &mn, 0);
    mpr_obj_set_prop((mpr_obj)recvsig, MPR_PROP_MAX, NULL, 1, MPR_FLT, &mx, 0);

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
    mpr_list list;
    char expr[128];

    mpr_map map = mpr_map_new(1, &sendsig, 1, &recvsig);

    snprintf(expr, 128, "foo=[%d,%d];y=x*10+foo", addend[0], addend[1]);
    mpr_obj_set_prop(map, MPR_PROP_EXPR, NULL, 1, MPR_STR, expr, 1);

    mpr_obj_push(map);

    /* wait until mapping has been established */
    while (!done && !mpr_map_get_is_ready(map)) {
        mpr_dev_poll(src, 10);
        mpr_dev_poll(dst, 10);
    }

    /* store 2 copies of the map, one from each endpoint */
    list = mpr_dev_get_maps(src, MPR_DIR_ANY);
    if (list) {
        src_map = *list;
        mpr_list_free(list);
    }
    else {
        eprintf("error retrieving map from source device\n");
        return 1;
    }
    list = mpr_dev_get_maps(dst, MPR_DIR_ANY);
    if (list) {
        dst_map = *list;
        mpr_list_free(list);
    }
    else {
        eprintf("error retrieving map from destination device\n");
        return 1;
    }

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
    mpr_time t;

    eprintf("Polling device..\n");
    while ((!terminate || i < 50) && !done) {
        float val = i * 1.0f;
        expected_val[0] = val * 10 + addend[0];
        expected_val[1] = val * 10 + addend[1];
        t = mpr_dev_get_time(src);
        expected_time = mpr_time_as_dbl(t);
        eprintf("Updating output signal to %f at time %f\n", (i * 1.0f),
                mpr_time_as_dbl(t));
        mpr_sig_set_value(sendsig, 0, 1, MPR_FLT, &val);
        sent++;
        mpr_dev_poll(src, 0);
        mpr_dev_poll(dst, period);
        i++;

        if (!verbose) {
            printf("\r  Sent: %4i, Received: %4i   ", sent, received);
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
    mpr_graph graph;

    /* process flags for -v verbose, -t terminate, -h help */
    for (i = 1; i < argc; i++) {
        if (argv[i] && argv[i][0] == '-') {
            int len = strlen(argv[i]);
            for (j = 1; j < len; j++) {
                switch (argv[i][j]) {
                    case 'h':
                        printf("testexpression.c: possible arguments "
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

    seed_srand();

    graph = shared_graph ? mpr_graph_new(0) : 0;

    if (setup_dst(graph, iface)) {
        eprintf("Error initializing destination.\n");
        result = 1;
        goto done;
    }

    if (setup_src(graph, iface)) {
        eprintf("Done initializing source.\n");
        result = 1;
        goto done;
    }

    if (wait_ready()) {
        eprintf("Device registration aborted.\n");
        result = 1;
        goto done;
    }

    randomize_addend();
    if (autoconnect && setup_maps()) {
        eprintf("Error setting map.\n");
        result = 1;
        goto done;
    }

    loop();

    /* By default, expression variable are private to the device processing the map.
     * We will modify the variable here and leave the 'publish' argument as False. */
    eprintf("Modifying expression variable at source device\n");
    {
        randomize_addend();
        mpr_obj_set_prop(src_map, MPR_PROP_EXTRA, "var@foo", 2, MPR_INT32, addend, 0);
        mpr_obj_push(src_map);

        /* wait for change to take effect */
        mpr_dev_poll(dst, 100);
        mpr_dev_poll(src, 100);

        loop();
    }

    /* Since the expression variable property is private, changing it remotely should not work.
     * Instead of randomizing the addend here we will use different values since we expect the
     * map to continue using the old addend values. */
    eprintf("Modifying private expression variable at remote device\n");
    if (!shared_graph) {
        int tmp[2] = { rand() * (rand() % 2 ? 1 : -1), rand() * (rand() % 2 ? 1 : -1) };
        mpr_obj_set_prop(dst_map, MPR_PROP_EXTRA, "var@foo", 2, MPR_INT32, tmp, 1);
        mpr_obj_push(dst_map);

        /* wait for change to take effect */
        mpr_dev_poll(dst, 100);
        mpr_dev_poll(src, 100);

        loop();
    }

    /* We can force the expression variable property to be public using the object property API. */
    eprintf("Modifying public expression variable at remote device\n");
    {
        mpr_obj_set_prop(src_map, MPR_PROP_EXTRA, "var@foo", 2, MPR_INT32, addend, 1);
        mpr_obj_push(src_map);

        /* wait for change to take effect */
        mpr_dev_poll(dst, 100);
        mpr_dev_poll(src, 100);

        /* Once the property is public we should be able to change the value remotely. */
        randomize_addend();
        mpr_obj_set_prop(dst_map, MPR_PROP_EXTRA, "var@foo", 2, MPR_INT32, addend, 1);
        mpr_obj_push(dst_map);

        /* wait for change to take effect */
        mpr_dev_poll(dst, 100);
        mpr_dev_poll(src, 100);

        loop();
    }

    /* Verify that editing the expression property overwrites our changes. */
    eprintf("Modifying expression to check that variable is overwritten\n");
    {
        addend[0] = addend[1] = 20;
        mpr_obj_set_prop(src_map, MPR_PROP_EXPR, NULL, 1, MPR_STR, "foo=[20,20];y=x*10+foo", 1);
        mpr_obj_push(src_map);

        /* wait for change to take effect */
        mpr_dev_poll(dst, 100);
        mpr_dev_poll(src, 100);

        loop();
    }

    eprintf("Modifying expression to check that variable is overwritten\n");
    {
        addend[0] = addend[1] = -50;
        mpr_obj_set_prop(dst_map, MPR_PROP_EXPR, NULL, 1, MPR_STR, "foo=[-50,-50];y=x*10+foo", 1);
        mpr_obj_push(dst_map);

        /* wait for change to take effect */
        mpr_dev_poll(dst, 100);
        mpr_dev_poll(src, 100);

        loop();
    }

    if (autoconnect && (!received || sent != matched)) {
        eprintf("Mismatch between sent and received/matched messages.\n");
        eprintf("Updated value %d time%s, but received %d and matched %d of them.\n",
                sent, sent == 1 ? "" : "s", received, matched);
        result = 1;
    }

  done:
    cleanup_dst();
    cleanup_src();
    if (graph)
        mpr_graph_free(graph);
    printf("...................Test %s\x1B[0m.\n",
           result ? "\x1B[31mFAILED" : "\x1B[32mPASSED");
    return result;
}
