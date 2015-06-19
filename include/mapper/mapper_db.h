#ifndef __MAPPER_DB_H__
#define __MAPPER_DB_H__

#ifdef __cplusplus
extern "C" {
#endif

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

/*! A record that keeps information about a device on the network.
 *  @ingroup devicedb */
typedef struct _mapper_db_device {
    char *lib_version;          //!< libmapper version of device.
    void *user_data;            //!< User modifiable data.

    char *identifier;           //!< The identifier (prefix) for this device.
    char *name;                 //!< The full name for this device, or zero.
    char *description;

    /*! Extra properties associated with this device. */
    struct _mapper_string_table *extra;

    uint64_t id;                //!< Unique id identifying this device.
    char *host;                 //!< Device network host name.

    mapper_timetag_t timetag;
    mapper_timetag_t synced; //!< Timestamp of last sync.

    int ordinal;
    int port;                   //!< Device network port.
    int num_inputs;             //!< Number of associated input signals.
    int num_outputs;            //!< Number of associated output signals.
    int num_incoming_maps;      //!< Number of associated incoming maps.
    int num_outgoing_maps;      //!< Number of associated outgoing maps.
    int version;                //!< Reported device state version.

    int subscribed;
} mapper_db_device_t, *mapper_db_device;

/* Bit flags to identify which fields in a mapper_db_map structure or
 * a mapper_db_map_slot are valid.  This is only used when specifying
 * map properties via the mmon_map() or mmon_modify_map() functions. */

#define MAP_EXPRESSION              0x0001
#define MAP_MODE                    0x0002
#define MAP_MUTED                   0x0004
#define MAP_NUM_SCOPES              0x0008
#define MAP_SCOPE_NAMES             0x0018 // need to know num_scopes also
#define MAP_SCOPE_HASHES            0x0028 // need to know num_scopes also

#define MAP_SLOT_BOUND_MAX          0x0040
#define MAP_SLOT_BOUND_MIN          0x0080
#define MAP_SLOT_CALIBRATING        0x0100
#define MAP_SLOT_CAUSE_UPDATE       0x0200
#define MAP_SLOT_LENGTH             0x0400
#define MAP_SLOT_MAX                0x0800
#define MAP_SLOT_MIN                0x1000
#define MAP_SLOT_TYPE               0x2000
#define MAP_SLOT_SEND_AS_INSTANCE   0x4000

// For range info to be known we also need to know data types and lengths
#define MAP_SLOT_MIN_KNOWN      (MAP_SLOT_MIN | MAP_SLOT_TYPE | MAP_SLOT_LENGTH)
#define MAP_SLOT_MAX_KNOWN      (MAP_SLOT_MAX | MAP_SLOT_TYPE | MAP_SLOT_LENGTH)
#define MAP_SLOT_RANGE_KNOWN    (MAP_SLOT_MIN_KNOWN | MAP_SLOT_MAX_KNOWN)

/*! Possible operations for composing db queries. */
typedef enum _mapper_db_op {
    OP_UNDEFINED = -1,
    OP_EQUAL,
    OP_GREATER_THAN,
    OP_GREATER_THAN_OR_EQUAL,
    OP_LESS_THAN,
    OP_LESS_THAN_OR_EQUAL,
    OP_NOT_EQUAL,
    OP_UNION,
    OP_INTERSECTION,
    OP_DIFFERENCE,
    N_MAPPER_DB_OPS
} mapper_db_op;

/*! Describes what happens when the range boundaries are exceeded.
 *  @ingroup mapdb */
typedef enum _mapper_boundary_action {
    BA_UNDEFINED,
    BA_NONE,    //!< Value is passed through unchanged. This is the default.
    BA_MUTE,    //!< Value is muted.
    BA_CLAMP,   //!< Value is limited to the boundary.
    BA_FOLD,    //!< Value continues in opposite direction.
    BA_WRAP,    //!< Value appears as modulus offset at the opposite boundary.
    N_MAPPER_BOUNDARY_ACTIONS
} mapper_boundary_action;

/*! Describes the map modes.
 *  @ingroup mapdb */
typedef enum _mapper_mode_type {
    MO_UNDEFINED,       //!< Not yet defined
    MO_RAW,             //!< No type coercion
    MO_LINEAR,          //!< Linear scaling
    MO_EXPRESSION,      //!< Expression
    N_MAPPER_MODE_TYPES
} mapper_mode_type;

/*! Describes the voice-stealing mode for instances.
 *  @ingroup mapdb */
typedef enum _mapper_instance_allocation_type {
    IN_UNDEFINED,       //!< Not yet defined
    IN_STEAL_OLDEST,    //!< Steal the oldest instance
    IN_STEAL_NEWEST,    //!< Steal the newest instance
    N_MAPPER_INSTANCE_ALLOCATION_TYPES
} mapper_instance_allocation_type;

typedef struct _mapper_map_scope {
    uint32_t *hashes;   //!< Array of map scope hashes.
    char **names;       //!< Array of map scope names.
    int size;           //!< The number of map scopes.
} mapper_map_scope_t, *mapper_map_scope;

/*! A record that describes properties of a signal.
 *  @ingroup signaldb */
typedef struct _mapper_db_signal {
    mapper_db_device device;
    char *path;         //! OSC path.  Must start with '/'.
    char *name;         //! The name of this signal (path+1).
    uint64_t id;        //!< Unique id identifying this signal.

    char *unit;         //!< The unit of this signal, or NULL for N/A.
    char *description;  //!< Description of this signal, or NULL for N/A.
    void *minimum;      //!< The minimum of this signal, or NULL for N/A.
    void *maximum;      //!< The maximum of this signal, or NULL for N/A.

    struct _mapper_string_table *extra; /*! Extra properties associated with
                                         *  this signal. */

    void *user_data;    //!< A pointer available for associating user context.

    float rate;         //!< The update rate, or 0 for non-periodic signals.
    int direction;      //!< DI_OUTGOING / DI_INCOMING / DI_BOTH
    int length;         //!< Length of the signal vector, or 1 for scalars.
    int num_instances;  //!< Number of instances.
    char type;              /*! The type of this signal, specified as an OSC type
                             *  character. */
} mapper_db_signal_t, *mapper_db_signal;

typedef struct _mapper_db_map_slot {
    mapper_db_signal signal;
    void *minimum;                      //!< Array of minima, or NULL for N/A.
    void *maximum;                      //!< Array of maxima, or NULL for N/A.
    int slot_id;                        //!< Slot ID
    int length;
    int num_instances;
    int flags;
    int direction;                      //!< DI_INCOMING or DI_OUTGOING
    int cause_update;                   //!< 1 to cause update, 0 otherwise.
    int send_as_instance;               //!< 1 to send as instance, 0 otherwise.

    mapper_boundary_action bound_max;   //!< Operation for exceeded upper bound.
    mapper_boundary_action bound_min;   //!< Operation for exceeded lower bound.
    int calibrating;                    //!< 1 if calibrating, 0 otherwise
    char type;
} mapper_db_map_slot_t, *mapper_db_map_slot;

/*! A record that describes the properties of a mapping.
 *  @ingroup mapdb */
typedef struct _mapper_db_map {
    mapper_db_map_slot sources;
    mapper_db_map_slot_t destination;
    uint64_t id;                        //!< Unique id identifying this map

    struct _mapper_map_scope scope;

    /*! Extra properties associated with this map. */
    struct _mapper_string_table *extra;

    char *expression;

    mapper_mode_type mode;              //!< MO_LINEAR or MO_EXPRESSION
    int muted;                          //!< 1 to mute mapping, 0 to unmute
    int num_sources;
    int process_location;               //!< 1 for source, 0 for destination
    int flags;
} mapper_db_map_t, *mapper_db_map;

#ifdef __cplusplus
}
#endif

#endif // __MAPPER_DB_H__
