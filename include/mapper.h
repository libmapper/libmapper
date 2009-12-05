// -*- mode:c++; indent-tabs-mode:nil; c-basic-offset:4; compile-command:"scons -DQ debug=1" -*-

#ifndef __MAPPER_H__
#define __MAPPER_H__

#include <lo/lo.h>
#include <sys/socket.h>

/***************/
/**** Types ****/
/***************/

/*! A structure to keep information about a namespace address */
struct mapper_method_;
typedef struct mapper_method_* mapper_method;
typedef struct mapper_method_ {
    mapper_method next;
    char *path;
    char *types;
} mapper_method_t;

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
    mapper_method_t         *input_head;    //<! List of messages accepted by this device.
    mapper_method_t         *output_head;   //<! List of messages transmitted by this device.
    char                     interface[16]; //<! The name of the network interface for receiving messages.
    struct in_addr           interface_ip;  //<! The IP address of interface.
    int                      registered;    //<! Non-zero if this device has been registered.
} mapper_admin_t;

/*! The handle to this device is a pointer. */
typedef mapper_admin_t *mapper_admin;

/*******************/
/**** Functions ****/
/*******************/

int mapper_admin_init();

mapper_admin mapper_admin_new(char *identifier, mapper_device_type_t type,
                              int initial_port);
void mapper_admin_free(mapper_admin admin);
void mapper_admin_poll(mapper_admin admin);
void mapper_admin_port_announce(mapper_admin admin);
void mapper_admin_name_announce(mapper_admin admin);

int mapper_admin_input_add(mapper_admin admin, const char *path, const char *types);
int mapper_admin_input_remove(mapper_admin admin, const char *path, const char *types);
int mapper_admin_output_add(mapper_admin admin, const char *path, const char *types);
int mapper_admin_output_remove(mapper_admin admin, const char *path, const char *types);

int mapper_method_add(mapper_method *head, const char *path, const char *types);
int mapper_method_remove(mapper_method *head, const char *path, const char *types);
int mapper_method_list_free(mapper_method *head);
int mapper_method_list_count(mapper_method head);

#endif // __MAPPER_H__
