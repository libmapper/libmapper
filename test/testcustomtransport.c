
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

void on_map(mapper_device dev, mapper_map map, mapper_record_event event)
{
    if (verbose) {
        printf("Map: ");
        mapper_map_print(map);
    }

    // we are looking for a map with one source (sendsig) and one dest (recvsig)
    if (mapper_map_num_slots(map, MAPPER_LOC_SOURCE) > 1)
        return;
    if (mapper_slot_signal(mapper_map_slot(map, MAPPER_LOC_SOURCE, 0)) != sendsig)
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
    char t;
    int length;
    if (mapper_map_property(map, "transport", &length, &t,
                            (const void **)&a_transport)
        || t != 's' || length != 1) {
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
    if (mapper_map_property(map, "tcpPort", &length, &t, (const void **)&a_port)
        || t != 'i' || length != 1) {
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

    const char *host = mapper_device_host(map->destination.signal->device);

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
    source = mapper_device_new("testcustomtransport-send", 0, 0);
    if (!source)
        goto error;
    eprintf("source created.\n");

    float mn=0, mx=10;

    mapper_device_set_map_callback(source, on_map);

    sendsig = mapper_device_add_output_signal(source, "outsig", 1, 'f', "Hz",
                                              &mn, &mx);

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

void insig_handler(mapper_signal sig, mapper_id instance, const void *value,
                   int count, mapper_timetag_t *timetag)
{
    if (value) {
        eprintf("--> destination got %s", mapper_signal_name(sig));
        float *v = (float*)value;
        int len = mapper_signal_length(sig);
        for (int i = 0; i < len; i++) {
            eprintf(" %f", v[i]);
        }
        eprintf("\n");
    }
    received++;
}

/*! Creation of a local destination. */
int setup_destination()
{
    destination = mapper_device_new("testcustomtransport-recv", 0, 0);
    if (!destination)
        goto error;
    eprintf("destination created.\n");

    float mn=0, mx=1;

    recvsig = mapper_device_add_input_signal(destination, "insig", 1, 'f', 0,
                                             &mn, &mx, insig_handler, 0);

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
        mapper_map_set_property(map, "transport", 1, 's', str, 1);

        // Add custom meta-data specifying a port to use for this map's
        // custom transport.
        mapper_map_set_property(map, "tcpPort", 1, 'i', &tcp_port, 1);
        mapper_map_push(map);
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
        // mapper_signal_update_float(sendsig, ((i % 10) * 1.0f));

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

        mapper_device_poll(destination, 100);
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
                               "-q quiet (suppress output), "
                               "-t terminate automatically, "
                               "-h help\n");
                        return 1;
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
    printf("Test %s.\n", result ? "FAILED" : "PASSED");
    return result;
}
