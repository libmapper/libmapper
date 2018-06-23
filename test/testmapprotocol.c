#include <mapper/mapper.h>
#include <stdio.h>
#include <math.h>
#include <string.h>
#include <unistd.h>

#define eprintf(format, ...) do {               \
    if (verbose)                                \
        fprintf(stdout, format, ##__VA_ARGS__); \
    else {                                      \
        if (col >= 50)                          \
            printf("\33[2K\r");                 \
        fprintf(stdout, ".");                   \
        ++col;                                  \
    }                                           \
    fflush(stdout);                             \
} while(0)

int verbose = 1;
int period = 100;
int col = 0;

mapper_device source = 0;
mapper_device destination = 0;
mapper_signal sendsig = 0;
mapper_signal recvsig = 0;

mapper_map map = 0;

int sent = 0;
int received = 0;

int setup_source()
{
    source = mapper_device_new("testmapprotocol-send", 0);
    if (!source)
        goto error;
    eprintf("source created.\n");

    float mn=0, mx=1;

    sendsig = mapper_device_add_signal(source, MAPPER_DIR_OUT, 1,
                                       "/outsig", 1, MAPPER_FLOAT, NULL,
                                       &mn, &mx, NULL);

    eprintf("Output signal /outsig registered.\n");
    eprintf("Number of outputs: %d\n",
            mapper_device_get_num_signals(source, MAPPER_DIR_OUT));
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

void insig_handler(mapper_signal sig, mapper_id instance_id, int length,
                   mapper_type type, const void *value, mapper_time t)
{
    if (value) {
        eprintf("handler: Got %f\n", (*(float*)value));
    }
    received++;
}

int setup_destination()
{
    destination = mapper_device_new("testmapprotocol-recv", 0);
    if (!destination)
        goto error;
    eprintf("destination created.\n");

    float mn=0, mx=1;

    recvsig = mapper_device_add_signal(destination, MAPPER_DIR_IN, 1,
                                       "/insig", 1, MAPPER_FLOAT, NULL,
                                       &mn, &mx, insig_handler);

    eprintf("Input signal /insig registered.\n");
    eprintf("Number of inputs: %d\n",
            mapper_device_get_num_signals(destination, MAPPER_DIR_IN));
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

void set_map_protocol(mapper_protocol proto)
{
    if (!map)
        return;

    if (!mapper_object_set_prop((mapper_object)map, MAPPER_PROP_PROTOCOL, NULL,
                                1, MAPPER_INT32, &proto, 1)) {
        // protocol not changed, exit
        return;
    }
    mapper_object_push((mapper_object)map);

    // wait until change has taken effect
    int len;
    mapper_type type;
    const void *val;
    do {
        mapper_device_poll(source, 10);
        mapper_device_poll(destination, 10);
        mapper_object_get_prop_by_index(map, MAPPER_PROP_PROTOCOL, NULL, &len,
                                        &type, &val);
    }
    while (1 != len || MAPPER_INT32 != type || *(int*)val != proto);
}

int setup_map()
{
    map = mapper_map_new(1, &sendsig, 1, &recvsig);
    mapper_object_push(map);

    // wait until map is established
    while (!mapper_map_ready(map)) {
        mapper_device_poll(destination, 10);
        mapper_device_poll(source, 10);
    }

    return 0;
}

void wait_ready()
{
    while (!(mapper_device_ready(source) && mapper_device_ready(destination))) {
        mapper_device_poll(source, 10);
        mapper_device_poll(destination, 10);
    }
}

void loop()
{
    int i;
    const char *name;
    mapper_object_get_prop_by_index(sendsig, MAPPER_PROP_NAME, NULL, NULL, NULL,
                                    (const void**)&name);
    for (i = 0; i < 10; i++) {
        mapper_device_poll(source, 0);
        float val = i * 1.0f;
        eprintf("Updating signal %s to %f\n", name, val);
        mapper_signal_set_value(sendsig, 0, 1, MAPPER_FLOAT, &val, MAPPER_NOW);
        sent++;
        mapper_device_poll(destination, period);
    }
}

int main(int argc, char **argv)
{
    int i, j, result = 0;

    // process flags for -v verbose, -h help
    for (i = 1; i < argc; i++) {
        if (argv[i] && argv[i][0] == '-') {
            int len = strlen(argv[i]);
            for (j = 1; j < len; j++) {
                switch (argv[i][j]) {
                    case 'h':
                        printf("testmapprotocol.c: possible arguments "
                               "-q quiet (suppress output), "
                               "-f fast (execute quickly), "
                               "-h help\n");
                        return 1;
                        break;
                    case 'f':
                        period = 1;
                        break;
                    case 'q':
                        verbose = 0;
                        break;
                    default:
                        break;
                }
            }
        }
    }

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

    wait_ready();

    if (setup_map()) {
        eprintf("Error initializing map.\n");
        result = 1;
        goto done;
    }

    eprintf("SENDING UDP\n");
    loop();

    set_map_protocol(MAPPER_PROTO_TCP);
    eprintf("SENDING TCP\n");
    loop();

    set_map_protocol(MAPPER_PROTO_UDP);
    eprintf("SENDING UDP AGAIN\n");
    loop();

    if (sent != received) {
        eprintf("Not all sent messages were received.\n");
        eprintf("Updated value %d time%s, but received %d of them.\n",
                sent, sent == 1 ? "" : "s", received);
        result = 1;
    }

done:
    cleanup_destination();
    cleanup_source();
    printf("\r..................................................Test %s\x1B[0m.\n",
           result ? "\x1B[31mFAILED" : "\x1B[32mPASSED");
    return result;
}
