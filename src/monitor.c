
#include <stdlib.h>
#include <unistd.h>
#include <sys/time.h>
#include <stdio.h>
#include <string.h>

#include "config.h"
#include "mapper_internal.h"

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
void monitor_subscribe_internal(mapper_monitor mon, const char *device_name,
                                int subscribe_flags, int timeout);

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

    mapper_admin_add_monitor(mon->admin, mon);
    if (autosubscribe_flags)
        mapper_monitor_autosubscribe(mon, autosubscribe_flags);
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
    int admin_count = mapper_admin_poll(mon->admin);

    // check if any subscriptions need to be renewed
    mapper_monitor_subscription s = mon->subscriptions;
    if (s) {
        mapper_clock_now(&mon->admin->clock, &mon->admin->clock.now);
    }
    while (s) {
        if (s->lease_expiration_sec < mon->admin->clock.now.sec) {
            monitor_subscribe_internal(mon, s->name, s->flags, 60);
            s->lease_expiration_sec = mon->admin->clock.now.sec + 50;
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

void monitor_subscribe_internal(mapper_monitor mon, const char *device_name,
                                int subscribe_flags, int timeout)
{
    char cmd[1024];
    snprintf(cmd, 1024, "%s/subscribe", device_name);

    mapper_monitor_set_bundle_dest(mon, device_name);
    lo_message m = lo_message_new();
    if (m) {
        lo_message_add_int32(m, timeout);
        if (subscribe_flags & SUB_DEVICE_ALL)
            lo_message_add_string(m, "all");
        else {
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
        lo_bundle_add_message(mon->admin->bundle, cmd, m);
        mapper_admin_send_bundle(mon->admin);
    }
}

void mapper_monitor_subscribe(mapper_monitor mon, const char *device_name,
                              int subscribe_flags, int timeout)
{
    if (timeout == -1) {
        // special case: autorenew subscription lease
        // first check if subscription already exists
        mapper_monitor_subscription s = mon->subscriptions;
        while (s) {
            if (strcmp(device_name, s->name)==0) {
                s->flags = subscribe_flags;
                return;
            }
            s = s->next;
        }
        // store subscription record
        s = malloc(sizeof(struct _mapper_monitor_subscription));
        s->name = strdup(device_name);
        s->flags = subscribe_flags;
        // set timeout for 50 seconds in the future
        mapper_clock_now(&mon->admin->clock, &mon->admin->clock.now);
        s->lease_expiration_sec = mon->admin->clock.now.sec + 50;
        // leave 10-second buffer for subscription lease
        s->next = mon->subscriptions;
        mon->subscriptions = s;
        timeout = 60;
    }

    monitor_subscribe_internal(mon, device_name, subscribe_flags, timeout);
}

void mapper_monitor_unsubscribe(mapper_monitor mon, const char *device_name)
{
    char cmd[1024];
    snprintf(cmd, 1024, "%s/unsubscribe", device_name);
    mapper_monitor_set_bundle_dest(mon, device_name);
    lo_message m = lo_message_new();
    if (m)
        mapper_admin_bundle_message(mon->admin, -1, cmd, "");

    // check if autorenewing subscription exists
    mapper_monitor_subscription *s = &mon->subscriptions;
    while (*s) {
        if (strcmp((*s)->name, device_name)==0) {
            // remove from subscriber list
            mapper_monitor_subscription temp = *s;
            *s = temp->next;
            if (temp->name)
                free(temp->name);
            free(temp);
            continue;
        }
        s = &(*s)->next;
    }
}

void mapper_monitor_link(mapper_monitor mon,
                         const char* source_device,
                         const char* dest_device,
                         mapper_db_link_t *props,
                         unsigned int props_flags)
{
    if (props && (props_flags & LINK_NUM_SCOPES) && props->num_scopes &&
        ((props_flags & LINK_SCOPE_NAMES) || (props_flags & LINK_SCOPE_HASHES))) {
        lo_message m = lo_message_new();
        if (!m)
            return;
        lo_message_add_string(m, source_device);
        lo_message_add_string(m, dest_device);
        lo_message_add_string(m, "@scope");
        int i;
        if (props_flags & LINK_SCOPE_NAMES) {
            for (i=0; i<props->num_scopes; i++) {
                lo_message_add_string(m, props->scope_names[i]);
            }
        }
        else if (props_flags & LINK_SCOPE_HASHES) {
            for (i=0; i<props->num_scopes; i++) {
                lo_message_add_int32(m, props->scope_hashes[i]);
            }
        }

        mapper_monitor_set_bundle_dest(mon, dest_device);

        // TODO: switch scopes to regular props
        lo_send_message(mon->admin->bus_addr, "/link", m);
        free(m);
    }
    else {
        mapper_admin_set_bundle_dest_bus(mon->admin);
        mapper_admin_bundle_message( mon->admin, ADM_LINK, 0, "ss",
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

void mapper_monitor_connection_modify(mapper_monitor mon,
                                      mapper_db_connection_t *props,
                                      unsigned int props_flags)
{
    if (props) {
        // TODO: lookup device ip/ports, send directly?
        mapper_admin_set_bundle_dest_bus(mon->admin);
        mapper_admin_bundle_message(mon->admin, ADM_CONNECTION_MODIFY, 0, "ss",
                                    props->src_name, props->dest_name,
                                    (props_flags & CONNECTION_BOUND_MIN)
                                    ? AT_BOUND_MIN : -1, props->bound_min,
                                    (props_flags & CONNECTION_BOUND_MAX)
                                    ? AT_BOUND_MAX : -1, props->bound_max,
                                    (props_flags & CONNECTION_RANGE_KNOWN)
                                    ? AT_RANGE : -1, &props->range,
                                    (props_flags & CONNECTION_EXPRESSION)
                                    ? AT_EXPRESSION : -1, props->expression,
                                    (props_flags & CONNECTION_MODE)
                                    ? AT_MODE : -1, props->mode,
                                    (props_flags & CONNECTION_MUTED)
                                    ? AT_MUTE : -1, props->muted);
        /* We cannot depend on string arguments sticking around for liblo to
         * serialize later: trigger immediate dispatch. */
        mapper_admin_send_bundle(mon->admin);
    }
}

void mapper_monitor_connect(mapper_monitor mon,
                            const char* source_signal,
                            const char* dest_signal,
                            mapper_db_connection_t *props,
                            unsigned int props_flags)
{
    // TODO: lookup device ip/ports, send directly?
    mapper_admin_set_bundle_dest_bus(mon->admin);
    if (props) {
        mapper_admin_bundle_message(mon->admin, ADM_CONNECT, 0, "ss",
                                    source_signal, dest_signal,
                                    (props_flags & CONNECTION_BOUND_MIN)
                                    ? AT_BOUND_MIN : -1, props->bound_min,
                                    (props_flags & CONNECTION_BOUND_MAX)
                                    ? AT_BOUND_MAX : -1, props->bound_max,
                                    (props_flags & CONNECTION_RANGE_KNOWN)
                                    ? AT_RANGE : -1, &props->range,
                                    (props_flags & CONNECTION_EXPRESSION)
                                    ? AT_EXPRESSION : -1, props->expression,
                                    (props_flags & CONNECTION_MODE)
                                    ? AT_MODE : -1, props->mode,
                                    (props_flags & CONNECTION_MUTED)
                                    ? AT_MUTE : -1, props->muted,
                                    (props_flags & CONNECTION_SEND_AS_INSTANCE)
                                    ? AT_SEND_AS_INSTANCE : -1,
                                    props->send_as_instance);
    }
    else
        mapper_admin_bundle_message(mon->admin, ADM_CONNECT, 0, "ss",
                                    source_signal, dest_signal);
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
    if (a == MDB_NEW)
    {
        mapper_monitor mon = (mapper_monitor)(user);

        // Subscribe to signals, links, connections for new devices.
        // TODO: re-enable batch requests if necessary
        if (mon->autosubscribe)
            mapper_monitor_subscribe(mon, dev->name, mon->autosubscribe, -1);
    }
}

void mapper_monitor_autosubscribe(mapper_monitor mon, int autosubscribe_flags)
{
    // TODO: remove autorenewing subscription record if necessary
    if (autosubscribe_flags)
        mapper_db_add_device_callback(&mon->db, on_device_autosubscribe, mon);
    else
        mapper_db_remove_device_callback(&mon->db, on_device_autosubscribe, mon);
    mon->autosubscribe = autosubscribe_flags;
}

void mapper_monitor_now(mapper_monitor mon, mapper_timetag_t *tt)
{
    mapper_clock_now(&mon->admin->clock, tt);
}
