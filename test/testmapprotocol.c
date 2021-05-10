#include <mapper/mapper.h>
#include <stdio.h>
#include <stdarg.h>
#include <math.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>

int verbose = 1;
int period = 100;
int col = 0;

mpr_dev src = 0;
mpr_dev dst = 0;
mpr_sig sendsig = 0;
mpr_sig recvsig = 0;
mpr_map map = 0;

int sent = 0;
int received = 0;
int done = 0;

int terminate = 0;

static void eprintf(const char *format, ...)
{
    va_list args;
    if (!verbose) {
        if (col >= 50)
            printf("\33[2K\r");
        fprintf(stdout, ".");
        ++col;
        return;
    }
    va_start(args, format);
    vprintf(format, args);
    fflush(stdout);
    va_end(args);
}

int setup_src(const char *iface)
{
    float mn=0, mx=1;
    mpr_list l;

    src = mpr_dev_new("testmapprotocol-send", 0);
    if (!src)
        goto error;
    if (iface)
        mpr_graph_set_interface(mpr_obj_get_graph(src), iface);
    eprintf("source created using interface %s.\n",
            mpr_graph_get_interface(mpr_obj_get_graph(src)));

    sendsig = mpr_sig_new(src, MPR_DIR_OUT, "outsig", 1, MPR_FLT, NULL,
                          &mn, &mx, NULL, NULL, 0);

    eprintf("Output signal /outsig registered.\n");
    l = mpr_dev_get_sigs(src, MPR_DIR_OUT);
    eprintf("Number of outputs: %d\n", mpr_list_get_size(l));
    mpr_list_free(l);
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

void handler(mpr_sig sig, mpr_sig_evt event, mpr_id instance, int length,
             mpr_type type, const void *value, mpr_time t)
{
    if (value) {
        eprintf("handler: Got %f\n", (*(float*)value));
    }
    received++;
}

int setup_dst(const char *iface)
{
    float mn=0, mx=1;
    mpr_list l;

    dst = mpr_dev_new("testmapprotocol-recv", 0);
    if (!dst)
        goto error;
    if (iface)
        mpr_graph_set_interface(mpr_obj_get_graph(dst), iface);
    eprintf("destination created using interface %s.\n",
            mpr_graph_get_interface(mpr_obj_get_graph(dst)));

    recvsig = mpr_sig_new(dst, MPR_DIR_IN, "insig", 1, MPR_FLT, NULL,
                          &mn, &mx, NULL, handler, MPR_SIG_UPDATE);

    eprintf("Input signal /insig registered.\n");
    l = mpr_dev_get_sigs(dst, MPR_DIR_IN);
    eprintf("Number of inputs: %d\n", mpr_list_get_size(l));
    mpr_list_free(l);
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

void set_map_protocol(mpr_proto proto)
{
    int len;
    mpr_type type;
    const void *val;

    if (!map)
        return;

    if (!mpr_obj_set_prop((mpr_obj)map, MPR_PROP_PROTOCOL, NULL, 1, MPR_INT32,
                          &proto, 1)) {
        /* protocol not changed, exit */
        return;
    }
    mpr_obj_push((mpr_obj)map);

    /* wait until change has taken effect */
    do {
        mpr_dev_poll(src, 10);
        mpr_dev_poll(dst, 10);
        mpr_obj_get_prop_by_idx(map, MPR_PROP_PROTOCOL, NULL, &len, &type, &val, 0);
    }
    while (1 != len || MPR_INT32 != type || *(int*)val != proto);
}

int setup_map()
{
    map = mpr_map_new(1, &sendsig, 1, &recvsig);
    mpr_obj_push(map);

    /* wait until map is established */
    while (!mpr_map_get_is_ready(map)) {
        mpr_dev_poll(dst, 10);
        mpr_dev_poll(src, 10);
    }

    return 0;
}

void wait_ready()
{
    while (!(mpr_dev_get_is_ready(src) && mpr_dev_get_is_ready(dst))) {
        mpr_dev_poll(src, 10);
        mpr_dev_poll(dst, 10);
    }
}

void loop()
{
    int i = 0;
    const char *name = mpr_obj_get_prop_as_str(sendsig, MPR_PROP_NAME, NULL);
    while (!done && i < 50) {
        float val = i * 1.0f;
        eprintf("Updating signal %s to %f\n", name, val);
        mpr_sig_set_value(sendsig, 0, 1, MPR_FLT, &val);
        sent++;
        mpr_dev_poll(src, 0);
        mpr_dev_poll(dst, period);
        ++i;
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
    char *iface = 0;

    /* process flags for -v verbose, -t terminate, -h help */
    for (i = 1; i < argc; i++) {
        if (argv[i] && argv[i][0] == '-') {
            int len = strlen(argv[i]);
            for (j = 1; j < len; j++) {
                switch (argv[i][j]) {
                    case 'h':
                        printf("testmapprotocol.c: possible arguments "
                               "-q quiet (suppress output), "
                               "-t terminate automatically, "
                               "-f fast (execute quickly), "
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

    wait_ready();

    if (setup_map()) {
        eprintf("Error initializing map.\n");
        result = 1;
        goto done;
    }

    do {
        set_map_protocol(MPR_PROTO_UDP);
        eprintf("SENDING UDP\n");
        loop();

        set_map_protocol(MPR_PROTO_TCP);
        eprintf("SENDING TCP\n");
        loop();
    } while (!terminate && !done);

    if (sent != received) {
        eprintf("Not all sent messages were received.\n");
        eprintf("Updated value %d time%s, but received %d of them.\n",
                sent, sent == 1 ? "" : "s", received);
        result = 1;
    }

done:
    cleanup_dst();
    cleanup_src();
    printf("\r..................................................Test %s\x1B[0m.\n",
           result ? "\x1B[31mFAILED" : "\x1B[32mPASSED");
    return result;
}
