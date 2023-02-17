
#ifndef __MPR_SIGNAL_H__
#define __MPR_SIGNAL_H__
#define __MPR_TYPES_H__

typedef struct _mpr_sig *mpr_sig;
typedef struct _mpr_local_sig *mpr_local_sig;
typedef int mpr_sig_group;

#include "object.h"
#include "mpr_type.h"
#include "time.h"

#define MPR_MAX_VECTOR_LEN 128

/*! A signal is defined as a vector of values, along with some metadata. */
/* plan: remove idx? we shouldn't need it anymore */
typedef struct _mpr_sig_inst
{
    mpr_id id;                  /*!< User-assignable instance id. */
    void *data;                 /*!< User data of this instance. */
    mpr_time created;           /*!< The instance's creation timestamp. */
    char *has_val_flags;        /*!< Indicates which vector elements have a value. */

    void *val;                  /*!< The current value of this signal instance. */
    mpr_time time;              /*!< The time associated with the current value. */

    uint8_t idx;                /*!< Index for accessing value history. */
    uint8_t has_val;            /*!< Indicates whether this instance has a value. */
    uint8_t active;             /*!< Status of this instance. */
} mpr_sig_inst_t, *mpr_sig_inst;

/* plan: remove inst, add map/slot resource index (is this the same for all source signals?) */
typedef struct _mpr_sig_idmap
{
    struct _mpr_id_map *map;    /*!< Associated mpr_id_map. */
    struct _mpr_sig_inst *inst; /*!< Signal instance. */
    int status;                 /*!< Either 0 or a combination of UPDATED,
                                 *   RELEASED_LOCALLY and RELEASED_REMOTELY. */
} mpr_sig_idmap_t;

#define MPR_SIG_STRUCT_ITEMS                                                            \
    mpr_obj_t obj;              /* always first */                                      \
    char *path;                 /*! OSC path.  Must start with '/'. */                  \
    char *name;                 /*! The name of this signal (path+1). */                \
    char *unit;                 /*!< The unit of this signal, or NULL for N/A. */       \
    float period;               /*!< Estimate of the update rate of this signal. */     \
    float jitter;               /*!< Estimate of the timing jitter of this signal. */   \
    int dir;                    /*!< DIR_OUTGOING / DIR_INCOMING / DIR_BOTH */          \
    int len;                    /*!< Length of the signal vector, or 1 for scalars. */  \
    int use_inst;               /*!< 1 if signal uses instances, 0 otherwise. */        \
    int num_inst;               /*!< Number of instances. */                            \
    int ephemeral;              /*!< 1 if signal is ephemeral, 0 otherwise. */          \
    int num_maps_in;            /* TODO: use dynamic query instead? */                  \
    int num_maps_out;           /* TODO: use dynamic query instead? */                  \
    mpr_steal_type steal_mode;  /*!< Type of voice stealing to perform. */              \
    mpr_type type;              /*!< The type of this signal. */

/*! A record that describes properties of a signal. */
typedef struct _mpr_sig
{
    MPR_SIG_STRUCT_ITEMS
    mpr_dev dev;
} mpr_sig_t, *mpr_sig;

typedef struct _mpr_local_sig
{
    MPR_SIG_STRUCT_ITEMS
    mpr_local_dev dev;

    struct _mpr_sig_idmap *idmaps;  /*!< ID maps and active instances. */
    int idmap_len;
    struct _mpr_sig_inst **inst;    /*!< Array of pointers to the signal insts. */
    char *vec_known;                /*!< Bitflags when entire vector is known. */
    char *updated_inst;             /*!< Bitflags to indicate updated instances. */

    /*! An optional function to be called when the signal value changes or when
     *  signal instance management events occur.. */
    void *handler;
    int event_flags;                /*! Flags for deciding when to call the
                                     *  instance event handler. */

    mpr_sig_group group;            /* TODO: replace with hierarchical instancing */
    uint8_t locked;
    uint8_t updated;                /* TODO: fold into updated_inst bitflags. */
} mpr_local_sig_t, *mpr_local_sig;

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
                          const void *val, mpr_time time, float diff);

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

void mpr_local_sig_set_updated(mpr_local_sig sig, int inst_idx);

#endif /* __MPR_SIGNAL_H__ */
