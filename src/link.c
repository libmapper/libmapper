#include <string.h>
#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <stddef.h>
#include <limits.h>

#include "mapper_internal.h"
#include "types_internal.h"
#include <mapper/mapper.h>

mpr_link mpr_link_new(mpr_dev local_dev, mpr_dev remote_dev)
{
    return mpr_graph_add_link(local_dev->obj.graph, local_dev, remote_dev);
}

void mpr_link_init(mpr_link link)
{
    mpr_net net = &link->obj.graph->net;
    if (!link->num_maps)
        link->num_maps = (int*)calloc(1, sizeof(int) * 2);
    link->obj.props.mask = 0;
    if (!link->obj.props.synced) {
        mpr_tbl t = link->obj.props.synced = mpr_tbl_new();
        mpr_tbl_link(t, MPR_PROP_DEV, 2, MPR_DEV, &link->devs,
                     NON_MODIFIABLE | LOCAL_ACCESS_ONLY);
        mpr_tbl_link(t, MPR_PROP_ID, 1, MPR_INT64, &link->obj.id, NON_MODIFIABLE);
        mpr_tbl_link(t, MPR_PROP_NUM_MAPS, 2, MPR_INT32, &link->num_maps,
                     NON_MODIFIABLE | INDIRECT);
    }
    if (!link->obj.props.staged)
        link->obj.props.staged = mpr_tbl_new();

    if (!link->obj.id && link->local_dev->loc)
        link->obj.id = mpr_dev_generate_unique_id(link->local_dev);

    link->clock.new = 1;
    link->clock.sent.msg_id = 0;
    link->clock.rcvd.msg_id = -1;
    mpr_time t;
    mpr_time_set(&t, MPR_NOW);
    link->clock.rcvd.time.sec = t.sec + 10;

    // request missing metadata
    char cmd[256];
    snprintf(cmd, 256, "/%s/subscribe", link->remote_dev->name);
    NEW_LO_MSG(m, return);
    lo_message_add_string(m, "device");
    mpr_net_use_bus(net);
    mpr_net_add_msg(net, cmd, 0, m);
    mpr_net_send(net);
}

void mpr_link_connect(mpr_link link, const char *host, int admin_port,
                      int data_port)
{
    char str[16];
    mpr_tbl_set(link->remote_dev->obj.props.synced, MPR_PROP_HOST, NULL, 1,
                MPR_STR, host, REMOTE_MODIFY);
    mpr_tbl_set(link->remote_dev->obj.props.synced, MPR_PROP_PORT, NULL, 1,
                MPR_INT32, &data_port, REMOTE_MODIFY);
    sprintf(str, "%d", data_port);
    link->addr.udp = lo_address_new(host, str);
    link->addr.tcp = lo_address_new_with_proto(LO_TCP, host, str);
    sprintf(str, "%d", admin_port);
    link->addr.admin = lo_address_new(host, str);
    trace_dev(link->local_dev, "activated router to device '%s' at %s:%d\n",
              link->remote_dev->name, host, data_port);
    link->bundle.udp[0] = link->bundle.udp[1] = 0;
    link->bundle.tcp[0] = link->bundle.tcp[1] = 0;
    link->bundle.idx = 0;
    mpr_dev_add_link(link->local_dev, link->remote_dev);
}

void mpr_link_free(mpr_link link)
{
    FUNC_IF(mpr_tbl_free, link->obj.props.synced);
    FUNC_IF(mpr_tbl_free, link->obj.props.staged);
    FUNC_IF(free, link->num_maps);
    if (!link->local_dev->loc)
        return;
    FUNC_IF(lo_address_free, link->addr.admin);
    FUNC_IF(lo_address_free, link->addr.udp);
    FUNC_IF(lo_address_free, link->addr.tcp);
    FUNC_IF(lo_bundle_free_recursive, link->bundle.udp[link->bundle.idx]);
    FUNC_IF(lo_bundle_free_recursive, link->bundle.tcp[link->bundle.idx]);
    mpr_dev_remove_link(link->local_dev, link->remote_dev);
}

// note on memory handling of mpr_link_add_msg():
// message: will be owned, will be freed when done
void mpr_link_add_msg(mpr_link link, mpr_sig dst, lo_message msg, mpr_time t, mpr_proto proto)
{
    RETURN_UNLESS(msg);
    if (link->local_dev == link->remote_dev)
        proto = MPR_PROTO_UDP;

    // add message to existing bundles
    unsigned int idx = link->bundle.idx;
    lo_bundle *b = (proto == MPR_PROTO_UDP) ? &link->bundle.udp[idx] : &link->bundle.tcp[idx];
    if (!*b)
        *b = lo_bundle_new(t);
    lo_bundle_add_message(*b, dst->path, msg);
}

void mpr_link_process_bundles(mpr_link link, mpr_time t)
{
    RETURN_UNLESS(link);

    unsigned int idx = link->bundle.idx;
    unsigned int alt = idx ? 0 : 1;

    // init alternate bundle
    link->bundle.udp[alt] = link->bundle.tcp[alt] = 0;

    lo_bundle udp = link->bundle.udp[idx];
    lo_bundle tcp = link->bundle.tcp[idx];

    // increment bundle index
    link->bundle.idx = alt;

    if (link->local_dev == link->remote_dev) {
        if (!udp)
            return;

        // set out-of-band timestamp
        mpr_dev_bundle_start(lo_bundle_get_timestamp(udp), NULL);
        // call handler directly instead of sending over the network
        int i = 0, num = lo_bundle_count(udp);
        const char *path;
        while (i < num) {
            lo_message m = lo_bundle_get_message(udp, i, &path);
            // need to look up signal by path
            mpr_rtr_sig rs = link->obj.graph->net.rtr->sigs;
            while (rs) {
                if (0 == strcmp(path, rs->sig->path)) {
                    mpr_dev_handler(NULL, lo_message_get_types(m),
                                    lo_message_get_argv(m),
                                    lo_message_get_argc(m), m, (void*)rs->sig);
                    break;
                }
                rs = rs->next;
            }
            ++i;
        }
        lo_bundle_free_recursive(udp);
    }
    else {
        mpr_net n = &link->obj.graph->net;
        if (udp) {
            if (lo_bundle_count(udp))
                lo_send_bundle_from(link->addr.udp, n->server.udp, udp);
            lo_bundle_free_recursive(udp);
        }
        if (tcp) {
            if (lo_bundle_count(tcp))
                lo_send_bundle_from(link->addr.tcp, n->server.tcp, tcp);
            lo_bundle_free_recursive(tcp);
        }
    }
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
    RETURN_UNLESS(link && link->devs[0]->obj.graph->maps, 0);
    mpr_list q = mpr_list_new_query((const void**)&link->devs[0]->obj.graph->maps,
                                    cmp_qry_link_maps, "h", link->obj.id);
    return mpr_list_start(q);
}

void mpr_link_remove_map(mpr_link link, mpr_map rem)
{
    int in = 0, out = 0, rev = link->devs[0]->loc ? 0 : 1;
    mpr_list list = mpr_link_get_maps(link);
    while (list) {
        mpr_map map = *(mpr_map*)list;
        list = mpr_list_get_next(list);
        if (map == rem)
            continue;
        if (map->dst->loc && map->dst->loc->rsig)
            ++in;
        else
            ++out;
    }
    link->num_maps[0] = rev ? out : in;
    link->num_maps[1] = rev ? in : out;
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
    return link->local_dev->loc || link->remote_dev->loc;
}
