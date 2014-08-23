
#include "../src/mapper_internal.h"
#include <mapper/mapper.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <lo/lo.h>

#include <unistd.h>
#include <signal.h>
#include <sys/time.h>

#ifdef WIN32
#define usleep(x) Sleep(x/1000)
#endif

mapper_device devices[5] = {0, 0, 0, 0, 0};
lo_timetag system_time;
mapper_timetag_t device_times[5];
uint32_t last_update;
int ready = 0;
int done = 0;

/*! Creation of devices. */
int setup_devices()
{
    int i;
    for (i=0; i<5; i++) {
        devices[i] = mdev_new("testsync", 0, 0);
        if (!devices[i])
            goto error;
    }
    return 0;

  error:
    return 1;
}

void cleanup_devices()
{
    int i;
    for (i=0; i<5; i++) {
        if (devices[i]) {
            printf("Freeing device %i... ", i);
            fflush(stdout);
            mdev_free(devices[i]);
            printf("ok\n");
        }
    }
}

void loop()
{
    int i = 0;
    printf("Loading devices...\n");

    while (i <= 100 && !done) {
        for (i=0; i<5; i++)
            mdev_poll(devices[i], 20);
        lo_timetag_now(&system_time);
        if (system_time.sec != last_update) {
            last_update = system_time.sec;
            if (ready) {
                for (i=0; i<5; i++) {
                    mdev_now(devices[i], &device_times[i]);
                }
                // calculate standard deviation
                double mean = 0;
                for (i=0; i<5; i++) {
                    mean += mapper_timetag_get_double(device_times[i]);
                }
                mean /= 5;
                double difference_aggregate = 0;
                for (i=0; i<5; i++) {
                    difference_aggregate += powf(mapper_timetag_get_double(device_times[i]) - mean, 2);
                }
                // print current system time and device diffs
                printf("%f", (double)system_time.sec +
                       (double)system_time.frac * 0.00000000023283064365);
                for (i=0; i<5; i++) {
                    printf("  |  %f", mapper_timetag_difference(system_time, device_times[i]));
                }
                printf("  |  %f", sqrtf(difference_aggregate / 5));
                printf("\n");
            }
            else {
                int count = 0;
                for (i=0; i<5; i++) {
                    count += mdev_ready(devices[i]);
                }
                if (count >= 5) {
                    printf("\nSYSTEM TIME *****  |  OFFSETS *****\n");
                    for (i=0; i<5; i++) {
                        // Give each device clock a random starting offset
//                        devices[i]->admin->clock.offset = (rand() % 100) - 50;
                    }
                    ready = 1;
                }
            }
        }
    }
}

void ctrlc(int sig)
{
    done = 1;
}

int main()
{
    int result = 0;
    printf("skipping test!\n");
    return 0;

    signal(SIGINT, ctrlc);

    if (setup_devices()) {
        printf("Error initializing devices.\n");
        result = 1;
        goto done;
    }

    loop();

  done:
    cleanup_devices();
    return result;
}
