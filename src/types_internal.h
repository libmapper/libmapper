
#ifndef __MAPPER_TYPES_H__
#define __MAPPER_TYPES_H__

#include <lo/lo_lowlevel.h>

#include "config.h"

#ifdef HAVE_ARPA_INET_H
 #include <arpa/inet.h>
#else
 #ifdef HAVE_WINSOCK2_H
  #include <winsock2.h>
 #endif
#endif

#include <mapper/mapper_db.h>

/**** Defined in mapper.h ****/

/* Types defined here replace opaque prototypes in mapper.h, thus we
 * cannot include it here.  Instead we include some prototypes here.
 * Typedefs cannot be repeated, therefore they are refered to by
 * struct name. */

struct _mapper_signal;
struct _mapper_admin;
typedef struct _mapper_expr *mapper_expr;

/* Forward declarations for this file. */

struct _mapper_admin_allocated_t;
struct _mapper_device;
struct _mapper_instance_map;

/**** String tables ****/

/*! A pair representing an arbitrary parameter value and its
 *  type. (Re-using liblo's OSC-oriented lo_arg data structure.) If
 *  type is a string, the allocated size may be longer than
 *  sizeof(mapper_osc_arg_t). */
typedef struct _mapper_osc_value {
    lo_type type;
    lo_arg value;
} mapper_osc_value_t;

/*! Used to hold string look-up table nodes. */
typedef struct {
    const char *key;
    void *value;
} string_table_node_t;

/*! Used to hold string look-up tables. */
typedef struct _mapper_string_table {
    string_table_node_t *store;
    int len;
    int alloced;
} mapper_string_table_t, *table;

/**** Admin bus ****/

/*! Function to call when an allocated resource is locked. */
typedef void mapper_admin_resource_on_lock(struct _mapper_device *md,
                                           struct _mapper_admin_allocated_t
                                           *resource);

/*! Function to call when an allocated resource encounters a collision. */
typedef void mapper_admin_resource_on_collision(struct _mapper_admin
                                                *admin);

/*! Allocated resources */
typedef struct _mapper_admin_allocated_t {
    unsigned int value;           //!< The resource to be allocated.
    int collision_count;          /*!< The number of collisions
                                   *   detected for this resource. */
    double count_time;            /*!< The last time at which the
                                   * collision count was updated. */
    int locked;                   /*!< Whether or not the value has
                                   *   been locked in (allocated). */
    double suggestion[8];         /*!< Availability of a range
                                       of resource values. */

    //!< Function to call when resource becomes locked.
    mapper_admin_resource_on_lock *on_lock;

   //! Function to call when resource collision occurs.
    mapper_admin_resource_on_collision *on_collision;
} mapper_admin_allocated_t;

/*! A structure that keeps information about a device. */
typedef struct _mapper_admin {
    char *identifier;                 /*!< The identifier (prefix) for
                                       *   this device. */
    char *name;                       /*!< The full name for this
                                       *   device, or zero. */
    mapper_admin_allocated_t ordinal; /*!< A unique ordinal for this
                                       *   device instance. */
    int name_hash;                    /*!< CRC-32 hash of full device name
                                       *   in the form <name>.<ordinal> */
    int random_id;                    /*!< Random ID for allocation
                                           speedup. */
    int port;                         /*!< This device's UDP port number. */
    lo_server_thread admin_server;    /*!< LibLo server thread for the
                                       *   admin bus. */
    lo_address admin_addr;            /*!< LibLo address for the admin
                                       *   bus. */
    char *interface_name;             /*!< The name of the network
                                       *   interface for receiving
                                       *   messages. */
    struct in_addr interface_ip;      /*!< The IP address of interface. */
    int registered;                   /*!< Non-zero if this device has
                                       *   been registered. */
    struct _mapper_device *device;    /*!< Device that this admin is
                                       *   in charge of. */
    struct _mapper_monitor *monitor;  /*!< Monitor that this admin is
                                       *   in charge of. */
} mapper_admin_t;

/*! The handle to this device is a pointer. */
typedef mapper_admin_t *mapper_admin;


/**** Router ****/

/*! The connection structure is a linked list of connections for a
 *  given signal.  Each signal can be associated with multiple
 *  outputs. This structure only contains state information used for
 *  performing mapping, the connection properties are publically
 *  defined in mapper_db.h. */

typedef struct _mapper_connection {
    mapper_db_connection_t props;               //!< Properties
    struct _mapper_connection *next;            //!< Next connection in the list.
    struct _mapper_signal *source;              //!< Source signal
    struct _mapper_router *router;              //!< Parent router
    int calibrating;   /*!< 1 if the source range is currently being
                        *   calibrated, 0 otherwise. */

    mapper_expr expr;  //!< The mapping expression.
} *mapper_connection;


/*! The signal connection is a linked list containing a signal and a
 *  list of connections.  For each router, there is one per signal of
 *  the associated device.  TODO: This should be replaced with a more
 *  efficient approach such as a hash table or search tree. */
typedef struct _mapper_signal_connection {
    struct _mapper_signal *signal;          //!< The associated signal.
    mapper_connection connection;           /*!< The first connection for
                                             *   this signal. */
    struct _mapper_signal_connection *next; /*!< The next signal connection
                                             *   in the list. */
} *mapper_signal_connection;

/*! The router structure is a linked list of routers each associated
 *  with a destination address that belong to a controller device. */
typedef struct _mapper_router {
    mapper_db_link_t props;                 //!< Properties.
    struct _mapper_device *device;        /*!< The device associated with
                                           *   this router */
    struct _mapper_router *next;          //!< Next router in the list.
    mapper_signal_connection connections; /*!< The list of connections
                                            *  for each signal. */
    lo_bundle bundle;                     /*!< Bundle for queuing up
                                           * sent messages. */
    lo_message message;                   /*!< A single message to
                                           * hold unless a bundle is
                                           * to be used. */
    const char *path;                     /*!< If message!=NUL, then
                                           * this is the path of that
                                           * message. */
    mapper_timetag_t tt;                  /*!< Timetag of message or
                                           * bundle waiting to be
                                           * sent. */
} *mapper_router;

/**** Device ****/

typedef struct _mapper_device {
    /*! Prefix for the name of this device.  It gets a unique ordinal
     *  appended to it to differentiate from other devices of the same
     *  name. */
    const char *name_prefix;

    /*! Non-zero if this device is the sole owner of this admin, i.e.,
     *  it was created during mdev_new() and should be freed during
     *  mdev_free(). */
    int own_admin;

    mapper_admin admin;
    struct _mapper_signal **inputs;
    struct _mapper_signal **outputs;
    int n_inputs;
    int n_outputs;
    int n_query_inputs;
    int n_alloc_inputs;
    int n_alloc_outputs;
    int n_links;
    int n_connections;
    int version;
    int flags;    /*!< Bitflags indicating if information has already been
                   *   sent in a given polling step. */
    mapper_router routers;
    struct _mapper_instance_id_map *active_id_map; /*!< The list of active instance
                                                    * id mappings. */
    struct _mapper_instance_id_map *reserve_id_map; /*!< The list of reserve instance
                                                     * id mappings. */

    int id_counter;

    /*! Server used to handle incoming messages.  NULL until at least
     *  one input has been registered and the incoming port has been
     *  allocated. */
    lo_server server;

    /*! Extra properties associated with this device. */
    struct _mapper_string_table *extra;
} *mapper_device;

/*! The instance ID map is a linked list of int32 instance ids for coordinating
 *  remote and local instances. */
typedef struct _mapper_instance_id_map {
    int local;                          //!< Local instance id to map.
    int group;                          //!< Link group id.
    int remote;                         //!< Remote instance id to map.
    int reference_count;
    struct _mapper_instance_id_map *next;  //!< The next id map in the list.
} *mapper_instance_id_map;

/*! Bit flags indicating if information has already been
 *   sent in a given polling step. */
#define FLAGS_WHO               0x01
#define FLAGS_INPUTS_GET        0x02
#define FLAGS_OUTPUTS_GET       0x04
#define FLAGS_LINKS_GET         0x08
#define FLAGS_CONNECTIONS_GET   0x10
#define FLAGS_ADMIN_MESSAGES    0x1F
#define FLAGS_ATTRIBS_CHANGED   0x20

/**** Monitor ****/

/*! A list of function and context pointers. */
typedef struct _fptr_list {
    void *f;
    void *context;
    struct _fptr_list *next;
} *fptr_list;

typedef struct _mapper_db {
    mapper_db_device     registered_devices;     //<! List of devices.
    mapper_db_signal     registered_inputs;      //<! List of inputs.
    mapper_db_signal     registered_outputs;     //<! List of outputs.
    mapper_db_connection registered_connections; //<! List of connections.
    mapper_db_link       registered_links;       //<! List of links.
    fptr_list   device_callbacks;     //<! List of device record callbacks.
    fptr_list   signal_callbacks;     //<! List of signal record callbacks.
    fptr_list   connection_callbacks; //<! List of connection record callbacks.
    fptr_list   link_callbacks;       //<! List of link record callbacks.
} mapper_db_t, *mapper_db;

typedef struct _mapper_monitor {
    mapper_admin      admin;    //<! Admin for this monitor.

    /*! Non-zero if this monitor is the sole owner of this admin, i.e.,
     *  it was created during mapper_monitor_new() and should be freed during
     *  mapper_monitor_free(). */
    int own_admin;

    mapper_db_t       db;       //<! Database for this monitor.
}  *mapper_monitor;

/**** Messages ****/

/*! Symbolic representation of recognized @-parameters. */
typedef enum {
    AT_CLIPMAX,
    AT_CLIPMIN,
    AT_DESTLENGTH,
    AT_DESTTYPE,
    AT_DIRECTION,
    AT_EXPRESSION,
    AT_ID,
    AT_INSTANCES,
    AT_IP,
    AT_LENGTH,
    AT_MAX,
    AT_MIN,
    AT_MODE,
    AT_MUTE,
    AT_NUMCONNECTIONS,
    AT_NUMINPUTS,
    AT_NUMLINKS,
    AT_NUMOUTPUTS,
    AT_PORT,
    AT_RANGE,
    AT_RATE,
    AT_REV,
    AT_SCOPE,
    AT_SRCLENGTH,
    AT_SRCTYPE,
    AT_TYPE,
    AT_UNITS,
    AT_EXTRA,
    N_AT_PARAMS
} mapper_msg_param_t;

/* Maximum number of "extra" signal parameters. */
#define N_EXTRA_PARAMS 20

/*! Strings that correspond to mapper_msg_param_t. */
extern const char* mapper_msg_param_strings[];

/*! Strings that correspond to mapper_clipping_type, defined in
 *  mapper_db.h. */
extern const char* mapper_clipping_type_strings[];

/*! Strings that correspond to mapper_mode_type, defined in
 *  mapper_db.h. */
extern const char* mapper_mode_type_strings[];

/*! Queriable representation of a parameterized message parsed from an
 *  incoming OSC message. Does not contain a copy of data, so only
 *  valid for the duration of the message handler. Also allows for a
 *  constant number of "extra" parameters; that is, unknown parameters
 *  that may be specified for a signal and used for metadata, which
 *  will be added to a general-purpose string table associated with
 *  the signal. */
typedef struct _mapper_message
{
    const char *path;               //!< OSC address.
    lo_arg **values[N_AT_PARAMS];   //!< Array of parameter values.
    const char *types[N_AT_PARAMS]; //!< Array of types for each value.
    lo_arg **extra_args[N_EXTRA_PARAMS]; //!< Pointers to extra parameters.
    char extra_types[N_EXTRA_PARAMS];    //!< Types of extra parameters.
} mapper_message_t;

/**** Queues ****/

typedef struct _mapper_queue
{
	int size;
	int position;
	mapper_timetag_t timetag;
	struct _mapper_signal_instance **instances;
    int *as_instance;
} mapper_queue_t, *mapper_queue;

#endif // __MAPPER_TYPES_H__
