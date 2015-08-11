
#include <stdlib.h>
#include <unistd.h>
#include <sys/time.h>
#include <stdio.h>
#include <string.h>

#include "config.h"
#include "mapper_internal.h"

#define AUTOSUBSCRIBE_INTERVAL 60

extern const char* prop_message_strings[NUM_AT_PARAMS];
extern const char* network_message_strings[NUM_MSG_STRINGS];

/*! Internal function to get the current time. */
static double current_time()
{
#ifdef HAVE_GETTIMEOFDAY
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (double) tv.tv_sec + tv.tv_usec / 1000000.0;
#else
#error No timing method known on this platform.
#endif
}

// function prototypes
static void monitor_subscribe_internal(mapper_monitor mon, mapper_device dev,
                                       int subscribe_flags, int timeout,
                                       int version);
static void monitor_unsubscribe_internal(mapper_monitor mon,
                                         mapper_device dev,
                                         int send_message);

typedef enum _db_request_direction {
    DIRECTION_IN,
    DIRECTION_OUT,
    DIRECTION_BOTH
} db_request_direction;

mapper_monitor mmon_new(mapper_network net, int subscribe_flags)
{
    mapper_monitor mon = (mapper_monitor)
        calloc(1, sizeof(struct _mapper_monitor));

    if (net) {
        mon->network = net;
        mon->own_network = 0;
    }
    else {
        mon->network = mapper_network_new(0, 0, 0);
        mon->own_network = 1;
    }

    if (!mon->network) {
        mmon_free(mon);
        return NULL;
    }

    mon->network->db.timeout_sec = MAPPER_TIMEOUT_SEC;

    mapper_network_add_monitor(mon->network, mon);
    if (subscribe_flags) {
        mmon_subscribe(mon, 0, subscribe_flags, -1);
    }
    return mon;
}

void mmon_free(mapper_monitor mon)
{
    if (!mon)
        return;

    // remove callbacks now so they won't be called when removing devices
    mapper_db_remove_all_callbacks(&mon->network->db);

    // unsubscribe from and remove any autorenewing subscriptions
    while (mon->subscriptions) {
        mmon_unsubscribe(mon, mon->subscriptions->device);
    }

    mapper_device *query = mapper_db_devices(&mon->network->db);
    while (query) {
        mapper_device dev = *query;
        query = mapper_device_query_next(query);
        if (!dev->local)
            mapper_db_remove_device(&mon->network->db, dev, 1);
    }

    if (mon->own_network) {
        if (mon->network->device)
            mon->network->device->local->own_network = 1;
        else
            mapper_network_free(mon->network);
    }
    else
        mapper_network_remove_monitor(mon->network, mon);

    free(mon);
}

int mmon_poll(mapper_monitor mon, int block_ms)
{
    int ping_time = mon->network->clock.next_ping;
    int admin_count = mapper_network_poll(mon->network);
    mapper_clock_now(&mon->network->clock, &mon->network->clock.now);

    // check if any subscriptions need to be renewed
    mapper_subscription s = mon->subscriptions;
    while (s) {
        if (s->lease_expiration_sec < mon->network->clock.now.sec) {
            monitor_subscribe_internal(mon, s->device, s->flags,
                                       AUTOSUBSCRIBE_INTERVAL, -1);
            // leave 10-second buffer for subscription renewal
            s->lease_expiration_sec =
                mon->network->clock.now.sec + AUTOSUBSCRIBE_INTERVAL - 10;
        }
        s = s->next;
    }

    if (block_ms) {
        double then = current_time();
        while ((current_time() - then)*1000 < block_ms) {
            admin_count += mapper_network_poll(mon->network);
#ifdef WIN32
            Sleep(block_ms);
#else
            usleep(block_ms * 100);
#endif
        }
    }

    if (ping_time != mon->network->clock.next_ping) {
        // some housekeeping: check if any devices have timed out
        mapper_db_check_device_status(&mon->network->db,
                                      mon->network->clock.now.sec);
    }

    return admin_count;
}

mapper_db mmon_db(mapper_monitor mon)
{
    return &mon->network->db;
}

static void monitor_set_bundle_dest(mapper_monitor mon, mapper_device dev)
{
    // TODO: look up device info, maybe send directly
    mapper_network_set_bundle_dest_bus(mon->network);
}

static void monitor_subscribe_internal(mapper_monitor mon, mapper_device dev,
                                       int subscribe_flags, int timeout,
                                       int version)
{
    char cmd[1024];
    snprintf(cmd, 1024, "/%s/subscribe", dev->name);

    monitor_set_bundle_dest(mon, dev);
    lo_message m = lo_message_new();
    if (m) {
        if (subscribe_flags & SUBSCRIBE_ALL)
            lo_message_add_string(m, "all");
        else {
            if (subscribe_flags & SUBSCRIBE_DEVICE)
                lo_message_add_string(m, "device");
            if (subscribe_flags & SUBSCRIBE_DEVICE_SIGNALS)
                lo_message_add_string(m, "signals");
            else {
                if (subscribe_flags & SUBSCRIBE_DEVICE_INPUTS)
                    lo_message_add_string(m, "inputs");
                else if (subscribe_flags & SUBSCRIBE_DEVICE_OUTPUTS)
                    lo_message_add_string(m, "outputs");
            }
            if (subscribe_flags & SUBSCRIBE_DEVICE_MAPS)
                lo_message_add_string(m, "maps");
            else {
                if (subscribe_flags & SUBSCRIBE_DEVICE_MAPS_IN)
                    lo_message_add_string(m, "incoming_maps");
                else if (subscribe_flags & SUBSCRIBE_DEVICE_MAPS_OUT)
                    lo_message_add_string(m, "outgoing_maps");
            }
        }
        lo_message_add_string(m, "@lease");
        lo_message_add_int32(m, timeout);
        if (version >= 0) {
            lo_message_add_string(m, "@version");
            lo_message_add_int32(m, version);
        }
        lo_bundle_add_message(mon->network->bundle, cmd, m);
        mapper_network_send_bundle(mon->network);
    }
}

static mapper_subscription subscription(mapper_monitor mon, mapper_device dev)
{
    mapper_subscription s = mon->subscriptions;
    while (s) {
        if (s->device == dev)
            return s;
        s = s->next;
    }
    return 0;
}

static void on_device_autosubscribe(mapper_device dev, mapper_action_t a,
                                    const void *user)
{
    mapper_monitor mon = (mapper_monitor)(user);

    // New subscriptions are handled in network.c as response to "sync" msg
    if (a == MAPPER_REMOVED) {
        monitor_unsubscribe_internal(mon, dev, 0);
    }
}

static void mmon_autosubscribe(mapper_monitor mon, int autosubscribe_flags)
{
    // TODO: remove autorenewing subscription record if necessary
    if (!mon->autosubscribe && autosubscribe_flags) {
        mapper_db_add_device_callback(&mon->network->db, on_device_autosubscribe,
                                      mon);
        mmon_request_devices(mon);
    }
    else if (mon->autosubscribe && !autosubscribe_flags) {
        mapper_db_remove_device_callback(&mon->network->db,
                                         on_device_autosubscribe, mon);
        while (mon->subscriptions) {
            monitor_unsubscribe_internal(mon, mon->subscriptions->device, 1);
        }
    }
    mon->autosubscribe = autosubscribe_flags;
}

void mmon_subscribe(mapper_monitor mon, mapper_device dev,
                    int subscribe_flags, int timeout)
{
    if (!dev) {
        mmon_autosubscribe(mon, subscribe_flags);
        return;
    }
    if (timeout == -1) {
        // special case: autorenew subscription lease
        // first check if subscription already exists
        mapper_subscription s = subscription(mon, dev);

        if (!s) {
            // store subscription record
            s = malloc(sizeof(struct _mapper_subscription));
            s->device = dev;
            s->next = mon->subscriptions;
            mon->subscriptions = s;
        }
        s->flags = subscribe_flags;

        mapper_clock_now(&mon->network->clock, &mon->network->clock.now);
        // leave 10-second buffer for subscription lease
        s->lease_expiration_sec =
            mon->network->clock.now.sec + AUTOSUBSCRIBE_INTERVAL - 10;

        timeout = AUTOSUBSCRIBE_INTERVAL;
    }

    monitor_subscribe_internal(mon, dev, subscribe_flags, timeout, 0);
}

static void monitor_unsubscribe_internal(mapper_monitor mon,
                                         mapper_device dev,
                                         int send_message)
{
    char cmd[1024];
    // check if autorenewing subscription exists
    mapper_subscription *s = &mon->subscriptions;
    while (*s) {
        if ((*s)->device == dev) {
            if (send_message) {
                snprintf(cmd, 1024, "/%s/unsubscribe", dev->name);
                monitor_set_bundle_dest(mon, dev);
                lo_message m = lo_message_new();
                if (!m)
                    break;
                lo_bundle_add_message(mon->network->bundle, cmd, m);
                mapper_network_send_bundle(mon->network);
            }
            // remove from subscriber list
            mapper_subscription temp = *s;
            *s = temp->next;
            free(temp);
            return;
        }
        s = &(*s)->next;
    }
}

void mmon_unsubscribe(mapper_monitor mon, mapper_device dev)
{
    if (!dev)
        mmon_autosubscribe(mon, SUBSCRIBE_NONE);
    monitor_unsubscribe_internal(mon, dev, 1);
}

void mmon_request_devices(mapper_monitor mon)
{
    lo_message msg = lo_message_new();
    if (!msg) {
        trace("couldn't allocate lo_message\n");
        return;
    }
    mapper_network_set_bundle_dest_bus(mon->network);
    lo_bundle_add_message(mon->network->bundle,
                          network_message_strings[MSG_WHO], msg);
}

static int alphabetise_signals(int num, mapper_signal *sigs, int *order)
{
    int i, j, result = 1;
    for (i = 0; i < num; i++)
        order[i] = i;
    for (i = 1; i < num; i++) {
        j = i-1;
        while (j >= 0
               && (((result = strcmp(sigs[order[j]]->device->name,
                                           sigs[order[j+1]]->device->name)) > 0)
               || ((result = strcmp(sigs[order[j]]->name,
                                    sigs[order[j+1]]->name)) > 0))) {
            int temp = order[j];
            order[j] = order[j+1];
            order[j+1] = temp;
            j--;
        }
        if (result == 0)
            return 1;
    }
    return 0;
}

mapper_map mmon_add_map(mapper_monitor mon, int num_sources,
                        mapper_signal *sources, mapper_signal destination)
{
    int i;
    if (num_sources <= 0 || num_sources > MAX_NUM_MAP_SOURCES
        || !sources || !destination)
        return 0;

    // TODO: do not allow "unregistered" signals to be mapped

    // check if record of map already exists
    mapper_map *maps, *temp;
    maps = mapper_db_signal_incoming_maps(&mon->network->db, destination);
    for (i = 0; i < num_sources; i++) {
        temp = mapper_db_signal_outgoing_maps(&mon->network->db, sources[i]);
        maps = mapper_map_query_intersection(maps, temp);
    }
    while (maps) {
        if ((*maps)->num_sources == num_sources)
            return *maps;
        maps = mapper_map_query_next(maps);
    }

    int order[num_sources];
    if (alphabetise_signals(num_sources, sources, order)) {
        trace("error in mmon_add_map(): multiple use of source signal.\n");
        return 0;
    }

    // place in map staging area
    // TODO: allow building multiple maps in parallel
    if (mon->staged_map) {
        mmon_update_map(mon, mon->staged_map);
        free(mon->staged_map->sources);
        free(mon->staged_map);
    }
    mon->staged_map = (mapper_map) calloc(1, sizeof(struct _mapper_map));
    mon->staged_map->sources = ((mapper_slot)
                                calloc(1, sizeof(struct _mapper_slot)
                                       * num_sources));
    mon->staged_map->num_sources = num_sources;
    for (i = 0; i < num_sources; i++) {
        mon->staged_map->sources[i].signal = sources[order[i]];
        mon->staged_map->sources[i].map = mon->staged_map;
    }
    mon->staged_map->destination.signal = destination;
    mon->staged_map->destination.map = mon->staged_map;
    mon->staged_map->extra = table_new();
    mon->staged_map->updater = table_new();
    return mon->staged_map;
}

void mmon_update_map(mapper_monitor mon, mapper_map map)
{
    if (!mon || !map)
        return;

    lo_message m = lo_message_new();
    if (!m)
        return;

    const char *src_names[map->num_sources];
    int i;
    for (i = 0; i < map->num_sources; i++) {
        char src_name[256];
        snprintf(src_name, 256, "%s%s", map->sources[i].signal->device->name,
                 map->sources[i].signal->path);
        src_names[i] = strdup(src_name);
        lo_message_add_string(m, src_names[i]);
    }
    lo_message_add_string(m, "->");
    char dest_name[256];
    snprintf(dest_name, 256, "%s%s", map->destination.signal->device->name,
             map->destination.signal->path);
    lo_message_add_string(m, dest_name);

    mapper_message_add_value_table(m, map->updater);

    // TODO: lookup device ip/ports, send directly?
    mapper_network_set_bundle_dest_bus(mon->network);
    lo_bundle_add_message(mon->network->bundle,
                          network_message_strings[map->id ? MSG_MODIFY_MAP : MSG_MAP], m);

    /* We cannot depend on string arguments sticking around for liblo to
     * serialize later: trigger immediate dispatch. */
    mapper_network_send_bundle(mon->network);

    for (i = 0; i < map->num_sources; i++) {
        free((char *)src_names[i]);
    }
    table_clear(map->updater);

    if (map == mon->staged_map) {
        if (map->sources)
            free(map->sources);
        table_free(map->updater);
        table_free(map->extra);
        free(map);
        mon->staged_map = 0;
    }
}

void mmon_remove_map(mapper_monitor mon, mapper_map_t *map)
{
    if (!mon || !map || !map->num_sources || !map->sources)
        return;

    lo_message m = lo_message_new();
    if (!m)
        return;

    const char *src_names[map->num_sources];
    int i;
    for (i = 0; i < map->num_sources; i++) {
        char src_name[256];
        snprintf(src_name, 256, "%s%s", map->sources[i].signal->device->name,
                 map->sources[i].signal->path);
        src_names[i] = strdup(src_name);
        lo_message_add_string(m, src_names[i]);
    }
    lo_message_add_string(m, "->");
    char dest_name[256];
    snprintf(dest_name, 256, "%s%s", map->destination.signal->device->name,
             map->destination.signal->path);
    lo_message_add_string(m, dest_name);

    // TODO: lookup device ip/ports, send directly?
    mapper_network_set_bundle_dest_bus(mon->network);
    lo_bundle_add_message(mon->network->bundle,
                          network_message_strings[MSG_UNMAP], m);

    /* We cannot depend on string arguments sticking around for liblo to
     * serialize later: trigger immediate dispatch. */
    mapper_network_send_bundle(mon->network);

    for (i = 0; i < map->num_sources; i++)
        free((char *)src_names[i]);
}

void mmon_now(mapper_monitor mon, mapper_timetag_t *tt)
{
    mapper_clock_now(&mon->network->clock, tt);
}

// Forward a message to the multicast bus
void mmon_send(mapper_monitor mon, const char *path, const char *types, ...)
{
    if (!mon || !path || !types)
        return;
    lo_message m = lo_message_new();
    if (!m)
        return;

    va_list aq;
    va_start(aq, types);
    char t[] = " ";

    while (types && *types) {
        t[0] = types[0];
        switch (t[0]) {
            case 'i':
                lo_message_add(m, t, va_arg(aq, int));
                break;
            case 's':
            case 'S':
                lo_message_add(m, t, va_arg(aq, char*));
                break;
            case 'f':
            case 'd':
                lo_message_add(m, t, va_arg(aq, double));
                break;
            case 'c':
                lo_message_add(m, t, (char)va_arg(aq, int));
                break;
            case 't':
                lo_message_add(m, t, va_arg(aq, mapper_timetag_t));
                break;
            default:
                die_unless(0, "message %s, unknown type '%c'\n",
                           path, t[0]);
        }
        types++;
    }
    mapper_network_set_bundle_dest_bus(mon->network);
    lo_bundle_add_message(mon->network->bundle, path, m);
    /* We cannot depend on string arguments sticking around for liblo to
     * serialize later: trigger immediate dispatch. */
    mapper_network_send_bundle(mon->network);
}
