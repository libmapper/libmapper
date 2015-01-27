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

/* Bit flags to identify which range extremities are known. If the bit
 * field is equal to RANGE_KNOWN, then all four required extremities
 * are known, and a linear connection can be calculated. */
#define CONNECTION_RANGE_SRC_MIN    0x01
#define CONNECTION_RANGE_SRC_MAX    0x02
#define CONNECTION_RANGE_DEST_MIN   0x04
#define CONNECTION_RANGE_DEST_MAX   0x08

/* Bit flags to identify which fields in a mapper_db_connection
 * structure are valid.  This is only used when specifying connection
 * properties via the mmon_connect() or
 * mmon_modify_connection() functions. Should be combined with the
 * above range bitflags. */
#define CONNECTION_BOUND_MIN        0x0010
#define CONNECTION_BOUND_MAX        0x0020
#define CONNECTION_EXPRESSION       0x0040
#define CONNECTION_MODE             0x0080
#define CONNECTION_MUTED            0x0100
#define CONNECTION_SEND_AS_INSTANCE 0x0200
#define CONNECTION_SRC_TYPE         0x0400
#define CONNECTION_DEST_TYPE        0x0800
#define CONNECTION_SRC_LENGTH       0x1000
#define CONNECTION_DEST_LENGTH      0x2000
#define CONNECTION_NUM_SCOPES       0x4000
#define CONNECTION_SCOPE_NAMES      0xC000  // need to know num_scopes also
#define CONNECTION_SCOPE_HASHES     0x14000 // need to know num_scopes also
#define CONNECTION_SLOT             0x20000
#define CONNECTION_ALL              0xFFFFF

// For range info to be known we also need to know data types and lengths
#define CONNECTION_RANGE_SRC_MIN_KNOWN  (  CONNECTION_RANGE_SRC_MIN     \
                                         | CONNECTION_SRC_TYPE          \
                                         | CONNECTION_SRC_LENGTH )
#define CONNECTION_RANGE_SRC_MAX_KNOWN  (  CONNECTION_RANGE_SRC_MAX     \
                                         | CONNECTION_SRC_TYPE          \
                                         | CONNECTION_SRC_LENGTH )
#define CONNECTION_RANGE_DEST_MIN_KNOWN (  CONNECTION_RANGE_DEST_MIN   \
                                         | CONNECTION_DEST_TYPE        \
                                         | CONNECTION_DEST_LENGTH )
#define CONNECTION_RANGE_DEST_MAX_KNOWN (  CONNECTION_RANGE_DEST_MAX   \
                                         | CONNECTION_DEST_TYPE        \
                                         | CONNECTION_DEST_LENGTH )
#define CONNECTION_RANGE_KNOWN          (  CONNECTION_RANGE_SRC_MIN_KNOWN   \
                                         | CONNECTION_RANGE_SRC_MAX_KNOWN   \
                                         | CONNECTION_RANGE_DEST_MIN_KNOWN  \
                                         | CONNECTION_RANGE_DEST_MAX_KNOWN )

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
    int size;                           //!< The number of connection scopes.
    uint32_t *hashes;                   //!< Array of connection scope hashes.
    char **names;                       //!< Array of connection scope names.
} mapper_connection_scope_t, *mapper_connection_scope;

/*! A record that describes properties of a signal.
 *  @ingroup signaldb */
typedef struct _mapper_db_signal
{
    /*! Signal index */
    int id;

	/*! Flag to indicate whether signal is source or destination */
	int is_output;

    /*! The type of this signal, specified as an OSC type
     *  character. */
    char type;

    /*! Length of the signal vector, or 1 for scalars. */
    int length;

    /*! Number of instances. */
    int num_instances;

    /*! The name of this signal, an OSC path.  Must start with '/'. */
    char *name;

    /*! The name of the device owning this signal. An OSC path.
     *  Must start with '/'. */
    char *device_name;

    /*! The unit of this signal, or NULL for N/A. */
    char *unit;

    /*! The minimum of this signal, or NULL for no minimum. */
    void *minimum;

    /*! The maximum of this signal, or NULL for no maximum. */
    void *maximum;

    /*! The rate of this signal, or 0 for non-periodic signals. */
    float rate;

    /*! Extra properties associated with this signal. */
    struct _mapper_string_table *extra;

    /*! A pointer available for associating user context. */
    void *user_data;
} mapper_db_signal_t, *mapper_db_signal;

typedef struct _mapper_db_connection_slot_props {
    mapper_db_signal signal;
    char *name;
    char *signal_name;              //!< offset into name property.
    char type;
    int length;
    float rate;
    int slot;                       //!< Destination signal slot.
    void *minimum;                  //!< Array of minima.
    void *maximum;                  //!< Array of maxima.
    int cause_update;
    int num_instances;
    int free_vars;
} mapper_db_connection_slot_props_t, *mapper_db_connection_slot_props;

/*! A record that describes the properties of a connection mapping.
 *  @ingroup connectiondb */
typedef struct _mapper_db_connection {
    int id;                         //!< Connection index
    int num_instances;
    int num_sources;
    mapper_db_connection_slot_props *sources;
    mapper_db_connection_slot_props dest;

    mapper_boundary_action bound_max; /*!< Operation for exceeded
                                       *   upper boundary. */
    mapper_boundary_action bound_min; /*!< Operation for exceeded
                                       *   lower boundary. */

    int send_as_instance;           //!< 1 to send as instance, 0 otherwise.

    char *expression;

    mapper_mode_type mode;          /*!< Bypass, linear, calibrate, or
                                     *   expression connection */
    int muted;                      /*!< 1 to mute mapping connection, 0
                                     *   to unmute */

    int direction;                  //!< DI_INCOMING or DI_OUTGOING

    struct _mapper_connection_scope scope;

    /*! Extra properties associated with this connection. */
    struct _mapper_string_table *extra;
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
