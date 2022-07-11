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
#include <math.h>

#include <mapper/mapper.h>

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

/*! Internal function to get the current time. */
static double current_time()
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (double) tv.tv_sec + tv.tv_usec / 1000000.0;
}

/*! A helper function to seed the random number generator. */
static void seed_srand()
{
    unsigned int s;
    double d;
    mpr_time t;

#ifndef WIN32
    FILE *f = fopen("/dev/urandom", "rb");
    if (f) {
        if (1 == fread(&s, 4, 1, f)) {
            srand(s);
            fclose(f);
            return;
        }
        fclose(f);
    }
#endif

    mpr_time_set(&t, MPR_NOW);
    d = mpr_time_as_dbl(t);
    s = (unsigned int)((d - (unsigned long)d) * 100000);
    srand(s);
}

void handler(mpr_sig sig, mpr_sig_evt event, mpr_id instance, int length,
             mpr_type type, const void *value, mpr_time t)
{
    if (value) {
        eprintf("handler: Got %f\n", (*(float*)value));
    }
    received++;
}

void generate_name(char *str, int len)
{
    int i, num = rand() % (len / 2);
    num = num ? num : 1;
    for (i = 0; i < num; i++) {
        str[i * 2] = "ABCDEF"[rand() % 3];
        str[i * 2 + 1] = (i + 1) < num ? '/' : 0;
    }
}

int setup_devs(const char *iface) {
	char str[20];
	float mn = 0, mx = 1;
    int i, j;

    seed_srand();

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

        /* give each device N inputs and N outputs */
		for (j = 0; j < 10; j++) {
            mn = fmod(rand() * 0.01, 21.f) - 10.f;
            mx = fmod(rand() * 0.01, 21.f) - 10.f;
            generate_name(str, 20);
			mpr_sig_new(devices[i], MPR_DIR_IN, str, 1, MPR_FLT, NULL,
                        &mn, &mx, NULL, NULL, 0);
            mn = fmod(rand() * 0.01, 21.f) - 10.f;
            mx = fmod(rand() * 0.01, 21.f) - 10.f;
            generate_name(str, 20);
            if (j%2==0)
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
	mpr_dev dest;
    int i;

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

void wait_local_devs(int *cancel) {
    int i, j = 0, k = 0, keep_waiting = 1, ordinal, highest = 0;

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
        ordinal = mpr_obj_get_prop_as_int32((mpr_obj)devices[i], MPR_PROP_ORDINAL,
                                             NULL);
        if (ordinal > highest)
            highest = ordinal;
    }
    for (i = 1; i <= highest; i++) {
        int count = 0;
        const char *name = 0;
        for (j = 0; j < num_devs; j++) {
            ordinal = mpr_obj_get_prop_as_int32((mpr_obj)devices[j], MPR_PROP_ORDINAL,
                                                 NULL);
            if (ordinal == i) {
                name = mpr_obj_get_prop_as_str((mpr_obj)devices[j], MPR_PROP_NAME,
                                               NULL);
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
    double now = current_time();
    int i, j, result = 0;
    char *iface = 0;

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
                    case '-':
                        if (strcmp(argv[i], "--devices")==0 && argc>i+1) {
                            i++;
                            num_devs = atoi(argv[i]);
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

    devices = (mpr_dev*)malloc(sizeof(mpr_dev)*num_devs);

    signal(SIGSEGV, segv);
    signal(SIGINT, ctrlc);
	srand( time(NULL) );

    if (setup_devs(iface)) {
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
    {
        mpr_graph g = mpr_obj_get_graph((mpr_obj)devices[0]);
        cleanup_devs();
        free(devices);
        if (shared_graph) mpr_graph_free(g);
    }
    printf("\r..................................................Test %s\x1B[0m.\n",
           result ? "\x1B[31mFAILED" : "\x1B[32mPASSED");
    return result;
}
