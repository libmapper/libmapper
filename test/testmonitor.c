
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
int update = 0;

const int polltime_ms = 100;

void dbpause()
{
    // Don't pause normally, but this is left here to be easily
    // enabled for debugging purposes.

    // sleep(1);
}

void printsignal(mapper_db_signal sig, int is_output)
{
    printf("  %s name=%s%s, type=%c, length=%d",
           is_output ? "output" : "input",
           sig->device_name, sig->name, sig->type, sig->length);
    if (sig->unit)
        printf(", unit=%s", sig->unit);
    if (sig->minimum) {
        if (sig->type == 'i')
            printf(", minimum=%d", sig->minimum->i32);
        else if (sig->type == 'f')
            printf(", minimum=%g", sig->minimum->f);
    }
    if (sig->maximum) {
        if (sig->type == 'i')
            printf(", maximum=%d", sig->maximum->i32);
        else if (sig->type == 'f')
            printf(", maximum=%g", sig->maximum->f);
    }
    printf("\n");
}

/*! Creation of a local dummy device. */
int setup_dummy_device()
{
    dummy = mdev_new("dummy", port, 0);
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
        usleep(polltime_ms * 1000);
    }

    mapper_db_dump();
}

void loop()
{
    while (!done)
    {
        mdev_poll(dummy, 0);
        usleep(polltime_ms * 1000);

        if (!update)
            continue;
        update = 0;

        // clear screen & cursor to home
        printf("\e[2J\e[0;0H");
        fflush(stdout);

        printf("Registered devices:\n");
        mapper_db_device *pdev = mapper_db_get_all_devices();
        while (pdev) {
            printf("  name=%s, host=%s, port=%d, canAlias=%d\n",
                   (*pdev)->name, (*pdev)->host,
                   (*pdev)->port, (*pdev)->canAlias);
            pdev = mapper_db_device_next(pdev);
        }

        printf("------------------------------\n");

        printf("Registered signals:\n");
        mapper_db_signal *psig =
            mapper_db_get_all_inputs();
        while (psig) {
            printsignal(*psig, 0);
            psig = mapper_db_signal_next(psig);
        }
        psig = mapper_db_get_all_outputs();
        while (psig) {
            printsignal(*psig, 1);
            psig = mapper_db_signal_next(psig);
        }

        printf("------------------------------\n");

        printf("Registered mappings:\n");
        mapper_db_mapping *pmap = mapper_db_get_all_mappings();
        while (pmap) {
            printf("  %s -> %s\n",
                   (*pmap)->src_name, (*pmap)->dest_name);
            pmap = mapper_db_mapping_next(pmap);
        }

        printf("------------------------------\n");

        printf("Registered links:\n");
        mapper_db_link *plink = mapper_db_get_all_links();
        while (plink) {
            printf("  %s -> %s\n",
                   (*plink)->src_name, (*plink)->dest_name);
            plink = mapper_db_link_next(plink);
        }

        printf("------------------------------\n");
    }
}

void on_device(mapper_db_device dev, mapper_db_action_t a, void *user)
{
    printf("Device %s ", dev->name);
    switch (a) {
    case MDB_NEW:
        printf("added.\n");

        // Request signals for new devices.
        // TODO: API function for this?
        char cmd[1024];
        snprintf(cmd, 1024, "%s/signals/get", dev->name);
        mapper_admin_send_osc(dummy->admin, cmd, "");

        // Request links for new devices.
        // TODO: API function for this?
        snprintf(cmd, 1024, "%s/links/get", dev->name);
        mapper_admin_send_osc(dummy->admin, cmd, "");

        // Request mappings for new devices.
        // TODO: API function for this?
        snprintf(cmd, 1024, "%s/connections/get", dev->name);
        mapper_admin_send_osc(dummy->admin, cmd, "");

        break;
    case MDB_MODIFY:
        printf("modified.\n");
        break;
    case MDB_REMOVE:
        printf("removed.\n");
        break;
    }
    dbpause();
    update = 1;
}

void on_signal(mapper_db_signal sig, mapper_db_action_t a, void *user)
{
    printf("Signal %s%s ", sig->device_name, sig->name);
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
    dbpause();
    update = 1;
}

void on_mapping(mapper_db_mapping map, mapper_db_action_t a, void *user)
{
    printf("Mapping %s -> %s ", map->src_name, map->dest_name);
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
    dbpause();
    update = 1;
}

void on_link(mapper_db_link lnk, mapper_db_action_t a, void *user)
{
    printf("Link %s -> %s ", lnk->src_name, lnk->dest_name);
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
    dbpause();
    update = 1;
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
    mapper_db_add_signal_callback(on_signal, 0);
    mapper_db_add_mapping_callback(on_mapping, 0);
    mapper_db_add_link_callback(on_link, 0);

    wait_local_devices();

    loop();

  done:
    mapper_db_remove_device_callback(on_device, 0);
    mapper_db_remove_signal_callback(on_signal, 0);
    mapper_db_remove_mapping_callback(on_mapping, 0);
    mapper_db_remove_link_callback(on_link, 0);
    cleanup_dummy_device();
    return result;
}
