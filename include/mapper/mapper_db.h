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

/*! Possible operations for composing db queries. */
typedef enum _mapper_db_query_op {
    QUERY_UNDEFINED = -1,
    QUERY_DOES_NOT_EXIST,
    QUERY_EQUAL,
    QUERY_EXISTS,
    QUERY_GREATER_THAN,
    QUERY_GREATER_THAN_OR_EQUAL,
    QUERY_LESS_THAN,
    QUERY_LESS_THAN_OR_EQUAL,
    QUERY_NOT_EQUAL,
    NUM_MAPPER_DB_QUERY_OPS
} mapper_db_query_op;

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
    LOC_SOURCE,
    LOC_DESTINATION
} mapper_location;

/*! Describes the voice-stealing mode for instances.
 *  @ingroup mapdb */
typedef enum _mapper_instance_allocation_type {
    IN_UNDEFINED,       //!< Not yet defined
    IN_STEAL_OLDEST,    //!< Steal the oldest instance
    IN_STEAL_NEWEST,    //!< Steal the newest instance
    NUM_MAPPER_INSTANCE_ALLOCATION_TYPES
} mapper_instance_allocation_type;

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

#ifdef __cplusplus
}
#endif

#endif // __MAPPER_DB_H__
