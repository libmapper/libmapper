#include <string.h>
#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <stddef.h>
#include <limits.h>

#include "mpr_internal.h"
#include "types_internal.h"
#include <mpr/mpr.h>

void mpr_link_init(mpr_link link)
{
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

    if (link->local_dev == link->remote_dev) {
        /* Add data_addr for use by self-connections. In the future we may
         * decide to call local handlers directly, however this could result in
         * unfortunate loops/stack overflow. Sending data for self-connections
         * to localhost adds the messages to liblo's stack and imposes a delay
         * since the receiving handler will not be called until
         * mpr_dev_poll(). */
        int len;
        mpr_type type;
        const void *val;
        mpr_obj_get_prop_by_idx(&link->local_dev->obj, MPR_PROP_PORT, NULL,
                                &len, &type, &val, 0);
        if (1 != len || MPR_INT32 != type) {
            trace_dev(link->local_dev, "Error retrieving port for link.");
            return;
        }
        char port[10];
        snprintf(port, 10, "%d", *(int*)val);
        link->addr.udp = lo_address_new("localhost", port);
        link->addr.tcp = lo_address_new_with_proto(LO_TCP, "localhost", port);
    }

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
    mpr_net n = &link->obj.graph->net;
    mpr_net_use_bus(n);
    mpr_net_add_msg(n, cmd, 0, m);
    mpr_net_send(n);
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
}

void mpr_link_free(mpr_link link)
{
    FUNC_IF(mpr_tbl_free, link->obj.props.synced);
    FUNC_IF(mpr_tbl_free, link->obj.props.staged);
    FUNC_IF(free, link->num_maps);
    FUNC_IF(lo_address_free, link->addr.admin);
    FUNC_IF(lo_address_free, link->addr.udp);
    FUNC_IF(lo_address_free, link->addr.tcp);
    while (link->queues) {
        mpr_queue queue = link->queues;
        lo_bundle_free_recursive(queue->bundle.udp);
        lo_bundle_free_recursive(queue->bundle.tcp);
        link->queues = queue->next;
        free(queue);
    }
}

void mpr_link_start_queue(mpr_link link, mpr_time t)
{
    if (!link)
        return;
    // check if queue already exists
    mpr_queue queue = link->queues;
    while (queue) {
        if (memcmp(&queue->time, &t, sizeof(mpr_time))==0)
            return;
        queue = queue->next;
    }
    // need to create a new queue
    queue = malloc(sizeof(struct _mpr_queue));
    memcpy(&queue->time, &t, sizeof(mpr_time));
    queue->bundle.udp = lo_bundle_new(t);
    queue->bundle.tcp = lo_bundle_new(t);
    queue->next = link->queues;
    link->queues = queue;
}

void mpr_link_send_queue(mpr_link link, mpr_time t)
{
    if (!link)
        return;
    mpr_queue *queue = &link->queues;
    while (*queue) {
        if (memcmp(&(*queue)->time, &t, sizeof(mpr_time))==0)
            break;
        queue = &(*queue)->next;
    }
    if (*queue) {
        mpr_net n = &link->obj.graph->net;
#ifdef HAVE_LIBLO_BUNDLE_COUNT
        if (lo_bundle_count((*queue)->bundle.udp))
#endif
            lo_send_bundle_from(link->addr.udp, n->server.udp, (*queue)->bundle.udp);
        lo_bundle_free_recursive((*queue)->bundle.udp);
#ifdef HAVE_LIBLO_BUNDLE_COUNT
        if (lo_bundle_count((*queue)->bundle.tcp))
#endif
            lo_send_bundle_from(link->addr.tcp, n->server.tcp, (*queue)->bundle.tcp);
        lo_bundle_free_recursive((*queue)->bundle.tcp);
        mpr_queue temp = *queue;
        *queue = (*queue)->next;
        free(temp);
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
