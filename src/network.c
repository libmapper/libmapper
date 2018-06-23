
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

extern const char* prop_msg_strings[MAPPER_PROP_EXTRA+1];

// set to 1 to force mesh comms to use multicast bus instead for debugging
#define FORCE_COMMS_TO_BUS      0

#define BUNDLE_DST_SUBSCRIBERS (void*)-1
#define BUNDLE_DST_BUS         0

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

const char* network_msg_strings[] =
{
    "/device",                  /* MSG_DEVICE */
    "/%s/modify",               /* MSG_DEVICE_MODIFY */
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
    "/unmap",                   /* MSG_UNMAP */
    "/unmapped",                /* MSG_UNMAPPED */
    "/who",                     /* MSG_WHO */
};

#define HANDLER_ARGS const char*, const char*, lo_arg**, int, lo_message, void*
/* Internal message handler prototypes. */
static int handler_dev(HANDLER_ARGS);
static int handler_dev_mod(HANDLER_ARGS);
static int handler_logout(HANDLER_ARGS);
static int handler_map(HANDLER_ARGS);
static int handler_map_to(HANDLER_ARGS);
static int handler_mapped(HANDLER_ARGS);
static int handler_map_mod(HANDLER_ARGS);
static int handler_probe(HANDLER_ARGS);
static int handler_registered(HANDLER_ARGS);
static int handler_ping(HANDLER_ARGS);
static int handler_sig(HANDLER_ARGS);
static int handler_sig_removed(HANDLER_ARGS);
static int handler_sig_mod(HANDLER_ARGS);
static int handler_subscribe(HANDLER_ARGS);
static int handler_sync(HANDLER_ARGS);
static int handler_unmap(HANDLER_ARGS);
static int handler_unmapped(HANDLER_ARGS);
static int handler_who(HANDLER_ARGS);

/* Handler <-> Message relationships */
struct handler_method_assoc {
    int str_idx;
    char *types;
    lo_method_handler h;
};

// handlers needed by devices
static struct handler_method_assoc device_handlers[] = {
    {MSG_DEVICE,                NULL,       handler_dev},
    {MSG_DEVICE_MODIFY,         NULL,       handler_dev_mod},
    {MSG_LOGOUT,                NULL,       handler_logout},
    {MSG_MAP,                   NULL,       handler_map},
    {MSG_MAP_TO,                NULL,       handler_map_to},
    {MSG_MAPPED,                NULL,       handler_mapped},
    {MSG_MAP_MODIFY,            NULL,       handler_map_mod},
    {MSG_PING,                  "hiid",     handler_ping},
    {MSG_SIGNAL_MODIFY,         NULL,       handler_sig_mod},
    {MSG_SUBSCRIBE,             NULL,       handler_subscribe},
    {MSG_UNMAP,                 NULL,       handler_unmap},
    {MSG_WHO,                   NULL,       handler_who},
};
const int NUM_DEVICE_HANDLERS =
    sizeof(device_handlers)/sizeof(device_handlers[0]);

// handlers needed by graph for archiving
static struct handler_method_assoc graph_handlers[] = {
    {MSG_DEVICE,                NULL,       handler_dev},
    {MSG_LOGOUT,                NULL,       handler_logout},
    {MSG_MAPPED,                NULL,       handler_mapped},
    {MSG_SIGNAL,                NULL,       handler_sig},
    {MSG_SIGNAL_REMOVED,        "s",        handler_sig_removed},
    {MSG_SYNC,                  NULL,       handler_sync},
    {MSG_UNMAPPED,              NULL,       handler_unmapped},
};
const int NUM_GRAPH_HANDLERS =
    sizeof(graph_handlers)/sizeof(graph_handlers[0]);

/* Internal LibLo error handler */
static void handler_error(int num, const char *msg, const char *where)
{
    trace_net("[libmapper] liblo server error %d in path %s: %s\n", num, where, msg);
}

/* Functions for handling the resource allocation scheme.  If check_collisions()
 * returns 1, the resource in question should be probed on the libmapper bus. */
static int check_collisions(mapper_network net, mapper_allocated resource);

/*! Local function to get the IP address of a network interface. */
static int get_iface_addr(const char* pref, struct in_addr* addr, char **iface)
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
    char path[256];
    for (i = 0; i < NUM_DEVICE_HANDLERS; i++) {
        snprintf(path, 256, network_msg_strings[device_handlers[i].str_idx],
                 mapper_device_get_name(net->dev));
        lo_server_add_method(net->server.bus, path, device_handlers[i].types,
                             device_handlers[i].h, net);
#if !FORCE_COMMS_TO_BUS
        lo_server_add_method(net->server.mesh, path, device_handlers[i].types,
                             device_handlers[i].h, net);
#endif
    }
}

void mapper_network_remove_device_methods(mapper_network net, mapper_device dev)
{
    int i, j;
    char path[256];
    for (i=0; i < NUM_DEVICE_HANDLERS; i++) {
        // make sure method isn't also used by graph
        int found = 0;
        for (j=0; j < NUM_GRAPH_HANDLERS; j++) {
            if (device_handlers[i].str_idx == graph_handlers[j].str_idx) {
                found = 1;
                break;
            }
        }
        if (found)
            continue;
        snprintf(path, 256, network_msg_strings[device_handlers[i].str_idx],
                 mapper_device_get_name(net->dev));
        lo_server_del_method(net->server.bus, path, device_handlers[i].types);
#if !FORCE_COMMS_TO_BUS
        lo_server_del_method(net->server.mesh, path, device_handlers[i].types);
#endif
    }
}

void mapper_network_add_graph_methods(mapper_network net)
{
    // add graph methods
    int i;
    for (i=0; i < NUM_GRAPH_HANDLERS; i++) {
        lo_server_add_method(net->server.bus,
                             network_msg_strings[graph_handlers[i].str_idx],
                             graph_handlers[i].types, graph_handlers[i].h, net);
#if !FORCE_COMMS_TO_BUS
        lo_server_add_method(net->server.mesh,
                             network_msg_strings[graph_handlers[i].str_idx],
                             graph_handlers[i].types, graph_handlers[i].h, net);
#endif
    }
    return;
}

void mapper_network_init(mapper_network n, const char *iface, const char *group,
                         int port)
{
    /* Default standard ip and port is group 224.0.1.3, port 7570 */
    char port_str[10], *s_port = port_str;

    if (!n->multicast.group)
        n->multicast.group = strdup(group ?: "224.0.1.3");
    if (!n->multicast.port)
        n->multicast.port = port ?: 7570;
    snprintf(port_str, 10, "%d", n->multicast.port);

    /* Initialize interface information. */
    if (get_iface_addr(iface, &n->iface.addr, &n->iface.name)) {
        trace_net("no interface found\n");
    }
    else {
        trace_net("using interface '%s'\n", n->iface.name);
    }

    /* Remove existing structures if necessary */
    if (n->addr.bus)
        lo_address_free(n->addr.bus);
    if (n->server.bus)
        lo_server_free(n->server.bus);
    if (n->server.mesh)
        lo_server_free(n->server.mesh);

    /* Open address */
    n->addr.bus = lo_address_new(n->multicast.group, s_port);
    if (!n->addr.bus) {
        return;
    }

    /* Set TTL for packet to 1 -> local subnet */
    lo_address_set_ttl(n->addr.bus, 1);

    /* Specify the interface to use for multicasting */
    lo_address_set_iface(n->addr.bus, n->iface.name, 0);

    /* Open server for multicast */
    n->server.bus = lo_server_new_multicast_iface(n->multicast.group, s_port,
                                                  n->iface.name, 0,
                                                  handler_error);

    if (!n->server.bus) {
        lo_address_free(n->addr.bus);
        return;
    }

    // Also open address/server for mesh-style communications
    // TODO: use TCP instead?
    while (!(n->server.mesh = lo_server_new(0, liblo_error_handler))) {}

    // Disable liblo message queueing.
    lo_server_enable_queue(n->server.bus, 0, 1);
    lo_server_enable_queue(n->server.mesh, 0, 1);

    mapper_network_add_graph_methods(n);
}

const char *mapper_version()
{
    return PACKAGE_VERSION;
}

void mapper_network_send(mapper_network net)
{
    if (!net->bundle)
        return;

#if FORCE_COMMS_TO_BUS
    lo_send_bundle_from(net->addr.bus, net->server.mesh, net->bundle);
#else
    if (net->addr.dst == BUNDLE_DST_SUBSCRIBERS) {
        mapper_subscriber *s = &net->dev->local->subscribers;
        mapper_time_t t;
        if (*s) {
            mapper_time_now(&t);
        }
        while (*s) {
            if ((*s)->lease_exp < t.sec || !(*s)->flags) {
                // subscription expired, remove from subscriber list
                trace_dev(net->dev, "removing expired subscription from "
                          "%s:%s\n", lo_address_get_hostname((*s)->addr),
                          lo_address_get_port((*s)->addr));
                mapper_subscriber temp = *s;
                *s = temp->next;
                if (temp->addr)
                    lo_address_free(temp->addr);
                free(temp);
                continue;
            }
            if ((*s)->flags & net->msg_type) {
                lo_send_bundle_from((*s)->addr, net->server.mesh, net->bundle);
            }
            s = &(*s)->next;
        }
    }
    else if (net->addr.dst == BUNDLE_DST_BUS) {
        lo_send_bundle_from(net->addr.bus, net->server.mesh, net->bundle);
    }
    else {
        lo_send_bundle_from(net->addr.dst, net->server.mesh, net->bundle);
    }
#endif
    lo_bundle_free_recursive(net->bundle);
    net->bundle = 0;
}

int mapper_network_init_bundle(mapper_network net)
{
    if (net->bundle)
        mapper_network_send(net);

    mapper_time_t t;
    mapper_time_now(&t);
    net->bundle = lo_bundle_new(t);
    if (!net->bundle) {
        trace_net("couldn't allocate lo_bundle\n");
        return 1;
    }

    return 0;
}

void mapper_network_bus(mapper_network net)
{
    if (net->bundle && (   net->addr.dst != BUNDLE_DST_BUS
                        || lo_bundle_count(net->bundle) >= MAX_BUNDLE_COUNT))
        mapper_network_send(net);
    net->addr.dst = BUNDLE_DST_BUS;
    if (!net->bundle)
        mapper_network_init_bundle(net);
}

void mapper_network_mesh(mapper_network net, lo_address addr)
{
    if (net->bundle && (   net->addr.dst != addr
                        || lo_bundle_count(net->bundle) >= MAX_BUNDLE_COUNT))
        mapper_network_send(net);
    net->addr.dst = addr;
    if (!net->bundle)
        mapper_network_init_bundle(net);
}

void mapper_network_subscribers(mapper_network net, int type)
{
    if (net->bundle && (   net->addr.dst != BUNDLE_DST_SUBSCRIBERS
                        || net->msg_type != type
                        || lo_bundle_count(net->bundle) >= MAX_BUNDLE_COUNT))
        mapper_network_send(net);
    net->addr.dst = BUNDLE_DST_SUBSCRIBERS;
    net->msg_type = type;
    if (!net->bundle)
        mapper_network_init_bundle(net);
}

void mapper_network_add_msg(mapper_network net, const char *str,
                            network_msg_t cmd, lo_message msg)
{
    lo_bundle_add_message(net->bundle, str ?: network_msg_strings[cmd], msg);
}

void mapper_network_free_msgs(mapper_network net)
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
    // send out any cached messages
    mapper_network_send(net);

    if (net->iface.name)
        free(net->iface.name);

    if (net->multicast.group)
        free(net->multicast.group);

    if (net->server.bus)
        lo_server_free(net->server.bus);

    if (net->server.mesh)
        lo_server_free(net->server.mesh);

    if (net->addr.bus)
        lo_address_free(net->addr.bus);
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

    /* Note: mapper_device_get_name() would refuse here since the ordinal is not
     * yet locked, so we have to build it manually at this point. */
    char name[256];
    snprintf(name, 256, "%s.%d", dev->identifier, dev->local->ordinal.val);
    trace_dev(dev, "probing name '%s'\n", name);

    /* Calculate an id from the name and store it in id.val */
    dev->obj.id = (mapper_id) crc32(0L, (const Bytef *)name, strlen(name)) << 32;

    /* For the same reason, we can't use mapper_network_send() here. */
    lo_send(net->addr.bus, network_msg_strings[MSG_NAME_PROBE], "si",
            name, net->random_id);
}

/*! Add an uninitialized device to this network. */
void mapper_network_add_device(mapper_network net, mapper_device dev)
{
    /* Initialize data structures */
    if (dev) {
        net->dev = dev;

        /* Seed the random number generator. */
        seed_srand();

        /* Choose a random ID for allocation speedup */
        net->random_id = rand();

        /* Add methods for libmapper bus.  Only add methods needed for
         * allocation here. Further methods are added when the device is
         * registered. */
        lo_server_add_method(net->server.bus, network_msg_strings[MSG_NAME_PROBE],
                             "si", handler_probe, net);
        lo_server_add_method(net->server.bus, network_msg_strings[MSG_NAME_REG],
                             NULL, handler_registered, net);

        /* Probe potential name to libmapper bus. */
        mapper_network_probe_device_name(net, dev);
    }
}

// TODO: rename to mapper_device...?
static void mapper_network_maybe_send_ping(mapper_network net, int force)
{
    mapper_device dev = net->dev;
    int go = 0;

    mapper_time_t now;
    mapper_time_now(&now);
    if (force || (now.sec >= net->next_ping)) {
        go = 1;
        net->next_ping = now.sec + 5 + (rand() % 4);
    }

    if (!dev || !go)
        return;

    mapper_network_bus(net);
    lo_message msg = lo_message_new();
    if (!msg) {
        trace_net("couldn't allocate lo_message\n");
        return;
    }
    lo_message_add_string(msg, mapper_device_get_name(dev));
    lo_message_add_int32(msg, dev->obj.version);
    mapper_network_add_msg(net, 0, MSG_SYNC, msg);

    int elapsed, num_maps;
    // some housekeeping: periodically check if our links are still active
    mapper_object *links = mapper_list_from_data(net->graph->links);
    while (links) {
        mapper_link link = (mapper_link)*links;
        if (link->remote_dev == dev) {
            links = mapper_object_list_next(links);
            continue;
        }
        num_maps = link->num_maps[0] + link->num_maps[1];
        mapper_sync_clock sync = &link->clock;
        elapsed = (sync->response.time.sec
                   ? now.sec - sync->response.time.sec : 0);
        if ((dev->local->link_timeout_sec
             && elapsed > dev->local->link_timeout_sec)) {
            if (sync->response.msg_id > 0) {
                if (num_maps) {
                    trace_dev(dev, "Lost contact with linked device '%s' (%d "
                              "seconds since sync).\n",
                              link->remote_dev->name, elapsed);
                }
                // tentatively mark link as expired
                sync->response.msg_id = -1;
                sync->response.time.sec = now.sec;
            }
            else {
                if (num_maps) {
                    trace_dev(dev, "Removing link to unresponsive device '%s' "
                              "(%d seconds since warning).\n",
                              link->remote_dev->name, elapsed);
                    /* TODO: release related maps, call local handlers
                     * and inform subscribers. */
                }
                else {
                    trace_dev(dev, "Removing link to device '%s'.\n",
                              link->remote_dev->name);
                }
                // remove related data structures
                mapper_router_remove_link(dev->local->rtr, link);
                mapper_graph_remove_link(net->graph, link,
                                         num_maps ? MAPPER_EXPIRED : MAPPER_REMOVED);
            }
        }
        else if (num_maps
                 && mapper_object_get_prop_by_index(&link->remote_dev->obj,
                                                    MAPPER_PROP_HOST, NULL,
                                                    NULL, NULL, NULL)) {
            /* Only send pings if this link has associated maps, ensuring empty
             * links are removed after the ping timeout. */
            lo_bundle b = lo_bundle_new(now);
            lo_message m = lo_message_new();
            lo_message_add_int64(m, dev->obj.id);
            ++sync->sent.msg_id;
            if (sync->sent.msg_id < 0)
                sync->sent.msg_id = 0;
            lo_message_add_int32(m, sync->sent.msg_id);
            lo_message_add_int32(m, sync->response.msg_id);
            if (sync->response.time.sec)
                lo_message_add_double(m, mapper_time_difference(now, sync->response.time));
            else
                lo_message_add_double(m, 0.);
            // need to send immediately
            lo_bundle_add_message(b, network_msg_strings[MSG_PING], m);
#if FORCE_COMMS_TO_BUS
            lo_send_bundle_from(net->addr.bus, net->server.mesh, b);
#else
            lo_send_bundle_from(link->addr.admin, net->server.mesh, b);
#endif
            mapper_time_copy(&sync->sent.time, lo_bundle_get_timestamp(b));
            lo_bundle_free_recursive(b);
        }
        links = mapper_object_list_next(links);
    }
}

/*! This is the main function to be called once in a while from a program so
 *  that the libmapper bus can be automatically managed. */
void mapper_network_poll(mapper_network net)
{
    int status;
    mapper_device dev = net->dev;

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
            mapper_device_on_registered(dev);

            /* Send registered msg. */
            lo_send(net->addr.bus, network_msg_strings[MSG_NAME_REG], "s",
                    mapper_device_get_name(dev));

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
        resource->val += i + 1;

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
static int handler_who(const char *path, const char *types, lo_arg **av, int ac,
                       lo_message msg, void *user)
{
    mapper_network net = (mapper_network)user;
    mapper_network_maybe_send_ping(net, 1);

    trace_dev(net->dev, "received /who\n");

    return 0;
}

/*! Register information about port and host for the device. */
static int handler_dev(const char *path, const char *types, lo_arg **av, int ac,
                       lo_message msg, void *user)
{
    mapper_network net = (mapper_network)user;
    mapper_device dev = net->dev;
    int i, j;
    mapper_msg props = 0;

    if (ac < 1)
        return 0;

    if (types[0] != MAPPER_STRING)
        return 0;

    const char *name = &av[0]->s;

    if (net->graph->autosubscribe
        || mapper_graph_subscribed_by_dev_name(net->graph, name)) {
        props = mapper_msg_parse_props(ac-1, &types[1], &av[1]);
        trace_net("got /device %s + %i arguments\n", name, ac-1);
        mapper_device remote;
        remote = mapper_graph_add_or_update_device(net->graph, name, props);
        if (!remote->subscribed && net->graph->autosubscribe) {
            mapper_graph_subscribe(net->graph, remote,
                                   net->graph->autosubscribe, -1);
        }
    }
    if (dev) {
        if (strcmp(&av[0]->s, mapper_device_get_name(dev))) {
            trace_dev(dev, "got /device %s\n", &av[0]->s);
        }
        else {
            // ignore own messages
            trace_dev(dev, "ignoring /device %s\n", &av[0]->s)
            return 0;
        }
    }
    else
        goto done;

    // Discover whether the device is linked.
    mapper_device remote = mapper_graph_get_device_by_name(net->graph, name);
    mapper_link link = dev ? mapper_device_get_link_by_remote_device(dev, remote) : 0;
    if (!link) {
        trace_dev(dev, "ignoring /device '%s', no link.\n", name);
        goto done;
    }
    else if (link->addr.admin) {
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
        props = mapper_msg_parse_props(ac-1, &types[1], &av[1]);
    mapper_msg_atom atom = mapper_msg_prop(props, MAPPER_PROP_PORT);
    if (!atom || atom->len != 1 || atom->types[0] != MAPPER_INT32) {
        trace_dev(dev, "can't perform /linkTo, port unknown\n");
        goto done;
    }
    int data_port = (atom->vals[0])->i;

    mapper_link_connect(link, host, atoi(admin_port), data_port);
    trace_dev(dev, "activated router to device '%s' at %s:%d\n", name, host,
              data_port);

    // check if we have maps waiting for this link
    mapper_router_sig rs = dev->local->rtr->sigs;
    while (rs) {
        for (i = 0; i < rs->num_slots; i++) {
            if (!rs->slots[i])
                continue;
            mapper_map map = rs->slots[i]->map;
            if (rs->slots[i]->dir == MAPPER_DIR_OUT) {
                // only send /mapTo once even if we have multiple local sources
                if (map->local->one_src && (rs->slots[i] != map->src[0]))
                    continue;
                if (map->dst->link == link) {
                    mapper_network_mesh(net, link->addr.admin);
                    mapper_map_send_state(map, -1, MSG_MAP_TO);
                }
            }
            else {
                for (j = 0; j < map->num_src; j++) {
                    if (map->src[j]->link != link)
                        continue;
                    mapper_network_mesh(net, link->addr.admin);
                    j = mapper_map_send_state(map, map->local->one_src ? -1 : j,
                                              MSG_MAP_TO);
                }
            }
        }
        rs = rs->next;
    }
done:
    if (props)
        mapper_msg_free(props);
    return 0;
}

/*! Handle remote requests to add, modify, or remove metadata to a device. */
static int handler_dev_mod(const char *path, const char *types, lo_arg **av,
                           int ac, lo_message msg, void *user)
{
    mapper_network net = (mapper_network)user;
    mapper_device dev = net->dev;

    if (!dev || !mapper_device_ready(dev))
        return 0;

    if (ac < 2)
        return 0;

    if (types[0] != MAPPER_STRING)
        return 0;

    mapper_msg props = mapper_msg_parse_props(ac, types, av);

    trace_dev(dev, "got /%s/modify + %d properties.\n", path, props->num_atoms);

    if (mapper_device_set_from_msg(dev, props)) {
        if (dev->local->subscribers) {
            trace_dev(dev, "informing subscribers (DEVICE)\n")
            mapper_network_subscribers(net, MAPPER_OBJ_DEVICE);
            mapper_device_send_state(dev, MSG_DEVICE);
        }
        mapper_table_clear_empty_records(dev->obj.props.synced);
    }
    return 0;
}

/*! Respond to /logout by deleting record of device. */
static int handler_logout(const char *path, const char *types, lo_arg **av,
                          int ac, lo_message msg, void *user)
{
    mapper_network net = (mapper_network)user;
    mapper_device dev = net->dev, remote;
    mapper_link link;

    int diff, ordinal;
    char *s;

    if (ac < 1)
        return 0;

    if (types[0] != MAPPER_STRING)
        return 0;

    char *name = &av[0]->s;

    if (!dev) {
        trace_net("got /logout '%s'\n", name);
    }
    else if (dev->local->ordinal.locked) {
        trace_dev(dev, "got /logout '%s'\n", name);
        // Check if we have any links to this device, if so remove them
        remote = mapper_graph_get_device_by_name(net->graph, name);
        link = remote ? mapper_device_get_link_by_remote_device(dev, remote) : 0;
        if (link) {
            // TODO: release maps, call local handlers and inform subscribers
            trace_dev(dev, "removing link to expired device '%s'.\n", name);
        }

        /* Parse the ordinal from the complete name which is in the
         * format: <name>.<n> */
        s = name;
        while (*s != '.' && *s++) {}
        ordinal = atoi(++s);

        // If device name matches
        strtok(name, ".");
        ++name;
        if (strcmp(name, dev->identifier) == 0) {
            // if registered ordinal is within my block, free it
            diff = ordinal - dev->local->ordinal.val - 1;
            if (diff >= 0 && diff < 8) {
                dev->local->ordinal.hints[diff] = 0;
            }
        }
    }

    dev = mapper_graph_get_device_by_name(net->graph, name);
    if (dev) {
        // remove subscriptions
        mapper_graph_unsubscribe(net->graph, dev);
        mapper_graph_remove_device(net->graph, dev, MAPPER_REMOVED, 0);
    }

    return 0;
}

/*! Respond to /subscribe message by adding or renewing a subscription. */
static int handler_subscribe(const char *path, const char *types, lo_arg **av,
                             int ac, lo_message msg, void *user)
{
    mapper_network net = (mapper_network)user;
    mapper_device dev = net->dev;
    int version = -1;

    trace_dev(dev, "got /subscribe.\n");

    lo_address a  = lo_message_get_source(msg);
    if (!a || !ac) {
        trace_dev(dev, "error retrieving subscription source address.\n");
        return 0;
    }

    int i, flags = 0, timeout_seconds = 0;
    for (i = 0; i < ac; i++) {
        if (types[i] != MAPPER_STRING)
            break;
        else if (strcmp(&av[i]->s, "all")==0)
            flags = MAPPER_OBJ_ALL;
        else if (strcmp(&av[i]->s, "device")==0)
            flags |= MAPPER_OBJ_DEVICE;
        else if (strcmp(&av[i]->s, "signals")==0)
            flags |= MAPPER_OBJ_SIGNAL;
        else if (strcmp(&av[i]->s, "inputs")==0)
            flags |= MAPPER_OBJ_INPUT_SIGNAL;
        else if (strcmp(&av[i]->s, "outputs")==0)
            flags |= MAPPER_OBJ_OUTPUT_SIGNAL;
        else if (strcmp(&av[i]->s, "maps")==0)
            flags |= MAPPER_OBJ_MAP;
        else if (strcmp(&av[i]->s, "maps_in")==0)
            flags |= MAPPER_OBJ_MAP_IN;
        else if (strcmp(&av[i]->s, "maps_out")==0)
            flags |= MAPPER_OBJ_MAP_OUT;
        else if (strcmp(&av[i]->s, "@version")==0) {
            // next argument is last device version recorded by subscriber
            ++i;
            if (i < ac && types[i] == MAPPER_INT32)
                version = av[i]->i;
        }
        else if (strcmp(&av[i]->s, "@lease")==0) {
            // next argument is lease timeout in seconds
            ++i;
            if (types[i] == MAPPER_INT32)
                timeout_seconds = av[i]->i;
            else if (types[i] == MAPPER_FLOAT)
                timeout_seconds = (int)av[i]->f;
            else if (types[i] == MAPPER_DOUBLE)
                timeout_seconds = (int)av[i]->d;
            else {
                trace_dev(dev, "error parsing subscription @lease property.\n");
            }
        }
    }

    // add or renew subscription
    mapper_device_manage_subscriber(dev, a, flags, timeout_seconds, version);
    return 0;
}

/*! Register information about a signal. */
static int handler_sig(const char *path, const char *types, lo_arg **av, int ac,
                       lo_message msg, void *user)
{
    mapper_network net = (mapper_network)user;

    if (ac < 2)
        return 1;

    if (types[0] != MAPPER_STRING)
        return 1;

    const char *full_sig_name = &av[0]->s;
    char *signamep, *devnamep;
    int devnamelen = mapper_parse_names(full_sig_name, &devnamep, &signamep);
    if (!devnamep || !signamep || devnamelen >= 1024)
        return 0;

    char devname[1024];
    strncpy(devname, devnamep, devnamelen);
    devname[devnamelen]=0;

    trace_net("got /signal %s:%s\n", devname, signamep);

    mapper_msg props = mapper_msg_parse_props(ac-1, &types[1], &av[1]);
    mapper_graph_add_or_update_signal(net->graph, signamep, devname, props);
    mapper_msg_free(props);

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

    const char *s1=str1,           *s2 = str2;

    while (*s1 && (*s1)!='/') ++s1;
    while (*s2 && (*s2)!='/') ++s2;

    int n1 = s1-str1, n2 = s2-str2;
    if (n1!=n2) return 1;

    int result = strncmp(str1, str2, n1);
    if (!result && rest)
        *rest = s1+1;

    return result;
}

/*! Handle remote requests to add, modify, or remove metadata to a signal. */
static int handler_sig_mod(const char *path, const char *types, lo_arg **av,
                           int ac, lo_message msg,  void *user)
{
    mapper_network net = (mapper_network)user;
    mapper_device dev = net->dev;
    mapper_signal sig = 0;

    if (!dev || !mapper_device_ready(dev))
        return 0;

    if (ac < 2)
        return 0;

    if (types[0] != MAPPER_STRING)
        return 0;

    // retrieve signal
    sig = mapper_device_get_signal_by_name(dev, &av[0]->s);
    if (!sig) {
        trace_dev(dev, "no signal found with name '%s'.\n", &av[0]->s);
        return 0;
    }

    mapper_msg props = mapper_msg_parse_props(ac-1, &types[1], &av[1]);

    trace_dev(dev, "got %s '%s' + %d properties.\n", path, sig->name,
              props->num_atoms);

    if (mapper_signal_set_from_msg(sig, props)) {
        if (dev->local->subscribers) {
            trace_dev(dev, "informing subscribers (SIGNAL)\n")
            if (sig->dir == MAPPER_DIR_OUT)
                mapper_network_subscribers(net, MAPPER_OBJ_OUTPUT_SIGNAL);
            else
                mapper_network_subscribers(net, MAPPER_OBJ_INPUT_SIGNAL);
            mapper_signal_send_state(sig, MSG_SIGNAL);
        }
        mapper_table_clear_empty_records(sig->obj.props.synced);
    }
    return 0;
}

/*! Unregister information about a removed signal. */
static int handler_sig_removed(const char *path, const char *types, lo_arg **av,
                               int ac, lo_message msg, void *user)
{
    mapper_network net = (mapper_network)user;

    if (ac < 1)
        return 1;

    if (types[0] != MAPPER_STRING)
        return 1;

    const char *full_sig_name = &av[0]->s;
    char *signamep, *devnamep;
    int devnamelen = mapper_parse_names(full_sig_name, &devnamep, &signamep);
    if (!devnamep || !signamep || devnamelen >= 1024)
        return 0;

    char devname[1024];
    strncpy(devname, devnamep, devnamelen);
    devname[devnamelen]=0;

    trace_net("got /signal/removed %s:%s\n", devname, signamep);

    mapper_device dev = mapper_graph_get_device_by_name(net->graph, devname);
    if (dev && !dev->local) {
        mapper_signal sig = mapper_device_get_signal_by_name(dev, signamep);
        if (sig)
            mapper_graph_remove_signal(net->graph, sig, MAPPER_REMOVED);
    }

    return 0;
}

/*! Repond to name collisions during allocation, help suggest IDs once allocated. */
static int handler_registered(const char *path, const char *types, lo_arg **av,
                              int ac, lo_message msg, void *user)
{
    mapper_network net = (mapper_network)user;
    mapper_device dev = net->dev;
    char *name;
    mapper_id id;
    int ordinal, diff, temp_id = -1, hint = 0;

    if (ac < 1)
        return 0;
    if (types[0] != MAPPER_STRING)
        return 0;
    name = &av[0]->s;
    if (ac > 1) {
        if (types[1] == MAPPER_INT32)
            temp_id = av[1]->i;
        if (types[2] == MAPPER_INT32)
            hint = av[2]->i;
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
            diff = ordinal - dev->local->ordinal.val - 1;
            if (diff >= 0 && diff < 8) {
                dev->local->ordinal.hints[diff] = -1;
            }
            if (hint) {
                // if suggested id is within my block, store timestamp
                diff = hint - dev->local->ordinal.val - 1;
                if (diff >= 0 && diff < 8) {
                    dev->local->ordinal.hints[diff] = mapper_get_current_time();
                }
            }
        }
    }
    else {
        id = (mapper_id) crc32(0L, (const Bytef *)name, strlen(name)) << 32;
        if (id == dev->obj.id) {
            if (temp_id < net->random_id) {
                /* Count ordinal collisions. */
                ++dev->local->ordinal.collision_count;
                dev->local->ordinal.count_time = mapper_get_current_time();
            }
            else if (temp_id == net->random_id && hint > 0
                     && hint != dev->local->ordinal.val) {
                dev->local->ordinal.val = hint;
                mapper_network_probe_device_name(net, dev);
            }
        }
    }
    return 0;
}

/*! Repond to name probes during allocation, help suggest names once allocated. */
static int handler_probe(const char *path, const char *types, lo_arg **av,
                         int ac, lo_message msg, void *user)
{
    mapper_network net = (mapper_network)user;
    mapper_device dev = net->dev;
    char *name = &av[0]->s;
    int i, temp_id = av[1]->i;
    double current_time;
    mapper_id id;

    trace_dev(dev, "got /name/probe %s %i \n", name, temp_id);

    id = (mapper_id) crc32(0L, (const Bytef *)name, strlen(name)) << 32;
    if (id == dev->obj.id) {
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
            lo_send(net->addr.bus, network_msg_strings[MSG_NAME_REG],
                    "sii", name, temp_id, dev->local->ordinal.val + i + 1);
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

static int parse_sig_names(const char *types, lo_arg **av, int ac, int *src_idx,
                           int *dst_idx, int *prop_idx)
{
    // old protocol: /connect src dst
    // new protocol: /map src1 ... srcN -> dst
    //               /map dst <- src1 ... srcN
    // if we find "->" or "<-" before '@' or end of args, count sources
    int i, num_src = 0;
    if (strncmp(types, "sss", 3))
        return 0;

    if (strcmp(&av[1]->s, "<-") == 0) {
        *src_idx = 2;
        *dst_idx = 0;
        if (ac > 2) {
            i = 2;
            while (i < ac && (types[i] == MAPPER_STRING)) {
                if ((&av[i]->s)[0] == '@')
                    break;
                else
                    ++num_src;
                ++i;
            }
        }
        if (prop_idx)
            *prop_idx = *src_idx + num_src;
    }
    else {
        *src_idx = 0;
        *dst_idx = 1;
        i = 1;
        while (i < ac && (types[i] == MAPPER_STRING)) {
            if ((&av[i]->s)[0] == '@')
                break;
            else if ((strcmp(&av[i]->s, "->") == 0)
                     && ac > (i+1)
                     && (types[i+1] == MAPPER_STRING)
                     && (&av[i+1]->s)[0] != '@') {
                num_src = i;
                *dst_idx = i+1;
                break;
            }
            ++i;
        }
        if (prop_idx)
            *prop_idx = *dst_idx+1;
    }

    /* Check that all signal names are well formed, and that no signal names
     * appear in both source and destination lists. */
    for (i = 0; i < num_src; i++) {
        if (!strchr((&av[*src_idx+i]->s)+1, '/')) {
            trace("malformed source signal name '%s'.\n", &av[*src_idx+i]->s);
            return 0;
        }
        if (strcmp(&av[*src_idx+i]->s, &av[*dst_idx]->s)==0) {
            trace("prevented attempt to connect signal '%s' to itself.\n",
                  &av[*dst_idx]->s);
        }
    }
    if (!strchr((&av[*dst_idx]->s)+1, '/')) {
        trace("malformed destination signal name '%s'.\n", &av[*dst_idx]->s);
        return 0;
    }
    return num_src;
}

/*! When the /map message is received by the destination device, send a /mapTo
 *  message to the source device. */
static int handler_map(const char *path, const char *types, lo_arg **av, int ac,
                       lo_message msg, void *user)
{
    mapper_network net = (mapper_network)user;
    mapper_graph g = net->graph;
    mapper_device dev = net->dev;
    mapper_signal sig = 0;
    int i, num_src, src_idx, dst_idx, prop_idx;

    const char *sig_name = 0;

    num_src = parse_sig_names(types, av, ac, &src_idx, &dst_idx, &prop_idx);
    if (!num_src)
        return 0;

    // check if we are the destination of this mapping
    if (prefix_cmp(&av[dst_idx]->s, mapper_device_get_name(dev), &sig_name)==0) {
        sig = mapper_device_get_signal_by_name(dev, sig_name);
        if (!sig) {
            trace_dev(dev, "no signal found with name '%s'.\n", sig_name);
            return 0;
        }
    }

#ifdef DEBUG
    trace_dev(dev, "%s /map", sig ? "got" : "ignoring");
    if (src_idx)
        printf(" %s <-", &av[dst_idx]->s);
    for (i = 0; i < num_src; i++)
        printf(" %s", &av[src_idx+i]->s);
    if (!src_idx)
        printf(" -> %s", &av[dst_idx]->s);
    printf("\n");
#endif
    if (!sig) {
        return 0;
    }

    // parse arguments from message if any
    mapper_msg props = mapper_msg_parse_props(ac-prop_idx, &types[prop_idx],
                                              &av[prop_idx]);

    mapper_map map = 0;
    mapper_msg_atom atom = props ? mapper_msg_prop(props, MAPPER_PROP_ID) : 0;
    if (atom && atom->types[0] == MAPPER_INT64) {
        map = (mapper_map)mapper_graph_get_object(g, MAPPER_OBJ_MAP,
                                                  (atom->vals[0])->i64);
        /* If a mapping already exists between these signals, forward the
         * message to handler_map_mod() and stop. */
        if (map && map->status >= STATUS_ACTIVE) {
            handler_map_mod(path, types, av, ac, msg, user);
            mapper_msg_free(props);
            return 0;
        }
    }
    if (!map) {
        // try to find map by signal names
        if (!is_alphabetical(num_src, &av[src_idx])) {
            trace_dev(dev, "error in /map: signal names out of order.");
            return 0;
        }
        const char *src_names[num_src];
        for (i = 0; i < num_src; i++) {
            src_names[i] = &av[src_idx+i]->s;
        }
        map = mapper_router_map_in(dev->local->rtr, sig, num_src, src_names);

        /* If a mapping already exists between these signals, forward the
         * message to handler_map_mod() and stop. */
        if (map) {
            if (map->status >= STATUS_ACTIVE)
                handler_map_mod(path, types, av, ac, msg, user);
            mapper_msg_free(props);
            return 0;
        }

        // safety check: make sure we don't have an outgoing map to src (loop)
        if (mapper_router_loop_check(dev->local->rtr, sig, num_src, src_names)) {
            trace_dev(dev, "error in /map: potential loop detected.")
            return 0;
        }

        // create a tentative map (flavourless)
        map = mapper_graph_add_or_update_map(g, num_src, src_names,
                                             &av[dst_idx]->s, 0);
        if (!map) {
            trace_dev(dev, "error creating local map.\n");
            return 0;
        }
    }

    if (!map->local)
        mapper_router_add_map(dev->local->rtr, map);

    mapper_map_set_from_msg(map, props, 1);
    mapper_msg_free(props);

    if (map->local->is_local_only) {
        trace_dev(dev, "map references only local signals... setting state to "
                  "ACTIVE.\n");
        map->status = STATUS_ACTIVE;
        ++dev->num_maps_out;
        ++dev->num_maps_in;

        // Inform subscribers
        if (dev->local->subscribers) {
            trace_dev(dev, "informing subscribers (MAPPED)\n")
            mapper_network_subscribers(net, MAPPER_OBJ_MAP);
            mapper_map_send_state(map, -1, MSG_MAPPED);
        }
        return 0;
    }

    if (map->local->one_src && !map->src[0]->local->rsig
        && map->src[0]->link && map->src[0]->link->addr.admin) {
        mapper_network_mesh(net, map->src[0]->link->addr.admin);
        mapper_map_send_state(map, -1, MSG_MAP_TO);
    }
    else {
        for (i = 0; i < num_src; i++) {
            // do not send if is local mapping
            if (map->src[i]->local->rsig)
                continue;

            // do not send if device host/port not yet known
            if (   !map->src[i]->link
                || !map->src[i]->link->addr.admin)
                continue;

            mapper_network_mesh(net, map->src[i]->link->addr.admin);
            i = mapper_map_send_state(map, i, MSG_MAP_TO);
        }
    }
    return 0;
}

/*! When the /mapTo message is received by a peer device, create a tentative
 *  map and respond with own signal metadata. */
static int handler_map_to(const char *path, const char *types, lo_arg **av,
                          int ac, lo_message msg, void *user)
{
    mapper_network net = (mapper_network)user;
    mapper_graph g = net->graph;
    mapper_device dev = net->dev;
    mapper_signal sig = 0;
    mapper_map map;
    int i, num_src, src_idx, dst_idx, prop_idx;

    const char *sig_name = 0;

    num_src = parse_sig_names(types, av, ac, &src_idx, &dst_idx, &prop_idx);
    if (!num_src)
        return 0;

    // check if we are an endpoint of this map
    if (!src_idx) {
        // check if we are the destination
        if (prefix_cmp(&av[dst_idx]->s, mapper_device_get_name(dev), &sig_name)==0) {
            sig = mapper_device_get_signal_by_name(dev, sig_name);
            if (!sig) {
                trace_dev(dev, "no signal found with name '%s'.\n", sig_name);
                return 0;
            }
        }
    }
    else {
        // check if we are a source – all sources must match!
        for (i = 0; i < num_src; i++) {
            if (prefix_cmp(&av[src_idx+i]->s, mapper_device_get_name(dev),
                           &sig_name)) {
                sig = 0;
                break;
            }
            sig = mapper_device_get_signal_by_name(dev, sig_name);
            if (!sig) {
                trace_dev(dev, "no signal found with name '%s'.\n", sig_name);
                break;
            }
        }
    }

#ifdef DEBUG
    trace_dev(dev, "%s /mapTo", sig ? "got" : "ignoring");
    if (src_idx)
        printf(" %s <-", &av[dst_idx]->s);
    for (i = 0; i < num_src; i++)
        printf(" %s", &av[src_idx+i]->s);
    if (!src_idx)
        printf(" -> %s", &av[dst_idx]->s);
    printf("\n");
#endif
    if (!sig) {
        return 0;
    }

    // ensure names are in alphabetical order
    if (!is_alphabetical(num_src, &av[src_idx])) {
        trace_dev(dev, "error in /mapTo: signal names out of order.\n");
        return 0;
    }
    mapper_msg props = mapper_msg_parse_props(ac-prop_idx, &types[prop_idx],
                                              &av[prop_idx]);
    if (!props) {
        trace_dev(dev, "ignoring /mapTo, no properties.\n");
        return 0;
    }
    mapper_msg_atom atom = mapper_msg_prop(props, MAPPER_PROP_ID);
    if (!atom || atom->types[0] != MAPPER_INT64) {
        trace_dev(dev, "ignoring /mapTo, no 'id' property.\n");
        mapper_msg_free(props);
        return 0;
    }
    mapper_id id = (atom->vals[0])->i64;

    map = mapper_router_map_by_id(dev->local->rtr, sig, id,
                                  src_idx ? MAPPER_DIR_OUT : MAPPER_DIR_IN);
    if (!map) {
        const char *src_names[num_src];
        for (i = 0; i < num_src; i++) {
            src_names[i] = &av[src_idx+i]->s;
        }
        // create a tentative mapping (flavourless)
        map = mapper_graph_add_or_update_map(g, num_src, src_names,
                                             &av[dst_idx]->s, 0);
        if (!map) {
            trace_dev(dev, "error creating local map in handler_map_to\n");
            return 0;
        }

        mapper_router_add_map(dev->local->rtr, map);
    }

    if (map->status < STATUS_ACTIVE) {
        /* Set map properties. */
        mapper_map_set_from_msg(map, props, 1);
    }

    if (map->status >= STATUS_READY) {
        if (map->dst->dir == MAPPER_DIR_OUT) {
            mapper_network_mesh(net, map->dst->link->addr.admin);
            mapper_map_send_state(map, -1, MSG_MAPPED);
        }
        else {
            for (i = 0; i < map->num_src; i++) {
                mapper_network_mesh(net, map->src[i]->link->addr.admin);
                i = mapper_map_send_state(map, map->local->one_src ? -1 : i,
                                          MSG_MAPPED);
            }
        }
    }
    mapper_msg_free(props);
    return 0;
}

/*! Respond to /mapped by storing mapping in graph. */
/*! Also used by devices to confirm connection to remote peers, and to share
 *  changes in mapping properties. */
static int handler_mapped(const char *path, const char *types, lo_arg **av,
                          int ac, lo_message msg, void *user)
{
    mapper_network net = (mapper_network)user;
    mapper_graph g = net->graph;
    mapper_device dev = net->dev;
    mapper_signal sig = 0;
    mapper_map map;
    int i, num_src, src_idx, dst_idx, prop_idx;
    const char *sig_name;

    num_src = parse_sig_names(types, av, ac, &src_idx, &dst_idx, &prop_idx);
    if (!num_src)
        return 0;

    if (!is_alphabetical(num_src, &av[src_idx])) {
        trace("error in /mapped: signals names out of order.");
        return 0;
    }

    // 2 scenarios: if message in form A -> B, only B interested
    //              if message in form A <- B, only B interested

    if (dev && mapper_device_get_name(dev)) {
        // check if we are an endpoint of this mapping
        if (!src_idx) {
            // check if we are the destination
            if (prefix_cmp(&av[dst_idx]->s, mapper_device_get_name(dev),
                           &sig_name)==0) {
                sig = mapper_device_get_signal_by_name(dev, sig_name);
            }
        }
        else {
            // check if we are a source – all sources must match!
            for (i = 0; i < num_src; i++) {
                if (prefix_cmp(&av[src_idx+i]->s, mapper_device_get_name(dev),
                               &sig_name)) {
                    sig = 0;
                    break;
                }
                sig = mapper_device_get_signal_by_name(dev, sig_name);
            }
        }
    }

#ifdef DEBUG
    if (dev) {
        trace_dev(dev, "%s /mapped", sig ? "got" : "ignoring");}
    else
        trace_net("got /mapped");
    if (src_idx)
        printf(" %s <-", &av[dst_idx]->s);
    for (i = 0; i < num_src; i++)
        printf(" %s", &av[src_idx+i]->s);
    if (!src_idx)
        printf(" -> %s", &av[dst_idx]->s);
    printf("\n");
#endif

    mapper_msg props = mapper_msg_parse_props(ac-prop_idx, &types[prop_idx],
                                              &av[prop_idx]);
    if (!sig) {
        int store = 0;
        if (g->autosubscribe & MAPPER_OBJ_MAP)
            store = 1;
        else if (mapper_graph_subscribed_by_sig_name(g, &av[dst_idx]->s))
            store = 1;
        else {
            for (i = 0; i < num_src; i++) {
                if (mapper_graph_subscribed_by_sig_name(g, &av[src_idx+i]->s)) {
                    store = 1;
                    break;
                }
            }
        }
        if (store) {
            const char *src_names[num_src];
            for (i = 0; i < num_src; i++) {
                src_names[i] = &av[src_idx+i]->s;
            }
            mapper_graph_add_or_update_map(g, num_src, src_names,
                                           &av[dst_idx]->s, props);
        }
        mapper_msg_free(props);
        return 0;
    }

    mapper_msg_atom atom = props ? mapper_msg_prop(props, MAPPER_PROP_ID) : 0;
    if (!atom || atom->types[0] != MAPPER_INT64) {
        trace_dev(dev, "ignoring /mapped, no 'id' property.\n");
        mapper_msg_free(props);
        return 0;
    }
    mapper_id id = (atom->vals[0])->i64;

    map = mapper_router_map_by_id(dev->local->rtr, sig, id,
                                  src_idx ? MAPPER_DIR_OUT : MAPPER_DIR_IN);
    if (!map) {
        trace_dev(dev, "no map found for /mapped.\n");
        mapper_msg_free(props);
        return 0;
    }
    if (src_idx && map->num_src != num_src) {
        trace_dev(dev, "wrong num_sources in /mapped.\n");
        mapper_msg_free(props);
        return 0;
    }

    if (map->local->is_local_only) {
        // no need to update since all properties are local
        mapper_msg_free(props);
        return 0;
    }

    // TODO: if this endpoint is map admin, do not allow overwiting props
    int rc = 0, updated = mapper_map_set_from_msg(map, props, 0);

    if (map->status < STATUS_READY) {
        mapper_msg_free(props);
        return 0;
    }
    if (map->status == STATUS_READY) {
        map->status = STATUS_ACTIVE;
        rc = 1;
        mapper_device dev;

        // Inform remote peer(s)
        if (map->dst->dir == MAPPER_DIR_OUT) {
            mapper_network_mesh(net, map->dst->link->addr.admin);
            mapper_map_send_state(map, -1, MSG_MAPPED);
            dev = map->src[0]->sig->dev;
            ++dev->num_maps_out;
        }
        else {
            for (i = 0; i < map->num_src; i++) {
                mapper_network_mesh(net, map->src[i]->link->addr.admin);
                i = mapper_map_send_state(map, map->local->one_src ? -1 : i,
                                          MSG_MAPPED);
            }
            dev = map->dst->sig->dev;
            ++dev->num_maps_in;
        }
    }
    if (rc || updated) {
        if (dev->local->subscribers) {
            trace_dev(dev, "informing subscribers (MAPPED)\n")
            // Inform subscribers
            if (map->dst->dir == MAPPER_DIR_OUT)
                mapper_network_subscribers(net, MAPPER_OBJ_MAP_OUT);
            else
                mapper_network_subscribers(net, MAPPER_OBJ_MAP_IN);
            mapper_map_send_state(map, -1, MSG_MAPPED);
        }
        fptr_list cb = g->callbacks, temp;
        while (cb) {
            temp = cb->next;
            if (cb->types & MAPPER_OBJ_MAP) {
                mapper_graph_handler *h = cb->f;
                h(g, (mapper_object)map,
                  rc ? MAPPER_ADDED : MAPPER_MODIFIED, cb->context);
            }
            cb = temp;
        }
    }
    mapper_msg_free(props);
    mapper_table_clear_empty_records(map->obj.props.synced);
    return 0;
}

/*! Modify the map properties : mode, range, expression, etc. */
static int handler_map_mod(const char *path, const char *types, lo_arg **av,
                           int ac, lo_message msg, void *user)
{
    mapper_network net = (mapper_network)user;
    mapper_device dev = net->dev;
    mapper_map map = 0;
    int i;

    if (ac < 4)
        return 0;

    // check the map's id
    for (i = 3; i < ac; i++) {
        if (types[i] != MAPPER_STRING)
            continue;
        if (strcmp(&av[i]->s, "@id")==0)
            break;
    }
    if (i < ac && types[++i] == MAPPER_INT64)
        map = (mapper_map)mapper_graph_get_object(net->graph, MAPPER_OBJ_MAP,
                                                  av[i]->i64);
    else {
        // try to find map by signal names
        mapper_signal sig = 0;
        int num_src, src_idx, dst_idx, prop_idx;
        const char *sig_name;
        num_src = parse_sig_names(types, av, ac, &src_idx, &dst_idx, &prop_idx);
        if (!num_src)
            return 0;
        if (!is_alphabetical(num_src, &av[src_idx])) {
            trace_dev(dev, "error in /map/modify: signal names out of order.");
            return 0;
        }
        const char *src_names[num_src];
        for (i = 0; i < num_src; i++) {
            src_names[i] = &av[src_idx+i]->s;
        }
        // check if we are the destination
        if (prefix_cmp(&av[dst_idx]->s, mapper_device_get_name(dev),
                       &sig_name)==0) {
            sig = mapper_device_get_signal_by_name(dev, sig_name);
            if (!sig) {
                trace_dev(dev, "no signal found with name '%s'.\n", sig_name);
            }
            map = mapper_router_map_in(dev->local->rtr, sig, num_src, src_names);
        }
        else {
            // check if we are a source – all sources must match!
            for (i = 0; i < num_src; i++) {
                if (prefix_cmp(src_names[i], mapper_device_get_name(dev),
                               &sig_name)) {
                    sig = 0;
                    break;
                }
                sig = mapper_device_get_signal_by_name(dev, sig_name);
                if (!sig) {
                    trace_dev(dev, "no signal found with name '%s'.\n", sig_name);
                    break;
                }
            }
            if (sig)
                map = mapper_router_map_out(dev->local->rtr, sig, num_src,
                                            src_names, &av[dst_idx]->s);
        }
    }

#ifdef DEBUG
    trace_dev(dev, "%s /map/modify\n", map && map->local ? "got" : "ignoring");
#endif
    if (!map || !map->local || map->status < STATUS_ACTIVE) {
        return 0;
    }

    mapper_msg props = mapper_msg_parse_props(ac, types, av);
    if (!props) {
        trace_dev(dev, "ignoring /map/modify, no properties.\n");
        return 0;
    }

    mapper_msg_atom atom;
    atom = mapper_msg_prop(props, MAPPER_PROP_PROCESS_LOC);
    if (atom) {
        map->process_loc = mapper_loc_from_string(&(atom->vals[0])->s);
        if (!map->local->one_src) {
            /* if map has sources from different remote devices, processing must
             * occur at the destination. */
            map->process_loc = MAPPER_LOC_DST;
        }
        else if ((atom = mapper_msg_prop(props, MAPPER_PROP_EXPR))) {
            if (strstr(&atom->vals[0]->s, "y={-")) {
                map->process_loc = MAPPER_LOC_DST;
            }
        }
        else if (map->expr_str && strstr(map->expr_str, "y{-")) {
            map->process_loc = MAPPER_LOC_DST;
        }
    }

    // do not continue if we are not in charge of processing
    if (map->process_loc == MAPPER_LOC_DST) {
        if (!map->dst->sig->local) {
            trace_dev(dev, "ignoring /map/modify, slaved to remote device.\n");
            return 0;
        }
    }
    else if (!map->src[0]->sig->local) {
        trace_dev(dev, "ignoring /map/modify, slaved to remote device.\n");
        return 0;
    }

    int updated = mapper_map_set_from_msg(map, props, 1);

    if (updated) {
        if (!map->local->is_local_only) {
            // TODO: may not need to inform all remote peers
            // Inform remote peer(s) of relevant changes
            if (!map->dst->local->rsig) {
                mapper_network_mesh(net, map->dst->link->addr.admin);
                mapper_map_send_state(map, -1, MSG_MAPPED);
            }
            else {
                for (i = 0; i < map->num_src; i++) {
                    if (map->src[i]->local->rsig)
                        continue;
                    mapper_network_mesh(net, map->src[i]->link->addr.admin);
                    i = mapper_map_send_state(map, i, MSG_MAPPED);
                }
            }
        }

        if (dev->local->subscribers) {
            trace_dev(dev, "informing subscribers (MAPPED)\n")
            // Inform subscribers
            if (map->dst->local->rsig)
                mapper_network_subscribers(net, MAPPER_OBJ_MAP_IN);
            else
                mapper_network_subscribers(net, MAPPER_OBJ_MAP_OUT);
            mapper_map_send_state(map, -1, MSG_MAPPED);
        }
    }
    trace_dev(dev, "updated %d map properties.\n", updated);

    mapper_msg_free(props);
    mapper_table_clear_empty_records(map->obj.props.synced);
    return 0;
}

/*! Unmap a set of signals. */
static int handler_unmap(const char *path, const char *types, lo_arg **av,
                         int ac, lo_message msg, void *user)
{
    mapper_network net = (mapper_network)user;
    mapper_device dev = net->dev;
    mapper_signal sig = 0;
    mapper_map map = 0;
    int i, num_src, src_idx, dst_idx;
    const char *sig_name;

    num_src = parse_sig_names(types, av, ac, &src_idx, &dst_idx, 0);
    if (!num_src)
        return 0;

    if (!is_alphabetical(num_src, &av[src_idx])) {
        trace_dev(dev, "error in /unmap: signal names out of order.");
        return 0;
    }
    const char *src_names[num_src];
    for (i = 0; i < num_src; i++) {
        src_names[i] = &av[src_idx+i]->s;
    }

    // check if we are the destination
    if (prefix_cmp(&av[dst_idx]->s, mapper_device_get_name(dev), &sig_name)==0) {
        sig = mapper_device_get_signal_by_name(dev, sig_name);
        if (!sig) {
            trace_dev(dev, "no signal found with name '%s'.\n", sig_name);
            return 0;
        }
        map = mapper_router_map_in(dev->local->rtr, sig, num_src, src_names);
    }
    else {
        // check if we are a source – all sources must match!
        for (i = 0; i < num_src; i++) {
            if (prefix_cmp(src_names[i], mapper_device_get_name(dev), &sig_name)) {
                sig = 0;
                break;
            }
            sig = mapper_device_get_signal_by_name(dev, sig_name);
            if (!sig) {
                trace_dev(dev, "no signal found with name '%s'.\n", sig_name);
                break;
            }
        }
        if (sig)
            map = mapper_router_map_out(dev->local->rtr, sig, num_src,
                                        src_names, &av[dst_idx]->s);
    }

#ifdef DEBUG
    trace_dev(dev, "%s /unmap", map ? "got" : "ignoring");
    if (src_idx)
        printf(" %s <-", &av[dst_idx]->s);
    for (i = 0; i < num_src; i++)
        printf(" %s", &av[src_idx+i]->s);
    if (!src_idx)
        printf(" -> %s", &av[dst_idx]->s);
    printf("\n");
#endif
    if (!map) {
        return 0;
    }

    // inform remote peer(s)
    if (!map->dst->local->rsig) {
        mapper_network_mesh(net, map->dst->link->addr.admin);
        mapper_map_send_state(map, -1, MSG_UNMAP);
    }
    else {
        for (i = 0; i < map->num_src; i++) {
            if (map->src[i]->local->rsig)
                continue;
            mapper_network_mesh(net, map->src[i]->link->addr.admin);
            i = mapper_map_send_state(map, i, MSG_UNMAP);
        }
    }

    if (dev->local->subscribers) {
        trace_dev(dev, "informing subscribers (UNMAPPED)\n")
        // Inform subscribers
        if (map->dst->local->rsig)
            mapper_network_subscribers(net, MAPPER_OBJ_MAP_IN);
        else
            mapper_network_subscribers(net, MAPPER_OBJ_MAP_OUT);
        mapper_map_send_state(map, -1, MSG_UNMAPPED);
    }

    /* The mapping is removed. */
    mapper_router_remove_map(dev->local->rtr, map);
    mapper_graph_remove_map(net->graph, map, MAPPER_REMOVED);
    // TODO: remove empty router_signals
    return 0;
}

/*! Respond to /unmapped by removing map from graph. */
static int handler_unmapped(const char *path, const char *types, lo_arg **av,
                            int ac, lo_message msg, void *user)
{
    mapper_network net = (mapper_network)user;
    int i, id_idx;
    mapper_id *id = 0;

    for (i = 0; i < ac; i++) {
        if (types[i] != MAPPER_STRING)
            return 0;
        if (strcmp(&av[i]->s, "@id")==0 && (types[i+1] == MAPPER_INT64)) {
            id_idx = i+1;
            id = (mapper_id*) &av[id_idx]->i64;
            break;
        }
    }
    if (!id) {
        trace_net("error: no 'id' property found in /unmapped message.")
        return 0;
    }

#ifdef DEBUG
    trace_net("got /unmapped");
    for (i = 0; i < id_idx; i++)
        printf(" %s", &av[i]->s);
    printf("\n");
#endif

    mapper_map map = (mapper_map)mapper_graph_get_object(net->graph,
                                                         MAPPER_OBJ_MAP, *id);
    if (map)
        mapper_graph_remove_map(net->graph, map, MAPPER_REMOVED);

    return 0;
}

static int handler_ping(const char *path, const char *types, lo_arg **av,
                        int ac, lo_message msg, void *user)
{
    mapper_network net = (mapper_network)user;
    mapper_device dev = net->dev, remote;
    mapper_link link;

    if (!dev)
        return 0;

    mapper_time_t now;
    mapper_time_now(&now);
    lo_timetag then = lo_message_get_timestamp(msg);

    remote = (mapper_device)mapper_graph_get_object(net->graph, MAPPER_OBJ_DEVICE,
                                                    av[0]->h);
    link = remote ? mapper_device_get_link_by_remote_device(dev, remote) : 0;
    if (link) {
        mapper_sync_clock clock = &link->clock;
        trace_dev(dev, "ping received from linked device '%s'\n",
                  link->remote_dev->name);
        if (av[2]->i == clock->sent.msg_id) {
            // total elapsed time since ping sent
            double elapsed = mapper_time_difference(now, clock->sent.time);
            // assume symmetrical latency
            double latency = (elapsed - av[3]->d) * 0.5;
            // difference between remote and local clocks (latency compensated)
            double offset = mapper_time_difference(now, then) - latency;

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
                    // remote time is in the future
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
        mapper_time_copy(&clock->response.time, now);
        clock->response.msg_id = av[1]->i;
    }
    return 0;
}

static int handler_sync(const char *path, const char *types, lo_arg **av,
                        int ac, lo_message msg, void *user)
{
    mapper_network net = (mapper_network)user;

    if (!net || !ac)
        return 0;
    if (types[0] != MAPPER_STRING)
        return 0;

    mapper_device dev = mapper_graph_get_device_by_name(net->graph, &av[0]->s);
    if (dev) {
        if (dev->local)
            return 0;
        trace_graph("updating sync record for device '%s'\n", dev->name);
        mapper_time_copy(&dev->synced, lo_message_get_timestamp(msg));

        if (!dev->subscribed && net->graph->autosubscribe) {
            trace_graph("autosubscribing to device '%s'.\n", &av[0]->s);
            mapper_graph_subscribe(net->graph, dev, net->graph->autosubscribe, -1);
        }
    }
    else if (net->graph->autosubscribe) {
        // only create device record after requesting more information
        trace_graph("requesting metadata for device '%s'.\n", &av[0]->s);
        mapper_device_t temp;
        temp.name = &av[0]->s;
        temp.obj.version = -1;
        temp.local = 0;
        mapper_graph_subscribe(net->graph, &temp, MAPPER_OBJ_DEVICE, 0);
    }
    else {
        trace_graph("ignoring sync from '%s' (autosubscribe = %d)\n", &av[0]->s,
                    net->graph->autosubscribe);
    }

    return 0;
}
