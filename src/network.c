#include "config.h"

#include <lo/lo.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <zlib.h>
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

#include "mapper_internal.h"
#include "types_internal.h"
#include "config.h"
#include <mapper/mapper.h>

extern const char* prop_msg_strings[MPR_PROP_EXTRA+1];

#define BUNDLE_DST_SUBSCRIBERS (void*)-1
#define BUNDLE_DST_BUS          0

#define MAX_BUNDLE_LEN 8192
#define FIND 0
#define UPDATE 1
#define ADD 2

static int is_alphabetical(int num, lo_arg **names)
{
    int i;
    RETURN_ARG_UNLESS(num > 1, 1);
    for (i = 1; i < num; i++)
        TRACE_RETURN_UNLESS(strcmp(&names[i-1]->s, &names[i]->s)<0, 0,
                            "error: signal names out of order.");
    return 1;
}

/* Extract the ordinal from a device name in the format: <name>.<n> */
static int extract_ordinal(char *name) {
    int ordinal;
    char *s = name;
    RETURN_ARG_UNLESS(s = strrchr(s, '.'), -1);
    ordinal = atoi(s+1);
    *s = 0;
    return ordinal;
}

MPR_INLINE static void inform_device_subscribers(mpr_net net, mpr_local_dev dev)
{
    if (dev->subscribers) {
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

static int _handler_name(HANDLER_ARGS);

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
    trace_net("[libmapper] liblo server error %d in path %s: %s\n", num, where, msg);
}

/* Functions for handling the resource allocation scheme.  If check_collisions()
 * returns 1, the resource in question should be probed on the libmapper bus. */
static int check_collisions(mpr_net net, mpr_allocated resource);

/*! Local function to get the IP address of a network interface. */
static int get_iface_addr(const char* pref, struct in_addr* addr, char **iface)
{
    struct sockaddr_in *sa;

#ifdef HAVE_GETIFADDRS

    struct in_addr zero;
    struct ifaddrs *ifaphead;
    struct ifaddrs *ifap;
    struct ifaddrs *iflo=0, *ifchosen=0;
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
            && memcmp(&sa->sin_addr, &zero, sizeof(struct in_addr))!=0) {
            ifchosen = ifap;
            if (pref && 0 == strcmp(ifap->ifa_name, pref))
                break;
            else if (ifap->ifa_flags & IFF_LOOPBACK)
                iflo = ifap;
        }
        ifap = ifap->ifa_next;
    }

    /* Default to loopback address in case user is working locally. */
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

#else /* !HAVE_GETIFADDRS */

#ifdef HAVE_LIBIPHLPAPI
    /* TODO consider "pref" as well */

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
    RETURN_ARG_UNLESS(ERROR_SUCCESS == rc, 2);

    PIP_ADAPTER_ADDRESSES loaa=0, aa = paa;
    PIP_ADAPTER_UNICAST_ADDRESS lopua=0;
    while (aa && ERROR_SUCCESS == rc) {
        PIP_ADAPTER_UNICAST_ADDRESS pua = aa->FirstUnicastAddress;
        /* Skip adapters that are not "Up". */
        if (pua && IfOperStatusUp == aa->OperStatus) {
            if (IF_TYPE_SOFTWARE_LOOPBACK == aa->IfType) {
                loaa = aa;
                lopua = pua;
            }
            else {
                /* Skip addresses starting with 0.X.X.X or 169.X.X.X. */
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

static void mpr_net_add_dev_methods(mpr_net net, mpr_local_dev dev)
{
    int i;
    char path[256];
    const char *dname = mpr_dev_get_name((mpr_dev)dev);
    for (i = 0; i < NUM_DEV_HANDLERS_SPECIFIC; i++) {
        snprintf(path, 256, net_msg_strings[dev_handlers_specific[i].str_idx], dname);
        lo_server_add_method((net)->servers[SERVER_BUS], path, dev_handlers_specific[i].types,
                             dev_handlers_specific[i].h, dev);
        lo_server_add_method((net)->servers[SERVER_MESH], path, dev_handlers_specific[i].types,
                             dev_handlers_specific[i].h, dev);
    }
    if (net->generic_dev_methods_added)
        return;
    for (i = 0; i < NUM_DEV_HANDLERS_GENERIC; i++) {
        lo_server_add_method((net)->servers[SERVER_BUS],
                             net_msg_strings[dev_handlers_generic[i].str_idx],
                             dev_handlers_generic[i].types,
                             dev_handlers_generic[i].h, dev->obj.graph);
        lo_server_add_method((net)->servers[SERVER_MESH],
                             net_msg_strings[dev_handlers_generic[i].str_idx],
                             dev_handlers_generic[i].types,
                             dev_handlers_generic[i].h, dev->obj.graph);
        net->generic_dev_methods_added = 1;
    }
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
        trace_net("error in mpr_net_remove_dev: device not found in local list\n");
        return;
    }
    --net->num_devs;
    for (; i < net->num_devs; i++)
        net->devs[i] = net->devs[i + 1];
    net->devs = realloc(net->devs, net->num_devs * sizeof(mpr_local_dev));

    for (i = 0; i < NUM_DEV_HANDLERS_SPECIFIC; i++) {
        snprintf(path, 256, net_msg_strings[dev_handlers_specific[i].str_idx],
                 mpr_dev_get_name((mpr_dev)dev));
        lo_server_del_method((net)->servers[SERVER_BUS], path, dev_handlers_specific[i].types);
        lo_server_del_method((net)->servers[SERVER_MESH], path, dev_handlers_specific[i].types);
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
            lo_server_del_method((net)->servers[SERVER_BUS],
                                 net_msg_strings[dev_handlers_generic[i].str_idx],
                                 dev_handlers_generic[i].types);
            lo_server_del_method((net)->servers[SERVER_MESH],
                                 net_msg_strings[dev_handlers_generic[i].str_idx],
                                 dev_handlers_generic[i].types);
        }
    }
}

void mpr_net_add_graph_methods(mpr_net net)
{
    /* add graph methods */
    int i;
    for (i = 0; i < NUM_GRAPH_HANDLERS; i++) {
        lo_server_add_method((net)->servers[SERVER_BUS], net_msg_strings[graph_handlers[i].str_idx],
                             graph_handlers[i].types, graph_handlers[i].h, net->graph);
        lo_server_add_method((net)->servers[SERVER_MESH], net_msg_strings[graph_handlers[i].str_idx],
                             graph_handlers[i].types, graph_handlers[i].h, net->graph);
    }
    return;
}

void mpr_net_init(mpr_net net, const char *iface, const char *group, int port)
{
    int i;

    /* Default standard ip and port is group 224.0.1.3, port 7570 */
    char port_str[10], *s_port = port_str;

    /* send out any cached messages */
    mpr_net_send(net);

    if (net->multicast.group) {
        if (group && strcmp(group, net->multicast.group)) {
            free(net->multicast.group);
            net->multicast.group = strdup(group);
        }
    }
    else
        net->multicast.group = strdup(group ? group : "224.0.1.3");
    if (!net->multicast.port)
        net->multicast.port = port ? port : 7570;
    snprintf(port_str, 10, "%d", net->multicast.port);

    /* Initialize interface information. */
    if (!net->iface.name || (iface && strcmp(iface, net->iface.name)))
        get_iface_addr(iface, &net->iface.addr, &net->iface.name);
    trace_net("found interface: %s\n", net->iface.name ? net->iface.name : "none");

    /* Remove existing structures if necessary */
    FUNC_IF(lo_address_free, net->addr.bus);
    FUNC_IF(lo_server_free, net->servers[SERVER_BUS]);
    FUNC_IF(lo_server_free, net->servers[SERVER_MESH]);

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
    net->servers[SERVER_BUS] = lo_server_new_multicast_iface(net->multicast.group, s_port,
                                                             net->iface.name, 0, handler_error);

    if (!net->servers[SERVER_BUS]) {
        lo_address_free(net->addr.bus);
        trace_net("problem allocating bus server.\n");
        return;
    }
    else
        trace_net("bus connected to %s:%s\n", net->multicast.group, s_port);

    /* Also open address/server for mesh-style communications */
    /* TODO: use TCP instead? */
    while (!(net->servers[SERVER_MESH] = lo_server_new(0, handler_error))) {}

    /* Disable liblo message queueing. */
    lo_server_enable_queue((net)->servers[SERVER_BUS], 0, 1);
    lo_server_enable_queue((net)->servers[SERVER_MESH], 0, 1);

    mpr_net_add_graph_methods(net);

    for (i = 0; i < net->num_devs; i++)
        mpr_net_add_dev(net, net->devs[i]);
}

const char *mpr_get_version()
{
    return PACKAGE_VERSION;
}

void mpr_net_send(mpr_net net)
{
    RETURN_UNLESS(net->bundle);

    if (BUNDLE_DST_SUBSCRIBERS == net->addr.dst) {
        mpr_subscriber *sub = &net->addr.dev->subscribers;
        mpr_time t;
        if (*sub)
            mpr_time_set(&t, MPR_NOW);
        while (*sub) {
            if ((*sub)->lease_exp < t.sec || !(*sub)->flags) {
                /* subscription expired, remove from subscriber list */
#ifdef DEBUG
                char *addr = lo_address_get_url((*sub)->addr);
                trace_dev(net->addr.dev, "removing expired subscription from %s\n", addr);
                free(addr);
#endif
                mpr_subscriber temp = *sub;
                *sub = temp->next;
                FUNC_IF(lo_address_free, temp->addr);
                free(temp);
                continue;
            }
            if ((*sub)->flags & net->msg_type)
                lo_send_bundle_from((*sub)->addr, net->servers[SERVER_MESH], net->bundle);
            sub = &(*sub)->next;
        }
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
    FUNC_IF(lo_server_free, net->servers[SERVER_UDP]);
    FUNC_IF(lo_server_free, net->servers[SERVER_TCP]);
    FUNC_IF(lo_address_free, net->addr.bus);
    FUNC_IF(free, net->addr.url);
    FUNC_IF(free, net->rtr);
}

/*! Probe the network to see if a device's proposed name.ordinal is available. */
static void mpr_net_probe_dev_name(mpr_net net, mpr_local_dev dev)
{
    int i;
    char name[256];

    /* reset collisions and hints */
    dev->ordinal_allocator.collision_count = 0;
    dev->ordinal_allocator.count_time = mpr_get_current_time();
    for (i = 0; i < 8; i++)
        dev->ordinal_allocator.hints[i] = 0;

    /* Note: mpr_dev_get_name() would refuse here since the ordinal is not
     * yet locked, so we have to build it manually at this point. */
    snprintf(name, 256, "%s.%d", dev->prefix, dev->ordinal_allocator.val);
    trace_dev(dev, "probing name '%s'\n", name);

    /* Calculate an id from the name and store it in id.val */
    dev->obj.id = (mpr_id) crc32(0L, (const Bytef *)name, strlen(name)) << 32;

    /* For the same reason, we can't use mpr_net_send() here. */
    lo_send(net->addr.bus, net_msg_strings[MSG_NAME_PROBE], "si", name, net->random_id);
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
        dev->registered = 0;
        dev->ordinal_allocator.val = i;
    }
    else {
        /* Initialize data structures */
        net->devs = realloc(net->devs, (net->num_devs + 1) * sizeof(mpr_local_dev));
        net->devs[net->num_devs] = dev;
        ++net->num_devs;
        dev->ordinal_allocator.val = net->num_devs;
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
    mpr_net_probe_dev_name(net, dev);
}

static void _send_device_sync(mpr_net net, mpr_local_dev dev)
{
    NEW_LO_MSG(msg, return);
    lo_message_add_string(msg, mpr_dev_get_name((mpr_dev)dev));
    lo_message_add_int32(msg, dev->obj.version);
    mpr_net_add_msg(net, 0, MSG_SYNC, msg);
}

/* TODO: rename to mpr_dev...? */
static void mpr_net_maybe_send_ping(mpr_net net, int force)
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
            if (dev->subscribers) {
                mpr_net_use_subscribers(net, dev, MPR_DEV);
                _send_device_sync(net, dev);
            }
        }
    }
    RETURN_UNLESS(net->num_devs);
    if (!force && (now.sec < net->next_bus_ping))
        return;
    net->next_bus_ping = now.sec + 5 + (rand() % 4);

    mpr_net_use_bus(net);
    for (i = 0; i < net->num_devs; i++) {
        if (net->devs[i]->registered)
            _send_device_sync(net, net->devs[i]);
    }

    /* housekeeping #2: periodically check if our links are still active */
    list = mpr_list_from_data(gph->links);
    while (list) {
        int num_maps;
        mpr_sync_clock clk;
        double elapsed;
        mpr_link lnk = (mpr_link)*list;
        list = mpr_list_get_next(list);
        num_maps = lnk->num_maps[0] + lnk->num_maps[1];
        clk = &lnk->clock;
        elapsed = (clk->rcvd.time.sec ? mpr_time_get_diff(now, clk->rcvd.time) : 0);
        if (elapsed > TIMEOUT_SEC) {
            if (clk->rcvd.msg_id > 0) {
                if (num_maps)
                    trace_dev(lnk->devs[LOCAL_DEV], "Lost contact with linked device '%s' "
                              "(%g seconds since sync).\n", lnk->devs[REMOTE_DEV]->name, elapsed);
                /* tentatively mark link as expired */
                clk->rcvd.msg_id = -1;
                clk->rcvd.time.sec = now.sec;
            }
            else {
                if (num_maps) {
                    trace_dev(lnk->devs[LOCAL_DEV], "Removing link to unresponsive device '%s' "
                              "(%g seconds since warning).\n", lnk->devs[REMOTE_DEV]->name, elapsed);
                    /* TODO: release related maps, call local handlers
                     * and inform subscribers. */
                }
                else
                    trace_dev(lnk->devs[LOCAL_DEV], "Removing link to device '%s'.\n",
                              lnk->devs[REMOTE_DEV]->name);
                /* remove related data structures */
                mpr_rtr_remove_link(net->rtr, lnk);
                mpr_graph_remove_link(gph, lnk, num_maps ? MPR_OBJ_EXP : MPR_OBJ_REM);
                continue;
            }
        }
        if (lnk->is_local_only)
            continue;
        if (num_maps && mpr_obj_get_prop_as_str(&lnk->devs[REMOTE_DEV]->obj, MPR_PROP_HOST, 0)) {
            /* Only send pings if this link has associated maps, ensuring empty
             * links are removed after the ping timeout. */
            lo_bundle bun = lo_bundle_new(now);
            NEW_LO_MSG(msg, ;);
            lo_message_add_int64(msg, lnk->devs[LOCAL_DEV]->obj.id);
            if (++clk->sent.msg_id < 0)
                clk->sent.msg_id = 0;
            lo_message_add_int32(msg, clk->sent.msg_id);
            lo_message_add_int32(msg, clk->rcvd.msg_id);
            lo_message_add_double(msg, elapsed);
            /* need to send immediately */
            lo_bundle_add_message(bun, net_msg_strings[MSG_PING], msg);
            lo_send_bundle_from(lnk->addr.admin, net->servers[SERVER_MESH], bun);
            mpr_time_set(&clk->sent.time, lo_bundle_get_timestamp(bun));
            lo_bundle_free_recursive(bun);
        }
    }
}

/*! This is the main function to be called once in a while from a program so
 *  that the libmapper bus can be automatically managed. */
void mpr_net_poll(mpr_net net)
{
    int i, registered = 0;

    /* send out any cached messages */
    mpr_net_send(net);

    if (!net->num_devs) {
        mpr_net_maybe_send_ping(net, 0);
        return;
    }

    /* If the ordinal is not yet locked, process collision timing.
     * Once the ordinal is locked it won't change. */
    for (i = 0; i < net->num_devs; i++) {
        mpr_local_dev dev = net->devs[i];
        if (!dev->registered) {
            /* If the ordinal has changed, re-probe the new name. */
            if (1 == check_collisions(net, &dev->ordinal_allocator))
                mpr_net_probe_dev_name(net, dev);

            /* If we are ready to register the device, add the message handlers. */
            if (dev->ordinal_allocator.locked) {
                mpr_dev_on_registered(dev);

                /* Send registered msg. */
                lo_send(net->addr.bus, net_msg_strings[MSG_NAME_REG], "s",
                        mpr_dev_get_name((mpr_dev)dev));

                mpr_net_add_dev_methods(net, dev);
                mpr_net_maybe_send_ping(net, 1);
                trace_dev(dev, "registered.\n");

                /* Send out any cached maps. */
                mpr_net_use_bus(&dev->obj.graph->net);
                mpr_dev_send_maps(dev, MPR_DIR_ANY, MSG_MAP);
                mpr_net_send(&dev->obj.graph->net);
            }
        }
        else
            ++registered;
    }
    if (registered) {
        /* Send out clock sync messages occasionally */
        mpr_net_maybe_send_ping(net, 0);
    }
    return;
}

/*! Algorithm for checking collisions and allocating resources. */
static int check_collisions(mpr_net net, mpr_allocated resource)
{
    int i;
    double current_time, timediff;
    RETURN_ARG_UNLESS(!resource->locked, 0);
    current_time = mpr_get_current_time();
    timediff = current_time - resource->count_time;

    if (!resource->online) {
        if (timediff >= 5.0) {
            /* reprobe with the same value */
            resource->count_time = current_time;
            return 1;
        }
        return 0;
    }
    else if (timediff >= 2.0 && resource->collision_count < 2) {
        resource->locked = 1;
        if (resource->on_lock)
            resource->on_lock(resource);
        return 2;
    }
    else if (timediff >= 0.5 && resource->collision_count > 1) {
        for (i = 0; i < 8; i++) {
            if (!resource->hints[i])
                break;
        }
        resource->val += i + (rand() % net->num_devs);

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
    mpr_graph gph = (mpr_graph)user;
    mpr_net net = &gph->net;
    RETURN_ARG_UNLESS(net->devs, 0);
    trace_net("received /who\n");
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
    int i, j, data_port, found;
    mpr_msg props = 0;
    mpr_msg_atom atom;
    mpr_list links = 0, cpy;
    const char *name, *host, *admin_port;
    lo_address a;
    mpr_rtr_sig rs;

    RETURN_ARG_UNLESS(ac && MPR_STR == types[0], 0);
    net = &graph->net;
    name = &av[0]->s;

    if (graph->autosub || mpr_graph_subscribed_by_dev(graph, name)) {
        props = mpr_msg_parse_props(ac-1, &types[1], &av[1]);
#ifdef DEBUG
        trace_net("received /device ");
        lo_message_pp(msg);
#endif
        remote = mpr_graph_add_dev(graph, name, props);
        if (!remote->subscribed && graph->autosub)
            mpr_graph_subscribe(graph, remote, graph->autosub, -1);
    }
    if (!net->devs)
        goto done;
    for (i = 0; i < net->num_devs; i++) {
        if (!mpr_dev_get_is_ready((mpr_dev)net->devs[i]))
            continue;
        if (0 == strcmp(&av[0]->s, mpr_dev_get_name((mpr_dev)net->devs[i])))
            break;
    }
    TRACE_NET_RETURN_UNLESS(i == net->num_devs, 0, "ignoring /device message from self\n");
    trace_net("received /device %s\n", &av[0]->s);

    /* Discover whether the device is linked. */
    remote = mpr_graph_get_dev_by_name(graph, name);
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

    a = lo_message_get_source(msg);
    if (!a) {
        trace_net("can't perform /linkTo, address unknown\n");
        goto done;
    }
    /* Find the sender's hostname */
    host = lo_address_get_hostname(a);
    admin_port = lo_address_get_port(a);
    if (!host) {
        trace_net("can't perform /linkTo, host unknown\n");
        goto done;
    }
    /* Retrieve the port */
    if (!props)
        props = mpr_msg_parse_props(ac-1, &types[1], &av[1]);
    atom = mpr_msg_get_prop(props, MPR_PROP_PORT);
    if (!atom || atom->len != 1 || atom->types[0] != MPR_INT32) {
        trace_net("can't perform /linkTo, port unknown\n");
        goto done;
    }
    data_port = (atom->vals[0])->i;

    cpy = mpr_list_get_cpy(links);
    found = 0;
    while (cpy) {
        mpr_link link = (mpr_link)*cpy;
        cpy = mpr_list_get_next(cpy);
        if (mpr_link_get_is_local(link)) {
            trace_net("establishing link to %s.\n", name)
            mpr_link_connect(link, host, atoi(admin_port), data_port);
            found = 1;
            break;
        }
    }
    if (!found)
        goto done;

    /* check if we have maps waiting for this link */
    trace_net("checking for waiting maps.\n");
    rs = net->rtr->sigs;
    while (rs) {
        for (i = 0; i < rs->num_slots; i++) {
            mpr_local_map map;
            if (!rs->slots[i])
                continue;
            map = (mpr_local_map)rs->slots[i]->map;
            if (MPR_DIR_OUT == rs->slots[i]->dir) {
                /* only send /mapTo once even if we have multiple local sources */
                if (map->one_src && (rs->slots[i] != map->src[0]))
                    continue;
                cpy = mpr_list_get_cpy(links);
                while (cpy) {
                    mpr_link link = (mpr_link)*cpy;
                    cpy = mpr_list_get_next(cpy);
                    if (mpr_link_get_is_local(link) && map->dst->link == link) {
                        mpr_net_use_mesh(net, link->addr.admin);
                        mpr_map_send_state((mpr_map)map, -1, MSG_MAP_TO);
                        for (j = 0; j < map->num_src; j++) {
                            if (!map->src[j]->sig->is_local)
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
                        j = mpr_map_send_state((mpr_map)map, map->one_src ? -1 : j, MSG_MAP_TO);
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
    mpr_local_dev dev = (mpr_local_dev)user;
    mpr_msg props;

    RETURN_ARG_UNLESS(dev && mpr_dev_get_is_ready((mpr_dev)dev)
                      && ac >= 2 && MPR_STR == types[0], 0);
    props = mpr_msg_parse_props(ac, types, av);
    trace_dev(dev, "received /%s/modify + %d properties.\n", path, props->num_atoms);
    if (mpr_dev_set_from_msg((mpr_dev)dev, props)) {
        inform_device_subscribers(&dev->obj.graph->net, dev);
        mpr_tbl_clear_empty(dev->obj.props.synced);
    }
    mpr_msg_free(props);
    return 0;
}

/*! Respond to /logout by deleting record of device. */
static int handler_logout(const char *path, const char *types, lo_arg **av,
                          int ac, lo_message msg, void *user)
{
    mpr_graph gph = (mpr_graph)user;
    mpr_net net = &gph->net;
    mpr_local_dev dev;
    mpr_dev remote;
    mpr_link lnk;

    RETURN_ARG_UNLESS(ac && MPR_STR == types[0], 0);

    remote = mpr_graph_get_dev_by_name(gph, &av[0]->s);
    trace_net("received /logout '%s'\n", &av[0]->s);

    if (net->num_devs) {
        int i = 0, diff, ordinal;
        char *prefix_str, *ordinal_str;

        /* Parse the ordinal from name in the format: <name>.<n> */
        prefix_str = &av[0]->s;
        ordinal_str = strrchr(&av[0]->s, '.');
        TRACE_RETURN_UNLESS(ordinal_str && isdigit(ordinal_str[1]), 0, "Malformed device name.\n");
        *ordinal_str = '\0';
        ordinal = atoi(++ordinal_str);

        for (i = 0; i < net->num_devs; i++) {
            dev = net->devs[i];
            if (!dev->ordinal_allocator.locked)
                continue;
            /* Check if we have any links to this device, if so remove them */
            if (remote && (lnk = mpr_dev_get_link_by_remote(dev, remote))) {
                /* TODO: release maps, call local handlers and inform subscribers */
                trace_dev(dev, "removing link to expired device '%s'.\n", remote->name);
                mpr_rtr_remove_link(net->rtr, lnk);
                mpr_graph_remove_link(gph, lnk, MPR_OBJ_REM);
            }
            if (0 == strcmp(prefix_str, dev->prefix)) {
                /* If device name matches and ordinal is within my block, free it */
                diff = ordinal - dev->ordinal_allocator.val - 1;
                if (diff >= 0 && diff < 8)
                    dev->ordinal_allocator.hints[diff] = 0;
            }
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
    mpr_local_dev dev = (mpr_local_dev)user;
    int i, version = -1, flags = 0, timeout_seconds = 0;

#ifdef DEBUG
    trace_dev(dev, "received /subscribe ");
    lo_message_pp(msg);
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

    RETURN_ARG_UNLESS(ac >= 2 && MPR_STR == types[0], 1);
    full_sig_name = &av[0]->s;
    devnamelen = mpr_parse_names(full_sig_name, &devnamep, &signamep);
    RETURN_ARG_UNLESS(devnamep && signamep, 0);

    props = mpr_msg_parse_props(ac-1, &types[1], &av[1]);
    devnamep[devnamelen] = 0;

#ifdef DEBUG
    trace_graph("received /signal %s:%s\n", devnamep, signamep);
#endif

    mpr_graph_add_sig(gph, signamep, devnamep, props);
    devnamep[devnamelen] = '/';
    mpr_msg_free(props);

#ifdef DEBUG
    lo_message_pp(msg);
#endif

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
    mpr_net net = &dev->obj.graph->net;
    mpr_sig sig;
    mpr_msg props;
    RETURN_ARG_UNLESS(dev && mpr_dev_get_is_ready((mpr_dev)dev) && ac > 1 && MPR_STR == types[0], 0);

    /* retrieve signal */
    sig = mpr_dev_get_sig_by_name((mpr_dev)dev, &av[0]->s);
    TRACE_DEV_RETURN_UNLESS(sig, 0, "no signal found with name '%s'.\n", &av[0]->s);

    props = mpr_msg_parse_props(ac-1, &types[1], &av[1]);
    trace_dev(dev, "received %s '%s' + %d properties.\n", path, sig->name, props->num_atoms);

    if (mpr_sig_set_from_msg(sig, props)) {
        if (dev->subscribers) {
            int dir = (MPR_DIR_IN == sig->dir) ? MPR_SIG_IN : MPR_SIG_OUT;
            trace_dev(dev, "informing subscribers (SIGNAL)\n");
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
    mpr_graph gph = (mpr_graph)user;
    mpr_dev dev;
    char *full_sig_name, *signamep, *devnamep, devname[1024];
    int devnamelen;

    RETURN_ARG_UNLESS(ac && MPR_STR == types[0], 1);
    full_sig_name = &av[0]->s;
    devnamelen = mpr_parse_names(full_sig_name, &devnamep, &signamep);
    RETURN_ARG_UNLESS(devnamep && signamep && devnamelen < 1024, 0);

    strncpy(devname, devnamep, devnamelen);
    devname[devnamelen]=0;

    trace_graph("received /signal/removed %s:%s\n", devname, signamep);

    dev = mpr_graph_get_dev_by_name(gph, devname);
    if (dev && !dev->is_local)
        mpr_graph_remove_sig(gph, mpr_dev_get_sig_by_name(dev, signamep), MPR_OBJ_REM);
    return 0;
}

/*! Repond to name collisions during allocation, help suggest IDs once allocated. */
static int handler_name(const char *path, const char *types, lo_arg **av,
                        int ac, lo_message msg, void *user)
{
    mpr_net net = (mpr_net)user;
    int i;

    RETURN_ARG_UNLESS(ac && MPR_STR == types[0], 0);

    for (i = 0; i < net->num_devs; i++)
        _handler_name(path, types, av, ac, msg, net->devs[i]);

    return 0;
}

/* hander_name and handler_name_probe are actually device callbacks,
 * but because all devices need to receive these messages on the same OSC
 * address, we have to dispatch manually */
static int _handler_name(const char *path, const char *types, lo_arg **av,
                        int ac, lo_message msg, void *user)
{
    mpr_local_dev dev = (mpr_local_dev)user;
    mpr_net net = &dev->obj.graph->net;
    int ordinal, diff, temp_id = -1, hint = 0;
    char *name;

    name = &av[0]->s;
    if (ac > 1) {
        if (MPR_INT32 == types[1])
            temp_id = av[1]->i;
        if (MPR_INT32 == types[2])
            hint = av[2]->i;
    }

#ifdef DEBUG
    if (hint)
        {trace_dev(dev, "received name %s %i %i\n", name, temp_id, hint);}
    else
        {trace_dev(dev, "received name %s\n", name);}
#endif

    if (dev->ordinal_allocator.locked) {
        ordinal = extract_ordinal(name);
        RETURN_ARG_UNLESS(ordinal >= 0, 0);

        /* If device name matches */
        if (0 == strcmp(name, dev->prefix)) {
            /* if id is locked and registered id is within my block, store it */
            diff = ordinal - dev->ordinal_allocator.val - 1;
            if (diff >= 0 && diff < 8)
                dev->ordinal_allocator.hints[diff] = -1;
            if (hint) {
                /* if suggested id is within my block, store timestamp */
                diff = hint - dev->ordinal_allocator.val - 1;
                if (diff >= 0 && diff < 8)
                    dev->ordinal_allocator.hints[diff] = mpr_get_current_time();
            }
        }
    }
    else {
        mpr_id id = (mpr_id) crc32(0L, (const Bytef *)name, strlen(name)) << 32;
        if (id == dev->obj.id) {
            if (temp_id < net->random_id) {
                /* Count ordinal collisions. */
                ++dev->ordinal_allocator.collision_count;
                dev->ordinal_allocator.count_time = mpr_get_current_time();
            }
            else if (temp_id == net->random_id && hint > 0 && hint != dev->ordinal_allocator.val) {
                dev->ordinal_allocator.val = hint;
                mpr_net_probe_dev_name(net, dev);
            }
        }
    }
    return 0;
}

static int _handler_name_probe(mpr_net net, mpr_local_dev dev, char *name, int temp_id, mpr_id id)
{
    int i;
    double current_time;
    if (id != dev->obj.id)
        return 0;

    trace_dev(dev, "name probe match %s %i \n", name, temp_id);
    current_time = mpr_get_current_time();
    if (dev->ordinal_allocator.locked || temp_id > net->random_id) {
        for (i = 0; i < 8; i++) {
            if (dev->ordinal_allocator.hints[i] >= 0
                && (current_time - dev->ordinal_allocator.hints[i]) > 2.0) {
                /* reserve suggested ordinal */
                dev->ordinal_allocator.hints[i] = current_time;
                break;
            }
        }
        /* Name may not yet be registered, so we can't use mpr_net_send(). */
        lo_send(net->addr.bus, net_msg_strings[MSG_NAME_REG], "sii", name,
                temp_id, dev->ordinal_allocator.val + i + 1);
    }
    else {
        dev->ordinal_allocator.collision_count += 1;
        dev->ordinal_allocator.count_time = current_time;
        if (temp_id == net->random_id)
            dev->ordinal_allocator.online = 1;
    }
    return 0;
}

/*! Respond to name probes during allocation, help suggest names once allocated. */
static int handler_name_probe(const char *path, const char *types, lo_arg **av,
                              int ac, lo_message msg, void *user)
{
    mpr_net net = (mpr_net)user;
    char *name = &av[0]->s;
    int i, temp_id = av[1]->i;
    mpr_id id = (mpr_id) crc32(0L, (const Bytef *)name, strlen(name)) << 32;

    trace_net("received name probe %s %i \n", name, temp_id);

    for (i = 0; i < net->num_devs; i++)
        _handler_name_probe(net, net->devs[i], name, temp_id, id);

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

static mpr_map find_map(mpr_net net, const char *types, int ac, lo_arg **av,
                        mpr_loc loc, mpr_sig *sig_ptr, int flags)
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
        id = av[i]->i64;
        map = (mpr_map)mpr_graph_get_obj(net->graph, MPR_MAP, id);
        trace_graph("%s map with id %"PR_MPR_ID"\n", map ? "found" : "couldn't find", id);
        if (map) {
#ifdef DEBUG
            trace_graph("  %s", map->num_src > 1 ? "[" : "");
            for (i = 0; i < map->num_src; i++)
                printf("'%s:%s'%s, ", map->src[i]->sig->dev->name, map->src[i]->sig->name,
                       map->src[i]->sig->is_local ? "*" : "");
            printf("\b\b%s -> '%s:%s'%s\n", map->num_src > 1 ? "]" : "", map->dst->sig->dev->name,
                   map->dst->sig->name, map->dst->sig->is_local ? "*" : "");
#endif
            is_loc = mpr_obj_get_prop_as_int32((mpr_obj)map, MPR_PROP_IS_LOCAL, NULL);
            RETURN_ARG_UNLESS(!loc || is_loc, MPR_MAP_ERROR);
            if (map->num_src < num_src && (flags & UPDATE)) {
                trace_graph("adding additional sources to map.\n");
                /* add additional sources */
                for (i = 0; i < num_src; i++)
                    src_names[i] = &av[src_idx+i]->s;
                map = mpr_graph_add_map(net->graph, id, num_src, src_names, &av[dst_idx]->s);
            }
            return map;
        }
        else if (!flags)
            return 0;
    }

    /* try signal names instead */
    dst_name = &av[dst_idx]->s;
    for (i = 0; i < num_src; i++)
        src_names[i] = &av[src_idx+i]->s;

    if (MPR_LOC_DST & loc) {
        /* check if we are the destination */
        for (i = 0; i < net->num_devs; i++) {
            mpr_local_dev dev = net->devs[i];
            if (!dev->registered)
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
                if (!dev->registered)
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
    trace_graph("%s map with src name%s", map ? "found" : "couldn't find",
                num_src > 1 ? "s: [" : ": ");
    for (i = 0; i < num_src; i++)
        printf("'%s', ", &av[src_idx+i]->s);
    printf("\b\b%s and dst name '%s'\n", num_src > 1 ? "]" : "", &av[dst_idx]->s);
#endif
    if (!map && (flags & ADD)) {
        /* safety check: make sure we don't have an outgoing map to src (loop) */
        if (sig && mpr_rtr_loop_check(net->rtr, (mpr_local_sig)sig, num_src, src_names)) {
            trace("error in /map: potential loop detected.")
            return MPR_MAP_ERROR;
        }
        map = mpr_graph_add_map(net->graph, id, num_src, src_names, &av[dst_idx]->s);
    }
    if (sig_ptr)
        *sig_ptr = sig;
    return map;
}


/*! When the /map message is received by the destination device, send a /mapTo
 *  message to the source device.
 */
static int handler_map(const char *path, const char *types, lo_arg **av, int ac,
                       lo_message msg, void *user)
{
    mpr_graph gph = (mpr_graph)user;
    mpr_net net = &gph->net;
    mpr_local_dev dev;
    mpr_sig sig = 0;
    mpr_local_map map;
    mpr_msg props;
    int i;

    RETURN_ARG_UNLESS(net->num_devs, 0);
    map = (mpr_local_map)find_map(net, types, ac, av, MPR_LOC_DST, &sig, ADD | UPDATE);
    RETURN_ARG_UNLESS(map && MPR_MAP_ERROR != (mpr_map)map, 0);

#ifdef DEBUG
    trace_dev(map->dst->sig->dev, "received /map ");
    lo_message_pp(msg);
#endif

    if (map->status >= MPR_STATUS_ACTIVE) {
        /* Forward to handler_map_mod() and stop. */
        handler_map_mod(path, types, av, ac, msg, user);
        return 0;
    }
    mpr_rtr_add_map(net->rtr, map);
    dev = (mpr_local_dev)map->dst->sig->dev;

    props = mpr_msg_parse_props(ac, types, av);
    mpr_map_set_from_msg((mpr_map)map, props, 1);
    mpr_msg_free(props);

    if (map->is_local_only && map->expr) {
        trace_dev(dev, "map references only local signals... activating.\n");
        map->status = MPR_STATUS_ACTIVE;

        /* Inform subscribers */
        if (dev->subscribers) {
            inform_device_subscribers(net, dev);

            trace_dev(dev, "informing subscribers (SIGNAL)\n")
            mpr_net_use_subscribers(net, dev, MPR_SIG);
            for (i = 0; i < map->num_src; i++)
                mpr_sig_send_state(map->src[i]->sig, MSG_SIG);
            mpr_sig_send_state(map->dst->sig, MSG_SIG);

            trace_dev(dev, "informing subscribers (MAPPED)\n")
            mpr_net_use_subscribers(net, dev, MPR_MAP);
            mpr_map_send_state((mpr_map)map, -1, MSG_MAPPED);
        }
        return 0;
    }

    for (i = 0; i < map->num_src; i++) {
        /* do not send if is local mapping */
        if (map->src[i]->rsig)
            continue;
        /* do not send if device host/port not yet known */
        if (!map->src[i]->link || !map->src[i]->link->addr.admin) {
            trace_dev(dev, "delaying map handshake while waiting for network link.\n");
            continue;
        }
        mpr_net_use_mesh(net, map->src[i]->link->addr.admin);
        mpr_sig_send_state(sig, MSG_SIG);
        i = mpr_map_send_state((mpr_map)map, map->one_src ? -1 : i, MSG_MAP_TO);
    }
    ++net->graph->staged_maps;
    return 0;
}

/*! When the /mapTo message is received by a peer device, create a tentative
 *  map and respond with own signal metadata. */
static int handler_map_to(const char *path, const char *types, lo_arg **av,
                          int ac, lo_message msg, void *user)
{
    mpr_graph gph = (mpr_graph)user;
    mpr_net net = &gph->net;
#ifdef DEBUG
    trace_net("received /map_to ");
    lo_message_pp(msg);
#endif

    mpr_local_map map = (mpr_local_map)find_map(net, types, ac, av, MPR_LOC_ANY, 0, ADD | UPDATE);
    RETURN_ARG_UNLESS(map && MPR_MAP_ERROR != (mpr_map)map, 0);
    mpr_rtr_add_map(net->rtr, map);

    if (map->status < MPR_STATUS_ACTIVE) {
        /* Set map properties. */
        mpr_msg props = mpr_msg_parse_props(ac, types, av);
        mpr_map_set_from_msg((mpr_map)map, props, 1);
        mpr_msg_free(props);
    }

    if (map->status >= MPR_STATUS_READY) {
        int i;
        if (MPR_DIR_OUT == map->dst->dir) {
            mpr_net_use_mesh(net, map->dst->link->addr.admin);
            mpr_map_send_state((mpr_map)map, -1, MSG_MAPPED);
            for (i = 0; i < map->num_src; i++) {
                if (!map->src[i]->sig->is_local)
                    continue;
                mpr_sig_send_state(map->src[i]->sig, MSG_SIG);
            }
        }
        else {
            for (i = 0; i < map->num_src; i++) {
                mpr_net_use_mesh(net, map->src[i]->link->addr.admin);
                i = mpr_map_send_state((mpr_map)map, map->one_src ? -1 : i, MSG_MAPPED);
                mpr_sig_send_state(map->dst->sig, MSG_SIG);
            }
        }
    }
    ++net->graph->staged_maps;
    return 0;
}

/*! Respond to /mapped by storing a map in the graph. Also used by devices to
 *  confirm connection to remote peers, and to share property changes. */
static int handler_mapped(const char *path, const char *types, lo_arg **av,
                          int ac, lo_message msg, void *user)
{
    mpr_graph gph = (mpr_graph)user;
    mpr_net net = &gph->net;
    mpr_map map;
    mpr_msg props;
    int i, rc = 0, updated;

#ifdef DEBUG
    trace_net("received /mapped ");
    lo_message_pp(msg);
#endif

    map = find_map(net, types, ac, av, 0, 0, UPDATE);
    RETURN_ARG_UNLESS(MPR_MAP_ERROR != map, 0);
    if (!map) {
        int store = 0, i = 0;
        if (gph->autosub & MPR_MAP)
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
            map = find_map(net, types, ac, av, 0, 0, ADD);
            rc = 1;
        }
        RETURN_ARG_UNLESS(map && MPR_MAP_ERROR != map, 0);
    }
    else if (map->is_local && ((mpr_local_map)map)->is_local_only) {
        /* no need to update since all properties are local */
        return 0;
    }
    props = mpr_msg_parse_props(ac, types, av);

    /* TODO: if this endpoint is map admin, do not allow overwriting props */
    updated = mpr_map_set_from_msg(map, props, 0);
    mpr_msg_free(props);
#ifdef DEBUG
    trace_net("updated %d map properties. (1)\n", updated);
#endif
    if (map->is_local) {
        RETURN_ARG_UNLESS(map->status >= MPR_STATUS_READY, 0);
        if (MPR_STATUS_READY == map->status) {
            map->status = MPR_STATUS_ACTIVE;
            rc = 1;

            if (MPR_DIR_OUT == map->dst->dir) {
                /* Inform remote destination */
                mpr_net_use_mesh(net, map->dst->link->addr.admin);
                mpr_map_send_state(map, -1, MSG_MAPPED);
            }
            else {
                /* Inform remote sources */
                for (i = 0; i < map->num_src; i++) {
                    mpr_net_use_mesh(net, map->src[i]->link->addr.admin);
                    i = mpr_map_send_state(map, ((mpr_local_map)map)->one_src ? -1 : i, MSG_MAPPED);
                }
            }

            /* TODO: don't send same data multiple times */
            for (i = 0; i < map->num_src; i++) {
                if (map->src[i]->sig->is_local) {
                    mpr_local_dev dev = (mpr_local_dev)map->src[i]->sig->dev;
                    inform_device_subscribers(net, dev);
                    trace_dev(dev, "informing subscribers (SIGNAL)\n");
                    mpr_net_use_subscribers(net, dev, MPR_SIG);
                    mpr_sig_send_state(map->src[i]->sig, MSG_SIG);
                }
            }
            if (map->dst->sig->is_local) {
                mpr_local_dev dev = (mpr_local_dev)map->dst->sig->dev;
                inform_device_subscribers(net, dev);
                trace_dev(dev, "informing subscribers (SIGNAL)\n");
                mpr_net_use_subscribers(net, dev, MPR_SIG);
                mpr_sig_send_state(map->dst->sig, MSG_SIG);
            }
        }
    }
    if (rc || updated) {
        if (map->is_local) {
            /* TODO: don't send same data multiple times */
            for (i = 0; i < map->num_src; i++) {
                if (map->src[i]->sig->is_local) {
                    mpr_local_dev dev = (mpr_local_dev)map->src[i]->sig->dev;
                    if (dev->subscribers) {
                        trace_dev(dev, "informing subscribers (MAPPED)\n")
                        mpr_net_use_subscribers(net, dev, MPR_MAP_OUT);
                        mpr_map_send_state(map, -1, MSG_MAPPED);
                    }
                }
            }
            if (map->dst->sig->is_local) {
                mpr_local_dev dev = (mpr_local_dev)map->dst->sig->dev;
                if (dev->subscribers) {
                    trace_dev(dev, "informing subscribers (MAPPED)\n")
                    mpr_net_use_subscribers(net, dev, MPR_MAP_IN);
                    mpr_map_send_state(map, -1, MSG_MAPPED);
                }
            }
        }
        mpr_graph_call_cbs(gph, (mpr_obj)map, MPR_MAP, rc ? MPR_OBJ_NEW : MPR_OBJ_MOD);
    }
    mpr_tbl_clear_empty(map->obj.props.synced);
    return 0;
}

/*! Modify the map properties : mode, range, expression, etc. */
static int handler_map_mod(const char *path, const char *types, lo_arg **av,
                           int ac, lo_message msg, void *user)
{
    mpr_graph gph = (mpr_graph)user;
    mpr_net net = &gph->net;
    mpr_local_map map;
    mpr_msg props;
    mpr_msg_atom a;
    mpr_loc loc = MPR_LOC_UNDEFINED;
    int i, updated;

    RETURN_ARG_UNLESS(ac >= 4, 0);
#ifdef DEBUG
    trace_net("received /map/modify ");
    lo_message_pp(msg);
#endif
    map = (mpr_local_map)find_map(net, types, ac, av, MPR_LOC_ANY, 0, FIND);
    RETURN_ARG_UNLESS(map && MPR_MAP_ERROR != (mpr_map)map, 0);
    RETURN_ARG_UNLESS(map->status >= MPR_STATUS_ACTIVE, 0);

    props = mpr_msg_parse_props(ac, types, av);
    TRACE_NET_RETURN_UNLESS(props, 0, "ignoring /map/modify, no properties.\n");

    if (!map->one_src) {
        /* if map has sources from different remote devices, processing must
         * occur at the destination. */
        loc = MPR_LOC_DST;
    }
    else {
        a = mpr_msg_get_prop(props, MPR_PROP_PROCESS_LOC);
        if (a)
            loc = mpr_loc_from_str(&(a->vals[0])->s);
        if ((a = mpr_msg_get_prop(props, MPR_PROP_EXPR))) {
            if (strstr(&a->vals[0]->s, "y{-"))
                loc = MPR_LOC_DST;
        }
        else if (map->expr_str && strstr(map->expr_str, "y{-"))
            loc = MPR_LOC_DST;
    }

    /* do not continue if we are not in charge of processing */
    if (MPR_LOC_DST == loc && !map->dst->sig->is_local) {
        trace_net("ignoring /map/modify, synced to remote destination.\n");
        goto done;
    }
    else if (MPR_LOC_SRC == loc && !map->src[0]->sig->is_local) {
        trace_net("ignoring /map/modify, synced to remote source.\n");
        goto done;
    }

    updated = mpr_map_set_from_msg((mpr_map)map, props, 1);
    if (updated) {
        if (!map->is_local_only) {
            /* Inform remote peer(s) of relevant changes */
            if (!map->dst->rsig) {
                mpr_net_use_mesh(net, map->dst->link->addr.admin);
                mpr_map_send_state((mpr_map)map, -1, MSG_MAPPED);
            }
            else {
                int i;
                for (i = 0; i < map->num_src; i++) {
                    if (map->src[i]->rsig)
                        continue;
                    mpr_net_use_mesh(net, map->src[i]->link->addr.admin);
                    i = mpr_map_send_state((mpr_map)map, i, MSG_MAPPED);
                }
            }
        }

        /* TODO: don't send same data multiple times */
        for (i = 0; i < map->num_src; i++) {
            if (map->src[i]->rsig) {
                mpr_local_dev dev = (mpr_local_dev)map->src[i]->sig->dev;
                if (dev->subscribers) {
                    trace_dev(dev, "informing subscribers (MAPPED)\n")
                    mpr_net_use_subscribers(net, dev, MPR_MAP_OUT);
                    mpr_map_send_state((mpr_map)map, -1, MSG_MAPPED);
                }
            }
        }
        if (map->dst->rsig) {
            mpr_local_dev dev = (mpr_local_dev)map->dst->sig->dev;
            if (dev->subscribers) {
                trace_dev(dev, "informing subscribers (MAPPED)\n")
                mpr_net_use_subscribers(net, dev, MPR_MAP_IN);
                mpr_map_send_state((mpr_map)map, -1, MSG_MAPPED);
            }
        }
    }
    trace_graph("updated %d map properties. (3)\n", updated);

done:
    mpr_msg_free(props);
    mpr_tbl_clear_empty(map->obj.props.synced);
    return 0;
}

/*! Unmap a set of signals. */
static int handler_unmap(const char *path, const char *types, lo_arg **av,
                         int ac, lo_message msg, void *user)
{
    mpr_graph gph = (mpr_graph)user;
    mpr_net net = &gph->net;
    mpr_local_map map;
    int i;

#ifdef DEBUG
    trace_net("received /unmap");
    lo_message_pp(msg);
#endif
    map = (mpr_local_map)find_map(net, types, ac, av, MPR_LOC_ANY, 0, FIND);
    /* TODO: make sure dev is actually involved in the map */
    RETURN_ARG_UNLESS(map && MPR_MAP_ERROR != (mpr_map)map, 0);

    /* inform remote peer(s) */
    if (!map->dst->is_local || !map->dst->rsig) {
        if (map->dst->link && map->dst->link->addr.admin) {
            mpr_net_use_mesh(net, map->dst->link->addr.admin);
            mpr_map_send_state((mpr_map)map, -1, MSG_UNMAP);
        }
    }
    else {
        for (i = 0; i < map->num_src; i++) {
            mpr_local_slot src = map->src[i];
            if (src->rsig || !src->link || !src->link->addr.admin)
                continue;
            mpr_net_use_mesh(net, src->link->addr.admin);
            i = mpr_map_send_state((mpr_map)map, i, MSG_UNMAP);
        }
    }

    /* TODO: don't send same data multiple times */
    for (i = 0; i < map->num_src; i++) {
        if (map->src[i]->sig->is_local) {
            mpr_local_dev dev = (mpr_local_dev)map->src[i]->sig->dev;
            inform_device_subscribers(net, dev);

            trace_dev(dev, "informing subscribers (SIGNAL)\n");
            mpr_net_use_subscribers(net, dev, MPR_SIG);
            mpr_sig_send_state(map->src[i]->sig, MSG_SIG);

            trace_dev(dev, "informing subscribers (UNMAPPED)\n")
            mpr_net_use_subscribers(net, dev, MPR_MAP_OUT);
            mpr_map_send_state((mpr_map)map, -1, MSG_UNMAPPED);
        }
    }
    if (map->dst->sig->is_local) {
        mpr_local_dev dev = (mpr_local_dev)map->dst->sig->dev;
        inform_device_subscribers(net, dev);

        trace_dev(dev, "informing subscribers (SIGNAL)\n");
        mpr_net_use_subscribers(net, dev, MPR_SIG);
        mpr_sig_send_state(map->dst->sig, MSG_SIG);

        trace_dev(dev, "informing subscribers (UNMAPPED)\n")
        mpr_net_use_subscribers(net, dev, MPR_MAP_IN);
        mpr_map_send_state((mpr_map)map, -1, MSG_UNMAPPED);
    }

    /* The mapping is removed. */
    mpr_rtr_remove_map(net->rtr, map);
    mpr_graph_remove_map(net->graph, (mpr_map)map, MPR_OBJ_REM);
    /* TODO: remove empty rtr_sigs */
    return 0;
}

/*! Respond to /unmapped by removing map from graph. */
static int handler_unmapped(const char *path, const char *types, lo_arg **av,
                            int ac, lo_message msg, void *user)
{
    mpr_graph gph = (mpr_graph)user;
#ifdef DEBUG
    trace_graph("received /unmapped");
    lo_message_pp(msg);
#endif
    mpr_map map = find_map(&gph->net, types, ac, av, 0, 0, FIND);
    RETURN_ARG_UNLESS(map && MPR_MAP_ERROR != map, 0);
    mpr_graph_remove_map(gph, map, MPR_OBJ_REM);
    return 0;
}

static int handler_ping(const char *path, const char *types, lo_arg **av,
                        int ac, lo_message msg, void *user)
{
    mpr_graph gph = (mpr_graph)user;
    mpr_net net = &gph->net;
    mpr_dev remote;
    mpr_link lnk;
    mpr_time now;
    lo_timetag then;
    int i;

    RETURN_ARG_UNLESS(net->num_devs, 0);
    mpr_time_set(&now, MPR_NOW);
    then = lo_message_get_timestamp(msg);

    remote = (mpr_dev)mpr_graph_get_obj(net->graph, MPR_DEV, av[0]->h);
    for (i = 0; i < net->num_devs; i++) {
        mpr_local_dev dev = net->devs[i];
        mpr_sync_clock clk;
        lnk = remote ? mpr_dev_get_link_by_remote(dev, remote) : 0;
        if (!lnk)
            continue;
        clk = &lnk->clock;
        trace_dev(dev, "ping received from device '%s'\n", lnk->devs[REMOTE_DEV]->name);
        if (av[2]->i == clk->sent.msg_id) {
            /* total elapsed time since ping sent */
            double elapsed = mpr_time_get_diff(now, clk->sent.time);
            /* assume symmetrical latency */
            double latency = (elapsed - av[3]->d) * 0.5;
            /* difference between remote and local clocks (latency compensated) */
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
                    /* remote time is in the future */
                    clk->offset = offset;
                }
                else if (   latency < clk->latency + clk->jitter
                         && latency > clk->latency - clk->jitter) {
                    clk->offset = clk->offset * 0.9 + offset * 0.1;
                    clk->latency = clk->latency * 0.9 + latency * 0.1;
                }
            }
        }

        /* update sync status */
        if (lnk->is_local_only)
            continue;
        mpr_time_set(&clk->rcvd.time, now);
        clk->rcvd.msg_id = av[1]->i;
    }
    return 0;
}

static int handler_sync(const char *path, const char *types, lo_arg **av,
                        int ac, lo_message msg, void *user)
{
    mpr_graph graph = (mpr_graph)user;
    mpr_dev dev;
    mpr_net net = &graph->net;
    RETURN_ARG_UNLESS(net && ac && MPR_STR == types[0], 0);
    graph = net->graph;
    dev = mpr_graph_get_dev_by_name(graph, &av[0]->s);
    if (dev) {
        RETURN_ARG_UNLESS(!dev->is_local, 0);
        trace_graph("updating sync record for device '%s'\n", dev->name);
        mpr_time_set(&dev->synced, MPR_NOW);

        if (!dev->subscribed && graph->autosub) {
            trace_graph("autosubscribing to device '%s'.\n", &av[0]->s);
            mpr_graph_subscribe(graph, dev, graph->autosub, -1);
        }
    }
    else if (graph->autosub) {
        /* only create device record after requesting more information */
        mpr_dev_t temp;
        temp.name = &av[0]->s;
        temp.obj.version = -1;
        temp.is_local = 0;
        trace_net("requesting metadata for device '%s'.\n", &av[0]->s);
        mpr_graph_subscribe(graph, &temp, MPR_DEV, 0);
    }
    else
        trace_graph("ignoring sync from '%s' (autosubscribe = %d)\n", &av[0]->s, graph->autosub);
    return 0;
}
