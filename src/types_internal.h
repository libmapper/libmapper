
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

#include <mapper/mapper_constants.h>

/**** Defined in mapper.h ****/

/* Types defined here replace opaque prototypes in mapper.h, thus we
 * cannot include it here.  Instead we include some prototypes here.
 * Typedefs cannot be repeated, therefore they are refered to by
 * struct name. */

struct _mapper_network;
typedef struct _mapper_expr *mapper_expr;

/* Forward declarations for this file. */

struct _mapper_device;
typedef struct _mapper_device mapper_device_t;
typedef struct _mapper_device *mapper_device;
struct _mapper_signal;
typedef struct _mapper_signal mapper_signal_t;
typedef struct _mapper_signal *mapper_signal;
struct _mapper_allocated_t;
struct _mapper_map;
struct _mapper_id_map;

/*! Symbolic representation of recognized properties. */
typedef enum {
    AT_BOUND_MAX,           /* 0x00 */
    AT_BOUND_MIN,           /* 0x01 */
    AT_CALIBRATING,         /* 0x02 */
    AT_CAUSES_UPDATE,       /* 0x03 */
    AT_DESCRIPTION,         /* 0x04 */
    AT_DIRECTION,           /* 0x05 */
    AT_EXPRESSION,          /* 0x06 */
    AT_HOST,                /* 0x07 */
    AT_ID,                  /* 0x08 */
    AT_INSTANCE,            /* 0x09 */
    AT_LENGTH,              /* 0x0A */
    AT_LIB_VERSION,         /* 0x0B */
    AT_MAX,                 /* 0x0C */
    AT_MIN,                 /* 0x0D */
    AT_MODE,                /* 0x0E */
    AT_MUTED,               /* 0x0F */
    AT_NAME,                /* 0x10 */
    AT_NUM_INCOMING_MAPS,   /* 0x11 */
    AT_NUM_INPUTS,          /* 0x12 */
    AT_NUM_INSTANCES,       /* 0x13 */
    AT_NUM_OUTGOING_MAPS,   /* 0x14 */
    AT_NUM_OUTPUTS,         /* 0x15 */
    AT_PORT,                /* 0x16 */
    AT_PROCESS_LOCATION,    /* 0x17 */
    AT_RATE,                /* 0x18 */
    AT_SCOPE,               /* 0x19 */
    AT_SLOT,                /* 0x1A */
    AT_STATUS,              /* 0x1B */
    AT_SYNCED,              /* 0x1C */
    AT_TYPE,                /* 0x1D */
    AT_UNIT,                /* 0x1E */
    AT_USE_AS_INSTANCE,     /* 0x1F */
    AT_USER_DATA,           /* 0x20 */
    AT_VERSION,             /* 0x21 */
    AT_EXTRA,               /* 0x22 */
    NUM_AT_PROPERTIES       /* 0x23 */
} mapper_property_t;

/**** String tables ****/

/*! An arbitrary property value and its type.  If type is a string, the
 *  allocated size may be longer than sizeof(mapper_property_value_t). */
typedef struct _mapper_property_value {
    void **value;
    int length;
    char type;
} mapper_property_value_t;

// bit flags for tracking permissions for modifying properties
#define NON_MODIFIABLE  0x00
#define LOCAL_MODIFY    0x01
#define REMOTE_MODIFY   0x02
#define MODIFIABLE      0x03
#define MUTABLE_TYPE    0x04
#define MUTABLE_LENGTH  0x08
#define INDIRECT        0x10
#define PROP_OWNED      0x20
#define PROP_DIRTY      0x40

/*! Used to hold look-up table records. */
typedef struct {
    const char *key;
    void **value;
    int length;
    char type;
    mapper_property_t index;
    char flags;
} mapper_table_record_t;

/*! Used to hold look-up tables. */
typedef struct _mapper_table {
    mapper_table_record_t *records;
    int num_records;
    int alloced;
    char dirty;
} mapper_table_t, *mapper_table;

/**** Database ****/

/*! A list of function and context pointers. */
typedef struct _fptr_list {
    void *f;
    void *context;
    struct _fptr_list *next;
} *fptr_list;

typedef struct _mapper_subscription {
    mapper_device device;
    int flags;
    uint32_t lease_expiration_sec;
    struct _mapper_subscription *next;
} *mapper_subscription;

typedef struct _mapper_database {
    struct _mapper_network *network;
    mapper_device devices;              //<! List of devices.
    mapper_signal signals;              //<! List of signals.
    struct _mapper_map *maps;           //<! List of mappings.
    struct _mapper_map *staged_maps;    //<! List of staged mappings.
    fptr_list device_callbacks;         //<! List of device record callbacks.
    fptr_list signal_callbacks;         //<! List of signal record callbacks.
    fptr_list map_callbacks;            //<! List of mapping record callbacks.

    /*! Linked-list of autorenewing device subscriptions. */
    mapper_subscription subscriptions;

    /*! Flags indicating whether information on signals and mappings should
     *  be automatically subscribed to when a new device is seen.*/
    int autosubscribe;

    /*! The time after which the database will declare devices "unresponsive". */
    int timeout_sec;
    uint32_t resource_counter;

    int own_network;
} mapper_database_t, *mapper_database;

/**** Messages ****/

/*! Some useful strings for sending administrative messages. */
typedef enum {
    MSG_MAP,
    MSG_MAP_TO,
    MSG_MAPPED,
    MSG_MODIFY_MAP,
    MSG_DEVICE,
    MSG_UNMAP,
    MSG_UNMAPPED,
    MSG_PING,
    MSG_LOGOUT,
    MSG_NAME_PROBE,
    MSG_NAME_REG,
    MSG_SIGNAL,
    MSG_SIGNAL_REMOVED,
    MSG_SUBSCRIBE,
    MSG_UNSUBSCRIBE,
    MSG_SYNC,
    MSG_WHO,
    NUM_MSG_STRINGS
} network_message_t;

/*! Function to call when an allocated resource is locked. */
typedef void mapper_resource_on_lock(struct _mapper_allocated_t *resource);

/*! Function to call when an allocated resource encounters a collision. */
typedef void mapper_resource_on_collision(struct _mapper_allocated_t *resource);

/*! Allocated resources */
typedef struct _mapper_allocated_t {
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
    mapper_resource_on_lock *on_lock;

   //! Function to call when resource collision occurs.
    mapper_resource_on_collision *on_collision;
} mapper_allocated_t, *mapper_allocated;

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

typedef struct _mapper_subscriber {
    lo_address                      address;
    uint32_t                        lease_expiration_sec;
    int                             flags;
    struct _mapper_subscriber *next;
} *mapper_subscriber;

/*! A structure that keeps information about a device. */
typedef struct _mapper_network {
    int random_id;                  /*!< Random ID for allocation speedup. */
    lo_server_thread bus_server;    /*!< LibLo server thread for the
                                     *   multicast bus. */
    int msgs_recvd;                 /*!< Number of messages received on the
                                     *   multicast bus. */
    lo_address bus_addr;            /*!< LibLo address for the multicast bus. */
    lo_server_thread mesh_server;   /*!< LibLo server thread for mesh comms. */
    char *interface_name;           /*!< The name of the network interface
                                     *   for receiving messages. */
    struct in_addr interface_ip;    /*!< The IP address of interface. */
    struct _mapper_device *device;  /*!< Device that this structure is
                                     *   in charge of. */
    mapper_database_t database;     /*<! Database of local and remote libmapper
                                     *   objects. */
    mapper_clock_t clock;           /*!< Clock for processing timed events. */
    lo_bundle bundle;               /*!< Bundle pointer for sending
                                     *   messages on the multicast bus. */
    lo_address bundle_dest;
    int message_type;
    int own_network;                /*! Zero if this network was created
                                     *  automatically by mapper_device_new()
                                     *  or mapper_database_new(), non-zero if
                                     *  it was created by mapper_network_new()
                                     *  and should be freed by
                                     *  mapper_network_free(). */
    int database_methods_added;
} mapper_network_t;

/*! The handle to this device is a pointer. */
typedef mapper_network_t *mapper_network;

#define TIMEOUT_SEC 10       // timeout after 10 seconds without ping

/**** Signal ****/

/*! A structure that stores the current and historical values and timetags
 *  of a signal. The size of the history arrays is determined by the needs
 *  of mapping expressions.
 *  @ingroup signals */

typedef struct _mapper_history
{
    char type;                  /*!< The type of this signal, specified as an
                                 *   OSC type character. */
    int position;               //!< Current position in the circular buffer.
    int size;                   //!< History size of the buffer.
    int length;                 //!< Vector length.
    void *value;                /*!< Value of the signal for each sample of
                                 *   stored history. */
    mapper_timetag_t *timetag;  //!< Timetag for each sample of stored history.
} mapper_history_t, *mapper_history;

/*! Bit flags for indicating signal instance status. */
#define RELEASED_LOCALLY  0x01
#define RELEASED_REMOTELY 0x02

/*! A signal is defined as a vector of values, along with some metadata. */
typedef struct _mapper_signal_instance
{
    mapper_id id;               //!< User-assignable instance id.
    int index;                  //!< Index for accessing value history.
    int is_active;              //!< Status of this instance.
    void *user_data;            //!< User data of this instance.
    mapper_timetag_t created;   //!< The instance's creation timestamp.
    int has_value;              //!< Indicates whether this instance has a value.
    char *has_value_flags;      //!< Indicates which vector elements have a value.

    void *value;                //!< The current value of this signal instance.
    mapper_timetag_t timetag;   //!< The timetag for the current value.
} mapper_signal_instance_t, *mapper_signal_instance;

typedef struct _mapper_signal_id_map
{
    struct _mapper_id_map *map;                 //!< Associated mapper_id_map.
    struct _mapper_signal_instance *instance;   //!< Signal instance.
    int status;                                 /*!< Either 0 or a combination of
                                                 *  MAPPER_RELEASED_LOCALLY and
                                                 MAPPER_RELEASED_REMOTELY. */
} mapper_signal_id_map_t;

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
    mapper_instance_stealing_type instance_stealing_mode;

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
    mapper_local_signal local;
    mapper_device device;
    char *path;         //! OSC path.  Must start with '/'.
    char *name;         //! The name of this signal (path+1).
    char *description;  //!< A short description of this signal.
    mapper_id id;       //!< Unique id identifying this signal.

    char *unit;         //!< The unit of this signal, or NULL for N/A.
    void *minimum;      //!< The minimum of this signal, or NULL for N/A.
    void *maximum;      //!< The maximum of this signal, or NULL for N/A.

    /*! Properties associated with this signal. */
    struct _mapper_table *props;
    struct _mapper_table *staged_props;

    void *user_data;    //!< A pointer available for associating user context.

    float rate;         //!< The update rate, or 0 for non-periodic signals.
    int direction;      //!< DI_OUTGOING / DI_INCOMING / DI_BOTH
    int length;         //!< Length of the signal vector, or 1 for scalars.
    int num_instances;  //!< Number of instances.
    int num_incoming_maps;
    int num_outgoing_maps;
    int version;
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

/**** Maps and Slots ****/

#define MAX_NUM_MAP_SOURCES 8    // arbitrary

#define STATUS_STAGED       0x00
#define STATUS_TYPE_KNOWN   0x01
#define STATUS_LENGTH_KNOWN 0x02
#define STATUS_LINK_KNOWN   0x04
#define STATUS_READY        0x0F
#define STATUS_ACTIVE       0x1F

typedef struct _mapper_slot_internal {
    // each slot can point to local signal or a remote link structure
    struct _mapper_router_signal *router_sig;    //!< Parent signal if local
    mapper_link link;                       //!< Remote device if not local

    mapper_history history;                 /*!< Array of value histories
                                             *   for each signal instance. */
    int history_size;                       //!< History size.
    char status;
} mapper_slot_internal_t, *mapper_slot_internal;

typedef struct _mapper_slot {
    mapper_slot_internal local;         //!< Pointer to local resources if any
    struct _mapper_map *map;            //!< Pointer to parent map
    mapper_signal signal;               //!< Pointer to parent signal

    /*! Properties associated with this slot. */
    struct _mapper_table *props;
    struct _mapper_table *staged_props;

    void *minimum;                      //!< Array of minima, or NULL for N/A
    void *maximum;                      //!< Array of maxima, or NULL for N/A
    int id;                             //!< Slot ID
    int num_instances;
    int direction;                      //!< DI_INCOMING or DI_OUTGOING
    int causes_update;                  //!< 1 if causes update, 0 otherwise.
    int use_as_instance;                //!< 1 if used as instance, 0 otherwise.

    mapper_boundary_action bound_max;   //!< Operation for exceeded upper bound.
    mapper_boundary_action bound_min;   //!< Operation for exceeded lower bound.
    int calibrating;                    //!< >1 if calibrating, 0 otherwise
} mapper_slot_t, *mapper_slot;

/*! The mapper_map_internal structure is a linked list of mappings for a given
 *  signal.  Each signal can be associated with multiple inputs or outputs. This
 *  structure only contains state information used for performing mapping, the
 *  properties are publically defined in mapper_constants.h. */
typedef struct _mapper_map_internal {
    struct _mapper_router *router;
    int is_local;

    mapper_expr expr;                   //!< The mapping expression.
    mapper_history *expr_vars;          //!< User variables values.
    int num_expr_vars;                  //!< Number of user variables.
    int num_var_instances;

    int one_source;

    mapper_mode mode;                   //!< Raw, linear, or expression.

} mapper_map_internal_t, *mapper_map_internal;

typedef struct _mapper_map_scope {
    mapper_device *devices; //!< Array of map scope devices.
    int size;               //!< The number of map scopes.
} mapper_map_scope_t, *mapper_map_scope;

/*! A record that describes the properties of a mapping.
 *  @ingroup map */
typedef struct _mapper_map {
    mapper_database database;           //!< Pointer back to the database.
    mapper_map_internal local;
    mapper_slot sources;
    mapper_slot_t destination;
    mapper_id id;                       //!< Unique id identifying this map

    struct _mapper_map_scope scope;

    /*! Properties associated with this map. */
    struct _mapper_table *props;
    struct _mapper_table *staged_props;
    char *description;                  //!< A short description of this map.

    void *user_data;

    char *expression;

    mapper_mode mode;                   //!< MO_LINEAR or MO_EXPRESSION
    int muted;                          //!< 1 to mute mapping, 0 to unmute
    int num_sources;
    mapper_location process_location;
    int status;
    int version;
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
} mapper_router_t, *mapper_router;

/*! The instance ID map is a linked list of int32 instance ids for coordinating
 *  remote and local instances. */
typedef struct _mapper_id_map {
    mapper_id global;               //!< Hash for originating device.
    mapper_id local;                //!< Local instance id to map.
    int refcount_local;
    int refcount_global;
    struct _mapper_id_map *next;    //!< The next id map in the list.
} mapper_id_map_t, *mapper_id_map;

/**** Device ****/

typedef struct _mapper_local_device {
    mapper_allocated_t ordinal;     /*!< A unique ordinal for this device
                                     *   instance. */
    int registered;                 /*!< Non-zero if this device has been
                                     *   registered. */

    int n_output_callbacks;
    mapper_router router;

    /*! Function to call for custom map handling. */
    void *map_handler;
    void *map_handler_userdata;

    mapper_subscriber subscribers;  /*!< Linked-list of subscribed peers. */

    /*! The list of active instance id mappings. */
    struct _mapper_id_map *active_id_map;

    /*! The list of reserve instance id mappings. */
    struct _mapper_id_map *reserve_id_map;

    int link_timeout_sec;   /* Number of seconds after which unresponsive
                             * links will be removed, or 0 for never. */

    /*! Server used to handle incoming messages. */
    lo_server server;

    int own_network;
} mapper_local_device_t, *mapper_local_device;


/*! A record that keeps information about a device on the network. */
struct _mapper_device {
    mapper_database database;   //!< Pointer back to the database.
    mapper_local_device local;
    char *lib_version;          //!< libmapper version of device.
    void *user_data;            //!< User modifiable data.

    char *identifier;           //!< The identifier (prefix) for this device.
    char *name;                 //!< The full name for this device, or zero.
    char *description;          //!< A short description of this device.

    /*! Properties associated with this device. */
    struct _mapper_table *props;
    struct _mapper_table *staged_props;

    mapper_id id;               //!< Unique id identifying this device.
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
    int status;

    int subscribed;
};

/**** Messages ****/

#define STATIC_PROPS        0x10
#define UPDATED_PROPS       0x20

#define PROPERTY_ADD        0x040
#define PROPERTY_REMOVE     0x080
#define DST_SLOT_PROPERTY   0x100
// currently 9 bits are used for mapper_property_t enum, add/remove, and dest slot
#define SRC_SLOT_PROPERTY_BIT_OFFSET    9
#define SRC_SLOT_PROPERTY(index) ((index + 1) << SRC_SLOT_PROPERTY_BIT_OFFSET)
#define MASK_PROP_BITFLAGS(index) (index & 0x3F)

/* Maximum number of "extra" properties for a signal, device, or map. */
#define NUM_EXTRA_PROPERTIES 20

/*! Strings that correspond to mapper_boundary_action, defined in
 *  mapper_constants.h. */
extern const char* mapper_boundary_action_strings[];

/*! Strings that correspond to mapper_mode, defined in mapper_constants.h. */
extern const char* mapper_mode_strings[];

/*! Queriable representation of a parameterized message parsed from an incoming
 *  OSC message. Does not contain a copy of data, so only valid for the duration
 *  of the message handler. Also allows for a constant number of "extra"
 *  parameters; that is, unknown parameters that may be specified for a signal
 *  and used for metadata, which will be added to a general-purpose string table
 *  associated with the signal. */
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
