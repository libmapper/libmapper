
#include <stdio.h>
#include <string.h>
#include <lo/lo_lowlevel.h>
#include "../src/types_internal.h"
#include "../src/mapper_internal.h"

#define eprintf(format, ...) do {               \
    if (verbose)                                \
        fprintf(stdout, format, ##__VA_ARGS__); \
} while(0)

int verbose = 1;

void printdevice(mapper_db_device dev)
{
    eprintf("  name=%s, host=%s, port=%d, id=%llu\n",
            dev->name, dev->host, dev->port, dev->id);
}

void printsignal(mapper_db_signal sig)
{
    eprintf("  name='%s':'%s', type=%c, length=%d",
            sig->device->name, sig->name, sig->type, sig->length);
    if (sig->unit)
        eprintf(", unit=%s", sig->unit);
    if (sig->minimum && verbose) {
        printf("minimum=");
        mapper_prop_pp(sig->type, sig->length, sig->minimum);
    }
    if (sig->maximum && verbose) {
        printf("maximum=");
        mapper_prop_pp(sig->type, sig->length, sig->maximum);
    }
    eprintf("\n");
}

void printmap(mapper_db_map map)
{
    int i;
    if (verbose) {
        printf("  source=");
        if (map->num_sources > 1)
            printf("[");
        for (i = 0; i < map->num_sources; i++)
            printf("'%s':'%s', ", map->sources[i].signal->device->name,
                   map->sources[i].signal->name);
        if (map->num_sources > 1)
            printf("\b\b], ");
        printf("dest='%s':'%s'\n", map->destination.signal->device->name,
               map->destination.signal->name);
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
    mapper_message_t msg;
    int port=1234;
    int one_i=1, two_i=2;
    float zero_f=0.f, one_f=1.f, two_f=2.f;
    uint64_t id = 1;
    mapper_db_t db_t, *db = &db_t;
    memset(db, 0, sizeof(db_t));

    /* Test the database functions */

    args[0] = (lo_arg*)"@port";
    args[1] = (lo_arg*)&port;
    args[2] = (lo_arg*)"@host";
    args[3] = (lo_arg*)"localhost";

    if (!mapper_msg_parse_params(&msg, "/registered", "siss", 4, args))
    {
        eprintf("1: Error, parsing failed.\n");
        result = 1;
        goto done;
    }

    mapper_db_add_or_update_device_params(db, "testdb.1", &msg, 0);
    mapper_db_add_or_update_device_params(db, "testdb__.2", &msg, 0);

    port = 3000;
    args[3] = (lo_arg*)"192.168.0.100";
    mapper_db_add_or_update_device_params(db, "testdb.3", &msg, 0);
    port = 5678;
    mapper_db_add_or_update_device_params(db, "testdb__.4", &msg, 0);

    args[0] = (lo_arg*)"@direction";
    args[1] = (lo_arg*)"input";
    args[2] = (lo_arg*)"@type";
    args[3] = (lo_arg*)"f";
    args[4] = (lo_arg*)"@id";
    args[5] = (lo_arg*)&id;

    if (!mapper_msg_parse_params(&msg, "testdb.1/signal",
                                 "ssscsh", 6, args))
    {
        eprintf("2: Error, parsing failed.\n");
        result = 1;
        goto done;
    }

    id++;
    mapper_db_add_or_update_signal_params(db, "in1", "testdb.1", &msg);
    id++;
    mapper_db_add_or_update_signal_params(db, "in2", "testdb.1", &msg);
    id++;
    mapper_db_add_or_update_signal_params(db, "in2", "testdb.1", &msg);

    args[1] = (lo_arg*)"output";

    if (!mapper_msg_parse_params(&msg, "testdb.1/signal",
                                 "ssscsh", 6, args))
    {
        eprintf("2: Error, parsing failed.\n");
        result = 1;
        goto done;
    }
    id++;
    mapper_db_add_or_update_signal_params(db, "out1", "testdb.1", &msg);
    id++;
    mapper_db_add_or_update_signal_params(db, "out2", "testdb.1", &msg);
    id++;
    mapper_db_add_or_update_signal_params(db, "out1", "testdb__.2", &msg);

    args[0] = (lo_arg*)"@mode";
    args[1] = (lo_arg*)"bypass";
    args[2] = (lo_arg*)"@boundMin";
    args[3] = (lo_arg*)"none";
    args[4] = (lo_arg*)"@id";
    args[5] = (lo_arg*)&id;

    if (!mapper_msg_parse_params(&msg, "/mapped", "sssssh", 6, args))
    {
        eprintf("4: Error, parsing failed.\n");
        result = 1;
        goto done;
    }

    id++;
    const char *src_sig_name = "testdb.1/out2";
    mapper_db_add_or_update_map_params(db, 1, &src_sig_name, "testdb__.2/in1", &msg);

    id++;
    src_sig_name = "testdb__.2/out1";
    mapper_db_add_or_update_map_params(db, 1, &src_sig_name, "testdb.1/in1", &msg);

    args[0] = (lo_arg*)"@mode";
    args[1] = (lo_arg*)"expression";
    args[2] = (lo_arg*)"@expression";
    args[3] = (lo_arg*)"(x-10)*80";
    args[4] = (lo_arg*)"@boundMin";
    args[5] = (lo_arg*)"clamp";
    args[6] = (lo_arg*)"@srcLength";
    args[7] = (lo_arg*)&two_i;
    args[8] = (lo_arg*)"@srcType";
    args[9] = (lo_arg*)"f";
    args[10] = (lo_arg*)"@srcMin";
    args[11] = (lo_arg*)&zero_f;
    args[12] = (lo_arg*)&one_f;
    args[13] = (lo_arg*)"@srcMax";
    args[14] = (lo_arg*)&one_f;
    args[15] = (lo_arg*)&two_f;
    args[16] = (lo_arg*)"@id";
    args[17] = (lo_arg*)&id;

    if (!mapper_msg_parse_params(&msg, "/mapped", "sssssssisssffsffsh", 18, args))
    {
        eprintf("5: Error, parsing failed.\n");
        result = 1;
        goto done;
    }

    id++;
    src_sig_name = "testdb.1/out1";
    mapper_db_add_or_update_map_params(db, 1, &src_sig_name, "testdb__.2/in2", &msg);

    id++;
    src_sig_name = "testdb.1/out1";
    mapper_db_add_or_update_map_params(db, 1, &src_sig_name, "testdb__.2/in1", &msg);

    args[6] = (lo_arg*)"@srcLength";
    args[7] = (lo_arg*)&one_i;
    args[8] = (lo_arg*)&two_i;
    args[9] = (lo_arg*)"@destLength";
    args[10] = (lo_arg*)&one_i;
    args[11] = (lo_arg*)"@srcType";
    args[12] = (lo_arg*)"f";
    args[13] = (lo_arg*)"i";
    args[14] = (lo_arg*)"@id";
    args[15] = (lo_arg*)&id;

    if (!mapper_msg_parse_params(&msg, "/mapped", "sssssssiisissssh", 16, args))
    {
        eprintf("5: Error, parsing failed.\n");
        result = 1;
        goto done;
    }

    id++;
    const char *multi_source[] = {"testdb__.2/out1", "testdb__.2/out2"};
    mapper_db_add_or_update_map_params(db, 2, multi_source, "testdb.1/in2", &msg);

    /*********/

    if (verbose) {
        eprintf("Dump:\n");
        mapper_db_dump(db);
    }

    /*********/

    eprintf("\n--- Devices ---\n");

    eprintf("\nWalk the whole database:\n");
    mapper_db_device *pdev = mapper_db_get_devices(db);
    int count=0;
    if (!pdev) {
        eprintf("mapper_db_get_devices() returned 0.\n");
        result = 1;
        goto done;
    }
    if (!*pdev) {
        eprintf("mapper_db_get_devices() returned something "
               "which pointed to 0.\n");
        result = 1;
        goto done;
    }

    while (pdev) {
        count ++;
        printdevice(*pdev);
        pdev = mapper_db_device_next(pdev);
    }

    if (count != 4) {
        eprintf("Expected 4 records, but counted %d.\n", count);
        result = 1;
        goto done;
    }

    /*********/

    eprintf("\nFind device named 'testdb.3':\n");

    mapper_db_device dev = mapper_db_get_device_by_name(db, "testdb.3");
    if (!dev) {
        eprintf("Not found.\n");
        result = 1;
        goto done;
    }

    printdevice(dev);

    /*********/

    eprintf("\nFind device named 'dummy':\n");

    dev = mapper_db_get_device_by_name(db, "dummy");
    if (dev) {
        eprintf("unexpected found 'dummy': %p\n", dev);
        result = 1;
        goto done;
    }
    eprintf("  not found, good.\n");

    /*********/

    eprintf("\nFind devices matching '__':\n");

    pdev = mapper_db_get_devices_by_name_match(db, "__");

    count=0;
    if (!pdev) {
        eprintf("mapper_db_get_devices_by_name_match() returned 0.\n");
        result = 1;
        goto done;
    }
    if (!*pdev) {
        eprintf("mapper_db_get_devices_by_name_match() returned something "
                "which pointed to 0.\n");
        result = 1;
        goto done;
    }

    while (pdev) {
        count ++;
        printdevice(*pdev);
        pdev = mapper_db_device_next(pdev);
    }

    if (count != 2) {
        eprintf("Expected 2 records, but counted %d.\n", count);
        result = 1;
        goto done;
    }

    /*********/

    eprintf("\nFind devices with property 'host'=='192.168.0.100':\n");

    pdev = mapper_db_get_devices_by_property(db, "host", 's', 1,
                                             "192.168.0.100", 0);

    count=0;
    if (!pdev) {
        eprintf("mapper_db_get_devices_by_property() returned 0.\n");
        result = 1;
        goto done;
    }
    if (!*pdev) {
        eprintf("mapper_db_get_devices_by_property() returned something "
                "which pointed to 0.\n");
        result = 1;
        goto done;
    }

    while (pdev) {
        count ++;
        printdevice(*pdev);
        pdev = mapper_db_device_next(pdev);
    }

    if (count != 2) {
        eprintf("Expected 2 records, but counted %d.\n", count);
        result = 1;
        goto done;
    }

    /*********/

    eprintf("\nFind devices with property 'port'<5678:\n");

    pdev = mapper_db_get_devices_by_property(db, "port", 'i', 1, &port, "<");

    count=0;
    if (!pdev) {
        eprintf("mapper_db_get_devices_by_property() returned 0.\n");
        result = 1;
        goto done;
    }
    if (!*pdev) {
        eprintf("mapper_db_get_devices_by_property() returned something "
                "which pointed to 0.\n");
        result = 1;
        goto done;
    }

    while (pdev) {
        count ++;
        printdevice(*pdev);
        pdev = mapper_db_device_next(pdev);
    }

    if (count != 3) {
        eprintf("Expected 3 records, but counted %d.\n", count);
        result = 1;
        goto done;
    }

    /*********/

    eprintf("\nFind devices with property 'num_outputs'==2:\n");
    int temp = 2;
    pdev = mapper_db_get_devices_by_property(db, "num_outputs", 'i', 1, &temp, "==");

    count=0;
    if (!pdev) {
        eprintf("mapper_db_get_devices_by_property() returned 0.\n");
        result = 1;
        goto done;
    }
    if (!*pdev) {
        eprintf("mapper_db_get_devices_by_property() returned something "
                "which pointed to 0.\n");
        result = 1;
        goto done;
    }

    while (pdev) {
        count ++;
        printdevice(*pdev);
        pdev = mapper_db_device_next(pdev);
    }

    if (count != 1) {
        eprintf("Expected 1 records, but counted %d.\n", count);
        result = 1;
        goto done;
    }

    /*********/

    eprintf("\nFind devices with properties 'host'!='localhost' AND 'port'>=4000:\n");

    mapper_db_device *d1 = mapper_db_get_devices_by_property(db, "host", 's', 1, "localhost", "!=");
    mapper_db_device *d2 = mapper_db_get_devices_by_property(db, "port", 'i', 1, &port, ">=");
    pdev = mapper_db_device_query_intersection(db, d1, d2);

    count=0;
    if (!pdev) {
        eprintf("mapper_db_get_devices_by_property() returned 0.\n");
        result = 1;
        goto done;
    }
    if (!*pdev) {
        eprintf("mapper_db_get_devices_by_property() returned something "
                "which pointed to 0.\n");
        result = 1;
        goto done;
    }

    while (pdev) {
        count ++;
        printdevice(*pdev);
        pdev = mapper_db_device_next(pdev);
    }

    if (count != 1) {
        eprintf("Expected 1 record, but counted %d.\n", count);
        result = 1;
        goto done;
    }

    /*********/

    eprintf("\n--- Signals ---\n");

    eprintf("\nFind all inputs for device 'testdb.1':\n");

    mapper_db_signal *psig =
        mapper_db_get_device_inputs(db, mapper_db_get_device_by_name(db, "testdb.1"));

    count=0;
    if (!psig) {
        eprintf("mapper_db_get_device_inputs() returned 0.\n");
        result = 1;
        goto done;
    }
    if (!*psig) {
        eprintf("mapper_db_get_device_inputs() returned something "
                "which pointed to 0.\n");
        result = 1;
        goto done;
    }

    while (psig) {
        count ++;
        printsignal(*psig);
        psig = mapper_db_signal_next(psig);
    }

    if (count != 2) {
        eprintf("Expected 2 records, but counted %d.\n", count);
        result = 1;
        goto done;
    }

    /*********/

    eprintf("\nFind all outputs for device 'testdb.1':\n");

    psig = mapper_db_get_device_outputs(db, mapper_db_get_device_by_name(db, "testdb.1"));

    count=0;
    if (!psig) {
        eprintf("mapper_db_get_device_outputs() returned 0.\n");
        result = 1;
        goto done;
    }
    if (!*psig) {
        eprintf("mapper_db_get_device_outputs() returned something "
                "which pointed to 0.\n");
        result = 1;
        goto done;
    }

    while (psig) {
        count ++;
        printsignal(*psig);
        psig = mapper_db_signal_next(psig);
    }

    if (count != 2) {
        eprintf("Expected 2 records, but counted %d.\n", count);
        result = 1;
        goto done;
    }

    /*********/

    eprintf("\nFind all inputs for device 'testdb__xx.2':\n");

    psig = mapper_db_get_device_inputs(db, mapper_db_get_device_by_name(db, "testdb__xx.2"));

    count=0;
    if (psig) {
        eprintf("mapper_db_get_device_inputs() incorrectly found something.\n");
        printsignal(*psig);
        result = 1;
        goto done;
    }
    else
        eprintf("  correctly returned 0.\n");

    /*********/

    eprintf("\nFind all outputs for device 'testdb__.2':\n");

    psig = mapper_db_get_device_outputs(db, mapper_db_get_device_by_name(db, "testdb__.2"));

    count=0;
    if (!psig) {
        eprintf("mapper_db_get_device_outputs() returned 0.\n");
        result = 1;
        goto done;
    }
    if (!*psig) {
        eprintf("mapper_db_get_device_outputs() returned something "
                "which pointed to 0.\n");
        result = 1;
        goto done;
    }

    while (psig) {
        count ++;
        printsignal(*psig);
        psig = mapper_db_signal_next(psig);
    }

    if (count != 1) {
        eprintf("Expected 1 records, but counted %d.\n", count);
        result = 1;
        goto done;
    }

    /*********/

    eprintf("\nFind matching input 'in' for device 'testdb.1':\n");

    psig = mapper_db_get_device_inputs_by_name_match(db, mapper_db_get_device_by_name(db, "testdb.1"), "in");

    count=0;
    if (!psig) {
        eprintf("mapper_db_get_device_inputs_by_name_match() returned 0.\n");
        result = 1;
        goto done;
    }
    if (!*psig) {
        eprintf("mapper_db_get_device_inputs_by_name_match() returned something "
                "which pointed to 0.\n");
        result = 1;
        goto done;
    }

    while (psig) {
        count ++;
        printsignal(*psig);
        psig = mapper_db_signal_next(psig);
    }

    if (count != 2) {
        eprintf("Expected 2 records, but counted %d.\n", count);
        result = 1;
        goto done;
    }

    /*********/

    eprintf("\nFind matching output 'out' for device 'testdb.1':\n");

    psig = mapper_db_get_device_outputs_by_name_match(db, mapper_db_get_device_by_name(db, "testdb.1"), "out");

    count=0;
    if (!psig) {
        eprintf("mapper_db_get_device_outputs_by_name_match() returned 0.\n");
        result = 1;
        goto done;
    }
    if (!*psig) {
        eprintf("mapper_db_get_device_outputs_by_name_match() returned something "
                "which pointed to 0.\n");
        result = 1;
        goto done;
    }

    while (psig) {
        count ++;
        printsignal(*psig);
        psig = mapper_db_signal_next(psig);
    }

    if (count != 2) {
        eprintf("Expected 2 records, but counted %d.\n", count);
        result = 1;
        goto done;
    }

    /*********/

    eprintf("\nFind matching output 'out' for device 'testdb__.2':\n");

    psig = mapper_db_get_device_outputs_by_name_match(db, mapper_db_get_device_by_name(db, "testdb__.2"), "out");

    count=0;
    if (!psig) {
        eprintf("mapper_db_get_device_outputs_by_name_match() returned 0.\n");
        result = 1;
        goto done;
    }
    if (!*psig) {
        eprintf("mapper_db_get_device_outputs_by_name_match() returned something "
                "which pointed to 0.\n");
        result = 1;
        goto done;
    }

    while (psig) {
        count ++;
        printsignal(*psig);
        psig = mapper_db_signal_next(psig);
    }

    if (count != 1) {
        eprintf("Expected 1 records, but counted %d.\n", count);
        result = 1;
        goto done;
    }

    /*********/

    eprintf("\n--- maps ---\n");

    eprintf("\nFind maps with source 'out1':\n");

    psig = mapper_db_get_signals_by_name(db, "out1");
    mapper_db_map* pmap = 0;
    while (psig) {
        mapper_db_map *temp = mapper_db_get_signal_outgoing_maps(db, *psig);
        pmap = mapper_db_map_query_union(db, pmap, temp);
        psig = mapper_db_signal_next(psig);
    }

    count=0;
    if (!pmap) {
        eprintf("mapper_db_get_signal_outgoing_maps() returned 0.\n");
        result = 1;
        goto done;
    }
    if (!*pmap) {
        eprintf("mapper_db_get_signal_outgoing_maps() returned something "
                "which pointed to 0.\n");
        result = 1;
        goto done;
    }

    while (pmap) {
        count ++;
        printmap(*pmap);
        pmap = mapper_db_map_next(pmap);
    }

    if (count != 4) {
        eprintf("Expected 4 records, but counted %d.\n", count);
        result = 1;
        goto done;
    }

    /*********/

    eprintf("\nFind maps for device 'testdb.1', source 'out1':\n");

    dev = mapper_db_get_device_by_name(db, "testdb.1");
    mapper_db_signal sig = mapper_db_get_device_signal_by_name(db, dev, "out1");
    pmap = mapper_db_get_signal_maps(db, sig);

    count=0;
    if (!pmap) {
        eprintf("mapper_db_get_signal_maps() returned 0.\n");
        result = 1;
        goto done;
    }
    if (!*pmap) {
        eprintf("mapper_db_get_signal_maps() "
                "returned something which pointed to 0.\n");
        result = 1;
        goto done;
    }

    while (pmap) {
        count ++;
        printmap(*pmap);
        pmap = mapper_db_map_next(pmap);
    }

    if (count != 2) {
        eprintf("Expected 2 records, but counted %d.\n", count);
        result = 1;
        goto done;
    }

    /*********/

    eprintf("\nFind maps with destination 'in2':\n");

    psig = mapper_db_get_signals_by_name(db, "in2");
    pmap = 0;
    while (psig) {
        mapper_db_map *temp = mapper_db_get_signal_incoming_maps(db, *psig);
        pmap = mapper_db_map_query_union(db, pmap, temp);
        psig = mapper_db_signal_next(psig);
    }

    count=0;
    if (!pmap) {
        eprintf("mapper_db_get_signal_incoming_maps() returned 0.\n");
        result = 1;
        goto done;
    }
    if (!*pmap) {
        eprintf("mapper_db_get_signal_incoming_maps() returned something "
                "which pointed to 0.\n");
        result = 1;
        goto done;
    }

    while (pmap) {
        count ++;
        printmap(*pmap);
        pmap = mapper_db_map_next(pmap);
    }

    if (count != 2) {
        eprintf("Expected 2 records, but counted %d.\n", count);
        result = 1;
        goto done;
    }

    /*********/

    eprintf("\nFind maps for device 'testdb__.2', destination 'in1':\n");

    dev = mapper_db_get_device_by_name(db, "testdb__.2");
    sig = mapper_db_get_device_signal_by_name(db, dev, "in1");
    pmap = mapper_db_get_signal_incoming_maps(db, sig);

    count=0;
    if (!pmap) {
        eprintf("mapper_db_get_signal_incoming_maps() returned 0.\n");
        result = 1;
        goto done;
    }
    if (!*pmap) {
        eprintf("mapper_db_get_signal_incoming_maps() "
                "returned something which pointed to 0.\n");
        result = 1;
        goto done;
    }

    while (pmap) {
        count ++;
        printmap(*pmap);
        pmap = mapper_db_map_next(pmap);
    }

    if (count != 2) {
        eprintf("Expected 2 records, but counted %d.\n", count);
        result = 1;
        goto done;
    }

    /*********/

    eprintf("\nFind maps for source device 'testdb__.2', signal 'out1'"
            "\n          AND dest device 'testdb.1', signal 'in1':\n");

    // get source signal
    dev = mapper_db_get_device_by_name(db, "testdb__.2");
    mapper_db_signal src_sig = mapper_db_get_device_signal_by_name(db, dev, "out1");

    // get destination signal
    dev = mapper_db_get_device_by_name(db, "testdb.1");
    mapper_db_signal dst_sig = mapper_db_get_device_signal_by_name(db, dev, "in1");

    // get maps
    pmap = mapper_db_map_query_intersection(db,
                                            mapper_db_get_signal_outgoing_maps(db, src_sig),
                                            mapper_db_get_signal_incoming_maps(db, dst_sig));

    count=0;
    if (!pmap) {
        eprintf("mapper_db_get_maps_by_device_and_signal_names() returned 0.\n");
        result = 1;
        goto done;
    }
    if (!*pmap) {
        eprintf("mapper_db_get_maps_by_device_and_signal_names() "
                "returned something which pointed to 0.\n");
        result = 1;
        goto done;
    }

    while (pmap) {
        count ++;
        printmap(*pmap);
        pmap = mapper_db_map_next(pmap);
    }

    if (count != 1) {
        eprintf("Expected 1 records, but counted %d.\n", count);
        result = 1;
        goto done;
    }

    /*********/

    eprintf("\nFind maps for source device 'testdb__.2', signals matching 'out',"
            "\n          AND dest device 'testdb.1', all signals:\n");

    mapper_db_map *src_pmap = 0, *dst_pmap = 0;

    // build combined source query
    dev = mapper_db_get_device_by_name(db, "testdb__.2");
    psig = mapper_db_get_device_signals_by_name_match(db, dev, "out");
    while (psig) {
        mapper_db_map *temp = mapper_db_get_signal_outgoing_maps(db, *psig);
        src_pmap = mapper_db_map_query_union(db, src_pmap, temp);
        psig = mapper_db_signal_next(psig);
    }

    // build combined destination query
    dev = mapper_db_get_device_by_name(db, "testdb.1");
    psig = mapper_db_get_device_signals(db, dev);
    while (psig) {
        mapper_db_map *temp = mapper_db_get_signal_incoming_maps(db, *psig);
        dst_pmap = mapper_db_map_query_union(db, dst_pmap, temp);
        psig = mapper_db_signal_next(psig);
    }

    // combine source and destination queries
    pmap = mapper_db_map_query_intersection(db, src_pmap, dst_pmap);

    count=0;
    if (!pmap) {
        eprintf("mapper_db_get_maps_by_signal_queries() returned 0.\n");
        result = 1;
        goto done;
    }
    if (!*pmap) {
        eprintf("mapper_db_get_maps_by_signal_queries() "
                "returned something which pointed to 0.\n");
        result = 1;
        goto done;
    }

    while (pmap) {
        count ++;
        printmap(*pmap);
        pmap = mapper_db_map_next(pmap);
    }

    if (count != 2) {
        eprintf("Expected 2 records, but counted %d.\n", count);
        result = 1;
        goto done;
    }

    /*********/
done:
    while (db->registered_devices)
        mapper_db_remove_device(db, db->registered_devices, 1);
    if (!verbose)
        printf("..................................................");
    printf("Test %s.\n", result ? "FAILED" : "PASSED");
    return result;
}
