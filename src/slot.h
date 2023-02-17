
#ifndef __MPR_SLOT_H__
#define __MPR_SLOT_H__

typedef struct _mpr_slot *mpr_slot;
typedef struct _mpr_local_slot *mpr_local_slot;

#include "map.h"
#include "mpr_signal.h"
#include "router.h"
#include "value.h"

mpr_slot mpr_slot_new(mpr_map map, mpr_sig sig, mpr_dir dir,
                      unsigned char is_local, unsigned char is_src);

void mpr_slot_alloc_values(mpr_local_slot slot, int num_inst, int hist_size);

void mpr_slot_free(mpr_slot slot);

void mpr_slot_free_value(mpr_local_slot slot);

int mpr_slot_set_from_msg(mpr_slot slot, mpr_msg msg);

void mpr_slot_add_props_to_msg(lo_message msg, mpr_slot slot, int is_dest);

void mpr_slot_print(mpr_slot slot, int is_dest);

int mpr_slot_match_full_name(mpr_slot slot, const char *full_name);

void mpr_slot_remove_inst(mpr_local_slot slot, int idx);

char mpr_slot_check_status(mpr_local_slot slot);

struct _mpr_link* mpr_slot_get_link(mpr_slot slot);

void mpr_slot_set_link(mpr_slot slot, struct _mpr_link* link);

mpr_map mpr_slot_get_map(mpr_slot slot);

mpr_sig mpr_slot_get_sig(mpr_slot slot);

mpr_dir mpr_slot_get_dir(mpr_slot slot);

void mpr_slot_set_dir(mpr_slot slot, mpr_dir dir);

int mpr_slot_get_id(mpr_slot slot);

void mpr_slot_set_id(mpr_slot slot, int id);

int mpr_slot_get_is_local(mpr_slot slot);

int mpr_slot_get_num_inst(mpr_slot slot);

/* TODO: some calls to this function shouldn't need access to the mpr_rtr_sig type, rather they
 * just need to know if the slot has a local signal. */
mpr_rtr_sig mpr_slot_get_rtr_sig(mpr_local_slot slot);

void mpr_slot_set_rtr_sig(mpr_local_slot slot, mpr_rtr_sig rsig);

int mpr_slot_get_causes_update(mpr_slot slot);

void mpr_slot_set_causes_update(mpr_slot slot, int causes_update);

mpr_value mpr_slot_get_value(mpr_local_slot slot);

int mpr_slot_set_value(mpr_local_slot slot, int inst_idx, void *val, mpr_time time);

void mpr_slot_reset_inst(mpr_local_slot slot, int inst_idx);

#endif /* __MPR_SLOT_H__ */
