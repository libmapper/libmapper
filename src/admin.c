
#include <lo/lo.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <ifaddrs.h>
#include <arpa/inet.h>
#include <sys/time.h>
#include <net/if.h>

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
static int handler_device(const char *, const char *, lo_arg **,
                          int, lo_message, void *);
static int handler_logout(const char *, const char *, lo_arg **,
                          int, lo_message, void *);
static int handler_id_n_signals_input_get(const char *, const char *,
                                          lo_arg **, int, lo_message, 
                                          void *);
static int handler_id_n_signals_output_get(const char *, const char *,
                                           lo_arg **, int, lo_message,
                                           void *);
static int handler_id_n_signals_get(const char *, const char *,
                                    lo_arg **, int, lo_message, void *);
static int handler_signal_info(const char *, const char *, lo_arg **,
                               int, lo_message, void *);
static int handler_device_port_probe(const char *, const char *, lo_arg **,
                                     int, lo_message, void *);
static int handler_device_name_probe(const char *, const char *, lo_arg **,
                                     int, lo_message, void *);
static int handler_device_port_registered(const char *, const char *, lo_arg **,
                                          int, lo_message, void *);
static int handler_device_name_registered(const char *, const char *, lo_arg **,
                                          int, lo_message, void *);
static int handler_device_link(const char *, const char *, lo_arg **, int,
                               lo_message, void *);
static int handler_device_linkTo(const char *, const char *, lo_arg **,
                                 int, lo_message, void *);
static int handler_device_linked(const char *, const char *, lo_arg **,
                                 int, lo_message, void *);
static int handler_device_unlink(const char *, const char *, lo_arg **,
                                 int, lo_message, void *);
static int handler_device_unlinked(const char *, const char *, lo_arg **,
                                   int, lo_message, void *);
static int handler_device_links_get(const char *, const char *, lo_arg **,
                                    int, lo_message, void *);
static int handler_signal_connect(const char *, const char *, lo_arg **,
                                  int, lo_message, void *);
static int handler_signal_connectTo(const char *, const char *, lo_arg **,
                                    int, lo_message, void *);
static int handler_signal_connected(const char *, const char *, lo_arg **,
                                    int, lo_message, void *);
static int handler_signal_connection_modify(const char *, const char *,
                                            lo_arg **, int, lo_message, void *);
static int handler_signal_disconnect(const char *, const char *, lo_arg **,
                                     int, lo_message, void *);
static int handler_signal_disconnected(const char *, const char *, lo_arg **,
                                       int, lo_message, void *);
static int handler_device_connections_get(const char *, const char *,
                                          lo_arg **, int, lo_message,
                                          void *);

/* Handler <-> Message relationships */
struct handler_method_assoc {
    char* path;
    char *types;
    lo_method_handler h;
};
static struct handler_method_assoc device_handlers[] = {
    {"/who",                    "",         handler_who},
    {"%s/signals/get",          "",         handler_id_n_signals_get},
    {"%s/signals/input/get",    "",         handler_id_n_signals_input_get},
    {"%s/signals/output/get",   "",         handler_id_n_signals_output_get},
    {"%s/info/get",             "",         handler_who},
    {"%s/links/get",            "",         handler_device_links_get},
    {"/link",                   "ss",       handler_device_link},
    {"/linkTo",                 "sssssi",   handler_device_linkTo},
    {"/unlink",                 "ss",       handler_device_unlink},
    {"%s/connections/get",      "",         handler_device_connections_get},
    {"/connect",                NULL,       handler_signal_connect},
    {"/connectTo",              NULL,       handler_signal_connectTo},
    {"/connection/modify",      NULL,       handler_signal_connection_modify},
    {"/disconnect",             "ss",       handler_signal_disconnect},
    {"/logout",                 NULL,       handler_logout},
};
const int N_DEVICE_HANDLERS =
    sizeof(device_handlers)/sizeof(device_handlers[0]);

static struct handler_method_assoc monitor_handlers[] = {
    {"/device",                 NULL,       handler_device},
    {"/logout",                 NULL,       handler_logout},
    {"/signal",                 NULL,       handler_signal_info},
    {"/linked",                 "ss",       handler_device_linked},
    {"/unlinked",               "ss",       handler_device_unlinked},
    {"/connected",              NULL,       handler_signal_connected},
    {"/disconnected",           "ss",       handler_signal_disconnected},
};
const int N_MONITOR_HANDLERS =
    sizeof(monitor_handlers)/sizeof(monitor_handlers[0]);

/* Internal LibLo error handler */
static void handler_error(int num, const char *msg, const char *where)
{
    printf("[libmapper] liblo server error %d in path %s: %s\n",
           num, where, msg);
}

/* Functions for handling the resource allocation scheme.  If
 * check_collisions() returns 1, the resource in question should be
 * probed on the admin bus. */
static int check_collisions(mapper_admin admin,
                            mapper_admin_allocated_t *resource);

/*! Local function to get the IP address of a network interface. */
static int get_interface_addr(const char* pref,
                              struct in_addr* addr, char **iface)
{
    struct ifaddrs *ifaphead;
    struct ifaddrs *ifap;
    struct ifaddrs *iflo=0, *ifchosen=0;
    struct in_addr zero;
    struct sockaddr_in *sa;

    if (getifaddrs(&ifaphead) != 0)
        return 1;

    inet_aton("0.0.0.0", &zero);

    ifap = ifaphead;
    while (ifap) {
        sa = (struct sockaddr_in *) ifap->ifa_addr;
        if (!sa) {
            ifap = ifap->ifa_next;
            continue;
        }

        // Note, we could also check for IFF_MULTICAST-- however this
        // is the data-sending port, not the admin bus port.

        if (sa->sin_family == AF_INET && ifap->ifa_flags & IFF_UP
            && memcmp(&sa->sin_addr, &zero, sizeof(struct in_addr))!=0)
        {
            ifchosen = ifap;
            if (pref && strcmp(ifap->ifa_name, pref)==0)
                break;
            else if (ifap->ifa_flags & IFF_LOOPBACK)
                iflo = ifap;
        }
        ifap = ifap->ifa_next;
    }

    // Default to loopback address in case user is working locally.
    if (!ifchosen)
        ifchosen = iflo;

    if (ifchosen) {
        if (*iface) free(*iface);
        *iface = strdup(ifchosen->ifa_name);
        sa = (struct sockaddr_in *) ifchosen->ifa_addr;
        *addr = sa->sin_addr;
        freeifaddrs(ifaphead);
        return 0;
    }

    freeifaddrs(ifaphead);
    return 2;
}

static void mapper_admin_add_device_methods(mapper_admin admin)
{
    int i;
    char fullpath[256];
    for (i=0; i < N_DEVICE_HANDLERS; i++)
    {
        snprintf(fullpath, 256, device_handlers[i].path,
                 mapper_admin_name(admin));
        lo_server_add_method(admin->admin_server, fullpath,
                             device_handlers[i].types,
                             device_handlers[i].h,
                             admin);
    }
}

static void mapper_admin_add_monitor_methods(mapper_admin admin)
{
    int i;
    for (i=0; i < N_MONITOR_HANDLERS; i++)
    {
        lo_server_add_method(admin->admin_server,
                             monitor_handlers[i].path,
                             monitor_handlers[i].types,
                             monitor_handlers[i].h,
                             admin);
    }
}

mapper_admin mapper_admin_new(const char *iface, const char *group, int port)
{
    mapper_admin admin = (mapper_admin)calloc(1, sizeof(mapper_admin_t));
    if (!admin)
        return NULL;

    admin->interface = 0;

    /* Default standard ip and port is group 224.0.1.3, port 7570 */
    char port_str[10], *s_port = port_str;
    if (!group) group = "224.0.1.3";
    if (port==0)
        s_port = "7570";
    else
        snprintf(port_str, 10, "%d", port);

    /* Initialize interface information.  We'll use defaults for now,
     * perhaps this should be configurable in the future. */
    if (get_interface_addr(iface, &admin->interface_ip,
                           &admin->interface))
        trace("no interface found\n");

    /* Open address */
    admin->admin_addr = lo_address_new(group, s_port);
    if (!admin->admin_addr) {
        free(admin->identifier);
        free(admin);
        return NULL;
    }

    /* Set TTL for packet to 1 -> local subnet */
    lo_address_set_ttl(admin->admin_addr, 1);

    /* Specify the interface to use for multicasting */
#ifdef HAVE_LIBLO_SET_IFACE
    lo_address_set_iface(admin->admin_addr,
                         admin->interface, 0);
#endif

    /* Open server for multicast group 224.0.1.3, port 7570 */
    admin->admin_server =
        lo_server_new_multicast(group, s_port, handler_error);
    if (!admin->admin_server) {
        free(admin->identifier);
        lo_address_free(admin->admin_addr);
        free(admin);
        return NULL;
    }

    return admin;
}

/*! Free the memory allocated by a mapper admin structure.
 *  \param admin An admin structure handle.
 */
void mapper_admin_free(mapper_admin admin)
{
    if (!admin)
        return;

    if (admin->port.locked && admin->ordinal.locked) {
        // A registered device must tell the network it is leaving.
        mapper_admin_send_osc(admin, "/logout", "s", mapper_admin_name(admin));
    }

    if (admin->identifier)
        free(admin->identifier);

    if (admin->name)
        free(admin->name);

    if (admin->interface)
        free(admin->interface);

    if (admin->admin_server)
        lo_server_free(admin->admin_server);

    if (admin->admin_addr)
        lo_address_free(admin->admin_addr);

    free(admin);
}

/*! Add an uninitialized device to this admin. */
void mapper_admin_add_device(mapper_admin admin, mapper_device dev,
                             const char *identifier, int initial_port)
{
    int i;
    /* Initialize data structures */
    if (dev)
    {
        admin->identifier = strdup(identifier);
        admin->name = 0;
        admin->ordinal.value = 1;
        admin->ordinal.locked = 0;
        admin->port.value = initial_port;
        admin->port.locked = 0;
        admin->registered = 0;
        admin->device = dev;
        for (i=0; i<8; i++) {
            admin->ordinal.suggestion[i] = 0;
            admin->port.suggestion[i] = 0;
        }
        admin->device->update = 0;
        
        /* Choose a random ID for allocation speedup */
        admin->random_id = rand();

        /* Add methods for admin bus.  Only add methods needed for
         * allocation here. Further methods are added when the device is
         * registered. */
        lo_server_add_method(admin->admin_server, "/port/probe", NULL,
                             handler_device_port_probe, admin);
        lo_server_add_method(admin->admin_server, "/name/probe", NULL,
                             handler_device_name_probe, admin);
        lo_server_add_method(admin->admin_server, "/port/registered", NULL,
                             handler_device_port_registered, admin);
        lo_server_add_method(admin->admin_server, "/name/registered", NULL,
                             handler_device_name_registered, admin);

        /* Probe potential port and name to admin bus. */
        mapper_admin_port_probe(admin);
        mapper_admin_name_probe(admin);
    }
}

/*! Add an uninitialized monitor to this admin. */
void mapper_admin_add_monitor(mapper_admin admin, mapper_monitor mon)
{
    /* Initialize monitor methods. */
    if (mon) {
        admin->monitor = mon;
        mapper_admin_add_monitor_methods(admin);
        mapper_admin_send_osc(admin, "/who", "");
    }
}

/*! This is the main function to be called once in a while from a
 *  program so that the admin bus can be automatically managed.
 */
int mapper_admin_poll(mapper_admin admin)
{

    int count = 0, status;

    while (count < 10 && lo_server_recv_noblock(admin->admin_server, 0)) {
        count++;
    }

    if (!admin->device)
        return count;

    /* If the port is not yet locked, process collision timing.  Once
     * the port is locked it won't change. */
    if (!admin->port.locked) {
        status = check_collisions(admin, &admin->port);
        if (status == 1) {
            /* If the port has changed, re-probe the new potential port. */
            mapper_admin_port_probe(admin);
        }
        else if (status == 2) {
            /* If the allocation routine has succeeded, send registered msg. */
            lo_send(admin->admin_addr, "/port/registered",
                    "i", admin->port.value);
        }
    }

    /* If the ordinal is not yet locked, process collision timing.
     * Once the ordinal is locked it won't change. */
    if (!admin->ordinal.locked) {
        status = check_collisions(admin, &admin->ordinal);
        if (status == 1) {
            /* If the ordinal has changed, re-probe the new name. */
            mapper_admin_name_probe(admin);
        }
        else if (status == 2) {
            /* If the allocation routine has succeeded, send registered msg. */
            lo_send(admin->admin_addr, "/name/registered",
                    "s", mapper_admin_name(admin));
        }
    }

    /* If we are ready to register the device, add the needed message
     * handlers. */
    if (!admin->registered
        && admin->port.locked && admin->ordinal.locked)
    {
        mapper_admin_add_device_methods(admin);

        admin->registered = 1;
        trace("</%s.?::%p> registered as <%s>\n",
              admin->identifier, admin, mapper_admin_name(admin));
        admin->device->update = 1;
    }
    if (admin->registered && admin->device->update) {
        admin->device->update = 0;
        mapper_admin_send_osc(
              admin, "/device", "s", mapper_admin_name(admin),
              AT_IP, inet_ntoa(admin->interface_ip),
              AT_PORT, admin->port.value,
              AT_NUMINPUTS, admin->device ? mdev_num_inputs(admin->device) : 0,
              AT_NUMOUTPUTS, admin->device ? mdev_num_outputs(admin->device) : 0,
              AT_REV, admin->device->version,
              AT_EXTRA, admin->device->extra);
    }
    return count;
}

/*! Probe the admin bus to see if a device's proposed port is already
 *  taken.
 */
void mapper_admin_port_probe(mapper_admin admin)
{
    trace("</%s.?::%p> probing port\n", admin->identifier, admin);
    
    admin->port.collision_count = -1;
    admin->port.count_time = get_current_time();

    /* We don't use mapper_admin_send_osc() here because the name is
     * not yet established and it would trigger a warning. */
    lo_send(admin->admin_addr, "/port/probe", "ii", admin->port.value, admin->random_id);
}

/*! Probe the admin bus to see if a device's proposed name.ordinal is
 *  already taken.
 */
void mapper_admin_name_probe(mapper_admin admin)
{
    admin->ordinal.collision_count = -1;
    admin->ordinal.count_time = get_current_time();
    
    /* Note: mapper_admin_name() would refuse here since the
     * ordinal is not yet locked, so we have to build it manually at
     * this point. */
    char name[256];
    trace("</%s.?::%p> probing name\n", admin->identifier, admin);
    snprintf(name, 256, "/%s.%d", admin->identifier, admin->ordinal.value);

    /* For the same reason, we can't use mapper_admin_send_osc()
     * here. */
    lo_send(admin->admin_addr, "/name/probe", "si", name, admin->random_id);
}

const char *_real_mapper_admin_name(mapper_admin admin,
                                    const char *file, unsigned int line)
{
#ifdef DEBUG
    if (!admin->identifier || !admin->device)
        trace("mapper_admin_name() called on non-device admin at %s:%d.\n",
              file, line);
#endif

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

    if (timediff >= 2.0 && resource->collision_count <= 1) {
        resource->locked = 1;
        if (resource->on_lock)
            resource->on_lock(admin->device, resource);
        return 2;
    }
    
    else if (timediff >= 0.5 && resource->collision_count > 0) {
        /* If port collisions were found within 500 milliseconds of the
         * last probe, add a random number based on the number of
         * collisions. */
        resource->value += rand() % (resource->collision_count + 1);

        /* Prepare for causing new port collisions. */
        resource->collision_count = -1;
        resource->count_time = get_current_time();

        /* Indicate that we need to re-probe the new value. */
        return 1;
    }

    return 0;
}

void _real_mapper_admin_send_osc(mapper_admin admin, const char *path,
                                 const char *types, ...)
{
    char str[1024];
    const char *namedpath=str;

    /* If string wants a name, mapper_admin_name() will complain about
     * no device in debug mode.  Otherwise, in non-debug mode, just
     * don't ask for the name if there's no device. */
#ifdef DEBUG
    if (strstr(path, "%s"))
#else
    if (admin->device)
#endif
    {
        snprintf(str, 1024, path, mapper_admin_name(admin));
    }
    else
        namedpath = path;

    char t[]=" ";

    lo_message m = lo_message_new();
    if (!m) {
        trace("couldn't allocate lo_message\n");
        return;
    }

    va_list aq;
    va_start(aq, types);

    while (types && *types) {
        t[0] = types[0];
        switch (t[0]) {
        case 'i': lo_message_add(m, t, va_arg(aq, int)); break;
        case 's': lo_message_add(m, t, va_arg(aq, char*)); break;
        case 'f': lo_message_add(m, t, va_arg(aq, double)); break;
        default:
            die_unless(0, "message %s, unknown type '%c'\n",
                       path, t[0]);
        }
        types++;
    }

    mapper_msg_prepare_varargs(m, aq);

    va_end(aq);

    lo_send_message(admin->admin_addr, namedpath, m);
    lo_message_free(m);
}

void _real_mapper_admin_send_osc_with_params(const char *file, int line,
                                             mapper_admin admin,
                                             mapper_message_t *params,
                                             mapper_string_table_t *extra,
                                             const char *path,
                                             const char *types, ...)
{
    char namedpath[1024];
    snprintf(namedpath, 1024, path, mapper_admin_name(admin));

    lo_message m = lo_message_new();
    if (!m) {
        trace("couldn't allocate lo_message\n");
        return;
    }

    va_list aq;
    va_start(aq, types);
    lo_message_add_varargs_internal(m, types, aq, file, line);

    mapper_msg_prepare_params(m, params);
    if (extra)
        mapper_msg_add_osc_value_table(m, extra);

    lo_send_message(admin->admin_addr, namedpath, m);
    lo_message_free(m);
}

static void mapper_admin_send_connected(mapper_admin admin,
                                        mapper_router router,
                                        mapper_mapping m)
{
    // Send /connected message
    lo_message mess = lo_message_new();
    if (!mess) {
        trace("couldn't allocate lo_message\n");
    }

    char src_name[1024], dest_name[1024];

    snprintf(src_name, 1024, "%s%s",
             mdev_name(router->device), m->props.src_name);

    snprintf(dest_name, 1024, "%s%s",
             router->dest_name, m->props.dest_name);

    lo_message_add_string(mess, src_name);
    lo_message_add_string(mess, dest_name);

    mapper_mapping_prepare_osc_message(mess, m);

    lo_send_message(admin->admin_addr, "/connected", mess);
    lo_message_free(mess);
}

/**********************************/
/* Internal OSC message handlers. */
/**********************************/

/*! Respond to /who by announcing the current device information. */
static int handler_who(const char *path, const char *types, lo_arg **argv,
                       int argc, lo_message msg, void *user_data)
{
    mapper_admin admin = (mapper_admin) user_data;

    mapper_admin_send_osc(
        admin, "/device", "s", mapper_admin_name(admin),
        AT_IP, inet_ntoa(admin->interface_ip),
        AT_PORT, admin->port.value,
        AT_NUMINPUTS, admin->device ? mdev_num_inputs(admin->device) : 0,
        AT_NUMOUTPUTS, admin->device ? mdev_num_outputs(admin->device) : 0,
        AT_REV, admin->device->version,
        AT_EXTRA, admin->device->extra);

    return 0;
}


/*! Register information about port and host for the device. */
static int handler_device(const char *path, const char *types,
                          lo_arg **argv, int argc, lo_message msg,
                          void *user_data)
{
    mapper_admin admin = (mapper_admin) user_data;
    mapper_monitor mon = admin->monitor;
    mapper_db db = mapper_monitor_get_db(mon);

    if (argc < 1)
        return 0;

    if (types[0] != 's' && types[0] != 'S')
        return 0;

    const char *name = &argv[0]->s;

    mapper_message_t params;
    mapper_msg_parse_params(&params, path, &types[1],
                            argc-1, &argv[1]);

    mapper_db_add_or_update_device_params(db, name, &params);

    return 0;
}

/*! Respond to /logout by deleting record of device. */
static int handler_logout(const char *path, const char *types,
                          lo_arg **argv, int argc, lo_message msg,
                          void *user_data)
{
    mapper_admin admin = (mapper_admin) user_data;
    mapper_monitor mon = admin->monitor;
    mapper_db db = mapper_monitor_get_db(mon);
    int diff, ordinal;
    char *s;

    if (argc < 1)
        return 0;

    if (types[0] != 's' && types[0] != 'S')
        return 0;

    char *name = &argv[0]->s;

    trace("got /logout %s\n", name);
    
    if (mon) {
        mapper_db_remove_device(db, name);
    }
    
    // If device exists and is registered
    if (admin->ordinal.locked) {
        /* Parse the ordinal from the complete name which is in the
         * format: /<name>.<n> */
        s = name;
        if (*s++ != '/')
            return 0;
        while (*s != '.' && *s++) {
        }
        ordinal = atoi(++s);
        
        // If device name matches
        strtok(name, ".");
        name++;
        if (strcmp(name, admin->identifier) == 0) {
        
            // if registered ordinal is within my block, free it
            diff = ordinal - admin->ordinal.value;
            if (diff > 0 && diff < 9) {
                admin->ordinal.suggestion[diff-1] = 0;
            }
        }
    }

    return 0;
}

/*! Respond to /signals/input/get by enumerating all supported
 *  inputs. */
static int handler_id_n_signals_input_get(const char *path,
                                          const char *types,
                                          lo_arg **argv, int argc,
                                          lo_message msg,
                                          void *user_data)
{
    mapper_admin admin = (mapper_admin) user_data;
    mapper_device md = admin->device;
    char sig_name[1024];
    int i;

    for (i = 0; i < md->n_inputs; i++) {
        mapper_signal sig = md->inputs[i];
        msig_full_name(sig, sig_name, 1024);
        mapper_admin_send_osc(
            admin, "/signal", "s", sig_name,
            AT_DIRECTION, "input",
            AT_TYPE, sig->props.type,
            AT_LENGTH, sig->props.length,
            sig->props.minimum ? AT_MIN : -1, sig,
            sig->props.maximum ? AT_MAX : -1, sig,
            AT_EXTRA, sig->props.extra);
    }

    return 0;
}

/*! Respond to /signals/output/get by enumerating all supported
 *  outputs. */
static int handler_id_n_signals_output_get(const char *path,
                                           const char *types,
                                           lo_arg **argv, int argc,
                                           lo_message msg,
                                           void *user_data)
{
    mapper_admin admin = (mapper_admin) user_data;
    mapper_device md = admin->device;
    char sig_name[1024];
    int i;

    for (i = 0; i < md->n_outputs; i++) {
        mapper_signal sig = md->outputs[i];
        msig_full_name(sig, sig_name, 1024);
        mapper_admin_send_osc(
            admin, "/signal", "s", sig_name,
            AT_DIRECTION, "output",
            AT_TYPE, sig->props.type,
            AT_LENGTH, sig->props.length,
            sig->props.minimum ? AT_MIN : -1, sig,
            sig->props.maximum ? AT_MAX : -1, sig,
            AT_EXTRA, sig->props.extra);
    }

    return 0;
}

/*! Respond to /signals/get by enumerating all supported inputs and
 *  outputs. */
static int handler_id_n_signals_get(const char *path, const char *types,
                                    lo_arg **argv, int argc,
                                    lo_message msg, void *user_data)
{

    handler_id_n_signals_input_get(path, types, argv, argc, msg,
                                   user_data);
    handler_id_n_signals_output_get(path, types, argv, argc, msg,
                                    user_data);

    return 0;

}

/*! Register information about a signal. */
static int handler_signal_info(const char *path, const char *types,
                               lo_arg **argv, int argc, lo_message m,
                               void *user_data)
{
    mapper_admin admin = (mapper_admin) user_data;
    mapper_monitor mon = admin->monitor;
    mapper_db db = mapper_monitor_get_db(mon);
    
    if (argc < 2)
        return 1;
    
    if (types[0] != 's' && types[0] != 'S')
        return 1;
    
    const char *full_sig_name = &argv[0]->s;
    const char *sig_name = strchr(full_sig_name+1, '/');
    if (!sig_name)
        return 1;
    
    int devnamelen = sig_name-full_sig_name;
    if (devnamelen >= 1024)
        return 0;
    
    char devname[1024];
    strncpy(devname, full_sig_name, devnamelen);
    devname[devnamelen]=0;
        
    mapper_message_t params;
    mapper_msg_parse_params(&params, path, &types[1],
                            argc-1, &argv[1]);

    mapper_db_add_or_update_signal_params( db, sig_name, 
                                          devname,
                                          &params );
    
	return 0;
}

/*! Repond to port collisions during allocation, help suggest ports once allocated. */
static int handler_device_port_registered(const char *path, const char *types,
                                          lo_arg **argv, int argc,
                                          lo_message msg, void *user_data)
{
    mapper_admin admin = (mapper_admin) user_data;
    unsigned int registered_port = 0;
    int ID = -1, suggestion = -1, diff;
    
    if (argc < 1)
        return 0;
    
    if (types[0] == 'i')
        registered_port = argv[0]->i;
    else if (types[0] == 'f')
        registered_port = (unsigned int) argv[0]->f;
    else
        return 0;
    
    if (argc > 1) {
        if (types[1] == 'i')
            ID = argv[1]->i;
        else if (types[1] == 'f')
            ID = (int) argv[1]->f;
        if (types[2] == 'i')
            suggestion = argv[2]->i;
        else if (types[2] == 'f')
            suggestion = (int) argv[2]->f;
    }
    
    trace("</%s.?::%p> got /port/registered %d %i \n",
          admin->identifier, admin, registered_port, ID);
    
    // if port is locked and registered port is within my block, store it
    if (admin->port.locked) {
        diff = registered_port - admin->port.value;
        if (diff > 0 && diff < 9) {
            admin->port.suggestion[diff-1] = -1;
        }
    }
    else {
        if (registered_port == admin->port.value) {
            if (ID == admin->random_id && suggestion > 0) {
                admin->port.value = suggestion;
                mapper_admin_port_probe(admin);
            }
            else {
                /* Count port collisions. */
                admin->port.collision_count++;
                admin->port.count_time = get_current_time();
            }
        }
    }
    return 0;
}

/*! Repond to name collisions during allocation, help suggest names once allocated. */
static int handler_device_name_registered(const char *path, const char *types,
                                          lo_arg **argv, int argc,
                                          lo_message msg, void *user_data)
{
    mapper_admin admin = (mapper_admin) user_data;
    char *registered_name = 0, *suggested_name = 0, *s;
    unsigned int registered_ordinal = 0;
    int ID = -1, diff;
    
    if (argc < 1)
        return 0;
    
    if (types[0] != 's' && types[0] != 'S')
        return 0;
    
    registered_name = &argv[0]->s;
    
    if (argc > 1) {
        if (types[1] == 'i')
            ID = argv[1]->i;
        else if (types[1] == 'f')
            ID = (int) argv[1]->f;
        if (types[2] == 's' || types[2] == 'S')
            suggested_name = &argv[2]->s;
    }
    
    /* Parse the ordinal from the complete name which is in the
     * format: /<name>.<n> */
    s = registered_name;
    if (*s++ != '/')
        return 0;
    while (*s != '.' && *s++) {
    }
    registered_ordinal = atoi(++s);
    
    trace("</%s.?::%p> got /name/registered %s %i \n",
          admin->identifier, admin, registered_name, ID);
    
    // If device name matches
    strtok(registered_name, ".");
    registered_name++;
    if (strcmp(registered_name, admin->identifier) == 0) {
    
        // if ordinal is locked and registered ordinal is within my block, store it
        if (admin->ordinal.locked) {
            diff = registered_ordinal - admin->ordinal.value;
            if (diff > 0 && diff < 9) {
                admin->ordinal.suggestion[diff-1] = -1;
            }
        }
        else {
            if (registered_ordinal == admin->ordinal.value) {
                if (ID == admin->random_id && suggested_name) {
                    // Parse the ordinal from the suggested name
                    s = suggested_name;
                    if (*s++ != '/')
                        return 0;
                    while (*s != '.' && *s++) {
                    }
                    admin->ordinal.value = atoi(++s);
                    mapper_admin_name_probe(admin);
                }
                else {
                    /* Count port collisions. */
                    admin->ordinal.collision_count++;
                    admin->ordinal.count_time = get_current_time();
                }
            }
        }
    }
    return 0;
}

/*! Repond to port probes during allocation, help suggest ports once allocated. */
static int handler_device_port_probe(const char *path, const char *types,
                                     lo_arg **argv, int argc,
                                     lo_message msg, void *user_data)
{
    mapper_admin admin = (mapper_admin) user_data;
    double current_time;
    unsigned int probed_port = 0;
    int ID = -1, i;

    if (argc < 1)
        return 0;

    if (types[0] == 'i')
        probed_port = argv[0]->i;
    else if (types[0] == 'f')
        probed_port = (unsigned int) argv[0]->f;
    else
        return 0;
    
    if (argc > 0) {
        if (types[1] == 'i')
            ID = argv[1]->i;
        else if (types[1] == 'f')
            ID = (int) argv[1]->f;
    }

    trace("</%s.?::%p> got /port/probe %d %i \n",
          admin->identifier, admin, probed_port, ID);
    
    if (probed_port == admin->port.value) {
        if (admin->port.locked) {
            current_time = get_current_time();
            for (i=0; i<8; i++) {
                if (admin->port.suggestion[i] >= 0 
                    && (current_time - admin->port.suggestion[i]) > 2.0) {
                    // reserve suggested port
                    admin->port.suggestion[i] = get_current_time();
                    break;
                }
            }
            /* Name may not yet be registered, so we can't use
             * mapper_admin_send_osc() here. */
            lo_send(admin->admin_addr, "/port/registered",
                    "iii", admin->port.value, ID, 
                    (admin->port.value+i+1));
        }
        else {
            admin->port.collision_count++;
            admin->port.count_time = get_current_time();
        }
    }
    return 0;
}

/*! Repond to name probes during allocation, help suggest names once allocated. */
static int handler_device_name_probe(const char *path, const char *types,
                                     lo_arg **argv, int argc,
                                     lo_message msg, void *user_data)
{
    mapper_admin admin = (mapper_admin) user_data;
    double current_time;
    char *probed_name = 0, *s;
    unsigned int probed_ordinal = 0;
    int ID = -1, i;

    if (argc < 1)
        return 0;

    if (types[0] != 's' && types[0] != 'S')
        return 0;

    probed_name = &argv[0]->s;
    
    if (argc > 0) {
        if (types[1] == 'i')
            ID = argv[1]->i;
        else if (types[1] == 'f')
            ID = (int) argv[1]->f;
    }

    /* Parse the ordinal from the complete name which is in the
     * format: /<name>.<n> */
    s = probed_name;
    if (*s++ != '/')
        return 0;
    while (*s != '.' && *s++) {
    }
    probed_ordinal = atoi(++s);

    trace("</%s.?::%p> got /name/probe %s\n",
          admin->identifier, admin, probed_name);

    /* Process ordinal collisions. */
    //The collision should be calculated separately per-device-name
    strtok(probed_name, ".");
    probed_name++;
    if ((strcmp(probed_name, admin->identifier) == 0)
        && (probed_ordinal == admin->ordinal.value)) {
        if (admin->ordinal.locked) {
            current_time = get_current_time();
            for (i=0; i<8; i++) {
                if (admin->ordinal.suggestion[i] >= 0 
                    && (current_time - admin->ordinal.suggestion[i]) > 2.0) {
                    // reserve suggested ordinal
                    admin->ordinal.suggestion[i] = get_current_time();
                    break;
                }
            }
            char suggested_name[256];
            snprintf(suggested_name, 256, "/%s.%d", admin->identifier, 
                     (admin->ordinal.value+i+1));
            lo_send(admin->admin_addr, "/name/registered",
                    "sis", mapper_admin_name(admin), ID, 
                    suggested_name);
        }
        else {
            admin->ordinal.collision_count++;
            admin->ordinal.count_time = get_current_time();
        }
    }
    return 0;
}

/*! Link two devices. */
static int handler_device_link(const char *path, const char *types,
                               lo_arg **argv, int argc, lo_message msg,
                               void *user_data)
{
    printf("got /link\n");
    mapper_admin admin = (mapper_admin) user_data;
    const char *src_name, *dest_name;

    if (argc < 2)
        return 0;

    if (types[0] != 's' && types[0] != 'S' && types[1] != 's'
        && types[1] != 'S')
        return 0;

    src_name = &argv[0]->s;
    dest_name = &argv[1]->s;

    trace("<%s> got /link %s %s\n", mapper_admin_name(admin),
          src_name, dest_name);

    /* If the device who received the message is the destination in the
     * /link message... */
    if (strcmp(mapper_admin_name(admin), dest_name) == 0) {
        mapper_admin_send_osc(
            admin, "/linkTo", "ss", src_name, dest_name,
            AT_IP, inet_ntoa(admin->interface_ip),
            AT_PORT, admin->port.value);
    }
    return 0;
}

/*! Link two devices... continued. */
static int handler_device_linkTo(const char *path, const char *types,
                                 lo_arg **argv, int argc, lo_message msg,
                                 void *user_data)
{
    mapper_admin admin = (mapper_admin) user_data;
    mapper_device md = admin->device;

    const char *src_name, *dest_name, *host=0;
    int port;
    mapper_message_t params;

    if (argc < 2)
        return 0;

    if (types[0] != 's' && types[0] != 'S' && types[1] != 's'
        && types[1] != 'S')
        return 0;

    src_name = &argv[0]->s;
    dest_name = &argv[1]->s;

    if (strcmp(src_name, mapper_admin_name(admin)))
    {
        trace("<%s> ignoring /linkTo %s %s\n",
              mapper_admin_name(admin), src_name, dest_name);
        return 0;
    }

    trace("<%s> got /linkTo %s %s\n", mapper_admin_name(admin),
          src_name, dest_name);

    // Discover whether the device is already linked.
    mapper_router router =
        mapper_router_find_by_dest_name(md->routers, dest_name);

    if (router)
        // Already linked, nothing to do.
        return 0;

    // Parse the message.
    if (mapper_msg_parse_params(&params, path, &types[2],
                                argc-2, &argv[2]))
        return 0;

    // Check the results.
    host = mapper_msg_get_param_if_string(&params, AT_IP);
    if (!host) {
        trace("can't perform /linkTo, host unknown\n");
        return 0;
    }

    if (mapper_msg_get_param_if_int(&params, AT_PORT, &port)) {
        trace("can't perform /linkTo, port unknown\n");
        return 0;
    }

    // Creation of a new router added to the source.
    router = mapper_router_new(md, host, port, dest_name);
    mdev_add_router(md, router);

    // Announce the result.
    mapper_admin_send_osc(admin, "/linked", "ss",
                          mapper_admin_name(admin), dest_name);

    trace("new router to %s -> host: %s, port: %d\n",
          dest_name, host, port);

    return 0;
}

/*! Store record of linked devices. */
static int handler_device_linked(const char *path, const char *types,
                                 lo_arg **argv, int argc, lo_message msg,
                                 void *user_data)
{
    mapper_admin admin = (mapper_admin) user_data;
    mapper_monitor mon = admin->monitor;
    mapper_db db = mapper_monitor_get_db(mon);
    const char *src_name, *dest_name;

    if (argc < 2)
        return 0;

    if (types[0] != 's' && types[0] != 'S' && types[1] != 's'
        && types[1] != 'S')
        return 0;

    src_name = &argv[0]->s;
    dest_name = &argv[1]->s;

    trace("<%s> got /linked %s %s\n", mapper_admin_name(admin),
          src_name, dest_name);

    mapper_message_t params;
    if (mapper_msg_parse_params(&params, path, types+2, argc-2, argv+2))
        return 0;
    mapper_db_add_or_update_link_params(db, src_name, dest_name, &params);

    return 0;
}

/*! Report existing links to the network */
static int handler_device_links_get(const char *path, const char *types,
                                    lo_arg **argv, int argc,
                                    lo_message msg, void *user_data)
{
    mapper_admin admin = (mapper_admin) user_data;
    mapper_device md = admin->device;
    mapper_router router = md->routers;

    trace("<%s> got %s/links/get\n", mapper_admin_name(admin),
          mapper_admin_name(admin));

    /*Search through linked devices */
    while (router != NULL) {
        mapper_admin_send_osc(admin, "/linked", "ss", mapper_admin_name(admin),
                              router->dest_name);
        router = router->next;
    }
    return 0;
}

/*! Unlink two devices. */
static int handler_device_unlink(const char *path, const char *types,
                                 lo_arg **argv, int argc, lo_message msg,
                                 void *user_data)
{
    const char *src_name, *dest_name;
    mapper_admin admin = (mapper_admin) user_data;
    mapper_device md = admin->device;

    if (argc < 2)
        return 0;

    if (types[0] != 's' && types[0] != 'S' && types[1] != 's'
        && types[1] != 'S')
        return 0;

    src_name = &argv[0]->s;
    dest_name = &argv[1]->s;

    trace("<%s> got /unlink %s %s\n", mapper_admin_name(admin),
          src_name, dest_name);

    /* Check if we are the indicated source. */
    if (strcmp(mapper_admin_name(admin), src_name))
        return 0;

    /* If so, remove the router for the destination. */
    mapper_router router =
        mapper_router_find_by_dest_name(md->routers, dest_name);
    if (router) {
        mdev_remove_router(md, router);
        mapper_admin_send_osc(admin, "/unlinked", "ss",
                              mapper_admin_name(admin), dest_name);
    }
    else {
        trace("<%s> no router for %s found in /unlink handler\n",
              mapper_admin_name(admin), dest_name);
    }

    return 0;
}

/*! Respond to /unlinked by removing link from database. */
static int handler_device_unlinked(const char *path, const char *types,
                                   lo_arg **argv, int argc, lo_message msg,
                                   void *user_data)
{
    mapper_admin admin = (mapper_admin) user_data;
    mapper_monitor mon = admin->monitor;
    mapper_db db = mapper_monitor_get_db(mon);

    if (argc < 2)
        return 0;

    if (types[0] != 's' && types[0] != 'S' && types[1] != 's'
        && types[1] != 'S')
        return 0;

    const char *src_name = &argv[0]->s;
    const char *dest_name = &argv[1]->s;

    trace("<%s> got /unlink %s %s\n", mapper_admin_name(admin),
          src_name, dest_name);

    mapper_db_remove_mappings_by_query(db,
        mapper_db_get_mappings_by_src_dest_device_names(db, src_name,
                                                        dest_name));

    mapper_db_remove_link(db,
        mapper_db_get_link_by_src_dest_names(db, src_name,
                                             dest_name));

    return 0;
}

/* Helper function to check if the OSC prefix matches.  Like strcmp(),
 * returns 0 if they match (up to the second '/'), non-0 otherwise.
 * Also optionally returns a pointer to the remainder of str1 after
 * the prefix. */
static int osc_prefix_cmp(const char *str1, const char *str2,
                          const char **rest)
{
    if (str1[0]!='/') {
        trace("OSC string '%s' does not start with '/'.\n", str1);
        return 0;
    }
    if (str2[0]!='/') {
        trace("OSC string '%s' does not start with '/'.\n", str2);
        return 0;
    }

    // skip first slash
    const char *s1=str1+1, *s2=str2+1;

    while (*s1 && (*s1)!='/') s1++;
    while (*s2 && (*s2)!='/') s2++;

    int n1 = s1-str1, n2 = s2-str2;
    if (n1!=n2) return 1;

    if (rest)
        *rest = s1;

    return strncmp(str1, str2, n1);
}

/*! When the /connect message is received by the destination device,
 *  send a connectTo message to the source device. */
static int handler_signal_connect(const char *path, const char *types,
                                  lo_arg **argv, int argc, lo_message msg,
                                  void *user_data)
{
    mapper_admin admin = (mapper_admin) user_data;
    mapper_device md = admin->device;
    mapper_signal input;

    const char *src_signal_name, *src_name;
    const char *dest_signal_name, *dest_name;

    if (argc < 2)
        return 0;

    if (types[0] != 's' && types[0] != 'S' && types[1] != 's'
        && types[1] != 'S')
        return 0;

    dest_name = &argv[1]->s;
    if (osc_prefix_cmp(dest_name, mapper_admin_name(admin),
                       &dest_signal_name))
        return 0;

    src_name = &argv[0]->s;
    src_signal_name = strchr(src_name+1, '/');

    if (!src_signal_name) {
        trace("<%s> source '%s' has no parameter in /connect.\n",
              mapper_admin_name(admin), src_name);
        return 0;
    }

    trace("<%s> got /connect %s %s\n", mapper_admin_name(admin),
          src_name, dest_name);

    if (!(input=mdev_get_input_by_name(md, dest_signal_name, 0)))
    {
        trace("<%s> no input signal found for '%s' in /connectTo\n",
              mapper_admin_name(admin), dest_signal_name);
        return 0;
    }

    mapper_message_t params;
    memset(&params, 0, sizeof(mapper_message_t));

    // add arguments from /connect if any
    if (mapper_msg_parse_params(&params, path, &types[2],
                                argc-2, &argv[2]))
    {
        trace("<%s> error parsing message parameters in /connect.\n",
              mapper_admin_name(admin));
        return 0;
    }

    // substitute some missing parameters with known properties
    lo_arg *arg_type = (lo_arg*) &input->props.type;
    if (!params.values[AT_TYPE]) {
        params.values[AT_TYPE] = &arg_type;
        params.types[AT_TYPE] = "c";
    }

    lo_arg *arg_length = (lo_arg*) &input->props.length;
    if (!params.values[AT_LENGTH]) {
        params.values[AT_LENGTH] = &arg_length;
        params.types[AT_LENGTH] = "i";
    }

    lo_arg *arg_min = (lo_arg*) input->props.minimum;
    if (!params.values[AT_MIN] && input->props.minimum) {
        params.values[AT_MIN] = &arg_min;
        params.types[AT_MIN] = &input->props.type;
    }

    lo_arg *arg_max = (lo_arg*) input->props.maximum;
    if (!params.values[AT_MAX] && input->props.maximum) {
        params.values[AT_MAX] = &arg_max;
        params.types[AT_MAX] = &input->props.type;
    }

    mapper_admin_send_osc_with_params(
        admin, &params, input->props.extra,
        "/connectTo", "ss", src_name, dest_name);

    return 0;
}

/*! Connect two signals. */
static int handler_signal_connectTo(const char *path, const char *types,
                                    lo_arg **argv, int argc,
                                    lo_message msg, void *user_data)
{
    mapper_admin admin = (mapper_admin) user_data;
    mapper_device md = admin->device;
    mapper_signal output;

    const char *src_signal_name, *src_name;
    const char *dest_signal_name, *dest_name;

    if (argc < 2)
        return 0;

    if ((types[0] != 's' && types[0] != 'S')
        || (types[1] != 's' && types[1] != 'S'))
        return 0;

    src_name = &argv[0]->s;
    if (osc_prefix_cmp(src_name, mapper_admin_name(admin),
                       &src_signal_name))
        return 0;

    dest_name = &argv[1]->s;
    dest_signal_name = strchr(dest_name+1, '/');

    if (!dest_signal_name) {
        trace("<%s> destination '%s' has no parameter in /connectTo.\n",
              mapper_admin_name(admin), dest_name);
        return 0;
    }

    trace("<%s> got /connectTo %s %s + %d arguments\n",
          mapper_admin_name(admin), src_name, dest_name, argc);

    if (!(output=mdev_get_output_by_name(md, src_signal_name, 0)))
    {
        trace("<%s> no output signal found for '%s' in /connectTo\n",
              mapper_admin_name(admin), src_signal_name);
        return 0;
    }

    mapper_message_t params;
    if (mapper_msg_parse_params(&params, path, types+2, argc-2, &argv[2]))
    {
        trace("<%s> error parsing parameters in /connectTo, "
              "continuing anyway.\n", mapper_admin_name(admin));
    }

    mapper_router router =
        mapper_router_find_by_dest_name(md->routers, dest_name);

    if (!router) {
        trace("<%s> not linked to '%s' on /connectTo.\n",
              mapper_admin_name(admin), dest_name);
        // TODO: Perform /linkTo?

        const char *host = mapper_msg_get_param_if_string(&params, AT_IP);
        int port;
        if (host && !mapper_msg_get_param_if_int(&params, AT_PORT, &port))
        {
            // Port and host are valid
            // TO DO: create routed using supplied host and port info
            // TO DO: send /linked message
        }
        else {
            /* TO DO: send /link message to start process - should
             * also cache /connectTo message for completion after
             * link??? */
        }

        return 0;
    }

    /* Creation of a mapping requires the type and length info. */
    if (!params.values[AT_TYPE] || !params.values[AT_LENGTH])
        return 0;

    char dest_type = 0;
    if (*params.types[AT_TYPE] == 'c')
        dest_type = (*params.values[AT_TYPE])->c;
    else if (*params.types[AT_TYPE] == 's')
        dest_type = (*params.values[AT_TYPE])->s;
    else
        return 0;

    int dest_length = 0;
    if (*params.types[AT_LENGTH] == 'i')
        dest_length = (*params.values[AT_LENGTH])->i;
    else
        return 0;
    
    /* Add a flavourless mapping */
    mapper_mapping m = mapper_router_add_mapping(router, output,
                                                 dest_signal_name,
                                                 dest_type, dest_length);
    if (!m) {
        trace("couldn't create mapper_mapping in handler_signal_connectTo\n");
        return 0;
    }

    if (argc > 2) {
        /* Set its properties. */
        mapper_mapping_set_from_message(m, output, &params);
    }

    mapper_admin_send_connected(admin, router, m);

    return 0;
}

/*! Respond to /connected by storing connection in database. */
static int handler_signal_connected(const char *path, const char *types,
                                    lo_arg **argv, int argc, lo_message msg,
                                    void *user_data)
{
    mapper_admin admin = (mapper_admin) user_data;
    mapper_monitor mon = admin->monitor;
    mapper_db db = mapper_monitor_get_db(mon);
    char *src_signal_name, *dest_signal_name;

    if (argc < 2)
        return 0;

    if (types[0] != 's' && types[0] != 'S' && types[1] != 's'
        && types[1] != 'S')
        return 0;

    src_signal_name = &argv[0]->s;
    dest_signal_name = &argv[1]->s;

    trace("<%s> got /connected %s %s\n", mapper_admin_name(admin),
          src_signal_name, dest_signal_name);

    mapper_message_t params;
    if (mapper_msg_parse_params(&params, path, types+2, argc-2, argv+2)) {
        lo_message_pp(msg);
        return 0;
    }
    mapper_db_add_or_update_mapping_params(db, src_signal_name,
                                           dest_signal_name, &params);

    return 0;
}

/*! Modify the connection properties : mode, range, expression,
 *  clipMin, clipMax. */
static int handler_signal_connection_modify(const char *path, const char *types,
                                            lo_arg **argv, int argc, lo_message msg,
                                            void *user_data)
{

    mapper_admin admin = (mapper_admin) user_data;
    mapper_device md = admin->device;
    mapper_signal output;

    const char *src_name, *src_signal_name;

    if (argc < 4)
        return 0;

    if ((types[0] != 's' && types[0] != 'S')
        || (types[1] != 's' && types[1] != 'S') || (types[2] != 's'
                                                    && types[2] != 'S'))
        return 0;

    src_name = &argv[0]->s;
    if (osc_prefix_cmp(src_name, mapper_admin_name(admin),
                       &src_signal_name))
        return 0;
            
    if (!(output=mdev_get_output_by_name(md, src_signal_name, 0)))
    {
        trace("<%s> no output signal found for '%s' in /connectTo\n",
              mapper_admin_name(admin), src_signal_name);
        return 0;
    }

    mapper_router router =
        mapper_router_find_by_dest_name(md->routers, &argv[1]->s);
    if (!router)
    {
        trace("<%s> no router found for '%s' in /connectTo\n",
              mapper_admin_name(admin), &argv[1]->s);
    }

    mapper_mapping m = mapper_mapping_find_by_names(md, &argv[0]->s,
                                                    &argv[1]->s);
    if (!m)
        return 0;

    mapper_message_t params;
    if (mapper_msg_parse_params(&params, path, types+2, argc-2, &argv[2]))
    {
        trace("<%s> error parsing parameters in /connectTo, "
              "continuing anyway.\n", mapper_admin_name(admin));
    }
    
    mapper_mapping_set_from_message(m, output, &params);

    mapper_admin_send_connected(admin, router, m);

    return 0;
}

/*! Disconnect two signals. */
static int handler_signal_disconnect(const char *path, const char *types,
                                     lo_arg **argv, int argc,
                                     lo_message msg, void *user_data)
{

    mapper_admin admin = (mapper_admin) user_data;
    mapper_device md = admin->device;
    mapper_signal output;
        
    const char *src_name, *src_signal_name;
    
    if (argc < 2)
        return 0;
    
    if (types[0] != 's' && types[0] != 'S' && types[1] != 's'
        && types[1] != 'S')
        return 0;
    
    src_name = &argv[0]->s;
    if (osc_prefix_cmp(src_name, mapper_admin_name(admin),
                       &src_signal_name))
        return 0;
    
    if (!(output=mdev_get_output_by_name(md, src_signal_name, 0)))
    {
        trace("<%s> no output signal found for '%s' in /disconnect\n",
              mapper_admin_name(admin), src_signal_name);
        return 0;
    }
    
    mapper_router router = mapper_router_find_by_dest_name(md->routers, &argv[1]->s);
    
    mapper_mapping m = mapper_mapping_find_by_names(md, &argv[0]->s, &argv[1]->s);
    
    /*The mapping is removed */
    if (mapper_router_remove_mapping(router, m)) {
        return 0;
    }
    
    mapper_admin_send_osc(admin, "/disconnected", "ss", &argv[0]->s, &argv[1]->s);
        
    return 0;
}

/*! Respond to /disconnected by removing connection from database. */
static int handler_signal_disconnected(const char *path, const char *types,
                                       lo_arg **argv, int argc,
                                       lo_message msg, void *user_data)
{
    mapper_admin admin = (mapper_admin) user_data;
    mapper_monitor mon = admin->monitor;
    mapper_db db = mapper_monitor_get_db(mon);

    if (argc < 2)
        return 0;

    if (types[0] != 's' && types[0] != 'S' && types[1] != 's'
        && types[1] != 'S')
        return 0;

    const char *src_signal_name = &argv[0]->s;
    const char *dest_signal_name = &argv[1]->s;

    trace("<%s> got /disconnected %s %s\n", mapper_admin_name(admin),
          src_signal_name, dest_signal_name);

    mapper_db_remove_mapping(db,
        mapper_db_get_mapping_by_signal_full_names(db, src_signal_name,
                                                   dest_signal_name));

    return 0;
}

/*! Report existing connections to the network */
static int handler_device_connections_get(const char *path,
                                          const char *types,
                                          lo_arg **argv, int argc,
                                          lo_message msg, void *user_data)
{
    mapper_admin admin = (mapper_admin) user_data;
    mapper_device md = admin->device;
    mapper_router router = md->routers;

    trace("<%s> got /connections/get\n", mapper_admin_name(admin));


    while (router) {

        mapper_signal_mapping sm = router->mappings;
        mapper_signal sig;

        while (sm) {

			mapper_mapping m = sm->mapping;
			sig = sm->signal;

            while (m) {
                mapper_admin_send_connected(admin, router, m);
                m = m->next;

            }
            sm = sm->next;

        }
        router = router->next;

    }

    return 0;
}
