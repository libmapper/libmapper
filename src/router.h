
#ifndef __MAPPER_ROUTER_H__
#define __MAPPER_ROUTER_H__

void mpr_rtr_remove_sig(mpr_rtr r, mpr_rtr_sig rs);

void mpr_rtr_num_inst_changed(mpr_rtr r, mpr_local_sig sig, int size);

void mpr_rtr_remove_inst(mpr_rtr rtr, mpr_local_sig sig, int idx);

/*! For a given signal instance, calculate mapping outputs and forward to
 *  destinations. */
void mpr_rtr_process_sig(mpr_rtr rtr, mpr_local_sig sig, int inst_idx, const void *val, mpr_time t);

void mpr_rtr_add_map(mpr_rtr rtr, mpr_local_map map);

void mpr_rtr_remove_link(mpr_rtr rtr, mpr_link lnk);

int mpr_rtr_remove_map(mpr_rtr rtr, mpr_local_map map);

mpr_local_slot mpr_rtr_get_slot(mpr_rtr rtr, mpr_local_sig sig, int slot_num);

int mpr_rtr_loop_check(mpr_rtr rtr, mpr_local_sig sig, int n_remote, const char **remote);

#endif /* __MAPPER_ROUTER_H__ */
