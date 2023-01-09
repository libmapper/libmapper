#ifdef WIN32
#include "../src/types_internal.h"
#endif
#include <mapper/mapper.h>
#include "../src/mpr_time.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <time.h>

#include <math.h>
#include <lo/lo.h>
#ifdef WIN32
#include <io.h>
#else
#include <sys/time.h>
#include <unistd.h>
#endif
#include <signal.h>

int verbose = 1;
int terminate = 0;
int shared_graph = 0;
int done = 0;

int num_devs = 5;
mpr_dev *devices = 0;
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

void handler(mpr_sig sig, mpr_sig_evt event, mpr_id instance, int length,
             mpr_type type, const void *value, mpr_time t)
{
    if (value) {
        eprintf("handler: Got %f\n", (*(float*)value));
    }
    received++;
}

int setup_devs(const char *iface) {
	char str[20];
	float mn=0, mx=1;
    int i, j;

    mpr_graph g = shared_graph ? mpr_graph_new(0) : 0;
    if (g && iface) mpr_graph_set_interface(g, iface);
	for (i = 0; i < num_devs; i++) {
		devices[i] = mpr_dev_new("testmany", g);
        if (!devices[i])
			goto error;
        if (!g && iface)
            mpr_graph_set_interface(mpr_obj_get_graph((mpr_obj)devices[i]), iface);
        eprintf("device %d created using interface %s.\n", i,
                mpr_graph_get_interface(mpr_obj_get_graph((mpr_obj)devices[i])));

        /* give each device 10 inputs and 10 outputs */
		for (j = 0; j < 10; j++) {
            mn = fmod(rand() * 0.01, 21.f) - 10.f;
            mx = fmod(rand() * 0.01, 21.f) - 10.f;
			sprintf(str, "in%d", j);
			mpr_sig_new(devices[i], MPR_DIR_IN, str, 1, MPR_FLT, NULL, &mn, &mx, NULL, NULL, 0);
            mn = fmod(rand() * 0.01, 21.f) - 10.f;
            mx = fmod(rand() * 0.01, 21.f) - 10.f;
            sprintf(str, "out%d", j);
            if (j % 2 == 0)
                mpr_sig_new(devices[i], MPR_DIR_OUT, str, 1, MPR_FLT, NULL,
                            &mn, &mx, NULL, NULL, 0);
            else
                mpr_sig_new(devices[i], MPR_DIR_OUT, str, 1, MPR_FLT, NULL,
                            &mn, NULL, NULL, NULL, 0);
		}
	}
    return 0;

  error:
    return 1;
}

void cleanup_devs() {
    int i;
	mpr_dev dest;

    eprintf("Freeing devices");
	for (i = 0; i < num_devs; i++) {
		dest = devices[i];

		if (dest) {
			mpr_dev_free(dest);
			eprintf(".");
		}
	}
    eprintf("\n");
}

int wait_local_devs(int *cancel) {
    int i, j = 0, k = 0, keep_waiting = 1, highest = 0, result = 0;

	while ( keep_waiting && !*cancel ) {
		keep_waiting = 0;

		for (i = 0; i < num_devs; i++) {
			mpr_dev_poll(devices[i], 50);
			if (!mpr_dev_get_is_ready(devices[i])) {
				keep_waiting = 1;
			}
		}
        if (j++ >= 1000) {
            printf(".");
            fflush(stdout);
            j = 0;
            ++k;
            if (k >= 50) {
                printf("\33[2K\r");
                fflush(stdout);
                k = 0;
            }
        }
	}
    eprintf("\nRegistered devices:\n");
    for (i = 0; i < num_devs; i++) {
        int ordinal = mpr_obj_get_prop_as_int32((mpr_obj)devices[i], MPR_PROP_ORDINAL, NULL);
        if (ordinal > highest)
            highest = ordinal;
    }
    for (i = 1; i <= highest; i++) {
        int count = 0;
        const char *name = 0;
        for (j = 0; j < num_devs; j++) {
            int ordinal = mpr_obj_get_prop_as_int32((mpr_obj)devices[j], MPR_PROP_ORDINAL, NULL);
            if (ordinal == i) {
                name = mpr_obj_get_prop_as_str((mpr_obj)devices[j], MPR_PROP_NAME, NULL);
                ++count;
            }
        }
        if (count && name) {
            eprintf("%s  %s\t\tx %i\n\x1B[0m", count > 1 ? "\x1B[31m" : "\x1B[32m", name, count);
        }
        if (count > 1)
            result = 1;
    }
    return result;
}

void loop() {
    int i = 0, j;
    eprintf("-------------------- GO ! --------------------\n");

    while (i >= 0 && !done) {
		for (j = 0; j < num_devs; j++) {
			mpr_dev_poll(devices[j], 10);
		}
        i++;
    }
}

void ctrlc(int sig) {
    done = 1;
}

void segv(int sig) {
    printf("\x1B[31m(SEGV)\n\x1B[0m");
    exit(1);
}

int main(int argc, char *argv[])
{
    double now = mpr_get_current_time();
    int i, j, T = 1, result = 0;
    char *iface = 0;
    mpr_graph g;
    mpr_list l;

    /* process flags for -v verbose, -t terminate, -h help */
    for (i = 1; i < argc; i++) {
        if (argv[i] && argv[i][0] == '-') {
            int len = strlen(argv[i]);
            for (j = 1; j < len; j++) {
                switch (argv[i][j]) {
                    case 'h':
                        printf("testlinear.c: possible arguments "
                               "-q quiet (suppress output), "
                               "-t terminate automatically, "
                               "-s share (use one mpr_graph only), "
                               "-h help, "
                               "--devices number of devices, "
                               "--iface network interface\n");
                        return 1;
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
                        if (strcmp(argv[i], "--devices")==0 && argc>i+1) {
                            i++;
                            num_devs = atoi(argv[i]);
                            j = 1;
                        }
                        else if (strcmp(argv[i], "--iface")==0 && argc>i+1) {
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

    devices = (mpr_dev*)malloc(sizeof(mpr_dev)*num_devs);

    signal(SIGSEGV, segv);
    signal(SIGINT, ctrlc);
	srand( time(NULL) );

    if (setup_devs(iface)) {
        eprintf("Error initializing devices.\n");
        result = 1;
        goto done;
    }

    if (wait_local_devs(&done))
        result = 1;
    now = mpr_get_current_time() - now;
    eprintf("Allocated %d devices in %f seconds.\n", num_devs, now);
    if (result)
        goto done;

    /* Check graph local devices */
    g = mpr_obj_get_graph((mpr_obj)devices[0]);
    l = mpr_graph_get_list(g, MPR_DEV);
    l = mpr_list_filter(l, MPR_PROP_IS_LOCAL, NULL, 1, MPR_BOOL, &T, MPR_OP_EQ);
    i = mpr_list_get_size(l);
    eprintf("Checking local device count for graph %p... %d\n", g, i);
    mpr_list_free(l);
    if ((shared_graph && i != num_devs) || (!shared_graph && i != 1)) {
        printf("ERROR! Should be %d\n", shared_graph ? num_devs : 1);
        result = 1;
        goto done;
    }

    if (!terminate)
        loop();

  done:
    {
        mpr_graph g = mpr_obj_get_graph(devices[0]);
        cleanup_devs();
        free(devices);
        if (shared_graph) mpr_graph_free(g);
    }
    printf("\r..................................................Test %s\x1B[0m.\n",
           result ? "\x1B[31mFAILED" : "\x1B[32mPASSED");
    return result;
}
