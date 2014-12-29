
#include <stdlib.h>
#include <unistd.h>
#include <sys/time.h>
#include <stdio.h>
#include <string.h>

#include "config.h"
#include "mapper_internal.h"

#define AUTOSUBSCRIBE_INTERVAL 60

// Helpers: for range info to be known we also need to know data types and lengths
#define SRC_MIN_KNOWN  (  CONNECTION_RANGE_SRC_MIN     \
                        | CONNECTION_SRC_TYPE          \
                        | CONNECTION_SRC_LENGTH )
#define SRC_MAX_KNOWN  (  CONNECTION_RANGE_SRC_MAX     \
                        | CONNECTION_SRC_TYPE          \
                        | CONNECTION_SRC_LENGTH )
#define DEST_MIN_KNOWN (  CONNECTION_RANGE_DEST_MIN    \
                        | CONNECTION_DEST_TYPE         \
                        | CONNECTION_DEST_LENGTH )
#define DEST_MAX_KNOWN (  CONNECTION_RANGE_DEST_MAX    \
                        | CONNECTION_DEST_TYPE         \
                        | CONNECTION_DEST_LENGTH )

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
static void monitor_subscribe_internal(mapper_monitor mon, const char *device_name,
                                       int subscribe_flags, int timeout,
                                       int version);

typedef enum _db_request_direction {
    DIRECTION_IN,
    DIRECTION_OUT,
    DIRECTION_BOTH
} db_request_direction;

mapper_monitor mapper_monitor_new(mapper_admin admin, int autosubscribe_flags)
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
        mapper_monitor_free(mon);
        return NULL;
    }

    mon->timeout_sec = ADMIN_TIMEOUT_SEC;

    mapper_admin_add_monitor(mon->admin, mon);
    if (autosubscribe_flags) {
        mapper_monitor_autosubscribe(mon, autosubscribe_flags);
        mapper_monitor_request_devices(mon);
    }
    return mon;
}

void mapper_monitor_free(mapper_monitor mon)
{
    if (!mon)
        return;

    // remove callbacks now so they won't be called when removing devices
    mapper_db_remove_all_callbacks(&mon->db);

    // unsubscribe from and remove any autorenewing subscriptions
    while (mon->subscriptions) {
        mapper_monitor_unsubscribe(mon, mon->subscriptions->name);
    }

    while (mon->db.registered_devices)
        mapper_db_remove_device_by_name(&mon->db, mon->db.registered_devices->name);
    if (mon->admin) {
        if (mon->own_admin)
            mapper_admin_free(mon->admin);
        else
            mapper_admin_remove_monitor(mon->admin, mon);
    }
    free(mon);
}

int mapper_monitor_poll(mapper_monitor mon, int block_ms)
{
    int ping_time = mon->admin->clock.next_ping;
    int admin_count = mapper_admin_poll(mon->admin);
    mapper_clock_now(&mon->admin->clock, &mon->admin->clock.now);

    // check if any subscriptions need to be renewed
    mapper_monitor_subscription s = mon->subscriptions;
    while (s) {
        if (s->lease_expiration_sec < mon->admin->clock.now.sec) {
            monitor_subscribe_internal(mon, s->name, s->flags,
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
        if (mapper_db_check_device_status(&mon->db,
                                          mon->admin->clock.now.sec
                                          - mon->timeout_sec))
            // if so, flush them
            mapper_monitor_flush_db(mon, 10, 0);
    }

    return admin_count;
}

mapper_db mapper_monitor_get_db(mapper_monitor mon)
{
    return &mon->db;
}

static void mapper_monitor_set_bundle_dest(mapper_monitor mon, const char *name)
{
    // TODO: look up device info, maybe send directly
    mapper_admin_set_bundle_dest_bus(mon->admin);
}

static void monitor_subscribe_internal(mapper_monitor mon, const char *device_name,
                                       int subscribe_flags, int timeout,
                                       int version)
{
    char cmd[1024];
    snprintf(cmd, 1024, "%s/subscribe", device_name);

    mapper_monitor_set_bundle_dest(mon, device_name);
    lo_message m = lo_message_new();
    if (m) {
        if (subscribe_flags & SUB_DEVICE_ALL)
            lo_message_add_string(m, "all");
        else {
            if (subscribe_flags & SUB_DEVICE)
                lo_message_add_string(m, "device");
            if (subscribe_flags & SUB_DEVICE_SIGNALS)
                lo_message_add_string(m, "signals");
            else {
                if (subscribe_flags & SUB_DEVICE_INPUTS)
                    lo_message_add_string(m, "inputs");
                else if (subscribe_flags & SUB_DEVICE_OUTPUTS)
                    lo_message_add_string(m, "outputs");
            }
            if (subscribe_flags & SUB_DEVICE_LINKS)
                lo_message_add_string(m, "links");
            else {
                if (subscribe_flags & SUB_DEVICE_LINKS_IN)
                    lo_message_add_string(m, "links_in");
                else if (subscribe_flags & SUB_DEVICE_LINKS_OUT)
                    lo_message_add_string(m, "links_out");
            }
            if (subscribe_flags & SUB_DEVICE_CONNECTIONS)
                lo_message_add_string(m, "connections");
            else {
                if (subscribe_flags & SUB_DEVICE_CONNECTIONS_IN)
                    lo_message_add_string(m, "connections_in");
                else if (subscribe_flags & SUB_DEVICE_CONNECTIONS_OUT)
                    lo_message_add_string(m, "connections_out");
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

void mapper_monitor_subscribe(mapper_monitor mon, const char *device_name,
                              int subscribe_flags, int timeout)
{
    mapper_db_device found = 0;
    if (timeout < 0) {
        // special case: autorenew subscription lease
        // first check if subscription already exists
        mapper_monitor_subscription s = mon->subscriptions;
        while (s) {
            if (strcmp(device_name, s->name)==0) {
                s->flags = subscribe_flags;
                // subscription already exists; check if monitor has device record
                if ((found = mapper_db_get_device_by_name(&mon->db, device_name)))
                    return;
                break;
            }
            s = s->next;
        }
        if (!s) {
            // store subscription record
            s = malloc(sizeof(struct _mapper_monitor_subscription));
            s->name = strdup(device_name);
            s->flags = subscribe_flags;
            s->next = mon->subscriptions;
            mon->subscriptions = s;
        }

        mapper_clock_now(&mon->admin->clock, &mon->admin->clock.now);
        // leave 10-second buffer for subscription lease
        s->lease_expiration_sec =
            mon->admin->clock.now.sec + AUTOSUBSCRIBE_INTERVAL - 10;

        timeout = AUTOSUBSCRIBE_INTERVAL;
    }

    monitor_subscribe_internal(mon, device_name, subscribe_flags, timeout, 0);
}

static void mapper_monitor_unsubscribe_internal(mapper_monitor mon,
                                                const char *device_name,
                                                int send_message)
{
    char cmd[1024];
    // check if autorenewing subscription exists
    mapper_monitor_subscription *s = &mon->subscriptions;
    while (*s) {
        if (strcmp((*s)->name, device_name)==0) {
            if (send_message) {
                snprintf(cmd, 1024, "%s/unsubscribe", device_name);
                mapper_monitor_set_bundle_dest(mon, device_name);
                lo_message m = lo_message_new();
                if (!m)
                    break;
                lo_bundle_add_message(mon->admin->bundle, cmd, m);
                mapper_admin_send_bundle(mon->admin);
            }
            // remove from subscriber list
            mapper_monitor_subscription temp = *s;
            *s = temp->next;
            if (temp->name)
                free(temp->name);
            free(temp);
            return;
        }
        s = &(*s)->next;
    }
}

void mapper_monitor_unsubscribe(mapper_monitor mon, const char *device_name)
{
    mapper_monitor_unsubscribe_internal(mon, device_name, 1);
}

void mapper_monitor_request_devices(mapper_monitor mon)
{
    mapper_admin_set_bundle_dest_bus(mon->admin);
    mapper_admin_bundle_message(mon->admin, ADM_WHO, 0, "");
}

void mapper_monitor_link(mapper_monitor mon,
                         const char* source_device,
                         const char* dest_device,
                         mapper_db_link_t *props,
                         unsigned int props_flags)
{
    mapper_admin_set_bundle_dest_bus(mon->admin);
    if (props) {
        lo_message m = lo_message_new();
        if (!m)
            return;
        lo_message_add_string(m, source_device);
        lo_message_add_string(m, dest_device);

        lo_send_message(mon->admin->bus_addr, "/link", m);
        free(m);
    }
    else {
        mapper_admin_bundle_message(mon->admin, ADM_LINK, 0, "ss",
                                    source_device, dest_device );
    }
    /* We cannot depend on string arguments sticking around for liblo to
     * serialize later: trigger immediate dispatch. */
    mapper_admin_send_bundle(mon->admin);
}

void mapper_monitor_unlink(mapper_monitor mon,
                           const char* src_device,
                           const char* dest_device)
{
    mapper_monitor_set_bundle_dest(mon, src_device);
    mapper_admin_bundle_message(mon->admin, ADM_UNLINK, 0, "ss",
                                src_device, dest_device);
}

void mapper_monitor_connect(mapper_monitor mon,
                            const char* source_signal,
                            const char* dest_signal,
                            mapper_db_connection_t *props,
                            unsigned int flags)
{
    // TODO: lookup device ip/ports, send directly?
    mapper_admin_set_bundle_dest_bus(mon->admin);
    if (props) {
        mapper_admin_bundle_message(
            mon->admin, ADM_CONNECT, 0, "ss", source_signal, dest_signal,
            (flags & CONNECTION_BOUND_MIN) ? AT_BOUND_MIN : -1, props->bound_min,
            (flags & CONNECTION_BOUND_MAX) ? AT_BOUND_MAX : -1, props->bound_max,
            (flags & SRC_MIN_KNOWN) == SRC_MIN_KNOWN ? AT_SRC_MIN : -1, props,
            (flags & SRC_MAX_KNOWN) == SRC_MAX_KNOWN ? AT_SRC_MAX : -1, props,
            (flags & DEST_MIN_KNOWN) == DEST_MIN_KNOWN ? AT_DEST_MIN : -1, props,
            (flags & DEST_MAX_KNOWN) == DEST_MAX_KNOWN ? AT_DEST_MAX : -1, props,
            (flags & CONNECTION_EXPRESSION) ? AT_EXPRESSION : -1, props->expression,
            (flags & CONNECTION_MODE) ? AT_MODE : -1, props->mode,
            (flags & CONNECTION_MUTED) ? AT_MUTE : -1, props->muted,
            (flags & CONNECTION_SEND_AS_INSTANCE) ? AT_SEND_AS_INSTANCE : -1,
             props->send_as_instance,
            ((flags & CONNECTION_SCOPE_NAMES) && props->scope.size)
             ? AT_SCOPE : -1, props->scope.names);
    }
    else
        mapper_admin_bundle_message(mon->admin, ADM_CONNECT, 0, "ss",
                                    source_signal, dest_signal);
    /* We cannot depend on string arguments sticking around for liblo to
     * serialize later: trigger immediate dispatch. */
    mapper_admin_send_bundle(mon->admin);
}

void mapper_monitor_connection_modify(mapper_monitor mon,
                                      const char* source_signal,
                                      const char* dest_signal,
                                      mapper_db_connection_t *props,
                                      unsigned int flags)
{
    if (!mon || !source_signal || !dest_signal || !props || !flags)
        return;

    mapper_admin_set_bundle_dest_bus(mon->admin);
    mapper_admin_bundle_message(
        mon->admin, ADM_CONNECTION_MODIFY, 0, "ss", source_signal, dest_signal,
        (flags & CONNECTION_BOUND_MIN) ? AT_BOUND_MIN : -1, props->bound_min,
        (flags & CONNECTION_BOUND_MAX) ? AT_BOUND_MAX : -1, props->bound_max,
        (flags & SRC_MIN_KNOWN) == SRC_MIN_KNOWN ? AT_SRC_MIN : -1, props,
        (flags & SRC_MAX_KNOWN) == SRC_MAX_KNOWN ? AT_SRC_MAX : -1, props,
        (flags & DEST_MIN_KNOWN) == DEST_MIN_KNOWN ? AT_DEST_MIN : -1, props,
        (flags & DEST_MAX_KNOWN) == DEST_MAX_KNOWN ? AT_DEST_MAX : -1, props,
        (flags & CONNECTION_EXPRESSION) ? AT_EXPRESSION : -1, props->expression,
        (flags & CONNECTION_MODE) ? AT_MODE : -1, props->mode,
        (flags & CONNECTION_MUTED) ? AT_MUTE : -1, props->muted,
        (flags & CONNECTION_SEND_AS_INSTANCE) ? AT_SEND_AS_INSTANCE : -1,
         props->send_as_instance,
        ((flags & CONNECTION_SCOPE_NAMES) && props->scope.size)
         ? AT_SCOPE : -1, props->scope.names);

    /* We cannot depend on string arguments sticking around for liblo to
     * serialize later: trigger immediate dispatch. */
    mapper_admin_send_bundle(mon->admin);
}

void mapper_monitor_disconnect(mapper_monitor mon,
                               const char* source_signal,
                               const char* dest_signal)
{
    // TODO: lookup device ip/ports, send directly?
    mapper_admin_set_bundle_dest_bus(mon->admin);
    mapper_admin_bundle_message(mon->admin, ADM_DISCONNECT, 0, "ss",
                                source_signal, dest_signal);
}

static void on_device_autosubscribe(mapper_db_device dev,
                                    mapper_db_action_t a,
                                    void *user)
{
    mapper_monitor mon = (mapper_monitor)(user);

    if (a == MDB_NEW) {
        // Subscribe to signals, links, and/or connections for new devices.
        if (mon->autosubscribe)
            mapper_monitor_subscribe(mon, dev->name, mon->autosubscribe, -1);
    }
    else if (a == MDB_REMOVE) {
        mapper_monitor_unsubscribe_internal(mon, dev->name, 0);
    }
}

void mapper_monitor_autosubscribe(mapper_monitor mon, int autosubscribe_flags)
{
    // TODO: remove autorenewing subscription record if necessary
    if (!mon->autosubscribe && autosubscribe_flags)
        mapper_db_add_device_callback(&mon->db, on_device_autosubscribe, mon);
    else if (mon->autosubscribe && !autosubscribe_flags) {
        mapper_db_remove_device_callback(&mon->db, on_device_autosubscribe, mon);
        while (mon->subscriptions) {
            mapper_monitor_unsubscribe_internal(mon, mon->subscriptions->name, 1);
        }
    }
    mon->autosubscribe = autosubscribe_flags;
}

void mapper_monitor_now(mapper_monitor mon, mapper_timetag_t *tt)
{
    mapper_clock_now(&mon->admin->clock, tt);
}

void mapper_monitor_set_timeout(mapper_monitor mon, int timeout_sec)
{
    if (timeout_sec < 0)
        timeout_sec = ADMIN_TIMEOUT_SEC;
    mon->timeout_sec = timeout_sec;
}

int mapper_monitor_get_timeout(mapper_monitor mon)
{
    return mon->timeout_sec;
}

void mapper_monitor_flush_db(mapper_monitor mon, int timeout_sec, int quiet)
{
    mapper_clock_now(&mon->admin->clock, &mon->admin->clock.now);

    // flush expired device records
    mapper_db_flush(&mon->db, mon->admin->clock.now.sec, timeout_sec, quiet);

    // also need to remove subscriptions
    mapper_monitor_subscription *s = &mon->subscriptions;
    while (*s) {
        if (!mapper_db_get_device_by_name(&mon->db, (*s)->name)) {
            // don't bother sending '/unsubscribe' since device is unresponsive
            // remove from subscriber list
            mapper_monitor_subscription temp = *s;
            *s = temp->next;
            if (temp->name)
                free(temp->name);
            free(temp);
        }
        else
            s = &(*s)->next;
    }
}
