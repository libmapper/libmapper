
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

extern const char* prop_message_strings[NUM_AT_PROPERTIES];

// set to 1 to force mesh comms to use multicast bus instead for debugging
#define FORCE_COMMS_TO_BUS      0

#define BUNDLE_DEST_SUBSCRIBERS (void*)-1
#define BUNDLE_DEST_BUS         0

#define MAX_BUNDLE_COUNT 10

/* Note: any call to liblo where get_liblo_error will be called afterwards must
 * lock this mutex, otherwise there is a race condition on receiving this
 * information.  Could be fixed by the liblo error handler having a user context
 * pointer. */
static int liblo_error_num = 0;
static void liblo_error_handler(int num, const char *msg, const char *path)
{
    liblo_error_num = num;
    if (num == LO_NOPORT) {
        trace_net("liblo could not start a server because port unavailable\n");
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

/* Extract the ordinal from a device name in the format: <name>.<n> */
static int extract_ordinal(char *name) {
    char *s = name;
    s = strrchr(s, '.');
    if (!s)
        return -1;
    int ordinal = atoi(s+1);
    *s = 0;
    return ordinal;
}

const char* network_message_strings[] =
{
    "/device",                  /* MSG_DEVICE */
    "/%s/modify",               /* MSG_DEVICE_MODIFY */
    "/linked",                  /* MSG_LINKED */
    "/link/modify",             /* MSG_LINK_MODIFY */
    "/logout",                  /* MSG_LOGOUT */
    "/map",                     /* MSG_MAP */
    "/mapTo",                   /* MSG_MAP_TO */
    "/mapped",                  /* MSG_MAPPED */
    "/map/modify",              /* MSG_MAP_MODIFY */
    "/name/probe",              /* MSG_NAME_PROBE */
    "/name/registered",         /* MSG_NAME_REG */
    "/ping",                    /* MSG_PING */
    "/signal",                  /* MSG_SIGNAL */
    "/signal/removed",          /* MSG_SIGNAL_REMOVED */
    "/%s/signal/modify",        /* MSG_SIGNAL_MODIFY */
    "/%s/subscribe",            /* MSG_SUBSCRIBE */
    "/sync",                    /* MSG_SYNC */
    "/unlinked",                /* MSG_UNLINKED */
    "/unmap",                   /* MSG_UNMAP */
    "/unmapped",                /* MSG_UNMAPPED */
    "/%s/unsubscribe",          /* MSG_UNSUBSCRIBE */
    "/who",                     /* MSG_WHO */
};

#define HANDLER_ARGS const char*, const char*, lo_arg**, int, lo_message, void*
/* Internal message handler prototypes. */
static int handler_device(HANDLER_ARGS);
static int handler_device_modify(HANDLER_ARGS);
static int handler_linked(HANDLER_ARGS);
static int handler_link_modify(HANDLER_ARGS);
static int handler_logout(HANDLER_ARGS);
static int handler_map(HANDLER_ARGS);
static int handler_map_to(HANDLER_ARGS);
static int handler_mapped(HANDLER_ARGS);
static int handler_map_modify(HANDLER_ARGS);
static int handler_probe(HANDLER_ARGS);
static int handler_registered(HANDLER_ARGS);
static int handler_ping(HANDLER_ARGS);
static int handler_signal(HANDLER_ARGS);
static int handler_signal_removed(HANDLER_ARGS);
static int handler_signal_modify(HANDLER_ARGS);
static int handler_subscribe(HANDLER_ARGS);
static int handler_sync(HANDLER_ARGS);
static int handler_unlinked(HANDLER_ARGS);
static int handler_unmap(HANDLER_ARGS);
static int handler_unmapped(HANDLER_ARGS);
static int handler_unsubscribe(HANDLER_ARGS);
static int handler_who(HANDLER_ARGS);

/* Handler <-> Message relationships */
struct handler_method_assoc {
    int str_index;
    char *types;
    lo_method_handler h;
};

// handlers needed by devices
static struct handler_method_assoc device_handlers[] = {
    {MSG_DEVICE,                NULL,       handler_device},
    {MSG_DEVICE_MODIFY,         NULL,       handler_device_modify},
    {MSG_LINKED,                NULL,       handler_linked},
    {MSG_LINK_MODIFY,           NULL,       handler_link_modify},
    {MSG_LOGOUT,                NULL,       handler_logout},
    {MSG_MAP,                   NULL,       handler_map},
    {MSG_MAP_TO,                NULL,       handler_map_to},
    {MSG_MAPPED,                NULL,       handler_mapped},
    {MSG_MAP_MODIFY,            NULL,       handler_map_modify},
    {MSG_PING,                  "hiid",     handler_ping},
    {MSG_SIGNAL_MODIFY,         NULL,       handler_signal_modify},
    {MSG_SUBSCRIBE,             NULL,       handler_subscribe},
    {MSG_UNMAP,                 NULL,       handler_unmap},
    {MSG_UNSUBSCRIBE,           NULL,       handler_unsubscribe},
    {MSG_WHO,                   NULL,       handler_who},
};
const int NUM_DEVICE_HANDLERS =
    sizeof(device_handlers)/sizeof(device_handlers[0]);

// handlers needed by database for archiving
static struct handler_method_assoc database_handlers[] = {
    {MSG_DEVICE,                NULL,       handler_device},
    {MSG_LINKED,                NULL,       handler_linked},
    {MSG_LOGOUT,                NULL,       handler_logout},
    {MSG_MAPPED,                NULL,       handler_mapped},
    {MSG_SIGNAL,                NULL,       handler_signal},
    {MSG_SIGNAL_REMOVED,        "s",        handler_signal_removed},
    {MSG_SYNC,                  NULL,       handler_sync},
    {MSG_UNLINKED,              "sss",      handler_unlinked},
    {MSG_UNMAPPED,              NULL,       handler_unmapped},
};
const int NUM_DATABASE_HANDLERS =
sizeof(database_handlers)/sizeof(database_handlers[0]);

/* Internal LibLo error handler */
static void handler_error(int num, const char *msg, const char *where)
{
    trace_net("[libmapper] liblo server error %d in path %s: %s\n", num, where, msg);
}

/* Functions for handling the resource allocation scheme.  If check_collisions()
 * returns 1, the resource in question should be probed on the libmapper bus. */
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

        /* Note, we could also check for IFF_MULTICAST-- however this is the
         * data-sending port, not the libmapper bus port. */

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
    FILE *f = fopen("/dev/urandom", "rb");
    if (f) {
        if (fread(&s, 4, 1, f)==1) {
            srand(s);
            fclose(f);
            return;
        }
        fclose(f);
    }
#endif

    double d = mapper_get_current_time();
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
        // make sure method isn't also used by a database
        if (net->database.autosubscribe || net->database.subscriptions) {
            int found = 0;
            for (j=0; j < NUM_DATABASE_HANDLERS; j++) {
                if (device_handlers[i].str_index == database_handlers[j].str_index) {
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

mapper_database mapper_network_add_database(mapper_network net)
{
    if (net->database_methods_added)
        return &net->database;

    // add database methods
    int i;
    for (i=0; i < NUM_DATABASE_HANDLERS; i++) {
        lo_server_add_method(net->bus_server,
                             network_message_strings[database_handlers[i].str_index],
                             database_handlers[i].types, database_handlers[i].h, net);
#if !FORCE_COMMS_TO_BUS
        lo_server_add_method(net->mesh_server,
                             network_message_strings[database_handlers[i].str_index],
                             database_handlers[i].types, database_handlers[i].h, net);
#endif
    }
    return &net->database;
}

void mapper_network_remove_database(mapper_network net)
{
    if (!net->database_methods_added)
        return;

    int i, j;
    for (i=0; i < NUM_DATABASE_HANDLERS; i++) {
        // make sure method isn't also used by a device
        if (net->device) {
            int found = 0;
            for (j=0; j < NUM_DEVICE_HANDLERS; j++) {
                if (database_handlers[i].str_index == device_handlers[j].str_index) {
                    found = 1;
                    break;
                }
            }
            if (found)
                continue;
        }
        lo_server_del_method(net->bus_server,
                             network_message_strings[database_handlers[i].str_index],
                             database_handlers[i].types);
#if !FORCE_COMMS_TO_BUS
        lo_server_del_method(net->mesh_server,
                             network_message_strings[database_handlers[i].str_index],
                             database_handlers[i].types);
#endif
    }
    net->database_methods_added = 0;
}

mapper_network mapper_network_new(const char *iface, const char *group, int port)
{
    mapper_network net = (mapper_network) calloc(1, sizeof(mapper_network_t));
    if (!net)
        return NULL;

    net->own_network = 1;
    net->database.network = net;
    net->database.timeout_sec = TIMEOUT_SEC;
    net->interface_name = 0;

    /* Default standard ip and port is group 224.0.1.3, port 7570 */
    char port_str[10], *s_port = port_str;
    if (!group) group = "224.0.1.3";
    if (port==0)
        s_port = "7570";
    else
        snprintf(port_str, 10, "%d", port);

    /* Initialize interface information. */
    if (get_interface_addr(iface, &net->interface_ip, &net->interface_name)) {
        trace_net("no interface found\n");
    }
    else {
        trace_net("using interface '%s'\n", net->interface_name);
    }

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

const char *mapper_version()
{
    return PACKAGE_VERSION;
}

mapper_database mapper_network_database(mapper_network net)
{
    return &net->database;
}

void mapper_network_send(mapper_network net)
{
    if (!net->bundle)
        return;

#if FORCE_COMMS_TO_BUS
    lo_send_bundle_from(net->bus_addr, net->mesh_server, net->bundle);
#else
    if (net->bundle_dest == BUNDLE_DEST_SUBSCRIBERS) {
        mapper_subscriber *s = &net->device->local->subscribers;
        mapper_timetag_t tt;
        if (*s) {
            mapper_timetag_now(&tt);
        }
        while (*s) {
            if ((*s)->lease_expiration_sec < tt.sec || !(*s)->flags) {
                // subscription expired, remove from subscriber list
                trace_dev(net->device, "removing expired subscription from "
                          "%s:%s\n", lo_address_get_hostname((*s)->address),
                          lo_address_get_port((*s)->address));
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
    lo_bundle_free_recursive(net->bundle);
    net->bundle = 0;
}

int mapper_network_init(mapper_network net)
{
    if (net->bundle)
        mapper_network_send(net);

    mapper_timetag_t tt;
    mapper_timetag_now(&tt);
    net->bundle = lo_bundle_new(tt);
    if (!net->bundle) {
        trace_net("couldn't allocate lo_bundle\n");
        return 1;
    }

    return 0;
}

void mapper_network_set_dest_bus(mapper_network net)
{
    if (net->bundle && (   net->bundle_dest != BUNDLE_DEST_BUS
                        || lo_bundle_count(net->bundle) >= MAX_BUNDLE_COUNT))
        mapper_network_send(net);
    net->bundle_dest = BUNDLE_DEST_BUS;
    if (!net->bundle)
        mapper_network_init(net);
}

void mapper_network_set_dest_mesh(mapper_network net, lo_address address)
{
    if (net->bundle && (   net->bundle_dest != address
                        || lo_bundle_count(net->bundle) >= MAX_BUNDLE_COUNT))
        mapper_network_send(net);
    net->bundle_dest = address;
    if (!net->bundle)
        mapper_network_init(net);
}

void mapper_network_set_dest_subscribers(mapper_network net, int type)
{
    if (net->bundle && (   net->bundle_dest != BUNDLE_DEST_SUBSCRIBERS
                        || net->message_type != type
                        || lo_bundle_count(net->bundle) >= MAX_BUNDLE_COUNT))
        mapper_network_send(net);
    net->bundle_dest = BUNDLE_DEST_SUBSCRIBERS;
    net->message_type = type;
    if (!net->bundle)
        mapper_network_init(net);
}

void mapper_network_add_message(mapper_network net, const char *str,
                                network_message_t cmd, lo_message msg)
{
    if (lo_bundle_count(net->bundle) >= MAX_BUNDLE_COUNT) {
        mapper_network_send(net);
        mapper_network_init(net);
    }
    lo_bundle_add_message(net->bundle, str ?: network_message_strings[cmd], msg);
}

void mapper_network_free_messages(mapper_network net)
{
    if (net->bundle)
        lo_bundle_free_recursive(net->bundle);
    net->bundle = 0;
}

/*! Free the memory allocated by a mapper network structure.
 *  \param network An network structure handle.
 */
void mapper_network_free(mapper_network net)
{
    if (!net)
        return;

    if (net->own_network)
        mapper_database_free(&net->database);

    // send out any cached messages
    mapper_network_send(net);

    if (net->interface_name)
        free(net->interface_name);

    if (net->bus_server)
        lo_server_free(net->bus_server);

    if (net->mesh_server)
        lo_server_free(net->mesh_server);

    if (net->bus_addr)
        lo_address_free(net->bus_addr);

    free(net);
}

/*! Probe the libmapper bus to see if a device's proposed name.ordinal is
 *  already taken. */
static void mapper_network_probe_device_name(mapper_network net,
                                             mapper_device dev)
{
    int i;

    // reset collisions and hints
    dev->local->ordinal.collision_count = 0;
    dev->local->ordinal.count_time = mapper_get_current_time();
    for (i = 0; i < 8; i++) {
        dev->local->ordinal.hints[i] = 0;
    }

    /* Note: mapper_device_name() would refuse here since the ordinal is not yet
     * locked, so we have to build it manually at this point. */
    char name[256];
    snprintf(name, 256, "%s.%d", dev->identifier, dev->local->ordinal.value);
    trace_dev(dev, "probing name '%s'\n", name);

    /* Calculate an id from the name and store it in id.value */
    dev->id = (mapper_id) crc32(0L, (const Bytef *)name, strlen(name)) << 32;

    /* For the same reason, we can't use mapper_network_send() here. */
    lo_send(net->bus_addr, network_message_strings[MSG_NAME_PROBE], "si",
            name, net->random_id);
}

/*! Add an uninitialized device to this network. */
void mapper_network_add_device(mapper_network net, mapper_device dev)
{
    /* Initialize data structures */
    if (dev) {
        net->device = dev;

        /* Seed the random number generator. */
        seed_srand();

        /* Choose a random ID for allocation speedup */
        net->random_id = rand();

        /* Add methods for libmapper bus.  Only add methods needed for
         * allocation here. Further methods are added when the device is
         * registered. */
        lo_server_add_method(net->bus_server,
                             network_message_strings[MSG_NAME_PROBE],
                             "si", handler_probe, net);
        lo_server_add_method(net->bus_server,
                             network_message_strings[MSG_NAME_REG],
                             NULL, handler_registered, net);

        /* Probe potential name to libmapper bus. */
        mapper_network_probe_device_name(net, dev);
    }
}

void mapper_network_remove_device(mapper_network net, mapper_device dev)
{
    if (!dev)
        return;
    mapper_network_remove_device_methods(net, dev);
    // TODO: should release of local device trigger local device handler?
    mapper_database_remove_device(&net->database, dev, MAPPER_REMOVED, 0);
    net->device = 0;
}

static void _send_device_sync(mapper_network net, mapper_device dev)
{
    lo_message msg = lo_message_new();
    if (!msg) {
        trace_net("couldn't allocate lo_message\n");
        return;
    }
    lo_message_add_string(msg, mapper_device_name(dev));
    lo_message_add_int32(msg, dev->version);
    mapper_network_add_message(net, 0, MSG_SYNC, msg);
}

// TODO: rename to mapper_device...?
static void mapper_network_maybe_send_ping(mapper_network net, int force)
{
    mapper_device dev = net->device;
    mapper_timetag_t now;
    mapper_timetag_now(&now);
    if (now.sec > net->next_sub_ping) {
        net->next_sub_ping = now.sec + 2;

        // housekeeping #1: check for staged maps that have expired
        mapper_database_cleanup(&net->database);

        if (!dev)
            return;
        if (dev->local->subscribers) {
            mapper_network_set_dest_subscribers(net, MAPPER_OBJ_DEVICES);
            _send_device_sync(net, dev);
        }
    }

    if (!dev || (!force && (now.sec < net->next_bus_ping)))
        return;
    net->next_bus_ping = now.sec + 5 + (rand() % 4);

    mapper_network_set_dest_bus(net);
    _send_device_sync(net, dev);

    int elapsed, num_maps;
    // housekeeping #2: periodically check if our links are still active
    mapper_link next, link = dev->database->links;
    while (link) {
        next = mapper_list_next(link);
        if (!link->local || (link->remote_device == dev)) {
            link = next;
            continue;
        }
        num_maps = link->num_maps[0] + link->num_maps[1];
        mapper_sync_clock sync = &link->local->clock;
        elapsed = (sync->response.timetag.sec
                   ? now.sec - sync->response.timetag.sec : 0);
        if ((dev->local->link_timeout_sec
             && elapsed > dev->local->link_timeout_sec)) {
            if (sync->response.message_id > 0) {
                if (num_maps) {
                    trace_dev(dev, "Lost contact with linked device '%s' (%d "
                              "seconds since sync).\n",
                              link->remote_device->name, elapsed);
                }
                // tentatively mark link as expired
                sync->response.message_id = -1;
                sync->response.timetag.sec = now.sec;
            }
            else {
                if (num_maps) {
                    trace_dev(dev, "Removing link to unresponsive device '%s' "
                              "(%d seconds since warning).\n",
                              link->remote_device->name, elapsed);
                    /* TODO: release related maps, call local handlers
                     * and inform subscribers. */
                }
                else {
                    trace_dev(dev, "Removing link to device '%s'.\n",
                              link->remote_device->name);
                }
                // Inform subscribers
                if (dev->local->subscribers) {
                    trace_dev(dev, "informing subscribers (UNLINKED)\n")
                    mapper_network_set_dest_subscribers(net, MAPPER_OBJ_LINKS);
                    mapper_link_send_state(link, MSG_UNLINKED, 0);
                }
                // Call local link handler if it exists
                mapper_device_link_handler *h = dev->local->link_handler;
                if (h)
                    h(dev, link, num_maps ? MAPPER_EXPIRED : MAPPER_REMOVED);
                // remove related data structures
                mapper_router_remove_link(dev->local->router, link);
                mapper_database_remove_link(dev->database, link, num_maps
                                            ? MAPPER_EXPIRED : MAPPER_REMOVED);
            }
        }
        else if (mapper_device_host(link->remote_device) && num_maps) {
            /* Only send pings if this link has associated maps, ensuring empty
             * links are removed after the ping timeout. */
            lo_bundle b = lo_bundle_new(now);
            lo_message m = lo_message_new();
            lo_message_add_int64(m, mapper_device_id(dev));
            ++sync->sent.message_id;
            if (sync->sent.message_id < 0)
                sync->sent.message_id = 0;
            lo_message_add_int32(m, sync->sent.message_id);
            lo_message_add_int32(m, sync->response.message_id);
            if (sync->response.timetag.sec)
                lo_message_add_double(m, mapper_timetag_difference(now,
                                                                   sync->response.timetag));
            else
                lo_message_add_double(m, 0.);
            // need to send immediately
            lo_bundle_add_message(b, network_message_strings[MSG_PING], m);
#if FORCE_COMMS_TO_BUS
            lo_send_bundle_from(net->bus_addr, net->mesh_server, b);
#else
            lo_send_bundle_from(link->local->admin_addr, net->mesh_server, b);
#endif
            mapper_timetag_copy(&sync->sent.timetag, lo_bundle_get_timestamp(b));
            lo_bundle_free_recursive(b);
        }
        link = next;
    }
}

/*! This is the main function to be called once in a while from a program so
 *  that the libmapper bus can be automatically managed. */
void mapper_network_poll(mapper_network net)
{
    int status;
    mapper_device dev = net->device;

    // send out any cached messages
    mapper_network_send(net);

    if (!dev) {
        mapper_network_maybe_send_ping(net, 0);
        return;
    }

    /* If the ordinal is not yet locked, process collision timing.
     * Once the ordinal is locked it won't change. */
    if (!dev->local->registered) {
        status = check_collisions(net, &dev->local->ordinal);
        if (status == 1) {
            /* If the ordinal has changed, re-probe the new name. */
            mapper_network_probe_device_name(net, dev);
        }

        /* If we are ready to register the device, add the message handlers. */
        if (dev->local->ordinal.locked) {
            mapper_device_registered(dev);

            /* Send registered msg. */
            lo_send(net->bus_addr, network_message_strings[MSG_NAME_REG],
                    "s", mapper_device_name(dev));

            mapper_network_add_device_methods(net, dev);
            mapper_network_maybe_send_ping(net, 1);

            trace_dev(dev, "registered.\n");
        }
    }
    else {
        // Send out clock sync messages occasionally
        mapper_network_maybe_send_ping(net, 0);
    }
    return;
}

const char *mapper_network_interface(mapper_network net)
{
    return net->interface_name;
}

const struct in_addr *mapper_network_ip4(mapper_network net)
{
    return &net->interface_ip;
}

const char *mapper_network_group(mapper_network net)
{
    return lo_address_get_hostname(net->bus_addr);
}

int mapper_network_port(mapper_network net)
{
    return lo_server_get_port(net->bus_server);
}

/*! Algorithm for checking collisions and allocating resources. */
static int check_collisions(mapper_network net, mapper_allocated resource)
{
    double timediff, current_time = mapper_get_current_time();
    int i;

    if (resource->locked)
        return 0;

    timediff = current_time - resource->count_time;

    if (!resource->online) {
        if (timediff >= 5.0) {
            // reprobe with the same value
            resource->count_time = current_time;
            return 1;
        }
        return 0;
    }
    else if (timediff >= 2.0 && resource->collision_count < 1) {
        resource->locked = 1;
        if (resource->on_lock)
            resource->on_lock(resource);
        return 2;
    }
    else if (timediff >= 0.5 && resource->collision_count > 0) {
        for (i = 0; i < 8; i++) {
            if (!resource->hints[i]) {
                break;
            }
        }
        resource->value += i + 1;

        /* Prepare for causing new resource collisions. */
        resource->collision_count = 0;
        resource->count_time = current_time;
        for (i = 0; i < 8; i++) {
            resource->hints[i] = 0;
        }

        /* Indicate that we need to re-probe the new value. */
        return 1;
    }

    return 0;
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

    trace_dev(net->device, "received /who\n");

    return 0;
}

/*! Register information about port and host for the device. */
static int handler_device(const char *path, const char *types,
                          lo_arg **argv, int argc, lo_message msg,
                          void *user_data)
{
    mapper_network net = (mapper_network) user_data;
    mapper_device dev = net->device;
    int i, j;
    mapper_message props = 0;

    if (argc < 1)
        return 0;

    if (types[0] != 's' && types[0] != 'S')
        return 0;

    const char *name = &argv[0]->s;

    if (net->database.autosubscribe
        || mapper_database_subscribed_by_device_name(&net->database, name)) {
        props = mapper_message_parse_properties(argc-1, &types[1], &argv[1]);
        trace_net("got /device %s + %i arguments\n", name, argc-1);
        mapper_device remote;
        remote = mapper_database_add_or_update_device(&net->database, name, props);
        if (!remote->subscribed && net->database.autosubscribe) {
            mapper_database_subscribe(&net->database, remote,
                                      net->database.autosubscribe, -1);
        }
    }
    if (dev) {
        if (strcmp(&argv[0]->s, mapper_device_name(dev))) {
            trace_dev(dev, "got /device %s\n", &argv[0]->s);
        }
        else {
            // ignore own messages
            trace_dev(dev, "ignoring /device %s\n", &argv[0]->s)
            goto done;
        }
    }
    else
        goto done;

    // Discover whether the device is linked.
    mapper_device remote = mapper_database_device_by_name(dev->database, name);
    mapper_link link = dev ? mapper_device_link_by_remote_device(dev, remote) : 0;
    if (!link) {
        trace_dev(dev, "ignoring /device '%s', no link.\n", name);
        goto done;
    }
    else if (link->local && link->local->admin_addr) {
        // already have metadata, can ignore this message
        trace_dev(dev, "ignoring /device '%s', link already set.\n", name);
        goto done;
    }

    lo_address a = lo_message_get_source(msg);
    if (!a) {
        trace_dev(dev, "can't perform /linkTo, address unknown\n");
        goto done;
    }
    // Find the sender's hostname
    const char *host = lo_address_get_hostname(a);
    const char *admin_port = lo_address_get_port(a);
    if (!host) {
        trace_dev(dev, "can't perform /linkTo, host unknown\n");
        goto done;
    }
    // Retrieve the port
    if (!props)
        props = mapper_message_parse_properties(argc-1, &types[1], &argv[1]);
    mapper_message_atom atom = mapper_message_property(props, AT_PORT);
    if (!atom || atom->length != 1 || atom->types[0] != 'i') {
        trace_dev(dev, "can't perform /linkTo, port unknown\n");
        goto done;
    }
    int data_port = (atom->values[0])->i;

    mapper_link_connect(link, host, atoi(admin_port), data_port);
    trace_dev(dev, "activated router to device '%s' at %s:%d\n", name, host,
              data_port);

    // send /linked to peer
    mapper_network_set_dest_mesh(net, link->local->admin_addr);
    mapper_link_send_state(link, MSG_LINKED, 0);

    // send /linked to interested subscribers
    if (dev->local->subscribers) {
        trace_dev(dev, "informing subscribers (LINKED)\n")
        mapper_network_set_dest_subscribers(net, MAPPER_OBJ_LINKS);
        mapper_link_send_state(link, MSG_LINKED, 0);
    }

    // Call local link handler if it exists
    mapper_device_link_handler *h = dev->local->link_handler;
    if (h)
        h(dev, link, MAPPER_ADDED);

    // check if we have maps waiting for this link
    mapper_router_signal rs = dev->local->router->signals;
    while (rs) {
        for (i = 0; i < rs->num_slots; i++) {
            if (!rs->slots[i])
                continue;
            mapper_map map = rs->slots[i]->map;
            if (rs->slots[i]->direction == MAPPER_DIR_OUTGOING) {
                // only send /mapTo once even if we have multiple local sources
                if (map->local->one_source && (rs->slots[i] != map->sources[0]))
                    continue;
                if (map->destination.link == link) {
                    mapper_network_set_dest_mesh(net, link->local->admin_addr);
                    mapper_map_send_state(map, -1, MSG_MAP_TO);
                }
            }
            else {
                for (j = 0; j < map->num_sources; j++) {
                    if (map->sources[j]->link != link)
                        continue;
                    mapper_network_set_dest_mesh(net, link->local->admin_addr);
                    j = mapper_map_send_state(map,
                                              map->local->one_source ? -1 : j,
                                              MSG_MAP_TO);
                }
            }
        }
        rs = rs->next;
    }
done:
    mapper_message_free(props);
    return 0;
}

/*! Handle remote requests to add, modify, or remove metadata to a device. */
static int handler_device_modify(const char *path, const char *types,
                                 lo_arg **argv, int argc, lo_message msg,
                                 void *user_data)
{
    mapper_network net = (mapper_network) user_data;
    mapper_device dev = net->device;

    if (!dev || !mapper_device_ready(dev))
        return 0;

    if (argc < 2)
        return 0;

    if (types[0] != 's' && types[0] != 'S')
        return 0;

    mapper_message props = mapper_message_parse_properties(argc, types, argv);

    trace_dev(dev, "got /%s/modify + %d properties.\n", path, props->num_atoms);

    if (mapper_device_set_from_message(dev, props)) {
        if (dev->local->subscribers) {
            trace_dev(dev, "informing subscribers (DEVICE)\n")
            mapper_network_set_dest_subscribers(net, MAPPER_OBJ_DEVICES);
            mapper_device_send_state(dev, MSG_DEVICE);
        }
        mapper_table_clear_empty_records(dev->props);
    }
    mapper_message_free(props);
    return 0;
}

/*! Respond to /logout by deleting record of device. */
static int handler_logout(const char *path, const char *types, lo_arg **argv,
                          int argc, lo_message msg, void *user_data)
{
    mapper_network net = (mapper_network) user_data;
    mapper_device dev = net->device, remote;
    mapper_link link;

    int diff, ordinal;
    char *s;

    if (argc < 1)
        return 0;

    if (types[0] != 's' && types[0] != 'S')
        return 0;

    char *name = &argv[0]->s;

    if (!dev) {
        trace_net("got /logout '%s'\n", name);
    }
    else if (dev->local->ordinal.locked) {
        trace_dev(dev, "got /logout '%s'\n", name);
        // Check if we have any links to this device, if so remove them
        remote = mapper_database_device_by_name(dev->database, name);
        link = remote ? mapper_device_link_by_remote_device(dev, remote) : 0;
        if (link) {
            // TODO: release maps, call local handlers and inform subscribers
            trace_dev(dev, "removing link to expired device '%s'.\n", name);

            // Inform subscribers
            if (dev->local->subscribers) {
                trace_dev(dev, "informing subscribers (UNLINKED)\n")
                mapper_network_set_dest_subscribers(net, MAPPER_OBJ_LINKS);
                mapper_link_send_state(link, MSG_UNLINKED, 0);
            }
            // Call local link handler if it exists
            mapper_device_link_handler *h = dev->local->link_handler;
            if (h)
                h(dev, link, MAPPER_REMOVED);
            // remove link immediately
            mapper_router_remove_link(dev->local->router, link);
            mapper_database_remove_link(&net->database, link, MAPPER_REMOVED);
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
            diff = ordinal - dev->local->ordinal.value - 1;
            if (diff >= 0 && diff < 8) {
                dev->local->ordinal.hints[diff] = 0;
            }
        }
    }

    dev = mapper_database_device_by_name(&net->database, name);
    if (dev) {
        // remove subscriptions
        mapper_database_unsubscribe(&net->database, dev);
        mapper_database_remove_device(&net->database, dev, MAPPER_REMOVED, 0);
    }

    return 0;
}

/*! Respond to /subscribe message by adding or renewing a subscription. */
static int handler_subscribe(const char *path, const char *types, lo_arg **argv,
                             int argc, lo_message msg, void *user_data)
{
    mapper_network net = (mapper_network) user_data;
    mapper_device dev = net->device;
    int version = -1;

    trace_dev(dev, "got /subscribe.\n");

    lo_address a  = lo_message_get_source(msg);
    if (!a || !argc) {
        trace_dev(dev, "error retrieving subscription source address.\n");
        return 0;
    }

    int i, flags = 0, timeout_seconds = 0;
    for (i = 0; i < argc; i++) {
        if (types[i] != 's' && types[i] != 'S')
            break;
        else if (strcmp(&argv[i]->s, "all")==0)
            flags = MAPPER_OBJ_ALL;
        else if (strcmp(&argv[i]->s, "device")==0)
            flags |= MAPPER_OBJ_DEVICES;
        else if (strcmp(&argv[i]->s, "signals")==0)
            flags |= MAPPER_OBJ_SIGNALS;
        else if (strcmp(&argv[i]->s, "inputs")==0)
            flags |= MAPPER_OBJ_INPUT_SIGNALS;
        else if (strcmp(&argv[i]->s, "outputs")==0)
            flags |= MAPPER_OBJ_OUTPUT_SIGNALS;
        else if (strcmp(&argv[i]->s, "links")==0)
            flags |= MAPPER_OBJ_LINKS;
        else if (strcmp(&argv[i]->s, "incoming_links")==0)
            flags |= MAPPER_OBJ_INCOMING_LINKS;
        else if (strcmp(&argv[i]->s, "outgoing_links")==0)
            flags |= MAPPER_OBJ_OUTGOING_LINKS;
        else if (strcmp(&argv[i]->s, "maps")==0)
            flags |= MAPPER_OBJ_MAPS;
        else if (strcmp(&argv[i]->s, "incoming_maps")==0)
            flags |= MAPPER_OBJ_INCOMING_MAPS;
        else if (strcmp(&argv[i]->s, "outgoing_maps")==0)
            flags |= MAPPER_OBJ_OUTGOING_MAPS;
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
                trace_dev(dev, "error parsing subscription @lease property.\n");
            }
        }
    }

    // add or renew subscription
    mapper_device_manage_subscriber(dev, a, flags, timeout_seconds, version);
    return 0;
}

/*! Respond to /unsubscribe message by removing a subscription. */
static int handler_unsubscribe(const char *path, const char *types,
                               lo_arg **argv, int argc, lo_message msg,
                               void *user_data)
{
    mapper_network net = (mapper_network) user_data;

    trace_dev(net->device, "got /unsubscribe.\n");

    lo_address a  = lo_message_get_source(msg);
    if (!a) return 0;

    // remove subscription
    mapper_device_manage_subscriber(net->device, a, 0, 0, 0);

    return 0;
}

/*! Register information about a signal. */
static int handler_signal(const char *path, const char *types, lo_arg **argv,
                          int argc, lo_message msg, void *user_data)
{
    mapper_network net = (mapper_network) user_data;

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

    trace_net("got /signal %s:%s\n", devname, signamep);

    mapper_message props = mapper_message_parse_properties(argc-1, &types[1],
                                                           &argv[1]);
    mapper_database_add_or_update_signal(&net->database, signamep, devname, props);
    mapper_message_free(props);
    return 0;
}

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

/*! Handle remote requests to add, modify, or remove metadata to a signal. */
static int handler_signal_modify(const char *path, const char *types,
                                 lo_arg **argv, int argc, lo_message msg,
                                 void *user_data)
{
    mapper_network net = (mapper_network) user_data;
    mapper_device dev = net->device;
    mapper_signal sig = 0;

    if (!dev || !mapper_device_ready(dev))
        return 0;

    if (argc < 2)
        return 0;

    if (types[0] != 's' && types[0] != 'S')
        return 0;

    // retrieve signal
    sig = mapper_device_signal_by_name(dev, &argv[0]->s);
    if (!sig) {
        trace_dev(dev, "no signal found with name '%s'.\n", &argv[0]->s);
        return 0;
    }

    mapper_message props = mapper_message_parse_properties(argc-1, &types[1],
                                                           &argv[1]);

    trace_dev(dev, "got %s '%s' + %d properties.\n", path, sig->name,
              props->num_atoms);

    if (mapper_signal_set_from_message(sig, props)) {
        if (dev->local->subscribers) {
            trace_dev(dev, "informing subscribers (SIGNAL)\n")
            if (sig->direction == MAPPER_DIR_OUTGOING)
                mapper_network_set_dest_subscribers(net, MAPPER_OBJ_OUTPUT_SIGNALS);
            else
                mapper_network_set_dest_subscribers(net, MAPPER_OBJ_INPUT_SIGNALS);
            mapper_signal_send_state(sig, MSG_SIGNAL);
        }
        mapper_table_clear_empty_records(sig->props);
    }
    mapper_message_free(props);
    return 0;
}

/*! Unregister information about a removed signal. */
static int handler_signal_removed(const char *path, const char *types,
                                  lo_arg **argv, int argc, lo_message msg,
                                  void *user_data)
{
    mapper_network net = (mapper_network) user_data;

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

    trace_net("got /signal/removed %s:%s\n", devname, signamep);

    mapper_device dev = mapper_database_device_by_name(&net->database, devname);
    if (dev && !dev->local) {
        mapper_signal sig = mapper_device_signal_by_name(dev, signamep);
        if (sig)
            mapper_database_remove_signal(&net->database, sig, MAPPER_REMOVED);
    }

    return 0;
}

/*! Repond to name collisions during allocation, help suggest IDs once allocated. */
static int handler_registered(const char *path, const char *types, lo_arg **argv,
                              int argc, lo_message msg, void *user_data)
{
    mapper_network net = (mapper_network) user_data;
    mapper_device dev = net->device;
    char *name;
    mapper_id id;
    int ordinal, diff, temp_id = -1, hint = 0;

    if (argc < 1)
        return 0;
    if (types[0] != 's' && types[0] != 'S')
        return 0;
    name = &argv[0]->s;
    if (argc > 1) {
        if (types[1] == 'i')
            temp_id = argv[1]->i;
        if (types[2] == 'i')
            hint = argv[2]->i;
    }

#ifdef DEBUG
    if (hint) {
        trace_dev(dev, "got /name/registered %s %i %i\n", name, temp_id, hint);
    }
    else {
        trace_dev(dev, "got /name/registered %s\n", name);
    }
#endif

    if (dev->local->ordinal.locked) {
        ordinal = extract_ordinal(name);
        if (ordinal == -1)
            return 0;

        // If device name matches
        if (strcmp(name, dev->identifier) == 0) {
            // if id is locked and registered id is within my block, store it
            diff = ordinal - dev->local->ordinal.value - 1;
            if (diff >= 0 && diff < 8) {
                dev->local->ordinal.hints[diff] = -1;
            }
            if (hint) {
                // if suggested id is within my block, store timestamp
                diff = hint - dev->local->ordinal.value - 1;
                if (diff >= 0 && diff < 8) {
                    dev->local->ordinal.hints[diff] = mapper_get_current_time();
                }
            }
        }
    }
    else {
        id = (mapper_id) crc32(0L, (const Bytef *)name, strlen(name)) << 32;
        if (id == dev->id) {
            if (temp_id < net->random_id) {
                /* Count ordinal collisions. */
                ++dev->local->ordinal.collision_count;
                dev->local->ordinal.count_time = mapper_get_current_time();
            }
            else if (temp_id == net->random_id && hint > 0
                     && hint != dev->local->ordinal.value) {
                dev->local->ordinal.value = hint;
                mapper_network_probe_device_name(net, dev);
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
    char *name = &argv[0]->s;
    int i, temp_id = argv[1]->i;
    double current_time;
    mapper_id id;

    trace_dev(dev, "got /name/probe %s %i \n", name, temp_id);

    id = (mapper_id) crc32(0L, (const Bytef *)name, strlen(name)) << 32;
    if (id == dev->id) {
        current_time = mapper_get_current_time();
        if (dev->local->ordinal.locked || temp_id > net->random_id) {
            for (i = 0; i < 8; i++) {
                if (dev->local->ordinal.hints[i] >= 0
                    && (current_time - dev->local->ordinal.hints[i]) > 2.0) {
                    // reserve suggested ordinal
                    dev->local->ordinal.hints[i] = current_time;
                    break;
                }
            }
            /* Name may not yet be registered, so we can't use
             * mapper_network_send() here. */
            lo_send(net->bus_addr, network_message_strings[MSG_NAME_REG],
                    "sii", name, temp_id, dev->local->ordinal.value + i + 1);
        }
        else {
            if (temp_id == net->random_id)
                dev->local->ordinal.online = 1;
            else {
                ++dev->local->ordinal.collision_count;
                dev->local->ordinal.count_time = current_time;
            }
        }
    }
    return 0;
}

/* Basic description of the protocol for establishing maps:
 *
 * The message "/map <signalA> -> <signalB>" starts the protocol.  If a device
 * doesn't already have a record for the remote device it will request this
 * information with a zero-lease "/subscribe" message.
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
 * device; if the map involves inputs from difference devices the destination
 * will provoke the creation of simple submaps from the various sources and
 * perform any combination signal-processing, otherwise processing metadata is
 * forwarded to the source device.  A convergent mapping is started with a
 * message in the form: "/map <sourceA> <sourceB> ... <sourceN> -> <destination>"
 */

static int parse_signal_names(const char *types, lo_arg **argv, int argc,
                              int *src_index, int *dest_index, int *prop_index)
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
        if (prop_index)
            *prop_index = *src_index + num_sources;
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
        if (prop_index)
            *prop_index = *dest_index+1;
    }

    /* Check that all signal names are well formed, and that no signal names
     * appear in both source and destination lists. */
    for (i = 0; i < num_sources; i++) {
        if (!strchr((&argv[*src_index+i]->s)+1, '/')) {
            trace("malformed source signal name '%s'.\n", &argv[*src_index+i]->s);
            return 0;
        }
        if (strcmp(&argv[*src_index+i]->s, &argv[*dest_index]->s)==0) {
            trace("prevented attempt to connect signal '%s' to itself.\n",
                  &argv[*dest_index]->s);
        }
    }
    if (!strchr((&argv[*dest_index]->s)+1, '/')) {
        trace("malformed destination signal name '%s'.\n", &argv[*dest_index]->s);
        return 0;
    }
    return num_sources;
}

static int handler_linked(const char *path, const char *types, lo_arg **argv,
                          int argc, lo_message msg, void *user_data)
{
    mapper_network net = (mapper_network) user_data;
    int updated = 0;

    if (argc < 3 || strncmp(types, "sss", 3))
        return 0;

    mapper_device dev1 = mapper_database_device_by_name(&net->database,
                                                        &argv[0]->s);
    if (!dev1) {
#ifdef DEBUG
        if (net->device) {
            trace_dev(net->device, "ignoring /linked; device '%s' not found.\n",
                      &argv[0]->s);
        }
        else
            trace_net("ignoring /linked; device '%s' not found.\n", &argv[0]->s);
#endif
        return 0;
    }
    mapper_device dev2 = mapper_database_device_by_name(&net->database,
                                                        &argv[2]->s);
    if (!dev2) {
#ifdef DEBUG
        if (net->device) {
            trace_dev(net->device, "ignoring /linked; device '%s' not found.\n",
                      &argv[2]->s);
        }
        else
            trace_net("ignoring /linked; device '%s' not found.\n", &argv[2]->s);
#endif
        return 0;
    }

    mapper_link link = mapper_device_link_by_remote_device(dev1, dev2);
    mapper_device ldev = dev1->local ? dev1 : dev2->local ? dev2 : 0;

    // do not allow /linked message to create local link
    if (!link && ldev) {
        trace_dev(ldev, "ignoring /linked: no staged link found.\n");
        return 0;
    }

    mapper_message props = mapper_message_parse_properties(argc-3, &types[3],
                                                           &argv[3]);
    if (link) {
        updated = mapper_database_update_link(&net->database, link, dev1, props);
    }
    else {
        link = mapper_database_add_or_update_link(&net->database, dev1, dev2,
                                                  props);
    }
    mapper_message_free(props);
    if (!link)
        return 0;

    if (!ldev) {
        trace_net("got /linked\n");
        return 0;
    }
    else
        trace_dev(ldev, "got /linked\n");

    if (updated) {
        if (ldev->local->subscribers) {
            // Inform subscribers
            trace_dev(ldev, "informing subscribers (LINKED)\n")
            mapper_network_set_dest_subscribers(net, MAPPER_OBJ_LINKS);
            mapper_link_send_state(link, MSG_LINKED, 0);
        }
        // Call local link handler if it exists
        mapper_device_link_handler *h = ldev->local->link_handler;
        if (h)
            h(ldev, link, updated ? MAPPER_MODIFIED : MAPPER_ADDED);
    }

    mapper_table_clear_empty_records(link->props);
    return 0;
}

static int handler_link_modify(const char *path, const char *types,
                               lo_arg **argv, int argc, lo_message msg,
                               void *user_data)
{
    mapper_network net = (mapper_network) user_data;
    int updated = 0;

    if (argc < 4 || strncmp(types, "ssss", 4))
        return 0;

    mapper_device dev1 = mapper_database_device_by_name(&net->database,
                                                        &argv[0]->s);
    mapper_device dev2 = mapper_database_device_by_name(&net->database,
                                                        &argv[2]->s);
    if (!dev1 || !dev2) {
        trace_net("ignoring /link/modify %s <-> %s\n", &argv[0]->s, &argv[2]->s);
        return 0;
    }

    mapper_device ldev = dev1->local ? dev1 : dev2->local ? dev2 : 0;
    if (!ldev) {
        trace_net("ignoring /link/modify %s <-> %s\n", &argv[0]->s, &argv[2]->s);
        return 0;
    }
    mapper_link link = mapper_device_link_by_remote_device(dev1, dev2);
    if (!link || !link->local) {
        trace_dev(ldev, "ignoring /link/modify %s <-> %s; link not found\n",
                  &argv[0]->s, &argv[2]->s);
        return 0;
    }

    trace_dev(ldev, "got /link/modify %s <-> %s\n", &argv[0]->s, &argv[2]->s);

    mapper_message props = mapper_message_parse_properties(argc-3, &types[3],
                                                           &argv[3]);
    updated = mapper_database_update_link(&net->database, link, dev1, props);
    if (updated) {
        if (link->local->admin_addr) {
            // inform peer device
            mapper_network_set_dest_mesh(net, link->local->admin_addr);
            mapper_link_send_state(link, MSG_LINKED, 0);
        }
        if (ldev->local->subscribers) {
            // inform subscribers
            trace_dev(ldev, "informing subscribers (LINKED)\n")
            mapper_network_set_dest_subscribers(net, MAPPER_OBJ_LINKS);
            mapper_link_send_state(link, MSG_LINKED, 0);
        }
        // Call local link handler if it exists
        mapper_device_link_handler *h = ldev->local->link_handler;
        if (h)
            h(ldev, link, MAPPER_MODIFIED);
        mapper_table_clear_empty_records(link->props);
    }
    mapper_message_free(props);
    return 0;
}

static int handler_unlinked(const char *path, const char *types, lo_arg **argv,
                            int argc, lo_message msg, void *user_data)
{
    mapper_network net = (mapper_network) user_data;
    mapper_device dev1 = mapper_database_device_by_name(&net->database,
                                                        &argv[0]->s);
    if (!dev1 || dev1->local)
        return 0;
    mapper_device dev2 = mapper_database_device_by_name(&net->database,
                                                        &argv[2]->s);
    if (!dev2 || dev2->local)
        return 0;
    trace_net("got /unlinked %s <-> %s\n", &argv[0]->s, &argv[2]->s);
    mapper_link link = mapper_device_link_by_remote_device(dev1, dev2);
    if (link)
        mapper_database_remove_link(&net->database, link, MAPPER_REMOVED);
    return 0;
}

/*! When the /map message is received by the destination device, send a /mapTo
 *  message to the source device. */
static int handler_map(const char *path, const char *types, lo_arg **argv,
                       int argc, lo_message msg, void *user_data)
{
    mapper_network net = (mapper_network) user_data;
    mapper_database db = &net->database;
    mapper_device dev = net->device;
    mapper_signal local_signal = 0;
    int i, num_sources, src_index, dest_index, prop_index;

    const char *local_signal_name = 0;

    num_sources = parse_signal_names(types, argv, argc, &src_index,
                                     &dest_index, &prop_index);
    if (!num_sources)
        return 0;

    // check if we are the destination of this mapping
    if (prefix_cmp(&argv[dest_index]->s, mapper_device_name(dev),
                   &local_signal_name)==0) {
        local_signal = mapper_device_signal_by_name(dev, local_signal_name);
        if (!local_signal) {
            trace_dev(dev, "no signal found with name '%s'.\n",
                      local_signal_name);
            return 0;
        }
    }

#ifdef DEBUG
    trace_dev(dev, "%s /map", local_signal ? "got" : "ignoring");
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

    // parse arguments from message if any
    mapper_message props = mapper_message_parse_properties(argc-prop_index,
                                                           &types[prop_index],
                                                           &argv[prop_index]);

    mapper_map map = 0;
    mapper_message_atom atom = props ? mapper_message_property(props, AT_ID) : 0;
    if (atom && atom->types[0] == 'h') {
        map = mapper_database_map_by_id(db, (atom->values[0])->i64);

        /* If a mapping already exists between these signals, forward the
         * message to handler_map_modify() and stop. */
        if (map && map->status >= STATUS_ACTIVE) {
            handler_map_modify(path, types, argv, argc, msg, user_data);
            goto done;
        }
    }
    if (!map) {
        // try to find map by signal names
        if (!is_alphabetical(num_sources, &argv[src_index])) {
            trace_dev(dev, "error in /map: signal names out of order.");
            goto done;
        }
        const char *src_names[num_sources];
        for (i = 0; i < num_sources; i++) {
            src_names[i] = &argv[src_index+i]->s;
        }
        map = mapper_router_incoming_map(dev->local->router, local_signal,
                                         num_sources, src_names);

        /* If a mapping already exists between these signals, forward the
         * message to /map/modify and stop. */
        if (map) {
            if (map->status >= STATUS_ACTIVE) {
                mapper_network_set_dest_mesh(net,
                                             map->sources[0]->link->local->admin_addr);
                lo_message_add_string(msg, mapper_property_protocol_string(AT_ID));
                lo_message_add_int64(msg, *((int64_t*)&map->id));
                mapper_network_add_message(net, 0, MSG_MAP_MODIFY, msg);
                mapper_network_send(net);
            }
            goto done;
        }

        // safety check: make sure we don't have an outgoing map to src (loop)
        if (mapper_router_loop_check(dev->local->router, local_signal,
                                     num_sources, src_names)) {
            trace_dev(dev, "error in /map: potential loop detected.")
            goto done;
        }

        // create a tentative map (flavourless)
        map = mapper_database_add_or_update_map(db, num_sources, src_names,
                                                &argv[dest_index]->s, 0);
        if (!map) {
            trace_dev(dev, "error creating local map.\n");
            goto done;
        }
    }

    if (!map->local)
        mapper_router_add_map(dev->local->router, map);

    mapper_map_set_from_message(map, props, 1);

    if (map->local->is_local_only) {
        trace_dev(dev, "map references only local signals... setting state to "
                  "ACTIVE.\n");
        map->status = STATUS_ACTIVE;
        ++dev->num_outgoing_maps;
        ++dev->num_incoming_maps;

        ++map->destination.signal->num_incoming_maps;
        for (i = 0; i < map->num_sources; i++)
            ++map->sources[i]->signal->num_outgoing_maps;

        mapper_link link = mapper_database_add_or_update_link(&net->database,
                                                              dev, dev, 0);

        // Inform subscribers
        if (dev->local->subscribers) {
            trace_dev(dev, "informing subscribers (DEVICE)\n")
            mapper_network_set_dest_subscribers(net, MAPPER_OBJ_DEVICES);
            mapper_device_send_state(dev, MSG_DEVICE);

            trace_dev(dev, "informing subscribers (SIGNAL)\n")
            mapper_network_set_dest_subscribers(net, MAPPER_OBJ_SIGNALS);
            for (i = 0; i < map->num_sources; i++)
                mapper_signal_send_state(map->sources[i]->signal, MSG_SIGNAL);
            mapper_signal_send_state(map->destination.signal, MSG_SIGNAL);

            trace_dev(dev, "informing subscribers (LINKED)\n")
            mapper_network_set_dest_subscribers(net, MAPPER_OBJ_LINKS);
            mapper_link_send_state(link, MSG_LINKED, 0);

            trace_dev(dev, "informing subscribers (MAPPED)\n")
            mapper_network_set_dest_subscribers(net, MAPPER_OBJ_MAPS);
            mapper_map_send_state(map, -1, MSG_MAPPED);
        }

        // Call local handlers if they exist
        mapper_device_link_handler *lh = dev->local->link_handler;
        if (lh)
            lh(dev, link, MAPPER_ADDED);
        mapper_device_map_handler *mh = dev->local->map_handler;
        if (mh)
            mh(dev, map, MAPPER_ADDED);
        goto done;
    }

    if (map->local->one_source && !map->sources[0]->local->router_sig
        && map->sources[0]->link && map->sources[0]->link->local->admin_addr) {
        mapper_network_set_dest_mesh(net, map->sources[0]->link->local->admin_addr);
        mapper_map_send_state(map, -1, MSG_MAP_TO);
    }
    else {
        for (i = 0; i < num_sources; i++) {
            // do not send if is local mapping
            if (map->sources[i]->local->router_sig)
                continue;

            // do not send if device host/port not yet known
            if (!map->sources[i]->link || !map->sources[i]->link->local->admin_addr)
                continue;

            mapper_network_set_dest_mesh(net,
                                         map->sources[i]->link->local->admin_addr);
            i = mapper_map_send_state(map, i, MSG_MAP_TO);
        }
    }
done:
    mapper_message_free(props);
    return 0;
}

/*! When the /mapTo message is received by a peer device, create a tentative
 *  map and respond with own signal metadata. */
static int handler_map_to(const char *path, const char *types, lo_arg **argv,
                          int argc, lo_message msg, void *user_data)
{
    mapper_network net = (mapper_network) user_data;
    mapper_database db = &net->database;
    mapper_device dev = net->device;
    mapper_signal local_signal = 0;
    mapper_map map;
    int i, num_sources, src_index, dest_index, prop_index;

    const char *local_signal_name = 0;

    num_sources = parse_signal_names(types, argv, argc, &src_index,
                                     &dest_index, &prop_index);
    if (!num_sources)
        return 0;

    // check if we are an endpoint of this map
    if (!src_index) {
        // check if we are the destination
        if (prefix_cmp(&argv[dest_index]->s, mapper_device_name(dev),
                       &local_signal_name)==0) {
            local_signal = mapper_device_signal_by_name(dev, local_signal_name);
            if (!local_signal) {
                trace_dev(dev, "no signal found with name '%s'.\n",
                          local_signal_name);
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
            local_signal = mapper_device_signal_by_name(dev, local_signal_name);
            if (!local_signal) {
                trace_dev(dev, "no signal found with name '%s'.\n",
                          local_signal_name);
                break;
            }
        }
    }

#ifdef DEBUG
    trace_dev(dev, "%s /mapTo", local_signal ? "got" : "ignoring");
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
        trace_dev(dev, "error in /mapTo: signal names out of order.\n");
        return 0;
    }
    mapper_message props = mapper_message_parse_properties(argc-prop_index,
                                                           &types[prop_index],
                                                           &argv[prop_index]);
    if (!props) {
        trace_dev(dev, "ignoring /mapTo, no properties.\n");
        return 0;
    }
    mapper_message_atom atom = mapper_message_property(props, AT_ID);
    if (!atom || atom->types[0] != 'h') {
        trace_dev(dev, "ignoring /mapTo, no 'id' property.\n");
        goto done;
    }
    mapper_id id = (atom->values[0])->i64;

    map = mapper_router_map_by_id(dev->local->router, local_signal, id,
                                  src_index ? MAPPER_DIR_OUTGOING :
                                  MAPPER_DIR_INCOMING);
    if (!map) {
        const char *src_names[num_sources];
        for (i = 0; i < num_sources; i++) {
            src_names[i] = &argv[src_index+i]->s;
        }
        // create a tentative mapping (flavourless)
        map = mapper_database_add_or_update_map(db, num_sources, src_names,
                                                &argv[dest_index]->s, 0);
        if (!map) {
            trace_dev(dev, "error creating local map in handler_map_to\n");
            goto done;
        }

        mapper_router_add_map(dev->local->router, map);
    }

    if (map->status < STATUS_ACTIVE) {
        /* Set map properties. */
        mapper_map_set_from_message(map, props, 1);
    }

    if (map->status >= STATUS_READY) {
        if (map->destination.direction == MAPPER_DIR_OUTGOING) {
            mapper_network_set_dest_mesh(net,
                                         map->destination.link->local->admin_addr);
            mapper_map_send_state(map, -1, MSG_MAPPED);
        }
        else {
            for (i = 0; i < map->num_sources; i++) {
                mapper_network_set_dest_mesh(net,
                                             map->sources[i]->link->local->admin_addr);
                i = mapper_map_send_state(map, map->local->one_source ? -1 : i,
                                          MSG_MAPPED);
            }
        }
    }
done:
    mapper_message_free(props);
    return 0;
}

/*! Respond to /mapped by storing mapping in database. */
/*! Also used by devices to confirm connection to remote peers, and to share
 *  changes in mapping properties. */
static int handler_mapped(const char *path, const char *types, lo_arg **argv,
                          int argc, lo_message msg, void *user_data)
{
    mapper_network net = (mapper_network) user_data;
    mapper_device dev = net->device;
    mapper_signal local_signal = 0;
    mapper_map map = 0;
    int i, num_sources, src_index, dest_index, prop_index;
    const char *local_signal_name;

    num_sources = parse_signal_names(types, argv, argc, &src_index,
                                     &dest_index, &prop_index);
    if (!num_sources)
        return 0;

    if (!is_alphabetical(num_sources, &argv[src_index])) {
        trace("error in /mapped: signals names out of order.");
        return 0;
    }

    // 2 scenarios: if message in form A -> B, only B interested
    //              if message in form A <- B, only B interested

    if (dev && mapper_device_name(dev)) {
        // check if we are an endpoint of this mapping
        if (!src_index) {
            // check if we are the destination
            if (prefix_cmp(&argv[dest_index]->s, mapper_device_name(dev),
                           &local_signal_name)==0) {
                local_signal = mapper_device_signal_by_name(dev,
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
                local_signal = mapper_device_signal_by_name(dev,
                                                            local_signal_name);
            }
        }
    }

#ifdef DEBUG
    if (dev) {
        trace_dev(dev, "%s /mapped", local_signal ? "got" : "ignoring");}
    else
        trace_net("got /mapped");
    if (src_index)
        printf(" %s <-", &argv[dest_index]->s);
    for (i = 0; i < num_sources; i++)
        printf(" %s", &argv[src_index+i]->s);
    if (!src_index)
        printf(" -> %s", &argv[dest_index]->s);
    printf("\n");
#endif

    mapper_message props = mapper_message_parse_properties(argc-prop_index,
                                                           &types[prop_index],
                                                           &argv[prop_index]);
    if (!local_signal) {
        int store = 0;
        if (net->database.autosubscribe & MAPPER_OBJ_MAPS)
            store = 1;
        else if (mapper_database_subscribed_by_signal_name(&net->database,
                                                           &argv[dest_index]->s))
            store = 1;
        else {
            for (i = 0; i < num_sources; i++) {
                if (mapper_database_subscribed_by_signal_name(&net->database,
                                                              &argv[src_index+i]->s)) {
                    store = 1;
                    break;
                }
            }
        }
        if (store) {
            const char *src_names[num_sources];
            for (i = 0; i < num_sources; i++) {
                src_names[i] = &argv[src_index+i]->s;
            }
            mapper_database_add_or_update_map(&net->database, num_sources,
                                              src_names, &argv[dest_index]->s,
                                              props);
        }
        goto done;
    }

    mapper_message_atom atom = props ? mapper_message_property(props, AT_ID) : 0;
    if (!atom || atom->types[0] != 'h') {
        trace_dev(dev, "ignoring /mapped, no 'id' property.\n");
        goto done;
    }
    mapper_id id = (atom->values[0])->i64;

    map = mapper_router_map_by_id(dev->local->router, local_signal, id,
                                  src_index ? MAPPER_DIR_OUTGOING :
                                  MAPPER_DIR_INCOMING);
    if (!map) {
        trace_dev(dev, "no map found for /mapped.\n");
        goto done;
    }
    if (src_index && map->num_sources != num_sources) {
        trace_dev(dev, "wrong num_sources in /mapped.\n");
        goto done;
    }

    if (map->local->is_local_only) {
        // no need to update since all properties are local
        goto done;
    }

    // TODO: if this endpoint is map admin, do not allow overwiting props
    int map_updated = mapper_map_set_from_message(map, props, 0);

    // link props may have been updated
    if (map->destination.direction == MAPPER_DIR_OUTGOING) {
        if (map->destination.link && map->destination.link->props->dirty) {
            if (dev->local->subscribers) {
                trace_dev(dev, "informing subscribers (LINKED)\n")
                mapper_network_set_dest_subscribers(net, MAPPER_OBJ_LINKS);
                mapper_link_send_state(map->destination.link, MSG_LINKED, 0);
            }
            map->destination.link->props->dirty = 0;

            // Call local link handler if it exists
            mapper_device_link_handler *h = dev->local->link_handler;
            if (h)
                h(dev, map->destination.link, MAPPER_ADDED);
        }
    }
    else {
        mapper_link link = 0;
        for (i = 0; i < map->num_sources; i++) {
            if (!map->sources[i]->link || map->sources[i]->link == link)
                continue;
            link = map->sources[i]->link;
            if (!link->props->dirty)
                continue;
            if (dev->local->subscribers) {
                trace_dev(dev, "informing subscribers (LINKED)\n")
                mapper_network_set_dest_subscribers(net, MAPPER_OBJ_LINKS);
                mapper_link_send_state(link, MSG_LINKED, 0);
            }
            link->props->dirty = 0;

            // Call local link handler if it exists
            mapper_device_link_handler *h = dev->local->link_handler;
            if (h)
                h(dev, link, MAPPER_ADDED);
        }
    }

    if (map->status < STATUS_READY) {
        goto done;
    }
    if (map->status == STATUS_READY) {
        map->status = STATUS_ACTIVE;
        ++map_updated;

        if (map->destination.direction == MAPPER_DIR_OUTGOING) {
            ++dev->num_outgoing_maps;
            for (i = 0; i < map->num_sources; i++) {
                if (map->sources[i]->signal->local)
                    ++map->sources[i]->signal->num_outgoing_maps;
            }

            // Inform remote destination
            mapper_network_set_dest_mesh(net,
                                         map->destination.link->local->admin_addr);
            mapper_map_send_state(map, -1, MSG_MAPPED);
        }
        else {
            ++dev->num_incoming_maps;
            ++map->destination.signal->num_incoming_maps;

            // Inform remote sources
            for (i = 0; i < map->num_sources; i++) {
                mapper_network_set_dest_mesh(net,
                                             map->sources[i]->link->local->admin_addr);
                i = mapper_map_send_state(map, map->local->one_source ? -1 : i,
                                          MSG_MAPPED);
            }
        }

        if (dev->local->subscribers) {
            trace_dev(dev, "informing subscribers (DEVICE)\n")
            mapper_network_set_dest_subscribers(net, MAPPER_OBJ_DEVICES);
            mapper_device_send_state(dev, MSG_DEVICE);

            trace_dev(dev, "informing subscribers (SIGNAL)\n")
            mapper_network_set_dest_subscribers(net, MAPPER_OBJ_SIGNALS);
            if (map->destination.direction == MAPPER_DIR_OUTGOING) {
                for (i = 0; i < map->num_sources; i++) {
                    if (map->sources[i]->signal->local)
                        mapper_signal_send_state(map->sources[i]->signal, MSG_SIGNAL);
                }
            }
            else {
                mapper_signal_send_state(map->destination.signal, MSG_SIGNAL);
            }
        }
    }
    if (map_updated) {
        if (dev->local->subscribers) {
            trace_dev(dev, "informing subscribers (MAPPED)\n")
            if (map->destination.direction == MAPPER_DIR_OUTGOING)
                mapper_network_set_dest_subscribers(net, MAPPER_OBJ_OUTGOING_MAPS);
            else
                mapper_network_set_dest_subscribers(net, MAPPER_OBJ_INCOMING_MAPS);
            mapper_map_send_state(map, -1, MSG_MAPPED);
        }

        // Call local map handler if it exists
        mapper_device_map_handler *h = dev->local->map_handler;
        if (h)
            h(dev, map, MAPPER_ADDED);
    }
done:
    mapper_message_free(props);
    if (map)
        mapper_table_clear_empty_records(map->props);
    return 0;
}

/*! Modify the map properties : mode, range, expression, etc. */
static int handler_map_modify(const char *path, const char *types, lo_arg **argv,
                              int argc, lo_message msg, void *user_data)
{
    mapper_network net = (mapper_network) user_data;
    mapper_device dev = net->device;
    mapper_map map = 0;
    int i;

    if (argc < 4)
        return 0;

    // check the map's id
    for (i = 3; i < argc; i++) {
        if (types[i] != 's' && types[i] != 'S')
            continue;
        if (strcmp(&argv[i]->s, "@id")==0)
            break;
    }
    if (i < argc && types[++i] == 'h')
        map = mapper_database_map_by_id(dev->database, argv[i]->i64);
    else {
        // try to find map by signal names
        mapper_signal local_signal = 0;
        int num_sources, src_index, dest_index, prop_index;
        const char *local_signal_name;
        num_sources = parse_signal_names(types, argv, argc, &src_index,
                                         &dest_index, &prop_index);
        if (!num_sources)
            return 0;
        if (!is_alphabetical(num_sources, &argv[src_index])) {
            trace_dev(dev, "error in /map/modify: signal names out of order.");
            return 0;
        }
        const char *src_names[num_sources];
        for (i = 0; i < num_sources; i++) {
            src_names[i] = &argv[src_index+i]->s;
        }
        // check if we are the destination
        if (prefix_cmp(&argv[dest_index]->s, mapper_device_name(dev),
                       &local_signal_name)==0) {
            local_signal = mapper_device_signal_by_name(dev, local_signal_name);
            if (!local_signal) {
                trace_dev(dev, "no signal found with name "
                          "'%s'.\n", local_signal_name);
            }
            map = mapper_router_incoming_map(dev->local->router, local_signal,
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
                local_signal = mapper_device_signal_by_name(dev, local_signal_name);
                if (!local_signal) {
                    trace_dev(dev, "no signal found with name '%s'.\n",
                              local_signal_name);
                    break;
                }
            }
            if (local_signal)
                map = mapper_router_outgoing_map(dev->local->router, local_signal,
                                                 num_sources, src_names,
                                                 &argv[dest_index]->s);
        }
    }

#ifdef DEBUG
    trace_dev(dev, "%s /map/modify\n", map && map->local ? "got" : "ignoring");
#endif
    if (!map || !map->local || map->status < STATUS_ACTIVE) {
        return 0;
    }

    mapper_message props = mapper_message_parse_properties(argc, types, argv);
    if (!props) {
        trace_dev(dev, "ignoring /map/modify, no properties.\n");
        return 0;
    }

    mapper_message_atom atom = mapper_message_property(props, AT_PROCESS_LOCATION);
    if (atom) {
        map->process_location = mapper_location_from_string(&(atom->values[0])->s);
        if (!map->local->one_source) {
            /* if map has sources from different remote devices, processing must
             * occur at the destination. */
            map->process_location = MAPPER_LOC_DESTINATION;
        }
    }
    if ((atom = mapper_message_property(props, AT_EXPRESSION))) {
        if (strstr(&atom->values[0]->s, "y{-")) {
            map->process_location = MAPPER_LOC_DESTINATION;
        }
    }
    else if (map->expression && strstr(map->expression, "y{-")) {
        map->process_location = MAPPER_LOC_DESTINATION;
    }

    // do not continue if we are not in charge of processing
    if (map->process_location == MAPPER_LOC_DESTINATION) {
        if (!map->destination.signal->local) {
            trace_dev(dev, "ignoring /map/modify, slaved to remote device.\n");
            goto done;
        }
    }
    else if (!map->sources[0]->signal->local) {
        trace_dev(dev, "ignoring /map/modify, slaved to remote device.\n");
        goto done;
    }

    int updated = mapper_map_set_from_message(map, props, 1);

    if (updated) {
        if (!map->local->is_local_only) {
            // TODO: may not need to inform all remote peers
            // Inform remote peer(s) of relevant changes
            if (!map->destination.local->router_sig) {
                mapper_network_set_dest_mesh(net,
                                             map->destination.link->local->admin_addr);
                mapper_map_send_state(map, -1, MSG_MAPPED);
            }
            else {
                for (i = 0; i < map->num_sources; i++) {
                    if (map->sources[i]->local->router_sig)
                        continue;
                    mapper_network_set_dest_mesh(net,
                                                 map->sources[i]->link->local->admin_addr);
                    i = mapper_map_send_state(map, i, MSG_MAPPED);
                }
            }
        }

        if (dev->local->subscribers) {
            trace_dev(dev, "informing subscribers (MAPPED)\n")
            // Inform subscribers
            if (map->destination.local->router_sig)
                mapper_network_set_dest_subscribers(net,
                                                    MAPPER_OBJ_INCOMING_MAPS);
            else
                mapper_network_set_dest_subscribers(net,
                                                    MAPPER_OBJ_OUTGOING_MAPS);
            mapper_map_send_state(map, -1, MSG_MAPPED);
        }

        // Call local map handler if it exists
        mapper_device_map_handler *h = dev->local->map_handler;
        if (h)
            h(dev, map, MAPPER_MODIFIED);
    }
    trace_dev(dev, "updated %d map properties.\n", updated);

done:
    mapper_message_free(props);
    mapper_table_clear_empty_records(map->props);
    return 0;
}

/*! Unmap a set of signals. */
static int handler_unmap(const char *path, const char *types, lo_arg **argv,
                         int argc, lo_message msg, void *user_data)
{
    mapper_network net = (mapper_network) user_data;
    mapper_device dev = net->device;
    mapper_signal local_signal=0;
    mapper_map map = 0;
    int i, num_sources, src_index, dest_index;
    const char *local_signal_name;

    num_sources = parse_signal_names(types, argv, argc, &src_index,
                                     &dest_index, 0);
    if (!num_sources)
        return 0;

    if (!is_alphabetical(num_sources, &argv[src_index])) {
        trace_dev(dev, "error in /unmap: signal names out of order.");
        return 0;
    }
    const char *src_names[num_sources];
    for (i = 0; i < num_sources; i++) {
        src_names[i] = &argv[src_index+i]->s;
    }

    // check if we are the destination
    if (prefix_cmp(&argv[dest_index]->s, mapper_device_name(dev),
                   &local_signal_name)==0) {
        local_signal = mapper_device_signal_by_name(dev, local_signal_name);
        if (!local_signal) {
            trace_dev(dev, "no signal found with name '%s'.\n", local_signal_name);
            return 0;
        }
        map = mapper_router_incoming_map(dev->local->router, local_signal,
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
            local_signal = mapper_device_signal_by_name(dev, local_signal_name);
            if (!local_signal) {
                trace_dev(dev, "no signal found with name '%s'.\n",
                          local_signal_name);
                break;
            }
        }
        if (local_signal)
            map = mapper_router_outgoing_map(dev->local->router, local_signal,
                                             num_sources, src_names,
                                             &argv[dest_index]->s);
    }

#ifdef DEBUG
    trace_dev(dev, "%s /unmap", map ? "got" : "ignoring");
    if (src_index)
        printf(" %s <-", &argv[dest_index]->s);
    for (i = 0; i < num_sources; i++)
        printf(" %s", &argv[src_index+i]->s);
    if (!src_index)
        printf(" -> %s", &argv[dest_index]->s);
    printf("\n");
#endif
    if (!map) {
        if (local_signal) {
            // return "unmapped" to bus
            mapper_network_set_dest_bus(net);
            mapper_network_add_message(net, 0, MSG_UNMAPPED, msg);
            mapper_network_send(net);
        }
        return 0;
    }

    // inform remote peer(s)
    if (!map->destination.local->router_sig) {
        --dev->num_outgoing_maps;
        for (i = 0; i < map->num_sources; i++) {
            if (map->sources[i]->signal->local) {
                if ((--map->sources[i]->signal->num_outgoing_maps) < 0)
                    map->sources[i]->signal->num_outgoing_maps = 0;
            }
        }

        mapper_network_set_dest_mesh(net,
                                     map->destination.link->local->admin_addr);
        mapper_map_send_state(map, -1, MSG_UNMAP);
    }
    else {
        --dev->num_incoming_maps;
        if ((--map->destination.signal->num_incoming_maps) < 0)
            map->destination.signal->num_incoming_maps = 0;

        for (i = 0; i < map->num_sources; i++) {
            if (map->sources[i]->local->router_sig)
                continue;
            mapper_network_set_dest_mesh(net,
                                         map->sources[i]->link->local->admin_addr);
            i = mapper_map_send_state(map, i, MSG_UNMAP);
        }
    }

    if (dev->local->subscribers) {
        trace_dev(dev, "informing subscribers (DEVICE)\n")
        mapper_network_set_dest_subscribers(net, MAPPER_OBJ_DEVICES);
        mapper_device_send_state(dev, MSG_DEVICE);

        trace_dev(dev, "informing subscribers (SIGNAL)\n")
        mapper_network_set_dest_subscribers(net, MAPPER_OBJ_SIGNALS);
        if (map->destination.direction == MAPPER_DIR_OUTGOING) {
            for (i = 0; i < map->num_sources; i++) {
                if (map->sources[i]->signal->local)
                    mapper_signal_send_state(map->sources[i]->signal, MSG_SIGNAL);
            }
        }
        else {
            mapper_signal_send_state(map->destination.signal, MSG_SIGNAL);
        }

        trace_dev(dev, "informing subscribers (UNMAPPED)\n")
        // Inform subscribers
        if (map->destination.local->router_sig)
            mapper_network_set_dest_subscribers(net, MAPPER_OBJ_INCOMING_MAPS);
        else
            mapper_network_set_dest_subscribers(net, MAPPER_OBJ_OUTGOING_MAPS);
        mapper_map_send_state(map, -1, MSG_UNMAPPED);
    }

    // Call local map handler if it exists
    mapper_device_map_handler *h = dev->local->map_handler;
    if (h)
        h(dev, map, MAPPER_REMOVED);

    /* The mapping is removed. */
    mapper_router_remove_map(dev->local->router, map);
    mapper_database_remove_map(dev->database, map, MAPPER_REMOVED);
    // TODO: remove empty router_signals
    return 0;
}

/*! Respond to /unmapped by removing map from database. */
static int handler_unmapped(const char *path, const char *types, lo_arg **argv,
                            int argc, lo_message msg, void *user_data)
{
    mapper_network net = (mapper_network) user_data;
    int i, id_index;
    mapper_id *id = 0;

    for (i = 0; i < argc; i++) {
        if (types[i] != 's' && types[i] != 'S')
            return 0;
        if (strcmp(&argv[i]->s, "@id")==0 && (types[i+1] == 'h')) {
            id_index = i+1;
            id = (mapper_id*) &argv[id_index]->i64;
            break;
        }
    }
    if (!id) {
        trace_net("error: no 'id' property found in /unmapped message.")
        return 0;
    }

#ifdef DEBUG
    trace_net("got /unmapped");
    for (i = 0; i < id_index; i++)
        printf(" %s", &argv[i]->s);
    printf(" %llu\n", *id);
#endif

    mapper_map map = mapper_database_map_by_id(&net->database, *id);
    if (map)
        mapper_database_remove_map(&net->database, map, MAPPER_REMOVED);

    return 0;
}

static int handler_ping(const char *path, const char *types, lo_arg **argv,
                        int argc,lo_message msg, void *user_data)
{
    mapper_network net = (mapper_network) user_data;
    mapper_device dev = net->device, remote;
    mapper_link link;

    if (!dev)
        return 0;

    mapper_timetag_t now;
    mapper_timetag_now(&now);
    lo_timetag then = lo_message_get_timestamp(msg);

    remote = mapper_database_device_by_id(dev->database, argv[0]->h);
    link = remote ? mapper_device_link_by_remote_device(dev, remote) : 0;
    if (link) {
        mapper_sync_clock clock = &link->local->clock;
        trace_dev(dev, "ping received from linked device '%s'\n",
                  link->remote_device->name);
        if (argv[2]->i == clock->sent.message_id) {
            // total elapsed time since ping sent
            double elapsed = mapper_timetag_difference(now, clock->sent.timetag);
            // assume symmetrical latency
            double latency = (elapsed - argv[3]->d) * 0.5;
            // difference between remote and local clocks (latency compensated)
            double offset = mapper_timetag_difference(now, then) - latency;

            if (latency < 0) {
                trace_dev(dev, "error: latency %f cannot be < 0.\n", latency);
                latency = 0;
            }

            if (clock->new == 1) {
                clock->offset = offset;
                clock->latency = latency;
                clock->jitter = 0;
                clock->new = 0;
            }
            else {
                clock->jitter = (clock->jitter * 0.9
                                 + fabs(clock->latency - latency) * 0.1);
                if (offset > clock->offset) {
                    // remote timetag is in the future
                    clock->offset = offset;
                }
                else if (latency < clock->latency + clock->jitter
                         && latency > clock->latency - clock->jitter) {
                    clock->offset = clock->offset * 0.9 + offset * 0.1;
                    clock->latency = clock->latency * 0.9 + latency * 0.1;
                }
            }
        }

        // update sync status
        mapper_timetag_copy(&clock->response.timetag, now);
        clock->response.message_id = argv[1]->i;
    }
    return 0;
}

static int handler_sync(const char *path, const char *types, lo_arg **argv,
                        int argc, lo_message msg, void *user_data)
{
    mapper_network net = (mapper_network) user_data;

    if (!net || !argc)
        return 0;

    mapper_device dev = 0;
    if (types[0] == 's' || types[0] == 'S') {
        dev = mapper_database_device_by_name(&net->database, &argv[0]->s);
        if (dev) {
            if (dev->local)
                return 0;
            trace_db("updating sync record for device '%s'\n", dev->name);
            mapper_timetag_now(&dev->synced);

            if (!dev->subscribed && net->database.autosubscribe) {
                trace_db("autosubscribing to device '%s'.\n", &argv[0]->s);
                mapper_database_subscribe(&net->database, dev,
                                          net->database.autosubscribe, -1);
            }
        }
        else if (net->database.autosubscribe) {
            // only create device record after requesting more information
            trace_db("requesting metadata for device '%s'.\n", &argv[0]->s);
            mapper_device_t temp;
            temp.name = &argv[0]->s;
            temp.version = -1;
            temp.local = 0;
            mapper_database_subscribe(&net->database, &temp,
                                      MAPPER_OBJ_DEVICES, 0);
        }
        else {
            trace_db("ignoring sync from '%s' (autosubscribe = %d)\n",
                     &argv[0]->s, net->database.autosubscribe);
        }
    }
    else if (types[0] == 'i') {
        if ((dev = mapper_database_device_by_id(&net->database, argv[0]->i)))
            mapper_timetag_now(&dev->synced);
    }

    return 0;
}

/* Send an arbitrary message to the multicast bus */
void mapper_network_send_message(mapper_network net, const char *path,
                                 const char *types, ...)
{
    if (!net || !path || !types)
        return;
    lo_message msg = lo_message_new();
    if (!msg)
        return;

    va_list aq;
    va_start(aq, types);
    char t[] = " ";

    while (types && *types) {
        t[0] = types[0];
        switch (t[0]) {
            case 'i':
                lo_message_add(msg, t, va_arg(aq, int));
                break;
            case 's':
            case 'S':
                lo_message_add(msg, t, va_arg(aq, char*));
                break;
            case 'f':
            case 'd':
                lo_message_add(msg, t, va_arg(aq, double));
                break;
            case 'c':
                lo_message_add(msg, t, (char)va_arg(aq, int));
                break;
            case 't':
                lo_message_add(msg, t, va_arg(aq, mapper_timetag_t));
                break;
            default:
                die_unless(0, "message %s, unknown type '%c'\n",
                           path, t[0]);
        }
        types++;
    }
    mapper_network_set_dest_bus(net);
    mapper_network_add_message(net, path, 0, msg);
    /* We cannot depend on path string sticking around for liblo to serialize
     * later: trigger immediate dispatch. */
    mapper_network_send(net);
}
