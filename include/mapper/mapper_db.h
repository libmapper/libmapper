#ifndef __MAPPER_DB_H__
#define __MAPPER_DB_H__

#ifdef __cplusplus
extern "C" {
#endif

//struct _mapper_device;
//typedef struct _mapper_device mapper_device_t;
//typedef struct _mapper_device *mapper_device;
//struct _mapper_signal;
//typedef struct _mapper_signal mapper_signal_t;
//typedef struct _mapper_signal *mapper_signal;

/* An opaque structure to hold a string table of key-value pairs, used
 * to hold arbitrary signal and device parameters. */
struct _mapper_string_table;

struct _mapper_monitor;

/*! \file This file defines structs used to return information from
 *  the network database. */

#include <lo/lo.h>

/*! A 64-bit data structure containing an NTP-compatible time tag, as
 *  used by OSC. */
typedef lo_timetag mapper_timetag_t;

/*! Possible operations for composing db queries. */
typedef enum _mapper_query_op {
    QUERY_UNDEFINED = -1,
    QUERY_DOES_NOT_EXIST,
    QUERY_EQUAL,
    QUERY_EXISTS,
    QUERY_GREATER_THAN,
    QUERY_GREATER_THAN_OR_EQUAL,
    QUERY_LESS_THAN,
    QUERY_LESS_THAN_OR_EQUAL,
    QUERY_NOT_EQUAL,
    NUM_MAPPER_QUERY_OPS
} mapper_query_op;

/*! Describes what happens when the range boundaries are exceeded.
 *  @ingroup mapdb */
typedef enum _mapper_boundary_action {
    BA_UNDEFINED,
    BA_NONE,    //!< Value is passed through unchanged. This is the default.
    BA_MUTE,    //!< Value is muted.
    BA_CLAMP,   //!< Value is limited to the boundary.
    BA_FOLD,    //!< Value continues in opposite direction.
    BA_WRAP,    //!< Value appears as modulus offset at the opposite boundary.
    NUM_MAPPER_BOUNDARY_ACTIONS
} mapper_boundary_action;

/*! Describes the map modes.
 *  @ingroup mapdb */
typedef enum _mapper_mode_type {
    MO_UNDEFINED,       //!< Not yet defined
    MO_RAW,             //!< No type coercion
    MO_LINEAR,          //!< Linear scaling
    MO_EXPRESSION,      //!< Expression
    NUM_MAPPER_MODE_TYPES
} mapper_mode_type;

typedef enum _mapper_location {
    LOC_UNDEFINED,
    LOC_SOURCE,
    LOC_DESTINATION
} mapper_location;

/*! The set of possible directions for a signal or mapping slot. */
typedef enum {
    DI_ANY      = 0x00,
    DI_OUTGOING = 0x01,
    DI_INCOMING = 0x02,
    DI_BOTH     = 0x03,
} mapper_direction_t;

/*! The set of possible actions on an instance, used to register callbacks
 *  to inform them of what is happening. */
typedef enum {
    IN_NEW                  = 0x01, //!< New instance has been created.
    IN_UPSTREAM_RELEASE     = 0x02, //!< Instance has been released by upstream device.
    IN_DOWNSTREAM_RELEASE   = 0x04, //!< Instance has been released by downstream device.
    IN_OVERFLOW             = 0x08, //!< No local instances left for incoming remote instance.
    IN_ALL                  = 0xFF
} mapper_instance_event_t;

/*! Describes the voice-stealing mode for instances.
 *  @ingroup mapdb */
typedef enum _mapper_instance_allocation_type {
    IN_UNDEFINED,       //!< Not yet defined
    IN_STEAL_OLDEST,    //!< Steal the oldest instance
    IN_STEAL_NEWEST,    //!< Steal the newest instance
    NUM_MAPPER_INSTANCE_ALLOCATION_TYPES
} mapper_instance_allocation_type;

/*! The set of possible actions on a database record, used to inform callbacks
 *  of what is happening to a record. */
typedef enum {
    MAPPER_ADDED,
    MAPPER_MODIFIED,
    MAPPER_REMOVED,
    MAPPER_EXPIRED,
} mapper_action_t;

#ifdef __cplusplus
}
#endif

#endif // __MAPPER_DB_H__
