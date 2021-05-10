#include "../src/mapper_internal.h"
#include <mapper/mapper.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <math.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>

int verbose = 1;
int terminate = 0;
int autoconnect = 1;
int done = 0;
int period = 100;
int vec_len = 3;

mpr_dev src = 0;
mpr_dev dst = 0;
mpr_sig sendsig = 0;
mpr_sig recvsig = 0;

int sent = 0;
int received = 0;

float *sMin, *sMax, *dMin, *dMax, *expected;
float *M, *B;

static void eprintf(const char *format, ...)
{
    va_list args;
    if (!verbose)
        return;
    va_start(args, format);
    vprintf(format, args);
    va_end(args);
}

int setup_src(const char *iface)
{
    mpr_list l;

    src = mpr_dev_new("testvector-send", 0);
    if (!src)
        goto error;
    if (iface)
        mpr_graph_set_interface(mpr_obj_get_graph((mpr_obj)src), iface);
    eprintf("source created using interface %s.\n",
            mpr_graph_get_interface(mpr_obj_get_graph((mpr_obj)src)));

    sendsig = mpr_sig_new(src, MPR_DIR_OUT, "outsig", vec_len, MPR_FLT, NULL,
                          sMin, sMax, NULL, NULL, 0);

    eprintf("Output signal 'outsig' registered.\n");
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

void handler(mpr_sig sig, mpr_sig_evt event, mpr_id instance, int length,
             mpr_type type, const void *value, mpr_time t)
{
    int i;
    float *f;

    if (!value || length != vec_len)
        return;
    f = (float*)value;
    eprintf("handler: Got [");
    for (i = 0; i < length; i++) {
        eprintf("%f, ", f[i]);
        if (f[i] != expected[i])
            value = 0;
    }
    eprintf("\b\b]\n");
    if (value)
        received++;
    else {
        eprintf("expected [");
        for (i = 0; i < length; i++)
            eprintf("%f, ", expected[i]);
        eprintf("\b\b]\n");
    }
}

int setup_dst(const char *iface)
{
    mpr_list l;

    dst = mpr_dev_new("testvector-recv", 0);
    if (!dst)
        goto error;
    if (iface)
        mpr_graph_set_interface(mpr_obj_get_graph((mpr_obj)dst), iface);
    eprintf("destination created using interface %s.\n",
            mpr_graph_get_interface(mpr_obj_get_graph((mpr_obj)dst)));

    recvsig = mpr_sig_new(dst, MPR_DIR_IN, "insig", vec_len, MPR_FLT, NULL,
                          dMin, dMax, NULL, handler, MPR_SIG_UPDATE);

    eprintf("Input signal 'insig' registered.\n");
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

int setup_maps()
{
    int i = 0;

    mpr_map map = mpr_map_new(1, &sendsig, 1, &recvsig);
    mpr_obj_push((mpr_obj)map);

    /* wait until mapping has been established */
    while (!done && !mpr_map_get_is_ready(map)) {
        mpr_dev_poll(src, 10);
        mpr_dev_poll(dst, 10);
        if (i++ > 100)
            return 1;
    }

    /* calculate M and B for generated expected values */
    for (i = 0; i < vec_len; i++) {
        float sRange = (float)sMax[i] - (float)sMin[i];
        if (sRange) {
            M[i] = ((float)dMax[i] - (float)dMin[i]) / sRange;
            B[i] = (  (float)dMin[i] * (float)sMax[i]
                    - (float)dMax[i] * (float)sMin[i]) / sRange;
        }
        else {
            M[i] = 0;
            B[i] = 0;
        }
    }

    return 0;
}

void wait_ready()
{
    while (!done && !(mpr_dev_get_is_ready(src) && mpr_dev_get_is_ready(dst))) {
        mpr_dev_poll(src, 25);
        mpr_dev_poll(dst, 25);
    }
}

void loop()
{
    int i = 0, j = 0;
    float *v = malloc(vec_len * sizeof(float));

    eprintf("Polling device..\n");
    while ((!terminate || i < 50) && !done) {
        for (j = 0; j < vec_len; j++) {
            v[j] = (float)(i + j);
            expected[j] = v[j] * M[j] + B[j];
        }
        eprintf("Updating signal %s to [", sendsig->name);
        for (j = 0; j < vec_len; j++)
            eprintf("%f, ", v[j]);
        eprintf("\b\b]\n");
        mpr_sig_set_value(sendsig, 0, vec_len, MPR_FLT, v);
        sent++;
        mpr_dev_poll(src, 0);
        mpr_dev_poll(dst, period);
        i++;

        if (!verbose) {
            printf("\r  Sent: %4i, Received: %4i   ", sent, received);
            fflush(stdout);
        }
    }
    free(v);
}

void ctrlc(int sig)
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
                        printf("testvector.c: possible arguments "
                               "-f fast (execute quickly), "
                               "-q quiet (suppress output), "
                               "-t terminate automatically, "
                               "-h help, "
                               "--vec_len vector length (default 3), "
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
                        if (strcmp(argv[i], "--vec_len")==0 && argc>i+1) {
                            i++;
                            vec_len = atoi(argv[i]);
                            j = 1;
                        }
                        else if (strcmp(argv[i], "--iface")==0 && argc>i+1) {
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

    sMin = malloc(vec_len * sizeof(float));
    sMax = malloc(vec_len * sizeof(float));
    dMin = malloc(vec_len * sizeof(float));
    dMax = malloc(vec_len * sizeof(float));
    M = malloc(vec_len * sizeof(float));
    B = malloc(vec_len * sizeof(float));
    expected = malloc(vec_len * sizeof(float));

    for (i = 0; i < vec_len; i++) {
        sMin[i] = rand() % 100;
        do {
            sMax[i] = rand() % 100;
        } while (sMax[i] == sMin[i]);
        dMin[i] = rand() % 100;
        do {
            dMax[i] = rand() % 100;
        } while (dMax[i] == dMin[i]);
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

    wait_ready();

    if (autoconnect && setup_maps()) {
        eprintf("Error connecting signals.\n");
        result = 1;
        goto done;
    }

    loop();

    if (sent != received) {
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
    free(sMin);
    free(sMax);
    free(dMin);
    free(dMax);
    free(M);
    free(B);
    free(expected);
    return result;
}
