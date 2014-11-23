
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

mapper_monitor mon = 0;
mapper_db db = 0;

int verbose = 1;
int terminate = 0;
int done = 0;
int update = 0;

const int polltime_ms = 100;

const char *mode_strings[] =
{
    "undefined",
    "bypass",
    "linear",
    "expression",
    "calibrate",
    "reverse"
};

const char *bound_strings[] =
{
    "none",
    "mute",
    "clamp",
    "fold",
    "wrap"
};

void dbpause()
{
    // Don't pause normally, but this is left here to be easily
    // enabled for debugging purposes.

    // sleep(1);
}

void printdevice(mapper_db_device dev)
{
    printf("  %s", dev->name);

    int i=0;
    const char *key;
    char type;
    const void *val;
    int length;
    while(!mapper_db_device_property_index(dev, i++, &key, &type,
                                           &val, &length))
    {
        die_unless(val!=0, "returned zero value\n");

        // already printed this
        if (strcmp(key, "name")==0)
            continue;
        if (strcmp(key, "synced")==0) {
            // check current time
            mapper_timetag_t now;
            mapper_monitor_now(mon, &now);
            mapper_timetag_t *tt = (mapper_timetag_t *)val;
            if (tt->sec == 0)
                printf(", seconds_since_sync=unknown");
            else
                printf(", seconds_since_sync=%f",
                       mapper_timetag_difference(now, *tt));
        }
        else if (length) {
            printf(", %s=", key);
            mapper_prop_pp(type, length, val);
        }
    }
    printf("\n");
}

void printsignal(mapper_db_signal sig)
{
    printf("  %s name=%s%s",
           sig->is_output ? "output" : "input",
           sig->device_name, sig->name);

    int i=0;
    const char *key;
    char type;
    const void *val;
    int length;
    while(!mapper_db_signal_property_index(sig, i++, &key, &type,
                                           &val, &length))
    {
        die_unless(val!=0, "returned zero value\n");

        // already printed these
        if (strcmp(key, "device_name")==0
            || strcmp(key, "name")==0
            || strcmp(key, "direction")==0)
            continue;

        if (length) {
            printf(", %s=", key);
            mapper_prop_pp(type, length, val);
        }
    }
    printf("\n");
}

void printlink(mapper_db_link link)
{
    printf("  %s -> %s", link->src_name, link->dest_name);

    int i=0;
    const char *key;
    char type;
    const void *val;
    int length;
    while(!mapper_db_link_property_index(link, i++, &key, &type,
                                         &val, &length))
    {
        die_unless(val!=0, "returned zero value\n");

        // already printed these
        if (strcmp(key, "src_name")==0
            || strcmp(key, "dest_name")==0)
            continue;

        if (length) {
            printf(", %s=", key);
            mapper_prop_pp(type, length, val);
        }
    }
    printf("\n");
}

void printconnection(mapper_db_connection con)
{
    printf("  %s -> %s", con->src_name, con->dest_name);

    int i=0;
    const char *key;
    char type;
    const void *val;
    int length;
    while(!mapper_db_connection_property_index(con, i++, &key, &type,
                                               &val, &length))
    {
        die_unless(val!=0, "returned zero value\n");

        // already printed these
        if (strcmp(key, "src_name")==0
            || strcmp(key, "dest_name")==0)
            continue;

        if (length) {
            printf(", %s=", key);
            if (strcmp(key, "mode")==0)
                printf("%s", mode_strings[*((int*)val)]);
            else if (strncmp(key, "bound", 5)==0)
                printf("%s", bound_strings[*((int*)val)]);
            else
                mapper_prop_pp(type, length, val);
        }
    }
    printf("\n");
}

/*! Creation of a local dummy device. */
int setup_monitor()
{
    mon = mapper_monitor_new(0, SUB_DEVICE_ALL);
    if (!mon)
        goto error;
    printf("Monitor created.\n");

    db = mapper_monitor_get_db(mon);

    return 0;

  error:
    return 1;
}

void cleanup_monitor()
{
    if (mon) {
        printf("\rFreeing monitor.. ");
        fflush(stdout);
        mapper_monitor_free(mon);
        printf("ok\n");
    }
}

void loop()
{
    int i = 0;
    while ((!terminate || i++ < 200) && !done)
    {
        mapper_monitor_poll(mon, 0);
        usleep(polltime_ms * 1000);

        if (update++ < 0)
            continue;
        update = -10;

        // clear screen & cursor to home
        printf("\e[2J\e[0;0H");
        fflush(stdout);

        printf("Registered devices:\n");
        mapper_db_device *pdev = mapper_db_get_all_devices(db);
        while (pdev) {
            printdevice(*pdev);
            pdev = mapper_db_device_next(pdev);
        }

        printf("------------------------------\n");

        printf("Registered signals:\n");
        mapper_db_signal *psig =
            mapper_db_get_all_inputs(db);
        while (psig) {
            printsignal(*psig);
            psig = mapper_db_signal_next(psig);
        }
        psig = mapper_db_get_all_outputs(db);
        while (psig) {
            printsignal(*psig);
            psig = mapper_db_signal_next(psig);
        }

        printf("------------------------------\n");

        printf("Registered links:\n");
        mapper_db_link *plink = mapper_db_get_all_links(db);
        while (plink) {
            printlink(*plink);
            plink = mapper_db_link_next(plink);
        }

        printf("------------------------------\n");

        printf("Registered connections:\n");
        mapper_db_connection *pcon = mapper_db_get_all_connections(db);
        while (pcon) {
            printconnection(*pcon);
            pcon = mapper_db_connection_next(pcon);
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
        break;
    case MDB_MODIFY:
        printf("modified.\n");
        break;
    case MDB_REMOVE:
        printf("removed.\n");
        break;
    case MDB_UNRESPONSIVE:
        printf("unresponsive.\n");
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
    case MDB_UNRESPONSIVE:
        printf("unresponsive.\n");
        break;
    }
    dbpause();
    update = 1;
}

void on_connection(mapper_db_connection con, mapper_db_action_t a, void *user)
{
    printf("Connection %s -> %s ", con->src_name, con->dest_name);
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
    case MDB_UNRESPONSIVE:
        printf("unresponsive.\n");
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
    case MDB_UNRESPONSIVE:
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
                        printf("testmonitor.c: possible arguments "
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

    if (setup_monitor()) {
        printf("Error initializing monitor.\n");
        result = 1;
        goto done;
    }

    mapper_db_add_device_callback(db, on_device, 0);
    mapper_db_add_signal_callback(db, on_signal, 0);
    mapper_db_add_connection_callback(db, on_connection, 0);
    mapper_db_add_link_callback(db, on_link, 0);

    loop();

  done:
    mapper_db_remove_device_callback(db, on_device, 0);
    mapper_db_remove_signal_callback(db, on_signal, 0);
    mapper_db_remove_connection_callback(db, on_connection, 0);
    mapper_db_remove_link_callback(db, on_link, 0);
    cleanup_monitor();
    return result;
}
