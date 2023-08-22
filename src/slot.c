#include <string.h>
#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <stddef.h>
#include <limits.h>

#include "device.h"
#include "expression.h"
#include "link.h"
#include "message.h"
#include "mpr_type.h"
#include "object.h"
#include "property.h"
#include "slot.h"
#include "table.h"

#include "util/mpr_debug.h"

#include <mapper/mapper.h>

#define MPR_SLOT_STRUCT_ITEMS                                                   \
    mpr_sig sig;                    /*!< Pointer to parent signal */            \
    int id;                                                                     \
    uint8_t num_inst;                                                           \
    char dir;                       /*!< `DI_INCOMING` or `DI_OUTGOING` */      \
    char causes_update;             /*!< 1 if causes update, 0 otherwise. */    \
    char is_local;

typedef struct _mpr_slot {
    MPR_SLOT_STRUCT_ITEMS
    mpr_map map;                    /*!< Pointer to parent map */
} mpr_slot_t;

typedef struct _mpr_local_slot {
    MPR_SLOT_STRUCT_ITEMS
    mpr_local_map map;              /*!< Pointer to parent map */

    /* TODO: use signal for holding memory of local slots for efficiency */
    mpr_value_t val;                /*!< Value histories for each signal instance. */
    mpr_link link;
} mpr_local_slot_t;

mpr_slot mpr_slot_new(mpr_map map, mpr_sig sig, mpr_dir dir,
                      unsigned char is_local, unsigned char is_src)
{
    size_t size = is_local ? sizeof(struct _mpr_local_slot) : sizeof(struct _mpr_slot);
    int sig_is_local = mpr_obj_get_is_local((mpr_obj)sig);
    int num_inst= mpr_sig_get_num_inst_internal(sig);
    mpr_slot slot = (mpr_slot)calloc(1, size);
    slot->map = map;
    slot->sig = sig;
    slot->is_local = is_local ? 1 : 0;
    slot->num_inst = num_inst > 1 ? num_inst : 1;
    if (MPR_DIR_UNDEFINED == dir)
        slot->dir = (is_src == sig_is_local) ? MPR_DIR_OUT : MPR_DIR_IN;
    else
        slot->dir = dir;
    slot->causes_update = 1; /* default */
    return slot;
}

static int slot_mask(mpr_slot slot)
{
    return slot == mpr_map_get_dst_slot(slot->map) ? DST_SLOT_PROP : SRC_SLOT_PROP(slot->id);
}

void mpr_slot_free(mpr_slot slot)
{
    if (slot->is_local) {
        mpr_local_slot lslot = (mpr_local_slot)slot;
        mpr_value_free(&lslot->val);
        if (mpr_obj_get_is_local((mpr_obj)slot->sig))
            mpr_local_sig_remove_slot((mpr_local_sig)slot->sig, lslot, lslot->dir);
    }
    free(slot);
}

int mpr_slot_set_from_msg(mpr_slot slot, mpr_msg msg)
{
    int updated = 0, mask;
    mpr_msg_atom a;
    mpr_tbl tbl;
    RETURN_ARG_UNLESS(slot && !mpr_slot_get_sig_if_local(slot), 0);
    mask = slot_mask(slot);
    tbl = mpr_obj_get_prop_tbl((mpr_obj)slot->sig);

    a = mpr_msg_get_prop(msg, MPR_PROP_LEN | mask);
    if (a) {
        mpr_prop prop = mpr_msg_atom_get_prop(a);
        mpr_msg_atom_set_prop(a, prop * ~mask);
        if (mpr_tbl_add_record_from_msg_atom(tbl, a, MOD_REMOTE))
            ++updated;
        mpr_msg_atom_set_prop(a, prop);
    }

    a = mpr_msg_get_prop(msg, MPR_PROP_TYPE | mask);
    if (a) {
        mpr_prop prop = mpr_msg_atom_get_prop(a);
        mpr_msg_atom_set_prop(a, prop & ~mask);
        if (mpr_tbl_add_record_from_msg_atom(tbl, a, MOD_REMOTE))
            ++updated;
        mpr_msg_atom_set_prop(a, prop);
    }

    if (slot->is_local) {
        const char *str;
        int num_inst;
        if ((str = mpr_msg_get_prop_as_str(msg, MPR_PROP_DIR | mask))) {
            int dir = mpr_dir_from_str(str);
            if (dir)
                updated += mpr_tbl_add_record(tbl, PROP(DIR), NULL, 1, MPR_INT32, &dir, MOD_REMOTE);
        }
        num_inst = mpr_msg_get_prop_as_int32(msg, MPR_PROP_NUM_INST | mask);
        if (num_inst && num_inst != slot->num_inst) {
            if (mpr_local_map_get_expr(((mpr_local_slot)slot)->map))
                updated += mpr_slot_alloc_values((mpr_local_slot)slot, num_inst, 0);
            else
                updated += mpr_tbl_add_record(tbl, PROP(NUM_INST), NULL, 1, MPR_INT32,
                                              &num_inst, MOD_REMOTE);
        }
    }
    return updated;
}

void mpr_slot_add_props_to_msg(lo_message msg, mpr_slot slot, int is_dst)
{
    int len;
    char temp[32];
    if (is_dst)
        snprintf(temp, 32, "@dst");
    else if (0 == (int)slot->id)
        snprintf(temp, 32, "@src");
    else
        snprintf(temp, 32, "@src.%d", (int)slot->id);
    len = strlen(temp);

    if (mpr_obj_get_is_local((mpr_obj)slot->sig)) {
        /* include length from associated signal */
        snprintf(temp+len, 32-len, "%s", mpr_prop_as_str(MPR_PROP_LEN, 0));
        lo_message_add_string(msg, temp);
        lo_message_add_int32(msg, mpr_sig_get_len(slot->sig));

        /* include type from associated signal */
        snprintf(temp+len, 32-len, "%s", mpr_prop_as_str(MPR_PROP_TYPE, 0));
        lo_message_add_string(msg, temp);
        lo_message_add_char(msg, mpr_sig_get_type(slot->sig));

        /* include direction from associated signal */
        snprintf(temp+len, 32-len, "%s", mpr_prop_as_str(MPR_PROP_DIR, 0));
        lo_message_add_string(msg, temp);
        lo_message_add_string(msg, mpr_sig_get_dir(slot->sig) == MPR_DIR_OUT ? "output" : "input");

        /* include num_inst property */
        snprintf(temp+len, 32-len, "%s", mpr_prop_as_str(MPR_PROP_NUM_INST, 0));
        lo_message_add_string(msg, temp);
        lo_message_add_int32(msg, slot->num_inst);
    }
}

void mpr_slot_print(mpr_slot slot, int is_dst)
{
    char temp[16];
    if (is_dst)
        snprintf(temp, 16, "@dst");
    else if (0 == (int)slot->id)
        snprintf(temp, 16, "@src");
    else
        snprintf(temp, 16, "@src.%d", (int)slot->id);

    printf(", %s%s=%d", temp, mpr_prop_as_str(MPR_PROP_LEN, 0), mpr_sig_get_len(slot->sig));
    printf(", %s%s=%c", temp, mpr_prop_as_str(MPR_PROP_TYPE, 0), mpr_sig_get_type(slot->sig));
    printf(", %s%s=%d", temp, mpr_prop_as_str(MPR_PROP_NUM_INST, 0), slot->num_inst);
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
    dev_name = mpr_dev_get_name(mpr_sig_get_dev(slot->sig));
    return (strlen(dev_name) != len || strncmp(full_name, dev_name, len)
            || strcmp(sig_name+1, mpr_sig_get_name(slot->sig))) ? 1 : 0;
}

int mpr_slot_alloc_values(mpr_local_slot slot, int num_inst, int hist_size)
{
    int len = mpr_sig_get_len(slot->sig), updated = 0;
    mpr_type type = mpr_sig_get_type(slot->sig);
    RETURN_ARG_UNLESS(type && len, 0);

#ifdef DEBUG
    trace("(re)allocating value memory for slot ");
    mpr_prop_print(1, MPR_SIG, slot->sig);
    printf("\n");
#endif

    if (hist_size > 0 && hist_size != mpr_value_get_mlen(&slot->val)) {
        trace("  updating slot hist_size to %d\n", hist_size);
        updated = 1;
    }

    if (mpr_obj_get_is_local((mpr_obj)slot->sig)) {
        num_inst = mpr_sig_get_num_inst_internal(slot->sig);
    }
    if (num_inst > 0 && num_inst != slot->num_inst) {
        trace("  updating slot num_inst to %d\n", num_inst);
        slot->num_inst = num_inst;
        updated = 1;
    }

    if (updated) {
        /* reallocate memory */
        trace("  reallocating value memory with num_inst %d and hist_size %d\n",
              slot->num_inst, hist_size);
        mpr_value_realloc(&slot->val, len, type, hist_size, slot->num_inst,
                          slot == (mpr_local_slot)mpr_map_get_dst_slot((mpr_map)slot->map));
    }
    return updated;
}

void mpr_slot_remove_inst(mpr_local_slot slot, int idx)
{
    RETURN_UNLESS(slot && idx >= 0 && idx < slot->num_inst);
    /* TODO: remove slot->num_inst property */
    slot->num_inst = mpr_value_remove_inst(&slot->val, idx);
}

mpr_value mpr_slot_get_value(mpr_local_slot slot)
{
    return &slot->val;
}

int mpr_slot_set_value(mpr_local_slot slot, int inst_idx, void *value, mpr_time time)
{
    mpr_value_set_samp(&slot->val, inst_idx, value, time);
    return slot->causes_update;
}

void mpr_slot_reset_inst(mpr_local_slot slot, int inst_idx)
{
    mpr_value_reset_inst(&slot->val, inst_idx);
}

mpr_link mpr_slot_get_link(mpr_slot slot)
{
    return slot->is_local ? ((mpr_local_slot)slot)->link : NULL;
}

void mpr_local_slot_set_link(mpr_local_slot slot, mpr_link link)
{
    slot->link = link;
}

mpr_map mpr_slot_get_map(mpr_slot slot)
{
    return slot->map;
}

mpr_dir mpr_slot_get_dir(mpr_slot slot)
{
    return slot->dir;
}

void mpr_slot_set_dir(mpr_slot slot, mpr_dir dir)
{
    slot->dir = dir;
}

int mpr_slot_get_id(mpr_slot slot)
{
    return slot->id;
}

void mpr_slot_set_id(mpr_slot slot, int id)
{
    slot->id = id;
}

int mpr_slot_get_is_local(mpr_slot slot)
{
    return slot->is_local;
}

mpr_local_sig mpr_slot_get_sig_if_local(mpr_slot slot)
{
    return mpr_obj_get_is_local((mpr_obj)slot->sig) ? (mpr_local_sig)slot->sig : NULL;
}

int mpr_slot_get_num_inst(mpr_slot slot)
{
    return slot->num_inst;
}

mpr_sig mpr_slot_get_sig(mpr_slot slot)
{
    return slot->sig;
}

void mpr_slot_set_sig(mpr_local_slot slot, mpr_local_sig sig)
{
    slot->sig = (mpr_sig)sig;
}

int mpr_slot_get_causes_update(mpr_slot slot)
{
    return slot->causes_update;
}

void mpr_slot_set_causes_update(mpr_slot slot, int causes_update)
{
    slot->causes_update = causes_update;
}

int mpr_slot_get_status(mpr_local_slot slot)
{
    char status = 0;
    mpr_sig sig = slot->sig;
    if (mpr_dev_get_is_registered(mpr_sig_get_dev(slot->sig)))
        status |= MPR_SLOT_DEV_KNOWN;
    if (mpr_sig_get_len(sig) && mpr_sig_get_type(sig))
        status |= MPR_SLOT_SIG_KNOWN;
    if (mpr_obj_get_is_local((mpr_obj)slot->sig) || mpr_link_get_is_ready(slot->link))
        status |= MPR_SLOT_LINK_KNOWN;
#ifdef DEBUG
    printf("sig: ");
    mpr_prop_print(1, MPR_SIG, slot->sig);
    printf(", dev: %s, sig: %s, link: %s\n",
           status & MPR_SLOT_DEV_KNOWN ? "Y" : "N",
           status & MPR_SLOT_SIG_KNOWN ? "Y" : "N",
           status & MPR_SLOT_LINK_KNOWN ? "Y" : "N");
#endif
    return status;
}

void mpr_local_slot_send_msg(mpr_local_slot slot, lo_message msg, mpr_time time, mpr_proto proto)
{
    mpr_link_add_msg(slot->link, mpr_sig_get_path((mpr_sig)slot->sig), msg, time, proto);
}

int mpr_slot_compare_names(mpr_slot l, mpr_slot r)
{
    mpr_sig lsig = l->sig;
    mpr_sig rsig = r->sig;
    int result = strcmp(mpr_dev_get_name(mpr_sig_get_dev(lsig)),
                        mpr_dev_get_name(mpr_sig_get_dev(rsig)));
    if (0 == result)
        return strcmp(mpr_sig_get_name(lsig), mpr_sig_get_name(rsig));
    return result;
}

lo_address mpr_slot_get_addr(mpr_slot slot)
{
    lo_address addr = NULL;
    if (!mpr_obj_get_is_local((mpr_obj)slot->sig) && ((mpr_local_slot)slot)->link)
        addr = mpr_link_get_admin_addr(((mpr_local_slot)slot)->link);
    return addr;
}
