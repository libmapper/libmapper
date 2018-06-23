#ifndef __MAPPER_CONSTANTS_H__
#define __MAPPER_CONSTANTS_H__

#ifdef __cplusplus
extern "C" {
#endif

/*! This file defines structs used to return information from the network. */

#include <lo/lo.h>

#define MAPPER_NOW ((mapper_time_t){0L,1L})

typedef enum {
    MAPPER_INT32    = 'i',
    MAPPER_INT64    = 'h',
    MAPPER_FLOAT    = 'f',
    MAPPER_DOUBLE   = 'd',
    MAPPER_STRING   = 's',
    MAPPER_BOOL     = 'b',
    MAPPER_TIME     = 't',
    MAPPER_CHAR     = 'c',
    MAPPER_PTR      = 'v',
    MAPPER_DEVICE   = 'D',
    MAPPER_SIGNAL   = 'Z',
    MAPPER_LINK     = 'L',
    MAPPER_MAP      = 'M',
    MAPPER_NULL     = 'N'
} mapper_data_type;

typedef char mapper_type;

/*! Symbolic representation of recognized properties. */
typedef enum {
    MAPPER_PROP_UNKNOWN             = 0x0000,
    MAPPER_PROP_CALIB               = 0x0100,
    MAPPER_PROP_DEVICE              = 0x0200,
    MAPPER_PROP_DIR                 = 0x0300,
    MAPPER_PROP_EXPR                = 0x0400,
    MAPPER_PROP_HOST                = 0x0500,
    MAPPER_PROP_ID                  = 0x0600,
    MAPPER_PROP_INSTANCE            = 0x0700,
    MAPPER_PROP_IS_LOCAL            = 0x0800,
    MAPPER_PROP_JITTER              = 0x0900,
    MAPPER_PROP_LENGTH              = 0x0A00,
    MAPPER_PROP_LIB_VERSION         = 0x0B00,
    MAPPER_PROP_MAX                 = 0x0C00,
    MAPPER_PROP_MIN                 = 0x0D00,
    MAPPER_PROP_MUTED               = 0x0E00,
    MAPPER_PROP_NAME                = 0x0F00,
    MAPPER_PROP_NUM_INPUTS          = 0x1000,
    MAPPER_PROP_NUM_INSTANCES       = 0x1100,
    MAPPER_PROP_NUM_MAPS            = 0x1200,
    MAPPER_PROP_NUM_MAPS_IN         = 0x1300,
    MAPPER_PROP_NUM_MAPS_OUT        = 0x1400,
    MAPPER_PROP_NUM_OUTPUTS         = 0x1500,
    MAPPER_PROP_ORDINAL             = 0x1600,
    MAPPER_PROP_PERIOD              = 0x1700,
    MAPPER_PROP_PORT                = 0x1800,
    MAPPER_PROP_PROCESS_LOC         = 0x1900,
    MAPPER_PROP_PROTOCOL            = 0x1A00,
    MAPPER_PROP_RATE                = 0x1B00,
    MAPPER_PROP_SCOPE               = 0x1C00,
    MAPPER_PROP_SIGNAL              = 0x1D00,
    MAPPER_PROP_SLOT                = 0x1E00,
    MAPPER_PROP_STATUS              = 0x1F00,
    MAPPER_PROP_SYNCED              = 0x2000,
    MAPPER_PROP_TYPE                = 0x2100,
    MAPPER_PROP_UNIT                = 0x2200,
    MAPPER_PROP_USE_INSTANCES       = 0x2300,
    MAPPER_PROP_USER_DATA           = 0x2400,
    MAPPER_PROP_VERSION             = 0x2500,
    MAPPER_PROP_EXTRA               = 0x2600,
} mapper_property;

/*! Bit flags for coordinating metadata subscriptions. */
typedef enum {
    MAPPER_OBJ_NONE           = 0x00, //!< No objects.
    MAPPER_OBJ_DEVICE         = 0x01, //!< Devices only.
    MAPPER_OBJ_INPUT_SIGNAL   = 0x02, //!< Input signals.
    MAPPER_OBJ_OUTPUT_SIGNAL  = 0x04, //!< Output signals.
    MAPPER_OBJ_SIGNAL         = 0x06, //!< All signals.
    MAPPER_OBJ_LINK           = 0x30, //!< All links.
    MAPPER_OBJ_MAP_IN         = 0x40, //!< Incoming maps.
    MAPPER_OBJ_MAP_OUT        = 0x80, //!< Outgoing maps.
    MAPPER_OBJ_MAP            = 0xC0, //!< All maps.
    MAPPER_OBJ_ALL            = 0xFF  //!< All objects: devices, signals, maps.
} mapper_object_type;

/*! A 64-bit data structure containing an NTP-compatible time tag, as
 *  used by OSC. */
typedef lo_timetag mapper_time_t;
typedef mapper_time_t *mapper_time;

/*! This data structure must be large enough to hold a system pointer or a
 *  uin64_t */
typedef uint64_t mapper_id;

/*! Possible operations for composing queries. */
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

/*! Describes the possible endpoints of a map.
 *  @ingroup map */
typedef enum {
    MAPPER_LOC_UNDEFINED    = 0x00, //!< Not yet defined
    MAPPER_LOC_SRC          = 0x01, //!< Source signal(s) for this map.
    MAPPER_LOC_DST          = 0x02, //!< Destination signal(s) for this map.
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

/*! The set of possible directions for a signal.
 *  @ingroup map */
typedef enum {
    MAPPER_DIR_UNDEFINED = 0x00,    //!< Not yet defined.
    MAPPER_DIR_IN        = 0x01,    //!< Signal is an input
    MAPPER_DIR_OUT       = 0x02,    //!< Signal is an output
    MAPPER_DIR_ANY       = 0x03,    //!< Either incoming or outgoing
    MAPPER_DIR_BOTH      = 0x04,    /*!< Both directions apply.  Currently
                                     *   signals cannot be both inputs and
                                     *   outputs, so this value is only used
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
} mapper_stealing_type;

/*! The set of possible events for a record, used to inform callbacks of what is
 *  happening to a record.
 *  @ingroup graph */
typedef enum {
    MAPPER_ADDED,           //!< New record has been added to the graph.
    MAPPER_MODIFIED,        //!< The existing record has been modified.
    MAPPER_REMOVED,         //!< The existing record has been removed.
    MAPPER_EXPIRED,         /*!< The graph has lost contact with the remote
                             *   entity. */
} mapper_record_event;

#ifdef __cplusplus
}
#endif

#endif // __MAPPER_CONSTANTS_H__
