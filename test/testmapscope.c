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

void cleanup_devs()
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

int setup_maps()
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

void wait_ready(int iterations)
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
}

void loop()
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

int check_map_scopes(mpr_map map, int num_scopes, mpr_dev *scopes) {
    const void *val;
    mpr_type type;
    int len;
    mpr_list list;

    if (verbose) {
        mpr_list src_sigs = mpr_map_get_sigs(map, MPR_LOC_SRC);
        mpr_list dst_sigs = mpr_map_get_sigs(map, MPR_LOC_DST);
        mpr_dev src_dev = mpr_sig_get_dev((mpr_sig)*src_sigs);
        mpr_dev dst_dev = mpr_sig_get_dev((mpr_sig)*dst_sigs);
        printf("Checking map %s:%s -> %s:%s\n",
               mpr_obj_get_prop_as_str((mpr_obj)src_dev, MPR_PROP_NAME, NULL),
               mpr_obj_get_prop_as_str((mpr_obj)*src_sigs, MPR_PROP_NAME, NULL),
               mpr_obj_get_prop_as_str((mpr_obj)dst_dev, MPR_PROP_NAME, NULL),
               mpr_obj_get_prop_as_str((mpr_obj)*dst_sigs, MPR_PROP_NAME, NULL));
        mpr_list_free(src_sigs);
        mpr_list_free(dst_sigs);
    }

    if (!mpr_obj_get_prop_by_idx(map, MPR_PROP_SCOPE, NULL, &len, &type, &val, NULL)) {
        eprintf("Error: map scope property not found\n");
        return 1;
    }
    if (len != 1 || type != MPR_LIST) {
        eprintf("Error: map scope property has len %d and type %d\n", len, type);
        return 1;
    }

    list = (mpr_list)val;
    if (verbose) {
        printf("  scopes: [");
        mpr_list cpy = mpr_list_get_cpy(list);
        while (cpy) {
            printf("%s, ", mpr_obj_get_prop_as_str((mpr_obj)*cpy, MPR_PROP_NAME, NULL));
            cpy = mpr_list_get_next(cpy);
        }
        printf("\b\b]\n");
    }

    len = mpr_list_get_size(list);
    if (len != num_scopes) {
        eprintf("Error: map has %d scopes (should be %d)\n", len, num_scopes);
        return 1;
    }

    while (list) {
        mpr_dev scope = (mpr_dev)*list;
        int i, found = 0;
        for (i = 0; i < num_scopes; i++) {
            if (   mpr_obj_get_prop_as_int64((mpr_obj)scope, MPR_PROP_ID, NULL)
                == mpr_obj_get_prop_as_int64((mpr_obj)scopes[i], MPR_PROP_ID, NULL)) {
                found = 1;
                break;
            }
        }
        if (!found) {
            if (verbose) {
                printf("Error: couldn't find scope '%s' in [",
                       mpr_obj_get_prop_as_str(scope, MPR_PROP_NAME, NULL));
                for (i = 0; i < num_scopes; i++)
                    printf("%s, ", mpr_obj_get_prop_as_str((mpr_obj)scopes[i], MPR_PROP_NAME, NULL));
                printf("\b\b]\n");
            }
            mpr_list_free(list);
            return 1;
        }
        list = mpr_list_get_next(list);
        ++i;
    }
    return 0;
}

int check_scopes(int mode)
{
    if (verbose) {
        printf("checking ");
        mpr_obj_print(maps[0], 0);
    }
    switch (mode) {
        case 0:
            /* map[0] should have scope [devs[0]] */
            if (check_map_scopes(maps[0], 1, &devs[0]))
                return 1;
            break;
        case 1:
            /* map[0] should have scope [devs[0]] */
            if (check_map_scopes(maps[0], 1, &devs[0]))
                return 1;
            break;
        case 2:
            /* map[0] should have empty scope */
            if (check_map_scopes(maps[0], 0, NULL))
                return 1;
            break;
        default:
            return 1;
    }

    if (verbose) {
        printf("checking ");
        mpr_obj_print(maps[1], 0);
    }
    switch (mode) {
        case 0:
            /* map[1] should have scope [devs[1]] */
            if (check_map_scopes(maps[1], 1, &devs[1]))
                return 1;
            break;
        case 1:
            /* map[1] should have scope [devs[0], devs[1]] */
            if (check_map_scopes(maps[1], 2, devs))
                return 1;
            break;
        case 2:
            /* map[1] should have scope [devs[1]] */
            if (check_map_scopes(maps[1], 2, devs))
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

    wait_ready(0);

    if (autoconnect && setup_maps()) {
        eprintf("Error initializing maps.\n");
        result = 1;
        goto done;
    }

    if (check_scopes(0)) {
        eprintf("Error\n");
        result = 1;
        goto done;
    }

    loop();

    if (received[1] != sent[0]) {
        eprintf("Error: %s received %d messages but should have received %d\n",
                mpr_obj_get_prop_as_str((mpr_obj)devs[1], MPR_PROP_NAME, NULL), received[1], sent[0]);
        result = 1;
        goto done;
    }
    if (received[2]) {
        eprintf("Error: %s received %d messages but should have received 0\n",
                mpr_obj_get_prop_as_str((mpr_obj)devs[2], MPR_PROP_NAME, NULL), received[2]);
        result = 1;
        goto done;
    }

    eprintf("Adding scope\n");
    mpr_map_add_scope(maps[1], devs[0]);
    mpr_obj_push((mpr_obj)maps[1]);
    wait_ready(3);

    if (check_scopes(1)) {
        eprintf("Error\n");
        result = 1;
        goto done;
    }

    for (i = 0; i < 3; i++)
        sent[i] = received[i] = 0;

    loop();

    if (received[1] != sent[0]) {
        eprintf("Error: %s received %d messages but should have received %d\n",
                mpr_obj_get_prop_as_str((mpr_obj)devs[1], MPR_PROP_NAME, NULL), received[1], sent[0]);
        result = 1;
        goto done;
    }
    if (received[2] != sent[0]) {
        eprintf("Error: %s received %d messages but should have received %d\n",
                mpr_obj_get_prop_as_str((mpr_obj)devs[2], MPR_PROP_NAME, NULL), received[2], sent[0]);
        result = 1;
        goto done;
    }

    eprintf("Removing scope\n");
    mpr_map_remove_scope(maps[0], devs[0]);
    mpr_obj_push((mpr_obj)maps[0]);
    wait_ready(3);

    if (check_scopes(2)) {
        eprintf("Error\n");
        result = 1;
        goto done;
    }

    for (i = 0; i < 3; i++)
        sent[i] = received[i] = 0;

    loop();

    if (received[1]) {
        eprintf("Error: %s received %d messages but should have received 0\n",
                mpr_obj_get_prop_as_str((mpr_obj)devs[1], MPR_PROP_NAME, NULL), received[1]);
        result = 1;
        goto done;
    }
    if (received[2]) {
        eprintf("Error: %s received %d messages but should have received 0\n",
                mpr_obj_get_prop_as_str((mpr_obj)devs[2], MPR_PROP_NAME, NULL), received[2]);
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
