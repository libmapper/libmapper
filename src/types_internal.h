
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

/* Forward declarations for this file. */

struct _mapper_admin_allocated_t;
struct _mapper_device;

/**** Admin bus ****/

/*! Types of devices supported by the admin bus. */
typedef enum {
    MAPPER_DEVICE_CONTROLLER,
    MAPPER_DEVICE_SYNTH,
    MAPPER_DEVICE_ROUTER,
    MAPPER_DEVICE_MAPPER
} mapper_device_type_t;

/*! Function to call when an allocated resource is locked. */
typedef void mapper_admin_resource_on_lock(struct _mapper_device *md,
                                           struct _mapper_admin_allocated_t *resource);

/*! Allocated resources */
typedef struct _mapper_admin_allocated_t {
    unsigned int value;            //!< The resource to be allocated.
    unsigned int collision_count;  //!< The number of collisions detected for this resource.
    double count_time;             //!< The last time at which the collision count was updated.
    int locked;                    //!< Whether or not the value has been locked in (i.e., allocated).
    mapper_admin_resource_on_lock *on_lock; //!< Function to call when resource becomes locked.
} mapper_admin_allocated_t;


/*! A structure that keeps information about a device. */
typedef struct
{
    char*                    			identifier;    //!< The identifier for this device.
    mapper_admin_allocated_t 			ordinal;       //!< The unique ordinal for this device.
    mapper_device_type_t     			device_type;   //!< The type of this device.
    mapper_admin_allocated_t 			port;          //!< This device's UDP port number.
    lo_server_thread         			admin_server;  //!< LibLo server thread for the admin bus.
    lo_address               			admin_addr;    //!< LibLo address for the admin bus.
    char                     			interface[16]; //!< The name of the network interface for receiving messages.
    struct in_addr           			interface_ip;  //!< The IP address of interface.
    int                      			registered;    //!< Non-zero if this device has been registered.
    struct _mapper_device   		 	*device;       //!< Device that this admin is in charge of.
} mapper_admin_t;

/*! The handle to this device is a pointer. */
typedef mapper_admin_t *mapper_admin;


/**** Router ****/

/*! Describes what happens when the clipping boundaries are
 *  exceeded. */
typedef enum _mapper_clipping_type
{
    CT_LIMIT,     //!< Value is limited to the boundary.  This is the default.
    CT_REFLECT,   //!< Value continues in opposite direction.
    CT_ROTATE,    //!< Value appears as offset at the opposite boundary.
} mapper_clipping_type;



typedef enum _mapper_mapping_type
{
    BYPASS, //!< Direct mapping
    LINEAR, //!< Linear mapping
    EXPRESSION, //!< Expression mapping
	MUTE, //!< Mute mapping
} mapper_mapping_type;

/*! The mapping structure is a linked list of mappings for a given
 *  signal.  Each signal can be associated with multiple outputs. */

typedef struct _mapper_mapping {
    char *name;                              //!< Destination name (OSC path).
    struct _mapper_mapping *next;            //!< Next mapping in the list.
    int order_input;                         //!< Order of the input side of the difference equation.
                                             //!< Maximum of MAX_HISTORY_ORDER.
    int order_output;                        //!< Order of the output side of the difference equation.
                                             //!< Usually not less than 1.  Maximum of MAX_HISTORY_ORDER.
    float coef_input[MAX_HISTORY_ORDER];     //!< Coefficients for the input polynomial.
    float coef_output[MAX_HISTORY_ORDER];    //!< Coefficients for the output polynomial.
    float history_input[MAX_HISTORY_ORDER];  //!< History of input.
    float history_output[MAX_HISTORY_ORDER]; //!< History of output.
    int history_pos;                         //!< Position in history ring buffers.
    mapper_clipping_type clip_upper;         //!< Operation for exceeded upper boundary.
    mapper_clipping_type clip_lower;         //!< Operation for exceeded lower boundary.
	
	float range[4];							 //!< {src_min ; src_max ; dest_min ; dest_max}
	char *expression;
	    
	mapper_mapping_type type;                //!< Bypass, linear, or expression mapping
    Tree *expr_tree;                         //!< Tree representing the mapping expression
} *mapper_mapping;


/*! The signal mapping is a linked list containing a signal and a list
 *  of mappings.  For each router, there is one per signal of the
 *  associated device.  TODO: This should be replaced with a more
 *  efficient approach such as a hash table or search tree. */
typedef struct _mapper_signal_mapping {
    struct _mapper_signal *signal;         //!< The associated signal.
    mapper_mapping mapping;                //!< The first mapping for this signal.
    struct _mapper_signal_mapping *next;   //!< The next signal mapping in the list.
} *mapper_signal_mapping;

/*! The router structure is a linked list of routers each associated
 *  with a destination address that belong to a controller device. */
typedef struct _mapper_router {             
	const char *target_name;                //!< Router name given by the target name.
    lo_address addr;                      //!< Sending address.
    struct _mapper_router *next;          //!< Next router in the list.
    mapper_signal_mapping mappings;       //!< The list of mappings for each signal.
} *mapper_router;

/**** Device ****/

typedef struct _mapper_device {
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

#endif // __MAPPER_TYPES_H__
