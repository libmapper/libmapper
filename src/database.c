#include <string.h>
#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#include <zlib.h>
#include <sys/time.h>

#include "mapper_internal.h"

#define AUTOSUBSCRIBE_INTERVAL 60
extern const char* network_message_strings[NUM_MSG_STRINGS];

static void unsubscribe_internal(mapper_database db, mapper_device dev,
                                 int send_message);

int _flushing = 0;
int _timeout_sec = TIMEOUT_SEC;

#ifdef DEBUG
static void print_subscription_flags(int flags)
{
    printf("[");
    if (flags == MAPPER_OBJ_NONE) {
        printf("none\n");
        return;
    }

    if (flags & MAPPER_OBJ_DEVICES)
        printf("devices, ");

    if (flags & MAPPER_OBJ_INPUT_SIGNALS) {
        if (flags & MAPPER_OBJ_OUTPUT_SIGNALS)
            printf("signals, ");
        else
            printf("input signals, ");
    }
    else if (flags & MAPPER_OBJ_OUTPUT_SIGNALS)
        printf("output signals, ");

    if (flags & MAPPER_OBJ_INCOMING_LINKS) {
        if (flags & MAPPER_OBJ_OUTGOING_LINKS)
            printf("links, ");
        else
            printf("incoming links, ");
    }
    else if (flags & MAPPER_OBJ_OUTGOING_LINKS)
        printf("outgoing links, ");

    if (flags & MAPPER_OBJ_INCOMING_MAPS) {
        if (flags & MAPPER_OBJ_OUTGOING_MAPS)
            printf("maps, ");
        else
            printf("incoming maps, ");
    }
    else if (flags & MAPPER_OBJ_OUTGOING_MAPS)
        printf("outgoing maps, ");

    printf("\b\b]\n");
}
#endif

mapper_database mapper_database_new(mapper_network net, int subscribe_flags)
{
    if (!net)
        net = mapper_network_new(0, 0, 0);
    if (!net) {
        trace_db("error: no network.\n");
        return 0;
    }

    net->own_network = 0;
    mapper_database db = mapper_network_add_database(net);

    if (subscribe_flags) {
        mapper_database_subscribe(db, 0, subscribe_flags, -1);
    }
    return db;
}

void mapper_database_free(mapper_database db)
{
    if (!db)
        return;

    // remove callbacks now so they won't be called when removing devices
    mapper_database_remove_all_callbacks(db);

    mapper_network_remove_database(db->network);

    // unsubscribe from and remove any autorenewing subscriptions
    while (db->subscriptions) {
        mapper_database_unsubscribe(db, db->subscriptions->device);
    }

    /* Remove all non-local maps */
    mapper_map *maps = mapper_database_maps(db);
    while (maps) {
        mapper_map map = *maps;
        maps = mapper_map_query_next(maps);
        if (!map->local)
            mapper_database_remove_map(db, map, MAPPER_REMOVED);
    }

    /* Remove all non-local devices and signals from the database except for
     * those referenced by local maps. */
    mapper_device *devs = mapper_database_devices(db);
    while (devs) {
        mapper_device dev = *devs;
        devs = mapper_device_query_next(devs);
        if (dev->local)
            continue;

        int no_local_device_maps = 1;
        mapper_signal *sigs = mapper_device_signals(dev, MAPPER_DIR_ANY);
        while (sigs) {
            mapper_signal sig = *sigs;
            sigs = mapper_signal_query_next(sigs);
            int no_local_signal_maps = 1;
            mapper_map *maps = mapper_signal_maps(sig, MAPPER_DIR_ANY);
            while (maps) {
                if ((*maps)->local) {
                    no_local_device_maps = no_local_signal_maps = 0;
                    mapper_map_query_done(maps);
                    break;
                }
                maps = mapper_map_query_next(maps);
            }
            if (no_local_signal_maps)
                mapper_database_remove_signal(db, sig, MAPPER_REMOVED);
        }
        if (no_local_device_maps)
            mapper_database_remove_device(db, dev, MAPPER_REMOVED, 1);
    }

    if (!db->network->device && !db->network->own_network)
        mapper_network_free(db->network);
}

mapper_network mapper_database_network(mapper_database db)
{
    return db->network;
}

void mapper_database_set_timeout(mapper_database db, int timeout_sec)
{
    db->timeout_sec = (timeout_sec <= 0) ? TIMEOUT_SEC : timeout_sec;
}

int mapper_database_timeout(mapper_database db)
{
    return db->timeout_sec;
}

void mapper_database_flush(mapper_database db, int timeout_sec, int quiet)
{
    _timeout_sec = (timeout_sec <= 0) ? TIMEOUT_SEC : timeout_sec;
    _flushing = quiet + 1;
}

static int add_callback(fptr_list *head, const void *f, const void *user)
{
    fptr_list cb = *head;
    fptr_list prevcb = 0;
    while (cb) {
        if (cb->f == f && cb->context == user)
            return 0;
        prevcb = cb;
        cb = cb->next;
    }

    cb = (fptr_list)malloc(sizeof(struct _fptr_list));
    cb->f = (void*)f;
    cb->context = (void*)user;
    cb->next = *head;
    *head = cb;
    return 1;
}

static int remove_callback(fptr_list *head, const void *f, const void *user)
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
        return 0;

    if (prevcb)
        prevcb->next = cb->next;
    else
        *head = cb->next;

    free(cb);
    return 1;
}

/**** Device records ****/

mapper_device mapper_database_add_or_update_device(mapper_database db,
                                                   const char *name,
                                                   mapper_message msg)
{
    const char *no_slash = skip_slash(name);
    mapper_device dev = mapper_database_device_by_name(db, no_slash);
    int rc = 0, updated = 0;

    if (!dev) {
        trace_db("adding device '%s'.\n", name);
        dev = (mapper_device)mapper_list_add_item((void**)&db->devices,
                                                  sizeof(*dev));
        dev->name = strdup(no_slash);
        dev->id = crc32(0L, (const Bytef *)no_slash, strlen(no_slash));
        dev->id <<= 32;
        dev->database = db;
        init_device_prop_table(dev);
        rc = 1;
    }

    if (dev) {
        updated = mapper_device_set_from_message(dev, msg);
        if (!rc)
            trace_db("updated %d properties for device '%s'.\n", updated,
                  name);
        mapper_timetag_now(&dev->synced);

        if (rc || updated) {
            fptr_list cb = db->device_callbacks;
            while (cb) {
                mapper_database_device_handler *h = cb->f;
                h(db, dev, rc ? MAPPER_ADDED : MAPPER_MODIFIED, cb->context);
                cb = cb->next;
            }
        }
    }
    return dev;
}

// Internal function called by /logout protocol handler
void mapper_database_remove_device(mapper_database db, mapper_device dev,
                                   mapper_record_event event, int quiet)
{
    if (!dev)
        return;

    mapper_database_remove_maps_by_query(db,
                                         mapper_device_maps(dev, MAPPER_DIR_ANY),
                                         event);

    // remove matching maps scopes
    mapper_map *maps = mapper_database_maps_by_scope(db, dev);
    while (maps) {
        mapper_map_remove_scope(*maps, dev);
        maps = mapper_map_query_next(maps);
    }

    mapper_database_remove_links_by_query(db, mapper_device_links(dev,
                                                                  MAPPER_DIR_ANY),
                                          event);

    mapper_database_remove_signals_by_query(db, mapper_device_signals(dev,
                                                                MAPPER_DIR_ANY),
                                            event);

    mapper_list_remove_item((void**)&db->devices, dev);

    if (!quiet) {
        fptr_list cb = db->device_callbacks;
        while (cb) {
            mapper_database_device_handler *h = cb->f;
            h(db, dev, event, cb->context);
            cb = cb->next;
        }
    }

    if (dev->props)
        mapper_table_free(dev->props);
    if (dev->staged_props)
        mapper_table_free(dev->staged_props);
    if (dev->name)
        free(dev->name);
    mapper_list_free_item(dev);
}

int mapper_database_num_devices(mapper_database db)
{
    int count = 0;
    mapper_device dev = db->devices;
    while (dev) {
        ++count;
        dev = mapper_list_next(dev);
    }
    return count;
}

mapper_device *mapper_database_devices(mapper_database db)
{
    return mapper_list_from_data(db->devices);
}

mapper_device mapper_database_device_by_name(mapper_database db,
                                             const char *name)
{
    const char *no_slash = skip_slash(name);
    mapper_device dev = db->devices;
    while (dev) {
        if (strcmp(dev->name, no_slash)==0)
            return dev;
        dev = mapper_list_next(dev);
    }
    return 0;
}

mapper_device mapper_database_device_by_id(mapper_database db, mapper_id id)
{
    mapper_device dev = db->devices;
    while (dev) {
        if (id == dev->id)
            return dev;
        dev = mapper_list_next(dev);
    }
    return 0;
}

static int match_pattern(const char* string, const char* pattern)
{
    if (!string || !pattern)
        return 0;

    if (!strchr(pattern, '*'))
        return strcmp(string, pattern)==0;

    // 1) tokenize pattern using strtok() with delimiter character '*'
    // 2) use strstr() to check if token exists in offset string
    char *str = (char*)string, *tok;
    char dup[strlen(pattern)+1], *pat = dup;
    strcpy(pat, pattern);
    int ends_wild = pattern[strlen(pattern)-1]  == '*';
    while (str && *str) {
        tok = strtok(pat, "*");
        if (!tok)
            return ends_wild;
        str = strstr(str, tok);
        if (str && *str)
            str += strlen(tok);
        else
            return 0;
        // subsequent calls to strtok() need first argument to be NULL
        pat = NULL;
    }
    return 1;
}

static int cmp_query_devices_by_name(const void *context_data, mapper_device dev)
{
    const char *name = (const char*)context_data;
    return match_pattern(dev->name, name);
}

mapper_device *mapper_database_devices_by_name(mapper_database db,
                                               const char *name)
{
    return ((mapper_device *)
            mapper_list_new_query(db->devices, cmp_query_devices_by_name,
                                  "s", name));
}

static inline int check_type(char type)
{
    return strchr("ifdscth", type) != 0;
}

static int compare_value(mapper_op op, int length, char type, const void *val1,
                         const void *val2)
{
    int i, compare = 0, difference = 0;
    switch (type) {
        case 's':
            if (length == 1)
                compare = strcmp((const char*)val1, (const char*)val2);
            else {
                for (i = 0; i < length; i++) {
                    compare += strcmp(((const char**)val1)[i],
                                      ((const char**)val2)[i]);
                    difference += abs(compare);
                }
            }
            break;
        case 'i':
            for (i = 0; i < length; i++) {
                compare += ((int*)val1)[i] > ((int*)val2)[i];
                compare -= ((int*)val1)[i] < ((int*)val2)[i];
                difference += abs(compare);
            }
            break;
        case 'f':
            for (i = 0; i < length; i++) {
                compare += ((float*)val1)[i] > ((float*)val2)[i];
                compare -= ((float*)val1)[i] < ((float*)val2)[i];
                difference += abs(compare);
            }
            break;
        case 'd':
            for (i = 0; i < length; i++) {
                compare += ((double*)val1)[i] > ((double*)val2)[i];
                compare -= ((double*)val1)[i] < ((double*)val2)[i];
                difference += abs(compare);
            }
            break;
        case 'c':
            for (i = 0; i < length; i++) {
                compare += ((char*)val1)[i] > ((char*)val2)[i];
                compare -= ((char*)val1)[i] < ((char*)val2)[i];
                difference += abs(compare);
            }
            break;
        case 'h':
        case 't':
            for (i = 0; i < length; i++) {
                compare += ((uint64_t*)val1)[i] > ((uint64_t*)val2)[i];
                compare -= ((uint64_t*)val1)[i] < ((uint64_t*)val2)[i];
                difference += abs(compare);
            }
            break;
        default:
            return 0;
    }
    switch (op) {
        case MAPPER_OP_EQUAL:
            return compare == 0 && !difference;
        case MAPPER_OP_GREATER_THAN:
            return compare > 0;
        case MAPPER_OP_GREATER_THAN_OR_EQUAL:
            return compare >= 0;
        case MAPPER_OP_LESS_THAN:
            return compare < 0;
        case MAPPER_OP_LESS_THAN_OR_EQUAL:
            return compare <= 0;
        case MAPPER_OP_NOT_EQUAL:
            return compare != 0 || difference;
        default:
            return 0;
    }
}

static int cmp_query_devices_by_property(const void *context_data,
                                         mapper_device dev)
{
    int op = *(int*)context_data;
    int length = *(int*)(context_data + sizeof(int));
    char type = *(char*)(context_data + sizeof(int) * 2);
    void *value = *(void**)(context_data + sizeof(int) * 3);
    const char *name = (const char*)(context_data + sizeof(int) * 3
                                     + sizeof(void*));
    int _length;
    char _type;
    const void *_value;
    if (mapper_device_property(dev, name, &_length, &_type, &_value))
        return (op == MAPPER_OP_DOES_NOT_EXIST);
    if (op == MAPPER_OP_EXISTS)
        return 1;
    if (op == MAPPER_OP_DOES_NOT_EXIST)
        return 0;
    if (_type != type || _length != length)
        return 0;
    return compare_value(op, length, type, _value, value);
}

mapper_device *mapper_database_devices_by_property(mapper_database db,
                                                   const char *name, int length,
                                                   char type, const void *value,
                                                   mapper_op op)
{
    if (!name || !check_type(type) || length < 1)
        return 0;
    if (op <= MAPPER_OP_UNDEFINED || op >= NUM_MAPPER_OPS)
        return 0;
    return ((mapper_device *)
            mapper_list_new_query(db->devices, cmp_query_devices_by_property,
                                  "iicvs", op, length, type, &value, name));
}

int mapper_database_add_device_callback(mapper_database db,
                                        mapper_database_device_handler *h,
                                        const void *user)
{
    return add_callback(&db->device_callbacks, h, user);
}

int mapper_database_remove_device_callback(mapper_database db,
                                           mapper_database_device_handler *h,
                                           const void *user)
{
    return remove_callback(&db->device_callbacks, h, user);
}

void mapper_database_check_device_status(mapper_database db, uint32_t time_sec)
{
    time_sec -= db->timeout_sec;
    mapper_device dev = db->devices;
    while (dev) {
        // check if device has "checked in" recently
        // this could be /sync ping or any sent metadata
        if (dev->synced.sec && (dev->synced.sec < time_sec)) {
            fptr_list cb = db->device_callbacks;
            while (cb) {
                mapper_database_device_handler *h = cb->f;
                h(db, dev, MAPPER_EXPIRED, cb->context);
                cb = cb->next;
            }
        }
        dev = mapper_list_next(dev);
    }
}

mapper_device mapper_database_expired_device(mapper_database db,
                                             uint32_t last_ping)
{
    mapper_device dev = db->devices;
    while (dev) {
        if (dev->synced.sec && (dev->synced.sec < last_ping)) {
            return dev;
        }
        dev = mapper_list_next(dev);
    }
    return 0;
}

/**** Signals ****/

mapper_signal mapper_database_add_or_update_signal(mapper_database db,
                                                   const char *name,
                                                   const char *device_name,
                                                   mapper_message msg)
{
    mapper_signal sig = 0;
    int sig_rc = 0, dev_rc = 0, updated = 0;

    mapper_device dev = mapper_database_device_by_name(db, device_name);
    if (dev) {
        sig = mapper_device_signal_by_name(dev, name);
        if (sig && sig->local)
            return sig;
    }
    else {
        dev = mapper_database_add_or_update_device(db, device_name, 0);
        dev_rc = 1;
    }

    if (!sig) {
        trace_db("adding signal '%s:%s'.\n", device_name, name);
        sig = (mapper_signal)mapper_list_add_item((void**)&db->signals,
                                                  sizeof(mapper_signal_t));

        // also add device record if necessary
        sig->device = dev;

        // Defaults (int, length=1)
        mapper_signal_init(sig, 0, 0, name, 0, 0, 0, 0, 0, 0, 0);

        sig_rc = 1;
    }

    if (sig) {
        updated = mapper_signal_set_from_message(sig, msg);
        if (!sig_rc)
            trace_db("updated %d properties for signal '%s:%s'.\n", updated,
                     device_name, name);

        if (sig_rc || updated) {
            // TODO: Should we really allow callbacks to free themselves?
            fptr_list cb = db->signal_callbacks, temp;
            while (cb) {
                temp = cb->next;
                mapper_database_signal_handler *h = cb->f;
                h(db, sig, sig_rc ? MAPPER_ADDED : MAPPER_MODIFIED, cb->context);
                cb = temp;
            }
        }
    }
    return sig;
}

int mapper_database_add_signal_callback(mapper_database db,
                                        mapper_database_signal_handler *h,
                                        const void *user)
{
    return add_callback(&db->signal_callbacks, h, user);
}

int mapper_database_remove_signal_callback(mapper_database db,
                                           mapper_database_signal_handler *h,
                                           const void *user)
{
    return remove_callback(&db->signal_callbacks, h, user);
}

static int cmp_query_signals(const void *context_data, mapper_signal sig)
{
    int dir = *(int*)context_data;
    return dir & sig->direction;
}

int mapper_database_num_signals(mapper_database db, mapper_direction dir)
{
    int count = 0;
    mapper_signal sig = db->signals;
    while (sig) {
        if (dir & sig->direction)
            ++count;
        sig = mapper_list_next(sig);
    }
    return count;
}

mapper_signal *mapper_database_signals(mapper_database db, mapper_direction dir)
{
    if (dir == MAPPER_DIR_ANY)
        return mapper_list_from_data(db->signals);
    return ((mapper_signal *)
            mapper_list_new_query(db->signals, cmp_query_signals, "i", dir));
}

mapper_signal mapper_database_signal_by_id(mapper_database db, mapper_id id)
{
    mapper_signal sig = db->signals;
    if (!sig)
        return 0;

    while (sig) {
        if (sig->id == id)
            return sig;
        sig = mapper_list_next(sig);
    }
    return 0;
}

static int cmp_query_signals_by_name(const void *context_data,
                                     mapper_signal sig)
{
    int dir = *(int*)context_data;
    const char *name = (const char*)(context_data + sizeof(int));
    return ((dir & sig->direction) && (match_pattern(sig->name, name)));
}

mapper_signal *mapper_database_signals_by_name(mapper_database db,
                                               const char *name)
{
    return ((mapper_signal *)
            mapper_list_new_query(db->signals, cmp_query_signals_by_name,
                                  "is", MAPPER_DIR_ANY, name));
}

static int cmp_query_signals_by_property(const void *context_data,
                                         mapper_signal sig)
{
    int op = *(int*)context_data;
    int length = *(int*)(context_data + sizeof(int));
    char type = *(char*)(context_data + sizeof(int) * 2);
    void *value = *(void**)(context_data + sizeof(int) * 3);
    const char *name = (const char*)(context_data + sizeof(int) * 3
                                     + sizeof(void*));
    int _length;
    char _type;
    const void *_value;
    if (mapper_signal_property(sig, name, &_length, &_type, &_value))
        return (op == MAPPER_OP_DOES_NOT_EXIST);
    if (op == MAPPER_OP_EXISTS)
        return 1;
    if (op == MAPPER_OP_DOES_NOT_EXIST)
        return 0;
    if (_type != type || _length != length)
        return 0;
    return compare_value(op, length, type, _value, value);
}

mapper_signal *mapper_database_signals_by_property(mapper_database db,
                                                   const char *name, int length,
                                                   char type, const void *value,
                                                   mapper_op op)
{
    if (!name || !check_type(type) || length < 1)
        return 0;
    if (op <= MAPPER_OP_UNDEFINED || op >= NUM_MAPPER_OPS)
        return 0;
    return ((mapper_signal *)
            mapper_list_new_query(db->signals, cmp_query_signals_by_property,
                                  "iicvs", op, length, type, &value, name));
}

void mapper_database_remove_signal(mapper_database db, mapper_signal sig,
                                   mapper_record_event event)
{
    // remove any stored maps using this signal
    mapper_database_remove_maps_by_query(db,
                                         mapper_signal_maps(sig, MAPPER_DIR_ANY),
                                         event);

    mapper_list_remove_item((void**)&db->signals, sig);

    fptr_list cb = db->signal_callbacks;
    while (cb) {
        mapper_database_signal_handler *h = cb->f;
        h(db, sig, event, cb->context);
        cb = cb->next;
    }

    if (sig->direction & MAPPER_DIR_INCOMING)
        --sig->device->num_inputs;
    if (sig->direction & MAPPER_DIR_OUTGOING)
        --sig->device->num_outputs;

    mapper_signal_free(sig);
    mapper_list_free_item(sig);
}

void mapper_database_remove_signals_by_query(mapper_database db,
                                             mapper_signal *query,
                                             mapper_record_event event)
{
    while (query) {
        mapper_signal sig = *query;
        query = mapper_signal_query_next(query);
        if (!sig->local)
            mapper_database_remove_signal(db, sig, event);
    }
}

/**** Link records ****/

static void mapper_database_call_link_handlers(mapper_database db,
                                               mapper_link link,
                                               mapper_record_event event)
{
    if (!db || !link)
        return;

    fptr_list cb = db->link_callbacks, temp;
    while (cb) {
        temp = cb->next;
        mapper_database_link_handler *h = cb->f;
        h(db, link, event, cb->context);
        cb = temp;
    }
}

mapper_link mapper_database_add_or_update_link(mapper_database db,
                                               mapper_device dev1,
                                               mapper_device dev2,
                                               mapper_message msg)
{
    if (!dev1 || !dev2)
        return 0;

    int updated, rc = 0;

    mapper_link link = mapper_device_link_by_remote_device(dev1, dev2);
    if (!link) {
        trace_db("adding link '%s' <-> '%s'.\n", dev1->name, dev2->name);

        link = (mapper_link)mapper_list_add_item((void**)&db->links,
                                                 sizeof(mapper_link_t));
        if (dev2->local) {
            link->local_device = dev2;
            link->remote_device = dev1;
        }
        else {
            link->local_device = dev1;
            link->remote_device = dev2;
        }
        mapper_link_init(link, 0);
        rc = 1;
    }
    if (link) {
        updated = mapper_link_set_from_message(link, msg,
                                               link->devices[0] != dev1);
        if (!rc)
            trace_db("updated %d properties for link '%s' <-> '%s'.\n", updated,
                     dev1->name, dev2->name);
        if (rc || updated)
            mapper_database_call_link_handlers(db, link,
                                               rc ? MAPPER_ADDED : MAPPER_MODIFIED);
    }
    return link;
}

int mapper_database_update_link(mapper_database db, mapper_link link,
                                mapper_device reporting_dev,
                                mapper_message msg)
{
    if (!link)
        return 0;

    int updated = mapper_link_set_from_message(link, msg,
                                               link->devices[0] != reporting_dev);
    if (updated) {
        trace_db("updated %d properties for link '%s' <-> '%s'.\n", updated,
                 link->devices[0]->name, link->devices[1]->name);
        mapper_database_call_link_handlers(db, link, MAPPER_MODIFIED);
    }
    return updated;
}

int mapper_database_add_link_callback(mapper_database db,
                                      mapper_database_link_handler *h,
                                      const void *user)
{
    return add_callback(&db->link_callbacks, h, user);
}

int mapper_database_remove_link_callback(mapper_database db,
                                         mapper_database_link_handler *h,
                                         const void *user)
{
    return remove_callback(&db->link_callbacks, h, user);
}

int mapper_database_num_links(mapper_database db)
{
    int count = 0;
    mapper_link link = db->links;
    while (link) {
        ++count;
        link = mapper_list_next(link);
    }
    return count;
}

mapper_link *mapper_database_links(mapper_database db)
{
    return mapper_list_from_data(db->links);
}

mapper_link mapper_database_link_by_id(mapper_database db, mapper_id id)
{
    mapper_link link = db->links;
    while (link) {
        if (link->id == id)
            return link;
        link = mapper_list_next(link);
    }
    return 0;
}

static int cmp_query_links_by_property(const void *context_data, mapper_link link)
{
    int op = *(int*)context_data;
    int length = *(int*)(context_data + sizeof(int));
    char type = *(char*)(context_data + sizeof(int) * 2);
    void *value = *(void**)(context_data + sizeof(int) * 3);
    const char *name = (const char*)(context_data + sizeof(int) * 3
                                     + sizeof(void*));
    int _length;
    char _type;
    const void *_value;
    if (mapper_link_property(link, name, &_length, &_type, &_value))
        return (op == MAPPER_OP_DOES_NOT_EXIST);
    if (op == MAPPER_OP_EXISTS)
        return 1;
    if (op == MAPPER_OP_DOES_NOT_EXIST)
        return 0;
    if (_type != type || _length != length)
        return 0;
    return compare_value(op, length, type, _value, value);
}

mapper_link *mapper_database_links_by_property(mapper_database db,
                                               const char *name, int length,
                                               char type, const void *value,
                                               mapper_op op)
{
    if (!name || !check_type(type) || length < 1)
        return 0;
    if (op <= MAPPER_OP_UNDEFINED || op >= NUM_MAPPER_OPS)
        return 0;
    return ((mapper_link *)
            mapper_list_new_query(db->links, cmp_query_links_by_property,
                                  "iicvs", op, length, type, &value, name));
}

void mapper_database_remove_links_by_query(mapper_database db,
                                           mapper_link *links,
                                           mapper_record_event event)
{
    while (links) {
        mapper_link link = *links;
        links = mapper_link_query_next(links);
        mapper_database_remove_link(db, link, event);
    }
}

void mapper_database_remove_link(mapper_database db, mapper_link link,
                                 mapper_record_event event)
{
    if (!link)
        return;

    mapper_database_remove_maps_by_query(db, mapper_link_maps(link), event);

    mapper_list_remove_item((void**)&db->links, link);

    fptr_list cb = db->link_callbacks;
    while (cb) {
        mapper_database_link_handler *h = cb->f;
        h(db, link, event, cb->context);
        cb = cb->next;
    }

    // TODO: also clear network info from remote devices?

    mapper_link_free(link);
    mapper_list_free_item(link);
}

/**** Map records ****/

static int compare_slot_names(const void *l, const void *r)
{
    int result = strcmp((*(mapper_slot*)l)->signal->device->name,
                        (*(mapper_slot*)r)->signal->device->name);
    if (result == 0)
        return strcmp((*(mapper_slot*)l)->signal->name,
                      (*(mapper_slot*)r)->signal->name);
    return result;
}

static mapper_signal add_sig_from_whole_name(mapper_database db,
                                             const char* name)
{
    char *devnamep, *signame, devname[256];
    int devnamelen = mapper_parse_names(name, &devnamep, &signame);
    if (!devnamelen || devnamelen >= 256) {
        trace_db("error extracting device name\n");
        return 0;
    }
    strncpy(devname, devnamep, devnamelen);
    devname[devnamelen] = 0;
    return mapper_database_add_or_update_signal(db, signame, devname, 0);
}

mapper_map mapper_database_add_or_update_map(mapper_database db, int num_sources,
                                             const char **src_names,
                                             const char *dst_name,
                                             mapper_message msg)
{
    if (num_sources > MAX_NUM_MAP_SOURCES) {
        trace_db("error: maximum mapping sources exceeded.\n");
        return 0;
    }

    mapper_map map = 0;
    int rc = 0, updated = 0, i, j;

    /* We could be part of larger "convergent" mapping, so we will retrieve
     * record by mapping id instead of names. */
    uint64_t id = 0;
    if (msg) {
        mapper_message_atom atom = mapper_message_property(msg, AT_ID);
        if (!atom || atom->types[0] != 'h') {
            trace_db("no 'id' property found in map metadata, aborting.\n");
            return 0;
        }
        id = atom->values[0]->i64;
        map = mapper_database_map_by_id(db, id);
        if (!map && mapper_database_map_by_id(db, 0)) {
            // may have staged map stored locally
            map = db->maps;
            while (map) {
                int i, found = 1;
                if (map->num_sources != num_sources)
                    found = 0;
                else if (mapper_slot_match_full_name(&map->destination,
                                                     dst_name))
                    found = 0;
                for (i = 0; i < num_sources; i++) {
                    if (mapper_slot_match_full_name(map->sources[i],
                                                    src_names[i])) {
                        found = 0;
                        break;
                    }
                }
                if (found)
                    break;
                map = mapper_list_next(map);
            }
        }
    }

    if (!map) {
#ifdef DEBUG
        trace_db("adding map [");
        for (i = 0; i < num_sources; i++)
            printf("%s, ", src_names[i]);
        if (num_sources > 1)
            printf("\b\b");
        printf("] -> [%s].\n", dst_name);
#endif
        // add signals first in case signal handlers trigger map queries
        mapper_signal dst_sig = add_sig_from_whole_name(db, dst_name);
        if (!dst_sig)
            return 0;
        mapper_signal src_sigs[num_sources];
        for (i = 0; i < num_sources; i++) {
            src_sigs[i] = add_sig_from_whole_name(db, src_names[i]);
            if (!src_sigs[i])
                return 0;
            mapper_database_add_or_update_link(db, dst_sig->device,
                                               src_sigs[i]->device, 0);
        }

        map = (mapper_map)mapper_list_add_item((void**)&db->maps,
                                               sizeof(mapper_map_t));
        map->database = db;
        map->id = id;
        map->num_sources = num_sources;
        map->sources = (mapper_slot*) malloc(sizeof(mapper_slot) * num_sources);
        for (i = 0; i < num_sources; i++) {
            map->sources[i] = (mapper_slot) calloc(1, sizeof(struct _mapper_slot));
            map->sources[i]->signal = src_sigs[i];
            map->sources[i]->id = i;
            map->sources[i]->causes_update = 1;
            map->sources[i]->map = map;
            if (map->sources[i]->signal->local) {
                map->sources[i]->num_instances = map->sources[i]->signal->num_instances;
                map->sources[i]->use_instances = map->sources[i]->num_instances > 1;
            }
        }
        map->destination.signal = dst_sig;
        map->destination.causes_update = 1;
        map->destination.map = map;
        if (map->destination.signal->local) {
            map->destination.num_instances = map->destination.signal->num_instances;
            map->destination.use_instances = map->destination.num_instances > 1;
        }

        mapper_map_init(map);
        rc = 1;
    }
    else {
        int changed = 0;
        // may need to add sources to existing map
        for (i = 0; i < num_sources; i++) {
            mapper_signal src_sig = add_sig_from_whole_name(db, src_names[i]);
            if (!src_sig)
                return 0;
            for (j = 0; j < map->num_sources; j++) {
                if (map->sources[j]->signal == src_sig) {
                    break;
                }
            }
            if (j == map->num_sources) {
                ++changed;
                ++map->num_sources;
                map->sources = realloc(map->sources, sizeof(struct _mapper_slot)
                                       * map->num_sources);
                map->sources[j] = (mapper_slot) calloc(1, sizeof(struct _mapper_slot));
                map->sources[j]->direction = MAPPER_DIR_OUTGOING;
                map->sources[j]->signal = src_sig;
                map->sources[j]->causes_update = 1;
                map->sources[j]->map = map;
                if (map->sources[j]->signal->local) {
                    map->sources[j]->num_instances = map->sources[j]->signal->num_instances;
                    map->sources[j]->use_instances = map->sources[j]->num_instances > 1;
                }
                mapper_slot_init(map->sources[j]);
                ++updated;
            }
        }
        if (changed) {
            // slots should be in alphabetical order
            qsort(map->sources, map->num_sources, sizeof(mapper_slot),
                  compare_slot_names);
            // fix slot ids
            mapper_table tab;
            mapper_table_record_t *rec;
            for (i = 0; i < num_sources; i++) {
                map->sources[i]->id = i;
                // also need to correct slot table indices
                tab = map->sources[i]->props;
                for (j = 0; j < tab->num_records; j++) {
                    rec = &tab->records[j];
                    rec->index = MASK_PROP_BITFLAGS(rec->index) | SRC_SLOT_PROPERTY(i);
                }
                tab = map->sources[i]->staged_props;
                for (j = 0; j < tab->num_records; j++) {
                    rec = &tab->records[j];
                    rec->index = MASK_PROP_BITFLAGS(rec->index) | SRC_SLOT_PROPERTY(i);
                }
            }
            // check again if this mirrors a staged map
            mapper_map next = db->maps, map2;
            while (next) {
                map2 = next;
                next = mapper_list_next(next);
                if (map2->id != 0)
                    continue;
                if (map->num_sources != map2->num_sources)
                    continue;
                if (map->destination.signal != map2->destination.signal)
                    continue;
                for (i = 0; i < map->num_sources; i++) {
                    if (map->sources[i]->signal != map2->sources[i]->signal) {
                        map2 = NULL;
                        break;
                    }
                }
                if (map2) {
                    mapper_database_remove_map(db, map2, 0);
                    break;
                }
            }
        }
    }

    if (map) {
        updated += mapper_map_set_from_message(map, msg, 0);
#ifdef DEBUG
        if (!rc) {
            trace_db("updated %d properties for map [", updated);
            for (i = 0; i < map->num_sources; i++) {
                printf("%s:%s, ", map->sources[i]->signal->device->name,
                       map->sources[i]->signal->name);
            }
            printf("\b\b] -> [%s:%s]\n", map->destination.signal->device->name,
                   map->destination.signal->name);
        }
#endif

        if (map->status < STATUS_ACTIVE)
            return map;
        if (rc || updated) {
            fptr_list cb = db->map_callbacks;
            while (cb) {
                mapper_database_map_handler *h = cb->f;
                h(db, map, rc ? MAPPER_ADDED : MAPPER_MODIFIED, cb->context);
                cb = cb->next;
            }
        }
    }

    return map;
}

int mapper_database_add_map_callback(mapper_database db,
                                     mapper_database_map_handler *h,
                                     const void *user)
{
    return add_callback(&db->map_callbacks, h, user);
}

int mapper_database_remove_map_callback(mapper_database db,
                                        mapper_database_map_handler *h,
                                        const void *user)
{
    return remove_callback(&db->map_callbacks, h, user);
}

int mapper_database_num_maps(mapper_database db)
{
    int count = 0;
    mapper_map map = db->maps;
    while (map) {
        ++count;
        map = mapper_list_next(map);
    }
    return count;
}

void mapper_database_cleanup(mapper_database db)
{
    if (db->staged_maps <= 0)
        return;
    int staged = 0;
    mapper_map *maps = mapper_database_maps(db);
    while (maps) {
        mapper_map map = *maps;
        maps = mapper_map_query_next(maps);
        if (map->status <= STATUS_STAGED) {
            if (map->status <= STATUS_EXPIRED)
                mapper_database_remove_map(db, map, MAPPER_REMOVED);
            else {
                --map->status;
                ++staged;
            }
        }
    }
    db->staged_maps = staged;
}

mapper_map *mapper_database_maps(mapper_database db)
{
    return mapper_list_from_data(db->maps);
}

mapper_map mapper_database_map_by_id(mapper_database db, mapper_id id)
{
    mapper_map map = db->maps;
    while (map) {
        if (map->id == id)
            return map;
        map = mapper_list_next(map);
    }
    return 0;
}

static int cmp_query_maps_by_scope(const void *context_data, mapper_map map)
{
    mapper_id id = *(mapper_id*)context_data;
    for (int i = 0; i < map->num_scopes; i++) {
        if (map->scopes[i]) {
            if (map->scopes[i]->id == id)
                return 1;
        }
        else if (id == 0)
            return 1;
    }
    return 0;
}

mapper_map *mapper_database_maps_by_scope(mapper_database db, mapper_device dev)
{
    return ((mapper_map *)
            mapper_list_new_query(db->maps, cmp_query_maps_by_scope,
                                  "h", dev ? dev->id : 0));
}

static int cmp_query_maps_by_property(const void *context_data, mapper_map map)
{
    int op = *(int*)context_data;
    int length = *(int*)(context_data + sizeof(int));
    char type = *(char*)(context_data + sizeof(int) * 2);
    void *value = *(void**)(context_data + sizeof(int) * 3);
    const char *name = (const char*)(context_data + sizeof(int) * 3
                                     + sizeof(void*));
    int _length;
    char _type;
    const void *_value;
    if (mapper_map_property(map, name, &_length, &_type, &_value))
        return (op == MAPPER_OP_DOES_NOT_EXIST);
    if (op == MAPPER_OP_EXISTS)
        return 1;
    if (op == MAPPER_OP_DOES_NOT_EXIST)
        return 0;
    if (_type != type || _length != length)
        return 0;
    return compare_value(op, length, type, _value, value);
}

mapper_map *mapper_database_maps_by_property(mapper_database db,
                                             const char *name, int length,
                                             char type, const void *value,
                                             mapper_op op)
{
    if (!name || !check_type(type) || length < 1)
        return 0;
    if (op <= MAPPER_OP_UNDEFINED || op >= NUM_MAPPER_OPS)
        return 0;
    return ((mapper_map *)
            mapper_list_new_query(db->maps, cmp_query_maps_by_property,
                                  "iicvs", op, length, type, &value, name));
}

static int cmp_query_maps_by_slot_property(const void *context_data,
                                           mapper_map map)
{
    int i, op = *(int*)(context_data);
    int length2, length1 = *(int*)(context_data + sizeof(int));
    char type2, type1 = *(char*)(context_data + sizeof(int) * 2);
    const void *value2, *value1 = *(void**)(context_data + sizeof(int) * 3);
    const char *name = (const char*)(context_data + sizeof(int) * 3
                                     + sizeof(void*));
    // check destination slot
    if (!mapper_slot_property(&map->destination, name, &length2, &type2, &value2)
        && type1 == type2 && length1 == length2
        && compare_value(op, length1, type1, value2, value1)) {
        return 1;
    }
    // check source slots
    for (i = 0; i < map->num_sources; i++) {
        if (!mapper_slot_property(map->sources[i], name, &length2, &type2, &value2)
            && type1 == type2 && length1 == length2
            && compare_value(op, length1, type1, value2, value1))
            return 1;
    }
    return 0;
}

mapper_map *mapper_database_maps_by_slot_property(mapper_database db,
                                                  const char *name, int length,
                                                  char type, const void *value,
                                                  mapper_op op)
{
    if (!name || !check_type(type) || length < 1)
        return 0;
    if (op <= MAPPER_OP_UNDEFINED || op >= NUM_MAPPER_OPS)
        return 0;
    return ((mapper_map *)
            mapper_list_new_query(db->maps, cmp_query_maps_by_slot_property,
                                  "iicvs", op, length, type, &value, name));
}

void mapper_database_remove_maps_by_query(mapper_database db, mapper_map *maps,
                                          mapper_record_event event)
{
    while (maps) {
        mapper_map map = *maps;
        maps = mapper_map_query_next(maps);
        mapper_database_remove_map(db, map, event);
    }
}

void mapper_database_remove_map(mapper_database db, mapper_map map,
                                mapper_record_event event)
{
    if (!map)
        return;

    mapper_list_remove_item((void**)&db->maps, map);

    if (map->id != 0) {
        fptr_list cb = db->map_callbacks;
        while (cb) {
            mapper_database_map_handler *h = cb->f;
            h(db, map, event, cb->context);
            cb = cb->next;
        }

        // decrement num_maps property of relevant links
        mapper_device src = 0, dst = map->destination.signal->device;
        int i;
        for (i = 0; i < map->num_sources; i++) {
            src = map->sources[i]->signal->device;
            mapper_link link = mapper_device_link_by_remote_device(dst, src);
            if (link && !link->local) {
                --link->num_maps[src == link->devices[0] ? 0 : 1];
                link->props->dirty = 1;
                mapper_database_call_link_handlers(db, link, MAPPER_MODIFIED);
            }
        }
    }

    mapper_map_free(map);
    mapper_list_free_item(map);
}

void mapper_database_remove_all_callbacks(mapper_database db)
{
    fptr_list cb;
    while ((cb = db->device_callbacks)) {
        db->device_callbacks = db->device_callbacks->next;
        free(cb);
    }
    while ((cb = db->signal_callbacks)) {
        db->signal_callbacks = db->signal_callbacks->next;
        free(cb);
    }
    while ((cb = db->link_callbacks)) {
        db->link_callbacks = db->link_callbacks->next;
        free(cb);
    }
    while ((cb = db->map_callbacks)) {
        db->map_callbacks = db->map_callbacks->next;
        free(cb);
    }
}

void mapper_database_print(mapper_database db)
{
#ifdef DEBUG
    mapper_device dev = db->devices;
    printf("Registered devices:\n");
    while (dev) {
        mapper_device_print(dev);
        dev = mapper_list_next(dev);
    }

    mapper_signal sig = db->signals;
    printf("Registered signals:\n");
    while (sig) {
        mapper_signal_print(sig, 1);
        sig = mapper_list_next(sig);
    }

    mapper_link link = db->links;
    printf("Registered links:\n");
    while (link) {
        mapper_link_print(link);
        link = mapper_list_next(link);
    }

    mapper_map map = db->maps;
    printf("Registered maps:\n");
    while (map) {
        mapper_map_print(map);
        map = mapper_list_next(map);
    }
#endif
}

static void set_network_dest(mapper_database db, mapper_device dev)
{
    // TODO: look up device info, maybe send directly
    mapper_network_set_dest_bus(db->network);
}

static void subscribe_internal(mapper_database db, mapper_device dev, int flags,
                               int timeout)
{
    char cmd[1024];
    snprintf(cmd, 1024, "/%s/subscribe", dev->name);

    set_network_dest(db, dev);
    lo_message msg = lo_message_new();
    if (msg) {
        if (flags == MAPPER_OBJ_ALL)
            lo_message_add_string(msg, "all");
        else {
            if (flags & MAPPER_OBJ_DEVICES)
                lo_message_add_string(msg, "device");
            if ((flags & MAPPER_OBJ_SIGNALS) == MAPPER_OBJ_SIGNALS)
                lo_message_add_string(msg, "signals");
            else {
                if (flags & MAPPER_OBJ_INPUT_SIGNALS)
                    lo_message_add_string(msg, "inputs");
                else if (flags & MAPPER_OBJ_OUTPUT_SIGNALS)
                    lo_message_add_string(msg, "outputs");
            }
            if ((flags & MAPPER_OBJ_LINKS) == MAPPER_OBJ_LINKS)
                lo_message_add_string(msg, "links");
            else {
                if (flags & MAPPER_OBJ_INCOMING_LINKS)
                    lo_message_add_string(msg, "incoming_links");
                else if (flags & MAPPER_OBJ_OUTGOING_LINKS)
                    lo_message_add_string(msg, "outgoing_links");
            }
            if ((flags & MAPPER_OBJ_MAPS) == MAPPER_OBJ_MAPS)
                lo_message_add_string(msg, "maps");
            else {
                if (flags & MAPPER_OBJ_INCOMING_MAPS)
                    lo_message_add_string(msg, "incoming_maps");
                else if (flags & MAPPER_OBJ_OUTGOING_MAPS)
                    lo_message_add_string(msg, "outgoing_maps");
            }
        }
        lo_message_add_string(msg, "@lease");
        lo_message_add_int32(msg, timeout);

        lo_message_add_string(msg, "@version");
        lo_message_add_int32(msg, dev->version);

        mapper_network_add_message(db->network, cmd, 0, msg);
        mapper_network_send(db->network);
    }
}

static void unsubscribe_internal(mapper_database db, mapper_device dev,
                                 int send_message)
{
    char cmd[1024];
    // check if autorenewing subscription exists
    mapper_subscription *s = &db->subscriptions;
    while (*s) {
        if ((*s)->device == dev) {
            if (send_message) {
                snprintf(cmd, 1024, "/%s/unsubscribe", dev->name);
                set_network_dest(db, dev);
                lo_message m = lo_message_new();
                if (!m) {
                    trace_db("couldn't allocate lo_message\n");
                    break;
                }
                mapper_network_add_message(db->network, cmd, 0, m);
                mapper_network_send(db->network);
            }
            // remove from subscriber list
            (*s)->device->subscribed = 0;
            mapper_subscription temp = *s;
            *s = temp->next;
            free(temp);
            return;
        }
        s = &(*s)->next;
    }
}

static void renew_subscriptions(mapper_database db, uint32_t time_sec)
{
    // check if any subscriptions need to be renewed
    mapper_subscription s = db->subscriptions;
    while (s) {
        if (s->lease_expiration_sec <= time_sec) {
            trace_db("automatically renewing subscription to %s for %d "
                     "seconds.\n", mapper_device_name(s->device),
                     AUTOSUBSCRIBE_INTERVAL);
            subscribe_internal(db, s->device, s->flags, AUTOSUBSCRIBE_INTERVAL);
            // leave 10-second buffer for subscription renewal
            s->lease_expiration_sec = (time_sec + AUTOSUBSCRIBE_INTERVAL - 10);
        }
        s = s->next;
    }
}

int mapper_database_poll(mapper_database db, int block_ms)
{
    mapper_network net = db->network;
    int count = 0, status[2];
    mapper_timetag_t tt;

    lo_server servers[2] = { net->bus_server, net->mesh_server };

    mapper_network_poll(net);
    mapper_timetag_now(&tt);
    renew_subscriptions(db, tt.sec);
    mapper_database_check_device_status(db, tt.sec);

    if (!block_ms) {
        if (lo_servers_recv_noblock(servers, status, 2, 0)) {
            count = status[0] + status[1];
            net->msgs_recvd |= count;
        }
        return count;
    }

    double then = mapper_get_current_time();
    int left_ms = block_ms, elapsed, checked_admin = 0;
    while (left_ms > 0) {
        if (left_ms > 100)
            left_ms = 100;

        if (lo_servers_recv_noblock(servers, status, 2, left_ms))
            count += status[0] + status[1];

        elapsed = (mapper_get_current_time() - then) * 1000;
        if ((elapsed - checked_admin) > 100) {
            mapper_timetag_now(&tt);
            renew_subscriptions(db, tt.sec);
            mapper_network_poll(net);
            mapper_database_check_device_status(db, tt.sec);
            checked_admin = elapsed;
        }

        left_ms = block_ms - elapsed;
    }

    net->msgs_recvd |= count;

    if (_flushing) {
        // flush expired device records
        mapper_timetag_t tt;
        mapper_timetag_now(&tt);
        mapper_device dev;
        uint32_t last_ping = tt.sec - _timeout_sec;
        while ((dev = mapper_database_expired_device(db, last_ping))) {
            // remove subscription
            unsubscribe_internal(db, dev, 1);
            mapper_database_remove_device(db, dev, MAPPER_REMOVED, _flushing != 1);
        }
        _flushing = 0;
    }

    return count;
}

static void on_device_autosubscribe(mapper_database db, mapper_device dev,
                                    mapper_record_event event, const void *user)
{
    // New subscriptions are handled in network.c as response to "sync" msg
    if (event == MAPPER_REMOVED) {
        unsubscribe_internal(db, dev, 0);
    }
}

static void mapper_database_autosubscribe(mapper_database db, int flags)
{
    if (!db->autosubscribe && flags) {
        mapper_database_add_device_callback(db, on_device_autosubscribe, db);
        // update flags for existing subscriptions
        mapper_timetag_t tt;
        mapper_timetag_now(&tt);
        mapper_subscription s = db->subscriptions;
        while (s) {
            trace_db("adjusting flags for existing autorenewing subscription "
                     "to %s.\n", mapper_device_name(s->device));
            if (flags & ~s->flags) {
                subscribe_internal(db, s->device, flags, AUTOSUBSCRIBE_INTERVAL);
                // leave 10-second buffer for subscription renewal
                s->lease_expiration_sec = (tt.sec + AUTOSUBSCRIBE_INTERVAL - 10);
            }
            s->flags = flags;
            s = s->next;
        }
        mapper_database_request_devices(db);
    }
    else if (db->autosubscribe && !flags) {
        mapper_database_remove_device_callback(db, on_device_autosubscribe, db);
        while (db->subscriptions) {
            unsubscribe_internal(db, db->subscriptions->device, 1);
        }
    }
#ifdef DEBUG
    trace_db("setting autosubscribe flags to ");
    print_subscription_flags(flags);
#endif
    db->autosubscribe = flags;
}

static mapper_subscription subscription(mapper_database db, mapper_device dev)
{
    mapper_subscription s = db->subscriptions;
    while (s) {
        if (s->device == dev)
            return s;
        s = s->next;
    }
    return 0;
}

void mapper_database_subscribe(mapper_database db, mapper_device dev, int flags,
                               int timeout)
{
    if (!dev) {
        mapper_database_autosubscribe(db, flags);
        return;
    }
    else if (dev->local) {
        // don't bother subscribing to local device
        trace_db("aborting subscription, device is local.\n");
        return;
    }
    if (timeout == -1) {
#ifdef DEBUG
        trace_db("adding %d-second autorenewing subscription to device '%s' "
                 "with flags ", AUTOSUBSCRIBE_INTERVAL, mapper_device_name(dev));
        print_subscription_flags(flags);
#endif
        // special case: autorenew subscription lease
        // first check if subscription already exists
        mapper_subscription s = subscription(db, dev);

        if (!s) {
            // store subscription record
            s = malloc(sizeof(struct _mapper_subscription));
            s->flags = 0;
            s->device = dev;
            s->device->version = -1;
            s->next = db->subscriptions;
            db->subscriptions = s;
        }
        dev->subscribed = 1;
        if (s->flags == flags)
            return;

        s->device->version = -1;
        s->flags = flags;

        mapper_timetag_t tt;
        mapper_timetag_now(&tt);
        // leave 10-second buffer for subscription lease
        s->lease_expiration_sec = (tt.sec + AUTOSUBSCRIBE_INTERVAL - 10);

        timeout = AUTOSUBSCRIBE_INTERVAL;
    }
#ifdef DEBUG
    else {
        trace_db("adding temporary %d-second subscription to device '%s' "
                 "with flags ", timeout, mapper_device_name(dev));
        print_subscription_flags(flags);
    }
#endif

    subscribe_internal(db, dev, flags, timeout);
}

void mapper_database_unsubscribe(mapper_database db, mapper_device dev)
{
    if (!dev)
        mapper_database_autosubscribe(db, MAPPER_OBJ_NONE);
    unsubscribe_internal(db, dev, 1);
}

int mapper_database_subscribed_by_device_name(mapper_database db,
                                              const char *name)
{
    mapper_device dev = mapper_database_device_by_name(db, name);
    if (dev) {
        mapper_subscription s = subscription(db, dev);
        return s ? s->flags : 0;
    }
    return 0;
}

int mapper_database_subscribed_by_signal_name(mapper_database db,
                                              const char *name)
{
    // name needs to be split
    char *devnamep, *signame, devname[256];
    int devnamelen = mapper_parse_names(name, &devnamep, &signame);
    if (!devnamelen || devnamelen >= 256) {
        trace_db("error extracting device name\n");
        return 0;
    }
    strncpy(devname, devnamep, devnamelen);
    devname[devnamelen] = 0;
    mapper_device dev = mapper_database_device_by_name(db, devname);
    if (dev) {
        mapper_subscription s = subscription(db, dev);
        return s ? s->flags : 0;
    }
    return 0;
}

void mapper_database_request_devices(mapper_database db)
{
    lo_message msg = lo_message_new();
    if (!msg) {
        trace_db("couldn't allocate lo_message\n");
        return;
    }
    trace_db("pinging all devices.\n")
    mapper_network_set_dest_bus(db->network);
    mapper_network_add_message(db->network, 0, MSG_WHO, msg);
}
