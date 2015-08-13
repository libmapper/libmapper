
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
        mapper_device_pp(dev);
}

void printsignal(mapper_signal sig)
{
    if (verbose)
        mapper_signal_pp(sig, 1);
}

void printmap(mapper_map map)
{
    if (verbose)
        mapper_map_pp(map);
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
                        eprintf("testdb.c: possible arguments "
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

    lo_arg *args[20];
    mapper_message msg;
    int port=1234;
    int one_i=1, two_i=2;
    float zero_f=0.f, one_f=1.f, two_f=2.f;
    uint64_t id = 1;
    mapper_network net = mapper_network_new(0, 0, 0);
    mapper_db db = &net->db;

    mapper_device dev, *pdev, *pdev2;
    mapper_signal sig, *psig, *psig2;
    mapper_map *pmap, *pmap2;

    /* Test the database functions */

    args[0] = (lo_arg*)"@port";
    args[1] = (lo_arg*)&port;
    args[2] = (lo_arg*)"@host";
    args[3] = (lo_arg*)"localhost";

    if (!(msg = mapper_message_parse_params(4, "siss", args))) {
        eprintf("1: Error, parsing failed.\n");
        result = 1;
        goto done;
    }

    mapper_db_add_or_update_device_params(db, "testdb.1", msg);
    mapper_db_add_or_update_device_params(db, "testdb__.2", msg);

    port = 3000;
    args[3] = (lo_arg*)"192.168.0.100";
    mapper_db_add_or_update_device_params(db, "testdb.3", msg);
    port = 5678;
    mapper_db_add_or_update_device_params(db, "testdb__.4", msg);

    mapper_message_free(msg);

    args[0] = (lo_arg*)"@direction";
    args[1] = (lo_arg*)"input";
    args[2] = (lo_arg*)"@type";
    args[3] = (lo_arg*)"f";
    args[4] = (lo_arg*)"@id";
    args[5] = (lo_arg*)&id;

    if (!(msg = mapper_message_parse_params(6, "ssscsh", args))) {
        eprintf("2: Error, parsing failed.\n");
        result = 1;
        goto done;
    }

    id++;
    mapper_db_add_or_update_signal_params(db, "in1", "testdb.1", msg);
    id++;
    mapper_db_add_or_update_signal_params(db, "in2", "testdb.1", msg);

    mapper_message_free(msg);

    args[1] = (lo_arg*)"output";

    if (!(msg = mapper_message_parse_params(6, "ssscsh", args))) {
        eprintf("2: Error, parsing failed.\n");
        result = 1;
        goto done;
    }
    id++;
    mapper_db_add_or_update_signal_params(db, "out1", "testdb.1", msg);
    id++;
    mapper_db_add_or_update_signal_params(db, "out2", "testdb.1", msg);
    id++;
    mapper_db_add_or_update_signal_params(db, "out1", "testdb__.2", msg);

    mapper_message_free(msg);

    args[0] = (lo_arg*)"@mode";
    args[1] = (lo_arg*)"bypass";
    args[2] = (lo_arg*)"@boundMin";
    args[3] = (lo_arg*)"none";
    args[4] = (lo_arg*)"@id";
    args[5] = (lo_arg*)&id;

    if (!(msg = mapper_message_parse_params(6, "sssssh", args))) {
        eprintf("4: Error, parsing failed.\n");
        result = 1;
        goto done;
    }

    id++;
    const char *src_sig_name = "testdb.1/out2";
    mapper_db_add_or_update_map_params(db, 1, &src_sig_name, "testdb__.2/in1",
                                       msg);

    id++;
    src_sig_name = "testdb__.2/out1";
    mapper_db_add_or_update_map_params(db, 1, &src_sig_name, "testdb.1/in1",
                                       msg);

    mapper_message_free(msg);

    args[0] = (lo_arg*)"@mode";
    args[1] = (lo_arg*)"expression";
    args[2] = (lo_arg*)"@expression";
    args[3] = (lo_arg*)"(x-10)*80";
    args[4] = (lo_arg*)"@boundMin";
    args[5] = (lo_arg*)"clamp";
    args[6] = (lo_arg*)"@src@length";
    args[7] = (lo_arg*)&two_i;
    args[8] = (lo_arg*)"@src@type";
    args[9] = (lo_arg*)"f";
    args[10] = (lo_arg*)"@src@min";
    args[11] = (lo_arg*)&zero_f;
    args[12] = (lo_arg*)&one_f;
    args[13] = (lo_arg*)"@src@max";
    args[14] = (lo_arg*)&one_f;
    args[15] = (lo_arg*)&two_f;
    args[16] = (lo_arg*)"@id";
    args[17] = (lo_arg*)&id;

    if (!(msg = mapper_message_parse_params(18, "sssssssisssffsffsh", args))) {
        eprintf("5: Error, parsing failed.\n");
        result = 1;
        goto done;
    }

    id++;
    src_sig_name = "testdb.1/out1";
    mapper_db_add_or_update_map_params(db, 1, &src_sig_name, "testdb__.2/in2",
                                       msg);

    id++;
    src_sig_name = "testdb.1/out1";
    mapper_db_add_or_update_map_params(db, 1, &src_sig_name, "testdb__.2/in1",
                                       msg);

    mapper_message_free(msg);

    args[6] = (lo_arg*)"@src@length";
    args[7] = (lo_arg*)&one_i;
    args[8] = (lo_arg*)&two_i;
    args[9] = (lo_arg*)"@dst@length";
    args[10] = (lo_arg*)&one_i;
    args[11] = (lo_arg*)"@src@type";
    args[12] = (lo_arg*)"f";
    args[13] = (lo_arg*)"i";
    args[14] = (lo_arg*)"@id";
    args[15] = (lo_arg*)&id;

    if (!(msg = mapper_message_parse_params(16, "sssssssiisissssh", args))) {
        eprintf("5: Error, parsing failed.\n");
        result = 1;
        goto done;
    }

    id++;
    const char *multi_source[] = {"testdb__.2/out1", "testdb__.2/out2"};
    mapper_db_add_or_update_map_params(db, 2, multi_source, "testdb.1/in2", msg);

    mapper_message_free(msg);

    /*********/

    if (verbose) {
        eprintf("Dump:\n");
        mapper_db_dump(db);
    }

    /*********/

    eprintf("\n--- Devices ---\n");

    eprintf("\nWalk the whole database:\n");
    pdev = mapper_db_devices(db);
    int count=0;
    if (!pdev) {
        eprintf("mapper_db_devices() returned 0.\n");
        result = 1;
        goto done;
    }
    if (!*pdev) {
        eprintf("mapper_db_devices() returned something "
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

    eprintf("\nFind device named 'testdb.3':\n");

    dev = mapper_db_device_by_name(db, "testdb.3");
    if (!dev) {
        eprintf("Not found.\n");
        result = 1;
        goto done;
    }

    printdevice(dev);

    /*********/

    eprintf("\nFind device named 'dummy':\n");

    dev = mapper_db_device_by_name(db, "dummy");
    if (dev) {
        eprintf("unexpected found 'dummy': %p\n", dev);
        result = 1;
        goto done;
    }
    eprintf("  not found, good.\n");

    /*********/

    eprintf("\nFind devices matching '__':\n");

    pdev = mapper_db_devices_by_name_match(db, "__");

    count=0;
    if (!pdev) {
        eprintf("mapper_db_devices_by_name_match() returned 0.\n");
        result = 1;
        goto done;
    }
    if (!*pdev) {
        eprintf("mapper_db_devices_by_name_match() returned something "
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

    pdev = mapper_db_devices_by_property(db, "host", 1, 's', "192.168.0.100",
                                         MAPPER_OP_EQUAL);

    count=0;
    if (!pdev) {
        eprintf("mapper_db_devices_by_property() returned 0.\n");
        result = 1;
        goto done;
    }
    if (!*pdev) {
        eprintf("mapper_db_devices_by_property() returned something "
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

    pdev = mapper_db_devices_by_property(db, "port", 1, 'i', &port,
                                         MAPPER_OP_LESS_THAN);

    count=0;
    if (!pdev) {
        eprintf("mapper_db_devices_by_property() returned 0.\n");
        result = 1;
        goto done;
    }
    if (!*pdev) {
        eprintf("mapper_db_devices_by_property() returned something "
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
    pdev = mapper_db_devices_by_property(db, "num_outputs", 1, 'i', &temp,
                                         MAPPER_OP_EQUAL);

    count=0;
    if (!pdev) {
        eprintf("mapper_db_devices_by_property() returned 0.\n");
        result = 1;
        goto done;
    }
    if (!*pdev) {
        eprintf("mapper_db_devices_by_property() returned something "
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

    pdev = mapper_db_devices_by_property(db, "host", 1, 's', "localhost",
                                         MAPPER_OP_NOT_EQUAL);
    pdev2 = mapper_db_devices_by_property(db, "port", 1, 'i', &port,
                                          MAPPER_OP_GREATER_THAN_OR_EQUAL);
    pdev = mapper_device_query_intersection(pdev, pdev2);

    count=0;
    if (!pdev) {
        eprintf("mapper_db_devices_by_property() returned 0.\n");
        result = 1;
        goto done;
    }
    if (!*pdev) {
        eprintf("mapper_db_devices_by_property() returned something "
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

    eprintf("\nFind all signals for device 'testdb.1':\n");

    dev = mapper_db_device_by_name(db, "testdb.1");
    psig = mapper_db_device_signals(db, dev, MAPPER_DIR_ANY);

    count=0;
    if (!psig) {
        eprintf("mapper_db_device_signals() returned 0.\n");
        result = 1;
        goto done;
    }
    if (!*psig) {
        eprintf("mapper_db_device_signals() returned something "
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

    eprintf("\nFind all signals for device 'testdb__xx.2':\n");

    dev = mapper_db_device_by_name(db, "testdb__xx.2");
    psig = mapper_db_device_signals(db, dev, MAPPER_DIR_ANY);

    count=0;
    if (psig) {
        eprintf("mapper_db_device_signals() incorrectly found something.\n");
        printsignal(*psig);
        mapper_signal_query_done(psig);
        result = 1;
        goto done;
    }
    else
        eprintf("  correctly returned 0.\n");

    /*********/

    eprintf("\nFind all outputs for device 'testdb__.2':\n");

    dev = mapper_db_device_by_name(db, "testdb__.2");
    psig = mapper_db_device_signals(db, dev, MAPPER_DIR_ANY);

    count=0;
    if (!psig) {
        eprintf("mapper_db_device_signals() returned 0.\n");
        result = 1;
        goto done;
    }
    if (!*psig) {
        eprintf("mapper_db_device_signals() returned something "
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

    eprintf("\nFind signal matching 'in' for device 'testdb.1':\n");

    dev = mapper_db_device_by_name(db, "testdb.1");
    psig = mapper_db_device_signals(db, dev, MAPPER_DIR_ANY);
    psig2 = mapper_db_signals_by_name_match(db, "in");
    psig = mapper_signal_query_intersection(psig, psig2);

    count=0;
    if (!psig) {
        eprintf("mapper_db_device_signals_by_name_match() returned 0.\n");
        result = 1;
        goto done;
    }
    if (!*psig) {
        eprintf("mapper_db_device_signals_by_name_match() returned something "
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

    eprintf("\nFind signal matching 'out' for device 'testdb.1':\n");

    dev = mapper_db_device_by_name(db, "testdb.1");
    psig = mapper_db_device_signals(db, dev, MAPPER_DIR_ANY);
    psig2 = mapper_db_signals_by_name_match(db, "out");
    psig = mapper_signal_query_intersection(psig, psig2);

    count=0;
    if (!psig) {
        eprintf("mapper_db_device_signals_by_name_match() returned 0.\n");
        result = 1;
        goto done;
    }
    if (!*psig) {
        eprintf("mapper_db_device_signals_by_name_match() returned something "
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

    eprintf("\nFind signal matching 'out' for device 'testdb__.2':\n");

    dev = mapper_db_device_by_name(db, "testdb__.2");
    psig = mapper_db_device_signals(db, dev, MAPPER_DIR_ANY);
    psig2 = mapper_db_signals_by_name_match(db, "out");
    psig = mapper_signal_query_intersection(psig, psig2);

    count=0;
    if (!psig) {
        eprintf("mapper_db_device_signals_by_name_match() returned 0.\n");
        result = 1;
        goto done;
    }
    if (!*psig) {
        eprintf("mapper_db_device_signals_by_name_match() returned something "
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

    eprintf("\n--- maps ---\n");

    eprintf("\nFind maps with source 'out1':\n");

    psig = mapper_db_signals_by_name(db, "out1");
    pmap = 0;
    while (psig) {
        pmap2 = mapper_db_signal_maps(db, *psig, MAPPER_OUTGOING);
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

    if (count != 4) {
        eprintf("Expected 4 records, but counted %d.\n", count);
        result = 1;
        goto done;
    }

    /*********/

    eprintf("\nFind maps for device 'testdb.1', source 'out1':\n");

    dev = mapper_db_device_by_name(db, "testdb.1");
    sig = mapper_db_device_signal_by_name(db, dev, "out1");
    pmap = mapper_db_signal_maps(db, sig, 0);

    count=0;
    if (!pmap) {
        eprintf("mapper_db_signal_maps() returned 0.\n");
        result = 1;
        goto done;
    }
    if (!*pmap) {
        eprintf("mapper_db_signal_maps() "
                "returned something which pointed to 0.\n");
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

    psig = mapper_db_signals_by_name(db, "in2");
    pmap = 0;
    while (psig) {
        pmap2 = mapper_db_signal_maps(db, *psig, MAPPER_INCOMING);
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

    if (count != 2) {
        eprintf("Expected 2 records, but counted %d.\n", count);
        result = 1;
        goto done;
    }

    /*********/

    eprintf("\nFind maps for device 'testdb__.2', destination 'in1':\n");

    dev = mapper_db_device_by_name(db, "testdb__.2");
    sig = mapper_db_device_signal_by_name(db, dev, "in1");
    pmap = mapper_db_signal_maps(db, sig, MAPPER_INCOMING);

    count=0;
    if (!pmap) {
        eprintf("mapper_db_signal_maps() returned 0.\n");
        result = 1;
        goto done;
    }
    if (!*pmap) {
        eprintf("mapper_db_signal_maps() "
                "returned something which pointed to 0.\n");
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

    eprintf("\nFind maps for source device 'testdb__.2', signal 'out1'"
            "\n          AND dest device 'testdb.1', signal 'in1':\n");

    // get maps with source signal
    dev = mapper_db_device_by_name(db, "testdb__.2");
    sig = mapper_db_device_signal_by_name(db, dev, "out1");
    pmap = mapper_db_signal_maps(db, sig, MAPPER_OUTGOING);

    // get maps with destination signal
    dev = mapper_db_device_by_name(db, "testdb.1");
    sig = mapper_db_device_signal_by_name(db, dev, "in1");
    pmap2 = mapper_db_signal_maps(db, sig, MAPPER_INCOMING);

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

    eprintf("\nFind maps for source device 'testdb__.2', signals matching 'out',"
            "\n          AND dest device 'testdb.1', all signals:\n");

    // build source query
    dev = mapper_db_device_by_name(db, "testdb__.2");
    psig = mapper_db_device_signals(db, dev, MAPPER_DIR_ANY);
    psig2 = mapper_db_signals_by_name_match(db, "out");
    psig = mapper_signal_query_intersection(psig, psig2);

    pmap = 0;
    while (psig) {
        pmap2 = mapper_db_signal_maps(db, *psig, MAPPER_OUTGOING);
        pmap = mapper_map_query_union(pmap, pmap2);
        psig = mapper_signal_query_next(psig);
    }

    // build destination query
    dev = mapper_db_device_by_name(db, "testdb.1");
    pmap2 = mapper_db_device_maps(db, dev, MAPPER_DIR_ANY);

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

    if (count != 2) {
        eprintf("Expected 2 records, but counted %d.\n", count);
        result = 1;
        goto done;
    }

    /*********/
done:
    while (db->devices)
        mapper_db_remove_device(db, db->devices, 1);
    if (!verbose)
        printf("..................................................");
    printf("Test %s.\n", result ? "FAILED" : "PASSED");
    return result;
}
