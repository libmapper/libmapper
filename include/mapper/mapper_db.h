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
    char *identifier;           /*!< The identifier (prefix) for
                                 *   this device. */
    char *name;                 /*!< The full name for this
                                 *   device, or zero. */
    char *description;
    int ordinal;
    uint32_t name_hash;         /*!< CRC-32 hash of full device name
                                 *   in the form <name>.<ordinal> */
    char *host;                 //!< Device network host name.
    int port;                   //!< Device network port.
    int num_inputs;             //!< Number of associated input signals.
    int num_outputs;            //!< Number of associated output signals.
    int num_connections_in;     //!< Number of associated incoming connections.
    int num_connections_out;    //!< Number of associated outgoing connections.
    int version;                //!< Reported device state version.
    char *lib_version;          //!< libmapper version of device.
    void* user_data;            //!< User modifiable data.

    mapper_timetag_t timetag;
    mapper_timetag_t synced; //!< Timestamp of last sync.

    /*! Extra properties associated with this device. */
    struct _mapper_string_table *extra;
} mapper_db_device_t, *mapper_db_device;

/* Bit flags to identify which range extremities are known. If the bit field is
 * equal to RANGE_KNOWN, then all four required extremities are known, and a
 * linear connection can be calculated. */
#define CONNECTION_SLOT_MIN              0x01
#define CONNECTION_SLOT_MAX              0x02
#define CONNECTION_SLOT_TYPE             0x04
#define CONNECTION_SLOT_LENGTH           0x08
#define CONNECTION_SLOT_CAUSE_UPDATE     0x10
#define CONNECTION_SLOT_SEND_AS_INSTANCE 0x20

// For range info to be known we also need to know data types and lengths
#define CONNECTION_SLOT_MIN_KNOWN   (  CONNECTION_SLOT_MIN      \
                                     | CONNECTION_SLOT_TYPE     \
                                     | CONNECTION_SLOT_LENGTH )
#define CONNECTION_SLOT_MAX_KNOWN   (  CONNECTION_SLOT_MAX      \
                                     | CONNECTION_SLOT_TYPE     \
                                     | CONNECTION_SLOT_LENGTH )
#define CONNECTION_SLOT_RANGE_KNOWN (  CONNECTION_SLOT_MIN_KNOWN\
                                     | CONNECTION_SLOT_MAX_KNOWN)

/* Bit flags to identify which fields in a mapper_db_connection structure are
 * valid.  This is only used when specifying connection properties via the
 * mmon_connect() or mmon_modify_connection() functions. */
#define CONNECTION_BOUND_MIN        0x001
#define CONNECTION_BOUND_MAX        0x002
#define CONNECTION_EXPRESSION       0x004
#define CONNECTION_MODE             0x008
#define CONNECTION_MUTED            0x010
#define CONNECTION_NUM_SCOPES       0x020
#define CONNECTION_SCOPE_NAMES      0x060 // need to know num_scopes also
#define CONNECTION_SCOPE_HASHES     0x090 // need to know num_scopes also
#define CONNECTION_SLOT             0x100
#define CONNECTION_ALL              0xFFF

/*! Describes what happens when the range boundaries are
 *  exceeded.
 *  @ingroup connectiondb */
typedef enum _mapper_boundary_action {
    BA_NONE,    /*!< Value is passed through unchanged. This is the
                 *   default. */
    BA_MUTE,    //!< Value is muted.
    BA_CLAMP,   //!< Value is limited to the boundary.
    BA_FOLD,    //!< Value continues in opposite direction.
    BA_WRAP,    /*!< Value appears as modulus offset at the opposite
                 *   boundary. */
    N_MAPPER_BOUNDARY_ACTIONS
} mapper_boundary_action;

/*! Describes the connection modes.
 *  @ingroup connectiondb */
typedef enum _mapper_mode_type {
    MO_UNDEFINED,    //!< Not yet defined
    MO_NONE,         //!< No mode
    MO_RAW,          //!< No type coercion
    MO_LINEAR,       //!< Linear scaling
    MO_EXPRESSION,   //!< Expression
    N_MAPPER_MODE_TYPES
} mapper_mode_type;

/*! Describes the voice-stealing mode for instances.
 *  @ingroup connectiondb */
typedef enum _mapper_instance_allocation_type {
    IN_UNDEFINED,    //!< Not yet defined
    IN_STEAL_OLDEST, //!< Steal the oldest instance
    IN_STEAL_NEWEST, //!< Steal the newest instance
    N_MAPPER_INSTANCE_ALLOCATION_TYPES
} mapper_instance_allocation_type;

typedef struct _mapper_connection_scope {
    uint32_t *hashes;                   //!< Array of connection scope hashes.
    char **names;                       //!< Array of connection scope names.
    int size;                           //!< The number of connection scopes.
} mapper_connection_scope_t, *mapper_connection_scope;

/*! A record that describes properties of a signal.
 *  @ingroup signaldb */
typedef struct _mapper_db_signal {
    mapper_db_device device;
    char *name;             /*! The name of this signal, an OSC path.  Must
                             *  start with '/'. */

    char *unit;             //!< The unit of this signal, or NULL for N/A.
    char *description;
    void *minimum;          //!< The minimum of this signal, or NULL for N/A.
    void *maximum;          //!< The maximum of this signal, or NULL for N/A.

    struct _mapper_string_table *extra; /*! Extra properties associated with
                                         *  this signal. */

    void *user_data;        //!< A pointer available for associating user context.

    float rate;             //!< The update rate, or 0 for non-periodic signals.
    int id;                 //!< Signal index.
    int direction;       	//!< DI_OUTGOING / DI_INCOMING / DI_BOTH
    int length;             //!< Length of the signal vector, or 1 for scalars.
    int num_instances;      //!< Number of instances.
    char type;              /*! The type of this signal, specified as an OSC type
                             *  character. */
} mapper_db_signal_t, *mapper_db_signal;

typedef struct _mapper_db_connection_slot {
    mapper_db_signal signal;
    mapper_db_device device;
    const char *signal_name;
    const char *device_name;
    void *minimum;                  //!< Array of minima.
    void *maximum;                  //!< Array of maxima.
    int slot_id;                    //!< Slot ID
    int length;
    int num_instances;
    int flags;
    int direction;                  //!< DI_INCOMING or DI_OUTGOING
    int cause_update;               //!< 1 to cause update, 0 otherwise.
    int send_as_instance;           //!< 1 to send as instance, 0 otherwise.
    char type;
} mapper_db_connection_slot_t, *mapper_db_connection_slot;

/*! A record that describes the properties of a connection mapping.
 *  @ingroup connectiondb */
typedef struct _mapper_db_connection {
    mapper_db_connection_slot sources;
    mapper_db_connection_slot_t destination;

    struct _mapper_connection_scope scope;

    /*! Extra properties associated with this connection. */
    struct _mapper_string_table *extra;

    char *expression;

    mapper_boundary_action bound_max;   /*!< Operation for exceeded
                                         *   upper boundary. */
    mapper_boundary_action bound_min;   /*!< Operation for exceeded
                                         *   lower boundary. */
    mapper_mode_type mode;              /*!< Bypass, linear, calibrate, or
                                         *   expression connection */
    int muted;                          /*!< 1 to mute mapping connection, 0
                                         *   to unmute */
    int calibrating;
    int id;                             //!< Connection index
    int num_sources;
    int flags;
} mapper_db_connection_t, *mapper_db_connection;

typedef struct _mapper_db_batch_request
{
    // pointer to monitor
    struct _mapper_monitor *monitor;
    // pointer to device
    struct _mapper_db_device *device;
    // current signal index
    int index;
    // total signal count
    int total_count;
    int batch_size;
    int direction;
} mapper_db_batch_request_t, *mapper_db_batch_request;

#ifdef __cplusplus
}
#endif

#endif // __MAPPER_DB_H__
