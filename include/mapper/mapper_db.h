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
    int num_links_in;           //!< Number of associated incoming links.
    int num_links_out;          //!< Number of associated outgoing links.
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
#define CONNECTION_RANGE_SRC_MIN  0x01
#define CONNECTION_RANGE_SRC_MAX  0x02
#define CONNECTION_RANGE_DEST_MIN 0x04
#define CONNECTION_RANGE_DEST_MAX 0x08
#define CONNECTION_RANGE_KNOWN    0x0F

/* Bit flags to identify which fields in a mapper_db_connection
 * structure are valid.  This is only used when specifying connection
 * properties via the mapper_monitor_connect() or
 * mapper_monitor_connection_modify() functions. Should be combined with the
 * above range bitflags. */
#define CONNECTION_BOUND_MIN        0x00010
#define CONNECTION_BOUND_MAX        0x00020
#define CONNECTION_EXPRESSION       0x00040
#define CONNECTION_MODE             0x00080
#define CONNECTION_MUTED            0x00100
#define CONNECTION_SEND_AS_INSTANCE 0x00200
#define CONNECTION_SRC_TYPE         0x00400
#define CONNECTION_DEST_TYPE        0x00800
#define CONNECTION_SRC_LENGTH       0x01000
#define CONNECTION_DEST_LENGTH      0x02000
#define CONNECTION_NUM_SCOPES       0x04000
#define CONNECTION_SCOPE_NAMES      0x0C000  // need to know num_scopes also
#define CONNECTION_SCOPE_HASHES     0x14000 // need to know num_scopes also
#define CONNECTION_ALL              0xFFFFF

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

/*! Describes the connection mode.
 *  @ingroup connectiondb */
typedef enum _mapper_mode_type {
    MO_UNDEFINED,    //!< Not yet defined
    MO_BYPASS,       //!< Direct throughput
    MO_LINEAR,       //!< Linear scaling
    MO_EXPRESSION,   //!< Expression
    MO_CALIBRATE,    //!< Calibrate to source signal
    MO_REVERSE,      //!< Update source on dest change
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

/*! A record that describes the properties of a connection mapping.
 *  @ingroup connectiondb */
typedef struct _mapper_db_connection {
    int id;                     //!< Connection index
    char *src_name;             //!< Source signal name (OSC path).
    char *dest_name;            //!< Destination signal name (OSC path).
    char *query_name;           //!< Used for sending queries/responses.

    char src_type;              //!< Source signal type.
    char dest_type;             //!< Destination signal type.

    int src_length;             //!< Source signal length.
    int dest_length;            //!< Destination signal length.
    int src_history_size;       //!< Source history size.
    int dest_history_size;      //!< Destination history size.

    mapper_boundary_action bound_max; /*!< Operation for exceeded
                                       *   upper boundary. */
    mapper_boundary_action bound_min; /*!< Operation for exceeded
                                       *   lower boundary. */

    int send_as_instance;       //!< 1 to send as instance, 0 otherwise.

    void *src_min;              //!< Array of source minima.
    void *src_max;              //!< Array of source maxima.
    void *dest_min;             //!< Array of destination minima.
    void *dest_max;             //!< Array of destination maxima.
    int range_known;            /*!< Bitfield identifying which range
                                 *   extremities are known. */

    char *expression;

    mapper_mode_type mode;      /*!< Bypass, linear, calibrate, or
                                 *   expression connection */
    int muted;                  /*!< 1 to mute mapping connection, 0
                                 *   to unmute */

    struct _mapper_connection_scope scope;

    /*! Extra properties associated with this connection. */
    struct _mapper_string_table *extra;
} mapper_db_connection_t, *mapper_db_connection;

/*! A structure that stores the current and historical values and timetags
 *  of a signal. The size of the history arrays is determined by the needs
 *  of connection expressions.
 *  @ingroup signals */

typedef struct _mapper_signal_history
{
    /*! The type of this signal, specified as an OSC type
     *  character. */
    char type;

    /*! Current position in the circular buffer. */
    int position;

    /*! History size of the buffer. */
    int size;

    /*! Vector length. */
    int length;

    /*! Value of the signal for each sample of stored history. */
    void *value;

    /*! Timetag for each sample of stored history. */
    mapper_timetag_t *timetag;
} mapper_signal_history_t;

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

/*! A record that describes the properties of a link between devices.
 *  @ingroup linkdb */
typedef struct _mapper_db_link {
    char *src_name;                 //!< Source device name (OSC path).
    uint32_t src_name_hash;         //!< CRC-32 hash of src device name.
    char *dest_name;                //!< Destination device name (OSC path).
    uint32_t dest_name_hash;        //!< CRC-32 hash of dest device name.
    char *src_host;                 //!< IP Address of the source device.
    int src_port;                   //!< Network port of source device.
    char *dest_host;                //!< IP Address of the destination device.
    int dest_port;                  //!< Network port of destination device.

    /*! Extra properties associated with this link. */
    struct _mapper_string_table *extra;
} mapper_db_link_t, *mapper_db_link;

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
