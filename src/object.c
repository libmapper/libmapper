#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>

#include "mapper_internal.h"
#include "types_internal.h"

mapper_graph mapper_object_get_graph(mapper_object obj)
{
    return obj->graph;
}

mapper_object_type mapper_object_get_type(mapper_object obj)
{
    return obj->type;
}

int mapper_object_get_num_props(mapper_object obj, int staged)
{
    int len = 0;
    if (obj) {
        if (obj->props.synced)
            len += mapper_table_num_records(obj->props.synced);
        if (staged && obj->props.staged)
            len += mapper_table_num_records(obj->props.staged);
    }
    return len;
}

mapper_property mapper_object_get_prop_by_name(mapper_object obj,
                                               const char *name, int *len,
                                               mapper_type *type,
                                               const void **val)
{
    if (!name)
        return 0;
    return mapper_table_get_prop_by_name(obj->props.synced, name, len, type, val);
}

mapper_property mapper_object_get_prop_by_index(mapper_object obj,
                                                mapper_property prop,
                                                const char **name, int *len,
                                                mapper_type *type,
                                                const void **val)
{
    return mapper_table_get_prop_by_index(obj->props.synced,
                                          prop | obj->props.mask, name,
                                          len, type, val);
}

mapper_property mapper_object_set_prop(mapper_object obj, mapper_property prop,
                                       const char *name, int len, mapper_type type,
                                       const void *val, int publish)
{
    // TODO: ensure ID property can't be changed by user code
    if (MAPPER_PROP_UNKNOWN == prop || !MASK_PROP_BITFLAGS(prop)) {
        if (!name)
            return MAPPER_PROP_UNKNOWN;
        prop = mapper_prop_from_string(name);
    }

    // check if object represents local resource
    int local = obj->props.staged ? 0 : 1;
    int flags = local ? LOCAL_MODIFY : REMOTE_MODIFY;
    if (!publish)
        flags |= LOCAL_ACCESS_ONLY;
    return mapper_table_set_record(local ? obj->props.synced : obj->props.staged,
                                   prop | obj->props.mask, name, len, type,
                                   val, flags);
}

int mapper_object_remove_prop(mapper_object obj, mapper_property prop,
                              const char *name)
{
    if (!obj)
        return 0;
    // check if object represents local resource
    int local = obj->props.staged ? 0 : 1;
    if (MAPPER_PROP_UNKNOWN == prop)
        prop = mapper_prop_from_string(name);
    if (MAPPER_PROP_USER_DATA == prop || local)
        return mapper_table_remove_record(obj->props.synced, prop, name,
                                          LOCAL_MODIFY);
    else if (MAPPER_PROP_EXTRA == prop)
        return mapper_table_set_record(obj->props.staged, prop | PROP_REMOVE,
                                       name, 0, 0, 0, REMOTE_MODIFY);
    return 0;
}

void mapper_object_push(mapper_object obj)
{
    if (!obj)
        return;

    mapper_network net = &obj->graph->net;

    if (obj->type == MAPPER_OBJ_DEVICE) {
        mapper_device dev = (mapper_device)obj;
        if (dev->local) {
            mapper_network_subscribers(net, obj->type);
            mapper_device_send_state(dev, MSG_DEVICE);
        }
        else {
            mapper_network_bus(net);
            mapper_device_send_state(dev, MSG_DEVICE_MODIFY);
        }
    }
    else if (obj->type & MAPPER_OBJ_SIGNAL) {
        mapper_signal sig = (mapper_signal)obj;
        if (sig->local) {
            mapper_object_type type = ((sig->dir == MAPPER_DIR_OUT)
                                       ? MAPPER_OBJ_OUTPUT_SIGNAL
                                       : MAPPER_OBJ_INPUT_SIGNAL);
            mapper_network_subscribers(net, type);
            mapper_signal_send_state(sig, MSG_SIGNAL);
        }
        else {
            mapper_network_bus(net);
            mapper_signal_send_state(sig, MSG_SIGNAL_MODIFY);
        }
    }
    else if (obj->type & MAPPER_OBJ_MAP) {
        mapper_network_bus(net);
        mapper_map map = (mapper_map)obj;
        if (map->status >= STATUS_ACTIVE)
            mapper_map_send_state(map, -1, MSG_MAP_MODIFY);
        else
            mapper_map_send_state(map, -1, MSG_MAP);
    }
    else {
        trace("mapper_object_push(): unknown object type %d\n", obj->type);
        return;
    }

    // clear the staged properties
    mapper_table_clear(obj->props.staged);
}

static int match_pattern(const char* string, const char* pattern)
{
    if (!string || !pattern)
        return 1;

    if (!strchr(pattern, '*'))
        return strcmp(string, pattern);

    // 1) tokenize pattern using strtok() with delimiter character '*'
    // 2) use strstr() to check if token exists in offset string
    char *str = (char*)string, *tok;
    char dup[strlen(pattern)+1], *pat = dup;
    strcpy(pat, pattern);
    int ends_wild = ('*' == pattern[strlen(pattern)-1]);
    while (str && *str) {
        tok = strtok(pat, "*");
        if (!tok)
            return !ends_wild;
        str = strstr(str, tok);
        if (str && *str)
            str += strlen(tok);
        else
            return 1;
        // subsequent calls to strtok() need first argument to be NULL
        pat = NULL;
    }
    return 0;
}

static int compare_val(mapper_op op, int len, mapper_type type,
                       const void *val1, const void *val2)
{
    int i, compare = 0, difference = 0;
    switch (type) {
        case MAPPER_STRING:
            if (1 == len)
                compare = match_pattern((const char*)val1, (const char*)val2);
            else {
                for (i = 0; i < len; i++) {
                    compare += match_pattern(((const char**)val1)[i],
                                             ((const char**)val2)[i]);
                    difference += abs(compare);
                }
            }
            break;
        case MAPPER_INT32:
            for (i = 0; i < len; i++) {
                compare += ((int*)val1)[i] > ((int*)val2)[i];
                compare -= ((int*)val1)[i] < ((int*)val2)[i];
                difference += abs(compare);
            }
            break;
        case MAPPER_FLOAT:
            for (i = 0; i < len; i++) {
                compare += ((float*)val1)[i] > ((float*)val2)[i];
                compare -= ((float*)val1)[i] < ((float*)val2)[i];
                difference += abs(compare);
            }
            break;
        case MAPPER_DOUBLE:
            for (i = 0; i < len; i++) {
                compare += ((double*)val1)[i] > ((double*)val2)[i];
                compare -= ((double*)val1)[i] < ((double*)val2)[i];
                difference += abs(compare);
            }
            break;
        case MAPPER_CHAR:
            for (i = 0; i < len; i++) {
                compare += ((char*)val1)[i] > ((char*)val2)[i];
                compare -= ((char*)val1)[i] < ((char*)val2)[i];
                difference += abs(compare);
            }
            break;
        case MAPPER_INT64:
        case MAPPER_TIME:
            for (i = 0; i < len; i++) {
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
            return (0 == compare) && !difference;
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

static int filter_by_prop(const void *context, mapper_object obj)
{
    mapper_property prop = *(int*)        (context);
    mapper_op op =         *(int*)        (context + sizeof(int));
    int len =              *(int*)        (context + sizeof(int)*2);
    mapper_type type =     *(mapper_type*)(context + sizeof(int)*3);
    void *val =            *(void**)      (context + sizeof(int)*4);
    const char *name =      (const char*) (context + sizeof(int)*4 + sizeof(void*));
    int _len;
    mapper_type _type;
    const void *_val;
    if (name && name[0])
        prop = mapper_object_get_prop_by_name((mapper_object)obj, name, &_len,
                                              &_type, &_val);
    else
        prop = mapper_object_get_prop_by_index((mapper_object)obj, prop, NULL,
                                               &_len, &_type, &_val);
    if (MAPPER_PROP_UNKNOWN == prop)
        return MAPPER_OP_DOES_NOT_EXIST == op;
    if (MAPPER_OP_EXISTS == op)
        return 1;
    if (_type != type || _len != len)
        return 0;
    return compare_val(op, len, type, _val, val);
}

mapper_object *mapper_object_list_filter(mapper_object *list, mapper_property prop,
                                         const char *name, int len,
                                         mapper_type type, const void *val,
                                         mapper_op op)
{
    if (!list || op <= MAPPER_OP_UNDEFINED || op >= NUM_MAPPER_OPS)
        return list;
    return (mapper_object*)mapper_list_filter((void**)list, filter_by_prop,
                                              "iiicvs", prop, op, len, type,
                                              &val, name);
}

inline mapper_object *mapper_object_list_union(mapper_object *list1,
                                               mapper_object *list2)
{
    return (mapper_object*)mapper_list_union((void**)list1, (void**)list2);
}

inline mapper_object *mapper_object_list_intersection(mapper_object *list1,
                                                      mapper_object *list2)
{
    return (mapper_object*)mapper_list_intersection((void**)list1, (void**)list2);
}

inline mapper_object *mapper_object_list_difference(mapper_object *list1,
                                                    mapper_object *list2)
{
    return (mapper_object*)mapper_list_difference((void**)list1, (void**)list2);
}

inline mapper_object mapper_object_list_get_index(mapper_object *list, int index)
{
    return (mapper_object)mapper_list_get_index((void**)list, index);
}

inline mapper_object *mapper_object_list_next(mapper_object *list)
{
    return (mapper_object*)mapper_list_next((void**)list);
}

inline mapper_object *mapper_object_list_copy(mapper_object *list)
{
    return (mapper_object*)mapper_list_copy((void**)list);
}

inline int mapper_object_list_get_length(mapper_object *list)
{
    return mapper_list_length((void**)list);
}

inline void mapper_object_list_free(mapper_object *list)
{
    mapper_list_free((void**)list);
}

void mapper_object_print(mapper_object obj, int staged)
{
    if (!obj || !obj->props.synced)
        return;
    int i = 0;
    mapper_property prop;
    const char *name;
    mapper_type type;
    const void *val;
    int len;

    switch (obj->type) {
        case MAPPER_OBJ_DEVICE:
            printf("DEVICE: ");
            mapper_prop_print(1, MAPPER_DEVICE, obj);
            break;
        case MAPPER_OBJ_SIGNAL:
            printf("SIGNAL: ");
            mapper_prop_print(1, MAPPER_SIGNAL, obj);
            break;
        case MAPPER_OBJ_MAP: {
            printf("MAP: ");
            mapper_map map = (mapper_map)obj;
            int i;
            if (map->num_src > 1)
                printf("[");
            for (i = 0; i < map->num_src; i++) {
                mapper_prop_print(1, MAPPER_SIGNAL,
                                  mapper_map_get_signal(map, MAPPER_LOC_SRC, i));
                printf(", ");
            }
            printf("\b\b");
            if (map->num_src > 1)
                printf("]");
            printf(" -> ");
            mapper_prop_print(1, MAPPER_SIGNAL,
                              mapper_map_get_signal(map, MAPPER_LOC_DST, 0));
            break;
        }
        default:
            trace("mapper_object_print(): unknown object type %d.", obj->type);
            return;
    }

    int num_props = mapper_object_get_num_props(obj, 0);
    for (i = 0; i < num_props; i++) {
        prop = mapper_object_get_prop_by_index(obj, i, &name, &len, &type, &val);
        die_unless(val != 0, "returned zero value\n");

        // already printed this
        if (MAPPER_PROP_NAME == prop)
            continue;

        printf(", %s=", name);

        // handle pretty-printing a few enum values
        if (1 == len && MAPPER_INT32 == type) {
            switch(prop) {
                case MAPPER_PROP_DIR:
                    printf("%s", MAPPER_DIR_OUT == *(int*)val
                           ? "output" : "input");
                    break;
                case MAPPER_PROP_PROCESS_LOC:
                    printf("%s", mapper_loc_string(*(int*)val));
                    break;
                case MAPPER_PROP_PROTOCOL:
                    printf("%s", mapper_protocol_string(*(int*)val));
                    break;
                default:
                    mapper_prop_print(len, type, val);
            }
        }
        else
            mapper_prop_print(len, type, val);

        if (!staged || !obj->props.staged)
            continue;

        // check if staged values exist
        if (prop == MAPPER_PROP_EXTRA)
            prop = mapper_table_get_prop_by_name(obj->props.staged, name,
                                                 &len, &type, &val);
        else
            prop = mapper_table_get_prop_by_index(obj->props.staged, prop, NULL,
                                                  &len, &type, &val);
        if (prop != MAPPER_PROP_UNKNOWN) {
            printf(" (staged: ");
            mapper_prop_print(len, type, val);
            printf(")");
        }
    }
    printf("\n");
}

