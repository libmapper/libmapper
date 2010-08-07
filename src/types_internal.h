
#ifndef __MAPPER_TYPES_H__
#define __MAPPER_TYPES_H__

#include <lo/lo_lowlevel.h>
#include "operations.h"
#include "expression.h"

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

/**** Constants ****/

#define MAX_ARGS 32

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
    unsigned int collision_count; /*!< The number of collisions
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

/*! Describes what happens when the clipping boundaries are
 *  exceeded. */
typedef enum _mapper_clipping_type {
    CT_NONE,    /*!< Value is passed through unchanged. This is the
                 *   default. */
    CT_MUTE,    //!< Value is muted.
    CT_CLAMP,   //!< Value is limited to the boundary.
    CT_FOLD,    //!< Value continues in opposite direction.
    CT_WRAP,    /*!< Value appears as modulus offset at the opposite
                 *   boundary. */
} mapper_clipping_type;



typedef enum _mapper_mapping_type {
    BYPASS,                     //!< Direct mapping
    LINEAR,                     //!< Linear mapping
    EXPRESSION,                 //!< Expression mapping
    CALIBRATE,                  //!< Calibrate to input
    MUTE,                       //!< Mute mapping
} mapper_mapping_type;


/* Bit flags to identify which range extremities are known. If the bit
 * field is equal to RANGE_KNOWN, then all four required extremities
 * are known, and a linear mapping can be calculated. */
#define RANGE_SRC_MIN  0x01
#define RANGE_SRC_MAX  0x02
#define RANGE_DEST_MIN 0x04
#define RANGE_DEST_MAX 0x08
#define RANGE_KNOWN    0x0F

typedef struct _mapper_mapping_range {
    float src_min;              //!< Source minimum.
    float src_max;              //!< Source maximum.
    float dest_min;             //!< Destination minimum.
    float dest_max;             //!< Destination maximum.
    int known;                  /*!< Bitfield identifying known range
                                 *   extremities. */
    int rewrite;                //!< Need to overwrite src_min and src_max?
} mapper_mapping_range_t;

/*! The mapping structure is a linked list of mappings for a given
 *  signal.  Each signal can be associated with multiple outputs. */

typedef struct _mapper_mapping {
    char *name;                     //!< Destination name (OSC path).
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

    int history_pos;                  /*!< Position in history ring
                                       *   buffers. */
    mapper_clipping_type clip_upper;  /*!< Operation for exceeded
                                       *   upper boundary. */
    mapper_clipping_type clip_lower;  /*!< Operation for exceeded
                                       *   lower boundary. */

    mapper_mapping_range_t range;     //!< Range information.
    char *expression;

    mapper_mapping_type type;   /*!< Bypass, linear, calibrate, or
                                 *   expression mapping */
    int muted;                  /*!< 1 to mute mapping connection, 0
                                 *   to unmute */
    Tree *expr_tree;            /*!< Tree representing the mapping
                                 *   expression */
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
    const char *target_name;        /*!< Router name given by the
                                     *   target name. */
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
    int num_routers;
    int num_mappings_out;
} *mapper_device;

/**** Local device database ****/

/*! A structure that keeps information sent by /registered. */
typedef struct _mapper_db_registered {
    char *full_name;
    char *host;
    int port;
    char *canAlias;
    struct _mapper_db_registered *next;
} *mapper_db_registered;

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
    AT_SCALING,
    AT_EXPRESSION,
    AT_CLIPMIN,
    AT_CLIPMAX,
    AT_RANGE,
    N_AT_PARAMS
} mapper_msg_param_t;

extern const char* mapper_msg_param_strings[];

/*! Queriable representation of a parameterized message parsed from an
 *  incoming OSC message. Does not contain a copy of data, so only
 *  valid for the duration of the message handler. */
typedef struct _mapper_message
{
    const char *path;                    //!< OSC address.
    mapper_msg_param_t params[MAX_ARGS]; //!< Array of parameter symbols.
    lo_arg **values[MAX_ARGS];           //!< Array of parameter values.
    int n_pairs;                         /*!< Number of items in
                                          *   params and values
                                          *   arrays. */
} mapper_message_t;

#endif // __MAPPER_TYPES_H__
