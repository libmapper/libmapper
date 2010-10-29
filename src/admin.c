
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

void mapper_db_create_mapping( const char* source_signal_path, 
		const char* dest_signal_path ) {

	lo_address a = lo_address_new_from_url( "osc.udp://224.0.1.3:7570" );
	lo_address_set_ttl( a, 1 );

	char source_device[1024];
	char dest_device[1024];

	const char* source_suffix = strchr( source_signal_path+1, '/' );
	int devnamelen = source_suffix-source_signal_path;
	strncpy( source_device, source_signal_path, devnamelen );
	source_device[devnamelen] = 0;

	const char* dest_suffix = strchr( dest_signal_path+1, '/' );
	devnamelen = dest_suffix-dest_signal_path;
	strncpy( dest_device, dest_signal_path, devnamelen );
	dest_device[devnamelen] = 0;

	trace( "add mapping %s %s %s %s\n", 
			source_device,
			source_signal_path,
			dest_device,
			dest_signal_path );

	lo_send( a, "/link", "ss", source_device, dest_device );
	lo_send( a, "/connect", "ss", source_signal_path, dest_signal_path );
	lo_address_free( a );

}

void mapper_db_destroy_mapping( const char* source_signal_path, 
		const char* dest_signal_path ) {

	lo_address a = lo_address_new_from_url( "osc.udp://224.0.1.3:7570" );
	lo_address_set_ttl( a, 1 );

	char source_device[1024];
	char dest_device[1024];

	const char* source_suffix = strchr( source_signal_path+1, '/' );
	int devnamelen = source_suffix-source_signal_path;
	strncpy( source_device, source_signal_path, devnamelen );
	source_device[devnamelen] = 0;

	const char* dest_suffix = strchr( dest_signal_path+1, '/' );
	devnamelen = dest_suffix-dest_signal_path;
	strncpy( dest_device, dest_signal_path, devnamelen );
	dest_device[devnamelen] = 0;

	trace( "remove mapping %s %s %s %s\n", 
			source_device,
			source_signal_path,
			dest_device,
			dest_signal_path );

	//lo_send( a, "/link", "ss", source_device, dest_device );
	lo_send( a, "/disconnect", "ss", source_signal_path, dest_signal_path );
	lo_address_free( a );

}

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
static int handler_device_alloc_port(const char *, const char *, lo_arg **,
                                     int, lo_message, void *);
static int handler_device_alloc_name(const char *, const char *, lo_arg **,
                                     int, lo_message, void *);
static int handler_device_link(const char *, const char *, lo_arg **, int,
                               lo_message, void *);
static int handler_device_link_to(const char *, const char *, lo_arg **,
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
static int handler_signal_connect_to(const char *, const char *, lo_arg **,
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
    {"/link_to",                "sssssiss", handler_device_link_to},
    {"/unlink",                 "ss",       handler_device_unlink},
    {"%s/connections/get",      "",         handler_device_connections_get},
    {"/connect",                NULL,       handler_signal_connect},
    {"/connect_to",             NULL,       handler_signal_connect_to},
    {"/connection/modify",      NULL,       handler_signal_connection_modify},
    {"/disconnect",             "ss",       handler_signal_disconnect},
};
const int N_DEVICE_HANDLERS =
    sizeof(device_handlers)/sizeof(device_handlers[0]);

static struct handler_method_assoc monitor_handlers[] = {
    {"/registered",             NULL,       handler_registered},
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
static void on_collision(mapper_admin_allocated_t *resource,
                         mapper_admin admin, int type);

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

    /* Resource allocation algorithm needs a seeded random number
     * generator. */
    srand(((unsigned int)(get_current_time()*1000000.0))%100000);

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
        mapper_admin_send_osc(admin, "/logout", "s",
                              mapper_admin_name(admin));
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
    /* Initialize data structures */
    if (dev)
    {
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
        admin->device = dev;

        /* Add methods for admin bus.  Only add methods needed for
         * allocation here. Further methods are added when the device is
         * registered. */
        lo_server_add_method(admin->admin_server, "/port/probe", NULL,
                             handler_device_alloc_port, admin);
        lo_server_add_method(admin->admin_server, "/name/probe", NULL,
                             handler_device_alloc_name, admin);
        lo_server_add_method(admin->admin_server, "/port/registered", NULL,
                             handler_device_alloc_port, admin);
        lo_server_add_method(admin->admin_server, "/name/registered", NULL,
                             handler_device_alloc_name, admin);

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
void mapper_admin_poll(mapper_admin admin)
{

    int count = 0;

    while (count < 10 && lo_server_recv_noblock(admin->admin_server, 0)) {
        count++;
    }

    if (!admin->device)
        return;

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
    if (!admin->registered
        && admin->port.locked && admin->ordinal.locked)
    {
        mapper_admin_add_device_methods(admin);

        /* Remove some handlers needed during allocation. */
        lo_server_del_method(admin->admin_server,
                             "/port/registered", NULL);
        lo_server_del_method(admin->admin_server,
                             "/name/registered", NULL);

        admin->registered = 1;
        trace("</%s.?::%p> registered as <%s>\n",
              admin->identifier, admin, mapper_admin_name(admin));
        mapper_admin_send_osc(admin, "/who", "");
    }
}

/*! Probe the admin bus to see if a device's proposed port is already
 *  taken.
 */
void mapper_admin_port_probe(mapper_admin admin)
{
    trace("</%s.?::%p> probing port\n", admin->identifier, admin);

    /* We don't use mapper_admin_send_osc() here because the name is
     * not yet established and it would trigger a warning. */
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
    trace("</%s.?::%p> probing name\n", admin->identifier, admin);
    snprintf(name, 256, "/%s.%d", admin->identifier, admin->ordinal.value);

    /* For the same reason, we can't use mapper_admin_send_osc()
     * here. */
    lo_send(admin->admin_addr, "/name/probe", "s", name);
}

/*! Announce on the admin bus a device's registered port. */
void mapper_admin_port_registered(mapper_admin admin)
{
    if (admin->port.locked)
        /* Name not yet registered, so we can't use
         * mapper_admin_send_osc() here. */
        lo_send(admin->admin_addr, "/port/registered",
                "i", admin->port.value);
}

/*! Announce on the admin bus a device's registered name.ordinal. */
void mapper_admin_name_registered(mapper_admin admin)
{
    if (admin->ordinal.locked)
        mapper_admin_send_osc(admin, "/name/registered",
                              "s", mapper_admin_name(admin));
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
        resource->value += rand() % (resource->collision_count + 1);

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

void mapper_admin_send_osc_with_params(mapper_admin admin,
                                       mapper_message_t *params,
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
    lo_message_add_varargs(m, types, aq);

    mapper_msg_prepare_params(m, params);

    lo_send_message(admin->admin_addr, namedpath, m);
    lo_message_free(m);
}

/**********************************/
/* Internal OSC message handlers. */
/**********************************/

/*! Respond to /who by announcing the current port. */
static int handler_who(const char *path, const char *types, lo_arg **argv,
                       int argc, lo_message msg, void *user_data)
{
    mapper_admin admin = (mapper_admin) user_data;

    mapper_admin_send_osc(
        admin, "/registered", "s", mapper_admin_name(admin),
        AT_IP, inet_ntoa(admin->interface_ip),
        AT_PORT, admin->port.value,
        AT_CANALIAS, 0,
        AT_NUMINPUTS, admin->device ? mdev_num_inputs(admin->device) : 0,
        AT_NUMOUTPUTS, admin->device ? mdev_num_outputs(admin->device) : 0,
        AT_HASH, 0);

    return 0;
}


/*! Register information about port and host for the device. */
static int handler_registered(const char *path, const char *types,
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

    if (argc < 1)
        return 0;

    if (types[0] != 's' && types[0] != 'S')
        return 0;

    const char *name = &argv[0]->s;

    trace("got /logout %s\n", name);

    mapper_db_remove_device(db, name);

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
            sig->props.maximum ? AT_MAX : -1, sig);
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
            sig->props.maximum ? AT_MAX : -1, sig);
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

    trace("</%s.?::%p> got /port/probe %d \n",
          admin->identifier, admin, probed_port);

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

    trace("</%s.?::%p> got /name/probe %s\n",
          admin->identifier, admin, probed_name);

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
            admin, "/link_to", "ss", src_name, dest_name,
            AT_IP, inet_ntoa(admin->interface_ip),
            AT_PORT, admin->port.value,
            AT_CANALIAS, 0);
    }
    return 0;
}

/*! Link two devices... continued. */
static int handler_device_link_to(const char *path, const char *types,
                                  lo_arg **argv, int argc, lo_message msg,
                                  void *user_data)
{
    mapper_admin admin = (mapper_admin) user_data;
    mapper_device md = admin->device;

    const char *src_name, *dest_name, *host=0, *canAlias=0;
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
        trace("<%s> ignoring /link_to %s %s\n",
              mapper_admin_name(admin), src_name, dest_name);
        return 0;
    }

    trace("<%s> got /link_to %s %s\n", mapper_admin_name(admin),
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
        trace("can't perform /link_to, host unknown\n");
        return 0;
    }

    if (mapper_msg_get_param_if_int(&params, AT_PORT, &port)) {
        trace("can't perform /link_to, port unknown\n");
        return 0;
    }

    canAlias = mapper_msg_get_param_if_string(&params, AT_CANALIAS);

    // Creation of a new router added to the source.
    router = mapper_router_new(md, host, port, dest_name);
    mdev_add_router(md, router);

    // Announce the result.
    mapper_admin_send_osc(admin, "/linked", "ss",
                          mapper_admin_name(admin), dest_name);

    trace("new router to %s -> host: %s, port: %d, canAlias: %s\n",
          dest_name, host, port, canAlias ? canAlias : "no");

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
 *  send a connect_to message to the source device. */
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

    if (mdev_find_input_by_name(md, dest_signal_name, &input) < 0)
    {
        trace("<%s> no input signal found for '%s' in /connect_to\n",
              mapper_admin_name(admin), dest_signal_name);
        return 0;
    }

    if (argc <= 2) {
        // use some default arguments related to the signal
        mapper_admin_send_osc(
            admin, "/connect_to", "ss", src_name, dest_name,
            AT_TYPE, input->props.type,
            input->props.minimum ? AT_MIN : -1, input,
            input->props.maximum ? AT_MAX : -1, input);
    } else {
        // add the remaining arguments from /connect
        mapper_message_t params;
        if (mapper_msg_parse_params(&params, path, &types[2],
                                    argc-2, &argv[2]))
        {
            trace("<%s> error parsing message parameters in /connect.\n",
                  mapper_admin_name(admin));
            return 0;
        }
        mapper_admin_send_osc_with_params(
            admin, &params, "/connect_to", "ss", src_name, dest_name);
    }

    return 0;
}

/*! Connect two signals. */
static int handler_signal_connect_to(const char *path, const char *types,
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
        trace("<%s> destination '%s' has no parameter in /connect_to.\n",
              mapper_admin_name(admin), dest_name);
        return 0;
    }

    trace("<%s> got /connect_to %s %s + %d arguments\n",
          mapper_admin_name(admin), src_name, dest_name, argc);

    if (mdev_find_output_by_name(md, src_signal_name, &output) < 0)
    {
        trace("<%s> no output signal found for '%s' in /connect_to\n",
              mapper_admin_name(admin), src_signal_name);
        return 0;
    }

    mapper_message_t params;
    if (mapper_msg_parse_params(&params, path, types+2, argc-2, &argv[2]))
    {
        trace("<%s> error parsing parameters in /connect_to, "
              "continuing anyway.\n", mapper_admin_name(admin));
    }

    mapper_router router =
        mapper_router_find_by_dest_name(md->routers, dest_name);

    if (!router) {
        trace("<%s> not linked to '%s' on /connect_to.\n",
              mapper_admin_name(admin), dest_name);
        // TODO: Perform /link_to?

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
             * also cache /connect_to message for completion after
             * link??? */
        }

        return 0;
    }
    
    /* Add a flavourless mapping */
    mapper_mapping m = mapper_router_add_mapping(router, output,
                                                 dest_signal_name);

    if (argc > 2) {
        /* Set its properties. */
        mapper_mapping_set_from_message(m, output, &params);
    }
    
    // Send /connected message
    lo_message mess = lo_message_new();
    if (!mess) {
        trace("couldn't allocate lo_message\n");
        return 0;
    }
    
    lo_message_add_string(mess, src_name);
    lo_message_add_string(mess, dest_name);
    
    mapper_mapping_prepare_osc_message(mess, m);
    
    lo_send_message(admin->admin_addr, "/connected", mess);
    lo_message_free(mess);

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

/*! Modify the connection properties : scaling, range, expression,
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
            
    if (mdev_find_output_by_name(md, src_signal_name, &output) < 0)
    {
        trace("<%s> no output signal found for '%s' in /connect_to\n",
              mapper_admin_name(admin), src_signal_name);
        return 0;
    }
    
    mapper_mapping m = mapper_mapping_find_by_names(md, &argv[0]->s, &argv[1]->s);
    
    mapper_message_t params;
    if (mapper_msg_parse_params(&params, path, types+2, argc-2, &argv[2]))
    {
        trace("<%s> error parsing parameters in /connect_to, "
              "continuing anyway.\n", mapper_admin_name(admin));
    }
    
    mapper_mapping_set_from_message(m, output, &params);

    lo_message mess = lo_message_new();
    if (!mess) {
        trace("couldn't allocate lo_message\n");
        return 0;
    }
    
    lo_message_add_string(mess, src_name);
    lo_message_add_string(mess, &argv[1]->s);
    
    mapper_mapping_prepare_osc_message(mess, m);
    
    lo_send_message(admin->admin_addr, "/connected", mess);
    lo_message_free(mess);

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
    
    if (mdev_find_output_by_name(md, src_signal_name, &output) < 0)
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
    char src_name[1024], dest_name[1024];
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

				msig_full_name(sig, src_name, 1024);
                snprintf(dest_name, 1024, "%s%s",
                         router->dest_name, m->props.dest_name);

                lo_message mess = lo_message_new();
                if (!mess) {
                    trace("couldn't allocate lo_message\n");
                    return 0;
                }

                lo_message_add_string(mess, src_name);
                lo_message_add_string(mess, dest_name);

                mapper_mapping_prepare_osc_message(mess, m);

                lo_send_message(admin->admin_addr, "/connected", mess);
                lo_message_free(mess);
                m = m->next;

            }
            sm = sm->next;

        }
        router = router->next;

    }

    return 0;
}
