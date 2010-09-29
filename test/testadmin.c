
#include "../src/mapper_internal.h"
#include <mapper/mapper.h>
#include <stdio.h>
#include <math.h>

#include <unistd.h>
#include <arpa/inet.h>

mapper_admin my_admin = NULL;

int test_admin()
{
    int error = 0, wait;

    my_admin = mapper_admin_new("tester", 0, 8000);
    if (!my_admin) {
        printf("Error creating admin structure.\n");
        return 1;
    }
    printf("Admin structure initialized.\n");

    printf("Found interface %s has IP %s\n", my_admin->interface,
           inet_ntoa(my_admin->interface_ip));

    while (!my_admin->port.locked || !my_admin->ordinal.locked) {
        usleep(10000);
        mapper_admin_poll(my_admin);
    }

    printf("Allocated port %d.\n", my_admin->port.value);
    printf("Allocated ordinal %d.\n", my_admin->ordinal.value);

    printf("Delaying for 5 seconds..\n");
    wait = 500;
    while (wait-- >= 0) {
        usleep(10000);
        mapper_admin_poll(my_admin);
    }

    mapper_admin_free(my_admin);
    printf("Admin structure freed.\n");

    return error;
}

int main()
{
    int result = test_admin();
    if (result) {
        printf("Test FAILED.\n");
        return 1;
    }

    printf("Test PASSED.\n");
    return 0;
}
