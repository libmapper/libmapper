
#ifndef __MPR_MAP_H__
#define __MPR_MAP_H__
#define __MPR_TYPES_H__

typedef struct _mpr_map *mpr_map;
typedef struct _mpr_local_map *mpr_local_map;

#include "expression.h"
#include "id_map.h"
#include "message.h"
#include "slot.h"

#define MAX_NUM_MAP_SRC     8       /* arbitrary */
#define MAX_NUM_MAP_DST     8       /* arbitrary */

#define MPR_MAP_STRUCT_ITEMS                                                    \
    mpr_obj_t obj;                  /* always first */                          \
    mpr_dev *scopes;                                                            \
    char *expr_str;                                                             \
    struct _mpr_id_map *idmap;      /*!< Associated mpr_id_map. */              \
    int muted;                      /*!< 1 to mute mapping, 0 to unmute */      \
    int num_scopes;                                                             \
    int num_src;                                                                \
    mpr_loc process_loc;                                                        \
    int status;                                                                 \
    int protocol;                   /*!< Data transport protocol. */            \
    int use_inst;                   /*!< 1 if using instances, 0 otherwise. */  \
    int bundle;

/*! A record that describes the properties of a mapping.
 *  @ingroup map */
typedef struct _mpr_map {
    MPR_MAP_STRUCT_ITEMS
    mpr_slot *src;
    mpr_slot dst;
} mpr_map_t;

typedef struct _mpr_local_map {
    MPR_MAP_STRUCT_ITEMS
    mpr_local_slot *src;
    mpr_local_slot dst;

    struct _mpr_rtr *rtr;

    mpr_expr expr;                  /*!< The mapping expression. */
    char *updated_inst;             /*!< Bitflags to indicate updated instances. */
    mpr_value_t *vars;              /*!< User variables values. */
    const char **var_names;         /*!< User variables names. */
    int num_vars;                   /*!< Number of user variables. */
    int num_inst;                   /*!< Number of local instances. */

    uint8_t is_local_only;
    uint8_t one_src;
    uint8_t updated;
} mpr_local_map_t;

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

#endif /* __MPR_MAP_H__ */
