#include <mpr/mpr.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include <math.h>
#include <lo/lo.h>
#include <unistd.h>
#include <signal.h>

#define eprintf(format, ...) do {               \
    if (verbose)                                \
        fprintf(stdout, format, ##__VA_ARGS__); \
} while(0)

int verbose = 1;
int terminate = 0;
int done = 0;

int num_devs = 5;
mpr_dev *devices = 0;
int sent = 0;
int received = 0;

/*! Internal function to get the current time. */
static double current_time()
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (double) tv.tv_sec + tv.tv_usec / 1000000.0;
}


void handler(mpr_sig sig, mpr_sig_evt event, mpr_id instance, int length,
             mpr_type type, const void *value, mpr_time t)
{
    if (value) {
        eprintf("handler: Got %f\n", (*(float*)value));
    }
    received++;
}

int setup_devs() {
	char str[20];
	float mn=0, mx=1;

	for (int i = 0; i < num_devs; i++) {
		devices[i] = mpr_dev_new("testmany", 0);
        if (!devices[i])
			goto error;

        // give each device 10 inputs and 10 outputs
		for (int j = 0; j < 10; j++) {
			sprintf(str, "in%d", j);
			mpr_sig_new(devices[i], MPR_DIR_IN, 1, str, 1, MPR_FLT, NULL,
                        &mn, &mx, NULL, 0);
            sprintf(str, "out%d", j);
            mpr_sig_new(devices[i], MPR_DIR_OUT, 1, str, 1, MPR_FLT, NULL,
                        &mn, &mx, NULL, 0);
		}
	}
    return 0;

  error:
    return 1;
}

void cleanup_devs() {
	mpr_dev dest;

    eprintf("Freeing devices");
	for (int i = 0; i < num_devs; i++) {
		dest = devices[i];

		if (dest) {
			mpr_dev_free(dest);
			eprintf(".");
		}
	}
    eprintf("\n");
}

void wait_local_devs(int *cancel) {
	int i, j = 0, k = 0, keep_waiting = 1;

	while ( keep_waiting && !*cancel ) {
		keep_waiting = 0;

		for (i = 0; i < num_devs; i++) {
			mpr_dev_poll(devices[i], 50);
			if (!mpr_dev_ready(devices[i])) {
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
    int highest = 0, len;
    mpr_type type;
    const void *val;
    for (i = 0; i < num_devs; i++) {
        mpr_obj_get_prop_by_idx((mpr_obj)devices[i], MPR_PROP_ORDINAL, NULL,
                                &len, &type, &val, 0);
        if (1 != len || MPR_INT32 != type) {
            eprintf("Error retrieving ordinal property.\n");
            return;
        }
        int ordinal = *(int*)val;
        if (ordinal > highest)
            highest = ordinal;
    }
    for (i = 1; i <= highest; i++) {
        int count = 0;
        const char *name = 0;
        for (j = 0; j < num_devs; j++) {
            mpr_obj_get_prop_by_idx((mpr_obj)devices[j], MPR_PROP_ORDINAL,
                                    NULL, &len, &type, &val, 0);
            if (1 != len || MPR_INT32 != type) {
                eprintf("Error retrieving ordinal property.\n");
                return;
            }
            int ordinal = *(int*)val;
            if (ordinal == i) {
                mpr_obj_get_prop_by_idx((mpr_obj)devices[j], MPR_PROP_NAME,
                                        NULL, NULL, NULL, (const void**)&name, 0);
                ++count;
            }
        }
        if (count && name) {
            eprintf("%s  %s\t\tx %i\n\x1B[0m",
                    count > 1 ? "\x1B[31m" : "\x1B[32m", name, count);
        }
    }
}

void loop() {
    eprintf("-------------------- GO ! --------------------\n");
    int i = 0;

    while (i >= 0 && !done) {
		for (int i = 0; i < num_devs; i++) {
			mpr_dev_poll(devices[i], 10);
		}
        i++;
    }
}

void ctrlc(int sig) {
    done = 1;
}

int main(int argc, char *argv[])
{
    double now = current_time();
    int i, j, result = 0;

    // process flags for -v verbose, -t terminate, -h help
    for (i = 1; i < argc; i++) {
        if (argv[i] && argv[i][0] == '-') {
            int len = strlen(argv[i]);
            for (j = 1; j < len; j++) {
                switch (argv[i][j]) {
                    case 'h':
                        printf("testlinear.c: possible arguments "
                               "-q quiet (suppress output), "
                               "-t terminate automatically, "
                               "-h help, "
                               "--devices number of devices\n");
                        return 1;
                        break;
                    case 'q':
                        verbose = 0;
                        break;
                    case 't':
                        terminate = 1;
                        break;
                    case '-':
                        if (strcmp(argv[i], "--devices")==0 && argc>i+1) {
                            i++;
                            num_devs = atoi(argv[i]);
                            j = 1;
                        }
                        break;
                    default:
                        break;
                }
            }
        }
    }

    devices = (mpr_dev*)malloc(sizeof(mpr_dev)*num_devs);

    signal(SIGINT, ctrlc);
	srand( time(NULL) );

    if (setup_devs()) {
        eprintf("Error initializing devices.\n");
        result = 1;
        goto done;
    }

    wait_local_devs(&done);
    now = current_time() - now;
    eprintf("Allocated %d devices in %f seconds.\n", num_devs, now);

    if (!terminate)
        loop();

  done:
    cleanup_devs();

    free(devices);
    printf("\r..................................................Test %s\x1B[0m.\n",
           result ? "\x1B[31mFAILED" : "\x1B[32mPASSED");
    return result;
}
