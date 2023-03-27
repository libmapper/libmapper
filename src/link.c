#include <string.h>
#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <stddef.h>
#include <limits.h>
#include <assert.h>

#include "device.h"
#include "link.h"
#include "mpr_time.h"
#include "network.h"
#include "object.h"
#include "slot.h"
#include "table.h"
#include "util/mpr_debug.h"

#include <mapper/mapper.h>

typedef struct _mpr_bundle {
    lo_bundle udp;
    lo_bundle tcp;
} mpr_bundle_t, *mpr_bundle;

/*! Clock and timing information. */
typedef struct _mpr_sync_time_t {
    lo_timetag time;
    int msg_id;
} mpr_sync_time_t;

typedef struct _mpr_sync_clock_t {
    double rate;
    double offset;
    double latency;
    double jitter;
    mpr_sync_time_t sent;
    mpr_sync_time_t rcvd;
    int new;
} mpr_sync_clock_t, *mpr_sync_clock;

typedef struct _mpr_link {
    mpr_obj_t obj;                      /* always first for type punning */
    mpr_dev devs[2];
    int num_maps[2];

    struct {
        lo_address admin;               /*!< Network address of remote endpoint */
        lo_address udp;                 /*!< Network address of remote endpoint */
        lo_address tcp;                 /*!< Network address of remote endpoint */
    } addr;

    int is_local_only;

    mpr_bundle_t bundles[NUM_BUNDLES];  /*!< Circular buffer to handle interrupts during poll() */

    mpr_sync_clock_t clock;
} mpr_link_t;

size_t mpr_link_get_struct_size()
{
    return sizeof(mpr_link_t);
}

mpr_link mpr_link_new(mpr_local_dev local_dev, mpr_dev remote_dev)
{
    return mpr_graph_add_link(mpr_obj_get_graph((mpr_obj)local_dev), (mpr_dev)local_dev, remote_dev);
}

int mpr_link_get_is_ready(mpr_link link)
{
    return link && link->addr.udp;
}

lo_address mpr_link_get_admin_addr(mpr_link link)
{
    return link->addr.admin;
}

void mpr_link_init(mpr_link link, mpr_graph g, mpr_dev dev1, mpr_dev dev2)
{
    mpr_net net = mpr_graph_get_net(g);
    lo_message msg;
    char cmd[256];

    link->devs[LINK_LOCAL_DEV] = dev1;
    link->devs[LINK_REMOTE_DEV] = dev2;
    link->obj.is_local = mpr_obj_get_is_local((mpr_obj)dev1) || mpr_obj_get_is_local((mpr_obj)dev2);
    link->is_local_only = mpr_obj_get_is_local((mpr_obj)dev1) && mpr_obj_get_is_local((mpr_obj)dev2);

    if (!link->obj.props.synced) {
        mpr_tbl t = link->obj.props.synced = mpr_tbl_new();
        mpr_tbl_link_value(t, MPR_PROP_DEV, 2, MPR_DEV, &link->devs, NON_MODIFIABLE | LOCAL_ACCESS_ONLY);
        mpr_tbl_link_value(t, MPR_PROP_ID, 1, MPR_INT64, &link->obj.id, NON_MODIFIABLE);
        mpr_tbl_link_value(t, MPR_PROP_NUM_MAPS, 2, MPR_INT32, &link->num_maps, NON_MODIFIABLE | INDIRECT);
    }
    if (!link->obj.props.staged)
        link->obj.props.staged = mpr_tbl_new();

    if (!link->obj.id && mpr_obj_get_is_local((mpr_obj)link->devs[LINK_LOCAL_DEV]))
        link->obj.id = mpr_dev_generate_unique_id(link->devs[LINK_LOCAL_DEV]);

    if (link->is_local_only) {
        mpr_link_connect(link, 0, 0, 0);
        return;
    }
    else {
        mpr_time t;
        link->clock.new = 1;
        link->clock.sent.msg_id = 0;
        link->clock.rcvd.msg_id = -1;
        mpr_time_set(&t, MPR_NOW);
        link->clock.rcvd.time.sec = t.sec + 10;
    }
    /* request missing metadata */
    snprintf(cmd, 256, "/%s/subscribe", mpr_dev_get_name(link->devs[LINK_REMOTE_DEV]));

    msg = lo_message_new();
    if (!msg) {
        trace("couldn't allocate lo_message\n");
        return;
    }

    lo_message_add_string(msg, "device");
    mpr_net_use_bus(net);
    mpr_net_add_msg(net, cmd, 0, msg);
    mpr_net_send(net);
}

void mpr_link_connect(mpr_link link, const char *host, int admin_port, int data_port)
{
    if (!link->is_local_only) {
        char str[16];
        mpr_tbl tbl = mpr_obj_get_prop_tbl((mpr_obj)link->devs[LINK_REMOTE_DEV]);
        mpr_tbl_add_record(tbl, MPR_PROP_HOST, NULL, 1, MPR_STR, host, REMOTE_MODIFY);
        mpr_tbl_add_record(tbl, MPR_PROP_PORT, NULL, 1, MPR_INT32, &data_port, REMOTE_MODIFY);
        sprintf(str, "%d", data_port);
        link->addr.udp = lo_address_new(host, str);
        link->addr.tcp = lo_address_new_with_proto(LO_TCP, host, str);
        sprintf(str, "%d", admin_port);
        link->addr.admin = lo_address_new(host, str);
        trace_dev(link->devs[LINK_LOCAL_DEV], "activated link to device '%s' at %s:%d\n",
                  mpr_dev_get_name(link->devs[LINK_REMOTE_DEV]), host, data_port);
    }
    else {
        trace_dev(link->devs[LINK_LOCAL_DEV], "activating link to local device '%s'\n",
                  mpr_dev_get_name(link->devs[LINK_REMOTE_DEV]));
    }
    memset(link->bundles, 0, sizeof(mpr_bundle_t) * NUM_BUNDLES);
    mpr_dev_add_link(link->devs[LINK_LOCAL_DEV], link->devs[LINK_REMOTE_DEV]);
}

void mpr_link_free(mpr_link link)
{
    int i;
    mpr_obj_free(&link->obj);
    if (!mpr_obj_get_is_local((mpr_obj)link->devs[LINK_LOCAL_DEV]))
        return;
    FUNC_IF(lo_address_free, link->addr.admin);
    FUNC_IF(lo_address_free, link->addr.udp);
    FUNC_IF(lo_address_free, link->addr.tcp);
    for (i = 0; i < NUM_BUNDLES; i++) {
        FUNC_IF(lo_bundle_free_recursive, link->bundles[i].udp);
        FUNC_IF(lo_bundle_free_recursive, link->bundles[i].tcp);
    }
    mpr_dev_remove_link(link->devs[LINK_LOCAL_DEV], link->devs[LINK_REMOTE_DEV]);
}

/* note on memory handling of mpr_link_add_msg():
 * message: will be owned, will be freed when done */
void mpr_link_add_msg(mpr_link link, const char *path, lo_message msg, mpr_time t,
                      mpr_proto proto, int idx)
{
    lo_bundle *b;
    RETURN_UNLESS(msg);

    /* add message to existing bundles */
    b = (proto == MPR_PROTO_UDP) ? &link->bundles[idx].udp : &link->bundles[idx].tcp;
    if (!(*b))
        *b = lo_bundle_new(t);
    lo_bundle_add_message(*b, path, msg);
}

/* TODO: pass in bundle index as argument */
/* TODO: interrupt driven signal updates may not be followed by mpr_dev_process_outputs(); in the
 * case where the interrupt has interrupted mpr_dev_poll() these messages will not be dispatched. */
int mpr_link_process_bundles(mpr_link link, mpr_time t, int idx)
{
    int i = 0, num = 0, tmp;
    mpr_bundle b;
    lo_bundle lb;
    RETURN_ARG_UNLESS(link, 0);

    b = &link->bundles[idx];

    if (!link->is_local_only) {
        mpr_local_dev ldev = (mpr_local_dev)link->devs[LINK_LOCAL_DEV];
        if ((lb = b->udp)) {
            b->udp = 0;
            if ((num = lo_bundle_count(lb))) {
                lo_send_bundle_from(link->addr.udp, mpr_local_dev_get_server(ldev, SERVER_UDP), lb);
            }
            lo_bundle_free_recursive(lb);
        }
        if ((lb = b->tcp)) {
            b->tcp = 0;
            if ((tmp = lo_bundle_count(lb))) {
                num += tmp;
                lo_send_bundle_from(link->addr.tcp, mpr_local_dev_get_server(ldev, SERVER_TCP), lb);
            }
            lo_bundle_free_recursive(lb);
        }
    }
    else {
        mpr_net net = mpr_graph_get_net(link->obj.graph);
        /* TODO: alias udp/tcp as array and use loop syntax here to reduce code duplication */
        if ((lb = b->udp)) {
            const char *path;
            b->udp = 0;
            /* set out-of-band timestamp */
            mpr_net_set_bundle_time(net, lo_bundle_get_timestamp(lb));
            /* call handler directly instead of sending over the network */
            num = lo_bundle_count(lb);
            while (i < num) {
                /* find the signal matching the message path */
                /* link stores the device endpoints, but we need to be sure we are using the correct
                 * device for local-only links since they might both have signals named /<path> */
                lo_message m = lo_bundle_get_message(lb, i, &path);
                mpr_sig dst = mpr_dev_get_sig_by_name(link->devs[0], path+1);
                if (dst)
                    mpr_sig_osc_handler(NULL, lo_message_get_types(m), lo_message_get_argv(m),
                                        lo_message_get_argc(m), m, dst);
                ++i;
            }
            lo_bundle_free_recursive(lb);
        }
        if ((lb = b->tcp)) {
            const char *path;
            b->tcp = 0;
            /* set out-of-band timestamp */
            mpr_net_set_bundle_time(net, lo_bundle_get_timestamp(lb));
            /* call handler directly instead of sending over the network */
            num = lo_bundle_count(lb);
            while (i < num) {
                /* find the signal matching the message path */
                /* link stores the device endpoints, but we need to be sure we are using the correct
                 * device for local-only links since they might both have signals named /<path> */
                lo_message m = lo_bundle_get_message(lb, i, &path);
                mpr_sig dst = mpr_dev_get_sig_by_name(link->devs[1], path+1);
                if (dst)
                    mpr_sig_osc_handler(NULL, lo_message_get_types(m), lo_message_get_argv(m),
                                        lo_message_get_argc(m), m, dst);
                ++i;
            }
            lo_bundle_free_recursive(lb);
        }
    }
    return num;
}

static int cmp_qry_maps(const void *context_data, mpr_map map)
{
    mpr_id link_id = *(mpr_id*)context_data;
    return mpr_map_get_has_link_id(map, link_id);
}

mpr_list mpr_link_get_maps(mpr_link link)
{
    /* TODO: remove this after verifying graphs are the same */
    assert(link->obj.graph == mpr_obj_get_graph((mpr_obj)link->devs[0]));

    RETURN_ARG_UNLESS(link, 0);
    /* TODO: can we use link->obj.graph here? */
    return mpr_graph_new_query(link->obj.graph, 1, MPR_MAP, (void*)cmp_qry_maps, "h", link->obj.id);
}

int mpr_link_get_has_maps(mpr_link link, mpr_dir dir)
{
    switch (dir) {
        case MPR_DIR_IN:    return link->num_maps[0];
        case MPR_DIR_OUT:   return link->num_maps[1];
        case MPR_DIR_ANY:   return link->num_maps[0] || link->num_maps[1];
        case MPR_DIR_BOTH:  return link->num_maps[0] && link->num_maps[1];
        default:            return 0;
    }
}

mpr_dev mpr_link_get_dev(mpr_link link, int idx)
{
    return link->devs[idx];
}

void mpr_link_add_map(mpr_link link, int is_src)
{
    ++link->num_maps[is_src];
    if (link->is_local_only)
        link->clock.rcvd.time.sec = 0;
    mpr_tbl_set_is_dirty(link->obj.props.synced, 1);
}

void mpr_link_remove_map(mpr_link link, mpr_map rem)
{
    /* Just update the map count; assume map to be removed has already be delisted from graph */
    int in = 0, out = 0, rev = mpr_obj_get_is_local((mpr_obj)link->devs[0]) ? 0 : 1;
    mpr_list list = mpr_link_get_maps(link);
    while (list) {
        mpr_map map = (mpr_map)*list;
        mpr_slot slot;
        list = mpr_list_get_next(list);
        slot = mpr_map_get_dst_slot(map);
        if (mpr_obj_get_is_local((mpr_obj)mpr_slot_get_sig(slot)))
            ++in;
        else
            ++out;
    }
    link->num_maps[0] = rev ? out : in;
    link->num_maps[1] = rev ? in : out;

    if (link->is_local_only && !in && !out)
        mpr_time_set(&link->clock.rcvd.time, MPR_NOW);
}

void mpr_link_send(mpr_link link, net_msg_t cmd)
{
    NEW_LO_MSG(msg, return);
    lo_message_add_string(msg, mpr_dev_get_name(link->devs[0]));
    lo_message_add_string(msg, "<->");
    lo_message_add_string(msg, mpr_dev_get_name(link->devs[1]));
    mpr_net_add_msg(mpr_graph_get_net(link->obj.graph), 0, cmd, msg);
}

void mpr_link_update_clock(mpr_link link, mpr_time then, mpr_time now,
                           int msg_id, int sent_id, double elapsed_remote)
{
    mpr_sync_clock clk = &link->clock;
    if (sent_id == clk->sent.msg_id) {
        /* total elapsed time since ping sent */
        double elapsed_local = mpr_time_get_diff(now, clk->sent.time);
        /* assume symmetrical latency */
        double latency = (elapsed_local - elapsed_remote) * 0.5;
        /* difference between remote and local clocks (latency compensated) */
        double offset = mpr_time_get_diff(now, then) - latency;

        if (latency < 0) {
            trace("error: link latency %f cannot be < 0.\n", latency);
            latency = 0;
        }

        if (1 == clk->new) {
            clk->offset = offset;
            clk->latency = latency;
            clk->jitter = 0;
            clk->new = 0;
        }
        else {
            clk->jitter = (clk->jitter * 0.9 + fabs(clk->latency - latency) * 0.1);
            if (offset > clk->offset) {
                /* remote time is in the future */
                clk->offset = offset;
            }
            else if (   latency < clk->latency + clk->jitter
                     && latency > clk->latency - clk->jitter) {
                clk->offset = clk->offset * 0.9 + offset * 0.1;
                clk->latency = clk->latency * 0.9 + latency * 0.1;
            }
        }
    }

    /* update sync status */
    if (link->is_local_only)
        return;
    mpr_time_set(&clk->rcvd.time, now);
    clk->rcvd.msg_id = msg_id;
}

int mpr_link_housekeeping(mpr_link link, mpr_time now)
{
    int mapped = mpr_link_get_has_maps(link, MPR_DIR_ANY);
    mpr_sync_clock clk = &link->clock;
    double elapsed = (clk->rcvd.time.sec ? mpr_time_get_diff(now, clk->rcvd.time) : 0);
    mpr_dev local_dev = link->devs[LINK_LOCAL_DEV];
    mpr_dev remote_dev = link->devs[LINK_REMOTE_DEV];

    if (elapsed > TIMEOUT_SEC) {
        if (clk->rcvd.msg_id > 0) {
            if (mapped)
                trace_dev(local_dev, "Lost contact with linked device '%s' "
                          "(%g seconds since sync).\n", mpr_dev_get_name(remote_dev), elapsed);
            /* tentatively mark link as expired */
            clk->rcvd.msg_id = -1;
            clk->rcvd.time.sec = now.sec;
        }
        else
            return 1;
    }

    if (link->is_local_only)
        return 0;

    /* Only send pings if this link has associated maps, ensuring empty
     * links are removed after the ping timeout. */
    if (mapped && mpr_obj_get_prop_as_str((mpr_obj)remote_dev, MPR_PROP_HOST, 0)) {
        mpr_net net = mpr_graph_get_net(link->obj.graph);
        NEW_LO_MSG(msg, ;);
        mpr_net_use_mesh(net, link->addr.admin);
        lo_message_add_int64(msg, mpr_obj_get_id((mpr_obj)local_dev));
        if (++clk->sent.msg_id < 0)
            clk->sent.msg_id = 0;
        lo_message_add_int32(msg, clk->sent.msg_id);
        lo_message_add_int32(msg, clk->rcvd.msg_id);
        lo_message_add_double(msg, elapsed);

        /* need to send immediately */
        mpr_net_add_msg(net, NULL, MSG_PING, msg);
        mpr_time_set(&clk->sent.time, now);
        mpr_net_send(net);
    }
    return 0;
}

int mpr_link_get_dev_dir(mpr_link link, mpr_dev dev)
{
    return dev == link->devs[1];
}
