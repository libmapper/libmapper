
#include "../src/mapper_internal.h"
#include <mapper/mapper.h>
#include <stdio.h>
#include <math.h>
#include <string.h>
#include <unistd.h>

#ifdef WIN32
#define usleep(x) Sleep(x/1000)
#endif

#define eprintf(format, ...) do {               \
    if (verbose)                                \
        fprintf(stdout, format, ##__VA_ARGS__); \
} while(0)

mapper_admin my_admin = NULL;
mapper_device my_device = NULL;

int verbose = 1;

int test_admin()
{
    int error = 0, wait;

    my_admin = mapper_admin_new(0, 0, 0);
    if (!my_admin) {
        eprintf("Error creating admin structure.\n");
        return 1;
    }

    eprintf("Admin structure initialized.\n");

    my_device = mdev_new("tester", 0, my_admin);
    if (!my_device) {
        eprintf("Error creating device structure.\n");
        return 1;
    }

    eprintf("Device structure initialized.\n");

    eprintf("Found interface %s has IP %s\n", my_admin->interface_name,
           inet_ntoa(my_admin->interface_ip));

    while (!my_device->registered) {
        usleep(10000);
        mapper_admin_poll(my_admin);
    }

    eprintf("Using port %d.\n", my_device->props.port);
    eprintf("Allocated ordinal %d.\n", my_device->ordinal.value);

    eprintf("Delaying for 5 seconds..\n");
    wait = 50;
    while (wait-- > 0) {
        usleep(50000);
        mapper_admin_poll(my_admin);
        if (!verbose) {
            printf(".");
            fflush(stdout);
        }
    }

    mdev_free(my_device);
    eprintf("Device structure freed.\n");
    mapper_admin_free(my_admin);
    eprintf("Admin structure freed.\n");

    return error;
}

int main(int argc, char **argv)
{
    int i, j, result = 0;

    // process flags for -v verbose, -h help
    for (i = 1; i < argc; i++) {
        if (argv[i] && argv[i][0] == '-') {
            int len = strlen(argv[i]);
            for (j = 1; j < len; j++) {
                switch (argv[i][j]) {
                    case 'h':
                        printf("testadmin.c: possible arguments "
                               "-q quiet (suppress output), "
                               "-h help\n");
                        return 1;
                        break;
                    case 'q':
                        verbose = 0;
                        break;
                    default:
                        break;
                }
            }
        }
    }

    result = test_admin();

    if (result) {
        printf("Test FAILED.\n");
        return 1;
    }

    printf("Test PASSED.\n");
    return 0;
}
