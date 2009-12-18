
#ifndef __MAPPER_TYPES_H__
#define __MAPPER_TYPES_H__

#include <lo/lo_lowlevel.h>


/**** Admin bus ****/

/*! Types of devices supported by the admin bus. */
typedef enum {
    MAPPER_DEVICE_CONTROLLER,
    MAPPER_DEVICE_SYNTH,
    MAPPER_DEVICE_ROUTER,
    MAPPER_DEVICE_MAPPER
} mapper_device_type_t;

/*! Allocated resources */
typedef struct {
    unsigned int value;            //<! The resource to be allocated.
    unsigned int collision_count;  //<! The number of collisions detected for this resource.
    double count_time;             //<! The last time at which the collision count was updated.
    int locked;                    //<! Whether or not the value has been locked in (i.e., allocated).
} mapper_admin_allocated_t;

/*! A structure that keeps information about a device. */
typedef struct
{
    char*                    identifier;    //<! The identifier for this device.
    mapper_admin_allocated_t ordinal;       //<! The unique ordinal for this device.
    mapper_device_type_t     device_type;   //<! The type of this device.
    mapper_admin_allocated_t port;          //<! This device's UDP port number.
    lo_server_thread         admin_server;  //<! LibLo server thread for the admin bus.
    lo_address               admin_addr;    //<! LibLo address for the admin bus.
    char                     interface[16]; //<! The name of the network interface for receiving messages.
    struct in_addr           interface_ip;  //<! The IP address of interface.
    int                      registered;    //<! Non-zero if this device has been registered.
} mapper_admin_t;

/*! The handle to this device is a pointer. */
typedef mapper_admin_t *mapper_admin;


/**** Router ****/

typedef struct _mapper_router {
    lo_address addr; //<! Sending address.
} *mapper_router;

/**** Device ****/

struct _mapper_signal;

typedef struct _mapper_device {
    char *name_prefix;
    mapper_admin admin;
    struct _mapper_signal **inputs;
    struct _mapper_signal **outputs;
    int n_inputs;
    int n_outputs;
    int n_alloc_inputs;
    int n_alloc_outputs;
    mapper_router *routers;
} *mapper_device;

#endif // __MAPPER_TYPES_H__
