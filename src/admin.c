
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

extern const char* prop_msg_strings[N_AT_PARAMS];

// set to 1 to force mesh comms to use admin bus instead for debugging
#define FORCE_ADMIN_TO_BUS      0

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

static int alphabetise_names(int num, lo_arg **names, int *order)
{
    if (!num || !names || !order)
        return 1;
    int i, j, result = 1;
    for (i = 0; i < num; i++)
        order[i] = i;
    for (i = 1; i < num; i++) {
        j = i-1;
        while (j >= 0 && ((result = strcmp(&names[order[j]]->s,
                                           &names[order[j+1]]->s)) > 0)) {
            int temp = order[j];
            order[j] = order[j+1];
            order[j+1] = temp;
            j--;
        }
        if (result == 0)
            return 1;
    }
    return 0;
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
    "/ping",                    /* ADM_PING */
    "/logout",                  /* ADM_LOGOUT */
    "/name/probe",              /* ADM_NAME_PROBE */
    "/name/registered",         /* ADM_NAME_REG */
    "/signal",                  /* ADM_SIGNAL */
    "/signal/removed",          /* ADM_SIGNAL_REMOVED */
    "%s/subscribe",             /* ADM_SUBSCRIBE */
    "%s/unsubscribe",           /* ADM_UNSUBSCRIBE */
    "/sync",                    /* ADM_SYNC */
    "/who",                     /* ADM_WHO */
};

/* Internal functions for sending admin messages. */
static void mapper_admin_send_device(mapper_admin admin, mapper_device device);
static void mapper_admin_send_connection(mapper_admin admin, mapper_device md,
                                         mapper_connection c, int slot, int cmd);

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
static int handler_device_ping(const char *, const char *, lo_arg **, int,
                               lo_message, void *);
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
static int handler_sync(const char *, const char *, lo_arg **,
                        int, lo_message, void *);

/* Handler <-> Message relationships */
struct handler_method_assoc {
    int str_index;
    char *types;
    lo_method_handler h;
};

// handlers needed by "devices"
static struct handler_method_assoc device_handlers[] = {
    {ADM_CONNECT,               NULL,       handler_signal_connect},
    {ADM_CONNECT_TO,            NULL,       handler_signal_connect_to},
    {ADM_CONNECTION_MODIFY,     NULL,       handler_signal_connection_modify},
    {ADM_DISCONNECT,            NULL,       handler_signal_disconnect},
    {ADM_LOGOUT,                NULL,       handler_logout},
    {ADM_SUBSCRIBE,             NULL,       handler_device_subscribe},
    {ADM_UNSUBSCRIBE,           NULL,       handler_device_unsubscribe},
    {ADM_WHO,                   NULL,       handler_who},
    {ADM_CONNECTED,             NULL,       handler_signal_connected},
    {ADM_DEVICE,                NULL,       handler_device},
    {ADM_PING,                  "iiid",     handler_device_ping},
};
const int N_DEVICE_HANDLERS =
    sizeof(device_handlers)/sizeof(device_handlers[0]);

// handlers needed by "monitors"
static struct handler_method_assoc monitor_handlers[] = {
    {ADM_CONNECTED,             NULL,       handler_signal_connected},
    {ADM_DEVICE,                NULL,       handler_device},
    {ADM_DISCONNECTED,          NULL,       handler_signal_disconnected},
    {ADM_LOGOUT,                NULL,       handler_logout},
    {ADM_SYNC,                  NULL,       handler_sync},
    {ADM_SIGNAL,                NULL,       handler_signal_info},
    {ADM_SIGNAL_REMOVED,        "s",        handler_signal_removed},
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

static void mapper_admin_add_device_methods(mapper_admin admin,
                                            mapper_device device)
{
    int i;
    char fullpath[256];
    for (i=0; i < N_DEVICE_HANDLERS; i++)
    {
        snprintf(fullpath, 256,
                 admin_msg_strings[device_handlers[i].str_index],
                 mdev_name(admin->device));
        lo_server_add_method(admin->bus_server, fullpath,
                             device_handlers[i].types,
                             device_handlers[i].h,
                             admin);
#if !FORCE_ADMIN_TO_BUS
        lo_server_add_method(admin->mesh_server, fullpath,
                             device_handlers[i].types,
                             device_handlers[i].h,
                             admin);
#endif
    }
}

static void mapper_admin_add_monitor_methods(mapper_admin admin)
{
    int i;
    for (i=0; i < N_MONITOR_HANDLERS; i++)
    {
        lo_server_add_method(admin->bus_server,
                             admin_msg_strings[monitor_handlers[i].str_index],
                             monitor_handlers[i].types,
                             monitor_handlers[i].h,
                             admin);
#if !FORCE_ADMIN_TO_BUS
        lo_server_add_method(admin->mesh_server,
                             admin_msg_strings[monitor_handlers[i].str_index],
                             monitor_handlers[i].types,
                             monitor_handlers[i].h,
                             admin);
#endif
    }
}

static void mapper_admin_remove_monitor_methods(mapper_admin admin)
{
    int i, j;
    for (i=0; i < N_MONITOR_HANDLERS; i++)
    {
        // make sure method isn't also used by a device
        if (admin->device) {
            int found = 0;
            for (j=0; j < N_DEVICE_HANDLERS; j++) {
                if (monitor_handlers[i].str_index == device_handlers[j].str_index) {
                    found = 1;
                    break;
                }
            }
            if (found)
                continue;
        }
        lo_server_del_method(admin->bus_server,
                             admin_msg_strings[monitor_handlers[i].str_index],
                             monitor_handlers[i].types);
#if !FORCE_ADMIN_TO_BUS
        lo_server_del_method(admin->mesh_server,
                             admin_msg_strings[monitor_handlers[i].str_index],
                             monitor_handlers[i].types);
#endif
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
    lo_send(admin->bus_addr, admin_msg_strings[ADM_NAME_PROBE], "si",
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
        lo_server_add_method(admin->bus_server, admin_msg_strings[ADM_NAME_PROBE],
                             NULL, handler_device_name_probe, admin);
        lo_server_add_method(admin->bus_server, admin_msg_strings[ADM_NAME_REG],
                             NULL, handler_device_name_registered, admin);

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
        if (link->props.name_hash == md->props.name_hash
            || !link->props.host) {
            // don't bother sending pings to self or if link not ready
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
                      link->props.name, elapsed);
                // tentatively mark link as expired
                sync->response.message_id = -1;
                sync->response.timetag.sec = clock->now.sec;
            }
            else {
                trace("<%s> Removing link to unresponsive device %s "
                      "(%d seconds since warning).\n", mdev_name(md),
                      link->props.name, elapsed);
                // TODO: release related connections, call local handlers and inform subscribers

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
            lo_bundle_add_message(b, admin_msg_strings[ADM_PING], m);
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
            lo_send(admin->bus_addr, admin_msg_strings[ADM_NAME_REG],
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
    mapper_admin_bundle_message(
        admin, ADM_SIGNAL, 0, "s", sig_name,
        AT_DIRECTION, sig,
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

// Send /connected message
static void mapper_admin_send_connection(mapper_admin admin, mapper_device md,
                                         mapper_connection c, int slot, int cmd)
{
    if (cmd == ADM_CONNECTED && c->status < MAPPER_READY)
        return;

    lo_message m = lo_message_new();
    if (!m) {
        trace("couldn't allocate lo_message\n");
        return;
    }

    char dest_name[1024], source_names[1024];
    snprintf(dest_name, 1024, "%s%s", c->destination.props->device_name,
             c->destination.props->signal_name);

    if (c->destination.props->direction == DI_INCOMING) {
        // add connection destination
        lo_message_add_string(m, dest_name);
        lo_message_add_string(m, "<-");
    }

    // add connection sources
    if (slot >= 0) {
        snprintf(source_names, 1024, "%s%s", c->sources[slot].props->device_name,
                 c->sources[slot].props->signal_name);
        lo_message_add_string(m, source_names);
    }
    else {
        int i, len = 0, result;
        for (i = 0; i < c->props.num_sources; i++) {
            result = snprintf(&source_names[len], 1024-len, "%s%s",
                              c->sources[i].props->device_name,
                              c->sources[i].props->signal_name);
            if (result < 0 || (len + result + 1) >= 1024) {
                trace("Error encoding sources for combined /connected msg");
                lo_message_free(m);
                return;
            }
            lo_message_add_string(m, &source_names[len]);
            len += result + 1;
        }
    }

    if (c->destination.props->direction == DI_OUTGOING) {
        // add connection destination
        lo_message_add_string(m, "->");
        lo_message_add_string(m, dest_name);
    }

    if (cmd >= ADM_DISCONNECT) {
        lo_bundle_add_message(admin->bundle, admin_msg_strings[cmd], m);
        return;
    }

    if (cmd == ADM_CONNECT_TO && c->destination.local) {
        // include "extra" signal metadata
        mapper_signal sig = c->destination.local->signal;
        if (sig->props.extra)
            mapper_msg_add_value_table(m, sig->props.extra);
    }

    // add other properties
    mapper_connection_prepare_osc_message(m, c, slot);

    lo_bundle_add_message(admin->bundle, admin_msg_strings[cmd], m);
    // TODO: should send bundle here since message refers to generated strings
//    mapper_admin_send_bundle(admin);
}

static void mapper_admin_send_connections_in(mapper_admin admin,
                                             mapper_device md,
                                             int min, int max)
{
    int i, count = 0;
    mapper_router_signal rs = md->router->signals;
    while (rs) {
        for (i = 0; i < rs->num_incoming_connections; i++) {
            if (!rs->incoming_connections[i])
                continue;
            if (max > 0 && count > max)
                return;
            if (count >= min) {
                mapper_admin_send_connection(admin, md,
                                             rs->incoming_connections[i],
                                             -1, ADM_CONNECTED);
            }
            count++;
        }
        rs = rs->next;
    }
}

static void mapper_admin_send_connections_out(mapper_admin admin,
                                              mapper_device md,
                                              int min, int max)
{
    int i, count = 0;
    mapper_router_signal rs = md->router->signals;
    while (rs) {
        for (i = 0; i < rs->num_outgoing_slots; i++) {
            if (!rs->outgoing_slots[i])
                continue;
            if (max > 0 && count > max)
                return;
            if (count >= min) {
                mapper_connection c = rs->outgoing_slots[i]->connection;
                mapper_admin_send_connection(admin, md, c, -1, ADM_CONNECTED);
            }
            count++;
        }
        rs = rs->next;
    }
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
    mapper_db db = mmon_get_db(mon);
    mapper_device md = admin->device;
    int i, j;

    if (argc < 1)
        return 0;

    if (types[0] != 's' && types[0] != 'S')
        return 0;

    const char *name = &argv[0]->s;
    lo_address a = lo_message_get_source(msg);

    mapper_message_t params;
    mapper_msg_parse_params(&params, path, &types[1],
                            argc-1, &argv[1]);

    mapper_clock_now(&admin->clock, &admin->clock.now);
    if (mon) {
        trace("<monitor> got /device %s + %i arguments\n", name, argc-1);
        mapper_db_add_or_update_device_params(db, name, &params, &admin->clock.now);
    }

    if (!md)
        return 0;

    if (strcmp(&argv[0]->s, mdev_name(md))) {
        trace("<%s> got /device %s\n", mdev_name(md), &argv[0]->s);
    }
    else {
        // ignore own messages
        trace("<%s> ignoring /device %s\n", mdev_name(md), &argv[0]->s);
        return 0;
    }
    // Discover whether the device is linked.
    mapper_link link =
        mapper_router_find_link_by_remote_name(md->router, name);
    if (!link) {
        trace("<%s> ignoring /device '%s', no link.\n", mdev_name(md), name);
        return 0;
    }
    else if (link->props.host) {
        // already have metadata, can ignore this message
        trace("<%s> ignoring /device '%s', link already set.\n",
              mdev_name(md), name);
        return 0;
    }
    if (!a) {
        trace("can't perform /linkTo, address unknown\n");
        return 0;
    }
    // Find the sender's hostname
    const char *host = lo_address_get_hostname(a);
    const char *admin_port = lo_address_get_port(a);
    if (!host) {
        trace("can't perform /linkTo, host unknown\n");
        return 0;
    }
    // Retrieve the port
    int data_port;
    if (mapper_msg_get_param_if_int(&params, AT_PORT, &data_port)) {
        trace("can't perform /linkTo, port unknown\n");
        return 0;
    }

    mapper_router_update_link(md->router, link, host,
                              atoi(admin_port), data_port);
    trace("<%s> activated router to %s -> host: %s, port: %d\n",
          mdev_name(md), name, host, data_port);

    // check if we have connections waiting for this link
    mapper_router_signal rs = md->router->signals;
    while (rs) {
        for (i = 0; i < rs->num_incoming_connections; i++) {
            if (!rs->incoming_connections[i])
                continue;
            mapper_connection c = rs->incoming_connections[i];
            if (c->one_source && c->sources[0].link == link) {
                mapper_admin_set_bundle_dest_bus(admin);
                mapper_admin_send_connection(admin, md, c, -1, ADM_CONNECT_TO);
            }
            else {
                for (j = 0; j < c->props.num_sources; j++) {
                    if (c->sources[j].link == link) {
                        mapper_admin_set_bundle_dest_mesh(admin, link->admin_addr);
                        mapper_admin_send_connection(admin, md, c, j, ADM_CONNECT_TO);
                    }
                }
            }
        }
        for (i = 0; i < rs->num_outgoing_slots; i++) {
            if (!rs->outgoing_slots[i])
                continue;
            mapper_connection c = rs->outgoing_slots[i]->connection;
            // only send /connectTo once even if we have multiple local sources
            if (c->one_source && (rs->outgoing_slots[i] != &c->sources[0]))
                    continue;
            if (c->destination.link == link) {
                mapper_admin_set_bundle_dest_mesh(admin, link->admin_addr);
                mapper_admin_send_connection(admin, md, c, -1, ADM_CONNECT_TO);
            }
        }
        rs = rs->next;
    }
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
    mapper_db db = mmon_get_db(mon);
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
        mmon_unsubscribe(mon, name);
    }

    // If device exists and is registered
    if (md && md->ordinal.locked) {
        // Check if we have any links to this device, if so remove them
        mapper_link link =
            mapper_router_find_link_by_remote_name(md->router, name);
        if (link) {
            // TODO: release connections, call local handlers and inform subscribers
            trace("<%s> Removing link to expired device %s.\n",
                  mdev_name(md), link->props.name);

            mapper_router_remove_link(md->router, link);
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

    if (!(*s) && timeout_seconds) {
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
    mapper_admin_set_bundle_dest_mesh(admin, address);
    if (flags)
        mapper_admin_send_device(admin, admin->device);
    if (flags & SUBSCRIBE_DEVICE_INPUTS)
        mapper_admin_send_inputs(admin, admin->device, -1, -1);
    if (flags & SUBSCRIBE_DEVICE_OUTPUTS)
        mapper_admin_send_outputs(admin, admin->device, -1, -1);
    if (flags & SUBSCRIBE_CONNECTIONS_IN)
        mapper_admin_send_connections_in(admin, admin->device, -1, -1);
    if (flags & SUBSCRIBE_CONNECTIONS_OUT)
        mapper_admin_send_connections_out(admin, admin->device, -1, -1);
    mapper_admin_send_bundle(admin);
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
            flags = SUBSCRIBE_ALL;
        else if (strcmp(&argv[i]->s, "device")==0)
            flags |= SUBSCRIBE_DEVICE;
        else if (strcmp(&argv[i]->s, "signals")==0)
            flags |= SUBSCRIBE_DEVICE_SIGNALS;
        else if (strcmp(&argv[i]->s, "inputs")==0)
            flags |= SUBSCRIBE_DEVICE_INPUTS;
        else if (strcmp(&argv[i]->s, "outputs")==0)
            flags |= SUBSCRIBE_DEVICE_OUTPUTS;
        else if (strcmp(&argv[i]->s, "connections")==0)
            flags |= SUBSCRIBE_CONNECTIONS;
        else if (strcmp(&argv[i]->s, "connections_in")==0)
            flags |= SUBSCRIBE_CONNECTIONS_IN;
        else if (strcmp(&argv[i]->s, "connections_out")==0)
            flags |= SUBSCRIBE_CONNECTIONS_OUT;
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
    mapper_db db = mmon_get_db(mon);

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
    mapper_db db = mmon_get_db(mon);

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
            lo_send(admin->bus_addr, admin_msg_strings[ADM_NAME_REG],
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

/* Basic description of the connection protocol:
 *      The message "/connect" "<signalA>" "<signalB>" starts the protocol.
 *
 *      If a device doesn't already have a record for the remote device it will
 *      request this information with the "/who" message.
 *
 *      "/connectTo" messages are sent between devices until each is satisfied
 *      that it has enough information to initialize the connection; at this
 *      point each device will send the message "/connected" to its peer.
 *
 *      The "/connection/modify" message is used to change the properties of
 *      existing connections. The device administering the connection will
 *      make appropriate changes and then send "/connected" to its peer.
 *
 *      Negotiation of convergent ("many-to-one") connections is governed by
 *      the destination device; if the connection involves multiple inputs the
 *      destination will provoke the creation of simple subconnections from the
 *      various sources and perform any combination signal-processing,
 *      otherwise processing metadata is forwarded to the source device.
 *      A convergent connection is started with the message:
 *      "/connect" "<sourceA>" "<sourceB>" ... "<sourceN>" "->" "<destination>"
 */

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

    int result = strncmp(str1, str2, n1);
    if (!result && rest)
        *rest = s1;

    return result;
}

static int parse_signal_names(const char *types, lo_arg **argv, int argc,
                              int *src_index, int *dest_index, int *param_index)
{
    // old protocol: /connect src dest
    // new protocol: /connect src1 src2 ... srcN -> dest
    //               /connect dest <- src1 src2 ... srcN
    // if we find "->" or "<-" before '@' or end of args, count sources
    int i, num_sources = 0;
    if (strncmp(types, "sss", 3))
        return 0;

    if (strcmp(&argv[1]->s, "<-") == 0) {
        *src_index = 2;
        *dest_index = 0;
        if (argc > 2) {
            i = 2;
            while (i < argc && (types[i] == 's' || types[i] == 'S')) {
                if ((&argv[i]->s)[0] == '@')
                    break;
                else
                    num_sources++;
                i++;
            }
        }
        if (param_index)
            *param_index = *src_index + num_sources;
    }
    else {
        *src_index = 0;
        *dest_index = 1;
        i = 1;
        while (i < argc && (types[i] == 's' || types[i] == 'S')) {
            if ((&argv[i]->s)[0] == '@')
                break;
            else if ((strcmp(&argv[i]->s, "->") == 0)
                     && argc > (i+1)
                     && (types[i+1] == 's' || types[i+1] == 'S')
                     && (&argv[i+1]->s)[0] != '@') {
                num_sources = i;
                *dest_index = i+1;
                break;
            }
            i++;
        }
        if (param_index)
            *param_index = *dest_index+1;
    }
    return num_sources;
}

/*! When the /connect message is received by the destination device,
 *  send a connectTo message to the source device. */
static int handler_signal_connect(const char *path, const char *types,
                                  lo_arg **argv, int argc, lo_message msg,
                                  void *user_data)
{
    mapper_admin admin = (mapper_admin) user_data;
    mapper_device md = admin->device;
    mapper_signal local_signal = 0;
    int i, num_sources, src_index, dest_index, num_params, param_index;

    const char *local_signal_name = 0;

    num_sources = parse_signal_names(types, argv, argc, &src_index,
                                     &dest_index, &param_index);
    if (!num_sources)
        return 0;

    // check if we are the destination of this connection
    if (prefix_cmp(&argv[dest_index]->s, mdev_name(md), &local_signal_name)==0) {
        local_signal = mdev_get_signal_by_name(md, local_signal_name, 0);
        if (!local_signal) {
            trace("<%s> no signal found with name '%s'.\n",
                  mdev_name(md), local_signal_name);
            return 0;
        }
    }

#ifdef DEBUG
    printf("-- <%s> %s /connect", mdev_name(md),
           local_signal ? "got" : "ignoring");
    if (src_index)
        printf(" %s <-", &argv[dest_index]->s);
    for (i = 0; i < num_sources; i++)
        printf(" %s", &argv[src_index+i]->s);
    if (!src_index)
        printf(" -> %s", &argv[dest_index]->s);
    printf("\n");
#endif
    if (!local_signal) {
        return 0;
    }

    // ensure names are in alphabetical order
    int order[num_sources];
    if (alphabetise_names(num_sources, &argv[src_index], order)) {
        trace("error in /connect: multiple use of source signal.");
        return 0;
    }
    const char *src_names[num_sources];
    for (i = 0; i < num_sources; i++) {
        src_names[i] = &argv[src_index+order[i]]->s;
    }
    mapper_connection c;
    c = mapper_router_find_incoming_connection(md->router, local_signal,
                                               num_sources, src_names);

    /* If a connection connection already exists between these signals,
     * forward the message to handler_signal_connection_modify() and stop. */
    if (c) {
        handler_signal_connection_modify(path, types, argv, argc, msg, user_data);
        return 0;
    }

    // parse arguments from /connect if any
    mapper_message_t params;
    num_params = mapper_msg_parse_params(&params, path, &types[dest_index+1],
                                         argc-dest_index-1, &argv[dest_index+1]);

    // check if any sources are local
    const char *src_signal_name;
    mapper_signal src_signals[num_sources];
    for (i = 0; i < num_sources; i++) {
        if (prefix_cmp(src_names[i], mdev_name(md), &src_signal_name)==0) {
            // this source is a local signal
            src_signals[i] = mdev_get_signal_by_name(md, src_signal_name, 0);
        }
        else
            src_signals[i] = 0;
    }

    // create a tentative connection (flavourless)
    c = mapper_router_add_connection(md->router, local_signal, num_sources,
                                     src_signals, src_names, DI_INCOMING);

    /* Set its properties, but don't allow overwriting local type and length */
    params.values[AT_TYPE] = 0;
    params.values[AT_LENGTH] = 0;
    params.values[AT_SRC_TYPE] = 0;
    params.values[AT_SRC_LENGTH] = 0;
    params.values[AT_DEST_TYPE] = 0;
    params.values[AT_DEST_LENGTH] = 0;
    mapper_connection_set_from_message(c, &params, order, 1);

    if (c->status == MAPPER_READY) {
        // This connection only references local signals, advance to "connected"

        // TODO: move num_connections_in/out prop to device instead of link
        c->status = MAPPER_ACTIVE;
        c->destination.link->props.num_connections_out++;
        c->destination.link->props.num_connections_in++;

        // Inform subscribers
        if (admin->subscribers) {
            mapper_admin_set_bundle_dest_subscribers(admin, SUBSCRIBE_CONNECTIONS_IN);
            mapper_admin_send_connection(admin, md, c, -1, ADM_CONNECTED);
        }

        // Call local connection handler if it exists
        if (md->connection_cb) {
            for (i = 0; i < c->props.num_sources; i++) {
                md->connection_cb(md, c->sources[i].local->signal, &c->props,
                                  c->sources[i].props, MDEV_LOCAL_ESTABLISHED,
                                  md->connection_cb_userdata);
            }
            md->connection_cb(md, c->destination.local->signal, &c->props,
                              c->destination.props, MDEV_LOCAL_ESTABLISHED,
                              md->connection_cb_userdata);
        }
        return 0;
    }

    if (c->one_source && !c->sources[0].local
        && c->sources[0].props->device->host) {
        mapper_admin_set_bundle_dest_bus(admin);
        mapper_admin_send_connection(admin, md, c, -1, ADM_CONNECT_TO);
    }
    else {
        for (i = 0; i < num_sources; i++) {
            // do not send if is local connection
            if (c->sources[i].local)
                continue;
            // do not send if device host/port not yet known
            if (!c->sources[i].link || !c->sources[i].link->admin_addr)
                continue;
            mapper_admin_set_bundle_dest_bus(admin);
            mapper_admin_send_connection(admin, md, c, i, ADM_CONNECT_TO);
        }
    }
    return 0;
}

/*! When the /connectTo message is received by a peer device, create
 *  tentative connection and respond with own signal metadata. */
static int handler_signal_connect_to(const char *path, const char *types,
                                     lo_arg **argv, int argc,
                                     lo_message msg, void *user_data)
{
    mapper_admin admin = (mapper_admin) user_data;
    mapper_device md = admin->device;
    mapper_signal local_signal = 0;
    mapper_connection c;
    int i, num_sources, src_index, dest_index, num_params, param_index;

    const char *local_signal_name = 0;

    num_sources = parse_signal_names(types, argv, argc, &src_index,
                                     &dest_index, &param_index);
    if (!num_sources)
        return 0;

    // check if we are an endpoint of this connection
    if (!src_index) {
        // check if we are the destination
        if (prefix_cmp(&argv[dest_index]->s, mdev_name(md),
                       &local_signal_name)==0) {
            local_signal = mdev_get_signal_by_name(md, local_signal_name, 0);
            if (!local_signal) {
                trace("<%s> no signal found with name '%s'.\n",
                      mdev_name(md), local_signal_name);
                return 0;
            }
        }
    }
    else {
        // check if we are a source – all sources must match!
        for (i = 0; i < num_sources; i++) {
            if (prefix_cmp(&argv[src_index+i]->s, mdev_name(md),
                           &local_signal_name)) {
                local_signal = 0;
                break;
            }
            local_signal = mdev_get_signal_by_name(md, local_signal_name, 0);
            if (!local_signal) {
                trace("<%s> no signal found with name '%s'.\n",
                      mdev_name(md), local_signal_name);
                break;
            }
        }
    }

#ifdef DEBUG
    printf("-- <%s> %s /connectTo", mdev_name(md),
           local_signal ? "got" : "ignoring");
    if (src_index)
        printf(" %s <-", &argv[dest_index]->s);
    for (i = 0; i < num_sources; i++)
        printf(" %s", &argv[src_index+i]->s);
    if (!src_index)
        printf(" -> %s", &argv[dest_index]->s);
    printf("\n");
#endif
    if (!local_signal) {
        return 0;
    }

    mapper_message_t params;
    num_params = mapper_msg_parse_params(&params, path, &types[param_index],
                                         argc-param_index, &argv[param_index]);

    if (!params.lengths[AT_ID] || *params.types[AT_ID] != 'i') {
        trace("<%s> ignoring /connectTo, no 'id' property.\n", mdev_name(md));
        return 0;
    }
    int id = (*params.values[AT_ID])->i;

    if (src_index) {
        c = mapper_router_find_outgoing_connection_id(md->router, local_signal,
                                                      &argv[dest_index]->s, id);
    }
    else {
        c = mapper_router_find_incoming_connection_id(md->router, local_signal,
                                                      id);
    }

    int order[num_sources];
    if (alphabetise_names(num_sources, &argv[src_index], order)) {
        trace("error in /connect: multiple use of source signal.");
        return 0;
    }

    if (!c) {
        // ensure names are in alphabetical order
        const char *src_names[num_sources];
        for (i = 0; i < num_sources; i++) {
            src_names[i] = &argv[src_index+order[i]]->s;
        }
        if (src_index) {
            // organize source signals
            mapper_signal src_sigs[num_sources];
            for (i = 0; i < num_sources; i++) {
                src_sigs[i] = mdev_get_signal_by_name(md, strchr(src_names[i]+1, '/'), 0);
            }
            /* Add a flavourless connection */
            const char *dest_name = &argv[dest_index]->s;
            c = mapper_router_add_connection(md->router, local_signal,
                                             num_sources, src_sigs,
                                             &dest_name, DI_OUTGOING);
        }
        else {
            /* Add a flavourless connection */
            c = mapper_router_add_connection(md->router, local_signal, num_sources,
                                             0, src_names, DI_INCOMING);
        }
        if (!c) {
            trace("couldn't create mapper_connection in handler_signal_connect_to\n");
            return 0;
        }
    }
    else if (src_index && c->props.num_sources != num_sources) {
        trace("<%s> wrong num_sources in /connected.\n", mdev_name(md));
        return 0;
    }

    /* TODO: handle passing @numInstances prop from signals, set sensible
     * defaults for new connections. */

    /* Set connection properties. */
    mapper_connection_set_from_message(c, &params, order, 1);

    if (c->status == MAPPER_READY) {
        if (c->destination.props->direction == DI_OUTGOING) {
            mapper_admin_set_bundle_dest_mesh(admin,
                                              c->destination.link->admin_addr);
            mapper_admin_send_connection(admin, md, c, -1, ADM_CONNECTED);
        }
        else {
            if (c->one_source) {
                mapper_admin_set_bundle_dest_mesh(admin,
                                                  c->sources[0].link->admin_addr);
                mapper_admin_send_connection(admin, md, c, -1, ADM_CONNECTED);
            }
            else {
                for (i = 0; i < c->props.num_sources; i++) {
                    mapper_admin_set_bundle_dest_mesh(admin,
                                                      c->sources[i].link->admin_addr);
                    mapper_admin_send_connection(admin, md, c, i, ADM_CONNECTED);
                }
            }
        }
    }
    return 0;
}

/*! Monitors respond to /connected by storing connection in database. */
/*! Also used by devices to confirm connection to remote peers, and to share
 *  changes in connection properties. */
static int handler_signal_connected(const char *path, const char *types,
                                    lo_arg **argv, int argc, lo_message msg,
                                    void *user_data)
{
    mapper_admin admin = (mapper_admin) user_data;
    mapper_device md = admin->device;
    mapper_signal local_signal = 0;
    mapper_connection c;
    mapper_monitor mon = admin->monitor;
    int i, num_sources, src_index, dest_index, num_params, param_index, slot = 0;
    const char *local_signal_name;

    num_sources = parse_signal_names(types, argv, argc, &src_index,
                                     &dest_index, &param_index);
    if (!num_sources)
        return 0;

    int order[num_sources];
    if (alphabetise_names(num_sources, &argv[src_index], order)) {
        trace("error in /connect: multiple use of source signal.");
        return 0;
    }

    mapper_message_t params;
    num_params = mapper_msg_parse_params(&params, path, &types[param_index],
                            argc-param_index, &argv[param_index]);

    if (mon) {
#ifdef DEBUG
        printf("-- <monitor> got /connected");
        if (src_index)
            printf(" %s <-", &argv[dest_index]->s);
        for (i = 0; i < num_sources; i++)
            printf(" %s", &argv[src_index+i]->s);
        if (!src_index)
            printf(" -> %s", &argv[dest_index]->s);
        printf("\n");
#endif
        const char *src_names[num_sources];
        for (i = 0; i < num_sources; i++) {
            src_names[i] = &argv[src_index+order[i]]->s;
        }
        mapper_db db = mmon_get_db(mon);
        mapper_db_add_or_update_connection_params(db, num_sources, src_names,
                                                  &argv[dest_index]->s, &params);
    }

    if (!md || !mdev_name(md))
        return 0;

    // 2 scenarios: if message in form A -> B, only B interested
    //              if message in form A <- B, only B interested

    // check if we are an endpoint of this connection
    if (!src_index) {
        // check if we are the destination
        if (prefix_cmp(&argv[dest_index]->s, mdev_name(md),
                       &local_signal_name)==0) {
            local_signal = mdev_get_signal_by_name(md, local_signal_name, 0);
            if (!local_signal) {
                trace("<%s> no signal found with name '%s'.\n",
                      mdev_name(md), local_signal_name);
                return 0;
            }
        }
    }
    else {
        // check if we are a source – all sources must match!
        for (i = 0; i < num_sources; i++) {
            if (prefix_cmp(&argv[src_index+i]->s, mdev_name(md),
                           &local_signal_name)) {
                local_signal = 0;
                break;
            }
            local_signal = mdev_get_signal_by_name(md, local_signal_name, 0);
            if (!local_signal) {
                trace("<%s> no signal found with name '%s'.\n",
                      mdev_name(md), local_signal_name);
                break;
            }
        }
    }

#ifdef DEBUG
    printf("-- <%s> %s /connected", mdev_name(md),
           local_signal ? "got" : "ignoring");
    if (src_index)
        printf(" %s <-", &argv[dest_index]->s);
    for (i = 0; i < num_sources; i++)
        printf(" %s", &argv[src_index+i]->s);
    if (!src_index)
        printf(" -> %s", &argv[dest_index]->s);
    printf("\n");
#endif
    if (!local_signal) {
        return 0;
    }

    if (!params.lengths[AT_ID] || *params.types[AT_ID] != 'i') {
        trace("<%s> ignoring /connected, no 'id' property.\n", mdev_name(md));
        return 0;
    }
    int id = (*params.values[AT_ID])->i;

    if (src_index) {
        c = mapper_router_find_outgoing_connection_id(md->router, local_signal,
                                                      &argv[dest_index]->s, id);
    }
    else {
        c = mapper_router_find_incoming_connection_id(md->router, local_signal,
                                                      id);
    }
    if (!c) {
        trace("<%s> no connection found for /connected.\n", mdev_name(md));
        return 0;
    }
    if (src_index && c->props.num_sources != num_sources) {
        trace("<%s> wrong num_sources in /connected.\n", mdev_name(md));
        return 0;
    }

    if (c->is_local) {
        // no need to update since all properties are local
        return 0;
    }

    // TODO: if this endpoint is connection admin, do not allow overwiting props
    int updated = mapper_connection_set_from_message(c, &params, order, 0);

    if (c->status == MAPPER_READY) {
        // Inform remote peer(s)
        if (c->destination.props->direction == DI_OUTGOING) {
            mapper_admin_set_bundle_dest_mesh(admin,
                                              c->destination.link->admin_addr);
            mapper_admin_send_connection(admin, md, c, -1, ADM_CONNECTED);
        }
        else {
            if (c->one_source) {
                mapper_admin_set_bundle_dest_mesh(admin,
                                                  c->sources[0].link->admin_addr);
                mapper_admin_send_connection(admin, md, c, -1, ADM_CONNECTED);
            }
            else {
                for (i = 0; i < c->props.num_sources; i++) {
                    mapper_admin_set_bundle_dest_mesh(admin,
                                                      c->sources[i].link->admin_addr);
                    mapper_admin_send_connection(admin, md, c, i, ADM_CONNECTED);
                }
            }
        }
        updated++;
    }
    if (c->status >= MAPPER_READY && updated) {
        if (admin->subscribers) {
            // Inform subscribers
            mapper_admin_set_bundle_dest_subscribers(admin, SUBSCRIBE_CONNECTIONS_IN);
            mapper_admin_send_connection(admin, md, c, -1, ADM_CONNECTED);
        }

        // Call local connection handler if it exists
        if (md->connection_cb) {
            for (i = 0; i < c->props.num_sources; i++) {
                if (c->sources[i].local) {
                    md->connection_cb(md, c->sources[i].local->signal, &c->props,
                                      c->sources[i].props,
                                      c->status == MAPPER_ACTIVE
                                      ? MDEV_LOCAL_MODIFIED : MDEV_LOCAL_ESTABLISHED,
                                      md->connection_cb_userdata);
                }
            }
            if (c->destination.local) {
                md->connection_cb(md, c->destination.local->signal, &c->props,
                                  c->destination.props,
                                  c->status == MAPPER_ACTIVE
                                  ? MDEV_LOCAL_MODIFIED : MDEV_LOCAL_ESTABLISHED,
                                  md->connection_cb_userdata);
            }
        }

        if (c->destination.props->direction == DI_OUTGOING)
            c->destination.props->device->num_connections_out++;
        else
            c->sources[slot].props->device->num_connections_in++;
        c->status = MAPPER_ACTIVE;
    }

    return 0;
}

/*! Modify the connection properties : mode, range, expression,
 *  boundMin, boundMax, etc. */
static int handler_signal_connection_modify(const char *path, const char *types,
                                            lo_arg **argv, int argc, lo_message msg,
                                            void *user_data)
{
    mapper_admin admin = (mapper_admin) user_data;
    mapper_device md = admin->device;
    mapper_connection c = 0;
    mapper_signal local_signal = 0;
    int i, num_sources, src_index, dest_index, num_params, param_index;
    const char *local_signal_name;

    if (argc < 4)
        return 0;

    num_sources = parse_signal_names(types, argv, argc, &src_index,
                                     &dest_index, &param_index);
    if (!num_sources)
        return 0;

    int order[num_sources];
    if (alphabetise_names(num_sources, &argv[src_index], order)) {
        trace("error in /connect: multiple use of source signal.");
        return 0;
    }
    const char *src_names[num_sources];
    for (i = 0; i < num_sources; i++) {
        src_names[i] = &argv[src_index+order[i]]->s;
    }

    // check if we are the destination
    if (prefix_cmp(&argv[dest_index]->s, mdev_name(md),
                   &local_signal_name)==0) {
        local_signal = mdev_get_signal_by_name(md, local_signal_name, 0);
        if (!local_signal) {
            trace("<%s> no signal found with name '%s'.\n",
                  mdev_name(md), local_signal_name);
            return 0;
        }
        c = mapper_router_find_incoming_connection(md->router, local_signal,
                                                   num_sources, src_names);
    }
    else {
        // check if we are a source – all sources must match!
        for (i = 0; i < num_sources; i++) {
            if (prefix_cmp(src_names[i], mdev_name(md),
                           &local_signal_name)) {
                local_signal = 0;
                break;
            }
            local_signal = mdev_get_signal_by_name(md, local_signal_name, 0);
            if (!local_signal) {
                trace("<%s> no signal found with name '%s'.\n",
                      mdev_name(md), local_signal_name);
                break;
            }
        }
        if (local_signal)
            c = mapper_router_find_outgoing_connection(md->router, local_signal,
                                                       num_sources, src_names,
                                                       &argv[dest_index]->s);
    }

#ifdef DEBUG
    printf("-- <%s> %s /connection/modify", mdev_name(md),
           c ? "got" : "ignoring");
    if (src_index)
        printf(" %s <-", &argv[dest_index]->s);
    for (i = 0; i < num_sources; i++)
        printf(" %s", &argv[src_index+i]->s);
    if (!src_index)
        printf(" -> %s", &argv[dest_index]->s);
    printf("\n");
#endif
    if (!local_signal) {
        return 0;
    }

    if (!c->is_admin) {
        trace("<%s> ignoring /connection/modify, slaved to remote device.\n",
              mdev_name(md));
        return 0;
    }

    mapper_message_t params;
    num_params = mapper_msg_parse_params(&params, path, &types[param_index],
                                         argc-param_index, &argv[param_index]);

    int updated = mapper_connection_set_from_message(c, &params, order, 1);

    // TODO: check for self-connections, don't need to send updates

    if (updated) {
        // TODO: may not need to inform all remote peers
        // Inform remote peer(s) of relevant changes
        if (!c->destination.local) {
            mapper_admin_set_bundle_dest_mesh(admin,
                                              c->destination.link->admin_addr);
            mapper_admin_send_connection(admin, md, c, -1, ADM_CONNECTED);
        }
        else {
            for (i = 0; i < c->props.num_sources; i++) {
                if (c->sources[i].local)
                    continue;
                mapper_admin_set_bundle_dest_mesh(admin,
                                                  c->sources[i].link->admin_addr);
                mapper_admin_send_connection(admin, md, c, i, ADM_CONNECTED);
            }
        }

        if (admin->subscribers) {
            // Inform subscribers
            if (c->destination.local)
                mapper_admin_set_bundle_dest_subscribers(admin,
                                                         SUBSCRIBE_CONNECTIONS_IN);
            else
                mapper_admin_set_bundle_dest_subscribers(admin,
                                                         SUBSCRIBE_CONNECTIONS_OUT);
            mapper_admin_send_connection(admin, md, c, -1, ADM_CONNECTED);
        }

        // Call local connection handler if it exists
        if (md->connection_cb) {
            for (i = 0; i < c->props.num_sources; i++) {
                if (c->sources[i].local)
                    md->connection_cb(md, c->sources[i].local->signal, &c->props,
                                      c->sources[i].props, MDEV_LOCAL_MODIFIED,
                                      md->connection_cb_userdata);
            }
            if (c->destination.local) {
                md->connection_cb(md, c->destination.local->signal, &c->props,
                                  c->destination.props, MDEV_LOCAL_MODIFIED,
                                  md->connection_cb_userdata);
            }
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
    mapper_signal local_signal;
    mapper_connection c = 0;
    int i, num_sources, src_index, dest_index;
    const char *local_signal_name;

    num_sources = parse_signal_names(types, argv, argc, &src_index,
                                     &dest_index, 0);
    if (!num_sources)
        return 0;

    int order[num_sources];
    if (alphabetise_names(num_sources, &argv[src_index], order)) {
        trace("error in /connect: multiple use of source signal.");
        return 0;
    }
    const char *src_names[num_sources];
    for (i = 0; i < num_sources; i++) {
        src_names[i] = &argv[src_index+order[i]]->s;
    }

    // check if we are the destination
    if (prefix_cmp(&argv[dest_index]->s, mdev_name(md),
                   &local_signal_name)==0) {
        local_signal = mdev_get_signal_by_name(md, local_signal_name, 0);
        if (!local_signal) {
            trace("<%s> no signal found with name '%s'.\n",
                  mdev_name(md), local_signal_name);
            return 0;
        }
        c = mapper_router_find_incoming_connection(md->router, local_signal,
                                                   num_sources, src_names);
    }
    else {
        // check if we are a source – all sources must match!
        for (i = 0; i < num_sources; i++) {
            if (prefix_cmp(src_names[i], mdev_name(md),
                           &local_signal_name)) {
                local_signal = 0;
                break;
            }
            local_signal = mdev_get_signal_by_name(md, local_signal_name, 0);
            if (!local_signal) {
                trace("<%s> no signal found with name '%s'.\n",
                      mdev_name(md), local_signal_name);
                break;
            }
        }
        if (local_signal)
            c = mapper_router_find_outgoing_connection(md->router, local_signal,
                                                       num_sources, src_names,
                                                       &argv[dest_index]->s);
    }

#ifdef DEBUG
    printf("-- <%s> %s /disconnect", mdev_name(md),
           c ? "got" : "ignoring");
    if (src_index)
        printf(" %s <-", &argv[dest_index]->s);
    for (i = 0; i < num_sources; i++)
        printf(" %s", &argv[src_index+i]->s);
    if (!src_index)
        printf(" -> %s", &argv[dest_index]->s);
    printf("\n");
#endif
    if (!c) {
        return 0;
    }

    if (c->is_admin) {
        // inform remote peer(s)
        if (!c->destination.local) {
            mapper_admin_set_bundle_dest_mesh(admin, c->destination.link->admin_addr);
            mapper_admin_send_connection(admin, md, c, -1, ADM_DISCONNECTED);
        }
        else {
            for (i = 0; i < c->props.num_sources; i++) {
                if (c->sources[i].local)
                    continue;
                mapper_admin_set_bundle_dest_mesh(admin, c->sources[i].link->admin_addr);
                mapper_admin_send_connection(admin, md, c, i, ADM_DISCONNECTED);
            }
        }
    }

    if (admin->subscribers) {
        // Inform subscribers
        if (c->destination.local)
            mapper_admin_set_bundle_dest_subscribers(admin,
                                                     SUBSCRIBE_CONNECTIONS_IN);
        else
            mapper_admin_set_bundle_dest_subscribers(admin,
                                                     SUBSCRIBE_CONNECTIONS_OUT);
        mapper_admin_send_connection(admin, md, c, -1, ADM_DISCONNECTED);
    }

    // Call local connection handler if it exists
    if (md->connection_cb) {
        for (i = 0; i < c->props.num_sources; i++)
            if (c->sources[i].local)
                md->connection_cb(md, c->sources[i].local->signal, &c->props,
                                  c->sources[i].props, MDEV_LOCAL_DESTROYED,
                                  md->connection_cb_userdata);
        if (c->destination.local)
            md->connection_cb(md, c->destination.local->signal, &c->props,
                              c->destination.props, MDEV_LOCAL_DESTROYED,
                              md->connection_cb_userdata);
    }

    /* The connection is removed. */
    mapper_router_remove_connection(md->router, c);
    // TODO: remove empty router_signals
    return 0;
}

/*! Respond to /disconnected by removing connection from database. */
static int handler_signal_disconnected(const char *path, const char *types,
                                       lo_arg **argv, int argc,
                                       lo_message msg, void *user_data)
{
    // TODO: devices should check if they are target, clean up old connections
    mapper_admin admin = (mapper_admin) user_data;
    mapper_monitor mon = admin->monitor;
    int i, num_sources, src_index, dest_index;

    num_sources = parse_signal_names(types, argv, argc, &src_index,
                                     &dest_index, 0);
    if (!num_sources)
        return 0;

    int order[num_sources];
    if (alphabetise_names(num_sources, &argv[src_index], order)) {
        trace("error in /connect: multiple use of source signal.");
        return 0;
    }
    const char *src_names[num_sources];
    for (i=0; i<num_sources; i++) {
        src_names[i] = &argv[src_index+order[i]]->s;
    }
    const char *dest_name = &argv[dest_index]->s;

    mapper_db db = mmon_get_db(mon);

#ifdef DEBUG
    printf("-- <monitor> got /disconnected");
    for (i = 0; i < num_sources; i++)
        printf(" %s", &argv[i]->s);
    printf(" -> %s\n", &argv[dest_index]->s);
#endif

    mapper_db_remove_connection(db,
        mapper_db_get_connection_by_signal_full_names(db, num_sources,
                                                      src_names, dest_name));

    return 0;
}

static int handler_device_ping(const char *path, const char *types,
                               lo_arg **argv, int argc,lo_message msg,
                               void *user_data)
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
        if ((reg = mapper_db_get_device_by_name(&mon->db, &argv[0]->s))) {
            mapper_timetag_cpy(&reg->synced, lo_message_get_timestamp(msg));
        }
        if (mon->autosubscribe && (!reg || !reg->subscribed)) {
            // only create device record after requesting more information
            mmon_subscribe(mon, &argv[0]->s, mon->autosubscribe, -1);
            if (reg)
                reg->subscribed = 1;
        }
    }
    else if (types[0] == 'i') {
        if ((reg = mapper_db_get_device_by_name_hash(&mon->db, argv[0]->i)))
            mapper_timetag_cpy(&reg->synced, lo_message_get_timestamp(msg));
    }

    return 0;
}
