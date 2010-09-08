
#ifndef __MAPPER_TYPES_H__
#define __MAPPER_TYPES_H__

#include <lo/lo_lowlevel.h>
#include "operations.h"
#include "expression.h"

#include <mapper/mapper_db.h>

/**** Defined in mapper.h ****/

/* Types defined here replace opaque prototypes in mapper.h, thus we
 * cannot include it here.  Instead we include some prototypes here.
 * Typedefs cannot be repeated, therefore they are refered to by
 * struct name. */

struct _mapper_signal;
struct _mapper_admin;

/* Forward declarations for this file. */

struct _mapper_admin_allocated_t;
struct _mapper_device;

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
    mapper_admin_allocated_t ordinal; /*!< The unique ordinal for this
                                       *   device. */
    mapper_admin_allocated_t port;    /*!< This device's UDP port number. */
    lo_server_thread admin_server;    /*!< LibLo server thread for the
                                       *   admin bus. */
    lo_address admin_addr;            /*!< LibLo address for the admin
                                       *   bus. */
    char interface[16];               /*!< The name of the network
                                       *   interface for receiving
                                       *   messages. */
    struct in_addr interface_ip;      /*!< The IP address of interface. */
    int registered;                   /*!< Non-zero if this device has
                                       *   been registered. */
    struct _mapper_device *device;    /*!< Device that this admin is
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
    int order_input;                /*!< Order of the input side of
                                     *   the difference equation. */
    int order_output;               /*!< Order of the output side of
                                     *   the difference equation. */

    /*! Coefficients for the input polynomial. */
    float coef_input[MAX_HISTORY_ORDER];

    /*! Coefficients for the output polynomial. */
    float coef_output[MAX_HISTORY_ORDER];

    float history_input[MAX_HISTORY_ORDER];  //!< History of input.
    float history_output[MAX_HISTORY_ORDER]; //!< History of output.

    int history_pos;   //!< Position in history ring buffers.

    int calibrating;   /*!< 1 if the source range is currently being
                        *   calibrated, 0 otherwise. */

    mapper_expr_tree expr_tree;  //!< Tree representing the mapping expression.
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
