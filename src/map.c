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

/*! Function prototypes. */
static void reallocate_map_histories(mapper_map map);
static int mapper_map_set_mode_linear(mapper_map map);

#define METADATA_OK (STATUS_TYPE_KNOWN | STATUS_LENGTH_KNOWN | STATUS_LINK_KNOWN)

static int alphabetise_signals(int num, mapper_signal *sigs, int *order)
{
    int i, j, result = 1;
    for (i = 0; i < num; i++)
        order[i] = i;
    for (i = 1; i < num; i++) {
        j = i-1;
        while (j >= 0
               && (((result = strcmp(sigs[order[j]]->device->name,
                                     sigs[order[j+1]]->device->name)) > 0)
                   || ((result = strcmp(sigs[order[j]]->name,
                                        sigs[order[j+1]]->name)) > 0))) {
                   int temp = order[j];
                   order[j] = order[j+1];
                   order[j+1] = temp;
                   j--;
               }
        if (result == 0)
            return 1;
    }
    return 0;
}

void mapper_map_init(mapper_map map)
{
    map->props = mapper_table_new();
    map->staged_props = mapper_table_new();

    // these properties need to be added in alphabetical order
    mapper_table_link_value(map->props, AT_DESCRIPTION, 1, 's',
                            &map->description, MODIFIABLE | INDIRECT);

    mapper_table_link_value(map->props, AT_EXPRESSION, 1, 's', &map->expression,
                            MODIFIABLE | INDIRECT);

    mapper_table_link_value(map->props, AT_ID, 1, 'h', &map->id, NON_MODIFIABLE);

    mapper_table_link_value(map->props, AT_NUM_INPUTS, 1, 'i', &map->num_sources,
                            NON_MODIFIABLE);

    mapper_table_link_value(map->props, AT_MODE, 1, 'i', &map->mode,
                            MODIFIABLE);

    mapper_table_link_value(map->props, AT_MUTED, 1, 'b', &map->muted,
                            MODIFIABLE);

    mapper_table_link_value(map->props, AT_PROCESS_LOCATION, 1, 'i',
                            &map->process_location, MODIFIABLE);

    mapper_table_link_value(map->props, AT_STATUS, 1, 'i', &map->status,
                            NON_MODIFIABLE);

    mapper_table_link_value(map->props, AT_USER_DATA, 1, 'v', &map->user_data,
                            MODIFIABLE | INDIRECT);

    mapper_table_link_value(map->props, AT_VERSION, 1, 'i', &map->version,
                            REMOTE_MODIFY);

    int i;
    for (i = 0; i < map->num_sources; i++)
        mapper_slot_init(&map->sources[i]);
    mapper_slot_init(&map->destination);
}

mapper_map mapper_map_new(int num_sources, mapper_signal *sources,
                          mapper_signal destination)
{
    int i;
    if (!sources || !destination)
        return 0;
    if (num_sources <= 0 || num_sources > MAX_NUM_MAP_SOURCES)
        return 0;

    mapper_local_device ldev = destination->device->local;
    mapper_database db = destination->device->database;

    // check if record of map already exists
    mapper_map map, *maps, *temp;
    maps = mapper_signal_maps(destination, MAPPER_DIR_INCOMING);
    if (maps) {
        mapper_signal sig;
        for (i = 0; i < num_sources; i++) {
            sig = mapper_database_signal_by_id(db, mapper_signal_id(sources[i]));
            if (sig) {
                temp = mapper_signal_maps(sig, MAPPER_DIR_OUTGOING);
                maps = mapper_map_query_intersection(maps, temp);
            }
            else {
                mapper_map_query_done(maps);
                maps = 0;
                break;
            }
        }
        while (maps) {
            if ((*maps)->num_sources == num_sources) {
                map = *maps;
                mapper_map_query_done(maps);
                return map;
            }
            maps = mapper_map_query_next(maps);
        }
    }

    int order[num_sources];
    if (alphabetise_signals(num_sources, sources, order)) {
        trace("error in mapper_map_new(): multiple use of source signal.\n");
        return 0;
    }

    map = (mapper_map)mapper_list_add_item((void**)&db->maps,
                                           sizeof(mapper_map_t));
    map->database = db;
    map->num_sources = num_sources;
    map->sources = (mapper_slot) calloc(1, sizeof(struct _mapper_slot)
                                        * num_sources);
    for (i = 0; i < num_sources; i++) {
        if (sources[order[i]]->device->local == ldev) {
            map->sources[i].signal = sources[order[i]];
        }
        else {
            map->sources[i].signal =
                mapper_database_add_or_update_signal(db, sources[order[i]]->name,
                                                     sources[order[i]]->device->name, 0);
            if (!map->sources[i].signal->id) {
                map->sources[i].signal->id = sources[order[i]]->id;
                map->sources[i].signal->direction = sources[order[i]]->direction;
            }
            if (!map->sources[i].signal->device->id) {
                map->sources[i].signal->device->id = sources[order[i]]->device->id;
            }
        }
        map->sources[i].map = map;
        map->sources[i].id = i;
    }
    map->destination.signal = destination;
    map->destination.map = map;
    map->destination.direction = MAPPER_DIR_INCOMING;

    // we need to give the map a temporary id – this may be overwritten later
    map->id = mapper_device_unique_id(destination->device);

    mapper_map_init(map);

    map->status = STATUS_STAGED;

    return map;
}

void mapper_map_release(mapper_map map)
{
    mapper_map_send_state(map, -1, MSG_UNMAP, 0);
}

void mapper_map_set_user_data(mapper_map map, const void *user_data)
{
    if (map)
        map->user_data = (void*)user_data;
}

void *mapper_map_user_data(mapper_map map)
{
    return map ? map->user_data : 0;
}

void mapper_map_push(mapper_map map)
{
    if (!map)
        return;
    int cmd;
    if (map->status >= STATUS_ACTIVE) {
        if (!map->props->dirty)
            return;
        cmd = MSG_MODIFY_MAP;
    }
    else
        cmd = MSG_MAP;

    mapper_network_set_dest_bus(map->database->network);
    mapper_map_send_state(map, -1, cmd, UPDATED_PROPS);
}

void mapper_map_free(mapper_map map)
{
    int i;
    if (map->sources) {
        for (i = 0; i < map->num_sources; i++) {
            mapper_slot_free(&map->sources[i]);
        }
        free(map->sources);
    }
    mapper_slot_free(&map->destination);
    if (map->scope.size && map->scope.devices) {
        free(map->scope.devices);
    }
    if (map->props)
        mapper_table_free(map->props);
    if (map->staged_props)
        mapper_table_free(map->staged_props);
    if (map->description)
        free(map->description);
    if (map->expression)
        free(map->expression);
}

const char *mapper_map_description(mapper_map map)
{
    mapper_table_record_t *rec = mapper_table_record(map->props, AT_DESCRIPTION, 0);
    if (rec && rec->type == 's' && rec->length == 1)
        return (const char*)rec->value;
    return 0;
}

int mapper_map_num_sources(mapper_map map)
{
    return map->num_sources;
}

mapper_slot mapper_map_source_slot(mapper_map map, int index)
{
    if (index < 0 || index >= map->num_sources)
        return 0;
    return &map->sources[index];
}

mapper_slot mapper_map_destination_slot(mapper_map map)
{
    return &map->destination;
}

mapper_slot mapper_map_slot_by_signal(mapper_map map, mapper_signal sig)
{
    int i;
    if (map->destination.signal->id == sig->id)
        return &map->destination;
    for (i = 0; i < map->num_sources; i++) {
        if (map->sources[i].signal->id == sig->id)
            return &map->sources[i];
    }
    return 0;
}

mapper_id mapper_map_id(mapper_map map)
{
    return map->id;
}

mapper_mode mapper_map_mode(mapper_map map)
{
    return map->mode;
}

const char *mapper_map_expression(mapper_map map)
{
    return map->expression;
}

int mapper_map_muted(mapper_map map)
{
    return map->muted;
}

mapper_location mapper_map_process_location(mapper_map map)
{
    return map->process_location;
}

static int cmp_query_map_scopes(const void *context_data, mapper_device dev)
{
    mapper_map_scope scope = (mapper_map_scope)context_data;
    for (int i = 0; i < scope->size; i++) {
        if (!scope->devices[i] || scope->devices[i]->id == dev->id)
            return 1;
    }
    return 0;
}

mapper_device *mapper_map_scopes(mapper_map map)
{
    if (!map || !map->scope.size)
        return 0;
    return ((mapper_device *)
            mapper_list_new_query(map->database->devices, cmp_query_map_scopes,
                                  "v", &map->scope));
}

int mapper_map_num_properties(mapper_map map) {
    return mapper_table_num_records(map->props);
}

int mapper_map_property(mapper_map map, const char *name, int *length,
                        char *type, const void **value)
{
    return mapper_table_property(map->props, name, length, type, value);
}

int mapper_map_property_index(mapper_map map, unsigned int index,
                              const char **property, int *length, char *type,
                              const void **value)
{
    return mapper_table_property_index(map->props, index, property, length,
                                       type, value);
}

int mapper_map_ready(mapper_map map)
{
    return map ? (map->status == STATUS_ACTIVE) : 0;
}

void mapper_map_set_description(mapper_map map, const char *description)
{
    if (!map)
        return;
    mapper_table_set_record(map->staged_props, AT_DESCRIPTION, NULL, 1, 's',
                            description, REMOTE_MODIFY);
}

void mapper_map_set_mode(mapper_map map, mapper_mode mode)
{
    if (mode > MAPPER_MODE_UNDEFINED && mode < NUM_MAPPER_MODES) {
        mapper_table_set_record(map->staged_props, AT_MODE, NULL, 1, 'i', &mode,
                                REMOTE_MODIFY);
    }
}

void mapper_map_set_expression(mapper_map map, const char *expression)
{
    mapper_table_set_record(map->staged_props, AT_EXPRESSION, NULL, 1, 's',
                            expression, REMOTE_MODIFY);
}

void mapper_map_set_muted(mapper_map map, int muted)
{
    mapper_table_set_record(map->staged_props, AT_MUTED, NULL, 1, 'b', &muted,
                            REMOTE_MODIFY);
}

void mapper_map_set_process_location(mapper_map map, mapper_location location)
{
    mapper_table_set_record(map->staged_props, AT_PROCESS_LOCATION, NULL, 1,
                            'i', &location, REMOTE_MODIFY);
}

void mapper_map_add_scope(mapper_map map, mapper_device device)
{
    mapper_property_t prop = AT_SCOPE | PROPERTY_ADD;
    mapper_table_record_t *rec = mapper_table_record(map->staged_props, prop,
                                                     NULL);
    if (rec && rec->type == 's') {
        const char *names[rec->length+1];
        if (rec->length == 1) {
            names[0] = (const char*)rec->value;
        }
        for (int i = 0; i < rec->length; i++) {
            names[i] = ((const char**)rec->value)[i];
        }
        names[rec->length] = device->name;
        mapper_table_set_record(map->staged_props, prop, NULL,
                                rec->length + 1, 's', names,
                                REMOTE_MODIFY);
    }
    else
        mapper_table_set_record(map->staged_props, prop, NULL, 1, 's',
                                device->name, REMOTE_MODIFY);
}

void mapper_map_remove_scope(mapper_map map, mapper_device device)
{
    mapper_property_t prop = AT_SCOPE | PROPERTY_REMOVE;
    mapper_table_record_t *rec = mapper_table_record(map->staged_props, prop,
                                                     NULL);
    if (rec && rec->type == 's') {
        const char *names[rec->length+1];
        if (rec->length == 1) {
            names[0] = (const char*)rec->value;
        }
        for (int i = 0; i < rec->length; i++) {
            names[i] = ((const char**)rec->value)[i];
        }
        names[rec->length] = device->name;
        mapper_table_set_record(map->staged_props, prop, NULL,
                                rec->length + 1, 's', names,
                                REMOTE_MODIFY);
    }
    else
        mapper_table_set_record(map->staged_props, prop, NULL, 1, 's',
                                device->name, REMOTE_MODIFY);
}

void mapper_map_set_property(mapper_map map, const char *name, int length,
                             char type, const void *value)
{
    mapper_property_t prop = mapper_property_from_string(name);
    if (prop == AT_USER_DATA)
        map->user_data = (void*)value;
    else
        mapper_table_set_record(map->staged_props, prop, name, length, type,
                                value, REMOTE_MODIFY);
}

void mapper_map_remove_property(mapper_map map, const char *name)
{
    // check if property is in static property table
    mapper_property_t prop = mapper_property_from_string(name);
    if (prop == AT_USER_DATA)
        map->user_data = 0;
    else
        mapper_table_set_record(map->staged_props, prop, name, 0, 0, 0,
                                REMOTE_MODIFY);
}

static int add_scope_internal(mapper_map map, const char *name)
{
    int i;
    if (!map || !name)
        return 0;
    mapper_device dev = 0;

    if (strcmp(name, "all")==0) {
        for (i = 0; i < map->scope.size; i++) {
            if (!map->scope.devices[i])
                return 0;
        }
    }
    else {
        dev = mapper_database_add_or_update_device(map->database, name, 0);
        for (i = 0; i < map->scope.size; i++) {
            if (map->scope.devices[i] && map->scope.devices[i]->id == dev->id)
                return 0;
        }
    }

    // not found - add a new scope
    i = ++map->scope.size;
    map->scope.devices = realloc(map->scope.devices, i * sizeof(mapper_device));
    map->scope.devices[i-1] = dev;
    return 1;
}

static int remove_scope_internal(mapper_map map, const char *name)
{
    int i;
    if (!map || !name)
        return 0;
    if (strcmp(name, "all")==0)
        name = 0;
    for (i = 0; i < map->scope.size; i++) {
        if (!map->scope.devices[i]) {
            if (!name)
                break;
        }
        else if (name && strcmp(map->scope.devices[i]->name, name) == 0)
            break;
    }
    if (i == map->scope.size)
        return 0;

    // found - remove scope at index i
    for (; i < map->scope.size; i++) {
        map->scope.devices[i-1] = map->scope.devices[i];
    }
    map->scope.size--;
    map->scope.devices = realloc(map->scope.devices,
                                 map->scope.size * sizeof(mapper_device));
    return 1;
}

static int mapper_map_update_scope(mapper_map map, mapper_message_atom atom)
{
    int i, j, updated = 0, num = atom->length;
    lo_arg **scope_list = atom->values;
    if (scope_list && *scope_list) {
        if (num == 1 && strcmp(&scope_list[0]->s, "none")==0)
            num = 0;
        const char *name, *no_slash;

        // First remove old scopes that are missing
        for (i = 0; i < map->scope.size; i++) {
            int found = 0;
            for (j = 0; j < num; j++) {
                name = &scope_list[j]->s;
                if (!map->scope.devices[i]) {
                    if (strcmp(name, "all") == 0) {
                        found = 1;
                        break;
                    }
                    break;
                }
                no_slash = name[0] == '/' ? name + 1 : name;
                if (strcmp(no_slash, map->scope.devices[i]->name) == 0) {
                    found = 1;
                    break;
                }
            }
            if (!found) {
                remove_scope_internal(map, &scope_list[i]->s);
                ++updated;
            }
        }
        // ...then add any new scopes
        for (i = 0; i < num; i++) {
            updated += (1 - add_scope_internal(map, &scope_list[i]->s));
        }
    }
    return updated;
}

// only called for outgoing maps
int mapper_map_perform(mapper_map map, mapper_slot slot, int instance,
                       char *typestring)
{
    int changed = 0, i;
    mapper_history from = slot->local->history;
    mapper_history to = map->destination.local->history;

    if (slot->calibrating == 1) {
        if (!slot->minimum) {
            slot->minimum = malloc(slot->signal->length
                                   * mapper_type_size(slot->signal->type));
        }
        if (!slot->maximum) {
            slot->maximum = malloc(slot->signal->length
                                   * mapper_type_size(slot->signal->type));
        }

        /* If calibration mode has just taken effect, first data
         * sample sets source min and max */
        switch (slot->signal->type) {
            case 'f': {
                float *v = mapper_history_value_ptr(from[instance]);
                float *src_min = (float*)slot->minimum;
                float *src_max = (float*)slot->maximum;
                if (!slot->calibrating) {
                    for (i = 0; i < from->length; i++) {
                        src_min[i] = v[i];
                        src_max[i] = v[i];
                    }
                    slot->calibrating = 1;
                    changed = 1;
                }
                else {
                    for (i = 0; i < from->length; i++) {
                        if (v[i] < src_min[i]) {
                            src_min[i] = v[i];
                            changed = 1;
                        }
                        if (v[i] > src_max[i]) {
                            src_max[i] = v[i];
                            changed = 1;
                        }
                    }
                }
                break;
            }
            case 'i': {
                int *v = mapper_history_value_ptr(from[instance]);
                int *src_min = (int*)slot->minimum;
                int *src_max = (int*)slot->maximum;
                if (!slot->calibrating) {
                    for (i = 0; i < from->length; i++) {
                        src_min[i] = v[i];
                        src_max[i] = v[i];
                    }
                    slot->calibrating = 1;
                    changed = 1;
                }
                else {
                    for (i = 0; i < from->length; i++) {
                        if (v[i] < src_min[i]) {
                            src_min[i] = v[i];
                            changed = 1;
                        }
                        if (v[i] > src_max[i]) {
                            src_max[i] = v[i];
                            changed = 1;
                        }
                    }
                }
                break;
            }
            case 'd': {
                double *v = mapper_history_value_ptr(from[instance]);
                double *src_min = (double*)slot->minimum;
                double *src_max = (double*)slot->maximum;
                if (!slot->calibrating) {
                    for (i = 0; i < from->length; i++) {
                        src_min[i] = v[i];
                        src_max[i] = v[i];
                    }
                    slot->calibrating = 1;
                    changed = 1;
                }
                else {
                    for (i = 0; i < from->length; i++) {
                        if (v[i] < src_min[i]) {
                            src_min[i] = v[i];
                            changed = 1;
                        }
                        if (v[i] > src_max[i]) {
                            src_max[i] = v[i];
                            changed = 1;
                        }
                    }
                }
                break;
            }
            default:
                break;
        }

        if (changed && map->mode == MAPPER_MODE_LINEAR)
            mapper_map_set_mode_linear(map);
    }

    if (map->status != STATUS_ACTIVE || map->muted) {
        return 0;
    }
    else if (map->process_location == MAPPER_LOC_DESTINATION) {
        to[instance].position = 0;
        // copy value without type coercion
        memcpy(mapper_history_value_ptr(to[instance]),
               mapper_history_value_ptr(from[instance]),
               mapper_signal_vector_bytes(slot->signal));
        // copy timetag
        memcpy(mapper_history_tt_ptr(to[instance]),
               mapper_history_tt_ptr(from[instance]),
               sizeof(mapper_timetag_t));
        for (i = 0; i < from->length; i++)
            typestring[i] = map->sources[0].signal->type;
        return 1;
    }

    if (!map->local->expr) {
        trace("error: missing expression.\n");
        return 0;
    }

    mapper_history sources[map->num_sources];
    for (i = 0; i < map->num_sources; i++)
        sources[i] = &map->sources[i].local->history[instance];
    return (mapper_expr_evaluate(map->local->expr, sources,
                                 &map->local->expr_vars[instance],
                                 &to[instance],
                                 mapper_history_tt_ptr(from[instance]),
                                 typestring));
}

int mapper_boundary_perform(mapper_history history, mapper_slot slot,
                            char *typestring)
{
    int i, muted = 0;

    double value;
    double dest_min, dest_max, swap, total_range, difference, modulo_difference;
    mapper_boundary_action bound_min, bound_max;

    if (   slot->bound_min == MAPPER_BOUND_NONE
        && slot->bound_max == MAPPER_BOUND_NONE) {
        return 0;
    }
    if (!slot->minimum && (   slot->bound_min != MAPPER_BOUND_NONE
                           || slot->bound_max == MAPPER_BOUND_WRAP)) {
        return 0;
    }
    if (!slot->maximum && (   slot->bound_max != MAPPER_BOUND_NONE
                           || slot->bound_min == MAPPER_BOUND_WRAP)) {
        return 0;
    }

    for (i = 0; i < history->length; i++) {
        if (typestring[i] == 'N') {
            ++muted;
            continue;
        }
        value = propval_double(mapper_history_value_ptr(*history),
                               slot->signal->type, i);
        dest_min = propval_double(slot->minimum, slot->signal->type, i);
        dest_max = propval_double(slot->maximum, slot->signal->type, i);
        if (dest_min < dest_max) {
            bound_min = slot->bound_min;
            bound_max = slot->bound_max;
        }
        else {
            bound_min = slot->bound_max;
            bound_max = slot->bound_min;
            swap = dest_max;
            dest_max = dest_min;
            dest_min = swap;
        }
        total_range = fabs(dest_max - dest_min);
        if (value < dest_min) {
            switch (bound_min) {
                case MAPPER_BOUND_MUTE:
                    // need to prevent value from being sent at all
                    typestring[i] = 'N';
                    ++muted;
                    break;
                case MAPPER_BOUND_CLAMP:
                    // clamp value to range minimum
                    value = dest_min;
                    break;
                case MAPPER_BOUND_FOLD:
                    // fold value around range minimum
                    difference = fabs(value - dest_min);
                    value = dest_min + difference;
                    if (value > dest_max) {
                        // value now exceeds range maximum!
                        switch (bound_max) {
                            case MAPPER_BOUND_MUTE:
                                // need to prevent value from being sent at all
                                typestring[i] = 'N';
                                ++muted;
                                break;
                            case MAPPER_BOUND_CLAMP:
                                // clamp value to range minimum
                                value = dest_max;
                                break;
                            case MAPPER_BOUND_FOLD:
                                // both boundary actions are set to fold!
                                difference = fabs(value - dest_max);
                                modulo_difference = difference
                                    - ((int)(difference / total_range)
                                       * total_range);
                                if ((int)(difference / total_range) % 2 == 0) {
                                    value = dest_max - modulo_difference;
                                }
                                else
                                    value = dest_min + modulo_difference;
                                break;
                            case MAPPER_BOUND_WRAP:
                                // wrap value back from range minimum
                                difference = fabs(value - dest_max);
                                modulo_difference = difference
                                    - ((int)(difference / total_range)
                                       * total_range);
                                value = dest_min + modulo_difference;
                                break;
                            default:
                                break;
                        }
                    }
                    break;
                case MAPPER_BOUND_WRAP:
                    // wrap value back from range maximum
                    difference = fabs(value - dest_min);
                    modulo_difference = difference
                        - (int)(difference / total_range) * total_range;
                    value = dest_max - modulo_difference;
                    break;
                default:
                    // leave the value unchanged
                    break;
            }
        }
        else if (value > dest_max) {
            switch (bound_max) {
                case MAPPER_BOUND_MUTE:
                    // need to prevent value from being sent at all
                    typestring[i] = 'N';
                    ++muted;
                    break;
                case MAPPER_BOUND_CLAMP:
                    // clamp value to range maximum
                    value = dest_max;
                    break;
                case MAPPER_BOUND_FOLD:
                    // fold value around range maximum
                    difference = fabs(value - dest_max);
                    value = dest_max - difference;
                    if (value < dest_min) {
                        // value now exceeds range minimum!
                        switch (bound_min) {
                            case MAPPER_BOUND_MUTE:
                                // need to prevent value from being sent at all
                                typestring[i] = 'N';
                                ++muted;
                                break;
                            case MAPPER_BOUND_CLAMP:
                                // clamp value to range minimum
                                value = dest_min;
                                break;
                            case MAPPER_BOUND_FOLD:
                                // both boundary actions are set to fold!
                                difference = fabs(value - dest_min);
                                modulo_difference = difference
                                    - ((int)(difference / total_range)
                                       * total_range);
                                if ((int)(difference / total_range) % 2 == 0) {
                                    value = dest_max + modulo_difference;
                                }
                                else
                                    value = dest_min - modulo_difference;
                                break;
                            case MAPPER_BOUND_WRAP:
                                // wrap value back from range maximum
                                difference = fabs(value - dest_min);
                                modulo_difference = difference
                                    - ((int)(difference / total_range)
                                       * total_range);
                                value = dest_max - modulo_difference;
                                break;
                            default:
                                break;
                        }
                    }
                    break;
                case MAPPER_BOUND_WRAP:
                    // wrap value back from range minimum
                    difference = fabs(value - dest_max);
                    modulo_difference = difference
                        - (int)(difference / total_range) * total_range;
                    value = dest_min + modulo_difference;
                    break;
                default:
                    break;
            }
        }
        propval_set_double(mapper_history_value_ptr(*history),
                           slot->signal->type, i, value);
    }
    return (muted == history->length);
}

/*! Build a value update message for a given map. */
lo_message mapper_map_build_message(mapper_map map, mapper_slot slot,
                                    const void *value, int count,
                                    char *typestring, mapper_id_map id_map)
{
    int i;
    int length = ((map->process_location == MAPPER_LOC_SOURCE)
                  ? map->destination.signal->length * count : slot->signal->length * count);

    lo_message msg = lo_message_new();
    if (!msg)
        return 0;

    if (value && typestring) {
        for (i = 0; i < length; i++) {
            switch (typestring[i]) {
                case 'i':
                    lo_message_add_int32(msg, ((int*)value)[i]);
                    break;
                case 'f':
                    lo_message_add_float(msg, ((float*)value)[i]);
                    break;
                case 'd':
                    lo_message_add_double(msg, ((double*)value)[i]);
                    break;
                case 'N':
                    lo_message_add_nil(msg);
                    break;
                default:
                    break;
            }
        }
    }
    else if (id_map) {
        for (i = 0; i < length; i++)
            lo_message_add_nil(msg);
    }

    if (id_map) {
        lo_message_add_string(msg, "@instance");
        lo_message_add_int64(msg, id_map->global);
    }

    if (map->process_location == MAPPER_LOC_DESTINATION) {
        // add slot
        lo_message_add_string(msg, "@slot");
        lo_message_add_int32(msg, slot->id);
    }

    return msg;
}

/* Helper to replace a map's expression only if the given string
 * parses successfully. Returns 0 on success, non-zero on error. */
static int replace_expression_string(mapper_map map, const char *expr_str)
{
    if (map->local->expr && map->expression
        && strcmp(map->expression, expr_str)==0)
        return 1;

    if (map->status < (STATUS_TYPE_KNOWN | STATUS_LENGTH_KNOWN))
        return 1;

    int i;
    char source_types[map->num_sources];
    int source_lengths[map->num_sources];
    for (i = 0; i < map->num_sources; i++) {
        source_types[i] = map->sources[i].signal->type;
        source_lengths[i] = map->sources[i].signal->length;
    }
    mapper_expr expr = mapper_expr_new_from_string(expr_str,
                                                   map->num_sources,
                                                   source_types, source_lengths,
                                                   map->destination.signal->type,
                                                   map->destination.signal->length);

    if (!expr)
        return 1;

    // expression update may force processing location to change
    // e.g. if expression combines signals from different devices
    // e.g. if expression refers to current/past value of destination
    int output_history_size = mapper_expr_output_history_size(expr);
    if (output_history_size > 1 && map->process_location == MAPPER_LOC_SOURCE) {
        map->process_location = MAPPER_LOC_DESTINATION;
        // copy expression string but do not execute it
        mapper_table_set_record(map->props, AT_EXPRESSION, NULL, 1, 's',
                                expr_str, REMOTE_MODIFY);
        mapper_expr_free(expr);
        return 1;
    }

    if (map->local->expr)
        mapper_expr_free(map->local->expr);

    map->local->expr = expr;

    if (map->expression == expr_str)
        return 0;

    mapper_table_set_record(map->props, AT_EXPRESSION, NULL, 1, 's',
                            expr_str, REMOTE_MODIFY);

    return 0;
}

static void mapper_map_set_mode_raw(mapper_map map)
{
    map->mode = MAPPER_MODE_RAW;
    reallocate_map_histories(map);
}

static int mapper_map_set_mode_linear(mapper_map map)
{
    if (map->num_sources > 1)
        return 1;

    if (map->status < (STATUS_TYPE_KNOWN | STATUS_LENGTH_KNOWN))
        return 1;

    int i, len;
    char expr[256] = "";
    const char *e = expr;

    if (   !map->sources[0].minimum  || !map->sources[0].maximum
        || !map->destination.minimum || !map->destination.maximum)
        return 1;

    int min_length = map->sources[0].signal->length < map->destination.signal->length ?
                     map->sources[0].signal->length : map->destination.signal->length;
    double src_min, src_max, dest_min, dest_max;

    if (map->destination.signal->length == map->sources[0].signal->length)
        snprintf(expr, 256, "y=x*");
    else if (map->destination.signal->length > map->sources[0].signal->length) {
        if (min_length == 1)
            snprintf(expr, 256, "y[0]=x*");
        else
            snprintf(expr, 256, "y[0:%i]=x*", min_length-1);
    }
    else {
        if (min_length == 1)
            snprintf(expr, 256, "y=x[0]*");
        else
            snprintf(expr, 256, "y=x[0:%i]*", min_length-1);
    }

    if (min_length > 1) {
        len = strlen(expr);
        snprintf(expr+len, 256-len, "[");
    }

    for (i = 0; i < min_length; i++) {
        // get multiplier
        src_min = propval_double(map->sources[0].minimum,
                                 map->sources[0].signal->type, i);
        src_max = propval_double(map->sources[0].maximum,
                                 map->sources[0].signal->type, i);
        len = strlen(expr);
        if (src_min == src_max)
            snprintf(expr+len, 256-len, "0,");
        else {
            dest_min = propval_double(map->destination.minimum,
                                      map->destination.signal->type, i);
            dest_max = propval_double(map->destination.maximum,
                                      map->destination.signal->type, i);
            if ((src_min == dest_min) && (src_max == dest_max)) {
                snprintf(expr+len, 256-len, "1,");
            }
            else {
                double scale = ((dest_min - dest_max) / (src_min - src_max));
                snprintf(expr+len, 256-len, "%g,", scale);
            }
        }
    }
    len = strlen(expr);
    if (min_length > 1)
        snprintf(expr+len-1, 256-len+1, "]+[");
    else
        snprintf(expr+len-1, 256-len+1, "+");

    // add offset
    for (i = 0; i < min_length; i++) {
        src_min = propval_double(map->sources[0].minimum,
                                 map->sources[0].signal->type, i);
        src_max = propval_double(map->sources[0].maximum,
                                 map->sources[0].signal->type, i);

        len = strlen(expr);
        if (src_min == src_max)
            snprintf(expr+len, 256-len, "%g,", dest_min);
        else {
            dest_min = propval_double(map->destination.minimum,
                                      map->destination.signal->type, i);
            dest_max = propval_double(map->destination.maximum,
                                      map->destination.signal->type, i);
            if ((src_min == dest_min) && (src_max == dest_max)) {
                snprintf(expr+len, 256-len, "0,");
            }
            else {
                double offset = ((dest_max * src_min - dest_min * src_max)
                                 / (src_min - src_max));
                snprintf(expr+len, 256-len, "%g,", offset);
            }
        }
    }
    len = strlen(expr);
    if (min_length > 1)
        snprintf(expr+len-1, 256-len+1, "]");
    else
        expr[len-1] = '\0';

    // If everything is successful, replace the map's expression.
    if (e) {
        int should_compile = 0;
        if (map->process_location == MAPPER_LOC_DESTINATION) {
            // check if destination is local
            if (map->destination.local->router_sig)
                should_compile = 1;
        }
        else {
            for (i = 0; i < map->num_sources; i++) {
                if (map->sources[i].local->router_sig)
                    should_compile = 1;
            }
        }
        if (should_compile) {
            if (!replace_expression_string(map, e))
                reallocate_map_histories(map);
        }
        else {
            mapper_table_set_record(map->props, AT_EXPRESSION, NULL, 1, 's',
                                    e, REMOTE_MODIFY);
        }
        map->mode = MAPPER_MODE_LINEAR;
        return 0;
    }
    return 1;
}

static void mapper_map_set_mode_expression(mapper_map map, const char *expr)
{
    if (map->status < (STATUS_TYPE_KNOWN | STATUS_LENGTH_KNOWN))
        return;

    int i, should_compile = 0;
    if (map->process_location == MAPPER_LOC_DESTINATION) {
        // check if destination is local
        if (map->destination.local->router_sig)
            should_compile = 1;
    }
    else {
        for (i = 0; i < map->num_sources; i++) {
            if (map->sources[i].local->router_sig)
                should_compile = 1;
        }
    }
    if (should_compile) {
        if (!replace_expression_string(map, expr)) {
            reallocate_map_histories(map);
            map->mode = MAPPER_MODE_EXPRESSION;
        }
        else
            return;
    }
    else {
        if (mapper_table_set_record(map->props, AT_EXPRESSION, NULL, 1, 's',
                                    expr, REMOTE_MODIFY)) {
            map->mode = MAPPER_MODE_EXPRESSION;
        }
        return;
    }

    /* Special case: if we are the receiver and the new expression evaluates to
     * a constant we can update immediately. */
    /* TODO: should call handler for all instances updated through this map. */
    int use_as_instance = 0;
    for (i = 0; i < map->num_sources; i++) {
        if (map->sources[i].use_as_instance) {
            use_as_instance = 1;
            break;
        }
    }
    use_as_instance += map->destination.use_as_instance;
    if (mapper_expr_constant_output(map->local->expr) && !use_as_instance) {
        mapper_timetag_t now;
        mapper_clock_now(&map->database->network->clock, &now);

        // evaluate expression
        mapper_expr_evaluate(map->local->expr, 0, 0,
                             map->destination.local->history, &now, 0);

        // call handler if it exists
        if (map->destination.local) {
            mapper_signal sig = map->destination.local->router_sig->signal;
            mapper_signal_update_handler *h = sig->local->update_handler;
            if (h)
                h(sig, 0, &map->destination.local->history[0].value, 1, &now);
        }
    }
}

// TODO: move to slot.c?
static void init_slot_history(mapper_slot slot)
{
    int i;
    if (slot->local->history) {
        return;
    }
    slot->local->history = malloc(sizeof(struct _mapper_history)
                                  * slot->num_instances);
    slot->local->history_size = 1;
    for (i = 0; i < slot->num_instances; i++) {
        slot->local->history[i].type = slot->signal->type;
        slot->local->history[i].length = slot->signal->length;
        slot->local->history[i].size = 1;
        slot->local->history[i].value = calloc(1, mapper_type_size(slot->signal->type)
                                               * slot->signal->length);
        slot->local->history[i].timetag = calloc(1, sizeof(mapper_timetag_t));
        slot->local->history[i].position = -1;
    }
}

static void apply_mode(mapper_map map)
{
    switch (map->mode) {
        case MAPPER_MODE_RAW:
            mapper_map_set_mode_raw(map);
            break;
        case MAPPER_MODE_LINEAR:
            if (mapper_map_set_mode_linear(map))
                break;
        default: {
            if (map->mode != MAPPER_MODE_EXPRESSION) {
                /* No mode type specified; if mode not yet set, see if
                 we know the range and choose between linear or direct map. */
                /* Try to use linear mapping .*/
                if (mapper_map_set_mode_linear(map) == 0)
                    break;
            }
            if (!map->expression) {
                char expr_str[256] = "";
                if (map->num_sources == 1) {
                    if (map->sources[0].signal->length == map->destination.signal->length)
                        snprintf(expr_str, 256, "y=x");
                    else {
                        if (map->sources[0].signal->length > map->destination.signal->length) {
                            // truncate source
                            if (map->destination.signal->length == 1)
                                snprintf(expr_str, 256, "y=x[0]");
                            else
                                snprintf(expr_str, 256, "y=x[0:%i]",
                                         map->destination.signal->length-1);
                        }
                        else {
                            // truncate destination
                            if (map->sources[0].signal->length == 1)
                                snprintf(expr_str, 256, "y[0]=x");
                            else
                                snprintf(expr_str, 256, "y[0:%i]=x",
                                         map->sources[0].signal->length-1);
                        }
                    }
                }
                else {
                    // check vector lengths
                    int i, j, max_vec_len = 0, min_vec_len = INT_MAX;
                    for (i = 0; i < map->num_sources; i++) {
                        if (map->sources[i].signal->length > max_vec_len)
                            max_vec_len = map->sources[i].signal->length;
                        if (map->sources[i].signal->length < min_vec_len)
                            min_vec_len = map->sources[i].signal->length;
                    }
                    int offset = 0, dest_vec_len;
                    if (max_vec_len < map->destination.signal->length) {
                        snprintf(expr_str, 256, "y[0:%d]=(", max_vec_len-1);
                        offset = strlen(expr_str);
                        dest_vec_len = max_vec_len;
                    }
                    else {
                        snprintf(expr_str, 256, "y=(");
                        offset = 3;
                        dest_vec_len = map->destination.signal->length;
                    }
                    for (i = 0; i < map->num_sources; i++) {
                        if (map->sources[i].signal->length > dest_vec_len) {
                            snprintf(expr_str + offset, 256 - offset,
                                     "x%d[0:%d]+", i, dest_vec_len-1);
                            offset = strlen(expr_str);
                        }
                        else if (map->sources[i].signal->length < dest_vec_len) {
                            snprintf(expr_str + offset, 256 - offset,
                                     "[x%d,0", i);
                            offset = strlen(expr_str);
                            for (j = 1; j < dest_vec_len - map->sources[0].signal->length; j++) {
                                snprintf(expr_str + offset, 256 - offset, ",0");
                                offset += 2;
                            }
                            snprintf(expr_str + offset, 256 - offset, "]+");
                            offset += 2;
                        }
                        else {
                            snprintf(expr_str + offset, 256 - offset, "x%d+", i);
                            offset = strlen(expr_str);
                        }
                    }
                    --offset;
                    snprintf(expr_str + offset, 256 - offset, ")/%d",
                             map->num_sources);
                }
                mapper_table_set_record(map->props, AT_EXPRESSION, NULL, 1, 's',
                                        expr_str, MODIFIABLE);
            }
            mapper_map_set_mode_expression(map, map->expression);
            break;
        }
    }
}

static int mapper_map_check_status(mapper_map map)
{
    if (bitmatch(map->status, STATUS_READY))
        return map->status;

    map->status |= METADATA_OK;
    int mask = ~METADATA_OK;
    if (map->destination.signal->length)
        map->destination.local->status |= STATUS_LENGTH_KNOWN;
    if (map->destination.signal->type)
        map->destination.local->status |= STATUS_TYPE_KNOWN;
    if (map->destination.local->router_sig
        || (map->destination.local->link
            && map->destination.local->link->remote_device->host))
        map->destination.local->status |= STATUS_LINK_KNOWN;
    map->status &= (map->destination.local->status | mask);

    int i;
    for (i = 0; i < map->num_sources; i++) {
        if (map->sources[i].signal->length)
            map->sources[i].local->status |= STATUS_LENGTH_KNOWN;
        if (map->sources[i].signal->type)
            map->sources[i].local->status |= STATUS_TYPE_KNOWN;
        if (map->sources[i].local->router_sig
            || (map->sources[i].local->link
                && map->sources[i].local->link->remote_device->host))
            map->sources[i].local->status |= STATUS_LINK_KNOWN;
        map->status &= (map->sources[i].local->status | mask);
    }

    if (map->status == METADATA_OK) {
        // allocate memory for map history
        for (i = 0; i < map->num_sources; i++) {
            init_slot_history(&map->sources[i]);
        }
        init_slot_history(&map->destination);
        if (!map->local->expr_vars) {
            map->local->expr_vars = calloc(1, sizeof(mapper_history*)
                                           * map->local->num_var_instances);
        }
        map->status = STATUS_READY;
        apply_mode(map);
    }
    return map->status;
}

// if 'override' flag is not set, only remote properties can be set
int mapper_map_set_from_message(mapper_map map, mapper_message msg, int override)
{
    int i, j, updated = 0;
    mapper_message_atom atom;
    if (!msg) {
        if (map->local && map->status < STATUS_READY) {
            // check if mapping is now "ready"
            mapper_map_check_status(map);
        }
        return 0;
    }

    if (map->destination.direction == MAPPER_DIR_OUTGOING) {
        // check if AT_SLOT property is defined
        atom = mapper_message_property(msg, AT_SLOT);
        if (atom && atom->length == map->num_sources) {
            for (i = 0; i < map->num_sources; i++) {
                map->sources[i].id = (atom->values[i])->i32;
            }
        }
    }

    // set destination slot properties
    int status = 0xFF;
    updated += mapper_slot_set_from_message(&map->destination, msg,
                                            DST_SLOT_PROPERTY, &status);

    // set source slot properties
    for (i = 0; i < map->num_sources; i++) {
        updated += mapper_slot_set_from_message(&map->sources[i], msg,
                                                SRC_SLOT_PROPERTY(map->sources[i].id),
                                                &status);
    }

    for (i = 0; i < msg->num_atoms; i++) {
        atom = &msg->atoms[i];
        switch (atom->index) {
            case AT_NUM_INPUTS:
            case AT_STATUS:
                if (map->local)
                    break;
                updated += mapper_table_set_record_from_atom(map->props, atom,
                                                             REMOTE_MODIFY);
                break;
            case AT_PROCESS_LOCATION: {
                int loc = ((strcmp(&(atom->values[0])->s, "source")==0)
                           ? MAPPER_LOC_SOURCE : MAPPER_LOC_DESTINATION);
                if (map->local && !map->local->one_source) {
                    /* Processing must take place at destination if map
                     * includes source signals from different devices. */
                    loc = MAPPER_LOC_DESTINATION;
                }
                updated += mapper_table_set_record(map->props,
                                                   AT_PROCESS_LOCATION, NULL, 1,
                                                   'i', &loc, REMOTE_MODIFY);
                break;
            }
            case AT_EXPRESSION: {
                    const char *expr_str = &atom->values[0]->s;
                    if (map->local && bitmatch(map->status, STATUS_READY)) {
                        if (strstr(expr_str, "y{-")) {
                            int loc = MAPPER_LOC_DESTINATION;
                            updated += mapper_table_set_record(map->props,
                                                               AT_PROCESS_LOCATION,
                                                               NULL, 1, 'i',
                                                               &loc, REMOTE_MODIFY);
                        }
                        int should_compile = 0;
                        if (map->process_location == MAPPER_LOC_DESTINATION) {
                            // check if destination is local
                            if (map->destination.local->router_sig)
                                should_compile = 1;
                        }
                        else {
                            for (j = 0; j < map->num_sources; j++) {
                                if (map->sources[j].local->router_sig)
                                    should_compile = 1;
                            }
                        }
                        if (should_compile) {
                            if (!replace_expression_string(map, expr_str))
                                reallocate_map_histories(map);
                            else {
                                updated += mapper_table_set_record(map->props,
                                                                   AT_EXPRESSION,
                                                                   NULL, 1, 's',
                                                                   expr_str,
                                                                   REMOTE_MODIFY);
                            }
                        }
                    }
                    else {
                        updated += mapper_table_set_record(map->props,
                                                           AT_EXPRESSION, NULL,
                                                           1, 's', expr_str,
                                                           REMOTE_MODIFY);
                    }
                }
                break;
            case AT_SCOPE:
                if (is_string_type(atom->types[0])) {
                    updated += mapper_map_update_scope(map, atom);
                }
                break;
            case AT_SCOPE | PROPERTY_ADD:
                for (j = 0; j < atom->length; j++)
                    updated += add_scope_internal(map, &(atom->values[j])->s);
                break;
            case AT_SCOPE | PROPERTY_REMOVE:
                for (j = 0; j < atom->length; j++)
                    updated += remove_scope_internal(map, &(atom->values[j])->s);
                break;
            case AT_MODE: {
                int mode = mapper_mode_from_string(&(atom->values[0])->s);
                updated += mapper_table_set_record(map->props, AT_MODE, NULL,
                                                   1, 'i', &mode, REMOTE_MODIFY);
                break;
            }
            case AT_EXTRA:
                if (!atom->key)
                    break;
            case AT_ID:
            case AT_DESCRIPTION:
            case AT_MUTED:
            case AT_VERSION:
                updated += mapper_table_set_record_from_atom(map->props, atom,
                                                             REMOTE_MODIFY);
                break;
            default:
                break;
        }
    }

    if (map->local) {
        if (map->status < STATUS_READY) {
            // check if mapping is now "ready"
            mapper_map_check_status(map);
        }
        else if (updated)
            apply_mode(map);
    }
    return updated;
}

/* TODO: figuring out the correct number of instances for the user variables
 * is a bit tricky... for now we will use the maximum. */
void reallocate_map_histories(mapper_map map)
{
    int i, j;
    mapper_slot slot;
    mapper_slot_internal islot;
    int history_size;

    // If there is no expression, then no memory needs to be reallocated.
    if (!map->local->expr)
        return;

    // Reallocate source histories
    for (i = 0; i < map->num_sources; i++) {
        slot = &map->sources[i];
        islot = slot->local;

        history_size = mapper_expr_input_history_size(map->local->expr, i);
        if (history_size > islot->history_size) {
            size_t sample_size = mapper_type_size(slot->signal->type) * slot->signal->length;;
            for (j = 0; j < slot->num_instances; j++) {
                mhist_realloc(&islot->history[j], history_size, sample_size, 1);
            }
            islot->history_size = history_size;
        }
        else if (history_size < islot->history_size) {
            // Do nothing for now...
        }
    }

    history_size = mapper_expr_output_history_size(map->local->expr);
    slot = &map->destination;
    islot = slot->local;

    // reallocate output histories
    if (history_size > islot->history_size) {
        int sample_size = mapper_type_size(slot->signal->type) * slot->signal->length;
        for (i = 0; i < slot->num_instances; i++) {
            mhist_realloc(&islot->history[i], history_size, sample_size, 0);
        }
        islot->history_size = history_size;
    }
    else if (history_size < islot->history_size) {
        // Do nothing for now...
    }

    // reallocate user variable histories
    int new_num_vars = mapper_expr_num_variables(map->local->expr);
    if (new_num_vars > map->local->num_expr_vars) {
        for (i = 0; i < map->local->num_var_instances; i++) {
            map->local->expr_vars[i] = realloc(map->local->expr_vars[i],
                                               new_num_vars *
                                               sizeof(struct _mapper_history));
            // initialize new variables...
            for (j = map->local->num_expr_vars; j < new_num_vars; j++) {
                map->local->expr_vars[i][j].type = 'd';
                map->local->expr_vars[i][j].length = 0;
                map->local->expr_vars[i][j].size = 0;
                map->local->expr_vars[i][j].position = -1;
            }
            for (j = 0; j < new_num_vars; j++) {
                int history_size = mapper_expr_variable_history_size(map->local->expr, j);
                int vector_length = mapper_expr_variable_vector_length(map->local->expr, j);
                mhist_realloc(map->local->expr_vars[i]+j, history_size,
                              vector_length * sizeof(double), 0);
                (map->local->expr_vars[i]+j)->length = vector_length;
                (map->local->expr_vars[i]+j)->size = history_size;
                (map->local->expr_vars[i]+j)->position = -1;
            }
        }
        map->local->num_expr_vars = new_num_vars;
    }
    else if (new_num_vars < map->local->num_expr_vars) {
        // Do nothing for now...
    }
}

void mhist_realloc(mapper_history history,
                   int history_size,
                   int sample_size,
                   int is_input)
{
    if (!history || !history_size || !sample_size)
        return;
    if (history_size == history->size)
        return;
    if (!is_input || (history_size > history->size) || (history->position == 0)) {
        // realloc in place
        history->value = realloc(history->value, history_size * sample_size);
        history->timetag = realloc(history->timetag, history_size * sizeof(mapper_timetag_t));
        if (!is_input) {
            // Initialize entire history to 0
            memset(history->value, 0, history_size * sample_size);
            history->position = -1;
        }
        else if (history->position == 0) {
            memset(history->value + sample_size * history->size, 0,
                   sample_size * (history_size - history->size));
        }
        else {
            int new_position = history_size - history->size + history->position;
            memcpy(history->value + sample_size * new_position,
                   history->value + sample_size * history->position,
                   sample_size * (history->size - history->position));
            memcpy(&history->timetag[new_position],
                   &history->timetag[history->position], sizeof(mapper_timetag_t)
                   * (history->size - history->position));
            memset(history->value + sample_size * history->position, 0,
                   sample_size * (history_size - history->size));
        }
    }
    else {
        // copying into smaller array
        if (history->position >= history_size * 2) {
            // no overlap - memcpy ok
            int new_position = history_size - history->size + history->position;
            memcpy(history->value,
                   history->value + sample_size * (new_position - history_size),
                   sample_size * history_size);
            memcpy(&history->timetag,
                   &history->timetag[history->position - history_size],
                   sizeof(mapper_timetag_t) * history_size);
            history->value = realloc(history->value, history_size * sample_size);
            history->timetag = realloc(history->timetag, history_size * sizeof(mapper_timetag_t));
        }
        else {
            // there is overlap between new and old arrays - need to allocate new memory
            mapper_history_t temp;
            temp.value = malloc(sample_size * history_size);
            temp.timetag = malloc(sizeof(mapper_timetag_t) * history_size);
            if (history->position < history_size) {
                memcpy(temp.value, history->value,
                       sample_size * history->position);
                memcpy(temp.value + sample_size * history->position,
                       history->value + sample_size
                       * (history->size - history_size + history->position),
                       sample_size * (history_size - history->position));
                memcpy(temp.timetag, history->timetag,
                       sizeof(mapper_timetag_t) * history->position);
                memcpy(&temp.timetag[history->position],
                       &history->timetag[history->size - history_size + history->position],
                       sizeof(mapper_timetag_t) * (history_size - history->position));
            }
            else {
                memcpy(temp.value, history->value + sample_size
                       * (history->position - history_size),
                       sample_size * history_size);
                memcpy(temp.timetag,
                       &history->timetag[history->position - history_size],
                       sizeof(mapper_timetag_t) * history_size);
                history->position = history_size - 1;
            }
            free(history->value);
            free(history->timetag);
            history->value = temp.value;
            history->timetag = temp.timetag;
        }
    }
    history->size = history_size;
}

/* If the "slot_index" argument is >= 0, we can assume this message will be sent
 * to a peer device rather than an administrator. */
static const char *add_properties_to_message(mapper_map map, lo_message msg,
                                             int slot, int flags)
{
    int i, indirect, size = 512;
    char *keys = malloc(size * sizeof(char));
    char *key_ptr = keys;
    mapper_link link;

    mapper_table_t *tab = ((flags == UPDATED_PROPS)
                           ? map->staged_props : map->props);
    mapper_table_record_t *rec;;
    for (i = 0; i < tab->num_records; i++) {
        rec = &tab->records[i];
        indirect = (flags != UPDATED_PROPS) && (rec->flags & INDIRECT);
        if (!rec->value)
            continue;
        if (indirect && !(*rec->value))
            continue;
        switch (rec->index) {
            case AT_ID:
                // already added
                break;
            case AT_USER_DATA:
                // local access only
                break;
            case AT_MODE:
                if (rec->type != 'i')
                    break;
                lo_message_add_string(msg, mapper_protocol_string(AT_MODE));
                lo_message_add_string(msg, mapper_mode_string(*(int*)rec->value));
                break;
            case AT_PROCESS_LOCATION:
                if (rec->type != 'i')
                    break;
                lo_message_add_string(msg,
                                      mapper_protocol_string(AT_PROCESS_LOCATION));
                lo_message_add_string(msg,
                                      mapper_location_string(*(int*)rec->value));
                break;
            case AT_EXTRA:
                lo_message_add_string(msg, rec->key);
                mapper_message_add_typed_value(msg, rec->length, rec->type,
                                               rec->value);
                break;
            default:
                // description, expression, muted, scopes, status etc
                lo_message_add_string(msg, mapper_protocol_string(rec->index));
                mapper_message_add_typed_value(msg, rec->length, rec->type,
                                               indirect ? *rec->value : rec->value);
                break;
        }
    }

    // Scope
    if (map->scope.size) {
        lo_message_add_string(msg, mapper_protocol_string(AT_SCOPE));
        for (i = 0; i < map->scope.size; i++) {
            if (map->scope.devices[i])
                lo_message_add_string(msg, map->scope.devices[i]->name);
            else
                lo_message_add_string(msg, "all");
        }
    }

    // Slot
    if (map->destination.direction == MAPPER_DIR_INCOMING
        && map->status < STATUS_READY && flags != UPDATED_PROPS) {
        lo_message_add_string(msg, mapper_protocol_string(AT_SLOT));
        i = (slot >= 0) ? slot : 0;
        link = map->sources[i].local ? map->sources[i].local->link : 0;
        for (; i < map->num_sources; i++) {
            if ((slot >= 0) && link && (link != map->sources[i].local->link))
                break;
            lo_message_add_int32(msg, map->sources[i].id);
        }
    }

    /* source properties */
    i = (slot >= 0) ? slot : 0;
    link = map->sources[i].local ? map->sources[i].local->link : 0;
    for (; i < map->num_sources; i++) {
        if ((slot >= 0) && link && (link != map->sources[i].local->link))
            break;
        mapper_slot_add_props_to_message(msg, &map->sources[i], 0, &key_ptr,
                                         &size, flags);
    }

    /* destination properties */
    mapper_slot_add_props_to_message(msg, &map->destination, 1, &key_ptr, &size,
                                     flags);

    return keys;
}

int mapper_map_send_state(mapper_map map, int slot, network_message_t cmd,
                          int flags)
{
    if (cmd == MSG_MAPPED && map->status < STATUS_READY)
        return slot;
    lo_message msg = lo_message_new();
    if (!msg) {
        trace("couldn't allocate lo_message\n");
        return slot;
    }

    char dest_name[256], source_names[1024];
    snprintf(dest_name, 256, "%s%s",
             map->destination.signal->device->name,
             map->destination.signal->path);

    if (map->destination.direction == MAPPER_DIR_INCOMING) {
        // add mapping destination
        lo_message_add_string(msg, dest_name);
        lo_message_add_string(msg, "<-");
    }

    // add mapping sources
    int i = (slot >= 0) ? slot : 0;
    int len = 0, result;
    mapper_link link = map->sources[i].local ? map->sources[i].local->link : 0;
    for (; i < map->num_sources; i++) {
        if ((slot >= 0) && link && (link != map->sources[i].local->link))
            break;
        result = snprintf(&source_names[len], 1024-len, "%s%s",
                          map->sources[i].signal->device->name,
                          map->sources[i].signal->path);
        if (result < 0 || (len + result + 1) >= 1024) {
            trace("Error encoding sources for combined /mapped msg");
            lo_message_free(msg);
            return slot;
        }
        lo_message_add_string(msg, &source_names[len]);
        len += result + 1;
    }

    if (map->destination.direction == MAPPER_DIR_OUTGOING) {
        // add mapping destination
        lo_message_add_string(msg, "->");
        lo_message_add_string(msg, dest_name);
    }

    // Always add unique id
    lo_message_add_string(msg, mapper_protocol_string(AT_ID));
    lo_message_add_int64(msg, *((int64_t*)&map->id));

    if (!flags) {
        mapper_network_add_message(map->database->network, 0, cmd, msg);
        mapper_network_send(map->database->network);
        return i-1;
    }

    const char *keys = 0;
    if (flags) {
        // add other properties
        keys = add_properties_to_message(map, msg, slot, flags);
    }

    mapper_network_add_message(map->database->network, 0, cmd, msg);
    // send immediately since message refers to generated strings
    mapper_network_send(map->database->network);

    if (keys)
        free((char*)keys);

    return i-1;
}

mapper_map *mapper_map_query_union(mapper_map *query1, mapper_map *query2)
{
    return (mapper_map*)mapper_list_query_union((void**)query1, (void**)query2);
}

mapper_map *mapper_map_query_intersection(mapper_map *query1, mapper_map *query2)
{
    return (mapper_map*)mapper_list_query_intersection((void**)query1,
                                                       (void**)query2);
}

mapper_map *mapper_map_query_difference(mapper_map *query1, mapper_map *query2)
{
    return (mapper_map*)mapper_list_query_difference((void**)query1,
                                                     (void**)query2);
}

mapper_map mapper_map_query_index(mapper_map *maps, int index)
{
    return (mapper_map)mapper_list_query_index((void**)maps, index);
}

mapper_map *mapper_map_query_next(mapper_map *maps)
{
    return (mapper_map*)mapper_list_query_next((void**)maps);
}

mapper_map *mapper_map_query_copy(mapper_map *maps)
{
    return (mapper_map*)mapper_list_query_copy((void**)maps);
}

void mapper_map_query_done(mapper_map *map)
{
    mapper_list_query_done((void**)map);
}

void mapper_map_pp(mapper_map map)
{
    int i;
    if (!map) {
        printf("NULL\n");
        return;
    }

    for (i = 0; i < map->num_sources; i++) {
        printf("%s/%s ", map->sources[i].signal->device->name,
               map->sources[i].signal->name);
    }
    printf("-> %s/%s\n", map->destination.signal->device->name,
           map->destination.signal->name);
    for (i = 0; i < map->num_sources; i++) {
        printf("    source[%d]: ", i);
        mapper_slot_pp(&map->sources[i]);
        printf("\n");
    }
    printf("    destination: ");
    mapper_slot_pp(&map->destination);
    printf("\n    properties: ");

    i = 0;
    const char *key;
    char type;
    const void *val;
    int length;
    while (!mapper_map_property_index(map, i++, &key, &length, &type, &val)) {
        die_unless(val!=0, "returned zero value\n");

        if (length) {
            printf("%s=", key);
            if (strcmp(key, "mode")==0)
                printf("%s", mapper_mode_strings[*((int*)val)] ?: "undefined");
            else if (strcmp(key, mapper_property_string(AT_PROCESS_LOCATION))==0)
                printf("%s", mapper_location_string(*(int*)val) ?: "undefined");
            else
                mapper_property_pp(length, type, val);
            printf(", ");
        }
    }
    printf("\b\b\n");
}
