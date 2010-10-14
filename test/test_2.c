
#include "../src/mapper_internal.h"
#include <mapper/mapper.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>
#include <lo/lo.h>

#include <unistd.h>
#include <arpa/inet.h>
#include <signal.h>

#define num_sources 10
#define num_dests 10
#define max_num_signals 10

int automate = 0;

mapper_device source_device_list[num_sources];
mapper_device dest_device_list[num_dests];
int num_signals[num_sources + num_dests];

int recvport = 9000;
int sendport = 9000;

int sent = 0;
int received = 0;
int done = 0;


int setup_sources() {

	char str[20];
	mapper_signal signal_pointer;
	int number;

	for ( int i=0; i<num_sources; i++ ) {

		sprintf( str, "qtsource%d", i );
		
		source_device_list[i] = mdev_new(str, sendport, 0);
		number = num_signals[i+num_dests];

		for ( int j=0; j<number; j++ ) {

			sprintf( str, "/outsig%d", j );
			signal_pointer = msig_float(1, str, 0, 0, 1, 0, 0, 0);
			mdev_register_output(source_device_list[i], signal_pointer);

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

	for ( int i=0; i<num_sources; i++ ) {

		source = source_device_list[i];

		if (source) {
			if (source->routers) {
				printf("Removing router.. ");
				fflush(stdout);
				mdev_remove_router(source, source->routers);
				printf("ok\n");
			}
			printf("Freeing source.. ");
			fflush(stdout);
			mdev_free(source);
			printf("ok\n");
		}
	}

}

void insig_handler(mapper_signal sig, mapper_signal_value_t *v) {

    //printf("--> destination got %s %f\n\n", sig->props.name, (*v).f);
    received++;

}

int setup_destinations() {

	char str[20];
	mapper_signal signal_pointer;
	int number;

	for ( int i=0; i<num_dests; i++ ) {

		sprintf( str, "qtdest%d", i );
		
		dest_device_list[i] = mdev_new(str, recvport, 0);
		number = num_signals[i];

		for ( int j=0; j<number; j++ ) {

			sprintf( str, "/insig%d", j );
			signal_pointer = msig_float(1, str, 0, 0, 1, 0, 0, 0);
			mdev_register_input(dest_device_list[i], signal_pointer);

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

	for ( int i=0; i<num_dests; i++ ) {

		dest = dest_device_list[i];

		if ( dest ) {
			if ( dest->routers ) {
				printf("Removing router.. ");
				fflush(stdout);
				mdev_remove_router(dest, dest->routers);
				printf("ok\n");
			}
			printf("Freeing dests.. ");
			fflush(stdout);
			mdev_free( dest );
			printf("ok\n");
		}
	}

}

void wait_local_devices() {

	int keep_waiting = 1;

	while ( keep_waiting ) {

		keep_waiting = 0;

		for ( int i=0; i<num_sources; i++ ) {

			mdev_poll( source_device_list[i], 0 );
			if ( !mdev_ready(source_device_list[i]) ) {

				keep_waiting = 1;

			}

		}

		for ( int i=0; i<num_dests; i++ ) {

			mdev_poll( dest_device_list[i], 0 );
			if ( !mdev_ready(dest_device_list[i]) ) {

				keep_waiting = 1;

			}

		}

	}

    //mapper_db_dump();

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

        usleep(500 * 1000);

        i++;

    }

}

void ctrlc(int sig) {

    done = 1;

}

int main() {

    int result = 0;

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

    wait_local_devices();

    loop();

  done:
    cleanup_destinations();
    cleanup_sources();
    return result;

}
