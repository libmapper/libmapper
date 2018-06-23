
#include <stdio.h>
#include <string.h>
#include <lo/lo_lowlevel.h>
#include "../src/mapper_internal.h"

#define eprintf(format, ...) do {               \
    if (verbose)                                \
        fprintf(stdout, format, ##__VA_ARGS__); \
} while(0)

int verbose = 1;

void printobject(mapper_object obj)
{
    if (verbose)
        mapper_object_print(obj, 0);
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
                        eprintf("testgraph.c: possible arguments "
                                "-q quiet (suppress output), "
                                "-h help\n");
                        return 1;
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

    lo_message lom;
    mapper_msg msg;
    uint64_t id = 1;
    mapper_graph graph = mapper_graph_new(0);

    mapper_object *list, *list2;
    mapper_device dev;
    mapper_signal sig;

    /* Test the graph functions */

    lom = lo_message_new();
    if (!lom) {
        result = 1;
        goto done;
    }
    lo_message_add_string(lom, "@port");
    lo_message_add_int32(lom, 1234);
    lo_message_add_string(lom, "@host");
    lo_message_add_string(lom, "localhost");
    lo_message_add_string(lom, "@num_inputs");
    lo_message_add_int32(lom, 2);
    lo_message_add_string(lom, "@num_outputs");
    lo_message_add_int32(lom, 2);

    if (!(msg = mapper_msg_parse_props(lo_message_get_argc(lom),
                                       lo_message_get_types(lom),
                                       lo_message_get_argv(lom)))) {
        eprintf("1: Error, parsing failed.\n");
        result = 1;
        goto done;
    }

    mapper_graph_add_or_update_device(graph, "testgraph.1", msg);

    mapper_msg_free(msg);
    lo_message_free(lom);

    lom = lo_message_new();
    if (!lom) {
        result = 1;
        goto done;
    }
    lo_message_add_string(lom, "@port");
    lo_message_add_int32(lom, 1234);
    lo_message_add_string(lom, "@host");
    lo_message_add_string(lom, "localhost");
    lo_message_add_string(lom, "@num_inputs");
    lo_message_add_int32(lom, 2);
    lo_message_add_string(lom, "@num_outputs");
    lo_message_add_int32(lom, 1);

    if (!(msg = mapper_msg_parse_props(lo_message_get_argc(lom),
                                       lo_message_get_types(lom),
                                       lo_message_get_argv(lom)))) {
        eprintf("1: Error, parsing failed.\n");
        result = 1;
        goto done;
    }

    mapper_graph_add_or_update_device(graph, "testgraph__.2", msg);

    mapper_msg_free(msg);
    lo_message_free(lom);

    lom = lo_message_new();
    if (!lom) {
        result = 1;
        goto done;
    }
    lo_message_add_string(lom, "@port");
    lo_message_add_int32(lom, 3000);
    lo_message_add_string(lom, "@host");
    lo_message_add_string(lom, "192.168.0.100");

    if (!(msg = mapper_msg_parse_props(lo_message_get_argc(lom),
                                       lo_message_get_types(lom),
                                       lo_message_get_argv(lom)))) {
        eprintf("1: Error, parsing failed.\n");
        result = 1;
        goto done;
    }

    mapper_graph_add_or_update_device(graph, "testgraph.3", msg);

    mapper_msg_free(msg);
    lo_message_free(lom);

    lom = lo_message_new();
    if (!lom) {
        result = 1;
        goto done;
    }
    lo_message_add_string(lom, "@port");
    lo_message_add_int32(lom, 5678);
    lo_message_add_string(lom, "@host");
    lo_message_add_string(lom, "192.168.0.100");

    if (!(msg = mapper_msg_parse_props(lo_message_get_argc(lom),
                                       lo_message_get_types(lom),
                                       lo_message_get_argv(lom)))) {
        eprintf("1: Error, parsing failed.\n");
        result = 1;
        goto done;
    }

    mapper_graph_add_or_update_device(graph, "testgraph__.4", msg);

    mapper_msg_free(msg);
    lo_message_free(lom);

    lom = lo_message_new();
    if (!lom) {
        result = 1;
        goto done;
    }
    lo_message_add_string(lom, "@direction");
    lo_message_add_string(lom, "input");
    lo_message_add_string(lom, "@type");
    lo_message_add_char(lom, 'f');
    lo_message_add_string(lom, "@id");
    lo_message_add_int64(lom, id);

    if (!(msg = mapper_msg_parse_props(lo_message_get_argc(lom),
                                       lo_message_get_types(lom),
                                       lo_message_get_argv(lom)))) {
        eprintf("1: Error, parsing failed.\n");
        result = 1;
        goto done;
    }

    mapper_graph_add_or_update_signal(graph, "in1", "testgraph.1", msg);

    mapper_msg_free(msg);
    lo_message_free(lom);

    lom = lo_message_new();
    if (!lom) {
        result = 1;
        goto done;
    }

    id++;
    lo_message_add_string(lom, "@direction");
    lo_message_add_string(lom, "input");
    lo_message_add_string(lom, "@type");
    lo_message_add_char(lom, 'f');
    lo_message_add_string(lom, "@id");
    lo_message_add_int64(lom, id);

    if (!(msg = mapper_msg_parse_props(lo_message_get_argc(lom),
                                       lo_message_get_types(lom),
                                       lo_message_get_argv(lom)))) {
        eprintf("1: Error, parsing failed.\n");
        result = 1;
        goto done;
    }

    mapper_graph_add_or_update_signal(graph, "in2", "testgraph.1", msg);

    mapper_msg_free(msg);
    lo_message_free(lom);

    lom = lo_message_new();
    if (!lom) {
        result = 1;
        goto done;
    }

    id++;
    lo_message_add_string(lom, "@direction");
    lo_message_add_string(lom, "output");
    lo_message_add_string(lom, "@type");
    lo_message_add_char(lom, 'f');
    lo_message_add_string(lom, "@id");
    lo_message_add_int64(lom, id);

    if (!(msg = mapper_msg_parse_props(lo_message_get_argc(lom),
                                       lo_message_get_types(lom),
                                       lo_message_get_argv(lom)))) {
        eprintf("1: Error, parsing failed.\n");
        result = 1;
        goto done;
    }

    mapper_graph_add_or_update_signal(graph, "out1", "testgraph.1", msg);

    mapper_msg_free(msg);
    lo_message_free(lom);

    lom = lo_message_new();
    if (!lom) {
        result = 1;
        goto done;
    }

    id++;
    lo_message_add_string(lom, "@direction");
    lo_message_add_string(lom, "output");
    lo_message_add_string(lom, "@type");
    lo_message_add_char(lom, 'f');
    lo_message_add_string(lom, "@id");
    lo_message_add_int64(lom, id);

    if (!(msg = mapper_msg_parse_props(lo_message_get_argc(lom),
                                       lo_message_get_types(lom),
                                       lo_message_get_argv(lom)))) {
        eprintf("1: Error, parsing failed.\n");
        result = 1;
        goto done;
    }

    mapper_graph_add_or_update_signal(graph, "out2", "testgraph.1", msg);

    mapper_msg_free(msg);
    lo_message_free(lom);

    lom = lo_message_new();
    if (!lom) {
        result = 1;
        goto done;
    }

    id++;
    lo_message_add_string(lom, "@direction");
    lo_message_add_string(lom, "output");
    lo_message_add_string(lom, "@type");
    lo_message_add_char(lom, 'f');
    lo_message_add_string(lom, "@id");
    lo_message_add_int64(lom, id);

    if (!(msg = mapper_msg_parse_props(lo_message_get_argc(lom),
                                       lo_message_get_types(lom),
                                       lo_message_get_argv(lom)))) {
        eprintf("1: Error, parsing failed.\n");
        result = 1;
        goto done;
    }

    mapper_graph_add_or_update_signal(graph, "out1", "testgraph__.2", msg);

    mapper_msg_free(msg);
    lo_message_free(lom);

    lom = lo_message_new();
    if (!lom) {
        result = 1;
        goto done;
    }

    id++;
    lo_message_add_string(lom, "@mode");
    lo_message_add_string(lom, "bypass");
    lo_message_add_string(lom, "@id");
    lo_message_add_int64(lom, id);

    if (!(msg = mapper_msg_parse_props(lo_message_get_argc(lom),
                                       lo_message_get_types(lom),
                                       lo_message_get_argv(lom)))) {
        eprintf("1: Error, parsing failed.\n");
        result = 1;
        goto done;
    }

    const char *src_sig_name = "testgraph.1/out2";
    mapper_graph_add_or_update_map(graph, 1, &src_sig_name, "testgraph__.2/in1",
                                   msg);

    mapper_msg_free(msg);
    lo_message_free(lom);

    lom = lo_message_new();
    if (!lom) {
        result = 1;
        goto done;
    }

    id++;
    lo_message_add_string(lom, "@mode");
    lo_message_add_string(lom, "bypass");
    lo_message_add_string(lom, "@id");
    lo_message_add_int64(lom, id);

    if (!(msg = mapper_msg_parse_props(lo_message_get_argc(lom),
                                       lo_message_get_types(lom),
                                       lo_message_get_argv(lom)))) {
        eprintf("1: Error, parsing failed.\n");
        result = 1;
        goto done;
    }

    src_sig_name = "testgraph__.2/out1";
    mapper_graph_add_or_update_map(graph, 1, &src_sig_name, "testgraph.1/in1",
                                   msg);

    mapper_msg_free(msg);
    lo_message_free(lom);

    lom = lo_message_new();
    if (!lom) {
        result = 1;
        goto done;
    }

    id++;
    lo_message_add_string(lom, "@mode");
    lo_message_add_string(lom, "expression");
    lo_message_add_string(lom, "@expression");
    lo_message_add_string(lom, "(x-10)*80");
    lo_message_add_string(lom, "@src@min");
    lo_message_add_float(lom, 0.f);
    lo_message_add_float(lom, 1.f);
    lo_message_add_string(lom, "@src@max");
    lo_message_add_float(lom, 1.f);
    lo_message_add_float(lom, 2.f);
    lo_message_add_string(lom, "@id");
    lo_message_add_int64(lom, id);

    if (!(msg = mapper_msg_parse_props(lo_message_get_argc(lom),
                                       lo_message_get_types(lom),
                                       lo_message_get_argv(lom)))) {
        eprintf("1: Error, parsing failed.\n");
        result = 1;
        goto done;
    }

    src_sig_name = "testgraph.1/out1";
    mapper_graph_add_or_update_map(graph, 1, &src_sig_name, "testgraph__.2/in2",
                                   msg);

    mapper_msg_free(msg);
    lo_message_free(lom);

    lom = lo_message_new();
    if (!lom) {
        result = 1;
        goto done;
    }

    id++;
    lo_message_add_string(lom, "@mode");
    lo_message_add_string(lom, "expression");
    lo_message_add_string(lom, "@expression");
    lo_message_add_string(lom, "(x-10)*80");
    lo_message_add_string(lom, "@src@min");
    lo_message_add_float(lom, 0.f);
    lo_message_add_float(lom, 1.f);
    lo_message_add_string(lom, "@src@max");
    lo_message_add_float(lom, 1.f);
    lo_message_add_float(lom, 2.f);
    lo_message_add_string(lom, "@id");
    lo_message_add_int64(lom, id);

    if (!(msg = mapper_msg_parse_props(lo_message_get_argc(lom),
                                       lo_message_get_types(lom),
                                       lo_message_get_argv(lom)))) {
        eprintf("1: Error, parsing failed.\n");
        result = 1;
        goto done;
    }

    src_sig_name = "testgraph.1/out1";
    mapper_graph_add_or_update_map(graph, 1, &src_sig_name, "testgraph__.2/in1",
                                   msg);

    mapper_msg_free(msg);
    lo_message_free(lom);

    /*********/

    if (verbose) {
        eprintf("Dump:\n");
        mapper_graph_print(graph);
    }

    /*********/

    eprintf("\n--- Devices ---\n");

    eprintf("\nWalk the whole graph:\n");
    list = mapper_graph_get_objects(graph, MAPPER_OBJ_DEVICE);
    int count=0;
    if (!list) {
        eprintf("mapper_graph_get_objects(devices) returned 0.\n");
        result = 1;
        goto done;
    }
    if (!*list) {
        eprintf("mapper_graph_get_objects(devices) returned something "
               "which pointed to 0.\n");
        result = 1;
        goto done;
    }

    while (list) {
        count ++;
        printobject(*list);
        list = mapper_object_list_next(list);
    }

    if (count != 4) {
        eprintf("Expected 4 records, but counted %d.\n", count);
        result = 1;
        goto done;
    }

    /*********/

    eprintf("\nFind device named 'testgraph.3':\n");

    list = mapper_graph_get_objects(graph, MAPPER_OBJ_DEVICE);
    list = mapper_object_list_filter(list, MAPPER_PROP_NAME, NULL, 1,
                                     MAPPER_STRING, "testgraph.3",
                                     MAPPER_OP_EQUAL);
    if (!list) {
        eprintf("Not found.\n");
        result = 1;
        goto done;
    }
    count = mapper_object_list_get_length(list);
    if (count != 1) {
        eprintf("Found %d devices (Should be 1).\n", count);
        result = 1;
        goto done;
    }

    printobject((mapper_object)*list);

    /*********/

    eprintf("\nFind device named 'dummy':\n");

    list = mapper_graph_get_objects(graph, MAPPER_OBJ_DEVICE);
    list = mapper_object_list_filter(list, MAPPER_PROP_NAME, NULL, 1,
                                     MAPPER_STRING, "dummy", MAPPER_OP_EQUAL);
    if (list) {
        eprintf("unexpected found 'dummy': %p\n", *list);
        result = 1;
        goto done;
    }
    eprintf("  not found, good.\n");

    /*********/

    eprintf("\nFind devices matching '__':\n");

    list = mapper_graph_get_objects(graph, MAPPER_OBJ_DEVICE);
    list = mapper_object_list_filter(list, MAPPER_PROP_NAME, NULL, 1,
                                     MAPPER_STRING, "*__*", MAPPER_OP_EQUAL);

    count=0;
    if (!list) {
        eprintf("objects(devices) -> filter(name) returned 0.\n");
        result = 1;
        goto done;
    }
    if (!*list) {
        eprintf("objects(devices) -> filter(name) returned something "
                "which pointed to 0.\n");
        result = 1;
        goto done;
    }
    while (list) {
        count ++;
        printobject(*list);
        list = mapper_object_list_next(list);
    }

    if (count != 2) {
        eprintf("Expected 2 records, but counted %d.\n", count);
        result = 1;
        goto done;
    }

    /*********/

    eprintf("\nFind devices with property 'host'=='192.168.0.100':\n");

    list = mapper_graph_get_objects(graph, MAPPER_OBJ_DEVICE);
    list = mapper_object_list_filter(list, MAPPER_PROP_HOST, NULL, 1,
                                     MAPPER_STRING, "192.168.0.100",
                                     MAPPER_OP_EQUAL);

    count=0;
    if (!list) {
        eprintf("objects(devices) -> filter(host) returned 0.\n");
        result = 1;
        goto done;
    }
    if (!*list) {
        eprintf("objects(devices) -> filter(host) returned something "
                "which pointed to 0.\n");
        result = 1;
        goto done;
    }

    while (list) {
        count ++;
        printobject(*list);
        list = mapper_object_list_next(list);
    }

    if (count != 2) {
        eprintf("Expected 2 records, but counted %d.\n", count);
        result = 1;
        goto done;
    }

    /*********/

    eprintf("\nFind devices with property 'port'<5678:\n");

    int port = 5678;
    list = mapper_graph_get_objects(graph, MAPPER_OBJ_DEVICE);
    list = mapper_object_list_filter(list, MAPPER_PROP_PORT, NULL, 1,
                                     MAPPER_INT32, &port, MAPPER_OP_LESS_THAN);

    count=0;
    if (!list) {
        eprintf("objects(devices) -> filter(port) returned 0.\n");
        result = 1;
        goto done;
    }
    if (!*list) {
        eprintf("objects(devices) -> filter(port) returned something "
                "which pointed to 0.\n");
        result = 1;
        goto done;
    }

    while (list) {
        count ++;
        printobject(*list);
        list = mapper_object_list_next(list);
    }

    if (count != 3) {
        eprintf("Expected 3 records, but counted %d.\n", count);
        result = 1;
        goto done;
    }

    /*********/

    eprintf("\nFind devices with property 'num_outputs'==2:\n");
    int temp = 2;
    list = mapper_graph_get_objects(graph, MAPPER_OBJ_DEVICE);
    list = mapper_object_list_filter(list, MAPPER_PROP_UNKNOWN, "num_outputs",
                                     1, MAPPER_INT32, &temp, MAPPER_OP_EQUAL);

    count=0;
    if (!list) {
        eprintf("objects(devices) -> filter(num_outputs) returned 0.\n");
        result = 1;
        goto done;
    }
    if (!*list) {
        eprintf("objects(devices) -> filter(num_outputs) returned something "
                "which pointed to 0.\n");
        result = 1;
        goto done;
    }

    while (list) {
        count ++;
        printobject(*list);
        list = mapper_object_list_next(list);
    }

    if (count != 1) {
        eprintf("Expected 1 records, but counted %d.\n", count);
        result = 1;
        goto done;
    }

    /*********/

    eprintf("\nFind devices with properties 'host'!='localhost' AND 'port'>=4000:\n");

    port = 4000;
    list = mapper_graph_get_objects(graph, MAPPER_OBJ_DEVICE);
    list = mapper_object_list_filter(list, MAPPER_PROP_HOST, NULL, 1,
                                     MAPPER_STRING, "localhost",
                                     MAPPER_OP_NOT_EQUAL);
    list = mapper_object_list_filter(list, MAPPER_PROP_PORT, NULL, 1,
                                     MAPPER_INT32, &port,
                                     MAPPER_OP_GREATER_THAN_OR_EQUAL);

    count=0;
    if (!list) {
        eprintf("objects(devices) -> filter(host) -> filter(port) returned 0.\n");
        result = 1;
        goto done;
    }
    if (!*list) {
        eprintf("objects(devices) -> filter(host) -> filter(port) returned "
                "something which pointed to 0.\n");
        result = 1;
        goto done;
    }

    while (list) {
        count ++;
        printobject(*list);
        list = mapper_object_list_next(list);
    }

    if (count != 1) {
        eprintf("Expected 1 record, but counted %d.\n", count);
        result = 1;
        goto done;
    }

    /*********/

    eprintf("\n--- Signals ---\n");

    eprintf("\nFind all signals for device 'testgraph.1':\n");

    list = mapper_graph_get_objects(graph, MAPPER_OBJ_DEVICE);
    list = mapper_object_list_filter(list, MAPPER_PROP_NAME, NULL, 1,
                                     MAPPER_STRING, "testgraph.1",
                                     MAPPER_OP_EQUAL);
    if (list && (dev = (mapper_device)*list))
        list = mapper_device_get_signals(dev, MAPPER_DIR_ANY);

    count=0;
    if (!list) {
        eprintf("mapper_device_get_signals() returned 0.\n");
        result = 1;
        goto done;
    }
    if (!*list) {
        eprintf("mapper_device_get_signals() returned something "
                "which pointed to 0.\n");
        result = 1;
        goto done;
    }

    while (list) {
        count ++;
        printobject(*list);
        list = mapper_object_list_next(list);
    }

    if (count != 4) {
        eprintf("Expected 4 records, but counted %d.\n", count);
        result = 1;
        goto done;
    }

    /*********/

    eprintf("\nFind all signals for device 'testgraph__xx.2':\n");

    list = mapper_graph_get_objects(graph, MAPPER_OBJ_DEVICE);
    list = mapper_object_list_filter(list, MAPPER_PROP_NAME, NULL, 1,
                                     MAPPER_STRING, "testgraph__xx.2",
                                     MAPPER_OP_EQUAL);
    if (list && (dev = (mapper_device)*list))
        list = mapper_device_get_signals(dev, MAPPER_DIR_ANY);

    count=0;
    if (list) {
        eprintf("mapper_device_get_signals() incorrectly found something.\n");
        printobject(*list);
        mapper_object_list_free(list);
        result = 1;
        goto done;
    }
    else
        eprintf("  correctly returned 0.\n");

    /*********/

    eprintf("\nFind all outputs for device 'testgraph__.2':\n");

    list = mapper_graph_get_objects(graph, MAPPER_OBJ_DEVICE);
    list = mapper_object_list_filter(list, MAPPER_PROP_NAME, NULL, 1,
                                     MAPPER_STRING, "testgraph__.2",
                                     MAPPER_OP_EQUAL);
    if (list && (dev = (mapper_device)*list))
        list = mapper_device_get_signals(dev, MAPPER_DIR_ANY);

    count=0;
    if (!list) {
        eprintf("mapper_device_get_signals() returned 0.\n");
        result = 1;
        goto done;
    }
    if (!*list) {
        eprintf("mapper_device_get_signals() returned something "
                "which pointed to 0.\n");
        result = 1;
        goto done;
    }

    while (list) {
        count ++;
        printobject(*list);
        list = mapper_object_list_next(list);
    }

    if (count != 3) {
        eprintf("Expected 3 records, but counted %d.\n", count);
        result = 1;
        goto done;
    }

    /*********/

    eprintf("\nFind signal matching 'in' for device 'testgraph.1':\n");

    list = mapper_graph_get_objects(graph, MAPPER_OBJ_DEVICE);
    list = mapper_object_list_filter(list, MAPPER_PROP_NAME, NULL, 1,
                                     MAPPER_STRING, "testgraph.1",
                                     MAPPER_OP_EQUAL);
    if (list && (dev = (mapper_device)*list)) {
        list = mapper_device_get_signals(dev, MAPPER_DIR_ANY);
        list = mapper_object_list_filter(list, MAPPER_PROP_NAME, NULL, 1,
                                         MAPPER_STRING, "*in*", MAPPER_OP_EQUAL);
    }

    count=0;
    if (!list) {
        eprintf("mapper_device_get_signals() -> filter(name) returned 0.\n");
        result = 1;
        goto done;
    }
    if (!*list) {
        eprintf("mapper_device_get_signals() -> filter(name) returned "
                "something which pointed to 0.\n");
        result = 1;
        goto done;
    }

    while (list) {
        count ++;
        printobject(*list);
        list = mapper_object_list_next(list);
    }

    if (count != 2) {
        eprintf("Expected 2 records, but counted %d.\n", count);
        result = 1;
        goto done;
    }

    /*********/

    eprintf("\nFind signal matching 'out' for device 'testgraph.1':\n");

    list = mapper_graph_get_objects(graph, MAPPER_OBJ_DEVICE);
    list = mapper_object_list_filter(list, MAPPER_PROP_NAME, NULL, 1,
                                     MAPPER_STRING, "testgraph.1",
                                     MAPPER_OP_EQUAL);
    if (list && (dev = (mapper_device)*list)) {
        list = mapper_device_get_signals(dev, MAPPER_DIR_ANY);
        list = mapper_object_list_filter(list, MAPPER_PROP_NAME, NULL, 1,
                                         MAPPER_STRING, "*out*",
                                         MAPPER_OP_EQUAL);
    }

    count=0;
    if (!list) {
        eprintf("mapper_device_get_signals() -> filter(name) returned 0.\n");
        result = 1;
        goto done;
    }
    if (!*list) {
        eprintf("mapper_device_get_signals() -> filter(name) returned "
                "something which pointed to 0.\n");
        result = 1;
        goto done;
    }

    while (list) {
        count ++;
        printobject(*list);
        list = mapper_object_list_next(list);
    }

    if (count != 2) {
        eprintf("Expected 2 records, but counted %d.\n", count);
        result = 1;
        goto done;
    }

    /*********/

    eprintf("\nFind signal matching 'out' for device 'testgraph__.2':\n");

    list = mapper_graph_get_objects(graph, MAPPER_OBJ_DEVICE);
    list = mapper_object_list_filter(list, MAPPER_PROP_NAME, NULL, 1,
                                     MAPPER_STRING, "testgraph__.2",
                                     MAPPER_OP_EQUAL);
    if (list && (dev = (mapper_device)*list)) {
        list = mapper_device_get_signals(dev, MAPPER_DIR_ANY);
        list = mapper_object_list_filter(list, MAPPER_PROP_NAME, NULL, 1,
                                         MAPPER_STRING, "*out*",
                                         MAPPER_OP_EQUAL);
    }

    count=0;
    if (!list) {
        eprintf("mapper_device_get_signals() -> filter(name) returned 0.\n");
        result = 1;
        goto done;
    }
    if (!*list) {
        eprintf("mapper_device_get_signals() -> filter(name) returned "
                "something which pointed to 0.\n");
        result = 1;
        goto done;
    }

    while (list) {
        count ++;
        printobject(*list);
        list = mapper_object_list_next(list);
    }

    if (count != 1) {
        eprintf("Expected 1 record, but counted %d.\n", count);
        result = 1;
        goto done;
    }

    /*********/

    eprintf("\n--- links ---\n");

    eprintf("\nFind all links':\n");

    list = mapper_graph_get_objects(graph, MAPPER_OBJ_LINK);

    count=0;
    if (!list) {
        eprintf("mapper_device_get_objects(links) returned 0.\n");
        result = 1;
        goto done;
    }
    if (!*list) {
        eprintf("mapper_device_get_objects(links) returned something "
                "which pointed to 0.\n");
        result = 1;
        goto done;
    }

    while (list) {
        count ++;
        printobject(*list);
        list = mapper_object_list_next(list);
    }

    if (count != 1) {
        eprintf("Expected 1 records, but counted %d.\n", count);
        result = 1;
        goto done;
    }

    /*********/

    eprintf("\n--- maps ---\n");

    eprintf("\nFind maps with source 'out1':\n");

    list = mapper_graph_get_objects(graph, MAPPER_OBJ_SIGNAL);
    list = mapper_object_list_filter(list, MAPPER_PROP_NAME, NULL, 1,
                                     MAPPER_STRING, "out1", MAPPER_OP_EQUAL);
    list2 = 0;
    while (list) {
        list2 = mapper_object_list_union(list2, mapper_signal_get_maps((mapper_signal)*list,
                                                                       MAPPER_DIR_OUT));
        list = mapper_object_list_next(list);
    }

    count=0;
    if (!list2) {
        eprintf("combined query returned 0.\n");
        result = 1;
        goto done;
    }
    if (!*list2) {
        eprintf("combined query returned something which pointed to 0.\n");
        result = 1;
        goto done;
    }

    while (list2) {
        count ++;
        printobject(*list2);
        list2 = mapper_object_list_next(list2);
    }

    if (count != 3) {
        eprintf("Expected 3 records, but counted %d.\n", count);
        result = 1;
        goto done;
    }

    /*********/

    eprintf("\nFind maps for device 'testgraph.1', source 'out1':\n");

    list = mapper_graph_get_objects(graph, MAPPER_OBJ_DEVICE);
    list = mapper_object_list_filter(list, MAPPER_PROP_NAME, NULL, 1,
                                     MAPPER_STRING, "testgraph.1", MAPPER_OP_EQUAL);
    if (list && (dev = (mapper_device)*list)) {
        list = mapper_device_get_signals(dev, MAPPER_DIR_ANY);
        list = mapper_object_list_filter(list, MAPPER_PROP_NAME, NULL, 1,
                                         MAPPER_STRING, "out1", MAPPER_OP_EQUAL);
        if (list && (sig = (mapper_signal)*list)) {
            list = mapper_signal_get_maps(sig, 0);
        }
    }

    count=0;
    if (!list) {
        eprintf("mapper_signal_get_maps() returned 0.\n");
        result = 1;
        goto done;
    }
    if (!*list) {
        eprintf("mapper_signal_get_maps() returned something which pointed to 0.\n");
        result = 1;
        goto done;
    }

    while (list) {
        count ++;
        printobject(*list);
        list = mapper_object_list_next(list);
    }

    if (count != 2) {
        eprintf("Expected 2 records, but counted %d.\n", count);
        result = 1;
        goto done;
    }

    /*********/

    eprintf("\nFind maps with destination signal named 'in2':\n");

    list = mapper_graph_get_objects(graph, MAPPER_OBJ_SIGNAL);
    list = mapper_object_list_filter(list, MAPPER_PROP_NAME, NULL, 1,
                                     MAPPER_STRING, "in2", MAPPER_OP_EQUAL);
    list2 = 0;
    while (list) {
        list2 = mapper_object_list_union(list2, mapper_signal_get_maps((mapper_signal)*list,
                                                                       MAPPER_DIR_IN));
        list = mapper_object_list_next(list);
    }

    count=0;
    if (!list2) {
        eprintf("combined query returned 0.\n");
        result = 1;
        goto done;
    }
    if (!*list2) {
        eprintf("combined query returned something which pointed to 0.\n");
        result = 1;
        goto done;
    }

    while (list2) {
        count ++;
        printobject(*list2);
        list2 = mapper_object_list_next(list2);
    }

    if (count != 1) {
        eprintf("Expected 1 record, but counted %d.\n", count);
        result = 1;
        goto done;
    }

    /*********/

    eprintf("\nFind maps for device 'testgraph__.2', destination 'in1':\n");
    list = mapper_graph_get_objects(graph, MAPPER_OBJ_DEVICE);
    list = mapper_object_list_filter(list, MAPPER_PROP_NAME, NULL, 1,
                                     MAPPER_STRING, "testgraph__.2",
                                     MAPPER_OP_EQUAL);
    if (list && (dev = (mapper_device)*list)) {
        list = mapper_device_get_signals(dev, MAPPER_DIR_ANY);
        list = mapper_object_list_filter(list, MAPPER_PROP_NAME, NULL, 1,
                                         MAPPER_STRING, "in1", MAPPER_OP_EQUAL);
        if (list && (sig = (mapper_signal)*list))
            list = mapper_signal_get_maps(sig, MAPPER_DIR_IN);
    }

    count=0;
    if (!list) {
        eprintf("mapper_signal_get_maps() returned 0.\n");
        result = 1;
        goto done;
    }
    if (!*list) {
        eprintf("mapper_signal_get_maps() returned something which pointed to 0.\n");
        result = 1;
        goto done;
    }

    while (list) {
        count ++;
        printobject(*list);
        list = mapper_object_list_next(list);
    }

    if (count != 2) {
        eprintf("Expected 2 records, but counted %d.\n", count);
        result = 1;
        goto done;
    }

    /*********/

    eprintf("\nFind maps for source device 'testgraph__.2', signal 'out1'"
            "\n          AND dest device 'testgraph.1', signal 'in1':\n");

    // get maps with source signal
    list = mapper_graph_get_objects(graph, MAPPER_OBJ_DEVICE);
    list = mapper_object_list_filter(list, MAPPER_PROP_NAME, NULL, 1,
                                     MAPPER_STRING, "testgraph__.2",
                                     MAPPER_OP_EQUAL);
    if (list && (dev = (mapper_device)*list)) {
        list = mapper_device_get_signals(dev, MAPPER_DIR_ANY);
        list = mapper_object_list_filter(list, MAPPER_PROP_NAME, NULL, 1,
                                         MAPPER_STRING, "out1", MAPPER_OP_EQUAL);
        if (list && (sig = (mapper_signal)*list))
            list = mapper_signal_get_maps(sig, MAPPER_DIR_OUT);
    }

    // get maps with destination signal
    list2 = mapper_graph_get_objects(graph, MAPPER_OBJ_DEVICE);
    list2 = mapper_object_list_filter(list2, MAPPER_PROP_NAME, NULL, 1,
                                      MAPPER_STRING, "testgraph.1",
                                      MAPPER_OP_EQUAL);
    if (list2 && (dev = (mapper_device)*list2)) {
        list2 = mapper_device_get_signals(dev, MAPPER_DIR_ANY);
        list2 = mapper_object_list_filter(list2, MAPPER_PROP_NAME, NULL, 1,
                                          MAPPER_STRING, "in1", MAPPER_OP_EQUAL);
        if (list2 && (sig = (mapper_signal)*list2))
            list2 = mapper_signal_get_maps(sig, MAPPER_DIR_IN);
    }

    // intersect map queries
    list = mapper_object_list_intersection(list, list2);

    count=0;
    if (!list) {
        eprintf("combined query returned 0.\n");
        result = 1;
        goto done;
    }
    if (!*list) {
        eprintf("combined query returned something which pointed to 0.\n");
        result = 1;
        goto done;
    }

    while (list) {
        count ++;
        printobject(*list);
        list = mapper_object_list_next(list);
    }

    if (count != 1) {
        eprintf("Expected 1 records, but counted %d.\n", count);
        result = 1;
        goto done;
    }

    /*********/

    eprintf("\nFind maps for source device 'testgraph__.2', signals matching 'out',"
            "\n          AND dest device 'testgraph.1', all signals:\n");

    // build source query
    list = mapper_graph_get_objects(graph, MAPPER_OBJ_DEVICE);
    list = mapper_object_list_filter(list, MAPPER_PROP_NAME, NULL, 1,
                                     MAPPER_STRING, "testgraph__.2",
                                     MAPPER_OP_EQUAL);
    if (list && (dev = (mapper_device)*list)) {
        list = mapper_device_get_signals(dev, MAPPER_DIR_ANY);
        list = mapper_object_list_filter(list, MAPPER_PROP_NAME, NULL, 1,
                                         MAPPER_STRING, "*out*", MAPPER_OP_EQUAL);
    }

    list2 = 0;
    while (list) {
        list2 = mapper_object_list_union(list2,
                                         mapper_signal_get_maps((mapper_signal)*list,
                                                                MAPPER_DIR_OUT));
        list = mapper_object_list_next(list);
    }

    // build destination query
    list = mapper_graph_get_objects(graph, MAPPER_OBJ_DEVICE);
    list = mapper_object_list_filter(list, MAPPER_PROP_NAME, NULL, 1,
                                     MAPPER_STRING, "testgraph.1",
                                     MAPPER_OP_EQUAL);
    if (list && (dev = (mapper_device)*list))
        list = mapper_device_get_maps(dev, MAPPER_DIR_ANY);

    // intersect queries
    list = mapper_object_list_intersection(list, list2);

    count=0;
    if (!list) {
        eprintf("combined query returned 0.\n");
        result = 1;
        goto done;
    }
    if (!*list) {
        eprintf("combined query returned something which pointed to 0.\n");
        result = 1;
        goto done;
    }

    while (list) {
        count ++;
        printobject(*list);
        list = mapper_object_list_next(list);
    }

    if (count != 1) {
        eprintf("Expected 1 record, but counted %d.\n", count);
        result = 1;
        goto done;
    }

    /*********/
done:
    mapper_graph_free(graph);
    if (!verbose)
        printf("..................................................");
    printf("Test %s\x1B[0m.\n", result ? "\x1B[31mFAILED" : "\x1B[32mPASSED");
    return result;
}
