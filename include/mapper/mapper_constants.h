#ifndef __MPR_CONSTANTS_H__
#define __MPR_CONSTANTS_H__

#ifdef __cplusplus
extern "C" {
#endif

/*! Describes the possible data types used by libmapper. */
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
    MPR_OP_BAND         = 0x09, /*!< Property value & query value (bitwise AND) */
    MPR_OP_BOR          = 0x0A, /*!< Property value | query value (bitwise OR) */
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
    MPR_LOC_ANY         = 0x03, /*!< Either source or destination signals. */
    MPR_LOC_BOTH        = 0x07  /*!< Both source and destination signals. */
} mpr_loc;

/*! Describes the possible network protocol for map communication.
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
    MPR_DIR_BOTH        = 0x07  /*!< Both directions apply.  Currently signals
                                 *   cannot be both inputs and outputs, so this
                                 *   value is only used for querying device maps
                                 *   that touch only local signals. */
} mpr_dir;

/*! Describes the voice-stealing mode for instances.
 *  @ingroup map */
typedef enum {
    MPR_STEAL_NONE,     /*!< No stealing will take place. */
    MPR_STEAL_OLDEST,   /*!< Steal the oldest instance. */
    MPR_STEAL_NEWEST    /*!< Steal the newest instance. */
} mpr_steal_type;

/*! Describes the possible statuses for a libmapper object.
 *  @ingroup object */

/* Status flag usage      | mpr_obj | mpr_dev | mpr_sig | instance | sig cb  | mpr_map | graph cb |
 | MPR_STATUS_EXPIRED     |    X    |    X    |    X    |          |         |    X    |    X     |
 | MPR_STATUS_REL_DNSTRM  |         |         |         |    X     |    X    |         |          |
 | MPR_STATUS_NEW         |    X    |    X    |    X    |    X     |    X    |    X    |    X     |
 | MPR_STATUS_MODIFIED    |    X    |    X    |    X    |          |         |    X    |    X     |
 | MPR_STATUS_RESERVED    |         |         |         |    X     |         |         |          |
 | MPR_STATUS_STAGED      |         |    X    |         |          |         |    X    |          |
 | MPR_STATUS_HAS_VALUE   |         |         |    X    |    X     |         |         |          |
 | MPR_STATUS_ACTIVE      |         |         |         |    X     |         |    X    |          |
 | MPR_STATUS_OVERFLOW    |         |         |    X    |          |    X    |         |          |
 | MPR_STATUS_REMOVED     |    X    |    X    |    X    |          |         |    X    |    X     |
 | MPR_STATUS_REL_UPSTRM  |         |         |         |    X     |    X    |         |          |
 | MPR_STATUS_READY       |         |    X    |         |          |         |    X    |          |
 | MPR_STATUS_UPDATE_LOC  |         |         |    X    |    X     |    X    |         |          |
 | MPR_STATUS_UPDATE_REM  |         |         |    X    |    X     |    X    |         |          |
*/

typedef enum {
    MPR_STATUS_UNDEFINED    = 0x0000,

    MPR_STATUS_NEW          = 0x0001,   /*!< New object. */
    MPR_STATUS_MODIFIED     = 0x0002,   /*!< Existing object has been modified. */
    MPR_STATUS_REMOVED      = 0x0004,   /*!< Existing object has been removed. */
    MPR_STATUS_EXPIRED      = 0x0008,   /*!< The graph has lost contact with the remote entity. */

    MPR_STATUS_STAGED       = 0x0010,   /*!< Object has been staged and is waiting. */
    MPR_STATUS_ACTIVE       = 0x0020,   /*!< Object is active. */

    MPR_STATUS_HAS_VALUE    = 0x0040,   /*!< Signal/instance has a value. */
    MPR_STATUS_NEW_VALUE    = 0x0080,   /*!< Signal/instance value has changed since last check. */

    MPR_STATUS_UPDATE_LOC   = 0x0100,   /*!< Signal/instance value was updated locally. */
    MPR_STATUS_UPDATE_REM   = 0x0200,   /*!< Signal/instance value was updated remotely. */

    MPR_STATUS_REL_UPSTRM   = 0x0400,   /*!< Instance was released upstream. */
    MPR_STATUS_REL_DNSTRM   = 0x0800,   /*!< Signal instance was released downstream. */

    MPR_STATUS_OVERFLOW     = 0x1000,   /*!< No local objects available for activation. */

    MPR_STATUS_ANY          = 0x1FFF
} mpr_status;

/* deprecated constants */
/*! The set of possible signal events, used to register and inform callbacks.
 *  @ingroup signal */
#define MPR_SIG_INST_NEW    MPR_STATUS_NEW          /*!< New instance has been created. */
#define MPR_SIG_REL_UPSTRM  MPR_STATUS_REL_UPSTRM   /*!< Instance was released upstream. */
#define MPR_SIG_REL_DNSTRM  MPR_STATUS_REL_DNSTRM   /*!< Instance was released downstream. */
#define MPR_SIG_INST_OFLW   MPR_STATUS_OVERFLOW     /*!< No local instances left. */
#define MPR_SIG_UPDATE      MPR_STATUS_UPDATE_REM   /*!< Instance value has been updated remotely. */
#define MPR_SIG_ALL         MPR_STATUS_ANY
#define mpr_sig_evt mpr_status

/*! The set of possible graph events, used to inform callbacks.
 *  @ingroup graph */
#define MPR_OBJ_NEW MPR_STATUS_NEW      /*!< New record has been added to the graph. */
#define MPR_OBJ_MOD MPR_STATUS_MODIFIED /*!< The existing record has been modified. */
#define MPR_OBJ_REM MPR_STATUS_REMOVED  /*!< The existing record has been removed. */
#define MPR_OBJ_EXP MPR_STATUS_EXPIRED  /*!< The graph has lost contact with the remote entity. */
#define mpr_graph_evt mpr_status

#ifdef __cplusplus
}
#endif

#endif /* __MPR_CONSTANTS_H__ */
