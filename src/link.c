#include <string.h>
#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <stddef.h>
#include <limits.h>
#include <zlib.h>

#include "mapper_internal.h"
#include "types_internal.h"
#include <mapper/mapper.h>

void mapper_link_init(mapper_link link)
{
    if (!link->num_maps)
        link->num_maps = (int*)calloc(1, sizeof(int) * 2);
    link->obj.props.mask = 0;
    if (!link->obj.props.synced) {
        mapper_table tab = link->obj.props.synced = mapper_table_new();

        mapper_table_link(tab, MAPPER_PROP_DEVICE, 2, MAPPER_DEVICE,
                          &link->devs, NON_MODIFIABLE | LOCAL_ACCESS_ONLY);

        mapper_table_link(tab, MAPPER_PROP_ID, 1, MAPPER_INT64, &link->obj.id,
                          NON_MODIFIABLE);

        mapper_table_link(tab, MAPPER_PROP_NUM_MAPS, 2, MAPPER_INT32,
                          &link->num_maps, NON_MODIFIABLE | INDIRECT);
    }
    if (!link->obj.props.staged)
        link->obj.props.staged = mapper_table_new();

    if (!link->obj.id && link->local_dev->local)
        link->obj.id = mapper_device_generate_unique_id(link->local_dev);

    if (link->local_dev == link->remote_dev) {
        /* Add data_addr for use by self-connections. In the future we may
         * decide to call local handlers directly, however this could result in
         * unfortunate loops/stack overflow. Sending data for self-connections
         * to localhost adds the messages to liblo's stack and imposes a delay
         * since the receiving handler will not be called until
         * mapper_device_poll(). */
        int len;
        mapper_type type;
        const void *val;
        mapper_object_get_prop_by_index(&link->local_dev->obj, MAPPER_PROP_PORT,
                                        NULL, &len, &type, &val);
        if (1 != len || MAPPER_INT32 != type) {
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
    link->clock.response.msg_id = -1;
    mapper_time_t t;
    mapper_time_now(&t);
    link->clock.response.time.sec = t.sec + 10;

    // request missing metadata
    char cmd[256];
    snprintf(cmd, 256, "/%s/subscribe", link->remote_dev->name);
    lo_message m = lo_message_new();
    if (m) {
        lo_message_add_string(m, "device");
        mapper_network net = &link->obj.graph->net;
        mapper_network_bus(net);
        mapper_network_add_msg(net, cmd, 0, m);
        mapper_network_send(net);
    }
}

void mapper_link_connect(mapper_link link, const char *host, int admin_port,
                         int data_port)
{
    char str[16];
    mapper_table_set_record(link->remote_dev->obj.props.synced,
                            MAPPER_PROP_HOST, NULL, 1, MAPPER_STRING, host,
                            REMOTE_MODIFY);
    mapper_table_set_record(link->remote_dev->obj.props.synced,
                            MAPPER_PROP_PORT, NULL, 1, MAPPER_INT32, &data_port,
                            REMOTE_MODIFY);
    sprintf(str, "%d", data_port);
    link->addr.udp = lo_address_new(host, str);
    link->addr.tcp = lo_address_new_with_proto(LO_TCP, host, str);
    sprintf(str, "%d", admin_port);
    link->addr.admin = lo_address_new(host, str);
}

void mapper_link_free(mapper_link link)
{
    if (link->obj.props.synced)
        mapper_table_free(link->obj.props.synced);
    if (link->obj.props.staged)
        mapper_table_free(link->obj.props.staged);
    if (link->num_maps)
        free(link->num_maps);
    if (link->addr.admin)
        lo_address_free(link->addr.admin);
    if (link->addr.udp)
        lo_address_free(link->addr.udp);
    if (link->addr.tcp)
        lo_address_free(link->addr.tcp);
    while (link->queues) {
        mapper_queue queue = link->queues;
        lo_bundle_free_recursive(queue->bundle.udp);
        lo_bundle_free_recursive(queue->bundle.tcp);
        link->queues = queue->next;
        free(queue);
    }
}

void mapper_link_start_queue(mapper_link link, mapper_time_t t)
{
    if (!link)
        return;
    // check if queue already exists
    mapper_queue queue = link->queues;
    while (queue) {
        if (memcmp(&queue->time, &t, sizeof(mapper_time_t))==0)
            return;
        queue = queue->next;
    }
    // need to create a new queue
    queue = malloc(sizeof(struct _mapper_queue));
    memcpy(&queue->time, &t, sizeof(mapper_time_t));
    queue->bundle.udp = lo_bundle_new(t);
    queue->bundle.tcp = lo_bundle_new(t);
    queue->next = link->queues;
    link->queues = queue;
}

void mapper_link_send_queue(mapper_link link, mapper_time_t t)
{
    if (!link)
        return;
    mapper_queue *queue = &link->queues;
    while (*queue) {
        if (memcmp(&(*queue)->time, &t, sizeof(mapper_time_t))==0)
            break;
        queue = &(*queue)->next;
    }
    if (*queue) {
#ifdef HAVE_LIBLO_BUNDLE_COUNT
        if (lo_bundle_count((*queue)->bundle.udp))
#endif
            lo_send_bundle_from(link->addr.udp, link->obj.graph->net.server.udp,
                                (*queue)->bundle.udp);
        lo_bundle_free_recursive((*queue)->bundle.udp);
#ifdef HAVE_LIBLO_BUNDLE_COUNT
        if (lo_bundle_count((*queue)->bundle.tcp))
#endif
            lo_send_bundle_from(link->addr.tcp, link->obj.graph->net.server.tcp,
                                (*queue)->bundle.tcp);
        lo_bundle_free_recursive((*queue)->bundle.tcp);
        mapper_queue temp = *queue;
        *queue = (*queue)->next;
        free(temp);
    }
}

static int cmp_query_link_maps(const void *context_data, mapper_map map)
{
    mapper_id link_id = *(mapper_id*)context_data;
    int i;
    for (i = 0; i < map->num_src; i++) {
        if (map->src[i]->link && map->src[i]->link->obj.id == link_id)
            return 1;
    }
    if (map->dst->link && map->dst->link->obj.id == link_id)
        return 1;
    return 0;
}

mapper_object *mapper_link_get_maps(mapper_link link)
{
    if (!link || !link->devs[0]->obj.graph->maps)
        return 0;
    return ((mapper_object *)
            mapper_list_new_query(link->devs[0]->obj.graph->maps,
                                  cmp_query_link_maps, "h", link->obj.id));
}

void mapper_link_send(mapper_link link, network_msg_t cmd)
{
    lo_message msg = lo_message_new();
    if (!msg)
        return;

    lo_message_add_string(msg, link->devs[0]->name);
    lo_message_add_string(msg, "<->");
    lo_message_add_string(msg, link->devs[1]->name);
    mapper_network_add_msg(&link->obj.graph->net, 0, cmd, msg);
}
