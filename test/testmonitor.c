#include "../src/operations.h"
#include "../src/expression.h"
#include "../src/mapper_internal.h"
#include <mapper/mapper.h>
#include <stdio.h>
#include <math.h>
#include <lo/lo.h>

#include <unistd.h>
#include <arpa/inet.h>
#include <signal.h>

mapper_device dummy = 0;

int port = 9000;
int done = 0;

/*! Creation of a local dummy device. */
int setup_dummy_device()
{
    dummy = mdev_new("dummy", port);
    if (!dummy)
        goto error;
    printf("Dummy device created.\n");

    return 0;

  error:
    return 1;
}

void cleanup_dummy_device()
{
    if (dummy) {
        if (dummy->routers) {
            printf("Removing router.. ");
            fflush(stdout);
            mdev_remove_router(dummy, dummy->routers);
            printf("ok\n");
        }
        printf("Freeing dummy.. ");
        fflush(stdout);
        mdev_free(dummy);
        printf("ok\n");
    }
}

void wait_local_devices()
{
    while (!(mdev_ready(dummy))) {
        mdev_poll(dummy, 0);
        usleep(500 * 1000);
    }

    mapper_db_dump();
}

void loop()
{
    while (!done)
    {
        // clear screen & cursor to home
        printf("\e[2J\e[0;0H");
        fflush(stdout);

        // TODO: replace with proper database queries
        mapper_db_dump();

        printf("------------------------------\n");

        mdev_poll(dummy, 0);
        usleep(500 * 1000);
    }
}

void on_device(mapper_db_device dev, mapper_db_action_t a, void *user)
{
    printf("Device %s ", dev->name);
    switch (a) {
    case MDB_NEW:
        printf("added.\n");
        break;
    case MDB_MODIFY:
        printf("modified.\n");
        break;
    case MDB_REMOVE:
        printf("removed.\n");
        break;
    }
    sleep(1);
}

void ctrlc(int sig)
{
    done = 1;
}

int main()
{
    int result = 0;

    signal(SIGINT, ctrlc);

    if (setup_dummy_device()) {
        printf("Done initializing dummy device.\n");
        result = 1;
        goto done;
    }

    mapper_db_add_device_callback(on_device, 0);

    wait_local_devices();

    loop();

  done:
    mapper_db_remove_device_callback(on_device, 0);
    cleanup_dummy_device();
    return result;
}
