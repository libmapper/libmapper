#include <string.h>
#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#include <zlib.h>
#include <sys/time.h>

#include "mapper_internal.h"

#define AUTOSUBSCRIBE_INTERVAL 60
extern const char* network_msg_strings[NUM_MSG_STRINGS];

#ifdef DEBUG
static void print_subscription_flags(int flags)
{
    printf("[");
    if (MAPPER_OBJ_NONE == flags) {
        printf("none\n");
        return;
    }

    if (flags & MAPPER_OBJ_DEVICE)
        printf("devices, ");

    if (flags & MAPPER_OBJ_INPUT_SIGNAL) {
        if (flags & MAPPER_OBJ_OUTPUT_SIGNAL)
            printf("signals, ");
        else
            printf("input signals, ");
    }
    else if (flags & MAPPER_OBJ_OUTPUT_SIGNAL)
        printf("output signals, ");

    if (flags & MAPPER_OBJ_MAP_IN) {
        if (flags & MAPPER_OBJ_MAP_OUT)
            printf("maps, ");
        else
            printf("incoming maps, ");
    }
    else if (flags & MAPPER_OBJ_MAP_OUT)
        printf("outgoing maps, ");

    printf("\b\b]\n");
}
#endif

mapper_graph mapper_graph_new(int subscribe_flags)
{
    mapper_graph g = (mapper_graph) calloc(1, sizeof(mapper_graph_t));
    if (!g)
        return NULL;

    g->net.graph = g;
    g->own = 1;
    mapper_network_init(&g->net, 0, 0, 0);

    if (subscribe_flags) {
        mapper_graph_subscribe(g, 0, subscribe_flags, -1);
    }
    return g;
}

void mapper_graph_free(mapper_graph g)
{
    if (!g)
        return;

    // remove callbacks now so they won't be called when removing devices
    mapper_graph_remove_all_callbacks(g);

    // unsubscribe from and remove any autorenewing subscriptions
    while (g->subscriptions) {
        mapper_graph_subscribe(g, g->subscriptions->dev, 0, 0);
    }

    /* Remove all non-local maps */
    mapper_object *maps = mapper_list_from_data(g->maps);
    while (maps) {
        mapper_map map = (mapper_map)*maps;
        maps = mapper_object_list_next(maps);
        if (!map->local)
            mapper_graph_remove_map(g, map, MAPPER_REMOVED);
    }

    /* Remove all non-local devices and signals from the graph except for
     * those referenced by local maps. */
    mapper_object *devs = mapper_list_from_data(g->devs);
    while (devs) {
        mapper_device dev = (mapper_device)*devs;
        devs = mapper_object_list_next(devs);
        if (dev->local)
            continue;

        int no_local_device_maps = 1;
        mapper_object *sigs = mapper_device_get_signals(dev, MAPPER_DIR_ANY);
        while (sigs) {
            mapper_signal sig = (mapper_signal)*sigs;
            sigs = mapper_object_list_next(sigs);
            int no_local_signal_maps = 1;
            mapper_object *maps = mapper_signal_get_maps(sig, MAPPER_DIR_ANY);
            while (maps) {
                if (((mapper_map)*maps)->local) {
                    no_local_device_maps = no_local_signal_maps = 0;
                    mapper_object_list_free(maps);
                    break;
                }
                maps = mapper_object_list_next(maps);
            }
            if (no_local_signal_maps)
                mapper_graph_remove_signal(g, sig, MAPPER_REMOVED);
        }
        if (no_local_device_maps)
            mapper_graph_remove_device(g, dev, MAPPER_REMOVED, 1);
    }

    mapper_network_free(&g->net);
    free(g);
}

static void add_callback(fptr_list *head, const void *f, int types,
                         const void *user)
{
    fptr_list cb = (fptr_list)malloc(sizeof(struct _fptr_list));
    cb->f = (void*)f;
    cb->types = types;
    cb->context = (void*)user;
    cb->next = *head;
    *head = cb;
}

static void remove_callback(fptr_list *head, const void *f, const void *user)
{
    fptr_list cb = *head;
    fptr_list prevcb = 0;
    while (cb) {
        if (cb->f == f && cb->context == user)
            break;
        prevcb = cb;
        cb = cb->next;
    }
    if (!cb)
        return;

    if (prevcb)
        prevcb->next = cb->next;
    else
        *head = cb->next;

    free(cb);
}

/**** Generic records ****/

static mapper_object _get_object_by_id(mapper_graph g, void *obj_list,
                                       mapper_id id)
{
    mapper_object *objs = mapper_list_from_data(obj_list);
    while (objs) {
        if (id == (*objs)->id)
            return *objs;
        objs = mapper_object_list_next(objs);
    }
    return NULL;
}

int mapper_graph_get_num_objects(mapper_graph g, int types)
{
    int count = 0;
    if (types & MAPPER_OBJ_DEVICE)
        count += mapper_object_list_get_length(mapper_list_from_data(g->devs));
    if (types & MAPPER_OBJ_SIGNAL)
        count += mapper_object_list_get_length(mapper_list_from_data(g->sigs));
    if (types & MAPPER_OBJ_LINK)
        count += mapper_object_list_get_length(mapper_list_from_data(g->links));
    if (types & MAPPER_OBJ_MAP)
        count += mapper_object_list_get_length(mapper_list_from_data(g->maps));
    return count;
}

mapper_object mapper_graph_get_object(mapper_graph g, mapper_object_type type,
                                      mapper_id id)
{
    mapper_object obj = NULL;
    if (type & MAPPER_OBJ_DEVICE)
        obj = _get_object_by_id(g, g->devs, id);
    if (!obj && (type & MAPPER_OBJ_SIGNAL))
        obj = _get_object_by_id(g, g->sigs, id);
    if (!obj && (type & MAPPER_OBJ_LINK))
        obj = _get_object_by_id(g, g->links, id);
    if (!obj && (type & MAPPER_OBJ_MAP))
        obj = _get_object_by_id(g, g->maps, id);
    // TODO: slots
    return obj;
}

// TODO: support queries over multiple object types.
mapper_object *mapper_graph_get_objects(mapper_graph g, int types)
{
    if (types & MAPPER_OBJ_DEVICE)
        return mapper_list_from_data(g->devs);
    if (types & MAPPER_OBJ_SIGNAL)
        return mapper_list_from_data(g->sigs);
    if (types & MAPPER_OBJ_LINK)
        return mapper_list_from_data(g->links);
    if (types & MAPPER_OBJ_MAP)
        return mapper_list_from_data(g->maps);
    return 0;
}

void mapper_graph_add_callback(mapper_graph g, mapper_graph_handler *h,
                               int types, const void *user)
{
    add_callback(&g->callbacks, h, types, user);
}

void mapper_graph_remove_callback(mapper_graph g, mapper_graph_handler *h,
                                  const void *user)
{
    remove_callback(&g->callbacks, h, user);
}

/**** Device records ****/

mapper_device mapper_graph_add_or_update_device(mapper_graph g, const char *name,
                                                mapper_msg msg)
{
    const char *no_slash = skip_slash(name);
    mapper_device dev = mapper_graph_get_device_by_name(g, no_slash);
    int rc = 0, updated = 0;

    if (!dev) {
        trace_graph("adding device '%s'.\n", name);
        dev = (mapper_device)mapper_list_add_item((void**)&g->devs, sizeof(*dev));
        dev->name = strdup(no_slash);
        dev->obj.id = crc32(0L, (const Bytef *)no_slash, strlen(no_slash));
        dev->obj.id <<= 32;
        dev->obj.type = MAPPER_OBJ_DEVICE;
        dev->obj.graph = g;
        init_device_prop_table(dev);
        rc = 1;
    }

    if (dev) {
        updated = mapper_device_set_from_msg(dev, msg);
        if (!rc)
            trace_graph("updated %d properties for device '%s'.\n", updated,
                        name);
        mapper_time_now(&dev->synced);

        if (rc || updated) {
            fptr_list cb = g->callbacks, temp;
            while (cb) {
                temp = cb->next;
                if (cb->types & MAPPER_OBJ_DEVICE) {
                    mapper_graph_handler *h = cb->f;
                    h(g, (mapper_object)dev,
                      rc ? MAPPER_ADDED : MAPPER_MODIFIED, cb->context);
                }
                cb = temp;
            }
        }
    }
    return dev;
}

// Internal function called by /logout protocol handler
void mapper_graph_remove_device(mapper_graph g, mapper_device dev,
                                mapper_record_event event, int quiet)
{
    if (!dev)
        return;

    mapper_graph_remove_maps_by_query(g, mapper_device_get_maps(dev, MAPPER_DIR_ANY),
                                      event);

    // remove matching maps scopes
    mapper_object *maps = mapper_graph_get_maps_by_scope(g, dev);
    while (maps) {
        mapper_map_remove_scope((mapper_map)*maps, dev);
        maps = mapper_object_list_next(maps);
    }

    mapper_graph_remove_links_by_query(g, mapper_device_get_links(dev, MAPPER_DIR_ANY),
                                       event);

    mapper_graph_remove_signals_by_query(g, mapper_device_get_signals(dev, MAPPER_DIR_ANY),
                                         event);

    mapper_list_remove_item((void**)&g->devs, dev);

    if (!quiet) {
        fptr_list cb = g->callbacks, temp;
        while (cb) {
            temp = cb->next;
            if (cb->types & MAPPER_OBJ_DEVICE) {
                mapper_graph_handler *h = cb->f;
                h(g, (mapper_object)dev, event, cb->context);
            }
            cb = temp;
        }
    }

    if (dev->obj.props.synced)
        mapper_table_free(dev->obj.props.synced);
    if (dev->obj.props.staged)
        mapper_table_free(dev->obj.props.staged);
    if (dev->name)
        free(dev->name);
    mapper_list_free_item(dev);
}

mapper_device mapper_graph_get_device_by_name(mapper_graph g, const char *name)
{
    const char *no_slash = skip_slash(name);
    mapper_object *devs = mapper_list_from_data(g->devs);
    while (devs) {
        mapper_device dev = (mapper_device)*devs;
        if (dev->name && (0 == strcmp(dev->name, no_slash)))
            return dev;
        devs = mapper_object_list_next(devs);
    }
    return 0;
}

void mapper_graph_check_device_status(mapper_graph g, uint32_t time_sec)
{
    time_sec -= TIMEOUT_SEC;
    mapper_object *devs = mapper_list_from_data(g->devs);
    while (devs) {
        mapper_device dev = (mapper_device)*devs;
        // check if device has "checked in" recently
        // this could be /sync ping or any sent metadata
        if (dev->synced.sec && (dev->synced.sec < time_sec)) {
            // remove subscription
            mapper_graph_subscribe(g, dev, 0, 0);
            mapper_graph_remove_device(g, dev, MAPPER_EXPIRED, 0);
        }
        devs = mapper_object_list_next(devs);
    }
}

/**** Signals ****/

mapper_signal mapper_graph_add_or_update_signal(mapper_graph g, const char *name,
                                                const char *device_name,
                                                mapper_msg msg)
{
    mapper_signal sig = 0;
    int sig_rc = 0, dev_rc = 0, updated = 0;

    mapper_device dev = mapper_graph_get_device_by_name(g, device_name);
    if (dev) {
        sig = mapper_device_get_signal_by_name(dev, name);
        if (sig && sig->local)
            return sig;
    }
    else {
        dev = mapper_graph_add_or_update_device(g, device_name, 0);
        dev_rc = 1;
    }

    if (!sig) {
        trace_graph("adding signal '%s:%s'.\n", device_name, name);
        sig = (mapper_signal)mapper_list_add_item((void**)&g->sigs,
                                                  sizeof(mapper_signal_t));

        // also add device record if necessary
        sig->dev = dev;

        sig->obj.graph = g;

        // Defaults (int, length=1)
        mapper_signal_init(sig, 0, 0, name, 0, 0, 0, 0, 0, 0);

        sig_rc = 1;
    }

    if (sig) {
        updated = mapper_signal_set_from_msg(sig, msg);
        if (!sig_rc)
            trace_graph("updated %d properties for signal '%s:%s'.\n", updated,
                     device_name, name);

        if (sig_rc || updated) {
            fptr_list cb = g->callbacks, temp;
            while (cb) {
                temp = cb->next;
                if (cb->types & MAPPER_OBJ_SIGNAL) {
                    mapper_graph_handler *h = cb->f;
                    h(g, (mapper_object)sig,
                      sig_rc ? MAPPER_ADDED : MAPPER_MODIFIED, cb->context);
                }
                cb = temp;
            }
        }
    }
    return sig;
}

void mapper_graph_remove_signal(mapper_graph g, mapper_signal sig,
                                mapper_record_event event)
{
    // remove any stored maps using this signal
    mapper_graph_remove_maps_by_query(g, mapper_signal_get_maps(sig, MAPPER_DIR_ANY),
                                      event);

    mapper_list_remove_item((void**)&g->sigs, sig);

    fptr_list cb = g->callbacks, temp;
    while (cb) {
        temp = cb->next;
        if (cb->types & MAPPER_OBJ_SIGNAL) {
            mapper_graph_handler *h = cb->f;
            h(g, (mapper_object)sig, event, cb->context);
        }
        cb = temp;
    }

    if (sig->dir & MAPPER_DIR_IN)
        --sig->dev->num_inputs;
    if (sig->dir & MAPPER_DIR_OUT)
        --sig->dev->num_outputs;

    mapper_signal_free(sig);
    mapper_list_free_item(sig);
}

void mapper_graph_remove_signals_by_query(mapper_graph g, mapper_object *query,
                                          mapper_record_event event)
{
    while (query) {
        mapper_signal sig = (mapper_signal)*query;
        query = mapper_object_list_next(query);
        if (!sig->local)
            mapper_graph_remove_signal(g, sig, event);
    }
}

/**** Link records ****/

mapper_link mapper_graph_add_link(mapper_graph g, mapper_device dev1,
                                  mapper_device dev2)
{
    if (!dev1 || !dev2)
        return 0;

    mapper_link link = mapper_device_get_link_by_remote_device(dev1, dev2);
    if (link)
        return link;

    link = (mapper_link)mapper_list_add_item((void**)&g->links,
                                             sizeof(mapper_link_t));
    if (dev2->local) {
        link->local_dev = dev2;
        link->remote_dev = dev1;
    }
    else {
        link->local_dev = dev1;
        link->remote_dev = dev2;
    }
    link->obj.type = MAPPER_OBJ_LINK;
    link->obj.graph = g;
    mapper_link_init(link);
    return link;
}

void mapper_graph_remove_links_by_query(mapper_graph g, mapper_object *links,
                                        mapper_record_event event)
{
    while (links) {
        mapper_link link = (mapper_link)*links;
        links = mapper_object_list_next(links);
        mapper_graph_remove_link(g, link, event);
    }
}

void mapper_graph_remove_link(mapper_graph g, mapper_link link,
                              mapper_record_event event)
{
    if (!link)
        return;

    mapper_graph_remove_maps_by_query(g, mapper_link_get_maps(link), event);

    mapper_list_remove_item((void**)&g->links, link);

    fptr_list cb = g->callbacks, temp;
    while (cb) {
        temp = cb->next;
        if (cb->types & MAPPER_OBJ_LINK) {
            mapper_graph_handler *h = cb->f;
            h(g, (mapper_object)link, event, cb->context);
        }
        cb = temp;
    }

    // TODO: also clear network info from remote devices?

    mapper_link_free(link);
    mapper_list_free_item(link);
}

/**** Map records ****/

static int compare_slot_names(const void *l, const void *r)
{
    int result = strcmp((*(mapper_slot*)l)->sig->dev->name,
                        (*(mapper_slot*)r)->sig->dev->name);
    if (0 == result)
        return strcmp((*(mapper_slot*)l)->sig->name,
                      (*(mapper_slot*)r)->sig->name);
    return result;
}

static mapper_signal add_sig_from_whole_name(mapper_graph g, const char* name)
{
    char *devnamep, *signame, devname[256];
    int devnamelen = mapper_parse_names(name, &devnamep, &signame);
    if (!devnamelen || devnamelen >= 256) {
        trace_graph("error extracting device name\n");
        return 0;
    }
    strncpy(devname, devnamep, devnamelen);
    devname[devnamelen] = 0;
    return mapper_graph_add_or_update_signal(g, signame, devname, 0);
}

mapper_map mapper_graph_add_or_update_map(mapper_graph g, int num_src,
                                          const char **src_names,
                                          const char *dst_name,
                                          mapper_msg msg)
{
    if (num_src >= MAX_NUM_MAP_SRC) {
        trace_graph("error: maximum mapping sources exceeded.\n");
        return 0;
    }

    mapper_map map = 0;
    int rc = 0, updated = 0, i, j;

    /* We could be part of larger "convergent" mapping, so we will retrieve
     * record by mapping id instead of names. */
    uint64_t id = 0;
    if (msg) {
        mapper_msg_atom atom = mapper_msg_prop(msg, MAPPER_PROP_ID);
        if (!atom || atom->types[0] != MAPPER_INT64) {
            trace_graph("no 'id' property found in map metadata, aborting.\n");
            return 0;
        }
        id = atom->vals[0]->i64;
        map = (mapper_map)_get_object_by_id(g, (mapper_object)g->maps, id);
        if (!map && _get_object_by_id(g, (mapper_object)g->maps, 0)) {
            // may have staged map stored locally
            mapper_object *maps = mapper_list_from_data(g->maps);
            while (maps) {
                map = (mapper_map)*maps;
                int i;
                if (map->num_src != num_src)
                    map = 0;
                else if (mapper_slot_match_full_name(map->dst, dst_name))
                    map = 0;
                else {
                    for (i = 0; i < num_src; i++) {
                        if (mapper_slot_match_full_name(map->src[i], src_names[i])) {
                            map = 0;
                            break;
                        }
                    }
                }
                if (map)
                    break;
                maps = mapper_object_list_next(maps);
            }
        }
    }

    if (!map) {
#ifdef DEBUG
        trace_graph("adding map [");
        for (i = 0; i < num_src; i++)
            printf("%s, ", src_names[i]);
        printf("\b\b] -> [%s].\n", dst_name);
#endif
        // add signals first in case signal handlers trigger map queries
        mapper_signal dst_sig = add_sig_from_whole_name(g, dst_name);
        if (!dst_sig)
            return 0;
        mapper_signal src_sigs[num_src];
        for (i = 0; i < num_src; i++) {
            src_sigs[i] = add_sig_from_whole_name(g, src_names[i]);
            if (!src_sigs[i])
                return 0;
            mapper_graph_add_link(g, dst_sig->dev, src_sigs[i]->dev);
        }

        map = (mapper_map)mapper_list_add_item((void**)&g->maps,
                                               sizeof(mapper_map_t));
        map->obj.type = MAPPER_OBJ_MAP;
        map->obj.graph = g;
        map->obj.id = id;
        map->num_src = num_src;
        map->src = (mapper_slot*)malloc(sizeof(mapper_slot) * num_src);
        for (i = 0; i < num_src; i++) {
            map->src[i] = (mapper_slot)calloc(1, sizeof(struct _mapper_slot));
            map->src[i]->sig = src_sigs[i];
            map->src[i]->obj.id = i;
            map->src[i]->obj.graph = g;
            map->src[i]->causes_update = 1;
            map->src[i]->map = map;
            if (map->src[i]->sig->local) {
                map->src[i]->num_inst = map->src[i]->sig->num_inst;
                map->src[i]->use_inst = map->src[i]->num_inst > 1;
            }
        }
        map->dst = (mapper_slot)calloc(1, sizeof(struct _mapper_slot));
        map->dst->sig = dst_sig;
        map->dst->obj.graph = g;
        map->dst->causes_update = 1;
        map->dst->map = map;
        if (map->dst->sig->local) {
            map->dst->num_inst = map->dst->sig->num_inst;
            map->dst->use_inst = map->dst->num_inst > 1;
        }

        mapper_map_init(map);
        rc = 1;
    }
    else {
        int changed = 0;
        // may need to add sources to existing map
        for (i = 0; i < num_src; i++) {
            mapper_signal src_sig = add_sig_from_whole_name(g, src_names[i]);
            if (!src_sig)
                return 0;
            for (j = 0; j < map->num_src; j++) {
                if (map->src[j]->sig == src_sig) {
                    break;
                }
            }
            if (j == map->num_src) {
                ++changed;
                ++map->num_src;
                map->src = realloc(map->src, sizeof(struct _mapper_slot)
                                   * map->num_src);
                map->src[j] = (mapper_slot) calloc(1, sizeof(struct _mapper_slot));
                map->src[j]->dir = MAPPER_DIR_OUT;
                map->src[j]->sig = src_sig;
                map->src[j]->obj.graph = g;
                map->src[j]->causes_update = 1;
                map->src[j]->map = map;
                if (map->src[j]->sig->local) {
                    map->src[j]->num_inst = map->src[j]->sig->num_inst;
                    map->src[j]->use_inst = map->src[j]->num_inst > 1;
                }
                mapper_slot_init(map->src[j]);
                ++updated;
            }
        }
        if (changed) {
            // slots should be in alphabetical order
            qsort(map->src, map->num_src, sizeof(mapper_slot),
                  compare_slot_names);
            // fix slot ids
            mapper_table tab;
            mapper_table_record_t *rec;
            for (i = 0; i < num_src; i++) {
                map->src[i]->obj.id = i;
                // also need to correct slot table indices
                tab = map->src[i]->obj.props.synced;
                for (j = 0; j < tab->num_records; j++) {
                    rec = &tab->records[j];
                    rec->prop = MASK_PROP_BITFLAGS(rec->prop) | SRC_SLOT_PROP(i);
                }
                tab = map->src[i]->obj.props.staged;
                for (j = 0; j < tab->num_records; j++) {
                    rec = &tab->records[j];
                    rec->prop = MASK_PROP_BITFLAGS(rec->prop) | SRC_SLOT_PROP(i);
                }
            }
        }
    }

    if (map) {
        updated += mapper_map_set_from_msg(map, msg, 0);
#ifdef DEBUG
        if (!rc) {
            trace_graph("updated %d properties for map [", updated);
            for (i = 0; i < map->num_src; i++) {
                printf("%s:%s, ", map->src[i]->sig->dev->name,
                       map->src[i]->sig->name);
            }
            printf("\b\b] -> [%s:%s]\n", map->dst->sig->dev->name,
                   map->dst->sig->name);
        }
#endif

        if (map->status < STATUS_ACTIVE)
            return map;
        if (rc || updated) {
            fptr_list cb = g->callbacks, temp;
            while (cb) {
                temp = cb->next;
                if (cb->types & MAPPER_OBJ_MAP) {
                    mapper_graph_handler *h = cb->f;
                    h(g, (mapper_object)map,
                      rc ? MAPPER_ADDED : MAPPER_MODIFIED, cb->context);
                }
                cb = temp;
            }
        }
    }

    return map;
}

static int cmp_query_maps_by_scope(const void *context_data, mapper_map map)
{
    mapper_id id = *(mapper_id*)context_data;
    for (int i = 0; i < map->num_scopes; i++) {
        if (map->scopes[i]) {
            if (map->scopes[i]->obj.id == id)
                return 1;
        }
        else if (0 == id)
            return 1;
    }
    return 0;
}

mapper_object *mapper_graph_get_maps_by_scope(mapper_graph g, mapper_device dev)
{
    return ((mapper_object *)
            mapper_list_new_query(g->maps, cmp_query_maps_by_scope,
                                  "h", dev ? dev->obj.id : 0));
}

void mapper_graph_remove_maps_by_query(mapper_graph g, mapper_object *maps,
                                       mapper_record_event event)
{
    while (maps) {
        mapper_map map = (mapper_map)*maps;
        maps = mapper_object_list_next(maps);
        mapper_graph_remove_map(g, map, event);
    }
}

void mapper_graph_remove_map(mapper_graph g, mapper_map map,
                             mapper_record_event event)
{
    if (!map)
        return;

    mapper_list_remove_item((void**)&g->maps, map);

    fptr_list cb = g->callbacks, temp;
    while (cb) {
        temp = cb->next;
        if (cb->types & MAPPER_OBJ_MAP) {
            mapper_graph_handler *h = cb->f;
            h(g, (mapper_object)map, event, cb->context);
        }
        cb = temp;
    }

    mapper_map_free(map);
    mapper_list_free_item(map);
}

void mapper_graph_remove_all_callbacks(mapper_graph g)
{
    fptr_list cb;
    while ((cb = g->callbacks)) {
        g->callbacks = g->callbacks->next;
        free(cb);
    }
}

void mapper_graph_print(mapper_graph g)
{
    printf("-------------------------------\n");
    printf("Registered devices (%d) and signals (%d):\n",
           mapper_graph_get_num_objects(g, MAPPER_OBJ_DEVICE),
           mapper_graph_get_num_objects(g, MAPPER_OBJ_SIGNAL));
    mapper_object *devs = mapper_list_from_data(g->devs);
    mapper_object *sigs;
    mapper_signal sig;
    while (devs) {
        printf(" └─ ");
        mapper_object_print(*devs, 0);
        sigs = mapper_device_get_signals((mapper_device)*devs, MAPPER_DIR_ANY);
        while (sigs) {
            sig = (mapper_signal)*sigs;
            sigs = mapper_object_list_next(sigs);
            printf("    %s ", sigs ? "├─" : "└─");
            mapper_object_print((mapper_object)sig, 0);
        }
        devs = mapper_object_list_next(devs);
    }

    printf("-------------------------------\n");
    printf("Registered maps (%d):\n",
           mapper_graph_get_num_objects(g, MAPPER_OBJ_MAP));
    mapper_object *maps = mapper_list_from_data(g->maps);
    while (maps) {
        printf(" └─ ");
        mapper_object_print(*maps, 0);
        int i;
        mapper_map map = (mapper_map)*maps;
        mapper_signal sig;
        for (i = 0; i < mapper_map_get_num_signals(map, MAPPER_LOC_SRC); i++) {
            sig = mapper_map_get_signal(map, MAPPER_LOC_SRC, i);
            printf("    ├─ SRC ");
            mapper_object_print((mapper_object)sig, 0);
        }
        for (i = 0; i < mapper_map_get_num_signals(map, MAPPER_LOC_DST); i++) {
            sig = mapper_map_get_signal(map, MAPPER_LOC_DST, i);
            printf("    └─ DST ");
            mapper_object_print((mapper_object)sig, 0);
        }
        maps = mapper_object_list_next(maps);
    }

    printf("-------------------------------\n");
}

static void set_network_dst(mapper_graph g, mapper_device dev)
{
    // TODO: look up device info, maybe send directly
    mapper_network_bus(&g->net);
}

static void send_subscribe_msg(mapper_graph g, mapper_device dev, int flags,
                               int timeout)
{
    char cmd[1024];
    snprintf(cmd, 1024, "/%s/subscribe", dev->name);

    set_network_dst(g, dev);
    lo_message msg = lo_message_new();
    if (msg) {
        if (MAPPER_OBJ_ALL == flags)
            lo_message_add_string(msg, "all");
        else {
            if (flags & MAPPER_OBJ_DEVICE)
                lo_message_add_string(msg, "device");
            if (MAPPER_OBJ_SIGNAL == (flags & MAPPER_OBJ_SIGNAL))
                lo_message_add_string(msg, "signals");
            else {
                if (flags & MAPPER_OBJ_INPUT_SIGNAL)
                    lo_message_add_string(msg, "inputs");
                else if (flags & MAPPER_OBJ_OUTPUT_SIGNAL)
                    lo_message_add_string(msg, "outputs");
            }
            if (MAPPER_OBJ_MAP == (flags & MAPPER_OBJ_MAP))
                lo_message_add_string(msg, "maps");
            else {
                if (flags & MAPPER_OBJ_MAP_IN)
                    lo_message_add_string(msg, "incoming_maps");
                else if (flags & MAPPER_OBJ_MAP_OUT)
                    lo_message_add_string(msg, "outgoing_maps");
            }
        }
        lo_message_add_string(msg, "@lease");
        lo_message_add_int32(msg, timeout);

        lo_message_add_string(msg, "@version");
        lo_message_add_int32(msg, dev->obj.version);

        mapper_network_add_msg(&g->net, cmd, 0, msg);
        mapper_network_send(&g->net);
    }
}

static void renew_subscriptions(mapper_graph g, uint32_t time_sec)
{
    // check if any subscriptions need to be renewed
    mapper_subscription s = g->subscriptions;
    while (s) {
        if (s->lease_expiration_sec <= time_sec) {
            trace_graph("automatically renewing subscription to %s for %d "
                        "seconds.\n", mapper_device_get_name(s->dev),
                        AUTOSUBSCRIBE_INTERVAL);
            send_subscribe_msg(g, s->dev, s->flags, AUTOSUBSCRIBE_INTERVAL);
            // leave 10-second buffer for subscription renewal
            s->lease_expiration_sec = (time_sec + AUTOSUBSCRIBE_INTERVAL - 10);
        }
        s = s->next;
    }
}

int mapper_graph_poll(mapper_graph g, int block_ms)
{
    mapper_network net = &g->net;
    int count = 0, status[2];
    mapper_time_t t;

    mapper_network_poll(net);
    mapper_time_now(&t);
    renew_subscriptions(g, t.sec);
    mapper_graph_check_device_status(g, t.sec);

    if (!block_ms) {
        if (lo_servers_recv_noblock(net->server.admin, status, 2, 0)) {
            count = (status[0] > 0) + (status[1] > 0);
            net->msgs_recvd |= count;
        }
        return count;
    }

    double then = mapper_get_current_time();
    int left_ms = block_ms, elapsed, checked_admin = 0;
    while (left_ms > 0) {
        if (left_ms > 100)
            left_ms = 100;

        if (lo_servers_recv_noblock(net->server.admin, status, 2, left_ms))
            count += (status[0] > 0) + (status[1] > 0);

        elapsed = (mapper_get_current_time() - then) * 1000;
        if ((elapsed - checked_admin) > 100) {
            mapper_time_now(&t);
            renew_subscriptions(g, t.sec);
            mapper_network_poll(net);
            mapper_graph_check_device_status(g, t.sec);
            checked_admin = elapsed;
        }

        left_ms = block_ms - elapsed;
    }

    net->msgs_recvd |= count;
    return count;
}

static void on_device_autosubscribe(mapper_graph g, mapper_object obj,
                                    mapper_record_event event, const void *user)
{
    // New subscriptions are handled in network.c as response to "sync" msg
    if (MAPPER_REMOVED == event) {
        mapper_graph_subscribe(g, (mapper_device)obj, 0, 0);
    }
}

static void mapper_graph_autosubscribe(mapper_graph g, int flags)
{
    if (!g->autosubscribe && flags) {
        mapper_graph_add_callback(g, on_device_autosubscribe, MAPPER_OBJ_DEVICE, g);
        // update flags for existing subscriptions
        mapper_time_t t;
        mapper_time_now(&t);
        mapper_subscription s = g->subscriptions;
        while (s) {
            trace_graph("adjusting flags for existing autorenewing subscription "
                        "to %s.\n", mapper_device_get_name(s->dev));
            if (flags & ~s->flags) {
                send_subscribe_msg(g, s->dev, flags, AUTOSUBSCRIBE_INTERVAL);
                // leave 10-second buffer for subscription renewal
                s->lease_expiration_sec = (t.sec + AUTOSUBSCRIBE_INTERVAL - 10);
            }
            s->flags = flags;
            s = s->next;
        }
        mapper_graph_request_devices(g);
    }
    else if (g->autosubscribe && !flags) {
        mapper_graph_remove_callback(g, on_device_autosubscribe, g);
        while (g->subscriptions) {
            mapper_graph_subscribe(g, g->subscriptions->dev, 0, 0);
        }
    }
#ifdef DEBUG
    trace_graph("setting autosubscribe flags to ");
    print_subscription_flags(flags);
#endif
    g->autosubscribe = flags;
}

static mapper_subscription subscription(mapper_graph g, mapper_device dev)
{
    mapper_subscription s = g->subscriptions;
    while (s) {
        if (s->dev == dev)
            return s;
        s = s->next;
    }
    return 0;
}

void mapper_graph_subscribe(mapper_graph g, mapper_device dev, int flags,
                            int timeout)
{
    if (!dev) {
        mapper_graph_autosubscribe(g, flags);
        return;
    }
    else if (dev->local) {
        // don't bother subscribing to local device
        trace_graph("aborting subscription, device is local.\n");
        return;
    }
    if (0 == flags || 0 == timeout) {
        mapper_subscription *s = &g->subscriptions;
        while (*s) {
            if ((*s)->dev == dev) {
                // remove from subscriber list
                (*s)->dev->subscribed = 0;
                mapper_subscription temp = *s;
                *s = temp->next;
                free(temp);
                send_subscribe_msg(g, dev, 0, 0);
                return;
            }
            s = &(*s)->next;
        }
    }
    else if (-1 == timeout) {
#ifdef DEBUG
        trace_graph("adding %d-second autorenewing subscription to device '%s' "
                    "with flags ", AUTOSUBSCRIBE_INTERVAL,
                    mapper_device_get_name(dev));
        print_subscription_flags(flags);
#endif
        // special case: autorenew subscription lease
        // first check if subscription already exists
        mapper_subscription s = subscription(g, dev);

        if (!s) {
            // store subscription record
            s = malloc(sizeof(struct _mapper_subscription));
            s->dev = dev;
            s->dev->obj.version = -1;
            s->next = g->subscriptions;
            g->subscriptions = s;
        }
        dev->subscribed = 1;
        if (s->flags == flags)
            return;

        s->dev->obj.version = -1;
        s->flags = flags;

        mapper_time_t t;
        mapper_time_now(&t);
        // leave 10-second buffer for subscription lease
        s->lease_expiration_sec = (t.sec + AUTOSUBSCRIBE_INTERVAL - 10);

        timeout = AUTOSUBSCRIBE_INTERVAL;
    }
#ifdef DEBUG
    else {
        trace_graph("adding temporary %d-second subscription to device '%s' "
                    "with flags ", timeout, mapper_device_get_name(dev));
        print_subscription_flags(flags);
    }
#endif

    send_subscribe_msg(g, dev, flags, timeout);
}

void mapper_graph_unsubscribe(mapper_graph g, mapper_device dev)
{
    if (!dev)
        mapper_graph_autosubscribe(g, MAPPER_OBJ_NONE);
    mapper_graph_subscribe(g, dev, 0, 0);
}

int mapper_graph_subscribed_by_dev_name(mapper_graph g, const char *name)
{
    mapper_device dev = mapper_graph_get_device_by_name(g, name);
    if (dev) {
        mapper_subscription s = subscription(g, dev);
        return s ? s->flags : 0;
    }
    return 0;
}

int mapper_graph_subscribed_by_sig_name(mapper_graph g, const char *name)
{
    // name needs to be split
    char *devnamep, *signame, devname[256];
    int devnamelen = mapper_parse_names(name, &devnamep, &signame);
    if (!devnamelen || devnamelen >= 256) {
        trace_graph("error extracting device name\n");
        return 0;
    }
    strncpy(devname, devnamep, devnamelen);
    devname[devnamelen] = 0;
    mapper_device dev = mapper_graph_get_device_by_name(g, devname);
    if (dev) {
        mapper_subscription s = subscription(g, dev);
        return s ? s->flags : 0;
    }
    return 0;
}

void mapper_graph_request_devices(mapper_graph g)
{
    lo_message msg = lo_message_new();
    if (!msg) {
        trace_graph("couldn't allocate lo_message\n");
        return;
    }
    trace_graph("pinging all devices.\n");
    mapper_network_bus(&g->net);
    mapper_network_add_msg(&g->net, 0, MSG_WHO, msg);
}

void mapper_graph_set_interface(mapper_graph g, const char *iface)
{
    mapper_network_init(&g->net, iface, 0, 0);
}

const char *mapper_graph_get_interface(mapper_graph g)
{
    return g->net.iface.name;
}

void mapper_graph_set_multicast_addr(mapper_graph g, const char *group, int port)
{
    mapper_network_init(&g->net, g->net.iface.name, group, port);
}

const char *mapper_graph_get_multicast_addr(mapper_graph g)
{
    return lo_address_get_url(g->net.addr.bus);
}
