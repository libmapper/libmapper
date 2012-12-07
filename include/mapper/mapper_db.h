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
    char *name;             //!< Device name.
    char *host;             //!< Device network host name.
    int port;               //!< Device network port.
    int n_inputs;           //!< Number of associated input signals.
    int n_outputs;          //!< Number of associated output signals.
    int n_links_in;         //!< Number of associated incoming links.
    int n_links_out;        //!< Number of associated outgoing links.
    int n_connections_in;   //!< Number of associated incoming connections.
    int n_connections_out;  //!< Number of associated outgoing connections.
    int version;            //!< Reported device state version.
    void* user_data;        //!< User modifiable data.

    /*! Extra properties associated with this device. */
    struct _mapper_string_table *extra;
} mapper_db_device_t, *mapper_db_device;

/* Bit flags to identify which fields in a mapper_db_link
 * structure are valid.  This is only used when specifying link
 * properties via the mapper_monitor_link() or
 * mapper_monitor_link_modify() functions. */
#define LINK_NUM_SCOPES         0x01
#define LINK_SCOPE_NAMES        0x02
#define LINK_SCOPE_HASHES       0x04

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
#define CONNECTION_CLIP_MIN      0x0010
#define CONNECTION_CLIP_MAX      0x0020
#define CONNECTION_EXPRESSION    0x0040
#define CONNECTION_MODE          0x0080
#define CONNECTION_MUTED         0x0100
#define CONNECTION_ALL           0x01FF

/*! A structure to keep range information, with a bitfield indicating
 *  which parts of the range are known.
 *  @ingroup connectiondb */
typedef struct _mapper_connection_range {
    float src_min;              //!< Source minimum.
    float src_max;              //!< Source maximum.
    float dest_min;             //!< Destination minimum.
    float dest_max;             //!< Destination maximum.
    int known;                  /*!< Bitfield identifying which range
                                 *   extremities are known. */
} mapper_connection_range_t;

/*! Describes what happens when the clipping boundaries are
 *  exceeded.
 *  @ingroup connectiondb */
typedef enum _mapper_clipping_type {
    CT_NONE,    /*!< Value is passed through unchanged. This is the
                 *   default. */
    CT_MUTE,    //!< Value is muted.
    CT_CLAMP,   //!< Value is limited to the boundary.
    CT_FOLD,    //!< Value continues in opposite direction.
    CT_WRAP,    /*!< Value appears as modulus offset at the opposite
                 *   boundary. */
    N_MAPPER_CLIPPING_TYPES
} mapper_clipping_type;

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

    mapper_clipping_type clip_max;    /*!< Operation for exceeded
                                       *   upper boundary. */
    mapper_clipping_type clip_min;    /*!< Operation for exceeded
                                       *   lower boundary. */

    int send_as_instance;       //!< 1 to send as instance, 0 otherwise.

    mapper_connection_range_t range;  //!< Range information.
    char *expression;

    mapper_mode_type mode;      /*!< Bypass, linear, calibrate, or
                                 *   expression connection */
    int muted;                  /*!< 1 to mute mapping connection, 0
                                 *   to unmute */

    /*! Extra properties associated with this connection. */
    struct _mapper_string_table *extra;
} mapper_db_connection_t, *mapper_db_connection;

/*! A signal value may be one of several different types, so we use a
 *  union to represent this.  The appropriate selection from this
 *  union is determined by the mapper_signal::type variable.
 *  @ingroup signaldb */

typedef union _mapper_signal_value {
    float f;
    double d;
    int i32;
} mapper_signal_value_t, mval;

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
    // TODO: switch to mapper_timetag_t;
    //mapper_timetag_t *timetag;
    lo_timetag *timetag;

    struct _mapper_signal_history *next;
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

    /*! Size of the history vector. */
    int history_size;

    /*! The name of this signal, an OSC path.  Must start with '/'. */
    const char *name;

    /*! The device name of which this signal is a member. An OSC path.
     *  Must start with '/'. */
    const char *device_name;

    /*! The unit of this signal, or NULL for N/A. */
    const char *unit;

    /*! The minimum of this signal, or NULL for no minimum. */
    mapper_signal_value_t *minimum;

    /*! The maximum of this signal, or NULL for no maximum. */
    mapper_signal_value_t *maximum;

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
    char *dest_name;                //!< Destination device name (OSC path).
    lo_address src_addr;            //!< Address of the source device.
    lo_address dest_addr;           //!< Address of the destination device.
    int num_scopes;                 //!< The number of instance group scopes.
    char **scope_names;             //!< Array of instance group scopes.
    int *scope_hashes;              //!< Array of CRC-32 scope hashes

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
} mapper_db_batch_request_t, *mapper_db_batch_request;

#ifdef __cplusplus
}
#endif

#endif // __MAPPER_DB_H__
