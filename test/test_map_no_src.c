#include <mapper/mapper.h>
#include <stdio.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>

int verbose = 1;
int terminate = 0;
int shared_graph = 0;
int autoconnect = 1;
int done = 0;
int period = 100;
int vec_len = 3;

mpr_dev dev = NULL;
mpr_sig recvsig = NULL;

mpr_map map = NULL;

int expected = 0;
int received = 0;

#define NUM_TESTS 4

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
    if (value) {
        eprintf("handler: Got %f\n", *(float*)value);
        ++received;
    }
}

int setup_dev(const char *iface)
{
    mpr_list l;

    dev = mpr_dev_new("testvector-recv", NULL);
    if (!dev)
        goto error;
    if (iface)
        mpr_graph_set_interface(mpr_obj_get_graph((mpr_obj)dev), iface);
    eprintf("destination created using interface %s.\n",
            mpr_graph_get_interface(mpr_obj_get_graph((mpr_obj)dev)));

    recvsig = mpr_sig_new(dev, MPR_DIR_IN, "insig", vec_len, MPR_FLT, NULL,
                          NULL, NULL, NULL, handler, MPR_SIG_UPDATE);

    eprintf("Input signal 'insig' registered.\n");
    l = mpr_dev_get_sigs(dev, MPR_DIR_IN);
    eprintf("Number of inputs: %d\n", mpr_list_get_size(l));
    mpr_list_free(l);
    return 0;

  error:
    return 1;
}

void cleanup_dev(void)
{
    if (dev) {
        eprintf("Freeing destination.. ");
        fflush(stdout);
        mpr_dev_free(dev);
        eprintf("ok\n");
    }
}

int setup_maps(int config)
{
    int i;
    char expr[256];

    printf("Configuration %d: ", config);

    if (map) {
        /* Release the previous map configuration */
        mpr_map_release(map);
        map = NULL;
        for (i = 0; i < 10; i++)
            mpr_dev_poll(dev, 10);
    }

    /* reset the 'received' counter here rather than before loop() since configuration 4 is
     * expected to generate an update during setup */
    received = 0;

    switch (config) {
        case 1:
            printf("NULL source\n");
            expected = 50;
            map = mpr_map_new(0, NULL, 1, &recvsig);
            mpr_obj_set_prop((mpr_obj)map, MPR_PROP_NAME, NULL, 1, MPR_STR, "config1", 1);
            snprintf(expr, 256, "y=a++; next+=%g;", period * 0.001);
            mpr_obj_set_prop((mpr_obj)map, MPR_PROP_EXPR, NULL, 1, MPR_STR, expr, 1);
            mpr_obj_push((mpr_obj)map);
            break;
        case 2:
            printf("source == destination\n");
            expected = 50;
            map = mpr_map_new(1, &recvsig, 1, &recvsig);
            mpr_obj_set_prop((mpr_obj)map, MPR_PROP_NAME, NULL, 1, MPR_STR, "config2", 1);
            snprintf(expr, 256, "y=a++; next+=%g;", period * 0.001);
            mpr_obj_set_prop((mpr_obj)map, MPR_PROP_EXPR, NULL, 1, MPR_STR, expr, 1);
            mpr_obj_push((mpr_obj)map);
            break;
        case 3:
            printf("expression missing source reference\n");
            expected = 50;
            snprintf(expr, 256, "%%y=a++; next+=%g;", period * 0.001);
            map = mpr_map_new_from_str(expr, recvsig);
            mpr_obj_set_prop((mpr_obj)map, MPR_PROP_NAME, NULL, 1, MPR_STR, "config3", 1);
            mpr_obj_push((mpr_obj)map);
            break;
        case 4:
            printf("literal destination assignment\n");
            expected = 1;
            map = mpr_map_new_from_str("%y=10;", recvsig);
            mpr_obj_set_prop((mpr_obj)map, MPR_PROP_NAME, NULL, 1, MPR_STR, "config4", 1);
            mpr_obj_push((mpr_obj)map);
            break;
        default:
            printf("error: bad configuration index\n");
            return 1;
    }

    /* wait until mapping has been established */
    i = 0;
    while (!done && !mpr_map_get_is_ready(map)) {
        mpr_dev_poll(dev, 10);
        if (i++ > 100)
            return 1;
    }

    return 0;
}

int wait_ready(void)
{
    while (!done && !mpr_dev_get_is_ready(dev)) {
        mpr_dev_poll(dev, 25);
    }
    return done;
}

void loop(void)
{
    int i = 0;

    eprintf("Polling device..\n");
    while ((!terminate || received < 50) && i < 500 && !done) {
        mpr_dev_poll(dev, period);
        i++;

        if (!verbose) {
            printf("\r  Received: %4i/%i   ", received, expected);
            fflush(stdout);
        }
    }
    printf("\n");
}

void ctrlc(int sig)
{
    done = 1;
}

int main(int argc, char **argv)
{
    int i, j, result = 0, config_start = 1, config_stop = NUM_TESTS;
    char *iface = 0;

    /* process flags for -v verbose, -t terminate, -h help */
    for (i = 1; i < argc; i++) {
        if (argv[i] && argv[i][0] == '-') {
            int len = strlen(argv[i]);
            for (j = 1; j < len; j++) {
                switch (argv[i][j]) {
                    case 'h':
                        printf("testvector.c: possible arguments "
                               "-f fast (execute quickly), "
                               "-q quiet (suppress output), "
                               "-t terminate automatically, "
                               "-h help, "
                               "--vec_len vector length (default 3), "
                               "--iface network interface, "
                               "--config specify a configuration to run (1-%d)\n", NUM_TESTS);
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
                        if (strcmp(argv[i], "--vec_len")==0 && argc>i+1) {
                            i++;
                            vec_len = atoi(argv[i]);
                            j = 1;
                        }
                        else if (strcmp(argv[i], "--iface")==0 && argc>i+1) {
                            i++;
                            iface = argv[i];
                            j = len;
                        }
                        else if (strcmp(argv[i], "--config")==0 && argc>i+1) {
                            i++;
                            config_start = atoi(argv[i]);
                            if (config_start > 0 && config_start <= NUM_TESTS) {
                                config_stop = config_start + 1;
                            }
                            else {
                                printf("config start argument must be between 1 and %d\n", NUM_TESTS);
                                return 1;
                            }
                            if (i + 1 < argc) {
                                if (strcmp(argv[i + 1], "...")==0) {
                                    config_stop = NUM_TESTS;
                                    ++i;
                                }
                                else if (isdigit(argv[i + 1][0])) {
                                    config_stop = atoi(argv[i + 1]);
                                    if (config_stop <= config_start || config_stop > NUM_TESTS) {
                                        printf("config stop argument must be between "
                                               "config start and %d\n", NUM_TESTS);
                                        return 1;
                                    }
                                    ++i;
                                }
                            }
                        }
                        break;
                    default:
                        break;
                }
            }
        }
    }

    signal(SIGINT, ctrlc);

    if (setup_dev(iface)) {
        eprintf("Error initializing destination.\n");
        result = 1;
        goto done;
    }

    if (wait_ready()) {
        eprintf("Device registration aborted.\n");
        result = 1;
        goto done;
    }

    i = config_start;
    while (!done && i <= config_stop) {
        if (autoconnect && setup_maps(i)) {
            eprintf("Error connecting signals.\n");
            result = 1;
            goto done;
        }
        loop();
        ++i;
    }

    if (received != expected) {
        eprintf("Expected %d update%s, but received %d of them.\n",
                expected, expected == 1 ? "" : "s", received);
        result = 1;
    }

  done:
    cleanup_dev();
    printf("..................................................Test %s\x1B[0m.\n",
           result ? "\x1B[31mFAILED" : "\x1B[32mPASSED");
    return result;
}
