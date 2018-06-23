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
    if (slot == slot->map->dst)
        return DST_SLOT_PROP;
    else
        return SRC_SLOT_PROP(slot->obj.id);
}

void mapper_slot_init(mapper_slot slot)
{
//    slot->obj.type = MAPPER_OBJ_SLOT;
    int local = slot->local && slot->local->rsig;
    mapper_table tab = slot->obj.props.synced = mapper_table_new();
    slot->obj.props.staged = mapper_table_new();
    int mask = slot_mask(slot);

    // these properties need to be added in alphabetical order
    mapper_table_link(tab, MAPPER_PROP_CALIB | mask, 1, MAPPER_BOOL,
                      &slot->calib, MODIFIABLE);

    mapper_table_link(tab, MAPPER_PROP_MAX | mask, slot->sig->len,
                      slot->sig->type, &slot->max, MODIFIABLE | INDIRECT);

    mapper_table_link(tab, MAPPER_PROP_MIN | mask, slot->sig->len,
                      slot->sig->type, &slot->min, MODIFIABLE | INDIRECT);

    mapper_table_link(tab, MAPPER_PROP_NUM_INSTANCES | mask, 1, MAPPER_INT32,
                      &slot->num_inst, local ? NON_MODIFIABLE : MODIFIABLE);

    mapper_table_link(tab, MAPPER_PROP_SIGNAL | mask, 1, MAPPER_SIGNAL, &slot->sig,
                      NON_MODIFIABLE | INDIRECT | LOCAL_ACCESS_ONLY);

    mapper_table_link(tab, MAPPER_PROP_USE_INSTANCES | mask, 1, MAPPER_BOOL,
                      &slot->use_inst, MODIFIABLE);

    slot->obj.props.mask = mask;
}

void mapper_slot_free(mapper_slot slot)
{
    if (slot->obj.props.synced)
        mapper_table_free(slot->obj.props.synced);
    if (slot->obj.props.staged)
        mapper_table_free(slot->obj.props.staged);
    if (slot->min)
        free(slot->min);
    if (slot->max)
        free(slot->max);
}

int mapper_slot_get_index(mapper_slot slot)
{
    if (slot == slot->map->dst)
        return 0;
    int i;
    for (i = 0; i < slot->map->num_src; i++) {
        if (slot == slot->map->src[i])
            return i;
    }
    return -1;
}

mapper_signal mapper_slot_get_signal(mapper_slot slot)
{
    return slot->sig;
}

void mapper_slot_upgrade_extrema_memory(mapper_slot slot)
{
    int len;
    mapper_table_record_t *rec;
    rec = mapper_table_record(slot->obj.props.synced, MAPPER_PROP_MIN, NULL);
    if (rec && rec->val && *rec->val) {
        void *new_mem = calloc(1, (mapper_type_size(slot->sig->type)
                                   * slot->sig->len));
        len = (rec->len < slot->sig->len ? rec->len : slot->sig->len);
        set_coerced_val(len, rec->type, rec->val, len, slot->sig->type, new_mem);
        mapper_table_set_record(slot->obj.props.synced, MAPPER_PROP_MIN, NULL,
                                slot->sig->len, slot->sig->type,
                                new_mem, REMOTE_MODIFY);
        if (new_mem)
            free(new_mem);
    }
    rec = mapper_table_record(slot->obj.props.synced, MAPPER_PROP_MAX, NULL);
    if (rec && rec->val && *rec->val) {
        void *new_mem = calloc(1, (mapper_type_size(slot->sig->type)
                                   * slot->sig->len));
        len = (rec->len < slot->sig->len ? rec->len : slot->sig->len);
        set_coerced_val(len, rec->type, rec->val, len, slot->sig->type, new_mem);
        mapper_table_set_record(slot->obj.props.synced, MAPPER_PROP_MAX, NULL,
                                slot->sig->len, slot->sig->type,
                                new_mem, REMOTE_MODIFY);
        if (new_mem)
            free(new_mem);
    }
}

int mapper_slot_set_from_msg(mapper_slot slot, mapper_msg msg, int *status)
{
    int i, updated = 0, mask = slot_mask(slot);
    mapper_msg_atom atom;

    if (!slot || !msg)
        return 0;

    /* type and length belong to parent signal */
    if (!slot->local || !slot->local->rsig) {
        atom = mapper_msg_prop(msg, MAPPER_PROP_LENGTH | mask);
        if (atom) {
            mapper_property prop = atom->prop;
            atom->prop &= ~mask;
            if (mapper_table_set_record_from_atom(slot->sig->obj.props.synced,
                                                  atom, REMOTE_MODIFY))
                ++updated;
            atom->prop = prop;
        }
        atom = mapper_msg_prop(msg, MAPPER_PROP_TYPE | mask);
        if (atom) {
            mapper_property prop = atom->prop;
            atom->prop &= ~mask;
            if (mapper_table_set_record_from_atom(slot->sig->obj.props.synced,
                                                  atom, REMOTE_MODIFY))
                ++updated;
            atom->prop = prop;
        }
        if (updated)
            mapper_slot_upgrade_extrema_memory(slot);
    }
    
    for (i = 0; i < msg->num_atoms; i++) {
        atom = &msg->atoms[i];
        if (!(atom->prop & mask))
            continue;
        switch (atom->prop & ~mask) {
            case MAPPER_PROP_MAX:
            case MAPPER_PROP_MIN:
                if (atom->types[0] == MAPPER_NULL) {
                    updated += mapper_table_remove_record(slot->obj.props.synced,
                                                          atom->prop & ~mask,
                                                          NULL, REMOTE_MODIFY);
                    break;
                }
                if (check_signal_type(atom->types[0]))
                    break;
                updated += mapper_table_set_record_from_atom(slot->obj.props.synced,
                                                             atom, REMOTE_MODIFY);
                if (!slot->local || !slot->local->rsig) {
                    if (!slot->sig->len) {
                        updated += mapper_table_set_record(slot->sig->obj.props.synced,
                                                           MAPPER_PROP_LENGTH,
                                                           NULL, 1, MAPPER_INT32,
                                                           &atom->len,
                                                           REMOTE_MODIFY);
                    }
                    if (!slot->sig->type) {
                        updated += mapper_table_set_record(slot->sig->obj.props.synced,
                                                           MAPPER_PROP_TYPE,
                                                           NULL, 1, MAPPER_CHAR,
                                                           &atom->types[0],
                                                           REMOTE_MODIFY);
                    }
                }
                else if (!slot->sig->len || !slot->sig->type)
                    break;
                if ((atom->len != slot->sig->len)
                    || atom->types[0] != slot->sig->type)
                    mapper_slot_upgrade_extrema_memory(slot);
                break;
            case MAPPER_PROP_NUM_INSTANCES:
                // static prop is slot is associated with a local signal
                if (slot->map->local)
                    break;
            default:
                updated += mapper_table_set_record_from_atom(slot->obj.props.synced,
                                                             atom, REMOTE_MODIFY);
                break;
        }
    }
    return updated;
}

void mapper_slot_add_props_to_msg(lo_message msg, mapper_slot slot, int is_dst,
                                  int staged)
{
    char temp[16];
    if (is_dst)
        snprintf(temp, 16, "@dst");
    else if (slot->obj.id == 0)
        snprintf(temp, 16, "@src");
    else
        snprintf(temp, 16, "@src.%d", (int)slot->obj.id);
    int len = strlen(temp);

    if (!staged && slot->sig->local) {
        // include length from associated signal
        snprintf(temp+len, 16-len, "%s",
                 mapper_prop_protocol_string(MAPPER_PROP_LENGTH));
        lo_message_add_string(msg, temp);
        lo_message_add_int32(msg, slot->sig->len);

        // include type from associated signal
        snprintf(temp+len, 16-len, "%s",
                 mapper_prop_protocol_string(MAPPER_PROP_TYPE));
        lo_message_add_string(msg, temp);
        lo_message_add_char(msg, slot->sig->type);
    }

    mapper_table_add_to_msg(0, (staged ? slot->obj.props.staged
                                : slot->obj.props.synced), msg);

    if (staged) {
        // clear the staged properties
        mapper_table_clear(slot->obj.props.staged);
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

    const char *slot_devname = slot->sig->dev->name;

    // first compare device name
    if (strlen(slot_devname) != len || strncmp(full_name, slot_devname, len))
        return 1;

    return strcmp(sig_name+1, slot->sig->name) ? 1 : 0;
}
