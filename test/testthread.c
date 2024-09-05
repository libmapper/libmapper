#include <mapper/mapper.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <math.h>
#ifdef WIN32
#include <io.h>
#else
#include <unistd.h>
#endif
#include <signal.h>
#include <string.h>

#if defined(WIN32) || defined(_MSC_VER)
#define HAVE_WIN32_THREADS 1
#define SLEEP_MS(x) Sleep(x)
#else
#include <pthread.h>
#define SLEEP_MS(x) usleep((x)*1000)
#endif

int verbose = 1;
int terminate = 0;
int autoconnect = 1;
int shared_graph = 0;
int done = 0;
int period = 100;

mpr_dev src = 0;
mpr_dev dst = 0;
mpr_sig sendsig = 0;
mpr_sig recvsig = 0;

int sent = 0;
int received = 0;

float expected;

volatile sig_atomic_t keep_going = 1;

static void eprintf(const char *format, ...)
{
    va_list args;
    if (!verbose)
        return;
    va_start(args, format);
    vprintf(format, args);
    va_end(args);
}

int setup_src(mpr_graph g, const char *iface)
{
    int mn = 0, mx = 1;
    mpr_list l;

    src = mpr_dev_new("testthread-send", g);
    if (!src)
        goto error;
    if (iface)
        mpr_graph_set_interface(mpr_obj_get_graph((mpr_obj)src), iface);
    eprintf("source created using interface %s.\n",
            mpr_graph_get_interface(mpr_obj_get_graph((mpr_obj)src)));

    sendsig = mpr_sig_new(src, MPR_DIR_OUT, "outsig", 1, MPR_INT32, NULL,
                          &mn, &mx, NULL, NULL, 0);

    eprintf("Output signal 'outsig' registered.\n");
    l = mpr_dev_get_sigs(src, MPR_DIR_OUT);
    eprintf("Number of outputs: %d\n", mpr_list_get_size(l));
    mpr_list_free(l);
    return 0;

  error:
    return 1;
}

void cleanup_src(void)
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
        if (fabs(*(float*)value - expected) < 0.0001)
            received++;
        else
            eprintf(" expected %f\n", expected);
    }
}

int setup_dst(mpr_graph g, const char *iface)
{
    float mn = 0, mx = 1;
    mpr_list l;

    dst = mpr_dev_new("testthread-recv", g);
    if (!dst)
        goto error;
    if (iface)
        mpr_graph_set_interface(mpr_obj_get_graph((mpr_obj)dst), iface);
    eprintf("destination created using interface %s.\n",
            mpr_graph_get_interface(mpr_obj_get_graph((mpr_obj)dst)));

    recvsig = mpr_sig_new(dst, MPR_DIR_IN, "insig", 1, MPR_FLT, NULL,
                          &mn, &mx, NULL, handler, MPR_SIG_UPDATE);

    eprintf("Input signal 'insig' registered.\n");
    l = mpr_dev_get_sigs(dst, MPR_DIR_IN);
    eprintf("Number of inputs: %d\n", mpr_list_get_size(l));
    mpr_list_free(l);
    return 0;

  error:
    return 1;
}

void cleanup_dst(void)
{
    if (dst) {
        eprintf("Freeing destination.. ");
        fflush(stdout);
        mpr_dev_free(dst);
        eprintf("ok\n");
    }
}

int setup_maps(void)
{
    mpr_map map = mpr_map_new(1, &sendsig, 1, &recvsig);

    char expr[128];
    snprintf(expr, 128, "y=x");
    mpr_obj_set_prop(map, MPR_PROP_EXPR, NULL, 1, MPR_STR, expr, 1);
    mpr_obj_push(map);

    /* Wait until mapping has been established */
    while (!done && !mpr_map_get_is_ready(map)) {
        mpr_dev_poll(src, 10);
        mpr_dev_poll(dst, 10);
    }

    eprintf("map initialized with expression '%s'\n",
            mpr_obj_get_prop_as_str(map, MPR_PROP_EXPR, NULL));

    return 0;
}

int wait_ready(void)
{
    while (!done && !(mpr_dev_get_is_ready(src) && mpr_dev_get_is_ready(dst))) {
        mpr_dev_poll(src, 25);
        mpr_dev_poll(dst, 25);
    }
    return done;
}

#ifdef HAVE_WIN32_THREADS
unsigned __stdcall update_thread(void *context)
#else
void *update_thread(void *context)
#endif
{
    const char *name = mpr_obj_get_prop_as_str((mpr_obj)sendsig, MPR_PROP_NAME, NULL);
    mpr_graph gf = mpr_obj_get_graph((mpr_obj)src);
    /* try creating and destroying a device */
    mpr_dev dev2 = mpr_dev_new("foo", gf);
    while ((!terminate || sent < 50) && !done) {
        eprintf("Updating signal %s to %d\n", name, sent);
        mpr_sig_set_value(sendsig, 0, 1, MPR_INT32, &sent);
        expected = sent;
        sent++;
        mpr_dev_update_maps(src);
        mpr_dev_poll(src, period);
    }
    mpr_dev_free(dev2);
    keep_going = 0;
    return 0;
}

void loop1(void)
{
#ifdef HAVE_WIN32_THREADS
    unsigned retval;
    HANDLE thread;
    if (!(thread=(HANDLE)_beginthreadex(NULL, 0, &update_thread, 0, 0, NULL))) {
        printf("Error creating thread (_beginthreadex)\n");
        exit(1);
    }
#else
    void *retval;
    pthread_t thread;
    if (pthread_create(&thread, 0, update_thread, 0)) {
        perror("error: pthread_create");
        exit(1);
    }
#endif /* HAVE_WIN32_THREADS */

    while (keep_going) {
        /* src device will be polled in another thread */
        if (shared_graph) {
            /* sleep here instead of calling poll on shared graph */
            SLEEP_MS(period);
        }
        else {
            mpr_dev_poll(dst, period);
        }

        if (!verbose) {
            printf("\r  Sent: %4i, Received: %4i   ", sent, received);
            fflush(stdout);
        }
    }
    mpr_dev_poll(src, period);
    mpr_dev_poll(dst, period);

#ifdef HAVE_WIN32_THREADS
    if (WaitForSingleObject(thread, INFINITE))
        printf("Error closing thread (WaitForSingleObject)\n");
    else
        printf("Thread joined, retval=%u\n", retval);
    CloseHandle(thread);
#else
    if (pthread_join(thread, &retval))
        printf("Error closing thread (pthread_join)\n");
    else
        printf("Thread joined, retval=%p\n", retval);
#endif /* HAVE_WIN32_THREADS */
}

void loop2(void)
{
    const char *name = mpr_obj_get_prop_as_str((mpr_obj)sendsig, MPR_PROP_NAME, NULL);
    mpr_dev_start_polling(dst, period);

    while ((!terminate || sent < 100) && !done) {
        eprintf("Updating signal %s to %d\n", name, sent);
        mpr_sig_set_value(sendsig, 0, 1, MPR_INT32, &sent);
        expected = sent;
        sent++;
        if (shared_graph) {
            /* sleep here instead of calling poll on shared graph */
            SLEEP_MS(period + 1);
        }
        else
            mpr_dev_poll(src, period);

        if (!verbose) {
            printf("\r  Sent: %4i, Received: %4i   ", sent, received);
            fflush(stdout);
        }
    }

    mpr_dev_stop_polling(dst);
}

void segv(int sig)
{
    printf("\x1B[31m(SEGV)\n\x1B[0m");
    exit(1);
}

void ctrlc(int signal)
{
    done = 1;
}

int main(int argc, char **argv)
{
    int i, j, result = 0;
    char *iface = 0;
    mpr_graph g;

    /* process flags for -v verbose, -t terminate, -h help */
    for (i = 1; i < argc; i++) {
        if (argv[i] && argv[i][0] == '-') {
            int len = strlen(argv[i]);
            for (j = 1; j < len; j++) {
                switch (argv[i][j]) {
                    case 'h':
                        printf("testthread.c: possible arguments "
                               "-f fast (execute quickly), "
                               "-q quiet (suppress output), "
                               "-t terminate automatically, "
                               "-s shared (use one mpr_graph only), "
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
                    case 's':
                        shared_graph = 1;
                        break;
                    case '-':
                        if (strcmp(argv[i], "--iface")==0 && argc>i+1) {
                            i++;
                            iface = argv[i];
                            j = len;
                        }
                        break;
                    default:
                        break;
                }
            }
        }
    }

    signal(SIGSEGV, segv);
    signal(SIGINT, ctrlc);

    g = shared_graph ? mpr_graph_new(0) : 0;

    if (setup_dst(g, iface)) {
        eprintf("Error initializing destination.\n");
        result = 1;
        goto done;
    }

    if (setup_src(g, iface)) {
        eprintf("Done initializing source.\n");
        result = 1;
        goto done;
    }

    if (wait_ready()) {
        eprintf("Device registration aborted.\n");
        result = 1;
        goto done;
    }

    if (autoconnect && setup_maps()) {
        eprintf("Error initializing maps.\n");
        result = 1;
        goto done;
    }

    /* update signals in another thread */
    loop1();

    /* poll device in another thread */
    loop2();

    if (autoconnect && (!received || sent > received)) {
        eprintf("Not all sent messages were received.\n");
        eprintf("Updated value %d time%s and received %d of them.\n",
                sent, sent == 1 ? "" : "s", received);
        result = 1;
    }

  done:
    cleanup_dst();
    cleanup_src();
    if (g) mpr_graph_free(g);
    printf("...................Test %s\x1B[0m.\n",
           result ? "\x1B[31mFAILED" : "\x1B[32mPASSED");
    return result;
}
