
#include "../src/mapper_internal.h"
#include <mapper/mapper.h>
#include <stdio.h>
#include <math.h>
#include <lo/lo.h>

#include <unistd.h>
#include <signal.h>
#include <stdlib.h>

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

/*! Creation of a local source. */
int setup_source()
{
    source = mdev_new("testsend", 0, 0);
    if (!source)
        goto error;
    printf("source created.\n");

    float mn=0, mx=10;

    sendsig = mdev_add_output(source, "/outsig", 1, 'f', "Hz", &mn, &mx);

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

/* This is where we test the use of select() to wait on multiple
 * devices at once. */
void select_on_both_devices()
{
    int i;

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

    /* Timeout should not be more than 100 ms */
    struct timeval timeout = { 0, 100000 };

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
    }
    else
    {
        /* If nothing happened in 100 ms, we should poll the devices
         * anyways in case action needs to be taken. */
        mdev_poll(source, 0);
        mdev_poll(destination, 0);
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

    while (i >= 0 && !done) {
        select_on_both_devices();

        msig_update_float(sendsig, ((i % 10) * 1.0f));
        printf("source value updated to %d -->\n", i % 10);
        printf("Received %i messages.\n\n", mdev_poll(destination, 100));

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
