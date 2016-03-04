
#include <stdio.h>
#include <string.h>
#include <lo/lo_lowlevel.h>
#include "../src/mapper_internal.h"

#define eprintf(format, ...) do {               \
    if (verbose)                                \
        fprintf(stdout, format, ##__VA_ARGS__); \
} while(0)

int verbose = 1;

void printdevice(mapper_device dev)
{
    if (verbose)
        mapper_device_print(dev);
}

void printsignal(mapper_signal sig)
{
    if (verbose)
        mapper_signal_print(sig, 1);
}

void printmap(mapper_map map)
{
    if (verbose)
        mapper_map_print(map);
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
                        eprintf("testdatabase.c: possible arguments "
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
    mapper_message msg;
    uint64_t id = 1;
    mapper_network net = mapper_network_new(0, 0, 0);
    mapper_database db = &net->database;

    mapper_device dev, *pdev, *pdev2;
    mapper_signal sig, *psig, *psig2;
    mapper_map *pmap, *pmap2;

    /* Test the database functions */

    lom = lo_message_new();
    if (!lom) {
        result = 1;
        goto done;
    }
    lo_message_add_string(lom, "@port");
    lo_message_add_int32(lom, 1234);
    lo_message_add_string(lom, "@host");
    lo_message_add_string(lom, "localhost");

    if (!(msg = mapper_message_parse_properties(lo_message_get_argc(lom),
                                                lo_message_get_types(lom),
                                                lo_message_get_argv(lom)))) {
        eprintf("1: Error, parsing failed.\n");
        result = 1;
        goto done;
    }

    mapper_database_add_or_update_device(db, "testdatabase.1", msg);
    mapper_database_add_or_update_device(db, "testdatabase__.2", msg);

    mapper_message_free(msg);
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

    if (!(msg = mapper_message_parse_properties(lo_message_get_argc(lom),
                                                lo_message_get_types(lom),
                                                lo_message_get_argv(lom)))) {
        eprintf("1: Error, parsing failed.\n");
        result = 1;
        goto done;
    }

    mapper_database_add_or_update_device(db, "testdatabase.3", msg);

    mapper_message_free(msg);
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

    if (!(msg = mapper_message_parse_properties(lo_message_get_argc(lom),
                                                lo_message_get_types(lom),
                                                lo_message_get_argv(lom)))) {
        eprintf("1: Error, parsing failed.\n");
        result = 1;
        goto done;
    }

    mapper_database_add_or_update_device(db, "testdatabase__.4", msg);

    mapper_message_free(msg);
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

    if (!(msg = mapper_message_parse_properties(lo_message_get_argc(lom),
                                                lo_message_get_types(lom),
                                                lo_message_get_argv(lom)))) {
        eprintf("1: Error, parsing failed.\n");
        result = 1;
        goto done;
    }

    mapper_database_add_or_update_signal(db, "in1", "testdatabase.1", msg);

    mapper_message_free(msg);
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

    if (!(msg = mapper_message_parse_properties(lo_message_get_argc(lom),
                                                lo_message_get_types(lom),
                                                lo_message_get_argv(lom)))) {
        eprintf("1: Error, parsing failed.\n");
        result = 1;
        goto done;
    }

    mapper_database_add_or_update_signal(db, "in2", "testdatabase.1", msg);

    mapper_message_free(msg);
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

    if (!(msg = mapper_message_parse_properties(lo_message_get_argc(lom),
                                                lo_message_get_types(lom),
                                                lo_message_get_argv(lom)))) {
        eprintf("1: Error, parsing failed.\n");
        result = 1;
        goto done;
    }

    mapper_database_add_or_update_signal(db, "out1", "testdatabase.1", msg);

    mapper_message_free(msg);
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

    if (!(msg = mapper_message_parse_properties(lo_message_get_argc(lom),
                                                lo_message_get_types(lom),
                                                lo_message_get_argv(lom)))) {
        eprintf("1: Error, parsing failed.\n");
        result = 1;
        goto done;
    }

    mapper_database_add_or_update_signal(db, "out2", "testdatabase.1", msg);

    mapper_message_free(msg);
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

    if (!(msg = mapper_message_parse_properties(lo_message_get_argc(lom),
                                                lo_message_get_types(lom),
                                                lo_message_get_argv(lom)))) {
        eprintf("1: Error, parsing failed.\n");
        result = 1;
        goto done;
    }

    mapper_database_add_or_update_signal(db, "out1", "testdatabase__.2", msg);

    mapper_message_free(msg);
    lo_message_free(lom);

    lom = lo_message_new();
    if (!lom) {
        result = 1;
        goto done;
    }

    id++;
    lo_message_add_string(lom, "@mode");
    lo_message_add_string(lom, "bypass");
    lo_message_add_string(lom, "@dst@bound_min");
    lo_message_add_string(lom, "none");
    lo_message_add_string(lom, "@id");
    lo_message_add_int64(lom, id);

    if (!(msg = mapper_message_parse_properties(lo_message_get_argc(lom),
                                                lo_message_get_types(lom),
                                                lo_message_get_argv(lom)))) {
        eprintf("1: Error, parsing failed.\n");
        result = 1;
        goto done;
    }

    const char *src_sig_name = "testdatabase.1/out2";
    mapper_database_add_or_update_map(db, 1, &src_sig_name,
                                      "testdatabase__.2/in1", msg);

    mapper_message_free(msg);
    lo_message_free(lom);

    lom = lo_message_new();
    if (!lom) {
        result = 1;
        goto done;
    }

    id++;
    lo_message_add_string(lom, "@mode");
    lo_message_add_string(lom, "bypass");
    lo_message_add_string(lom, "@dst@bound_min");
    lo_message_add_string(lom, "none");
    lo_message_add_string(lom, "@id");
    lo_message_add_int64(lom, id);

    if (!(msg = mapper_message_parse_properties(lo_message_get_argc(lom),
                                                lo_message_get_types(lom),
                                                lo_message_get_argv(lom)))) {
        eprintf("1: Error, parsing failed.\n");
        result = 1;
        goto done;
    }

    src_sig_name = "testdatabase__.2/out1";
    mapper_database_add_or_update_map(db, 1, &src_sig_name,
                                      "testdatabase.1/in1", msg);

    mapper_message_free(msg);
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
    lo_message_add_string(lom, "@dst@bound_min");
    lo_message_add_string(lom, "clamp");
    lo_message_add_string(lom, "@src@min");
    lo_message_add_float(lom, 0.f);
    lo_message_add_float(lom, 1.f);
    lo_message_add_string(lom, "@src@max");
    lo_message_add_float(lom, 1.f);
    lo_message_add_float(lom, 2.f);
    lo_message_add_string(lom, "@id");
    lo_message_add_int64(lom, id);

    if (!(msg = mapper_message_parse_properties(lo_message_get_argc(lom),
                                                lo_message_get_types(lom),
                                                lo_message_get_argv(lom)))) {
        eprintf("1: Error, parsing failed.\n");
        result = 1;
        goto done;
    }

    src_sig_name = "testdatabase.1/out1";
    mapper_database_add_or_update_map(db, 1, &src_sig_name,
                                      "testdatabase__.2/in2", msg);

    mapper_message_free(msg);
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
    lo_message_add_string(lom, "@dst@bound_min");
    lo_message_add_string(lom, "clamp");
    lo_message_add_string(lom, "@src@min");
    lo_message_add_float(lom, 0.f);
    lo_message_add_float(lom, 1.f);
    lo_message_add_string(lom, "@src@max");
    lo_message_add_float(lom, 1.f);
    lo_message_add_float(lom, 2.f);
    lo_message_add_string(lom, "@id");
    lo_message_add_int64(lom, id);

    if (!(msg = mapper_message_parse_properties(lo_message_get_argc(lom),
                                                lo_message_get_types(lom),
                                                lo_message_get_argv(lom)))) {
        eprintf("1: Error, parsing failed.\n");
        result = 1;
        goto done;
    }

    src_sig_name = "testdatabase.1/out1";
    mapper_database_add_or_update_map(db, 1, &src_sig_name,
                                      "testdatabase__.2/in1", msg);

    mapper_message_free(msg);
    lo_message_free(lom);

    /*********/

    if (verbose) {
        eprintf("Dump:\n");
        mapper_database_print(db);
    }

    /*********/

    eprintf("\n--- Devices ---\n");

    eprintf("\nWalk the whole database:\n");
    pdev = mapper_database_devices(db);
    int count=0;
    if (!pdev) {
        eprintf("mapper_database_devices() returned 0.\n");
        result = 1;
        goto done;
    }
    if (!*pdev) {
        eprintf("mapper_database_devices() returned something "
               "which pointed to 0.\n");
        result = 1;
        goto done;
    }

    while (pdev) {
        count ++;
        printdevice(*pdev);
        pdev = mapper_device_query_next(pdev);
    }

    if (count != 4) {
        eprintf("Expected 4 records, but counted %d.\n", count);
        result = 1;
        goto done;
    }

    /*********/

    eprintf("\nFind device named 'testdatabase.3':\n");

    dev = mapper_database_device_by_name(db, "testdatabase.3");
    if (!dev) {
        eprintf("Not found.\n");
        result = 1;
        goto done;
    }

    printdevice(dev);

    /*********/

    eprintf("\nFind device named 'dummy':\n");

    dev = mapper_database_device_by_name(db, "dummy");
    if (dev) {
        eprintf("unexpected found 'dummy': %p\n", dev);
        result = 1;
        goto done;
    }
    eprintf("  not found, good.\n");

    /*********/

    eprintf("\nFind devices matching '__':\n");

    pdev = mapper_database_devices_by_name(db, "*__*");

    count=0;
    if (!pdev) {
        eprintf("mapper_database_devices_by_name() returned 0.\n");
        result = 1;
        goto done;
    }
    if (!*pdev) {
        eprintf("mapper_database_devices_by_name() returned something "
                "which pointed to 0.\n");
        result = 1;
        goto done;
    }

    while (pdev) {
        count ++;
        printdevice(*pdev);
        pdev = mapper_device_query_next(pdev);
    }

    if (count != 2) {
        eprintf("Expected 2 records, but counted %d.\n", count);
        result = 1;
        goto done;
    }

    /*********/

    eprintf("\nFind devices with property 'host'=='192.168.0.100':\n");

    pdev = mapper_database_devices_by_property(db, "host", 1, 's',
                                               "192.168.0.100", MAPPER_OP_EQUAL);

    count=0;
    if (!pdev) {
        eprintf("mapper_database_devices_by_property() returned 0.\n");
        result = 1;
        goto done;
    }
    if (!*pdev) {
        eprintf("mapper_database_devices_by_property() returned something "
                "which pointed to 0.\n");
        result = 1;
        goto done;
    }

    while (pdev) {
        count ++;
        printdevice(*pdev);
        pdev = mapper_device_query_next(pdev);
    }

    if (count != 2) {
        eprintf("Expected 2 records, but counted %d.\n", count);
        result = 1;
        goto done;
    }

    /*********/

    eprintf("\nFind devices with property 'port'<5678:\n");

    int port = 5678;
    pdev = mapper_database_devices_by_property(db, "port", 1, 'i', &port,
                                               MAPPER_OP_LESS_THAN);

    count=0;
    if (!pdev) {
        eprintf("mapper_database_devices_by_property() returned 0.\n");
        result = 1;
        goto done;
    }
    if (!*pdev) {
        eprintf("mapper_database_devices_by_property() returned something "
                "which pointed to 0.\n");
        result = 1;
        goto done;
    }

    while (pdev) {
        count ++;
        printdevice(*pdev);
        pdev = mapper_device_query_next(pdev);
    }

    if (count != 3) {
        eprintf("Expected 3 records, but counted %d.\n", count);
        result = 1;
        goto done;
    }

    /*********/

    eprintf("\nFind devices with property 'num_outputs'==2:\n");
    int temp = 2;
    pdev = mapper_database_devices_by_property(db, "num_outputs", 1, 'i', &temp,
                                               MAPPER_OP_EQUAL);

    count=0;
    if (!pdev) {
        eprintf("mapper_database_devices_by_property() returned 0.\n");
        result = 1;
        goto done;
    }
    if (!*pdev) {
        eprintf("mapper_database_devices_by_property() returned something "
                "which pointed to 0.\n");
        result = 1;
        goto done;
    }

    while (pdev) {
        count ++;
        printdevice(*pdev);
        pdev = mapper_device_query_next(pdev);
    }

    if (count != 1) {
        eprintf("Expected 1 records, but counted %d.\n", count);
        result = 1;
        goto done;
    }

    /*********/

    eprintf("\nFind devices with properties 'host'!='localhost' AND 'port'>=4000:\n");

    pdev = mapper_database_devices_by_property(db, "host", 1, 's', "localhost",
                                               MAPPER_OP_NOT_EQUAL);
    pdev2 = mapper_database_devices_by_property(db, "port", 1, 'i', &port,
                                                MAPPER_OP_GREATER_THAN_OR_EQUAL);
    pdev = mapper_device_query_intersection(pdev, pdev2);

    count=0;
    if (!pdev) {
        eprintf("mapper_database_devices_by_property() returned 0.\n");
        result = 1;
        goto done;
    }
    if (!*pdev) {
        eprintf("mapper_database_devices_by_property() returned something "
                "which pointed to 0.\n");
        result = 1;
        goto done;
    }

    while (pdev) {
        count ++;
        printdevice(*pdev);
        pdev = mapper_device_query_next(pdev);
    }

    if (count != 1) {
        eprintf("Expected 1 record, but counted %d.\n", count);
        result = 1;
        goto done;
    }

    /*********/

    eprintf("\n--- Signals ---\n");

    eprintf("\nFind all signals for device 'testdatabase.1':\n");

    dev = mapper_database_device_by_name(db, "testdatabase.1");
    psig = mapper_device_signals(dev, MAPPER_DIR_ANY);

    count=0;
    if (!psig) {
        eprintf("mapper_device_signals() returned 0.\n");
        result = 1;
        goto done;
    }
    if (!*psig) {
        eprintf("mapper_device_signals() returned something "
                "which pointed to 0.\n");
        result = 1;
        goto done;
    }

    while (psig) {
        count ++;
        printsignal(*psig);
        psig = mapper_signal_query_next(psig);
    }

    if (count != 4) {
        eprintf("Expected 4 records, but counted %d.\n", count);
        result = 1;
        goto done;
    }

    /*********/

    eprintf("\nFind all signals for device 'testdatabase__xx.2':\n");

    dev = mapper_database_device_by_name(db, "testdatabase__xx.2");
    psig = mapper_device_signals(dev, MAPPER_DIR_ANY);

    count=0;
    if (psig) {
        eprintf("mapper_device_signals() incorrectly found something.\n");
        printsignal(*psig);
        mapper_signal_query_done(psig);
        result = 1;
        goto done;
    }
    else
        eprintf("  correctly returned 0.\n");

    /*********/

    eprintf("\nFind all outputs for device 'testdatabase__.2':\n");

    dev = mapper_database_device_by_name(db, "testdatabase__.2");
    psig = mapper_device_signals(dev, MAPPER_DIR_ANY);

    count=0;
    if (!psig) {
        eprintf("mapper_device_signals() returned 0.\n");
        result = 1;
        goto done;
    }
    if (!*psig) {
        eprintf("mapper_device_signals() returned something "
                "which pointed to 0.\n");
        result = 1;
        goto done;
    }

    while (psig) {
        count ++;
        printsignal(*psig);
        psig = mapper_signal_query_next(psig);
    }

    if (count != 3) {
        eprintf("Expected 3 records, but counted %d.\n", count);
        result = 1;
        goto done;
    }

    /*********/

    eprintf("\nFind signal matching 'in' for device 'testdatabase.1':\n");

    dev = mapper_database_device_by_name(db, "testdatabase.1");
    psig = mapper_device_signals(dev, MAPPER_DIR_ANY);
    psig2 = mapper_database_signals_by_name(db, "*in*");
    psig = mapper_signal_query_intersection(psig, psig2);

    count=0;
    if (!psig) {
        eprintf("intersection of mapper_device_signals() and "
                "mapper_database_signals_by_name() returned 0.\n");
        result = 1;
        goto done;
    }
    if (!*psig) {
        eprintf("intersection of mapper_device_signals() and "
                "mapper_database_signals_by_name() returned something "
                "which pointed to 0.\n");
        result = 1;
        goto done;
    }

    while (psig) {
        count ++;
        printsignal(*psig);
        psig = mapper_signal_query_next(psig);
    }

    if (count != 2) {
        eprintf("Expected 2 records, but counted %d.\n", count);
        result = 1;
        goto done;
    }

    /*********/

    eprintf("\nFind signal matching 'out' for device 'testdatabase.1':\n");

    dev = mapper_database_device_by_name(db, "testdatabase.1");
    psig = mapper_device_signals(dev, MAPPER_DIR_ANY);
    psig2 = mapper_database_signals_by_name(db, "*out*");
    psig = mapper_signal_query_intersection(psig, psig2);

    count=0;
    if (!psig) {
        eprintf("intersection of mapper_device_signals() and "
                "mapper_database_signals_by_name() returned 0.\n");
        result = 1;
        goto done;
    }
    if (!*psig) {
        eprintf("intersection of mapper_device_signals() and "
                "mapper_database_signals_by_name() returned something "
                "which pointed to 0.\n");
        result = 1;
        goto done;
    }

    while (psig) {
        count ++;
        printsignal(*psig);
        psig = mapper_signal_query_next(psig);
    }

    if (count != 2) {
        eprintf("Expected 2 records, but counted %d.\n", count);
        result = 1;
        goto done;
    }

    /*********/

    eprintf("\nFind signal matching 'out' for device 'testdatabase__.2':\n");

    dev = mapper_database_device_by_name(db, "testdatabase__.2");
    psig = mapper_device_signals(dev, MAPPER_DIR_ANY);
    psig2 = mapper_database_signals_by_name(db, "*out*");
    psig = mapper_signal_query_intersection(psig, psig2);

    count=0;
    if (!psig) {
        eprintf("intersection of mapper_device_signals() and "
                "mapper_database_signals_by_name() returned 0.\n");
        result = 1;
        goto done;
    }
    if (!*psig) {
        eprintf("intersection of mapper_device_signals() and "
                "mapper_database_signals_by_name() returned something "
                "which pointed to 0.\n");
        result = 1;
        goto done;
    }

    while (psig) {
        count ++;
        printsignal(*psig);
        psig = mapper_signal_query_next(psig);
    }

    if (count != 1) {
        eprintf("Expected 1 record, but counted %d.\n", count);
        result = 1;
        goto done;
    }

    /*********/

    eprintf("\n--- maps ---\n");

    eprintf("\nFind maps with source 'out1':\n");

    psig = mapper_database_signals_by_name(db, "out1");
    pmap = 0;
    while (psig) {
        pmap2 = mapper_signal_maps(*psig, MAPPER_DIR_OUTGOING);
        pmap = mapper_map_query_union(pmap, pmap2);
        psig = mapper_signal_query_next(psig);
    }

    count=0;
    if (!pmap) {
        eprintf("combined query returned 0.\n");
        result = 1;
        goto done;
    }
    if (!*pmap) {
        eprintf("combined query returned something which pointed to 0.\n");
        result = 1;
        goto done;
    }

    while (pmap) {
        count ++;
        printmap(*pmap);
        pmap = mapper_map_query_next(pmap);
    }

    if (count != 3) {
        eprintf("Expected 3 records, but counted %d.\n", count);
        result = 1;
        goto done;
    }

    /*********/

    eprintf("\nFind maps for device 'testdatabase.1', source 'out1':\n");

    dev = mapper_database_device_by_name(db, "testdatabase.1");
    sig = mapper_device_signal_by_name(dev, "out1");
    pmap = mapper_signal_maps(sig, 0);

    count=0;
    if (!pmap) {
        eprintf("mapper_signal_maps() returned 0.\n");
        result = 1;
        goto done;
    }
    if (!*pmap) {
        eprintf("mapper_signal_maps() returned something which pointed to 0.\n");
        result = 1;
        goto done;
    }

    while (pmap) {
        count ++;
        printmap(*pmap);
        pmap = mapper_map_query_next(pmap);
    }

    if (count != 2) {
        eprintf("Expected 2 records, but counted %d.\n", count);
        result = 1;
        goto done;
    }

    /*********/

    eprintf("\nFind maps with destination signal named 'in2':\n");

    psig = mapper_database_signals_by_name(db, "in2");
    pmap = 0;
    while (psig) {
        pmap2 = mapper_signal_maps(*psig, MAPPER_DIR_INCOMING);
        pmap = mapper_map_query_union(pmap, pmap2);
        psig = mapper_signal_query_next(psig);
    }

    count=0;
    if (!pmap) {
        eprintf("combined query returned 0.\n");
        result = 1;
        goto done;
    }
    if (!*pmap) {
        eprintf("combined query returned something which pointed to 0.\n");
        result = 1;
        goto done;
    }

    while (pmap) {
        count ++;
        printmap(*pmap);
        pmap = mapper_map_query_next(pmap);
    }

    if (count != 1) {
        eprintf("Expected 1 record, but counted %d.\n", count);
        result = 1;
        goto done;
    }

    /*********/

    eprintf("\nFind maps for device 'testdatabase__.2', destination 'in1':\n");

    dev = mapper_database_device_by_name(db, "testdatabase__.2");
    sig = mapper_device_signal_by_name(dev, "in1");
    pmap = mapper_signal_maps(sig, MAPPER_DIR_INCOMING);

    count=0;
    if (!pmap) {
        eprintf("mapper_signal_maps() returned 0.\n");
        result = 1;
        goto done;
    }
    if (!*pmap) {
        eprintf("mapper_signal_maps() returned something which pointed to 0.\n");
        result = 1;
        goto done;
    }

    while (pmap) {
        count ++;
        printmap(*pmap);
        pmap = mapper_map_query_next(pmap);
    }

    if (count != 2) {
        eprintf("Expected 2 records, but counted %d.\n", count);
        result = 1;
        goto done;
    }

    /*********/

    eprintf("\nFind maps for source device 'testdatabase__.2', signal 'out1'"
            "\n          AND dest device 'testdatabase.1', signal 'in1':\n");

    // get maps with source signal
    dev = mapper_database_device_by_name(db, "testdatabase__.2");
    sig = mapper_device_signal_by_name(dev, "out1");
    pmap = mapper_signal_maps(sig, MAPPER_DIR_OUTGOING);

    // get maps with destination signal
    dev = mapper_database_device_by_name(db, "testdatabase.1");
    sig = mapper_device_signal_by_name(dev, "in1");
    pmap2 = mapper_signal_maps(sig, MAPPER_DIR_INCOMING);

    // intersect map queries
    pmap = mapper_map_query_intersection(pmap, pmap2);

    count=0;
    if (!pmap) {
        eprintf("combined query returned 0.\n");
        result = 1;
        goto done;
    }
    if (!*pmap) {
        eprintf("combined query returned something which pointed to 0.\n");
        result = 1;
        goto done;
    }

    while (pmap) {
        count ++;
        printmap(*pmap);
        pmap = mapper_map_query_next(pmap);
    }

    if (count != 1) {
        eprintf("Expected 1 records, but counted %d.\n", count);
        result = 1;
        goto done;
    }

    /*********/

    eprintf("\nFind maps for source device 'testdatabase__.2', signals matching 'out',"
            "\n          AND dest device 'testdatabase.1', all signals:\n");

    // build source query
    dev = mapper_database_device_by_name(db, "testdatabase__.2");
    psig = mapper_device_signals(dev, MAPPER_DIR_ANY);
    psig2 = mapper_database_signals_by_name(db, "*out*");
    psig = mapper_signal_query_intersection(psig, psig2);

    pmap = 0;
    while (psig) {
        pmap2 = mapper_signal_maps(*psig, MAPPER_DIR_OUTGOING);
        pmap = mapper_map_query_union(pmap, pmap2);
        psig = mapper_signal_query_next(psig);
    }

    // build destination query
    dev = mapper_database_device_by_name(db, "testdatabase.1");
    pmap2 = mapper_device_maps(dev, MAPPER_DIR_ANY);

    // intersect queries
    pmap = mapper_map_query_intersection(pmap, pmap2);

    count=0;
    if (!pmap) {
        eprintf("combined query returned 0.\n");
        result = 1;
        goto done;
    }
    if (!*pmap) {
        eprintf("combined query returned something which pointed to 0.\n");
        result = 1;
        goto done;
    }

    while (pmap) {
        count ++;
        printmap(*pmap);
        pmap = mapper_map_query_next(pmap);
    }

    if (count != 1) {
        eprintf("Expected 1 record, but counted %d.\n", count);
        result = 1;
        goto done;
    }

    /*********/
done:
    mapper_network_free(net);
    if (!verbose)
        printf("..................................................");
    printf("Test %s.\n", result ? "FAILED" : "PASSED");
    return result;
}
