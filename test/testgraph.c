#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <lo/lo_lowlevel.h>
#include "../src/mapper_internal.h"

int verbose = 1;

static void eprintf(const char *format, ...)
{
    va_list args;
    if (!verbose)
        return;
    va_start(args, format);
    vprintf(format, args);
    va_end(args);
}

void printobject(mpr_obj obj)
{
    if (verbose)
        mpr_obj_print(obj, 0);
}

int main(int argc, char **argv)
{
    int i, j, result = 0, count, intval;
    lo_message lom;
    mpr_msg props;
    uint64_t id = 1;
    mpr_graph graph;
    mpr_list devlist, siglist, maplist, maplist2;
    mpr_dev dev;
    mpr_sig sig;
    mpr_map map;
    const char *src_sig_name;

    /* process flags for -v verbose, -h help */
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

    graph = mpr_graph_new(0);

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

    if (!(props = mpr_msg_parse_props(lo_message_get_argc(lom),
                                      lo_message_get_types(lom),
                                      lo_message_get_argv(lom)))) {
        eprintf("1: Error, parsing failed.\n");
        result = 1;
        goto done;
    }

    mpr_graph_add_dev(graph, "testgraph.1", props);

    mpr_msg_free(props);
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

    if (!(props = mpr_msg_parse_props(lo_message_get_argc(lom),
                                      lo_message_get_types(lom),
                                      lo_message_get_argv(lom)))) {
        eprintf("1: Error, parsing failed.\n");
        result = 1;
        goto done;
    }

    mpr_graph_add_dev(graph, "testgraph__.2", props);

    mpr_msg_free(props);
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

    if (!(props = mpr_msg_parse_props(lo_message_get_argc(lom),
                                      lo_message_get_types(lom),
                                      lo_message_get_argv(lom)))) {
        eprintf("1: Error, parsing failed.\n");
        result = 1;
        goto done;
    }

    mpr_graph_add_dev(graph, "testgraph.3", props);

    mpr_msg_free(props);
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

    if (!(props = mpr_msg_parse_props(lo_message_get_argc(lom),
                                      lo_message_get_types(lom),
                                      lo_message_get_argv(lom)))) {
        eprintf("1: Error, parsing failed.\n");
        result = 1;
        goto done;
    }

    mpr_graph_add_dev(graph, "testgraph__.4", props);

    mpr_msg_free(props);
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

    if (!(props = mpr_msg_parse_props(lo_message_get_argc(lom),
                                      lo_message_get_types(lom),
                                      lo_message_get_argv(lom)))) {
        eprintf("1: Error, parsing failed.\n");
        result = 1;
        goto done;
    }

    mpr_graph_add_sig(graph, "in1", "testgraph.1", props);

    mpr_msg_free(props);
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

    if (!(props = mpr_msg_parse_props(lo_message_get_argc(lom),
                                      lo_message_get_types(lom),
                                      lo_message_get_argv(lom)))) {
        eprintf("1: Error, parsing failed.\n");
        result = 1;
        goto done;
    }

    mpr_graph_add_sig(graph, "in2", "testgraph.1", props);

    mpr_msg_free(props);
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

    if (!(props = mpr_msg_parse_props(lo_message_get_argc(lom),
                                      lo_message_get_types(lom),
                                      lo_message_get_argv(lom)))) {
        eprintf("1: Error, parsing failed.\n");
        result = 1;
        goto done;
    }

    mpr_graph_add_sig(graph, "out1", "testgraph.1", props);

    mpr_msg_free(props);
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

    if (!(props = mpr_msg_parse_props(lo_message_get_argc(lom),
                                      lo_message_get_types(lom),
                                      lo_message_get_argv(lom)))) {
        eprintf("1: Error, parsing failed.\n");
        result = 1;
        goto done;
    }

    mpr_graph_add_sig(graph, "out2", "testgraph.1", props);

    mpr_msg_free(props);
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

    if (!(props = mpr_msg_parse_props(lo_message_get_argc(lom),
                                      lo_message_get_types(lom),
                                      lo_message_get_argv(lom)))) {
        eprintf("1: Error, parsing failed.\n");
        result = 1;
        goto done;
    }

    mpr_graph_add_sig(graph, "out1", "testgraph__.2", props);

    mpr_msg_free(props);
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

    if (!(props = mpr_msg_parse_props(lo_message_get_argc(lom),
                                      lo_message_get_types(lom),
                                      lo_message_get_argv(lom)))) {
        eprintf("1: Error, parsing failed.\n");
        result = 1;
        goto done;
    }

    src_sig_name = "testgraph.1/out2";
    map = mpr_graph_add_map(graph, id, 1, &src_sig_name, "testgraph__.2/in1");
    mpr_map_set_from_msg(map, props, 0);

    mpr_msg_free(props);
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

    if (!(props = mpr_msg_parse_props(lo_message_get_argc(lom),
                                      lo_message_get_types(lom),
                                      lo_message_get_argv(lom)))) {
        eprintf("1: Error, parsing failed.\n");
        result = 1;
        goto done;
    }

    src_sig_name = "testgraph__.2/out1";
    map = mpr_graph_add_map(graph, id, 1, &src_sig_name, "testgraph.1/in1");
    mpr_map_set_from_msg(map, props, 0);

    mpr_msg_free(props);
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

    if (!(props = mpr_msg_parse_props(lo_message_get_argc(lom),
                                      lo_message_get_types(lom),
                                      lo_message_get_argv(lom)))) {
        eprintf("1: Error, parsing failed.\n");
        result = 1;
        goto done;
    }

    src_sig_name = "testgraph.1/out1";
    map = mpr_graph_add_map(graph, id, 1, &src_sig_name, "testgraph__.2/in2");
    mpr_map_set_from_msg(map, props, 0);

    mpr_msg_free(props);
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

    if (!(props = mpr_msg_parse_props(lo_message_get_argc(lom),
                                      lo_message_get_types(lom),
                                      lo_message_get_argv(lom)))) {
        eprintf("1: Error, parsing failed.\n");
        result = 1;
        goto done;
    }

    src_sig_name = "testgraph.1/out1";
    mpr_graph_add_map(graph, id, 1, &src_sig_name, "testgraph__.2/in1");
    mpr_map_set_from_msg(map, props, 0);

    mpr_msg_free(props);
    lo_message_free(lom);

    /*********/

    if (verbose) {
        eprintf("Dump:\n");
        mpr_obj_print((mpr_obj)graph, 0);
    }

    /*********/

    eprintf("\n--- Devices ---\n");

    eprintf("\nWalk the whole graph:\n");
    devlist = mpr_graph_get_list(graph, MPR_DEV);
    count = 0;
    if (!devlist) {
        eprintf("query returned 0.\n");
        result = 1;
        goto done;
    }

    while (devlist) {
        count ++;
        printobject(*devlist);
        devlist = mpr_list_get_next(devlist);
    }

    if (count != 4) {
        eprintf("Expected 4 records, but counted %d.\n", count);
        result = 1;
        goto done;
    }

    /*********/

    eprintf("\nFind device named 'testgraph.3':\n");

    devlist = mpr_graph_get_list(graph, MPR_DEV);
    devlist = mpr_list_filter(devlist, MPR_PROP_NAME, NULL, 1, MPR_STR, "testgraph.3", MPR_OP_EQ);
    if (!devlist) {
        eprintf("Not found.\n");
        result = 1;
        goto done;
    }
    count = mpr_list_get_size(devlist);
    if (count != 1) {
        eprintf("Found %d devices (Should be 1).\n", count);
        result = 1;
        mpr_list_free(devlist);
        goto done;
    }

    printobject((mpr_obj)*devlist);
    mpr_list_free(devlist);

    /*********/

    eprintf("\nFind device named 'dummy':\n");

    devlist = mpr_graph_get_list(graph, MPR_DEV);
    devlist = mpr_list_filter(devlist, MPR_PROP_NAME, NULL, 1, MPR_STR, "dummy", MPR_OP_EQ);
    if (mpr_list_get_size(devlist)) {
        eprintf("unexpectedly found 'dummy': %p\n", *devlist);
        result = 1;
        mpr_list_free(devlist);
        goto done;
    }
    eprintf("  not found, good.\n");

    /*********/

    eprintf("\nFind devices matching '__':\n");

    devlist = mpr_graph_get_list(graph, MPR_DEV);
    devlist = mpr_list_filter(devlist, MPR_PROP_NAME, NULL, 1, MPR_STR, "*__*", MPR_OP_EQ);

    count=0;
    if (!devlist) {
        eprintf("query returned 0.\n");
        result = 1;
        goto done;
    }

    while (devlist) {
        ++count;
        printobject(*devlist);
        devlist = mpr_list_get_next(devlist);
    }

    if (count != 2) {
        eprintf("Expected 2 records, but counted %d.\n", count);
        result = 1;
        goto done;
    }

    /*********/

    eprintf("\nFind devices with property 'host'=='192.168.0.100':\n");

    devlist = mpr_graph_get_list(graph, MPR_DEV);
    devlist = mpr_list_filter(devlist, MPR_PROP_HOST, NULL, 1, MPR_STR, "192.168.0.100", MPR_OP_EQ);

    count=0;
    if (!devlist) {
        eprintf("query returned 0.\n");
        result = 1;
        goto done;
    }
    if (!*devlist) {
        eprintf("query returned something which pointed to 0.\n");
        result = 1;
        goto done;
    }

    while (devlist) {
        ++count;
        printobject(*devlist);
        devlist = mpr_list_get_next(devlist);
    }

    if (count != 2) {
        eprintf("Expected 2 records, but counted %d.\n", count);
        result = 1;
        goto done;
    }

    /*********/

    eprintf("\nFind devices with property 'port'<5678:\n");

    intval = 5678;
    devlist = mpr_graph_get_list(graph, MPR_DEV);
    devlist = mpr_list_filter(devlist, MPR_PROP_PORT, NULL, 1, MPR_INT32, &intval, MPR_OP_LT);

    count=0;
    if (!devlist) {
        eprintf("query returned 0.\n");
        result = 1;
        goto done;
    }
    if (!*devlist) {
        eprintf("query returned something which pointed to 0.\n");
        result = 1;
        goto done;
    }

    while (devlist) {
        ++count;
        printobject(*devlist);
        devlist = mpr_list_get_next(devlist);
    }

    if (count != 3) {
        eprintf("Expected 3 records, but counted %d.\n", count);
        result = 1;
        goto done;
    }

    /*********/

    eprintf("\nFind devices with property 'num_outputs'==2:\n");
    intval = 2;
    devlist = mpr_graph_get_list(graph, MPR_DEV);
    devlist = mpr_list_filter(devlist, MPR_PROP_UNKNOWN, "num_outputs", 1,
                              MPR_INT32, &intval, MPR_OP_EQ);

    count=0;
    if (!devlist) {
        eprintf("query returned 0.\n");
        result = 1;
        goto done;
    }
    if (!*devlist) {
        eprintf("query returned something which pointed to 0.\n");
        result = 1;
        goto done;
    }

    while (devlist) {
        ++count;
        printobject(*devlist);
        devlist = mpr_list_get_next(devlist);
    }

    if (count != 1) {
        eprintf("Expected 1 records, but counted %d.\n", count);
        result = 1;
        goto done;
    }

    /*********/

    eprintf("\nFind devices with properties 'host'!='localhost' AND 'port'>=4000:\n");

    intval = 4000;
    devlist = mpr_graph_get_list(graph, MPR_DEV);
    devlist = mpr_list_filter(devlist, MPR_PROP_HOST, NULL, 1, MPR_STR, "localhost", MPR_OP_NEQ);
    devlist = mpr_list_filter(devlist, MPR_PROP_PORT, NULL, 1, MPR_INT32, &intval, MPR_OP_GTE);

    count=0;
    if (!devlist) {
        eprintf("query returned 0.\n");
        result = 1;
        goto done;
    }
    if (!*devlist) {
        eprintf("query returned something which pointed to 0.\n");
        result = 1;
        goto done;
    }

    while (devlist) {
        ++count;
        printobject(*devlist);
        devlist = mpr_list_get_next(devlist);
    }

    if (count != 1) {
        eprintf("Expected 1 record, but counted %d.\n", count);
        result = 1;
        goto done;
    }

    /*********/

    eprintf("\n--- Signals ---\n");

    eprintf("\nFind all signals for device 'testgraph.1':\n");

    devlist = mpr_graph_get_list(graph, MPR_DEV);
    devlist = mpr_list_filter(devlist, MPR_PROP_NAME, NULL, 1, MPR_STR, "testgraph.1", MPR_OP_EQ);
    if (!devlist || !(dev = (mpr_dev)*devlist)) {
        eprintf("device query returned 0.\n");
        result = 1;
        goto done;
    }
    siglist = mpr_graph_get_list(graph, MPR_SIG);
    siglist = mpr_list_filter(siglist, MPR_PROP_DEV, NULL, 1, MPR_DEV, dev, MPR_OP_EQ);
    mpr_list_free(devlist);

    count=0;
    if (!siglist) {
        eprintf("query returned 0.\n");
        result = 1;
        goto done;
    }
    if (!*siglist) {
        eprintf("query returned something which pointed to 0.\n");
        result = 1;
        goto done;
    }

    while (siglist) {
        ++count;
        printobject(*siglist);
        siglist = mpr_list_get_next(siglist);
    }

    if (count != 4) {
        eprintf("Expected 4 records, but counted %d.\n", count);
        result = 1;
        goto done;
    }

    /*********/

    eprintf("\nFind all signals for device 'testgraph__xx.2':\n");

    devlist = mpr_graph_get_list(graph, MPR_DEV);
    devlist = mpr_list_filter(devlist, MPR_PROP_NAME, NULL, 1, MPR_STR, "testgraph__xx.2", MPR_OP_EQ);
    if (devlist && (dev = (mpr_dev)*devlist)) {
        eprintf("device query  incorrectly found something.\n");
        mpr_list_free(devlist);
        result = 1;
        goto done;
    }
    else
        eprintf("query correctly returned 0.\n");

    /*********/

    eprintf("\nFind all outputs for device 'testgraph__.2':\n");

    devlist = mpr_graph_get_list(graph, MPR_DEV);
    devlist = mpr_list_filter(devlist, MPR_PROP_NAME, NULL, 1, MPR_STR, "testgraph__.2", MPR_OP_EQ);
    if (!devlist || !(dev = (mpr_dev)*devlist)) {
        eprintf("device query returned 0.\n");
        result = 1;
        goto done;
    }
    siglist = mpr_obj_get_prop_as_list((mpr_obj)dev, MPR_PROP_SIG, NULL);
    intval = MPR_DIR_OUT;
    siglist = mpr_list_filter(siglist, MPR_PROP_DIR, NULL, 1, MPR_INT32, &intval, MPR_OP_EQ);
    mpr_list_free(devlist);

    count=0;
    if (!siglist) {
        eprintf("query returned 0.\n");
        result = 1;
        goto done;
    }
    if (!*siglist) {
        eprintf("query returned something which pointed to 0.\n");
        result = 1;
        goto done;
    }

    while (siglist) {
        ++count;
        printobject(*siglist);
        siglist = mpr_list_get_next(siglist);
    }

    if (count != 3) {
        eprintf("Expected 3 records, but counted %d.\n", count);
        result = 1;
        goto done;
    }

    /*********/

    eprintf("\nFind signal matching 'in' for device 'testgraph.1':\n");

    devlist = mpr_graph_get_list(graph, MPR_DEV);
    devlist = mpr_list_filter(devlist, MPR_PROP_NAME, NULL, 1, MPR_STR, "testgraph.1", MPR_OP_EQ);
    if (!devlist || !(dev = (mpr_dev)*devlist)) {
        eprintf("device query returned 0.\n");
        result = 1;
        goto done;
    }
    siglist = mpr_graph_get_list(graph, MPR_SIG);
    siglist = mpr_list_filter(siglist, MPR_PROP_DEV, NULL, 1, MPR_DEV, dev, MPR_OP_EQ);
    siglist = mpr_list_filter(siglist, MPR_PROP_NAME, NULL, 1, MPR_STR, "*in*", MPR_OP_EQ);
    mpr_list_free(devlist);

    count=0;
    if (!siglist) {
        eprintf("query returned 0.\n");
        result = 1;
        goto done;
    }
    if (!*siglist) {
        eprintf("query returned something which pointed to 0.\n");
        result = 1;
        goto done;
    }

    while (siglist) {
        ++count;
        printobject(*siglist);
        siglist = mpr_list_get_next(siglist);
    }

    if (count != 2) {
        eprintf("Expected 2 records, but counted %d.\n", count);
        result = 1;
        goto done;
    }

    /*********/

    eprintf("\nFind signal matching 'out' for device 'testgraph.1':\n");

    devlist = mpr_graph_get_list(graph, MPR_DEV);
    devlist = mpr_list_filter(devlist, MPR_PROP_NAME, NULL, 1, MPR_STR, "testgraph.1", MPR_OP_EQ);
    if (!devlist || !(dev = (mpr_dev)*devlist)) {
        eprintf("device query returned 0.\n");
        result = 1;
        goto done;
    }
    siglist = mpr_graph_get_list(graph, MPR_SIG);
    siglist = mpr_list_filter(siglist, MPR_PROP_DEV, NULL, 1, MPR_DEV, dev, MPR_OP_EQ);
    siglist = mpr_list_filter(siglist, MPR_PROP_NAME, NULL, 1, MPR_STR, "*out*", MPR_OP_EQ);
    mpr_list_free(devlist);

    count=0;
    if (!siglist) {
        eprintf("query returned 0.\n");
        result = 1;
        goto done;
    }
    if (!*siglist) {
        eprintf("query returned something which pointed to 0.\n");
        result = 1;
        goto done;
    }

    while (siglist) {
        ++count;
        printobject(*siglist);
        siglist = mpr_list_get_next(siglist);
    }

    if (count != 2) {
        eprintf("Expected 2 records, but counted %d.\n", count);
        result = 1;
        goto done;
    }

    /*********/

    eprintf("\nFind signal matching 'out' for device 'testgraph__.2':\n");

    devlist = mpr_graph_get_list(graph, MPR_DEV);
    devlist = mpr_list_filter(devlist, MPR_PROP_NAME, NULL, 1, MPR_STR, "testgraph__.2", MPR_OP_EQ);
    if (!devlist || !(dev = (mpr_dev)*devlist)) {
        eprintf("device query returned 0.\n");
        result = 1;
        goto done;
    }
    siglist = mpr_graph_get_list(graph, MPR_SIG);
    siglist = mpr_list_filter(siglist, MPR_PROP_DEV, NULL, 1, MPR_DEV, dev, MPR_OP_EQ);
    siglist = mpr_list_filter(siglist, MPR_PROP_NAME, NULL, 1, MPR_STR, "*out*", MPR_OP_EQ);
    mpr_list_free(devlist);

    count=0;
    if (!siglist) {
        eprintf("query returned 0.\n");
        result = 1;
        goto done;
    }
    if (!*siglist) {
        eprintf("query returned something which pointed to 0.\n");
        result = 1;
        goto done;
    }

    while (siglist) {
        ++count;
        printobject(*siglist);
        siglist = mpr_list_get_next(siglist);
    }

    if (count != 1) {
        eprintf("Expected 1 record, but counted %d.\n", count);
        result = 1;
        goto done;
    }

    /*********/

    eprintf("\n--- maps ---\n");

    eprintf("\nFind maps with source 'out1':\n");

    siglist = mpr_graph_get_list(graph, MPR_SIG);
    siglist = mpr_list_filter(siglist, MPR_PROP_NAME, NULL, 1, MPR_STR, "out1", MPR_OP_EQ);
    maplist = 0;
    while (siglist) {
        maplist = mpr_list_get_union(maplist, mpr_sig_get_maps((mpr_sig)*siglist, MPR_DIR_OUT));
        siglist = mpr_list_get_next(siglist);
    }

    count=0;
    if (!maplist) {
        eprintf("combined query returned 0.\n");
        result = 1;
        goto done;
    }
    if (!*maplist) {
        eprintf("combined query returned something which pointed to 0.\n");
        result = 1;
        goto done;
    }

    while (maplist) {
        ++count;
        printobject(*maplist);
        maplist = mpr_list_get_next(maplist);
    }

    if (count != 3) {
        eprintf("Expected 3 records, but counted %d.\n", count);
        result = 1;
        goto done;
    }

    /*********/

    eprintf("\nFind maps for device 'testgraph.1', source 'out1':\n");

    devlist = mpr_graph_get_list(graph, MPR_DEV);
    devlist = mpr_list_filter(devlist, MPR_PROP_NAME, NULL, 1, MPR_STR, "testgraph.1", MPR_OP_EQ);
    if (!devlist || !(dev = (mpr_dev)*devlist)) {
        eprintf("device query returned 0.\n");
        result = 1;
        goto done;
    }
    siglist = mpr_graph_get_list(graph, MPR_SIG);
    siglist = mpr_list_filter(siglist, MPR_PROP_DEV, NULL, 1, MPR_DEV, dev, MPR_OP_EQ);
    siglist = mpr_list_filter(siglist, MPR_PROP_NAME, NULL, 1, MPR_STR, "out1", MPR_OP_EQ);
    mpr_list_free(devlist);
    if (!siglist || !(sig = (mpr_sig)*siglist)) {
        eprintf("signal query returned 0.\n");
        result = 1;
        goto done;
    }
    maplist = mpr_sig_get_maps(sig, 0);
    mpr_list_free(siglist);

    count=0;
    if (!maplist) {
        eprintf("map query returned 0.\n");
        result = 1;
        goto done;
    }
    if (!*maplist) {
        eprintf("map query returned something which pointed to 0.\n");
        result = 1;
        goto done;
    }

    while (maplist) {
        ++count;
        printobject(*maplist);
        maplist = mpr_list_get_next(maplist);
    }

    if (count != 2) {
        eprintf("Expected 2 records, but counted %d.\n", count);
        result = 1;
        goto done;
    }

    /*********/

    eprintf("\nFind maps with destination signal named 'in2':\n");

    siglist = mpr_graph_get_list(graph, MPR_SIG);
    siglist = mpr_list_filter(siglist, MPR_PROP_NAME, NULL, 1, MPR_STR, "in2", MPR_OP_EQ);
    maplist = 0;
    while (siglist) {
        maplist = mpr_list_get_union(maplist, mpr_sig_get_maps((mpr_sig)*siglist, MPR_DIR_IN));
        siglist = mpr_list_get_next(siglist);
    }

    count=0;
    if (!maplist) {
        eprintf("combined query returned 0.\n");
        result = 1;
        goto done;
    }
    if (!*maplist) {
        eprintf("combined query returned something which pointed to 0.\n");
        result = 1;
        goto done;
    }

    while (maplist) {
        ++count;
        printobject(*maplist);
        maplist = mpr_list_get_next(maplist);
    }

    if (count != 1) {
        eprintf("Expected 1 record, but counted %d.\n", count);
        result = 1;
        goto done;
    }

    /*********/

    eprintf("\nFind maps for device 'testgraph__.2', destination 'in1':\n");
    devlist = mpr_graph_get_list(graph, MPR_DEV);
    devlist = mpr_list_filter(devlist, MPR_PROP_NAME, NULL, 1, MPR_STR, "testgraph__.2", MPR_OP_EQ);
    if (!devlist || !(dev = (mpr_dev)*devlist)) {
        eprintf("device query returned 0.\n");
        result = 1;
        goto done;
    }
    siglist = mpr_graph_get_list(graph, MPR_SIG);
    siglist = mpr_list_filter(siglist, MPR_PROP_DEV, NULL, 1, MPR_DEV, dev, MPR_OP_EQ);
    siglist = mpr_list_filter(siglist, MPR_PROP_NAME, NULL, 1, MPR_STR, "in1", MPR_OP_EQ);
    mpr_list_free(devlist);
    if (!siglist || !(sig = (mpr_sig)*siglist)) {
        eprintf("signal query returned 0.\n");
        result = 1;
        goto done;
    }
    maplist = mpr_sig_get_maps(sig, MPR_DIR_IN);
    mpr_list_free(siglist);

    count=0;
    if (!maplist) {
        eprintf("map query returned 0.\n");
        result = 1;
        goto done;
    }
    if (!*maplist) {
        eprintf("map query returned something which pointed to 0.\n");
        result = 1;
        goto done;
    }

    while (maplist) {
        ++count;
        printobject(*maplist);
        maplist = mpr_list_get_next(maplist);
    }

    if (count != 2) {
        eprintf("Expected 2 records, but counted %d.\n", count);
        result = 1;
        goto done;
    }

    goto done;

    /*********/

    eprintf("\nFind maps for source device 'testgraph__.2', signal 'out1'"
            "\n          AND dest device 'testgraph.1', signal 'in1':\n");

    /* get maps with source signal */
    devlist = mpr_graph_get_list(graph, MPR_DEV);
    devlist = mpr_list_filter(devlist, MPR_PROP_NAME, NULL, 1, MPR_STR, "testgraph__.2", MPR_OP_EQ);
    if (!devlist || !(dev = (mpr_dev)*devlist)) {
        eprintf("device query returned 0.\n");
        result = 1;
        goto done;
    }
    siglist = mpr_graph_get_list(graph, MPR_SIG);
    siglist = mpr_list_filter(siglist, MPR_PROP_DEV, NULL, 1, MPR_DEV, dev, MPR_OP_EQ);
    siglist = mpr_list_filter(siglist, MPR_PROP_NAME, NULL, 1, MPR_STR, "out1", MPR_OP_EQ);
    mpr_list_free(devlist);
    if (!siglist || !(sig = (mpr_sig)*siglist)) {
        eprintf("signal query returned 0.\n");
        result = 1;
        goto done;
    }
    maplist = mpr_sig_get_maps(sig, MPR_DIR_OUT);
    mpr_list_free(siglist);

    /* get maps with destination signal */
    devlist = mpr_graph_get_list(graph, MPR_DEV);
    devlist = mpr_list_filter(devlist, MPR_PROP_NAME, NULL, 1, MPR_STR, "testgraph.1", MPR_OP_EQ);
    if (!devlist || !(dev = (mpr_dev)*devlist)) {
        eprintf("device query returned 0.\n");
        result = 1;
        goto done;
    }
    siglist = mpr_graph_get_list(graph, MPR_SIG);
    siglist = mpr_list_filter(siglist, MPR_PROP_DEV, NULL, 1, MPR_DEV, dev, MPR_OP_EQ);
    siglist = mpr_list_filter(siglist, MPR_PROP_NAME, NULL, 1, MPR_STR, "in1", MPR_OP_EQ);
    mpr_list_free(devlist);
    if (!siglist || !(sig = (mpr_sig)*siglist)) {
        eprintf("signal query returned 0.\n");
        result = 1;
        goto done;
    }

    /* intersect map queries */
    maplist = mpr_list_get_isect(maplist, mpr_sig_get_maps(sig, MPR_DIR_IN));

    count=0;
    if (!maplist) {
        eprintf("combined query returned 0.\n");
        result = 1;
        goto done;
    }
    if (!*maplist) {
        eprintf("combined query returned something which pointed to 0.\n");
        result = 1;
        goto done;
    }

    while (maplist) {
        ++count;
        printobject(*maplist);
        maplist = mpr_list_get_next(maplist);
    }

    if (count != 1) {
        eprintf("Expected 1 records, but counted %d.\n", count);
        result = 1;
        goto done;
    }

    /*********/

    eprintf("\nFind maps for source device 'testgraph__.2', signals matching 'out',"
            "\n          AND dest device 'testgraph.1', all signals:\n");

    /* build source query */
    devlist = mpr_graph_get_list(graph, MPR_DEV);
    devlist = mpr_list_filter(devlist, MPR_PROP_NAME, NULL, 1, MPR_STR, "testgraph__.2", MPR_OP_EQ);
    if (!devlist || !(dev = (mpr_dev)*devlist)) {
        eprintf("device query returned 0.\n");
        result = 1;
        goto done;
    }
    siglist = mpr_graph_get_list(graph, MPR_SIG);
    siglist = mpr_list_filter(siglist, MPR_PROP_DEV, NULL, 1, MPR_DEV, dev, MPR_OP_EQ);
    siglist = mpr_list_filter(siglist, MPR_PROP_NAME, NULL, 1, MPR_STR, "*out*", MPR_OP_EQ);
    mpr_list_free(devlist);

    maplist = 0;
    while (siglist) {
        maplist = mpr_list_get_union(maplist, mpr_sig_get_maps((mpr_sig)*siglist, MPR_DIR_OUT));
        siglist = mpr_list_get_next(siglist);
    }
    mpr_list_free(siglist);

    /* build destination query */
    devlist = mpr_graph_get_list(graph, MPR_DEV);
    devlist = mpr_list_filter(devlist, MPR_PROP_NAME, NULL, 1, MPR_STR, "testgraph.1", MPR_OP_EQ);
    if (!devlist || !(dev = (mpr_dev)*devlist)) {
        eprintf("device query returned 0.\n");
        result = 1;
        goto done;
    }
    siglist = mpr_graph_get_list(graph, MPR_SIG);
    siglist = mpr_list_filter(siglist, MPR_PROP_DEV, NULL, 1, MPR_DEV, dev, MPR_OP_EQ);
    maplist2 = 0;
    while (siglist) {
        maplist2= mpr_list_get_union(maplist2, mpr_sig_get_maps((mpr_sig)*siglist, MPR_DIR_IN));
        siglist = mpr_list_get_next(siglist);
    }

    /* intersect queries */
    maplist = mpr_list_get_isect(maplist, maplist2);

    count=0;
    if (!maplist) {
        eprintf("combined query returned 0.\n");
        result = 1;
        goto done;
    }
    if (!*maplist) {
        eprintf("combined query returned something which pointed to 0.\n");
        result = 1;
        goto done;
    }

    while (maplist) {
        ++count;
        printobject(*maplist);
        maplist = mpr_list_get_next(maplist);
    }

    if (count != 1) {
        eprintf("Expected 1 record, but counted %d.\n", count);
        result = 1;
        goto done;
    }

    /*********/

    eprintf("\nFind maps with scope 'testgraph__.2':\n");

    devlist = mpr_graph_get_list(graph, MPR_DEV);
    devlist = mpr_list_filter(devlist, MPR_PROP_NAME, NULL, 1, MPR_STR, "testgraph__.2", MPR_OP_EQ);
    if (!devlist || !(dev = (mpr_dev)*devlist)) {
        eprintf("failed to find device 'testgraph__.2'.\n");
        result = 1;
        goto done;
    }

    maplist = mpr_graph_get_list(graph, MPR_MAP);
    maplist = mpr_list_filter(maplist, MPR_PROP_SCOPE, NULL, 1, MPR_DEV, dev, MPR_OP_ANY);
    mpr_list_free(devlist);

    count=0;
    if (!maplist) {
        eprintf("map scope filter query returned 0.\n");
        result = 1;
        goto done;
    }
    if (!*maplist) {
        eprintf("map scope filter query returned something which pointed to 0.\n");
        result = 1;
        goto done;
    }

    while (maplist) {
        ++count;
        printobject(*maplist);
        maplist = mpr_list_get_next(maplist);
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
