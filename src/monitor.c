
#include <stdlib.h>
#include <unistd.h>
#include <sys/time.h>
#include <stdio.h>
#include <string.h>

#include "config.h"
#include "mapper_internal.h"

#define AUTOSUBSCRIBE_INTERVAL 60

extern const char* prop_msg_strings[N_AT_PARAMS];
extern const char* admin_msg_strings[N_ADM_STRINGS];

/*! Internal function to get the current time. */
static double get_current_time()
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
static void monitor_subscribe_internal(mapper_monitor mon, mapper_db_device dev,
                                       int subscribe_flags, int timeout,
                                       int version);
static void monitor_unsubscribe_internal(mapper_monitor mon,
                                         mapper_db_device dev,
                                         int send_message);

typedef enum _db_request_direction {
    DIRECTION_IN,
    DIRECTION_OUT,
    DIRECTION_BOTH
} db_request_direction;

mapper_monitor mmon_new(mapper_admin admin, int subscribe_flags)
{
    mapper_monitor mon = (mapper_monitor)
        calloc(1, sizeof(struct _mapper_monitor));

    if (admin) {
        mon->admin = admin;
        mon->own_admin = 0;
    }
    else {
        mon->admin = mapper_admin_new(0, 0, 0);
        mon->own_admin = 1;
    }

    if (!mon->admin) {
        mmon_free(mon);
        return NULL;
    }

    mon->timeout_sec = ADMIN_TIMEOUT_SEC;

    mapper_admin_add_monitor(mon->admin, mon);
    if (subscribe_flags) {
        mmon_subscribe(mon, 0, subscribe_flags, -1);
        mmon_request_devices(mon);
    }
    return mon;
}

void mmon_free(mapper_monitor mon)
{
    if (!mon)
        return;

    // remove callbacks now so they won't be called when removing devices
    mapper_db_remove_all_callbacks(&mon->db);

    // unsubscribe from and remove any autorenewing subscriptions
    while (mon->subscriptions) {
        mmon_unsubscribe(mon, mon->subscriptions->device);
    }

    while (mon->db.devices)
        mapper_db_remove_device(&mon->db, mon->db.devices, 1);
    if (mon->admin) {
        if (mon->own_admin)
            mapper_admin_free(mon->admin);
        else
            mapper_admin_remove_monitor(mon->admin, mon);
    }
    free(mon);
}

int mmon_poll(mapper_monitor mon, int block_ms)
{
    int ping_time = mon->admin->clock.next_ping;
    int admin_count = mapper_admin_poll(mon->admin);
    mapper_clock_now(&mon->admin->clock, &mon->admin->clock.now);

    // check if any subscriptions need to be renewed
    mapper_subscription s = mon->subscriptions;
    while (s) {
        if (s->lease_expiration_sec < mon->admin->clock.now.sec) {
            monitor_subscribe_internal(mon, s->device, s->flags,
                                       AUTOSUBSCRIBE_INTERVAL, -1);
            // leave 10-second buffer for subscription renewal
            s->lease_expiration_sec =
                mon->admin->clock.now.sec + AUTOSUBSCRIBE_INTERVAL - 10;
        }
        s = s->next;
    }

    if (block_ms) {
        double then = get_current_time();
        while ((get_current_time() - then)*1000 < block_ms) {
            admin_count += mapper_admin_poll(mon->admin);
#ifdef WIN32
            Sleep(block_ms);
#else
            usleep(block_ms * 100);
#endif
        }
    }

    if (ping_time != mon->admin->clock.next_ping) {
        // some housekeeping: check if any devices have timed out
        mapper_db_check_device_status(&mon->db,
                                      mon->admin->clock.now.sec - mon->timeout_sec);
    }

    return admin_count;
}

mapper_db mmon_get_db(mapper_monitor mon)
{
    return &mon->db;
}

static void monitor_set_bundle_dest(mapper_monitor mon, mapper_db_device dev)
{
    // TODO: look up device info, maybe send directly
    mapper_admin_set_bundle_dest_bus(mon->admin);
}

static void monitor_subscribe_internal(mapper_monitor mon, mapper_db_device dev,
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
        lo_bundle_add_message(mon->admin->bundle, cmd, m);
        mapper_admin_send_bundle(mon->admin);
    }
}

static mapper_subscription get_subscription(mapper_monitor mon,
                                            mapper_db_device dev)
{
    mapper_subscription s = mon->subscriptions;
    while (s) {
        if (s->device == dev)
            return s;
        s = s->next;
    }
    return 0;
}

static void on_device_autosubscribe(mapper_db_device dev, mapper_db_action_t a,
                                    void *user)
{
    mapper_monitor mon = (mapper_monitor)(user);

    // New subscriptions are handled in admin.c as response to device "sync" msg
    if (a == MDB_REMOVE) {
        monitor_unsubscribe_internal(mon, dev, 0);
    }
}

static void mmon_autosubscribe(mapper_monitor mon, int autosubscribe_flags)
{
    // TODO: remove autorenewing subscription record if necessary
    if (!mon->autosubscribe && autosubscribe_flags) {
        mapper_db_add_device_callback(&mon->db, on_device_autosubscribe, mon);
        mmon_request_devices(mon);
    }
    else if (mon->autosubscribe && !autosubscribe_flags) {
        mapper_db_remove_device_callback(&mon->db, on_device_autosubscribe, mon);
        while (mon->subscriptions) {
            monitor_unsubscribe_internal(mon, mon->subscriptions->device, 1);
        }
    }
    mon->autosubscribe = autosubscribe_flags;
}

void mmon_subscribe(mapper_monitor mon, mapper_db_device dev,
                    int subscribe_flags, int timeout)
{
    if (!dev) {
        mmon_autosubscribe(mon, subscribe_flags);
        return;
    }
    if (timeout == -1) {
        // special case: autorenew subscription lease
        // first check if subscription already exists
        mapper_subscription s = get_subscription(mon, dev);

        if (!s) {
            // store subscription record
            s = malloc(sizeof(struct _mapper_subscription));
            s->device = dev;
            s->next = mon->subscriptions;
            mon->subscriptions = s;
        }
        s->flags = subscribe_flags;

        mapper_clock_now(&mon->admin->clock, &mon->admin->clock.now);
        // leave 10-second buffer for subscription lease
        s->lease_expiration_sec =
            mon->admin->clock.now.sec + AUTOSUBSCRIBE_INTERVAL - 10;

        timeout = AUTOSUBSCRIBE_INTERVAL;
    }

    monitor_subscribe_internal(mon, dev, subscribe_flags, timeout, 0);
}

static void monitor_unsubscribe_internal(mapper_monitor mon,
                                         mapper_db_device dev,
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
                lo_bundle_add_message(mon->admin->bundle, cmd, m);
                mapper_admin_send_bundle(mon->admin);
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

void mmon_unsubscribe(mapper_monitor mon, mapper_db_device dev)
{
    if (!dev)
        mmon_autosubscribe(mon, SUBSCRIBE_NONE);
    monitor_unsubscribe_internal(mon, dev, 1);
}

void mmon_request_devices(mapper_monitor mon)
{
    mapper_admin_set_bundle_dest_bus(mon->admin);
    mapper_admin_bundle_message(mon->admin, ADM_WHO, 0, "");
}

#define prep_varargs(...)    \
    _real_prep_varargs(__VA_ARGS__, N_AT_PARAMS);

// simple wrapper function for building osc messages
static void _real_prep_varargs(lo_message m, ...)
{
    va_list aq;
    va_start(aq, m);
    mapper_msg_prepare_varargs(m, aq);
    va_end(aq);
}

mapper_map mmon_add_map(mapper_monitor mon, int num_sources,
                        mapper_db_signal *sources,
                        mapper_db_signal destination)
{
    if (num_sources <= 0 || num_sources > MAX_NUM_MAP_SOURCES
        || !sources || !destination)
        return 0;

    // check if record of map already exists
    int i;
    mapper_map *maps, *temp;
    maps = mapper_db_get_signal_incoming_maps(&mon->db, destination);
    for (i = 0; i < num_sources; i++) {
        temp = mapper_db_get_signal_outgoing_maps(&mon->db, sources[i]);
        maps = mapper_db_map_query_intersection(&mon->db, maps, temp);
    }
    while (maps) {
        if ((*maps)->num_sources == num_sources)
            return *maps;
        maps = mapper_db_map_query_next(maps);
    }

    // place in map staging area
    // TODO: allow building multiple maps in parallel
    if (mon->staged_map) {
        mmon_update_map(mon, mon->staged_map);
        free(mon->staged_map->sources);
        free(mon->staged_map);
    }
    mon->staged_map = (mapper_map) calloc(1, sizeof(struct _mapper_map));
    mon->staged_map->sources = ((mapper_map_slot)
                                calloc(1, sizeof(struct _mapper_map_slot)
                                       * num_sources));
    mon->staged_map->num_sources = num_sources;
    for (i = 0; i < num_sources; i++)
        mon->staged_map->sources[i].signal = sources[i];
    mon->staged_map->destination.signal = destination;
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

    int src_flags = 0;
    for (i = 0; i < map->num_sources; i++)
        src_flags |= map->sources[i].flags;
    mapper_map_slot d = &map->destination;

    if (map->flags || src_flags || d->flags) {
        prep_varargs(m, map->id ? AT_ID : -1, map,
            (src_flags & MAP_SLOT_BOUND_MIN) ? AT_SRC_BOUND_MIN : -1, map,
            (src_flags & MAP_SLOT_BOUND_MAX) ? AT_SRC_BOUND_MAX : -1, map,
            (d->flags & MAP_SLOT_BOUND_MIN) ? AT_DEST_BOUND_MIN : -1, d->bound_min,
            (d->flags & MAP_SLOT_BOUND_MAX) ? AT_DEST_BOUND_MAX : -1, d->bound_max,
            bitmatch(src_flags, MAP_SLOT_MIN_KNOWN) ? AT_SRC_MIN : -1, map,
            bitmatch(src_flags, MAP_SLOT_MAX_KNOWN) ? AT_SRC_MAX : -1, map,
            bitmatch(d->flags, MAP_SLOT_MIN_KNOWN) ? AT_DEST_MIN : -1, map,
            bitmatch(d->flags, MAP_SLOT_MAX_KNOWN) ? AT_DEST_MAX : -1, map,
            (map->flags & MAP_EXPRESSION) ? AT_EXPRESSION : -1, map->expression,
            (map->flags & MAP_MODE) ? AT_MODE : -1, map->mode,
            (map->flags & MAP_MUTED) ? AT_MUTE : -1, map->muted,
            (src_flags & MAP_SLOT_SEND_AS_INSTANCE) ? AT_SEND_AS_INSTANCE : -1, map,
            (bitmatch(map->flags, MAP_SCOPE_NAMES)
             && map->scope.size) ? AT_SCOPE : -1, map->scope.names,
            (src_flags & MAP_SLOT_CAUSE_UPDATE) ? AT_CAUSE_UPDATE : -1, map);
    }

    // TODO: lookup device ip/ports, send directly?
    mapper_admin_set_bundle_dest_bus(mon->admin);
    lo_bundle_add_message(mon->admin->bundle,
                          admin_msg_strings[map->id ? ADM_MODIFY_MAP : ADM_MAP], m);

    /* We cannot depend on string arguments sticking around for liblo to
     * serialize later: trigger immediate dispatch. */
    mapper_admin_send_bundle(mon->admin);

    for (i = 0; i < map->num_sources; i++)
        free((char *)src_names[i]);
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
    mapper_admin_set_bundle_dest_bus(mon->admin);
    lo_bundle_add_message(mon->admin->bundle, admin_msg_strings[ADM_UNMAP], m);

    /* We cannot depend on string arguments sticking around for liblo to
     * serialize later: trigger immediate dispatch. */
    mapper_admin_send_bundle(mon->admin);

    for (i = 0; i < map->num_sources; i++)
        free((char *)src_names[i]);
}

void mmon_now(mapper_monitor mon, mapper_timetag_t *tt)
{
    mapper_clock_now(&mon->admin->clock, tt);
}

void mmon_set_timeout(mapper_monitor mon, int timeout_sec)
{
    if (timeout_sec < 0)
        timeout_sec = ADMIN_TIMEOUT_SEC;
    mon->timeout_sec = timeout_sec;
}

int mmon_get_timeout(mapper_monitor mon)
{
    return mon->timeout_sec;
}

void mmon_flush_db(mapper_monitor mon, int timeout_sec, int quiet)
{
    mapper_clock_now(&mon->admin->clock, &mon->admin->clock.now);

    // flush expired device records
    mapper_db_device dev;
    uint32_t last_ping = mon->admin->clock.now.sec - timeout_sec;
    while ((dev = mapper_db_get_expired_device(&mon->db, last_ping))) {
        // also need to remove subscriptions
        mapper_subscription *s = &mon->subscriptions;
        while (*s) {
            if ((*s)->device == dev) {
                // don't bother sending '/unsubscribe' since device is unresponsive
                // remove from subscriber list
                mapper_subscription temp = *s;
                *s = temp->next;
                free(temp);
            }
            else
                s = &(*s)->next;
        }
        mapper_db_remove_device(&mon->db, dev, quiet);
    }
}

// Forward a message to the admin bus
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
    mapper_admin_set_bundle_dest_bus(mon->admin);
    lo_bundle_add_message(mon->admin->bundle, path, m);
    /* We cannot depend on string arguments sticking around for liblo to
     * serialize later: trigger immediate dispatch. */
    mapper_admin_send_bundle(mon->admin);
}
