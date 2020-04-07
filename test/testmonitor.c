
#include "../src/mapper_internal.h"
#include <mapper/mapper.h>
#include <stdio.h>
#include <math.h>
#include <string.h>
#include <lo/lo.h>

#include <unistd.h>
#include <signal.h>

mapper_database db = 0;

int verbose = 1;
int terminate = 0;
int done = 0;
int update = 0;

const int polltime_ms = 100;

void monitor_pause()
{
    // Don't pause normally, but this is left here to be easily
    // enabled for debugging purposes.

    // sleep(1);
}

void printdevice(mapper_device dev)
{
    printf(" └─ ");
    mapper_device_print(dev);
}

void printlink(mapper_link link)
{
    printf(" └─ ");
    mapper_link_print(link);
}

void printmap(mapper_map map)
{
    printf(" └─ ");
    mapper_map_print(map);
}

/*! Creation of a local dummy device. */
int setup_database()
{
    db = mapper_database_new(0, MAPPER_OBJ_ALL);
    if (!db)
        goto error;
    printf("Database created.\n");

    return 0;

  error:
    return 1;
}

void cleanup_database()
{
    if (db) {
        printf("\rFreeing database.. ");
        fflush(stdout);
        mapper_database_free(db);
        printf("ok\n");
    }
}

void loop()
{
    int i = 0;
    while ((!terminate || i++ < 250) && !done)
    {
        mapper_database_poll(db, polltime_ms);

        if (update++ < 0)
            continue;
        update = -10;

        if (!verbose)
            continue;

        // clear screen & cursor to home
        printf("\e[2J\e[0;0H");
        fflush(stdout);

        printf("-------------------------------\n");

        printf("Registered devices (%d) and signals (%d):\n",
               mapper_database_num_devices(db),
               mapper_database_num_signals(db, MAPPER_DIR_ANY));
        mapper_device *pdev = mapper_database_devices(db), tempdev;
        mapper_signal *psig, tempsig;
        while (pdev) {
            tempdev = *pdev;
            pdev = mapper_device_query_next(pdev);
            printdevice(tempdev);

            psig = mapper_device_signals(tempdev, MAPPER_DIR_OUTGOING);
            while (psig) {
                tempsig = *psig;
                psig = mapper_signal_query_next(psig);
                printf("    %s ", psig ? "├─" : "└─");
                mapper_signal_print(tempsig, 0);
            }
            psig = mapper_device_signals(tempdev, MAPPER_DIR_INCOMING);
            while (psig) {
                tempsig = *psig;
                psig = mapper_signal_query_next(psig);
                printf("    %s ", psig ? "├─" : "└─");
                mapper_signal_print(tempsig, 0);
            }
        }

        printf("-------------------------------\n");

        printf("Registered links (%d):\n", mapper_database_num_links(db));
        mapper_link *plink = mapper_database_links(db);
        while (plink) {
            printlink(*plink);
            plink = mapper_link_query_next(plink);
        }

        printf("-------------------------------\n");

        printf("Registered maps (%d):\n", mapper_database_num_maps(db));
        mapper_map *pmap = mapper_database_maps(db);
        while (pmap) {
            printmap(*pmap);
            pmap = mapper_map_query_next(pmap);
        }

        printf("-------------------------------\n");
    }
}

void on_device(mapper_database db, mapper_device dev, mapper_record_event e,
               const void *user)
{
    if (verbose) {
        printf("Device %s ", dev->name);
        switch (e) {
        case MAPPER_ADDED:
            printf("added.\n");
            break;
        case MAPPER_MODIFIED:
            printf("modified.\n");
            break;
        case MAPPER_REMOVED:
            printf("removed.\n");
            break;
        case MAPPER_EXPIRED:
            printf("unresponsive.\n");
            mapper_database_flush(db, 10, 0);
            break;
        }
    }
    monitor_pause();
    update = 1;
}

void on_link(mapper_database db, mapper_link lnk, mapper_record_event e,
             const void *user)
{
    if (verbose) {
        printf("Link %s <-> %s ", lnk->devices[0]->name, lnk->devices[1]->name);
        switch (e) {
            case MAPPER_ADDED:
                printf("added.\n");
                break;
            case MAPPER_MODIFIED:
                printf("modified.\n");
                break;
            case MAPPER_REMOVED:
                printf("removed.\n");
                break;
            case MAPPER_EXPIRED:
                printf("unresponsive.\n");
                break;
        }
    }
    monitor_pause();
    update = 1;
}

void on_signal(mapper_database db, mapper_signal sig, mapper_record_event e,
               const void *user)
{
    if (verbose) {
        printf("Signal %s/%s ", sig->device->name, sig->name);
        switch (e) {
        case MAPPER_ADDED:
            printf("added.\n");
            break;
        case MAPPER_MODIFIED:
            printf("modified.\n");
            break;
        case MAPPER_REMOVED:
            printf("removed.\n");
            break;
        case MAPPER_EXPIRED:
            printf("unresponsive.\n");
            break;
        }
    }
    monitor_pause();
    update = 1;
}

void on_map(mapper_database db, mapper_map map, mapper_record_event e,
            const void *user)
{
    int i;
    if (verbose) {
        printf("Map ");
        for (i = 0; i < map->num_sources; i++)
            printf("%s/%s ", map->sources[i]->signal->device->name,
                   map->sources[i]->signal->name);
        printf("-> %s/%s ", map->destination.signal->device->name,
               map->destination.signal->name);
        switch (e) {
        case MAPPER_ADDED:
            printf("added.\n");
            break;
        case MAPPER_MODIFIED:
            printf("modified.\n");
            break;
        case MAPPER_REMOVED:
            printf("removed.\n");
            break;
        case MAPPER_EXPIRED:
            printf("unresponsive.\n");
            break;
        }
    }
    monitor_pause();
    update = 1;
}

void ctrlc(int sig)
{
    done = 1;
}

int main(int argc, char **argv)
{
    int i, j, result = 0;

    // process flags for -v verbose, -t terminate, -h help
    for (i = 1; i < argc; i++) {
        if (argv[i] && argv[i][0] == '-') {
            int len = strlen(argv[i]);
            for (j = 1; j < len; j++) {
                switch (argv[i][j]) {
                    case 'h':
                        printf("testadmin.c: possible arguments "
                               "-q quiet (suppress output), "
                               "-t terminate automatically, "
                               "-h help\n");
                        return 1;
                        break;
                    case 'q':
                        verbose = 0;
                        break;
                    case 't':
                        terminate = 1;
                        break;
                    default:
                        break;
                }
            }
        }
    }

    signal(SIGINT, ctrlc);

    if (setup_database()) {
        printf("Error initializing database.\n");
        result = 1;
        goto done;
    }

    mapper_database_add_device_callback(db, on_device, 0);
    mapper_database_add_signal_callback(db, on_signal, 0);
    mapper_database_add_link_callback(db, on_link, 0);
    mapper_database_add_map_callback(db, on_map, 0);

    loop();

  done:
    mapper_database_remove_device_callback(db, on_device, 0);
    mapper_database_remove_signal_callback(db, on_signal, 0);
    mapper_database_remove_link_callback(db, on_link, 0);
    mapper_database_remove_map_callback(db, on_map, 0);
    cleanup_database();
    return result;
}
