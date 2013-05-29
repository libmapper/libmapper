
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

typedef enum _db_request_direction {
    DIRECTION_IN,
    DIRECTION_OUT,
    DIRECTION_BOTH
} db_request_direction;

mapper_monitor mapper_monitor_new(mapper_admin admin,
                                  mapper_monitor_autoreq_mode_t flags)
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
    if (flags)
        mapper_monitor_autorequest(mon, flags);
    return mon;
}

void mapper_monitor_free(mapper_monitor mon)
{
    if (!mon)
        return;

    // remove callbacks now so they won't be called when removing devices
    mapper_db_remove_all_callbacks(&mon->db);

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

static int request_signals_by_device_name_internal(mapper_monitor mon,
                                                   const char* name,
                                                   int direction)
{
	char cmd[1024];
    if (direction == DIRECTION_IN)
        snprintf(cmd, 1024, "%s/signals/input/get", name);
    else if (direction == DIRECTION_OUT)
        snprintf(cmd, 1024, "%s/signals/output/get", name);
    else
        snprintf(cmd, 1024, "%s/signals/get", name);
	mapper_admin_send_osc(mon->admin, 0, cmd, "");
    return 0;
}

int mapper_monitor_request_signals_by_device_name(mapper_monitor mon,
                                                  const char* name)
{
    return request_signals_by_device_name_internal(mon, name, DIRECTION_BOTH);
}

int mapper_monitor_request_input_signals_by_device_name(mapper_monitor mon,
                                                        const char* name)
{
    return request_signals_by_device_name_internal(mon, name, DIRECTION_IN);
}

int mapper_monitor_request_output_signals_by_device_name(mapper_monitor mon,
                                                         const char* name)
{
    return request_signals_by_device_name_internal(mon, name, DIRECTION_OUT);
}

static int request_signal_range_by_device_name_internal(mapper_monitor mon,
                                                        const char* name,
                                                        int start_index,
                                                        int stop_index,
                                                        int direction)
{
	char cmd[1024];
    if (direction == DIRECTION_IN)
        snprintf(cmd, 1024, "%s/signals/input/get", name);
    else if (direction == DIRECTION_OUT)
        snprintf(cmd, 1024, "%s/signals/output/get", name);
    else
        snprintf(cmd, 1024, "%s/signals/get", name);
	mapper_admin_send_osc(mon->admin, 0, cmd, "ii", start_index, stop_index);
    return 0;
}

int mapper_monitor_request_signal_range_by_device_name(mapper_monitor mon,
                                                       const char* name,
                                                       int start_index,
                                                       int stop_index)
{
    return request_signal_range_by_device_name_internal(mon, name,
                                                        start_index,
                                                        stop_index,
                                                        DIRECTION_BOTH);
}

int mapper_monitor_request_input_signal_range_by_device_name(mapper_monitor mon,
                                                             const char* name,
                                                             int start_index,
                                                             int stop_index)
{
    return request_signal_range_by_device_name_internal(mon, name,
                                                        start_index,
                                                        stop_index,
                                                        DIRECTION_IN);
}

int mapper_monitor_request_output_signal_range_by_device_name(mapper_monitor mon,
                                                              const char* name,
                                                              int start_index,
                                                              int stop_index)
{
	return request_signal_range_by_device_name_internal(mon, name,
                                                        start_index,
                                                        stop_index,
                                                        DIRECTION_OUT);
}

static void on_signal_continue_batch_request(mapper_db_signal sig,
                                             mapper_db_action_t a,
                                             void *user)
{
    if (a != MDB_NEW)
        return;

    mapper_db_batch_request data = (mapper_db_batch_request)user;
    if (!data)
        return;

    mapper_db_device dev_to_match = data->device;
    if (strcmp(sig->device_name, dev_to_match->name) != 0)
        return;
    if (sig->id >= (data->total_count - 1)) {
        // signal reporting is complete
        mapper_db_remove_signal_callback(&data->monitor->db,
                                         on_signal_continue_batch_request, data);
        free(data);
        return;
    }
    if (sig->id > 0 && (sig->id % data->batch_size == 0))
        request_signal_range_by_device_name_internal(data->monitor,
                                                     data->device->name,
                                                     sig->id + 1,
                                                     sig->id + data->batch_size,
                                                     data->direction);
}

static int batch_request_signals_by_device_name_internal(mapper_monitor mon,
                                                         const char* name,
                                                         int batch_size,
                                                         int direction)
{
    // find the db record of device
    mapper_db_device dev = mapper_db_get_device_by_name(&mon->db, name);
    if (!dev) {
        return 1;
    }

    int signal_count = 0;
    lo_type type;
    const lo_arg *value;
    if (!mapper_db_device_property_lookup(dev, "n_inputs", &type, &value)) {
        if (type == LO_INT32)
            signal_count = value->i32;
    }
    if (!mapper_db_device_property_lookup(dev, "n_outputs", &type, &value))
        if (type == LO_INT32)
            signal_count = signal_count > value->i32 ? signal_count : value->i32;

    if (!signal_count)
        return 1;

    if (signal_count <= batch_size) {
        request_signals_by_device_name_internal(mon, name, direction);
        return 1;
    }

    mapper_db_batch_request data = (mapper_db_batch_request)
    malloc(sizeof(struct _mapper_db_batch_request));
    data->monitor = mon;
    data->device = dev;
    data->index = 0;
    data->total_count = signal_count;
    data->batch_size = batch_size;
    data->direction = direction;

    mapper_db_add_signal_callback(&mon->db, on_signal_continue_batch_request, data);
    request_signal_range_by_device_name_internal(mon, name, 0, batch_size, direction);
    return 0;
}

int mapper_monitor_batch_request_signals_by_device_name(mapper_monitor mon,
                                                        const char* name,
                                                        int batch_size)
{
    return batch_request_signals_by_device_name_internal(mon, name,
                                                         batch_size,
                                                         DIRECTION_BOTH);
}

int mapper_monitor_batch_request_input_signals_by_device_name(mapper_monitor mon,
                                                              const char* name,
                                                              int batch_size)
{
    return batch_request_signals_by_device_name_internal(mon, name,
                                                         batch_size,
                                                         DIRECTION_IN);
}

int mapper_monitor_batch_request_output_signals_by_device_name(mapper_monitor mon,
                                                               const char* name,
                                                               int batch_size)
{
    return batch_request_signals_by_device_name_internal(mon, name,
                                                         batch_size,
                                                         DIRECTION_OUT);
}

int mapper_monitor_request_devices(mapper_monitor mon)
{
    mapper_admin_send_osc(mon->admin, 0, "/who", "");
    return 0;
}

int mapper_monitor_request_device_info(mapper_monitor mon,
                                       const char* name)
{
    char cmd[1024];
	snprintf(cmd, 1024, "%s/info/get", name);
	mapper_admin_send_osc(mon->admin, 0, cmd, "");
    return 0;
}

int mapper_monitor_request_links_by_device_name(mapper_monitor mon,
                                                const char* name)
{
	char cmd[1024];
	snprintf(cmd, 1024, "%s/links/get", name);
	mapper_admin_send_osc(mon->admin, 0, cmd, "");
    return 0;
}

int mapper_monitor_request_links_by_src_device_name(mapper_monitor mon,
                                                    const char* name)
{
	char cmd[1024];
	snprintf(cmd, 1024, "%s/links/out/get", name);
	mapper_admin_send_osc(mon->admin, 0, cmd, "");
    return 0;
}

int mapper_monitor_request_links_by_dest_device_name(mapper_monitor mon,
                                                    const char* name)
{
	char cmd[1024];
	snprintf(cmd, 1024, "%s/links/in/get", name);
	mapper_admin_send_osc(mon->admin, 0, cmd, "");
    return 0;
}

static int request_connections_by_device_name_internal(mapper_monitor mon,
                                                       const char* name,
                                                       int direction)
{
	char cmd[1024];
    if (direction == DIRECTION_IN)
        snprintf(cmd, 1024, "%s/connections/in/get", name);
    else if (direction == DIRECTION_OUT)
        snprintf(cmd, 1024, "%s/connections/out/get", name);
    else
        snprintf(cmd, 1024, "%s/connections/get", name);
	mapper_admin_send_osc(mon->admin, 0, cmd, "");
    return 0;
}

int mapper_monitor_request_connections_by_device_name(mapper_monitor mon,
                                                      const char* name)
{
    return request_connections_by_device_name_internal(mon, name,
                                                       DIRECTION_BOTH);
}

int mapper_monitor_request_connections_by_src_device_name(mapper_monitor mon,
                                                          const char* name)
{
    return request_connections_by_device_name_internal(mon, name,
                                                       DIRECTION_OUT);
}

int mapper_monitor_request_connections_by_dest_device_name(mapper_monitor mon,
                                                           const char* name)
{
    return request_connections_by_device_name_internal(mon, name,
                                                       DIRECTION_IN);
}

static int request_connection_range_by_device_name_internal(mapper_monitor mon,
                                                            const char* name,
                                                            int start_index,
                                                            int stop_index,
                                                            int direction)
{
	char cmd[1024];
    if (direction == DIRECTION_IN)
        snprintf(cmd, 1024, "%s/connections/in/get", name);
    else if (direction == DIRECTION_OUT)
        snprintf(cmd, 1024, "%s/connections/out/get", name);
    else
        snprintf(cmd, 1024, "%s/connections/get", name);
	mapper_admin_send_osc(mon->admin, 0, cmd, "ii", start_index, stop_index);
    return 0;
}

int mapper_monitor_request_connection_range_by_device_name(mapper_monitor mon,
                                                           const char* name,
                                                           int start_index,
                                                           int stop_index)
{
    return request_connection_range_by_device_name_internal(mon, name,
                                                            start_index,
                                                            stop_index,
                                                            DIRECTION_BOTH);
}

int mapper_monitor_request_connection_range_by_src_device_name(mapper_monitor mon,
                                                               const char* name,
                                                               int start_index,
                                                               int stop_index)
{
    return request_connection_range_by_device_name_internal(mon, name,
                                                            start_index,
                                                            stop_index,
                                                            DIRECTION_OUT);
}

int mapper_monitor_request_connection_range_by_dest_device_name(mapper_monitor mon,
                                                                const char* name,
                                                                int start_index,
                                                                int stop_index)
{
	return request_connection_range_by_device_name_internal(mon, name,
                                                            start_index,
                                                            stop_index,
                                                            DIRECTION_IN);
}

static void on_connection_continue_batch_request(mapper_db_connection con,
                                                 mapper_db_action_t a,
                                                 void *user)
{
    if (a != MDB_NEW)
        return;

    mapper_db_batch_request data = (mapper_db_batch_request)user;
    if (!data)
        return;

    mapper_db_device dev_to_match = data->device;
    if (strcmp(con->src_name, dev_to_match->name) != 0)
        return;
    if (con->id >= (data->total_count - 1)) {
        // connection reporting is complete
        mapper_db_remove_connection_callback(&data->monitor->db,
                                             on_connection_continue_batch_request,
                                             data);
        free(data);
        return;
    }
    if (con->id > 0 && (con->id % data->batch_size == 0))
        request_connection_range_by_device_name_internal(data->monitor,
                                                         data->device->name,
                                                         con->id + 1,
                                                         con->id + data->batch_size,
                                                         data->direction);
}

static int batch_request_connections_by_device_name_internal(mapper_monitor mon,
                                                             const char* name,
                                                             int batch_size,
                                                             int direction)
{
    // find the db record of device
    mapper_db_device dev = mapper_db_get_device_by_name(&mon->db, name);
    if (!dev) {
        return 1;
    }

    int connection_count = 0;
    lo_type type;
    const lo_arg *value;

    if ((direction == DIRECTION_IN || direction == DIRECTION_BOTH) &&
        !mapper_db_device_property_lookup(dev, "n_connections_in", &type, &value)) {
        if (type == LO_INT32)
            connection_count += value->i32;
    }
    if ((direction == DIRECTION_OUT || direction == DIRECTION_BOTH) &&
        !mapper_db_device_property_lookup(dev, "n_connections_out", &type, &value)) {
        if (type == LO_INT32)
            connection_count += value->i32;
    }

    if (!connection_count)
        return 1;

    if (connection_count <= batch_size) {
        request_connections_by_device_name_internal(mon, name, direction);
        return 1;
    }

    mapper_db_batch_request data = (mapper_db_batch_request)
                                    malloc(sizeof(struct _mapper_db_batch_request));
    data->monitor = mon;
    data->device = dev;
    data->index = 0;
    data->total_count = connection_count;
    data->batch_size = batch_size;
    data->direction = direction;

    mapper_db_add_connection_callback(&mon->db, on_connection_continue_batch_request, data);
    request_connection_range_by_device_name_internal(mon, name, 0, batch_size, direction);
    return 0;
}

int mapper_monitor_batch_request_connections_by_device_name(mapper_monitor mon,
                                                            const char* name,
                                                            int batch_size)
{
    return batch_request_connections_by_device_name_internal(mon, name,
                                                             batch_size,
                                                             DIRECTION_BOTH);
}

int mapper_monitor_batch_request_connections_by_src_device_name(mapper_monitor mon,
                                                                const char* name,
                                                                int batch_size)
{
    return batch_request_connections_by_device_name_internal(mon, name,
                                                             batch_size,
                                                             DIRECTION_OUT);
}

int mapper_monitor_batch_request_connections_by_dest_device_name(mapper_monitor mon,
                                                                 const char* name,
                                                                 int batch_size)
{
    return batch_request_connections_by_device_name_internal(mon, name,
                                                             batch_size,
                                                             DIRECTION_IN);
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

        lo_send_message(mon->admin->admin_addr, "/link", m);
        free(m);
    }
    else
        mapper_admin_send_osc( mon->admin, 0, "/link", "ss",
                               source_device, dest_device );
}

void mapper_monitor_unlink(mapper_monitor mon,
                           const char* source_device,
                           const char* dest_device)
{
    mapper_admin_send_osc( mon->admin, 0, "/unlink", "ss",
                           source_device, dest_device );
}

void mapper_monitor_connection_modify(mapper_monitor mon,
                                      mapper_db_connection_t *props,
                                      unsigned int props_flags)
{
    if (props) {
        mapper_admin_send_osc( mon->admin, 0, "/connection/modify", "ss",
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
                               ? AT_MUTE : -1, props->muted );
    }
}

void mapper_monitor_connect(mapper_monitor mon,
                            const char* source_signal,
                            const char* dest_signal,
                            mapper_db_connection_t *props,
                            unsigned int props_flags)
{
    if (props) {
        mapper_admin_send_osc( mon->admin, 0, "/connect", "ss",
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
                               ? AT_MUTE : -1, props->muted );
    }
    else
        mapper_admin_send_osc( mon->admin, 0, "/connect", "ss",
                               source_signal, dest_signal );
}

void mapper_monitor_disconnect(mapper_monitor mon,
                               const char* source_signal,
                               const char* dest_signal)
{
    mapper_admin_send_osc( mon->admin, 0, "/disconnect", "ss",
                           source_signal, dest_signal );
}

static void on_device_autorequest(mapper_db_device dev,
                                  mapper_db_action_t a,
                                  void *user)
{
    if (a == MDB_NEW)
    {
        mapper_monitor mon = (mapper_monitor)(user);

        // Request signals, links, connections for new devices.
        if ((mon->autorequest & AUTOREQ_SIGNALS))
            mapper_monitor_batch_request_signals_by_device_name(mon, dev->name, 10);
        if (mon->autorequest & AUTOREQ_LINKS)
            mapper_monitor_request_links_by_src_device_name(mon, dev->name);
        if (mon->autorequest & AUTOREQ_CONNECTIONS)
            mapper_monitor_batch_request_connections_by_src_device_name(mon, dev->name, 10);
    }
}

void mapper_monitor_autorequest(mapper_monitor mon,
                                mapper_monitor_autoreq_mode_t flags)
{
    if (flags)
        mapper_db_add_device_callback(&mon->db, on_device_autorequest, mon);
    else
        mapper_db_remove_device_callback(&mon->db, on_device_autorequest, mon);
    mon->autorequest = flags;
}

void mapper_monitor_now(mapper_monitor mon, mapper_timetag_t *tt)
{
    mapper_clock_now(&mon->admin->clock, tt);
}
