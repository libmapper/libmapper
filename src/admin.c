
#include "config.h"

#include <lo/lo.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <sys/time.h>
#include <zlib.h>
#include <math.h>

#ifdef HAVE_GETIFADDRS
 #include <ifaddrs.h>
 #include <net/if.h>
#endif

#ifdef HAVE_ARPA_INET_H
 #include <arpa/inet.h>
#else
 #ifdef HAVE_WINSOCK2_H
  #include <winsock2.h>
  #include <ws2tcpip.h>
  #include <iphlpapi.h>
 #endif
#endif

#include "mapper_internal.h"
#include "types_internal.h"
#include "config.h"
#include <mapper/mapper.h>

// set to 1 to force mesh comms to use admin bus instead for debugging
#define FORCE_ADMIN_TO_BUS      1

#define BUNDLE_DEST_SUBSCRIBERS (void*)-1
#define BUNDLE_DEST_BUS         0

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

/* Note: any call to liblo where get_liblo_error will be called
 * afterwards must lock this mutex, otherwise there is a race
 * condition on receiving this information.  Could be fixed by the
 * liblo error handler having a user context pointer. */
static int liblo_error_num = 0;
static void liblo_error_handler(int num, const char *msg, const char *path)
{
    liblo_error_num = num;
    if (num == LO_NOPORT) {
        trace("liblo could not start a server because port unavailable\n");
    } else
        fprintf(stderr, "[libmapper] liblo server error %d in path %s: %s\n",
                num, path, msg);
}

const char* admin_msg_strings[] =
{
    "/combine",                 /* ADM_COMBINE */
    "/combined",                /* ADM_COMBINED */
    "/connect",                 /* ADM_CONNECT */
    "/connectTo",               /* ADM_CONNECT_TO */
    "/connected",               /* ADM_CONNECTED */
    "/connection/modify",       /* ADM_CONNECTION_MODIFY */
    "/device",                  /* ADM_DEVICE */
    "/disconnect",              /* ADM_DISCONNECT */
    "/disconnected",            /* ADM_DISCONNECTED */
    "/link",                    /* ADM_LINK */
    "/link/modify",             /* ADM_LINK_MODIFY */
    "/linkTo",                  /* ADM_LINK_TO */
    "/linked",                  /* ADM_LINKED */
    "/link/ping",               /* ADM_LINK_PING */
    "/logout",                  /* ADM_LOGOUT */
    "/signal",                  /* ADM_SIGNAL */
    "/signal/removed",          /* ADM_SIGNAL_REMOVED */
    "%s/subscribe",             /* ADM_SUBSCRIBE */
    "%s/unsubscribe",           /* ADM_UNSUBSCRIBE */
    "/sync",                    /* ADM_SYNC */
    "/unlink",                  /* ADM_UNLINK */
    "/unlinked",                /* ADM_UNLINKED */
    "/who",                     /* ADM_WHO */
};

/* Internal functions for sending admin messages. */
static void mapper_admin_send_device(mapper_admin admin, mapper_device device);
static void mapper_admin_send_linked(mapper_admin admin, mapper_device device,
                                     mapper_link link);
static void mapper_admin_send_connected(mapper_admin admin, mapper_device md,
                                        mapper_connection c, int index);

/* Internal message handler prototypes. */
static int handler_who(const char *, const char *, lo_arg **, int,
                       lo_message, void *);
static int handler_device(const char *, const char *, lo_arg **,
                          int, lo_message, void *);
static int handler_logout(const char *, const char *, lo_arg **,
                          int, lo_message, void *);
static int handler_device_subscribe(const char *, const char *, lo_arg **,
                                    int, lo_message, void *);
static int handler_device_unsubscribe(const char *, const char *, lo_arg **,
                                      int, lo_message, void *);
static int handler_signal_info(const char *, const char *, lo_arg **,
                               int, lo_message, void *);
static int handler_signal_removed(const char *, const char *, lo_arg **,
                                  int, lo_message, void *);
static int handler_device_name_probe(const char *, const char *, lo_arg **,
                                     int, lo_message, void *);
static int handler_device_name_registered(const char *, const char *, lo_arg **,
                                          int, lo_message, void *);
static int handler_device_link(const char *, const char *, lo_arg **, int,
                               lo_message, void *);
static int handler_device_linkTo(const char *, const char *, lo_arg **,
                                 int, lo_message, void *);
static int handler_device_linked(const char *, const char *, lo_arg **,
                                 int, lo_message, void *);
static int handler_device_link_modify(const char *, const char *, lo_arg **, int,
                                      lo_message, void *);
static int handler_device_link_ping(const char *, const char *, lo_arg **, int,
                                    lo_message, void *);
static int handler_device_unlink(const char *, const char *, lo_arg **,
                                 int, lo_message, void *);
static int handler_device_unlinked(const char *, const char *, lo_arg **,
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
static int handler_sync(const char *, const char *, lo_arg **,
                        int, lo_message, void *);
static int handler_signal_combine(const char *, const char *, lo_arg **,
                                  int, lo_message, void *);
static int handler_signal_combined(const char *, const char *, lo_arg **,
                                   int, lo_message, void *);

/* Handler <-> Message relationships */
struct handler_method_assoc {
    int str_index;
    char *types;
    lo_method_handler h;
};

// handlers needed by both "devices" and "monitors"
static struct handler_method_assoc admin_bus_handlers[] = {
    {ADM_LOGOUT,                 NULL,      handler_logout},
};
const int N_ADMIN_BUS_HANDLERS =
    sizeof(admin_bus_handlers)/sizeof(admin_bus_handlers[0]);

static struct handler_method_assoc admin_mesh_handlers[] = {
    {ADM_CONNECTED,              NULL,      handler_signal_connected},
};
const int N_ADMIN_MESH_HANDLERS =
    sizeof(admin_mesh_handlers)/sizeof(admin_mesh_handlers[0]);

static struct handler_method_assoc device_bus_handlers[] = {
    {ADM_COMBINE,               NULL,       handler_signal_combine},
    {ADM_CONNECT,               NULL,       handler_signal_connect},
    {ADM_CONNECT_TO,            NULL,       handler_signal_connectTo},
    {ADM_CONNECTED,             NULL,       handler_signal_connected},
    {ADM_CONNECTION_MODIFY,     NULL,       handler_signal_connection_modify},
    {ADM_DISCONNECT,            NULL,       handler_signal_disconnect},
    {ADM_LINK,                  NULL,       handler_device_link},
    {ADM_LINK_TO,               NULL,       handler_device_linkTo},
    {ADM_LINK_MODIFY,           NULL,       handler_device_link_modify},
    {ADM_UNLINK,                NULL,       handler_device_unlink},
    {ADM_SUBSCRIBE,             NULL,       handler_device_subscribe},
    {ADM_UNSUBSCRIBE,           NULL,       handler_device_unsubscribe},
    {ADM_WHO,                   NULL,       handler_who},
};
const int N_DEVICE_BUS_HANDLERS =
    sizeof(device_bus_handlers)/sizeof(device_bus_handlers[0]);

static struct handler_method_assoc device_mesh_handlers[] = {
    {ADM_CONNECT_TO,            NULL,       handler_signal_connectTo},
    {ADM_LINK_PING,             "iiid",     handler_device_link_ping},
    {ADM_SUBSCRIBE,             NULL,       handler_device_subscribe},
    {ADM_UNSUBSCRIBE,           NULL,       handler_device_unsubscribe},
};

const int N_DEVICE_MESH_HANDLERS =
    sizeof(device_mesh_handlers)/sizeof(device_mesh_handlers[0]);

static struct handler_method_assoc monitor_bus_handlers[] = {
    {ADM_DEVICE,                NULL,       handler_device},
    {ADM_DISCONNECTED,          NULL,       handler_signal_disconnected},
    {ADM_SYNC,                  NULL,       handler_sync},
    {ADM_UNLINKED,              NULL,       handler_device_unlinked},
};
const int N_MONITOR_BUS_HANDLERS =
    sizeof(monitor_bus_handlers)/sizeof(monitor_bus_handlers[0]);

static struct handler_method_assoc monitor_mesh_handlers[] = {
    {ADM_COMBINED,              NULL,       handler_signal_combined},
    {ADM_DEVICE,                NULL,       handler_device},
    {ADM_DISCONNECTED,          NULL,       handler_signal_disconnected},
    {ADM_LINKED,                NULL,       handler_device_linked},
    {ADM_SIGNAL,                NULL,       handler_signal_info},
    {ADM_SIGNAL_REMOVED,        "s",        handler_signal_removed},
    {ADM_UNLINKED,              NULL,       handler_device_unlinked},
};
const int N_MONITOR_MESH_HANDLERS =
    sizeof(monitor_mesh_handlers)/sizeof(monitor_mesh_handlers[0]);

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
    struct in_addr zero;
    struct sockaddr_in *sa;

    *(unsigned int *)&zero = inet_addr("0.0.0.0");

#ifdef HAVE_GETIFADDRS

    struct ifaddrs *ifaphead;
    struct ifaddrs *ifap;
    struct ifaddrs *iflo=0, *ifchosen=0;

    if (getifaddrs(&ifaphead) != 0)
        return 1;

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

#else // !HAVE_GETIFADDRS

#ifdef HAVE_LIBIPHLPAPI
    // TODO consider "pref" as well

    /* Start with recommended 15k buffer for GetAdaptersAddresses. */
    ULONG size = 15*1024/2;
    int tries = 3;
    PIP_ADAPTER_ADDRESSES paa = malloc(size*2);
    DWORD rc = ERROR_SUCCESS-1;
    while (rc!=ERROR_SUCCESS && paa && tries-- > 0) {
        size *= 2;
        paa = realloc(paa, size);
        rc = GetAdaptersAddresses(AF_INET, 0, 0, paa, &size);
    }
    if (rc!=ERROR_SUCCESS)
        return 2;

    PIP_ADAPTER_ADDRESSES loaa=0, aa = paa;
    PIP_ADAPTER_UNICAST_ADDRESS lopua=0;
    while (aa && rc==ERROR_SUCCESS) {
        PIP_ADAPTER_UNICAST_ADDRESS pua = aa->FirstUnicastAddress;
        // Skip adapters that are not "Up".
        if (pua && aa->OperStatus == IfOperStatusUp) {
            if (aa->IfType == IF_TYPE_SOFTWARE_LOOPBACK) {
                loaa = aa;
                lopua = pua;
            }
            else {
                // Skip addresses starting with 0.X.X.X or 169.X.X.X.
                sa = (struct sockaddr_in *) pua->Address.lpSockaddr;
                unsigned char prefix = sa->sin_addr.s_addr&0xFF;
                if (prefix!=0xA9 && prefix!=0) {
                    if (*iface) free(*iface);
                    *iface = strdup(aa->AdapterName);
                    *addr = sa->sin_addr;
                    free(paa);
                    return 0;
                }
            }
        }
        aa = aa->Next;
    }

    if (loaa && lopua) {
        if (*iface) free(*iface);
        *iface = strdup(loaa->AdapterName);
        sa = (struct sockaddr_in *) lopua->Address.lpSockaddr;
        *addr = sa->sin_addr;
        free(paa);
        return 0;
    }

    if (paa) free(paa);

#else

  #error No known method on this system to get the network interface address.

#endif // HAVE_LIBIPHLPAPI
#endif // !HAVE_GETIFADDRS

    return 2;
}

/*! A helper function to seed the random number generator. */
static void seed_srand()
{
    unsigned int s;

#ifndef WIN32
    FILE *f = fopen("/dev/random", "rb");
    if (f) {
        if (fread(&s, 4, 1, f)==1) {
            srand(s);
            fclose(f);
            return;
        }
        fclose(f);
    }
#endif

    double d = get_current_time();
    s = (unsigned int)((d-(unsigned long)d)*100000);
    srand(s);
}

static void mapper_admin_add_admin_methods(mapper_admin admin)
{
    int i;
    for (i=0; i < N_ADMIN_BUS_HANDLERS; i++)
    {
        lo_server_add_method(admin->bus_server,
                             admin_msg_strings[admin_bus_handlers[i].str_index],
                             admin_bus_handlers[i].types,
                             admin_bus_handlers[i].h,
                             admin);
    }
    for (i=0; i < N_ADMIN_MESH_HANDLERS; i++)
    {
        lo_server_add_method(admin->mesh_server,
                             admin_msg_strings[admin_mesh_handlers[i].str_index],
                             admin_mesh_handlers[i].types,
                             admin_mesh_handlers[i].h,
                             admin);
        // add them for bus also
        lo_server_add_method(admin->bus_server,
                             admin_msg_strings[admin_mesh_handlers[i].str_index],
                             admin_mesh_handlers[i].types,
                             admin_mesh_handlers[i].h,
                             admin);
    }
}

static void mapper_admin_add_device_methods(mapper_admin admin,
                                            mapper_device device)
{
    int i;
    char fullpath[256];
    for (i=0; i < N_DEVICE_BUS_HANDLERS; i++)
    {
        snprintf(fullpath, 256,
                 admin_msg_strings[device_bus_handlers[i].str_index],
                 mdev_name(admin->device));
        lo_server_add_method(admin->bus_server, fullpath,
                             device_bus_handlers[i].types,
                             device_bus_handlers[i].h,
                             admin);
    }
    for (i=0; i < N_DEVICE_MESH_HANDLERS; i++)
    {
        snprintf(fullpath, 256,
                 admin_msg_strings[device_mesh_handlers[i].str_index],
                 mdev_name(admin->device));
        lo_server_add_method(admin->mesh_server, fullpath,
                             device_mesh_handlers[i].types,
                             device_mesh_handlers[i].h,
                             admin);
        // add them for bus also
        lo_server_add_method(admin->bus_server, fullpath,
                             device_mesh_handlers[i].types,
                             device_mesh_handlers[i].h,
                             admin);
    }
}

static void mapper_admin_add_monitor_methods(mapper_admin admin)
{
    int i;
    for (i=0; i < N_MONITOR_BUS_HANDLERS; i++)
    {
        lo_server_add_method(admin->bus_server,
                             admin_msg_strings[monitor_bus_handlers[i].str_index],
                             monitor_bus_handlers[i].types,
                             monitor_bus_handlers[i].h,
                             admin);
    }
    for (i=0; i < N_MONITOR_MESH_HANDLERS; i++)
    {
        lo_server_add_method(admin->mesh_server,
                             admin_msg_strings[monitor_mesh_handlers[i].str_index],
                             monitor_mesh_handlers[i].types,
                             monitor_mesh_handlers[i].h,
                             admin);
        // add them for bus also
        lo_server_add_method(admin->bus_server,
                             admin_msg_strings[monitor_mesh_handlers[i].str_index],
                             monitor_mesh_handlers[i].types,
                             monitor_mesh_handlers[i].h,
                             admin);
    }
}

static void mapper_admin_remove_monitor_methods(mapper_admin admin)
{
    int i;
    for (i=0; i < N_MONITOR_BUS_HANDLERS; i++)
    {
        lo_server_del_method(admin->bus_server,
                             admin_msg_strings[monitor_bus_handlers[i].str_index],
                             monitor_bus_handlers[i].types);
    }
    for (i=0; i < N_MONITOR_MESH_HANDLERS; i++)
    {
        lo_server_del_method(admin->mesh_server,
                             admin_msg_strings[monitor_mesh_handlers[i].str_index],
                             monitor_mesh_handlers[i].types);
        lo_server_del_method(admin->bus_server,
                             admin_msg_strings[monitor_mesh_handlers[i].str_index],
                             monitor_mesh_handlers[i].types);
    }
}

mapper_admin mapper_admin_new(const char *iface, const char *group, int port)
{
    mapper_admin admin = (mapper_admin)calloc(1, sizeof(mapper_admin_t));
    if (!admin)
        return NULL;

    admin->interface_name = 0;

    /* Default standard ip and port is group 224.0.1.3, port 7570 */
    char port_str[10], *s_port = port_str;
    if (!group) group = "224.0.1.3";
    if (port==0)
        s_port = "7570";
    else
        snprintf(port_str, 10, "%d", port);

    /* Initialize interface information. */
    if (get_interface_addr(iface, &admin->interface_ip,
                           &admin->interface_name))
        trace("no interface found\n");

    /* Open address */
    admin->bus_addr = lo_address_new(group, s_port);
    if (!admin->bus_addr) {
        free(admin);
        return NULL;
    }

    /* Set TTL for packet to 1 -> local subnet */
    lo_address_set_ttl(admin->bus_addr, 1);

    /* Specify the interface to use for multicasting */
#ifdef HAVE_LIBLO_SET_IFACE
    lo_address_set_iface(admin->bus_addr,
                         admin->interface_name, 0);
#endif

    /* Open server for multicast group 224.0.1.3, port 7570 */
    admin->bus_server =
#ifdef HAVE_LIBLO_SERVER_IFACE
        lo_server_new_multicast_iface(group, s_port,
                                      admin->interface_name, 0,
                                      handler_error);
#else
        lo_server_new_multicast(group, s_port, handler_error);
#endif

    if (!admin->bus_server) {
        lo_address_free(admin->bus_addr);
        free(admin);
        return NULL;
    }

    // Also open address/server for mesh-style admin communications
    // TODO: use TCP instead?
    while (!(admin->mesh_server = lo_server_new(0, liblo_error_handler))) {}

    // Disable liblo message queueing.
    lo_server_enable_queue(admin->bus_server, 0, 1);
    lo_server_enable_queue(admin->mesh_server, 0, 1);

    // Add methods required by both devices and monitors
    mapper_admin_add_admin_methods(admin);

    return admin;
}

const char *mapper_admin_libversion(mapper_admin admin)
{
    return PACKAGE_VERSION;
}

void mapper_admin_send_bundle(mapper_admin admin)
{
    if (!admin->bundle)
        return;
#if FORCE_ADMIN_TO_BUS
    lo_send_bundle_from(admin->bus_addr, admin->mesh_server, admin->bundle);
#else
    if (admin->bundle_dest == BUNDLE_DEST_SUBSCRIBERS) {
        mapper_admin_subscriber *s = &admin->subscribers;
        if (*s) {
            mapper_clock_now(&admin->clock, &admin->clock.now);
        }
        while (*s) {
            if ((*s)->lease_expiration_sec < admin->clock.now.sec || !(*s)->flags) {
                // subscription expired, remove from subscriber list
                mapper_admin_subscriber temp = *s;
                *s = temp->next;
                if (temp->address)
                    lo_address_free(temp->address);
                free(temp);
                continue;
            }
            if ((*s)->flags & admin->message_type) {
                lo_send_bundle_from((*s)->address, admin->mesh_server, admin->bundle);
            }
            s = &(*s)->next;
        }
    }
    else if (admin->bundle_dest == BUNDLE_DEST_BUS) {
        lo_send_bundle_from(admin->bus_addr, admin->mesh_server, admin->bundle);
    }
    else {
        lo_send_bundle_from(admin->bundle_dest, admin->mesh_server, admin->bundle);
    }
#endif
    lo_bundle_free_messages(admin->bundle);
    admin->bundle = 0;
}

static int mapper_admin_init_bundle(mapper_admin admin)
{
    if (admin->bundle)
        mapper_admin_send_bundle(admin);

    mapper_clock_now(&admin->clock, &admin->clock.now);
    admin->bundle = lo_bundle_new(admin->clock.now);
    if (!admin->bundle) {
        trace("couldn't allocate lo_bundle\n");
        return 1;
    }

    return 0;
}

void mapper_admin_set_bundle_dest_bus(mapper_admin admin)
{
    if (admin->bundle && admin->bundle_dest != BUNDLE_DEST_BUS)
        mapper_admin_send_bundle(admin);
    admin->bundle_dest = BUNDLE_DEST_BUS;
    if (!admin->bundle)
        mapper_admin_init_bundle(admin);
}

void mapper_admin_set_bundle_dest_mesh(mapper_admin admin, lo_address address)
{
    if (admin->bundle && admin->bundle_dest != address)
        mapper_admin_send_bundle(admin);
    admin->bundle_dest = address;
    if (!admin->bundle)
        mapper_admin_init_bundle(admin);
}

void mapper_admin_set_bundle_dest_subscribers(mapper_admin admin, int type)
{
    if ((admin->bundle && admin->bundle_dest != BUNDLE_DEST_SUBSCRIBERS) ||
        (admin->message_type != type))
        mapper_admin_send_bundle(admin);
    admin->bundle_dest = BUNDLE_DEST_SUBSCRIBERS;
    admin->message_type = type;
    if (!admin->bundle)
        mapper_admin_init_bundle(admin);
}

/*! Free the memory allocated by a mapper admin structure.
 *  \param admin An admin structure handle.
 */
void mapper_admin_free(mapper_admin admin)
{
    if (!admin)
        return;

    // send out any cached messages
    mapper_admin_send_bundle(admin);

    if (admin->interface_name)
        free(admin->interface_name);

    if (admin->bus_server)
        lo_server_free(admin->bus_server);

    if (admin->mesh_server)
        lo_server_free(admin->mesh_server);

    if (admin->bus_addr)
        lo_address_free(admin->bus_addr);

    mapper_admin_subscriber s;
    while (admin->subscribers) {
        s = admin->subscribers;
        if (s->address)
            lo_address_free(s->address);
        admin->subscribers = s->next;
        free(s);
    }

    free(admin);
}

/*! Probe the admin bus to see if a device's proposed name.ordinal is
 *  already taken.
 */
static void mapper_admin_probe_device_name(mapper_admin admin,
                                           mapper_device device)
{
    device->ordinal.collision_count = -1;
    device->ordinal.count_time = get_current_time();

    /* Note: mdev_name() would refuse here since the
     * ordinal is not yet locked, so we have to build it manually at
     * this point. */
    char name[256];
    trace("</%s.?::%p> probing name\n", device->props.identifier, admin);
    snprintf(name, 256, "/%s.%d", device->props.identifier, device->ordinal.value);

    /* Calculate a hash from the name and store it in id.value */
    device->props.name_hash = crc32(0L, (const Bytef *)name, strlen(name));

    /* For the same reason, we can't use mapper_admin_send()
     * here. */
    lo_send(admin->bus_addr, "/name/probe", "si",
            name, admin->random_id);
}

/*! Add an uninitialized device to this admin. */
void mapper_admin_add_device(mapper_admin admin, mapper_device dev)
{
    /* Initialize data structures */
    if (dev)
    {
        admin->device = dev;
        mapper_clock_init(&admin->clock);

        /* Seed the random number generator. */
        seed_srand();

        /* Choose a random ID for allocation speedup */
        admin->random_id = rand();

        /* Add methods for admin bus.  Only add methods needed for
         * allocation here. Further methods are added when the device is
         * registered. */
        lo_server_add_method(admin->bus_server, "/name/probe", NULL,
                             handler_device_name_probe, admin);
        lo_server_add_method(admin->bus_server, "/name/registered", NULL,
                             handler_device_name_registered, admin);

        /* Probe potential name to admin bus. */
        mapper_admin_probe_device_name(admin, dev);
    }
}

/*! Add an uninitialized monitor to this admin. */
void mapper_admin_add_monitor(mapper_admin admin, mapper_monitor mon)
{
    /* Initialize monitor methods. */
    if (mon) {
        admin->monitor = mon;
        mapper_admin_add_monitor_methods(admin);
    }
}

void mapper_admin_remove_monitor(mapper_admin admin, mapper_monitor mon)
{
    if (mon) {
        admin->monitor = 0;
        mapper_admin_remove_monitor_methods(admin);
    }
}

static void mapper_admin_maybe_send_ping(mapper_admin admin, int force)
{
    mapper_device md = admin->device;
    int go = 0;

    mapper_clock_t *clock = &admin->clock;
    mapper_clock_now(clock, &clock->now);
    if (force || (clock->now.sec >= clock->next_ping)) {
        go = 1;
        clock->next_ping = clock->now.sec + 5 + (rand() % 4);
    }

    if (!md || !go)
        return;

    mapper_admin_set_bundle_dest_bus(admin);
    mapper_admin_bundle_message(admin, ADM_SYNC, 0, "si", mdev_name(md),
                                md->props.version);

    int elapsed;
    // some housekeeping: periodically check if our links are still active
    mapper_link link = md->router->links;
    while (link) {
        if (link->props.remote_name_hash == md->props.name_hash) {
            // don't bother sending pings to self
            link = link->next;
            continue;
        }
        mapper_sync_clock sync = &link->clock;
        elapsed = (sync->response.timetag.sec
                   ? clock->now.sec - sync->response.timetag.sec : 0);
        if (md->link_timeout_sec && elapsed > md->link_timeout_sec) {
            if (sync->response.message_id > 0) {
                trace("<%s> Lost contact with linked device %s "
                      "(%d seconds since sync).\n", mdev_name(md),
                      link->props.remote_name, elapsed);
                // tentatively mark link as expired
                sync->response.message_id = -1;
                sync->response.timetag.sec = clock->now.sec;
            }
            else {
                trace("<%s> Removing link to unresponsive device %s "
                      "(%d seconds since warning).\n", mdev_name(md),
                      link->props.remote_name, elapsed);
                // Call the local link handler if it exists
                if (md->link_cb)
                    md->link_cb(md, &link->props, MDEV_LOCAL_DESTROYED,
                                md->link_cb_userdata);

                // inform subscribers of link timeout
                mapper_admin_set_bundle_dest_subscribers(admin, SUB_DEVICE_LINKS);
                mapper_admin_bundle_message(admin, ADM_UNLINKED, 0, "ssss",
                                            mdev_name(md),
                                            link->props.remote_name,
                                            "@status", "timeout");

                // remove related data structures
                mapper_router_remove_link(md->router, link);
            }
        }
        else {
            lo_bundle b = lo_bundle_new(clock->now);
            lo_message m = lo_message_new();
            lo_message_add_int32(m, mdev_id(md));
            ++sync->sent.message_id;
            if (sync->sent.message_id < 0)
                sync->sent.message_id = 0;
            lo_message_add_int32(m, sync->sent.message_id);
            lo_message_add_int32(m, sync->response.message_id);
            if (sync->response.timetag.sec)
                lo_message_add_double(m, mapper_timetag_difference(clock->now,
                                                                   sync->response.timetag));
            else
                lo_message_add_double(m, 0.);
            // need to send immediately
            lo_bundle_add_message(b, admin_msg_strings[ADM_LINK_PING], m);
#if FORCE_ADMIN_TO_BUS
            lo_send_bundle_from(admin->bus_addr, admin->mesh_server, b);
#else
            lo_send_bundle_from(link->admin_addr, admin->mesh_server, b);
#endif
            mapper_timetag_cpy(&sync->sent.timetag, lo_bundle_get_timestamp(b));
            lo_bundle_free_messages(b);
        }
        link = link->next;
    }
}

/*! This is the main function to be called once in a while from a
 *  program so that the admin bus can be automatically managed.
 */
int mapper_admin_poll(mapper_admin admin)
{
    int count = 0, status;
    mapper_device md = admin->device;

    // send out any cached messages
    mapper_admin_send_bundle(admin);

    if (md)
        md->flags &= ~FLAGS_SENT_ALL_DEVICE_MESSAGES;

    while (count < 10 && (lo_server_recv_noblock(admin->bus_server, 0)
           + lo_server_recv_noblock(admin->mesh_server, 0))) {
        count++;
    }
    admin->msgs_recvd += count;

    if (!md) {
        mapper_admin_maybe_send_ping(admin, 0);
        return count;
    }

    /* If the ordinal is not yet locked, process collision timing.
     * Once the ordinal is locked it won't change. */
    if (!md->registered) {
        status = check_collisions(admin, &md->ordinal);
        if (status == 1) {
            /* If the ordinal has changed, re-probe the new name. */
            mapper_admin_probe_device_name(admin, md);
        }

        /* If we are ready to register the device, add the needed message
         * handlers. */
        if (md->ordinal.locked)
        {
            mdev_registered(md);

            /* Send registered msg. */
            lo_send(admin->bus_addr, "/name/registered",
                    "s", mdev_name(md));

            mapper_admin_add_device_methods(admin, md);
            mapper_admin_maybe_send_ping(admin, 1);

            trace("</%s.?::%p> registered as <%s>\n",
                  md->props.identifier, admin, mdev_name(md));
            md->flags |= FLAGS_DEVICE_ATTRIBS_CHANGED;
        }
    }
    else {
        // Send out clock sync messages occasionally
        mapper_admin_maybe_send_ping(admin, 0);
    }
    return count;
}

/*! Algorithm for checking collisions and allocating resources. */
static int check_collisions(mapper_admin admin,
                            mapper_admin_allocated_t *resource)
{
    double timediff;

    if (resource->locked)
        return 0;

    timediff = get_current_time() - resource->count_time;

    if (!admin->msgs_recvd) {
        if (timediff >= 5.0) {
            // reprobe with the same value
            return 1;
        }
        return 0;
    }
    else if (timediff >= 2.0 && resource->collision_count <= 1) {
        resource->locked = 1;
        if (resource->on_lock)
            resource->on_lock(admin->device, resource);
        return 2;
    }
    else if (timediff >= 0.5 && resource->collision_count > 0) {
        /* If resource collisions were found within 500 milliseconds of the
         * last probe, add a random number based on the number of
         * collisions. */
        resource->value += rand() % (resource->collision_count + 1);

        /* Prepare for causing new resource collisions. */
        resource->collision_count = -1;
        resource->count_time = get_current_time();

        /* Indicate that we need to re-probe the new value. */
        return 1;
    }

    return 0;
}

void _real_mapper_admin_bundle_message(mapper_admin admin,
                                       int msg_index,
                                       const char *path,
                                       const char *types, ...)
{
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
        case 'f':
        case 'd': lo_message_add(m, t, va_arg(aq, double)); break;
        default:
            die_unless(0, "message %s, unknown type '%c'\n",
                       path, t[0]);
        }
        types++;
    }

    mapper_msg_prepare_varargs(m, aq);

    va_end(aq);

    lo_bundle_add_message(admin->bundle, msg_index == -1 ?
                          path : admin_msg_strings[msg_index], m);

    /* Since liblo doesn't cache path strings, we must send the bundle
     * immediately if the path is non-standard. */
    if (msg_index == -1)
        mapper_admin_send_bundle(admin);
}

void _real_mapper_admin_bundle_message_with_params(mapper_admin admin,
                                                   mapper_message_t *params,
                                                   mapper_string_table_t *extra,
                                                   int msg_index,
                                                   const char *path,
                                                   const char *types, ...)
{
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

    mapper_msg_prepare_params(m, params);
    if (extra)
        mapper_msg_add_value_table(m, extra);

    lo_bundle_add_message(admin->bundle, msg_index == -1 ?
                          path : admin_msg_strings[msg_index], m);

    /* Since liblo doesn't cache path strings, we must send the bundle
     * immediately if the path is non-standard. */
    if (msg_index == -1)
        mapper_admin_send_bundle(admin);
}

static void mapper_admin_send_device(mapper_admin admin,
                                     mapper_device device)
{
    if (!device)
        return;
    if (device->flags & FLAGS_SENT_DEVICE_INFO)
        return;

    mapper_admin_bundle_message(
        admin, ADM_DEVICE, 0, "s", mdev_name(device),
        AT_LIB_VERSION, PACKAGE_VERSION,
        AT_PORT, device->props.port,
        AT_NUM_INPUTS, mdev_num_inputs(device),
        AT_NUM_OUTPUTS, mdev_num_outputs(device),
        AT_NUM_LINKS, mdev_num_links(device),
        AT_NUM_CONNECTIONS_IN, mdev_num_connections_in(device),
        AT_NUM_CONNECTIONS_OUT, mdev_num_connections_out(device),
        AT_REV, device->props.version,
        AT_EXTRA, device->props.extra);

    device->flags |= FLAGS_SENT_DEVICE_INFO;
}

void mapper_admin_send_signal(mapper_admin admin, mapper_device md,
                              mapper_signal sig)
{
    char sig_name[1024];
    msig_full_name(sig, sig_name, 1024);
    char direction[3] = {'_','_', 0};
    if (sig->handler)
        direction[0] = sig->props.is_output ? 'i' : 'I';
    direction[1] = sig->props.is_output ? 'O' : 'o';
    mapper_admin_bundle_message(
        admin, ADM_SIGNAL, 0, "s", sig_name,
        AT_DIRECTION, direction,
        AT_TYPE, sig->props.type,
        AT_LENGTH, sig->props.length,
        sig->props.unit ? AT_UNITS : -1, sig,
        sig->props.minimum ? AT_MIN : -1, sig,
        sig->props.maximum ? AT_MAX : -1, sig,
        sig->props.num_instances > 1 ? AT_INSTANCES : -1, sig,
        sig->props.rate ? AT_RATE : -1, sig,
        AT_EXTRA, sig->props.extra);
}

void mapper_admin_send_signal_removed(mapper_admin admin, mapper_device md,
                                      mapper_signal sig)
{
    char sig_name[1024];
    msig_full_name(sig, sig_name, 1024);
    mapper_admin_bundle_message(admin, ADM_SIGNAL_REMOVED, 0, "s", sig_name);
}

static void mapper_admin_send_inputs(mapper_admin admin, mapper_device md,
                                     int min, int max)
{
    if (min < 0)
        min = 0;
    else if (min > md->props.num_inputs)
        return;
    if (max < 0 || max > md->props.num_inputs)
        max = md->props.num_inputs-1;

    int i = min;
    for (; i <= max; i++)
        mapper_admin_send_signal(admin, md, md->inputs[i]);
}

static void mapper_admin_send_outputs(mapper_admin admin, mapper_device md,
                                      int min, int max)
{
    if (min < 0)
        min = 0;
    else if (min > md->props.num_outputs)
        return;
    if (max < 0 || max > md->props.num_outputs)
        max = md->props.num_outputs-1;

    int i = min;
    for (; i <= max; i++)
        mapper_admin_send_signal(admin, md, md->outputs[i]);
}

static void mapper_admin_send_linked(mapper_admin admin,
                                     mapper_device device,
                                     mapper_link link)
{
    // Send /linked message
    lo_message m = lo_message_new();
    if (!m) {
        trace("couldn't allocate lo_message\n");
        return;
    }

    /* Since links are bidirectional, use alphabetical ordering of device
     * names to avoid confusing monitors. */
    int swap = strcmp(mdev_name(device), link->props.remote_name) > 0;

    lo_message_add_string(m, swap ? link->props.remote_name : mdev_name(device));
    lo_message_add_string(m, swap ? mdev_name(device) : link->props.remote_name);

    mapper_link_prepare_osc_message(m, link, swap);

    lo_bundle_add_message(admin->bundle, "/linked", m);
}

static void mapper_admin_send_links(mapper_admin admin, mapper_device md)
{
    mapper_link l = md->router->links;
    while (l) {
        mapper_admin_send_linked(admin, md, l);
        l = l->next;
    }
}

static void mapper_admin_send_connected(mapper_admin admin, mapper_device md,
                                        mapper_connection c, int index)
{
    // Send /connected message
    lo_message m = lo_message_new();
    if (!m) {
        trace("couldn't allocate lo_message\n");
        return;
    }

    char local_name[1024], remote_name[1024];

    snprintf(local_name, 1024, "%s%s", mdev_name(md),
             c->parent->signal->props.name);
    snprintf(remote_name, 1024, "%s%s", c->link->props.remote_name,
             c->props.remote_name);

    if (c->props.direction == DI_OUTGOING) {
        lo_message_add_string(m, local_name);
        lo_message_add_string(m, "->");
        lo_message_add_string(m, remote_name);
    }
    else {
        lo_message_add_string(m, remote_name);
        lo_message_add_string(m, "->");
        lo_message_add_string(m, local_name);
    }

    if (index != -1) {
        lo_message_add_string(m, "@ID");
        lo_message_add_int32(m, index);
    }

    mapper_connection_prepare_osc_message(m, c);

    lo_bundle_add_message(admin->bundle, "/connected", m);
}

static void mapper_admin_send_connections_in(mapper_admin admin,
                                             mapper_device md,
                                             int min, int max)
{
    int i = 0;
    mapper_router_signal rs = md->router->signals;
    while (rs) {
        if (!rs->signal->props.is_output) {
            mapper_connection c = rs->connections;
            while (c) {
                if (max > 0 && i > max)
                    break;
                if (i >= min) {
                    mapper_admin_send_connected(admin, md, c, i);
                }
                c = c->next;
                i++;
            }
        }
        rs = rs->next;
    }
}

static void mapper_admin_send_connections_out(mapper_admin admin,
                                              mapper_device md,
                                              int min, int max)
{
    int i = 0;
    mapper_router_signal rs = md->router->signals;
    while (rs) {
        if (rs->signal->props.is_output) {
            mapper_connection c = rs->connections;
            while (c) {
                if (max > 0 && i > max)
                    break;
                if (i >= min) {
                    mapper_admin_send_connected(admin, md, c, i);
                }
                c = c->next;
                i++;
            }
        }
        rs = rs->next;
    }
}

static void mapper_admin_send_combined(mapper_admin admin, mapper_device md,
                                       mapper_combiner c)
{
    // Send /combined message
    lo_message m = lo_message_new();
    if (!m) {
        trace("couldn't allocate lo_message\n");
        return;
    }

    if (!mapper_combiner_prepare_osc_message(m, c))
        lo_bundle_add_message(admin->bundle, "/combined", m);
    else
        lo_message_free(m);
}

/**********************************/
/* Internal OSC message handlers. */
/**********************************/

/*! Respond to /who by announcing the basic device information. */
static int handler_who(const char *path, const char *types, lo_arg **argv,
                       int argc, lo_message msg, void *user_data)
{
    mapper_admin admin = (mapper_admin) user_data;
    mapper_admin_maybe_send_ping(admin, 1);

    trace("%s received /who\n", mdev_name(admin->device));

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

    trace("<monitor> got /device %s + %i arguments\n", name, argc-1);

    mapper_message_t params;
    mapper_msg_parse_params(&params, path, &types[1],
                            argc-1, &argv[1]);

    if (params.types[AT_IP]==0 || params.values[AT_IP]==0) {
        // Find the sender's hostname
        lo_address a = lo_message_get_source(msg);
        if (a) {
            const char *host = lo_address_get_hostname(a);
            if (host) {
                params.types[AT_IP] = types;  // 's'
                params.values[AT_IP] = (lo_arg**)&host;
                params.lengths[AT_IP] = 1;
            }
        }
        else
            trace("Couldn't retrieve host for device %s.\n", name);
    }

    mapper_clock_now(&admin->clock, &admin->clock.now);
    mapper_db_add_or_update_device_params(db, name, &params, &admin->clock.now);

    return 0;
}

/*! Respond to /logout by deleting record of device. */
static int handler_logout(const char *path, const char *types,
                          lo_arg **argv, int argc, lo_message msg,
                          void *user_data)
{
    mapper_admin admin = (mapper_admin) user_data;
    mapper_device md = admin->device;
    mapper_monitor mon = admin->monitor;
    mapper_db db = mapper_monitor_get_db(mon);
    int diff, ordinal;
    char *s;

    if (argc < 1)
        return 0;

    if (types[0] != 's' && types[0] != 'S')
        return 0;

    char *name = &argv[0]->s;

    trace("<%s> got /logout %s\n",
          (md && md->ordinal.locked) ? mdev_name(md) : "monitor", name);

    if (mon) {
        mapper_db_remove_device_by_name(db, name);

        // remove subscriptions
        mapper_monitor_unsubscribe(mon, name);
    }

    // If device exists and is registered
    if (md && md->ordinal.locked) {
        // Check if we have any links to this device, if so remove them
        mapper_link link =
            mapper_router_find_link_by_remote_name(md->router, name);
        if (link) {
            // Call the local link handler if it exists
            if (md->link_cb)
                md->link_cb(md, &link->props, MDEV_LOCAL_DESTROYED,
                            md->link_cb_userdata);

            trace("<%s> Removing link to expired device %s.\n",
                  mdev_name(md), link->props.remote_name);

            mapper_router_remove_link(md->router, link);

            // Inform subscribers
            mapper_admin_set_bundle_dest_subscribers(admin, SUB_DEVICE_LINKS);
            mapper_admin_bundle_message(admin, ADM_UNLINKED, 0, "ss",
                                        mdev_name(md), name);
        }

        /* Parse the ordinal from the complete name which is in the
         * format: /<name>.<n> */
        s = name;
        if (*s++ != '/')
            return 0;
        while (*s != '.' && *s++) {}
        ordinal = atoi(++s);

        // If device name matches
        strtok(name, ".");
        name++;
        if (strcmp(name, md->props.identifier) == 0) {
            // if registered ordinal is within my block, free it
            diff = ordinal - md->ordinal.value;
            if (diff > 0 && diff < 9) {
                md->ordinal.suggestion[diff-1] = 0;
            }
        }
    }

    return 0;
}

// Add/renew/remove a monitor subscription.
static void mapper_admin_manage_subscriber(mapper_admin admin, lo_address address,
                                           int flags, int timeout_seconds,
                                           int revision)
{
    mapper_admin_subscriber *s = &admin->subscribers;
    const char *ip = lo_address_get_hostname(address);
    const char *port = lo_address_get_port(address);
    if (!ip || !port)
        return;

    mapper_clock_t *clock = &admin->clock;
    mapper_clock_now(clock, &clock->now);

    while (*s) {
        if (strcmp(ip, lo_address_get_hostname((*s)->address))==0 &&
            strcmp(port, lo_address_get_port((*s)->address))==0) {
            // subscriber already exists
            if (!flags || !timeout_seconds) {
                // remove subscription
                mapper_admin_subscriber temp = *s;
                int prev_flags = temp->flags;
                *s = temp->next;
                if (temp->address)
                    lo_address_free(temp->address);
                free(temp);
                if (!flags || !(flags &= ~prev_flags))
                    return;
            }
            else {
                // reset timeout
                (*s)->lease_expiration_sec = clock->now.sec + timeout_seconds;
                if ((*s)->flags == flags) {
                    if (revision)
                        return;
                    else
                        break;
                }
                int temp = flags;
                flags &= ~(*s)->flags;
                (*s)->flags = temp;
            }
            break;
        }
        s = &(*s)->next;
    }

    if (!flags)
        return;

    if (!(*s)) {
        // add new subscriber
        mapper_admin_subscriber sub = malloc(sizeof(struct _mapper_admin_subscriber));
        sub->address = lo_address_new(ip, port);
        sub->lease_expiration_sec = clock->now.sec + timeout_seconds;
        sub->flags = flags;
        sub->next = admin->subscribers;
        admin->subscribers = sub;
        s = &sub;
    }

    if (revision == admin->device->props.version)
        return;

    // bring new subscriber up to date
    mapper_admin_set_bundle_dest_mesh(admin, (*s)->address);
    if (flags & SUB_DEVICE)
        mapper_admin_send_device(admin, admin->device);
    if (flags & SUB_DEVICE_INPUTS)
        mapper_admin_send_inputs(admin, admin->device, -1, -1);
    if (flags & SUB_DEVICE_OUTPUTS)
        mapper_admin_send_outputs(admin, admin->device, -1, -1);
    if (flags & SUB_DEVICE_LINKS)
        mapper_admin_send_links(admin, admin->device);
    if (flags & SUB_DEVICE_CONNECTIONS_IN)
        mapper_admin_send_connections_in(admin, admin->device, -1, -1);
    if (flags & SUB_DEVICE_CONNECTIONS_OUT)
        mapper_admin_send_connections_out(admin, admin->device, -1, -1);
}

/*! Respond to /subscribe message by adding or renewing a subscription. */
static int handler_device_subscribe(const char *path, const char *types,
                                    lo_arg **argv, int argc, lo_message msg,
                                    void *user_data)
{
    mapper_admin admin = (mapper_admin) user_data;
    mapper_device md = admin->device;
    int version = -1;

    lo_address a  = lo_message_get_source(msg);
    if (!a || !argc) return 0;

    int i, flags = 0, timeout_seconds = 0;
    for (i = 0; i < argc; i++) {
        if (types[i] != 's' && types[i] != 'S')
            break;
        else if (strcmp(&argv[i]->s, "all")==0)
            flags = SUB_DEVICE_ALL;
        else if (strcmp(&argv[i]->s, "device")==0)
            flags |= SUB_DEVICE;
        else if (strcmp(&argv[i]->s, "inputs")==0)
            flags |= SUB_DEVICE_INPUTS;
        else if (strcmp(&argv[i]->s, "outputs")==0)
            flags |= SUB_DEVICE_OUTPUTS;
        else if (strcmp(&argv[i]->s, "links")==0)
            flags |= SUB_DEVICE_LINKS;
        else if (strcmp(&argv[i]->s, "connections")==0)
            flags |= SUB_DEVICE_CONNECTIONS;
        else if (strcmp(&argv[i]->s, "connections_in")==0)
            flags |= SUB_DEVICE_CONNECTIONS_IN;
        else if (strcmp(&argv[i]->s, "connections_out")==0)
            flags |= SUB_DEVICE_CONNECTIONS_OUT;
        else if (strcmp(&argv[i]->s, "@version")==0) {
            // next argument is last device version recorded by subscriber
            ++i;
            if (i < argc && types[i] == 'i')
                version = argv[i]->i;
        }
        else if (strcmp(&argv[i]->s, "@lease")==0) {
            // next argument is lease timeout in seconds
            ++i;
            if (types[i] == 'i')
                timeout_seconds = argv[i]->i;
            else if (types[i] == 'f')
                timeout_seconds = (int)argv[i]->f;
            else if (types[i] == 'd')
                timeout_seconds = (int)argv[i]->d;
            else {
                trace("<%s> error parsing @lease property in /subscribe.\n",
                      mdev_name(md));
            }
        }
    }

    // add or renew subscription
    mapper_admin_manage_subscriber(admin, a, flags, timeout_seconds, version);

    return 0;
}

/*! Respond to /unsubscribe message by removing a subscription. */
static int handler_device_unsubscribe(const char *path, const char *types,
                                      lo_arg **argv, int argc, lo_message msg,
                                      void *user_data)
{
    mapper_admin admin = (mapper_admin) user_data;

    lo_address a  = lo_message_get_source(msg);
    if (!a) return 0;

    // remove subscription
    mapper_admin_manage_subscriber(admin, a, 0, 0, 0);

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
    mapper_msg_parse_params(&params, path, &types[1], argc-1, &argv[1]);

    mapper_db_add_or_update_signal_params(db, sig_name, devname, &params);

    return 0;
}

/*! Unregister information about a removed signal. */
static int handler_signal_removed(const char *path, const char *types,
                                  lo_arg **argv, int argc, lo_message m,
                                  void *user_data)
{
    mapper_admin admin = (mapper_admin) user_data;
    mapper_monitor mon = admin->monitor;
    mapper_db db = mapper_monitor_get_db(mon);

    if (argc < 1)
        return 1;

    if (types[0] != 's' && types[0] != 'S')
        return 1;

    const char *full_sig_name = &argv[0]->s;
    const char *sig_name = strchr(full_sig_name+1, '/');
    if (!sig_name)
        return 1;

    int dev_name_len = sig_name-full_sig_name;
    if (dev_name_len >= 1024)
        return 0;

    char dev_name[1024];
    strncpy(dev_name, full_sig_name, dev_name_len);
    dev_name[dev_name_len]=0;

    mapper_db_remove_signal_by_name(db, dev_name, sig_name);

    return 0;
}

/*! Repond to name collisions during allocation, help suggest IDs once allocated. */
static int handler_device_name_registered(const char *path, const char *types,
                                          lo_arg **argv, int argc,
                                          lo_message msg, void *user_data)
{
    mapper_admin admin = (mapper_admin) user_data;
    mapper_device md = admin->device;
    char *name, *s;
    int hash, ordinal, diff;
    int temp_id = -1, suggestion = -1;

    if (argc < 1)
        return 0;

    if (types[0] != 's' && types[0] != 'S')
        return 0;

    name = &argv[0]->s;

    trace("</%s.?::%p> got /name/registered %s %i \n",
          md->props.identifier, admin, name, temp_id);

    if (md->ordinal.locked) {
        /* Parse the ordinal from the complete name which is in the
         * format: /<name>.<n> */
        s = name;
        if (*s != '/')
            return 0;
        s = strrchr(s, '.');
        if (!s)
            return 0;
        ordinal = atoi(s+1);
        *s = 0;

        // If device name matches
        if (strcmp(name+1, md->props.identifier) == 0) {
            // if id is locked and registered id is within my block, store it
            diff = ordinal - md->ordinal.value;
            if (diff > 0 && diff < 9) {
                md->ordinal.suggestion[diff-1] = -1;
            }
        }
    }
    else {
        hash = crc32(0L, (const Bytef *)name, strlen(name));
        if (hash == md->props.name_hash) {
            if (argc > 1) {
                if (types[1] == 'i')
                    temp_id = argv[1]->i;
                if (types[2] == 'i')
                    suggestion = argv[2]->i;
            }
            if (temp_id == admin->random_id &&
                suggestion != md->ordinal.value && suggestion > 0) {
                md->ordinal.value = suggestion;
                mapper_admin_probe_device_name(admin, md);
            }
            else {
                /* Count ordinal collisions. */
                md->ordinal.collision_count++;
                md->ordinal.count_time = get_current_time();
            }
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
    mapper_device md = admin->device;
    char *name;
    double current_time;
    int hash, temp_id = -1, i;

    if (types[0] == 's' || types[0] == 'S')
        name = &argv[0]->s;
    else
        return 0;

    if (argc > 0) {
        if (types[1] == 'i')
            temp_id = argv[1]->i;
        else if (types[1] == 'f')
            temp_id = (int) argv[1]->f;
    }

    trace("</%s.?::%p> got /name/probe %s %i \n",
          md->props.identifier, admin, name, temp_id);

    hash = crc32(0L, (const Bytef *)name, strlen(name));
    if (hash == md->props.name_hash) {
        if (md->ordinal.locked) {
            current_time = get_current_time();
            for (i=0; i<8; i++) {
                if (md->ordinal.suggestion[i] >= 0
                    && (current_time - md->ordinal.suggestion[i]) > 2.0) {
                    // reserve suggested ordinal
                    md->ordinal.suggestion[i] = get_current_time();
                    break;
                }
            }
            /* Name may not yet be registered, so we can't use
             * mapper_admin_send() here. */
            lo_send(admin->bus_addr, "/name/registered",
                    "sii", name, temp_id,
                    (md->ordinal.value+i+1));
        }
        else {
            md->ordinal.collision_count++;
            md->ordinal.count_time = get_current_time();
        }
    }
    return 0;
}

/*! Link two devices. */
static int handler_device_link(const char *path, const char *types,
                               lo_arg **argv, int argc, lo_message msg,
                               void *user_data)
{
    mapper_admin admin = (mapper_admin) user_data;
    mapper_device md = admin->device;
    int self_ref = 0;

    if (argc < 2)
        return 0;

    // Need at least 2 devices to link
    if (types[0] != 's' && types[0] != 'S' && types[1] != 's'
        && types[1] != 'S')
        return 0;

    const char *remote_name = NULL;

    if (strcmp(mdev_name(md), &argv[0]->s) == 0) {
        remote_name = &argv[1]->s;
        self_ref++;
    }
    if (strcmp(mdev_name(md), &argv[1]->s) == 0) {
        remote_name = &argv[0]->s;
        self_ref++;
    }
    if (!self_ref)
        return 0;

    trace("<%s> got /link %s %s\n", mdev_name(md),
          &argv[0]->s, &argv[1]->s);

    mapper_message_t params;
    // add arguments from /link if any
    mapper_msg_parse_params(&params, path, &types[2], argc-2, &argv[2]);

    // Discover whether the device is already linked.
    mapper_link link =
        mapper_router_find_link_by_remote_name(md->router, remote_name);

    if (link) {
        // Already linked, forward to link/modify handler.
        if (argc > 3)
            handler_device_link_modify(path, types, argv, argc,
                                       msg, user_data);
        return 0;
    }

    if (self_ref == 2) {
        // this is a self-link, no need to continue linking protocol
        // Creation of a new router added to the source.
        link = mapper_router_add_link(md->router, "localhost",
                                      lo_server_get_port(admin->mesh_server),
                                      md->props.port, mdev_name(md));
        if (!link) {
            trace("can't perform /linkTo, NULL link\n");
            return 0;
        }

        if (argc > 2)
            mapper_router_set_link_from_message(md->router, link, &params, 0);

        // Call local link handler if it exists
        if (md->link_cb)
            md->link_cb(md, &link->props, MDEV_LOCAL_ESTABLISHED,
                        md->link_cb_userdata);

        // Announce the result to subscribers.
        mapper_admin_set_bundle_dest_subscribers(admin, SUB_DEVICE_LINKS);
        mapper_admin_send_linked(admin, md, link);

        trace("<%s> added new router to self\n", mdev_name(md));
    }
    else {
        lo_arg *arg_port = (lo_arg*) &md->props.port;
        params.values[AT_PORT] = &arg_port;
        params.types[AT_PORT] = "i";
        params.lengths[AT_PORT] = 1;

        char cmd[1024];
        snprintf(cmd, 1024, "%s/linkTo", remote_name);
        mapper_admin_set_bundle_dest_bus(admin);
        mapper_admin_bundle_message_with_params(
            admin, &params, 0, ADM_LINK_TO, 0, "ss", remote_name, mdev_name(md));
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

    const char *remote_name = NULL, *host = NULL, *admin_port;
    mapper_message_t params;
    lo_address a = NULL;
    int data_port;

    if (argc < 2)
        return 0;

    // Need at least 2 devices to link
    if (types[0] != 's' && types[0] != 'S' && types[1] != 's'
        && types[1] != 'S')
        return 0;

    // Return if we are not the "src"
    if (strcmp(mdev_name(md), &argv[0]->s) == 0)
        remote_name = &argv[1]->s;
    else
        return 0;

    trace("<%s> got /linkTo %s\n", mdev_name(md), remote_name);

    // Parse the message.
    mapper_msg_parse_params(&params, path, &types[1], argc-1, &argv[1]);

    // Discover whether the device is already linked.
    mapper_link link =
        mapper_router_find_link_by_remote_name(md->router, remote_name);

    if (link) {
        // Already linked, forward to link/modify handler.
        if (argc > 3)
            handler_device_link_modify(path, types, argv, argc, msg, user_data);
        return 0;
    }

    // Find the sender's hostname
    a = lo_message_get_source(msg);
    host = lo_address_get_hostname(a);
    admin_port = lo_address_get_port(a);
    if (!host) {
        trace("can't perform /linkTo, host unknown\n");
        return 0;
    }

    // Retrieve the port
    if (mapper_msg_get_param_if_int(&params, AT_PORT, &data_port)) {
        trace("can't perform /linkTo, port unknown\n");
        return 0;
    }

    // Creation of a new router added to the source.
    link = mapper_router_add_link(md->router, host, atoi(admin_port),
                                  data_port, remote_name);
    if (!link) {
        trace("can't perform /linkTo, NULL link\n");
        return 0;
    }

    if (argc > 2)
        mapper_router_set_link_from_message(md->router, link, &params, 0);

    // Call local link handler if it exists
    if (md->link_cb)
        md->link_cb(md, &link->props, MDEV_LOCAL_ESTABLISHED,
                    md->link_cb_userdata);

    // Announce the result to destination and subscribers.
    if (!a)
        mapper_admin_set_bundle_dest_bus(admin);
    else
        mapper_admin_set_bundle_dest_mesh(admin, a);
    mapper_admin_send_linked(admin, md, link);
    mapper_admin_set_bundle_dest_subscribers(admin, SUB_DEVICE_LINKS);
    mapper_admin_send_linked(admin, md, link);

    trace("<%s> added new router to %s -> host: %s, port: %d\n",
          mdev_name(md), remote_name, host, data_port);

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

    trace("<monitor> got /linked %s %s\n", src_name, dest_name);

    mapper_message_t params;
    mapper_msg_parse_params(&params, path, types+2, argc-2, argv+2);

    mapper_db_add_or_update_link_params(db, src_name, dest_name, &params);

    return 0;
}

/*! Modify the link properties : scope, etc. */
static int handler_device_link_modify(const char *path, const char *types,
                                      lo_arg **argv, int argc, lo_message msg,
                                      void *user_data)
{
    mapper_admin admin = (mapper_admin) user_data;
    mapper_device md = admin->device;

    int updated, swap = 0;
    mapper_message_t params;

    if (argc < 2)
        return 0;

    if (types[0] != 's' && types[0] != 'S' && types[1] != 's'
        && types[1] != 'S')
        return 0;

    const char *remote_name = NULL;

    if (strcmp(&argv[0]->s, mdev_name(md)) == 0)
        remote_name = &argv[1]->s;
    else if (strcmp(&argv[1]->s, mdev_name(md)) == 0) {
        remote_name = &argv[0]->s;
        swap = 1;
    }
    else {
        trace("<%s> ignoring /link/modify %s %s\n",
              mdev_name(md), &argv[0]->s, &argv[1]->s);
        return 0;
    }

    trace("<%s> got /link/modify %s %s\n", mdev_name(md),
          &argv[0]->s, &argv[1]->s);

    // Discover whether the device is already linked.
    mapper_link link =
        mapper_router_find_link_by_remote_name(md->router, remote_name);

    if (!link) {
        trace("can't perform /link/modify, link not found.\n");
        return 0;
    }

    // Parse the message.
    mapper_msg_parse_params(&params, path, &types[2], argc-2, &argv[2]);

    updated = mapper_router_set_link_from_message(md->router, link, &params, swap);

    if (updated) {
        // increment device version
        md->version += updated;

        // Inform subscribers
        mapper_admin_set_bundle_dest_subscribers(admin, SUB_DEVICE_LINKS);
        mapper_admin_send_linked(admin, md, link);

        // Call local link handler if it exists
        if (md->link_cb)
            md->link_cb(md, &link->props, MDEV_LOCAL_MODIFIED,
                        md->link_cb_userdata);

        trace("<%s> modified link to %s\n", mdev_name(md), remote_name);
    }

    return 0;
}

/*! Unlink two devices. */
static int handler_device_unlink(const char *path, const char *types,
                                 lo_arg **argv, int argc, lo_message msg,
                                 void *user_data)
{
    mapper_admin admin = (mapper_admin) user_data;
    mapper_device md = admin->device;
    const char *remote_name;

    if (argc < 2)
        return 0;

    if (types[0] != 's' && types[0] != 'S' && types[1] != 's'
        && types[1] != 'S')
        return 0;

    if (strcmp(&argv[0]->s, mdev_name(md)) == 0)
        remote_name = &argv[1]->s;
    else if (strcmp(&argv[1]->s, mdev_name(md)) == 0) {
        remote_name = &argv[0]->s;
    }
    else
        return 0;

    trace("<%s> got /unlink %s %s + %i arguments\n", mdev_name(md),
          &argv[0]->s, &argv[1]->s, argc-2);

    mapper_message_t params;
    mapper_msg_parse_params(&params, path, &types[2], argc-2, &argv[2]);

    /* Remove the link for the destination. */
    mapper_link link =
        mapper_router_find_link_by_remote_name(md->router, remote_name);
    if (link) {
        // Call the local link handler if it exists
        if (md->link_cb)
            md->link_cb(md, &link->props, MDEV_LOCAL_DESTROYED,
                        md->link_cb_userdata);

        // Inform subscribers
        mapper_admin_set_bundle_dest_subscribers(admin, SUB_DEVICE_LINKS);
        mapper_admin_bundle_message_with_params(admin, &params, 0, ADM_UNLINKED,
                                                0, "ss", mdev_name(md), remote_name);

        mapper_router_remove_link(md->router, link);
    }
    else {
        trace("<%s> no router for %s found in /unlink handler\n",
              mdev_name(md), remote_name);
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

    if (argc < 2)
        return 0;

    if (types[0] != 's' && types[0] != 'S' && types[1] != 's'
        && types[1] != 'S')
        return 0;

    const char *src_name = &argv[0]->s;
    const char *dest_name = &argv[1]->s;

    trace("<monitor> got /unlinked %s %s + %i arguments\n",
          src_name, dest_name, argc-2);

    mapper_db db = mapper_monitor_get_db(mon);

    mapper_db_remove_connections_by_query(db,
        mapper_db_get_connections_by_src_dest_device_names(db, src_name,
                                                           dest_name));
    mapper_db_remove_link(db,
        mapper_db_get_link_by_device_names(db, src_name, dest_name));

    return 0;
}

/* Helper function to check if the prefix matches.  Like strcmp(),
 * returns 0 if they match (up to the second '/'), non-0 otherwise.
 * Also optionally returns a pointer to the remainder of str1 after
 * the prefix. */
static int prefix_cmp(const char *str1, const char *str2,
                      const char **rest)
{
    if (str1[0]!='/') {
        trace("String '%s' does not start with '/'.\n", str1);
        return -1;
    }
    if (str2[0]!='/') {
        trace("String '%s' does not start with '/'.\n", str2);
        return -1;
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
    mapper_signal src_signal, dest_signal;
    int i = 1, num_inputs = 1, dest_index = 1, num_params, slot = -1;

    const char *dest_signal_name, *dest_name;
    const char *src_signal_name, *src_name = NULL;

    if (argc < 2)
        return 0;

    if (types[0] != 's' && types[0] != 'S' && types[1] != 's'
        && types[1] != 'S')
        return 0;

    while (i < argc && (types[i] == 's' || types[i] == 'S')) {
        // old protocol: /connect src dest
        // new protocol: /connect src1 src2 ... srcN -> dest
        // if we find "->" before '@' or end of args, count inputs
        if ((&argv[i]->s)[0] == '@')
            break;
        else if (strcmp(&argv[i]->s, "->") == 0) {
            num_inputs = i;
            dest_index = i+1;
            break;
        }
        i++;
    }

    // check if we are the destination
    if (prefix_cmp(&argv[dest_index]->s, mdev_name(md), &dest_signal_name) == 0) {
        dest_name = &argv[dest_index]->s;
        // we are the destination of this connection
        trace("<%s> got /connect from remote signal%s",
              mdev_name(md), num_inputs > 1 ? "s" : "");
        for (i = 0; i < num_inputs; i++)
            printf(" %s", &argv[i]->s);
        printf(" to local signal %s\n", dest_name);
    }
    else {
        trace("<%s> ignoring /connect", mdev_name(md));
        for (i = 0; i <= dest_index; i++)
            printf(" %s", &argv[i]->s);
        printf("\n");
        return 0;
    }

    // check if we have a matching signal
    dest_signal = mdev_get_signal_by_name(md, dest_signal_name, 0);
    if (!dest_signal) {
        trace("<%s> no matching signal found for '%s' in /connect\n",
              mdev_name(md), dest_signal_name);
        return 0;
    }
    else if (!dest_signal->handler) {
        trace("<%s> matching signal %s has no input handler\n",
              mdev_name(md), dest_signal_name);
        return 0;
    }

    // parse arguments from /connect if any
    mapper_message_t params;
    num_params = mapper_msg_parse_params(&params, path, &types[dest_index+1],
                                         argc-dest_index-1, &argv[dest_index+1]);

    /* If this is a multi-input connection, create combiner and requisite
     * bypass-mode subconnections. */
    if (num_inputs > 1) {
        // set subconnections to "raw" mode
        params.values[AT_MODE] = 0;
        params.lengths[AT_MODE] = 0;

        // Create a combiner
        mapper_combiner com = mapper_router_find_combiner(md->router, dest_signal);
        if (!com)
            com = mapper_router_add_combiner(md->router, dest_signal, num_inputs);
        else
            mapper_combiner_set_num_slots(com, num_inputs);

        mapper_combiner_set_from_message(com, &params);

        // now overwrite some parameters for the subconnections
        params.values[AT_EXPRESSION] = 0;
    }

    for (i = 0; i < num_inputs; i++) {
        src_name = &argv[i]->s;
        src_signal_name = strchr(src_name+1, '/');
        if (!src_signal_name) {
            trace("<%s> remote '%s' has no parameter in /connect.\n",
                  mdev_name(md), src_name);
            return 0;
        }

        mapper_link link =
            mapper_router_find_link_by_remote_name(md->router, src_name);

        /* If no link found, we simply stop here. The idea was floated
         * that we could automatically create links, but it was agreed
         * that this kind of logic would be best left up to client
         * applications. */
        if (!link) {
            trace("<%s> not linked to '%s' on /connect.\n",
                  mdev_name(md), src_name);
            return 0;
        }

        mapper_connection c = mapper_router_find_connection(md->router,
                                                            dest_signal,
                                                            src_signal_name);

        /* If a connection connection already exists between these two signals,
         * forward the message to handler_signal_connection_modify() and stop. */
        if (c) {
            if (num_inputs == 1)
                handler_signal_connection_modify(path, types, argv, argc,
                                                 msg, user_data);
            else {
                // TODO: we need to over-ride the existing connection, set it to raw mode
                // also need to reset slot number
            }
            return 0;
        }

        if (link->self_link) {
            // no need to continue with connection protocol
            src_signal = mdev_get_signal_by_name(md, src_signal_name, 0);
            if (!src_signal) {
                trace("<%s> no matching signal found for '%s' in /connect\n",
                      mdev_name(md), src_signal_name);
                return 0;
            }
            else if (src_signal == dest_signal) {
                trace("<%s> connections from signal to self are not allowed\n",
                      mdev_name(md));
                return 0;
            }

            /* Add flavourless connections */
            // TODO: allow single connection structure to be shared between signals?
            mapper_connection c2;
            c = mapper_router_add_connection(md->router, link, dest_signal,
                                             src_signal->props.name,
                                             src_signal->props.type,
                                             src_signal->props.length,
                                             DI_INCOMING);
            c2 = mapper_router_add_connection(md->router, link, src_signal,
                                              dest_signal->props.name,
                                              dest_signal->props.type,
                                              dest_signal->props.length,
                                              DI_OUTGOING);
            if (!c || !c2) {
                trace("couldn't create mapper_connection "
                      "in handler_signal_connectTo\n");
                return 0;
            }
            c->complement = c2;
            c2->complement = c;

            if (num_inputs == 1 && num_params) {
                /* If send_as_instance property is not set, make connection
                 * default to passing updates as instances if either source
                 * or destination signals have multiple instances. */
                if (!params.values[AT_SEND_AS_INSTANCE]) {
                    int remote_instances = 0;
                    mapper_msg_get_param_if_int(&params, AT_INSTANCES,
                                                &remote_instances);
                    if (dest_signal->props.num_instances > 1
                        || src_signal->props.num_instances > 1) {
                        c->props.send_as_instance = 1;
                        c2->props.send_as_instance = 1;
                    }
                    else {
                        c->props.send_as_instance = 0;
                        c2->props.send_as_instance = 0;
                    }
                }

                /* Set its properties. */
                mapper_connection_set_from_message(c, &params, DI_INCOMING);
                mapper_connection_set_from_message(c2, &params, DI_OUTGOING);

                c->props.slot = c2->props.slot = -1;
            }

            // Inform subscribers
            mapper_admin_set_bundle_dest_subscribers(admin, SUB_DEVICE_CONNECTIONS);
            mapper_admin_send_connected(admin, md, c, -1);

            // Call local connection handler if it exists
            if (md->connection_cb) {
                md->connection_cb(md, &link->props, dest_signal,
                                  &c->props, MDEV_LOCAL_ESTABLISHED,
                                  md->connection_cb_userdata);
                md->connection_cb(md, &link->props, src_signal,
                                  &c2->props, MDEV_LOCAL_ESTABLISHED,
                                  md->connection_cb_userdata);
            }
            c->new = 0;
            return 0;
        }

        // substitute some missing parameters with known properties
        lo_arg *arg_type = (lo_arg*) &dest_signal->props.type;
        params.values[AT_TYPE] = &arg_type;
        params.types[AT_TYPE] = "c";
        params.lengths[AT_TYPE] = 1;

        lo_arg *arg_length = (lo_arg*) &dest_signal->props.length;
        params.values[AT_LENGTH] = &arg_length;
        params.types[AT_LENGTH] = "i";
        params.lengths[AT_LENGTH] = 1;

        lo_arg *arg_num_instances = (lo_arg*) &dest_signal->props.num_instances;
        params.values[AT_INSTANCES] = &arg_num_instances;
        params.types[AT_INSTANCES] = "i";
        params.lengths[AT_INSTANCES] = 1;

        if (num_inputs > 1) {
            ++slot;
            lo_arg *arg_slot = (lo_arg*) &slot;
            params.values[AT_SLOT] = &arg_slot;
            params.types[AT_SLOT] = "i";
            params.lengths[AT_SLOT] = 1;
        }

        mapper_admin_set_bundle_dest_mesh(admin, link->admin_addr);
        mapper_admin_bundle_message_with_params(
            admin, &params, dest_signal->props.extra,
            ADM_CONNECT_TO, 0, "sss", src_name, "->", dest_name,
            (num_inputs > 1) ? AT_MODE : -1, MO_RAW,
            (!params.values[AT_MIN] && dest_signal->props.minimum) ? AT_MIN : -1, dest_signal,
            (!params.values[AT_MAX] && dest_signal->props.maximum) ? AT_MAX : -1, dest_signal);
    }

    return 0;
}

/*! Connect two signals. */
static int handler_signal_connectTo(const char *path, const char *types,
                                    lo_arg **argv, int argc,
                                    lo_message msg, void *user_data)
{
    mapper_admin admin = (mapper_admin) user_data;
    mapper_device md = admin->device;
    mapper_signal signal;
    int outgoing = 1, skip_arg = 0;

    const char *local_signal_name, *local_name;
    const char *remote_signal_name, *remote_name;

    if (argc < 2)
        return 0;

    if ((types[0] != 's' && types[0] != 'S')
        || (types[1] != 's' && types[1] != 'S'))
        return 0;

    if (!(prefix_cmp(&argv[0]->s, mdev_name(md), &local_signal_name) == 0)) {
        trace("<%s> ignoring /connectTo %s %s\n",
              mdev_name(md), &argv[0]->s, &argv[1+skip_arg]->s);
        return 0;
    }
    local_name = &argv[0]->s;

    if (argc > 2 && (types[2] == 's' || types[2] == 'S')) {
        // check for string "->" in argv[1]
        if (strcmp(&argv[1]->s, "->") == 0)
            skip_arg = 1;
    }
    remote_name = &argv[1+skip_arg]->s;

    signal = mdev_get_signal_by_name(md, local_signal_name, 0);
    if (!signal) {
        trace("<%s> no matching signal found for '%s' in /connectTo\n",
              mdev_name(md), local_signal_name);
        return 0;
    }
    else if (!outgoing && !signal->handler) {
        trace("<%s> matching signal %s has no input handler\n",
              mdev_name(md), local_signal_name);
        return 0;
    }

    remote_signal_name = strchr(remote_name+1, '/');

    if (!remote_signal_name) {
        trace("<%s> remote '%s' has no parameter in /connectTo.\n",
              mdev_name(md), remote_name);
        return 0;
    }

    trace("<%s> got /connectTo %s %s %s\n", mdev_name(md),
          &argv[0]->s, outgoing ? "->" : "<-", &argv[1+skip_arg]->s);

    mapper_message_t params;
    mapper_msg_parse_params(&params, path, types+2+skip_arg,
                            argc-2-skip_arg, &argv[2+skip_arg]);

    mapper_link link =
        mapper_router_find_link_by_remote_name(md->router, remote_name);

    /* If no link found, we simply stop here. The idea was floated
     * that we could automatically create links, but it was agreed
     * that this kind of logic would be best left up to client
     * applications. */
    if (!link) {
        trace("<%s> not linked to '%s' on /connectTo.\n",
              mdev_name(md), remote_name);
        return 0;
    }

    mapper_connection c =
        mapper_router_find_connection(md->router, signal, remote_signal_name);

    /* If a connection connection already exists between these two signals,
     * forward the message to handler_signal_connection_modify() and stop. */
    if (c) {
        handler_signal_connection_modify(path, types, argv, argc,
                                         msg, user_data);
        return 0;
    }

    /* Creation of a connection requires the type and length info. */
    if (!params.values[AT_TYPE] || !params.values[AT_LENGTH])
        return 0;

    char remote_type = 0;
    if (*params.types[AT_TYPE] == 'c')
        remote_type = (*params.values[AT_TYPE])->c;
    else if (*params.types[AT_TYPE] == 's')
        remote_type = (*params.values[AT_TYPE])->s;
    else
        return 0;

    int remote_length = 0;
    if (*params.types[AT_LENGTH] == 'i')
        remote_length = (*params.values[AT_LENGTH])->i;
    else
        return 0;

    /* Add a flavourless connection */
    c = mapper_router_add_connection(md->router, link, signal, remote_signal_name,
                                     remote_type, remote_length,
                                     outgoing ? DI_OUTGOING : DI_INCOMING);
    if (!c) {
        trace("couldn't create mapper_connection "
              "in handler_signal_connectTo\n");
        return 0;
    }

    if (argc > 2) {
        /* If send_as_instance property is not set, make connection
         * default to passing updates as instances if either source
         * or destination signals have multiple instances. */
        if (!params.values[AT_SEND_AS_INSTANCE]) {
            int remote_instances = 0;
            mapper_msg_get_param_if_int(&params, AT_INSTANCES,
                                        &remote_instances);
            if (remote_instances > 1 || signal->props.num_instances > 1) {
                c->props.send_as_instance = 1;
            }
            else
                c->props.send_as_instance = 0;
        }

        /* Set its properties. */
        mapper_connection_set_from_message(
            c, &params, outgoing ? DI_OUTGOING : DI_INCOMING);
    }

    if (c->props.direction == DI_OUTGOING) {
        // Inform remote device
        mapper_admin_set_bundle_dest_mesh(admin, link->admin_addr);
        mapper_admin_send_connected(admin, md, c, -1);

        // Inform subscribers
        mapper_admin_set_bundle_dest_subscribers(admin, SUB_DEVICE_CONNECTIONS_OUT);
        mapper_admin_send_connected(admin, md, c, -1);

        // Call local connection handler if it exists
        if (md->connection_cb)
            md->connection_cb(md, &link->props, signal,
                              &c->props, MDEV_LOCAL_ESTABLISHED,
                              md->connection_cb_userdata);
        c->new = 0;
    }

    return 0;
}

/*! Respond to /connected by storing connection in database. */
static int handler_signal_connected(const char *path, const char *types,
                                    lo_arg **argv, int argc, lo_message msg,
                                    void *user_data)
{
    // "/connected" messages will always follow the form <src> -> <dest>
    mapper_admin admin = (mapper_admin) user_data;
    mapper_device md = admin->device;
    mapper_monitor mon = admin->monitor;
    mapper_signal signal;
    int skip_arg = 0;

    const char *local_signal_name;
    const char *remote_signal_name, *remote_name;

    if (argc < 2)
        return 0;

    if (types[0] != 's' && types[0] != 'S' && types[1] != 's'
        && types[1] != 'S')
        return 0;

    if (argc > 2 && (types[2] == 's' || types[2] == 'S')) {
        // check for string "->" in argv[1]
        if (strcmp(&argv[1]->s, "->") == 0)
            skip_arg = 1;
    }

    mapper_message_t params;
    mapper_msg_parse_params(&params, path, types+2+skip_arg,
                            argc-2-skip_arg, argv+2+skip_arg);

    if (mon) {
        mapper_db db = mapper_monitor_get_db(mon);

        trace("<monitor> got /connected %s -> %s\n",
              &argv[0]->s, &argv[1+skip_arg]->s);

        mapper_db_add_or_update_connection_params(db, &argv[0]->s,
                                                  &argv[1+skip_arg]->s, &params);
    }

    // devices only care about possible extra info from upstream node
    if (!md || !mdev_name(md) || prefix_cmp(&argv[1+skip_arg]->s,
                                            mdev_name(md), &local_signal_name))
        return 0;

    trace("<%s> got /connected %s -> %s + %d arguments\n",
          mdev_name(md), &argv[0]->s, &argv[1+skip_arg]->s, argc-2-skip_arg);

    remote_name = &argv[0]->s;
    remote_signal_name = strchr(remote_name+1, '/');
    if (!remote_signal_name)
        return 0;

    signal = mdev_get_signal_by_name(md, local_signal_name, 0);
    if (!signal) {
        trace("<%s> no matching signal found for '%s' in /connected\n",
              mdev_name(md), local_signal_name);
        return 0;
    }

    mapper_link link =
        mapper_router_find_link_by_remote_name(md->router, remote_name);
    if (!link) {
        trace("<%s> not linked from '%s' on /connected.\n",
              mdev_name(md), remote_name);
        return 0;
    }
    else if (link->self_link) {
        // no further action necessary
        return 0;
    }

    mapper_connection c =
        mapper_router_find_connection(md->router, signal, remote_signal_name);
    if (c) {
        if (argc <= (2+skip_arg))
            return 0;
        // connection already exists, add metadata
        int updated = mapper_connection_set_from_message(c, &params, DI_INCOMING);
        if (updated || c->new) {
            // Inform subscribers
            mapper_admin_set_bundle_dest_subscribers(admin, SUB_DEVICE_CONNECTIONS_IN);
            mapper_admin_send_connected(admin, md, c, -1);

            // Call local connection handler if it exists
            if (md->connection_cb)
                md->connection_cb(md, &link->props, signal, &c->props,
                                  c->new ? MDEV_LOCAL_ESTABLISHED : MDEV_LOCAL_MODIFIED,
                                  md->connection_cb_userdata);
            c->new = 0;
        }
    }
    else {
        /* Creation of a connection requires the type and length info. */
        if (!params.values[AT_SRC_TYPE] || !params.values[AT_SRC_LENGTH])
            return 0;

        char remote_type = 0;
        if (*params.types[AT_SRC_TYPE] == 'c')
            remote_type = (*params.values[AT_SRC_TYPE])->c;
        else if (*params.types[AT_SRC_TYPE] == 's')
            remote_type = (*params.values[AT_SRC_TYPE])->s;
        else
            return 0;

        int remote_length = 0;
        if (*params.types[AT_SRC_LENGTH] == 'i')
            remote_length = (*params.values[AT_SRC_LENGTH])->i;
        else
            return 0;

        // Add a flavourless connection
        c = mapper_router_add_connection(md->router, link, signal, remote_signal_name,
                                         remote_type, remote_length, DI_INCOMING);

        /* Set its properties. */
        mapper_connection_set_from_message(c, &params, DI_INCOMING);

        // Inform subscribers
        mapper_admin_set_bundle_dest_subscribers(admin, SUB_DEVICE_CONNECTIONS_IN);
        mapper_admin_send_connected(admin, md, c, -1);

        // Call local connection handler if it exists
        if (md->connection_cb)
            md->connection_cb(md, &link->props, signal, &c->props,
                              MDEV_LOCAL_ESTABLISHED, md->connection_cb_userdata);
    }

    // Check if this connection targets a combiner
    mapper_combiner cb;
    if (c->props.slot >=0 &&
        (cb = mapper_router_find_combiner(md->router, signal))) {
        if (!mapper_combiner_set_slot_connection(cb, c->props.slot, c)) {
            // Combiner is initialised, inform subscribers
            mapper_admin_set_bundle_dest_subscribers(admin, SUB_DEVICE_CONNECTIONS_IN);
            mapper_admin_send_combined(admin, md, cb);
        }
    }

    return 0;
}

/*! Modify the connection properties : mode, range, expression,
 *  boundMin, boundMax. */
static int handler_signal_connection_modify(const char *path, const char *types,
                                            lo_arg **argv, int argc, lo_message msg,
                                            void *user_data)
{
    mapper_admin admin = (mapper_admin) user_data;
    mapper_device md = admin->device;
    mapper_signal sig;
    int outgoing = 1, skip_arg = 0;

    const char *local_signal_name, *local_name;
    const char *remote_signal_name, *remote_name;

    if (argc < 4)
        return 0;

    if ((types[0] != 's' && types[0] != 'S')
        || (types[1] != 's' && types[1] != 'S') || (types[2] != 's'
                                                    && types[2] != 'S'))
        return 0;

    if (argc > 2 && (types[2] == 's' || types[2] == 'S')) {
        // check for string "->" or "<-" in argv[1]
        if (strcmp(&argv[1]->s, "<-") == 0) {
            outgoing = 0;
            skip_arg = 1;
        }
        else if (strcmp(&argv[1]->s, "->") == 0) {
            skip_arg = 1;
        }
    }

    if (prefix_cmp(&argv[0]->s, mdev_name(md), &local_signal_name) == 0) {
        local_name = &argv[0]->s;
        remote_name = &argv[1+skip_arg]->s;
    }
    else if (prefix_cmp(&argv[1+skip_arg]->s, mdev_name(md), &local_signal_name) == 0) {
        local_name = &argv[1+skip_arg]->s;
        remote_name = &argv[0]->s;
        outgoing ^= 1;
    }
    else {
        trace("<%s> ignoring /connection/modify %s %s %s\n", mdev_name(md),
              &argv[0]->s, outgoing ? "->" : "<-", &argv[1+skip_arg]->s);
        return 0;
    }

    remote_signal_name = strchr(remote_name+1, '/');

    if (!remote_signal_name) {
        trace("<%s> destination '%s' has no parameter in /connection/modify.\n",
              mdev_name(md), remote_name);
        return 0;
    }

    if (!(sig=mdev_get_signal_by_name(md, local_signal_name, 0)))
    {
        trace("<%s> no signal found for '%s' in /connection/modify\n",
              mdev_name(md), local_name);
        return 0;
    }

    mapper_link link = mapper_router_find_link_by_remote_name(md->router,
                                                              remote_name);
    if (!link) {
        trace("<%s> ignoring /connection/modify, no link found for '%s'\n",
              mdev_name(md), remote_name);
        return 0;
    }

    mapper_connection c =
        mapper_router_find_connection(md->router, sig, remote_signal_name);
    if (!c) {
        trace("<%s> ignoring /connection/modify, no connection found "
              "for '%s' %s '%s'\n", mdev_name(md), &argv[0]->s,
              outgoing ? "->" : "<-", &argv[1+skip_arg]->s);
        return 0;
    }

    mapper_message_t params;
    mapper_msg_parse_params(&params, path, types+2, argc-2, &argv[2]);

    int updated = mapper_connection_set_from_message(c, &params, outgoing
                                                     ? DI_OUTGOING : DI_INCOMING);
    if (updated) {
        if (link->self_link) {
            // also update the complementary connection structures
            mapper_connection_set_from_message(c->complement, &params, outgoing
                                               ? DI_INCOMING : DI_OUTGOING);
        }
        else {
            // Inform remote device
            mapper_admin_set_bundle_dest_mesh(admin, link->admin_addr);
            mapper_admin_send_connected(admin, md, c, -1);
        }

        // Inform subscribers
        mapper_admin_set_bundle_dest_subscribers(admin,
            outgoing ? SUB_DEVICE_CONNECTIONS_OUT : SUB_DEVICE_CONNECTIONS_IN);
        mapper_admin_send_connected(admin, md, c, -1);

        // Call local connection handler if it exists
        if (md->connection_cb)
            md->connection_cb(md, &link->props, sig,
                              &c->props, MDEV_LOCAL_MODIFIED,
                              md->connection_cb_userdata);

        if (link->self_link) {
            // Inform subscribers
            mapper_admin_set_bundle_dest_subscribers(admin,
                outgoing ? SUB_DEVICE_CONNECTIONS_IN : SUB_DEVICE_CONNECTIONS_OUT);
            mapper_admin_send_connected(admin, md, c->complement, -1);

            // Call local connection handler if it exists
            if (md->connection_cb)
                md->connection_cb(md, &link->props, c->complement->parent->signal,
                                  &c->complement->props, MDEV_LOCAL_MODIFIED,
                                  md->connection_cb_userdata);
        }
    }
    return 0;
}

/*! Disconnect two signals. */
static int handler_signal_disconnect(const char *path, const char *types,
                                     lo_arg **argv, int argc,
                                     lo_message msg, void *user_data)
{
    mapper_admin admin = (mapper_admin) user_data;
    mapper_device md = admin->device;
    mapper_signal sig;
    int skip_arg = 0;

    const char *local_signal_name, *local_name;
    const char *remote_signal_name, *remote_name;

    if (argc < 2)
        return 0;

    if (types[0] != 's' && types[0] != 'S' && types[1] != 's'
        && types[1] != 'S')
        return 0;

    if (argc > 2 && (types[2] == 's' || types[2] == 'S')) {
        // check for string "->" or "<-" in argv[1]
        if ((strcmp(&argv[1]->s, "<-") == 0) || (strcmp(&argv[1]->s, "->") == 0))
            skip_arg = 1;
    }

    if (prefix_cmp(&argv[0]->s, mdev_name(md), &local_signal_name) == 0) {
        local_name = &argv[0]->s;
        remote_name = &argv[1+skip_arg]->s;
    }
    else if (prefix_cmp(&argv[1+skip_arg]->s, mdev_name(md), &local_signal_name) == 0) {
        local_name = &argv[1+skip_arg]->s;
        remote_name = &argv[0]->s;
    }
    else {
        trace("<%s> ignoring /disconnect %s -> %s\n", mdev_name(md),
              &argv[0]->s, &argv[1+skip_arg]->s);
        return 0;
    }

    remote_signal_name = strchr(remote_name+1, '/');

    if (!remote_signal_name) {
        trace("<%s> destination '%s' has no parameter in /disconnect.\n",
              mdev_name(md), remote_name);
        return 0;
    }

    if (!(sig=mdev_get_signal_by_name(md, local_signal_name, 0)))
    {
        trace("<%s> no signal found for '%s' in /disconnect\n",
              mdev_name(md), local_name);
        return 0;
    }

    mapper_link link = mapper_router_find_link_by_remote_name(md->router,
                                                              remote_name);
    if (!link) {
        trace("<%s> ignoring /disconnect, no link found for '%s'\n",
              mdev_name(md), remote_name);
        return 0;
    }

    mapper_connection c =
        mapper_router_find_connection(md->router, sig, remote_signal_name);
    if (!c) {
        trace("<%s> ignoring /disconnect, "
              "no connection found for '%s' -> '%s'\n",
              mdev_name(md), &argv[0]->s, &argv[1+skip_arg]->s);
        return 0;
    }
    if (link->self_link) {
        // Call local connection handler if it exists
        if (md->connection_cb)
            md->connection_cb(md, &link->props, sig, &c->complement->props,
                              MDEV_LOCAL_DESTROYED, md->connection_cb_userdata);

        /* The connection is removed. */
        if (mapper_router_remove_connection(md->router, c->complement)) {
            return 0;
        }
    }

    // Call local connection handler if it exists
    if (md->connection_cb)
        md->connection_cb(md, &link->props, sig, &c->props, MDEV_LOCAL_DESTROYED,
                          md->connection_cb_userdata);

    /* The connection is removed. */
    if (mapper_router_remove_connection(md->router, c)) {
        return 0;
    }

    if (!link->self_link) {
        // Inform remote device
        mapper_admin_set_bundle_dest_mesh(admin, link->admin_addr);
        mapper_admin_bundle_message(admin, ADM_DISCONNECTED, 0, "sss",
                                    &argv[0]->s, "->", &argv[1+skip_arg]->s);
    }
    // Inform subscribers
    mapper_admin_set_bundle_dest_subscribers(admin, SUB_DEVICE_CONNECTIONS_OUT);
    mapper_admin_bundle_message(admin, ADM_DISCONNECTED, 0, "sss",
                                &argv[0]->s, "->", &argv[1+skip_arg]->s);

    return 0;
}

/*! Respond to /disconnected by removing connection from database. */
static int handler_signal_disconnected(const char *path, const char *types,
                                       lo_arg **argv, int argc,
                                       lo_message msg, void *user_data)
{
    mapper_admin admin = (mapper_admin) user_data;
    mapper_monitor mon = admin->monitor;
    int skip_arg = 0;

    if (argc < 2)
        return 0;

    if (types[0] != 's' && types[0] != 'S' && types[1] != 's'
        && types[1] != 'S')
        return 0;

    if (argc > 2 && (types[2] == 's' || types[2] == 'S')) {
        // check for string "->" or "<-" in argv[1]
        if ((strcmp(&argv[1]->s, "<-") == 0) || (strcmp(&argv[1]->s, "->") == 0))
            skip_arg = 1;
    }

    const char *src_name = &argv[0]->s;
    const char *dest_name = &argv[1+skip_arg]->s;

    mapper_db db = mapper_monitor_get_db(mon);

    trace("<monitor> got /disconnected %s %s\n", src_name, dest_name);

    mapper_db_remove_connection(db,
        mapper_db_get_connection_by_signal_full_names(db, src_name, dest_name));

    return 0;
}

/*! When the /combine message is received by the destination device,
 *  construct or modify the signal's combiner. */
static int handler_signal_combine(const char *path, const char *types,
                                  lo_arg **argv, int argc, lo_message msg,
                                  void *user_data)
{
    mapper_admin admin = (mapper_admin) user_data;
    mapper_device md = admin->device;
    mapper_signal sig;
    const char *signal_name;

    if (argc < 3)
        return 0;

    if (types[0] != 's' && types[0] != 'S')
        return 0;

    if (!prefix_cmp(&argv[0]->s, mdev_name(md), &signal_name) == 0) {
        trace("<%s> ignoring /combine %s\n", mdev_name(md), &argv[0]->s);
        return 0;
    }

    sig = mdev_get_signal_by_name(md, signal_name, 0);
    if (!sig) {
        trace("<%s> no matching signal found for '%s' in /combine\n",
              mdev_name(md), signal_name);
        return 0;
    }

    trace("<%s> found matching signal '%s' in /combine\n",
          mdev_name(md), signal_name);

    mapper_message_t params;
    // extract arguments from /combine if any
    mapper_msg_parse_params(&params, path, &types[1], argc-1, &argv[1]);

    mapper_combiner c = mapper_router_find_combiner(md->router, sig);
    if (!c)
        c = mapper_router_add_combiner(md->router, sig, 0);
    if (mapper_combiner_set_from_message(c, &params)) {
        // Inform subscribers
        mapper_admin_set_bundle_dest_subscribers(admin, SUB_DEVICE_CONNECTIONS_IN);
        mapper_admin_send_combined(admin, md, c);
    }

    return 0;
}

/*! Respond to /combined by adding combiner to database. */
static int handler_signal_combined(const char *path, const char *types,
                                   lo_arg **argv, int argc,
                                   lo_message msg, void *user_data)
{
//    mapper_admin admin = (mapper_admin) user_data;
//    mapper_monitor mon = admin->monitor;

    if (!argc)
        return 0;

    if (types[0] != 's' && types[0] != 'S')
        return 0;

    const char *sig_name = &argv[0]->s;

//    mapper_db db = mapper_monitor_get_db(mon);

    trace("<monitor> got /combined %s + %i arguments.\n", sig_name, argc-1);

    if (argc == 1) {
//        mapper_db_remove_combiner(db, sig_name);
    }
    else {
        mapper_message_t params;
        // extract arguments from /combine if any
        mapper_msg_parse_params(&params, path, &types[1], argc-1, &argv[1]);
//        mapper_db_add_or_update_combiner_params(db, sig_name, &params);
    }

    return 0;
}

static int handler_device_link_ping(const char *path,
                                    const char *types,
                                    lo_arg **argv, int argc,
                                    lo_message msg, void *user_data)
{
    mapper_admin admin = (mapper_admin) user_data;
    mapper_device md = admin->device;
    mapper_clock_t *clock = &admin->clock;

    if (!md)
        return 0;

    mapper_timetag_t now;
    mapper_clock_now(clock, &now);
    lo_timetag then = lo_message_get_timestamp(msg);

    mapper_link link = mapper_router_find_link_by_remote_hash(md->router,
                                                              argv[0]->i);
    if (link) {
        if (argv[2]->i == link->clock.sent.message_id) {
            // total elapsed time since ping sent
            double elapsed = mapper_timetag_difference(now, link->clock.sent.timetag);
            // assume symmetrical latency
            double latency = (elapsed - argv[3]->d) * 0.5;
            // difference between remote and local clocks (latency compensated)
            double offset = mapper_timetag_difference(now, then) - latency;

            if (latency < 0) {
                trace("error: latency cannot be < 0");
                return 0;
            }

            if (link->clock.new == 1) {
                link->clock.offset = offset;
                link->clock.latency = latency;
                link->clock.jitter = 0;
                link->clock.new = 0;
            }
            else {
                link->clock.jitter = (link->clock.jitter * 0.9
                                      + fabs(link->clock.latency - latency) * 0.1);
                if (offset > link->clock.offset) {
                    // remote timetag is in the future
                    link->clock.offset = offset;
                }
                else if (latency < link->clock.latency + link->clock.jitter
                         && latency > link->clock.latency - link->clock.jitter) {
                    link->clock.offset = link->clock.offset * 0.9 + offset * 0.1;
                    link->clock.latency = link->clock.latency * 0.9 + latency * 0.1;
                }
            }
        }

        // update sync status
        mapper_timetag_cpy(&link->clock.response.timetag, now);
        link->clock.response.message_id = argv[1]->i;
    }
    return 0;
}

static int handler_sync(const char *path,
                        const char *types,
                        lo_arg **argv, int argc,
                        lo_message msg, void *user_data)
{
    mapper_admin admin = (mapper_admin) user_data;
    mapper_monitor mon = admin->monitor;

    if (!mon || !argc)
        return 0;

    mapper_db_device reg = 0;
    if (types[0] == 's' || types[0] == 'S') {
        if ((reg = mapper_db_get_device_by_name(&mon->db, &argv[0]->s)))
            mapper_timetag_cpy(&reg->synced, lo_message_get_timestamp(msg));
        else if (mon->autosubscribe) {
            // only create device record after requesting more information
            mapper_monitor_subscribe(mon, &argv[0]->s, mon->autosubscribe, -1);
        }
    }
    else if (types[0] == 'i') {
        if ((reg = mapper_db_get_device_by_name_hash(&mon->db, argv[0]->i)))
            mapper_timetag_cpy(&reg->synced, lo_message_get_timestamp(msg));
    }

    return 0;
}
