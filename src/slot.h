
#ifndef __MAPPER_SLOT_H__
#define __MAPPER_SLOT_H__

mpr_slot mpr_slot_new(mpr_map map, mpr_sig sig, unsigned char is_local, unsigned char is_src);

void mpr_slot_alloc_values(mpr_local_slot slot, int num_inst, int hist_size);

void mpr_slot_free(mpr_slot slot);

void mpr_slot_free_value(mpr_local_slot slot);

int mpr_slot_set_from_msg(mpr_slot slot, mpr_msg msg);

void mpr_slot_add_props_to_msg(lo_message msg, mpr_slot slot, int is_dest);

void mpr_slot_print(mpr_slot slot, int is_dest);

int mpr_slot_match_full_name(mpr_slot slot, const char *full_name);

void mpr_slot_remove_inst(mpr_local_slot slot, int idx);

#endif /* __MAPPER_SLOT_H__ */
