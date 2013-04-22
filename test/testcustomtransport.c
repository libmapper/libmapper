
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

int automate = 1;

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
    printf("%s link for %s (%s -> %s), ",
           action == MDEV_LOCAL_ESTABLISHED ? "New"
           : action == MDEV_LOCAL_DESTROYED ? "Destroyed" : "????",
           mdev_name(dev), link->src_name, link->dest_name);

    printf("destination host is %s, port is %s\n",
           lo_address_get_hostname(link->dest_addr),
           lo_address_get_port(link->dest_addr));
}

void on_mdev_connection(mapper_device dev,
                        mapper_db_link link,
                        mapper_signal sig,
                        mapper_db_connection connection,
                        mapper_device_local_action_t action,
                        void *user)
{
    printf("%s connection for %s (%s:%s -> %s:%s), ",
           action == MDEV_LOCAL_ESTABLISHED ? "New"
           : action == MDEV_LOCAL_DESTROYED ? "Destroyed" : "????",
           mdev_name(dev),
           link->src_name, connection->src_name,
           link->dest_name, connection->dest_name);

    printf("destination host is %s, port is %s\n",
           lo_address_get_hostname(link->dest_addr),
           lo_address_get_port(link->dest_addr));

    if (send_socket != -1) {
        printf("send socket already in use, not doing anything.\n");
        return;
    }

    const lo_arg *a_transport;
    lo_type t;
    if (mapper_db_connection_property_lookup(connection,
                                             "transport",
                                             &t, &a_transport)
        || t != 's')
    {
        printf("Couldn't find `transport' property.\n");
        return;
    }

    if (strncmp(&a_transport->s, "tcp", 3) != 0) {
        printf("Unknown transport property `%s', "
               "was expecting `tcp'.\n", &a_transport->s);
        return;
    }

    // Find the TCP port in the connection properties
    const lo_arg *a_port;
    if (mapper_db_connection_property_lookup(connection,
                                             "tcpPort",
                                             &t, &a_port)
        || t != 'i')
    {
        printf("Couldn't make TCP connection, "
               "tcpPort property not found.\n");
        return;
    }

    int port = a_port->i, on = 1;

    send_socket = socket(AF_INET, SOCK_STREAM, 0);

    // Set socket to be non-blocking so that accept() is successful
    if (ioctl(send_socket, FIONBIO, (char *)&on) < 0)
    {
        perror("ioctl() failed on FIONBIO");
        close(send_socket);
        exit(1);
    }

    const char *hostname = lo_address_get_hostname(link->dest_addr);

    printf("Connecting with TCP to `%s' on port %d.\n", hostname, port);

    struct sockaddr_in addr;
    memset((char *) &addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = inet_addr(hostname);
    addr.sin_port = htons(port);

    if (connect(send_socket, (struct sockaddr*)&addr, sizeof(addr)))
    {
        if (errno == EINPROGRESS)
            printf("Connecting!\n");
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
    printf("source created.\n");

    float mn=0, mx=10;

    mdev_add_link_callback(source, on_mdev_link, 0);
    mdev_add_connection_callback(source, on_mdev_connection, 0);

    sendsig = mdev_add_output(source, "/outsig", 1, 'f', "Hz", &mn, &mx);

    // Add custom meta-data specifying that this signal supports a
    // special TCP transport.
    msig_set_property(sendsig, "transport", 's', (lo_arg*)"tcp");

    printf("Output signal /outsig registered.\n");

    return 0;

  error:
    return 1;
}

void cleanup_source()
{
    if (source) {
        printf("Freeing source.. ");
        fflush(stdout);
        mdev_free(source);
        printf("ok\n");
    }
}

void insig_handler(mapper_signal sig, mapper_db_signal props,
                   int instance_id, void *value, int count,
                   mapper_timetag_t *timetag)
{
    if (value) {
        printf("--> destination got %s", props->name);
        float *v = value;
        for (int i = 0; i < props->length; i++) {
            printf(" %f", v[i]);
        }
        printf("\n");
    }
    received++;
}

/*! Creation of a local destination. */
int setup_destination()
{
    destination = mdev_new("testrecv", 0, 0);
    if (!destination)
        goto error;
    printf("destination created.\n");

    float mn=0, mx=1;

    recvsig = mdev_add_input(destination, "/insig", 1, 'f',
                             0, &mn, &mx, insig_handler, 0);

    // Add custom meta-data specifying a special transport for this
    // signal.
    msig_set_property(recvsig, "transport", 's', (lo_arg*)"tcp");

    // Add custom meta-data specifying a port to use for this signal's
    // custom transport.
    msig_set_property(recvsig, "tcpPort", 'i', (lo_arg*)&tcp_port);

    printf("Input signal /insig registered.\n");

    return 0;

  error:
    return 1;
}

void cleanup_destination()
{
    if (destination) {
        printf("Freeing destination.. ");
        fflush(stdout);
        mdev_free(destination);
        printf("ok\n");
    }
}

void wait_local_devices()
{
    while (!(mdev_ready(source) && mdev_ready(destination))) {
        mdev_poll(source, 0);
        mdev_poll(destination, 0);

        usleep(50 * 1000);
    }
}

void loop()
{
    printf("-------------------- GO ! --------------------\n");
    int i = 0;

    if (automate) {
        mapper_monitor mon = mapper_monitor_new(source->admin, 0);

        char src_name[1024], dest_name[1024];
        mapper_monitor_link(mon, mdev_name(source),
                            mdev_name(destination), 0, 0);

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

    printf("Bound to TCP port %d\n", tcp_port);

    listen(listen_socket, 1);

    while (i >= 0 && !done) {
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
                        printf("TCP connection accepted.\n");
                }

                if (recv_socket >= 0
                    && FD_ISSET(recv_socket, &fdsr))
                {
                    float j;
                    if (recv(recv_socket, &j, sizeof(float), 0) > 0)
                        printf("received value %g\n", j);
                    else {
                        perror("recv");
                        printf("closing receive socket.\n");
                        close(recv_socket);
                        recv_socket = -1;
                    }
                }

                if (FD_ISSET(send_socket, &fdss)) {

                    float j = (i % 10) * 1.0f;
                    if (send(send_socket, &j, sizeof(float), 0) > 0)
                        printf("source value updated to %g -->\n", j);
                    else {
                        perror("send");
                        printf("closing send socket.\n");
                        close(send_socket);
                        send_socket = -1;
                    }
                }
            }
        }

        mdev_poll(destination, 100);
        i++;
    }
}

void ctrlc(int sig)
{
    done = 1;
}

int main()
{
    int result = 0;

    signal(SIGINT, ctrlc);

    if (setup_destination()) {
        printf("Error initializing destination.\n");
        result = 1;
        goto done;
    }

    if (setup_source()) {
        printf("Done initializing source.\n");
        result = 1;
        goto done;
    }

    wait_local_devices();

    loop();

  done:
    cleanup_destination();
    cleanup_source();
    return result;
}
