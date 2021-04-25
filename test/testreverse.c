#include <mapper/mapper.h>
#include <stdio.h>
#include <stdarg.h>
#include <math.h>
#include <lo/lo.h>
#include <unistd.h>
#include <signal.h>
#include <string.h>

int verbose = 1;
int terminate = 0;
int autoconnect = 1;
int done = 0;
int period = 100;

mpr_dev src = 0;
mpr_dev dst = 0;
mpr_sig sendsig;
mpr_sig recvsig;

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

void handler(mpr_sig sig, mpr_sig_evt event, mpr_id inst, int length,
             mpr_type type, const void *val, mpr_time t)
{
    const char *name = mpr_obj_get_prop_as_str(sig, MPR_PROP_NAME, NULL);
    int i;
    eprintf("--> %s got ", name);
    if (val) {
        if (type == MPR_FLT) {
            for (i = 0; i < length; i++)
                eprintf("%f ", ((float*)val)[i]);
            eprintf("\n");
        }
        else if (type == MPR_INT32) {
            for (i = 0; i < length; i++)
                eprintf("%i ", ((int*)val)[i]);
            eprintf("\n");
        }
    }
    else {
        for (i = 0; i < length; i++)
            eprintf("NIL ");
        eprintf("\n");
    }
    received++;
}

/*! Creation of a local source. */
int setup_src()
{
    float mn[] = {0.f, 0.f}, mx[] = {10.f, 10.f};
    mpr_list l;

    src = mpr_dev_new("testreverse-send", 0);
    if (!src)
        goto error;
    eprintf("source created.\n");

    sendsig = mpr_sig_new(src, MPR_DIR_OUT, "outsig", 2, MPR_FLT, NULL,
                          mn, mx, NULL, NULL, 0);
    mpr_sig_set_cb(sendsig, handler, MPR_SIG_UPDATE);

    eprintf("Output signals registered.\n");
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

/*! Creation of a local destination. */
int setup_dst()
{
    float mn = 0, mx = 1;
    mpr_list l;

    dst = mpr_dev_new("testreverse-recv", 0);
    if (!dst)
        goto error;
    eprintf("destination created.\n");

    recvsig = mpr_sig_new(dst, MPR_DIR_IN, "insig", 1, MPR_FLT, NULL,
                          &mn, &mx, NULL, handler, MPR_SIG_UPDATE);

    eprintf("Input signal insig registered.\n");
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

int setup_maps()
{
    int i = 0;

    mpr_map map = mpr_map_new(1, &recvsig, 1, &sendsig);
    mpr_obj_push(map);

    /* wait until mapping has been established */
    while (!done && !mpr_map_get_is_ready(map)) {
        mpr_dev_poll(src, 10);
        mpr_dev_poll(dst, 10);
        if (i++ > 100)
            return 1;
    }

    return 0;
}

void loop()
{
    int i = 0;
    float val[] = {0, 0};

    eprintf("-------------------- GO ! --------------------\n");

    mpr_sig_set_value(sendsig, 0, 2, MPR_FLT, val);

    while ((!terminate || i < 50) && !done) {
        val[0] = i % 10;
        mpr_sig_set_value(recvsig, 0, 1, MPR_FLT, val);
        sent++;
        eprintf("\ndestination value updated to %f -->\n", (i % 10) * 1.0f);
        mpr_dev_poll(dst, 0);
        mpr_dev_poll(src, period);
        i++;

        if (i == 25) {
            eprintf("setting sendsig direction to INPUT\n");
            int dir = MPR_DIR_IN;
            mpr_obj_set_prop(sendsig, MPR_PROP_DIR, NULL, 1, MPR_INT32, &dir, 1);
            mpr_obj_push(sendsig);
        }

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

int main(int argc, char **argv)
{
    int i, j, result = 0;

    /* process flags for -v verbose, -t terminate, -h help */
    for (i = 1; i < argc; i++) {
        if (argv[i] && argv[i][0] == '-') {
            int len = strlen(argv[i]);
            for (j = 1; j < len; j++) {
                switch (argv[i][j]) {
                    case 'h':
                        eprintf("testreverse.c: possible arguments"
                                "-f fast (execute quickly), "
                                "-q quiet (suppress output), "
                                "-t terminate automatically, "
                                "-h help\n");
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
                    default:
                        break;
                }
            }
        }
    }

    signal(SIGINT, ctrlc);

    if (setup_dst()) {
        eprintf("Error initializing destination.\n");
        result = 1;
        goto done;
    }

    if (setup_src()) {
        eprintf("Error initializing source.\n");
        result = 1;
        goto done;
    }

    wait_local_devs();

    if (autoconnect && setup_maps()) {
        eprintf("Error connecting signals.\n");
        result = 1;
        goto done;
    }

    loop();

    if ((sent - 25) != received) {
        eprintf("Not all sent messages were received.\n");
        eprintf("Updated value %d time%s, but received %d of them.\n",
                sent, sent == 1 ? "" : "s", received);
        result = 1;
    }

  done:
    cleanup_dst();
    cleanup_src();
    printf("...................Test %s\x1B[0m.\n",
           result ? "\x1B[31mFAILED" : "\x1B[32mPASSED");
    return result;
}
