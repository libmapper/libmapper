#include "../src/mapper_internal.h"
#include <mapper/mapper.h>
#include <stdio.h>
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

#define eprintf(format, ...) do {               \
    if (verbose)                                \
        fprintf(stdout, format, ##__VA_ARGS__); \
} while(0)

int autoconnect = 1;
int terminate = 0;
int iterations = 50; // only matters when terminate==1
int verbose = 1;
int period = 100;

mapper_device source = 0;
mapper_device destination = 0;
mapper_signal sendsig = 0;
mapper_signal recvsig = 0;

int sent = 0;
int received = 0;
int done = 0;

// Our sending socket for a custom TCP transport
// We only send on it if it's valid (i.e, != -1)
int send_socket = -1;

// Our receiving socket for a custom TCP transport
int recv_socket = -1;

// Our listening socket for accepting TCP transport connections.
int listen_socket = -1;

int tcp_port = 12000;

void on_map(mapper_graph g, mapper_object obj, mapper_record_event event,
            const void *user)
{
    if (MAPPER_OBJ_MAP != mapper_object_get_type(obj)) {
        printf("Error in map handler!\n");
        return;
    }

    if (verbose) {
        printf("Map: ");
        mapper_object_print(obj, 0);
    }
    mapper_map map = (mapper_map)obj;

    // we are looking for a map with one source (sendsig) and one dest (recvsig)
    if (mapper_map_get_num_signals(map, MAPPER_LOC_SRC) > 1)
        return;
    if (mapper_map_get_signal(map, MAPPER_LOC_SRC, 0) != sendsig)
        return;

    if (event == MAPPER_REMOVED) {
        if (send_socket != -1) {
            close(send_socket);
            send_socket = -1;
        }
    }
    else if (send_socket != -1) {
        eprintf("send socket already in use, not doing anything.\n");
        return;
    }

    const char *a_transport;
    mapper_type type;
    int length;
    if (!mapper_object_get_prop_by_name((mapper_object)map, "transport", &length,
                                        &type, (const void **)&a_transport)
        || type != MAPPER_STRING || length != 1) {
        eprintf("Couldn't find `transport' property.\n");
        return;
    }

    if (strncmp(a_transport, "tcp", 3) != 0) {
        eprintf("Unknown transport property `%s', "
                "was expecting `tcp'.\n", a_transport);
        return;
    }

    // Find the TCP port in the mapping properties
    const int *a_port;
    if (!mapper_object_get_prop_by_name((mapper_object)map, "tcpPort", &length,
                                        &type, (const void **)&a_port)
        || type != MAPPER_INT32 || length != 1) {
        eprintf("Couldn't make TCP connection, tcpPort property not found.\n");
        return;
    }

    int port = *a_port, on = 1;

    send_socket = socket(AF_INET, SOCK_STREAM, 0);

    // Set socket to be non-blocking so that accept() is successful
    if (ioctl(send_socket, FIONBIO, (char *)&on) < 0)
    {
        perror("ioctl() failed on FIONBIO");
        close(send_socket);
        exit(1);
    }

    mapper_signal dstsig = mapper_map_get_signal(map, MAPPER_LOC_DST, 0);
    mapper_device dstdev = mapper_signal_get_device(dstsig);
    const char *host;
    mapper_object_get_prop_by_index((mapper_object)dstdev, MAPPER_PROP_HOST,
                                    NULL, NULL, NULL, (const void**)&host);

    eprintf("Connecting with TCP to `%s' on port %d.\n", host, port);

    struct sockaddr_in addr;
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
int setup_source()
{
    source = mapper_device_new("testcustomtransport-send", 0);
    if (!source)
        goto error;
    eprintf("source created.\n");

    float mn=0, mx=10;

    mapper_graph_add_callback(mapper_object_get_graph((mapper_object)source),
                              on_map, MAPPER_OBJ_MAP, NULL);

    sendsig = mapper_device_add_signal(source, MAPPER_DIR_OUT, 1, "outsig",
                                       1, MAPPER_FLOAT, "Hz", &mn, &mx, NULL);

    eprintf("Output signal 'outsig' registered.\n");

    return 0;

  error:
    return 1;
}

void cleanup_source()
{
    if (source) {
        eprintf("Freeing source.. ");
        fflush(stdout);
        mapper_device_free(source);
        eprintf("ok\n");
    }
}

void insig_handler(mapper_signal sig, mapper_id instance, int length,
                   mapper_type type, const void *value, mapper_time t)
{
    const char *name;
    mapper_object_get_prop_by_index((mapper_object)sig, MAPPER_PROP_NAME, NULL,
                                    NULL, NULL, (const void**)&name);
    if (value) {
        eprintf("--> destination got %s", name);
        float *v = (float*)value;
        for (int i = 0; i < length; i++) {
            eprintf(" %f", v[i]);
        }
        eprintf("\n");
    }
    received++;
}

/*! Creation of a local destination. */
int setup_destination()
{
    destination = mapper_device_new("testcustomtransport-recv", 0);
    if (!destination)
        goto error;
    eprintf("destination created.\n");

    float mn=0, mx=1;

    recvsig = mapper_device_add_signal(destination, MAPPER_DIR_IN, 1,
                                       "insig", 1, MAPPER_FLOAT, NULL,
                                       &mn, &mx, insig_handler);

    eprintf("Input signal 'insig' registered.\n");

    return 0;

  error:
    return 1;
}

void cleanup_destination()
{
    if (destination) {
        eprintf("Freeing destination.. ");
        fflush(stdout);
        mapper_device_free(destination);
        eprintf("ok\n");
    }
}

void wait_local_devices()
{
    while (!done && !(mapper_device_ready(source)
                      && mapper_device_ready(destination))) {
        mapper_device_poll(source, 25);
        mapper_device_poll(destination, 25);
    }
}

void loop()
{
    eprintf("-------------------- GO ! --------------------\n");
    int i = 0;

    if (autoconnect) {
        mapper_map map = mapper_map_new(1, &sendsig, 1, &recvsig);

        // Add custom meta-data specifying a special transport for this map.
        char *str = "tcp";
        mapper_object_set_prop((mapper_object)map, MAPPER_PROP_EXTRA, "transport",
                               1, MAPPER_STRING, str, 1);

        // Add custom meta-data specifying a port to use for this map's
        // custom transport.
        mapper_object_set_prop((mapper_object)map, MAPPER_PROP_EXTRA, "tcpPort",
                               1, MAPPER_INT32, &tcp_port, 1);
        mapper_object_push((mapper_object)map);
    }

    // Set up a mini TCP server for our custom stream
    listen_socket = socket(AF_INET, SOCK_STREAM, 0);

    struct sockaddr_in addr;
    memset((char *) &addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(tcp_port);
    if (bind(listen_socket, (struct sockaddr *) &addr,
             sizeof(addr)) < 0 ) {
        perror("bind");
        close(listen_socket);
        exit(1);
    }

    eprintf("Bound to TCP port %d\n", tcp_port);

    listen(listen_socket, 1);

    while ((!terminate || received < iterations) && !done) {
        mapper_device_poll(source, 0);

        // Instead of
        // mapper_signal_update(sendsig, etc.);

        // We will instead send our data on the custom TCP socket if
        // it is valid
        if (send_socket != -1) {
            int m = listen_socket;
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

            struct timeval timeout = { .tv_sec = 0, .tv_usec = 0 };

            if (select(m+1, &fdsr, &fdss, 0, &timeout) > 0) {

                if (FD_ISSET(listen_socket, &fdsr)) {
                    recv_socket = accept(listen_socket, 0, 0);
                    if (recv_socket < 0)
                        perror("accept");
                    else
                        eprintf("TCP connection accepted.\n");
                }

                if (recv_socket >= 0
                    && FD_ISSET(recv_socket, &fdsr))
                {
                    float j;
                    if (recv(recv_socket, &j, sizeof(float), 0) > 0) {
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
                    if (send(send_socket, &j, sizeof(float), 0) > 0) {
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

        mapper_device_poll(destination, period);
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

    // process flags for -v verbose, -t terminate, -h help
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
                               "-h help\n");
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
                    default:
                        break;
                }
            }
        }
    }

    signal(SIGINT, ctrlc);

    if (setup_destination()) {
        eprintf("Error initializing destination.\n");
        result = 1;
        goto done;
    }

    if (setup_source()) {
        eprintf("Done initializing source.\n");
        result = 1;
        goto done;
    }

    wait_local_devices();

    loop();

    if (autoconnect && received != sent) {
        eprintf("sent: %d, recvd: %d\n", sent, received);
        result = 1;
    }

  done:
    cleanup_destination();
    cleanup_source();
    printf("...................Test %s\x1B[0m.\n",
           result ? "\x1B[31mFAILED" : "\x1B[32mPASSED");
    return result;
}
