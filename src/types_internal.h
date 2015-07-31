
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

struct _mapper_admin;
typedef struct _mapper_expr *mapper_expr;

/* Forward declarations for this file. */

struct _mapper_device;
typedef struct _mapper_device mapper_device_t;
typedef struct _mapper_device *mapper_device;
struct _mapper_signal;
typedef struct _mapper_signal mapper_signal_t;
typedef struct _mapper_signal *mapper_signal;
struct _mapper_admin_allocated_t;
struct _mapper_map;
struct _mapper_id_map;

typedef struct {
    char type;
    union {
        int indirect;
        int alt_type;
    };
    int length;     // lengths stored as negatives, lookup lengths as offsets
    int offset;
} property_table_value_t;

/**** String tables ****/

/*! A pair representing an arbitrary parameter value and its type. (Re-using
 *  liblo's OSC-oriented lo_arg data structure.) If type is a string, the
 *  allocated size may be longer than sizeof(mapper_prop_value_t). */
typedef struct _mapper_prop_value {
    char type;
    int length;
    void *value;
} mapper_prop_value_t;

/*! Used to hold string look-up table nodes. */
typedef struct {
    const char *key;
    void *value;
    int is_prop;
} string_table_node_t;

/*! Used to hold string look-up tables. */
typedef struct _mapper_string_table {
    string_table_node_t *store;
    int len;
    int alloced;
} mapper_string_table_t, *table;

/**** Database ****/

/*! A list of function and context pointers. */
typedef struct _fptr_list {
    void *f;
    void *context;
    struct _fptr_list *next;
} *fptr_list;

typedef struct _mapper_db {
    struct _mapper_admin *admin;
    mapper_device devices;          //<! List of devices.
    mapper_signal signals;          //<! List of signals.
    struct _mapper_map *maps;       //<! List of mappings.
    fptr_list device_callbacks;     //<! List of device record callbacks.
    fptr_list signal_callbacks;     //<! List of signal record callbacks.
    fptr_list map_callbacks;        //<! List of mapping record callbacks.

    /*! The time after which the db will declare devices "unresponsive". */
    int timeout_sec;
} mapper_db_t, *mapper_db;

/**** Admin bus ****/

/*! Some useful strings for sending admin messages. */
/*! Symbolic representation of recognized @-parameters. */
typedef enum {
    ADM_MAP,
    ADM_MAP_TO,
    ADM_MAPPED,
    ADM_MODIFY_MAP,
    ADM_DEVICE,
    ADM_UNMAP,
    ADM_UNMAPPED,
    ADM_PING,
    ADM_LOGOUT,
    ADM_NAME_PROBE,
    ADM_NAME_REG,
    ADM_SIGNAL,
    ADM_SIGNAL_REMOVED,
    ADM_SUBSCRIBE,
    ADM_UNSUBSCRIBE,
    ADM_SYNC,
    ADM_WHO,
    NUM_ADM_STRINGS
} admin_message_t;

/*! Function to call when an allocated resource is locked. */
typedef void mapper_admin_resource_on_lock(struct _mapper_device *md,
                                           struct _mapper_admin_allocated_t
                                           *resource);

/*! Function to call when an allocated resource encounters a collision. */
typedef void mapper_admin_resource_on_collision(struct _mapper_admin *admin);

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
} mapper_admin_allocated_t, *mapper_admin_allocated;

/*! Clock and timing information. */
typedef struct _mapper_sync_timetag_t {
    int message_id;
    lo_timetag timetag;
} mapper_sync_timetag_t;

typedef struct _mapper_clock_t {
    mapper_timetag_t now;
    uint32_t next_ping;
} mapper_clock_t, *mapper_clock;

typedef struct _mapper_sync_clock_t {
    double rate;
    double offset;
    double latency;
    double jitter;
    mapper_sync_timetag_t sent;
    mapper_sync_timetag_t response;
    int new;
} mapper_sync_clock_t, *mapper_sync_clock;

typedef struct _mapper_admin_subscriber {
    lo_address                      address;
    uint32_t                        lease_expiration_sec;
    int                             flags;
    struct _mapper_admin_subscriber *next;
} *mapper_admin_subscriber;

/*! A structure that keeps information about a device. */
typedef struct _mapper_admin {
    int random_id;                    /*!< Random ID for allocation speedup. */
    lo_server_thread bus_server;      /*!< LibLo server thread for the
                                       *   admin bus. */
    int msgs_recvd;                   /*!< Number of messages received on the
                                           admin bus. */
    lo_address bus_addr;              /*!< LibLo address for the admin bus. */
    lo_server_thread mesh_server;     /*!< LibLo server thread for the
                                       *   admin mesh. */
    char *interface_name;             /*!< The name of the network interface
                                       *   for receiving messages. */
    struct in_addr interface_ip;      /*!< The IP address of interface. */
    struct _mapper_device *device;    /*!< Device that this admin is
                                       *   in charge of. */
    struct _mapper_monitor *monitor;  /*!< Monitor that this admin is
                                       *   in charge of. */
    mapper_db_t       db;       //<! Database for this monitor.
    mapper_clock_t clock;             /*!< Clock for processing timed events. */
    lo_bundle bundle;                 /*!< Bundle pointer for sending
                                       *   messages on the admin bus. */
    lo_address bundle_dest;
    int message_type;
    mapper_admin_subscriber subscribers; /*!< Linked-list of subscribed peers. */
} mapper_admin_t;

/*! The handle to this device is a pointer. */
typedef mapper_admin_t *mapper_admin;

#define ADMIN_TIMEOUT_SEC 10        // timeout after 10 seconds without ping

/**** Signal ****/

/*! A structure that stores the current and historical values and timetags
 *  of a signal. The size of the history arrays is determined by the needs
 *  of mapping expressions.
 *  @ingroup signals */

typedef struct _mapper_history
{
    char type;                      /*!< The type of this signal, specified as
                                     *   an OSC type character. */
    int position;                   /*!< Current position in the circular buffer. */
    int size;                       /*!< History size of the buffer. */
    int length;                     /*!< Vector length. */
    void *value;                    /*!< Value of the signal for each sample of
                                     *   stored history. */
    mapper_timetag_t *timetag;      /*!< Timetag for each sample of stored history. */
} mapper_history_t, *mapper_history;

/*! A signal is defined as a vector of values, along with some
 *  metadata. */
typedef struct _mapper_local_signal
{
    /*! The device associated with this signal. */
    struct _mapper_device *device;

    /*! ID maps and active instances. */
    struct _mapper_signal_id_map *id_maps;
    int id_map_length;

    /*! Array of pointers to the signal instances. */
    struct _mapper_signal_instance **instances;

    /*! Bitflag value when entire signal vector is known. */
    char *has_complete_value;

    /*! Type of voice stealing to perform on instances. */
    mapper_instance_allocation_type instance_allocation_type;

    /*! An optional function to be called when the signal value changes. */
    void *update_handler;

    /*! An optional function to be called when the signal instance management
     *  events occur. */
    void *instance_event_handler;

    /*! Flags for deciding when to call the instance event handler. */
    int instance_event_flags;
} mapper_local_signal_t, *mapper_local_signal;

/*! A record that describes properties of a signal. */
struct _mapper_signal {
    mapper_db db;       //!< Pointer back to the db.
    mapper_local_signal local;
    mapper_device device;
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
    char type;          /*! The type of this signal, specified as an OSC type
                         *  character. */
};

/**** Router ****/

typedef struct _mapper_queue {
    mapper_timetag_t tt;
    lo_bundle bundle;
    struct _mapper_queue *next;
} *mapper_queue;

/*! The link structure is a linked list of links each associated
 *  with a destination address that belong to a controller device. */
typedef struct _mapper_link {
    mapper_device local_device;         //!< Pointer to parent device
    mapper_device remote_device;
    lo_address admin_addr;              //!< Network address of remote endpoint
    lo_address data_addr;               //!< Network address of remote endpoint
    mapper_queue queues;                /*!< Linked-list of message queues
                                         *   waiting to be sent. */
    mapper_sync_clock_t clock;

    int num_incoming_maps;
    int num_outgoing_maps;

    struct _mapper_link *next;          //!< Next link in the list.
} *mapper_link;

#define MAPPER_TYPE_KNOWN   0x01
#define MAPPER_LENGTH_KNOWN 0x02
#define MAPPER_LINK_KNOWN   0x04
#define MAPPER_READY        0x07
#define MAPPER_ACTIVE       0x17

#define MAX_NUM_MAP_SOURCES 8    // arbitrary

typedef struct _mapper_slot_internal {
    // each slot can point to local signal or a remote link structure
    struct _mapper_router_signal *router_sig;    //!< Parent signal if local
    mapper_link link;                       //!< Remote device if not local

    mapper_history history;                 /*!< Array of value histories
                                             *   for each signal instance. */
    int history_size;                       //!< History size.
    char status;
} mapper_slot_internal_t, *mapper_slot_internal;

// TODO: remove type, length, flags
typedef struct _mapper_slot {
    mapper_db db;                       //!< Pointer back to the db.
    mapper_slot_internal local;         //!< Pointer to local resources if any
    struct _mapper_map *map;            //!< Pointer to parent map
    mapper_signal signal;               //!< Pointer to parent signal

    void *minimum;                      //!< Array of minima, or NULL for N/A
    void *maximum;                      //!< Array of maxima, or NULL for N/A
    int id;                             //!< Slot ID
    int length;
    int num_instances;
    int flags;
    int direction;                      //!< DI_INCOMING or DI_OUTGOING
    int cause_update;                   //!< 1 to cause update, 0 otherwise.
    int send_as_instance;               //!< 1 to send as instance, 0 otherwise.

    mapper_boundary_action bound_max;   //!< Operation for exceeded upper bound.
    mapper_boundary_action bound_min;   //!< Operation for exceeded lower bound.
    int calibrating;                    //!< >1 if calibrating, 0 otherwise
    char type;
} mapper_slot_t, *mapper_slot;

/*! The mapper_map_internal structure is a linked list of mappings for a given
 *  signal.  Each signal can be associated with multiple inputs or outputs. This
 *  structure only contains state information used for performing mapping, the
 *  properties are publically defined in mapper_db.h. */
typedef struct _mapper_map_internal {
    struct _mapper_router *router;
    int is_admin;
    int is_local;

    // TODO: move expr_vars into expr structure?
    mapper_expr expr;                       //!< The mapping expression.
    mapper_history *expr_vars;              //!< User variables values.
    int num_expr_vars;                      //!< Number of user variables.
    int num_var_instances;

    int status;
    int one_source;

    mapper_mode_type mode;                  /*!< Raw, linear, or expression. */

} mapper_map_internal_t, *mapper_map_internal;

typedef struct _mapper_map_scope {
    uint32_t *hashes;   //!< Array of map scope hashes.
    char **names;       //!< Array of map scope names.
    int size;           //!< The number of map scopes.
} mapper_map_scope_t, *mapper_map_scope;

/*! A record that describes the properties of a mapping.
 *  @ingroup mapdb */
typedef struct _mapper_map {
    mapper_db db;                       //!< Pointer back to the db.
    mapper_map_internal local;
    mapper_slot sources;
    mapper_slot_t destination;
    uint64_t id;                        //!< Unique id identifying this map

    struct _mapper_map_scope scope;

    /*! Extra properties associated with this map. */
    struct _mapper_string_table *extra;
    struct _mapper_string_table *updater;

    char *expression;
    char *description;

    mapper_mode_type mode;              //!< MO_LINEAR or MO_EXPRESSION
    int muted;                          //!< 1 to mute mapping, 0 to unmute
    int num_sources;
    int process_location;               //!< 1 for source, 0 for destination
} mapper_map_t, *mapper_map;

/*! The router_signal is a linked list containing a signal and a list of
 *  mappings.  TODO: This should be replaced with a more efficient approach
 *  such as a hash table or search tree. */
typedef struct _mapper_router_signal {
    struct _mapper_router *link;        //!< The parent link.
    struct _mapper_signal *signal;      //!< The associated signal.

    mapper_slot *slots;
    int num_slots;
    int id_counter;

    struct _mapper_router_signal *next; //!< The next router_signal in the list.
} *mapper_router_signal;

/*! The router structure. */
typedef struct _mapper_router {
    struct _mapper_device *device;  //!< The device associated with this link.
    mapper_router_signal signals;   //!< The list of mappings for each signal.
    mapper_link links;              //!< The list of links to other devices.
} *mapper_router;

/*! The instance ID map is a linked list of int32 instance ids for coordinating
 *  remote and local instances. */
typedef struct _mapper_id_map {
    uint64_t global;                        //!< Hash for originating device.
    int refcount_local;
    int local;                              //!< Local instance id to map.
    int refcount_global;
    struct _mapper_id_map *next;            //!< The next id map in the list.
} *mapper_id_map;

/**** Device ****/

typedef struct _mapper_local_device {
    mapper_admin_allocated_t ordinal;   /*!< A unique ordinal for this
                                         *   device instance. */
    int registered;                     /*!< Non-zero if this device has
                                         *   been registered. */

    /*! Non-zero if this device is the sole owner of this admin, i.e.,
     *  it was created during mdev_new() and should be freed during
     *  mdev_free(). */
    int own_admin;

    int n_output_callbacks;
    int version;
    mapper_router router;

    /*! Function to call for custom map handling. */
    void *map_handler;
    void *map_handler_userdata;

    /*! The list of active instance id mappings. */
    struct _mapper_id_map *active_id_map;

    /*! The list of reserve instance id mappings. */
    struct _mapper_id_map *reserve_id_map;

    uint32_t resource_counter;

    int link_timeout_sec;   /* Number of seconds after which unresponsive
                             * links will be removed, or 0 for never. */

    /*! Server used to handle incoming messages. */
    lo_server server;
} mapper_local_device_t, *mapper_local_device;


/*! A record that keeps information about a device on the network. */
struct _mapper_device {
    mapper_db db;               //!< Pointer back to the db.
    mapper_local_device local;
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
};

/*! Bit flags indicating if information has already been
 *  sent in a given polling step. */
#define FLAGS_SENT_DEVICE_INFO              0x01
#define FLAGS_SENT_DEVICE_INPUTS            0x02
#define FLAGS_SENT_DEVICE_OUTPUTS           0x04
#define FLAGS_SENT_DEVICE_INCOMING_MAPS     0x08
#define FLAGS_SENT_DEVICE_OUTGOING_MAPS     0x10
#define FLAGS_SENT_ALL_DEVICE_MESSAGES      0x1F
#define FLAGS_DEVICE_ATTRIBS_CHANGED        0x20

/**** Monitor ****/

typedef struct _mapper_subscription {
    mapper_device device;
    int flags;
    uint32_t lease_expiration_sec;
    struct _mapper_subscription *next;
} *mapper_subscription;

typedef struct _mapper_monitor {
    mapper_admin      admin;    //<! Admin for this monitor.

    /*! Non-zero if this monitor is the sole owner of this admin, i.e.,
     *  it was created during mmon_new() and should be freed during
     *  mmon_free(). */
    int own_admin;

    /*! Flags indicating whether information on signals and mappings should
     *  be automatically subscribed to when a new device is seen.*/
    int autosubscribe;

    /*! Linked-list of autorenewing device subscriptions. */
    mapper_subscription subscriptions;

    mapper_map staged_map;
}  *mapper_monitor;

/**** Messages ****/

/*! Symbolic representation of recognized @-parameters. */
typedef enum {
    AT_BOUND_MAX,           /* 0x00 */
    AT_BOUND_MIN,           /* 0x01 */
    AT_CALIBRATING,         /* 0x02 */
    AT_CAUSE_UPDATE,        /* 0x03 */
    AT_DIRECTION,           /* 0x04 */
    AT_EXPRESSION,          /* 0x05 */
    AT_HOST,                /* 0x06 */
    AT_ID,                  /* 0x07 */
    AT_INSTANCES,           /* 0x08 */
    AT_LENGTH,              /* 0x09 */
    AT_LIB_VERSION,         /* 0x0A */
    AT_MAX,                 /* 0x0B */
    AT_MIN,                 /* 0x0C */
    AT_MODE,                /* 0x0D */
    AT_MUTE,                /* 0x0E */
    AT_NUM_INCOMING_MAPS,   /* 0x0F */
    AT_NUM_OUTGOING_MAPS,   /* 0x10 */
    AT_NUM_INPUTS,          /* 0x11 */
    AT_NUM_OUTPUTS,         /* 0x12 */
    AT_PORT,                /* 0x13 */
    AT_PROCESS,             /* 0x14 */
    AT_RATE,                /* 0x15 */
    AT_REV,                 /* 0x16 */
    AT_SCOPE,               /* 0x17 */
    AT_SEND_AS_INSTANCE,    /* 0x18 */
    AT_SLOT,                /* 0x19 */
    AT_TYPE,                /* 0x1A */
    AT_UNITS,               /* 0x1B */
    AT_EXTRA,               /* 0x1C */
    NUM_AT_PARAMS           /* 0x1D */
} mapper_message_param_t;

#define PARAM_ADD                   0x20
#define PARAM_REMOVE                0x40
#define DST_SLOT_PARAM              0x80
// currently 8 bits are used for @param enum, add/remove, and dest slot
#define SRC_SLOT_PARAM_BIT_OFFSET   8
#define SRC_SLOT_PARAM(index)       ((index + 1) << SRC_SLOT_PARAM_BIT_OFFSET)

/* Maximum number of "extra" signal parameters. */
#define NUM_EXTRA_PARAMS 20

/*! Strings that correspond to mapper_message_param_t. */
extern const char* mapper_message_param_strings[];

/*! Strings that correspond to mapper_boundary_action, defined in
 *  mapper_db.h. */
extern const char* mapper_boundary_action_strings[];

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
typedef struct _mapper_message_atom
{
    const char *key;
    lo_arg **values;
    const char *types;
    int length;
    int index;
} mapper_message_atom_t, *mapper_message_atom;

typedef struct _mapper_message
{
    mapper_message_atom_t *atoms;
    int num_atoms;
} mapper_message_t, *mapper_message;

#endif // __MAPPER_TYPES_H__
