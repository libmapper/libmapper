
#include <stdlib.h>
#include <unistd.h>
#include <sys/time.h>

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

mapper_monitor mapper_monitor_new(void)
{
    mapper_monitor mon = (mapper_monitor)
        calloc(1, sizeof(struct _mapper_monitor));
    mon->admin = mapper_admin_new(0, 0, 0);
    mapper_admin_add_monitor(mon->admin, mon);
    return mon;
}

void mapper_monitor_free(mapper_monitor mon)
{
    // TODO: free structures pointed to by the database
    if (mon)
        free(mon);
}

int mapper_monitor_poll(mapper_monitor mon, int block_ms)
{
    int admin_count = mapper_admin_poll(mon->admin);
    if (block_ms) {
        double then = get_current_time();
        while ((get_current_time() - then) < block_ms*1000) {
            admin_count += mapper_admin_poll(mon->admin);
            usleep(block_ms * 100);
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

int mapper_monitor_request_links_by_name(mapper_monitor mon,
                                         const char* name)
{
	char cmd[1024];
	snprintf(cmd, 1024, "%s/links/get", name);
	mapper_admin_send_osc(mon->admin, cmd, "");
    return 0;
}

int mapper_monitor_request_mappings_by_name(mapper_monitor mon,
                                            const char* name)
{
	char cmd[1024];
	snprintf(cmd, 1024, "%s/connections/get", name);
	mapper_admin_send_osc(mon->admin, cmd, "");
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

void mapper_monitor_mapping_modify(mapper_monitor mon,
                            mapper_db_mapping_t *props,
                            unsigned int props_flags)
{

    if (props) {
        mapper_admin_send_osc( mon->admin, "/connection/modify", "ss",
                               props->src_name, props->dest_name,
                               (props_flags & MAPPING_CLIPMIN)
                               ? AT_CLIPMIN : -1, props->clip_min,
                               (props_flags & MAPPING_CLIPMAX)
                               ? AT_CLIPMAX : -1, props->clip_max,
                               (props_flags & MAPPING_RANGE_KNOWN)
                               ? AT_RANGE : -1, &props->range,
                               (props_flags & MAPPING_EXPRESSION)
                               ? AT_EXPRESSION : -1, props->expression,
                               (props_flags & MAPPING_MODE)
                               ? AT_MODE : -1, props->mode,
                               (props_flags & MAPPING_MUTED)
                               ? AT_MUTE : -1, props->muted );
    } else {
        mapper_admin_send_osc( mon->admin, "/connection/modify", "ss",
                               props->src_name, props->dest_name );
	}

}

void mapper_monitor_connect(mapper_monitor mon,
                            const char* source_signal,
                            const char* dest_signal,
                            mapper_db_mapping_t *props,
                            unsigned int props_flags)
{
    if (props) {
        mapper_admin_send_osc( mon->admin, "/connect", "ss",
                               source_signal, dest_signal,
                               (props_flags & MAPPING_CLIPMIN)
                               ? AT_CLIPMIN : -1, props->clip_min,
                               (props_flags & MAPPING_CLIPMAX)
                               ? AT_CLIPMAX : -1, props->clip_max,
                               (props_flags & MAPPING_RANGE_KNOWN)
                               ? AT_RANGE : -1, &props->range,
                               (props_flags & MAPPING_EXPRESSION)
                               ? AT_EXPRESSION : -1, props->expression,
                               (props_flags & MAPPING_MODE)
                               ? AT_MODE : -1, props->mode,
                               (props_flags & MAPPING_MUTED)
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
