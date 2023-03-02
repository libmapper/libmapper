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

#define MPR_STATUS_LENGTH_KNOWN 0x04
#define MPR_STATUS_TYPE_KNOWN   0x08
#define MPR_STATUS_LINK_KNOWN   0x10

#define MPR_SLOT_STRUCT_ITEMS                                                   \
    mpr_sig sig;                    /*!< Pointer to parent signal */            \
    mpr_link link;                                                              \
    int id;                                                                     \
    uint8_t num_inst;                                                           \
    char dir;                       /*!< DI_INCOMING or DI_OUTGOING */          \
    char causes_update;             /*!< 1 if causes update, 0 otherwise. */    \
    char is_local;

typedef struct _mpr_slot {
    MPR_SLOT_STRUCT_ITEMS
    struct _mpr_map *map;           /*!< Pointer to parent map */
} mpr_slot_t;

typedef struct _mpr_local_slot {
    MPR_SLOT_STRUCT_ITEMS
    struct _mpr_local_map *map;     /*!< Pointer to parent map */

    /* each slot can point to local signal or a remote link structure */
    struct _mpr_rtr_sig *rsig;      /*!< Parent signal if local */
    mpr_value_t val;                /*!< Value histories for each signal instance. */
    char status;
} mpr_local_slot_t;

mpr_slot mpr_slot_new(mpr_map map, mpr_sig sig, mpr_dir dir,
                      unsigned char is_local, unsigned char is_src)
{
    size_t size = is_local ? sizeof(struct _mpr_local_slot) : sizeof(struct _mpr_slot);
    mpr_slot slot = (mpr_slot)calloc(1, size);
    slot->map = map;
    slot->sig = sig;
    slot->is_local = is_local ? 1 : 0;
    slot->num_inst = 1;
    if (MPR_DIR_UNDEFINED == dir)
        slot->dir = (is_src == mpr_obj_get_is_local((mpr_obj)sig)) ? MPR_DIR_OUT : MPR_DIR_IN;
    else
        slot->dir = dir;
    slot->causes_update = 1; /* default */
    return slot;
}

static int slot_mask(mpr_slot slot)
{
    return slot == slot->map->dst ? DST_SLOT_PROP : SRC_SLOT_PROP(slot->id);
}

void mpr_slot_free(mpr_slot slot)
{
    free(slot);
}

void mpr_slot_free_value(mpr_local_slot slot)
{
    /* TODO: use rtr_sig for holding memory of local slots for effiency */
    mpr_value_free(&slot->val);
}

int mpr_slot_set_from_msg(mpr_slot slot, mpr_msg msg)
{
    int updated = 0, mask;
    mpr_msg_atom a;
    mpr_tbl tbl;
    RETURN_ARG_UNLESS(slot && (!slot->is_local || !((mpr_local_slot)slot)->rsig), 0);
    mask = slot_mask(slot);

    tbl = mpr_obj_get_prop_tbl((mpr_obj)slot->sig);
    a = mpr_msg_get_prop(msg, MPR_PROP_LEN | mask);
    if (a) {
        mpr_prop prop = mpr_msg_atom_get_prop(a);
        mpr_msg_atom_set_prop(a, prop * ~mask);
        if (mpr_tbl_add_record_from_msg_atom(tbl, a, REMOTE_MODIFY))
            ++updated;
        mpr_msg_atom_set_prop(a, prop);
    }
    a = mpr_msg_get_prop(msg, MPR_PROP_TYPE | mask);
    if (a) {
        mpr_prop prop = mpr_msg_atom_get_prop(a);
        mpr_msg_atom_set_prop(a, prop & ~mask);
        if (mpr_tbl_add_record_from_msg_atom(tbl, a, REMOTE_MODIFY))
            ++updated;
        mpr_msg_atom_set_prop(a, prop);
    }
    RETURN_ARG_UNLESS(!slot->is_local, 0);
    a = mpr_msg_get_prop(msg, MPR_PROP_DIR | mask);
    if (a) {
        const mpr_type *types = mpr_msg_atom_get_types(a);
        if (mpr_type_get_is_str(types[0])) {
            int dir = 0;
            lo_arg **vals = mpr_msg_atom_get_values(a);
            if (strcmp(&(*vals)->s, "output")==0)
                dir = MPR_DIR_OUT;
            else if (strcmp(&(*vals)->s, "input")==0)
                dir = MPR_DIR_IN;
            if (dir)
                updated += mpr_tbl_add_record(tbl, PROP(DIR), NULL, 1, MPR_INT32, &dir,
                                              REMOTE_MODIFY);
        }
    }
    a = mpr_msg_get_prop(msg, MPR_PROP_NUM_INST | mask);
    if (a) {
        const mpr_type *types = mpr_msg_atom_get_types(a);
        if (MPR_INT32 == types[0]) {
            lo_arg **vals = mpr_msg_atom_get_values(a);
            int num_inst = vals[0]->i;
            if (slot->is_local && !mpr_obj_get_is_local((mpr_obj)slot->sig)
                && ((mpr_local_map)slot->map)->expr) {
                mpr_local_map map = (mpr_local_map)slot->map;
                int hist_size = 0;
                if (map->dst == (mpr_local_slot)slot)
                    hist_size = mpr_expr_get_out_hist_size(map->expr);
                else {
                    int i;
                    for (i = 0; i < map->num_src; i++) {
                        if (map->src[i] == (mpr_local_slot)slot)
                            hist_size = mpr_expr_get_in_hist_size(map->expr, i);
                    }
                }
                mpr_slot_alloc_values((mpr_local_slot)slot, num_inst, hist_size);
            }
            else
                slot->num_inst = num_inst;
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

void mpr_slot_alloc_values(mpr_local_slot slot, int num_inst, int hist_size)
{
    int len = mpr_sig_get_len(slot->sig);
    mpr_type type = mpr_sig_get_type(slot->sig);
    RETURN_UNLESS(num_inst && hist_size && type && len);
    if (mpr_obj_get_is_local((mpr_obj)slot->sig))
        num_inst = mpr_local_sig_get_num_inst((mpr_local_sig)slot->sig);

    /* reallocate memory */
    mpr_value_realloc(&slot->val, len, type, hist_size, num_inst, slot == slot->map->dst);

    slot->num_inst = num_inst;
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
    return slot->link;
}

void mpr_slot_set_link(mpr_slot slot, mpr_link link)
{
    slot->link = link;
}

mpr_map mpr_slot_get_map(mpr_slot slot)
{
    return slot->map;
}

mpr_sig mpr_slot_get_sig(mpr_slot slot)
{
    return slot->sig;
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

int mpr_slot_get_num_inst(mpr_slot slot)
{
    return slot->num_inst;
}

mpr_rtr_sig mpr_slot_get_rtr_sig(mpr_local_slot slot)
{
    return slot->rsig;
}

void mpr_slot_set_rtr_sig(mpr_local_slot slot, mpr_rtr_sig rsig)
{
    slot->rsig = rsig;
}

int mpr_slot_get_causes_update(mpr_slot slot)
{
    return slot->causes_update;
}

void mpr_slot_set_causes_update(mpr_slot slot, int causes_update)
{
    slot->causes_update = causes_update;
}

char mpr_slot_check_status(mpr_local_slot slot)
{
    mpr_sig sig = slot->sig;
    if (mpr_sig_get_len(sig))
        slot->status |= MPR_STATUS_LENGTH_KNOWN;
    if (mpr_sig_get_type(sig))
        slot->status |= MPR_STATUS_TYPE_KNOWN;
    if (slot->rsig || mpr_link_get_is_ready(slot->link))
        slot->status |= MPR_STATUS_LINK_KNOWN;
    return slot->status;
}

void mpr_slot_send_msg(mpr_local_slot slot, lo_message msg, mpr_time time, mpr_proto proto,
                       int bundle_idx)
{
    mpr_link_add_msg(slot->link, mpr_sig_get_path((mpr_sig)slot->sig), msg, time, proto, bundle_idx);
}

int mpr_slot_compare_names(const void *l, const void *r)
{
    mpr_sig lsig = (*(mpr_slot*)l)->sig;
    mpr_sig rsig = (*(mpr_slot*)r)->sig;
    int result = strcmp(mpr_dev_get_name(mpr_sig_get_dev(lsig)),
                        mpr_dev_get_name(mpr_sig_get_dev(rsig)));
    if (0 == result)
        return strcmp(mpr_sig_get_name(lsig), mpr_sig_get_name(rsig));
    return result;
}
