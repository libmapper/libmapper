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

/* Static data for property tables embedded in the map data structure.
 *
 * Maps have properties that can be indexed by name, and can have extra
 * properties attached to them either by using setter functions on a local
 * representation of the ap, or if they are specified in a map description
 * provided by a remote device. The utility of this is to allow for attaching of
 * arbitrary metadata to objects on the network.  For example, a map could have
 * a 'author' value to indicate the creator's name.
 *
 * It is also useful to be able to look up standard properties like mode,
 * expression, muting, or process location by specifying these as a string.
 *
 * The following data provides a string table (as usable by the implementation
 * in table.c) for indexing the static data existing in the map data
 * structure.  Some of these static properties may not actually exist, such as
 * 'expression' which is an optional property of maps.  Therefore an 'indirect'
 * form is available where the table points to a pointer to the value, which may
 * be null.
 *
 * A property lookup consists of looking through the 'extra' properties of the
 * structure.  If the requested property is not found, then the 'static'
 * properties are searched---in the worst case, an unsuccessful lookup may
 * therefore take twice as long.
 *
 * To iterate through all available properties, the caller must request by
 * index, starting at 0, and incrementing until failure.  They are not
 * guaranteed to be in a particular order.
 */

#define MAP_OFFSET(x) offsetof(mapper_map_t, x)
#define SLOT_OFFSET(x) offsetof(mapper_slot_t, x)

#define SLOT_TYPE       (SLOT_OFFSET(type))
#define SLOT_LENGTH     (SLOT_OFFSET(length))
#define NUM_SCOPES      (MAP_OFFSET(scope.size))
#define NUM_SOURCES     (MAP_OFFSET(num_sources))

#define LOCAL_TYPE      (MAP_OFFSET(local_type))
#define REMOTE_TYPE     (MAP_OFFSET(remote_type))
#define LOCAL_LENGTH    (MAP_OFFSET(local_length))
#define REMOTE_LENGTH   (MAP_OFFSET(remote_length))
#define NUM_SCOPES      (MAP_OFFSET(scope.size))

static property_table_value_t map_values[] = {
    { 's', {1}, -1,         MAP_OFFSET(expression) },
    { 'h', {0}, -1,         MAP_OFFSET(id)},
    { 'i', {0}, -1,         MAP_OFFSET(mode) },
    { 'i', {0}, -1,         MAP_OFFSET(muted) },
    { 'i', {0}, -1,         MAP_OFFSET(scope.size) },
    { 'i', {0}, -1,         MAP_OFFSET(num_sources) },
    { 'i', {0}, -1,         MAP_OFFSET(process_location) },
    { 's', {1}, NUM_SCOPES, MAP_OFFSET(scope.names) },
};

/* This table must remain in alphabetical order. */
static string_table_node_t map_strings[] = {
    { "expression",         &map_values[0] },
    { "id",                 &map_values[1] },
    { "mode",               &map_values[2] },
    { "muted",              &map_values[3] },
    { "num_scopes",         &map_values[4] },
    { "num_sources",        &map_values[5] },
    { "process_at",         &map_values[6] },
    { "scope_names",        &map_values[7] },
};

const int NUM_MAP_STRINGS = sizeof(map_strings)/sizeof(map_strings[0]);
static mapper_string_table_t map_table =
    { map_strings, NUM_MAP_STRINGS, NUM_MAP_STRINGS };

static property_table_value_t slot_values[] = {
    { 'i', {0},         -1,          SLOT_OFFSET(bound_max) },
    { 'i', {0},         -1,          SLOT_OFFSET(bound_min) },
    { 'i', {0},         -1,          SLOT_OFFSET(calibrating) },
    { 'i', {0},         -1,          SLOT_OFFSET(causes_update) },
    { 'i', {0},         -1,          SLOT_OFFSET(direction) },
    { 'i', {0},         -1,          SLOT_OFFSET(length) },
    { 'o', {SLOT_TYPE}, SLOT_LENGTH, SLOT_OFFSET(maximum) },
    { 'o', {SLOT_TYPE}, SLOT_LENGTH, SLOT_OFFSET(minimum) },
    { 'i', {0},         -1,          SLOT_OFFSET(sends_as_instance) },
    { 'c', {0},         -1,          SLOT_OFFSET(type) },
};

/* This table must remain in alphabetical order. */
static string_table_node_t slot_strings[] = {
    { "bound_max",          &slot_values[0] },
    { "bound_min",          &slot_values[1] },
    { "calibrating",        &slot_values[2] },
    { "causes_update",      &slot_values[3] },
    { "direction",          &slot_values[4] },
    { "length",             &slot_values[5] },
    { "maximum",            &slot_values[6] },
    { "minimum",            &slot_values[7] },
    { "sends_as_instance",  &slot_values[8] },
    { "type",               &slot_values[9] },
};

const int NUM_SLOT_STRINGS = sizeof(slot_strings)/sizeof(slot_strings[0]);
static mapper_string_table_t slot_table =
    { slot_strings, NUM_SLOT_STRINGS, NUM_SLOT_STRINGS };

void mapper_map_free(mapper_map map)
{
    if (!map)
        return;
    free(map->sources);
    free(map);
}

const char *mapper_map_description(mapper_map map)
{
    return map->description;
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
    if (map->destination.signal == sig)
        return &map->destination;
    for (i = 0; i < map->num_sources; i++) {
        if (map->sources[i].signal == sig)
            return &map->sources[i];
    }
    return 0;
}

uint64_t mapper_map_id(mapper_map map)
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

// TODO: use query functionality to retrieve map scopes
mapper_device *mapper_map_scopes(mapper_map map)
{
    return 0;
}

int mapper_map_property(mapper_map map, const char *property, int *length,
                        char *type, const void **value)
{
    return mapper_db_property(map, map->extra, property, length, type, value,
                              &map_table);
}

int mapper_map_property_index(mapper_map map, unsigned int index,
                              const char **property, int *length, char *type,
                              const void **value)
{
    return mapper_db_property_index(map, map->extra, index, property, length,
                                    type, value, &map_table);
}

void mapper_map_set_description(mapper_map map, const char *description)
{
    mapper_property_set_string(&map->description, description);
}

void mapper_map_set_mode(mapper_map map, mapper_mode mode)
{
    if (mode >= 1 && mode < NUM_MAPPER_MODES) {
        mapper_table_add_or_update_typed_value(map->updater, "mode", 1, 's',
                                               mapper_mode_strings[mode]);
    }
}

void mapper_map_set_expression(mapper_map map, const char *expression)
{
    mapper_table_add_or_update_typed_value(map->updater, "expression", 1, 's',
                                           expression);
}

void mapper_map_set_muted(mapper_map map, int muted)
{
    mapper_table_add_or_update_typed_value(map->updater, "muted", 1, 'b', &muted);
}

void mapper_map_set_process_location(mapper_map map, mapper_location location)
{
    mapper_table_add_or_update_typed_value(map->updater, "process", 1, 'i',
                                           &location);
}

void mapper_map_add_scope(mapper_map map, mapper_device device)
{
    mapper_property_value_t *prop = table_find_p(map->updater, "scope");
    if (!prop)
        mapper_table_add_or_update_typed_value(map->updater, "scope", 1, 's',
                                               device->name);
    else if (prop->type == 's') {
        const char *names[prop->length+1];
        if (prop->length == 1) {
            names[0] = (const char*)prop->value;
        }
        for (int i = 0; i < prop->length; i++) {
            names[i] = ((const char**)prop->value)[i];
        }
        names[prop->length] = device->name;
        mapper_table_add_or_update_typed_value(map->updater, "scope",
                                               prop->length+1, 's', names);
    }
}

void mapper_map_remove_scope(mapper_map map, mapper_device device)
{
    mapper_property_value_t *prop = table_find_p(map->updater, "-scope");
    if (!prop)
        mapper_table_add_or_update_typed_value(map->updater, "-scope", 1, 's',
                                               device->name);
    else if (prop->type == 's') {
        const char *names[prop->length+1];
        if (prop->length == 1) {
            names[0] = (const char*)prop->value;
        }
        for (int i = 0; i < prop->length; i++) {
            names[i] = ((const char**)prop->value)[i];
        }
        names[prop->length] = device->name;
        mapper_table_add_or_update_typed_value(map->updater, "-scope",
                                               prop->length+1, 's', names);
    }
}

void mapper_map_set_property(mapper_map map, const char *property, int length,
                             char type, const void *value)
{
    if (!map)
        return;
    // can't set num_sources
    if (   strcmp(property, "num_sources") == 0
        || strcmp(property, "source") == 0
        || strcmp(property, "sources") == 0
        || strcmp(property, "destination") == 0
        || strcmp(property, "id") == 0) {
        trace("Cannot set static map property '%s'\n", property);
        return;
    }
    else if (strcmp(property, "description") == 0) {
        if (is_string_type(type) && length == 1)
            mapper_map_set_description(map, (const char*)value);
        else if (!value || !length)
            mapper_map_set_description(map, 0);
        return;
    }
    else if (strcmp(property, "mode") == 0) {
        if (length == 1 && type == 'i')
            mapper_map_set_mode(map, *(int*)value);
        return;
    }
    else if (strcmp(property, "expression") == 0) {
        if (length == 1 && is_string_type(type))
            mapper_map_set_expression(map, (const char*)value);
        return;
    }
    else if (strcmp(property, "muted") == 0) {
        if (length == 1 && type == 'i')
            mapper_map_set_muted(map, *(int*)value);
        return;
    }
    else if (strcmp(property, "process_location") == 0) {
        if (length == 1 && type == 'i')
            mapper_map_set_process_location(map, *(int*)value);
        return;
    }
    else if (   strcmp(property, "scope") == 0
             || strcmp(property, "scopes") == 0) {
        // TODO: remove unmatched scopes
        // TODO: add new scopes
        return;
    }
    mapper_table_add_or_update_typed_value(map->updater, property, length, type,
                                           value);
}

int mapper_slot_index(mapper_slot slot)
{
    if (slot->direction == MAPPER_INCOMING)
        return 0;
    int i;
    for (i = 0; i < slot->map->num_sources; i++) {
        if (slot == &slot->map->sources[i])
            return i;
    }
    return -1;
}

mapper_signal mapper_slot_signal(mapper_slot slot)
{
    return slot->signal;
}

mapper_boundary_action mapper_slot_bound_max(mapper_slot slot)
{
    return slot->bound_max;
}

mapper_boundary_action mapper_slot_bound_min(mapper_slot slot)
{
    return slot->bound_min;
}

int mapper_slot_calibrating(mapper_slot slot)
{
    return (slot->calibrating != 0);
}

int mapper_slot_causes_update(mapper_slot slot)
{
    return slot->causes_update;
}

int mapper_slot_sends_as_instance(mapper_slot slot)
{
    return slot->sends_as_instance;
}

void mapper_slot_maximum(mapper_slot slot, int *length, char *type, void **value)
{
    if (length)
        *length = slot->length;
    if (type)
        *type = slot->type;
    if (value)
        *value = slot->maximum;
}

void mapper_slot_minimum(mapper_slot slot, int *length, char *type, void **value)
{
    if (length)
        *length = slot->length;
    if (type)
        *type = slot->type;
    if (value)
        *value = slot->minimum;
}

int mapper_slot_property(mapper_slot slot, const char *property, int *length,
                         char *type, const void **value)
{
    return mapper_db_property(slot, 0, property, length, type, value,
                              &slot_table);
}

int mapper_slot_property_index(mapper_slot slot, unsigned int index,
                               const char **property, int *length, char *type,
                               const void **value)
{
    return mapper_db_property_index(slot, 0, index, property, length, type,
                                    value, &slot_table);
}

void mapper_slot_set_bound_max(mapper_slot slot, mapper_boundary_action a)
{
    int i;
    char propname[16] = "dst@boundMax";
    if (slot->direction == MAPPER_OUTGOING) {
        // source slot - figure out which source slot we are
        if (slot->map->num_sources == 1)
            snprintf(propname, 16, "src@boundMax");
        else {
            for (i = 0; i < slot->map->num_sources; i++) {
                if (&slot->map->sources[i] == slot) {
                    snprintf(propname, 16, "src.%d@boundMax", i);
                    break;
                }
            }
        }
    }
    mapper_table_add_or_update_typed_value(slot->map->updater, propname, 1, 's',
                                           mapper_boundary_action_strings[a]);
}

void mapper_slot_set_bound_min(mapper_slot slot, mapper_boundary_action a)
{
    int i;
    char propname[16] = "dst@boundMin";
    if (slot->direction == MAPPER_OUTGOING) {
        // source slot - figure out which source slot we are
        if (slot->map->num_sources == 1)
            snprintf(propname, 16, "src@boundMin");
        else {
            for (i = 0; i < slot->map->num_sources; i++) {
                if (&slot->map->sources[i] == slot) {
                    snprintf(propname, 16, "src.%d@boundMin", i);
                    break;
                }
            }
        }
    }
    mapper_table_add_or_update_typed_value(slot->map->updater, propname, 1, 's',
                                           mapper_boundary_action_strings[a]);
}

static void set_bool_slot_prop(mapper_slot slot, const char *propname, int value)
{
    int i;
    char fullpropname[128];
    if (slot->direction == MAPPER_INCOMING) {
        // destination slot
        snprintf(fullpropname, 128, "dst@%s", propname);
    }
    else {
        // source slot - figure out which source slot we are
        if (slot->map->num_sources == 1)
            snprintf(fullpropname, 16, "src@%s", propname);
        else {
            for (i = 0; i < slot->map->num_sources; i++) {
                if (&slot->map->sources[i] == slot) {
                    snprintf(fullpropname, 128, "src.%d@%s", i, propname);
                    break;
                }
            }
        }
    }
    mapper_table_add_or_update_typed_value(slot->map->updater, fullpropname,
                                           1, 'b', &value);
}

void mapper_slot_set_calibrating(mapper_slot slot, int calibrating)
{
    set_bool_slot_prop(slot, "calibrating", calibrating);
}

void mapper_slot_set_causes_update(mapper_slot slot, int causes_update)
{
    set_bool_slot_prop(slot, "causesUpdate", causes_update);
}

void mapper_slot_set_sends_as_instance(mapper_slot slot, int sends_as_instance)
{
    set_bool_slot_prop(slot, "sendsAsInstance", sends_as_instance);
}

static void set_extrema_prop(mapper_slot slot, const char *propname, int length,
                             char type, const void *value)
{
    int i;
    char fullpropname[128];
    if (slot->direction == MAPPER_INCOMING) {
        // destination slot
        snprintf(fullpropname, 128, "dst@%s", fullpropname);
    }
    else {
        // source slot - figure out which slot we are
        if (slot->map->num_sources == 1)
            snprintf(fullpropname, 16, "src@%s", propname);
        else {
            for (i = 0; i < slot->map->num_sources; i++) {
                if (&slot->map->sources[i] == slot) {
                    snprintf(fullpropname, 128, "src.%d@%s", i, propname);
                    break;
                }
            }
        }
    }
    mapper_table_add_or_update_typed_value(slot->map->updater, fullpropname,
                                           length, type, value);
}

void mapper_slot_set_maximum(mapper_slot slot, int length, char type,
                             const void *value)
{
    set_extrema_prop(slot, "max", length, type, value);
}

void mapper_slot_set_minimum(mapper_slot slot, int length, char type,
                             const void *value)
{
    set_extrema_prop(slot, "min", length, type, value);
}

void mapper_slot_set_property(mapper_slot slot, const char *property,
                              int length, char type, const void *value)
{
    if (!slot)
        return;
    if (strcmp(property, "bound_max") == 0) {
        if (length != 1 || type != 'i')
            return;
        int bound_max = *(int*)value;
        if (bound_max <= MAPPER_UNDEFINED || bound_max > NUM_MAPPER_BOUNDARY_ACTIONS)
            return;
        mapper_slot_set_bound_max(slot, bound_max);
    }
    else if (strcmp(property, "bound_min") == 0) {
        if (length != 1 || type != 'i')
            return;
        int bound_min = *(int*)value;
        if (bound_min <= MAPPER_UNDEFINED || bound_min > NUM_MAPPER_BOUNDARY_ACTIONS)
            return;
        mapper_slot_set_bound_min(slot, bound_min);
    }
    else if (strcmp(property, "calibrating") == 0) {
        if (length != 1 || type != 'i')
            return;
        mapper_slot_set_calibrating(slot, *(int*)value);
    }
    else if (strcmp(property, "causes_update") == 0) {
        if (length != 1 || type != 'i')
            return;
        mapper_slot_set_causes_update(slot, *(int*)value);
    }
    else if (strcmp(property, "sends_as_instance") == 0) {
        if (length != 1 || type != 'i')
            return;
        mapper_slot_set_sends_as_instance(slot, *(int*)value);
    }
    else if (strcmp(property, "maximum") == 0
             || strcmp(property, "max") == 0) {
        mapper_slot_set_maximum(slot, length, type, value);
    }
    else if (strcmp(property, "minimum") == 0
             || strcmp(property, "min") == 0) {
        mapper_slot_set_minimum(slot, length, type, value);
    }

    // TODO: should we allow slots to have "extra" properties?
//    else {
//        mapper_table_add_or_update_typed_value(slot->updater, property, type,
//                                               value, length);
//    }
}

static int add_scope_internal(mapper_map_scope scope, const char *name)
{
    int i;
    if (!scope || !name)
        return 1;

    // Check if scope is already stored for this map
    uint32_t hash;
    if (strcmp(name, "all")==0)
        hash = 0;
    else
        hash = crc32(0L, (const Bytef*)name, strlen(name));
    for (i=0; i<scope->size; i++)
        if (scope->hashes[i] == hash)
            return 1;

    // not found - add a new scope
    i = ++scope->size;
    scope->names = realloc(scope->names, i * sizeof(char*));
    scope->names[i-1] = strdup(name);
    scope->hashes = realloc(scope->hashes, i * sizeof(uint32_t));
    scope->hashes[i-1] = hash;
    return 0;
}

static int remove_scope_internal(mapper_map_scope scope, int index)
{
    int i;

    free(scope->names[index]);
    for (i = index+1; i < scope->size; i++) {
        scope->names[i-1] = scope->names[i];
        scope->hashes[i-1] = scope->hashes[i];
    }
    scope->size--;
    scope->names = realloc(scope->names, scope->size * sizeof(char*));
    scope->hashes = realloc(scope->hashes, scope->size * sizeof(uint32_t));
    return 0;
}

static int mapper_map_update_scope(mapper_map_scope scope,
                                   mapper_message_atom atom)
{
    int i, j, updated = 0, num = atom->length;
    lo_arg **scope_list = atom->values;
    if (scope_list && *scope_list) {
        if (num == 1 && strcmp(&scope_list[0]->s, "none")==0)
            num = 0;

        // First remove old scopes that are missing
        for (i = 0; i < scope->size; i++) {
            int found = 0;
            for (j = 0; j < num; j++) {
                if (strcmp(scope->names[i], &scope_list[j]->s) == 0) {
                    found = 1;
                    break;
                }
            }
            if (!found) {
                remove_scope_internal(scope, i);
                updated++;
            }
        }
        // ...then add any new scopes
        for (i = 0; i < num; i++)
            updated += (1 - add_scope_internal(scope, &scope_list[i]->s));
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

    if (slot->calibrating == 1)
    {
        if (!slot->minimum) {
            slot->minimum = malloc(slot->length * mapper_type_size(slot->type));
        }
        if (!slot->maximum) {
            slot->maximum = malloc(slot->length * mapper_type_size(slot->type));
        }

        /* If calibration mode has just taken effect, first data
         * sample sets source min and max */
        switch (slot->type) {
            case 'f': {
                float *v = mapper_history_value_ptr(*from);
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
                int *v = mapper_history_value_ptr(*from);
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
                double *v = mapper_history_value_ptr(*from);
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

    if (map->local->status != MAPPER_ACTIVE || map->muted) {
        return 0;
    }
    else if (map->process_location == MAPPER_DESTINATION) {
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
            typestring[i] = map->sources[0].type;
        return 1;
    }

    if (!map->local->expr) {
        trace("error: missing expression.\n");
        return 0;
    }

    mapper_history sources[map->num_sources];
    for (i = 0; i < map->num_sources; i++)
        sources[i] = &map->sources[i].local->history[instance];
    return (mapper_expr_evaluate(map->local->expr,
                                 sources, &map->local->expr_vars[instance], &to[instance],
                                 mapper_history_tt_ptr(from[instance]),
                                 typestring));
}

int mapper_boundary_perform(mapper_history history, mapper_slot s,
                            char *typestring)
{
    int i, muted = 0;

    double value;
    double dest_min, dest_max, swap, total_range, difference, modulo_difference;
    mapper_boundary_action bound_min, bound_max;

    if (s->bound_min == MAPPER_NONE && s->bound_max == MAPPER_NONE) {
        return 0;
    }
    if (!s->minimum && (s->bound_min != MAPPER_NONE || s->bound_max == MAPPER_WRAP)) {
        return 0;
    }
    if (!s->maximum && (s->bound_max != MAPPER_NONE || s->bound_min == MAPPER_WRAP)) {
        return 0;
    }

    for (i = 0; i < history->length; i++) {
        if (typestring[i] == 'N') {
            muted++;
            continue;
        }
        value = propval_double(mapper_history_value_ptr(*history), s->type, i);
        dest_min = propval_double(s->minimum, s->type, i);
        dest_max = propval_double(s->maximum, s->type, i);
        if (dest_min < dest_max) {
            bound_min = s->bound_min;
            bound_max = s->bound_max;
        }
        else {
            bound_min = s->bound_max;
            bound_max = s->bound_min;
            swap = dest_max;
            dest_max = dest_min;
            dest_min = swap;
        }
        total_range = fabs(dest_max - dest_min);
        if (value < dest_min) {
            switch (bound_min) {
                case MAPPER_MUTE:
                    // need to prevent value from being sent at all
                    typestring[i] = 'N';
                    muted++;
                    break;
                case MAPPER_CLAMP:
                    // clamp value to range minimum
                    value = dest_min;
                    break;
                case MAPPER_FOLD:
                    // fold value around range minimum
                    difference = fabs(value - dest_min);
                    value = dest_min + difference;
                    if (value > dest_max) {
                        // value now exceeds range maximum!
                        switch (bound_max) {
                            case MAPPER_MUTE:
                                // need to prevent value from being sent at all
                                typestring[i] = 'N';
                                muted++;
                                break;
                            case MAPPER_CLAMP:
                                // clamp value to range minimum
                                value = dest_max;
                                break;
                            case MAPPER_FOLD:
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
                            case MAPPER_WRAP:
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
                case MAPPER_WRAP:
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
                case MAPPER_MUTE:
                    // need to prevent value from being sent at all
                    typestring[i] = 'N';
                    muted++;
                    break;
                case MAPPER_CLAMP:
                    // clamp value to range maximum
                    value = dest_max;
                    break;
                case MAPPER_FOLD:
                    // fold value around range maximum
                    difference = fabs(value - dest_max);
                    value = dest_max - difference;
                    if (value < dest_min) {
                        // value now exceeds range minimum!
                        switch (bound_min) {
                            case MAPPER_MUTE:
                                // need to prevent value from being sent at all
                                typestring[i] = 'N';
                                muted++;
                                break;
                            case MAPPER_CLAMP:
                                // clamp value to range minimum
                                value = dest_min;
                                break;
                            case MAPPER_FOLD:
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
                            case MAPPER_WRAP:
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
                case MAPPER_WRAP:
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
        propval_set_double(mapper_history_value_ptr(*history), s->type, i, value);
    }
    return (muted == history->length);
}

/*! Build a value update message for a given map. */
lo_message mapper_map_build_message(mapper_map map, mapper_slot slot,
                                    const void *value, int count,
                                    char *typestring, mapper_id_map id_map)
{
    int i;
    int length = ((map->process_location == MAPPER_SOURCE)
                  ? map->destination.length * count : slot->length * count);

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

    if (map->process_location == MAPPER_DESTINATION) {
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
    if (map->local->expr && map->expression && strcmp(map->expression, expr_str)==0)
        return 1;

    if (map->local->status < (MAPPER_TYPE_KNOWN | MAPPER_LENGTH_KNOWN))
        return 1;

    int i;
    char source_types[map->num_sources];
    int source_lengths[map->num_sources];
    for (i = 0; i < map->num_sources; i++) {
        source_types[i] = map->sources[i].type;
        source_lengths[i] = map->sources[i].length;
    }
    mapper_expr expr = mapper_expr_new_from_string(expr_str,
                                                   map->num_sources,
                                                   source_types, source_lengths,
                                                   map->destination.type,
                                                   map->destination.length);

    if (!expr)
        return 1;

    // expression update may force processing location to change
    // e.g. if expression combines signals from different devices
    // e.g. if expression refers to current/past value of destination
    int output_history_size = mapper_expr_output_history_size(expr);
    if (output_history_size > 1 && map->process_location == MAPPER_SOURCE) {
        map->process_location = MAPPER_DESTINATION;
        // copy expression string but do not execute it
        if (map->expression)
            free(map->expression);
        map->expression = strdup(expr_str);
        return 1;
    }

    if (map->local->expr)
        mapper_expr_free(map->local->expr);

    map->local->expr = expr;

    if (map->expression == expr_str)
        return 0;

    int len = strlen(expr_str);
    if (!map->expression || len > strlen(map->expression))
        map->expression = realloc(map->expression, len+1);

    /* Using strncpy() here causes memory profiling errors due to possible
     * overlapping memory (e.g. expr_str == map->expression). */
    memcpy(map->expression, expr_str, len);
    map->expression[len] = '\0';

    return 0;
}

void mapper_map_set_mode_raw(mapper_map map)
{
    map->mode = MAPPER_MODE_RAW;
    reallocate_map_histories(map);
}

static int mapper_map_set_mode_linear(mapper_map map)
{
    if (map->num_sources > 1)
        return 1;

    if (map->local->status < (MAPPER_TYPE_KNOWN | MAPPER_LENGTH_KNOWN))
        return 1;

    int i, len;
    char expr[256] = "";
    const char *e = expr;

    if (   !map->sources[0].minimum  || !map->sources[0].maximum
        || !map->destination.minimum || !map->destination.maximum)
        return 1;

    int min_length = map->sources[0].length < map->destination.length ?
                     map->sources[0].length : map->destination.length;
    double src_min, src_max, dest_min, dest_max;

    if (map->destination.length == map->sources[0].length)
        snprintf(expr, 256, "y=x*");
    else if (map->destination.length > map->sources[0].length) {
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
                                 map->sources[0].type, i);
        src_max = propval_double(map->sources[0].maximum,
                                 map->sources[0].type, i);

        len = strlen(expr);
        if (src_min == src_max)
            snprintf(expr+len, 256-len, "0,");
        else {
            dest_min = propval_double(map->destination.minimum,
                                      map->destination.type, i);
            dest_max = propval_double(map->destination.maximum,
                                      map->destination.type, i);
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
    for (i=0; i<min_length; i++) {
        src_min = propval_double(map->sources[0].minimum,
                                 map->sources[0].type, i);
        src_max = propval_double(map->sources[0].maximum,
                                 map->sources[0].type, i);

        len = strlen(expr);
        if (src_min == src_max)
            snprintf(expr+len, 256-len, "%g,", dest_min);
        else {
            dest_min = propval_double(map->destination.minimum,
                                      map->destination.type, i);
            dest_max = propval_double(map->destination.maximum,
                                      map->destination.type, i);
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
        if (map->process_location == MAPPER_DESTINATION) {
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
            if (!map->expression)
                map->expression = strdup(e);
            else if (strcmp(map->expression, e)) {
                free(map->expression);
                map->expression = strdup(e);
            }
        }
        map->mode = MAPPER_MODE_LINEAR;
        return 0;
    }
    return 1;
}

void mapper_map_set_mode_expression(mapper_map map, const char *expr)
{
    if (map->local->status < (MAPPER_TYPE_KNOWN | MAPPER_LENGTH_KNOWN))
        return;

    int i, should_compile = 0;
    if (map->process_location == MAPPER_DESTINATION) {
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
        if (!map->expression)
            map->expression = strdup(expr);
        else if (strcmp(map->expression, expr)) {
            free(map->expression);
            map->expression = strdup(expr);
        }
        map->mode = MAPPER_MODE_EXPRESSION;
        return;
    }

    /* Special case: if we are the receiver and the new expression
     * evaluates to a constant we can update immediately. */
    /* TODO: should call handler for all instances updated
     * through this map. */
    int sends_as_instance = 0;
    for (i = 0; i < map->num_sources; i++) {
        if (map->sources[i].sends_as_instance) {
            sends_as_instance = 1;
            break;
        }
    }
    sends_as_instance += map->destination.sends_as_instance;
    if (mapper_expr_constant_output(map->local->expr) && !sends_as_instance) {
        mapper_timetag_t now;
        mapper_clock_now(&map->db->network->clock, &now);

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

static void init_map_history(mapper_slot slot)
{
    int i;
    if (slot->local->history)
        return;
    slot->local->history = malloc(sizeof(struct _mapper_history)
                                  * slot->num_instances);
    slot->local->history_size = 1;
    for (i = 0; i < slot->num_instances; i++) {
        slot->local->history[i].type = slot->type;
        slot->local->history[i].length = slot->length;
        slot->local->history[i].size = 1;
        slot->local->history[i].value = calloc(1, mapper_type_size(slot->type)
                                               * slot->length);
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
                if (map->num_sources == 1) {
                    if (map->sources[0].length == map->destination.length)
                        map->expression = strdup("y=x");
                    else {
                        char expr[256] = "";
                        if (map->sources[0].length > map->destination.length) {
                            // truncate source
                            if (map->destination.length == 1)
                                snprintf(expr, 256, "y=x[0]");
                            else
                                snprintf(expr, 256, "y=x[0:%i]",
                                         map->destination.length-1);
                        }
                        else {
                            // truncate destination
                            if (map->sources[0].length == 1)
                                snprintf(expr, 256, "y[0]=x");
                            else
                                snprintf(expr, 256, "y[0:%i]=x",
                                         map->sources[0].length-1);
                        }
                        map->expression = strdup(expr);
                    }
                }
                else {
                    // check vector lengths
                    int i, j, max_vec_len = 0, min_vec_len = INT_MAX;
                    for (i = 0; i < map->num_sources; i++) {
                        if (map->sources[i].length > max_vec_len)
                            max_vec_len = map->sources[i].length;
                        if (map->sources[i].length < min_vec_len)
                            min_vec_len = map->sources[i].length;
                    }
                    char expr[256] = "";
                    int offset = 0, dest_vec_len;
                    if (max_vec_len < map->destination.length) {
                        snprintf(expr, 256, "y[0:%d]=(", max_vec_len-1);
                        offset = strlen(expr);
                        dest_vec_len = max_vec_len;
                    }
                    else {
                        snprintf(expr, 256, "y=(");
                        offset = 3;
                        dest_vec_len = map->destination.length;
                    }
                    for (i = 0; i < map->num_sources; i++) {
                        if (map->sources[i].length > dest_vec_len) {
                            snprintf(expr+offset, 256-offset, "x%d[0:%d]+",
                                     i, dest_vec_len-1);
                            offset = strlen(expr);
                        }
                        else if (map->sources[i].length < dest_vec_len) {
                            snprintf(expr+offset, 256-offset, "[x%d,0", i);
                            offset = strlen(expr);
                            for (j = 1; j < dest_vec_len - map->sources[0].length; j++) {
                                snprintf(expr+offset, 256-offset, ",0");
                                offset += 2;
                            }
                            snprintf(expr+offset, 256-offset, "]+");
                            offset += 2;
                        }
                        else {
                            snprintf(expr+offset, 256-offset, "x%d+", i);
                            offset = strlen(expr);
                        }
                    }
                    --offset;
                    snprintf(expr+offset, 256-offset, ")/%d", map->num_sources);
                    map->expression = strdup(expr);
                }
            }
            mapper_map_set_mode_expression(map, map->expression);
            break;
        }
    }
}

static int mapper_map_check_status(mapper_map map)
{
    map->local->status |= MAPPER_READY;
    int mask = ~MAPPER_READY;
    if (map->destination.local->router_sig
        || (map->destination.local->link
            && map->destination.local->link->remote_device->host))
        map->destination.local->status |= MAPPER_LINK_KNOWN;
    map->local->status &= (map->destination.local->status | mask);

    int i;
    for (i = 0; i < map->num_sources; i++) {
        if (map->sources[i].local->router_sig
            || (map->sources[i].local->link
                && map->sources[i].local->link->remote_device->host))
            map->sources[i].local->status |= MAPPER_LINK_KNOWN;
        map->local->status &= (map->sources[i].local->status | mask);
    }

    if ((map->local->status & MAPPER_TYPE_KNOWN)
        && (map->local->status & MAPPER_LENGTH_KNOWN)) {
        // allocate memory for map history
        // TODO: should we wait for link information also?
        for (i = 0; i < map->num_sources; i++) {
            init_map_history(&map->sources[i]);
        }
        init_map_history(&map->destination);
        if (!map->local->expr_vars) {
            map->local->expr_vars = calloc(1, sizeof(mapper_history)
                                           * map->local->num_var_instances);
        }
        apply_mode(map);
    }
    return map->local->status;
}

static void upgrade_extrema_memory(mapper_slot slot, int length, char type)
{
    int i;
    if (slot->minimum) {
        void *new_mem = calloc(1, length * mapper_type_size(type));
        switch (slot->type) {
            case 'i':
                for (i = 0; i < length; i++) {
                    propval_set_from_lo_arg(new_mem, type,
                                            (lo_arg*)&((int*)slot->minimum)[i],
                                            'i', i);
                }
                break;
            case 'f':
                for (i = 0; i < length; i++) {
                    propval_set_from_lo_arg(new_mem, type,
                                            (lo_arg*)&((float*)slot->minimum)[i],
                                            'f', i);
                }
                break;
            case 'd':
                for (i = 0; i < length; i++) {
                    propval_set_from_lo_arg(new_mem, type,
                                            (lo_arg*)&((double*)slot->minimum)[i],
                                            'd', i);
                }
                break;
            default:
                break;
        }
        free(slot->minimum);
        slot->minimum = new_mem;
    }
    if (slot->maximum) {
        void *new_mem = calloc(1, length * mapper_type_size(type));
        switch (slot->type) {
            case 'i':
                for (i = 0; i < length; i++) {
                    propval_set_from_lo_arg(new_mem, type,
                                            (lo_arg*)&((int*)slot->minimum)[i],
                                            'i', i);
                }
                break;
            case 'f':
                for (i = 0; i < length; i++) {
                    propval_set_from_lo_arg(new_mem, type,
                                            (lo_arg*)&((float*)slot->minimum)[i],
                                            'f', i);
                }
                break;
            case 'd':
                for (i = 0; i < length; i++) {
                    propval_set_from_lo_arg(new_mem, type,
                                            (lo_arg*)&((double*)slot->minimum)[i],
                                            'd', i);
                }
                break;
            default:
                break;
        }
        free(slot->maximum);
        slot->maximum = new_mem;
    }
    slot->type = type;
    slot->length= length;
}

static int set_slot_from_message(mapper_slot slot, mapper_message msg, int mask)
{
    int i, updated = 0, result;
    mapper_message_atom atom;

    /* slot id */
//    updated += mapper_update_int_if_arg(&slot->id, msg, AT_SLOT | mask);

    /* type and length */
    if (!slot->local || !slot->local->router_sig) {
        char type = slot->type;
        if (mapper_update_char_if_arg(&type, msg, AT_TYPE | mask)) {
            if (slot->local)
                slot->local->status |= MAPPER_TYPE_KNOWN;
            updated++;
        }

        int length = slot->length;
        if (mapper_update_int_if_arg(&length, msg, AT_LENGTH | mask)) {
            if (slot->local)
                slot->local->status |= MAPPER_LENGTH_KNOWN;
            updated++;
        }
        if (updated)
            upgrade_extrema_memory(slot, length, type);
    }

    /* range boundary actions */
    mapper_boundary_action bound;
    atom = mapper_message_param(msg, AT_BOUND_MAX | mask);
    if (atom && (atom->length == 1) && is_string_type(atom->types[0])) {
        bound = mapper_boundary_action_from_string(&(atom->values[0])->s);
        if (bound != MAPPER_UNDEFINED && bound != slot->bound_max) {
            slot->bound_max = bound;
            updated++;
        }
    }
    atom = mapper_message_param(msg, AT_BOUND_MIN | mask);
    if (atom && (atom->length == 1) && is_string_type(atom->types[0])) {
        bound = mapper_boundary_action_from_string(&(atom->values[0])->s);
        if (bound != MAPPER_UNDEFINED && bound != slot->bound_min) {
            slot->bound_min = bound;
            updated++;
        }
    }

    /* calibrating */
    updated += mapper_update_bool_if_arg(&slot->calibrating, msg,
                                         AT_CALIBRATING | mask);

    /* causes update */
    updated += mapper_update_bool_if_arg(&slot->causes_update, msg,
                                         AT_CAUSES_UPDATE | mask);

    /* sends as instance */
    updated += mapper_update_bool_if_arg(&slot->sends_as_instance, msg,
                                         AT_SENDS_AS_INSTANCE | mask);

    /* maximum */
    atom = mapper_message_param(msg, AT_MAX | mask);
    if (atom && is_number_type(atom->types[0])) {
        if (!slot->type)
            slot->type = atom->types[0];
        if (!slot->length)
            slot->length = atom->length;
        if (slot->local)
            slot->local->status |= MAPPER_TYPE_KNOWN | MAPPER_LENGTH_KNOWN;
        if (!slot->maximum)
            slot->maximum = calloc(1, slot->length * mapper_type_size(slot->type));
        for (i = 0; i < atom->length && i < slot->length; i++) {
            result = propval_set_from_lo_arg(slot->maximum, slot->type,
                                             ((lo_arg**)atom->values)[i],
                                             atom->types[i], i);
            if (result == -1)
                break;
            updated += result;
        }
    }
    if (!slot->maximum && slot->signal->maximum) {
        // copy range from signal
        slot->maximum = malloc(mapper_signal_vector_bytes(slot->signal));
        memcpy(slot->maximum, slot->signal->maximum,
               mapper_signal_vector_bytes(slot->signal));
        updated++;
    }

    /* minimum */
    atom = mapper_message_param(msg, AT_MIN | mask);
    if (atom && is_number_type(atom->types[0])) {
        if (!slot->type)
            slot->type = atom->types[0];
        if (!slot->length)
            slot->length = atom->length;
        if (slot->local)
            slot->local->status |= MAPPER_TYPE_KNOWN | MAPPER_LENGTH_KNOWN;
        if (!slot->minimum)
            slot->minimum = calloc(1, slot->length * mapper_type_size(slot->type));
        for (i = 0; i < atom->length && i < slot->length; i++) {
            result = propval_set_from_lo_arg(slot->minimum, slot->type,
                                             ((lo_arg**)atom->values)[i],
                                             atom->types[i], i);
            if (result == -1)
                break;
            updated += result;
        }
    }
    if (!slot->minimum && slot->signal->minimum) {
        // copy range from signal
        slot->minimum = malloc(mapper_signal_vector_bytes(slot->signal));
        memcpy(slot->minimum, slot->signal->minimum,
               mapper_signal_vector_bytes(slot->signal));
        updated++;
    }

    return updated;
}

// if 'override' flag is not set, only remote properties can be set
int mapper_map_set_from_message(mapper_map map, mapper_message msg, int override)
{
    int i, j, updated = 0;
    mapper_message_atom atom;

    if (!msg) {
        if (map->local->status < MAPPER_READY) {
            // check if mapping is now "ready"
            mapper_map_check_status(map);
        }
        return 0;
    }

    updated += mapper_update_int64_if_arg((int64_t*)&map->id, msg, AT_ID);

    // set src slot ids if specified
    atom = mapper_message_param(msg, AT_SLOT);
    if (atom && atom->types[0] == 'i' && atom->length == map->num_sources) {
        for (i = 0; i < map->num_sources; i++) {
            map->sources[i].id = atom->values[i]->i;
        }
    }

    // set destination slot properties
    if (override || !map->destination.local) {
        set_slot_from_message(&map->destination, msg, DST_SLOT_PARAM);
    }

    // set source slot properties
    for (i = 0; i < map->num_sources; i++) {
        if (map->sources[i].local && !override)
            continue;
        set_slot_from_message(&map->sources[i], msg,
                              SRC_SLOT_PARAM(map->sources[i].id));
    }

    /* muting */
    updated += mapper_update_bool_if_arg(&map->muted, msg, AT_MUTE);

    /* processing location. */
    atom = mapper_message_param(msg, AT_PROCESS);
    if (atom && (atom->length == 1) && is_string_type(atom->types[0])) {
        int at_source = (strcmp(&(atom->values[0])->s, "source")==0);
        if (at_source && map->local && !map->local->one_source) {
            /* Processing must take place at destination if map
             * includes source signals from different devices. */
            at_source = 0;
        }
        if (!at_source != (map->process_location != MAPPER_SOURCE)) {
            map->process_location = at_source ? MAPPER_SOURCE : MAPPER_DESTINATION;
            updated++;
        }
    }

    /* expression */
    atom = mapper_message_param(msg, AT_EXPRESSION);
    if (atom && (atom->length == 1) && is_string_type(atom->types[0])) {
        const char *expr = &atom->values[0]->s;
        if (map->local && (map->local->status & MAPPER_LENGTH_KNOWN)
            && (map->local->status & MAPPER_TYPE_KNOWN)) {
            if (strstr(expr, "y{-")) {
                map->process_location = MAPPER_DESTINATION;
            }
            int should_compile = 0;
            if (map->process_location == MAPPER_DESTINATION) {
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
                if (!replace_expression_string(map, expr))
                    reallocate_map_histories(map);
                else {
                    if (!map->expression)
                        map->expression = strdup(expr);
                    else if (strcmp(map->expression, expr)) {
                        map->expression = realloc(map->expression, strlen(expr)+1);
                        memcpy(map->expression, expr, strlen(expr)+1);
                    }
                }
            }
        }
        else if (!map->expression) {
            map->expression = strdup(expr);
            updated++;
        }
        else if (strcmp(map->expression, expr)) {
            map->expression = realloc(map->expression, strlen(expr)+1);
            memcpy(map->expression, expr, strlen(expr)+1);
        }
    }

    /* scopes */
    atom = mapper_message_param(msg, AT_SCOPE);
    if (atom && is_string_type(atom->types[0])) {
        updated += mapper_map_update_scope(&map->scope, atom);
    }
    atom = mapper_message_param(msg, AT_SCOPE | PARAM_ADD);
    if (atom && is_string_type(atom->types[0])) {
        for (i = 0; i < atom->length; i++)
            updated += (1 - add_scope_internal(&map->scope,
                                               &(atom->values[i])->s));
    }
    atom = mapper_message_param(msg, AT_SCOPE | PARAM_REMOVE);
    if (atom && is_string_type(atom->types[0])) {
        for (i = 0; i < atom->length; i++) {
            // find scope
            for (j = 0; j < map->scope.size; j++) {
                if (strcmp(map->scope.names[j], &(atom->values[i])->s)==0) {
                    remove_scope_internal(&map->scope, j);
                    updated++;
                    continue;
                }
            }
        }
    }

    /* extra properties */
    updated += mapper_message_add_or_update_extra_params(map->extra, msg);

    /* mode */
    atom = mapper_message_param(msg, AT_MODE);
    if (atom && (atom->length == 1) && is_string_type(atom->types[0])) {
        int mode = mapper_mode_from_string(&(atom->values[0])->s);
        if (mode != map->mode) {
            map->mode = mode;
            updated++;
        }
    }

    if (map->local) {
        if (map->local->status < MAPPER_READY) {
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

    // Reallocate source histories
    for (i = 0; i < map->num_sources; i++) {
        slot = &map->sources[i];
        islot = slot->local;

        // If there is no expression, then no memory needs to be reallocated.
        if (!map->local->expr)
            continue;

        history_size = mapper_expr_input_history_size(map->local->expr, i);
        if (history_size > islot->history_size) {
            size_t sample_size = mapper_type_size(slot->type) * slot->length;;
            for (j = 0; j < slot->num_instances; j++) {
                mhist_realloc(&islot->history[j], history_size, sample_size, 1);
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
                    map->local->expr_vars[i][j].value = 0;
                    map->local->expr_vars[i][j].timetag = 0;
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

    history_size = mapper_expr_output_history_size(map->local->expr);
    slot = &map->destination;
    islot = slot->local;

    // If there is no expression, then no memory needs to be reallocated.
    if (map->local->expr) {
        // reallocate output histories
        if (history_size > islot->history_size) {
            int sample_size = mapper_type_size(slot->type) * slot->length;
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
                    map->local->expr_vars[i][j].value = 0;
                    map->local->expr_vars[i][j].timetag = 0;
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

inline static void message_add_bool(lo_message m, int value) {
    if (value)
        lo_message_add_true(m);
    else
        lo_message_add_false(m);
}

static void add_slot_props_to_message(lo_message msg, mapper_slot slot,
                                      int is_dest, char **key_ptr, int *size)
{
    int len;
    char prefix[16];
    if (is_dest)
        snprintf(prefix, 16, "@dst");
    else
        snprintf(prefix, 16, "@src.%d", slot->id);

    /* slot */
    if (is_dest) {
        len = snprintf(*key_ptr, *size, "%s%s", prefix,
                       mapper_param_string(AT_SLOT));
        if (len < 0 && len > *size)
            return;
        lo_message_add_string(msg, *key_ptr);
        lo_message_add_int32(msg, slot->id);
    }

    /* type */
    if (slot->type) {
        len = snprintf(*key_ptr, *size, "%s%s", prefix,
                       mapper_param_string(AT_TYPE));
        if (len < 0 && len > *size)
            return;
        lo_message_add_string(msg, *key_ptr);
        lo_message_add_char(msg, slot->type);
        *key_ptr += len + 1;
        *size -= len + 1;
    }

    /* length */
    if (slot->length) {
        len = snprintf(*key_ptr, *size, "%s%s", prefix,
                       mapper_param_string(AT_LENGTH));
        if (len < 0 && len > *size)
            return;
        lo_message_add_string(msg, *key_ptr);
        lo_message_add_int32(msg, slot->length);
        *key_ptr += len + 1;
        *size -= len + 1;
    }

    /* maximum */
    if (slot->maximum || slot->signal->maximum) {
        len = snprintf(*key_ptr, *size, "%s%s", prefix,
                       mapper_param_string(AT_MAX));
        if (len < 0 && len > *size)
            return;
        lo_message_add_string(msg, *key_ptr);
        mapper_message_add_typed_value(msg, slot->length, slot->type,
                                       slot->maximum ?: slot->signal->maximum);
        *key_ptr += len + 1;
        *size -= len + 1;
    }

    /* minimum */
    if (slot->minimum || slot->signal->minimum) {
        len = snprintf(*key_ptr, *size, "%s%s", prefix,
                       mapper_param_string(AT_MIN));
        if (len < 0 && len > *size)
            return;
        lo_message_add_string(msg, *key_ptr);
        mapper_message_add_typed_value(msg, slot->length, slot->type,
                                       slot->minimum ?: slot->signal->minimum);
        *key_ptr += len + 1;
        *size -= len + 1;
    }

    /* boundary actions */
    if (slot->bound_max) {
        len = snprintf(*key_ptr, *size, "%s%s", prefix,
                       mapper_param_string(AT_BOUND_MAX));
        if (len < 0 && len > *size)
            return;
        lo_message_add_string(msg, *key_ptr);
        lo_message_add_string(msg, mapper_boundary_action_string(slot->bound_max));
        *key_ptr += len + 1;
        *size -= len + 1;
    }
    if (slot->bound_min) {
        len = snprintf(*key_ptr, *size, "%s%s", prefix,
                       mapper_param_string(AT_BOUND_MIN));
        if (len < 0 && len > *size)
            return;
        lo_message_add_string(msg, *key_ptr);
        lo_message_add_string(msg, mapper_boundary_action_string(slot->bound_min));
        *key_ptr += len + 1;
        *size -= len + 1;
    }

    // Calibrating
    len = snprintf(*key_ptr, *size, "%s%s", prefix,
                   mapper_param_string(AT_CALIBRATING));
    if (len < 0 && len > *size)
        return;
    lo_message_add_string(msg, *key_ptr);
    message_add_bool(msg, slot->calibrating);
    *key_ptr += len + 1;
    *size -= len + 1;

    // Sends as Instance
    len = snprintf(*key_ptr, *size, "%s%s", prefix,
                   mapper_param_string(AT_SENDS_AS_INSTANCE));
    if (len < 0 && len > *size)
        return;
    lo_message_add_string(msg, *key_ptr);
    message_add_bool(msg, slot->sends_as_instance);
    *key_ptr += len + 1;
    *size -= len + 1;

    // Causes update
    len = snprintf(*key_ptr, *size, "%s%s", prefix,
                   mapper_param_string(AT_CAUSES_UPDATE));
    if (len < 0 && len > *size)
        return;
    lo_message_add_string(msg, *key_ptr);
    message_add_bool(msg, slot->causes_update);
    *key_ptr += len + 1;
    *size -= len + 1;
}

/* If the "slot_index" argument is >= 0, we can assume this message will be sent
 * to a peer device rather than an administrator. */
const char *mapper_map_prepare_message(mapper_map map, lo_message msg, int slot)
{
    int i, size = 512;
    char *keys = malloc(size * sizeof(char));
    char *key_ptr = keys;
    mapper_link link;

    // Mapping id
    lo_message_add_string(msg, mapper_param_string(AT_ID));
    lo_message_add_int64(msg, *((int64_t*)&map->id));

    // Mapping mode
    lo_message_add_string(msg, mapper_param_string(AT_MODE));
    lo_message_add_string(msg, mapper_mode_string(map->mode));

    // Processing location
    lo_message_add_string(msg, mapper_param_string(AT_PROCESS));
    if (map->process_location == MAPPER_SOURCE)
        lo_message_add_string(msg, "source");
    else
        lo_message_add_string(msg, "destination");

    // Expression string
    if (map->expression) {
        lo_message_add_string(msg, mapper_param_string(AT_EXPRESSION));
        lo_message_add_string(msg, map->expression);
    }

    // Muting
    lo_message_add_string(msg, mapper_param_string(AT_MUTE));
    message_add_bool(msg, map->muted);

    // Slot
    if (map->destination.direction == MAPPER_INCOMING
        && map->local->status < MAPPER_READY) {
        lo_message_add_string(msg, mapper_param_string(AT_SLOT));
        i = (slot >= 0) ? slot : 0;
        link = map->sources[i].local->link;
        for (; i < map->num_sources; i++) {
            if (slot >= 0 && link != map->sources[i].local->link)
                break;
            lo_message_add_int32(msg, map->sources[i].id);
        }
    }

    /* source properties */
    i = (slot >= 0) ? slot : 0;
    link = map->sources[i].local->link;
    for (; i < map->num_sources; i++) {
        if (slot >= 0 && link != map->sources[i].local->link)
            break;
        add_slot_props_to_message(msg, &map->sources[i], 0, &key_ptr, &size);
    }

    /* destination properties */
    add_slot_props_to_message(msg, &map->destination, 1, &key_ptr, &size);

    // Mapping scopes
    lo_message_add_string(msg, mapper_param_string(AT_SCOPE));
    if (map->scope.size) {
        for (i = 0; i < map->scope.size; i++)
            lo_message_add_string(msg, map->scope.names[i]);
    }
    else
        lo_message_add_string(msg, "none");

    // "Extra" properties
    mapper_message_add_value_table(msg, map->extra);

    return keys;
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

void mapper_slot_pp(mapper_slot slot)
{
    printf("%s/%s", slot->signal->device->name, slot->signal->name);
    int i = 0;
    const char *key;
    char type;
    const void *val;
    int length;
    while(!mapper_slot_property_index(slot, i++, &key, &length, &type, &val))
    {
        die_unless(val!=0, "returned zero value\n");

        // already printed these
        if (strcmp(key, "device_name")==0 || strcmp(key, "signal_name")==0)
            continue;

        die_unless(val!=0, "returned zero value\n");
        if (length) {
            printf(", %s=", key);
            if (strncmp(key, "bound", 5)==0)
                printf("%s", mapper_boundary_action_strings[*((int*)val)]
                       ?: "undefined");
            else
                mapper_property_pp(length, type, val);
        }
    }
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
    while(!mapper_map_property_index(map, i++, &key, &length, &type, &val))
    {
        die_unless(val!=0, "returned zero value\n");

        if (length) {
            printf("%s=", key);
            if (strcmp(key, "mode")==0)
                printf("%s", mapper_mode_strings[*((int*)val)] ?: "undefined");
            else if (strcmp(key, "process_at")==0) {
                switch (*((int*)val)) {
                    case MAPPER_SOURCE:
                        printf("source");
                        break;
                    case MAPPER_DESTINATION:
                        printf("destination");
                        break;
                    default:
                        printf("undefined");
                        break;
                }
            }
            else
                mapper_property_pp(length, type, val);
            printf(", ");
        }
    }
    printf("\b\b\n");
}
