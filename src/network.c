#include "config.h"

#include <lo/lo.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <math.h>
#include <ctype.h>

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
  #define HAVE_LIBIPHLPAPI
 #endif
#endif

#include "link.h"
#include "list.h"
#include "map.h"
#include "message.h"
#include "mpr_signal.h"
#include "network.h"
#include "object.h"
#include "path.h"
#include "property.h"
#include "slot.h"
#include "util/mpr_debug.h"

#include "config.h"
#include <mapper/mapper.h>

extern const char* prop_msg_strings[MPR_PROP_EXTRA+1];

#define SERVER_BUS      0   /* Multicast comms. */
#define SERVER_MESH     1   /* Mesh comms. */

#define BUNDLE_DST_SUBSCRIBERS (void*)-1
#define BUNDLE_DST_BUS          0

#define MAX_BUNDLE_LEN 8192
#define FIND 0
#define UPDATE 1
#define ADD 2

/*! A structure that keeps information about network communications. */
typedef struct _mpr_net {
    mpr_graph graph;

    lo_server servers[2];

    struct {
        lo_address bus;             /*!< LibLo address for the multicast bus. */
        lo_address dst;
        struct _mpr_local_dev *dev;
        char *url;
    } addr;

    struct {
        char *name;                 /*!< The name of the network interface. */
        struct in_addr addr;        /*!< The IP address of network interface. */
    } iface;

    struct _mpr_local_dev **devs;   /*!< Local devices managed by this network structure. */
    lo_bundle bundle;               /*!< Bundle pointer for sending messages on the multicast bus. */
    mpr_time bundle_time;

    struct {
        char *group;
        int port;
    } multicast;

    int random_id;                  /*!< Random id for allocation speedup. */
    int msg_type;
    int num_devs;
    uint32_t next_bus_ping;
    uint32_t next_sub_ping;
    uint8_t generic_dev_methods_added;
    uint8_t registered;
} mpr_net_t;

static int is_alphabetical(int num, lo_arg **names)
{
    int i;
    RETURN_ARG_UNLESS(num > 1, 1);
    for (i = 1; i < num; i++)
        TRACE_RETURN_UNLESS(strcmp(&names[i-1]->s, &names[i]->s)<0, 0,
                            "error: signal names out of order.");
    return 1;
}

MPR_INLINE static void inform_device_subscribers(mpr_net net, mpr_local_dev dev)
{
    if (mpr_local_dev_has_subscribers(dev)) {
        trace_dev(dev, "informing subscribers (DEVICE)\n")
        mpr_net_use_subscribers(net, dev, MPR_DEV);
        mpr_dev_send_state((mpr_dev)dev, MPR_DEV);
    }
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

/* handlers needed by all devices */
static struct handler_method_assoc dev_handlers_generic[] = {
    {MSG_MAP,                   NULL,       handler_map},
    {MSG_MAP_TO,                NULL,       handler_map_to},
    {MSG_MAPPED,                NULL,       handler_mapped},
    {MSG_MAP_MOD,               NULL,       handler_map_mod},
    {MSG_PING,                  "hiid",     handler_ping},
    {MSG_UNMAP,                 NULL,       handler_unmap},
};
const int NUM_DEV_HANDLERS_GENERIC =
    sizeof(dev_handlers_generic)/sizeof(dev_handlers_generic[0]);

/* handlers needed by specific devices */
static struct handler_method_assoc dev_handlers_specific[] = {
    {MSG_DEV_MOD,               NULL,       handler_dev_mod},
    {MSG_SIG_MOD,               NULL,       handler_sig_mod},
    {MSG_SUBSCRIBE,             NULL,       handler_subscribe},
};
const int NUM_DEV_HANDLERS_SPECIFIC =
    sizeof(dev_handlers_specific)/sizeof(dev_handlers_specific[0]);

/* handlers needed by graph for archiving */
static struct handler_method_assoc graph_handlers[] = {
    {MSG_DEV,                   NULL,       handler_dev},
    {MSG_LOGOUT,                NULL,       handler_logout},
    {MSG_MAPPED,                NULL,       handler_mapped},
    {MSG_SIG,                   NULL,       handler_sig},
    {MSG_SIG_REM,               "s",        handler_sig_removed},
    {MSG_SYNC,                  NULL,       handler_sync},
    {MSG_UNMAPPED,              NULL,       handler_unmapped},
    {MSG_WHO,                   NULL,       handler_who},
};
const int NUM_GRAPH_HANDLERS =
    sizeof(graph_handlers)/sizeof(graph_handlers[0]);

/* Internal LibLo error handler */
static void handler_error(int num, const char *msg, const char *where)
{
    trace("[libmapper] liblo server error %d in path %s: %s\n", num, where, msg);
}

int mpr_net_bundle_start(lo_timetag t, void *data)
{
    mpr_time_set(&((mpr_net)data)->bundle_time, t);
    return 0;
}

mpr_time mpr_net_get_bundle_time(mpr_net net)
{
    return net->bundle_time;
}

/*! Local function to get the IP address of a network interface. */
static int get_iface_addr(const char* pref, struct in_addr* addr, char **iface)
{
    struct sockaddr_in *sa;

#ifdef HAVE_GETIFADDRS

    struct in_addr zero;
    struct ifaddrs *ifaphead;
    struct ifaddrs *ifap;
    struct ifaddrs *iflo = 0, *ifchosen = 0;
    RETURN_ARG_UNLESS(0 == getifaddrs(&ifaphead), 1);
    *(unsigned int *)&zero = inet_addr("0.0.0.0");
    ifap = ifaphead;
    while (ifap) {
        sa = (struct sockaddr_in *) ifap->ifa_addr;
        if (!sa) {
            ifap = ifap->ifa_next;
            continue;
        }

        /* Note, we could also check for IFF_MULTICAST-- however this is the
         * data-sending port, not the libmapper bus port. */

        if (AF_INET == sa->sin_family && ifap->ifa_flags & IFF_UP
            && memcmp(&sa->sin_addr, &zero, sizeof(struct in_addr)) != 0) {
            trace("checking network interface '%s' (pref: '%s')\n",
                  ifap->ifa_name, pref ? pref : "NULL");
            ifchosen = ifap;
            if (pref && 0 == strcmp(ifap->ifa_name, pref)) {
                trace("  preferred interface found!\n");
                break;
            }
            else if (ifap->ifa_flags & IFF_LOOPBACK)
                iflo = ifap;
        }
        ifap = ifap->ifa_next;
    }

    /* Default to loopback address in case user is working locally. */
    if (!ifchosen) {
        trace("defaulting to local loopback interface\n");
        ifchosen = iflo;
    }

    if (ifchosen) {
        if (*iface && !strcmp(*iface, ifchosen->ifa_name)) {
            freeifaddrs(ifaphead);
            return 1;
        }
        FUNC_IF(free, *iface);
        *iface = strdup(ifchosen->ifa_name);
        sa = (struct sockaddr_in *) ifchosen->ifa_addr;
        *addr = sa->sin_addr;
        freeifaddrs(ifaphead);
        return 0;
    }

    freeifaddrs(ifaphead);

#else /* !HAVE_GETIFADDRS */

#ifdef HAVE_LIBIPHLPAPI
    char namebuf[BUFSIZ];
    /* Start with recommended 15k buffer for GetAdaptersAddresses. */
    ULONG size = 15 * 1024 / 2;
    int tries = 3;
    PIP_ADAPTER_ADDRESSES paa = malloc(size * 2);
    DWORD rc = ERROR_SUCCESS - 1;
    while (rc != ERROR_SUCCESS && paa && tries-- > 0) {
        size *= 2;
        paa = realloc(paa, size);
        if (paa)
            rc = GetAdaptersAddresses(AF_INET, 0, 0, paa, &size);
    }
    RETURN_ARG_UNLESS(paa && ERROR_SUCCESS == rc, 2);

    PIP_ADAPTER_ADDRESSES aa_lo = 0, aa_chosen = 0, aa = paa;
    PIP_ADAPTER_UNICAST_ADDRESS ua_chosen = 0;
    while (aa && ERROR_SUCCESS == rc) {
        PIP_ADAPTER_UNICAST_ADDRESS pua = aa->FirstUnicastAddress;
        /* Skip adapters that are not "Up". */
        if (pua && IfOperStatusUp == aa->OperStatus) {
            trace("checking network interface '%wS' (pref: '%s')\n",
                  aa->FriendlyName, pref ? pref : "NULL");
            if (IF_TYPE_SOFTWARE_LOOPBACK == aa->IfType) {
                aa_lo = aa;
            }
            else {
                /* Skip addresses starting with 0.X.X.X or 169.X.X.X. */
                sa = (struct sockaddr_in *) pua->Address.lpSockaddr;
                unsigned char prefix = sa->sin_addr.s_addr & 0xFF;
                if (prefix != 0xA9 && prefix != 0) {
                    aa_chosen = aa;
                }
            }
            if (pref) {
                memset(namebuf, 0, BUFSIZ);
                WideCharToMultiByte(CP_ACP, 0, aa->FriendlyName, wcslen(aa->FriendlyName),
                                    namebuf, BUFSIZ, NULL, NULL);
                if (0 == strcmp(pref, namebuf)) {
                    trace("  preferred interface found!\n");
                    aa_chosen = aa;
                    break;
                }
            }
        }
        aa = aa->Next;
    }

    /* Default to loopback address in case user is working locally. */
    if (!aa_chosen) {
        trace("defaulting to local loopback interface\n");
        aa_chosen = aa_lo;
    }

    if (aa_chosen) {
        PIP_ADAPTER_UNICAST_ADDRESS pua = aa_chosen->FirstUnicastAddress;
        memset(namebuf, 0, BUFSIZ);
        WideCharToMultiByte(CP_ACP, 0, aa_chosen->FriendlyName, wcslen(aa_chosen->FriendlyName),
                            namebuf, BUFSIZ, NULL, NULL);
        if (!pua || (*iface && !strcmp(*iface, namebuf))) {
            free(paa);
            return 1;
        }
        FUNC_IF(free, *iface);
        *iface = strdup(namebuf);
        sa = (struct sockaddr_in *) pua->Address.lpSockaddr;
        *addr = sa->sin_addr;
        free(paa);
        return 0;
    }

    FUNC_IF(free, paa);

#else
  #error No known method on this system to get the network interface address.
#endif /* HAVE_LIBIPHLPAPI */
#endif /* !HAVE_GETIFADDRS */

    return 2;
}

/*! A helper function to seed the random number generator. */
static void seed_srand()
{
    unsigned int s;
    double d;

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

    d = mpr_get_current_time();
    s = (unsigned int)((d-(unsigned long)d)*100000);
    srand(s);
}

void mpr_net_add_dev_methods(mpr_net net, mpr_local_dev dev)
{
    int i;
    char path[256];
    const char *dname = mpr_dev_get_name((mpr_dev)dev);
    mpr_graph graph = mpr_obj_get_graph((mpr_obj)dev);
    for (i = 0; i < NUM_DEV_HANDLERS_SPECIFIC; i++) {
        snprintf(path, 256, net_msg_strings[dev_handlers_specific[i].str_idx], dname);
        lo_server_add_method(net->servers[SERVER_BUS], path, dev_handlers_specific[i].types,
                             dev_handlers_specific[i].h, dev);
        lo_server_add_method(net->servers[SERVER_MESH], path, dev_handlers_specific[i].types,
                             dev_handlers_specific[i].h, dev);
    }
    if (net->generic_dev_methods_added)
        return;
    for (i = 0; i < NUM_DEV_HANDLERS_GENERIC; i++) {
        lo_server_add_method(net->servers[SERVER_BUS],
                             net_msg_strings[dev_handlers_generic[i].str_idx],
                             dev_handlers_generic[i].types, dev_handlers_generic[i].h, graph);
        lo_server_add_method(net->servers[SERVER_MESH],
                             net_msg_strings[dev_handlers_generic[i].str_idx],
                             dev_handlers_generic[i].types,dev_handlers_generic[i].h, graph);
        net->generic_dev_methods_added = 1;
    }
}

/*! Add an uninitialized device to this network. */
void mpr_net_add_dev(mpr_net net, mpr_local_dev dev)
{
    int i, found = 0;
    RETURN_UNLESS(dev);

    /* Check if device was already added. */
    for (i = 0; i < net->num_devs; i++) {
        if (net->devs[i] == dev) {
            found = 1;
            break;
        }
    }
    if (found) {
        /* reset registered flag */
        mpr_local_dev_restart_registration(dev, i);
    }
    else {
        /* Initialize data structures */
        net->devs = realloc(net->devs, (net->num_devs + 1) * sizeof(mpr_local_dev));
        net->devs[net->num_devs] = dev;
        ++net->num_devs;
        mpr_local_dev_restart_registration(dev, net->num_devs);
        net->registered = 0;
    }

    if (1 == net->num_devs) {
        /* Seed the random number generator. */
        seed_srand();

        /* Choose a random ID for allocation speedup */
        net->random_id = rand();

        /* Add allocation methods for bus communications. Further methods are added
         * when the device is registered. */
        lo_server_add_method(net->servers[SERVER_BUS], net_msg_strings[MSG_NAME_PROBE], "si",
                             handler_name_probe, net);
        lo_server_add_method(net->servers[SERVER_BUS], net_msg_strings[MSG_NAME_REG], NULL,
                             handler_name, net);
    }

    /* Probe potential name. */
    mpr_local_dev_probe_name(dev, net);
}

void mpr_net_remove_dev(mpr_net net, mpr_local_dev dev)
{
    int i, j;
    char path[256];

    for (i = 0; i < net->num_devs; i++) {
        if (dev == net->devs[i])
            break;
    }
    if (i == net->num_devs) {
        trace("error in mpr_net_remove_dev: device not found in local list\n");
        return;
    }
    --net->num_devs;
    for (; i < net->num_devs; i++)
        net->devs[i] = net->devs[i + 1];
    net->devs = realloc(net->devs, net->num_devs * sizeof(mpr_local_dev));

    for (i = 0; i < NUM_DEV_HANDLERS_SPECIFIC; i++) {
        snprintf(path, 256, net_msg_strings[dev_handlers_specific[i].str_idx],
                 mpr_dev_get_name((mpr_dev)dev));
        lo_server_del_method(net->servers[SERVER_BUS], path, dev_handlers_specific[i].types);
        lo_server_del_method(net->servers[SERVER_MESH], path, dev_handlers_specific[i].types);
    }
    if (net->num_devs == 0) {
        /* Also remove generic device handlers. */
        for (i = 0; i < NUM_DEV_HANDLERS_GENERIC; i++) {
            /* make sure method isn't also used by graph */
            int found = 0;
            for (j = 0; j < NUM_GRAPH_HANDLERS; j++) {
                if (dev_handlers_generic[i].str_idx == graph_handlers[j].str_idx) {
                    found = 1;
                    break;
                }
            }
            if (found)
                continue;
            lo_server_del_method(net->servers[SERVER_BUS],
                                 net_msg_strings[dev_handlers_generic[i].str_idx],
                                 dev_handlers_generic[i].types);
            lo_server_del_method(net->servers[SERVER_MESH],
                                 net_msg_strings[dev_handlers_generic[i].str_idx],
                                 dev_handlers_generic[i].types);
        }
    }
}

int mpr_net_get_num_devs(mpr_net net)
{
    return net->num_devs;
}

static void mpr_net_add_graph_methods(mpr_net net, lo_server server)
{
    /* add graph methods */
    int i;
    for (i = 0; i < NUM_GRAPH_HANDLERS; i++) {
        lo_server_add_method(server, net_msg_strings[graph_handlers[i].str_idx],
                             graph_handlers[i].types, graph_handlers[i].h, net->graph);
    }
    return;
}

mpr_net mpr_net_new(mpr_graph g)
{
    mpr_net net = (mpr_net) calloc(1, sizeof(mpr_net_t));
    net->graph = g;
    mpr_net_init(net, 0, 0, 0);
    return net;
}

int mpr_net_init(mpr_net net, const char *iface, const char *group, int port)
{
    int i, updated = 0;
    lo_address temp_addr1, temp_addr2;
    lo_server temp_server1, temp_server2;

    /* Default standard ip and port is group 224.0.1.3, port 7570 */
    char port_str[10], *s_port = port_str;

    /* send out any cached messages */
    mpr_net_send(net);

    if (net->multicast.group) {
        if (group && strcmp(group, net->multicast.group)) {
            free(net->multicast.group);
            net->multicast.group = strdup(group);
            updated = 1;
        }
    }
    else {
        net->multicast.group = strdup(group ? group : "224.0.1.3");
        updated = 1;
    }
    if (!net->multicast.port || (port && port != net->multicast.port)) {
        net->multicast.port = port ? port : 7570;
        updated = 1;
    }
    snprintf(port_str, 10, "%d", net->multicast.port);

    /* Initialize interface information. */
    if ((!net->iface.name || (iface && strcmp(iface, net->iface.name)))
        && !get_iface_addr(iface, &net->iface.addr, &net->iface.name))
        updated = 1;
    trace("found interface: %s\n", net->iface.name ? net->iface.name : "none");

    if (!updated)
        return 0;

    /* Open address */
    temp_addr1 = lo_address_new(net->multicast.group, s_port);
    if (!temp_addr1) {
        trace("problem allocating bus address.\n");
        return 1;
    }

    /* Set TTL for packet to 1 -> local subnet */
    lo_address_set_ttl(temp_addr1, 1);

    /* Specify the interface to use for multicasting */
    lo_address_set_iface(temp_addr1, net->iface.name, 0);

    /* Swap and free old address structure if necessary */
    temp_addr2 = net->addr.bus;
    net->addr.bus = temp_addr1;
    FUNC_IF(lo_address_free, temp_addr2);

    /* Open server for multicast */
    temp_server1 = lo_server_new_multicast_iface(net->multicast.group, s_port,
                                                 net->iface.name, 0, handler_error);

    if (!temp_server1) {
        trace("problem allocating bus server.\n");
        return 2;
    }
    trace("bus connected to %s:%s\n", net->multicast.group, s_port);

    /* Disable liblo message queueing and add methods. */
    lo_server_enable_queue(temp_server1, 0, 1);
    mpr_net_add_graph_methods(net, temp_server1);

    /* Swap and free old server structure if necessary */
    temp_server2 = net->servers[SERVER_BUS];
    net->servers[SERVER_BUS] = temp_server1;
    FUNC_IF(lo_server_free, temp_server2);

    /* Also open address/server for mesh-style communications */
    /* TODO: use TCP instead? */
    while (!(temp_server1 = lo_server_new(0, handler_error))) {}

    /* Disable liblo message queueing and add methods. */
    lo_server_enable_queue(temp_server1, 0, 1);
    mpr_net_add_graph_methods(net, temp_server1);

    /* Swap and free old server structure if necessary */
    temp_server2 = net->servers[SERVER_MESH];
    net->servers[SERVER_MESH] = temp_server1;
    FUNC_IF(lo_server_free, temp_server2);

    for (i = 0; i < net->num_devs; i++) {
        mpr_net_add_dev(net, net->devs[i]);
        mpr_dev_set_net_servers(net->devs[i], net->servers);
    }

    return 0;
}

const char *mpr_net_get_interface(mpr_net net)
{
    return net->iface.name;
}

const char *mpr_net_get_address(mpr_net net)
{
    if (!net->addr.url)
        net->addr.url = lo_address_get_url(net->addr.bus);
    return net->addr.url;
}

const char *mpr_get_version()
{
    return PACKAGE_VERSION;
}

void mpr_net_send(mpr_net net)
{
    RETURN_UNLESS(net->bundle);

    if (BUNDLE_DST_SUBSCRIBERS == net->addr.dst) {
        mpr_local_dev_send_to_subscribers(net->addr.dev, net->bundle, net->msg_type,
                                          net->servers[SERVER_MESH]);
    }
    else if (BUNDLE_DST_BUS == net->addr.dst)
        lo_send_bundle_from(net->addr.bus, net->servers[SERVER_MESH], net->bundle);
    else
        lo_send_bundle_from(net->addr.dst, net->servers[SERVER_MESH], net->bundle);

    lo_bundle_free_recursive(net->bundle);
    net->bundle = 0;
}

static int init_bundle(mpr_net net)
{
    mpr_time t;
    mpr_net_send(net);
    mpr_time_set(&t, MPR_NOW);
    net->bundle = lo_bundle_new(t);
    return net->bundle ? 0 : 1;
}

void mpr_net_use_bus(mpr_net net)
{
    if (net->bundle && (net->addr.dst != BUNDLE_DST_BUS))
        mpr_net_send(net);
    net->addr.dst = BUNDLE_DST_BUS;
    if (!net->bundle)
        init_bundle(net);
}

void mpr_net_use_mesh(mpr_net net, lo_address addr)
{
    if (net->bundle && (net->addr.dst != addr))
        mpr_net_send(net);
    net->addr.dst = addr;
    if (!net->bundle)
        init_bundle(net);
}

void mpr_net_use_subscribers(mpr_net net, mpr_local_dev dev, int type)
{
    if (net->bundle && (   net->addr.dst != BUNDLE_DST_SUBSCRIBERS
                        || net->addr.dev != dev
                        || net->msg_type != type))
        mpr_net_send(net);
    net->addr.dst = BUNDLE_DST_SUBSCRIBERS;
    net->addr.dev = dev;
    net->msg_type = type;
    if (!net->bundle)
        init_bundle(net);
}

void mpr_net_add_msg(mpr_net net, const char *s, net_msg_t c, lo_message m)
{
    int len = lo_bundle_length(net->bundle);
    if (!s)
        s = net_msg_strings[c];
    if (len && len + lo_message_length(m, s) >= MAX_BUNDLE_LEN) {
        mpr_net_send(net);
        init_bundle(net);
    }
    lo_bundle_add_message(net->bundle, s, m);
}

void mpr_net_free_msgs(mpr_net net)
{
    FUNC_IF(lo_bundle_free_recursive, net->bundle);
    net->bundle = 0;
}

/*! Free the memory allocated by a network structure.
 *  \param net      A network structure handle. */
void mpr_net_free(mpr_net net)
{
    /* send out any cached messages */
    mpr_net_send(net);
    FUNC_IF(free, net->iface.name);
    FUNC_IF(free, net->multicast.group);
    FUNC_IF(lo_server_free, net->servers[SERVER_BUS]);
    FUNC_IF(lo_server_free, net->servers[SERVER_MESH]);
    FUNC_IF(lo_address_free, net->addr.bus);
#ifndef WIN32
    /* For some reason Windows thinks return of lo_address_get_url() should not be freed */
    FUNC_IF(free, net->addr.url);
#endif /* WIN32 */
    free(net);
}

/*! Probe the network to see if a device's proposed name.ordinal is available. */
void mpr_net_send_name_probe(mpr_net net, const char *name)
{
    NEW_LO_MSG(msg, return);
    mpr_net_use_bus(net);
    lo_message_add_string(msg, name);
    lo_message_add_int32(msg, net->random_id);
    mpr_net_add_msg(net, 0, MSG_NAME_PROBE, msg);
}

static void send_device_sync(mpr_net net, mpr_local_dev dev)
{
    NEW_LO_MSG(msg, return);
    lo_message_add_string(msg, mpr_dev_get_name((mpr_dev)dev));
    lo_message_add_int32(msg, mpr_obj_get_version((mpr_obj)dev));
    mpr_net_add_msg(net, 0, MSG_SYNC, msg);
}

/* TODO: rename to mpr_dev...? */
void mpr_net_maybe_send_ping(mpr_net net, int force)
{
    int i;
    mpr_graph gph = net->graph;
    mpr_list list;
    mpr_time now;
    mpr_time_set(&now, MPR_NOW);
    if (now.sec > net->next_sub_ping) {
        net->next_sub_ping = now.sec + 2;

        /* housekeeping #1: check for staged maps that have expired */
        mpr_graph_cleanup(gph);

        RETURN_UNLESS(net->num_devs);
        for (i = 0; i < net->num_devs; i++) {
            mpr_local_dev dev = net->devs[i];
            if (mpr_local_dev_has_subscribers(dev)) {
                mpr_net_use_subscribers(net, dev, MPR_DEV);
                send_device_sync(net, dev);
            }
        }
    }
    RETURN_UNLESS(net->num_devs);
    if (!force && (now.sec < net->next_bus_ping))
        return;
    net->next_bus_ping = now.sec + 5 + (rand() % 4);

    mpr_net_use_bus(net);
    for (i = 0; i < net->num_devs; i++) {
        if (mpr_dev_get_is_registered((mpr_dev)net->devs[i]))
            send_device_sync(net, net->devs[i]);
    }

    /* housekeeping #2: periodically check if our links are still active */
    list = mpr_graph_get_list(gph, MPR_LINK);
    while (list) {
        mpr_link link = (mpr_link)*list;
        list = mpr_list_get_next(list);
        if (mpr_obj_get_is_local((mpr_obj)link) && mpr_link_housekeeping(link, now)) {
            int mapped = mpr_link_get_has_maps(link, MPR_DIR_ANY);
            mpr_dev local_dev = mpr_link_get_dev(link, LINK_LOCAL_DEV);
#ifdef DEBUG
            mpr_dev remote_dev = mpr_link_get_dev(link, LINK_REMOTE_DEV);
            trace_dev(local_dev, "Removing link to %sdevice '%s'\n", mapped ? "unresponsive " : "",
                      mpr_dev_get_name(remote_dev));
#endif
            /* remove related data structures */
            mpr_graph_remove_link(gph, link, mapped ? MPR_OBJ_EXP : MPR_OBJ_REM);
            mpr_net_use_subscribers(net, (mpr_local_dev)local_dev, MPR_DEV);
            mpr_dev_send_state(local_dev, MPR_DEV);
        }
    }
}

/*! This is the main function to be called once in a while from a program so
 *  that the libmapper bus can be automatically managed. */
void mpr_net_poll(mpr_net net, int force_ping)
{
    int i, num_devs = net->num_devs, registered = 0;

    /* send out any cached messages */
    mpr_net_send(net);

    if (num_devs) {
        if (net->registered < num_devs) {
            for (i = 0; i < net->num_devs; i++)
                registered += mpr_dev_get_is_registered((mpr_dev)net->devs[i]);
            net->registered = registered;
        }

        if (net->registered) {
            /* Send out clock sync messages occasionally */
            mpr_net_maybe_send_ping(net, force_ping);
        }
    }
    else {
        mpr_net_maybe_send_ping(net, 0);
    }

    mpr_graph_housekeeping(net->graph);
    return;
}

/**********************************/
/* Internal OSC message handlers. */
/**********************************/

/*! Respond to /who by announcing the basic device information. */
static int handler_who(const char *path, const char *types, lo_arg **av, int ac,
                       lo_message msg, void *user)
{
    mpr_graph gph = (mpr_graph)user;
    mpr_net net = mpr_graph_get_net(gph);
    RETURN_ARG_UNLESS(net->devs, 0);
    trace_net(net);
    mpr_net_maybe_send_ping(net, 1);
    return 0;
}

/*! Register information about port and host for the device. */
static int handler_dev(const char *path, const char *types, lo_arg **av, int ac,
                       lo_message msg, void *user)
{
    mpr_net net;
    mpr_dev remote;
    mpr_graph graph = (mpr_graph)user;
    int data_port;
    mpr_msg props = 0;
    mpr_list links = 0, cpy;
    const char *name, *host, *admin_port;
    lo_address a;

    RETURN_ARG_UNLESS(ac && MPR_STR == types[0], 0);
    net = mpr_graph_get_net(graph);
    name = &av[0]->s;

    trace_net(net);

    props = mpr_msg_parse_props(ac-1, &types[1], &av[1]);
    remote = mpr_graph_add_dev(graph, name, props, 0);
    if (!remote || !net->devs)
        goto done;
    TRACE_NET_RETURN_UNLESS(!mpr_obj_get_is_local((mpr_obj)remote), 0,
                            "ignoring /device message from self\n");
    trace("  checking device '%s'\n", &av[0]->s);

    /* TODO: link should be between mpr_net objects instead? */
    /* Discover whether the device is linked. */
    links = mpr_dev_get_links(remote, MPR_DIR_UNDEFINED);
    if (!links || !(*links)) {
        trace("  ignoring /device '%s', no link.\n", name);
        goto done;
    }
    else {
        mpr_list cpy = mpr_list_get_cpy(links);
        while (cpy) {
            mpr_link link = (mpr_link)*cpy;
            if (!mpr_link_get_admin_addr(link))
                break;
            cpy = mpr_list_get_next(cpy);
        }
        if (!cpy) {
            trace("  ignoring /device '%s', links already set.\n", name);
            goto done;
        }
        mpr_list_free(cpy);
    }

    a = lo_message_get_source(msg);
    if (!a) {
        trace("  can't perform /linkTo, address unknown\n");
        goto done;
    }

    /* Find the sender's hostname */
    host = lo_address_get_hostname(a);
    admin_port = lo_address_get_port(a);
    if (!host) {
        trace("  can't perform /linkTo, host unknown\n");
        goto done;
    }

    /* Retrieve the port */
    data_port = mpr_msg_get_prop_as_int32(props, MPR_PROP_PORT);
    if (!data_port) {
        trace("  can't perform /linkTo, port unknown\n");
        goto done;
    }

    cpy = mpr_list_get_cpy(links);
    while (cpy) {
        mpr_link link = (mpr_link)*cpy;
        cpy = mpr_list_get_next(cpy);
        if (mpr_obj_get_is_local((mpr_obj)link)) {
            mpr_list maps;
            trace("  establishing link to %s.\n", name)
            mpr_link_connect(link, host, atoi(admin_port), data_port);

            /* check if we have maps waiting for this link */
            trace("  checking for waiting maps.\n");
            maps = mpr_link_get_maps(link);
            while (maps) {
                mpr_map map = (mpr_map)*maps;
                mpr_loc locality = mpr_map_get_locality(map);
                maps = mpr_list_get_next(maps);
                if (locality & MPR_LOC_SRC) {
                    int i;
                    mpr_net_use_mesh(net, mpr_link_get_admin_addr(link));
                    for (i = 0; i < mpr_map_get_num_src(map); i++) {
                        mpr_sig sig = mpr_map_get_src_sig(map, i);
                        if (!mpr_obj_get_is_local((mpr_obj)sig))
                            continue;
                        mpr_sig_send_state(sig, MSG_SIG);
                    }
                    mpr_map_send_state(map, -1, MSG_MAP_TO);
                }
                if (locality & MPR_LOC_DST) {
                    int i;
                    for (i = 0; i < mpr_map_get_num_src((mpr_map)map); i++) {
                        if (mpr_slot_get_link(mpr_map_get_src_slot(map, i)) != link)
                            continue;
                        mpr_net_use_mesh(net, mpr_link_get_admin_addr(link));
                        mpr_sig_send_state(mpr_map_get_dst_sig(map), MSG_SIG);
                        i = mpr_map_send_state(map, i, MSG_MAP_TO);
                    }
                }
            }
            break;
        }
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
    mpr_local_dev dev = (mpr_local_dev)user;
    mpr_msg props;

    RETURN_ARG_UNLESS(dev && mpr_dev_get_is_ready((mpr_dev)dev)
                      && ac >= 2 && MPR_STR == types[0], 0);
    props = mpr_msg_parse_props(ac, types, av);
    trace_dev(dev, "received /%s/modify + %d properties.\n", path, mpr_msg_get_num_atoms(props));
    if (mpr_dev_set_from_msg((mpr_dev)dev, props)) {
        inform_device_subscribers(mpr_graph_get_net(mpr_obj_get_graph((mpr_obj)dev)), dev);
        mpr_obj_clear_empty_props((mpr_obj)dev);
    }
    mpr_msg_free(props);
    return 0;
}

/*! Respond to /logout by deleting record of device. */
static int handler_logout(const char *path, const char *types, lo_arg **av,
                          int ac, lo_message msg, void *user)
{
    mpr_graph gph = (mpr_graph)user;
    mpr_net net = mpr_graph_get_net(gph);
    mpr_dev remote;

    RETURN_ARG_UNLESS(ac && MPR_STR == types[0], 0);
    trace_net(net);
    remote = mpr_graph_get_dev_by_name(gph, &av[0]->s);
    if (net->num_devs) {
        int i = 0, ordinal;
        char *prefix_str, *ordinal_str;

        /* Parse the ordinal from name in the format: <name>.<n> */
        prefix_str = &av[0]->s;
        ordinal_str = strrchr(&av[0]->s, '.');
        TRACE_RETURN_UNLESS(ordinal_str && isdigit(ordinal_str[1]), 0, "Malformed device name.\n");
        *ordinal_str = '\0';
        ordinal = atoi(++ordinal_str);

        for (i = 0; i < net->num_devs; i++) {
            mpr_local_dev_handler_logout(net->devs[i], remote, prefix_str, ordinal);
        }
    }
    if (remote) {
        mpr_graph_unsubscribe(gph, remote);
        mpr_graph_remove_dev(gph, remote, MPR_OBJ_REM);
    }
    return 0;
}

/*! Respond to /subscribe message by adding or renewing a subscription. */
static int handler_subscribe(const char *path, const char *types, lo_arg **av,
                             int ac, lo_message msg, void *user)
{
    mpr_local_dev dev = (mpr_local_dev)user;
    int i, version = -1, flags = 0, timeout_seconds = -1;

#ifdef DEBUG
    trace_net(mpr_graph_get_net(mpr_obj_get_graph((mpr_obj)dev)));
    trace_dev(dev, "received /subscribe\n");
#endif

    lo_address addr  = lo_message_get_source(msg);
    TRACE_DEV_RETURN_UNLESS(addr && ac, 0, "error retrieving subscription source address.\n");

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
            /* next argument is last device version recorded by subscriber */
            ++i;
            if (i < ac && MPR_INT32 == types[i])
                version = av[i]->i;
        }
        else if (0 == strcmp(&av[i]->s, "@lease")) {
            /* next argument is lease timeout in seconds */
            ++i;
            if (MPR_INT32 == types[i])
                timeout_seconds = av[i]->i;
            else if (MPR_FLT == types[i])
                timeout_seconds = (int)av[i]->f;
            else if (MPR_DBL == types[i])
                timeout_seconds = (int)av[i]->d;
            else
                {trace_dev(dev, "error parsing subscription lease prop.\n");}
            timeout_seconds = timeout_seconds >= 0 ? timeout_seconds : 0;
        }
    }

    /* add or renew subscription */
    mpr_dev_manage_subscriber(dev, addr, flags, timeout_seconds, version);
    return 0;
}

/*! Register information about a signal. */
static int handler_sig(const char *path, const char *types, lo_arg **av, int ac,
                       lo_message msg, void *user)
{
    mpr_graph gph = (mpr_graph)user;
    char *full_sig_name, *signamep, *devnamep;
    int devnamelen;
    mpr_msg props;

    trace_net(mpr_graph_get_net(gph));

    RETURN_ARG_UNLESS(ac >= 2 && MPR_STR == types[0], 1);
    full_sig_name = &av[0]->s;
    devnamelen = mpr_path_parse(full_sig_name, &devnamep, &signamep);
    RETURN_ARG_UNLESS(devnamep && signamep, 0);

    props = mpr_msg_parse_props(ac-1, &types[1], &av[1]);
    devnamep[devnamelen] = 0;

    mpr_graph_add_sig(gph, signamep, devnamep, props);
    devnamep[devnamelen] = '/';
    mpr_msg_free(props);

    return 0;
}

/* Helper function to check if the prefix matches.  Like strcmp(), returns 0 if
 * they match (up to the first '/'), non-0 otherwise.  Also optionally returns a
 * pointer to the remainder of str1 after the prefix. */
static int prefix_cmp(const char *str1, const char *str2, const char **rest)
{
    const char *s1, *s2;
    int n1, n2, result;

    /* skip first slash */
    str1 += ('/' == str1[0]);
    str2 += ('/' == str2[0]);

    s1 = str1;
    s2 = str2;

    while (*s1 && (*s1)!='/') ++s1;
    while (*s2 && (*s2)!='/') ++s2;

    n1 = s1 - str1;
    n2 = s2 - str2;
    if (n1!=n2) return 1;

    result = strncmp(str1, str2, n1);
    if (!result && rest)
        *rest = s1+1;

    return result;
}

/*! Handle remote requests to add, modify, or remove metadata to a signal. */
static int handler_sig_mod(const char *path, const char *types, lo_arg **av,
                           int ac, lo_message msg, void *user)
{
    mpr_local_dev dev = (mpr_local_dev)user;
    mpr_net net = mpr_graph_get_net(mpr_obj_get_graph((mpr_obj)dev));
    mpr_sig sig;
    mpr_msg props;
    RETURN_ARG_UNLESS(dev && mpr_dev_get_is_ready((mpr_dev)dev) && ac > 1 && MPR_STR == types[0], 0);

    /* retrieve signal */
    sig = mpr_dev_get_sig_by_name((mpr_dev)dev, &av[0]->s);
    TRACE_DEV_RETURN_UNLESS(sig, 0, "no signal found with name '%s'.\n", &av[0]->s);

    props = mpr_msg_parse_props(ac-1, &types[1], &av[1]);
    trace_dev(dev, "received %s + %d properties.\n", path, mpr_msg_get_num_atoms(props));

    if (mpr_sig_set_from_msg(sig, props)) {
        if (mpr_local_dev_has_subscribers(dev)) {
            int dir = (MPR_DIR_IN == mpr_sig_get_dir(sig)) ? MPR_SIG_IN : MPR_SIG_OUT;
            trace_dev(dev, "informing subscribers (SIGNAL)\n");
            mpr_net_use_subscribers(net, dev, dir);
            mpr_sig_send_state(sig, MSG_SIG);
        }
        mpr_obj_clear_empty_props((mpr_obj)sig);
    }
    mpr_msg_free(props);
    return 0;
}

/*! Unregister information about a removed signal. */
static int handler_sig_removed(const char *path, const char *types, lo_arg **av,
                               int ac, lo_message msg, void *user)
{
    mpr_graph gph = (mpr_graph)user;
    mpr_dev dev;
    char *full_sig_name, *signamep, *devnamep, devname[1024];
    int devnamelen;
    RETURN_ARG_UNLESS(ac && MPR_STR == types[0], 1);

    trace_net(mpr_graph_get_net(gph));

    full_sig_name = &av[0]->s;
    devnamelen = mpr_path_parse(full_sig_name, &devnamep, &signamep);
    RETURN_ARG_UNLESS(devnamep && signamep && devnamelen < 1024, 0);

    strncpy(devname, devnamep, devnamelen);
    devname[devnamelen] = 0;

    dev = mpr_graph_get_dev_by_name(gph, devname);
    if (dev && !mpr_obj_get_is_local((mpr_obj)dev))
        mpr_graph_remove_sig(gph, mpr_dev_get_sig_by_name(dev, signamep), MPR_OBJ_REM);
    return 0;
}

/*! Respond to name collisions during allocation, help suggest IDs once allocated. */
static int handler_name(const char *path, const char *types, lo_arg **av,
                        int ac, lo_message msg, void *user)
{
    mpr_net net = (mpr_net)user;
    int i, temp_id = -1, hint = 0;
    char *name;
    RETURN_ARG_UNLESS(ac && MPR_STR == types[0], 0);

    trace_net(net);

    name = &av[0]->s;
    if (ac > 1) {
        if (MPR_INT32 == types[1])
            temp_id = av[1]->i;
        if (MPR_INT32 == types[2])
            hint = av[2]->i;
    }

    for (i = 0; i < net->num_devs; i++)
        mpr_local_dev_handler_name(net->devs[i], name, temp_id, net->random_id, hint);

    return 0;
}

/*! Respond to name probes during allocation, help suggest names once allocated. */
static int handler_name_probe(const char *path, const char *types, lo_arg **av,
                              int ac, lo_message msg, void *user)
{
    mpr_net net = (mpr_net)user;
    char *name = &av[0]->s;
    int i, temp_id = av[1]->i;
    mpr_id id = mpr_id_from_str(name);

    trace_net(net);
    for (i = 0; i < net->num_devs; i++)
        mpr_local_dev_handler_name_probe(net->devs[i], name, temp_id, net->random_id, id);

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
 * device; if the map involves inputs from different devices the destination
 * will provoke the creation of simple submaps from the various sources and
 * perform any combination signal-processing, otherwise processing metadata is
 * forwarded to the source device.  A convergent mapping is started with a
 * message in the form: "/map <sourceA> <sourceB> ... <sourceN> -> <destination>"
 */

static int parse_sig_names(const char *types, lo_arg **av, int ac, int *src_idx,
                           int *dst_idx, int *prop_idx)
{
    /* protocol: /map src1 ... srcN -> dst OR /map dst <- src1 ... srcN */
    int i, num_src = 0;
    RETURN_ARG_UNLESS(!strncmp(types, "sss", 3), 0);
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
        TRACE_RETURN_UNLESS(strchr((&av[*src_idx+i]->s)+1, '/'), 0,
                            "malformed source signal name '%s'.\n", &av[*src_idx+i]->s);
        TRACE_RETURN_UNLESS(strcmp(&av[*src_idx+i]->s, &av[*dst_idx]->s), 0,
                            "prevented attempt to connect signal '%s' to itself.\n",
                            &av[*dst_idx]->s);
    }
    TRACE_RETURN_UNLESS(strchr((&av[*dst_idx]->s)+1, '/'), 0, "malformed "
                        "destination signal name '%s'.\n", &av[*dst_idx]->s)
    return num_src;
}

#define MPR_MAP_ERROR (mpr_map)-1

static mpr_map find_map(mpr_net net, const char *types, int ac, lo_arg **av, mpr_loc loc, int flags)
{
    int i, is_loc = 0, src_idx, dst_idx, prop_idx, num_src;
    mpr_sig sig = 0;
    mpr_map map;
    mpr_id id = 0;
    const char *sig_name, *src_names[MAX_NUM_MAP_SRC], *dst_name;

    RETURN_ARG_UNLESS(net->num_devs || !loc, MPR_MAP_ERROR);
    num_src = parse_sig_names(types, av, ac, &src_idx, &dst_idx, &prop_idx);
    RETURN_ARG_UNLESS(num_src, MPR_MAP_ERROR);
    RETURN_ARG_UNLESS(is_alphabetical(num_src, &av[src_idx]), MPR_MAP_ERROR);

    /* first check for an 'id' property */
    for (i = 3; i < ac; i++) {
        if (types[i] != MPR_STR)
            continue;
        if (0 == strcmp(&av[i]->s, "@id"))
            break;
    }

    if (i < ac && MPR_INT64 == types[++i]) {
        /* 'id' property found */
        id = av[i]->i64;
        map = (mpr_map)mpr_graph_get_obj(net->graph, id, MPR_MAP);
#ifdef DEBUG
        trace("%s map with id %"PR_MPR_ID" ", map ? "found" : "couldn't find", id);
        if (map)
            mpr_prop_print(1, MPR_MAP, map);
        printf("\n");
#endif
        if (map) {
            int locality = mpr_map_get_locality(map);
            RETURN_ARG_UNLESS(!loc || (locality & loc), MPR_MAP_ERROR);
            if (mpr_map_get_num_src(map) < num_src && (flags & UPDATE)) {
                /* add additional sources */
                for (i = 0; i < num_src; i++)
                    src_names[i] = &av[src_idx+i]->s;
                map = mpr_graph_add_map(net->graph, id, num_src, src_names, &av[dst_idx]->s);
            }
            return map;
        }
        else if (!(flags & ADD))
            return 0;
        else {
            for (i = 0; i < net->num_devs; i++) {
                if ((id & 0xFFFFFFFF00000000) == mpr_obj_get_id((mpr_obj)net->devs[i])) {
                    trace_dev(net->devs[i], "Ignoring unknown local map - possibly deleted.\n")
                    break;
                }
            }
            if (i < net->num_devs)
                return 0;
        }
    }

    /* try signal names instead */
    dst_name = &av[dst_idx]->s;
    for (i = 0; i < num_src; i++)
        src_names[i] = &av[src_idx+i]->s;

    if (MPR_LOC_DST & loc) {
        /* check if we are the destination */
        for (i = 0; i < net->num_devs; i++) {
            mpr_local_dev dev = net->devs[i];
            if (!mpr_dev_get_is_registered((mpr_dev)dev))
                continue;
            if (   !prefix_cmp(&av[dst_idx]->s, mpr_dev_get_name((mpr_dev)dev), &sig_name)
                && (sig = mpr_dev_get_sig_by_name((mpr_dev)dev, sig_name))) {
                is_loc = 1;
                break;
            }
        }
        RETURN_ARG_UNLESS(is_loc || MPR_LOC_DST != loc, MPR_MAP_ERROR);
    }
    if (!sig && MPR_LOC_SRC & loc) {
        /* check if we are a source – all sources must match! */
        for (i = 0; i < num_src; i++) {
            int j;
            for (j = 0; j < net->num_devs; j++) {
                mpr_local_dev dev = net->devs[j];
                if (!mpr_dev_get_is_registered((mpr_dev)dev))
                    continue;
                if (   !prefix_cmp(src_names[i], mpr_dev_get_name((mpr_dev)dev), &sig_name)
                    && (sig = mpr_dev_get_sig_by_name((mpr_dev)dev, sig_name))) {
                    is_loc = 1;
                    break;
                }
            }
            RETURN_ARG_UNLESS(is_loc || MPR_LOC_SRC != loc, MPR_MAP_ERROR);
        }
    }
    RETURN_ARG_UNLESS(!loc || is_loc, MPR_MAP_ERROR);
    map = mpr_graph_get_map_by_names(net->graph, num_src, src_names, dst_name);
#ifdef DEBUG
    trace("%s map with src name%s", map ? "found" : "couldn't find", num_src > 1 ? "s: [" : ": ");
    for (i = 0; i < num_src; i++)
        printf("'%s', ", src_names[i]);
    printf("\b\b%s and dst name '%s'\n", num_src > 1 ? "]" : "", dst_name);
#endif
    if (!map && (flags & ADD)) {
        /* safety check: make sure we don't already have an outgoing map from sig -> src. */
        if (sig && mpr_local_sig_check_outgoing((mpr_local_sig)sig, num_src, src_names)) {
            trace("error in /map: potential loop detected.")
            return MPR_MAP_ERROR;
        }
        map = mpr_graph_add_map(net->graph, id, num_src, src_names, &av[dst_idx]->s);
    }
    return map;
}

static void mpr_net_handle_map(mpr_net net, mpr_local_map map, mpr_msg props)
{
    mpr_sig sig = mpr_map_get_dst_sig((mpr_map)map);
    mpr_local_dev dev = (mpr_local_dev)mpr_sig_get_dev(sig);
    int i;

    mpr_map_set_from_msg((mpr_map)map, props);

    if (MPR_LOC_BOTH == mpr_map_get_locality((mpr_map)map) && mpr_local_map_get_expr(map)) {
        trace_dev(dev, "map references only local signals... activating.\n");
        mpr_map_set_status((mpr_map)map, MPR_STATUS_ACTIVE);

        /* Inform subscribers */
        if (mpr_local_dev_has_subscribers(dev)) {
            inform_device_subscribers(net, dev);

            trace_dev(dev, "informing subscribers (SIGNAL)\n")
            mpr_net_use_subscribers(net, dev, MPR_SIG);
            for (i = 0; i < mpr_map_get_num_src((mpr_map)map); i++)
                mpr_sig_send_state(mpr_map_get_src_sig((mpr_map)map, i), MSG_SIG);
            mpr_sig_send_state(mpr_map_get_dst_sig((mpr_map)map), MSG_SIG);

            trace_dev(dev, "informing subscribers (MAPPED)\n")
            mpr_net_use_subscribers(net, dev, MPR_MAP);
            mpr_map_send_state((mpr_map)map, -1, MSG_MAPPED);
        }
        return;
    }

    for (i = 0; i < mpr_map_get_num_src((mpr_map)map); i++) {
        mpr_link link;
        lo_address addr;
        mpr_slot slot = mpr_map_get_src_slot((mpr_map)map, i);
        /* do not send if is local mapping */
        if (mpr_slot_get_sig_if_local(slot))
            continue;
        /* do not send if device host/port not yet known */
        if (   !(link = mpr_slot_get_link(slot))
            || !(addr = mpr_link_get_admin_addr(link))) {
            trace_dev(dev, "delaying map handshake while waiting for network link.\n");
            continue;
        }
        trace_dev(dev, "sending /mapTo to remote source.\n");
        mpr_net_use_mesh(net, addr);

        /* TODO: do we need this call? */
        mpr_sig_send_state(sig, MSG_SIG);

        i = mpr_map_send_state((mpr_map)map, i, MSG_MAP_TO);
    }
}


/*! When the /map message is received by the destination device, send a /mapTo
 *  message to the source device.
 */
static int handler_map(const char *path, const char *types, lo_arg **av, int ac,
                       lo_message msg, void *user)
{
    mpr_graph gph = (mpr_graph)user;
    mpr_net net = mpr_graph_get_net(gph);
    mpr_local_map map;
    mpr_msg props;

    RETURN_ARG_UNLESS(net->num_devs, 0);
    trace_net(net);
    map = (mpr_local_map)find_map(net, types, ac, av, MPR_LOC_DST, ADD | UPDATE);
    RETURN_ARG_UNLESS(map && MPR_MAP_ERROR != (mpr_map)map, 0);

#ifdef DEBUG
    {
        mpr_sig sig = mpr_map_get_dst_sig((mpr_map)map);
        trace_dev(mpr_sig_get_dev(sig), "received /map ");
        lo_message_pp(msg);
    }
#endif

    props = mpr_msg_parse_props(ac, types, av);
    if (mpr_map_get_status((mpr_map)map) == MPR_STATUS_ACTIVE) {
        mpr_loc loc = mpr_local_map_get_process_loc_from_msg(map, props);
        if (MPR_LOC_DST == loc) {
            /* Forward to local handler_map_mod(). */
            trace("Map is already active! Forwarding map message to modify handler...\n");
            handler_map_mod(path, types, av, ac, msg, user);
        }
        else {
            /* Forward message to remote peer as /map/modify. */
            int i, num_src = mpr_map_get_num_src((mpr_map)map);
            trace("Map is already active! Forwarding map message to remote peer...\n");
            for (i = 0; i < num_src; i++) {
                mpr_slot slot = mpr_map_get_src_slot((mpr_map)map, i);
                mpr_link link = mpr_slot_get_link(slot);
                mpr_net_use_mesh(net, mpr_link_get_admin_addr(link));
                mpr_net_add_msg(net, 0, MSG_MAP_MOD, msg);
            }
            mpr_net_send(net);
        }
    }
    else {
        mpr_net_handle_map(net, map, props);
    }
    mpr_msg_free(props);
    return 0;
}

/*! When the /mapTo message is received by a peer device, create a tentative
 *  map and respond with own signal metadata. */
static int handler_map_to(const char *path, const char *types, lo_arg **av,
                          int ac, lo_message msg, void *user)
{
    mpr_graph gph = (mpr_graph)user;
    mpr_net net = mpr_graph_get_net(gph);
    mpr_local_map map = (mpr_local_map)find_map(net, types, ac, av, MPR_LOC_ANY, ADD | UPDATE);

    trace_net(net);
    RETURN_ARG_UNLESS(map && MPR_MAP_ERROR != (mpr_map)map, 0);

    if (mpr_map_get_status((mpr_map)map) < MPR_STATUS_ACTIVE) {
        /* Set map properties. */
        mpr_msg props = mpr_msg_parse_props(ac, types, av);
        mpr_map_set_from_msg((mpr_map)map, props);
        mpr_msg_free(props);
    }

    if (mpr_map_get_status((mpr_map)map) >= MPR_STATUS_READY) {
        int i, num_src = mpr_map_get_num_src((mpr_map)map);
        mpr_slot slot = mpr_map_get_dst_slot((mpr_map)map);
        if (MPR_DIR_OUT == mpr_slot_get_dir(slot)) {
            mpr_link link = mpr_slot_get_link(slot);
            mpr_net_use_mesh(net, mpr_link_get_admin_addr(link));
            mpr_map_send_state((mpr_map)map, -1, MSG_MAPPED);
            for (i = 0; i < num_src; i++) {
                mpr_sig sig = mpr_map_get_src_sig((mpr_map)map, i);
                if (!mpr_obj_get_is_local((mpr_obj)sig))
                    continue;
                mpr_sig_send_state(sig, MSG_SIG);
            }
        }
        else {
            for (i = 0; i < num_src; i++) {
                mpr_slot slot = mpr_map_get_src_slot((mpr_map)map, i);
                mpr_link link = mpr_slot_get_link(slot);
                mpr_net_use_mesh(net, mpr_link_get_admin_addr(link));
                i = mpr_map_send_state((mpr_map)map, i, MSG_MAPPED);
                mpr_sig_send_state(mpr_map_get_dst_sig((mpr_map)map), MSG_SIG);
            }
        }
    }
    return 0;
}

/*! Respond to /mapped by storing a map in the graph. Also used by devices to
 *  confirm connection to remote peers, and to share property changes. */
static int handler_mapped(const char *path, const char *types, lo_arg **av,
                          int ac, lo_message msg, void *user)
{
    mpr_graph gph = (mpr_graph)user;
    mpr_net net = mpr_graph_get_net(gph);
    mpr_map map;
    mpr_msg props;
    mpr_loc loc = MPR_LOC_UNDEFINED;
    int rc = 0, updated = 0;

    trace_net(net);
    map = find_map(net, types, ac, av, 0, UPDATE);
    RETURN_ARG_UNLESS(MPR_MAP_ERROR != map, 0);
    if (!map) {
        int store = 0, i = 0;
        if (mpr_graph_get_autosub(gph) & MPR_MAP)
            store = 1;
        else {
            while (MPR_STR == types[i] && '@' != (&av[i]->s)[0]) {
                if ('-' != (&av[i]->s)[0] && mpr_graph_subscribed_by_sig(gph, &av[i]->s)) {
                    store = 1;
                    break;
                }
                ++i;
            }
        }
        if (store) {
            map = find_map(net, types, ac, av, 0, ADD);
            rc = 1;
        }
        RETURN_ARG_UNLESS(map && MPR_MAP_ERROR != map, 0);
    }
    else if (MPR_LOC_BOTH == mpr_map_get_locality(map)) {
        /* no need to update since all properties are local */
        return 0;
    }

    props = mpr_msg_parse_props(ac, types, av);
    if (props) {
        const char *str;
        if ((str = mpr_msg_get_prop_as_str(props, MPR_PROP_PROCESS_LOC))) {
            loc = mpr_loc_from_str(str);
        }
        if (   (str = mpr_msg_get_prop_as_str(props, MPR_PROP_EXPR))
            || (str = mpr_map_get_expr_str((mpr_map)map))) {
            if (strstr(str, "y{-")) {
                loc = MPR_LOC_DST;
            }
        }
    }

    if (!(loc & mpr_map_get_locality(map))) {
        updated = mpr_map_set_from_msg(map, props);
#ifdef DEBUG
        trace("  updated %d properties for %s map. (1)\n", updated,
              mpr_obj_get_is_local((mpr_obj)map) ? "local" : "remote");
#endif
    }
    else {
        /* Call mpr_map_set_from_msg() anyway to check map status. */
        /* TODO: refactor for clarity */
        mpr_map_set_from_msg(map, 0);
    }
    mpr_msg_free(props);

    if (mpr_obj_get_is_local((mpr_obj)map)) {
        int status = mpr_map_get_status(map);
        RETURN_ARG_UNLESS(status >= MPR_STATUS_READY, 0);
        if (MPR_STATUS_ACTIVE > status) {
            int i, num_src = mpr_map_get_num_src(map);
            mpr_sig sig;
            mpr_slot slot = mpr_map_get_dst_slot(map);
            mpr_map_set_status(map, MPR_STATUS_ACTIVE);
            rc = 1;

            if (MPR_DIR_OUT == mpr_slot_get_dir(slot)) {
                /* Inform remote destination */
                mpr_net_use_mesh(net, mpr_link_get_admin_addr(mpr_slot_get_link(slot)));
                mpr_map_send_state(map, -1, MSG_MAPPED);
            }
            else {
                /* Inform remote sources */
                for (i = 0; i < num_src; i++) {
                    mpr_slot slot = mpr_map_get_src_slot(map, i);
                    mpr_net_use_mesh(net, mpr_link_get_admin_addr(mpr_slot_get_link(slot)));
                    i = mpr_map_send_state(map, i, MSG_MAPPED);
                }
            }

            for (i = 0; i < num_src; i++) {
                sig = mpr_map_get_src_sig(map, i);
                if (mpr_obj_get_is_local((mpr_obj)sig)) {
                    mpr_local_dev dev = (mpr_local_dev)mpr_sig_get_dev(sig);
                    inform_device_subscribers(net, dev);
                    trace_dev(dev, "informing subscribers (SIGNAL)\n");
                    mpr_net_use_subscribers(net, dev, MPR_SIG);
                    mpr_sig_send_state(sig, MSG_SIG);
                }
            }
            sig = mpr_map_get_dst_sig(map);
            if (mpr_obj_get_is_local((mpr_obj)sig)) {
                mpr_local_dev dev = (mpr_local_dev)mpr_sig_get_dev(sig);
                inform_device_subscribers(net, dev);
                trace_dev(dev, "informing subscribers (SIGNAL)\n");
                mpr_net_use_subscribers(net, dev, MPR_SIG);
                mpr_sig_send_state(sig, MSG_SIG);
            }
        }
    }
    if (rc || updated) {
        if (mpr_obj_get_is_local((mpr_obj)map)) {
            mpr_sig sig;
            int i, num_src = mpr_map_get_num_src(map);
            for (i = 0; i < num_src; i++) {
                sig = mpr_map_get_src_sig(map, i);
                if (mpr_obj_get_is_local((mpr_obj)sig)) {
                    mpr_local_dev dev = (mpr_local_dev)mpr_sig_get_dev(sig);
                    if (mpr_local_dev_has_subscribers(dev)) {
                        trace_dev(dev, "informing subscribers (MAPPED)\n")
                        mpr_net_use_subscribers(net, dev, MPR_MAP_OUT);
                        mpr_map_send_state(map, -1, MSG_MAPPED);
                    }
                }
            }
            sig = mpr_map_get_dst_sig(map);
            if (mpr_obj_get_is_local((mpr_obj)sig)) {
                mpr_local_dev dev = (mpr_local_dev)mpr_sig_get_dev(sig);
                if (mpr_local_dev_has_subscribers(dev)) {
                    trace_dev(dev, "informing subscribers (MAPPED)\n")
                    mpr_net_use_subscribers(net, dev, MPR_MAP_IN);
                    mpr_map_send_state(map, -1, MSG_MAPPED);
                }
            }
        }
        mpr_graph_call_cbs(gph, (mpr_obj)map, MPR_MAP, rc ? MPR_OBJ_NEW : MPR_OBJ_MOD);
    }
    mpr_obj_clear_empty_props((mpr_obj)map);
    return 0;
}

/*! Modify the map properties : mode, range, expression, etc. */
static int handler_map_mod(const char *path, const char *types, lo_arg **av,
                           int ac, lo_message msg, void *user)
{
    mpr_graph gph = (mpr_graph)user;
    mpr_net net = mpr_graph_get_net(gph);
    mpr_local_map map;
    mpr_msg props;
    mpr_loc locality = MPR_LOC_UNDEFINED;
    int i, updated;

    RETURN_ARG_UNLESS(ac >= 4, 0);
    trace_net(net);

    map = (mpr_local_map)find_map(net, types, ac, av, MPR_LOC_ANY, FIND);
    RETURN_ARG_UNLESS(map && MPR_MAP_ERROR != (mpr_map)map, 0);
    RETURN_ARG_UNLESS(mpr_map_get_status((mpr_map)map) == MPR_STATUS_ACTIVE, 0);

    props = mpr_msg_parse_props(ac, types, av);
    TRACE_RETURN_UNLESS(props, 0, "  ignoring /map/modify, no properties.\n");

    /* do not continue if we are not in charge of processing */
    locality = mpr_map_get_locality((mpr_map)map);
    if (!(locality & mpr_local_map_get_process_loc_from_msg(map, props))) {
        trace(  "ignoring /map/modify, synced to remote peer.\n");
        goto done;
    }

    updated = mpr_map_set_from_msg((mpr_map)map, props);
    if (updated) {
        int num_src = mpr_map_get_num_src((mpr_map)map);
        if (MPR_LOC_BOTH != locality) {
            /* Inform remote peer(s) of relevant changes */
            mpr_slot slot = mpr_map_get_dst_slot((mpr_map)map);
            if (!mpr_slot_get_sig_if_local(slot)) {
                mpr_net_use_mesh(net, mpr_link_get_admin_addr(mpr_slot_get_link(slot)));
                mpr_map_send_state((mpr_map)map, -1, MSG_MAPPED);
            }
            else {
                int i;
                for (i = 0; i < num_src; i++) {
                    mpr_slot slot = mpr_map_get_src_slot((mpr_map)map, i);
                    if (mpr_slot_get_sig_if_local(slot))
                        continue;
                    mpr_net_use_mesh(net, mpr_link_get_admin_addr(mpr_slot_get_link(slot)));
                    i = mpr_map_send_state((mpr_map)map, i, MSG_MAPPED);
                }
            }
        }

        if (MPR_LOC_SRC & locality) {
            mpr_local_dev dev = 0;
            for (i = 0; i < num_src; i++) {
                mpr_slot slot = mpr_map_get_src_slot((mpr_map)map, i);
                mpr_sig sig = (mpr_sig)mpr_slot_get_sig_if_local(slot);
                if (sig && dev != (mpr_local_dev)mpr_sig_get_dev(sig)) {
                    dev = (mpr_local_dev)mpr_sig_get_dev(sig);
                    if (mpr_local_dev_has_subscribers(dev)) {
                        trace_dev(dev, "informing subscribers (MAPPED)\n")
                        mpr_net_use_subscribers(net, dev, MPR_MAP_OUT);
                        mpr_map_send_state((mpr_map)map, -1, MSG_MAPPED);
                    }
                }
            }
        }
        if (MPR_LOC_DST & locality) {
            mpr_slot slot = mpr_map_get_dst_slot((mpr_map)map);
            mpr_sig sig = (mpr_sig)mpr_slot_get_sig_if_local(slot);
            if (sig) {
                mpr_local_dev dev = (mpr_local_dev)mpr_sig_get_dev(sig);
                if (mpr_local_dev_has_subscribers(dev)) {
                    trace_dev(dev, "informing subscribers (MAPPED)\n")
                    mpr_net_use_subscribers(net, dev, MPR_MAP_IN);
                    mpr_map_send_state((mpr_map)map, -1, MSG_MAPPED);
                }
            }
        }
    }

done:
    mpr_msg_free(props);
    mpr_map_clear_empty_props(map);
    return 0;
}

/*! Unmap a set of signals. */
static int handler_unmap(const char *path, const char *types, lo_arg **av,
                         int ac, lo_message msg, void *user)
{
    mpr_graph graph = (mpr_graph)user;
    mpr_net net = mpr_graph_get_net(graph);
    mpr_local_map map;
    mpr_slot slot;
    lo_address addr;
    mpr_sig sig;
    int i, num_src;

    trace_net(net);
    map = (mpr_local_map)find_map(net, types, ac, av, MPR_LOC_ANY, FIND);
    RETURN_ARG_UNLESS(map && MPR_MAP_ERROR != (mpr_map)map, 0);

    num_src = mpr_map_get_num_src((mpr_map)map);

    /* inform remote peer(s) */
    slot = mpr_map_get_dst_slot((mpr_map)map);
    addr = mpr_slot_get_addr(slot);
    /* if destination is remote this guarantees all sources are local */
    if (addr) {
        mpr_net_use_mesh(net, addr);
        mpr_map_send_state((mpr_map)map, -1, MSG_UNMAP);
    }
    else {
        for (i = 0; i < num_src; i++) {
            slot = mpr_map_get_src_slot((mpr_map)map, i);
            addr = mpr_slot_get_addr(slot);
            if (addr) {
                mpr_net_use_mesh(net, addr);
                i = mpr_map_send_state((mpr_map)map, i, MSG_UNMAP);
            }
        }
    }

    for (i = 0; i < num_src; i++) {
        mpr_sig sig = mpr_map_get_src_sig((mpr_map)map, i);
        mpr_local_dev dev = 0;
        if (mpr_obj_get_is_local((mpr_obj)sig)) {
            if (dev != (mpr_local_dev)mpr_sig_get_dev(sig)) {
                dev = (mpr_local_dev)mpr_sig_get_dev(sig);
                inform_device_subscribers(net, dev);

                trace_dev(dev, "informing subscribers (UNMAPPED)\n")
                mpr_net_use_subscribers(net, dev, MPR_MAP_OUT);
                mpr_map_send_state((mpr_map)map, -1, MSG_UNMAPPED);
            }
            trace_dev(dev, "informing subscribers (SIGNAL)\n");
            mpr_net_use_subscribers(net, dev, MPR_SIG);
            mpr_sig_send_state(sig, MSG_SIG);
        }
    }
    sig = mpr_map_get_dst_sig((mpr_map)map);
    if (mpr_obj_get_is_local((mpr_obj)sig)) {
        mpr_local_dev dev = (mpr_local_dev)mpr_sig_get_dev(sig);
        trace_dev(dev, "informing subscribers (SIGNAL)\n");
        mpr_net_use_subscribers(net, dev, MPR_SIG);
        mpr_sig_send_state(sig, MSG_SIG);

        if (MPR_LOC_BOTH != mpr_map_get_locality((mpr_map)map)) {
            inform_device_subscribers(net, dev);

            trace_dev(dev, "informing subscribers (UNMAPPED)\n")
            mpr_net_use_subscribers(net, dev, MPR_MAP_IN);
            mpr_map_send_state((mpr_map)map, -1, MSG_UNMAPPED);
        }
    }

    /* The mapping is removed. */
    mpr_graph_remove_map(graph, (mpr_map)map, MPR_OBJ_REM);
    return 0;
}

/*! Respond to /unmapped by removing map from graph. */
static int handler_unmapped(const char *path, const char *types, lo_arg **av,
                            int ac, lo_message msg, void *user)
{
    mpr_graph graph = (mpr_graph)user;
    mpr_net net = mpr_graph_get_net(graph);
    mpr_map map;

    trace_net(net);
    map = find_map(net, types, ac, av, 0, FIND);
    RETURN_ARG_UNLESS(map && MPR_MAP_ERROR != map, 0);
    mpr_graph_remove_map(graph, map, MPR_OBJ_REM);
    return 0;
}

static int handler_ping(const char *path, const char *types, lo_arg **av,
                        int ac, lo_message msg, void *user)
{
    mpr_graph graph = (mpr_graph)user;
    mpr_net net = mpr_graph_get_net(graph);
    mpr_dev remote_dev;
    mpr_time now;
    lo_timetag then;
    int i;

    RETURN_ARG_UNLESS(net->num_devs, 0);
    trace_net(net);

    mpr_time_set(&now, MPR_NOW);
    then = lo_message_get_timestamp(msg);
    remote_dev = (mpr_dev)mpr_graph_get_obj(graph, av[0]->h, MPR_DEV);
    RETURN_ARG_UNLESS(remote_dev, 0);
    for (i = 0; i < net->num_devs; i++) {
        mpr_local_dev dev = net->devs[i];
        mpr_link link = mpr_dev_get_link_by_remote((mpr_dev)dev, remote_dev);
        if (!link)
            continue;
        trace_dev(dev, "ping received from device '%s'\n", mpr_dev_get_name(remote_dev));
        mpr_link_update_clock(link, then, now, av[1]->i, av[2]->i, av[3]->d);
    }
    return 0;
}

static int handler_sync(const char *path, const char *types, lo_arg **av,
                        int ac, lo_message msg, void *user)
{
    mpr_graph graph = (mpr_graph)user;
    RETURN_ARG_UNLESS(ac && MPR_STR == types[0], 0);
    trace_net(mpr_graph_get_net(graph));
    mpr_graph_sync_dev(graph, &av[0]->s);
    return 0;
}

lo_server *mpr_net_get_servers(mpr_net n)
{
    return n->servers;
}
