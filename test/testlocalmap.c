#include <mapper/mapper.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <math.h>
#include <unistd.h>
#include <signal.h>
#include <string.h>

int verbose = 1;
int terminate = 0;
int autoconnect = 1;
int done = 0;
int period = 100;

mpr_dev dev = 0;
mpr_sig sendsig = 0;
mpr_sig recvsig = 0;
mpr_sig sig3 = 0;

int sent = 0;
int received = 0;

float M, B, expected;

static void eprintf(const char *format, ...)
{
    va_list args;
    if (!verbose)
        return;
    va_start(args, format);
    vprintf(format, args);
    va_end(args);
}

void handler(mpr_sig sig, mpr_sig_evt event, mpr_id instance, int length,
             mpr_type type, const void *value, mpr_time t)
{
    if (!value)
        return;
    eprintf("handler: signal %s got value %f, time %f\n",
            mpr_obj_get_prop_as_str(sig, MPR_PROP_NAME, 0),
            (*(float*)value), mpr_time_as_dbl(t));
    if (fabs(*(float*)value - expected) < 0.0001)
        received++;
    else
        eprintf(" expected %f\n", expected);
}

int setup(const char *iface)
{
    int mni=0, mxi=1;
    float mnf=0, mxf=1;
    mpr_list l;

    dev = mpr_dev_new("testlocalmap", 0);
    if (!dev)
        goto error;
    if (iface)
        mpr_graph_set_interface(mpr_obj_get_graph(dev), iface);
    eprintf("device created using interface %s.\n",
            mpr_graph_get_interface(mpr_obj_get_graph(dev)));

    sendsig = mpr_sig_new(dev, MPR_DIR_IN, "outsig", 1, MPR_INT32, NULL,
                          &mni, &mxi, NULL, NULL, 0);
    eprintf("Output signal 'outsig' registered.\n");
    l = mpr_dev_get_sigs(dev, MPR_DIR_OUT);
    eprintf("Number of outputs: %d\n", mpr_list_get_size(l));
    mpr_list_free(l);

    recvsig = mpr_sig_new(dev, MPR_DIR_IN, "insig", 1, MPR_FLT, NULL,
                          &mnf, &mxf, NULL, handler, MPR_SIG_UPDATE);
    eprintf("Input signal 'insig' registered.\n");
    l = mpr_dev_get_sigs(dev, MPR_DIR_IN);
    eprintf("Number of inputs: %d\n", mpr_list_get_size(l));
    mpr_list_free(l);

    return 0;

  error:
    return 1;
}

void cleanup()
{
    if (dev) {
        eprintf("Freeing device.. ");
        fflush(stdout);
        mpr_dev_free(dev);
        eprintf("ok\n");
    }
}

int setup_maps()
{
    mpr_map map = mpr_map_new(1, &sendsig, 1, &recvsig);
    float sMin, sMax, dMin, dMax;
    char expr[128];

    sMin = rand() % 100;
    do {
        sMax = rand() % 100;
    } while (sMax == sMin);
    dMin = rand() % 100;
    do {
        dMax = rand() % 100;
    } while (dMax == dMin);

    snprintf(expr, 128, "y=linear(x,%f,%f,%f,%f)", sMin, sMax, dMin, dMax);
    mpr_obj_set_prop(map, MPR_PROP_EXPR, NULL, 1, MPR_STR, expr, 1);
    mpr_obj_push(map);

    /* Wait until mapping has been established */
    while (!done && !mpr_map_get_is_ready(map)) {
        mpr_dev_poll(dev, 10);
    }

    eprintf("map initialized with expression '%s'\n",
            mpr_obj_get_prop_as_str(map, MPR_PROP_EXPR, NULL));

    /* calculate M and B for checking generated expression */
    M = (dMax - dMin) / (sMax - sMin);
    B = (dMin * sMax - dMax * sMin) / (sMax - sMin);

    return 0;
}

int setup_loop_test()
{
    mpr_map map1, map2;

    /* libmapper provides rudimentary loop detection so we will need a 3rd
       signal to create a loop. */
    sig3 = mpr_sig_new(dev, MPR_DIR_IN, "sig3", 1, MPR_FLT, NULL, NULL, NULL,
                       NULL, handler, MPR_SIG_UPDATE);
    eprintf("Input signal 'sig3' registered.\n");

    /* map from sendsig -> recvsig already exists */

    /* create map from recvsig -> sig3 */
    map1 = mpr_map_new(1, &recvsig, 1, &sig3);
    mpr_obj_push(map1);

    /* create map from sig3 -> sendsig */
    map2 = mpr_map_new(1, &sig3, 1, &sendsig);
    mpr_obj_push(map2);

    /* Wait until mapping has been established */
    while (!done && !mpr_map_get_is_ready(map1) && !mpr_map_get_is_ready(map2)) {
        mpr_dev_poll(dev, 10);
    }

    eprintf("Three-signal mapping loop established.\n");

    return 0;
}

void wait_ready()
{
    while (!done && !(mpr_dev_get_is_ready(dev))) {
        mpr_dev_poll(dev, 25);
    }
}

void loop()
{
    int i = 0;
    mpr_time t;
    const char *name;

    eprintf("Polling device..\n");

    name = mpr_obj_get_prop_as_str((mpr_obj)sendsig, MPR_PROP_NAME, NULL);
    while ((!terminate || i < 50) && !done) {
        t = mpr_dev_get_time(dev);
        eprintf("Updating signal %s to %d at time %f\n", name, i, mpr_time_as_dbl(t));
        expected = i * M + B;
        mpr_sig_set_value(sendsig, 0, 1, MPR_INT32, &i);
        sent++;
        mpr_dev_poll(dev, period);
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

    /* process flags for -v verbose, -t terminate, -h help */
    for (i = 1; i < argc; i++) {
        if (argv[i] && argv[i][0] == '-') {
            int len = strlen(argv[i]);
            for (j = 1; j < len; j++) {
                switch (argv[i][j]) {
                    case 'h':
                        printf("testlocalmap.c: possible arguments "
                               "-f fast (execute quickly), "
                               "-q quiet (suppress output), "
                               "-t terminate automatically, "
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
                    case '-':
                        if (strcmp(argv[i], "--iface")==0 && argc>i+1) {
                            i++;
                            iface = argv[i];
                            j = 1;
                        }
                        break;
                    default:
                        break;
                }
            }
        }
    }

    signal(SIGINT, ctrlc);

    if (setup(iface)) {
        eprintf("Error initializing device.\n");
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

    if (autoconnect && setup_loop_test()) {
        eprintf("Error initializing additional maps.\n");
        result = 1;
        goto done;
    }

    /* try to provoke an update loop */
    mpr_sig_set_value(sendsig, 0, 1, MPR_INT32, &i);

    if (autoconnect && (!received || sent != received)) {
        eprintf("Not all sent messages were received.\n");
        eprintf("Updated value %d time%s and received %d of them.\n",
                sent, sent == 1 ? "" : "s", received);
        result = 1;
    }

  done:
    cleanup();
    printf("...................Test %s\x1B[0m.\n",
           result ? "\x1B[31mFAILED" : "\x1B[32mPASSED");
    return result;
}
