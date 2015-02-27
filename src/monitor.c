
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
static void monitor_subscribe_internal(mapper_monitor mon,
                                       const char *device_name,
                                       int subscribe_flags, int timeout,
                                       int version);

typedef enum _db_request_direction {
    DIRECTION_IN,
    DIRECTION_OUT,
    DIRECTION_BOTH
} db_request_direction;

mapper_monitor mmon_new(mapper_admin admin, int autosubscribe_flags)
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
    if (autosubscribe_flags) {
        mmon_autosubscribe(mon, autosubscribe_flags);
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
        mmon_unsubscribe(mon, mon->subscriptions->name);
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

int mmon_poll(mapper_monitor mon, int block_ms)
{
    int ping_time = mon->admin->clock.next_ping;
    int admin_count = mapper_admin_poll(mon->admin);
    mapper_clock_now(&mon->admin->clock, &mon->admin->clock.now);

    // check if any subscriptions need to be renewed
    mapper_subscription s = mon->subscriptions;
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
        mapper_db_check_device_status(&mon->db,
                                      mon->admin->clock.now.sec - mon->timeout_sec);
    }

    return admin_count;
}

mapper_db mmon_get_db(mapper_monitor mon)
{
    return &mon->db;
}

static void monitor_set_bundle_dest(mapper_monitor mon, const char *name)
{
    // TODO: look up device info, maybe send directly
    mapper_admin_set_bundle_dest_bus(mon->admin);
}

static void monitor_subscribe_internal(mapper_monitor mon,
                                       const char *device_name,
                                       int subscribe_flags, int timeout,
                                       int version)
{
    char cmd[1024];
    snprintf(cmd, 1024, "%s/subscribe", device_name);

    monitor_set_bundle_dest(mon, device_name);
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

static mapper_subscription get_subscription(mapper_monitor mon,
                                            const char *device_name)
{
    mapper_subscription s = mon->subscriptions;
    while (s) {
        if (strcmp(device_name, s->name)==0)
            return s;
        s = s->next;
    }
    return 0;
}

void mmon_subscribe(mapper_monitor mon, const char *device_name,
                    int subscribe_flags, int timeout)
{
    if (timeout == -1) {
        // special case: autorenew subscription lease
        // first check if subscription already exists
        mapper_subscription s = get_subscription(mon, device_name);

        if (!s) {
            // store subscription record
            s = malloc(sizeof(struct _mapper_subscription));
            s->name = strdup(device_name);
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

    monitor_subscribe_internal(mon, device_name, subscribe_flags, timeout, 0);
}

static void monitor_unsubscribe_internal(mapper_monitor mon,
                                         const char *device_name,
                                         int send_message)
{
    char cmd[1024];
    // check if autorenewing subscription exists
    mapper_subscription *s = &mon->subscriptions;
    while (*s) {
        if (strcmp((*s)->name, device_name)==0) {
            if (send_message) {
                snprintf(cmd, 1024, "%s/unsubscribe", device_name);
                monitor_set_bundle_dest(mon, device_name);
                lo_message m = lo_message_new();
                if (!m)
                    break;
                lo_bundle_add_message(mon->admin->bundle, cmd, m);
                mapper_admin_send_bundle(mon->admin);
            }
            // remove from subscriber list
            mapper_subscription temp = *s;
            *s = temp->next;
            if (temp->name)
                free(temp->name);
            free(temp);
            return;
        }
        s = &(*s)->next;
    }
}

void mmon_unsubscribe(mapper_monitor mon, const char *device_name)
{
    monitor_unsubscribe_internal(mon, device_name, 1);
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

static int bitmatch(unsigned int a, unsigned int b)
{
    return (a & b) == b;
}

void mmon_connect_signals_by_name(mapper_monitor mon, int num_sources,
                                  const char **source_names,
                                  const char *dest_name,
                                  mapper_db_connection_t *props,
                                  unsigned int flags)
{
    if (!mon || !num_sources || !source_names || !dest_name)
        return;

    lo_message m = lo_message_new();
    if (!m)
        return;

    int i;
    for (i = 0; i < num_sources; i++)
        lo_message_add_string(m, source_names[i]);
    lo_message_add_string(m, "->");
    lo_message_add_string(m, dest_name);

    if (props && flags) {
        prep_varargs(m,
                     (flags & CONNECTION_BOUND_MIN) ? AT_BOUND_MIN : -1,
                      props->bound_min,
                     (flags & CONNECTION_BOUND_MAX) ? AT_BOUND_MAX : -1,
                      props->bound_max,
                     bitmatch(flags, CONNECTION_SRC_MIN_KNOWN)
                      ? AT_SRC_MIN : -1, props,
                     bitmatch(flags, CONNECTION_SRC_MAX_KNOWN)
                      ? AT_SRC_MAX : -1, props,
                     bitmatch(flags, CONNECTION_DEST_MIN_KNOWN)
                      ? AT_DEST_MIN : -1, props,
                     bitmatch(flags, CONNECTION_DEST_MAX_KNOWN)
                      ? AT_DEST_MAX : -1, props,
                     (flags & CONNECTION_EXPRESSION) ? AT_EXPRESSION : -1,
                      props->expression,
                     (flags & CONNECTION_MODE) ? AT_MODE : -1, props->mode,
                     (flags & CONNECTION_MUTED) ? AT_MUTE : -1, props->muted,
                     (flags & CONNECTION_SEND_AS_INSTANCE)
                      ? AT_SEND_AS_INSTANCE : -1, props->send_as_instance,
                     ((flags & CONNECTION_SCOPE_NAMES) && props->scope.size)
                      ? AT_SCOPE : -1, props->scope.names);
    }

    // TODO: lookup device ip/ports, send directly?
    mapper_admin_set_bundle_dest_bus(mon->admin);
    lo_bundle_add_message(mon->admin->bundle, admin_msg_strings[ADM_CONNECT], m);

    /* We cannot depend on string arguments sticking around for liblo to
     * serialize later: trigger immediate dispatch. */
    mapper_admin_send_bundle(mon->admin);
}

void mmon_connect_signals_by_db_record(mapper_monitor mon, int num_sources,
                                       mapper_db_signal_t **sources,
                                       mapper_db_signal_t *dest,
                                       mapper_db_connection_t *props,
                                       unsigned int flags)
{
    if (!mon || !num_sources || !sources || !dest)
        return;

    const char *src_names[num_sources];
    int i;
    for (i = 0; i < num_sources; i++) {
        char src_name[256];
        snprintf(src_name, 256, "%s%s", sources[i]->device->name,
                 sources[i]->name);
        src_names[i] = strdup(src_name);
    }
    char dest_name[256];
    snprintf(dest_name, 256, "%s%s", dest->device->name, dest->name);

    mmon_connect_signals_by_name(mon, num_sources, src_names, dest_name,
                                 props, flags);

    for (i = 0; i < num_sources; i++)
        free((char *)src_names[i]);
}

void mmon_modify_connection_by_signal_names(mapper_monitor mon, int num_sources,
                                            const char **source_names,
                                            const char *dest_name,
                                            mapper_db_connection_t *props,
                                            unsigned int flags)
{
    if (!mon || !num_sources || !source_names || !dest_name || !props || !flags)
        return;

    lo_message m = lo_message_new();
    if (!m)
        return;

    int i;
    for (i = 0; i < num_sources; i++)
        lo_message_add_string(m, source_names[i]);
    lo_message_add_string(m, "->");
    lo_message_add_string(m, dest_name);

    prep_varargs(m,
                 (flags & CONNECTION_BOUND_MIN) ? AT_BOUND_MIN : -1,
                 props->bound_min,
                 (flags & CONNECTION_BOUND_MAX) ? AT_BOUND_MAX : -1,
                 props->bound_max,
                 bitmatch(flags, CONNECTION_SRC_MIN_KNOWN)
                 ? AT_SRC_MIN : -1, props,
                 bitmatch(flags, CONNECTION_SRC_MAX_KNOWN)
                 ? AT_SRC_MAX : -1, props,
                 bitmatch(flags, CONNECTION_DEST_MIN_KNOWN)
                 ? AT_DEST_MIN : -1, props,
                 bitmatch(flags, CONNECTION_DEST_MAX_KNOWN)
                 ? AT_DEST_MAX : -1, props,
                 (flags & CONNECTION_EXPRESSION) ? AT_EXPRESSION : -1,
                 props->expression,
                 (flags & CONNECTION_MODE) ? AT_MODE : -1, props->mode,
                 (flags & CONNECTION_MUTED) ? AT_MUTE : -1, props->muted,
                 (flags & CONNECTION_SEND_AS_INSTANCE)
                 ? AT_SEND_AS_INSTANCE : -1, props->send_as_instance,
                 ((flags & CONNECTION_SCOPE_NAMES) && props->scope.size)
                 ? AT_SCOPE : -1, props->scope.names);

    // TODO: lookup device ip/ports, send directly?
    mapper_admin_set_bundle_dest_bus(mon->admin);
    lo_bundle_add_message(mon->admin->bundle,
                          admin_msg_strings[ADM_CONNECTION_MODIFY], m);

    /* We cannot depend on string arguments sticking around for liblo to
     * serialize later: trigger immediate dispatch. */
    mapper_admin_send_bundle(mon->admin);
}

void mmon_modify_connection_by_signal_db_records(mapper_monitor mon,
                                                 int num_sources,
                                                 mapper_db_signal_t **sources,
                                                 mapper_db_signal_t *dest,
                                                 mapper_db_connection_t *props,
                                                 unsigned int flags)
{
    if (!mon || !num_sources || !sources || !dest || !props || !flags)
        return;

    const char *src_names[num_sources];
    int i;
    for (i = 0; i < num_sources; i++) {
        char src_name[256];
        snprintf(src_name, 256, "%s%s", sources[i]->device->name,
                 sources[i]->name);
        src_names[i] = strdup(src_name);
    }
    char dest_name[256];
    snprintf(dest_name, 256, "%s%s", dest->device->name, dest->name);

    mmon_modify_connection_by_signal_names(mon, num_sources, src_names,
                                           dest_name, props, flags);

    for (i = 0; i < num_sources; i++)
        free((char *)src_names[i]);
}

void mmon_modify_connection(mapper_monitor mon, mapper_db_connection_t *props,
                            unsigned int flags)
{
    if (!mon || !props || !props->num_sources || !props->sources || !flags)
        return;

    const char *src_names[props->num_sources];
    int i;
    for (i = 0; i < props->num_sources; i++) {
        char src_name[256];
        snprintf(src_name, 256, "%s%s", props->sources[i].device_name,
                 props->sources[i].signal_name);
        src_names[i] = strdup(src_name);
    }
    char dest_name[256];
    snprintf(dest_name, 256, "%s%s", props->destination.device_name,
             props->destination.signal_name);

    mmon_modify_connection_by_signal_names(mon, props->num_sources, src_names,
                                           dest_name, props, flags);

    for (i = 0; i < props->num_sources; i++)
        free((char *)src_names[i]);
}

void mmon_disconnect_signals_by_name(mapper_monitor mon, int num_sources,
                                     const char **sources, const char *dest)
{
    if (!mon || !num_sources || !sources || !dest)
        return;

    lo_message m = lo_message_new();
    if (!m)
        return;

    int i;
    for (i = 0; i < num_sources; i++)
        lo_message_add_string(m, sources[i]);
    lo_message_add_string(m, "->");
    lo_message_add_string(m, dest);

    // TODO: lookup device ip/ports, send directly?
    mapper_admin_set_bundle_dest_bus(mon->admin);
    lo_bundle_add_message(mon->admin->bundle, admin_msg_strings[ADM_DISCONNECT], m);

    /* We cannot depend on string arguments sticking around for liblo to
     * serialize later: trigger immediate dispatch. */
    mapper_admin_send_bundle(mon->admin);
}

void mmon_disconnect_signals_by_db_record(mapper_monitor mon, int num_sources,
                                          mapper_db_signal_t **sources,
                                          mapper_db_signal_t *dest)
{
    if (!mon || !num_sources || !sources || !dest)
        return;

    const char *src_names[num_sources];
    int i;
    for (i = 0; i < num_sources; i++) {
        char src_name[256];
        snprintf(src_name, 256, "%s%s", sources[i]->device->name,
                 sources[i]->name);
        src_names[i] = strdup(src_name);
    }
    char dest_name[256];
    snprintf(dest_name, 256, "%s%s", dest->device->name, dest->name);

    mmon_disconnect_signals_by_name(mon, num_sources, src_names, dest_name);

    for (i = 0; i < num_sources; i++)
        free((char *)src_names[i]);
}

void mmon_remove_connection(mapper_monitor mon, mapper_db_connection_t *c)
{
    if (!mon || !c || !c->num_sources || !c->sources)
        return;

    const char *src_names[c->num_sources];
    int i;
    for (i = 0; i < c->num_sources; i++) {
        char src_name[256];
        snprintf(src_name, 256, "%s%s", c->sources[i].device_name,
                 c->sources[i].signal_name);
        src_names[i] = strdup(src_name);
    }
    char dest_name[256];
    snprintf(dest_name, 256, "%s%s", c->destination.device_name,
             c->destination.signal_name);

    mmon_disconnect_signals_by_name(mon, c->num_sources, src_names, dest_name);

    for (i = 0; i < c->num_sources; i++)
        free((char *)src_names[i]);
}

static void on_device_autosubscribe(mapper_db_device dev,
                                    mapper_db_action_t a,
                                    void *user)
{
    mapper_monitor mon = (mapper_monitor)(user);

    // New subscriptions are handled in admin.c as response to device "sync" msg
    if (a == MDB_REMOVE) {
        monitor_unsubscribe_internal(mon, dev->name, 0);
    }
}

void mmon_autosubscribe(mapper_monitor mon, int autosubscribe_flags)
{
    // TODO: remove autorenewing subscription record if necessary
    if (!mon->autosubscribe && autosubscribe_flags)
        mapper_db_add_device_callback(&mon->db, on_device_autosubscribe, mon);
    else if (mon->autosubscribe && !autosubscribe_flags) {
        mapper_db_remove_device_callback(&mon->db, on_device_autosubscribe, mon);
        while (mon->subscriptions) {
            monitor_unsubscribe_internal(mon, mon->subscriptions->name, 1);
        }
    }
    mon->autosubscribe = autosubscribe_flags;
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
    mapper_db_flush(&mon->db, mon->admin->clock.now.sec, timeout_sec, quiet);

    // also need to remove subscriptions
    mapper_subscription *s = &mon->subscriptions;
    while (*s) {
        if (!mapper_db_get_device_by_name(&mon->db, (*s)->name)) {
            // don't bother sending '/unsubscribe' since device is unresponsive
            // remove from subscriber list
            mapper_subscription temp = *s;
            *s = temp->next;
            if (temp->name)
                free(temp->name);
            free(temp);
        }
        else
            s = &(*s)->next;
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
