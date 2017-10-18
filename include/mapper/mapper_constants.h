#ifndef __MAPPER_CONSTANTS_H__
#define __MAPPER_CONSTANTS_H__

#ifdef __cplusplus
extern "C" {
#endif

/*! This file defines structs used to return information from the network
 *  database. */

#include <lo/lo.h>

#define MAPPER_NOW ((mapper_timetag_t){0L,1L})

/*! Bit flags for coordinating metadata subscriptions. */
typedef enum {
    MAPPER_OBJ_NONE           = 0x00, //!< No objects.
    MAPPER_OBJ_DEVICES        = 0x01, //!< Devices only.
    MAPPER_OBJ_INPUT_SIGNALS  = 0x02, //!< Input signals.
    MAPPER_OBJ_OUTPUT_SIGNALS = 0x04, //!< Output signals.
    MAPPER_OBJ_SIGNALS        = 0x06, //!< All signals.
    MAPPER_OBJ_INCOMING_LINKS = 0x10, //!< Links supporting incoming maps.
    MAPPER_OBJ_OUTGOING_LINKS = 0x20, //!< Links supporting outgoing maps.
    MAPPER_OBJ_LINKS          = 0x30, //!< All links.
    MAPPER_OBJ_INCOMING_MAPS  = 0x40, //!< Incoming maps.
    MAPPER_OBJ_OUTGOING_MAPS  = 0x80, //!< Outgoing maps.
    MAPPER_OBJ_MAPS           = 0xC0, //!< All maps.
    MAPPER_OBJ_ALL            = 0xFF  //!< All objects: devices, signals, maps.
} mapper_object_type;

/*! A 64-bit data structure containing an NTP-compatible time tag, as
 *  used by OSC. */
typedef lo_timetag mapper_timetag_t;
typedef mapper_timetag_t *mapper_timetag;

/*! This data structure must be large enough to hold a system pointer or a
 *  uin64_t */
typedef uint64_t mapper_id;

/*! Possible operations for composing database queries. */
typedef enum {
    MAPPER_OP_UNDEFINED = -1,
    MAPPER_OP_DOES_NOT_EXIST,           //!< Property does not exist.
    MAPPER_OP_EQUAL,                    //!< Property value == query value.
    MAPPER_OP_EXISTS,                   //!< Property exists for this entity.
    MAPPER_OP_GREATER_THAN,             //!< Property value > query value.
    MAPPER_OP_GREATER_THAN_OR_EQUAL,    //!< Property value >= query value
    MAPPER_OP_LESS_THAN,                //!< Property value < query value
    MAPPER_OP_LESS_THAN_OR_EQUAL,       //!< Property value <= query value
    MAPPER_OP_NOT_EQUAL,                //!< Property value != query value
    NUM_MAPPER_OPS
} mapper_op;

/*! Describes what happens when the range boundaries are exceeded.
 *  @ingroup map */
typedef enum {
    MAPPER_BOUND_UNDEFINED, //!< Not yet defined.
    MAPPER_BOUND_NONE,      //!< Value is passed through unchanged (default).
    MAPPER_BOUND_MUTE,      //!< Value is muted if it exceeds the range boundary.
    MAPPER_BOUND_CLAMP,     //!< Value is limited to the range boundary.
    MAPPER_BOUND_FOLD,      //!< Value continues in opposite direction.
    MAPPER_BOUND_WRAP,      /*!< Value appears as modulus offset at the opposite
                             *   boundary. */
    NUM_MAPPER_BOUNDARY_ACTIONS
} mapper_boundary_action;

/*! Describes the map modes.
 *  @ingroup map */
typedef enum {
    MAPPER_MODE_UNDEFINED,  //!< Mode is not yet defined
    MAPPER_MODE_RAW,        //!< No type coercion or vector trucation/padding
    MAPPER_MODE_LINEAR,     //!< Linear scaling
    MAPPER_MODE_EXPRESSION, //!< Expression
    NUM_MAPPER_MODES
} mapper_mode;

/*! Describes the possible endpoints of a map.
 *  @ingroup map */
typedef enum {
    MAPPER_LOC_UNDEFINED    = 0x00, //!< Not yet defined
    MAPPER_LOC_SOURCE       = 0x01, //!< Source signal(s) for this map.
    MAPPER_LOC_DESTINATION  = 0x02, //!< Destination signal(s) for this map.
    MAPPER_LOC_ANY          = 0x03  //!< Either source or destination signals.
} mapper_location;

/*! Describes the possible network protocol for map communication
 *  @ingroup map */
typedef enum {
    MAPPER_PROTO_UNDEFINED,             //!< Not yet defined
    MAPPER_PROTO_UDP,                   //!< Map updates are sent using UDP.
    MAPPER_PROTO_TCP,                   //!< Map updates are sent using TCP.
    NUM_MAPPER_PROTOCOLS
} mapper_protocol;

/*! The set of possible directions for a signal or mapping slot.
 *  @ingroup map */
typedef enum {
    MAPPER_DIR_UNDEFINED = 0x00,    //!< Not yet defined.
    MAPPER_DIR_INCOMING  = 0x01,    //!< Incoming: signal or slot is an input
    MAPPER_DIR_OUTGOING  = 0x02,    //!< Outgoing: signal or slot is an output
    MAPPER_DIR_ANY       = 0x03,    //!< Either incoming or outgoing
    MAPPER_DIR_BOTH      = 0x04,    /*!< Both directions apply.  Currently
                                     *   signals and slots cannot be both inputs
                                     *   and outputs, so this value is only used
                                     *   for querying device maps that touch
                                     *   only local signals. */
} mapper_direction;

/*! The set of possible actions on an instance, used to register callbacks
 *  to inform them of what is happening. */
typedef enum {
    MAPPER_NEW_INSTANCE       = 0x01, //!< New instance has been created.
    MAPPER_UPSTREAM_RELEASE   = 0x02, //!< Instance was released upstream.
    MAPPER_DOWNSTREAM_RELEASE = 0x04, //!< Instance was released downstream.
    MAPPER_INSTANCE_OVERFLOW  = 0x08, //!< No local instances left.
    MAPPER_INSTANCE_ALL       = 0x0F,
} mapper_instance_event;

/*! Describes the voice-stealing mode for instances.
 *  @ingroup map */
typedef enum {
    MAPPER_NO_STEALING,     //!< No stealing will take place.
    MAPPER_STEAL_OLDEST,    //!< Steal the oldest instance.
    MAPPER_STEAL_NEWEST,    //!< Steal the newest instance.
} mapper_instance_stealing_type;

/*! The set of possible events for a database record, used to inform callbacks
 *  of what is happening to a record.
 *  @ingroup database */
typedef enum {
    MAPPER_ADDED,           //!< New record has been added to the database.
    MAPPER_MODIFIED,        //!< The existing record has been modified.
    MAPPER_REMOVED,         //!< The existing record has been removed.
    MAPPER_EXPIRED,         /*!< The database has lost contact with the remote
                             *   entity. */
} mapper_record_event;

#ifdef __cplusplus
}
#endif

#endif // __MAPPER_CONSTANTS_H__
