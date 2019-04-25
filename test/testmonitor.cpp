#include <cstdio>
#include <cstring>
#include <signal.h>

#include <mpr/mpr_cpp.h>

#define eprintf(format, ...) do {               \
    if (verbose)                                \
        fprintf(stdout, format, ##__VA_ARGS__); \
} while(0)


using namespace mpr;

int verbose = 1;
int terminate = 0;
int done = 0;
int update = 0;
const int polltime_ms = 100;

void monitor_pause()
{
    // Don't pause normally, but this is left here to be easily
    // enabled for debugging purposes.

    // sleep(1);
}

void on_object(mpr_graph g, mpr_obj o, mpr_graph_evt e, const void *user)
{
    if (verbose)
        mpr_obj_print(o, 0);

    switch (e) {
    case MPR_OBJ_NEW:
        eprintf("added.\n");
        break;
    case MPR_OBJ_MOD:
        eprintf("modified.\n");
        break;
    case MPR_OBJ_REM:
        eprintf("removed.\n");
        break;
    case MPR_OBJ_EXP:
        eprintf("unresponsive.\n");
        break;
    }
    monitor_pause();
    update = 1;
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
                        printf("testadmin.c: possible arguments "
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

    Graph graph(MPR_OBJ);
    if (!graph) {
        eprintf("Error initializing graph.\n");
        result = 1;
        goto done;
    }

    graph.add_callback(on_object, MPR_OBJ, 0);

    i = 0;
    while ((!terminate || i++ < 200) && !done) {
        graph.poll(polltime_ms);

        if (update++ < 0)
            continue;
        update = -100;

        if (verbose) {
            // clear screen & cursor to home
            printf("\e[2J\e[0;0H");
            fflush(stdout);

            // print current state of graph
            graph.print();
        }
        else {
            printf("\r  Devices: %4i, Signals: %4i, Maps: %4i",
                   graph.devices().size(), graph.signals().size(),
                   graph.maps().size());
            fflush(stdout);
        }
    }

  done:
    return result;
}
