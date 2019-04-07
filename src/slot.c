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

static int slot_mask(mapper_slot slot)
{
    if (slot == &slot->map->destination)
        return DST_SLOT_PROPERTY;
    else
        return SRC_SLOT_PROPERTY(slot->id);
}

void mapper_slot_init(mapper_slot slot)
{
    int local = slot->local && slot->local->router_sig;
    slot->props = mapper_table_new();
    slot->staged_props = mapper_table_new();
    slot->bound_min = slot->bound_max = MAPPER_BOUND_NONE;
    int mask = slot_mask(slot);

    // these properties need to be added in alphabetical order
    mapper_table_link_value(slot->props, AT_BOUND_MAX | mask, 1, 'i',
                            &slot->bound_max, MODIFIABLE);

    mapper_table_link_value(slot->props, AT_BOUND_MIN | mask, 1, 'i',
                            &slot->bound_min, MODIFIABLE);

    mapper_table_link_value(slot->props, AT_CALIBRATING | mask, 1, 'b',
                            &slot->calibrating, MODIFIABLE);

    mapper_table_link_value(slot->props, AT_CAUSES_UPDATE | mask, 1, 'b',
                            &slot->causes_update, MODIFIABLE);

    mapper_table_link_value(slot->props, AT_MAX | mask, slot->signal->length,
                            slot->signal->type, &slot->maximum,
                            MODIFIABLE | INDIRECT | MUTABLE_LENGTH | MUTABLE_TYPE);

    mapper_table_link_value(slot->props, AT_MIN | mask, slot->signal->length,
                            slot->signal->type, &slot->minimum,
                            MODIFIABLE | INDIRECT | MUTABLE_LENGTH | MUTABLE_TYPE);

    mapper_table_link_value(slot->props, AT_NUM_INSTANCES | mask, 1, 'i',
                            &slot->num_instances,
                            local ? NON_MODIFIABLE : MODIFIABLE);

    mapper_table_link_value(slot->props, AT_USE_INSTANCES | mask, 1, 'b',
                            &slot->use_instances, MODIFIABLE);
}

void mapper_slot_free(mapper_slot slot)
{
    if (slot->props)
        mapper_table_free(slot->props);
    if (slot->staged_props)
        mapper_table_free(slot->staged_props);
    if (slot->minimum)
        free(slot->minimum);
    if (slot->maximum)
        free(slot->maximum);
}

int mapper_slot_index(mapper_slot slot)
{
    if (slot == &slot->map->destination)
        return 0;
    int i;
    for (i = 0; i < slot->map->num_sources; i++) {
        if (slot == slot->map->sources[i])
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

int mapper_slot_num_instances(mapper_slot slot)
{
    return slot->num_instances;
}

int mapper_slot_use_instances(mapper_slot slot)
{
    return slot->use_instances;
}

void mapper_slot_maximum(mapper_slot slot, int *length, char *type, void **value)
{
    if (length)
        *length = slot->signal->length;
    if (type)
        *type = slot->signal->type;
    if (value)
        *value = slot->maximum;
}

void mapper_slot_minimum(mapper_slot slot, int *length, char *type, void **value)
{
    if (length)
        *length = slot->signal->length;
    if (type)
        *type = slot->signal->type;
    if (value)
        *value = slot->minimum;
}

int mapper_slot_num_properties(mapper_slot slot) {
    return mapper_table_num_records(slot->props);
}

int mapper_slot_property(mapper_slot slot, const char *name, int *length,
                         char *type, const void **value)
{
    return mapper_table_property(slot->props, name, length, type, value);
}

int mapper_slot_property_index(mapper_slot slot, unsigned int index,
                               const char **name, int *length, char *type,
                               const void **value)
{
    return mapper_table_property_index(slot->props, index, name, length, type,
                                       value);
}

void mapper_slot_set_bound_max(mapper_slot slot, mapper_boundary_action a)
{
    int index = AT_BOUND_MAX | slot_mask(slot);
    mapper_table_set_record(slot->staged_props, index, NULL, 1, 'i', &a,
                            REMOTE_MODIFY);
}

void mapper_slot_set_bound_min(mapper_slot slot, mapper_boundary_action a)
{
    int index = AT_BOUND_MIN | slot_mask(slot);
    mapper_table_set_record(slot->staged_props, index, NULL, 1, 'i', &a,
                            REMOTE_MODIFY);
}

void mapper_slot_set_calibrating(mapper_slot slot, int calibrating)
{
    int index = AT_CALIBRATING | slot_mask(slot);
    mapper_table_set_record(slot->staged_props, index, NULL, 1, 'b',
                            &calibrating, REMOTE_MODIFY);
}

void mapper_slot_set_causes_update(mapper_slot slot, int causes_update)
{
    int index = AT_CAUSES_UPDATE | slot_mask(slot);
    mapper_table_set_record(slot->staged_props, index, NULL, 1, 'b',
                            &causes_update, REMOTE_MODIFY);
}

void mapper_slot_set_use_instances(mapper_slot slot, int use_instances)
{
    int index = AT_USE_INSTANCES | slot_mask(slot);
    mapper_table_set_record(slot->staged_props, index, NULL, 1, 'b',
                            &use_instances, REMOTE_MODIFY);
}

void mapper_slot_set_maximum(mapper_slot slot, int length, char type,
                             const void *value)
{
    int index = AT_MAX| slot_mask(slot);
    mapper_table_set_record(slot->staged_props, index, NULL, length, type,
                            value, REMOTE_MODIFY);
}

void mapper_slot_set_minimum(mapper_slot slot, int length, char type,
                             const void *value)
{
    int index = AT_MIN | slot_mask(slot);
    mapper_table_set_record(slot->staged_props, index, NULL, length, type,
                            value, REMOTE_MODIFY);
}

int mapper_slot_set_property(mapper_slot slot, const char *name, int length,
                             char type, const void *value, int publish)
{
    mapper_property_t prop = mapper_property_from_string(name);
    if (prop == AT_BOUND_MAX || prop == AT_BOUND_MIN) {
        if (type != 'i' || length != 1 || !value)
            return 0;
        int bound = *(int*)value;
        if (bound < MAPPER_BOUND_UNDEFINED || bound >= NUM_MAPPER_BOUNDARY_ACTIONS)
            return 0;
    }
    prop = prop | slot_mask(slot);
    int flags = REMOTE_MODIFY | (publish ? 0 : LOCAL_ACCESS_ONLY);
    return mapper_table_set_record(slot->staged_props, prop, name, length, type,
                                   value, flags);
}

int mapper_slot_remove_property(mapper_slot slot, const char *name)
{
    if (!slot)
        return 0;
    mapper_property_t prop = mapper_property_from_string(name);
    prop = prop | slot_mask(slot) | PROPERTY_REMOVE;
    return mapper_table_set_record(slot->staged_props, prop, name, 0, 0, 0,
                                   REMOTE_MODIFY);
}

void mapper_slot_clear_staged_properties(mapper_slot slot) {
    if (slot)
        mapper_table_clear(slot->staged_props);
}

void mapper_slot_upgrade_extrema_memory(mapper_slot slot)
{
    int len;
    mapper_table_record_t *rec;
    rec = mapper_table_record(slot->props, AT_MIN, NULL);
    if (rec && rec->value && *rec->value) {
        void *old_mem = *rec->value;
        void *new_mem = calloc(1, slot->signal->length
                               * mapper_type_size(slot->signal->type));
        len = (rec->length < slot->signal->length
               ? rec->length : slot->signal->length);
        set_coerced_value(new_mem, old_mem, len, slot->signal->type, rec->type);
        mapper_table_set_record(slot->props, AT_MIN, NULL, slot->signal->length,
                                slot->signal->type, new_mem, REMOTE_MODIFY);
        if (new_mem)
            free(new_mem);
    }
    rec = mapper_table_record(slot->props, AT_MAX, NULL);
    if (rec && rec->value && *rec->value) {
        void *old_mem = *rec->value;
        void *new_mem = calloc(1, slot->signal->length
                               * mapper_type_size(slot->signal->type));
        len = (rec->length < slot->signal->length
               ? rec->length : slot->signal->length);
        set_coerced_value(new_mem, old_mem, len, slot->signal->type, rec->type);
        mapper_table_set_record(slot->props, AT_MAX, NULL, slot->signal->length,
                                slot->signal->type, new_mem, REMOTE_MODIFY);
        if (new_mem)
            free(new_mem);
    }
}

int mapper_slot_set_from_message(mapper_slot slot, mapper_message msg,
                                 int *status)
{
    int i, updated = 0, mask = slot_mask(slot);
    mapper_message_atom atom;

    if (!slot || !msg)
        return 0;

    /* type and length belong to parent signal */
    if (!slot->local || !slot->local->router_sig) {
        atom = mapper_message_property(msg, AT_LENGTH | mask);
        if (atom) {
            int index = atom->index;
            atom->index &= ~mask;
            if (mapper_table_set_record_from_atom(slot->signal->props, atom,
                                                  REMOTE_MODIFY))
                ++updated;
            atom->index = index;
        }
        atom = mapper_message_property(msg, AT_TYPE | mask);
        if (atom) {
            int index = atom->index;
            atom->index &= ~mask;
            if (mapper_table_set_record_from_atom(slot->signal->props, atom,
                                                  REMOTE_MODIFY))
                ++updated;
            atom->index = index;
        }
        if (updated)
            mapper_slot_upgrade_extrema_memory(slot);
    }
    
    for (i = 0; i < msg->num_atoms; i++) {
        atom = &msg->atoms[i];
        if ((atom->index & ~0xFF) != mask)
            continue;
        switch (atom->index & ~mask) {
            case AT_BOUND_MAX: {
                mapper_boundary_action bound;
                bound = mapper_boundary_action_from_string(&(atom->values[0])->s);
                updated += mapper_table_set_record(slot->props, atom->index,
                                                   NULL, 1, 'i', &bound,
                                                   REMOTE_MODIFY);
                break;
            }
            case AT_BOUND_MIN: {
                mapper_boundary_action bound;
                bound = mapper_boundary_action_from_string(&(atom->values[0])->s);
                updated += mapper_table_set_record(slot->props, atom->index,
                                                   NULL, 1, 'i', &bound,
                                                   REMOTE_MODIFY);
                break;
            }
            case AT_MAX:
            case AT_MIN:
                if (atom->types[0] == 'N') {
                    updated += mapper_table_remove_record(slot->props,
                                                          atom->index & ~mask,
                                                          NULL, REMOTE_MODIFY);
                    break;
                }
                if (check_signal_type(atom->types[0]))
                    break;
                updated += mapper_table_set_record_from_atom(slot->props, atom,
                                                             REMOTE_MODIFY);
                if (!slot->local || !slot->local->router_sig) {
                    if (!slot->signal->length) {
                        updated += mapper_table_set_record(slot->signal->props,
                                                           AT_LENGTH, NULL, 1,
                                                           'i', &atom->length,
                                                           REMOTE_MODIFY);
                    }
                    if (!slot->signal->type) {
                        updated += mapper_table_set_record(slot->signal->props,
                                                           AT_TYPE, NULL, 1,
                                                           'c', &atom->types[0],
                                                           REMOTE_MODIFY);
                    }
                }
                else if (!slot->signal->length || !slot->signal->type)
                    break;
                if ((atom->length != slot->signal->length)
                    || atom->types[0] != slot->signal->type)
                    mapper_slot_upgrade_extrema_memory(slot);
                break;
            case AT_NUM_INSTANCES:
                // static prop is slot is associated with a local signal
                if (slot->map->local)
                    break;
            default:
                updated += mapper_table_set_record_from_atom(slot->props, atom,
                                                             REMOTE_MODIFY);
                break;
        }
    }
    return updated;
}

void mapper_slot_add_props_to_message(lo_message msg, mapper_slot slot,
                                      int is_dest, int staged)
{
    char temp[16];
    if (is_dest)
        snprintf(temp, 16, "@dst");
    else if (slot->id == 0)
        snprintf(temp, 16, "@src");
    else
        snprintf(temp, 16, "@src.%d", slot->id);
    int len = strlen(temp);

    if (!staged && slot->signal->local) {
        // include length from associated signal
        snprintf(temp+len, 16-len, "%s",
                 mapper_property_protocol_string(AT_LENGTH));
        lo_message_add_string(msg, temp);
        lo_message_add_int32(msg, slot->signal->length);

        // include type from associated signal
        snprintf(temp+len, 16-len, "%s",
                 mapper_property_protocol_string(AT_TYPE));
        lo_message_add_string(msg, temp);
        lo_message_add_char(msg, slot->signal->type);
    }

    mapper_table_add_to_message(0, staged ? slot->staged_props : slot->props,
                                msg);

    if (staged) {
        // clear the staged properties
        mapper_table_clear(slot->staged_props);
    }
}

void mapper_slot_print(mapper_slot slot)
{
    printf("%s/%s", slot->signal->device->name, slot->signal->name);
    int i = 0;
    const char *key;
    char type;
    const void *val;
    int length;
    while (!mapper_slot_property_index(slot, i++, &key, &length, &type, &val)) {
        die_unless(val!=0, "returned zero value\n");

        // already printed these
        if (strcmp(key, "device_name")==0 || strcmp(key, "signal_name")==0)
            continue;

        if (length) {
            printf(", %s=", key);
            if (strncmp(key, "bound", 5)==0) {
                int bound = *(int*)val;
                if (bound > 0 && bound < NUM_MAPPER_BOUNDARY_ACTIONS)
                    printf("%s", mapper_boundary_action_strings[bound]);
                else
                    printf("undefined");
            }
            else
                mapper_property_print(length, type, val);
        }
    }
}

int mapper_slot_match_full_name(mapper_slot slot, const char *full_name)
{
    if (!full_name)
        return 1;
    full_name += (full_name[0]=='/');
    const char *sig_name = strchr(full_name+1, '/');
    if (!sig_name)
        return 1;
    int len = sig_name - full_name;

    const char *slot_devname = slot->signal->device->name;

    // first compare device name
    if (strlen(slot_devname) != len || strncmp(full_name, slot_devname, len))
        return 1;

    return strcmp(sig_name+1, slot->signal->name) ? 1 : 0;
}
