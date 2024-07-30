
#ifndef __MPR_NETWORK_H__
#define __MPR_NETWORK_H__

typedef struct _mpr_net *mpr_net;

#include <lo/lo.h>

#include "device.h"
#include "graph.h"
#include "mpr_time.h"
#include "util/mpr_inline.h"

typedef enum {
    SERVER_UDP = 0,
    SERVER_TCP = 1
} dev_server_t;

mpr_net mpr_net_new(mpr_graph g);

int mpr_net_bundle_start(lo_timetag t, void *data);

mpr_time mpr_net_get_bundle_time(mpr_net net);


MPR_INLINE static void mpr_net_set_bundle_time(mpr_net net, mpr_time time)
{
    mpr_net_bundle_start(time, net);
}

void mpr_net_add_dev(mpr_net n, mpr_local_dev d);

void mpr_net_remove_dev(mpr_net n, mpr_local_dev d);

int mpr_net_get_num_devs(mpr_net net);

lo_server mpr_net_get_dev_server(mpr_net net, mpr_local_dev dev, dev_server_t idx);

void mpr_net_add_dev_server_method(mpr_net net, mpr_local_dev dev, const char *path,
                                   lo_method_handler h, void *data);

void mpr_net_remove_dev_server_method(mpr_net net, mpr_local_dev dev, const char *path);

int mpr_net_poll(mpr_net n, int block_ms);

int mpr_net_start_polling(mpr_net net, int block_ms);

int mpr_net_stop_polling(mpr_net net);

int mpr_net_init(mpr_net n, const char *iface, const char *group, int port);

void mpr_net_use_bus(mpr_net n);

void mpr_net_use_mesh(mpr_net n, lo_address addr);

void mpr_net_use_subscribers(mpr_net net, mpr_local_dev dev, int type);

void mpr_net_add_msg(mpr_net n, const char *str, net_msg_t cmd, lo_message msg);

void mpr_net_send(mpr_net n);

void mpr_net_free_msgs(mpr_net n);

void mpr_net_free(mpr_net n);

void mpr_net_send_name_probe(mpr_net net, const char *name);

void mpr_net_add_dev_methods(mpr_net net, mpr_local_dev dev);

const char *mpr_net_get_interface(mpr_net net);

const char *mpr_net_get_address(mpr_net net);

#define NEW_LO_MSG(VARNAME, FAIL)           \
lo_message VARNAME = lo_message_new();      \
if (!VARNAME) {                             \
    trace("couldn't allocate lo_message\n");\
    FAIL;                                   \
}

#endif /* __MPR_NETWORK_H__ */
