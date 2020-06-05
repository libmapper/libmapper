#include <string.h>
#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <stddef.h>
#include <limits.h>

#include "mpr_internal.h"
#include "types_internal.h"
#include <mpr/mpr.h>

static int slot_mask(mpr_slot slot)
{
    return slot == slot->map->dst ? DST_SLOT_PROP : SRC_SLOT_PROP(slot->obj.id);
}

void mpr_slot_init(mpr_slot slot)
{
    int local = slot->loc && slot->loc->rsig;
    mpr_tbl t = slot->obj.props.synced = mpr_tbl_new();
    slot->obj.props.staged = mpr_tbl_new();
    int mask = slot_mask(slot);

    mpr_tbl_link(t, MPR_PROP_NUM_INST | mask, 1, MPR_INT32, &slot->num_inst,
                 local ? NON_MODIFIABLE : MODIFIABLE);
    mpr_tbl_link(t, MPR_PROP_SIG | mask, 1, MPR_SIG, &slot->sig,
                 NON_MODIFIABLE | INDIRECT | LOCAL_ACCESS_ONLY);

    slot->obj.props.mask = mask;
}

void mpr_slot_free(mpr_slot slot)
{
    FUNC_IF(mpr_tbl_free, slot->obj.props.synced);
    FUNC_IF(mpr_tbl_free, slot->obj.props.staged);
}

int mpr_slot_set_from_msg(mpr_slot slot, mpr_msg msg, int *status)
{
    RETURN_UNLESS(slot && msg, 0);
    int i, updated = 0, mask = slot_mask(slot);
    mpr_msg_atom a;

    /* type and length belong to parent signal */
    if (!slot->loc || !slot->loc->rsig) {
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
    mpr_tbl slot_props = slot->obj.props.synced;
    for (i = 0; i < msg->num_atoms; i++) {
        a = &msg->atoms[i];
        if ((a->prop & ~0xFFFF) != mask)
            continue;
        switch (a->prop & ~mask) {
            case MPR_PROP_NUM_INST:
                // static prop if slot is associated with a local signal
                if (slot->map->loc)
                    break;
            default:
                updated += mpr_tbl_set_from_atom(slot_props, a, REMOTE_MODIFY);
                break;
        }
    }
    return updated;
}

void mpr_slot_add_props_to_msg(lo_message msg, mpr_slot slot, int is_dst,
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

    if (!staged && slot->sig->loc) {
        // include length from associated signal
        snprintf(temp+len, 16-len, "%s", mpr_prop_as_str(MPR_PROP_LEN, 0));
        lo_message_add_string(msg, temp);
        lo_message_add_int32(msg, slot->sig->len);

        // include type from associated signal
        snprintf(temp+len, 16-len, "%s", mpr_prop_as_str(MPR_PROP_TYPE, 0));
        lo_message_add_string(msg, temp);
        lo_message_add_char(msg, slot->sig->type);
    }

    mpr_tbl_add_to_msg(0, (staged ? slot->obj.props.staged
                           : slot->obj.props.synced), msg);

    // clear the staged properties
    if (staged)
        mpr_tbl_clear(slot->obj.props.staged);
}

int mpr_slot_match_full_name(mpr_slot slot, const char *full_name)
{
    RETURN_UNLESS(full_name, 1);
    full_name += (full_name[0]=='/');
    const char *sig_name = strchr(full_name+1, '/');
    RETURN_UNLESS(sig_name, 1);
    int len = sig_name - full_name;
    const char *dev_name = slot->sig->dev->name;
    return (strlen(dev_name) != len || strncmp(full_name, dev_name, len)
            || strcmp(sig_name+1, slot->sig->name)) ? 1 : 0;
}
