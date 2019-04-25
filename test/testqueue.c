#include <mpr/mpr.h>
#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>

#define eprintf(format, ...) do {               \
    if (verbose)                                \
        fprintf(stdout, format, ##__VA_ARGS__); \
} while(0)

int verbose = 1;
int terminate = 0;
int autoconnect = 1;
int done = 0;
int period = 100;

mpr_dev src = 0;
mpr_dev dst = 0;
mpr_sig sendsig = 0;
mpr_sig recvsig = 0;
mpr_sig sendsig1 = 0;
mpr_sig recvsig1 = 0;

int sent = 0;
int received = 0;

int setup_src()
{
    src = mpr_dev_new("testqueue-send", 0);
    if (!src)
        goto error;
    eprintf("source created.\n");

    float mn=0, mx=1;

    sendsig = mpr_sig_new(src, MPR_DIR_OUT, "outsig", 1, MPR_FLT, NULL,
                          &mn, &mx, NULL, NULL, 0);
    sendsig1= mpr_sig_new(src, MPR_DIR_OUT, "outsig1", 1, MPR_FLT, NULL,
                          &mn, &mx, NULL, NULL, 0);

    eprintf("Output signal 'outsig' registered.\n");
    eprintf("Number of outputs: %d\n",
            mpr_list_get_size(mpr_dev_get_sigs(src, MPR_DIR_OUT)));
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

void handler(mpr_sig sig, mpr_sig_evt event, mpr_id instance, int len,
             mpr_type type, const void *value, mpr_time t)
{
    if (value) {
        eprintf("handler: Got %f\n", (*(float*)value));
    }
    received++;
}

int setup_dst()
{
    dst = mpr_dev_new("testqueue-recv", 0);
    if (!dst)
        goto error;
    eprintf("destination created.\n");

    float mn=0, mx=1;

    recvsig = mpr_sig_new(dst, MPR_DIR_IN, "insig", 1, MPR_FLT, NULL,
                          &mn, &mx, NULL, handler, MPR_SIG_UPDATE);
	recvsig1= mpr_sig_new(dst, MPR_DIR_IN, "insig1", 1, MPR_FLT, NULL,
                          &mn, &mx, NULL, handler, MPR_SIG_UPDATE);

    eprintf("Input signal 'insig' registered.\n");
    eprintf("Number of inputs: %d\n",
            mpr_list_get_size(mpr_dev_get_sigs(dst, MPR_DIR_IN)));
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

int create_maps()
{
    mpr_map maps[2];
    maps[0] = mpr_map_new(1, &sendsig, 1, &recvsig);
    mpr_obj_push(maps[0]);
    maps[1] = mpr_map_new(1, &sendsig1, 1, &recvsig);
    mpr_obj_push(maps[1]);

    // wait until mapping has been established
    while (!done && !mpr_map_get_is_ready(maps[0]) && !mpr_map_get_is_ready(maps[1])) {
        mpr_dev_poll(src, 10);
        mpr_dev_poll(dst, 10);
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
    eprintf("Polling device..\n");
	int i = 0;
	float j=1;
    const char *name = mpr_obj_get_prop_as_str(sendsig, MPR_PROP_NAME, NULL);
	while ((!terminate || i < 50) && !done) {
        j=i;
        mpr_time now;
        mpr_time_set(&now, MPR_NOW);
        mpr_dev_start_queue(src, now);
		mpr_dev_poll(src, 0);
        eprintf("Updating signal %s to %f\n", name, j);
        mpr_sig_set_value(sendsig, 0, 1, MPR_FLT, &j, now);
		mpr_sig_set_value(sendsig1, 0, 1, MPR_FLT, &j, now);
		mpr_dev_send_queue(src, now);
		sent = sent+2;
        mpr_dev_poll(dst, period);
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

int main(int argc, char **argv)
{
    int i, j, result = 0;

    // process flags for -v verbose, -t terminate, -h help
    for (i = 1; i < argc; i++) {
        if (argv[i] && argv[i][0] == '-') {
            int len = strlen(argv[i]);
            for (j = 1; j < len; j++) {
                switch (argv[i][j]) {
                    case 'h':
                        eprintf("testqueue.c: possible arguments "
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
        eprintf("Done initializing source.\n");
        result = 1;
        goto done;
    }

    wait_ready();

    if (autoconnect && create_maps()) {
        eprintf("Error creating maps.\n");
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
    return result;
}
