#include <cstdio>
#include <cstring>
#include <signal.h>

#include <mapper/mapper_cpp.h>

#define eprintf(format, ...) do {               \
    if (verbose)                                \
        fprintf(stdout, format, ##__VA_ARGS__); \
} while(0)


using namespace mapper;

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

void on_object(Graph&& g, Object&& o, Graph::Event e)
{
    switch (e) {
        case Graph::Event::OBJ_NEW:
            eprintf("Added: ");
            break;
        case Graph::Event::OBJ_MOD:
            eprintf("Modified: ");
            break;
        case Graph::Event::OBJ_REM:
            eprintf("Removed: ");
            break;
        case Graph::Event::OBJ_EXP:
            eprintf("Unresponsive: ");
            break;
    }
    if (verbose)
        o.print();
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
    char *iface = 0;

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
                               "-h help, "
                               "--iface network interface\n");
                        return 1;
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

    Graph graph;
    if (!graph) {
        eprintf("Error initializing graph.\n");
        result = 1;
        goto done;
    }
    if (iface)
        graph.set_iface(iface);
    graph.add_callback(on_object, Type::OBJECT);

    i = 0;
    while ((!terminate || i++ < 250) && !done) {
        graph.poll(polltime_ms);

        if (update++ < 0)
            continue;
        update = -100;

        if (verbose) {
            // clear screen & cursor to home
            printf("\e[2J\e[0;0H");
            fflush(stdout);

            // print the network interface in use
            printf("Network Interface: %s\n", graph.iface().c_str());

            // print current state of graph
            graph.print();
        }
        else {
            printf("\r[%s] Devices: %4i, Signals: %4i, Maps: %4i", graph.iface().c_str(),
                   graph.devices().size(), graph.signals().size(),
                   graph.maps().size());
            fflush(stdout);
        }
    }

  done:
    return result;
}
