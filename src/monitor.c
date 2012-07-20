
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

mapper_monitor mapper_monitor_new(mapper_admin admin, int enable_autorequest)
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
    if (enable_autorequest)
        mapper_monitor_autorequest(mon, 1);
    return mon;
}

void mapper_monitor_free(mapper_monitor mon)
{
    // TODO: free structures pointed to by the database
    if (!mon) {
        if (mon->admin && mon->own_admin)
            mapper_admin_free(mon->admin);
        free(mon);
    }
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

int mapper_monitor_request_signals_by_name(mapper_monitor mon,
                                           const char* name)
{
	char cmd[1024];
	snprintf(cmd, 1024, "%s/signals/get", name);
	mapper_admin_send_osc(mon->admin, cmd, "");
    return 0;
}

int mapper_monitor_request_signals_by_name_and_index(mapper_monitor mon,
                                                     const char* name,
                                                     int start_index,
                                                     int stop_index)
{
	char cmd[1024];
	snprintf(cmd, 1024, "%s/signals/get", name);
	mapper_admin_send_osc(mon->admin, cmd, "ii", start_index, stop_index);
    return 0;
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
    int index;
    lo_type type;
    const lo_arg *value;
    if (!mapper_db_signal_property_lookup(sig, "ID", &type, &value)) {
        if (type == LO_INT32)
            index = value->i32;
        else
            return;
        if (index == (data->total_count - 1)) {
            // signal reporting is complete
            mapper_db_remove_signal_callback(&data->monitor->db,
                                             on_signal_continue_batch_request, data);
            free(data);
            return;
        }
        if (index > 0 && (index % data->batch_size == 0))
            mapper_monitor_request_signals_by_name_and_index(data->monitor,
                                                             data->device->name,
                                                             index + 1,
                                                             index + data->batch_size);
    }
}

int mapper_monitor_batch_request_signals_by_name(mapper_monitor mon,
                                                 const char* name,
                                                 int batch_size)
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
        mapper_monitor_request_signals_by_name(mon, name);
        return 1;
    }

    mapper_db_batch_request data = (mapper_db_batch_request)
                                    malloc(sizeof(struct _mapper_db_batch_request));
    data->monitor = mon;
    data->device = dev;
    data->index = 0;
    data->total_count = signal_count;
    data->batch_size = batch_size;

    mapper_db_add_signal_callback(&mon->db, on_signal_continue_batch_request, data);
    mapper_monitor_request_signals_by_name_and_index(mon, name, 0, batch_size);
    return 0;
}

int mapper_monitor_request_devices(mapper_monitor mon)
{
    mapper_admin_send_osc(mon->admin, "/who", "");
    return 0;
}

int mapper_monitor_request_links_by_name(mapper_monitor mon,
                                         const char* name)
{
	char cmd[1024];
	snprintf(cmd, 1024, "%s/links/get", name);
	mapper_admin_send_osc(mon->admin, cmd, "");
    return 0;
}

int mapper_monitor_request_connections_by_name(mapper_monitor mon,
                                               const char* name)
{
	char cmd[1024];
	snprintf(cmd, 1024, "%s/connections/get", name);
	mapper_admin_send_osc(mon->admin, cmd, "");
    return 0;
}

int mapper_monitor_request_connections_by_name_and_index(mapper_monitor mon,
                                                         const char* name,
                                                         int start_index,
                                                         int stop_index)
{
	char cmd[1024];
	snprintf(cmd, 1024, "%s/connections/get", name);
	mapper_admin_send_osc(mon->admin, cmd, "ii", start_index, stop_index);
    return 0;
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
    int index;
    lo_type type;
    const lo_arg *value;
    if (!mapper_db_connection_property_lookup(con, "ID", &type, &value)) {
        if (type == LO_INT32)
            index = value->i32;
        else
            return;
        if (index == (data->total_count - 1)) {
            // connection reporting is complete
            mapper_db_remove_connection_callback(&data->monitor->db,
                                                 on_connection_continue_batch_request,
                                                 data);
            free(data);
            return;
        }
        if (index > 0 && (index % data->batch_size == 0))
            mapper_monitor_request_connections_by_name_and_index(data->monitor,
                                                                 data->device->name,
                                                                 index + 1,
                                                                 index + data->batch_size);
    }
}

int mapper_monitor_batch_request_connections_by_name(mapper_monitor mon,
                                                     const char* name,
                                                     int batch_size)
{
    // find the db record of device
    mapper_db_device dev = mapper_db_get_device_by_name(&mon->db, name);
    if (!dev) {
        return 1;
    }

    int connection_count = 0;
    lo_type type;
    const lo_arg *value;
    if (!mapper_db_device_property_lookup(dev, "n_connections", &type, &value)) {
        if (type == LO_INT32)
            connection_count = value->i32;
    }

    if (!connection_count)
        return 1;

    if (connection_count <= batch_size) {
        mapper_monitor_request_connections_by_name(mon, name);
        return 1;
    }

    mapper_db_batch_request data = (mapper_db_batch_request)
    malloc(sizeof(struct _mapper_db_batch_request));
    data->monitor = mon;
    data->device = dev;
    data->index = 0;
    data->total_count = connection_count;
    data->batch_size = batch_size;

    mapper_db_add_connection_callback(&mon->db, on_connection_continue_batch_request, data);
    mapper_monitor_request_connections_by_name_and_index(mon, name, 0, batch_size);
    return 0;
}

void mapper_monitor_link(mapper_monitor mon,
                         const char* source_device, 
                         const char* dest_device)
{
    mapper_admin_send_osc( mon->admin, "/link", "ss",
                           source_device, dest_device );
}

void mapper_monitor_unlink(mapper_monitor mon,
                           const char* source_device, 
                           const char* dest_device)
{
    mapper_admin_send_osc( mon->admin, "/unlink", "ss",
                           source_device, dest_device );
}

void mapper_monitor_connection_modify(mapper_monitor mon,
                                      mapper_db_connection_t *props,
                                      unsigned int props_flags)
{
    if (props) {
        mapper_admin_send_osc( mon->admin, "/connection/modify", "ss",
                               props->src_name, props->dest_name,
                               (props_flags & CONNECTION_CLIPMIN)
                               ? AT_CLIPMIN : -1, props->clip_min,
                               (props_flags & CONNECTION_CLIPMAX)
                               ? AT_CLIPMAX : -1, props->clip_max,
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
        mapper_admin_send_osc( mon->admin, "/connect", "ss",
                               source_signal, dest_signal,
                               (props_flags & CONNECTION_CLIPMIN)
                               ? AT_CLIPMIN : -1, props->clip_min,
                               (props_flags & CONNECTION_CLIPMAX)
                               ? AT_CLIPMAX : -1, props->clip_max,
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
        mapper_admin_send_osc( mon->admin, "/connect", "ss",
                               source_signal, dest_signal );
}

void mapper_monitor_disconnect(mapper_monitor mon,
                               const char* source_signal, 
                               const char* dest_signal)
{
    mapper_admin_send_osc( mon->admin, "/disconnect", "ss",
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
        mapper_monitor_batch_request_signals_by_name(mon, dev->name, 10);
        mapper_monitor_request_links_by_name(mon, dev->name);
        mapper_monitor_batch_request_connections_by_name(mon, dev->name, 10);
    }
}

void mapper_monitor_autorequest(mapper_monitor mon, int enable)
{
    if (enable)
        mapper_db_add_device_callback(&mon->db, on_device_autorequest, mon);
    else
        mapper_db_remove_device_callback(&mon->db, on_device_autorequest, mon);
}
