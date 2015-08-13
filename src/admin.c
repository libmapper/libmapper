
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
static void subscribe_internal(mapper_admin adm, mapper_device dev,
                               int subscribe_flags, int timeout, int version);
static void unsubscribe_internal(mapper_admin adm, mapper_device dev,
                                 int send_message);

typedef enum _db_request_direction {
    DIRECTION_IN,
    DIRECTION_OUT,
    DIRECTION_BOTH
} db_request_direction;

mapper_admin mapper_admin_new(mapper_network net, int subscribe_flags)
{
    mapper_admin adm = (mapper_admin) calloc(1, sizeof(struct _mapper_admin));

    if (net) {
        adm->network = net;
        adm->own_network = 0;
    }
    else {
        adm->network = mapper_network_new(0, 0, 0);
        adm->own_network = 1;
    }

    if (!adm->network) {
        mapper_admin_free(adm);
        return NULL;
    }

    adm->network->db.timeout_sec = MAPPER_TIMEOUT_SEC;

    mapper_network_add_admin(adm->network, adm);
    if (subscribe_flags) {
        mapper_admin_subscribe(adm, 0, subscribe_flags, -1);
    }
    return adm;
}

void mapper_admin_free(mapper_admin adm)
{
    if (!adm)
        return;

    // remove callbacks now so they won't be called when removing devices
    mapper_db_remove_all_callbacks(&adm->network->db);

    // unsubscribe from and remove any autorenewing subscriptions
    while (adm->subscriptions) {
        mapper_admin_unsubscribe(adm, adm->subscriptions->device);
    }

    mapper_device *devs = mapper_db_devices(&adm->network->db);
    while (devs) {
        mapper_device dev = *devs;
        devs = mapper_device_query_next(devs);
        if (!dev->local)
            mapper_db_remove_device(&adm->network->db, dev, 1);
    }

    if (adm->own_network) {
        if (adm->network->device)
            adm->network->device->local->own_network = 1;
        else
            mapper_network_free(adm->network);
    }
    else
        mapper_network_remove_admin(adm->network, adm);

    free(adm);
}

int mapper_admin_poll(mapper_admin adm, int block_ms)
{
    int ping_time = adm->network->clock.next_ping;
    int admin_count = mapper_network_poll(adm->network);
    mapper_clock_now(&adm->network->clock, &adm->network->clock.now);

    // check if any subscriptions need to be renewed
    mapper_subscription s = adm->subscriptions;
    while (s) {
        if (s->lease_expiration_sec < adm->network->clock.now.sec) {
            subscribe_internal(adm, s->device, s->flags,
                               AUTOSUBSCRIBE_INTERVAL, -1);
            // leave 10-second buffer for subscription renewal
            s->lease_expiration_sec =
                adm->network->clock.now.sec + AUTOSUBSCRIBE_INTERVAL - 10;
        }
        s = s->next;
    }

    if (block_ms) {
        double then = current_time();
        while ((current_time() - then)*1000 < block_ms) {
            admin_count += mapper_network_poll(adm->network);
#ifdef WIN32
            Sleep(block_ms);
#else
            usleep(block_ms * 100);
#endif
        }
    }

    if (ping_time != adm->network->clock.next_ping) {
        // some housekeeping: check if any devices have timed out
        mapper_db_check_device_status(&adm->network->db,
                                      adm->network->clock.now.sec);
    }

    return admin_count;
}

mapper_db mapper_admin_db(mapper_admin adm)
{
    return &adm->network->db;
}

static void set_bundle_dest(mapper_admin adm, mapper_device dev)
{
    // TODO: look up device info, maybe send directly
    mapper_network_set_bundle_dest_bus(adm->network);
}

static void subscribe_internal(mapper_admin adm, mapper_device dev,
                               int subscribe_flags, int timeout, int version)
{
    char cmd[1024];
    snprintf(cmd, 1024, "/%s/subscribe", dev->name);

    set_bundle_dest(adm, dev);
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
        lo_bundle_add_message(adm->network->bundle, cmd, m);
        mapper_network_send_bundle(adm->network);
    }
}

static mapper_subscription subscription(mapper_admin adm, mapper_device dev)
{
    mapper_subscription s = adm->subscriptions;
    while (s) {
        if (s->device == dev)
            return s;
        s = s->next;
    }
    return 0;
}

static void on_device_autosubscribe(mapper_device dev, mapper_record_action a,
                                    const void *user)
{
    mapper_admin adm = (mapper_admin)(user);

    // New subscriptions are handled in network.c as response to "sync" msg
    if (a == MAPPER_REMOVED) {
        unsubscribe_internal(adm, dev, 0);
    }
}

static void mapper_admin_autosubscribe(mapper_admin adm, int autosubscribe_flags)
{
    // TODO: remove autorenewing subscription record if necessary
    if (!adm->autosubscribe && autosubscribe_flags) {
        mapper_db_add_device_callback(&adm->network->db, on_device_autosubscribe,
                                      adm);
        mapper_admin_request_devices(adm);
    }
    else if (adm->autosubscribe && !autosubscribe_flags) {
        mapper_db_remove_device_callback(&adm->network->db,
                                         on_device_autosubscribe, adm);
        while (adm->subscriptions) {
            unsubscribe_internal(adm, adm->subscriptions->device, 1);
        }
    }
    adm->autosubscribe = autosubscribe_flags;
}

void mapper_admin_subscribe(mapper_admin adm, mapper_device dev,
                            int subscribe_flags, int timeout)
{
    if (!dev) {
        mapper_admin_autosubscribe(adm, subscribe_flags);
        return;
    }
    if (timeout == -1) {
        // special case: autorenew subscription lease
        // first check if subscription already exists
        mapper_subscription s = subscription(adm, dev);

        if (!s) {
            // store subscription record
            s = malloc(sizeof(struct _mapper_subscription));
            s->device = dev;
            s->next = adm->subscriptions;
            adm->subscriptions = s;
        }
        s->flags = subscribe_flags;

        mapper_clock_now(&adm->network->clock, &adm->network->clock.now);
        // leave 10-second buffer for subscription lease
        s->lease_expiration_sec =
            adm->network->clock.now.sec + AUTOSUBSCRIBE_INTERVAL - 10;

        timeout = AUTOSUBSCRIBE_INTERVAL;
    }

    subscribe_internal(adm, dev, subscribe_flags, timeout, 0);
}

static void unsubscribe_internal(mapper_admin adm, mapper_device dev,
                                 int send_message)
{
    char cmd[1024];
    // check if autorenewing subscription exists
    mapper_subscription *s = &adm->subscriptions;
    while (*s) {
        if ((*s)->device == dev) {
            if (send_message) {
                snprintf(cmd, 1024, "/%s/unsubscribe", dev->name);
                set_bundle_dest(adm, dev);
                lo_message m = lo_message_new();
                if (!m)
                    break;
                lo_bundle_add_message(adm->network->bundle, cmd, m);
                mapper_network_send_bundle(adm->network);
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

void mapper_admin_unsubscribe(mapper_admin adm, mapper_device dev)
{
    if (!dev)
        mapper_admin_autosubscribe(adm, SUBSCRIBE_NONE);
    unsubscribe_internal(adm, dev, 1);
}

void mapper_admin_request_devices(mapper_admin adm)
{
    lo_message msg = lo_message_new();
    if (!msg) {
        trace("couldn't allocate lo_message\n");
        return;
    }
    mapper_network_set_bundle_dest_bus(adm->network);
    lo_bundle_add_message(adm->network->bundle,
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

mapper_map mapper_admin_add_map(mapper_admin adm, int num_sources,
                                mapper_signal *sources,
                                mapper_signal destination)
{
    int i;
    if (num_sources <= 0 || num_sources > MAX_NUM_MAP_SOURCES
        || !sources || !destination)
        return 0;

    // TODO: do not allow "unregistered" signals to be mapped

    // check if record of map already exists
    mapper_map *maps, *temp;
    maps = mapper_db_signal_maps(&adm->network->db, destination, MAPPER_INCOMING);
    for (i = 0; i < num_sources; i++) {
        temp = mapper_db_signal_maps(&adm->network->db, sources[i], MAPPER_OUTGOING);
        maps = mapper_map_query_intersection(maps, temp);
    }
    while (maps) {
        if ((*maps)->num_sources == num_sources) {
            mapper_map map = *maps;
            mapper_map_query_done(maps);
            return map;
        }
        maps = mapper_map_query_next(maps);
    }

    int order[num_sources];
    if (alphabetise_signals(num_sources, sources, order)) {
        trace("error in mapper_admin_add_map(): multiple use of source signal.\n");
        return 0;
    }

    // place in map staging area
    // TODO: allow building multiple maps in parallel
    if (adm->staged_map) {
        mapper_admin_update_map(adm, adm->staged_map);
        free(adm->staged_map->sources);
        free(adm->staged_map);
    }
    adm->staged_map = (mapper_map) calloc(1, sizeof(struct _mapper_map));
    adm->staged_map->sources = ((mapper_slot)
                                calloc(1, sizeof(struct _mapper_slot)
                                       * num_sources));
    adm->staged_map->num_sources = num_sources;
    for (i = 0; i < num_sources; i++) {
        adm->staged_map->sources[i].signal = sources[order[i]];
        adm->staged_map->sources[i].map = adm->staged_map;
    }
    adm->staged_map->destination.signal = destination;
    adm->staged_map->destination.map = adm->staged_map;
    adm->staged_map->extra = table_new();
    adm->staged_map->updater = table_new();
    return adm->staged_map;
}

void mapper_admin_update_map(mapper_admin adm, mapper_map map)
{
    if (!adm || !map)
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
    mapper_network_set_bundle_dest_bus(adm->network);
    lo_bundle_add_message(adm->network->bundle,
                          network_message_strings[map->id ? MSG_MODIFY_MAP : MSG_MAP], m);

    /* We cannot depend on string arguments sticking around for liblo to
     * serialize later: trigger immediate dispatch. */
    mapper_network_send_bundle(adm->network);

    for (i = 0; i < map->num_sources; i++) {
        free((char *)src_names[i]);
    }
    table_clear(map->updater);

    if (map == adm->staged_map) {
        if (map->sources)
            free(map->sources);
        table_free(map->updater);
        table_free(map->extra);
        free(map);
        adm->staged_map = 0;
    }
}

void mapper_admin_remove_map(mapper_admin adm, mapper_map map)
{
    if (!adm || !map || !map->num_sources || !map->sources)
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
    mapper_network_set_bundle_dest_bus(adm->network);
    lo_bundle_add_message(adm->network->bundle,
                          network_message_strings[MSG_UNMAP], m);

    /* We cannot depend on string arguments sticking around for liblo to
     * serialize later: trigger immediate dispatch. */
    mapper_network_send_bundle(adm->network);

    for (i = 0; i < map->num_sources; i++)
        free((char *)src_names[i]);
}

void mapper_admin_now(mapper_admin adm, mapper_timetag_t *tt)
{
    mapper_clock_now(&adm->network->clock, tt);
}

// Forward a message to the multicast bus
void mapper_admin_send(mapper_admin adm, const char *path, const char *types, ...)
{
    if (!adm || !path || !types)
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
    mapper_network_set_bundle_dest_bus(adm->network);
    lo_bundle_add_message(adm->network->bundle, path, m);
    /* We cannot depend on string arguments sticking around for liblo to
     * serialize later: trigger immediate dispatch. */
    mapper_network_send_bundle(adm->network);
}
