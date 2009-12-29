
#include <lo/lo.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <ifaddrs.h>
#include <arpa/inet.h>
#include <sys/time.h>

#include "mapper_internal.h"
#include "types_internal.h"
#include "config.h"
#include <mapper/mapper.h>

/*! Internal function to get the current time. */
static double get_current_time()
{
#ifdef HAVE_GETTIMEOFDAY
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (double)tv.tv_sec + tv.tv_usec / 1000000.0;
#else
    #error No timing method known on this platform.
#endif
}

/* Internal message handler prototypes. */
static int handler_device_who(const char*, const char*, lo_arg **, int, lo_message, void*);
static int handler_id_n_namespace_get(const char*, const char*, lo_arg **, int, lo_message, void*);
static int handler_device_alloc_port(const char*, const char*, lo_arg **, int, lo_message, void*);
static int handler_device_alloc_name(const char*, const char*, lo_arg **, int, lo_message, void*);

/* Internal LibLo error handler */
static void handler_error(int num, const char *msg, const char *where)
{
    printf("liblo server error %d in path %s: %s\n", num, where, msg);
}

/* Functions for handling the resource allocation scheme.  If
 * check_collisions() returns 1, the resource in question should be
 * announced on the admin bus. */
static int check_collisions(mapper_admin admin,
                            mapper_admin_allocated_t *resource);
static void on_collision(mapper_admin_allocated_t *resource);

/*! Local function to get the IP address of a network interface. */
static struct in_addr get_interface_addr(const char *ifname)
{
    struct ifaddrs   *ifaphead;
    struct ifaddrs   *ifap;
    struct in_addr    error = {0};

    if (getifaddrs(&ifaphead)!=0)
        return error;

    ifap = ifaphead;
    while (ifap) {
        struct sockaddr_in *sa = (struct sockaddr_in*)ifap->ifa_addr;
        if (sa->sin_family == AF_INET
            && strcmp(ifap->ifa_name, ifname)==0 )
        {
            return sa->sin_addr;
        }
        ifap = ifap->ifa_next;
    }

    return error;
}

/*! Allocate and initialize a new admin structure.
 *  \param identifier An identifier for this device which does not
 *  need to be unique.
 *  \param type The device type for this device. (Data direction,
 *  functionality.)
 *  \param initial_port The initial UDP port to use for this
 *  device. This will likely change within a few minutes after the
 *  device is allocated.
 *  \return A newly initialized mapper admin structure.
 */
mapper_admin mapper_admin_new(const char *identifier,
                              mapper_device device,
                              mapper_device_type_t type,
                              int initial_port)
{
    mapper_admin admin = malloc(sizeof(mapper_admin_t));
    if (!admin)
        return NULL;

    // Initialize interface information
    // We'll use defaults for now, perhaps this should be configurable in the future.
    {
        char *eths[] = {"eth0", "eth1", "eth2", "eth3", "eth4",
                        "en0", "en1", "en2", "en3", "en4", "lo"};
        int num = sizeof(eths)/sizeof(char*), i;
        for (i=0; i<num; i++) {
            admin->interface_ip = get_interface_addr(eths[i]);
            if (admin->interface_ip.s_addr != 0) {
                strcpy(admin->interface, eths[i]);
                break;
            }
        }
    }

    /* Open address for multicast group 224.0.1.3, port 7570 */
    admin->admin_addr = lo_address_new("224.0.1.3", "7570");
    if (!admin->admin_addr) {
        free(admin->identifier);
        free(admin);
        return NULL;
    }

    /* Set TTL for packet to 1 -> local subnet */
    lo_address_set_ttl(admin->admin_addr, 1);

    /* Open server for multicast group 224.0.1.3, port 7570 */
    admin->admin_server = lo_server_new_multicast("224.0.1.3", "7570", handler_error);
    if (!admin->admin_server) {
        free(admin->identifier);
        lo_address_free(admin->admin_addr);
        free(admin);
        return NULL;
    }

    /* Initialize data structures */
    admin->identifier = strdup(identifier);
    admin->device_type = type;
    admin->ordinal.value = 1;
    admin->ordinal.locked = 0;
    admin->ordinal.collision_count = -1;
    admin->ordinal.count_time = get_current_time();
    admin->port.value = initial_port;
    admin->port.locked = 0;
    admin->port.collision_count = -1;
    admin->port.count_time = get_current_time();
    admin->registered = 0;
    admin->device = device;

    /* Add methods for admin bus.  Only add methods needed for
     * allocation here. Further methods are added when the device is
     * registered. */
    lo_server_add_method(admin->admin_server, "/device/alloc/port", NULL, handler_device_alloc_port, admin);
    lo_server_add_method(admin->admin_server, "/device/alloc/name", NULL, handler_device_alloc_name, admin);

    /* Announce port and name to admin bus. */
    mapper_admin_port_announce(admin);
    mapper_admin_name_announce(admin);

    return admin;
}

/*! Free the memory allocated by a mapper admin structure.
 *  \param admin An admin structure handle.
 */
void mapper_admin_free(mapper_admin admin)
{
    if (!admin)
        return;

    if (admin->identifier)
        free(admin->identifier);

    if (admin->admin_server)
        lo_server_free(admin->admin_server);

    if (admin->admin_addr)
        lo_address_free(admin->admin_addr);

    free(admin);
}

/*! This is the main function to be called once in a while from a
 *  program so that the admin bus can be automatically managed.
 */
void mapper_admin_poll(mapper_admin admin)
{
    lo_server_recv_noblock(admin->admin_server, 0);

    /* If the port is not yet locked, process collision timing.  Once
     * the port is locked it won't change. */
    if (!admin->port.locked)
        if (check_collisions(admin, &admin->port))
            /* If the port has changed, re-announce the new port. */
            mapper_admin_port_announce(admin);

    /* If the ordinal is not yet locked, process collision timing.
     * Once the ordinal is locked it won't change. */
    if (!admin->ordinal.locked)
        if (check_collisions(admin, &admin->ordinal))
            /* If the ordinal has changed, re-announce the new name. */
            mapper_admin_name_announce(admin);

    /* If we are ready to register the device, add the needed message
     * handlers. */
    if (!admin->registered && admin->port.locked && admin->ordinal.locked)
    {
        char namespaceget[256];
        lo_server_add_method(admin->admin_server, "/device/who", "", handler_device_who, admin);

        snprintf(namespaceget, 256, "/%s/%d/namespace/get", admin->identifier, admin->ordinal.value);
        lo_server_add_method(admin->admin_server, namespaceget, "", handler_id_n_namespace_get, admin);

        admin->registered = 1;
    }
}

/*! Announce the device's current port on the admin bus to enable the
 *  allocation protocol.
 */
void mapper_admin_port_announce(mapper_admin admin)
{
    trace("announcing port\n");
    lo_send(admin->admin_addr, "/device/alloc/port", "is", admin->port.value, admin->identifier);
}

/*! Announce the device's current name on the admin bus to enable the
 *  allocation protocol.
 */
void mapper_admin_name_announce(mapper_admin admin)
{
    char name[256];
    trace("announcing name\n");
    snprintf(name, 256, "/%s/%d", admin->identifier, admin->ordinal.value);
    lo_send(admin->admin_addr, "/device/alloc/name", "ss", name, "libmapper");
}

/*! Algorithm for checking collisions and allocating resources. */
static int check_collisions(mapper_admin admin,
                            mapper_admin_allocated_t *resource)
{
    double timediff;

    if (resource->locked)
        return 0;

    timediff = get_current_time() - resource->count_time;

    if (timediff >= 2.0) {
        resource->locked = 1;
        if (resource->on_lock)
            resource->on_lock(admin->device, resource);
    }

    else
        /* If port collisions were found within 500 milliseconds of the
         * last announcement, try a new random port. */
        if (timediff >= 0.5 && resource->collision_count > 0)
        {
            /* Otherwise, add a random number based on the number of
             * collisions. */
            resource->value += (int)(((double)rand())/RAND_MAX
                                     * (resource->collision_count+1));
            
            /* Prepare for causing new port collisions. */

            resource->collision_count = -1;
            resource->count_time = get_current_time();

            /* Indicate that we need to re-announce the new value. */
            return 1;
        }
    
    return 0;
}

static void on_collision(mapper_admin_allocated_t *resource)
{
    if (resource->locked)
        return;

    /* Count port collisions. */
    resource->collision_count ++;
    trace("%d collision_count = %d\n", resource->value, resource->collision_count);
    resource->count_time = get_current_time();
}

/**********************************/
/* Internal OSC message handlers. */
/**********************************/

/*! Respond to /device/who by announcing the current port. */
static int handler_device_who(const char *path, const char *types, lo_arg **argv,
                              int argc, lo_message msg, void *user_data)
{
    mapper_admin admin = (mapper_admin)user_data;
    char name[256];

    snprintf(name, 256, "/%s/%d", admin->identifier, admin->ordinal.value);

    lo_send(admin->admin_addr,
            "/device/registered", "ssssssisisi",
            name,
            "@class", admin->identifier,
            "@IP", inet_ntoa(admin->interface_ip),
            "@port", admin->port.value,
#if 1 // TODO
            "@inputs", 0, "@outputs", 0);
#else
            "@inputs", mapper_method_list_count(admin->input_head),
            "@outputs", mapper_method_list_count(admin->output_head));
#endif
    return 0;
}

/*! Respond to /namespace/get by enumerating all supported inputs and outputs. */
static int handler_id_n_namespace_get(const char *path, const char *types, lo_arg **argv,
                                      int argc, lo_message msg, void *user_data)
{
#if 0
    mapper_admin admin = (mapper_admin)user_data;
    char name[256], response[256], method[256];
    mapper_method node;

    snprintf(name, 256, "/%s/%d", admin->identifier, admin->ordinal.value);

    node = admin->input_head;
    strcpy(response, name);
    strcat(response, "/namespace/input");

    while (node) {
        strcpy(method, name);
        strcat(method, node->path);
        lo_send(admin->admin_addr, response, "ssssisi", method, "@type", node->types, "@min", 0, "@max", 0);
        node = node->next;
    }

    node = admin->output_head;
    strcpy(response, name);
    strcat(response, "/namespace/output");

    while (node) {
        strcpy(method, name);
        strcat(method, node->path);
        lo_send(admin->admin_addr, response, "ssssisi", method, "@type", node->types, "@min", 0, "@max", 0);
        node = node->next;
    }
#endif
    return 0;
}

static int handler_device_alloc_port(const char *path, const char *types, lo_arg **argv,
                                     int argc, lo_message msg, void *user_data)
{
    mapper_admin admin = (mapper_admin) user_data;

    unsigned int  announced_port = 0;
    char         *announced_id   = 0;  // Note: not doing anything with this right now

    if (argc < 1)
        return 0;

    if (types[0]=='i')
        announced_port = argv[0]->i;
    else if (types[0]=='f')
        announced_port = (unsigned int)argv[0]->f;
    else
        return 0;

    if (argc > 1 && (types[1]=='s' || types[1]=='S'))
        announced_id = &argv[1]->s;

    trace("got /device/alloc/port %d %s\n", announced_port, announced_id);

    /* Process port collisions. */
    if (announced_port == admin->port.value)
        on_collision(&admin->port);

    return 0;
}

static int handler_device_alloc_name(const char *path, const char *types, lo_arg **argv,
                                     int argc, lo_message msg, void *user_data)
{
    mapper_admin admin = (mapper_admin) user_data;

    char         *announced_name = 0, *s;
    unsigned int  announced_ordinal = 0;

    if (argc < 1)
        return 0;

    if (types[0]!='s' && types[0]!='S')
        return 0;

    announced_name = &argv[0]->s;

    /* Parse the ordinal from the complete name which is in the format: /<name>/<n> */
    s = announced_name;
    if (*s++ != '/') return 0;
    while (*s != '/' && *s++) {}
    announced_ordinal = atoi(++s);

    trace("got /device/alloc/name %d %s\n", announced_ordinal, announced_name);

    /* Process ordinal collisions. */
    if (announced_ordinal == admin->ordinal.value)
        on_collision(&admin->ordinal);

    return 0;
}
