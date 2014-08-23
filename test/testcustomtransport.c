
#include "../src/mapper_internal.h"
#include <mapper/mapper.h>
#include <stdio.h>
#include <math.h>
#include <string.h>
#include <stdlib.h>
#include <lo/lo.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>

#ifdef WIN32
#define usleep(x) Sleep(x/1000)
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

void on_mdev_link(mapper_device dev,
                  mapper_db_link link,
                  mapper_device_local_action_t action,
                  void *user)
{
    eprintf("%s link for %s (%s -> %s), ",
            action == MDEV_LOCAL_ESTABLISHED ? "New"
            : action == MDEV_LOCAL_DESTROYED ? "Destroyed" : "????",
            mdev_name(dev), link->src_name, link->dest_name);

    eprintf("destination host is %s, port is %i\n",
            link->dest_host, link->dest_port);
}

void on_mdev_connection(mapper_device dev,
                        mapper_db_link link,
                        mapper_signal sig,
                        mapper_db_connection connection,
                        mapper_device_local_action_t action,
                        void *user)
{
    eprintf("%s connection for %s (%s:%s -> %s:%s), ",
            action == MDEV_LOCAL_ESTABLISHED ? "New"
            : action == MDEV_LOCAL_DESTROYED ? "Destroyed" : "????",
            mdev_name(dev),
            link->src_name, connection->src_name,
            link->dest_name, connection->dest_name);

    eprintf("destination host is %s, port is %i\n",
            link->dest_host, link->dest_port);

    if (action == MDEV_LOCAL_DESTROYED) {
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
    if (mapper_db_connection_property_lookup(connection, "transport", &t,
                                             (const void **)&a_transport,
                                             &length)
        || t != 's' || length != 1)
    {
        eprintf("Couldn't find `transport' property.\n");
        return;
    }

    
    if (strncmp(a_transport, "tcp", 3) != 0) {
        eprintf("Unknown transport property `%s', "
                "was expecting `tcp'.\n", a_transport);
        return;
    }

    // Find the TCP port in the connection properties
    const int *a_port;
    if (mapper_db_connection_property_lookup(connection, "tcpPort", &t,
                                             (const void **)&a_port, &length)
        || t != 'i' || length != 1)
    {
        eprintf("Couldn't make TCP connection, "
                "tcpPort property not found.\n");
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

    const char *host = link->dest_host;

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
    source = mdev_new("testsend", 0, 0);
    if (!source)
        goto error;
    eprintf("source created.\n");

    float mn=0, mx=10;

    mdev_set_link_callback(source, on_mdev_link, 0);
    mdev_set_connection_callback(source, on_mdev_connection, 0);

    sendsig = mdev_add_output(source, "/outsig", 1, 'f', "Hz", &mn, &mx);

    // Add custom meta-data specifying that this signal supports a
    // special TCP transport.
    char *str = "tcp";
    msig_set_property(sendsig, "transport", 's', &str, 1);

    eprintf("Output signal /outsig registered.\n");

    return 0;

  error:
    return 1;
}

void cleanup_source()
{
    if (source) {
        eprintf("Freeing source.. ");
        fflush(stdout);
        mdev_free(source);
        eprintf("ok\n");
    }
}

void insig_handler(mapper_signal sig, mapper_db_signal props,
                   int instance_id, void *value, int count,
                   mapper_timetag_t *timetag)
{
    if (value) {
        eprintf("--> destination got %s", props->name);
        float *v = value;
        for (int i = 0; i < props->length; i++) {
            eprintf(" %f", v[i]);
        }
        eprintf("\n");
    }
    received++;
}

/*! Creation of a local destination. */
int setup_destination()
{
    destination = mdev_new("testrecv", 0, 0);
    if (!destination)
        goto error;
    eprintf("destination created.\n");

    float mn=0, mx=1;

    recvsig = mdev_add_input(destination, "/insig", 1, 'f',
                             0, &mn, &mx, insig_handler, 0);

    // Add custom meta-data specifying a special transport for this
    // signal.
    char *str = "tcp";
    msig_set_property(recvsig, "transport", 's', &str, 1);

    // Add custom meta-data specifying a port to use for this signal's
    // custom transport.
    msig_set_property(recvsig, "tcpPort", 'i', &tcp_port, 1);

    eprintf("Input signal /insig registered.\n");

    return 0;

  error:
    return 1;
}

void cleanup_destination()
{
    if (destination) {
        eprintf("Freeing destination.. ");
        fflush(stdout);
        mdev_free(destination);
        eprintf("ok\n");
    }
}

void wait_local_devices()
{
    while (!done && !(mdev_ready(source) && mdev_ready(destination))) {
        mdev_poll(source, 0);
        mdev_poll(destination, 0);

        usleep(50 * 1000);
    }
}

void loop()
{
    eprintf("-------------------- GO ! --------------------\n");
    int i = 0;

    if (autoconnect) {
        mapper_monitor mon = mapper_monitor_new(source->admin, 0);

        char src_name[1024], dest_name[1024];
        mapper_monitor_link(mon, mdev_name(source),
                            mdev_name(destination), 0, 0);

        while (i++ < 10) {
            mdev_poll(source, 0);
            mdev_poll(destination, 0);
        }

        msig_full_name(sendsig, src_name, 1024);
        msig_full_name(recvsig, dest_name, 1024);
        mapper_monitor_connect(mon, src_name, dest_name, 0, 0);

        mapper_monitor_free(mon);
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
        mdev_poll(source, 0);

        // Instead of
        // msig_update_float(sendsig, ((i % 10) * 1.0f));

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

        mdev_poll(destination, 100);
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
