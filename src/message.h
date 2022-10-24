
#ifndef __MAPPER_MESSAGE_H__
#define __MAPPER_MESSAGE_H__

/*! Parse a message based on an OSC path and named properties.
 *  \param argc     Number of arguments in the argv array.
 *  \param types    String containing message parameter types.
 *  \param argv     Vector of lo_arg structures.
 *  \return         A mpr_msg structure. Free when done using mpr_msg_free. */
mpr_msg mpr_msg_parse_props(int argc, const mpr_type *types, lo_arg **argv);

void mpr_msg_free(mpr_msg msg);

/*! Look up the value of a message parameter by symbolic identifier.
 *  \param msg      Structure containing parameter info.
 *  \param prop     Symbolic identifier of the property to look for.
 *  \return         Pointer to mpr_msg_atom, or zero if not found. */
mpr_msg_atom mpr_msg_get_prop(mpr_msg msg, int prop);

void mpr_msg_add_typed_val(lo_message msg, int len, mpr_type type, const void *val);

#endif /* __MAPPER_MESSAGE_H__ */
