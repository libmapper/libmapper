
#include <stdio.h>
#include <string.h>

#include <lo/lo_lowlevel.h>

#include "../src/types_internal.h"
#include "../src/mapper_internal.h"

int main()
{
    lo_arg *args[20];
    mapper_message_t msg;
    int port=1234;

    /* Test the database functions */

    args[0] = (lo_arg*)"@port";
    args[1] = (lo_arg*)&port;
    args[2] = (lo_arg*)"@IP";
    args[3] = (lo_arg*)"localhost";

    if (mapper_msg_parse_params(&msg, "/registered", "siss", 4, args))
    {
        printf("1: Error, parsing failed.\n");
        return 1;
    }

    mapper_db_add_or_update_device_params("/testdb.1", &msg);
    mapper_db_add_or_update_device_params("/testdb__.2", &msg);
    mapper_db_add_or_update_device_params("/testdb.3", &msg);
    mapper_db_add_or_update_device_params("/testdb__.4", &msg);

    /*********/

    trace("Dump:\n");
    mapper_db_dump();

    /*********/

    printf("\nWalk the whole database:\n");
    mapper_db_device *pdev = mapper_db_get_all_devices();
    int count=0;
    if (!pdev) {
        printf("mapper_db_get_all_devices() returned 0.\n");
        return 1;
    }
    if (!*pdev) {
        printf("mapper_db_get_all_devices() returned something "
               "which pointed to 0.\n");
        return 1;
    }

    while (pdev) {
        count ++;
        printf("  name=%s, host=%s, port=%d, canAlias=%d\n",
               (*pdev)->name, (*pdev)->host,
               (*pdev)->port, (*pdev)->canAlias);
        pdev = mapper_db_device_next(pdev);
    }

    if (count != 4) {
        printf("Expected 4 records, but counted %d.\n", count);
        return 1;
    }

    /*********/

    printf("\nFind /testdb.3:\n");

    mapper_db_device dev = mapper_db_get_device_by_name("/testdb.3");
    if (!dev) {
        printf("Not found.\n");
        return 1;
    }

    printf("  name=%s, host=%s, port=%d, canAlias=%d\n",
           dev->name, dev->host, dev->port, dev->canAlias);

    /*********/

    printf("\nFind /dummy:\n");

    dev = mapper_db_get_device_by_name("/dummy");
    if (dev) {
        printf("unexpected found /dummy: %p\n", dev);
        return 1;
    }
    printf("  not found, good.\n");

    /*********/

    printf("\nFind matching '__':\n");

    pdev = mapper_db_match_device_by_name("__");

    count=0;
    if (!pdev) {
        printf("mapper_db_get_all_devices() returned 0.\n");
        return 1;
    }
    if (!*pdev) {
        printf("mapper_db_get_all_devices() returned something "
               "which pointed to 0.\n");
        return 1;
    }

    while (pdev) {
        count ++;
        printf("  name=%s, host=%s, port=%d, canAlias=%d\n",
               (*pdev)->name, (*pdev)->host,
               (*pdev)->port, (*pdev)->canAlias);
        pdev = mapper_db_device_next(pdev);
    }

    if (count != 2) {
        printf("Expected 2 records, but counted %d.\n", count);
        return 1;
    }

    /*********/

    printf("\nTest PASSED.\n");
    return 0;
}
