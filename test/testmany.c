
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

int num_sources = 5;
int num_dests = 5;
int max_num_signals = 4;

mapper_device *source_device_list = 0;
mapper_device *dest_device_list = 0;
int *num_signals = 0;

int sent = 0;
int received = 0;
int done = 0;

/*! Internal function to get the current time. */
static double get_current_time()
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (double) tv.tv_sec + tv.tv_usec / 1000000.0; 
}

int setup_sources() {
	char str[20];
	int number;

	for ( int i=0; i<num_sources; i++ ) {
		source_device_list[i] = mdev_new("source", 0, 0);
		number = num_signals[i+num_dests];

		for ( int j=0; j<number; j++ ) {
			sprintf( str, "/outsig%d", j );
            float mn=0, mx=1;
			mdev_add_output(source_device_list[i], str, 1, 'f',
                            0, &mn, &mx);
		}
		if ( !source_device_list[i] ) {
			goto error;
		}
	}
    return 0;

  error:
    return 1;

}

void cleanup_sources() {
	mapper_device source;

    printf("Freeing sources");
	for ( int i=0; i<num_sources; i++ ) {
		source = source_device_list[i];

		if (source) {
			mdev_free(source);
			printf(".");
		}
	}
    printf("\n");
}

void insig_handler(mapper_signal sig, mapper_db_signal props,
                   int instance_id, void *value, int count,
                   mapper_timetag_t timetag)
{
    if (value) {
        printf("handler: Got %f\n", (*(float*)value));
    }
    received++;
}

int setup_destinations() {
	char str[20];
	int number;
	float mn=0, mx=1;

	for ( int i=0; i<num_dests; i++ ) {
		dest_device_list[i] = mdev_new("dest", 0, 0);
		number = num_signals[i];

		for ( int j=0; j<number; j++ ) {
			sprintf( str, "/insig%d", j );
			mdev_add_input(dest_device_list[i], str, 1, 'f',
                           0, &mn, &mx, 0, 0);
		}
		if ( !dest_device_list[i] ) {
			goto error;
		}
	}
    return 0;

  error:
    return 1;
}

void cleanup_destinations() {
	mapper_device dest;

    printf("Freeing dests");
	for ( int i=0; i<num_dests; i++ ) {
		dest = dest_device_list[i];

		if ( dest ) {
			mdev_free( dest );
			printf(".");
		}
	}
    printf("\n");
}

void wait_local_devices(int *cancel) {
	int i, keep_waiting = 1;

	while ( keep_waiting && !*cancel ) {
		keep_waiting = 0;

		for ( i=0; i<num_sources; i++ ) {
			mdev_poll( source_device_list[i], 10 );
			if ( !mdev_ready(source_device_list[i]) ) {
				keep_waiting = 1;
			}
		}
		for ( i=0; i<num_dests; i++ ) {
			mdev_poll( dest_device_list[i], 10 );
			if ( !mdev_ready(dest_device_list[i]) ) {
				keep_waiting = 1;
			}
		}
	}
    printf("Registered devices:\n");
    for ( i=0; i<num_sources; i++)
        printf("  %s\n", mdev_name(source_device_list[i]));
    for ( i=0; i<num_dests; i++)
        printf("  %s\n", mdev_name(dest_device_list[i]));
}

void loop() {
    printf("-------------------- GO ! --------------------\n");
    int i = 0;

    while ( i >= 0 && !done ) {
		for ( int i=0; i<num_sources; i++ ) {
			mdev_poll( source_device_list[i], 0 );
		}
		for ( int i=0; i<num_dests; i++ ) {
			mdev_poll( dest_device_list[i], 0 );
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
    int result = 0;
    int do_loop = 1;

    if (argc > 1) {
        if (strcmp(argv[1], "-h")==0
            || strcmp(argv[1], "--help")==0)
        {
            printf("Usage: testmany [num_sources=5] [num_dests=5] "
                   "[max_num_signals=4] [loop=1]\n");
            exit(0);
        }
        else
            num_sources = atoi(argv[1]);
    }

    if (argc > 2)
        num_dests = atoi(argv[2]);

    if (argc > 3)
        max_num_signals = atoi(argv[3]);

    if (argc > 4)
        do_loop = atoi(argv[4]);

    source_device_list = (mapper_device*)malloc(
        sizeof(mapper_device)*num_sources);

    dest_device_list = (mapper_device*)malloc(
        sizeof(mapper_device)*num_dests);

    num_signals = (int*)malloc(sizeof(int)*(num_sources + num_dests));

    signal(SIGINT, ctrlc);
	srand( time(NULL) );

	for (int i=0; i<num_sources+num_dests; i++) {
		num_signals[i] = (rand() % max_num_signals) + 1;
	}

    if (setup_sources()) {
        printf("Error initializing sources.\n");
        result = 1;
        goto done;
    }
    if (setup_destinations()) {
        printf("Error initializing destinations.\n");
        result = 1;
        goto done;
    }

    wait_local_devices(&done);
    now = get_current_time() - now;
    printf("Allocated %d devices in %f seconds.\n", num_sources + num_dests, now);

    if (do_loop)
        loop();

  done:
    cleanup_destinations();
    cleanup_sources();

    free(source_device_list);
    free(dest_device_list);
    free(num_signals);

    return result;
}
