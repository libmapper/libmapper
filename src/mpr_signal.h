
#ifndef __MPR_SIGNAL_H__
#define __MPR_SIGNAL_H__
#define __MPR_TYPES_H__

typedef struct _mpr_sig *mpr_sig;
typedef struct _mpr_local_sig *mpr_local_sig;
typedef struct _mpr_sig_inst *mpr_sig_inst;
typedef int mpr_sig_group;

#include "id_map.h"
#include "object.h"
#include "mpr_type.h"
#include "time.h"

#define MPR_MAX_VECTOR_LEN 128

int mpr_sig_lo_handler(const char *path, const char *types, lo_arg **argv, int argc,
                       lo_message msg, void *data);

/*! Initialize an already-allocated mpr_sig structure. */
void mpr_sig_init(mpr_sig sig, mpr_dev dev, int is_local, mpr_dir dir, const char *name, int len,
                  mpr_type type, const char *unit, const void *min, const void *max, int *num_inst);

void mpr_sig_call_handler(mpr_local_sig sig, int evt, mpr_id inst, int len,
                          const void *val, mpr_time time, float diff);

int mpr_sig_set_from_msg(mpr_sig sig, mpr_msg msg);

void mpr_sig_update_timing_stats(mpr_local_sig sig, float diff);

/*! Free memory used by a `mpr_sig`. Call this only for signals that are not
 *  registered with a device. Registered signals will be freed by `mpr_sig_free()`.
 *  \param sig      The signal to free. */
void mpr_sig_free_internal(mpr_sig sig);

void mpr_sig_send_state(mpr_sig sig, net_msg_t cmd);

void mpr_sig_send_removed(mpr_local_sig sig);

/**** Instances ****/

/*! Fetch a reserved (preallocated) signal instance using an instance id,
 *  activating it if necessary.
 *  \param sig      The signal owning the desired instance.
 *  \param LID      The requested signal instance id.
 *  \param flags    Bitflags indicating if search should include released
 *                  instances.
 *  \param t        Time associated with this action.
 *  \param activate Set to 1 to activate a reserved instance if necessary.
 *  \return         The index of the retrieved instance id map, or -1 if no free
 *                  instances were available and allocation of a new instance
 *                  was unsuccessful according to the selected allocation
 *                  strategy. */
int mpr_sig_get_id_map_with_LID(mpr_local_sig sig, mpr_id LID, int flags, mpr_time t, int activate);

/*! Fetch a reserved (preallocated) signal instance using instance id map,
 *  activating it if necessary.
 *  \param sig      The signal owning the desired instance.
 *  \param GID      Globally unique id of this instance.
 *  \param flags    Bitflags indicating if search should include released instances.
 *  \param t        Time associated with this action.
 *  \param activate Set to 1 to activate a reserved instance if necessary.
 *  \return         The index of the retrieved instance id map, or -1 if no free
 *                  instances were available and allocation of a new instance
 *                  was unsuccessful according to the selected allocation
 *                  strategy. */
int mpr_sig_get_id_map_with_GID(mpr_local_sig sig, mpr_id GID, int flags, mpr_time t, int activate);

mpr_sig_inst mpr_local_sig_get_inst_by_idx(mpr_local_sig sig, int inst_idx, mpr_id_map *id_map);

mpr_sig_inst mpr_local_sig_get_inst_by_id_map_idx(mpr_local_sig sig, int id_map_idx,
                                                  mpr_id_map *id_map);

int mpr_local_sig_get_id_map_status(mpr_local_sig sig, int id_map_idx);

/*! Release a specific signal instance. */
void mpr_sig_release_inst_internal(mpr_local_sig sig, int inst_idx);

int mpr_local_sig_get_updated(mpr_local_sig sig, int inst_idx);

void mpr_local_sig_set_updated(mpr_local_sig sig, int inst_idx);

void mpr_sig_add_method(mpr_sig sig, lo_server server);

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

int mpr_local_sig_get_num_id_maps(mpr_local_sig sig);

int mpr_local_sig_get_num_inst(mpr_local_sig sig);

const char *mpr_sig_get_path(mpr_sig sig);

size_t mpr_sig_get_struct_size();

mpr_type mpr_sig_get_type(mpr_sig sig);

int mpr_sig_get_use_inst(mpr_sig sig);

int mpr_sig_compare_names(mpr_sig l, mpr_sig r);

void mpr_sig_copy_props(mpr_sig to, mpr_sig from);

uint8_t *mpr_local_sig_get_lock(mpr_local_sig sig);

void mpr_sig_set_num_maps(mpr_sig sig, int num_maps_in, int num_maps_out);

void mpr_local_sig_release_map_inst(mpr_local_sig sig, mpr_time time);

uint8_t mpr_sig_inst_get_idx(mpr_sig_inst si);

mpr_time mpr_sig_inst_get_time(mpr_sig_inst si);

void mpr_local_sig_set_inst_value(mpr_local_sig sig, mpr_sig_inst si, const void *value,
                                  mpr_time time);

/* only used by testinstance.c for printing instance indices */
mpr_sig_inst *mpr_local_sig_get_insts(mpr_local_sig sig);

#endif /* __MPR_SIGNAL_H__ */
