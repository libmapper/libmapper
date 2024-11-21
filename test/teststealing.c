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

mpr_dev src = 0;
mpr_dev dst = 0;
mpr_sig sendsig = 0;
mpr_sig recvsig = 0;

int sent = 0;
int received = 0;
int local_updates = 0;
int num_inst = 10;

mpr_time *timetags;

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

    src = mpr_dev_new("teststealing.send", g);
    if (!src)
        goto error;
    if (iface)
        mpr_graph_set_interface(mpr_obj_get_graph(src), iface);
    eprintf("source created using interface %s.\n",
            mpr_graph_get_interface(mpr_obj_get_graph(src)));

    sendsig = mpr_sig_new(src, MPR_DIR_OUT, "outsig", 1, MPR_INT32, NULL,
                          NULL, NULL, &num_inst, NULL, 0);

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
        eprintf("handler: %s.%d got %f\n",
                mpr_obj_get_prop_as_str((mpr_obj)sig, MPR_PROP_NAME, NULL),
                instance, (*(float*)value));
    }
}

int setup_dst(mpr_graph g, const char *iface)
{
    int ephemeral = 0;
    mpr_list l;

    dst = mpr_dev_new("teststealing.recv", g);
    if (!dst)
        goto error;
    if (iface)
        mpr_graph_set_interface(mpr_obj_get_graph(dst), iface);
    eprintf("destination created using interface %s.\n",
            mpr_graph_get_interface(mpr_obj_get_graph(dst)));

    recvsig = mpr_sig_new(dst, MPR_DIR_IN, "insig", 1, MPR_FLT, NULL, NULL, NULL,
                          &num_inst, handler, MPR_SIG_UPDATE);
    mpr_obj_set_prop((mpr_obj)recvsig, MPR_PROP_EPHEM, NULL, 1, MPR_INT32, &ephemeral, 1);

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
    mpr_map map = mpr_map_new(1, &sendsig, 1, &recvsig);
    mpr_obj_push(map);

    /* Wait until mapping has been established */
    while (!done && !mpr_map_get_is_ready(map)) {
        mpr_dev_poll(src, 10);
        mpr_dev_poll(dst, 10);
    }

    eprintf("map initialized\n");

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
    float f;
    const char *sendsig_name = mpr_obj_get_prop_as_str((mpr_obj)sendsig, MPR_PROP_NAME, NULL);
    const char *recvsig_name = mpr_obj_get_prop_as_str((mpr_obj)recvsig, MPR_PROP_NAME, NULL);
    while ((!terminate || i < 50) && !done) {
        switch (i % 10) {
            case 5:
            case 6:
            case 7:
            case 8:
                for (j = 0; j < num_inst / 2; j++) {
                    eprintf("Updating signal %s.%d to %d\n", sendsig_name, j, i);
                    mpr_sig_set_value(sendsig, j, 1, MPR_INT32, &i);
                    ++sent;
                }
                break;
            case 9:
                for (j = 0; j < num_inst / 2; j++) {
                    eprintf("Releasing signal %s.%d\n", sendsig_name, j);
                    mpr_sig_release_inst(sendsig, j);
                }
                break;
            default:
                break;
        }
        mpr_dev_poll(src, 0);
        mpr_dev_poll(dst, period);
        i++;

        f = i * 2;
        for (j = 0; j < num_inst; j++) {
            int status = mpr_sig_get_inst_status(recvsig, j);
            if (status & MPR_STATUS_UPDATE_REM) {
                const void *value = mpr_sig_get_value(recvsig, j, NULL);
                eprintf("Signal %s.%d updated remotely to %g\n", recvsig_name, j, *(float*)value);
                ++received;
            }
            else {
                mpr_sig_set_value(recvsig, j, 1, MPR_FLT, &f);
                eprintf("Signal %s.%d updated locally to %g\n", recvsig_name, j, f);
                ++local_updates;
            }

            timetags[j] = mpr_dev_get_time(dst);
        }

        if (!verbose) {
            printf("\r  Sent: %4i, Received: %4i, Local set: %4i  ",
                   sent, received, local_updates);
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
                        printf("teststealing.c: possible arguments "
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

    timetags = calloc(1, sizeof(mpr_time) * num_inst);

    /* test 1: using handlers for instance release etc */
    loop();

    /* test 2: no handler */
    mpr_sig_set_cb(recvsig, NULL, 0);
    loop();

    if (autoconnect && (sent != received || local_updates != sent * 4)) {
        eprintf("Updated value %d time%s, but received %d of them.\n",
                sent, sent == 1 ? "" : "s", received);
        result = 1;
    }

  done:
    cleanup_dst();
    cleanup_src();
    if (g)
        mpr_graph_free(g);
    if (timetags)
        free(timetags);
    printf("...Test %s\x1B[0m.\n",
           result ? "\x1B[31mFAILED" : "\x1B[32mPASSED");
    return result;
}
