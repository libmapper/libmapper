
#ifndef __MPR_NETWORK_H__
#define __MPR_NETWORK_H__

typedef struct _mpr_net *mpr_net;

#include "message.h"

/*! A structure that keeps information about network communications. */
typedef struct _mpr_net {
    struct _mpr_graph *graph;

    lo_server servers[2];

    struct {
        lo_address bus;             /*!< LibLo address for the multicast bus. */
        lo_address dst;
        struct _mpr_local_dev *dev;
        char *url;
    } addr;

    struct {
        char *name;                 /*!< The name of the network interface. */
        struct in_addr addr;        /*!< The IP address of network interface. */
    } iface;

    struct _mpr_local_dev **devs;   /*!< Local devices managed by this network structure. */
    lo_bundle bundle;               /*!< Bundle pointer for sending messages on the multicast bus. */
    mpr_time bundle_time;

    struct {
        char *group;
        int port;
    } multicast;

    int random_id;                  /*!< Random id for allocation speedup. */
    int msg_type;
    int num_devs;
    uint32_t next_bus_ping;
    uint32_t next_sub_ping;
    uint8_t generic_dev_methods_added;
    uint8_t registered;
} mpr_net_t;

int mpr_net_bundle_start(lo_timetag t, void *data);

mpr_time mpr_net_get_bundle_time(mpr_net net);


MPR_INLINE static void mpr_net_set_bundle_time(mpr_net net, mpr_time time)
{
    mpr_net_bundle_start(time, net);
}

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

lo_server *mpr_net_get_servers(mpr_net n);

void mpr_net_send_name_probe(mpr_net net, const char *name);

void mpr_net_send_name_registered(mpr_net net, const char *name, int id, int hint);

void mpr_net_add_dev_methods(mpr_net net, mpr_local_dev dev);

void mpr_net_maybe_send_ping(mpr_net net, int force);

#define NEW_LO_MSG(VARNAME, FAIL)           \
lo_message VARNAME = lo_message_new();      \
if (!VARNAME) {                             \
    trace("couldn't allocate lo_message\n");\
    FAIL;                                   \
}

#endif /* __MPR_NETWORK_H__ */
