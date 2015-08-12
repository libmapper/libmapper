
#include "../src/mapper_internal.h"
#include <mapper/mapper.h>
#include <stdio.h>
#include <math.h>
#include <string.h>
#include <lo/lo.h>

#include <unistd.h>
#include <signal.h>

#ifdef WIN32
#define usleep(x) Sleep(x/1000)
#endif

mapper_admin adm = 0;
mapper_db db = 0;

int verbose = 1;
int terminate = 0;
int done = 0;
int update = 0;

const int polltime_ms = 100;

void dbpause()
{
    // Don't pause normally, but this is left here to be easily
    // enabled for debugging purposes.

    // sleep(1);
}

void printdevice(mapper_device dev)
{
    printf(" └─ %s", dev->name);

    int i=0;
    const char *key;
    char type;
    const void *val;
    int length;
    while(!mapper_device_property_index(dev, i++, &key, &length, &type, &val))
    {
        die_unless(val!=0, "returned zero value\n");

        // already printed this
        if (strcmp(key, "name")==0)
            continue;
        if (strcmp(key, "synced")==0) {
            // check current time
            mapper_timetag_t now;
            mapper_admin_now(adm, &now);
            mapper_timetag_t *tt = (mapper_timetag_t *)val;
            if (tt->sec == 0)
                printf(", seconds_since_sync=unknown");
            else
                printf(", seconds_since_sync=%f",
                       mapper_timetag_difference(now, *tt));
        }
        else if (length) {
            printf(", %s=", key);
            mapper_property_pp(length, type, val);
        }
    }
    printf("\n");
}

void printsignal(mapper_signal sig)
{
    printf("%s, direction=", sig->name);
    switch (sig->direction) {
        case DI_BOTH:
            printf("both");
            break;
        case DI_OUTGOING:
            printf("output");
            break;
        case DI_INCOMING:
            printf("input");
            break;
        default:
            printf("unknown");
            break;
    }

    int i=0;
    const char *key;
    char type;
    const void *val;
    int length;
    while(!mapper_signal_property_index(sig, i++, &key, &length, &type, &val))
    {
        die_unless(val!=0, "returned zero value\n");

        // already printed these
        if (strcmp(key, "device_name")==0
            || strcmp(key, "name")==0
            || strcmp(key, "direction")==0)
            continue;

        if (length) {
            printf(", %s=", key);
            mapper_property_pp(length, type, val);
        }
    }
    printf("\n");
}

void printmap(mapper_map map)
{
    printf(" └─ ");
    mapper_map_pp(map);
}

/*! Creation of a local dummy device. */
int setup_admin()
{
    adm = mapper_admin_new(0, SUBSCRIBE_ALL);
    if (!adm)
        goto error;
    printf("Admin created.\n");

    db = mapper_admin_db(adm);

    return 0;

  error:
    return 1;
}

void cleanup_admin()
{
    if (adm) {
        printf("\rFreeing admin.. ");
        fflush(stdout);
        mapper_admin_free(adm);
        printf("ok\n");
    }
}

void loop()
{
    int i = 0;
    while ((!terminate || i++ < 200) && !done)
    {
        mapper_admin_poll(adm, 0);
        usleep(polltime_ms * 1000);

        if (update++ < 0)
            continue;
        update = -10;

        // clear screen & cursor to home
        printf("\e[2J\e[0;0H");
        fflush(stdout);

        printf("-------------------------------\n");

        printf("Registered devices and signals:\n");
        mapper_device *pdev = mapper_db_devices(db), tempdev;
        mapper_signal *psig, tempsig;
        while (pdev) {
            tempdev = *pdev;
            pdev = mapper_device_query_next(pdev);
            printdevice(tempdev);

            int numsigs = tempdev->num_outputs;
            psig = mapper_db_device_outputs(db, tempdev);
            while (psig) {
                tempsig = *psig;
                psig = mapper_signal_query_next(psig);
                printf("    %s ", psig ? "├─" : "└─");
                printsignal(tempsig);
            }
            numsigs = tempdev->num_inputs;
            psig = mapper_db_device_inputs(db, tempdev);
            while (psig) {
                tempsig = *psig;
                psig = mapper_signal_query_next(psig);
                printf("    %s ", psig ? "├─" : "└─");
                printsignal(tempsig);
            }
        }

        printf("-------------------------------\n");

        printf("Registered maps:\n");
        mapper_map *pmap = mapper_db_maps(db);
        while (pmap) {
            printmap(*pmap);
            pmap = mapper_map_query_next(pmap);
        }

        printf("-------------------------------\n");
    }
}

void on_device(mapper_device dev, mapper_action_t a, const void *user)
{
    printf("Device %s ", dev->name);
    switch (a) {
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
        mapper_db_flush(db, 10, 0);
        break;
    }
    dbpause();
    update = 1;
}

void on_signal(mapper_signal sig, mapper_action_t a, const void *user)
{
    printf("Signal %s/%s ", sig->device->name, sig->name);
    switch (a) {
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
    dbpause();
    update = 1;
}

void on_map(mapper_map map, mapper_action_t a, const void *user)
{
    int i;
    printf("Map ");
    for (i = 0; i < map->num_sources; i++)
        printf("%s/%s ", map->sources[i].signal->device->name,
               map->sources[i].signal->name);
    printf("-> %s/%s ", map->destination.signal->device->name,
           map->destination.signal->name);
    switch (a) {
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
    dbpause();
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

    if (setup_admin()) {
        printf("Error initializing admin.\n");
        result = 1;
        goto done;
    }

    mapper_db_add_device_callback(db, on_device, 0);
    mapper_db_add_signal_callback(db, on_signal, 0);
    mapper_db_add_map_callback(db, on_map, 0);

    loop();

  done:
    mapper_db_remove_device_callback(db, on_device, 0);
    mapper_db_remove_signal_callback(db, on_signal, 0);
    mapper_db_remove_map_callback(db, on_map, 0);
    cleanup_admin();
    return result;
}
