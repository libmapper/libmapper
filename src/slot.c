#include <string.h>
#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <stddef.h>
#include <limits.h>

#include "mapper_internal.h"
#include "types_internal.h"
#include <mapper/mapper.h>

mpr_slot mpr_slot_new(mpr_map map, mpr_sig sig, unsigned char is_local, unsigned char is_src)
{
    size_t size = is_local ? sizeof(struct _mpr_local_slot) : sizeof(struct _mpr_slot);
    mpr_slot slot = (mpr_slot)calloc(1, size);
    slot->map = map;
    slot->sig = sig;
    slot->is_local = is_local;
    slot->dir = (is_src == sig->is_local) ? MPR_DIR_OUT : MPR_DIR_IN;
    slot->causes_update = 1; /* default */
    return slot;
}

static int slot_mask(mpr_slot slot)
{
    return slot == slot->map->dst ? DST_SLOT_PROP : SRC_SLOT_PROP(slot->obj.id);
}

void mpr_slot_init(mpr_slot slot)
{
    int mask = slot_mask(slot);
    mpr_tbl t = slot->obj.props.synced = mpr_tbl_new();
    slot->obj.props.staged = mpr_tbl_new();

    if (slot->sig->is_local)
        mpr_tbl_link(t, MPR_PROP_NUM_INST | mask, 1, MPR_INT32,
                     &slot->sig->num_inst, NON_MODIFIABLE);
    mpr_tbl_link(t, MPR_PROP_SIG | mask, 1, MPR_SIG, &slot->sig,
                 NON_MODIFIABLE | INDIRECT | LOCAL_ACCESS_ONLY);

    slot->obj.props.mask = mask;
}

void mpr_slot_free(mpr_slot slot)
{
    FUNC_IF(mpr_tbl_free, slot->obj.props.synced);
    FUNC_IF(mpr_tbl_free, slot->obj.props.staged);
    free(slot);
}

void mpr_slot_free_value(mpr_local_slot slot)
{
    /* TODO: use rtr_sig for holding memory of local slots for effiency */
    mpr_value_free(&slot->val);
}

int mpr_slot_set_from_msg(mpr_slot slot, mpr_msg msg)
{
    int i, updated = 0, mask;
    mpr_msg_atom a;
    mpr_tbl slot_props;
    RETURN_ARG_UNLESS(slot && msg, 0);
    mask = slot_mask(slot);

    /* type and length belong to parent signal */
    if (!slot->is_local || !((mpr_local_slot)slot)->rsig) {
        a = mpr_msg_get_prop(msg, MPR_PROP_LEN | mask);
        if (a) {
            mpr_prop prop = a->prop;
            a->prop &= ~mask;
            if (mpr_tbl_set_from_atom(slot->sig->obj.props.synced, a, REMOTE_MODIFY))
                ++updated;
            a->prop = prop;
        }
        a = mpr_msg_get_prop(msg, MPR_PROP_TYPE | mask);
        if (a) {
            mpr_prop prop = a->prop;
            a->prop &= ~mask;
            if (mpr_tbl_set_from_atom(slot->sig->obj.props.synced, a, REMOTE_MODIFY))
                ++updated;
            a->prop = prop;
        }
    }
    slot_props = slot->obj.props.synced;
    for (i = 0; i < msg->num_atoms; i++) {
        a = &msg->atoms[i];
        if ((a->prop & ~0xFFFF) != mask)
            continue;
        switch (a->prop & ~mask) {
            case MPR_PROP_LEN:
            case MPR_PROP_TYPE:
                /* handled above */
                break;
            case MPR_PROP_NUM_INST:
                /* static prop if slot is associated with a local map */
                if (slot->map->is_local)
                    break;
            default:
                updated += mpr_tbl_set_from_atom(slot_props, a, REMOTE_MODIFY);
                break;
        }
    }
    return updated;
}

void mpr_slot_add_props_to_msg(lo_message msg, mpr_slot slot, int is_dst, int staged)
{
    int len;
    char temp[16];
    if (is_dst)
        snprintf(temp, 16, "@dst");
    else if (0 == (int)slot->obj.id)
        snprintf(temp, 16, "@src");
    else
        snprintf(temp, 16, "@src.%d", (int)slot->obj.id);
    len = strlen(temp);

    if (!staged && slot->sig->is_local) {
        /* include length from associated signal */
        snprintf(temp+len, 16-len, "%s", mpr_prop_as_str(MPR_PROP_LEN, 0));
        lo_message_add_string(msg, temp);
        lo_message_add_int32(msg, slot->sig->len);

        /* include type from associated signal */
        snprintf(temp+len, 16-len, "%s", mpr_prop_as_str(MPR_PROP_TYPE, 0));
        lo_message_add_string(msg, temp);
        lo_message_add_char(msg, slot->sig->type);
    }

    mpr_tbl_add_to_msg(0, (staged ? slot->obj.props.staged : slot->obj.props.synced), msg);

    /* clear the staged properties */
    if (staged)
        mpr_tbl_clear(slot->obj.props.staged);
}

int mpr_slot_match_full_name(mpr_slot slot, const char *full_name)
{
    int len;
    const char *sig_name, *dev_name;
    RETURN_ARG_UNLESS(full_name, 1);
    full_name += (full_name[0]=='/');
    sig_name = strchr(full_name+1, '/');
    RETURN_ARG_UNLESS(sig_name, 1);
    len = sig_name - full_name;
    dev_name = slot->sig->dev->name;
    return (strlen(dev_name) != len || strncmp(full_name, dev_name, len)
            || strcmp(sig_name+1, slot->sig->name)) ? 1 : 0;
}

void mpr_slot_alloc_values(mpr_local_slot slot, int num_inst, int hist_size)
{
    RETURN_UNLESS(num_inst && hist_size && slot->sig->type && slot->sig->len);
    if (slot->sig->is_local)
        num_inst = slot->sig->num_inst;

    /* reallocate memory */
    mpr_value_realloc(&slot->val, slot->sig->len, slot->sig->type,
                      hist_size, num_inst, slot == slot->map->dst);

    slot->num_inst = num_inst;
}

void mpr_slot_remove_inst(mpr_local_slot slot, int idx)
{
    RETURN_UNLESS(slot && idx >= 0 && idx < slot->num_inst);
    /* TODO: remove slot->num_inst property */
    slot->num_inst = mpr_value_remove_inst(&slot->val, idx);
}
