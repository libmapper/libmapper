
#include <stdio.h>
#include <string.h>
#include <lo/lo_lowlevel.h>
#include "../src/mpr_internal.h"

#define eprintf(format, ...) do {               \
    if (verbose)                                \
        fprintf(stdout, format, ##__VA_ARGS__); \
} while(0)

int verbose = 1;

void printobject(mpr_obj obj)
{
    if (verbose)
        mpr_obj_print(obj, 0);
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
    mpr_msg msg;
    uint64_t id = 1;
    mpr_graph graph = mpr_graph_new(0);

    mpr_list list, list2;
    mpr_dev dev;
    mpr_sig sig;

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

    if (!(msg = mpr_msg_parse_props(lo_message_get_argc(lom),
                                    lo_message_get_types(lom),
                                    lo_message_get_argv(lom)))) {
        eprintf("1: Error, parsing failed.\n");
        result = 1;
        goto done;
    }

    mpr_graph_add_dev(graph, "testgraph.1", msg);

    mpr_msg_free(msg);
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

    if (!(msg = mpr_msg_parse_props(lo_message_get_argc(lom),
                                    lo_message_get_types(lom),
                                    lo_message_get_argv(lom)))) {
        eprintf("1: Error, parsing failed.\n");
        result = 1;
        goto done;
    }

    mpr_graph_add_dev(graph, "testgraph__.2", msg);

    mpr_msg_free(msg);
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

    if (!(msg = mpr_msg_parse_props(lo_message_get_argc(lom),
                                    lo_message_get_types(lom),
                                    lo_message_get_argv(lom)))) {
        eprintf("1: Error, parsing failed.\n");
        result = 1;
        goto done;
    }

    mpr_graph_add_dev(graph, "testgraph.3", msg);

    mpr_msg_free(msg);
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

    if (!(msg = mpr_msg_parse_props(lo_message_get_argc(lom),
                                    lo_message_get_types(lom),
                                    lo_message_get_argv(lom)))) {
        eprintf("1: Error, parsing failed.\n");
        result = 1;
        goto done;
    }

    mpr_graph_add_dev(graph, "testgraph__.4", msg);

    mpr_msg_free(msg);
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

    if (!(msg = mpr_msg_parse_props(lo_message_get_argc(lom),
                                    lo_message_get_types(lom),
                                    lo_message_get_argv(lom)))) {
        eprintf("1: Error, parsing failed.\n");
        result = 1;
        goto done;
    }

    mpr_graph_add_sig(graph, "in1", "testgraph.1", msg);

    mpr_msg_free(msg);
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

    if (!(msg = mpr_msg_parse_props(lo_message_get_argc(lom),
                                    lo_message_get_types(lom),
                                    lo_message_get_argv(lom)))) {
        eprintf("1: Error, parsing failed.\n");
        result = 1;
        goto done;
    }

    mpr_graph_add_sig(graph, "in2", "testgraph.1", msg);

    mpr_msg_free(msg);
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

    if (!(msg = mpr_msg_parse_props(lo_message_get_argc(lom),
                                    lo_message_get_types(lom),
                                    lo_message_get_argv(lom)))) {
        eprintf("1: Error, parsing failed.\n");
        result = 1;
        goto done;
    }

    mpr_graph_add_sig(graph, "out1", "testgraph.1", msg);

    mpr_msg_free(msg);
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

    if (!(msg = mpr_msg_parse_props(lo_message_get_argc(lom),
                                    lo_message_get_types(lom),
                                    lo_message_get_argv(lom)))) {
        eprintf("1: Error, parsing failed.\n");
        result = 1;
        goto done;
    }

    mpr_graph_add_sig(graph, "out2", "testgraph.1", msg);

    mpr_msg_free(msg);
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

    if (!(msg = mpr_msg_parse_props(lo_message_get_argc(lom),
                                    lo_message_get_types(lom),
                                    lo_message_get_argv(lom)))) {
        eprintf("1: Error, parsing failed.\n");
        result = 1;
        goto done;
    }

    mpr_graph_add_sig(graph, "out1", "testgraph__.2", msg);

    mpr_msg_free(msg);
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
    lo_message_add_string(lom, "@scope");
    lo_message_add_string(lom, "testgraph__.2");

    if (!(msg = mpr_msg_parse_props(lo_message_get_argc(lom),
                                    lo_message_get_types(lom),
                                    lo_message_get_argv(lom)))) {
        eprintf("1: Error, parsing failed.\n");
        result = 1;
        goto done;
    }

    const char *src_sig_name = "testgraph.1/out2";
    mpr_graph_add_map(graph, 1, &src_sig_name, "testgraph__.2/in1", msg);

    mpr_msg_free(msg);
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

    if (!(msg = mpr_msg_parse_props(lo_message_get_argc(lom),
                                    lo_message_get_types(lom),
                                    lo_message_get_argv(lom)))) {
        eprintf("1: Error, parsing failed.\n");
        result = 1;
        goto done;
    }

    src_sig_name = "testgraph__.2/out1";
    mpr_graph_add_map(graph, 1, &src_sig_name, "testgraph.1/in1", msg);

    mpr_msg_free(msg);
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

    if (!(msg = mpr_msg_parse_props(lo_message_get_argc(lom),
                                    lo_message_get_types(lom),
                                    lo_message_get_argv(lom)))) {
        eprintf("1: Error, parsing failed.\n");
        result = 1;
        goto done;
    }

    src_sig_name = "testgraph.1/out1";
    mpr_graph_add_map(graph, 1, &src_sig_name, "testgraph__.2/in2", msg);

    mpr_msg_free(msg);
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

    if (!(msg = mpr_msg_parse_props(lo_message_get_argc(lom),
                                    lo_message_get_types(lom),
                                    lo_message_get_argv(lom)))) {
        eprintf("1: Error, parsing failed.\n");
        result = 1;
        goto done;
    }

    src_sig_name = "testgraph.1/out1";
    mpr_graph_add_map(graph, 1, &src_sig_name, "testgraph__.2/in1", msg);

    mpr_msg_free(msg);
    lo_message_free(lom);

    /*********/

    if (verbose) {
        eprintf("Dump:\n");
        mpr_graph_print(graph);
    }

    /*********/

    eprintf("\n--- Devices ---\n");

    eprintf("\nWalk the whole graph:\n");
    list = mpr_graph_get_objs(graph, MPR_DEV);
    int count=0;
    if (!list) {
        eprintf("query returned 0.\n");
        result = 1;
        goto done;
    }

    while (list) {
        count ++;
        printobject(*list);
        list = mpr_list_get_next(list);
    }

    if (count != 4) {
        eprintf("Expected 4 records, but counted %d.\n", count);
        result = 1;
        goto done;
    }

    /*********/

    eprintf("\nFind device named 'testgraph.3':\n");

    list = mpr_graph_get_objs(graph, MPR_DEV);
    list = mpr_list_filter(list, MPR_PROP_NAME, NULL, 1, MPR_STR, "testgraph.3",
                           MPR_OP_EQ);
    if (!list) {
        eprintf("Not found.\n");
        result = 1;
        goto done;
    }
    count = mpr_list_get_size(list);
    if (count != 1) {
        eprintf("Found %d devices (Should be 1).\n", count);
        result = 1;
        mpr_list_free(list);
        goto done;
    }

    printobject((mpr_obj)*list);
    mpr_list_free(list);

    /*********/

    eprintf("\nFind device named 'dummy':\n");

    list = mpr_graph_get_objs(graph, MPR_DEV);
    list = mpr_list_filter(list, MPR_PROP_NAME, NULL, 1, MPR_STR, "dummy",
                           MPR_OP_EQ);
    if (mpr_list_get_size(list)) {
        eprintf("unexpectedly found 'dummy': %p\n", *list);
        result = 1;
        mpr_list_free(list);
        goto done;
    }
    eprintf("  not found, good.\n");

    /*********/

    eprintf("\nFind devices matching '__':\n");

    list = mpr_graph_get_objs(graph, MPR_DEV);
    list = mpr_list_filter(list, MPR_PROP_NAME, NULL, 1, MPR_STR, "*__*",
                           MPR_OP_EQ);

    count=0;
    if (!list) {
        eprintf("query returned 0.\n");
        result = 1;
        goto done;
    }

    while (list) {
        ++count;
        printobject(*list);
        list = mpr_list_get_next(list);
    }

    if (count != 2) {
        eprintf("Expected 2 records, but counted %d.\n", count);
        result = 1;
        goto done;
    }

    /*********/

    eprintf("\nFind devices with property 'host'=='192.168.0.100':\n");

    list = mpr_graph_get_objs(graph, MPR_DEV);
    list = mpr_list_filter(list, MPR_PROP_HOST, NULL, 1, MPR_STR,
                           "192.168.0.100", MPR_OP_EQ);

    count=0;
    if (!list) {
        eprintf("query returned 0.\n");
        result = 1;
        goto done;
    }
    if (!*list) {
        eprintf("query returned something which pointed to 0.\n");
        result = 1;
        goto done;
    }

    while (list) {
        ++count;
        printobject(*list);
        list = mpr_list_get_next(list);
    }

    if (count != 2) {
        eprintf("Expected 2 records, but counted %d.\n", count);
        result = 1;
        goto done;
    }

    /*********/

    eprintf("\nFind devices with property 'port'<5678:\n");

    int port = 5678;
    list = mpr_graph_get_objs(graph, MPR_DEV);
    list = mpr_list_filter(list, MPR_PROP_PORT, NULL, 1, MPR_INT32, &port,
                           MPR_OP_LT);

    count=0;
    if (!list) {
        eprintf("query returned 0.\n");
        result = 1;
        goto done;
    }
    if (!*list) {
        eprintf("query returned something which pointed to 0.\n");
        result = 1;
        goto done;
    }

    while (list) {
        ++count;
        printobject(*list);
        list = mpr_list_get_next(list);
    }

    if (count != 3) {
        eprintf("Expected 3 records, but counted %d.\n", count);
        result = 1;
        goto done;
    }

    /*********/

    eprintf("\nFind devices with property 'num_outputs'==2:\n");
    int temp = 2;
    list = mpr_graph_get_objs(graph, MPR_DEV);
    list = mpr_list_filter(list, MPR_PROP_UNKNOWN, "num_outputs", 1, MPR_INT32,
                           &temp, MPR_OP_EQ);

    count=0;
    if (!list) {
        eprintf("query returned 0.\n");
        result = 1;
        goto done;
    }
    if (!*list) {
        eprintf("query returned something which pointed to 0.\n");
        result = 1;
        goto done;
    }

    while (list) {
        ++count;
        printobject(*list);
        list = mpr_list_get_next(list);
    }

    if (count != 1) {
        eprintf("Expected 1 records, but counted %d.\n", count);
        result = 1;
        goto done;
    }

    /*********/

    eprintf("\nFind devices with properties 'host'!='localhost' AND 'port'>=4000:\n");

    port = 4000;
    list = mpr_graph_get_objs(graph, MPR_DEV);
    list = mpr_list_filter(list, MPR_PROP_HOST, NULL, 1, MPR_STR, "localhost",
                           MPR_OP_NEQ);
    list = mpr_list_filter(list, MPR_PROP_PORT, NULL, 1, MPR_INT32, &port,
                           MPR_OP_GTE);

    count=0;
    if (!list) {
        eprintf("query returned 0.\n");
        result = 1;
        goto done;
    }
    if (!*list) {
        eprintf("query returned something which pointed to 0.\n");
        result = 1;
        goto done;
    }

    while (list) {
        ++count;
        printobject(*list);
        list = mpr_list_get_next(list);
    }

    if (count != 1) {
        eprintf("Expected 1 record, but counted %d.\n", count);
        result = 1;
        goto done;
    }

    /*********/

    eprintf("\n--- Signals ---\n");

    eprintf("\nFind all signals for device 'testgraph.1':\n");

    list = mpr_graph_get_objs(graph, MPR_DEV);
    list = mpr_list_filter(list, MPR_PROP_NAME, NULL, 1, MPR_STR, "testgraph.1",
                           MPR_OP_EQ);
    if (list && (dev = (mpr_dev)*list)) {
        list = mpr_graph_get_objs(graph, MPR_SIG);
        list = mpr_list_filter(list, MPR_PROP_DEV, NULL, 1, MPR_DEV, dev,
                               MPR_OP_EQ);
    }

    count=0;
    if (!list) {
        eprintf("query returned 0.\n");
        result = 1;
        goto done;
    }
    if (!*list) {
        eprintf("query returned something which pointed to 0.\n");
        result = 1;
        goto done;
    }

    while (list) {
        ++count;
        printobject(*list);
        list = mpr_list_get_next(list);
    }

    if (count != 4) {
        eprintf("Expected 4 records, but counted %d.\n", count);
        result = 1;
        goto done;
    }

    /*********/

    eprintf("\nFind all signals for device 'testgraph__xx.2':\n");

    list = mpr_graph_get_objs(graph, MPR_DEV);
    list = mpr_list_filter(list, MPR_PROP_NAME, NULL, 1, MPR_STR,
                           "testgraph__xx.2", MPR_OP_EQ);
    if (list && (dev = (mpr_dev)*list)) {
        list = mpr_graph_get_objs(graph, MPR_SIG);
        list = mpr_list_filter(list, MPR_PROP_DEV, NULL, 1, MPR_DEV, dev,
                               MPR_OP_EQ);
    }

    count=0;
    if (mpr_list_get_size(list)) {
        eprintf("query incorrectly found something.\n");
        printobject(*list);
        mpr_list_free(list);
        result = 1;
        goto done;
    }
    else
        eprintf("query correctly returned 0.\n");

    /*********/

    eprintf("\nFind all outputs for device 'testgraph__.2':\n");

    list = mpr_graph_get_objs(graph, MPR_DEV);
    list = mpr_list_filter(list, MPR_PROP_NAME, NULL, 1, MPR_STR,
                           "testgraph__.2", MPR_OP_EQ);
    if (list && (dev = (mpr_dev)*list)) {
//        list = mpr_dev_sigs(dev, MPR_DIR_OUT);
        list = mpr_obj_get_prop_as_list((mpr_obj)dev, MPR_PROP_SIG, NULL);
//        list = mpr_graph_get_objs(graph, MPR_SIG);
//        list = mpr_list_filter(list, MPR_PROP_DEV, NULL, 1, MPR_DEV, dev,
//                               MPR_OP_EQ);
//        mpr_dir dir = MPR_DIR_OUT;
//        list = mpr_list_filter(list, MPR_PROP_DIR, NULL, 1, MPR_INT32, &dir,
//                               MPR_OP_EQ);
    }

    count=0;
    if (!list) {
        eprintf("query returned 0.\n");
        result = 1;
        goto done;
    }
    if (!*list) {
        eprintf("query returned something which pointed to 0.\n");
        result = 1;
        goto done;
    }

    while (list) {
        ++count;
        printobject(*list);
        list = mpr_list_get_next(list);
    }

    if (count != 3) {
        eprintf("Expected 3 records, but counted %d.\n", count);
        result = 1;
        goto done;
    }

    /*********/

    eprintf("\nFind signal matching 'in' for device 'testgraph.1':\n");

    list = mpr_graph_get_objs(graph, MPR_DEV);
    list = mpr_list_filter(list, MPR_PROP_NAME, NULL, 1, MPR_STR, "testgraph.1",
                           MPR_OP_EQ);
    if (list && (dev = (mpr_dev)*list)) {
        list = mpr_graph_get_objs(graph, MPR_SIG);
        list = mpr_list_filter(list, MPR_PROP_DEV, NULL, 1, MPR_DEV, dev,
                               MPR_OP_EQ);
        list = mpr_list_filter(list, MPR_PROP_NAME, NULL, 1, MPR_STR, "*in*",
                               MPR_OP_EQ);
    }

    count=0;
    if (!list) {
        eprintf("query returned 0.\n");
        result = 1;
        goto done;
    }
    if (!*list) {
        eprintf("query returned something which pointed to 0.\n");
        result = 1;
        goto done;
    }

    while (list) {
        ++count;
        printobject(*list);
        list = mpr_list_get_next(list);
    }

    if (count != 2) {
        eprintf("Expected 2 records, but counted %d.\n", count);
        result = 1;
        goto done;
    }

    /*********/

    eprintf("\nFind signal matching 'out' for device 'testgraph.1':\n");

    list = mpr_graph_get_objs(graph, MPR_DEV);
    list = mpr_list_filter(list, MPR_PROP_NAME, NULL, 1, MPR_STR, "testgraph.1",
                           MPR_OP_EQ);
    if (list && (dev = (mpr_dev)*list)) {
        list = mpr_graph_get_objs(graph, MPR_SIG);
        list = mpr_list_filter(list, MPR_PROP_DEV, NULL, 1, MPR_DEV, dev,
                               MPR_OP_EQ);
        list = mpr_list_filter(list, MPR_PROP_NAME, NULL, 1, MPR_STR, "*out*",
                               MPR_OP_EQ);
    }

    count=0;
    if (!list) {
        eprintf("query returned 0.\n");
        result = 1;
        goto done;
    }
    if (!*list) {
        eprintf("query returned something which pointed to 0.\n");
        result = 1;
        goto done;
    }

    while (list) {
        ++count;
        printobject(*list);
        list = mpr_list_get_next(list);
    }

    if (count != 2) {
        eprintf("Expected 2 records, but counted %d.\n", count);
        result = 1;
        goto done;
    }

    /*********/

    eprintf("\nFind signal matching 'out' for device 'testgraph__.2':\n");

    list = mpr_graph_get_objs(graph, MPR_DEV);
    list = mpr_list_filter(list, MPR_PROP_NAME, NULL, 1, MPR_STR,
                           "testgraph__.2", MPR_OP_EQ);
    if (list && (dev = (mpr_dev)*list)) {
        list = mpr_graph_get_objs(graph, MPR_SIG);
        list = mpr_list_filter(list, MPR_PROP_DEV, NULL, 1, MPR_DEV, dev,
                               MPR_OP_EQ);
        list = mpr_list_filter(list, MPR_PROP_NAME, NULL, 1, MPR_STR, "*out*",
                               MPR_OP_EQ);
    }

    count=0;
    if (!list) {
        eprintf("query returned 0.\n");
        result = 1;
        goto done;
    }
    if (!*list) {
        eprintf("query returned something which pointed to 0.\n");
        result = 1;
        goto done;
    }

    while (list) {
        ++count;
        printobject(*list);
        list = mpr_list_get_next(list);
    }

    if (count != 1) {
        eprintf("Expected 1 record, but counted %d.\n", count);
        result = 1;
        goto done;
    }

    /*********/

    eprintf("\n--- maps ---\n");

    eprintf("\nFind maps with source 'out1':\n");

    list = mpr_graph_get_objs(graph, MPR_SIG);
    list = mpr_list_filter(list, MPR_PROP_NAME, NULL, 1, MPR_STR, "out1", MPR_OP_EQ);
    list2 = 0;
    while (list) {
        list2 = mpr_list_get_union(list2, mpr_sig_get_maps((mpr_sig)*list, MPR_DIR_OUT));
        list = mpr_list_get_next(list);
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
        ++count;
        printobject(*list2);
        list2 = mpr_list_get_next(list2);
    }

    if (count != 3) {
        eprintf("Expected 3 records, but counted %d.\n", count);
        result = 1;
        goto done;
    }

    /*********/

    eprintf("\nFind maps for device 'testgraph.1', source 'out1':\n");

    list = mpr_graph_get_objs(graph, MPR_DEV);
    list = mpr_list_filter(list, MPR_PROP_NAME, NULL, 1, MPR_STR, "testgraph.1",
                           MPR_OP_EQ);
    if (list && (dev = (mpr_dev)*list)) {
        list = mpr_graph_get_objs(graph, MPR_SIG);
        list = mpr_list_filter(list, MPR_PROP_DEV, NULL, 1, MPR_DEV, dev,
                               MPR_OP_EQ);
        list = mpr_list_filter(list, MPR_PROP_NAME, NULL, 1, MPR_STR, "out1",
                               MPR_OP_EQ);
        if (list && (sig = (mpr_sig)*list)) {
            list = mpr_sig_get_maps(sig, 0);
        }
    }

    count=0;
    if (!list) {
        eprintf("query returned 0.\n");
        result = 1;
        goto done;
    }
    if (!*list) {
        eprintf("query returned something which pointed to 0.\n");
        result = 1;
        goto done;
    }

    while (list) {
        ++count;
        printobject(*list);
        list = mpr_list_get_next(list);
    }

    if (count != 2) {
        eprintf("Expected 2 records, but counted %d.\n", count);
        result = 1;
        goto done;
    }

    /*********/

    eprintf("\nFind maps with destination signal named 'in2':\n");

    list = mpr_graph_get_objs(graph, MPR_SIG);
    list = mpr_list_filter(list, MPR_PROP_NAME, NULL, 1, MPR_STR, "in2", MPR_OP_EQ);
    list2 = 0;
    while (list) {
        list2 = mpr_list_get_union(list2, mpr_sig_get_maps((mpr_sig)*list, MPR_DIR_IN));
        list = mpr_list_get_next(list);
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
        ++count;
        printobject(*list2);
        list2 = mpr_list_get_next(list2);
    }

    if (count != 1) {
        eprintf("Expected 1 record, but counted %d.\n", count);
        result = 1;
        goto done;
    }

    /*********/

    eprintf("\nFind maps for device 'testgraph__.2', destination 'in1':\n");
    list = mpr_graph_get_objs(graph, MPR_DEV);
    list = mpr_list_filter(list, MPR_PROP_NAME, NULL, 1, MPR_STR,
                           "testgraph__.2", MPR_OP_EQ);
    if (list && (dev = (mpr_dev)*list)) {
        list = mpr_graph_get_objs(graph, MPR_SIG);
        list = mpr_list_filter(list, MPR_PROP_DEV, NULL, 1, MPR_DEV, dev,
                               MPR_OP_EQ);
        list = mpr_list_filter(list, MPR_PROP_NAME, NULL, 1, MPR_STR, "in1",
                               MPR_OP_EQ);
        if (list && (sig = (mpr_sig)*list))
            list = mpr_sig_get_maps(sig, MPR_DIR_IN);
    }

    count=0;
    if (!list) {
        eprintf("query returned 0.\n");
        result = 1;
        goto done;
    }
    if (!*list) {
        eprintf("query returned something which pointed to 0.\n");
        result = 1;
        goto done;
    }

    while (list) {
        ++count;
        printobject(*list);
        list = mpr_list_get_next(list);
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
    list = mpr_graph_get_objs(graph, MPR_DEV);
    list = mpr_list_filter(list, MPR_PROP_NAME, NULL, 1, MPR_STR,
                           "testgraph__.2", MPR_OP_EQ);
    if (list && (dev = (mpr_dev)*list)) {
        list = mpr_graph_get_objs(graph, MPR_SIG);
        list = mpr_list_filter(list, MPR_PROP_DEV, NULL, 1, MPR_DEV, dev,
                               MPR_OP_EQ);
        list = mpr_list_filter(list, MPR_PROP_NAME, NULL, 1, MPR_STR, "out1",
                               MPR_OP_EQ);
        if (list && (sig = (mpr_sig)*list)) {
            list = mpr_sig_get_maps(sig, MPR_DIR_OUT);
        }
    }

    // get maps with destination signal
    list2 = mpr_graph_get_objs(graph, MPR_DEV);
    list2 = mpr_list_filter(list2, MPR_PROP_NAME, NULL, 1, MPR_STR,
                            "testgraph.1", MPR_OP_EQ);
    if (list2 && (dev = (mpr_dev)*list2)) {
        list2 = mpr_graph_get_objs(graph, MPR_SIG);
        list2 = mpr_list_filter(list2, MPR_PROP_DEV, NULL, 1, MPR_DEV, dev,
                                MPR_OP_EQ);
        list2 = mpr_list_filter(list2, MPR_PROP_NAME, NULL, 1, MPR_STR, "in1",
                                MPR_OP_EQ);
        if (list2 && (sig = (mpr_sig)*list2)) {
            list2 = mpr_sig_get_maps(sig, MPR_DIR_IN);
        }
    }

    // intersect map queries
    list = mpr_list_get_isect(list, list2);

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
        ++count;
        printobject(*list);
        list = mpr_list_get_next(list);
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
    list = mpr_graph_get_objs(graph, MPR_DEV);
    list = mpr_list_filter(list, MPR_PROP_NAME, NULL, 1, MPR_STR,
                           "testgraph__.2", MPR_OP_EQ);
    if (list && (dev = (mpr_dev)*list)) {
        list = mpr_graph_get_objs(graph, MPR_SIG);
        list = mpr_list_filter(list, MPR_PROP_DEV, NULL, 1, MPR_DEV, dev,
                               MPR_OP_EQ);
        list = mpr_list_filter(list, MPR_PROP_NAME, NULL, 1, MPR_STR, "*out*",
                               MPR_OP_EQ);
    }

    list2 = 0;
    while (list) {
        list2 = mpr_list_get_union(list2, mpr_sig_get_maps((mpr_sig)*list, MPR_DIR_OUT));
        list = mpr_list_get_next(list);
    }

    // build destination query
    list = mpr_graph_get_objs(graph, MPR_DEV);
    list = mpr_list_filter(list, MPR_PROP_NAME, NULL, 1, MPR_STR, "testgraph.1",
                           MPR_OP_EQ);
    if (list && (dev = (mpr_dev)*list)) {
        mpr_list sigs = mpr_graph_get_objs(graph, MPR_SIG);
        sigs = mpr_list_filter(sigs, MPR_PROP_DEV, NULL, 1, MPR_DEV, dev,
                               MPR_OP_EQ);
        list = 0;
        while (sigs) {
            list = mpr_list_get_union(list, mpr_sig_get_maps((mpr_sig)*sigs, MPR_DIR_IN));
            sigs = mpr_list_get_next(sigs);
        }
    }

    // intersect queries
    list = mpr_list_get_isect(list, list2);

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
        ++count;
        printobject(*list);
        list = mpr_list_get_next(list);
    }

    if (count != 1) {
        eprintf("Expected 1 record, but counted %d.\n", count);
        result = 1;
        goto done;
    }

    /*********/

    eprintf("\nFind maps with scope 'testgraph__.2':\n");

    list = mpr_graph_get_objs(graph, MPR_DEV);
    list = mpr_list_filter(list, MPR_PROP_NAME, NULL, 1, MPR_STR,
                           "testgraph__.2", MPR_OP_EQ);
    if (!list || !(dev = (mpr_dev)*list)) {
        eprintf("failed to find device 'testgraph__.2'.\n");
        result = 1;
        goto done;
    }

    list = mpr_graph_get_objs(graph, MPR_MAP);
    list = mpr_list_filter(list, MPR_PROP_SCOPE, NULL, 1, MPR_DEV, dev, MPR_OP_ANY);

    count=0;
    if (!list) {
        eprintf("map scope filter query returned 0.\n");
        result = 1;
        goto done;
    }
    if (!*list) {
        eprintf("map scope filter query returned something which pointed to 0.\n");
        result = 1;
        goto done;
    }

    while (list) {
        ++count;
        printobject(*list);
        list = mpr_list_get_next(list);
    }

    if (count != 1) {
        eprintf("Expected 1 record, but counted %d.\n", count);
        result = 1;
        goto done;
    }

    /*********/
done:
    mpr_graph_free(graph);
    if (!verbose)
        printf("..................................................");
    printf("Test %s\x1B[0m.\n", result ? "\x1B[31mFAILED" : "\x1B[32mPASSED");
    return result;
}
