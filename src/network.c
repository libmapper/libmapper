
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

extern const char* prop_message_strings[NUM_AT_PARAMS];

// set to 1 to force mesh comms to use multicast bus instead for debugging
#define FORCE_COMMS_TO_BUS      0

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

static int is_alphabetical(int num, lo_arg **names)
{
    int i;
    if (num == 1)
        return 1;
    for (i = 1; i < num; i++) {
        if (strcmp(&names[i-1]->s, &names[i]->s)>=0)
            return 0;
    }
    return 1;
}

const char* network_message_strings[] =
{
    "/map",                     /* MSG_MAP */
    "/mapTo",                   /* MSG_MAP_TO */
    "/mapped",                  /* MSG_MAPPED */
    "/map/modify",              /* MSG_MODIFY_MAP */
    "/device",                  /* MSG_DEVICE */
    "/unmap",                   /* MSG_UNMAP */
    "/unmapped",                /* MSG_UNMAPPED */
    "/ping",                    /* MSG_PING */
    "/logout",                  /* MSG_LOGOUT */
    "/name/probe",              /* MSG_NAME_PROBE */
    "/name/registered",         /* MSG_NAME_REG */
    "/signal",                  /* MSG_SIGNAL */
    "/signal/removed",          /* MSG_SIGNAL_REMOVED */
    "/%s/subscribe",            /* MSG_SUBSCRIBE */
    "/%s/unsubscribe",          /* MSG_UNSUBSCRIBE */
    "/sync",                    /* MSG_SYNC */
    "/who",                     /* MSG_WHO */
};

/* Internal functions for sending network messages. */
static void mapper_network_send_device(mapper_network net, mapper_device dev);
static int mapper_network_send_map(mapper_network net, mapper_map map, int slot,
                                   int cmd);

#define HANDLER_ARGS const char*, const char*, lo_arg**, int, lo_message, void*
/* Internal message handler prototypes. */
static int handler_who(HANDLER_ARGS);
static int handler_device(HANDLER_ARGS);
static int handler_logout(HANDLER_ARGS);
static int handler_subscribe(HANDLER_ARGS);
static int handler_unsubscribe(HANDLER_ARGS);
static int handler_signal_info(HANDLER_ARGS);
static int handler_signal_removed(HANDLER_ARGS);
static int handler_probe(HANDLER_ARGS);
static int handler_registered(HANDLER_ARGS);
static int handler_ping(HANDLER_ARGS);
static int handler_map(HANDLER_ARGS);
static int handler_map_to(HANDLER_ARGS);
static int handler_mapped(HANDLER_ARGS);
static int handler_modify_map(HANDLER_ARGS);
static int handler_unmap(HANDLER_ARGS);
static int handler_unmapped(HANDLER_ARGS);
static int handler_sync(HANDLER_ARGS);

/* Handler <-> Message relationships */
struct handler_method_assoc {
    int str_index;
    char *types;
    lo_method_handler h;
};

// handlers needed by "devices"
static struct handler_method_assoc device_handlers[] = {
    {MSG_MAP,                   NULL,       handler_map},
    {MSG_MAP_TO,                NULL,       handler_map_to},
    {MSG_MODIFY_MAP,            NULL,       handler_modify_map},
    {MSG_UNMAP,                 NULL,       handler_unmap},
    {MSG_LOGOUT,                NULL,       handler_logout},
    {MSG_SUBSCRIBE,             NULL,       handler_subscribe},
    {MSG_UNSUBSCRIBE,           NULL,       handler_unsubscribe},
    {MSG_WHO,                   NULL,       handler_who},
    {MSG_MAPPED,                NULL,       handler_mapped},
    {MSG_DEVICE,                NULL,       handler_device},
    {MSG_PING,                  "hiid",     handler_ping},
};
const int NUM_DEVICE_HANDLERS =
    sizeof(device_handlers)/sizeof(device_handlers[0]);

// handlers needed by "admins"
static struct handler_method_assoc admin_handlers[] = {
    {MSG_MAPPED,                NULL,       handler_mapped},
    {MSG_DEVICE,                NULL,       handler_device},
    {MSG_UNMAPPED,              NULL,       handler_unmapped},
    {MSG_LOGOUT,                NULL,       handler_logout},
    {MSG_SYNC,                  NULL,       handler_sync},
    {MSG_SIGNAL,                NULL,       handler_signal_info},
    {MSG_SIGNAL_REMOVED,        "s",        handler_signal_removed},
};
const int NUM_ADMIN_HANDLERS =
sizeof(admin_handlers)/sizeof(admin_handlers[0]);

/* Internal LibLo error handler */
static void handler_error(int num, const char *msg, const char *where)
{
    printf("[libmapper] liblo server error %d in path %s: %s\n",
           num, where, msg);
}

/* Functions for handling the resource allocation scheme.  If
 * check_collisions() returns 1, the resource in question should be
 * probed on the libmapper bus. */
static int check_collisions(mapper_network net, mapper_allocated resource);

/*! Local function to get the IP address of a network interface. */
static int get_interface_addr(const char* pref, struct in_addr* addr,
                              char **iface)
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
        // is the data-sending port, not the libmapper bus port.

        if (sa->sin_family == AF_INET && ifap->ifa_flags & IFF_UP
            && memcmp(&sa->sin_addr, &zero, sizeof(struct in_addr))!=0) {
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

static void mapper_network_add_device_methods(mapper_network net,
                                              mapper_device dev)
{
    int i;
    char fullpath[256];
    for (i=0; i < NUM_DEVICE_HANDLERS; i++) {
        snprintf(fullpath, 256,
                 network_message_strings[device_handlers[i].str_index],
                 mapper_device_name(net->device));
        lo_server_add_method(net->bus_server, fullpath, device_handlers[i].types,
                             device_handlers[i].h, net);
#if !FORCE_COMMS_TO_BUS
        lo_server_add_method(net->mesh_server, fullpath, device_handlers[i].types,
                             device_handlers[i].h, net);
#endif
    }
}

static void mapper_network_remove_device_methods(mapper_network net,
                                                 mapper_device dev)
{
    int i, j;
    char fullpath[256];
    for (i=0; i < NUM_DEVICE_HANDLERS; i++) {
        snprintf(fullpath, 256,
                 network_message_strings[device_handlers[i].str_index],
                 mapper_device_name(net->device));
        // make sure method isn't also used by a device
        if (net->admin) {
            int found = 0;
            for (j=0; j < NUM_ADMIN_HANDLERS; j++) {
                if (device_handlers[i].str_index == admin_handlers[j].str_index) {
                    found = 1;
                    break;
                }
            }
            if (found)
                continue;
        }
        lo_server_del_method(net->bus_server,
                             network_message_strings[device_handlers[i].str_index],
                             device_handlers[i].types);
#if !FORCE_COMMS_TO_BUS
        lo_server_del_method(net->mesh_server,
                             network_message_strings[device_handlers[i].str_index],
                             device_handlers[i].types);
#endif
    }
}

static void mapper_network_add_admin_methods(mapper_network net)
{
    int i;
    for (i=0; i < NUM_ADMIN_HANDLERS; i++) {
        lo_server_add_method(net->bus_server,
                             network_message_strings[admin_handlers[i].str_index],
                             admin_handlers[i].types, admin_handlers[i].h, net);
#if !FORCE_COMMS_TO_BUS
        lo_server_add_method(net->mesh_server,
                             network_message_strings[admin_handlers[i].str_index],
                             admin_handlers[i].types, admin_handlers[i].h, net);
#endif
    }
}

static void mapper_network_remove_admin_methods(mapper_network net)
{
    int i, j;
    for (i=0; i < NUM_ADMIN_HANDLERS; i++) {
        // make sure method isn't also used by a device
        if (net->device) {
            int found = 0;
            for (j=0; j < NUM_DEVICE_HANDLERS; j++) {
                if (admin_handlers[i].str_index == device_handlers[j].str_index) {
                    found = 1;
                    break;
                }
            }
            if (found)
                continue;
        }
        lo_server_del_method(net->bus_server,
                             network_message_strings[admin_handlers[i].str_index],
                             admin_handlers[i].types);
#if !FORCE_COMMS_TO_BUS
        lo_server_del_method(net->mesh_server,
                             network_message_strings[admin_handlers[i].str_index],
                             admin_handlers[i].types);
#endif
    }
}

mapper_network mapper_network_new(const char *iface, const char *group, int port)
{
    mapper_network net = (mapper_network) calloc(1, sizeof(mapper_network_t));
    if (!net)
        return NULL;

    net->db.network = net;
    net->interface_name = 0;

    /* Default standard ip and port is group 224.0.1.3, port 7570 */
    char port_str[10], *s_port = port_str;
    if (!group) group = "224.0.1.3";
    if (port==0)
        s_port = "7570";
    else
        snprintf(port_str, 10, "%d", port);

    /* Initialize interface information. */
    if (get_interface_addr(iface, &net->interface_ip, &net->interface_name))
        trace("no interface found\n");

    /* Open address */
    net->bus_addr = lo_address_new(group, s_port);
    if (!net->bus_addr) {
        free(net);
        return NULL;
    }

    /* Set TTL for packet to 1 -> local subnet */
    lo_address_set_ttl(net->bus_addr, 1);

    /* Specify the interface to use for multicasting */
#ifdef HAVE_LIBLO_SET_IFACE
    lo_address_set_iface(net->bus_addr, net->interface_name, 0);
#endif

    /* Open server for multicast group 224.0.1.3, port 7570 */
    net->bus_server =
#ifdef HAVE_LIBLO_SERVER_IFACE
        lo_server_new_multicast_iface(group, s_port, net->interface_name, 0,
                                      handler_error);
#else
        lo_server_new_multicast(group, s_port, handler_error);
#endif

    if (!net->bus_server) {
        lo_address_free(net->bus_addr);
        free(net);
        return NULL;
    }

    // Also open address/server for mesh-style communications
    // TODO: use TCP instead?
    while (!(net->mesh_server = lo_server_new(0, liblo_error_handler))) {}

    // Disable liblo message queueing.
    lo_server_enable_queue(net->bus_server, 0, 1);
    lo_server_enable_queue(net->mesh_server, 0, 1);

    return net;
}

// TODO: move to header?
const char *mapper_libversion(mapper_network net)
{
    return PACKAGE_VERSION;
}

void mapper_network_send_bundle(mapper_network net)
{
    if (!net->bundle)
        return;
#if FORCE_COMMS_TO_BUS
    lo_send_bundle_from(net->bus_addr, net->mesh_server, net->bundle);
#else
    if (net->bundle_dest == BUNDLE_DEST_SUBSCRIBERS) {
        mapper_subscriber *s = &net->subscribers;
        if (*s) {
            mapper_clock_now(&net->clock, &net->clock.now);
        }
        while (*s) {
            if ((*s)->lease_expiration_sec < net->clock.now.sec || !(*s)->flags) {
                // subscription expired, remove from subscriber list
                mapper_subscriber temp = *s;
                *s = temp->next;
                if (temp->address)
                    lo_address_free(temp->address);
                free(temp);
                continue;
            }
            if ((*s)->flags & net->message_type) {
                lo_send_bundle_from((*s)->address, net->mesh_server, net->bundle);
            }
            s = &(*s)->next;
        }
    }
    else if (net->bundle_dest == BUNDLE_DEST_BUS) {
        lo_send_bundle_from(net->bus_addr, net->mesh_server, net->bundle);
    }
    else {
        lo_send_bundle_from(net->bundle_dest, net->mesh_server, net->bundle);
    }
#endif
    lo_bundle_free_messages(net->bundle);
    net->bundle = 0;
}

static int mapper_network_init_bundle(mapper_network net)
{
    if (net->bundle)
        mapper_network_send_bundle(net);

    mapper_clock_now(&net->clock, &net->clock.now);
    net->bundle = lo_bundle_new(net->clock.now);
    if (!net->bundle) {
        trace("couldn't allocate lo_bundle\n");
        return 1;
    }

    return 0;
}

void mapper_network_set_bundle_dest_bus(mapper_network net)
{
    if (net->bundle && net->bundle_dest != BUNDLE_DEST_BUS)
        mapper_network_send_bundle(net);
    net->bundle_dest = BUNDLE_DEST_BUS;
    if (!net->bundle)
        mapper_network_init_bundle(net);
}

void mapper_network_set_bundle_dest_mesh(mapper_network net, lo_address address)
{
    if (net->bundle && net->bundle_dest != address)
        mapper_network_send_bundle(net);
    net->bundle_dest = address;
    if (!net->bundle)
        mapper_network_init_bundle(net);
}

void mapper_network_set_bundle_dest_subscribers(mapper_network net, int type)
{
    if ((net->bundle && net->bundle_dest != BUNDLE_DEST_SUBSCRIBERS) ||
        (net->message_type != type))
        mapper_network_send_bundle(net);
    net->bundle_dest = BUNDLE_DEST_SUBSCRIBERS;
    net->message_type = type;
    if (!net->bundle)
        mapper_network_init_bundle(net);
}

/*! Free the memory allocated by a mapper network structure.
 *  \param network An network structure handle.
 */
void mapper_network_free(mapper_network net)
{
    if (!net)
        return;

    // send out any cached messages
    mapper_network_send_bundle(net);

    if (net->interface_name)
        free(net->interface_name);

    if (net->bus_server)
        lo_server_free(net->bus_server);

    if (net->mesh_server)
        lo_server_free(net->mesh_server);

    if (net->bus_addr)
        lo_address_free(net->bus_addr);

    mapper_subscriber s;
    while (net->subscribers) {
        s = net->subscribers;
        if (s->address)
            lo_address_free(s->address);
        net->subscribers = s->next;
        free(s);
    }

    free(net);
}

/*! Probe the libmapper bus to see if a device's proposed name.ordinal is
 *  already taken.
 */
static void mapper_network_probe_device_name(mapper_network net,
                                             mapper_device dev)
{
    dev->local->ordinal.collision_count = -1;
    dev->local->ordinal.count_time = get_current_time();

    /* Note: mapper_device_name() would refuse here since the
     * ordinal is not yet locked, so we have to build it manually at
     * this point. */
    char name[256];
    trace("</%s.?::%p> probing name\n", dev->identifier, net);
    snprintf(name, 256, "%s.%d", dev->identifier, dev->local->ordinal.value);

    /* Calculate an id from the name and store it in id.value */
    dev->id = crc32(0L, (const Bytef *)name, strlen(name)) << 32;

    /* For the same reason, we can't use mapper_network_send()
     * here. */
    lo_send(net->bus_addr, network_message_strings[MSG_NAME_PROBE], "si",
            name, net->random_id);
}

/*! Add an uninitialized device to this network. */
void mapper_network_add_device(mapper_network net, mapper_device dev)
{
    /* Initialize data structures */
    if (dev) {
        net->device = dev;
        mapper_clock_init(&net->clock);

        /* Seed the random number generator. */
        seed_srand();

        /* Choose a random ID for allocation speedup */
        net->random_id = rand();

        /* Add methods for libmapper bus.  Only add methods needed for
         * allocation here. Further methods are added when the device is
         * registered. */
        lo_server_add_method(net->bus_server,
                             network_message_strings[MSG_NAME_PROBE],
                             NULL, handler_probe, net);
        lo_server_add_method(net->bus_server,
                             network_message_strings[MSG_NAME_REG],
                             NULL, handler_registered, net);

        /* Probe potential name to libmapper bus. */
        mapper_network_probe_device_name(net, dev);
    }
}

/*! Add an uninitialized admin to this network. */
void mapper_network_add_admin(mapper_network net, mapper_admin adm)
{
    /* Initialize admin methods. */
    if (adm) {
        net->admin = adm;
        mapper_network_add_admin_methods(net);
    }
}

void mapper_network_remove_device(mapper_network net, mapper_device dev)
{
    if (dev) {
        mapper_network_remove_device_methods(net, dev);
        net->device = 0;
    }
}

void mapper_network_remove_admin(mapper_network net, mapper_admin adm)
{
    if (adm) {
        mapper_network_remove_admin_methods(net);
        net->admin = 0;
    }
}

// TODO: rename to mapper_device...?
static void mapper_network_maybe_send_ping(mapper_network net, int force)
{
    mapper_device dev = net->device;
    int go = 0;

    mapper_clock_t *clock = &net->clock;
    mapper_clock_now(clock, &clock->now);
    if (force || (clock->now.sec >= clock->next_ping)) {
        go = 1;
        clock->next_ping = clock->now.sec + 5 + (rand() % 4);
    }

    if (!dev || !go)
        return;

    mapper_network_set_bundle_dest_bus(net);
    lo_message msg = lo_message_new();
    if (!msg) {
        trace("couldn't allocate lo_message\n");
        return;
    }
    lo_message_add_string(msg, mapper_device_name(dev));
    lo_message_add_int32(msg, dev->version);
    lo_bundle_add_message(net->bundle, network_message_strings[MSG_SYNC], msg);

    int elapsed, num_maps;
    // some housekeeping: periodically check if our links are still active
    mapper_link link = dev->local->router->links;
    while (link) {
        if (link->remote_device->id == dev->id) {
            // don't bother sending pings to self
            link = link->next;
            continue;
        }
        num_maps = link->num_incoming_maps + link->num_outgoing_maps;
        mapper_sync_clock sync = &link->clock;
        elapsed = (sync->response.timetag.sec
                   ? clock->now.sec - sync->response.timetag.sec : 0);
        if ((dev->local->link_timeout_sec
             && elapsed > dev->local->link_timeout_sec)) {
            if (sync->response.message_id > 0) {
                if (num_maps) {
                    trace("<%s> Lost contact with linked device %s "
                          "(%d seconds since sync).\n", mapper_device_name(dev),
                          link->remote_device->name, elapsed);
                }
                // tentatively mark link as expired
                sync->response.message_id = -1;
                sync->response.timetag.sec = clock->now.sec;
            }
            else {
                if (num_maps) {
                    trace("<%s> Removing link to unresponsive device %s "
                          "(%d seconds since warning).\n", mapper_device_name(dev),
                          link->remote_device->name, elapsed);
                    /* TODO: release related maps, call local handlers
                     * and inform subscribers. */
                }
                else {
                    trace("<%s> Removing link to device %s.\n",
                          mapper_device_name(dev), link->remote_device->name);
                }
                // remove related data structures
                mapper_router_remove_link(dev->local->router, link);
            }
        }
        else if (link->remote_device->host && num_maps) {
            /* Only send pings if this link has associated maps, ensuring
             * empty links are removed after the ping timeout. */
            lo_bundle b = lo_bundle_new(clock->now);
            lo_message m = lo_message_new();
            lo_message_add_int64(m, mapper_device_id(dev));
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
            lo_bundle_add_message(b, network_message_strings[MSG_PING], m);
#if FORCE_COMMS_TO_BUS
            lo_send_bundle_from(net->bus_addr, net->mesh_server, b);
#else
            lo_send_bundle_from(link->admin_addr, net->mesh_server, b);
#endif
            mapper_timetag_copy(&sync->sent.timetag, lo_bundle_get_timestamp(b));
            lo_bundle_free_messages(b);
        }
        link = link->next;
    }
}

/*! This is the main function to be called once in a while from a
 *  program so that the libmapper bus can be automatically managed.
 */
int mapper_network_poll(mapper_network net)
{
    int count = 0, status;
    mapper_device dev = net->device;

    // send out any cached messages
    mapper_network_send_bundle(net);

    while (count < 10 && (lo_server_recv_noblock(net->bus_server, 0)
           + lo_server_recv_noblock(net->mesh_server, 0))) {
        count++;
    }
    net->msgs_recvd += count;

    if (!dev) {
        mapper_network_maybe_send_ping(net, 0);
        return count;
    }

    /* If the ordinal is not yet locked, process collision timing.
     * Once the ordinal is locked it won't change. */
    if (!dev->local->registered) {
        status = check_collisions(net, &dev->local->ordinal);
        if (status == 1) {
            /* If the ordinal has changed, re-probe the new name. */
            mapper_network_probe_device_name(net, dev);
        }

        /* If we are ready to register the device, add the needed message
         * handlers. */
        if (dev->local->ordinal.locked) {
            mapper_device_registered(dev);

            /* Send registered msg. */
            lo_send(net->bus_addr, network_message_strings[MSG_NAME_REG],
                    "s", mapper_device_name(dev));

            mapper_network_add_device_methods(net, dev);
            mapper_network_maybe_send_ping(net, 1);

            trace("</%s.?::%p> registered as <%s>\n",
                  dev->identifier, net, mapper_device_name(dev));
        }
    }
    else {
        // Send out clock sync messages occasionally
        mapper_network_maybe_send_ping(net, 0);
    }
    return count;
}

/*! Algorithm for checking collisions and allocating resources. */
static int check_collisions(mapper_network net, mapper_allocated resource)
{
    double timediff;

    if (resource->locked)
        return 0;

    timediff = get_current_time() - resource->count_time;

    if (!net->msgs_recvd) {
        if (timediff >= 5.0) {
            // reprobe with the same value
            return 1;
        }
        return 0;
    }
    else if (timediff >= 2.0 && resource->collision_count <= 1) {
        resource->locked = 1;
        if (resource->on_lock)
            resource->on_lock(resource);
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

static void mapper_network_send_device(mapper_network net, mapper_device dev)
{
    if (!dev)
        return;
    lo_message msg = lo_message_new();
    if (!msg) {
        trace("couldn't allocate lo_message\n");
        return;
    }
    mapper_device_prepare_message(dev, msg);
    lo_bundle_add_message(net->bundle, network_message_strings[MSG_DEVICE], msg);
}

/* TODO: this message needs to be dispatched immediately since liblo will not
 * cache the signal name string. The protocol should be updated to send
 * device and signal names as separate strings. */
void mapper_network_send_signal(mapper_network net, mapper_signal sig)
{
    lo_message msg = lo_message_new();
    if (!msg) {
        trace("couldn't allocate lo_message\n");
        return;
    }
    char sig_name[1024];
    mapper_signal_full_name(sig, sig_name, 1024);
    lo_message_add_string(msg, sig_name);
    mapper_signal_prepare_message(sig, msg);
    lo_bundle_add_message(net->bundle, network_message_strings[MSG_SIGNAL], msg);
}

void mapper_network_send_signal_removed(mapper_network net, mapper_signal sig)
{
    lo_message msg = lo_message_new();
    if (!msg) {
        trace("couldn't allocate lo_message\n");
        return;
    }
    char sig_name[1024];
    mapper_signal_full_name(sig, sig_name, 1024);
    lo_message_add_string(msg, sig_name);
    lo_bundle_add_message(net->bundle,
                          network_message_strings[MSG_SIGNAL_REMOVED], msg);
}

static void mapper_network_send_inputs(mapper_network net, mapper_device dev,
                                       int min, int max)
{
    if (min < 0)
        min = 0;
    else if (min > dev->num_inputs)
        return;
    if (max < 0 || max > dev->num_inputs)
        max = dev->num_inputs-1;

    int i = 0;
    mapper_signal *sig = mapper_db_device_signals(&net->db, dev, MAPPER_INCOMING);
    while (sig) {
        if (i > max) {
            mapper_signal_query_done(sig);
            return;
        }
        if (i >= min)
            mapper_network_send_signal(net, *sig);
        i++;
        sig = mapper_signal_query_next(sig);
    }
}

static void mapper_network_send_outputs(mapper_network net, mapper_device dev,
                                        int min, int max)
{
    if (min < 0)
        min = 0;
    else if (min > dev->num_outputs)
        return;
    if (max < 0 || max > dev->num_outputs)
        max = dev->num_outputs-1;

    int i = 0;
    mapper_signal *sig = mapper_db_device_signals(&net->db, dev, MAPPER_OUTGOING);
    while (sig) {
        if (i > max) {
            mapper_signal_query_done(sig);
            return;
        }
        if (i >= min)
            mapper_network_send_signal(net, *sig);
        i++;
        sig = mapper_signal_query_next(sig);
    }
}

// Send /mapped message
static int mapper_network_send_map(mapper_network net, mapper_map map, int slot,
                                   int cmd)
{
    if (cmd == MSG_MAPPED && map->local->status < MAPPER_READY)
        return slot;

    lo_message m = lo_message_new();
    if (!m) {
        trace("couldn't allocate lo_message\n");
        return slot;
    }

    char dest_name[1024], source_names[1024];
    snprintf(dest_name, 1024, "%s%s",
             map->destination.signal->device->name,
             map->destination.signal->path);

    if (map->destination.direction == MAPPER_INCOMING) {
        // add mapping destination
        lo_message_add_string(m, dest_name);
        lo_message_add_string(m, "<-");
    }

    // add mapping sources
    int i = (slot >= 0) ? slot : 0;
    int len = 0, result;
    mapper_link link = map->sources[i].local->link;
    for (; i < map->num_sources; i++) {
        if (slot >= 0 && link != map->sources[i].local->link)
            break;
        result = snprintf(&source_names[len], 1024-len, "%s%s",
                          map->sources[i].signal->device->name,
                          map->sources[i].signal->path);
        if (result < 0 || (len + result + 1) >= 1024) {
            trace("Error encoding sources for combined /mapped msg");
            lo_message_free(m);
            return slot;
        }
        lo_message_add_string(m, &source_names[len]);
        len += result + 1;
    }

    if (map->destination.direction == MAPPER_OUTGOING) {
        // add mapping destination
        lo_message_add_string(m, "->");
        lo_message_add_string(m, dest_name);
    }

    if (cmd == MSG_UNMAP) {
        lo_bundle_add_message(net->bundle, network_message_strings[cmd], m);
        return i-1;
    }

    if (cmd == MSG_MAP_TO && map->destination.local->router_sig) {
        // include "extra" metadata from the destination signal
        mapper_signal sig = map->destination.local->router_sig->signal;
        if (sig->extra)
            mapper_message_add_value_table(m, sig->extra);
    }

    // add other properties
    const char *keys = mapper_map_prepare_message(map, m, slot);

    lo_bundle_add_message(net->bundle, network_message_strings[cmd], m);
    // send bundle here since message refers to generated strings
    mapper_network_send_bundle(net);
    if (keys)
        free((char*)keys);
    return i-1;
}

static void mapper_network_send_incoming_maps(mapper_network net,
                                              mapper_device dev,
                                              int min, int max)
{
    int i, count = 0;
    mapper_router_signal rs = dev->local->router->signals;
    while (rs) {
        for (i = 0; i < rs->num_slots; i++) {
            if (!rs->slots[i] || rs->slots[i]->direction == MAPPER_OUTGOING)
                continue;
            if (max > 0 && count > max)
                return;
            if (count >= min) {
                mapper_map map = rs->slots[i]->map;
                mapper_network_init_bundle(net);
                mapper_network_send_map(net, map, -1, MSG_MAPPED);
            }
            count++;
        }
        rs = rs->next;
    }
}

static void mapper_network_send_outgoing_maps(mapper_network net,
                                              mapper_device dev,
                                              int min, int max)
{
    int i, count = 0;
    mapper_router_signal rs = dev->local->router->signals;
    while (rs) {
        for (i = 0; i < rs->num_slots; i++) {
            if (!rs->slots[i] || rs->slots[i]->direction == MAPPER_INCOMING)
                continue;
            if (max > 0 && count > max)
                return;
            if (count >= min) {
                mapper_map map = rs->slots[i]->map;
                mapper_network_init_bundle(net);
                mapper_network_send_map(net, map, -1, MSG_MAPPED);
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
    mapper_network net = (mapper_network) user_data;
    mapper_network_maybe_send_ping(net, 1);

    trace("%s received /who\n", mapper_device_name(net->device));

    return 0;
}

/*! Register information about port and host for the device. */
static int handler_device(const char *path, const char *types,
                          lo_arg **argv, int argc, lo_message msg,
                          void *user_data)
{
    mapper_network net = (mapper_network) user_data;
    mapper_admin adm = net->admin;
    mapper_device dev = net->device;
    int i, j;

    if (argc < 1)
        return 0;

    if (types[0] != 's' && types[0] != 'S')
        return 0;

    const char *name = &argv[0]->s;
    lo_address a = lo_message_get_source(msg);

    mapper_message params = mapper_message_parse_params(argc-1, &types[1],
                                                        &argv[1]);

    if (adm) {
        trace("<admin> got /device %s + %i arguments\n", name, argc-1);
        mapper_db db = mapper_admin_db(adm);
        mapper_device dev;
        dev = mapper_db_add_or_update_device_params(db, name, params);
        if (adm->autosubscribe && (!dev->subscribed)) {
            mapper_admin_subscribe(adm, dev, adm->autosubscribe, -1);
            dev->subscribed = 1;
        }
    }

    if (!dev) {
        mapper_message_free(params);
        return 0;
    }

    if (strcmp(&argv[0]->s, mapper_device_name(dev))) {
        trace("<%s> got /device %s\n", mapper_device_name(dev), &argv[0]->s);
    }
    else {
        // ignore own messages
        trace("<%s> ignoring /device %s\n", mapper_device_name(dev), &argv[0]->s);
        mapper_message_free(params);
        return 0;
    }
    // Discover whether the device is linked.
    mapper_link link = mapper_router_find_link_by_remote_name(dev->local->router,
                                                              name);
    if (!link) {
        trace("<%s> ignoring /device '%s', no link.\n",
              mapper_device_name(dev), name);
        mapper_message_free(params);
        return 0;
    }
    else if (link->remote_device->host) {
        // already have metadata, can ignore this message
        trace("<%s> ignoring /device '%s', link already set.\n",
              mapper_device_name(dev), name);
        mapper_message_free(params);
        return 0;
    }
    if (!a) {
        trace("can't perform /linkTo, address unknown\n");
        mapper_message_free(params);
        return 0;
    }
    // Find the sender's hostname
    const char *host = lo_address_get_hostname(a);
    const char *admin_port = lo_address_get_port(a);
    if (!host) {
        trace("can't perform /linkTo, host unknown\n");
        mapper_message_free(params);
        return 0;
    }
    // Retrieve the port
    int data_port = 0;
    mapper_update_int_if_arg(&data_port, params, AT_PORT);
    mapper_message_free(params);
    if (!data_port) {
        trace("can't perform /linkTo, port unknown\n");
        return 0;
    }

    mapper_router_update_link(dev->local->router, link, host, atoi(admin_port),
                              data_port);
    trace("<%s> activated router to %s -> host: %s, port: %d\n",
          mapper_device_name(dev), name, host, data_port);

    // check if we have maps waiting for this link
    mapper_router_signal rs = dev->local->router->signals;
    while (rs) {
        for (i = 0; i < rs->num_slots; i++) {
            if (!rs->slots[i])
                continue;
            mapper_map map = rs->slots[i]->map;
            if (rs->slots[i]->direction == MAPPER_OUTGOING) {
                // only send /mapTo once even if we have multiple local sources
                if (map->local->one_source && (rs->slots[i] != &map->sources[0]))
                    continue;
                if (map->destination.local->link == link) {
                    mapper_network_set_bundle_dest_mesh(net, link->admin_addr);
                    mapper_network_send_map(net, map, -1, MSG_MAP_TO);
                }
            }
            else {
                for (j = 0; j < map->num_sources; j++) {
                    if (map->sources[j].local->link != link)
                        continue;
                    mapper_network_set_bundle_dest_mesh(net, link->admin_addr);
                    j = mapper_network_send_map(net, map,
                                                map->local->one_source ? -1 : j,
                                                MSG_MAP_TO);
                }
            }
        }
        rs = rs->next;
    }
    return 0;
}

/*! Respond to /logout by deleting record of device. */
static int handler_logout(const char *path, const char *types, lo_arg **argv,
                          int argc, lo_message msg, void *user_data)
{
    lo_message_pp(msg);
    mapper_network net = (mapper_network) user_data;
    mapper_device dev = net->device;

    mapper_admin adm = net->admin;
    mapper_db db = mapper_admin_db(adm);
    int diff, ordinal;
    char *s;

    if (argc < 1)
        return 0;

    if (types[0] != 's' && types[0] != 'S')
        return 0;

    char *name = &argv[0]->s;

    trace("<%s> got /logout %s\n", (dev && dev->local->ordinal.locked)
          ? mapper_device_name(dev) : "admin", name);

    if (adm) {
        mapper_device dev = mapper_db_device_by_name(db, name);
        if (dev) {
            // remove subscriptions
            mapper_admin_unsubscribe(adm, dev);
            mapper_db_remove_device(db, dev, 0);
        }
    }

    // If device exists and is registered
    if (dev && dev->local->ordinal.locked) {
        // Check if we have any links to this device, if so remove them
        mapper_link link =
            mapper_router_find_link_by_remote_name(dev->local->router, name);
        if (link) {
            // TODO: release maps, call local handlers and inform subscribers
            trace("<%s> Removing link to expired device %s.\n",
                  mapper_device_name(dev), link->remote_device->name);

            mapper_router_remove_link(dev->local->router, link);
        }

        /* Parse the ordinal from the complete name which is in the
         * format: <name>.<n> */
        s = name;
        while (*s != '.' && *s++) {}
        ordinal = atoi(++s);

        // If device name matches
        strtok(name, ".");
        name++;
        if (strcmp(name, dev->identifier) == 0) {
            // if registered ordinal is within my block, free it
            diff = ordinal - dev->local->ordinal.value;
            if (diff > 0 && diff < 9) {
                dev->local->ordinal.suggestion[diff-1] = 0;
            }
        }
    }

    return 0;
}

// Add/renew/remove a subscription.
// TODO: move to device
static void mapper_network_manage_subscriber(mapper_network net,
                                             lo_address address,
                                             int flags, int timeout_seconds,
                                             int revision)
{
    mapper_subscriber *s = &net->subscribers;
    const char *ip = lo_address_get_hostname(address);
    const char *port = lo_address_get_port(address);
    if (!ip || !port)
        return;

    mapper_clock_t *clock = &net->clock;
    mapper_clock_now(clock, &clock->now);

    while (*s) {
        if (strcmp(ip, lo_address_get_hostname((*s)->address))==0 &&
            strcmp(port, lo_address_get_port((*s)->address))==0) {
            // subscriber already exists
            if (!flags || !timeout_seconds) {
                // remove subscription
                mapper_subscriber temp = *s;
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
        mapper_subscriber sub = malloc(sizeof(struct _mapper_subscriber));
        sub->address = lo_address_new(ip, port);
        sub->lease_expiration_sec = clock->now.sec + timeout_seconds;
        sub->flags = flags;
        sub->next = net->subscribers;
        net->subscribers = sub;
        s = &sub;
    }

    if (revision == net->device->version)
        return;

    // bring new subscriber up to date
    mapper_network_set_bundle_dest_mesh(net, address);
    mapper_network_send_device(net, net->device);
    if (flags & SUBSCRIBE_DEVICE_INPUTS)
        mapper_network_send_inputs(net, net->device, -1, -1);
    if (flags & SUBSCRIBE_DEVICE_OUTPUTS)
        mapper_network_send_outputs(net, net->device, -1, -1);
    if (flags & SUBSCRIBE_DEVICE_MAPS_IN)
        mapper_network_send_incoming_maps(net, net->device, -1, -1);
    if (flags & SUBSCRIBE_DEVICE_MAPS_OUT)
        mapper_network_send_outgoing_maps(net, net->device, -1, -1);
    mapper_network_send_bundle(net);
}

/*! Respond to /subscribe message by adding or renewing a subscription. */
static int handler_subscribe(const char *path, const char *types, lo_arg **argv,
                             int argc, lo_message msg, void *user_data)
{
    mapper_network net = (mapper_network) user_data;
    mapper_device dev = net->device;
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
        else if (strcmp(&argv[i]->s, "maps")==0)
            flags |= SUBSCRIBE_DEVICE_MAPS;
        else if (strcmp(&argv[i]->s, "incoming_maps")==0)
            flags |= SUBSCRIBE_DEVICE_MAPS_IN;
        else if (strcmp(&argv[i]->s, "outgoing_maps")==0)
            flags |= SUBSCRIBE_DEVICE_MAPS_OUT;
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
                      mapper_device_name(dev));
            }
        }
    }

    // add or renew subscription
    mapper_network_manage_subscriber(net, a, flags, timeout_seconds, version);
    return 0;
}

/*! Respond to /unsubscribe message by removing a subscription. */
static int handler_unsubscribe(const char *path, const char *types,
                               lo_arg **argv, int argc, lo_message msg,
                               void *user_data)
{
    mapper_network net = (mapper_network) user_data;

    lo_address a  = lo_message_get_source(msg);
    if (!a) return 0;

    // remove subscription
    mapper_network_manage_subscriber(net, a, 0, 0, 0);

    return 0;
}

/*! Register information about a signal. */
static int handler_signal_info(const char *path, const char *types,
                               lo_arg **argv, int argc, lo_message msg,
                               void *user_data)
{
    mapper_network net = (mapper_network) user_data;
    mapper_admin adm = net->admin;
    mapper_db db = mapper_admin_db(adm);

    if (argc < 2)
        return 1;

    if (types[0] != 's' && types[0] != 'S')
        return 1;

    const char *full_sig_name = &argv[0]->s;
    char *signamep, *devnamep;
    int devnamelen = mapper_parse_names(full_sig_name, &devnamep, &signamep);
    if (!devnamep || !signamep || devnamelen >= 1024)
        return 0;

    char devname[1024];
    strncpy(devname, devnamep, devnamelen);
    devname[devnamelen]=0;

    mapper_message params = mapper_message_parse_params(argc-1, &types[1],
                                                        &argv[1]);
    mapper_db_add_or_update_signal_params(db, signamep, devname, params);
    mapper_message_free(params);

    return 0;
}

/*! Unregister information about a removed signal. */
static int handler_signal_removed(const char *path, const char *types,
                                  lo_arg **argv, int argc, lo_message msg,
                                  void *user_data)
{
    mapper_network net = (mapper_network) user_data;
    mapper_admin adm = net->admin;
    mapper_db db = mapper_admin_db(adm);

    if (argc < 1)
        return 1;

    if (types[0] != 's' && types[0] != 'S')
        return 1;

    const char *full_sig_name = &argv[0]->s;
    char *signamep, *devnamep;
    int devnamelen = mapper_parse_names(full_sig_name, &devnamep, &signamep);
    if (!devnamep || !signamep || devnamelen >= 1024)
        return 0;

    char devname[1024];
    strncpy(devname, devnamep, devnamelen);
    devname[devnamelen]=0;

    mapper_db_remove_signal_by_name(db, devname, signamep);

    return 0;
}

/*! Repond to name collisions during allocation, help suggest IDs once allocated. */
static int handler_registered(const char *path, const char *types, lo_arg **argv,
                              int argc, lo_message msg, void *user_data)
{
    mapper_network net = (mapper_network) user_data;
    mapper_device dev = net->device;
    char *name, *s;
    uint64_t id;
    int ordinal, diff, temp_id = -1, suggestion = -1;

    if (argc < 1)
        return 0;

    if (types[0] != 's' && types[0] != 'S')
        return 0;

    name = &argv[0]->s;

    trace("</%s.?::%p> got /name/registered %s %i \n",
          dev->identifier, net, name, temp_id);

    if (dev->local->ordinal.locked) {
        /* Parse the ordinal from the complete name which is in the
         * format: <name>.<n> */
        s = name;
        s = strrchr(s, '.');
        if (!s)
            return 0;
        ordinal = atoi(s+1);
        *s = 0;

        // If device name matches
        if (strcmp(name+1, dev->identifier) == 0) {
            // if id is locked and registered id is within my block, store it
            diff = ordinal - dev->local->ordinal.value;
            if (diff > 0 && diff < 9) {
                dev->local->ordinal.suggestion[diff-1] = -1;
            }
        }
    }
    else {
        id = crc32(0L, (const Bytef *)name, strlen(name)) << 32;
        if (id == dev->id) {
            if (argc > 1) {
                if (types[1] == 'i')
                    temp_id = argv[1]->i;
                if (types[2] == 'i')
                    suggestion = argv[2]->i;
            }
            if (temp_id == net->random_id &&
                suggestion != dev->local->ordinal.value && suggestion > 0) {
                dev->local->ordinal.value = suggestion;
                mapper_network_probe_device_name(net, dev);
            }
            else {
                /* Count ordinal collisions. */
                dev->local->ordinal.collision_count++;
                dev->local->ordinal.count_time = get_current_time();
            }
        }
    }
    return 0;
}

/*! Repond to name probes during allocation, help suggest names once allocated. */
static int handler_probe(const char *path, const char *types, lo_arg **argv,
                         int argc, lo_message msg, void *user_data)
{
    mapper_network net = (mapper_network) user_data;
    mapper_device dev = net->device;
    char *name;
    double current_time;
    uint64_t id;
    int temp_id = -1, i;

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
          dev->identifier, net, name, temp_id);

    id = crc32(0L, (const Bytef *)name, strlen(name)) << 32;
    if (id == dev->id) {
        if (dev->local->ordinal.locked) {
            current_time = get_current_time();
            for (i=0; i<8; i++) {
                if (dev->local->ordinal.suggestion[i] >= 0
                    && (current_time - dev->local->ordinal.suggestion[i]) > 2.0) {
                    // reserve suggested ordinal
                    dev->local->ordinal.suggestion[i] = get_current_time();
                    break;
                }
            }
            /* Name may not yet be registered, so we can't use
             * mapper_network_send() here. */
            lo_send(net->bus_addr, network_message_strings[MSG_NAME_REG],
                    "sii", name, temp_id,
                    (dev->local->ordinal.value+i+1));
        }
        else {
            dev->local->ordinal.collision_count++;
            dev->local->ordinal.count_time = get_current_time();
        }
    }
    return 0;
}

/* Basic description of the protocol for establishing maps:
 *
 * The message "/map <signalA> -> <signalB>" starts the protocol.  If a device
 * doesn't already have a record for the remote device it will request this
 * information with a "/subscribe" message.
 *
 * "/mapTo" messages are sent between devices until each is satisfied that it
 *  has enough information to initialize the map; at this point each device will
 *  send the message "/mapped" to its peer.  Data will be sent only after the
 *  "/mapped" message has been received from the peer device.
 *
 * The "/map/modify" message is used to change the properties of existing maps.
 * The device administering the map will make appropriate changes and then send
 * "/mapped" to its peer.
 *
 * Negotiation of convergent ("many-to-one") maps is governed by the destination
 * device; if the map involves multiple inputs the destination will provoke the
 * creation of simple submaps from the various sources and perform any
 * combination signal-processing, otherwise processing metadata is forwarded to
 * the source device.  A convergent mapping is started with a message in the
 * form: "/map <sourceA> <sourceB> ... <sourceN> -> <destination>"
 */

/* Helper function to check if the prefix matches.  Like strcmp(), returns 0 if
 * they match (up to the first '/'), non-0 otherwise.  Also optionally returns a
 * pointer to the remainder of str1 after the prefix. */
static int prefix_cmp(const char *str1, const char *str2, const char **rest)
{
    // skip first slash
    str1 += (str1[0] == '/');
    str2 += (str2[0] == '/');

    const char *s1=str1, *s2=str2;

    while (*s1 && (*s1)!='/') s1++;
    while (*s2 && (*s2)!='/') s2++;

    int n1 = s1-str1, n2 = s2-str2;
    if (n1!=n2) return 1;

    int result = strncmp(str1, str2, n1);
    if (!result && rest)
        *rest = s1+1;

    return result;
}

static int parse_signal_names(const char *types, lo_arg **argv, int argc,
                              int *src_index, int *dest_index, int *param_index)
{
    // old protocol: /connect src dest
    // new protocol: /map src1 ... srcN -> dest
    //               /map dest <- src1 ... srcN
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

    // check that all signal names are well formed
    for (i = 0; i < num_sources; i++) {
        if (!strchr((&argv[*src_index+i]->s)+1, '/')) {
            trace("malformed source signal name '%s'.\n", &argv[*src_index+i]->s);
            return 0;
        }
    }
    if (!strchr((&argv[*dest_index]->s)+1, '/')) {
        trace("malformed destination signal name '%s'.\n", &argv[*dest_index]->s);
        return 0;
    }
    return num_sources;
}

/*! When the /map message is received by the destination device,
 *  send a /mapTo message to the source device. */
static int handler_map(const char *path, const char *types, lo_arg **argv,
                       int argc, lo_message msg, void *user_data)
{
    mapper_network net = (mapper_network) user_data;
    mapper_device dev = net->device;
    mapper_signal local_signal = 0;
    int i, num_sources, src_index, dest_index, param_index;

    const char *local_signal_name = 0;

    num_sources = parse_signal_names(types, argv, argc, &src_index,
                                     &dest_index, &param_index);
    if (!num_sources)
        return 0;

    // check if we are the destination of this mapping
    if (prefix_cmp(&argv[dest_index]->s, mapper_device_name(dev),
                   &local_signal_name)==0) {
        local_signal = mapper_db_device_signal_by_name(&net->db, dev,
                                                       local_signal_name);
        if (!local_signal) {
            trace("<%s> no signal found with name '%s'.\n",
                  mapper_device_name(dev), local_signal_name);
            return 0;
        }
    }

#ifdef DEBUG
    printf("-- <%s> %s /map", mapper_device_name(dev),
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
    if (!is_alphabetical(num_sources, &argv[src_index])) {
        trace("error in /map: signal names out of order.\n");
        return 0;
    }
    const char *src_names[num_sources];
    for (i = 0; i < num_sources; i++) {
        src_names[i] = &argv[src_index+i]->s;
    }
    mapper_map map;
    map = mapper_router_find_incoming_map(dev->local->router, local_signal,
                                          num_sources, src_names);

    /* If a mapping already exists between these signals, forward the message
     * to handler_modify_map() and stop. */
    if (map) {
        handler_modify_map(path, types, argv, argc, msg, user_data);
        return 0;
    }

    // check if any sources are local
    const char *src_signal_name;
    mapper_signal src_signals[num_sources];
    for (i = 0; i < num_sources; i++) {
        if (prefix_cmp(src_names[i], mapper_device_name(dev),
                       &src_signal_name)==0) {
            // this source is a local signal
            src_signals[i] = mapper_db_device_signal_by_name(&net->db, dev,
                                                             src_signal_name);
        }
        else
            src_signals[i] = 0;
    }

    // create a tentative mapping (flavourless)
    map = mapper_router_add_map(dev->local->router, local_signal, num_sources,
                                src_signals, src_names, MAPPER_INCOMING);

    // parse arguments from message if any
    mapper_message params = mapper_message_parse_params(argc-dest_index-1,
                                                        &types[dest_index+1],
                                                        &argv[dest_index+1]);
    mapper_map_set_from_message(map, params, 1);
    mapper_message_free(params);

    if (map->local->status == MAPPER_READY) {
        // This mapping only references local signals, advance to "active" state
        map->local->status = MAPPER_ACTIVE;
        ++dev->num_outgoing_maps;
        ++dev->num_incoming_maps;
//        ++map->sources[i].local->link->num_outgoing_maps;
//        ++map->destination.local->link->num_incoming_maps;

        // Inform subscribers
        if (net->subscribers) {
            mapper_network_set_bundle_dest_subscribers(net, SUBSCRIBE_DEVICE_MAPS_IN);
            mapper_network_send_map(net, map, -1, MSG_MAPPED);
        }
        return 0;
    }

    if (map->local->one_source && !map->sources[0].local->router_sig
        && map->sources[0].local->link && map->sources[0].local->link->admin_addr) {
        mapper_network_set_bundle_dest_mesh(net,
                                            map->sources[0].local->link->admin_addr);
        mapper_network_send_map(net, map, -1, MSG_MAP_TO);
    }
    else {
        for (i = 0; i < num_sources; i++) {
            // do not send if is local mapping
            if (map->sources[i].local->router_sig)
                continue;
            // do not send if device host/port not yet known
            if (!map->sources[i].local->link
                || !map->sources[i].local->link->admin_addr)
                continue;
            mapper_network_set_bundle_dest_mesh(net,
                                                map->sources[i].local->link->admin_addr);
            i = mapper_network_send_map(net, map, i, MSG_MAP_TO);
        }
    }
    return 0;
}

/*! When the /mapTo message is received by a peer device, create
 *  tentative mapping and respond with own signal metadata. */
static int handler_map_to(const char *path, const char *types, lo_arg **argv,
                          int argc, lo_message msg, void *user_data)
{
    mapper_network net = (mapper_network) user_data;
    mapper_device dev = net->device;
    mapper_signal local_signal = 0;
    mapper_map map;
    int i, num_sources, src_index, dest_index, param_index;

    const char *local_signal_name = 0;

    num_sources = parse_signal_names(types, argv, argc, &src_index,
                                     &dest_index, &param_index);
    if (!num_sources)
        return 0;

    // check if we are an endpoint of this map
    if (!src_index) {
        // check if we are the destination
        if (prefix_cmp(&argv[dest_index]->s, mapper_device_name(dev),
                       &local_signal_name)==0) {
            local_signal = mapper_db_device_signal_by_name(&net->db, dev,
                                                           local_signal_name);
            if (!local_signal) {
                trace("<%s> no signal found with name '%s'.\n",
                      mapper_device_name(dev), local_signal_name);
                return 0;
            }
        }
    }
    else {
        // check if we are a source – all sources must match!
        for (i = 0; i < num_sources; i++) {
            if (prefix_cmp(&argv[src_index+i]->s, mapper_device_name(dev),
                           &local_signal_name)) {
                local_signal = 0;
                break;
            }
            local_signal = mapper_db_device_signal_by_name(&net->db, dev,
                                                           local_signal_name);
            if (!local_signal) {
                trace("<%s> no signal found with name '%s'.\n",
                      mapper_device_name(dev), local_signal_name);
                break;
            }
        }
    }

#ifdef DEBUG
    printf("-- <%s> %s /mapTo", mapper_device_name(dev),
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
    if (!is_alphabetical(num_sources, &argv[src_index])) {
        trace("error in /mapTo: signal names out of order.\n");
        return 0;
    }
    mapper_message params = mapper_message_parse_params(argc-param_index,
                                                        &types[param_index],
                                                        &argv[param_index]);
    if (!params) {
        trace("<%s> ignoring /mapTo, no properties.\n", mapper_device_name(dev));
        return 0;
    }
    mapper_message_atom atom = mapper_message_param(params, AT_ID);
    if (!atom || atom->types[0] != 'h') {
        trace("<%s> ignoring /mapTo, no 'id' property.\n",
              mapper_device_name(dev));
        mapper_message_free(params);
        return 0;
    }
    uint64_t id = (atom->values[0])->i64;

    if (src_index) {
        map = mapper_router_find_outgoing_map_by_id(dev->local->router,
                                                    local_signal, id);
    }
    else {
        map = mapper_router_find_incoming_map_by_id(dev->local->router,
                                                    local_signal, id);
    }

    if (!map) {
        char *src_names[num_sources];
        for (i = 0; i < num_sources; i++) {
            mapper_parse_names(&argv[src_index+i]->s, 0, &src_names[i]);
            if (!src_names[i]) {
                trace("error parsing signal name.\n")
                mapper_message_free(params);
                return 0;
            }
        }
        if (src_index) {
            // organize source signals
            mapper_signal src_sigs[num_sources];
            for (i = 0; i < num_sources; i++) {
                src_sigs[i] = mapper_db_device_signal_by_name(&net->db, dev,
                                                              src_names[i]);
            }
            /* Add a flavourless mapping */
            const char *dest_name = &argv[dest_index]->s;
            map = mapper_router_add_map(dev->local->router, local_signal,
                                        num_sources, src_sigs, &dest_name,
                                        MAPPER_OUTGOING);
        }
        else {
            /* Add a flavourless mapping */
            map = mapper_router_add_map(dev->local->router, local_signal,
                                        num_sources, 0, (const char**)src_names,
                                        MAPPER_INCOMING);
        }
        if (!map) {
            trace("couldn't create map in handler_map_to\n");
            mapper_message_free(params);
            return 0;
        }
    }
    else if (map->num_sources != num_sources) {
        if (src_index) {
            trace("<%s> wrong num_sources in /map_to.\n", mapper_device_name(dev));
            mapper_message_free(params);
            return 0;
        }
    }

    /* TODO: handle passing @numInstances prop from signals, set sensible
     * defaults for new mappings. */

    /* Set map properties. */
    mapper_map_set_from_message(map, params, 1);

    if (map->local->status == MAPPER_READY) {
        if (map->destination.direction == MAPPER_OUTGOING) {
            mapper_network_set_bundle_dest_mesh(net,
                                                map->destination.local->link->admin_addr);
            mapper_network_send_map(net, map, -1, MSG_MAPPED);
        }
        else {
            for (i = 0; i < map->num_sources; i++) {
                mapper_network_set_bundle_dest_mesh(net,
                                                    map->sources[i].local->link->admin_addr);
                i = mapper_network_send_map(net, map,
                                            map->local->one_source ? -1 : i,
                                            MSG_MAPPED);
            }
        }
    }

    mapper_message_free(params);
    return 0;
}

/*! Admins respond to /mapped by storing mapping in database. */
/*! Also used by devices to confirm connection to remote peers, and to share
 *  changes in mapping properties. */
static int handler_mapped(const char *path, const char *types, lo_arg **argv,
                          int argc, lo_message msg, void *user_data)
{
    mapper_network net = (mapper_network) user_data;
    mapper_device dev = net->device;
    mapper_signal local_signal = 0;
    mapper_map map;
    mapper_admin adm = net->admin;
    int i, num_sources, src_index, dest_index, param_index;
    const char *local_signal_name;

    num_sources = parse_signal_names(types, argv, argc, &src_index,
                                     &dest_index, &param_index);
    if (!num_sources)
        return 0;

    if (!is_alphabetical(num_sources, &argv[src_index])) {
        trace("error in /maped: signals names out of order.");
        return 0;
    }

    mapper_message params = mapper_message_parse_params(argc-param_index,
                                                        &types[param_index],
                                                        &argv[param_index]);

    // 2 scenarios: if message in form A -> B, only B interested
    //              if message in form A <- B, only B interested

    if (dev && mapper_device_name(dev)) {
        // check if we are an endpoint of this mapping
        if (!src_index) {
            // check if we are the destination
            if (prefix_cmp(&argv[dest_index]->s, mapper_device_name(dev),
                           &local_signal_name)==0) {
                local_signal = mapper_db_device_signal_by_name(&net->db, dev,
                                                               local_signal_name);
            }
        }
        else {
            // check if we are a source – all sources must match!
            for (i = 0; i < num_sources; i++) {
                if (prefix_cmp(&argv[src_index+i]->s, mapper_device_name(dev),
                               &local_signal_name)) {
                    local_signal = 0;
                    break;
                }
                local_signal = mapper_db_device_signal_by_name(&net->db, dev,
                                                               local_signal_name);
            }
        }
    }

#ifdef DEBUG
    if (dev)
        printf("-- <%s> %s /mapped", mapper_device_name(dev) ?: "",
               local_signal ? "got" : "ignoring");
    else
        printf("-- <admin> got /mapped");
    if (src_index)
        printf(" %s <-", &argv[dest_index]->s);
    for (i = 0; i < num_sources; i++)
        printf(" %s", &argv[src_index+i]->s);
    if (!src_index)
        printf(" -> %s", &argv[dest_index]->s);
    printf("\n");
#endif
    if (!local_signal) {
        if (adm) {
            const char *src_names[num_sources];
            for (i = 0; i < num_sources; i++) {
                src_names[i] = &argv[src_index+i]->s;
            }
            mapper_db_add_or_update_map_params(&net->db, num_sources, src_names,
                                               &argv[dest_index]->s, params);
        }
        mapper_message_free(params);
        return 0;
    }

    mapper_message_atom atom = mapper_message_param(params, AT_ID);
    if (!atom || atom->types[0] != 'h') {
        trace("<%s> ignoring /mapped, no 'id' property.\n",
              mapper_device_name(dev));
        mapper_message_free(params);
        return 0;
    }
    uint64_t id = (atom->values[0])->i64;

    if (src_index) {
        map = mapper_router_find_outgoing_map_by_id(dev->local->router,
                                                    local_signal, id);
    }
    else {
        map = mapper_router_find_incoming_map_by_id(dev->local->router,
                                                    local_signal, id);
    }
    if (!map) {
        trace("<%s> no map found for /mapped.\n", mapper_device_name(dev));
        mapper_message_free(params);
        return 0;
    }
    if (src_index && map->num_sources != num_sources) {
        trace("<%s> wrong num_sources in /mapped.\n", mapper_device_name(dev));
        mapper_message_free(params);
        return 0;
    }

    if (map->local->is_local) {
        // no need to update since all properties are local
        mapper_message_free(params);
        return 0;
    }

    // TODO: if this endpoint is map admin, do not allow overwiting props
    int updated = mapper_map_set_from_message(map, params, 0);

    if (map->local->status == MAPPER_READY) {
        // Inform remote peer(s)
        if (map->destination.direction == MAPPER_OUTGOING) {
            mapper_network_set_bundle_dest_mesh(net,
                                                map->destination.local->link->admin_addr);
            mapper_network_send_map(net, map, -1, MSG_MAPPED);
        }
        else {
            for (i = 0; i < map->num_sources; i++) {
                mapper_network_set_bundle_dest_mesh(net,
                                                    map->sources[i].local->link->admin_addr);
                i = mapper_network_send_map(net, map,
                                            map->local->one_source ? -1 : i,
                                            MSG_MAPPED);
            }
        }
        updated++;
    }
    if (map->local->status >= MAPPER_READY && updated) {
        // Update map counts
        if (map->destination.direction == MAPPER_OUTGOING) {
            ++dev->num_outgoing_maps;
            ++map->destination.local->link->num_outgoing_maps;
        }
        else {
            ++dev->num_incoming_maps;
            mapper_link link = 0;
            for (i = 0; i < map->num_sources; i++) {
                if (!map->sources[i].local->link
                    || map->sources[i].local->link == link)
                    continue;
                link = map->sources[i].local->link;
                ++link->num_incoming_maps;
            }
        }
        if (net->subscribers) {
            // Inform subscribers
            mapper_network_set_bundle_dest_subscribers(net,
                                                       SUBSCRIBE_DEVICE_MAPS_IN);
            mapper_network_send_map(net, map, -1, MSG_MAPPED);
        }
        map->local->status = MAPPER_ACTIVE;

        // Call local map handler if it exists
        mapper_device_map_handler *h = dev->local->map_handler;
        if (h)
            h(map, MAPPER_ADDED, dev->local->map_handler_userdata);
    }
    mapper_message_free(params);
    return 0;
}

/*! Modify the mapping properties : mode, range, expression,
 *  boundMin, boundMax, etc. */
static int handler_modify_map(const char *path, const char *types, lo_arg **argv,
                              int argc, lo_message msg, void *user_data)
{
    mapper_network net = (mapper_network) user_data;
    mapper_device dev = net->device;
    mapper_map map = 0;
    mapper_signal local_signal = 0;
    int i, num_sources, src_index, dest_index, param_index;
    const char *local_signal_name;

    if (argc < 4)
        return 0;

    num_sources = parse_signal_names(types, argv, argc, &src_index,
                                     &dest_index, &param_index);
    if (!num_sources)
        return 0;

    if (!is_alphabetical(num_sources, &argv[src_index])) {
        trace("error in /map: signal names out of order.");
        return 0;
    }
    const char *src_names[num_sources];
    for (i = 0; i < num_sources; i++) {
        src_names[i] = &argv[src_index+i]->s;
    }

    // check if we are the destination
    if (prefix_cmp(&argv[dest_index]->s, mapper_device_name(dev),
                   &local_signal_name)==0) {
        local_signal = mapper_db_device_signal_by_name(&net->db, dev,
                                                       local_signal_name);
        if (!local_signal) {
            trace("<%s> no signal found with name '%s'.\n",
                  mapper_device_name(dev), local_signal_name);
            return 0;
        }
        map = mapper_router_find_incoming_map(dev->local->router, local_signal,
                                              num_sources, src_names);
    }
    else {
        // check if we are a source – all sources must match!
        for (i = 0; i < num_sources; i++) {
            if (prefix_cmp(src_names[i], mapper_device_name(dev),
                           &local_signal_name)) {
                local_signal = 0;
                break;
            }
            local_signal = mapper_db_device_signal_by_name(&net->db, dev,
                                                           local_signal_name);
            if (!local_signal) {
                trace("<%s> no signal found with name '%s'.\n",
                      mapper_device_name(dev), local_signal_name);
                break;
            }
        }
        if (local_signal)
            map = mapper_router_find_outgoing_map(dev->local->router, local_signal,
                                                  num_sources, src_names,
                                                  &argv[dest_index]->s);
    }

#ifdef DEBUG
    printf("-- <%s> %s /map/modify", mapper_device_name(dev),
           map ? "got" : "ignoring");
    if (src_index)
        printf(" %s <-", &argv[dest_index]->s);
    for (i = 0; i < num_sources; i++)
        printf(" %s", &argv[src_index+i]->s);
    if (!src_index)
        printf(" -> %s", &argv[dest_index]->s);
    printf("\n");
#endif
    if (!map) {
        return 0;
    }

    if (!map->local->is_admin) {
        trace("<%s> ignoring /map/modify, slaved to remote device.\n",
              mapper_device_name(dev));
        return 0;
    }

    mapper_message params = mapper_message_parse_params(argc-param_index,
                                                        &types[param_index],
                                                        &argv[param_index]);
    if (!params) {
        trace("<%s> ignoring /map/modify, no properties.\n",
              mapper_device_name(dev));
        return 0;
    }

    int updated = mapper_map_set_from_message(map, params, 1);

    // TODO: check for self-connections, don't need to send updates

    if (updated) {
        // TODO: may not need to inform all remote peers
        // Inform remote peer(s) of relevant changes
        if (!map->destination.local->router_sig) {
            mapper_network_set_bundle_dest_mesh(net,
                                                map->destination.local->link->admin_addr);
            mapper_network_send_map(net, map, -1, MSG_MAPPED);
        }
        else {
            for (i = 0; i < map->num_sources; i++) {
                if (map->sources[i].local->router_sig)
                    continue;
                mapper_network_set_bundle_dest_mesh(net,
                                                    map->sources[i].local->link->admin_addr);
                i = mapper_network_send_map(net, map, i, MSG_MAPPED);
            }
        }

        if (net->subscribers) {
            // Inform subscribers
            if (map->destination.local->router_sig)
                mapper_network_set_bundle_dest_subscribers(net,
                                                           SUBSCRIBE_DEVICE_MAPS_IN);
            else
                mapper_network_set_bundle_dest_subscribers(net,
                                                           SUBSCRIBE_DEVICE_MAPS_OUT);
            mapper_network_send_map(net, map, -1, MSG_MAPPED);
        }

        // Call local map handler if it exists
        mapper_device_map_handler *h = dev->local->map_handler;
        if (h)
            h(map, MAPPER_MODIFIED, dev->local->map_handler_userdata);
    }
    mapper_message_free(params);
    return 0;
}

/*! Unmap a set of signals. */
static int handler_unmap(const char *path, const char *types, lo_arg **argv,
                         int argc, lo_message msg, void *user_data)
{
    mapper_network net = (mapper_network) user_data;
    mapper_device dev = net->device;
    mapper_signal local_signal;
    mapper_map map = 0;
    int i, num_sources, src_index, dest_index;
    const char *local_signal_name;

    num_sources = parse_signal_names(types, argv, argc, &src_index,
                                     &dest_index, 0);
    if (!num_sources)
        return 0;

    if (!is_alphabetical(num_sources, &argv[src_index])) {
        trace("error in /unmap: signal names out of order.");
        return 0;
    }
    const char *src_names[num_sources];
    for (i = 0; i < num_sources; i++) {
        src_names[i] = &argv[src_index+i]->s;
    }

    // check if we are the destination
    if (prefix_cmp(&argv[dest_index]->s, mapper_device_name(dev),
                   &local_signal_name)==0) {
        local_signal = mapper_db_device_signal_by_name(&net->db, dev,
                                                       local_signal_name);
        if (!local_signal) {
            trace("<%s> no signal found with name '%s'.\n",
                  mapper_device_name(dev), local_signal_name);
            return 0;
        }
        map = mapper_router_find_incoming_map(dev->local->router, local_signal,
                                              num_sources, src_names);
    }
    else {
        // check if we are a source – all sources must match!
        for (i = 0; i < num_sources; i++) {
            if (prefix_cmp(src_names[i], mapper_device_name(dev),
                           &local_signal_name)) {
                local_signal = 0;
                break;
            }
            local_signal = mapper_db_device_signal_by_name(&net->db, dev,
                                                           local_signal_name);
            if (!local_signal) {
                trace("<%s> no signal found with name '%s'.\n",
                      mapper_device_name(dev), local_signal_name);
                break;
            }
        }
        if (local_signal)
            map = mapper_router_find_outgoing_map(dev->local->router, local_signal,
                                                  num_sources, src_names,
                                                  &argv[dest_index]->s);
    }

#ifdef DEBUG
    printf("-- <%s> %s /unmap", mapper_device_name(dev), map ? "got" : "ignoring");
    if (src_index)
        printf(" %s <-", &argv[dest_index]->s);
    for (i = 0; i < num_sources; i++)
        printf(" %s", &argv[src_index+i]->s);
    if (!src_index)
        printf(" -> %s", &argv[dest_index]->s);
    printf("\n");
#endif
    if (!map) {
        return 0;
    }

    if (map->local->is_admin) {
        // inform remote peer(s)
        if (!map->destination.local->router_sig) {
            mapper_network_set_bundle_dest_mesh(net,
                                                map->destination.local->link->admin_addr);
            mapper_network_send_map(net, map, -1, MSG_UNMAP);
        }
        else {
            for (i = 0; i < map->num_sources; i++) {
                if (map->sources[i].local->router_sig)
                    continue;
                mapper_network_set_bundle_dest_mesh(net,
                                                    map->sources[i].local->link->admin_addr);
                i = mapper_network_send_map(net, map, i, MSG_UNMAP);
            }
        }
    }

    if (net->subscribers) {
        // Inform subscribers
        if (map->destination.local->router_sig)
            mapper_network_set_bundle_dest_subscribers(net, SUBSCRIBE_DEVICE_MAPS_IN);
        else
            mapper_network_set_bundle_dest_subscribers(net, SUBSCRIBE_DEVICE_MAPS_OUT);
        mapper_network_send_map(net, map, -1, MSG_UNMAPPED);
    }

    // Call local map handler if it exists
    mapper_device_map_handler *h = dev->local->map_handler;
    if (h)
        h(map, MAPPER_REMOVED, dev->local->map_handler_userdata);

    /* The mapping is removed. */
    mapper_router_remove_map(dev->local->router, map);
    // TODO: remove empty router_signals
    return 0;
}

/*! Respond to /unmapped by removing map from database. */
static int handler_unmapped(const char *path, const char *types, lo_arg **argv,
                            int argc, lo_message msg, void *user_data)
{
    // TODO: devices should check if they are target, clean up old maps
    mapper_network net = (mapper_network) user_data;
    mapper_admin adm = net->admin;
    int i, id_index;
    uint64_t *id = 0;

    for (i = 0; i < argc; i++) {
        if (types[i] != 's' && types[i] != 'S')
            return 0;
        if (strcmp(&argv[i]->s, "@id")==0 && (types[i+1] == 'h')) {
            id_index = i+1;
            id = (uint64_t*)&argv[id_index]->i64;
            break;
        }
    }
    if (!id) {
        trace("error: no 'id' property found in /unmapped message.")
        return 0;
    }

#ifdef DEBUG
    printf("-- <admin> got /unmapped");
    for (i = 0; i < id_index; i++)
        printf(" %s", &argv[i]->s);
#endif

    mapper_db db = mapper_admin_db(adm);
    mapper_map map = mapper_db_map_by_id(db, *id);
    if (map && map->local)
        mapper_db_remove_map(db, map);

    return 0;
}

static int handler_ping(const char *path, const char *types, lo_arg **argv,
                        int argc,lo_message msg, void *user_data)
{
    mapper_network net = (mapper_network) user_data;
    mapper_device dev = net->device;
    mapper_clock_t *clock = &net->clock;

    if (!dev)
        return 0;

    mapper_timetag_t now;
    mapper_clock_now(clock, &now);
    lo_timetag then = lo_message_get_timestamp(msg);

    mapper_link link = mapper_router_find_link_by_remote_id(dev->local->router,
                                                            argv[0]->h);
    if (link) {
        if (argv[2]->i == link->clock.sent.message_id) {
            // total elapsed time since ping sent
            double elapsed = mapper_timetag_difference(now, link->clock.sent.timetag);
            // assume symmetrical latency
            double latency = (elapsed - argv[3]->d) * 0.5;
            // difference between remote and local clocks (latency compensated)
            double offset = mapper_timetag_difference(now, then) - latency;

            if (latency < 0) {
                trace("error: latency cannot be < 0 (%f).\n", latency);
                latency = 0;
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
        mapper_timetag_copy(&link->clock.response.timetag, now);
        link->clock.response.message_id = argv[1]->i;
    }
    return 0;
}

static int handler_sync(const char *path, const char *types, lo_arg **argv,
                        int argc, lo_message msg, void *user_data)
{
    mapper_network net = (mapper_network) user_data;
    mapper_admin adm = net->admin;

    if (!adm || !argc)
        return 0;

    mapper_device dev = 0;
    if (types[0] == 's' || types[0] == 'S') {
        if ((dev = mapper_db_device_by_name(&net->db, &argv[0]->s))) {
            mapper_timetag_copy(&dev->synced, lo_message_get_timestamp(msg));
        }
        if (adm->autosubscribe && (!dev || !dev->subscribed)) {
            // only create device record after requesting more information
            if (dev) {
                mapper_admin_subscribe(adm, dev, adm->autosubscribe, -1);
                dev->subscribed = 1;
            }
            else {
                mapper_device_t temp;
                temp.name = &argv[0]->s;
                mapper_admin_subscribe(adm, &temp, adm->autosubscribe, 0);
            }
        }
    }
    else if (types[0] == 'i') {
        if ((dev = mapper_db_device_by_id(&net->db, argv[0]->i)))
            mapper_timetag_copy(&dev->synced, lo_message_get_timestamp(msg));
    }

    return 0;
}
