
#ifndef __MPR_ROUTER_H__
#define __MPR_ROUTER_H__

typedef struct _mpr_rtr *mpr_rtr;
typedef struct _mpr_rtr_sig *mpr_rtr_sig;

#include "link.h"
#include "network.h"

mpr_rtr mpr_rtr_new(mpr_net net);

void mpr_rtr_free(mpr_rtr rtr);

void mpr_rtr_remove_sig(mpr_rtr rtr, mpr_local_sig sig);

void mpr_rtr_num_inst_changed(mpr_rtr rtr, mpr_local_sig sig, int size);

void mpr_rtr_remove_inst(mpr_rtr rtr, mpr_local_sig sig, int idx);

/*! For a given signal instance, calculate mapping outputs and forward to destinations. */
void mpr_rtr_process_sig(mpr_rtr rtr, mpr_local_sig sig, int inst_idx, const void *val, mpr_time t);

void mpr_rtr_add_map(mpr_rtr rtr, mpr_local_map map);

void mpr_rtr_remove_link(mpr_rtr rtr, mpr_link lnk);

int mpr_rtr_remove_map(mpr_rtr rtr, mpr_local_map map);

mpr_local_slot mpr_rtr_get_slot(mpr_rtr rtr, mpr_local_sig sig, int slot_num);

int mpr_rtr_loop_check(mpr_rtr rtr, mpr_local_sig sig, int n_remote, const char **remote);

void mpr_rtr_check_links(mpr_rtr rtr, mpr_list links);

void mpr_rtr_call_local_handler(mpr_rtr rtr, const char *path, lo_message msg);

mpr_expr_stack mpr_rtr_get_expr_stack(mpr_rtr rtr);

/* temporary! */
void mpr_rtr_add_dev(mpr_rtr rtr, mpr_local_dev dev);

#endif /* __MPR_ROUTER_H__ */
