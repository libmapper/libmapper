
#ifndef __MPR_ROUTER_H__
#define __MPR_ROUTER_H__

typedef struct _mpr_rtr *mpr_rtr;
typedef struct _mpr_rtr_sig *mpr_rtr_sig;

#include "link.h"
#include "network.h"

/*! The rtr_sig is a linked list containing a signal and a list of mapping
 *  slots.  TODO: This should be replaced with a more efficient approach
 *  such as a hash table or search tree. */
typedef struct _mpr_rtr_sig {
    struct _mpr_rtr_sig *next;      /*!< The next rtr_sig in the list. */
    struct _mpr_local_sig *sig;     /*!< The associated signal. */
    mpr_local_slot *slots;
    int num_slots;
    int id_counter;
} mpr_rtr_sig_t;

/*! The router structure. */
typedef struct _mpr_rtr {
    mpr_net net;
    /* TODO: rtr should either be stored in local_dev or shared */
    struct _mpr_local_dev *dev;     /*!< The device associated with this link. */
    mpr_rtr_sig sigs;               /*!< The list of mappings for each signal. */
} mpr_rtr_t;

void mpr_rtr_remove_sig(mpr_rtr r, mpr_rtr_sig rs);

void mpr_rtr_num_inst_changed(mpr_rtr r, mpr_local_sig sig, int size);

void mpr_rtr_remove_inst(mpr_rtr rtr, mpr_local_sig sig, int idx);

/*! For a given signal instance, calculate mapping outputs and forward to destinations. */
void mpr_rtr_process_sig(mpr_rtr rtr, mpr_local_sig sig, int inst_idx, const void *val, mpr_time t);

void mpr_rtr_add_map(mpr_rtr rtr, mpr_local_map map);

void mpr_rtr_remove_link(mpr_rtr rtr, mpr_link lnk);

int mpr_rtr_remove_map(mpr_rtr rtr, mpr_local_map map);

mpr_local_slot mpr_rtr_get_slot(mpr_rtr rtr, mpr_local_sig sig, int slot_num);

int mpr_rtr_loop_check(mpr_rtr rtr, mpr_local_sig sig, int n_remote, const char **remote);

void mpr_rtr_check_links(mpr_rtr rtr, mpr_list links);

#endif /* __MPR_ROUTER_H__ */
