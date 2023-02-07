
#ifndef __MAPPER_NETWORK_H__
#define __MAPPER_NETWORK_H__

#include "message.h"

void mpr_net_add_dev(mpr_net n, mpr_local_dev d);

void mpr_net_remove_dev(mpr_net n, mpr_local_dev d);

void mpr_net_poll(mpr_net n);

int mpr_net_init(mpr_net n, const char *iface, const char *group, int port);

void mpr_net_use_bus(mpr_net n);

void mpr_net_use_mesh(mpr_net n, lo_address addr);

void mpr_net_use_subscribers(mpr_net net, mpr_local_dev dev, int type);

void mpr_net_add_msg(mpr_net n, const char *str, net_msg_t cmd, lo_message msg);

void mpr_net_send(mpr_net n);

void mpr_net_free_msgs(mpr_net n);

void mpr_net_free(mpr_net n);

mpr_rtr mpr_net_get_rtr(mpr_net n);

lo_server *mpr_net_get_servers(mpr_net n);

#define NEW_LO_MSG(VARNAME, FAIL)                   \
lo_message VARNAME = lo_message_new();              \
if (!VARNAME) {                                     \
    trace_net("couldn't allocate lo_message\n");    \
    FAIL;                                           \
}

#endif /* __MAPPER_NETWORK_H__ */
