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

#include "mpr_internal.h"
#include "types_internal.h"
#include "config.h"
#include <mpr/mpr.h>

extern const char* prop_msg_strings[MPR_PROP_EXTRA+1];

// set to 1 to force mesh comms to use multicast bus instead for debugging
#define FORCE_COMMS_TO_BUS      0
#define BUNDLE_DST_SUBSCRIBERS (void*)-1
#define BUNDLE_DST_BUS          0

#define MAX_BUNDLE_COUNT 10

#if FORCE_COMMS_TO_BUS
    #define NET_SERVER_FUNC(NET, FUNC, ...)                     \
    {                                                           \
        lo_server_ ## FUNC((NET)->server.bus, __VA_ARGS__);     \
    }
#else
    #define NET_SERVER_FUNC(NET, FUNC, ...)                     \
    {                                                           \
        lo_server_ ## FUNC((NET)->server.bus, __VA_ARGS__);     \
        lo_server_ ## FUNC((NET)->server.mesh, __VA_ARGS__);    \
    }
#endif

static int is_alphabetical(int num, lo_arg **names)
{
    RETURN_UNLESS(num > 1, 1);
    int i;
    for (i = 1; i < num; i++)
        TRACE_RETURN_UNLESS(strcmp(&names[i-1]->s, &names[i]->s)<0, 0,
                            "error: signal names out of order.");
    return 1;
}

/* Extract the ordinal from a device name in the format: <name>.<n> */
static int extract_ordinal(char *name) {
    char *s = name;
    RETURN_UNLESS(s = strrchr(s, '.'), -1);
    int ordinal = atoi(s+1);
    *s = 0;
    return ordinal;
}

const char* net_msg_strings[] =
{
    "/device",                  /* MSG_DEV */
    "/%s/modify",               /* MSG_DEV_MOD */
    "/logout",                  /* MSG_LOGOUT */
    "/map",                     /* MSG_MAP */
    "/mapTo",                   /* MSG_MAP_TO */
    "/mapped",                  /* MSG_MAPPED */
    "/map/modify",              /* MSG_MAP_MOD */
    "/name/probe",              /* MSG_NAME_PROBE */
    "/name/registered",         /* MSG_NAME_REG */
    "/ping",                    /* MSG_PING */
    "/signal",                  /* MSG_SIG */
    "/signal/removed",          /* MSG_SIG_REM */
    "/%s/signal/modify",        /* MSG_SIG_MOD */
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
static int handler_name_probe(HANDLER_ARGS);
static int handler_name(HANDLER_ARGS);
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
    {MSG_DEV,                   NULL,       handler_dev},
    {MSG_DEV_MOD,               NULL,       handler_dev_mod},
    {MSG_LOGOUT,                NULL,       handler_logout},
    {MSG_MAP,                   NULL,       handler_map},
    {MSG_MAP_TO,                NULL,       handler_map_to},
    {MSG_MAPPED,                NULL,       handler_mapped},
    {MSG_MAP_MOD,               NULL,       handler_map_mod},
    {MSG_PING,                  "hiid",     handler_ping},
    {MSG_SIG,                   NULL,       handler_sig},
    {MSG_SIG_MOD,               NULL,       handler_sig_mod},
    {MSG_SIG_REM,               "s",        handler_sig_removed},
    {MSG_SUBSCRIBE,             NULL,       handler_subscribe},
    {MSG_UNMAP,                 NULL,       handler_unmap},
    {MSG_WHO,                   NULL,       handler_who},
};
const int NUM_DEV_HANDLERS =
    sizeof(device_handlers)/sizeof(device_handlers[0]);

// handlers needed by graph for archiving
static struct handler_method_assoc graph_handlers[] = {
    {MSG_DEV,                   NULL,       handler_dev},
    {MSG_LOGOUT,                NULL,       handler_logout},
    {MSG_MAPPED,                NULL,       handler_mapped},
    {MSG_SIG,                   NULL,       handler_sig},
    {MSG_SIG_REM,               "s",        handler_sig_removed},
    {MSG_SYNC,                  NULL,       handler_sync},
    {MSG_UNMAPPED,              NULL,       handler_unmapped},
};
const int NUM_GRAPH_HANDLERS =
    sizeof(graph_handlers)/sizeof(graph_handlers[0]);

/* Internal LibLo error handler */
static void handler_error(int num, const char *msg, const char *where)
{
    trace_net("[libmpr] liblo server error %d in path %s: %s\n", num, where, msg);
}

/* Functions for handling the resource allocation scheme.  If check_collisions()
 * returns 1, the resource in question should be probed on the libmpr bus. */
static int check_collisions(mpr_net net, mpr_allocated resource);

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
    RETURN_UNLESS(0 == getifaddrs(&ifaphead), 1);

    ifap = ifaphead;
    while (ifap) {
        sa = (struct sockaddr_in *) ifap->ifa_addr;
        if (!sa) {
            ifap = ifap->ifa_next;
            continue;
        }

        /* Note, we could also check for IFF_MULTICAST-- however this is the
         * data-sending port, not the libmpr bus port. */

        if (AF_INET == sa->sin_family && ifap->ifa_flags & IFF_UP
            && memcmp(&sa->sin_addr, &zero, sizeof(struct in_addr))!=0) {
            ifchosen = ifap;
            if (pref && 0 == strcmp(ifap->ifa_name, pref))
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
        FUNC_IF(free, *iface);
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
    RETURN_UNLESS(ERROR_SUCCESS == rc, 2);

    PIP_ADAPTER_ADDRESSES loaa=0, aa = paa;
    PIP_ADAPTER_UNICAST_ADDRESS lopua=0;
    while (aa && ERROR_SUCCESS == rc) {
        PIP_ADAPTER_UNICAST_ADDRESS pua = aa->FirstUnicastAddress;
        // Skip adapters that are not "Up".
        if (pua && IfOperStatusUp == aa->OperStatus) {
            if (IF_TYPE_SOFTWARE_LOOPBACK == aa->IfType) {
                loaa = aa;
                lopua = pua;
            }
            else {
                // Skip addresses starting with 0.X.X.X or 169.X.X.X.
                sa = (struct sockaddr_in *) pua->Address.lpSockaddr;
                unsigned char prefix = sa->sin_addr.s_addr&0xFF;
                if (prefix!=0xA9 && prefix!=0) {
                    FUNC_IF(free, *iface);
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
        FUNC_IF(free, *iface);
        *iface = strdup(loaa->AdapterName);
        sa = (struct sockaddr_in *) lopua->Address.lpSockaddr;
        *addr = sa->sin_addr;
        free(paa);
        return 0;
    }

    FUNC_IF(free, paa);

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
        if (1 == fread(&s, 4, 1, f)) {
            srand(s);
            fclose(f);
            return;
        }
        fclose(f);
    }
#endif

    double d = mpr_get_current_time();
    s = (unsigned int)((d-(unsigned long)d)*100000);
    srand(s);
}

static void mpr_net_add_dev_methods(mpr_net net, mpr_dev dev)
{
    int i;
    char path[256];
    for (i = 0; i < NUM_DEV_HANDLERS; i++) {
        snprintf(path, 256, net_msg_strings[device_handlers[i].str_idx],
                 mpr_dev_get_name(dev));
        NET_SERVER_FUNC(net, add_method, path, device_handlers[i].types,
                        device_handlers[i].h, net);
    }
}

void mpr_net_remove_dev_methods(mpr_net net, mpr_dev dev)
{
    int i, j;
    char path[256];
    for (i = 0; i < NUM_DEV_HANDLERS; i++) {
        // make sure method isn't also used by graph
        int found = 0;
        for (j = 0; j < NUM_GRAPH_HANDLERS; j++) {
            if (device_handlers[i].str_idx == graph_handlers[j].str_idx) {
                found = 1;
                break;
            }
        }
        if (found)
            continue;
        snprintf(path, 256, net_msg_strings[device_handlers[i].str_idx],
                 mpr_dev_get_name(dev));
        NET_SERVER_FUNC(net, del_method, path, device_handlers[i].types);
    }
}

void mpr_net_add_graph_methods(mpr_net net)
{
    // add graph methods
    int i;
    for (i = 0; i < NUM_GRAPH_HANDLERS; i++)
        NET_SERVER_FUNC(net, add_method, net_msg_strings[graph_handlers[i].str_idx],
                        graph_handlers[i].types, graph_handlers[i].h, net);
    return;
}

void mpr_net_init(mpr_net net, const char *iface, const char *group, int port)
{
    /* Default standard ip and port is group 224.0.1.3, port 7570 */
    char port_str[10], *s_port = port_str;

    if (!net->multicast.group)
        net->multicast.group = strdup(group ?: "224.0.1.3");
    if (!net->multicast.port)
        net->multicast.port = port ?: 7570;
    snprintf(port_str, 10, "%d", net->multicast.port);

    /* Initialize interface information. */
    get_iface_addr(iface, &net->iface.addr, &net->iface.name);
    trace_net("found interface: %s\n", net->iface.name ?: "none");

    /* Remove existing structures if necessary */
    FUNC_IF(lo_address_free, net->addr.bus);
    FUNC_IF(lo_server_free, net->server.bus);
    FUNC_IF(lo_server_free, net->server.mesh);

    /* Open address */
    net->addr.bus = lo_address_new(net->multicast.group, s_port);
    if (!net->addr.bus) {
        trace_net("problem allocating bus address.\n");
        return;
    }

    /* Set TTL for packet to 1 -> local subnet */
    lo_address_set_ttl(net->addr.bus, 1);

    /* Specify the interface to use for multicasting */
    lo_address_set_iface(net->addr.bus, net->iface.name, 0);

    /* Open server for multicast */
    net->server.bus = lo_server_new_multicast_iface(net->multicast.group, s_port,
                                                    net->iface.name, 0,
                                                    handler_error);

    if (!net->server.bus) {
        lo_address_free(net->addr.bus);
        trace_net("problem allocating bus server.\n");
        return;
    }
    else
        trace_net("bus connected to %s:%s\n", net->multicast.group, s_port);

    // Also open address/server for mesh-style communications
    // TODO: use TCP instead?
    while (!(net->server.mesh = lo_server_new(0, handler_error))) {}

    // Disable liblo message queueing.
    NET_SERVER_FUNC(net, enable_queue, 0, 1);

    mpr_net_add_graph_methods(net);
}

const char *mpr_get_version()
{
    return PACKAGE_VERSION;
}

void mpr_net_send(mpr_net net)
{
    RETURN_UNLESS(net->bundle);

#if FORCE_COMMS_TO_BUS
    lo_send_bundle_from(net->addr.bus, net->server.mesh, net->bundle);
#else
    if (BUNDLE_DST_SUBSCRIBERS == net->addr.dst) {
        mpr_subscriber *sub = &net->addr.dev->loc->subscribers;
        mpr_time t;
        if (*sub)
            mpr_time_set(&t, MPR_NOW);
        while (*sub) {
            if ((*sub)->lease_exp < t.sec || !(*sub)->flags) {
                // subscription expired, remove from subscriber list
                trace_dev(net->addr.dev, "removing expired subscription from "
                          "%s\n", lo_address_get_url((*sub)->addr));
                mpr_subscriber temp = *sub;
                *sub = temp->next;
                FUNC_IF(lo_address_free, temp->addr);
                free(temp);
                continue;
            }
            if ((*sub)->flags & net->msg_type)
                lo_send_bundle_from((*sub)->addr, net->server.mesh, net->bundle);
            sub = &(*sub)->next;
        }
    }
    else if (BUNDLE_DST_BUS == net->addr.dst)
        lo_send_bundle_from(net->addr.bus, net->server.mesh, net->bundle);
    else
        lo_send_bundle_from(net->addr.dst, net->server.mesh, net->bundle);
#endif
    lo_bundle_free_recursive(net->bundle);
    net->bundle = 0;
}

static int init_bundle(mpr_net net)
{
    if (net->bundle)
        mpr_net_send(net);
    mpr_time t;
    mpr_time_set(&t, MPR_NOW);
    net->bundle = lo_bundle_new(t);
    return net->bundle ? 0 : 1;
}

void mpr_net_use_bus(mpr_net net)
{
    if (net->bundle && (   net->addr.dst != BUNDLE_DST_BUS
                        || lo_bundle_count(net->bundle) >= MAX_BUNDLE_COUNT))
        mpr_net_send(net);
    net->addr.dst = BUNDLE_DST_BUS;
    if (!net->bundle)
        init_bundle(net);
}

void mpr_net_use_mesh(mpr_net net, lo_address addr)
{
    if (net->bundle && (   net->addr.dst != addr
                        || lo_bundle_count(net->bundle) >= MAX_BUNDLE_COUNT))
        mpr_net_send(net);
    net->addr.dst = addr;
    if (!net->bundle)
        init_bundle(net);
}

void mpr_net_use_subscribers(mpr_net net, mpr_dev dev, int type)
{
    if (net->bundle && (   net->addr.dst != BUNDLE_DST_SUBSCRIBERS
                        || net->addr.dev != dev
                        || net->msg_type != type
                        || lo_bundle_count(net->bundle) >= MAX_BUNDLE_COUNT))
        mpr_net_send(net);
    net->addr.dst = BUNDLE_DST_SUBSCRIBERS;
    net->addr.dev = dev;
    net->msg_type = type;
    if (!net->bundle)
        init_bundle(net);
}

void mpr_net_add_msg(mpr_net net, const char *s, net_msg_t c, lo_message m)
{
    if (lo_bundle_count(net->bundle) >= MAX_BUNDLE_COUNT) {
        mpr_net_send(net);
        init_bundle(net);
    }
    lo_bundle_add_message(net->bundle, s ?: net_msg_strings[c], m);
}

void mpr_net_free_msgs(mpr_net net)
{
    FUNC_IF(free, net->devs);
    FUNC_IF(lo_bundle_free_recursive, net->bundle);
    net->bundle = 0;
}

/*! Free the memory allocated by a network structure.
 *  \param net      A network structure handle. */
void mpr_net_free(mpr_net net)
{
    // send out any cached messages
    mpr_net_send(net);
    FUNC_IF(free, net->iface.name);
    FUNC_IF(free, net->multicast.group);
    FUNC_IF(lo_server_free, net->server.bus);
    FUNC_IF(lo_server_free, net->server.mesh);
    FUNC_IF(lo_address_free, net->addr.bus);
}

/*! Probe the network to see if a device's proposed name.ordinal is available. */
static void mpr_net_probe_dev_name(mpr_net net, mpr_dev dev)
{
    int i;

    // reset collisions and hints
    dev->loc->ordinal.collision_count = 0;
    dev->loc->ordinal.count_time = mpr_get_current_time();
    for (i = 0; i < 8; i++)
        dev->loc->ordinal.hints[i] = 0;

    /* Note: mpr_dev_get_name() would refuse here since the ordinal is not
     * yet locked, so we have to build it manually at this point. */
    char name[256];
    snprintf(name, 256, "%s.%d", dev->prefix, dev->loc->ordinal.val);
    trace_dev(dev, "probing name '%s'\n", name);

    /* Calculate an id from the name and store it in id.val */
    dev->obj.id = (mpr_id) crc32(0L, (const Bytef *)name, strlen(name)) << 32;

    /* For the same reason, we can't use mpr_net_send() here. */
    lo_send(net->addr.bus, net_msg_strings[MSG_NAME_PROBE], "si", name,
            net->random_id);
}

/*! Add an uninitialized device to this network. */
void mpr_net_add_dev(mpr_net net, mpr_dev dev)
{
    RETURN_UNLESS(dev);

    /* Check if device was already added. */
    int i;
    for (i = 0; i < net->num_devs; i++) {
        if (net->devs[i] == dev)
            return;
    }

    /* Initialize data structures */
    net->devs = realloc(net->devs, (net->num_devs+1)*sizeof(mpr_dev));
    net->devs[net->num_devs] = dev;
    ++net->num_devs;

    /* Seed the random number generator. */
    seed_srand();

    /* Choose a random ID for allocation speedup */
    net->random_id = rand();

    /* Add allocation methods for bus communications. Further methods are added
     * when the device is registered. */
    lo_server_add_method(net->server.bus, net_msg_strings[MSG_NAME_PROBE], "si",
                         handler_name_probe, net);
    lo_server_add_method(net->server.bus, net_msg_strings[MSG_NAME_REG], NULL,
                         handler_name, net);

    /* Probe potential name. */
    mpr_net_probe_dev_name(net, dev);
}

static void _send_device_sync(mpr_net net, mpr_dev dev)
{
    NEW_LO_MSG(msg, return);
    lo_message_add_string(msg, mpr_dev_get_name(dev));
    lo_message_add_int32(msg, dev->obj.version);
    mpr_net_add_msg(net, 0, MSG_SYNC, msg);
}

// TODO: rename to mpr_dev...?
static void mpr_net_maybe_send_ping(mpr_net net, int force)
{
    RETURN_UNLESS(net->num_devs);
    int go = 0, i;
    mpr_graph gph = net->graph;
    mpr_time now;
    mpr_time_set(&now, MPR_NOW);
    for (i = 0; i < net->num_devs; i++) {
        mpr_dev dev = net->devs[i];
        if (dev->loc->subscribers && (now.sec >= net->next_sub_ping)) {
            mpr_net_use_subscribers(net, dev, MPR_DEV);
            _send_device_sync(net, dev);
            net->next_sub_ping = now.sec + 2;
        }
    }
    if (force || (now.sec >= net->next_bus_ping)) {
        go = 1;
        net->next_bus_ping = now.sec + 5 + (rand() % 4);
    }
    RETURN_UNLESS(go);

    mpr_net_use_bus(net);
    for (i = 0; i < net->num_devs; i++) {
        _send_device_sync(net, net->devs[i]);
    }

    // some housekeeping: periodically check if our links are still active
    mpr_list list = mpr_list_from_data(gph->links);
    while (list) {
        mpr_link lnk = (mpr_link)*list;
        list = mpr_list_get_next(list);
        if (lnk->remote_dev->loc)
            continue;
        int num_maps = lnk->num_maps[0] + lnk->num_maps[1];
        mpr_sync_clock clk = &lnk->clock;
        double elapsed = (clk->rcvd.time.sec ? mpr_time_get_diff(now, clk->rcvd.time) : 0);
        if (elapsed > TIMEOUT_SEC) {
            if (clk->rcvd.msg_id > 0) {
                if (num_maps)
                    trace_dev(lnk->local_dev, "Lost contact with linked "
                              "device '%s' (%g seconds since sync).\n",
                              lnk->remote_dev->name, elapsed);
                // tentatively mark link as expired
                clk->rcvd.msg_id = -1;
                clk->rcvd.time.sec = now.sec;
            }
            else {
                if (num_maps) {
                    trace_dev(lnk->local_dev, "Removing link to unresponsive "
                              "device '%s' (%g seconds since warning).\n",
                              lnk->remote_dev->name, elapsed);
                    /* TODO: release related maps, call local handlers
                     * and inform subscribers. */
                }
                else
                    trace_dev(lnk->local_dev, "Removing link to device '%s'.\n",
                              lnk->remote_dev->name);
                // remove related data structures
                mpr_rtr_remove_link(net->rtr, lnk);
                mpr_graph_remove_link(gph, lnk, num_maps ? MPR_OBJ_EXP : MPR_OBJ_REM);
            }
        }
        else if (num_maps && mpr_obj_get_prop_by_idx(&lnk->remote_dev->obj,
                                                     MPR_PROP_HOST, 0, 0, 0, 0, 0)) {
            /* Only send pings if this link has associated maps, ensuring empty
             * links are removed after the ping timeout. */
            lo_bundle bun = lo_bundle_new(now);
            NEW_LO_MSG(msg, ;);
            lo_message_add_int64(msg, lnk->local_dev->obj.id);
            if (++clk->sent.msg_id < 0)
                clk->sent.msg_id = 0;
            lo_message_add_int32(msg, clk->sent.msg_id);
            lo_message_add_int32(msg, clk->rcvd.msg_id);
            lo_message_add_double(msg, elapsed);
            // need to send immediately
            lo_bundle_add_message(bun, net_msg_strings[MSG_PING], msg);
#if FORCE_COMMS_TO_BUS
            lo_send_bundle_from(net->addr.bus, net->server.mesh, bun);
#else
            lo_send_bundle_from(lnk->addr.admin, net->server.mesh, bun);
#endif
            mpr_time_set(&clk->sent.time, lo_bundle_get_timestamp(bun));
            lo_bundle_free_recursive(bun);
        }
    }
}

/*! This is the main function to be called once in a while from a program so
 *  that the libmpr bus can be automatically managed. */
void mpr_net_poll(mpr_net net)
{
    // send out any cached messages
    mpr_net_send(net);

    if (!net->num_devs) {
        mpr_net_maybe_send_ping(net, 0);
        return;
    }

    /* If the ordinal is not yet locked, process collision timing.
     * Once the ordinal is locked it won't change. */
    int i, registered = 0;
    for (i = 0; i < net->num_devs; i++) {
        mpr_dev dev = net->devs[i];
        if (!dev->loc->registered) {
            /* If the ordinal has changed, re-probe the new name. */
            if (1 == check_collisions(net, &dev->loc->ordinal))
                mpr_net_probe_dev_name(net, dev);

            /* If we are ready to register the device, add the message handlers. */
            if (dev->loc->ordinal.locked) {
                mpr_dev_on_registered(dev);

                /* Send registered msg. */
                lo_send(net->addr.bus, net_msg_strings[MSG_NAME_REG], "s",
                        mpr_dev_get_name(dev));

                mpr_net_add_dev_methods(net, dev);
                mpr_net_maybe_send_ping(net, 1);
                trace_dev(dev, "registered.\n");
            }
        }
        else
            ++registered;
    }
    if (registered) {
        // Send out clock sync messages occasionally
        mpr_net_maybe_send_ping(net, 0);
    }
    return;
}

/*! Algorithm for checking collisions and allocating resources. */
static int check_collisions(mpr_net net, mpr_allocated resource)
{
    RETURN_UNLESS(!resource->locked, 0);
    double current_time = mpr_get_current_time();
    double timediff = current_time - resource->count_time;
    int i;

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
            if (!resource->hints[i])
                break;
        }
        resource->val += i + 1;

        /* Prepare for causing new resource collisions. */
        resource->collision_count = 0;
        resource->count_time = current_time;
        for (i = 0; i < 8; i++)
            resource->hints[i] = 0;

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
    mpr_net net = (mpr_net)user;
    mpr_net_maybe_send_ping(net, 1);
    trace_net("received /who\n");
    return 0;
}

/*! Register information about port and host for the device. */
static int handler_dev(const char *path, const char *types, lo_arg **av, int ac,
                       lo_message msg, void *user)
{
    RETURN_UNLESS(ac && MPR_STR == types[0], 0);
    mpr_net net = (mpr_net)user;
#ifdef DEBUG
    mpr_dev dev = net->devs ? net->devs[0] : 0;
#endif
    mpr_graph graph = net->graph;
    int i, j;
    mpr_msg props = 0;
    mpr_list links = 0, cpy;
    const char *name = &av[0]->s;

    if (graph->autosub || mpr_graph_subscribed_by_dev(graph, name)) {
        props = mpr_msg_parse_props(ac-1, &types[1], &av[1]);
        trace_net("got /device %s + %i arguments\n", name, ac-1);
        mpr_dev remote = mpr_graph_add_dev(graph, name, props);
        if (!remote->subscribed && graph->autosub)
            mpr_graph_subscribe(graph, remote, graph->autosub, -1);
    }
    if (!net->num_devs)
        goto done;
    for (i = 0; i < net->num_devs; i++) {
        if (0 == strcmp(&av[0]->s, mpr_dev_get_name(net->devs[i])))
            break;
    }
    TRACE_DEV_RETURN_UNLESS(i == net->num_devs, 0,
                            "ignoring /device message from self\n");
    trace_net("got /device %s\n", &av[0]->s);

    // Discover whether the device is linked.
    mpr_dev remote = mpr_graph_get_dev_by_name(graph, name);
    links = mpr_dev_get_links(remote, MPR_DIR_ANY);
    if (!links || !(*links)) {
        trace_net("ignoring /device '%s', no link.\n", name);
        goto done;
    }
    else {
        cpy = mpr_list_get_cpy(links);
        while (cpy) {
            mpr_link link = (mpr_link)*cpy;
            if (!link->addr.admin)
                break;
            cpy = mpr_list_get_next(cpy);
        }
        if (!cpy) {
            trace_net("ignoring /device '%s', links already set.\n", name);
            goto done;
        }
        mpr_list_free(cpy);
    }

    lo_address a = lo_message_get_source(msg);
    if (!a) {
        trace_net("can't perform /linkTo, address unknown\n");
        goto done;
    }
    // Find the sender's hostname
    const char *host = lo_address_get_hostname(a);
    const char *admin_port = lo_address_get_port(a);
    if (!host) {
        trace_net("can't perform /linkTo, host unknown\n");
        goto done;
    }
    // Retrieve the port
    if (!props)
        props = mpr_msg_parse_props(ac-1, &types[1], &av[1]);
    mpr_msg_atom atom = mpr_msg_get_prop(props, MPR_PROP_PORT);
    if (!atom || atom->len != 1 || atom->types[0] != MPR_INT32) {
        trace_net("can't perform /linkTo, port unknown\n");
        goto done;
    }
    int data_port = (atom->vals[0])->i;

    cpy = mpr_list_get_cpy(links);
    while (cpy) {
        mpr_link link = (mpr_link)*cpy;
        cpy = mpr_list_get_next(cpy);
        if (mpr_link_get_is_local(link))
            mpr_link_connect(link, host, atoi(admin_port), data_port);
    }

    // check if we have maps waiting for this link
    mpr_rtr_sig rs = net->rtr->sigs;
    while (rs) {
        for (i = 0; i < rs->num_slots; i++) {
            if (!rs->slots[i])
                continue;
            mpr_map map = rs->slots[i]->map;
            if (MPR_DIR_OUT == rs->slots[i]->dir) {
                // only send /mapTo once even if we have multiple local sources
                if (map->loc->one_src && (rs->slots[i] != map->src[0]))
                    continue;
                cpy = mpr_list_get_cpy(links);
                while (cpy) {
                    mpr_link link = (mpr_link)*cpy;
                    cpy = mpr_list_get_next(cpy);
                    if (mpr_link_get_is_local(link) && map->dst->link == link) {
                        mpr_net_use_mesh(net, link->addr.admin);
                        mpr_map_send_state(map, -1, MSG_MAP_TO);
                        for (j = 0; j < map->num_src; j++) {
                            if (!map->src[j]->sig->loc)
                                continue;
                            mpr_sig_send_state(map->src[j]->sig, MSG_SIG);
                        }
                    }
                }
            }
            else {
                cpy = mpr_list_get_cpy(links);
                while (cpy) {
                    mpr_link link = (mpr_link)*cpy;
                    cpy = mpr_list_get_next(cpy);
                    if (!mpr_link_get_is_local(link))
                        continue;
                    for (j = 0; j < map->num_src; j++) {
                        if (map->src[j]->link != link)
                            continue;
                        mpr_net_use_mesh(net, link->addr.admin);
                        j = mpr_map_send_state(map, map->loc->one_src ? -1 : j,
                                               MSG_MAP_TO);
                        mpr_sig_send_state(map->dst->sig, MSG_SIG);
                    }
                }
            }
        }
        rs = rs->next;
    }
done:
    mpr_list_free(links);
    mpr_msg_free(props);
    return 0;
}

/*! Handle remote requests to add, modify, or remove metadata to a device. */
static int handler_dev_mod(const char *path, const char *types, lo_arg **av,
                           int ac, lo_message msg, void *user)
{
    mpr_net net = (mpr_net)user;
    mpr_dev dev = net->devs ? net->devs[0] : 0;
    RETURN_UNLESS(dev && mpr_dev_get_is_ready(dev) && ac >= 2 && MPR_STR == types[0], 0);
    mpr_msg props = mpr_msg_parse_props(ac, types, av);
    trace_dev(dev, "got /%s/modify + %d properties.\n", path, props->num_atoms);
    if (mpr_dev_set_from_msg(dev, props)) {
        if (dev->loc->subscribers) {
            trace_dev(dev, "informing subscribers (DEVICE)\n")
            mpr_net_use_subscribers(net, dev, MPR_DEV);
            mpr_dev_send_state(dev, MSG_DEV);
        }
        mpr_tbl_clear_empty(dev->obj.props.synced);
    }
    mpr_msg_free(props);
    return 0;
}

/*! Respond to /logout by deleting record of device. */
static int handler_logout(const char *path, const char *types, lo_arg **av,
                          int ac, lo_message msg, void *user)
{
    RETURN_UNLESS(ac && MPR_STR == types[0], 0);
    mpr_net net = (mpr_net)user;
    mpr_dev dev = net->devs ? net->devs[0] : 0, remote;
    mpr_graph gph = net->graph;
    mpr_link lnk;
    int diff, ordinal;
    char *s, *name = &av[0]->s;
    remote = mpr_graph_get_dev_by_name(gph, name);

    if (!dev)
        {trace_net("got /logout '%s'\n", name);}
    else if (dev->loc->ordinal.locked) {
        trace_dev(dev, "got /logout '%s'\n", name);
        // Check if we have any links to this device, if so remove them
        lnk = remote ? mpr_dev_get_link_by_remote(dev, remote) : 0;
        if (lnk) {
            // TODO: release maps, call local handlers and inform subscribers
            trace_dev(dev, "removing link to expired device '%s'.\n", name);
            mpr_rtr_remove_link(net->rtr, lnk);
            mpr_graph_remove_link(gph, lnk, MPR_OBJ_REM);
        }

        // Parse the ordinal from name in the format: <name>.<n>
        s = name;
        while (*s != '.' && *s++) {}
        ordinal = atoi(++s);

        strtok(name, ".");
        ++name;
        if (0 == strcmp(name, dev->prefix)) {
            // If device name matches and ordinal is within my block, free it
            diff = ordinal - dev->loc->ordinal.val - 1;
            if (diff >= 0 && diff < 8)
                dev->loc->ordinal.hints[diff] = 0;
        }
    }
    if (remote) {
        mpr_graph_unsubscribe(gph, remote);
        mpr_graph_remove_dev(gph, remote, MPR_OBJ_REM, 0);
    }
    return 0;
}

/*! Respond to /subscribe message by adding or renewing a subscription. */
static int handler_subscribe(const char *path, const char *types, lo_arg **av,
                             int ac, lo_message msg, void *user)
{
    mpr_net net = (mpr_net)user;
    mpr_dev dev = net->devs ? net->devs[0] : 0;
    int version = -1;

#ifdef DEBUG
    trace_dev(dev, "received /subscribe ");
    lo_message_pp(msg);
#endif

    lo_address addr  = lo_message_get_source(msg);
    TRACE_DEV_RETURN_UNLESS(addr && ac, 0, "error retrieving subscription "
                            "source address.\n");

    int i, flags = 0, timeout_seconds = 0;
    for (i = 0; i < ac; i++) {
        if (types[i] != MPR_STR)
            break;
        else if (0 == strcmp(&av[i]->s, "all"))
            flags = MPR_OBJ;
        else if (0 == strcmp(&av[i]->s, "device"))
            flags |= MPR_DEV;
        else if (0 == strcmp(&av[i]->s, "signals"))
            flags |= MPR_SIG;
        else if (0 == strcmp(&av[i]->s, "inputs"))
            flags |= MPR_SIG_IN;
        else if (0 == strcmp(&av[i]->s, "outputs"))
            flags |= MPR_SIG_OUT;
        else if (0 == strcmp(&av[i]->s, "maps"))
            flags |= MPR_MAP;
        else if (0 == strcmp(&av[i]->s, "maps_in"))
            flags |= MPR_MAP_IN;
        else if (0 == strcmp(&av[i]->s, "maps_out"))
            flags |= MPR_MAP_OUT;
        else if (0 == strcmp(&av[i]->s, "@version")) {
            // next argument is last device version recorded by subscriber
            ++i;
            if (i < ac && MPR_INT32 == types[i])
                version = av[i]->i;
        }
        else if (0 == strcmp(&av[i]->s, "@lease")) {
            // next argument is lease timeout in seconds
            ++i;
            if (MPR_INT32 == types[i])
                timeout_seconds = av[i]->i;
            else if (MPR_FLT == types[i])
                timeout_seconds = (int)av[i]->f;
            else if (MPR_DBL == types[i])
                timeout_seconds = (int)av[i]->d;
            else
                {trace_dev(dev, "error parsing subscription lease prop.\n");}
        }
    }

    // add or renew subscription
    mpr_dev_manage_subscriber(dev, addr, flags, timeout_seconds, version);
    return 0;
}

/*! Register information about a signal. */
static int handler_sig(const char *path, const char *types, lo_arg **av, int ac,
                       lo_message msg, void *user)
{
    RETURN_UNLESS(ac >= 2 && MPR_STR == types[0], 1);
    mpr_net net = (mpr_net)user;
    const char *full_sig_name = &av[0]->s;
    char *signamep, *devnamep;
    int devnamelen = mpr_parse_names(full_sig_name, &devnamep, &signamep);
    RETURN_UNLESS(devnamep && signamep && devnamelen < 1024, 0);

    char devname[1024];
    strncpy(devname, devnamep, devnamelen);
    devname[devnamelen]=0;

#ifdef DEBUG
    if (net->devs) {
        trace_dev(net->devs[0], "got /signal %s:%s\n", devname, signamep);
    }
    else {
        trace_net("got /signal %s:%s\n", devname, signamep);
    }
#endif

    mpr_msg props = mpr_msg_parse_props(ac-1, &types[1], &av[1]);
    mpr_graph_add_sig(net->graph, signamep, devname, props);
    mpr_msg_free(props);
    return 0;
}

/* Helper function to check if the prefix matches.  Like strcmp(), returns 0 if
 * they match (up to the first '/'), non-0 otherwise.  Also optionally returns a
 * pointer to the remainder of str1 after the prefix. */
static int prefix_cmp(const char *str1, const char *str2, const char **rest)
{
    // skip first slash
    str1 += ('/' == str1[0]);
    str2 += ('/' == str2[0]);

    const char *s1=str1, *s2 = str2;

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
    mpr_net net = (mpr_net)user;
    mpr_dev dev = net->devs ? net->devs[0] : 0;
    RETURN_UNLESS(dev && mpr_dev_get_is_ready(dev) && ac > 1 && MPR_STR == types[0], 0);

    // retrieve signal
    mpr_sig sig = mpr_dev_get_sig_by_name(dev, &av[0]->s);
    TRACE_DEV_RETURN_UNLESS(sig, 0, "no signal found with name '%s'.\n", &av[0]->s);

    mpr_msg props = mpr_msg_parse_props(ac-1, &types[1], &av[1]);
    trace_dev(dev, "got %s '%s' + %d properties.\n", path, sig->name,
              props->num_atoms);

    if (mpr_sig_set_from_msg(sig, props)) {
        if (dev->loc->subscribers) {
            trace_dev(dev, "informing subscribers (SIGNAL)\n");
            int dir = (MPR_DIR_IN == sig->dir) ? MPR_SIG_IN : MPR_SIG_OUT;
            mpr_net_use_subscribers(net, dev, dir);
            mpr_sig_send_state(sig, MSG_SIG);
        }
        mpr_tbl_clear_empty(sig->obj.props.synced);
    }
    mpr_msg_free(props);
    return 0;
}

/*! Unregister information about a removed signal. */
static int handler_sig_removed(const char *path, const char *types, lo_arg **av,
                               int ac, lo_message msg, void *user)
{
    RETURN_UNLESS(ac && MPR_STR == types[0], 1);
    mpr_net net = (mpr_net)user;
    const char *full_sig_name = &av[0]->s;
    char *signamep, *devnamep;
    int devnamelen = mpr_parse_names(full_sig_name, &devnamep, &signamep);
    RETURN_UNLESS(devnamep && signamep && devnamelen < 1024, 0);

    char devname[1024];
    strncpy(devname, devnamep, devnamelen);
    devname[devnamelen]=0;

    trace_net("got /signal/removed %s:%s\n", devname, signamep);

    mpr_dev dev = mpr_graph_get_dev_by_name(net->graph, devname);
    if (dev && !dev->loc)
        mpr_graph_remove_sig(net->graph, mpr_dev_get_sig_by_name(dev, signamep),
                             MPR_OBJ_REM);
    return 0;
}

/*! Repond to name collisions during allocation, help suggest IDs once allocated. */
static int handler_name(const char *path, const char *types, lo_arg **av,
                        int ac, lo_message msg, void *user)
{
    RETURN_UNLESS(ac && MPR_STR == types[0], 0);
    mpr_net net = (mpr_net)user;
    mpr_dev dev = net->devs ? net->devs[0] : 0;
    int ordinal, diff, temp_id = -1, hint = 0;
    char *name = &av[0]->s;
    if (ac > 1) {
        if (MPR_INT32 == types[1])
            temp_id = av[1]->i;
        if (MPR_INT32 == types[2])
            hint = av[2]->i;
    }

#ifdef DEBUG
    if (hint)
        {trace_dev(dev, "got name %s %i %i\n", name, temp_id, hint);}
    else
        {trace_dev(dev, "got name %s\n", name);}
#endif

    if (dev->loc->ordinal.locked) {
        ordinal = extract_ordinal(name);
        RETURN_UNLESS(ordinal >= 0, 0);

        // If device name matches
        if (0 == strcmp(name, dev->prefix)) {
            // if id is locked and registered id is within my block, store it
            diff = ordinal - dev->loc->ordinal.val - 1;
            if (diff >= 0 && diff < 8)
                dev->loc->ordinal.hints[diff] = -1;
            if (hint) {
                // if suggested id is within my block, store timestamp
                diff = hint - dev->loc->ordinal.val - 1;
                if (diff >= 0 && diff < 8)
                    dev->loc->ordinal.hints[diff] = mpr_get_current_time();
            }
        }
    }
    else {
        mpr_id id = (mpr_id) crc32(0L, (const Bytef *)name, strlen(name)) << 32;
        if (id == dev->obj.id) {
            if (temp_id < net->random_id) {
                /* Count ordinal collisions. */
                ++dev->loc->ordinal.collision_count;
                dev->loc->ordinal.count_time = mpr_get_current_time();
            }
            else if (temp_id == net->random_id && hint > 0
                     && hint != dev->loc->ordinal.val) {
                dev->loc->ordinal.val = hint;
                mpr_net_probe_dev_name(net, dev);
            }
        }
    }
    return 0;
}

/*! Repond to name probes during allocation, help suggest names once allocated. */
static int handler_name_probe(const char *path, const char *types, lo_arg **av,
                              int ac, lo_message msg, void *user)
{
    mpr_net net = (mpr_net)user;
    mpr_dev dev = net->devs ? net->devs[0] : 0;
    char *name = &av[0]->s;
    int i, temp_id = av[1]->i;

    trace_dev(dev, "got name probe %s %i \n", name, temp_id);

    mpr_id id = (mpr_id) crc32(0L, (const Bytef *)name, strlen(name)) << 32;
    if (id == dev->obj.id) {
        double current_time = mpr_get_current_time();
        if (dev->loc->ordinal.locked || temp_id > net->random_id) {
            for (i = 0; i < 8; i++) {
                if (dev->loc->ordinal.hints[i] >= 0
                    && (current_time - dev->loc->ordinal.hints[i]) > 2.0) {
                    // reserve suggested ordinal
                    dev->loc->ordinal.hints[i] = current_time;
                    break;
                }
            }
            // Name may not yet be registered, so we can't use mpr_net_send().
            lo_send(net->addr.bus, net_msg_strings[MSG_NAME_REG], "sii", name,
                    temp_id, dev->loc->ordinal.val + i + 1);
        }
        else if (temp_id == net->random_id)
            dev->loc->ordinal.online = 1;
        else {
            ++dev->loc->ordinal.collision_count;
            dev->loc->ordinal.count_time = current_time;
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
 * has enough information to initialize the map; at this point each device will
 * send the message "/mapped" to its peer.  Data will be sent only after the
 * "/mapped" message has been received from the peer device.
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
    // protocol: /map src1 ... srcN -> dst OR /map dst <- src1 ... srcN
    RETURN_UNLESS(!strncmp(types, "sss", 3), 0);
    int i, num_src = 0;
    if (0 == strcmp(&av[1]->s, "<-")) {
        *src_idx = 2;
        *dst_idx = 0;
        if (ac > 2) {
            i = 2;
            while (i < ac && (MPR_STR == types[i])) {
                if ('@' == (&av[i]->s)[0])
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
        while (i < ac && (MPR_STR == types[i])) {
            if ('@' == (&av[i]->s)[0])
                break;
            else if ((0 == strcmp(&av[i]->s, "->"))
                     && ac > (i+1)
                     && (MPR_STR == types[i+1])
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
        TRACE_RETURN_UNLESS(strchr((&av[*src_idx+i]->s)+1, '/'), 0, "malformed "
                            "source signal name '%s'.\n", &av[*src_idx+i]->s);
        TRACE_RETURN_UNLESS(strcmp(&av[*src_idx+i]->s, &av[*dst_idx]->s), 0,
                            "prevented attempt to connect signal '%s' to "
                            "itself.\n", &av[*dst_idx]->s);
    }
    TRACE_RETURN_UNLESS(strchr((&av[*dst_idx]->s)+1, '/'), 0, "malformed "
                        "destination signal name '%s'.\n", &av[*dst_idx]->s)
    return num_src;
}

#define MPR_MAP_ERROR (mpr_map)-1

static mpr_map find_map(mpr_net net, const char *types, int ac, lo_arg **av,
                        mpr_loc loc, mpr_sig *sig_ptr, int add)
{
    int i;

    // first check for an 'id' property
    for (i = 3; i < ac; i++) {
        if (types[i] != MPR_STR)
            continue;
        if (0 == strcmp(&av[i]->s, "@id"))
            break;
    }
    if (i < ac && MPR_INT64 == types[++i]) {
        mpr_obj obj = mpr_graph_get_obj(net->graph, MPR_MAP, av[i]->i64);
        trace_graph("%s map with id %"PR_MPR_ID"\n",
                    obj ? "found" : "couldn't find", av[i]->i64);
        if (obj) {
            int is_local = mpr_obj_get_prop_as_i32(obj, MPR_PROP_IS_LOCAL, NULL);
            return loc && !is_local ? MPR_MAP_ERROR : (mpr_map)obj;
        }
    }

    // try signal names instead
    mpr_dev dev = net->devs ? net->devs[0] : 0;
    RETURN_UNLESS(dev || !loc, MPR_MAP_ERROR);
    mpr_sig sig = 0;
    int num_src, src_idx, dst_idx, prop_idx;
    const char *sig_name;
    num_src = parse_sig_names(types, av, ac, &src_idx, &dst_idx, &prop_idx);
    RETURN_UNLESS(num_src, MPR_MAP_ERROR);
    RETURN_UNLESS(is_alphabetical(num_src, &av[src_idx]), MPR_MAP_ERROR);

#ifdef DEBUG
    trace_graph("looking for map with signals: ");
    if (src_idx)
        printf(" %s <-", &av[dst_idx]->s);
    for (i = 0; i < num_src; i++)
        printf(" %s", &av[src_idx+i]->s);
    if (!src_idx)
        printf(" -> %s", &av[dst_idx]->s);
    printf("\n");
#endif

    const char *src_names[num_src];
    for (i = 0; i < num_src; i++)
        src_names[i] = &av[src_idx+i]->s;
    const char *dst_name = &av[dst_idx]->s;

    if (MPR_LOC_DST & loc) {
        // check if we are the destination
        if (   prefix_cmp(&av[dst_idx]->s, mpr_dev_get_name(dev), &sig_name)
            || !(sig = mpr_dev_get_sig_by_name(dev, sig_name)))
            RETURN_UNLESS(MPR_LOC_DST != loc, MPR_MAP_ERROR);
    }
    if (!sig && MPR_LOC_SRC & loc) {
        // check if we are a source – all sources must match!
        for (i = 0; i < num_src; i++) {
            if (prefix_cmp(src_names[i], mpr_dev_get_name(dev), &sig_name)
                || !(sig = mpr_dev_get_sig_by_name(dev, sig_name)))
                RETURN_UNLESS(MPR_LOC_SRC != loc, MPR_MAP_ERROR);
        }
    }
    mpr_map map = mpr_graph_get_map_by_names(net->graph, num_src, src_names,
                                             dst_name);
    if (!map && add) {
        // safety check: make sure we don't have an outgoing map to src (loop)
        if (sig && mpr_rtr_loop_check(net->rtr, sig, num_src, src_names)) {
            trace_dev(dev, "error in /map: potential loop detected.")
            return MPR_MAP_ERROR;
        }
        map = mpr_graph_add_map(net->graph, num_src, src_names, &av[dst_idx]->s, 0);
    }
    if (sig_ptr)
        *sig_ptr = sig;
    return map;
}


/*! When the /map message is received by the destination device, send a /mapTo
 *  message to the source device. */
static int handler_map(const char *path, const char *types, lo_arg **av, int ac,
                       lo_message msg, void *user)
{
    mpr_net net = (mpr_net)user;
    mpr_dev dev = net->devs ? net->devs[0] : 0;
#ifdef DEBUG
    trace_dev(dev, "received /map ");
    lo_message_pp(msg);
#endif
    mpr_sig sig = 0;
    int i;

    mpr_map map = find_map(net, types, ac, av, MPR_LOC_DST, &sig, 1);
    RETURN_UNLESS(map && map != MPR_MAP_ERROR, 0);
    if (map->status >= MPR_STATUS_ACTIVE) {
        /* Forward to handler_map_mod() and stop. */
        handler_map_mod(path, types, av, ac, msg, user);
        return 0;
    }
    mpr_rtr_add_map(net->rtr, map);

    mpr_msg props = mpr_msg_parse_props(ac, types, av);
    mpr_map_set_from_msg(map, props, 1);
    mpr_msg_free(props);

    if (map->loc->is_local_only) {
        trace_dev(dev, "map references only local signals... activating.\n");
        map->status = MPR_STATUS_ACTIVE;

        // Inform subscribers
        if (dev->loc->subscribers) {
            trace_dev(dev, "informing subscribers (DEVICE)\n")
            mpr_net_use_subscribers(net, dev, MPR_DEV);
            mpr_dev_send_state(dev, MSG_DEV);

            trace_dev(dev, "informing subscribers (SIGNAL)\n")
            mpr_net_use_subscribers(net, dev, MPR_SIG);
            for (i = 0; i < map->num_src; i++)
                mpr_sig_send_state(map->src[i]->sig, MSG_SIG);
            mpr_sig_send_state(map->dst->sig, MSG_SIG);

            trace_dev(dev, "informing subscribers (MAPPED)\n")
            mpr_net_use_subscribers(net, dev, MPR_MAP);
            mpr_map_send_state(map, -1, MSG_MAPPED);
        }
        return 0;
    }

    if (map->loc->one_src && !map->src[0]->loc->rsig
        && map->src[0]->link && map->src[0]->link->addr.admin) {
        mpr_net_use_mesh(net, map->src[0]->link->addr.admin);
        mpr_map_send_state(map, -1, MSG_MAP_TO);
        mpr_sig_send_state(sig, MSG_SIG);
    }
    else {
        for (i = 0; i < map->num_src; i++) {
            // do not send if is local mapping
            if (map->src[i]->loc->rsig)
                continue;
            // do not send if device host/port not yet known
            if (!map->src[i]->link || !map->src[i]->link->addr.admin)
                continue;
            mpr_net_use_mesh(net, map->src[i]->link->addr.admin);
            i = mpr_map_send_state(map, i, MSG_MAP_TO);
            mpr_sig_send_state(sig, MSG_SIG);
        }
    }
    return 0;
}

/*! When the /mapTo message is received by a peer device, create a tentative
 *  map and respond with own signal metadata. */
static int handler_map_to(const char *path, const char *types, lo_arg **av,
                          int ac, lo_message msg, void *user)
{
    mpr_net net = (mpr_net)user;
#ifdef DEBUG
    trace_dev(net->devs[0], "received /map_to ");
    lo_message_pp(msg);
#endif

    mpr_map map = find_map(net, types, ac, av, MPR_LOC_ANY, 0, 1);
    RETURN_UNLESS(map && MPR_MAP_ERROR != map, 0);
    mpr_rtr_add_map(net->rtr, map);

    if (map->status < MPR_STATUS_ACTIVE) {
        /* Set map properties. */
        mpr_msg props = mpr_msg_parse_props(ac, types, av);
        mpr_map_set_from_msg(map, props, 1);
        mpr_msg_free(props);
    }

    if (map->status >= MPR_STATUS_READY) {
        int i;
        if (MPR_DIR_OUT == map->dst->dir) {
            mpr_net_use_mesh(net, map->dst->link->addr.admin);
            mpr_map_send_state(map, -1, MSG_MAPPED);
            for (i = 0; i < map->num_src; i++) {
                if (!map->src[i]->sig->loc)
                    continue;
                mpr_sig_send_state(map->src[i]->sig, MSG_SIG);
            }
        }
        else {
            for (i = 0; i < map->num_src; i++) {
                mpr_net_use_mesh(net, map->src[i]->link->addr.admin);
                i = mpr_map_send_state(map, map->loc->one_src ? -1 : i, MSG_MAPPED);
                mpr_sig_send_state(map->dst->sig, MSG_SIG);
            }
        }
    }
    return 0;
}

/*! Respond to /mapped by storing mapping in graph. Also used by devices to
 *  confirm connection to remote peers, and to share property changes. */
static int handler_mapped(const char *path, const char *types, lo_arg **av,
                          int ac, lo_message msg, void *user)
{
    mpr_net net = (mpr_net)user;
    mpr_graph graph = net->graph;
    mpr_dev dev = net->devs ? net->devs[0] : 0;
    int i;

#ifdef DEBUG
    if (dev)
        { trace_dev(dev, "received /mapped "); }
    else
        { trace_graph("received /mapped "); }
    lo_message_pp(msg);
#endif

    mpr_map map = find_map(net, types, ac, av, 0, 0, 0);
    RETURN_UNLESS(MPR_MAP_ERROR != map, 0);
    if (!map) {
        int store = 0, i = 0;
        if (graph->autosub & MPR_MAP)
            store = 1;
        else {
            while (MPR_STR == types[i] && '@' != (&av[i]->s)[0]) {
                if ('-' != (&av[i]->s)[0] && mpr_graph_subscribed_by_sig(graph, &av[i]->s)) {
                    store = 1;
                    break;
                }
                ++i;
            }
        }
        if (store)
            map = find_map(net, types, ac, av, 0, 0, 1);
        RETURN_UNLESS(map && MPR_MAP_ERROR != map, 0);
    }
    else if (map->loc && map->loc->is_local_only) {
        // no need to update since all properties are local
        return 0;
    }
    mpr_msg props = mpr_msg_parse_props(ac, types, av);

    // TODO: if this endpoint is map admin, do not allow overwiting props
    int rc = 0, updated = mpr_map_set_from_msg(map, props, 0);
    trace_dev(dev, "updated %d map properties.\n", updated);
    if (map->status < MPR_STATUS_READY) {
        mpr_msg_free(props);
        return 0;
    }
    if (MPR_STATUS_READY == map->status) {
        map->status = MPR_STATUS_ACTIVE;
        rc = 1;

        if (MPR_DIR_OUT == map->dst->dir) {
            // Inform remote destination
            mpr_net_use_mesh(net, map->dst->link->addr.admin);
            mpr_map_send_state(map, -1, MSG_MAPPED);
        }
        else {
            // Inform remote sources
            for (i = 0; i < map->num_src; i++) {
                mpr_net_use_mesh(net, map->src[i]->link->addr.admin);
                i = mpr_map_send_state(map, map->loc->one_src ? -1 : i, MSG_MAPPED);
            }
        }

        if (dev->loc->subscribers) {
            trace_dev(dev, "informing subscribers (DEVICE)\n");
            mpr_net_use_subscribers(net, dev, MPR_DEV);
            mpr_dev_send_state(dev, MSG_DEV);

            trace_dev(dev, "informing subscribers (SIGNAL)\n");
            mpr_net_use_subscribers(net, dev, MPR_SIG);
            if (MPR_DIR_OUT == map->dst->dir) {
                for (i = 0; i < map->num_src; i++) {
                    if (map->src[i]->sig->loc)
                        mpr_sig_send_state(map->src[i]->sig, MSG_SIG);
                }
            }
            else {
                mpr_sig_send_state(map->dst->sig, MSG_SIG);
            }
        }
    }
    if (rc || updated) {
        if (mpr_obj_get_prop_as_i32(&map->obj, MPR_PROP_IS_LOCAL, 0)
            && dev && dev->loc && dev->loc->subscribers) {
            trace_dev(dev, "informing subscribers (MAPPED)\n")
            int dir = (MPR_DIR_OUT == map->dst->dir) ? MPR_MAP_OUT : MPR_MAP_IN;
            mpr_net_use_subscribers(net, dev, dir);
            mpr_map_send_state(map, -1, MSG_MAPPED);
        }
        fptr_list cb = graph->callbacks, temp;
        while (cb) {
            temp = cb->next;
            if (cb->types & MPR_MAP) {
                mpr_graph_handler *h = cb->f;
                h(graph, (mpr_obj)map, rc ? MPR_OBJ_NEW : MPR_OBJ_MOD, cb->ctx);
            }
            cb = temp;
        }
    }
    mpr_msg_free(props);
    mpr_tbl_clear_empty(map->obj.props.synced);
    return 0;
}

/*! Modify the map properties : mode, range, expression, etc. */
static int handler_map_mod(const char *path, const char *types, lo_arg **av,
                           int ac, lo_message msg, void *user)
{
    RETURN_UNLESS(ac >= 4, 0);
    mpr_net net = (mpr_net)user;
    mpr_dev dev = net->devs ? net->devs[0] : 0;
    mpr_map map = find_map(net, types, ac, av, MPR_LOC_ANY, 0, 0);
    RETURN_UNLESS(map && MPR_MAP_ERROR != map, 0);
    RETURN_UNLESS(map->loc && map->status >= MPR_STATUS_ACTIVE, 0);

    mpr_msg props = mpr_msg_parse_props(ac, types, av);
    TRACE_DEV_RETURN_UNLESS(props, 0, "ignoring /map/modify, no properties.\n");
#ifdef DEBUG
    trace_dev(dev, "received /map/modify ");
    lo_message_pp(msg);
#endif
    mpr_msg_atom a = mpr_msg_get_prop(props, MPR_PROP_PROCESS_LOC);
    mpr_loc loc = MPR_LOC_UNDEFINED;
    if (a) {
        loc = mpr_loc_from_str(&(a->vals[0])->s);
        if (!map->loc->one_src) {
            /* if map has sources from different remote devices, processing must
             * occur at the destination. */
            loc = MPR_LOC_DST;
        }
    }
    if ((a = mpr_msg_get_prop(props, MPR_PROP_EXPR))) {
        if (strstr(&a->vals[0]->s, "y{-"))
            loc = MPR_LOC_DST;
    }
    else if (map->expr_str && strstr(map->expr_str, "y{-"))
        loc = MPR_LOC_DST;

    // do not continue if we are not in charge of processing
    if (MPR_LOC_DST == loc) {
        if (!map->dst->sig->loc) {
            trace_dev(dev, "ignoring /map/modify, slaved to remote source.\n");
            goto done;
        }
    }
    else if (!map->src[0]->sig->loc) {
        trace_dev(dev, "ignoring /map/modify, slaved to remote destination.\n");
        goto done;
    }

    int updated = mpr_map_set_from_msg(map, props, 1);
    if (updated) {
        if (!map->loc->is_local_only) {
            // Inform remote peer(s) of relevant changes
            if (!map->dst->loc->rsig) {
                mpr_net_use_mesh(net, map->dst->link->addr.admin);
                mpr_map_send_state(map, -1, MSG_MAPPED);
            }
            else {
                for (int i = 0; i < map->num_src; i++) {
                    if (map->src[i]->loc->rsig)
                        continue;
                    mpr_net_use_mesh(net, map->src[i]->link->addr.admin);
                    i = mpr_map_send_state(map, i, MSG_MAPPED);
                }
            }
        }
        if (dev->loc->subscribers) {
            trace_dev(dev, "informing subscribers (MAPPED)\n")
            int dir = map->dst->loc->rsig ? MPR_MAP_IN : MPR_MAP_OUT;
            mpr_net_use_subscribers(net, dev, dir);
            mpr_map_send_state(map, -1, MSG_MAPPED);
        }
    }
    trace_dev(dev, "updated %d map properties.\n", updated);

done:
    mpr_msg_free(props);
    mpr_tbl_clear_empty(map->obj.props.synced);
    return 0;
}

/*! Unmap a set of signals. */
static int handler_unmap(const char *path, const char *types, lo_arg **av,
                         int ac, lo_message msg, void *user)
{
    mpr_net net = (mpr_net)user;
    mpr_dev dev = net->devs ? net->devs[0] : 0;
    mpr_map map = find_map(net, types, ac, av, MPR_LOC_ANY, 0, 0);
    int i;

#ifdef DEBUG
    trace_dev(dev, "%s /unmap\n", map && MPR_MAP_ERROR != map ? "got" : "ignoring");
#endif
    RETURN_UNLESS(map, 0);

    // inform remote peer(s)
    if (!map->dst->loc->rsig) {
        mpr_net_use_mesh(net, map->dst->link->addr.admin);
        mpr_map_send_state(map, -1, MSG_UNMAP);
    }
    else {
        for (i = 0; i < map->num_src; i++) {
            if (map->src[i]->loc->rsig)
                continue;
            mpr_net_use_mesh(net, map->src[i]->link->addr.admin);
            i = mpr_map_send_state(map, i, MSG_UNMAP);
        }
    }

    mpr_rtr_remove_map(net->rtr, map);

    if (dev->loc->subscribers) {
        trace_dev(dev, "informing subscribers (DEVICE)\n")
        mpr_net_use_subscribers(net, dev, MPR_DEV);
        mpr_dev_send_state(dev, MSG_DEV);

        trace_dev(dev, "informing subscribers (SIGNAL)\n")
        mpr_net_use_subscribers(net, dev, MPR_SIG);
        if (MPR_DIR_OUT == map->dst->dir) {
            for (i = 0; i < map->num_src; i++) {
                if (map->src[i]->sig->loc)
                    mpr_sig_send_state(map->src[i]->sig, MSG_SIG);
            }
        }
        else
            mpr_sig_send_state(map->dst->sig, MSG_SIG);

        trace_dev(dev, "informing subscribers (UNMAPPED)\n")
        int dir = map->dst->loc->rsig ? MPR_MAP_IN : MPR_MAP_OUT;
        mpr_net_use_subscribers(net, dev, dir);
        mpr_map_send_state(map, -1, MSG_UNMAPPED);
    }

    /* The mapping is removed. */
    mpr_graph_remove_map(net->graph, map, MPR_OBJ_REM);
    // TODO: remove empty rtr_sigs
    return 0;
}

/*! Respond to /unmapped by removing map from graph. */
static int handler_unmapped(const char *path, const char *types, lo_arg **av,
                            int ac, lo_message msg, void *user)
{
    mpr_net net = (mpr_net)user;
    mpr_map map = find_map(net, types, ac, av, 0, 0, 0);
#ifdef DEBUG
    trace_net("%s /unmapped\n", map ? "got" : "ignoring");
#endif
    if (map)
        mpr_graph_remove_map(net->graph, map, MPR_OBJ_REM);
    return 0;
}

static int handler_ping(const char *path, const char *types, lo_arg **av,
                        int ac, lo_message msg, void *user)
{
    mpr_net net = (mpr_net)user;
    mpr_dev dev = net->devs[0], remote;
    RETURN_UNLESS(dev, 0);
    mpr_link lnk;
    mpr_time now;
    mpr_time_set(&now, MPR_NOW);
    lo_timetag then = lo_message_get_timestamp(msg);

    remote = (mpr_dev)mpr_graph_get_obj(net->graph, MPR_DEV, av[0]->h);
    lnk = remote ? mpr_dev_get_link_by_remote(dev, remote) : 0;
    if (lnk) {
        mpr_sync_clock clk = &lnk->clock;
        trace_dev(dev, "ping received from linked device '%s'\n",
                  lnk->remote_dev->name);
        if (av[2]->i == clk->sent.msg_id) {
            // total elapsed time since ping sent
            double elapsed = mpr_time_get_diff(now, clk->sent.time);
            // assume symmetrical latency
            double latency = (elapsed - av[3]->d) * 0.5;
            // difference between remote and local clocks (latency compensated)
            double offset = mpr_time_get_diff(now, then) - latency;

            if (latency < 0) {
                trace_dev(dev, "error: latency %f cannot be < 0.\n", latency);
                latency = 0;
            }

            if (1 == clk->new) {
                clk->offset = offset;
                clk->latency = latency;
                clk->jitter = 0;
                clk->new = 0;
            }
            else {
                clk->jitter = (clk->jitter * 0.9 + fabs(clk->latency - latency) * 0.1);
                if (offset > clk->offset) {
                    // remote time is in the future
                    clk->offset = offset;
                }
                else if (   latency < clk->latency + clk->jitter
                         && latency > clk->latency - clk->jitter) {
                    clk->offset = clk->offset * 0.9 + offset * 0.1;
                    clk->latency = clk->latency * 0.9 + latency * 0.1;
                }
            }
        }

        // update sync status
        mpr_time_set(&clk->rcvd.time, now);
        clk->rcvd.msg_id = av[1]->i;
    }
    return 0;
}

static int handler_sync(const char *path, const char *types, lo_arg **av,
                        int ac, lo_message msg, void *user)
{
    mpr_net net = (mpr_net)user;
    RETURN_UNLESS(net && ac && MPR_STR == types[0], 0);
    mpr_graph graph = net->graph;
    mpr_dev dev = mpr_graph_get_dev_by_name(graph, &av[0]->s);
    if (dev) {
        RETURN_UNLESS(!dev->loc, 0);
        trace_graph("updating sync record for device '%s'\n", dev->name);
        mpr_time_set(&dev->synced, MPR_NOW);

        if (!dev->subscribed && graph->autosub) {
            trace_graph("autosubscribing to device '%s'.\n", &av[0]->s);
            mpr_graph_subscribe(graph, dev, graph->autosub, -1);
        }
    }
    else if (graph->autosub) {
        // only create device record after requesting more information
        trace_net("requesting metadata for device '%s'.\n", &av[0]->s);
        mpr_dev_t temp;
        temp.name = &av[0]->s;
        temp.obj.version = -1;
        temp.loc = 0;
        mpr_graph_subscribe(graph, &temp, MPR_DEV, 0);
    }
    else
        trace_graph("ignoring sync from '%s' (autosubscribe = %d)\n", &av[0]->s,
                    graph->autosub);
    return 0;
}
