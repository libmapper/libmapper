
#ifndef __MPR_MESSAGE_H__
#define __MPR_MESSAGE_H__

#include "mpr_type.h"
#include <mapper/mapper_constants.h>

typedef struct _mpr_msg *mpr_msg;
typedef struct _mpr_msg_atom *mpr_msg_atom;

/*! Some useful strings for sending administrative messages. */
typedef enum {
    MSG_DEV,
    MSG_DEV_MOD,
    MSG_LOGOUT,
    MSG_MAP,
    MSG_MAP_TO,
    MSG_MAPPED,
    MSG_MAP_MOD,
    MSG_NAME_PROBE,
    MSG_NAME_REG,
    MSG_PING,
    MSG_SIG,
    MSG_SIG_REM,
    MSG_SIG_MOD,
    MSG_SUBSCRIBE,
    MSG_SYNC,
    MSG_UNMAP,
    MSG_UNMAPPED,
    MSG_WHO,
    NUM_MSG_STRINGS
} net_msg_t;

/* For property indexes, bits 1–8 are used for numerical index, bits 9–14 are
 * used for the mpr_prop enum. */
#define PROP_ADD        0x04000
#define PROP_REMOVE     0x08000
#define DST_SLOT_PROP   0x10000
#define SRC_SLOT_PROP_BIT_OFFSET    17
#define SRC_SLOT_PROP(idx) ((idx + 1) << SRC_SLOT_PROP_BIT_OFFSET)
#define SRC_SLOT(idx) ((idx >> SRC_SLOT_PROP_BIT_OFFSET) - 1)
#define MASK_PROP_BITFLAGS(idx) (idx & 0x3F00)
#define PROP_TO_INDEX(prop) ((prop & 0x3F00) >> 8)
#define INDEX_TO_PROP(idx) (idx << 8)

/*! Parse a message based on an OSC path and named properties.
 *  \param argc     Number of arguments in the argv array.
 *  \param types    String containing message parameter types.
 *  \param argv     Vector of lo_arg structures.
 *  \return         A mpr_msg structure. Free when done using mpr_msg_free. */
mpr_msg mpr_msg_parse_props(int argc, const mpr_type *types, lo_arg **argv);

void mpr_msg_free(mpr_msg msg);

int mpr_msg_get_num_atoms(mpr_msg m);

mpr_msg_atom mpr_msg_get_atom(mpr_msg m, int idx);

/*! Look up the value of a message parameter by symbolic identifier.
 *  \param msg      Structure containing parameter info.
 *  \param prop     Symbolic identifier of the property to look for.
 *  \return         Pointer to mpr_msg_atom, or zero if not found. */
mpr_msg_atom mpr_msg_get_prop(mpr_msg msg, int prop);

int mpr_msg_atom_get_len(mpr_msg_atom a);

int mpr_msg_atom_get_prop(mpr_msg_atom a);

void mpr_msg_atom_set_prop(mpr_msg_atom a, int prop);

const char *mpr_msg_atom_get_key(mpr_msg_atom a);

/* TODO: pass whole type array instead? */
const mpr_type *mpr_msg_atom_get_types(mpr_msg_atom a);

lo_arg **mpr_msg_atom_get_values(mpr_msg_atom a);

int mpr_msg_get_prop_as_int32(mpr_msg msg, int prop);

const char *mpr_msg_get_prop_as_str(mpr_msg msg, int prop);

#endif /* __MPR_MESSAGE_H__ */
