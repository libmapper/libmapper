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

void mapper_slot_init(mapper_slot slot)
{
    int local = slot->local && slot->local->router_sig;
    slot->props = mapper_table_new();
    slot->staged_props = mapper_table_new();

    // these properties need to be added in alphabetical order
    mapper_table_link_value(slot->props, AT_BOUND_MAX, 1, 'i', &slot->bound_max,
                            MODIFIABLE);

    mapper_table_link_value(slot->props, AT_BOUND_MIN, 1, 'i', &slot->bound_min,
                            MODIFIABLE);

    mapper_table_link_value(slot->props, AT_CALIBRATING, 1, 'b',
                            &slot->calibrating, MODIFIABLE);

    mapper_table_link_value(slot->props, AT_CAUSES_UPDATE, 1, 'b',
                            &slot->causes_update, MODIFIABLE);

    mapper_table_link_value(slot->props, AT_MAX, slot->signal->length,
                            slot->signal->type, &slot->maximum,
                            MODIFIABLE | INDIRECT | MUTABLE_LENGTH | MUTABLE_TYPE);

    mapper_table_link_value(slot->props, AT_MIN, slot->signal->length,
                            slot->signal->type, &slot->minimum,
                            MODIFIABLE | INDIRECT | MUTABLE_LENGTH | MUTABLE_TYPE);

    mapper_table_link_value(slot->props, AT_NUM_INSTANCES, 1, 'i',
                            &slot->num_instances,
                            local ? NON_MODIFIABLE : MODIFIABLE);

    mapper_table_link_value(slot->props, AT_USE_AS_INSTANCE, 1, 'b',
                            &slot->use_as_instance, MODIFIABLE);
}

int mapper_slot_index(mapper_slot slot)
{
    if (slot == &slot->map->destination)
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

int mapper_slot_num_instances(mapper_slot slot)
{
    return slot->num_instances;
}

int mapper_slot_use_as_instance(mapper_slot slot)
{
    return slot->use_as_instance;
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

static int slot_prop_index(mapper_slot slot, mapper_property_t prop)
{
    int i, index = 0;
    if (slot == &slot->map->destination) {
        index = prop | DST_SLOT_PROPERTY;
    }
    else {
        // source slot - figure out which source slot we are
        if (slot->map->num_sources == 1)
            index = prop | SRC_SLOT_PROPERTY(0);
        else {
            for (i = 0; i < slot->map->num_sources; i++) {
                if (&slot->map->sources[i] == slot) {
                    index = prop | SRC_SLOT_PROPERTY(i);
                    break;
                }
            }
        }
    }
    return index;
}

void mapper_slot_set_bound_max(mapper_slot slot, mapper_boundary_action a)
{
    int index = slot_prop_index(slot, AT_BOUND_MAX);
    mapper_table_set_record(slot->staged_props, index, NULL, 1, 's',
                            mapper_boundary_action_strings[a], REMOTE_MODIFY);
}

void mapper_slot_set_bound_min(mapper_slot slot, mapper_boundary_action a)
{
    int index = slot_prop_index(slot, AT_BOUND_MIN);
    mapper_table_set_record(slot->staged_props, index, NULL, 1, 's',
                            mapper_boundary_action_strings[a], REMOTE_MODIFY);
}

void mapper_slot_set_calibrating(mapper_slot slot, int calibrating)
{
    int index = slot_prop_index(slot, AT_CALIBRATING);
    mapper_table_set_record(slot->staged_props, index, NULL, 1, 'b',
                            &calibrating, REMOTE_MODIFY);
}

void mapper_slot_set_causes_update(mapper_slot slot, int causes_update)
{
    int index = slot_prop_index(slot, AT_CAUSES_UPDATE);
    mapper_table_set_record(slot->staged_props, index, NULL, 1, 'b',
                            &causes_update, REMOTE_MODIFY);
}

void mapper_slot_set_use_as_instance(mapper_slot slot, int use_as_instance)
{
    int index = slot_prop_index(slot, AT_USE_AS_INSTANCE);
    mapper_table_set_record(slot->staged_props, index, NULL, 1, 'b',
                            &use_as_instance, REMOTE_MODIFY);
}

void mapper_slot_set_maximum(mapper_slot slot, int length, char type,
                             const void *value)
{
    int index = slot_prop_index(slot, AT_MAX);
    mapper_table_set_record(slot->staged_props, index, NULL, length, type,
                            value, REMOTE_MODIFY);
}

void mapper_slot_set_minimum(mapper_slot slot, int length, char type,
                             const void *value)
{
    int index = slot_prop_index(slot, AT_MIN);
    mapper_table_set_record(slot->staged_props, index, NULL, length, type,
                            value, REMOTE_MODIFY);
}

void mapper_slot_set_property(mapper_slot slot, const char *name, int length,
                              char type, const void *value)
{
    mapper_property_t prop = mapper_property_from_string(name);
    prop = slot_prop_index(slot, prop);
    mapper_table_set_record(slot->staged_props, prop, name, length, type, value,
                            REMOTE_MODIFY);
}

void mapper_slot_remove_property(mapper_slot slot, const char *name)
{
    mapper_property_t prop = mapper_property_from_string(name);
    prop = slot_prop_index(slot, prop) | PROPERTY_REMOVE;
    mapper_table_set_record(slot->staged_props, prop, name, 0, 0, 0,
                            REMOTE_MODIFY);
}

void mapper_slot_upgrade_extrema_memory(mapper_slot slot)
{
    int i, j;
    mapper_table_record_t *rec;
    rec = mapper_table_record(slot->props, AT_MIN, NULL);
    if (rec && rec->value && *rec->value) {
        void *old_mem = *rec->value;
        void *new_mem = calloc(1, slot->signal->length
                               * mapper_type_size(slot->signal->type));
        switch (rec->type) {
            case 'i':
                for (i = 0, j = 0; i < slot->signal->length; i++, j++) {
                    propval_set_from_lo_arg(new_mem, slot->signal->type,
                                            (lo_arg*)&((int*)old_mem)[i],
                                            'i', i);
                    if (j >= rec->length)
                        j = rec->length - 1;
                }
                break;
            case 'f':
                for (i = 0, j = 0; i < slot->signal->length; i++, j++) {
                    propval_set_from_lo_arg(new_mem, slot->signal->type,
                                            (lo_arg*)&((float*)old_mem)[i],
                                            'f', i);
                    if (j >= rec->length)
                        j = rec->length - 1;
                }
                break;
            case 'd':
                for (i = 0, j = 0; i < slot->signal->length; i++, j++) {
                    propval_set_from_lo_arg(new_mem, slot->signal->type,
                                            (lo_arg*)&((double*)old_mem)[i],
                                            'd', i);
                    if (j >= rec->length)
                        j = rec->length - 1;
                }
                break;
            default:
                break;
        }
        mapper_table_set_record(slot->props, AT_MIN, NULL, slot->signal->length,
                                slot->signal->type, new_mem, REMOTE_MODIFY);
    }
    rec = mapper_table_record(slot->props, AT_MAX, NULL);
    if (rec && rec->value && *rec->value) {
        void *old_mem = *rec->value;
        void *new_mem = calloc(1, slot->signal->length
                               * mapper_type_size(slot->signal->type));
        switch (rec->type) {
            case 'i':
                for (i = 0, j = 0; i < slot->signal->length; i++, j++) {
                    propval_set_from_lo_arg(new_mem, slot->signal->type,
                                            (lo_arg*)&((int*)old_mem)[i],
                                            'i', i);
                    if (j >= rec->length)
                        j = rec->length - 1;
                }
                break;
            case 'f':
                for (i = 0, j = 0; i < slot->signal->length; i++, j++) {
                    propval_set_from_lo_arg(new_mem, slot->signal->type,
                                            (lo_arg*)&((float*)old_mem)[i],
                                            'f', i);
                    if (j >= rec->length)
                        j = rec->length - 1;
                }
                break;
            case 'd':
                for (i = 0, j = 0; i < slot->signal->length; i++, j++) {
                    propval_set_from_lo_arg(new_mem, slot->signal->type,
                                            (lo_arg*)&((double*)old_mem)[i],
                                            'd', i);
                    if (j >= rec->length)
                        j = rec->length - 1;
                }
                break;
            default:
                break;
        }
        mapper_table_set_record(slot->props, AT_MAX, NULL, slot->signal->length,
                                slot->signal->type, new_mem, REMOTE_MODIFY);
    }
}

int mapper_slot_set_from_message(mapper_slot slot, mapper_message msg, int mask,
                                 int *status)
{
    int i, index, updated = 0;
    mapper_message_atom atom;

    if (!slot || !msg)
        return 0;

    /* type and length belong to parent signal */
    if (!slot->local || !slot->local->router_sig) {
        atom = mapper_message_property(msg, AT_LENGTH | mask);
        if (atom) {
            index = atom->index;
            atom->index &= ~mask;
            if (mapper_table_set_record_from_atom(slot->signal->props, atom,
                                                  REMOTE_MODIFY))
                ++updated;
            atom->index = index;
        }
        atom = mapper_message_property(msg, AT_TYPE | mask);
        if (atom) {
            index = atom->index;
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
        if (!(atom->index & mask))
            continue;
        index = atom->index;
        atom->index &= ~mask;
        switch (atom->index) {
            case AT_BOUND_MAX: {
                mapper_boundary_action bound;
                bound = mapper_boundary_action_from_string(&(atom->values[0])->s);
                updated += mapper_table_set_record(slot->props, AT_BOUND_MAX,
                                                   NULL, 1, 'i', &bound,
                                                   REMOTE_MODIFY);
                break;
            }
            case AT_BOUND_MIN: {
                mapper_boundary_action bound;
                bound = mapper_boundary_action_from_string(&(atom->values[0])->s);
                updated += mapper_table_set_record(slot->props, AT_BOUND_MIN,
                                                   NULL, 1, 'i', &bound,
                                                   REMOTE_MODIFY);
                break;
            }
            case AT_MAX:
            case AT_MIN:
                mapper_table_set_record_from_atom(slot->props, atom,
                                                  REMOTE_MODIFY);
                if (!slot->local || !slot->local->router_sig) {
                    if (!slot->signal->length) {
                        mapper_table_set_record(slot->signal->props, AT_LENGTH,
                                                NULL, 1, 'i', &atom->length,
                                                REMOTE_MODIFY);
                    }
                    if (!slot->signal->type) {
                        mapper_table_set_record(slot->signal->props, AT_TYPE,
                                                NULL, 1, 'c', &atom->types[0],
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
                updated = mapper_table_set_record_from_atom(slot->props, atom,
                                                            REMOTE_MODIFY);
                break;
        }
        atom->index = index;
    }
    return updated;
}

void mapper_slot_add_props_to_message(lo_message msg, mapper_slot slot,
                                      int is_dest, char **key_ptr, int *size,
                                      int flags)
{
    int i, indirect, len;
    char prefix[16];
    if (is_dest)
        snprintf(prefix, 16, "@dst");
    else if (slot->id == 0)
        snprintf(prefix, 16, "@src");
    else
        snprintf(prefix, 16, "@src.%d", slot->id);

    // use length from associated signal
    if (slot->signal->local) {
        len = snprintf(*key_ptr, *size, "%s%s", prefix,
                       mapper_protocol_string(AT_LENGTH));
        if (len < 0 || len > *size)
            return;
        lo_message_add_string(msg, *key_ptr);
        lo_message_add_int32(msg, slot->signal->length);
        *key_ptr += len + 1;
        *size -= len + 1;
    }

    // use type from associated signal
    if (slot->signal->local) {
        len = snprintf(*key_ptr, *size, "%s%s", prefix,
                       mapper_protocol_string(AT_TYPE));
        if (len < 0 && len > *size)
            return;
        lo_message_add_string(msg, *key_ptr);
        lo_message_add_char(msg, slot->signal->type);
        *key_ptr += len + 1;
        *size -= len + 1;
    }

    mapper_table_t *tab = ((flags == UPDATED_PROPS)
                           ? slot->staged_props : slot->props);
    mapper_table_record_t *rec;
    for (i = 0; i < tab->num_records; i++) {
        rec = &tab->records[i];
        indirect = (flags != UPDATED_PROPS) && (rec->flags & INDIRECT);
        if (!rec->value)
            continue;
        if (indirect && !(*rec->value))
            continue;
        switch (rec->index) {
            case AT_BOUND_MAX:
            case AT_BOUND_MIN:
                if (rec->type != 'i')
                    break;
                if (*(int*)rec->value <= 0)
                    break;
                len = snprintf(*key_ptr, *size, "%s%s", prefix,
                               mapper_protocol_string(rec->index));
                if (len < 0 && len > *size)
                    return;
                lo_message_add_string(msg, *key_ptr);
                lo_message_add_string(msg,
                                      mapper_boundary_action_string(*(int*)rec->value));
                *key_ptr += len + 1;
                *size -= len + 1;
                break;
            default:
                len = snprintf(*key_ptr, *size, "%s%s", prefix,
                               mapper_protocol_string(rec->index));
                if (len < 0 && len > *size)
                    return;
                lo_message_add_string(msg, *key_ptr);
                mapper_message_add_typed_value(msg, rec->length, rec->type,
                                               indirect ? *rec->value : rec->value);
                *key_ptr += len + 1;
                *size -= len + 1;
                break;
        }
    }
    // TODO: add signal boundary action if not overridden by slot properties
}

void mapper_slot_pp(mapper_slot slot)
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
