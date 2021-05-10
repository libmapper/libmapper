#include <mapper/mapper.h>
#include <stdio.h>
#include <stdarg.h>
#include <math.h>
#include <lo/lo.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <string.h>

mpr_dev src = 0;
mpr_dev dst = 0;
mpr_sig sendsig_1 = 0;
mpr_sig recvsig_1 = 0;
mpr_sig sendsig_2 = 0;
mpr_sig recvsig_2 = 0;
mpr_sig sendsig_3 = 0;
mpr_sig recvsig_3 = 0;
mpr_sig sendsig_4 = 0;
mpr_sig recvsig_4 = 0;

int sent = 0;
int received = 0;
int done = 0;

int verbose = 1;
int terminate = 0;
int autoconnect = 1;

int period = 100;

static void eprintf(const char *format, ...)
{
    va_list args;
    if (!verbose)
        return;
    va_start(args, format);
    vprintf(format, args);
    va_end(args);
}

/*! Creation of a local source. */
int setup_src(const char *iface)
{
    float mnf[]={3.2,2,0}, mxf[]={-2,13,100};
    double mnd=0, mxd=10;
    mpr_list l;

    src = mpr_dev_new("test-send", 0);
    if (!src)
        goto error;
    if (iface)
        mpr_graph_set_interface(mpr_obj_get_graph(src), iface);
    eprintf("source created using interface %s.\n",
            mpr_graph_get_interface(mpr_obj_get_graph(src)));

    sendsig_1 = mpr_sig_new(src, MPR_DIR_OUT, "outsig_1", 1, MPR_DBL, "Hz",
                            &mnd, &mxd, NULL, NULL, 0);
    sendsig_2 = mpr_sig_new(src, MPR_DIR_OUT, "outsig_2", 1, MPR_FLT, "mm",
                            mnf, mxf, NULL, NULL, 0);
    sendsig_3 = mpr_sig_new(src, MPR_DIR_OUT, "outsig_3", 3, MPR_FLT, NULL,
                            mnf, mxf, NULL, NULL, 0);
    sendsig_4 = mpr_sig_new(src, MPR_DIR_OUT, "outsig_4", 1, MPR_FLT, NULL,
                            mnf, mxf, NULL, NULL, 0);

    eprintf("Output signal 'outsig' registered.\n");

    /* Make sure we can add and remove outputs without crashing. */
    mpr_sig_free(mpr_sig_new(src, MPR_DIR_OUT, "outsig_5", 1, MPR_FLT, NULL,
                             &mnf, &mxf, NULL, NULL, 0));

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

void handler(mpr_sig sig, mpr_sig_evt evt, mpr_id id, int len, mpr_type type,
             const void *val, mpr_time t)
{
    if (val) {
        int i;
        const char *name = mpr_obj_get_prop_as_str(sig, MPR_PROP_NAME, NULL);
        eprintf("--> destination got %s", name);

        switch (type) {
            case MPR_FLT: {
                float *v = (float*)val;
                for (i = 0; i < len; i++) {
                    eprintf(" %f", v[i]);
                }
                break;
            }
            case MPR_DBL: {
                double *v = (double*)val;
                for (i = 0; i < len; i++) {
                    eprintf(" %f", v[i]);
                }
                break;
            }
            default:
                break;
        }
        eprintf("\n");
    }
    received++;
}

/*! Creation of a local destination. */
int setup_dst(const char *iface)
{
    float mnf[]={0,0,0}, mxf[]={1,1,1};
    double mnd=0, mxd=1;
    mpr_list l;

    dst = mpr_dev_new("test-recv", 0);
    if (!dst)
        goto error;
    if (iface)
        mpr_graph_set_interface(mpr_obj_get_graph(dst), iface);
    eprintf("destination created using interface %s.\n",
            mpr_graph_get_interface(mpr_obj_get_graph(dst)));

    recvsig_1 = mpr_sig_new(dst, MPR_DIR_IN, "insig_1", 1, MPR_FLT, NULL,
                            mnf, mxf, NULL, handler, MPR_SIG_UPDATE);
    recvsig_2 = mpr_sig_new(dst, MPR_DIR_IN, "insig_2", 1, MPR_DBL, NULL,
                            &mnd, &mxd, NULL, handler, MPR_SIG_UPDATE);
    recvsig_3 = mpr_sig_new(dst, MPR_DIR_IN, "insig_3", 3, MPR_FLT, NULL,
                            mnf, mxf, NULL, handler, MPR_SIG_UPDATE);
    recvsig_4 = mpr_sig_new(dst, MPR_DIR_IN, "insig_4", 1, MPR_FLT, NULL,
                            mnf, mxf, NULL, handler, MPR_SIG_UPDATE);

    eprintf("Input signal 'insig' registered.\n");

    /* Make sure we can add and remove inputs and inputs within crashing. */
    mpr_sig_free(mpr_sig_new(dst, MPR_DIR_IN, "insig_5", 1, MPR_FLT,
                             NULL, &mnf, &mxf, NULL, NULL, MPR_SIG_UPDATE));

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



void wait_local_devs()
{
    while (!done && !(mpr_dev_get_is_ready(src) && mpr_dev_get_is_ready(dst))) {
        mpr_dev_poll(src, 25);
        mpr_dev_poll(dst, 25);
    }
}

void loop()
{
    int i = 0, recvd, num_maps;
    float val[3];
    eprintf("-------------------- GO ! --------------------\n");

    if (!done && autoconnect) {
        mpr_map maps[4];
        maps[0] = mpr_map_new(1, &sendsig_1, 1, &recvsig_1);
        maps[1] = mpr_map_new(1, &sendsig_2, 1, &recvsig_2);
        maps[2] = mpr_map_new(1, &sendsig_3, 1, &recvsig_3);
        maps[3] = mpr_map_new(1, &sendsig_3, 1, &recvsig_4);

        for (i = 0; i < 4; i++) {
            mpr_obj_push(maps[i]);
        }

        /* wait until all maps has been established */
        num_maps = 0;
        while (!done && num_maps < 4) {
            mpr_dev_poll(src, 10);
            mpr_dev_poll(dst, 10);
            num_maps = 0;
            for (i = 0; i < 4; i++) {
                num_maps += (mpr_map_get_is_ready(maps[i]));
            }
        }
    }

    i = 0;
    while ((!terminate || i < 50) && !done) {

        val[0] = val[1] = val[2] = (i % 10) * 1.0f;
        mpr_sig_set_value(sendsig_1, 0, 1, MPR_FLT, val);
        eprintf("outsig_1 value updated to %d -->\n", i % 10);

        mpr_sig_set_value(sendsig_2, 0, 1, MPR_FLT, val);
        eprintf("outsig_2 value updated to %d -->\n", i % 10);

        mpr_sig_set_value(sendsig_3, 0, 3, MPR_FLT, val);
        eprintf("outsig_3 value updated to [%f,%f,%f] -->\n",
               val[0], val[1], val[2]);

        mpr_sig_set_value(sendsig_4, 0, 1, MPR_FLT, val);
        eprintf("outsig_4 value updated to %d -->\n", i % 10);

        eprintf("Sent %i messages.\n", 4);
        sent += 4;
        mpr_dev_poll(src, 0);
        recvd = mpr_dev_poll(dst, period);
        eprintf("Received %i messages.\n\n", recvd);
        i++;

        if (!verbose) {
            printf("\r  Sent: %4i, Received: %4i   ", sent, received);
            fflush(stdout);
        }
    }
}

void ctrlc(int sig)
{
    done = 1;
}

int main(int argc, char ** argv)
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
                        printf("test.c: possible arguments "
                               "-q quiet (suppress output), "
                               "-t terminate automatically, "
                               "-f fast (execute quickly), "
                               "-h help, "
                               "--iface network interface\n");
                        return 1;
                        break;
                    case 'q':
                        verbose = 0;
                        break;
                    case 'f':
                        period = 1;
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

    if (setup_dst(iface)) {
        eprintf("Error initializing destination.\n");
        result = 1;
        goto done;
    }

    if (setup_src(iface)) {
        eprintf("Done initializing source.\n");
        result = 1;
        goto done;
    }

    wait_local_devs();

    loop();

    if (autoconnect && (!received || received != sent)) {
        eprintf("sent: %d, recvd: %d\n", sent, received);
        result = 1;
    }

  done:
    cleanup_dst();
    cleanup_src();
    printf("...................Test %s\x1B[0m.\n",
           result ? "\x1B[31mFAILED" : "\x1B[32mPASSED");
    return result;
}
