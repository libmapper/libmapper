
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

#ifdef HAVE_INTTYPES_H
#include <inttypes.h>
#define PR_MAPPER_ID PRIu64
#else
#define PR_MAPPER_ID "llu"
#endif

/**** Defined in mapper.h ****/

/* Types defined here replace opaque prototypes in mapper.h, thus we
 * cannot include it here.  Instead we include some prototypes here.
 * Typedefs cannot be repeated, therefore they are refered to by
 * struct name. */

typedef struct _mapper_expr *mapper_expr;

/* Forward declarations for this file. */

struct _mapper_device;
typedef struct _mapper_device mapper_device_t;
typedef struct _mapper_device *mapper_device;
struct _mapper_signal;
typedef struct _mapper_signal mapper_signal_t;
typedef struct _mapper_signal *mapper_signal;
struct _mapper_link;
typedef struct _mapper_link mapper_link_t;
typedef struct _mapper_link *mapper_link;
struct _mapper_map;
typedef struct _mapper_map mapper_map_t;
typedef struct _mapper_map *mapper_map;
struct _mapper_allocated_t;
struct _mapper_map;
struct _mapper_idmap;
typedef int mapper_signal_group;

/**** String tables ****/

// bit flags for tracking permissions for modifying properties
#define NON_MODIFIABLE      0x00
#define LOCAL_MODIFY        0x01
#define REMOTE_MODIFY       0x02
#define MODIFIABLE          0x03
#define LOCAL_ACCESS_ONLY   0x04
#define MUTABLE_TYPE        0x08
#define MUTABLE_LENGTH      0x10
#define INDIRECT            0x20
#define PROP_OWNED          0x40
#define PROP_DIRTY          0x80

/*! Used to hold look-up table records. */
typedef struct {
    const char *name;
    void **val;
    int len;
    mapper_property prop;
    mapper_type type;
    char flags;
} mapper_table_record_t;

/*! Used to hold look-up tables. */
typedef struct _mapper_table {
    mapper_table_record_t *records;
    int num_records;
    int alloced;
    char dirty;
} mapper_table_t, *mapper_table;

typedef struct _mapper_dict {
    struct _mapper_table *synced;
    struct _mapper_table *staged;
    int mask;
} mapper_dict_t, *mapper_dict;

/**** Graph ****/

/*! A list of function and context pointers. */
typedef struct _fptr_list {
    void *f;
    void *context;
    struct _fptr_list *next;
    int types;
} *fptr_list;

typedef struct _mapper_subscription {
    struct _mapper_subscription *next;
    mapper_device dev;
    int flags;
    uint32_t lease_expiration_sec;
} *mapper_subscription;

/*! A structure that keeps information about network communications. */
typedef struct _mapper_network {
    struct _mapper_graph *graph;
    struct {
        union {
            lo_server all[4];
            struct {
                lo_server_thread admin[2];
                lo_server device[2];
            };
            struct {
                lo_server_thread bus;   /*!< LibLo server for the multicast. */
                lo_server_thread mesh;  /*!< LibLo server for mesh comms. */

                /*! Servers used to handle incoming signal messages. */
                lo_server udp;
                lo_server tcp;
            };
        };
    } server;

    struct {
        lo_address bus;             /*!< LibLo address for the multicast bus. */
        lo_address dst;
    } addr;

    struct {
        char *name;                 /*!< The name of the network interface
                                     *   for receiving messages. */
        struct in_addr addr;        /*!< The IP address of interface. */
    } iface;

    struct _mapper_device *dev;     /*!< Device that this structure is
                                     *   in charge of. */
    lo_bundle bundle;               /*!< Bundle pointer for sending
                                     *   messages on the multicast bus. */

    struct {
        char *group;
        int port;
    } multicast;

    int random_id;                  /*!< Random id for allocation speedup. */
    int msgs_recvd;                 /*!< 1 if messages have been received on the
                                     *   multicast bus/mesh. */
    int msg_type;
    uint32_t next_ping;
    uint8_t graph_methods_added;
} mapper_network_t, *mapper_network;

typedef struct _mapper_graph {
    mapper_network_t net;
    mapper_device devs;                 //<! List of devices.
    mapper_signal sigs;                 //<! List of signals.
    mapper_map maps;                    //<! List of maps.
    mapper_link links;                  //<! List of links.
    fptr_list callbacks;                //<! List of object record callbacks.

    /*! Linked-list of autorenewing device subscriptions. */
    mapper_subscription subscriptions;

    /*! Flags indicating whether information on signals and mappings should
     *  be automatically subscribed to when a new device is seen.*/
    int autosubscribe;

    int own;

    uint32_t resource_counter;
} mapper_graph_t, *mapper_graph;

/**** Messages ****/

/*! Some useful strings for sending administrative messages. */
typedef enum {
    MSG_DEVICE,
    MSG_DEVICE_MODIFY,
    MSG_LOGOUT,
    MSG_MAP,
    MSG_MAP_TO,
    MSG_MAPPED,
    MSG_MAP_MODIFY,
    MSG_NAME_PROBE,
    MSG_NAME_REG,
    MSG_PING,
    MSG_SIGNAL,
    MSG_SIGNAL_REMOVED,
    MSG_SIGNAL_MODIFY,
    MSG_SUBSCRIBE,
    MSG_SYNC,
    MSG_UNMAP,
    MSG_UNMAPPED,
    MSG_WHO,
    NUM_MSG_STRINGS
} network_msg_t;

/*! Function to call when an allocated resource is locked. */
typedef void mapper_resource_on_lock(struct _mapper_allocated_t *resource);

/*! Function to call when an allocated resource encounters a collision. */
typedef void mapper_resource_on_collision(struct _mapper_allocated_t *resource);

/*! Allocated resources */
typedef struct _mapper_allocated_t {
    double count_time;          /*!< The last time at which the collision count
                                 *   was updated. */
    double hints[8];            //!< Availability of a range of resource values.

    //!< Function to call when resource becomes locked.
    mapper_resource_on_lock *on_lock;

   //! Function to call when resource collision occurs.
    mapper_resource_on_collision *on_collision;

    unsigned int val;           //!< The resource to be allocated.
    int collision_count;        /*!< The number of collisions detected for this
                                 *   resource. */
    uint8_t locked;             /*!< Whether or not the value has been locked
                                 *   in (allocated). */
    uint8_t online;             /*!< Whether or not we are connected to the
                                 *   distributed allocation network. */
} mapper_allocated_t, *mapper_allocated;

/*! Clock and timing information. */
typedef struct _mapper_sync_time_t {
    lo_timetag time;
    int msg_id;
} mapper_sync_time_t;

typedef struct _mapper_sync_clock_t {
    double rate;
    double offset;
    double latency;
    double jitter;
    mapper_sync_time_t sent;
    mapper_sync_time_t response;
    int new;
} mapper_sync_clock_t, *mapper_sync_clock;

typedef struct _mapper_subscriber {
    struct _mapper_subscriber *next;
    lo_address                      addr;
    uint32_t                        lease_exp;
    int                             flags;
} *mapper_subscriber;

#define TIMEOUT_SEC 10              // timeout after 10 seconds without ping

/**** Object ****/

typedef struct _mapper_object
{
    mapper_graph graph;             //!< Pointer back to the graph.
    mapper_id id;                   //!< Unique id for this object.
    void *user;                     //!< User context pointer.
    struct _mapper_dict props;      //!< Properties associated with this signal.
    int version;                    //!< Version number.
    mapper_object_type type;        //!< Object type.
} mapper_object_t, *mapper_object;

/**** Signal ****/

/*! A structure that stores the current and historical values and times
 *  of a signal. The size of the history arrays is determined by the needs
 *  of mapping expressions.
 *  @ingroup signals */

typedef struct _mapper_hist
{
    void *val;                  /*!< Value of the signal for each sample of
                                 *   stored history. */
    mapper_time_t *time;        //!< Time for each sample of stored history.
    int len;                    //!< Vector length.
    int pos;                    //!< Current position in the circular buffer.
    char size;                  //!< History size of the buffer.
    mapper_type type;           /*!< The type of this signal, specified as an
                                 *   OSC type character. */
} mapper_hist_t, *mapper_hist;

/*! Bit flags for indicating signal instance status. */
#define RELEASED_LOCALLY  0x01
#define RELEASED_REMOTELY 0x02

/*! A signal is defined as a vector of values, along with some metadata. */
typedef struct _mapper_signal_inst
{
    mapper_id id;               //!< User-assignable instance id.
    void *user;                 //!< User data of this instance.
    mapper_time_t created;      //!< The instance's creation timestamp.
    char *has_val_flags;        //!< Indicates which vector elements have a value.

    void *val;                  //!< The current value of this signal instance.
    mapper_time_t time;         //!< The time associated with the current value.

    int idx;                    //!< Index for accessing value history.
    uint8_t has_val;            //!< Indicates whether this instance has a value.
    uint8_t active;             //!< Status of this instance.
} mapper_signal_inst_t, *mapper_signal_inst;

typedef struct _mapper_signal_idmap
{
    struct _mapper_idmap *map;                  //!< Associated mapper_idmap.
    struct _mapper_signal_inst *inst;           //!< Signal instance.
    int status;                                 /*!< Either 0 or a combination of
                                                 *   MAPPER_RELEASED_LOCALLY and
                                                 *   MAPPER_RELEASED_REMOTELY. */
} mapper_signal_idmap_t;

typedef struct _mapper_local_signal
{
    /*! The device associated with this signal. */
    struct _mapper_device *dev;

    /*! ID maps and active instances. */
    struct _mapper_signal_idmap *idmaps;
    int idmap_len;

    /*! Array of pointers to the signal instances. */
    struct _mapper_signal_inst **inst;

    /*! Bitflag value when entire signal vector is known. */
    char *vec_known;

    /*! Type of voice stealing to perform on instances. */
    mapper_stealing_type stealing_mode;

    /*! An optional function to be called when the signal value changes. */
    void *update_handler;

    /*! An optional function to be called when the signal instance management
     *  events occur. */
    void *inst_event_handler;

    /*! Flags for deciding when to call the instance event handler. */
    int inst_event_flags;

    mapper_signal_group group;
} mapper_local_signal_t, *mapper_local_signal;

/*! A record that describes properties of a signal. */
struct _mapper_signal {
    mapper_object_t obj;        // always first
    mapper_local_signal local;
    mapper_device dev;
    char *path;         //! OSC path.  Must start with '/'.
    char *name;         //! The name of this signal (path+1).

    char *unit;         //!< The unit of this signal, or NULL for N/A.
    void *min;          //!< The minimum of this signal, or NULL for N/A.
    void *max;          //!< The maximum of this signal, or NULL for N/A.

    float period;       //!< Estimate of the update rate of this signal.
    float jitter;       //!< Estimate of the timing jitter of this signal.

    int dir;            //!< DIR_OUTGOING / DIR_INCOMING / DIR_BOTH
    int len;            //!< Length of the signal vector, or 1 for scalars.
    int num_inst;       //!< Number of instances.
    int num_maps_in;
    int num_maps_out;

    mapper_type type;   /*! The type of this signal, specified as an OSC type
                         *  character. */
};

/**** Router ****/

typedef struct _mapper_queue {
    mapper_time_t time;
    struct {
        lo_bundle udp;
        lo_bundle tcp;
    } bundle;
    struct _mapper_queue *next;
} *mapper_queue;

typedef struct _mapper_link {
    mapper_object_t obj;                // always first
    union {
        mapper_device devs[2];
        struct {
            mapper_device local_dev;
            mapper_device remote_dev;
        };
    };
    int *num_maps;

    struct {
        lo_address admin;           //!< Network address of remote endpoint
        lo_address udp;             //!< Network address of remote endpoint
        lo_address tcp;             //!< Network address of remote endpoint
    } addr;

    mapper_queue queues;                /*!< Linked-list of message queues
                                         *   waiting to be sent. */
    mapper_sync_clock_t clock;
} mapper_link_t, *mapper_link;

/**** Maps and Slots ****/

#define MAX_NUM_MAP_SRC     8    // arbitrary
#define MAX_NUM_MAP_DST     8    // arbitrary

#define STATUS_STAGED       0x00
#define STATUS_LINK_KNOWN   0x01
#define STATUS_READY        0x0F
#define STATUS_ACTIVE       0x1F

typedef struct _mapper_local_slot {
    // each slot can point to local signal or a remote link structure
    struct _mapper_router_sig *rsig;    //!< Parent signal if local
    mapper_hist hist;                   /*!< Array of value histories for
                                         *   each signal instance. */
    int hist_size;                      //!< History size.
    char status;
} mapper_local_slot_t, *mapper_local_slot;

typedef struct _mapper_slot {
    mapper_object_t obj;                // always first
    mapper_local_slot local;            //!< Pointer to local resources if any
    struct _mapper_map *map;            //!< Pointer to parent map
    mapper_signal sig;                  //!< Pointer to parent signal
    mapper_link link;

    void *min;                          //!< Array of minima, or NULL for N/A
    void *max;                          //!< Array of maxima, or NULL for N/A
    int num_inst;

    int dir;                            //!< DI_INCOMING or DI_OUTGOING
    int causes_update;                  //!< 1 if causes update, 0 otherwise.
    int use_inst;                       //!< 1 if using instances, 0 otherwise.
    int calib;                          //!< >1 if calibrating, 0 otherwise
} mapper_slot_t, *mapper_slot;

/*! The mapper_local_map structure is a linked list of mappings for a given
 *  signal.  Each signal can be associated with multiple inputs or outputs. This
 *  structure only contains state information used for performing mapping, the
 *  properties are publically defined in mapper_constants.h. */
typedef struct _mapper_local_map {
    struct _mapper_router *rtr;

    mapper_expr expr;                   //!< The mapping expression.
    mapper_hist *expr_var;              //!< User variables values.
    int num_expr_var;                   //!< Number of user variables.
    int num_var_inst;

    uint8_t is_local_only;
    uint8_t one_src;
} mapper_local_map_t, *mapper_local_map;

/*! A record that describes the properties of a mapping.
 *  @ingroup map */
typedef struct _mapper_map {
    mapper_object_t obj;                // always first
    mapper_local_map local;
    mapper_slot *src;
    mapper_slot dst;

    mapper_device *scopes;

    char *expr_str;

    int muted;                          //!< 1 to mute mapping, 0 to unmute
    int num_scopes;
    int num_src;
    mapper_location process_loc;
    int status;
    int protocol;                       //!< Data transport protocol.
} mapper_map_t, *mapper_map;

/*! The router_sig is a linked list containing a signal and a list of mapping
 *  slots.  TODO: This should be replaced with a more efficient approach
 *  such as a hash table or search tree. */
typedef struct _mapper_router_sig {
    struct _mapper_router_sig *next;    //!< The next router_sig in the list.

    struct _mapper_router *link;        //!< The parent link.
    struct _mapper_signal *sig;         //!< The associated signal.

    mapper_slot *slots;
    int num_slots;
    int id_counter;

} *mapper_router_sig;

/*! The router structure. */
typedef struct _mapper_router {
    struct _mapper_device *dev;     //!< The device associated with this link.
    mapper_router_sig sigs;      //!< The list of mappings for each signal.
} mapper_router_t, *mapper_router;

/*! The instance ID map is a linked list of int32 instance ids for coordinating
 *  remote and local instances. */
typedef struct _mapper_idmap {
    struct _mapper_idmap *next;    //!< The next id map in the list.

    mapper_id global;               //!< Hash for originating device.
    mapper_id local;                //!< Local instance id to map.
    int refcount_local;
    int refcount_global;
} mapper_idmap_t, *mapper_idmap;

/**** Device ****/

typedef struct _mapper_local_device {
    mapper_allocated_t ordinal;     /*!< A unique ordinal for this device
                                     *   instance. */
    int registered;                 /*!< Non-zero if this device has been
                                     *   registered. */

    int n_output_callbacks;
    mapper_router rtr;

    mapper_subscriber subscribers;  /*!< Linked-list of subscribed peers. */

    struct {
        /*! The list of active instance id maps. */
        struct _mapper_idmap **active;

        /*! The list of reserve instance id maps. */
        struct _mapper_idmap *reserve;
    } idmaps;

    // TODO: move to network
    int link_timeout_sec;   /* Number of seconds after which unresponsive
                             * links will be removed, or 0 for never. */

    int num_signal_groups;
} mapper_local_device_t, *mapper_local_device;


/*! A record that keeps information about a device. */
struct _mapper_device {
    mapper_object_t obj;        // always first
    mapper_local_device local;

    char *identifier;           //!< The identifier (prefix) for this device.
    char *name;                 //!< The full name for this device, or zero.

    mapper_time_t synced;       //!< Timestamp of last sync.

    int ordinal;
    int num_inputs;             //!< Number of associated input signals.
    int num_outputs;            //!< Number of associated output signals.
    int num_maps_in;            //!< Number of associated incoming maps.
    int num_maps_out;           //!< Number of associated outgoing maps.
    int status;

    uint8_t subscribed;
};

/**** Messages ****/
/* For property indexes, bits 1–8 are used for numberical index, bits 9–14 are
 * used for the mapper_property enum. */
#define PROP_ADD        0x04000
#define PROP_REMOVE     0x08000
#define DST_SLOT_PROP   0x10000
#define SRC_SLOT_PROP_BIT_OFFSET    17
#define SRC_SLOT_PROP(idx) ((idx + 1) << SRC_SLOT_PROP_BIT_OFFSET)
#define SRC_SLOT(idx) ((idx >> SRC_SLOT_PROP_BIT_OFFSET) - 1)
#define MASK_PROP_BITFLAGS(idx) (idx & 0x3F00)
#define PROP_TO_INDEX(prop) ((prop & 0x3F00) >> 8)
#define INDEX_TO_PROP(idx) (idx << 8)

/* Maximum number of "extra" properties for a signal, device, or map. */
#define NUM_EXTRA_PROPS 20

/*! Queriable representation of a parameterized message parsed from an incoming
 *  OSC message. Does not contain a copy of data, so only valid for the duration
 *  of the message handler. Also allows for a constant number of "extra"
 *  parameters; that is, unknown parameters that may be specified for a signal
 *  and used for metadata, which will be added to a general-purpose string table
 *  associated with the signal. */
typedef struct _mapper_msg_atom
{
    const char *name;
    lo_arg **vals;
    const char *types;
    int len;
    mapper_property prop;
} mapper_msg_atom_t, *mapper_msg_atom;

typedef struct _mapper_msg
{
    mapper_msg_atom_t *atoms;
    int num_atoms;
} *mapper_msg;

#endif // __MAPPER_TYPES_H__
