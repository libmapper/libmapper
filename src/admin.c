
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
    return (double) tv.tv_sec + tv.tv_usec / 1000000.0;
#else
#error No timing method known on this platform.
#endif
}

/* Internal message handler prototypes. */
static int handler_who(const char *, const char *, lo_arg **, int,
                       lo_message, void *);
static int handler_registered(const char *, const char *, lo_arg **,
                              int, lo_message, void *);
static int handler_id_n_namespace_input_get(const char *, const char *,
                                            lo_arg **, int, lo_message,
                                            void *);
static int handler_id_n_namespace_output_get(const char *, const char *,
                                             lo_arg **, int, lo_message,
                                             void *);
static int handler_id_n_namespace_get(const char *, const char *,
                                      lo_arg **, int, lo_message, void *);
static int handler_device_alloc_port(const char *, const char *, lo_arg **,
                                     int, lo_message, void *);
static int handler_device_alloc_name(const char *, const char *, lo_arg **,
                                     int, lo_message, void *);
static int handler_device_link(const char *, const char *, lo_arg **, int,
                               lo_message, void *);
static int handler_device_link_to(const char *, const char *, lo_arg **,
                                  int, lo_message, void *);
static int handler_device_unlink(const char *, const char *, lo_arg **,
                                 int, lo_message, void *);
static int handler_device_links_get(const char *, const char *, lo_arg **,
                                    int, lo_message, void *);
static int handler_param_connect(const char *, const char *, lo_arg **,
                                 int, lo_message, void *);
static int handler_param_connect_to(const char *, const char *, lo_arg **,
                                    int, lo_message, void *);
static int handler_param_connection_modify(const char *, const char *,
                                           lo_arg **, int, lo_message,
                                           void *);
static int handler_param_disconnect(const char *, const char *, lo_arg **,
                                    int, lo_message, void *);
static int handler_device_connections_get(const char *, const char *,
                                          lo_arg **, int, lo_message,
                                          void *);



/* Internal LibLo error handler */
static void handler_error(int num, const char *msg, const char *where)
{
    printf("liblo server error %d in path %s: %s\n", num, where, msg);
}

/* Functions for handling the resource allocation scheme.  If
 * check_collisions() returns 1, the resource in question should be
 * probed on the admin bus. */
static int check_collisions(mapper_admin admin,
                            mapper_admin_allocated_t *resource);
static void on_collision(mapper_admin_allocated_t *resource,
                         mapper_admin admin, int type);

/*! Local function to get the IP address of a network interface. */
static struct in_addr get_interface_addr(const char *ifname)
{
    struct ifaddrs *ifaphead;
    struct ifaddrs *ifap;
    struct in_addr error = { 0 };

    if (getifaddrs(&ifaphead) != 0)
        return error;

    ifap = ifaphead;
    while (ifap) {
        struct sockaddr_in *sa = (struct sockaddr_in *) ifap->ifa_addr;
        if (!sa) {
            trace("ifap->ifa_addr = 0, unknown condition.\n");
            ifap = ifap->ifa_next;
            continue;
        }
        if (sa->sin_family == AF_INET
            && strcmp(ifap->ifa_name, ifname) == 0) {
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
                              mapper_device device, int initial_port)
{
    mapper_admin admin = malloc(sizeof(mapper_admin_t));
    if (!admin)
        return NULL;

    /* Initialize interface information.  We'll use defaults for now,
     * perhaps this should be configurable in the future. */
    {
        char *eths[] = { "eth0", "eth1", "eth2", "eth3", "eth4",
                         "en0", "en1", "en2", "en3", "en4", "lo" };
        int num = sizeof(eths) / sizeof(char *), i;
        for (i = 0; i < num; i++) {
            admin->interface_ip = get_interface_addr(eths[i]);
            if (admin->interface_ip.s_addr != 0) {
                strcpy(admin->interface, eths[i]);
                break;
            }
        }
        if (i >= num) {
            trace("no interface found\n");
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
    admin->admin_server =
        lo_server_new_multicast("224.0.1.3", "7570", handler_error);
    if (!admin->admin_server) {
        free(admin->identifier);
        lo_address_free(admin->admin_addr);
        free(admin);
        return NULL;
    }

    /* Initialize data structures */
    admin->identifier = strdup(identifier);
    admin->name = 0;
    admin->ordinal.value = 1;
    admin->ordinal.locked = 0;
    admin->ordinal.collision_count = -1;
    admin->ordinal.count_time = get_current_time();
    admin->ordinal.on_collision = mapper_admin_name_registered;
    admin->port.value = initial_port;
    admin->port.locked = 0;
    admin->port.collision_count = -1;
    admin->port.count_time = get_current_time();
    admin->port.on_collision = mapper_admin_port_registered;
    admin->registered = 0;
    admin->device = device;

    /* Add methods for admin bus.  Only add methods needed for
     * allocation here. Further methods are added when the device is
     * registered. */
    lo_server_add_method(admin->admin_server, "/port/probe", NULL,
                         handler_device_alloc_port, admin);
    lo_server_add_method(admin->admin_server, "/name/probe", NULL,
                         handler_device_alloc_name, admin);

    /* Probe potential port and name to admin bus. */
    mapper_admin_port_probe(admin);
    mapper_admin_name_probe(admin);

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

    if (admin->name)
        free(admin->name);

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

    int count = 0;

    while (count < 10 && lo_server_recv_noblock(admin->admin_server, 0)) {
        count++;
    }


    /* If the port is not yet locked, process collision timing.  Once
     * the port is locked it won't change. */
    if (!admin->port.locked)
        if (check_collisions(admin, &admin->port))
            /* If the port has changed, re-probe the new potential port. */
            mapper_admin_port_probe(admin);

    /* If the ordinal is not yet locked, process collision timing.
     * Once the ordinal is locked it won't change. */
    if (!admin->ordinal.locked)
        if (check_collisions(admin, &admin->ordinal))
            /* If the ordinal has changed, re-probe the new name. */
            mapper_admin_name_probe(admin);

    /* If we are ready to register the device, add the needed message
     * handlers. */
    if (!admin->registered && admin->port.locked && admin->ordinal.locked) {
        char namespaceget[256];
        lo_server_add_method(admin->admin_server, "/who", "", handler_who,
                             admin);
        lo_server_add_method(admin->admin_server, "/registered", NULL,
                             handler_registered, admin);

        snprintf(namespaceget, 256, "%s/namespace/get",
                 mapper_admin_name(admin));
        lo_server_add_method(admin->admin_server, namespaceget, "",
                             handler_id_n_namespace_get, admin);

        snprintf(namespaceget, 256, "%s/namespace/input/get",
                 mapper_admin_name(admin));
        lo_server_add_method(admin->admin_server, namespaceget, "",
                             handler_id_n_namespace_input_get, admin);

        snprintf(namespaceget, 256, "%s/namespace/output/get",
                 mapper_admin_name(admin));
        lo_server_add_method(admin->admin_server, namespaceget, "",
                             handler_id_n_namespace_output_get, admin);

        snprintf(namespaceget, 256, "%s/info/get",
                 mapper_admin_name(admin));
        lo_server_add_method(admin->admin_server, namespaceget, "",
                             handler_who, admin);

        char linksget[256];
        snprintf(linksget, 256, "%s/links/get", mapper_admin_name(admin));
        lo_server_add_method(admin->admin_server, linksget, "",
                             handler_device_links_get, admin);
        lo_server_add_method(admin->admin_server, "/*/links/get", "",
                             handler_device_links_get, admin);

        lo_server_add_method(admin->admin_server, "/link", "ss",
                             handler_device_link, admin);
        lo_server_add_method(admin->admin_server, "/link_to", "sssssiss",
                             handler_device_link_to, admin);
        lo_server_add_method(admin->admin_server, "/unlink", "ss",
                             handler_device_unlink, admin);

        char connectionsget[256];
        snprintf(connectionsget, 256, "%s/connections/get",
                 mapper_admin_name(admin));
        lo_server_add_method(admin->admin_server, connectionsget, "",
                             handler_device_connections_get, admin);

        lo_server_add_method(admin->admin_server, "/connect", NULL,
                             handler_param_connect, admin);
        lo_server_add_method(admin->admin_server, "/connect_to", NULL,
                             handler_param_connect_to, admin);
        lo_server_add_method(admin->admin_server, "/connection/modify",
                             NULL, handler_param_connection_modify, admin);
        lo_server_add_method(admin->admin_server, "/disconnect", "ss",
                             handler_param_disconnect, admin);

        lo_send(admin->admin_addr, "/who", "");
    }
}

/*! Probe the admin bus to see if a device's proposed port is already
 *  taken.
 */
void mapper_admin_port_probe(mapper_admin admin)
{
    trace("probing port\n");

    lo_send(admin->admin_addr, "/port/probe", "i", admin->port.value);
}

/*! Probe the admin bus to see if a device's proposed name.ordinal is
 *  already taken.
 */
void mapper_admin_name_probe(mapper_admin admin)
{
    /* Note: mapper_admin_name() would refuse here since the
     * ordinal is not yet locked, so we have to build it manually at
     * this point. */
    char name[256];
    trace("probing name\n");
    snprintf(name, 256, "/%s.%d", admin->identifier, admin->ordinal.value);

    lo_send(admin->admin_addr, "/name/probe", "s", name);
}

/*! Announce on the admin bus a device's registered port. */
void mapper_admin_port_registered(mapper_admin admin)
{
    if (admin->port.locked)
        lo_send(admin->admin_addr, "/port/registered", "i",
                admin->port.value);
}

/*! Announce on the admin bus a device's registered name.ordinal. */
void mapper_admin_name_registered(mapper_admin admin)
{
    if (admin->ordinal.locked)
        lo_send(admin->admin_addr, "/name/registered", "s",
                mapper_admin_name(admin));
}

const char *_real_mapper_admin_name(mapper_admin admin,
                                    const char *file, unsigned int line)
{
    if (!admin->ordinal.locked) {
        /* Since this function is intended to be used internally in a
         * fairly liberal manner, we want to trace any situations
         * where returning 0 might cause a problem.  The external call
         * to this function, mdev_full_name(), has been special-cased
         * to allow this. */
        trace("mapper_admin_name() returning 0 at %s:%d.\n", file, line);
        return 0;
    }

    if (admin->name)
        return admin->name;

    unsigned int len = strlen(admin->identifier) + 6;
    admin->name = (char *) malloc(len);
    admin->name[0] = 0;
    snprintf(admin->name, len, "/%s.%d", admin->identifier,
             admin->ordinal.value);

    return admin->name;
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
         * last probe, try a new random port. */
    if (timediff >= 0.5 && resource->collision_count > 0) {
        /* Otherwise, add a random number based on the number of
         * collisions. */
        resource->value += (int) (((double) rand()) / RAND_MAX
                                  * (resource->collision_count + 1));

        /* Prepare for causing new port collisions. */

        resource->collision_count = -1;
        resource->count_time = get_current_time();

        /* Indicate that we need to re-probe the new value. */
        return 1;
    }

    return 0;
}

static void on_collision(mapper_admin_allocated_t *resource,
                         mapper_admin admin, int type)
{
    if (resource->locked && resource->on_collision)
        resource->on_collision(admin);

    /* Count port collisions. */
    resource->collision_count++;
    trace("%d collision_count = %d\n", resource->value,
          resource->collision_count);
    resource->count_time = get_current_time();
}

/**********************************/
/* Internal OSC message handlers. */
/**********************************/

/*! Respond to /who by announcing the current port. */
static int handler_who(const char *path, const char *types, lo_arg **argv,
                       int argc, lo_message msg, void *user_data)
{
    mapper_admin admin = (mapper_admin) user_data;

    lo_send(admin->admin_addr, "/registered", "ssssisssisisiiiiiiii",
            mapper_admin_name(admin),
            "@IP", inet_ntoa(admin->interface_ip),
            "@port", admin->port.value,
            "@canAlias", "no",   /*TODO : OSC aliases */
            "@numInputs", mdev_num_inputs(admin->device),
            "@numOutputs", mdev_num_outputs(admin->device),
            "@hash", 0, 0, 0, 0, 0, 0, 0, 0);

    /* If the device who received this message is not yet registered,
     * it is added to the global LOCAL_DEVICES list.  */
    if (!(*((mapper_admin) user_data)).registered) {
        (*((mapper_admin) user_data)).registered = 1;
        printf("NEW LOCAL DEVICE : %s\n", mapper_admin_name(admin));
    }
    return 0;
}


/*! Register information about port and host for the device. */
static int handler_registered(const char *path, const char *types,
                              lo_arg **argv, int argc, lo_message msg,
                              void *user_data)
{
    if (0) {                    //change to test whether device is GUI
        if (argc < 1)
            return 0;

        if (types[0] != 's' && types[0] != 'S')
            return 0;

        // TODO: pick up these parameters more intelligently
        // TODO: modify the record if it already exists
        mapper_db_add(&argv[0]->s,  // name
                      &argv[2]->s,  // host
                      argv[4]->i,   // port
                      &argv[6]->s); // canAlias
    }

    return 0;
}


/*! Respond to /namespace/input/get by enumerating all supported
 *  inputs. */
static int handler_id_n_namespace_input_get(const char *path,
                                            const char *types,
                                            lo_arg **argv, int argc,
                                            lo_message msg,
                                            void *user_data)
{
    mapper_admin admin = (mapper_admin) user_data;
    mapper_device md = admin->device;
    char response[1024], method[1024], type[2] = { 0, 0 };
    int i;

    snprintf(response, 1024, "/%s/namespace/input",
             mapper_admin_name(admin));

    for (i = 0; i < md->n_inputs; i++) {
        mapper_signal sig = md->inputs[i];
        lo_message m = lo_message_new();

        strcpy(method, sig->name);
        lo_message_add_string(m, method);

        lo_message_add_string(m, "@type");
        type[0] = sig->type;
        lo_message_add_string(m, type);

        if (sig->minimum) {
            lo_message_add_string(m, "@min");
            mval_add_to_message(m, sig, sig->minimum);
        }

        if (sig->maximum) {
            lo_message_add_string(m, "@max");
            mval_add_to_message(m, sig, sig->maximum);
        }

        lo_send_message(admin->admin_addr, response, m);
        lo_message_free(m);
    }

    return 0;
}

/*! Respond to /namespace/output/get by enumerating all supported
 *  outputs. */
static int handler_id_n_namespace_output_get(const char *path,
                                             const char *types,
                                             lo_arg **argv, int argc,
                                             lo_message msg,
                                             void *user_data)
{
    mapper_admin admin = (mapper_admin) user_data;
    mapper_device md = admin->device;
    char response[1024], method[1024], type[2] = { 0, 0 };
    int i;

    snprintf(response, 1024, "/%s/namespace/output",
             mapper_admin_name(admin));

    for (i = 0; i < md->n_outputs; i++) {
        mapper_signal sig = md->outputs[i];
        lo_message m = lo_message_new();

        strcpy(method, sig->name);
        lo_message_add_string(m, method);

        lo_message_add_string(m, "@type");
        type[0] = sig->type;
        lo_message_add_string(m, type);

        if (sig->minimum) {
            lo_message_add_string(m, "@min");
            mval_add_to_message(m, sig, sig->minimum);
        }

        if (sig->maximum) {
            lo_message_add_string(m, "@max");
            mval_add_to_message(m, sig, sig->maximum);
        }

        lo_send_message(admin->admin_addr, response, m);
        lo_message_free(m);
    }

    return 0;
}

/*! Respond to /namespace/get by enumerating all supported inputs and
 *  outputs. */
static int handler_id_n_namespace_get(const char *path, const char *types,
                                      lo_arg **argv, int argc,
                                      lo_message msg, void *user_data)
{

    handler_id_n_namespace_input_get(path, types, argv, argc, msg,
                                     user_data);
    handler_id_n_namespace_output_get(path, types, argv, argc, msg,
                                      user_data);

    return 0;

}

static int handler_device_alloc_port(const char *path, const char *types,
                                     lo_arg **argv, int argc,
                                     lo_message msg, void *user_data)
{
    mapper_admin admin = (mapper_admin) user_data;


    unsigned int probed_port = 0;

    if (argc < 1)
        return 0;

    if (types[0] == 'i')
        probed_port = argv[0]->i;
    else if (types[0] == 'f')
        probed_port = (unsigned int) argv[0]->f;
    else
        return 0;

    trace("got /port/probe %d \n", probed_port);

    /* Process port collisions. */
    if (probed_port == admin->port.value)
        on_collision(&admin->port, admin, 0);

    return 0;
}

static int handler_device_alloc_name(const char *path, const char *types,
                                     lo_arg **argv, int argc,
                                     lo_message msg, void *user_data)
{
    mapper_admin admin = (mapper_admin) user_data;


    char *probed_name = 0, *s;
    unsigned int probed_ordinal = 0;

    if (argc < 1)
        return 0;

    if (types[0] != 's' && types[0] != 'S')
        return 0;

    probed_name = &argv[0]->s;

    /* Parse the ordinal from the complete name which is in the
     * format: /<name>.<n> */
    s = probed_name;
    if (*s++ != '/')
        return 0;
    while (*s != '.' && *s++) {
    }
    probed_ordinal = atoi(++s);

    trace("got /name/probe %s\n", probed_name);

    /* Process ordinal collisions. */
    //The collision should be calculated separately per-device-name
    strtok(probed_name, ".");
    probed_name++;
    if ((strcmp(probed_name, admin->identifier) == 0)
        && (probed_ordinal == admin->ordinal.value))
        on_collision(&admin->ordinal, admin, 1);

    return 0;
}

/*! Link two devices. */
static int handler_device_link(const char *path, const char *types,
                               lo_arg **argv, int argc, lo_message msg,
                               void *user_data)
{

    char device_name[1024], sender_name[1024], target_name[1024];

    if (argc < 2)
        return 0;

    if (types[0] != 's' && types[0] != 'S' && types[1] != 's'
        && types[1] != 'S')
        return 0;

    snprintf(device_name, 256, "/%s.%d",
             (*((mapper_admin) user_data)).identifier,
             (*((mapper_admin) user_data)).ordinal.value);
    strcpy(sender_name, &argv[0]->s);
    strcpy(target_name, &argv[1]->s);

    trace("got /link %s %s\n", sender_name, target_name);
    printf("got /link %s %s\n", sender_name, target_name);

    /* If the device who received the message is the target in the
     * /link message... */
    if (strcmp(device_name, target_name) == 0) {
        lo_send((*((mapper_admin) user_data)).admin_addr, "/link_to",
                "sssssiss", sender_name, target_name, "@IP",
                inet_ntoa((*((mapper_admin) user_data)).interface_ip),
                "@port", (*((mapper_admin) user_data)).port.value,
                "@canAlias", "no");
    }
    return 0;
}

/*! Link two devices... continued. */
static int handler_device_link_to(const char *path, const char *types,
                                  lo_arg **argv, int argc, lo_message msg,
                                  void *user_data)
{
    char device_name[1024], sender_name[1024], target_name[1024],
        host_address[1024], can_alias[1024] = "no";
    int recvport, f = 1, j = 2;
    mapper_admin admin = (mapper_admin) user_data;
    mapper_device md = admin->device;
    mapper_router router = md->routers;

    if (argc < 2)
        return 0;

    if (types[0] != 's' && types[0] != 'S' && types[1] != 's'
        && types[1] != 'S')
        return 0;

    snprintf(device_name, 256, "/%s.%d",
             (*((mapper_admin) user_data)).identifier,
             (*((mapper_admin) user_data)).ordinal.value);
    strcpy(sender_name, &argv[0]->s);
    strcpy(target_name, &argv[1]->s);

    printf("got /link_to %s %s\n", sender_name, target_name);

    /* Parse the options list */
    while ((argc - j) >= 2) {
        if (types[j] != 's' && types[j] != 'S') {
            j++;
        }

        else if (strcmp(&argv[j]->s, "@IP") == 0) {
            strcpy(host_address, &argv[j + 1]->s);
            j += 2;
        }

        else if (strcmp(&argv[j]->s, "@port") == 0) {
            recvport = argv[j + 1]->i;
            j += 2;
        }

        else if (strcmp(&argv[j]->s, "@canAlias") == 0) {
            strcpy(can_alias, &argv[j + 1]->s);
            j += 2;
        } else {
            j++;
        }
    }

    printf("got /link_to %s %s\n", sender_name, target_name);

    /* If the device who received the message is the sender in the
     * /link message... */
    if (strcmp(device_name, sender_name) == 0) {
        /*Search if the device is already linked */
        while (router != NULL && f == 0) {
            f *= strcmp(router->target_name, target_name);
            router = router->next;
        }

        if (f != 0) {           /*! Should we also send /linked
                                 *  message in response to duplicate
                                 *  link requests? */
            printf("NEW LINKED DEVICE %s\nHost : %s, Port : %d, "
                   "canAlias : %s\n\n",
                   target_name, host_address, recvport, can_alias);

            /* Creation of a new router added to the sender */
            router =
                mapper_router_new((*((mapper_admin) user_data)).device,
                                  host_address, recvport, target_name);
            mdev_add_router((*((mapper_admin) user_data)).device, router);
            (*((mapper_admin) user_data)).device->num_routers++;
            printf("Router to %s : %d added.\n", host_address, recvport);
            lo_send((*((mapper_admin) user_data)).admin_addr, "/linked",
                    "ss", device_name,
                    ((*((mapper_admin) user_data)).device->routers->
                     target_name));
        }
    }
    return 0;
}

/*! Report existing links to the network */
static int handler_device_links_get(const char *path, const char *types,
                                    lo_arg **argv, int argc,
                                    lo_message msg, void *user_data)
{
    char device_name[1024];
    mapper_admin admin = (mapper_admin) user_data;
    mapper_device md = admin->device;
    mapper_router router = md->routers;

    snprintf(device_name, 256, "/%s.%d",
             (*((mapper_admin) user_data)).identifier,
             (*((mapper_admin) user_data)).ordinal.value);

    trace("got /%s/links/get\n", device_name);

    /*Search through linked devices */
    while (router != NULL) {
        lo_send((*((mapper_admin) user_data)).admin_addr, "/linked", "ss",
                device_name, router->target_name);
        router = router->next;
    }
    return 0;
}

/*! Unlink two devices. */
static int handler_device_unlink(const char *path, const char *types,
                                 lo_arg **argv, int argc, lo_message msg,
                                 void *user_data)
{

    int f = 0;
    char device_name[1024], sender_name[1024], target_name[1024];
    mapper_admin admin = (mapper_admin) user_data;
    mapper_device md = admin->device;
    mapper_router router = md->routers;

    if (argc < 2)
        return 0;

    if (types[0] != 's' && types[0] != 'S' && types[1] != 's'
        && types[1] != 'S')
        return 0;

    snprintf(device_name, 256, "/%s.%d",
             (*((mapper_admin) user_data)).identifier,
             (*((mapper_admin) user_data)).ordinal.value);
    strcpy(sender_name, &argv[0]->s);
    strcpy(target_name, &argv[1]->s);

    trace("got /unlink %s %s\n", sender_name, target_name);

    /*If the device who received the message is the sender in the
     * /unlink message ... */
    if (strcmp(device_name, sender_name) == 0) {
        /* Search the router to remove */
        while (router != NULL && f == 0) {
            if (strcmp(router->target_name, target_name) == 0) {
                mdev_remove_router(md, router);
                (*((mapper_admin) user_data)).device->num_routers--;
                /*mapper_router_free(router); */
                f = 1;
            } else
                router = router->next;
        }

        if (f == 1)
            lo_send((*((mapper_admin) user_data)).admin_addr, "/unlinked",
                    "ss", device_name, target_name);
    }

    else if (strcmp(device_name, target_name) == 0) {
        /*lo_send((*((mapper_admin)
         * user_data)).admin_addr,"/unlinked", "ss", sender_name,
         * target_name ); */
    }

    return 0;
}


/*! Connect two signals. */
static int handler_param_connect_to(const char *path, const char *types,
                                    lo_arg **argv, int argc,
                                    lo_message msg, void *user_data)
{

    mapper_admin admin = (mapper_admin) user_data;
    mapper_device md = admin->device;
    mapper_router router = md->routers;

    int md_num_outputs = (*((mapper_admin) user_data)).device->n_outputs;
    mapper_signal *md_outputs =
        (*((mapper_admin) user_data)).device->outputs;

    int i = 0, j = 2, f1 = 0, f2 = 0, recvport = -1, range_update = 0;

    char device_name[1024], src_param_name[1024], src_device_name[1024],
        target_param_name[1024], target_device_name[1024], scaling[1024] =
        "dummy", host_address[1024];
    char dest_type;
    float dest_range_min = 0, dest_range_max = 1;

    if (argc < 2)
        return 0;

    if ((types[0] != 's' && types[0] != 'S')
        || (types[1] != 's' && types[1] != 'S'))
        return 0;

    snprintf(device_name, 256, "/%s.%d",
             (*((mapper_admin) user_data)).identifier,
             (*((mapper_admin) user_data)).ordinal.value);

    strcpy(src_device_name, &argv[0]->s);
    strtok(src_device_name, "/");

    /* Check OSC pattern match */
    if (strcmp(device_name, src_device_name) == 0) {

        strcpy(src_param_name, &argv[0]->s + strlen(src_device_name));
        strcpy(target_device_name, &argv[1]->s);
        strtok(target_device_name, "/");
        strcpy(target_param_name,
               &argv[1]->s + strlen(target_device_name));

        trace("got /connect_to %s%s %s%s+ %d arguments\n", src_device_name,
              src_param_name, target_device_name, target_param_name, argc);
        printf("got /connect_to %s%s %s%s + %d arguments\n",
               src_device_name, src_param_name, target_device_name,
               target_param_name, argc);

        /* Searches the source signal among the outputs of the device */
        while (i < md_num_outputs && f1 == 0) {

            /* If the signal exists ... */
            if (strcmp(md_outputs[i]->name, src_param_name) == 0) {
                printf("signal exists: %s\n", md_outputs[i]->name);

                /* Search the router linking to the receiver */
                while (router != NULL && f2 == 0) {
                    if (strcmp(router->target_name, target_device_name) ==
                        0)
                        f2 = 1;
                    else
                        router = router->next;
                }
                f1 = 1;
            } else
                i++;
        }

        /* If the router doesn't exist yet */
        if (f2 == 0) {
            printf("devices are not linked!");
            if (host_address != NULL && recvport != -1) {
                //TO DO: create routed using supplied host and port info
                //TO DO: send /linked message
            } else {
                //TO DO: send /link message to start process - should
                //       also cache /connect_to message for completion after
                //       link???
            }
        }

        /* If this router exists... */
        else {
            if (argc == 2) {
                /* If no properties were provided, default to direct
                 * mapping */
                mapper_router_add_direct_mapping(router,
                                                 admin->device->outputs[i],
                                                 target_param_name);
            } else {
                /* If properties were provided, construct a custom
                 * mapping state: */

                /* Add a flavourless mapping */
                mapper_mapping m = mapper_router_add_blank_mapping(
                    router, admin->device->outputs[i], target_param_name);

                /* Parse the list of properties */
                while (j < argc) {
                    if (types[j] != 's' && types[j] != 'S') {
                        j++;
                    } else if (strcmp(&argv[j]->s, "@type") == 0) {
                        dest_type = argv[j + 1]->c;
                        j += 2;
                    } else if (strcmp(&argv[j]->s, "@min") == 0) {
                        dest_range_min = argv[j + 1]->f;
                        range_update++;
                        j += 2;
                    } else if (strcmp(&argv[j]->s, "@max") == 0) {
                        dest_range_max = argv[j + 1]->f;
                        range_update++;
                        j += 2;
                    } else if (strcmp(&argv[j]->s, "@scaling") == 0) {
                        if (strcmp(&argv[j + 1]->s, "bypass") == 0)
                            m->type = BYPASS;
                        if (strcmp(&argv[j + 1]->s, "linear") == 0)
                            m->type = LINEAR;
                        if (strcmp(&argv[j + 1]->s, "expression") == 0)
                            m->type = EXPRESSION;
                        if (strcmp(&argv[j + 1]->s, "calibrate") == 0)
                            m->type = CALIBRATE;
                        j += 2;
                    } else if (strcmp(&argv[j]->s, "@range") == 0) {
                        if (types[j + 1] == 'i')
                            m->range.src_min = (float) argv[j + 1]->i;
                        else if (types[j + 1] == 'f')
                            m->range.src_min = argv[j + 1]->f;
                        if (types[j + 2] == 'i')
                            m->range.src_max = (float) argv[j + 2]->i;
                        else if (types[j + 2] == 'f')
                            m->range.src_max = argv[j + 2]->f;
                        if (types[j + 3] == 'i')
                            m->range.dest_min = (float) argv[j + 3]->i;
                        else if (types[j + 3] == 'f')
                            m->range.dest_min = argv[j + 3]->f;
                        if (types[j + 4] == 'i')
                            m->range.dest_max = (float) argv[j + 4]->i;
                        else if (types[j + 4] == 'f')
                            m->range.dest_max = argv[j + 4]->f;
                        range_update += 4;
                        j += 5;
                    } else if (strcmp(&argv[j]->s, "@expression") == 0) {
                        char received_expr[1024];
                        strcpy(received_expr, &argv[j + 1]->s);
                        Tree *T = NewTree();
                        int success_tree = get_expr_Tree(T, received_expr);

                        if (success_tree) {
                            free(m->expression);
                            m->expression = strdup(&argv[j + 1]->s);
                            DeleteTree(m->expr_tree);
                            m->expr_tree = T;
                        }
                        j += 2;
                    } else if (strcmp(&argv[j]->s, "@clipMin") == 0) {
                        //m->clipMin = strdup(&argv[j+1]->s);
                        j += 2;
                    } else if (strcmp(&argv[j]->s, "@clipMax") == 0) {
                        //m->clipMax = strdup(&argv[j+1]->s);
                        j += 2;
                    } else {
                        j++;
                    }
                }

                if (strcmp(scaling, "dummy") == 0) {
                    if ((range_update == 2)
                        && (md_outputs[i]->type == 'i'
                            || md_outputs[i]->type == 'f')
                        && (dest_type == 'i' || dest_type == 'f')) {
                        /* If destination range was provided and types
                         * are 'i' or 'f', default to linear
                         * mapping. */
                        mapper_router_add_linear_range_mapping(
                            router, admin->device->outputs[i],
                            target_param_name, md_outputs[i]->minimum->f,
                            md_outputs[i]->maximum->f,
                            dest_range_min, dest_range_max);
                    } else {
                        // Otherwise default to direct mapping
                        mapper_router_add_direct_mapping(
                            router, admin->device->outputs[i],
                            target_param_name);
                    }
                }
            }
            /* Add clipping! */
        }
    }
    return 0;
}

/*! Modify the connection properties : scaling, range, expression,
 *  clipMin, clipMax. */
static int handler_param_connection_modify(const char *path,
                                           const char *types,
                                           lo_arg **argv, int argc,
                                           lo_message msg, void *user_data)
{

    mapper_admin admin = (mapper_admin) user_data;
    mapper_device md = admin->device;
    mapper_router router = md->routers;

    int md_num_outputs = (*((mapper_admin) user_data)).device->n_outputs;
    mapper_signal *md_outputs =
        (*((mapper_admin) user_data)).device->outputs;

    int i = 0, f1 = 0, f2 = 0;

    char device_name[1024], src_param_name[1024], src_device_name[1024],
        target_param_name[1024], target_device_name[1024],
        modif_prop[1024];
    char mapping_type[1024];

    if (argc < 4)
        return 0;

    if ((types[0] != 's' && types[0] != 'S')
        || (types[1] != 's' && types[1] != 'S') || (types[2] != 's'
                                                    && types[2] != 'S'))
        return 0;

    snprintf(device_name, 256, "/%s.%d",
             (*((mapper_admin) user_data)).identifier,
             (*((mapper_admin) user_data)).ordinal.value);

    strcpy(src_device_name, &argv[0]->s);
    strtok(src_device_name, "/");

    /* Check OSC pattern match */
    if (strcmp(device_name, src_device_name) == 0) {

        strcpy(src_param_name, &argv[0]->s + strlen(src_device_name));
        strcpy(target_device_name, &argv[1]->s);
        strtok(target_device_name, "/");
        strcpy(target_param_name,
               &argv[1]->s + strlen(target_device_name));
        strcpy(modif_prop, &argv[2]->s);

        /* Search the source signal among the outputs of the device */
        while (i < md_num_outputs && f1 == 0) {

            /* If this signal exists... */
            if (strcmp(md_outputs[i]->name, src_param_name) == 0) {

                /* Search the router linking to the receiver */
                while (router != NULL && f2 == 0) {
                    if (strcmp(router->target_name, target_device_name) ==
                        0)
                        f2 = 1;
                    else
                        router = router->next;
                }

                /* If this router exists ... */
                if (f2 == 1) {

                    /* Search the mapping corresponding to this connection */
                    mapper_signal_mapping sm = router->mappings;
                    while (sm && sm->signal != md_outputs[i])
                        sm = sm->next;
                    if (!sm)
                        return 0;

                    mapper_mapping m = sm->mapping;
                    while (m && strcmp(m->name, target_param_name) != 0) {
                        m = m->next;
                    }
                    if (!m)
                        return 0;

                    /* Modify scaling */
                    if (strcmp(modif_prop, "@scaling") == 0) {
                        char scaling[1024];
                        strcpy(scaling, &argv[3]->s);

                        /* Expression type */
                        if (strcmp(scaling, "expression") == 0) {
                            m->type = EXPRESSION;
                            /* lo_send((*((mapper_admin)
                             * user_data)).admin_addr, "/connected",
                             * "ssss", "@scaling", "expression",
                             * "@expression", m->expression ); */
                        }

                        /* Linear type */
                        else if ((strcmp(scaling, "linear") == 0)
                                 || (strcmp(scaling, "calibrate") == 0)) {
                            if (strcmp(scaling, "calibrate") == 0) {
                                m->type = CALIBRATE;
                                m->range.rewrite = 1;
                            } else
                                m->type = LINEAR;

                            /* The expression has to be modified to fit
                             * the range */
                            free(m->expression);
                            m->expression = malloc(256 * sizeof(char));
                            snprintf(m->expression, 256, "y=(x-%g)*%g+%g",
                                     m->range.src_min,
                                     (m->range.dest_max -
                                      m->range.dest_min) /
                                     (m->range.src_max - m->range.src_min),
                                     m->range.dest_min);
                            DeleteTree(m->expr_tree);
                            Tree *T = NewTree();
                            int success_tree =
                                get_expr_Tree(T, m->expression);

                            if (!success_tree)
                                return 0;

                            m->expr_tree = T;
                            /*lo_send((*((mapper_admin)
                             * user_data)).admin_addr,"/connected",
                             * "ssss", "@scaling", "linear",
                             * "@expression", m->expression ); */
                        }

                        /* Bypass type */
                        else if (strcmp(scaling, "bypass") == 0) {
                            m->type = BYPASS;
                            m->expression = strdup("y=x");
                            /*lo_send((*((mapper_admin)
                             * user_data)).admin_addr,"/connected",
                             * "ssss", "@scaling", "linear",
                             * "@expression", m->expression ); */
                        }
                    }

                    /* Modify expression */
                    else if (strcmp
                             (modif_prop,
                              "@scaling expression @expression") == 0) {
                        char received_expr[1024];
                        strcpy(received_expr, &argv[3]->s);
                        Tree *T = NewTree();
                        int success_tree = get_expr_Tree(T, received_expr);

                        if (success_tree) {
                            free(m->expression);
                            m->expression = strdup(&argv[3]->s);
                            DeleteTree(m->expr_tree);
                            m->expr_tree = T;
                        }

                        /*get_expr_Tree(T, m->expression); */
                        /*m->expr_tree=T; */
                        /*lo_send((*((mapper_admin)
                         * user_data)).admin_addr,"/connected", "ss",
                         * "@scaling expression @expression",
                         * m->expression ); */
                    }

                    /* Modify range */
                    else if (strcmp(modif_prop, "@range") == 0) {
                        char invert[1024];
                        strcpy(invert, &argv[3]->s);
                        if (strcmp(invert, "invert") == 0) {
                            printf("invert message\n");
                            strcpy(invert, &argv[4]->s);
                            if (strcmp(invert, "input") == 0) {
                                float temp = m->range.src_min;
                                m->range.src_min = m->range.src_max;
                                m->range.src_max = temp;
                            } else if (strcmp(invert, "output") == 0) {
                                float temp = m->range.dest_min;
                                m->range.dest_min = m->range.dest_max;
                                m->range.dest_max = temp;
                            }
                        } else {
                            switch (types[3]) {
                            case 'f':
                                m->range.src_min = argv[3]->f;
                                break;
                            case 'i':
                                m->range.src_min = argv[3]->i;
                                break;
                            }

                            switch (types[4]) {
                            case 'f':
                                m->range.src_max = argv[4]->f;
                                break;
                            case 'i':
                                m->range.src_max = argv[4]->i;
                                break;
                            }

                            switch (types[5]) {
                            case 'f':
                                m->range.dest_min = argv[5]->f;
                                break;
                            case 'i':
                                m->range.dest_min = argv[5]->i;
                                break;
                            }

                            switch (types[6]) {
                            case 'f':
                                m->range.dest_max = argv[6]->f;
                                break;
                            case 'i':
                                m->range.dest_max = argv[6]->i;
                                break;
                            }
                        }

                        if (m->type == LINEAR || m->type == CALIBRATE) {
                            /* The expression has to be modified to
                             * fit the new range */
                            free(m->expression);
                            m->expression = malloc(256 * sizeof(char));
                            snprintf(m->expression, 256, "y=(x-%g)*%g+%g",
                                     m->range.src_min,
                                     (m->range.dest_max -
                                      m->range.dest_min) /
                                     (m->range.src_max - m->range.src_min),
                                     m->range.dest_min);
                            DeleteTree(m->expr_tree);
                            Tree *T = NewTree();
                            int success_tree =
                                get_expr_Tree(T, m->expression);
                            if (!success_tree)
                                return 0;
                            /*get_expr_Tree(T, m->expression); */
                            m->expr_tree = T;
                        }
                        /*lo_send((*((mapper_admin)
                         * user_data)).admin_addr,"/connected",
                         * "sffff", @range, m->range.src_min,
                         * m->range.src_max, m->range.dest_min,
                         * m->range.dest_max); */
                    }

                    else if (strcmp(modif_prop, "@clipMin") == 0) {
                        char clipMin[1024];
                        strcpy(clipMin, &argv[3]->s);
                         /*TODO*/
                        /*lo_send((*((mapper_admin)
                         * user_data)).admin_addr,
                         * "/connected",.... ); */
                    }

                    else if (strcmp(modif_prop, "@clipMax") == 0) {
                        char clipMax[1024];
                        strcpy(clipMax, &argv[3]->s);
                         /*TODO*/
                        /*lo_send((*((mapper_admin)
                             * user_data)).admin_addr,
                             * "/connected",.... ); */
                    }

/***********************TEMPORARY, then only send 
                        the modified parameters********************/

                    switch (m->type) {
                    case EXPRESSION:
                        strcpy(mapping_type, "expression");
                        break;

                    case LINEAR:
                        strcpy(mapping_type, "linear");
                        break;

                    case BYPASS:
                        strcpy(mapping_type, "bypass");
                        break;

                    case CALIBRATE:
                        strcpy(mapping_type, "calibrate");
                        break;

                    default:
                        break;

                    }

                    lo_send((*((mapper_admin) user_data)).admin_addr,
                            "/connected", "sssssffffssssss",
                            strcat(src_device_name, src_param_name),
                            strcat(target_device_name, target_param_name),
                            "@scaling", mapping_type, "@range",
                            m->range.src_min, m->range.src_max,
                            m->range.dest_min, m->range.dest_max,
                            "@expression", m->expression,
                            "@clipMin", "none",
                            "@clipMax", "none");
                }
                f1 = 1;
            } else
                i++;
        }
    }
    return 0;
}

/*! Disconnect two signals. */
static int handler_param_disconnect(const char *path, const char *types,
                                    lo_arg **argv, int argc,
                                    lo_message msg, void *user_data)
{

    mapper_admin admin = (mapper_admin) user_data;
    mapper_device md = admin->device;
    mapper_router router = md->routers;

    int md_num_outputs = (*((mapper_admin) user_data)).device->n_outputs;
    mapper_signal *md_outputs =
        (*((mapper_admin) user_data)).device->outputs;
    int i = 0, f1 = 0, f2 = 0;

    char device_name[1024], src_param_name[1024], src_device_name[1024],
        target_param_name[1024], target_device_name[1024];

    if (argc < 2)
        return 0;

    if (types[0] != 's' && types[0] != 'S' && types[1] != 's'
        && types[1] != 'S')
        return 0;

    snprintf(device_name, 256, "/%s.%d",
             (*((mapper_admin) user_data)).identifier,
             (*((mapper_admin) user_data)).ordinal.value);

    strcpy(src_device_name, &argv[0]->s);
    strtok(src_device_name, "/");

    /* Check OSC pattern match */
    if (strcmp(device_name, src_device_name) == 0) {

        strcpy(src_param_name, &argv[0]->s + strlen(src_device_name));
        strcpy(target_device_name, &argv[1]->s);
        strtok(target_device_name, "/");
        strcpy(target_param_name,
               &argv[1]->s + strlen(target_device_name));

        trace("got /disconnect %s%s %s%s\n", src_device_name,
              src_param_name, target_device_name, target_param_name);

        /* Searches the source signal among the outputs of the device */
        while (i < md_num_outputs && f1 == 0) {
            /* If this signal exists ... */
            if (strcmp(md_outputs[i]->name, src_param_name) == 0) {

                /* Searches the router linking to the receiver */
                while (router != NULL && f2 == 0) {
                    if (strcmp(router->target_name, target_device_name) ==
                        0)
                        f2 = 1;
                    else
                        router = router->next;
                }

                /* If this router exists ... */
                if (f2 == 1) {
                    /* Search the mapping corresponding to this connection */
                    mapper_signal_mapping sm = router->mappings;
                    while (sm && sm->signal != md_outputs[i])
                        sm = sm->next;
                    if (!sm)
                        return 0;

                    mapper_mapping m = sm->mapping;
                    while (m && strcmp(m->name, target_param_name) != 0) {
                        m = m->next;
                    }
                    if (!m)
                        return 0;

                    /*The mapping is removed */
                    mapper_router_remove_mapping( /*router, */ sm, m);
                } else
                    return 0;
                f1 = 1;
            } else
                i++;
        }
    }
    return 0;
}


/*! When the /connect message is received by the destination device,
 *  send a connect_to message to the source device. */
static int handler_param_connect(const char *path, const char *types,
                                 lo_arg **argv, int argc, lo_message msg,
                                 void *user_data)
{
    mapper_admin admin = (mapper_admin) user_data;

    int md_num_inputs = admin->device->n_inputs;
    mapper_signal *md_inputs = admin->device->inputs;
    int i = 0, j = 2, f = 0;

    char device_name[1024], src_param_name[1024], src_device_name[1024],
        target_param_name[1024], target_device_name[1024], temp_type[2];

    if (argc < 2)
        return 0;

    if (types[0] != 's' && types[0] != 'S' && types[1] != 's'
        && types[1] != 'S')
        return 0;

    snprintf(device_name, 256, "/%s.%d",
             (*((mapper_admin) user_data)).identifier,
             (*((mapper_admin) user_data)).ordinal.value);

    strcpy(target_device_name, &argv[1]->s);
    strtok(target_device_name, "/");

    // check OSC pattern match
    if (strcmp(device_name, target_device_name) == 0) {
        strcpy(target_param_name,
               &argv[1]->s + strlen(target_device_name));
        strcpy(src_device_name, &argv[0]->s);
        strtok(src_device_name, "/");
        strcpy(src_param_name, &argv[0]->s + strlen(src_device_name));

        trace("got /connect %s%s %s%s\n", src_device_name, src_param_name,
              target_device_name, target_param_name);

        while (i < md_num_inputs && f == 0) {

            if (strcmp(md_inputs[i]->name, target_param_name) == 0) {
                lo_message m = lo_message_new();
                lo_message_add(m, "ss",
                               strcat(src_device_name, src_param_name),
                               strcat(target_device_name,
                                      target_param_name));

                /*Add device connection info */
                /*lo_message_add(m, "sssi", "@IP",
                   inet_ntoa((*((mapper_admin)
                   user_data)).interface_ip), "@port",
                   (*((mapper_admin) user_data)).port.value); */

                /*If options added to the connect message */
                if (argc > 2) {
                    while (j < argc) {
                        switch (types[j]) {
                        case ('s'):    /*case ('S'): */
                            lo_message_add(m, "s", (char *) &argv[j]->s);
                            break;

                        case ('i'):    /*case ('h'): */
                            lo_message_add(m, "i", argv[j]->i);
                            break;

                        case ('f'):
                            lo_message_add(m, "f", argv[j]->f);
                            break;

                        default:
                            printf("Unknown message type %c\n", types[j]);
                        }
                        j++;
                    }
                } else {
                    /*Add default connection info: type, range */
                    temp_type[0] = md_inputs[i]->type;
                    temp_type[1] = '\0';
                    lo_message_add(m, "ss", "@type", temp_type);
                    if (temp_type[0] == 'f')
                        lo_message_add(m, "sfsf", "@min",
                                       md_inputs[i]->minimum->f, "@max",
                                       md_inputs[i]->maximum->f);
                    else if (temp_type[0] == 'i')
                        lo_message_add(m, "sisi", "@min",
                                       md_inputs[i]->minimum->i32, "@max",
                                       md_inputs[i]->maximum->i32);
                }

                lo_send_message((*((mapper_admin) user_data)).admin_addr,
                                "/connect_to", m);
                lo_message_free(m);
                f = 1;
            } else
                i++;
        }
    }
    return 0;
}

/*! Report existing connections to the network */
static int handler_device_connections_get(const char *path,
                                          const char *types,
                                          lo_arg **argv, int argc,
                                          lo_message msg, void *user_data)
{
    char src_name[256], target_name[256];
    int i = 0;
    mapper_admin admin = (mapper_admin) user_data;
    mapper_device md = admin->device;
    mapper_router router = md->routers;

    int md_num_outputs = (*((mapper_admin) user_data)).device->n_outputs;
    mapper_signal *md_outputs =
        (*((mapper_admin) user_data)).device->outputs;

    while (i < md_num_outputs) {

        /* Searches the router linking to the receiver */
        while (router != NULL) {
            mapper_signal_mapping sm = router->mappings;
            snprintf(src_name, 256, "/%s.%d%s",
                     (*((mapper_admin) user_data)).identifier,
                     (*((mapper_admin) user_data)).ordinal.value,
                     md_outputs[i]->name);
            while (sm != NULL) {
                mapper_mapping m = sm->mapping;
                snprintf(target_name, 256, "%s%s", router->target_name,
                         m->name);
                lo_send((*((mapper_admin) user_data)).admin_addr,
                        "/connected", "ss", src_name, target_name);
                sm = sm->next;
            }
            router = router->next;
        }
        i++;
    }


    return 0;
}
