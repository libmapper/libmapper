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

void mapper_link_init(mapper_link link, int is_local)
{
    if (!link->num_maps)
        link->num_maps = (int*)calloc(1, sizeof(int) * 2);
    if (!link->object.props) {
        link->object.props = mapper_table_new();
        mapper_table_link_value(link->object.props, AT_ID, 1, MAPPER_INT64,
                                &link->object.id, NON_MODIFIABLE);
        mapper_table_link_value(link->object.props, AT_NUM_MAPS, 2, MAPPER_INT32,
                                &link->num_maps, NON_MODIFIABLE | INDIRECT);
        mapper_table_link_value(link->object.props, AT_USER_DATA, 1, MAPPER_PTR,
                                &link->object.user_data,
                                MODIFIABLE | INDIRECT | LOCAL_ACCESS_ONLY);
        mapper_table_set_record(link->object.props, AT_IS_LOCAL, NULL, 1,
                                MAPPER_BOOL, &is_local,
                                LOCAL_ACCESS_ONLY | NON_MODIFIABLE);
    }
    if (!link->object.staged_props)
        link->object.staged_props = mapper_table_new();
    if (!is_local)
        return;

    link->local = ((mapper_local_link)
                   calloc(1, sizeof(struct _mapper_local_link)));

    if (!link->object.id && link->local_device->local)
        link->object.id = mapper_device_generate_unique_id(link->local_device);

    if (link->local_device == link->remote_device) {
        /* Add data_addr for use by self-connections. In the future we may
         * decide to call local handlers directly, however this could result in
         * unfortunate loops/stack overflow. Sending data for self-connections
         * to localhost adds the messages to liblo's stack and imposes a delay
         * since the receiving handler will not be called until
         * mapper_device_poll(). */
        char str[16];
        snprintf(str, 16, "%d", mapper_device_port(link->local_device));
        link->local->udp_data_addr = lo_address_new("localhost", str);
        link->local->tcp_data_addr = lo_address_new_with_proto(LO_TCP,
                                                               "localhost", str);
    }

    link->local->clock.new = 1;
    link->local->clock.sent.message_id = 0;
    link->local->clock.response.message_id = -1;
    mapper_timetag_t tt;
    mapper_timetag_now(&tt);
    link->local->clock.response.timetag.sec = tt.sec + 10;

    // request missing metadata
    char cmd[256];
    snprintf(cmd, 256, "/%s/subscribe", link->remote_device->name);
    lo_message m = lo_message_new();
    if (m) {
        lo_message_add_string(m, "device");
        mapper_network_set_dest_bus(link->local_device->database->network);
        mapper_network_add_message(link->local_device->database->network, cmd,
                                   0, m);
        mapper_network_send(link->local_device->database->network);
    }
    // increment device link count
    ++link->local_device->num_links;
}

void mapper_link_connect(mapper_link link, const char *host, int admin_port,
                         int data_port)
{
    char str[16];
    mapper_table_set_record(link->remote_device->object.props, AT_HOST, NULL, 1,
                            MAPPER_STRING, host, REMOTE_MODIFY);
    mapper_table_set_record(link->remote_device->object.props, AT_PORT, NULL, 1,
                            MAPPER_INT32, &data_port, REMOTE_MODIFY);
    sprintf(str, "%d", data_port);
    link->local->udp_data_addr = lo_address_new(host, str);
    link->local->tcp_data_addr = lo_address_new_with_proto(LO_TCP, host, str);
    sprintf(str, "%d", admin_port);
    link->local->admin_addr = lo_address_new(host, str);
}

void mapper_link_free(mapper_link link)
{
    if (link->object.props)
        mapper_table_free(link->object.props);
    if (link->object.staged_props)
        mapper_table_free(link->object.staged_props);
    if (link->num_maps)
        free(link->num_maps);
    if (link->local) {
        if (link->local->admin_addr)
            lo_address_free(link->local->admin_addr);
        if (link->local->udp_data_addr)
            lo_address_free(link->local->udp_data_addr);
        if (link->local->tcp_data_addr)
            lo_address_free(link->local->tcp_data_addr);
        while (link->local->queues) {
            mapper_queue queue = link->local->queues;
            lo_bundle_free_recursive(queue->udp_bundle);
            lo_bundle_free_recursive(queue->tcp_bundle);
            link->local->queues = queue->next;
            free(queue);
        }
        --link->local_device->num_links;
        free(link->local);
    }
}

void mapper_link_start_queue(mapper_link link, mapper_timetag_t tt)
{
    if (!link || !link->local)
        return;
    // check if queue already exists
    mapper_queue queue = link->local->queues;
    while (queue) {
        if (memcmp(&queue->tt, &tt, sizeof(mapper_timetag_t))==0)
            return;
        queue = queue->next;
    }
    // need to create a new queue
    queue = malloc(sizeof(struct _mapper_queue));
    memcpy(&queue->tt, &tt, sizeof(mapper_timetag_t));
    queue->udp_bundle = lo_bundle_new(tt);
    queue->tcp_bundle = lo_bundle_new(tt);
    queue->next = link->local->queues;
    link->local->queues = queue;
}

void mapper_link_send_queue(mapper_link link, mapper_timetag_t tt)
{
    if (!link || !link->local)
        return;
    mapper_queue *queue = &link->local->queues;
    while (*queue) {
        if (memcmp(&(*queue)->tt, &tt, sizeof(mapper_timetag_t))==0)
            break;
        queue = &(*queue)->next;
    }
    if (*queue) {
#ifdef HAVE_LIBLO_BUNDLE_COUNT
        if (lo_bundle_count((*queue)->udp_bundle))
#endif
            lo_send_bundle_from(link->local->udp_data_addr,
                                link->local_device->local->udp_server,
                                (*queue)->udp_bundle);
        lo_bundle_free_recursive((*queue)->udp_bundle);
#ifdef HAVE_LIBLO_BUNDLE_COUNT
        if (lo_bundle_count((*queue)->tcp_bundle))
#endif
            lo_send_bundle_from(link->local->tcp_data_addr,
                                link->local_device->local->tcp_server,
                                (*queue)->tcp_bundle);
        lo_bundle_free_recursive((*queue)->tcp_bundle);
        mapper_queue temp = *queue;
        *queue = (*queue)->next;
        free(temp);
    }
}

mapper_device mapper_link_device(mapper_link link, int idx)
{
    if (idx < 0 || idx > 1)
        return 0;
    return link->devices[idx];
}

int mapper_link_num_maps(mapper_link link)
{
    return link->num_maps[0] + link->num_maps[1];
}

static int cmp_query_link_maps(const void *context_data, mapper_map map)
{
    mapper_id link_id = *(mapper_id*)context_data;
    int i;
    for (i = 0; i < map->num_sources; i++) {
        if (map->sources[i]->link && map->sources[i]->link->object.id == link_id)
            return 1;
    }
    if (map->destination.link && map->destination.link->object.id == link_id)
        return 1;
    return 0;
}

mapper_map *mapper_link_maps(mapper_link link)
{
    if (!link || !link->devices[0]->database->maps)
        return 0;
    return ((mapper_map *)
            mapper_list_new_query(link->devices[0]->database->maps,
                                  cmp_query_link_maps, "h", link->object.id));
}

mapper_id mapper_link_id(mapper_link link)
{
    return link->object.id;
}

void mapper_link_set_user_data(mapper_link link, const void *user_data)
{
    if (link)
        link->object.user_data = (void*)user_data;
}

void *mapper_link_user_data(mapper_link link)
{
    return link ? link->object.user_data : 0;
}

int mapper_link_num_properties(mapper_link link)
{
    return mapper_table_num_records(link->object.props);
}

int mapper_link_property(mapper_link link, const char *name, int *length,
                         mapper_type *type, const void **value)
{
    return mapper_table_property(link->object.props, name, length, type, value);
}

int mapper_link_property_index(mapper_link link, unsigned int index,
                               const char **name, int *length, mapper_type *type,
                               const void **value)
{
    return mapper_table_property_index(link->object.props, index, name, length,
                                       type, value);
}

int mapper_link_set_property(mapper_link link, const char *name, int length,
                             mapper_type type, const void *value, int publish)
{
    mapper_property_t prop = mapper_property_from_string(name);
    int flags = REMOTE_MODIFY;
    if (!publish)
        flags |= LOCAL_ACCESS_ONLY;
    return mapper_table_set_record(link->object.staged_props, prop, name, length, type,
                                   value, flags);
}

int mapper_link_remove_property(mapper_link link, const char *name)
{
    if (!link)
        return 0;
    mapper_property_t prop = mapper_property_from_string(name);
    return mapper_table_set_record(link->object.staged_props, prop | PROPERTY_REMOVE,
                                   name, 0, 0, 0, REMOTE_MODIFY);
}

int mapper_link_set_from_message(mapper_link link, mapper_message msg,
                                 int reversed)
{
    int i, updated = 0;

    if (!link || !msg)
        return 0;
    
    for (i = 0; i < msg->num_atoms; i++) {
        if (msg->atoms[i].index == AT_ID) {
            // choose lowest id
            if (!link->object.id || link->object.id > (*msg->atoms[i].values)->h) {
                mapper_id id = msg->atoms[i].values[0]->h;
                mapper_table_set_record(link->object.props, AT_ID, NULL, 1,
                                        MAPPER_INT64, &id, LOCAL_MODIFY);
                ++updated;
            }
        }
        else if (link->local
                 && (MASK_PROP_BITFLAGS(msg->atoms[i].index) != AT_EXTRA))
            continue;
        else if (msg->atoms[i].index == AT_NUM_MAPS) {
            if (msg->atoms[i].length != 2 || msg->atoms[i].types[0] != MAPPER_INT32)
                continue;
            link->num_maps[0] = msg->atoms[i].values[0+reversed]->i32;
            link->num_maps[1] = msg->atoms[i].values[1-reversed]->i32;
            ++updated;
        }
        else {
            updated += mapper_table_set_record_from_atom(link->object.props,
                                                         &msg->atoms[i],
                                                         REMOTE_MODIFY);
        }
    }
    return updated;
}

void mapper_link_clear_staged_properties(mapper_link link) {
    if (link)
        mapper_table_clear(link->object.staged_props);
}

void mapper_link_push(mapper_link link)
{
    if (!link || !link->devices[0])
        return;

    mapper_network_set_dest_bus(link->devices[0]->database->network);
    mapper_link_send_state(link, MSG_LINK_MODIFY, 1);

    // clear the staged properties
    mapper_table_clear(link->object.staged_props);
}

void mapper_link_send_state(mapper_link link, network_message_t cmd, int staged)
{
    lo_message msg = lo_message_new();
    if (!msg)
        return;

    lo_message_add_string(msg, link->devices[0]->name);
    lo_message_add_string(msg, "<->");
    lo_message_add_string(msg, link->devices[1]->name);

    if (cmd != MSG_UNLINKED) {
        mapper_table_add_to_message(0, (staged
                                        ? link->object.staged_props
                                        : link->object.props), msg);
    }
    mapper_network_add_message(link->devices[0]->database->network, 0, cmd, msg);
}

mapper_link *mapper_link_query_union(mapper_link *query1, mapper_link *query2)
{
    return (mapper_link*)mapper_list_query_union((void**)query1, (void**)query2);
}

mapper_link *mapper_link_query_intersection(mapper_link *query1,
                                            mapper_link *query2)
{
    return (mapper_link*)mapper_list_query_intersection((void**)query1,
                                                        (void**)query2);
}

mapper_link *mapper_link_query_difference(mapper_link *query1,
                                          mapper_link *query2)
{
    return (mapper_link*)mapper_list_query_difference((void**)query1,
                                                      (void**)query2);
}

mapper_link mapper_link_query_index(mapper_link *query, int index)
{
    return (mapper_link)mapper_list_query_index((void**)query, index);
}

mapper_link *mapper_link_query_next(mapper_link *query)
{
    return (mapper_link*)mapper_list_query_next((void**)query);
}

mapper_link *mapper_link_query_copy(mapper_link *query)
{
    return (mapper_link*)mapper_list_query_copy((void**)query);
}

void mapper_link_query_done(mapper_link *query)
{
    mapper_list_query_done((void**)query);
}

void mapper_link_print(mapper_link link)
{
    printf("%s <-> %s", link->devices[0]->name, link->devices[1]->name);
    int i = 0;
    const char *key;
    mapper_type type;
    const void *val;
    int len;
    while (!mapper_link_property_index(link, i++, &key, &len, &type, &val)) {
        die_unless(val!=0, "returned zero value\n");
        if (len) {
            printf(", %s=", key);
            mapper_property_print(len, type, val);
        }
    }
    if (link->local) {
        if (link->local->admin_addr)
            printf(", admin_addr=%s:%s",
                   lo_address_get_hostname(link->local->admin_addr),
                   lo_address_get_port(link->local->admin_addr));
        if (link->local->udp_data_addr)
            printf(", udp_data_addr=%s:%s",
                   lo_address_get_hostname(link->local->udp_data_addr),
                   lo_address_get_port(link->local->udp_data_addr));
        if (link->local->tcp_data_addr)
            printf(", tcp_data_addr=%s:%s",
                   lo_address_get_hostname(link->local->tcp_data_addr),
                   lo_address_get_port(link->local->tcp_data_addr));
    }
    printf("\n");
}
