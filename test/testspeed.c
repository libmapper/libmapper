#include <mapper/mapper.h>
#include "../src/mpr_time.h"
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <math.h>
#include <time.h>

#include <lo/lo.h>
#ifdef WIN32
#include <io.h>
#else
#include <sys/time.h>
#include <unistd.h>
#endif
#include <signal.h>
#include <string.h>

#define NUM_INST 10

int count = 0;

int verbose = 1;
int shared_graph = 0;

mpr_dev src = 0;
mpr_dev dst = 0;
mpr_sig sendsig = 0;
mpr_sig recvsig = 0;

int numTrials = 10;
int trial = 0;
int numModes = 2;
int mode = 0;
int use_inst = 1;
int iterations = 10000;
int counter = 0;
int received = 0;
int matched = 0;
int done = 0;

double times[100];
float expected[NUM_INST];

void switch_modes(void);
void print_results(void);

static void eprintf(const char *format, ...)
{
    va_list args;
    if (!verbose) {
        if (count >= 20) {
            count = 0;
            fprintf(stdout, "\33[2K\r");
        }
        else {
            fprintf(stdout, ".");
            ++count;
        }
        fflush(stdout);
        return;
    }
    va_start(args, format);
    vprintf(format, args);
    va_end(args);
}

/*! Creation of a local source. */
int setup_src(mpr_graph g, const char *iface)
{
    int num_inst = NUM_INST;
    mpr_list l;
    src = mpr_dev_new("testspeed-send", g);
    if (!src)
        goto error;
    if (iface)
        mpr_graph_set_interface(mpr_obj_get_graph((mpr_obj)src), iface);
    eprintf("source created using interface %s.\n",
            mpr_graph_get_interface(mpr_obj_get_graph((mpr_obj)src)));

    sendsig = mpr_sig_new(src, MPR_DIR_OUT, "outsig", 1, MPR_FLT, NULL,
                          NULL, NULL, &num_inst, NULL, 0);
    if (!sendsig)
        goto error;

    eprintf("Output signal registered.\n");
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
        eprintf("Freeing source... ");
        fflush(stdout);
        mpr_dev_free(src);
        eprintf("ok\n");
    }
}

void handler(mpr_sig sig, mpr_sig_evt event, mpr_id inst, int length,
             mpr_type type, const void *value, mpr_time t)
{
    if (value) {
        float fvalue = *(float*)value;
        if (inst < 0 || inst > NUM_INST - 1)
            printf("error: inst out of bounds!\n");
        else if (fvalue != expected[inst])
            printf("error: value[%d] %g != %g\n", (int)inst, *(float*)value, expected[inst]);
        else
            ++matched;
        if (++received >= iterations)
            switch_modes();
        if (use_inst) {
            counter = (counter + 1) % 10;
            fvalue = ++expected[counter];
            mpr_sig_set_value(sendsig, counter, length, type, &fvalue);
        }
        else {
            fvalue = ++expected[0];
            mpr_sig_set_value(sendsig, 0, length, type, &fvalue);
        }
        mpr_dev_update_maps(mpr_sig_get_dev(sendsig));
    }
    else {
        const char *name = mpr_obj_get_prop_as_str((mpr_obj)sig, MPR_PROP_NAME, NULL);
        eprintf("--> destination %s instance %ld got NULL\n", name, (long)inst);
        mpr_sig_release_inst(sig, inst);
    }
}

/*! Creation of a local destination. */
int setup_dst(mpr_graph g, const char *iface)
{
    int num_inst = NUM_INST;
    mpr_list l;
    dst = mpr_dev_new("testspeed-recv", g);
    if (!dst)
        goto error;
    if (iface)
        mpr_graph_set_interface(mpr_obj_get_graph((mpr_obj)dst), iface);
    eprintf("destination created using interface %s.\n",
            mpr_graph_get_interface(mpr_obj_get_graph((mpr_obj)dst)));

    recvsig = mpr_sig_new(dst, MPR_DIR_IN, "insig", 1, MPR_FLT, NULL,
                          NULL, NULL, &num_inst, handler, MPR_SIG_UPDATE);
    if (!recvsig)
        goto error;

    eprintf("Input signal registered.\n");
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
        eprintf("Freeing destination... ");
        fflush(stdout);
        mpr_dev_free(dst);
        eprintf("ok\n");
    }
}

int wait_ready(void)
{
    while (!done && !(mpr_dev_get_is_ready(src) && mpr_dev_get_is_ready(dst))) {
        mpr_dev_poll(src, 25);
        mpr_dev_poll(dst, 25);
    }
    if (!done)
        eprintf("Devices are ready.\n");
    return done;
}

void map_sigs(void)
{
    mpr_map map;

    eprintf("Creating maps... ");
    map = mpr_map_new(1, &sendsig, 1, &recvsig);
    mpr_obj_push((mpr_obj)map);

    /* wait until mapping has been established */
    while (!done && !mpr_map_get_is_ready(map)) {
        mpr_dev_poll(src, 10);
        mpr_dev_poll(dst, 10);
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

void switch_modes(void)
{
    int i;
    eprintf("MODE %i TRIAL %i COMPLETED...\n", mode, trial);
    received = 0;
    times[mode * numTrials + trial] = mpr_get_current_time() - times[mode * numTrials + trial];
    if (++trial >= numTrials) {
        eprintf("SWITCHING MODES...\n");
        trial = 0;
        ++mode;
    }
    if (mode >= numModes) {
        done = 1;
        return;
    }

    switch (mode)
    {
        case 0:
            use_inst = 1;
            break;
        case 1:
            use_inst = 0;
            for (i = 1; i < 10; i++) {
                mpr_sig_release_inst(sendsig, i);
            }
            break;
    }

    times[mode * numTrials + trial] = mpr_get_current_time();
}

void print_results(void)
{
    int i, j, sent = iterations * numModes * numTrials;
    double total_elapsed_time = 0;

    for (i = 0; i < numModes; i++) {
        for (j = 0; j < numTrials; j++)
            total_elapsed_time += times[i * numTrials + j];
    }
    printf(" (%i messages in %f seconds).\n", sent, total_elapsed_time);
    if (!verbose)
        return;

    if (matched != sent) {
        eprintf("Mismatch between sent and matched messages.\n");
        eprintf("Updated value %d time%s, but matched %d of them.\n",
                sent, sent == 1 ? "" : "s", matched);
    }

    eprintf("\n*****************************************************\n");
    eprintf("\nRESULTS OF SPEED TEST:\n");
    for (i = 0; i < numModes; i++) {
        float bestTime = times[i * numTrials];
        eprintf("MODE %i\n", i);
        for (j = 0; j < numTrials; j++) {
            eprintf("trial %i: %i messages processed in %f seconds\n", j,
                    iterations, times[i * numTrials + j]);
            if (times[i * numTrials + j] < bestTime)
                bestTime = times[i * numTrials + j];
        }
        eprintf("\nbest trial: %i messages in %f seconds\n", iterations, bestTime);
    }
    eprintf("\n*****************************************************\n");
}

int main(int argc, char **argv)
{
    int i, j, result = 0;
    char *iface = 0;
    mpr_graph g;

    /* process flags for -v verbose, -h help */
    for (i = 1; i < argc; i++) {
        if (argv[i] && argv[i][0] == '-') {
            int len = strlen(argv[i]);
            for (j = 1; j < len; j++) {
                switch (argv[i][j]) {
                    case 'h':
                        printf("testspeed.c: possible arguments "
                               "-q quiet (suppress output), "
                               "-s shared (use one mpr_graph only), "
                               "-h help, "
                               "--iface network interface\n");
                        return 1;
                        break;
                    case 's':
                        shared_graph = 0;
                        break;
                    case 'q':
                        verbose = 0;
                        break;
                    case '-':
                        if (strcmp(argv[i], "--iface") == 0 && argc > i + 1) {
                            ++i;
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
        printf("Error initializing destination.\n");
        result = 1;
        goto done;
    }

    if (setup_src(g, iface)) {
        eprintf("Error initializing source.\n");
        result = 1;
        goto done;
    }

    if (wait_ready()) {
        eprintf("Device registration aborted.\n");
        result = 1;
        goto done;
    }

    map_sigs();

    expected[0] = 2;
    for (i = 0; i < NUM_INST; i++)
        expected[i] = (float)rand();

    /* start things off */
    eprintf("STARTING TEST...\n");
    times[0] = mpr_get_current_time();
    mpr_sig_set_value(sendsig, counter, 1, MPR_FLT, &(expected[counter]));

    while (!done) {
        if ((matched % 1000) == 0)
            mpr_dev_poll(src, -1);
        mpr_dev_poll(dst, -1);
    }

    if (matched != iterations * numModes * numTrials) {
        result = 1;
    }

    goto done;

  done:
    cleanup_dst();
    cleanup_src();
    if (g) mpr_graph_free(g);
    printf("\r..................................................Test %s\x1B[0m.",
           result ? "\x1B[31mFAILED" : "\x1B[32mPASSED");
    print_results();
    return result;
}
