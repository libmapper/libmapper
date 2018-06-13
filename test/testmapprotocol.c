
#include "../src/mapper_internal.h"
#include <mapper/mapper.h>
#include <stdio.h>
#include <math.h>

#include <unistd.h>

mapper_device source = 0;
mapper_device destination = 0;
mapper_signal sendsig = 0;
mapper_signal recvsig = 0;

mapper_map map = 0;

int sent = 0;
int received = 0;

int setup_source()
{
    source = mapper_device_new("testmapprotocol-send", 0, 0);
    if (!source)
        goto error;
    printf("source created.\n");

    float mn=0, mx=1;

    sendsig = mapper_device_add_output_signal(source, "/outsig", 1,
                                              MAPPER_FLOAT, 0, &mn, &mx);

    printf("Output signal /outsig registered.\n");
    printf("Number of outputs: %d\n",
           mapper_device_num_signals(source, MAPPER_DIR_OUTGOING));
    return 0;

error:
    return 1;
}

void cleanup_source()
{
    if (source) {
        printf("Freeing source.. ");
        fflush(stdout);
        mapper_device_free(source);
        printf("ok\n");
    }
}

void insig_handler(mapper_signal sig, mapper_id instance_id, const void *value,
                   int count, mapper_timetag timetag)
{
    if (value) {
        printf("handler: Got %f\n", (*(float*)value));
    }
    received++;
}

int setup_destination()
{
    destination = mapper_device_new("testmapprotocol-recv", 0, 0);
    if (!destination)
        goto error;
    printf("destination created.\n");

    float mn=0, mx=1;

    recvsig = mapper_device_add_input_signal(destination, "/insig", 1,
                                             MAPPER_FLOAT, 0, &mn, &mx,
                                             insig_handler, 0);

    printf("Input signal /insig registered.\n");
    printf("Number of inputs: %d\n",
           mapper_device_num_signals(destination, MAPPER_DIR_INCOMING));
    return 0;

error:
    return 1;
}

void cleanup_destination()
{
    if (destination) {
        printf("Freeing destination.. ");
        fflush(stdout);
        mapper_device_free(destination);
        printf("ok\n");
    }
}

void set_map_protocol(mapper_protocol proto)
{
    if (!map || (mapper_map_protocol(map) == proto))
        return;

    mapper_map_set_protocol(map, proto);
    mapper_map_push(map);

    // wait until change has taken effect
    while (mapper_map_protocol(map) != proto) {
        mapper_device_poll(source, 10);
        mapper_device_poll(destination, 10);
    }
}

int setup_map()
{
    map = mapper_map_new(1, &sendsig, 1, &recvsig);
    mapper_map_push(map);

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
    for (i = 0; i < 10; i++) {
        mapper_device_poll(source, 0);
        printf("Updating signal %s to %f\n", mapper_signal_name(sendsig),
               (i * 1.0f));
        mapper_signal_update_float(sendsig, (i * 1.0f));
        sent++;
        mapper_device_poll(destination, 100);
    }
}

int main()
{
    int result = 0;

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

    wait_ready();

    if (setup_map()) {
        printf("Error initializing map.\n");
        result = 1;
        goto done;
    }

    printf("SENDING UDP\n");
    loop();

    set_map_protocol(MAPPER_PROTO_TCP);
    printf("SENDING TCP\n");
    loop();

    set_map_protocol(MAPPER_PROTO_UDP);
    printf("SENDING UDP AGAIN\n");
    loop();

    if (sent != received) {
        printf("Not all sent messages were received.\n");
        printf("Updated value %d time%s, but received %d of them.\n",
               sent, sent == 1 ? "" : "s", received);
        result = 1;
    }

done:
    cleanup_destination();
    cleanup_source();
    printf("Test %s.\n", result ? "FAILED" : "PASSED");
    return result;
}
