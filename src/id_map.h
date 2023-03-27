
#ifndef __MPR_ID_MAP_H__
#define __MPR_ID_MAP_H__

#include <stdint.h>
#include "util/mpr_inline.h"

/*! Bit flags for indicating instance id_map status. */
#define UPDATED           0x01
#define RELEASED_LOCALLY  0x02
#define RELEASED_REMOTELY 0x04

/*! The instance ID map is a linked list of int32 instance ids for coordinating
 *  remote and local instances. */
typedef struct _mpr_id_map {
    struct _mpr_id_map *next;       /*!< The next id map in the list. */

    uint64_t GID;                   /*!< Hash for originating device. */
    uint64_t LID;                   /*!< Local instance id to map. */
    int LID_refcount;
    int GID_refcount;
} mpr_id_map_t, *mpr_id_map;

MPR_INLINE static void mpr_id_map_incref_local(mpr_id_map id_map)
{
    ++id_map->LID_refcount;
}

MPR_INLINE static void mpr_id_map_incref_global(mpr_id_map id_map)
{
    ++id_map->GID_refcount;
}

#endif /* __MPR_ID_MAP_H__ */
