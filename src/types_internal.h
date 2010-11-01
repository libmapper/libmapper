
#ifndef __MAPPER_TYPES_H__
#define __MAPPER_TYPES_H__

#include <lo/lo_lowlevel.h>

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

/*! Used to hold string look-up table nodes. */
typedef struct {
    char *key;
    void *value;
} node_t;

/*! Used to hold string look-up tables. */
typedef struct {
    node_t *store;
    int len;
    int alloced;
} table_t, *table;

/*! Create a new string table. */
table table_new();

/*! Free a string table. */
void table_free(table t);

/*! Add a string to a table. */
void table_add(table t, const char *key, void *value);

/*! Sort a table.  Call this after table_add and before table_find. */
void table_sort(table t);

/*! Look up a value in a table.  Returns 1 if found, 0 if not found,
 *  and fills in value if found. */
int table_find(table t, const char *key, void **value);

/*! Look up a value in a table.  Returns the value directly, which may
 *  be zero, but also returns 0 if not found. */
void *table_find_p(table t, const char *key);

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
    int random_id;                    /*!< Random ID for allocation
                                           speedup. */
    mapper_admin_allocated_t ordinal; /*!< The unique ordinal for this
                                       *   device. */
    mapper_admin_allocated_t port;    /*!< This device's UDP port number. */
    lo_server_thread admin_server;    /*!< LibLo server thread for the
                                       *   admin bus. */
    lo_address admin_addr;            /*!< LibLo address for the admin
                                       *   bus. */
    char *interface;                  /*!< The name of the network
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

/*! The mapping structure is a linked list of mappings for a given
 *  signal.  Each signal can be associated with multiple outputs. This
 *  structure only contains state information used for performing
 *  mapping, the mapping properties are publically defined in
 *  mapper_db.h. */

typedef struct _mapper_mapping {
    mapper_db_mapping_t props;      //!< Properties.
    struct _mapper_mapping *next;   //!< Next mapping in the list.

    int calibrating;   /*!< 1 if the source range is currently being
                        *   calibrated, 0 otherwise. */

    mapper_expr expr;  //!< The mapping expression.
} *mapper_mapping;


/*! The signal mapping is a linked list containing a signal and a list
 *  of mappings.  For each router, there is one per signal of the
 *  associated device.  TODO: This should be replaced with a more
 *  efficient approach such as a hash table or search tree. */
typedef struct _mapper_signal_mapping {
    struct _mapper_signal *signal;       //!< The associated signal.
    mapper_mapping mapping;              /*!< The first mapping for
                                          *   this signal. */
    struct _mapper_signal_mapping *next; /*!< The next signal mapping
                                          *   in the list. */
} *mapper_signal_mapping;

/*! The router structure is a linked list of routers each associated
 *  with a destination address that belong to a controller device. */
typedef struct _mapper_router {
    const char *dest_name;          /*!< Router name given by the
                                     *   destination name. */
    struct _mapper_device *device;  /*!< The device associated with
                                     *   this router */
    lo_address addr;                //!< Sending address.
    struct _mapper_router *next;    //!< Next router in the list.
    mapper_signal_mapping mappings; /*!< The list of mappings for each
                                     *   signal. */
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
    int n_alloc_inputs;
    int n_alloc_outputs;
    mapper_router routers;

    /*! Server used to handle incoming messages.  NULL until at least
     *  one input has been registered and the incoming port has been
     *  allocated. */
    lo_server server;
} *mapper_device;

/**** Monitor ****/

/*! A list of function and context pointers. */
typedef struct _fptr_list {
    void *f;
    void *context;
    struct _fptr_list *next;
} *fptr_list;

typedef struct _mapper_db {
    mapper_db_device  registered_devices;  //<! List of devices.
    mapper_db_signal  registered_inputs;   //<! List of inputs.
    mapper_db_signal  registered_outputs;  //<! List of outputs.
    mapper_db_mapping registered_mappings; //<! List of mappings.
    mapper_db_link    registered_links;    //<! List of links.
    fptr_list         device_callbacks;  //<! List of device record callbacks.
    fptr_list         signal_callbacks;  //<! List of signal record callbacks.
    fptr_list         mapping_callbacks; //<! List of mapping record callbacks.
    fptr_list         link_callbacks;    //<! List of link record callbacks.
} mapper_db_t, *mapper_db;

typedef struct _mapper_monitor {
    mapper_admin      admin;    //<! Admin for this monitor.
    mapper_db_t       db;       //<! Database for this monitor. 
}  *mapper_monitor;

/**** Messages ****/

/*! Symbolic representation of recognized @-parameters. */
typedef enum {
    AT_IP,
    AT_PORT,
    AT_CANALIAS,
    AT_NUMINPUTS,
    AT_NUMOUTPUTS,
    AT_HASH,
    AT_TYPE,
    AT_MIN,
    AT_MAX,
    AT_MINIMUM,
    AT_MAXIMUM,
    AT_SCALING,
    AT_EXPRESSION,
    AT_CLIPMIN,
    AT_CLIPMAX,
    AT_RANGE,
    AT_UNITS,
    AT_MUTE,
    AT_LENGTH,
    AT_DIRECTION,
    N_AT_PARAMS
} mapper_msg_param_t;

/*! Strings that correspond to mapper_msg_param_t. */
extern const char* mapper_msg_param_strings[];

/*! Strings that correspond to mapper_clipping_type, defined in
 *  mapper_db.h. */
extern const char* mapper_clipping_type_strings[];

/*! Strings that correspond to mapper_scaling_type, defined in
 *  mapper_db.h. */
extern const char* mapper_scaling_type_strings[];

/*! Queriable representation of a parameterized message parsed from an
 *  incoming OSC message. Does not contain a copy of data, so only
 *  valid for the duration of the message handler. */
typedef struct _mapper_message
{
    const char *path;               //!< OSC address.
    lo_arg **values[N_AT_PARAMS];   //!< Array of parameter values.
    const char *types[N_AT_PARAMS]; //!< Array of types for each value.
} mapper_message_t;

#endif // __MAPPER_TYPES_H__
