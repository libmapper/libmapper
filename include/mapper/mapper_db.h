#ifndef __MAPPER_DB_H__
#define __MAPPER_DB_H__

#ifdef __cplusplus
extern "C" {
#endif

/*! \file This file defines structs used to return information from
 *  the network database. */

#include <lo/lo.h>

typedef enum {
    MAPPER_STAGED       = 0x00,
    MAPPER_TYPE_KNOWN   = 0x01,
    MAPPER_LENGTH_KNOWN = 0x02,
    MAPPER_LINK_KNOWN   = 0x04,
    MAPPER_READY        = 0x0F,
    MAPPER_ACTIVE       = 0x1F
} mapper_status;

/*! A 64-bit data structure containing an NTP-compatible time tag, as
 *  used by OSC. */
typedef lo_timetag mapper_timetag_t;

/*! Possible operations for composing db queries. */
typedef enum {
    MAPPER_OP_UNDEFINED = -1,
    MAPPER_OP_DOES_NOT_EXIST,
    MAPPER_OP_EQUAL,
    MAPPER_OP_EXISTS,
    MAPPER_OP_GREATER_THAN,
    MAPPER_OP_GREATER_THAN_OR_EQUAL,
    MAPPER_OP_LESS_THAN,
    MAPPER_OP_LESS_THAN_OR_EQUAL,
    MAPPER_OP_NOT_EQUAL,
    NUM_MAPPER_OPS
} mapper_op;

/*! Describes what happens when the range boundaries are exceeded.
 *  @ingroup mapdb */
typedef enum {
    MAPPER_UNDEFINED,
    MAPPER_NONE,    //!< Value is passed through unchanged. This is the default.
    MAPPER_MUTE,    //!< Value is muted.
    MAPPER_CLAMP,   //!< Value is limited to the boundary.
    MAPPER_FOLD,    //!< Value continues in opposite direction.
    MAPPER_WRAP,    //!< Value appears as modulus offset at the opposite boundary.
    NUM_MAPPER_BOUNDARY_ACTIONS
} mapper_boundary_action;

/*! Describes the map modes.
 *  @ingroup mapdb */
typedef enum {
    MAPPER_MODE_UNDEFINED,  //!< Not yet defined
    MAPPER_MODE_RAW,        //!< No type coercion
    MAPPER_MODE_LINEAR,     //!< Linear scaling
    MAPPER_MODE_EXPRESSION, //!< Expression
    NUM_MAPPER_MODES
} mapper_mode;

typedef enum {
    MAPPER_SOURCE = 1,
    MAPPER_DESTINATION
} mapper_location;

/*! The set of possible directions for a signal or mapping slot. */
typedef enum {
    MAPPER_DIR_ANY  = 0x00,
    MAPPER_INCOMING = 0x01,
    MAPPER_OUTGOING = 0x02,
} mapper_direction;

/*! The set of possible actions on an instance, used to register callbacks
 *  to inform them of what is happening. */
typedef enum {
    MAPPER_NEW_INSTANCE         = 0x01, //!< New instance has been created.
    MAPPER_UPSTREAM_RELEASE     = 0x02, //!< Instance was released upstream.
    MAPPER_DOWNSTREAM_RELEASE   = 0x04, //!< Instance was released downstream.
    MAPPER_INSTANCE_OVERFLOW    = 0x08, //!< No local instances left.
    MAPPER_INSTANCE_ALL         = 0x0F
} mapper_instance_event;

/*! Describes the voice-stealing mode for instances.
 *  @ingroup mapdb */
typedef enum {
    MAPPER_NO_STEALING,
    MAPPER_STEAL_OLDEST,    //!< Steal the oldest instance
    MAPPER_STEAL_NEWEST,    //!< Steal the newest instance
    NUM_MAPPER_INSTANCE_ALLOCATION_TYPES
} mapper_instance_allocation_type;

/*! The set of possible actions on a database record, used to inform callbacks
 *  of what is happening to a record. */
typedef enum {
    MAPPER_ADDED,
    MAPPER_MODIFIED,
    MAPPER_REMOVED,
    MAPPER_EXPIRED,
} mapper_record_action;

#ifdef __cplusplus
}
#endif

#endif // __MAPPER_DB_H__
