
#ifndef __MAPPER_SIGNAL_H__
#define __MAPPER_SIGNAL_H__

#define MPR_MAX_VECTOR_LEN 128

/*! Initialize an already-allocated mpr_sig structure. */
void mpr_sig_init(mpr_sig s, mpr_dir dir, const char *name, int len,
                  mpr_type type, const char *unit, const void *min,
                  const void *max, int *num_inst);

/*! Get the full OSC name of a signal, including device name prefix.
 *  \param sig  The signal value to query.
 *  \param name A string to accept the name.
 *  \param len  The length of string pointed to by name.
 *  \return     The number of characters used, or 0 if error.  Note that in some
 *              cases the name may not be available. */
int mpr_sig_get_full_name(mpr_sig sig, char *name, int len);

void mpr_sig_call_handler(mpr_local_sig sig, int evt, mpr_id inst, int len,
                          const void *val, mpr_time *time, float diff);

int mpr_sig_set_from_msg(mpr_sig sig, mpr_msg msg);

void mpr_sig_update_timing_stats(mpr_local_sig sig, float diff);

/*! Free memory used by a mpr_sig. Call this only for signals that are not
 *  registered with a device. Registered signals will be freed by mpr_sig_free().
 *  \param s        The signal to free. */
void mpr_sig_free_internal(mpr_sig sig);

void mpr_sig_send_state(mpr_sig sig, net_msg_t cmd);

void mpr_sig_send_removed(mpr_local_sig sig);

/*! Helper to find the size in bytes of a signal's full vector. */
MPR_INLINE static size_t mpr_sig_get_vector_bytes(mpr_sig sig)
{
    return mpr_type_get_size(sig->type) * sig->len;
}

/*! Helper to check if a type character is valid. */
MPR_INLINE static int check_sig_length(int length)
{
    return (length < 1 || length > MPR_MAX_VECTOR_LEN);
}

/**** Instances ****/

/*! Fetch a reserved (preallocated) signal instance using an instance id,
 *  activating it if necessary.
 *  \param s        The signal owning the desired instance.
 *  \param LID      The requested signal instance id.
 *  \param flags    Bitflags indicating if search should include released
 *                  instances.
 *  \param t        Time associated with this action.
 *  \param activate Set to 1 to activate a reserved instance if necessary.
 *  \return         The index of the retrieved instance id map, or -1 if no free
 *                  instances were available and allocation of a new instance
 *                  was unsuccessful according to the selected allocation
 *                  strategy. */
int mpr_sig_get_idmap_with_LID(mpr_local_sig sig, mpr_id LID, int flags, mpr_time t, int activate);

/*! Fetch a reserved (preallocated) signal instance using instance id map,
 *  activating it if necessary.
 *  \param s        The signal owning the desired instance.
 *  \param GID      Globally unique id of this instance.
 *  \param flags    Bitflags indicating if search should include released instances.
 *  \param t        Time associated with this action.
 *  \param activate Set to 1 to activate a reserved instance if necessary.
 *  \return         The index of the retrieved instance id map, or -1 if no free
 *                  instances were available and allocation of a new instance
 *                  was unsuccessful according to the selected allocation
 *                  strategy. */
int mpr_sig_get_idmap_with_GID(mpr_local_sig sig, mpr_id GID, int flags, mpr_time t, int activate);

/*! Release a specific signal instance. */
void mpr_sig_release_inst_internal(mpr_local_sig sig, int inst_idx);

#endif /* __MAPPER_SIGNAL_H__ */
