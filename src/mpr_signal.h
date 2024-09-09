
#ifndef __MPR_SIGNAL_H__
#define __MPR_SIGNAL_H__
#define __MPR_TYPES_H__

typedef struct _mpr_sig *mpr_sig;
typedef struct _mpr_local_sig *mpr_local_sig;
typedef struct _mpr_sig_inst *mpr_sig_inst;
typedef int mpr_sig_group;

#include "id_map.h"
#include "mpr_time.h"
#include "mpr_type.h"
#include "network.h"
#include "slot.h"
#include "value.h"

#define MPR_MAX_VECTOR_LEN 128

size_t mpr_sig_get_struct_size(int is_local);

int mpr_sig_osc_handler(const char *path, const char *types, lo_arg **argv, int argc,
                        lo_message msg, void *data);

/*! Initialize an already-allocated mpr_sig structure. */
void mpr_sig_init(mpr_sig sig, mpr_dev dev, int is_local, mpr_dir dir, const char *name, int len,
                  mpr_type type, const char *unit, const void *min, const void *max, int *num_inst);

void mpr_local_sig_add_to_net(mpr_local_sig sig, mpr_net net);

void mpr_sig_call_handler(mpr_local_sig sig, int evt, mpr_id inst, unsigned int inst_idx, float diff);

int mpr_sig_set_from_msg(mpr_sig sig, mpr_msg msg);

/*! Free memory used by a `mpr_sig`. Call this only for signals that are not
 *  registered with a device. Registered signals should be freed using `mpr_sig_free()`.
 *  \param sig      The signal to free. */
void mpr_sig_free_internal(mpr_sig sig);

void mpr_sig_send_state(mpr_sig sig, net_msg_t cmd);

void mpr_local_sig_set_dev_id(mpr_local_sig sig, mpr_id id);

mpr_dir mpr_sig_get_dir(mpr_sig sig);

/*! Get the full OSC name of a signal, including device name prefix.
 *  \param sig  The signal value to query.
 *  \param name A string to accept the name.
 *  \param len  The length of string pointed to by name.
 *  \return     The number of characters used, or 0 if error.  Note that in some
 *              cases the name may not be available. */
int mpr_sig_get_full_name(mpr_sig sig, char *name, int len);

int mpr_sig_get_len(mpr_sig sig);

const char *mpr_sig_get_name(mpr_sig sig);

const char *mpr_sig_get_path(mpr_sig sig);

mpr_type mpr_sig_get_type(mpr_sig sig);

int mpr_sig_compare_names(mpr_sig l, mpr_sig r);

void mpr_sig_copy_props(mpr_sig to, mpr_sig from);

int mpr_local_sig_check_outgoing(mpr_local_sig sig, int num_dst_sigs, const char **dst_sig_names);

void mpr_local_sig_add_slot(mpr_local_sig sig, mpr_local_slot slot, mpr_dir dir);

void mpr_local_sig_remove_slot(mpr_local_sig sig, mpr_local_slot slot, mpr_dir dir);

/**** Instances ****/

int mpr_sig_get_num_inst_internal(mpr_sig sig);

int mpr_sig_get_use_inst(mpr_sig sig);

void mpr_local_sig_set_inst_value(mpr_local_sig sig, const void *value, int inst_idx,
                                  mpr_id_map id_map, int status, int map_manages_inst,
                                  mpr_time time);

/* Functions below are only used by testinstance.c for printing instance indices */
mpr_sig_inst *mpr_local_sig_get_insts(mpr_local_sig sig);

uint8_t mpr_sig_get_inst_idx(mpr_sig_inst si);

unsigned int mpr_local_sig_get_num_id_maps(mpr_local_sig sig);

mpr_id_map mpr_local_sig_get_id_map_by_inst_idx(mpr_local_sig sig, unsigned int inst_idx);

mpr_sig_group mpr_local_sig_get_group(mpr_local_sig sig);

void mpr_local_sig_release_inst_by_origin(mpr_local_sig sig, mpr_dev origin);

#endif /* __MPR_SIGNAL_H__ */
