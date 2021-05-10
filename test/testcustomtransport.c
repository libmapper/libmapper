#include "../src/mapper_internal.h"
#include <mapper/mapper.h>
#include <stdio.h>
#include <stdarg.h>
#include <math.h>
#include <string.h>
#include <stdlib.h>
#include <lo/lo.h>
#include <signal.h>
#include <errno.h>

#ifdef WIN32
 #include <winsock2.h>
 #define ioctl(x,y,z) ioctlsocket(x,y,z)
 #ifndef EINPROGRESS
  #define EINPROGRESS WSAEINPROGRESS
 #endif
#else
 #include <sys/ioctl.h>
 #include <unistd.h>
#endif

int autoconnect = 1;
int terminate = 0;
int iterations = 50; /* only matters when terminate==1 */
int verbose = 1;
int period = 100;

mpr_dev src = 0;
mpr_dev dst = 0;
mpr_sig sendsig = 0;
mpr_sig recvsig = 0;

int sent = 0;
int received = 0;
int done = 0;

/* Our sending socket for a custom TCP transport
 * We only send on it if it's valid (i.e, != -1) */
int send_socket = -1;

/* Our receiving socket for a custom TCP transport */
int recv_socket = -1;

/* Our listening socket for accepting TCP transport connections. */
int listen_socket = -1;

int tcp_port = 12000;

static void eprintf(const char *format, ...)
{
    va_list args;
    if (!verbose)
        return;
    va_start(args, format);
    vprintf(format, args);
    va_end(args);
}

void on_map(mpr_graph g, mpr_obj o, mpr_graph_evt e, const void *user)
{
    mpr_map map;
    mpr_list l;
    const char *a_transport, *host;
    const int *a_port;
    mpr_type type;
    int length, port;
    unsigned long on = 1;
    mpr_sig dstsig;
    mpr_dev dstdev;
    struct sockaddr_in addr;

    if (MPR_MAP != mpr_obj_get_type(o)) {
        printf("Error in map handler!\n");
        return;
    }

    if (verbose) {
        printf("Map: ");
        mpr_obj_print(o, 0);
    }
    map = (mpr_map)o;

    /* we are looking for a map with one source (sendsig) and one dest (recvsig) */
    l = mpr_map_get_sigs(map, MPR_LOC_SRC);
    if (mpr_list_get_size(l) > 1 || *(mpr_sig*)l != sendsig) {
        mpr_list_free(l);
        return;
    }
    mpr_list_free(l);

    if (e == MPR_OBJ_REM) {
        if (send_socket != -1) {
            close(send_socket);
            send_socket = -1;
        }
    }
    else if (send_socket != -1) {
        eprintf("send socket already in use, not doing anything.\n");
        return;
    }

    if (!mpr_obj_get_prop_by_key((mpr_obj)map, "transport", &length, &type,
                                 (const void **)&a_transport, 0)
        || type != MPR_STR || length != 1) {
        eprintf("Couldn't find `transport' property.\n");
        return;
    }

    if (strncmp(a_transport, "tcp", 3) != 0) {
        eprintf("Unknown transport property `%s', was expecting `tcp'.\n", a_transport);
        return;
    }

    /* Find the TCP port in the mapping properties */
    if (!mpr_obj_get_prop_by_key((mpr_obj)map, "tcpPort", &length, &type, (const void **)&a_port, 0)
        || type != MPR_INT32 || length != 1) {
        eprintf("Couldn't make TCP connection, tcpPort property not found.\n");
        return;
    }

    port = *a_port;

    send_socket = socket(AF_INET, SOCK_STREAM, 0);

    /* Set socket to be non-blocking so that accept() is successful */
    if (ioctl(send_socket, FIONBIO, &on) < 0)
    {
        perror("ioctl() failed on FIONBIO");
        close(send_socket);
        exit(1);
    }

    l = mpr_map_get_sigs(map, MPR_LOC_DST);
    dstsig = *(mpr_sig*)l;
    mpr_list_free(l);
    dstdev = mpr_sig_get_dev(dstsig);
    host = mpr_obj_get_prop_as_str((mpr_obj)dstdev, MPR_PROP_HOST, NULL);

    eprintf("Connecting with TCP to `%s' on port %d.\n", host, port);

    memset((char *) &addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = inet_addr(host);
    addr.sin_port = htons(port);

    if (connect(send_socket, (struct sockaddr*)&addr, sizeof(addr)))
    {
        if (errno == EINPROGRESS)
            eprintf("Connecting!\n");
        else
        {
            perror("connect");
            close(send_socket);
            send_socket = -1;
            return;
        }
    }
}

/*! Creation of a local source. */
int setup_src(const char *iface)
{
    float mn=0, mx=10;

    src = mpr_dev_new("testcustomtransport-send", 0);
    if (!src)
        goto error;
    if (iface)
        mpr_graph_set_interface(mpr_obj_get_graph((mpr_obj)src), iface);
    eprintf("source created using interface %s.\n",
            mpr_graph_get_interface(mpr_obj_get_graph((mpr_obj)src)));

    mpr_graph_add_cb(mpr_obj_get_graph((mpr_obj)src), on_map, MPR_MAP, NULL);

    sendsig = mpr_sig_new(src, MPR_DIR_OUT, "outsig", 1, MPR_FLT, "Hz", &mn, &mx, NULL, NULL, 0);

    eprintf("Output signal 'outsig' registered.\n");

    return 0;

  error:
    return 1;
}

void cleanup_src()
{
    if (src) {
        eprintf("Freeing source.. ");
        fflush(stdout);
        mpr_dev_free(src);
        eprintf("ok\n");
    }
}

void insig_handler(mpr_sig sig, mpr_sig_evt event, mpr_id instance, int length,
                   mpr_type type, const void *value, mpr_time t)
{
    const char *name = mpr_obj_get_prop_as_str((mpr_obj)sig, MPR_PROP_NAME, NULL);
    if (value) {
        int i;
        float *v = (float*)value;
        eprintf("--> destination got %s", name);
        for (i = 0; i < length; i++) {
            eprintf(" %f", v[i]);
        }
        eprintf("\n");
    }
    received++;
}

/*! Creation of a local destination. */
int setup_dst(const char *iface)
{
    float mn=0, mx=1;

    dst = mpr_dev_new("testcustomtransport-recv", 0);
    if (!dst)
        goto error;
    if (iface)
        mpr_graph_set_interface(mpr_obj_get_graph((mpr_obj)dst), iface);
    eprintf("destination created using interface %s.\n",
            mpr_graph_get_interface(mpr_obj_get_graph((mpr_obj)dst)));

    recvsig = mpr_sig_new(dst, MPR_DIR_IN, "insig", 1, MPR_FLT, NULL, &mn, &mx,
                          NULL, insig_handler, MPR_SIG_UPDATE);

    eprintf("Input signal 'insig' registered.\n");

    return 0;

  error:
    return 1;
}

void cleanup_dst()
{
    if (dst) {
        eprintf("Freeing destination.. ");
        fflush(stdout);
        mpr_dev_free(dst);
        eprintf("ok\n");
    }
}

void wait_local_devs()
{
    while (!done && !(mpr_dev_get_is_ready(src) && mpr_dev_get_is_ready(dst))) {
        mpr_dev_poll(src, 25);
        mpr_dev_poll(dst, 25);
    }
}

void loop()
{
    int i = 0;
    struct sockaddr_in addr;

    eprintf("-------------------- GO ! --------------------\n");

    if (autoconnect) {
        mpr_map map = mpr_map_new(1, &sendsig, 1, &recvsig);

        /* Add custom meta-data specifying a special transport for this map. */
        char *str = "tcp";
        mpr_obj_set_prop((mpr_obj)map, MPR_PROP_EXTRA, "transport", 1, MPR_STR, str, 1);

        /* Add custom meta-data specifying a port to use for this map's custom transport. */
        mpr_obj_set_prop((mpr_obj)map, MPR_PROP_EXTRA, "tcpPort", 1, MPR_INT32, &tcp_port, 1);
        mpr_obj_push((mpr_obj)map);
    }

    /* Set up a mini TCP server for our custom stream */
    listen_socket = socket(AF_INET, SOCK_STREAM, 0);

    memset((char *) &addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(tcp_port);
    if (bind(listen_socket, (struct sockaddr *) &addr, sizeof(addr)) < 0 ) {
        perror("bind");
        close(listen_socket);
        exit(1);
    }

    eprintf("Bound to TCP port %d\n", tcp_port);

    listen(listen_socket, 1);

    while ((!terminate || received < iterations) && !done) {
        mpr_dev_poll(src, 0);

        /* Instead of
         * mpr_sig_update(sendsig, etc.);
         * We will instead send our data on the custom TCP socket if it is valid
         */
        if (send_socket != -1) {
            int m = listen_socket;
            struct timeval timeout = { .tv_sec = 0, .tv_usec = 0 };
            fd_set fdsr, fdss;
            FD_ZERO(&fdsr);
            FD_ZERO(&fdss);
            FD_SET(listen_socket, &fdsr);
            if (recv_socket >= 0) {
                FD_SET(recv_socket, &fdsr);
                if (recv_socket > m) m = recv_socket;
            }
            if (send_socket >= 0) {
                FD_SET(send_socket, &fdss);
                if (send_socket > m) m = send_socket;
            }

            if (select(m+1, &fdsr, &fdss, 0, &timeout) > 0) {

                if (FD_ISSET(listen_socket, &fdsr)) {
                    recv_socket = accept(listen_socket, 0, 0);
                    if (recv_socket < 0)
                        perror("accept");
                    else
                        eprintf("TCP connection accepted.\n");
                }

                if (recv_socket >= 0 && FD_ISSET(recv_socket, &fdsr))
                {
                    float j;
                    if (recv(recv_socket, (void*)&j, sizeof(float), 0) > 0) {
                        eprintf("received value %g\n", j);
                        received++;
                    }
                    else {
                        perror("recv");
                        eprintf("closing receive socket.\n");
                        close(recv_socket);
                        recv_socket = -1;
                    }
                }

                if (FD_ISSET(send_socket, &fdss)
                    && (!terminate || sent < iterations)) {

                    float j = (i % 10) * 1.0f;
                    if (send(send_socket, (void*)&j, sizeof(float), 0) > 0) {
                        eprintf("source value updated to %g -->\n", j);
                        sent++;
                    }
                    else {
                        perror("send");
                        eprintf("closing send socket.\n");
                        close(send_socket);
                        send_socket = -1;
                    }
                }
            }
        }
        if (!verbose) {
            printf("\r  Sent: %4i, Received: %4i   ", sent, received);
            fflush(stdout);
        }

        mpr_dev_poll(dst, period);
        i++;
    }
    if (send_socket != -1)
        close(send_socket);
}

void ctrlc(int sig)
{
    done = 1;
}

int main(int argc, char **argv)
{
    int i, j, result = 0;
    char *iface = 0;

    /* process flags for -v verbose, -t terminate, -h help */
    for (i = 1; i < argc; i++) {
        if (argv[i] && argv[i][0] == '-') {
            int len = strlen(argv[i]);
            for (j = 1; j < len; j++) {
                switch (argv[i][j]) {
                    case 'h':
                        printf("testcustomtransport.c: possible arguments "
                               "-f fast (execute quickly), "
                               "-q quiet (suppress output), "
                               "-t terminate automatically, "
                               "-h help, "
                               "--iface network interface\n");
                        return 1;
                        break;
                    case 'f':
                        period = 1;
                        break;
                    case 'q':
                        verbose = 0;
                        break;
                    case 't':
                        terminate = 1;
                        break;
                    case '-':
                        if (strcmp(argv[i], "--iface")==0 && argc>i+1) {
                            i++;
                            iface = argv[i];
                            j = 1;
                        }
                        break;
                    default:
                        break;
                }
            }
        }
    }

    signal(SIGINT, ctrlc);

    if (setup_dst(iface)) {
        eprintf("Error initializing destination.\n");
        result = 1;
        goto done;
    }

    if (setup_src(iface)) {
        eprintf("Done initializing source.\n");
        result = 1;
        goto done;
    }

    wait_local_devs();

    loop();

    if (autoconnect && (!sent || received != sent)) {
        eprintf("sent: %d, recvd: %d\n", sent, received);
        result = 1;
    }

  done:
    cleanup_dst();
    cleanup_src();
    printf("...................Test %s\x1B[0m.\n", result ? "\x1B[31mFAILED" : "\x1B[32mPASSED");
    return result;
}
