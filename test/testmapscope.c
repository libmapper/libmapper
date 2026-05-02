#include <mapper/mapper.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
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
int num_inst = 10;

mpr_dev devs[3] = {0, 0, 0};
mpr_sig sendsigs[3] = {0, 0, 0};
mpr_sig recvsigs[3] = {0, 0, 0};
mpr_map maps[2] = {0, 0};

int sent[3] = {0, 0, 0};
int received[3] = {0, 0, 0};

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
    int i;
    if (!value)
        return;

    for (i = 0; i < 3; i++) {
        if (sig == recvsigs[i])
            break;
    }
    if (i >= 3)
        eprintf("error: unknown signal in handler\n");
    else {
        eprintf("handler: Instance %d got %f\n", instance,  (*(float*)value));
        ++received[i];
        mpr_sig_set_value(sendsigs[i], instance, length, type, value);
        ++sent[i];
    }
}

int setup_devs(mpr_graph g, const char *iface)
{
    int i;
    float mn=0, mx=1;

    for (i = 0; i < 3; i++) {
        mpr_dev dev = devs[i] = mpr_dev_new("testmapscope", g);
        if (!dev)
            goto error;
        if (iface)
            mpr_graph_set_interface(mpr_obj_get_graph(dev), iface);
        eprintf("device created using interface %s.\n",
                mpr_graph_get_interface(mpr_obj_get_graph(dev)));

        sendsigs[i] = mpr_sig_new(dev, MPR_DIR_OUT, "outsig", 1, MPR_FLT, NULL,
                                  &mn, &mx, &num_inst, NULL, 0);
        recvsigs[i] = mpr_sig_new(dev, MPR_DIR_IN, "insig", 1, MPR_FLT, NULL,
                                  &mn, &mx, &num_inst, handler, MPR_SIG_UPDATE);
    }
    return 0;

  error:
    return 1;
}

void cleanup_devs(void)
{
    int i;
    for (i = 0; i < 3; i++) {
        if (devs[i]) {
            eprintf("Freeing device... ");
            fflush(stdout);
            mpr_dev_free(devs[i]);
            eprintf("ok\n");
        }
    }
}

int setup_maps(void)
{
    maps[0] = mpr_map_new(1, &sendsigs[0], 1, &recvsigs[1]);
    mpr_obj_push(maps[0]);

    maps[1] = mpr_map_new(1, &sendsigs[1], 1, &recvsigs[2]);
    mpr_obj_push(maps[1]);

    /* Wait until mappings have been established */
    while (!done && !(mpr_map_get_is_ready(maps[0]) && mpr_map_get_is_ready(maps[1]))) {
        mpr_dev_poll(devs[0], 10);
        mpr_dev_poll(devs[1], 10);
        mpr_dev_poll(devs[2], 10);
    }

    eprintf("maps initialized\n");
    return 0;
}

int wait_ready(int iterations)
{
    while (!done && !(   mpr_dev_get_is_ready(devs[0])
                      && mpr_dev_get_is_ready(devs[1])
                      && mpr_dev_get_is_ready(devs[2]))) {
        mpr_dev_poll(devs[0], 25);
        mpr_dev_poll(devs[1], 25);
        mpr_dev_poll(devs[2], 25);
    }
    while (--iterations >= 0) {
        mpr_dev_poll(devs[0], 25);
        mpr_dev_poll(devs[1], 25);
        mpr_dev_poll(devs[2], 25);
    }
    return done;
}

int loop(void)
{
    int i = 0;
    const char *devname = mpr_obj_get_prop_as_str((mpr_obj)devs[0], MPR_PROP_NAME, NULL);
    const char *signame = mpr_obj_get_prop_as_str((mpr_obj)sendsigs[0], MPR_PROP_NAME, NULL);
    while ((!terminate || i < 50) && !done) {
        eprintf("Updating signal %s:%s to %d\n", devname, signame, i);
        mpr_sig_set_value(sendsigs[0], i % 10, 1, MPR_INT32, &i);
        sent[0]++;
        mpr_dev_poll(devs[0], 0);
        mpr_dev_poll(devs[1], 0);
        mpr_dev_poll(devs[2], period);
        i++;

        if (!verbose) {
            printf("\r  Sent: [%2i, %2i, %2i], Received: [%2i, %2i, %2i]  ",
                   sent[0], sent[1], sent[2], received[0], received[1], received[2]);
            fflush(stdout);
        }
    }
    mpr_dev_poll(devs[0], period);
    mpr_dev_poll(devs[1], period);
    mpr_dev_poll(devs[2], period);

    return done;
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

int check_map_scopes(mpr_map map, int expect_len, mpr_type expect_type,
                     mpr_dev *expect_devs, const char *expect_str) {
    const void *val;
    mpr_type type;
    int len;
    mpr_list list;

    if (!mpr_obj_get_prop_by_idx(map, MPR_PROP_ALLOW_ORIGIN, NULL, &len, &type, &val, NULL)) {
        eprintf("Error: map ALLOW_ORIGIN property not found\n");
        return 1;
    }
    if (len != 1 || type != expect_type) {
        eprintf("Error: map ALLOW_ORIGIN property has len %d and type %c (expected %d and %c)\n",
                len, type, expect_len, expect_type);
        return 1;
    }

    if (MPR_LIST == type) {
        list = (mpr_list)val;
        if (verbose) {
            printf("  allowed origins: [");
            mpr_list cpy = mpr_list_get_cpy(list);
            if (cpy) {
                while (cpy) {
                    printf("%s, ", mpr_obj_get_prop_as_str((mpr_obj)*cpy, MPR_PROP_NAME, NULL));
                    cpy = mpr_list_get_next(cpy);
                }
                printf("\b\b]\n");
            }
            else
                printf("]\n");
        }

        len = mpr_list_get_size(list);
        if (len != expect_len) {
            eprintf("Error: map has %d allowed origins (should be %d)\n", len, expect_len);
            return 1;
        }

        while (list) {
            mpr_dev origin = (mpr_dev)*list;
            int i, found = 0;
            for (i = 0; i < len; i++) {
                if (   mpr_obj_get_prop_as_int64((mpr_obj)origin, MPR_PROP_ID, NULL)
                    == mpr_obj_get_prop_as_int64((mpr_obj)expect_devs[i], MPR_PROP_ID, NULL)) {
                    found = 1;
                    break;
                }
            }
            if (!found) {
                if (verbose) {
                    printf("Error: couldn't find origin device '%s' in [",
                           mpr_obj_get_prop_as_str(origin, MPR_PROP_NAME, NULL));
                    for (i = 0; i < len; i++)
                        printf("%s, ", mpr_obj_get_prop_as_str((mpr_obj)expect_devs[i],
                                                               MPR_PROP_NAME, NULL));
                    printf("\b\b]\n");
                }
                mpr_list_free(list);
                return 1;
            }
            list = mpr_list_get_next(list);
            ++i;
        }
    }
    else if (MPR_STR == type) {
        if (1 != len) {
            eprintf("Error: map has %d allowed origins (expected 1)\n", len);
            return 1;
        }
        if (strcmp((const char*)val, expect_str)) {
            eprintf("Error: map has allowed origin '%s' (expected '%s')\n", (const char*)val, expect_str);
            return 1;
        }
    }
    return 0;
}

int check_scopes(int mode)
{
    if (verbose) {
        printf("Checking ");
        mpr_obj_print(maps[0], 0);
    }
    switch (mode) {
        case 0:
            /* map[0] should have allowed origin 'all' */
            if (check_map_scopes(maps[0], 1, MPR_STR, devs, "all"))
                return 1;
            break;
        case 1:
            /* map[0] should have empty allowed origin list */
            if (check_map_scopes(maps[0], 0, MPR_LIST, NULL, NULL))
                return 1;
            break;
        case 2:
            /* map[0] should have allowed origins [devs[1], devs[2]] */
            if (check_map_scopes(maps[0], 2, MPR_LIST, &devs[1], NULL))
                return 1;
            break;
        default:
            return 1;
    }

    if (verbose) {
        printf("Checking ");
        mpr_obj_print(maps[1], 0);
    }
    switch (mode) {
        case 0:
            /* map[1] should have empty allowed origin list */
            if (check_map_scopes(maps[1], 0, MPR_LIST, NULL, NULL))
                return 1;
            break;
        case 1:
            /* map[1] should have allowed origins [devs[0], devs[1]] */
            if (check_map_scopes(maps[1], 2, MPR_LIST, devs, NULL))
                return 1;
            break;
        case 2:
            /* map[1] should have allowed origin [devs[0]] */
            if (check_map_scopes(maps[1], 1, MPR_LIST, devs, NULL))
                return 1;
            break;
        default:
            return 1;
    }
    return 0;
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
                        printf("testmapscope.c: possible arguments "
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

    if (setup_devs(g, iface)) {
        eprintf("Error initializing devices.\n");
        result = 1;
        goto done;
    }

    if (wait_ready(0)) {
        eprintf("Device registration aborted.\n");
        result = 1;
        goto done;
    }

    if (autoconnect && setup_maps()) {
        eprintf("Error initializing maps.\n");
        result = 1;
        goto done;
    }

    eprintf("Test 1:\n");
    eprintf("  Blocking all origins from map 1\n");
    mpr_map_block_instance_origin(maps[1], NULL);
    mpr_obj_push((mpr_obj)maps[1]);
    wait_ready(3);

    if (check_scopes(0)) {
        result = 1;
        goto done;
    }

    if (loop()) {
        eprintf("  Test aborted\n");
        result = 1;
        goto done;
    }

    if (received[1] != sent[0]) {
        eprintf("  Error: %s received %d messages but should have received %d\n",
                mpr_obj_get_prop_as_str((mpr_obj)devs[1], MPR_PROP_NAME, NULL), received[1], sent[0]);
        result = 1;
        goto done;
    }
    if (received[2]) {
        eprintf("  Error: %s received %d messages but should have received 0\n",
                mpr_obj_get_prop_as_str((mpr_obj)devs[2], MPR_PROP_NAME, NULL), received[2]);
        result = 1;
        goto done;
    }

    eprintf("Test 2:\n");
    eprintf("  Blocking all origins from map 0\n");
    mpr_map_block_instance_origin(maps[0], NULL);
    mpr_obj_push((mpr_obj)maps[0]);

    eprintf("  Allowing origins ['%s', '%s'] on map 1\n",
            mpr_obj_get_prop_as_str((mpr_obj)devs[0], MPR_PROP_NAME, NULL),
            mpr_obj_get_prop_as_str((mpr_obj)devs[1], MPR_PROP_NAME, NULL));
    mpr_map_allow_instance_origin(maps[1], devs[0]);
    mpr_map_allow_instance_origin(maps[1], devs[1]);
    mpr_obj_push((mpr_obj)maps[1]);
    wait_ready(3);

    if (check_scopes(1)) {
        result = 1;
        goto done;
    }

    for (i = 0; i < 3; i++)
        sent[i] = received[i] = 0;

    if (loop()) {
        eprintf("  Test aborted\n");
        result = 1;
        goto done;
    }

    if (received[1]) {
        eprintf("  Error: %s received %d messages but should have received 0\n",
                mpr_obj_get_prop_as_str((mpr_obj)devs[1], MPR_PROP_NAME, NULL), received[1]);
        result = 1;
        goto done;
    }
    if (received[2]) {
        eprintf("  Error: %s received %d messages but should have received 0\n",
                mpr_obj_get_prop_as_str((mpr_obj)devs[2], MPR_PROP_NAME, NULL), received[2]);
        result = 1;
        goto done;
    }

    eprintf("Test 3:\n");
    eprintf("  Allowing origins ['%s', '%s'] on map 0\n",
            mpr_obj_get_prop_as_str((mpr_obj)devs[1], MPR_PROP_NAME, NULL),
            mpr_obj_get_prop_as_str((mpr_obj)devs[2], MPR_PROP_NAME, NULL));
    mpr_map_allow_instance_origin(maps[0], devs[1]);
    mpr_map_allow_instance_origin(maps[0], devs[2]);
    mpr_obj_push((mpr_obj)maps[0]);

    eprintf("  Blocking origin '%s' on map 1\n",
            mpr_obj_get_prop_as_str((mpr_obj)devs[0], MPR_PROP_NAME, NULL));
    mpr_map_block_instance_origin(maps[1], devs[1]);
    mpr_obj_push((mpr_obj)maps[1]);

    wait_ready(3);

    if (check_scopes(2)) {
        result = 1;
        goto done;
    }

    for (i = 0; i < 3; i++)
        sent[i] = received[i] = 0;

    if (loop()) {
        eprintf("  Test aborted\n");
        result = 1;
        goto done;
    }

    if (received[1]) {
        eprintf("  Error: %s received %d messages but should have received %d\n",
                mpr_obj_get_prop_as_str((mpr_obj)devs[1], MPR_PROP_NAME, NULL), received[1], sent[0]);
        result = 1;
        goto done;
    }
    if (received[2]) {
        eprintf("  Error: %s received %d messages but should have received %d\n",
                mpr_obj_get_prop_as_str((mpr_obj)devs[2], MPR_PROP_NAME, NULL), received[2], sent[0]);
        result = 1;
        goto done;
    }

  done:
    cleanup_devs();
    if (g) mpr_graph_free(g);
    printf("....Test %s\x1B[0m.\n",
           result ? "\x1B[31mFAILED" : "\x1B[32mPASSED");
    return result;
}
