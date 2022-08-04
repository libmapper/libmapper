#include <string.h>
#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <stddef.h>
#include <limits.h>

#include "mapper_internal.h"
#include "types_internal.h"
#include <mapper/mapper.h>

mpr_link mpr_link_new(mpr_local_dev local_dev, mpr_dev remote_dev)
{
    return mpr_graph_add_link(local_dev->obj.graph, (mpr_dev)local_dev, remote_dev);
}

void mpr_link_init(mpr_link link)
{
    mpr_net net = &link->obj.graph->net;
    mpr_time t;
    lo_message msg;
    char cmd[256];
    if (!link->obj.props.synced) {
        mpr_tbl t = link->obj.props.synced = mpr_tbl_new();
        mpr_tbl_link(t, MPR_PROP_DEV, 2, MPR_DEV, &link->devs, NON_MODIFIABLE | LOCAL_ACCESS_ONLY);
        mpr_tbl_link(t, MPR_PROP_ID, 1, MPR_INT64, &link->obj.id, NON_MODIFIABLE);
        mpr_tbl_link(t, MPR_PROP_NUM_MAPS, 2, MPR_INT32, &link->num_maps, NON_MODIFIABLE | INDIRECT);
    }
    if (!link->obj.props.staged)
        link->obj.props.staged = mpr_tbl_new();

    if (!link->obj.id && link->devs[LOCAL_DEV]->is_local)
        link->obj.id = mpr_dev_generate_unique_id(link->devs[LOCAL_DEV]);

    if (link->is_local_only) {
        mpr_link_connect(link, 0, 0, 0);
        return;
    }
    else {
        link->clock.new = 1;
        link->clock.sent.msg_id = 0;
        link->clock.rcvd.msg_id = -1;
        mpr_time_set(&t, MPR_NOW);
        link->clock.rcvd.time.sec = t.sec + 10;
    }
    /* request missing metadata */
    snprintf(cmd, 256, "/%s/subscribe", link->devs[REMOTE_DEV]->name); /* MSG_SUBSCRIBE */

    msg = lo_message_new();
    if (!msg) {
        trace_net("couldn't allocate lo_message\n");
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
        mpr_tbl_set(link->devs[REMOTE_DEV]->obj.props.synced, MPR_PROP_HOST, NULL, 1,
                    MPR_STR, host, REMOTE_MODIFY);
        mpr_tbl_set(link->devs[REMOTE_DEV]->obj.props.synced, MPR_PROP_PORT, NULL, 1,
                    MPR_INT32, &data_port, REMOTE_MODIFY);
        sprintf(str, "%d", data_port);
        link->addr.udp = lo_address_new(host, str);
        link->addr.tcp = lo_address_new_with_proto(LO_TCP, host, str);
        sprintf(str, "%d", admin_port);
        link->addr.admin = lo_address_new(host, str);
        trace_dev(link->devs[LOCAL_DEV], "activated link to device '%s' at %s:%d\n",
                  link->devs[REMOTE_DEV]->name, host, data_port);
    }
    else {
        trace_dev(link->devs[LOCAL_DEV], "activating link to local device '%s'\n",
                  link->devs[REMOTE_DEV]->name);
    }
    memset(link->bundles, 0, sizeof(mpr_bundle_t) * NUM_BUNDLES);
    mpr_dev_add_link(link->devs[LOCAL_DEV], link->devs[REMOTE_DEV]);
}

void mpr_link_free(mpr_link link)
{
    int i;
    FUNC_IF(mpr_tbl_free, link->obj.props.synced);
    FUNC_IF(mpr_tbl_free, link->obj.props.staged);
    if (!link->devs[LOCAL_DEV]->is_local)
        return;
    FUNC_IF(lo_address_free, link->addr.admin);
    FUNC_IF(lo_address_free, link->addr.udp);
    FUNC_IF(lo_address_free, link->addr.tcp);
    for (i = 0; i < NUM_BUNDLES; i++) {
        FUNC_IF(lo_bundle_free_recursive, link->bundles[i].udp);
        FUNC_IF(lo_bundle_free_recursive, link->bundles[i].tcp);
    }
    mpr_dev_remove_link(link->devs[LOCAL_DEV], link->devs[REMOTE_DEV]);
}

/* note on memory handling of mpr_link_add_msg():
 * message: will be owned, will be freed when done */
void mpr_link_add_msg(mpr_link link, mpr_sig dst, lo_message msg, mpr_time t,
                      mpr_proto proto, int idx)
{
    lo_bundle *b;
    RETURN_UNLESS(msg);
    if (link->devs[0] == link->devs[1])
        proto = MPR_PROTO_UDP;

    /* add message to existing bundles */
    b = (proto == MPR_PROTO_UDP) ? &link->bundles[idx].udp : &link->bundles[idx].tcp;
    if (!(*b))
        *b = lo_bundle_new(t);
    lo_bundle_add_message(*b, dst->path, msg);
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
        mpr_local_dev ldev = (mpr_local_dev)link->devs[LOCAL_DEV];
        if ((lb = b->udp)) {
            b->udp = 0;
            if ((num = lo_bundle_count(lb))) {
                lo_send_bundle_from(link->addr.udp, ldev->servers[SERVER_UDP], lb);
            }
            lo_bundle_free_recursive(lb);
        }
        if ((lb = b->tcp)) {
            b->tcp = 0;
            if ((tmp = lo_bundle_count(lb))) {
                num += tmp;
                lo_send_bundle_from(link->addr.tcp, ldev->servers[SERVER_TCP], lb);
            }
            lo_bundle_free_recursive(lb);
        }
    }
    else if ((lb = b->udp)) {
        const char *path;
        b->udp = 0;
        /* set out-of-band timestamp */
        mpr_dev_bundle_start(lo_bundle_get_timestamp(lb), NULL);
        /* call handler directly instead of sending over the network */
        num = lo_bundle_count(lb);
        while (i < num) {
            lo_message m = lo_bundle_get_message(lb, i, &path);
            /* need to look up signal by path */
            mpr_rtr_sig rs = link->obj.graph->net.rtr->sigs;
            while (rs) {
                if (0 == strcmp(path, rs->sig->path)) {
                    mpr_dev_handler(NULL, lo_message_get_types(m), lo_message_get_argv(m),
                                    lo_message_get_argc(m), m, (void*)rs->sig);
                    break;
                }
                rs = rs->next;
            }
            ++i;
        }
        lo_bundle_free_recursive(lb);
    }
    return num;
}

static int cmp_qry_link_maps(const void *context_data, mpr_map map)
{
    mpr_id link_id = *(mpr_id*)context_data;
    int i;
    for (i = 0; i < map->num_src; i++) {
        if (map->src[i]->link && map->src[i]->link->obj.id == link_id)
            return 1;
    }
    if (map->dst->link && map->dst->link->obj.id == link_id)
        return 1;
    return 0;
}

mpr_list mpr_link_get_maps(mpr_link link)
{
    mpr_list q;
    RETURN_ARG_UNLESS(link && link->devs[0]->obj.graph->maps, 0);
    q = mpr_list_new_query((const void**)&link->devs[0]->obj.graph->maps,
                           (void*)cmp_qry_link_maps, "h", link->obj.id);
    return mpr_list_start(q);
}

void mpr_link_add_map(mpr_link link, int is_src)
{
    ++link->num_maps[is_src];
    if (link->is_local_only)
        link->clock.rcvd.time.sec = 0;
}

void mpr_link_remove_map(mpr_link link, mpr_local_map rem)
{
    int in = 0, out = 0, rev = link->devs[0]->is_local ? 0 : 1;
    mpr_list list = mpr_link_get_maps(link);
    while (list) {
        mpr_local_map map = *(mpr_local_map*)list;
        list = mpr_list_get_next(list);
        if (map == rem)
            continue;
        if (map->dst->is_local && map->dst->rsig)
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
    lo_message_add_string(msg, link->devs[0]->name);
    lo_message_add_string(msg, "<->");
    lo_message_add_string(msg, link->devs[1]->name);
    mpr_net_add_msg(&link->obj.graph->net, 0, cmd, msg);
}

int mpr_link_get_is_local(mpr_link link)
{
    return link->devs[0]->is_local || link->devs[1]->is_local;
}
