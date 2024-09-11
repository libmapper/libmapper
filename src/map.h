
#ifndef __MPR_MAP_H__
#define __MPR_MAP_H__
#define __MPR_TYPES_H__

#define MAX_NUM_MAP_SRC     8       /* arbitrary */
#define MAX_NUM_MAP_DST     8       /* arbitrary */

typedef struct _mpr_map *mpr_map;
typedef struct _mpr_local_map *mpr_local_map;

#include "expression.h"
#include "id_map.h"
#include "message.h"
#include "mpr_signal.h"
#include "slot.h"

#define MPR_MAP_STATUS_PUSHED   0x2000
#define MPR_MAP_STATUS_READY    0xC000


size_t mpr_map_get_struct_size(int is_local);

void mpr_map_alloc_values(mpr_local_map map, int quiet);

/*! Process the signal instance value according to mapping properties.
 *  The result of this operation should be sent to the destination.
 *  \param map          The mapping process to perform.
 *  \param time         Timestamp for this update. */
void mpr_map_send(mpr_local_map map, mpr_time time);

void mpr_map_receive(mpr_local_map map, mpr_time time);

lo_message mpr_map_build_msg(mpr_local_map map, mpr_local_slot slot, const void *val,
                             char *has_value, mpr_id_map id_map);

/*! Set a mapping's properties based on message parameters. */
int mpr_map_set_from_msg(mpr_map map, mpr_msg msg);

int mpr_local_map_update_status(mpr_local_map map);

int mpr_map_send_state(mpr_map map, int slot, net_msg_t cmd, int version);

void mpr_map_init(mpr_map map, int num_src, mpr_sig *src, mpr_sig dst, int is_local);

void mpr_map_free(mpr_map map);

void mpr_map_add_src(mpr_map map, mpr_sig sig, mpr_dir dir, int is_local);

int mpr_map_compare(mpr_map l, mpr_map r);

int mpr_map_compare_names(mpr_map map, int num_src, const char **srcs, const char *dst);

int mpr_map_get_has_dev(mpr_map map, mpr_id dev_id, mpr_dir dir);

int mpr_map_get_has_link_id(mpr_map map, mpr_id link_id);

int mpr_map_get_has_sig(mpr_map map, mpr_sig sig, mpr_dir dir);

mpr_sig mpr_map_get_dst_sig(mpr_map map);

mpr_slot mpr_map_get_dst_slot(mpr_map map);

mpr_expr mpr_local_map_get_expr(mpr_local_map map);

const char *mpr_map_get_expr_str(mpr_map map);

int mpr_local_map_get_has_scope(mpr_local_map map, mpr_id id);

int mpr_local_map_get_is_one_src(mpr_local_map map);

int mpr_map_get_locality(mpr_map map);

int mpr_local_map_get_num_inst(mpr_local_map map);

int mpr_map_get_num_src(mpr_map map);

mpr_loc mpr_map_get_process_loc(mpr_map map);

mpr_loc mpr_local_map_get_process_loc_from_msg(mpr_local_map map, mpr_msg msg);

mpr_proto mpr_map_get_protocol(mpr_map map);

mpr_sig mpr_map_get_src_sig(mpr_map map, int idx);

mpr_slot mpr_map_get_src_slot(mpr_map map, int idx);

mpr_slot mpr_map_get_src_slot_by_id(mpr_map map, int id);

void mpr_local_map_set_updated(mpr_local_map map, int inst_idx);

void mpr_map_status_decr(mpr_map map);

int mpr_map_get_use_inst(mpr_map map);

void mpr_map_remove_scope_internal(mpr_map map, mpr_dev dev);

void mpr_map_clear_empty_props(mpr_local_map map);

mpr_id_map mpr_local_map_get_id_map(mpr_local_map map);

#endif /* __MPR_MAP_H__ */
