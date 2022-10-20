
#ifndef __MAPPER_MAP_H__
#define __MAPPER_MAP_H__

void mpr_map_alloc_values(mpr_local_map map);

/*! Process the signal instance value according to mapping properties.
 *  The result of this operation should be sent to the destination.
 *  \param map          The mapping process to perform.
 *  \param time         Timestamp for this update. */
void mpr_map_send(mpr_local_map map, mpr_time time);

void mpr_map_receive(mpr_local_map map, mpr_time time);

lo_message mpr_map_build_msg(mpr_local_map map, mpr_local_slot slot, const void *val,
                             mpr_type *types, mpr_id_map idmap);

/*! Set a mapping's properties based on message parameters. */
int mpr_map_set_from_msg(mpr_map map, mpr_msg msg, int override);

const char *mpr_loc_as_str(mpr_loc loc);

mpr_loc mpr_loc_from_str(const char *string);

const char *mpr_protocol_as_str(mpr_proto pro);

mpr_proto mpr_protocol_from_str(const char *string);

const char *mpr_steal_as_str(mpr_steal_type stl);

int mpr_map_send_state(mpr_map map, int slot, net_msg_t cmd);

void mpr_map_init(mpr_map map);

void mpr_map_free(mpr_map map);

/*! Prepare a lo_message for sending based on a map struct. */
const char *mpr_map_prepare_msg(mpr_map map, lo_message msg, int slot_idx);

#endif /* __MAPPER_MAP_H__ */
