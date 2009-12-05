// -*- mode:c++; indent-tabs-mode:nil; c-basic-offset:4; compile-command:"scons -DQ debug=1" -*-

#include <mapper.h>
#include <stdio.h>

#include <unistd.h>
#include <arpa/inet.h>

mapper_admin my_admin = NULL;

void dump_input_methods(mapper_admin admin)
{
    printf("Input methods for %s:\n", admin->identifier);
    mapper_method node = admin->input_head;
    if (!node) {
        printf("(none)\n");
    }
    else
        while (node) {
            printf("%s (%s)\n", node->path, node->types);
            node = node->next;
        }
}

int test()
{
    int error=0, wait;

    if (!mapper_admin_init()) {
        printf("Error initializing mapper admin subsystem.\n");
        return 1;
    }
    printf("Mapper admin subsystem initialized.\n");

    my_admin = mapper_admin_new("tester", MAPPER_DEVICE_SYNTH, 8000);
    if (!my_admin)
    {
        printf("Error creating admin structure.\n");
        return 1;
    }
    printf("Admin structure initialized.\n");

    printf("Found interface %s has IP %s\n", my_admin->interface,
           inet_ntoa(my_admin->interface_ip));

    if (mapper_admin_input_add(my_admin, "/test/input","i"))
        printf("Added input address /test/input,i\n");
    else {
        printf("Failed to add input address /test/input,i\n");
        error = 1;
    }

    if (mapper_admin_input_add(my_admin, "/test/input","f"))
        printf("Added input address /test/input,f\n");
    else {
        printf("Failed to add input address /test/input,f\n");
        error = 1;
    }

    if (mapper_admin_input_add(my_admin, "/test/input","f")) {
        printf("(Incorrectly) added input address /test/input,f\n");
        error = 1;
    }
    else
        printf("(Correctly) failed to add input address /test/input,f\n");

    dump_input_methods(my_admin);

    if (mapper_admin_input_remove(my_admin, "/test/input","i"))
        printf("Removed input address /test/input,i\n");
    else {
        printf("Failed to remove input address /test/input,i\n");
        error = 1;
    }

    dump_input_methods(my_admin);

    while (    !my_admin->port.locked
            || !my_admin->ordinal.locked )
    {
        usleep(10000);
        mapper_admin_poll(my_admin);
    }

    printf("Allocated port %d.\n", my_admin->port.value);
    printf("Allocated ordinal %d.\n", my_admin->ordinal.value);

    printf("Delaying for 5 seconds..\n");
    wait = 500;
    while (wait-- >= 0)
    {
        usleep(10000);
        mapper_admin_poll(my_admin);
    }

    mapper_admin_free(my_admin);
    printf("Admin structure freed.\n");

    return error;
}

int main()
{
    int result = test();
    if (result) {
        printf("Test FAILED.\n");
        return 1;
    }
    else {
        printf("Test PASSED.\n");
        return 0;
    }
}
