#ifndef __MPR_CONSTANTS_H__
#define __MPR_CONSTANTS_H__

#ifdef __cplusplus
extern "C" {
#endif

/*! This file defines structs used to return information from the network. */

#include <lo/lo.h>

/*! A 64-bit data structure containing an NTP-compatible time tag, as used by OSC. */
typedef lo_timetag mpr_time;
#define MPR_NOW LO_TT_IMMEDIATE

enum {
    MPR_DEV             = 0x01,             /*!< Devices only. */
    MPR_SIG_IN          = 0x02,             /*!< Input signals. */
    MPR_SIG_OUT         = 0x04,             /*!< Output signals. */
    MPR_SIG             = 0x06,             /*!< All signals. */
    MPR_MAP_IN          = 0x08,             /*!< Incoming maps. */
    MPR_MAP_OUT         = 0x10,             /*!< Outgoing maps. */
    MPR_MAP             = 0x18,             /*!< All maps. */
    MPR_OBJ             = 0x1F,             /*!< All objects: devs, sigs, maps. */
    MPR_LIST            = 0x40,             /*!< Object query. */
    MPR_GRAPH           = 0x41,             /*!< Graph. */
    MPR_BOOL            = 'b',  /* 0x62 */  /*!< Boolean value. */
    MPR_TYPE            = 'c',  /* 0x63 */  /*!< libmapper data type. */
    MPR_DBL             = 'd',  /* 0x64 */  /*!< 64-bit floating point. */
    MPR_FLT             = 'f',  /* 0x66 */  /*!< 32-bit floating point. */
    MPR_INT64           = 'h',  /* 0x68 */  /*!< 64-bit integer. */
    MPR_INT32           = 'i',  /* 0x69 */  /*!< 32-bit integer. */
    MPR_STR             = 's',  /* 0x73 */  /*!< String. */
    MPR_TIME            = 't',  /* 0x74 */  /*!< 64-bit NTP timestamp. */
    MPR_PTR             = 'v',  /* 0x76 */  /*!< pointer. */
    MPR_NULL            = 'N'   /* 0x4E */  /*!< NULL value. */
};

typedef char mpr_type;

/*! Symbolic representation of recognized properties. */
typedef enum {
    MPR_PROP_UNKNOWN        = 0x0000,
    MPR_PROP_BUNDLE         = 0x0100,
    MPR_PROP_DATA           = 0x0200,
    MPR_PROP_DEV            = 0x0300,
    MPR_PROP_DIR            = 0x0400,
    MPR_PROP_EPHEM          = 0x0500,
    MPR_PROP_EXPR           = 0x0600,
    MPR_PROP_HOST           = 0x0700,
    MPR_PROP_ID             = 0x0800,
    MPR_PROP_IS_LOCAL       = 0x0900,
    MPR_PROP_JITTER         = 0x0A00,
    MPR_PROP_LEN            = 0x0B00,
    MPR_PROP_LIBVER         = 0x0C00,
    MPR_PROP_LINKED         = 0x0D00,
    MPR_PROP_MAX            = 0x0E00,
    MPR_PROP_MIN            = 0x0F00,
    MPR_PROP_MUTED          = 0x1000,
    MPR_PROP_NAME           = 0x1100,
    MPR_PROP_NUM_INST       = 0x1200,
    MPR_PROP_NUM_MAPS       = 0x1300,
    MPR_PROP_NUM_MAPS_IN    = 0x1400,
    MPR_PROP_NUM_MAPS_OUT   = 0x1500,
    MPR_PROP_NUM_SIGS_IN    = 0x1600,
    MPR_PROP_NUM_SIGS_OUT   = 0x1700,
    MPR_PROP_ORDINAL        = 0x1800,
    MPR_PROP_PERIOD         = 0x1900,
    MPR_PROP_PORT           = 0x1A00,
    MPR_PROP_PROCESS_LOC    = 0x1B00,
    MPR_PROP_PROTOCOL       = 0x1C00,
    MPR_PROP_RATE           = 0x1D00,
    MPR_PROP_SCOPE          = 0x1E00,
    MPR_PROP_SIG            = 0x1F00,
    MPR_PROP_SLOT           = 0x2000,
    MPR_PROP_STATUS         = 0x2100,
    MPR_PROP_STEAL_MODE     = 0x2200,
    MPR_PROP_SYNCED         = 0x2300,
    MPR_PROP_TYPE           = 0x2400,
    MPR_PROP_UNIT           = 0x2500,
    MPR_PROP_USE_INST       = 0x2600,
    MPR_PROP_VERSION        = 0x2700,
    MPR_PROP_EXTRA          = 0x2800
} mpr_prop;

/*! This data structure must be large enough to hold a system pointer or a uin64_t */
typedef uint64_t mpr_id;

/*! Possible operations for composing queries. */
typedef enum {
    MPR_OP_UNDEFINED    = 0x00,
    MPR_OP_NEX          = 0x01, /*!< Property does not exist for this entity. */
    MPR_OP_EQ           = 0x02, /*!< Property value == query value. */
    MPR_OP_EX           = 0x03, /*!< Property exists for this entity. */
    MPR_OP_GT           = 0x04, /*!< Property value > query value. */
    MPR_OP_GTE          = 0x05, /*!< Property value >= query value */
    MPR_OP_LT           = 0x06, /*!< Property value < query value */
    MPR_OP_LTE          = 0x07, /*!< Property value <= query value */
    MPR_OP_NEQ          = 0x08, /*!< Property value != query value */
    MPR_OP_ALL          = 0x10, /*!< Applies to all elements of value */
    MPR_OP_ANY          = 0x20, /*!< Applies to any element of value */
    MPR_OP_NONE         = 0x40
} mpr_op;

/*! Describes the possible endpoints of a map.
 *  @ingroup map */
typedef enum {
    MPR_LOC_UNDEFINED   = 0x00, /*!< Not yet defined */
    MPR_LOC_SRC         = 0x01, /*!< Source signal(s) for this map. */
    MPR_LOC_DST         = 0x02, /*!< Destination signal(s) for this map. */
    MPR_LOC_ANY         = 0x03  /*!< Either source or destination signals. */
} mpr_loc;

/*! Describes the possible network protocol for map communication
 *  @ingroup map */
typedef enum {
    MPR_PROTO_UNDEFINED,        /*!< Not yet defined */
    MPR_PROTO_UDP,              /*!< Map updates are sent using UDP. */
    MPR_PROTO_TCP,              /*!< Map updates are sent using TCP. */
    MPR_NUM_PROTO
} mpr_proto;

/*! The set of possible directions for a signal.
 *  @ingroup signal */
typedef enum {
    MPR_DIR_UNDEFINED   = 0x00, /*!< Not yet defined. */
    MPR_DIR_IN          = 0x01, /*!< Signal is an input */
    MPR_DIR_OUT         = 0x02, /*!< Signal is an output */
    MPR_DIR_ANY         = 0x03, /*!< Either incoming or outgoing */
    MPR_DIR_BOTH        = 0x04  /*!< Both directions apply.  Currently signals
                                 *   cannot be both inputs and outputs, so this
                                 *   value is only used for querying device maps
                                 *   that touch only local signals. */
} mpr_dir;

/*! The set of possible signal events, used to register and inform callbacks.
 *  @ingroup signal */
typedef enum {
    MPR_SIG_INST_NEW    = 0x01, /*!< New instance has been created. */
    MPR_SIG_REL_UPSTRM  = 0x02, /*!< Instance was released upstream. */
    MPR_SIG_REL_DNSTRM  = 0x04, /*!< Instance was released downstream. */
    MPR_SIG_INST_OFLW   = 0x08, /*!< No local instances left. */
    MPR_SIG_UPDATE      = 0x10, /*!< Instance value has been updated. */
    MPR_SIG_ALL         = 0x1F
} mpr_sig_evt;

/*! Describes the voice-stealing mode for instances.
 *  @ingroup map */
typedef enum {
    MPR_STEAL_NONE,     /*!< No stealing will take place. */
    MPR_STEAL_OLDEST,   /*!< Steal the oldest instance. */
    MPR_STEAL_NEWEST    /*!< Steal the newest instance. */
} mpr_steal_type;

/*! The set of possible graph events, used to inform callbacks.
 *  @ingroup graph */
typedef enum {
    MPR_OBJ_NEW,        /*!< New record has been added to the graph. */
    MPR_OBJ_MOD,        /*!< The existing record has been modified. */
    MPR_OBJ_REM,        /*!< The existing record has been removed. */
    MPR_OBJ_EXP         /*!< The graph has lost contact with the remote entity. */
} mpr_graph_evt;

typedef enum {
    MPR_STATUS_UNDEFINED    = 0x00,
    MPR_STATUS_EXPIRED      = 0x01,
    MPR_STATUS_STAGED       = 0x02,
    MPR_STATUS_READY        = 0x3E,
    MPR_STATUS_ACTIVE       = 0x7E, /* must exclude MPR_STATUS_RESERVED */
    MPR_STATUS_RESERVED     = 0x80,
    MPR_STATUS_ALL          = 0xFF
} mpr_status;

#ifdef __cplusplus
}
#endif

#endif /* __MPR_CONSTANTS_H__ */
