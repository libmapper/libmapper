
#include "../src/mapper_internal.h"
#include <mapper/mapper.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include <math.h>
#include <lo/lo.h>
#include <unistd.h>
#include <signal.h>

#ifdef WIN32
#define usleep(x) Sleep(x/1000)
#endif

#define eprintf(format, ...) do {               \
    if (verbose)                                \
        fprintf(stdout, format, ##__VA_ARGS__); \
} while(0)

int verbose = 1;
int terminate = 0;
int done = 0;

int num_devices = 5;
mapper_device *device_list = 0;
int sent = 0;
int received = 0;

/*! Internal function to get the current time. */
static double get_current_time()
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (double) tv.tv_sec + tv.tv_usec / 1000000.0; 
}

void insig_handler(mapper_signal sig, mapper_db_signal props,
                   int instance_id, void *value, int count,
                   mapper_timetag_t timetag)
{
    if (value) {
        eprintf("handler: Got %f\n", (*(float*)value));
    }
    received++;
}

int setup_devices() {
	char str[20];
	float mn=0, mx=1;

	for (int i = 0; i < num_devices; i++) {
		device_list[i] = mdev_new("device", 0, 0);
        if (!device_list[i])
			goto error;

        // give each device 10 inputs and 10 outputs
		for (int j = 0; j < 10; j++) {
			sprintf(str, "/sig%d", j);
			mdev_add_input(device_list[i], str, 1, 'f', 0, &mn, &mx, 0, 0);
            mdev_add_output(device_list[i], str, 1, 'f', 0, &mn, &mx);
		}
	}
    return 0;

  error:
    return 1;
}

void cleanup_devices() {
	mapper_device dest;

    eprintf("Freeing devices");
	for (int i = 0; i < num_devices; i++) {
		dest = device_list[i];

		if (dest) {
			mdev_free(dest);
			eprintf(".");
		}
	}
    eprintf("\n");
}

void wait_local_devices(int *cancel) {
	int i, j = 0, keep_waiting = 1;

	while ( keep_waiting && !*cancel ) {
		keep_waiting = 0;

		for (i = 0; i < num_devices; i++) {
			mdev_poll(device_list[i], 50);
			if (!mdev_ready(device_list[i])) {
				keep_waiting = 1;
			}
            printf(".");
            fflush(stdout);
            if (j++ >= 50) {
                while (--j)
                    printf("\b \b");
            }
		}
	}
    eprintf("\nRegistered devices:\n");
    for ( i=0; i<num_devices; i++)
        eprintf("  %s\n", mdev_name(device_list[i]));
}

void loop() {
    eprintf("-------------------- GO ! --------------------\n");
    int i = 0;

    while (i >= 0 && !done) {
		for (int i = 0; i < num_devices; i++) {
			mdev_poll(device_list[i], 0);
		}

        usleep(50 * 1000);
        i++;
    }
}

void ctrlc(int sig) {
    done = 1;
}

int main(int argc, char *argv[])
{
    double now = get_current_time();
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
                            num_devices = atoi(argv[i]);
                            j = 1;
                        }
                        break;
                    default:
                        break;
                }
            }
        }
    }

    device_list = (mapper_device*)malloc(sizeof(mapper_device)*num_devices);

    signal(SIGINT, ctrlc);
	srand( time(NULL) );

    if (setup_devices()) {
        eprintf("Error initializing devices.\n");
        result = 1;
        goto done;
    }

    wait_local_devices(&done);
    now = get_current_time() - now;
    eprintf("Allocated %d devices in %f seconds.\n", num_devices, now);

    if (!terminate)
        loop();

  done:
    cleanup_devices();

    free(device_list);
    printf("Test %s.\n", result ? "FAILED" : "PASSED");
    return result;
}
