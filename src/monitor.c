
#include <stdlib.h>

#include "mapper_internal.h"

mapper_monitor mapper_monitor_new()
{
    mapper_monitor mon = (mapper_monitor)
        calloc(1, sizeof(struct _mapper_monitor));
    mon->admin = mapper_admin_new(0, 0, 0);
    mapper_admin_add_monitor(mon->admin, mon);
    return mon;
}

void mapper_monitor_free(mapper_monitor mon)
{
    if (mon)
        free(mon);
}

void mapper_monitor_poll(mapper_monitor mon, int block_ms)
{
    mapper_admin_poll(mon->admin);
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
