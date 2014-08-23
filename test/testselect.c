
#include "../src/mapper_internal.h"
#include <mapper/mapper.h>
#include <stdio.h>
#include <math.h>
#include <lo/lo.h>
#include <unistd.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

#ifdef WIN32
#define usleep(x) Sleep(x/1000)

void timersub(struct timeval *a, struct timeval *b, struct timeval *res)
{
    res->tv_sec = a->tv_sec - b->tv_sec;
    if (a->tv_usec >= b->tv_usec)
        res->tv_usec = a->tv_usec - b->tv_usec;
    else {
        res->tv_sec--;
        res->tv_usec = 999999 - b->tv_usec + a->tv_usec;
    }
}
#endif

#define eprintf(format, ...) do {               \
    if (verbose)                                \
        fprintf(stdout, format, ##__VA_ARGS__); \
} while(0)

int verbose = 1;
int terminate = 0;
int autoconnect = 1;
int done = 0;

mapper_device source = 0;
mapper_device destination = 0;
mapper_signal sendsig = 0;
mapper_signal recvsig = 0;

int sent = 0;
int received = 0;

/*! Creation of a local source. */
int setup_source()
{
    source = mdev_new("testselect-send", 0, 0);
    if (!source)
        goto error;
    eprintf("source created.\n");

    float mn=0, mx=10;

    sendsig = mdev_add_output(source, "/outsig", 1, 'f', "Hz", &mn, &mx);

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
    destination = mdev_new("testselect-recv", 0, 0);
    if (!destination)
        goto error;
    eprintf("destination created.\n");

    float mn=0, mx=1;

    recvsig = mdev_add_input(destination, "/insig", 1, 'f',
                             0, &mn, &mx, insig_handler, 0);

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

int setup_connection()
{
    int count = 0;

    mapper_monitor mon = mapper_monitor_new(source->admin, 0);
    if (!mon)
        goto error;

    char src_name[1024], dest_name[1024];
    mapper_monitor_link(mon, mdev_name(source),
                        mdev_name(destination), 0, 0);

    while (!done && !source->routers) {
        if (count++ > 50)
            goto error;
        mdev_poll(source, 10);
        mdev_poll(destination, 10);
    }

    msig_full_name(sendsig, src_name, 1024);
    msig_full_name(recvsig, dest_name, 1024);
    mapper_monitor_connect(mon, src_name, dest_name, 0, 0);

    // wait until connection has been established
    while (!done && !source->routers->num_connections) {
        if (count++ > 50)
            goto error;
        mdev_poll(source, 10);
        mdev_poll(destination, 10);
    }
    eprintf("Connection established.\n");

    mapper_monitor_free(mon);

    return 0;

  error:
    return 1;
}

void wait_local_devices()
{
    while (!done && !(mdev_ready(source) && mdev_ready(destination))) {
        mdev_poll(source, 0);
        mdev_poll(destination, 0);

        usleep(50 * 1000);
    }
}

/* This is where we test the use of select() to wait on multiple
 * devices at once. */
void select_on_both_devices(int block_ms)
{
    int i, updated = 0;

    fd_set fdr;

    int nfds1 = mdev_num_fds(source);
    int nfds2 = mdev_num_fds(destination);

    int *fds1 = alloca(sizeof(int)*nfds1);
    int *fds2 = alloca(sizeof(int)*nfds2);

    int mfd = -1;

    nfds1 = mdev_get_fds(source, fds1, nfds1);
    nfds2 = mdev_get_fds(destination, fds2, nfds2);

    FD_ZERO(&fdr);
    for (i = 0; i < nfds1; i++) {
        FD_SET(fds1[i], &fdr);
        if (fds1[i] > mfd) mfd = fds1[i];
    }
    for (i = 0; i < nfds2; i++) {
        FD_SET(fds2[i], &fdr);
        if (fds2[i] > mfd) mfd = fds2[i];
    }

    struct timeval timeout = { block_ms * 0.001, (block_ms * 1000) % 1000000 };
    struct timeval now, then;
    gettimeofday(&now, NULL);
    then.tv_sec = now.tv_sec + block_ms * 0.001;
    then.tv_usec = now.tv_usec + block_ms * 1000;
    if (then.tv_usec > 1000000) {
        then.tv_sec++;
        then.tv_usec %= 1000000;
    }

    while (timercmp(&now, &then, <)) {
        if (select(mfd+1, &fdr, 0, 0, &timeout) > 0)
        {
            for (i = 0; i < nfds1; i++) {
                if (FD_ISSET(fds1[i], &fdr))
                    mdev_service_fd(source, fds1[i]);
            }
            for (i = 0; i < nfds2; i++) {
                if (FD_ISSET(fds2[i], &fdr))
                    mdev_service_fd(destination, fds2[i]);
            }
            updated ++;
        }
        gettimeofday(&now, NULL);

        // not necessary in Linux since timeout is updated by select()
        timersub(&then, &now, &timeout);
    }

    if (!updated) {
        /* If nothing happened in 100 ms, we should poll the devices
         * anyways in case action needs to be taken. */
        mdev_poll(source, 0);
        mdev_poll(destination, 0);
    }
}

void loop()
{
    eprintf("-------------------- GO ! --------------------\n");
    int i = 0;

    while ((!terminate || i < 50) && !done) {
        msig_update_float(sendsig, ((i % 10) * 1.0f));
        eprintf("\nsource value updated to %d -->\n", i % 10);
        i++;
        sent++;
        select_on_both_devices(100);
        if (!verbose) {
            printf("\r  Sent: %4i, Received: %4i   ", sent, received);
            fflush(stdout);
        }
    }
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
                        eprintf("testselect.c: possible arguments"
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
        eprintf("Error initializing source.\n");
        result = 1;
        goto done;
    }

    wait_local_devices();

    if (autoconnect && setup_connection()) {
        eprintf("Error initializing connection.\n");
        result = 1;
        goto done;
    }

    loop();

    if (sent != received) {
        result = 1;
        eprintf("Error: sent %i messages but received %i messages.\n",
                sent, received);
    }

  done:
    cleanup_destination();
    cleanup_source();
    printf("Test %s.\n", result ? "FAILED" : "PASSED");
    return result;
}
