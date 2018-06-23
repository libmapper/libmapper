#include <mapper/mapper.h>
#include <stdio.h>
#include <math.h>
#include <string.h>
#include <lo/lo.h>

#include <unistd.h>
#include <signal.h>

#define eprintf(format, ...) do {               \
    if (verbose)                                \
        fprintf(stdout, format, ##__VA_ARGS__); \
} while(0)

mapper_graph graph = 0;

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

/*! Creation of a local dummy device. */
int setup_graph()
{
    graph = mapper_graph_new(MAPPER_OBJ_ALL);
    if (!graph)
        goto error;
    eprintf("Graph created.\n");

    return 0;

  error:
    return 1;
}

void cleanup_graph()
{
    if (graph) {
        eprintf("\rFreeing graph.. ");
        fflush(stdout);
        mapper_graph_free(graph);
        eprintf("ok\n");
    }
}

void loop()
{
    int i = 0;
    while ((!terminate || i++ < 200) && !done)
    {
        mapper_graph_poll(graph, polltime_ms);

        if (update++ < 0)
            continue;
        update = -100;

        if (verbose) {
            // clear screen & cursor to home
            printf("\e[2J\e[0;0H");
            fflush(stdout);

            // print current state of graph
            mapper_graph_print(graph);
        }
        else {
            printf("\r  Devices: %4i, Signals: %4i, Maps: %4i",
                   mapper_graph_get_num_objects(graph, MAPPER_OBJ_DEVICE),
                   mapper_graph_get_num_objects(graph, MAPPER_OBJ_SIGNAL),
                   mapper_graph_get_num_objects(graph, MAPPER_OBJ_MAP));
            fflush(stdout);
        }
    }
}

void on_object(mapper_graph graph, mapper_object obj, mapper_record_event e,
               const void *user)
{
    if (verbose)
        mapper_object_print(obj, 0);

    switch (e) {
    case MAPPER_ADDED:
        eprintf("added.\n");
        break;
    case MAPPER_MODIFIED:
        eprintf("modified.\n");
        break;
    case MAPPER_REMOVED:
        eprintf("removed.\n");
        break;
    case MAPPER_EXPIRED:
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

    if (setup_graph()) {
        eprintf("Error initializing graph.\n");
        result = 1;
        goto done;
    }

    mapper_graph_add_callback(graph, on_object, MAPPER_OBJ_ALL, 0);

    loop();

  done:
    mapper_graph_remove_callback(graph, on_object, 0);
    cleanup_graph();
    return result;
}
