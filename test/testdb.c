
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

void printsignal(mapper_db_signal sig)
{
    int i;
    eprintf("  name=%s%s, type=%c, length=%d",
            sig->device_name, sig->name, sig->type, sig->length);
    if (sig->unit)
        eprintf(", unit=%s", sig->unit);
    if (sig->minimum) {
        if (sig->type == 'i') {
            int *vals = (int*)sig->minimum;
            for (i = 0; i < sig->length; i++)
                eprintf(", minimum=%d", vals[i]);
        }
        else if (sig->type == 'f') {
            float *vals = (float*)sig->minimum;
            for (i = 0; i < sig->length; i++)
                eprintf(", minimum=%g", vals[i]);
        }
        else if (sig->type == 'd') {
            double *vals = (double*)sig->minimum;
            for (i = 0; i < sig->length; i++)
                eprintf(", minimum=%g", vals[i]);
        }
    }
    if (sig->maximum) {
        if (sig->type == 'i') {
            int *vals = (int*)sig->maximum;
            for (i = 0; i < sig->length; i++)
                eprintf(", maximum=%d", vals[i]);
        }
        else if (sig->type == 'f') {
            float *vals = (float*)sig->maximum;
            for (i = 0; i < sig->length; i++)
                eprintf(", maximum=%g", vals[i]);
        }
        else if (sig->type == 'd') {
            double *vals = (double*)sig->maximum;
            for (i = 0; i < sig->length; i++)
                eprintf(", minimum=%g", vals[i]);
        }
    }
    eprintf("\n");
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
    int one=1, two=2;
    float zerof=0.;
    mapper_db_t db_t, *db = &db_t;
    memset(db, 0, sizeof(db_t));

    /* Test the database functions */

    args[0] = (lo_arg*)"@port";
    args[1] = (lo_arg*)&port;
    args[2] = (lo_arg*)"@IP";
    args[3] = (lo_arg*)"localhost";

    if (mapper_msg_parse_params(&msg, "/registered", "siss", 4, args))
    {
        eprintf("1: Error, parsing failed.\n");
        result = 1;
        goto done;
    }

    mapper_db_add_or_update_device_params(db, "/testdb.1", &msg, 0);
    mapper_db_add_or_update_device_params(db, "/testdb__.2", &msg, 0);
    mapper_db_add_or_update_device_params(db, "/testdb.3", &msg, 0);
    mapper_db_add_or_update_device_params(db, "/testdb__.4", &msg, 0);

    args[0] = (lo_arg*)"@direction";
    args[1] = (lo_arg*)"input";
    args[2] = (lo_arg*)"@type";
    args[3] = (lo_arg*)"f";
    args[4] = (lo_arg*)"@IP";
    args[5] = (lo_arg*)"localhost";

    if (mapper_msg_parse_params(&msg, "/testdb.1/signal",
                                "sc", 2, args))
    {
        eprintf("2: Error, parsing failed.\n");
        result = 1;
        goto done;
    }

    mapper_db_add_or_update_signal_params(db, "/in1", "/testdb.1", &msg);
    mapper_db_add_or_update_signal_params(db, "/in2", "/testdb.1", &msg);
    mapper_db_add_or_update_signal_params(db, "/in2", "/testdb.1", &msg);
    
    args[1] = (lo_arg*)"output";
    
    if (mapper_msg_parse_params(&msg, "/testdb.1/signal",
                                "sc", 2, args))
    {
        eprintf("2: Error, parsing failed.\n");
        result = 1;
        goto done;
    }

    mapper_db_add_or_update_signal_params(db, "/out1", "/testdb.1", &msg);
    mapper_db_add_or_update_signal_params(db, "/out2", "/testdb.1", &msg);
    mapper_db_add_or_update_signal_params(db, "/out1", "/testdb__.2", &msg);

    args[0] = (lo_arg*)"@mode";
    args[1] = (lo_arg*)"bypass";
    args[2] = (lo_arg*)"@boundMin";
    args[3] = (lo_arg*)"none";

    if (mapper_msg_parse_params(&msg, "/connected",
                                "ssss", 4, args))
    {
        eprintf("4: Error, parsing failed.\n");
        result = 1;
        goto done;
    }

    mapper_db_add_or_update_connection_params(db, "/testdb.1/out2",
                                              "/testdb__.2/in1", &msg);
    mapper_db_add_or_update_connection_params(db, "/testdb__.2/out1",
                                              "/testdb.1/in1", &msg);

    args[0] = (lo_arg*)"@mode";
    args[1] = (lo_arg*)"expression";
    args[2] = (lo_arg*)"@expression";
    args[3] = (lo_arg*)"(x-10)*80";
    args[4] = (lo_arg*)"@boundMin";
    args[5] = (lo_arg*)"clamp";
    args[6] = (lo_arg*)"@srcLength";
    args[7] = (lo_arg*)&two;
    args[8] = (lo_arg*)"@srcType";
    args[9] = (lo_arg*)"f";
    args[10] = (lo_arg*)"@srcMin";
    args[11] = (lo_arg*)&zerof;
    args[12] = (lo_arg*)&one;
    args[13] = (lo_arg*)"@srcMax";
    args[14] = (lo_arg*)&one;
    args[15] = (lo_arg*)&two;

    if (mapper_msg_parse_params(&msg, "/connected",
                                "sssssssisssfisii", 16, args))
    {
        eprintf("5: Error, parsing failed.\n");
        result = 1;
        goto done;
    }

    mapper_db_add_or_update_connection_params(db, "/testdb.1/out1",
                                              "/testdb__.2/in2", &msg);
    mapper_db_add_or_update_connection_params(db, "/testdb.1/out1",
                                              "/testdb__.2/in1", &msg);
    mapper_db_add_or_update_connection_params(db, "/testdb__.2/out2",
                                              "/testdb.1/in2", &msg);

    if (mapper_msg_parse_params(&msg, "/linked",
                                "", 0, args))
    {
        eprintf("6: Error, parsing failed (on no args!)\n");
        result = 1;
        goto done;
    }

    mapper_db_add_or_update_link_params(db, "/testdb.1", "/testdb__.2", &msg);
    mapper_db_add_or_update_link_params(db, "/testdb__.2", "/testdb.3", &msg);
    mapper_db_add_or_update_link_params(db, "/testdb__.2", "/testdb.3", &msg);
    mapper_db_add_or_update_link_params(db, "/testdb.3", "/testdb.1", &msg);
    mapper_db_add_or_update_link_params(db, "/testdb__.2", "/testdb__.4", &msg);

    /*********/

    if (verbose) {
        eprintf("Dump:\n");
        mapper_db_dump(db);
    }

    /*********/

    eprintf("\n--- Devices ---\n");

    eprintf("\nWalk the whole database:\n");
    mapper_db_device *pdev = mapper_db_get_all_devices(db);
    int count=0;
    if (!pdev) {
        eprintf("mapper_db_get_all_devices() returned 0.\n");
        result = 1;
        goto done;
    }
    if (!*pdev) {
        eprintf("mapper_db_get_all_devices() returned something "
               "which pointed to 0.\n");
        result = 1;
        goto done;
    }

    while (pdev) {
        count ++;
        eprintf("  name=%s, host=%s, port=%d\n",
               (*pdev)->name, (*pdev)->host, (*pdev)->port);
        pdev = mapper_db_device_next(pdev);
    }

    if (count != 4) {
        eprintf("Expected 4 records, but counted %d.\n", count);
        result = 1;
        goto done;
    }

    /*********/

    eprintf("\nFind /testdb.3:\n");

    mapper_db_device dev = mapper_db_get_device_by_name(db, "/testdb.3");
    if (!dev) {
        eprintf("Not found.\n");
        result = 1;
        goto done;
    }

    eprintf("  name=%s, host=%s, port=%d\n",
            dev->name, dev->host, dev->port);

    /*********/

    eprintf("\nFind /dummy:\n");

    dev = mapper_db_get_device_by_name(db, "/dummy");
    if (dev) {
        eprintf("unexpected found /dummy: %p\n", dev);
        result = 1;
        goto done;
    }
    eprintf("  not found, good.\n");

    /*********/

    eprintf("\nFind matching '__':\n");

    pdev = mapper_db_match_devices_by_name(db, "__");

    count=0;
    if (!pdev) {
        eprintf("mapper_db_match_device_by_name() returned 0.\n");
        result = 1;
        goto done;
    }
    if (!*pdev) {
        eprintf("mapper_db_match_device_by_name() returned something "
                "which pointed to 0.\n");
        result = 1;
        goto done;
    }

    while (pdev) {
        count ++;
        eprintf("  name=%s, host=%s, port=%d\n",
                (*pdev)->name, (*pdev)->host, (*pdev)->port);
        pdev = mapper_db_device_next(pdev);
    }

    if (count != 2) {
        eprintf("Expected 2 records, but counted %d.\n", count);
        result = 1;
        goto done;
    }

    /*********/

    eprintf("\n--- Signals ---\n");

    eprintf("\nFind all inputs for device '/testdb.1':\n");

    mapper_db_signal *psig =
        mapper_db_get_inputs_by_device_name(db, "/testdb.1");

    count=0;
    if (!psig) {
        eprintf("mapper_db_get_inputs_by_device_name() returned 0.\n");
        result = 1;
        goto done;
    }
    if (!*psig) {
        eprintf("mapper_db_get_inputs_by_device_name() returned something "
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

    eprintf("\nFind all outputs for device '/testdb.1':\n");

    psig = mapper_db_get_outputs_by_device_name(db, "/testdb.1");

    count=0;
    if (!psig) {
        eprintf("mapper_db_get_outputs_by_device_name() returned 0.\n");
        result = 1;
        goto done;
    }
    if (!*psig) {
        eprintf("mapper_db_get_outputs_by_device_name() returned something "
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

    eprintf("\nFind all inputs for device '/testdb__.2':\n");

    psig = mapper_db_get_inputs_by_device_name(db, "/testdb__.2");

    count=0;
    if (psig) {
        eprintf("mapper_db_get_inputs_by_device_name() "
                "incorrectly found something.\n");
        printsignal(*psig);
        result = 1;
        goto done;
    }
    else
        eprintf("  correctly returned 0.\n");

    /*********/

    eprintf("\nFind all outputs for device '/testdb__.2':\n");

    psig = mapper_db_get_outputs_by_device_name(db, "/testdb__.2");

    count=0;
    if (!psig) {
        eprintf("mapper_db_get_outputs_by_device_name() returned 0.\n");
        result = 1;
        goto done;
    }
    if (!*psig) {
        eprintf("mapper_db_get_outputs_by_device_name() returned something "
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
        eprintf("Expected 1 record, but counted %d.\n", count);
        result = 1;
        goto done;
    }

    /*********/

    eprintf("\nFind matching input 'in' for device '/testdb.1':\n");

    psig = mapper_db_match_inputs_by_device_name(db, "/testdb.1", "in");

    count=0;
    if (!psig) {
        eprintf("mapper_db_match_inputs_by_device_name() returned 0.\n");
        result = 1;
        goto done;
    }
    if (!*psig) {
        eprintf("mapper_db_match_inputs_by_device_name() returned something "
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

    eprintf("\nFind matching output 'out' for device '/testdb.1':\n");

    psig = mapper_db_match_outputs_by_device_name(db, "/testdb.1", "out");

    count=0;
    if (!psig) {
        eprintf("mapper_db_match_outputs_by_device_name() returned 0.\n");
        result = 1;
        goto done;
    }
    if (!*psig) {
        eprintf("mapper_db_match_outputs_by_device_name() returned something "
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

    eprintf("\nFind matching output 'out' for device '/testdb__.2':\n");

    psig = mapper_db_match_outputs_by_device_name(db, "/testdb__.2", "out");

    count=0;
    if (!psig) {
        eprintf("mapper_db_match_outputs_by_device_name() returned 0.\n");
        result = 1;
        goto done;
    }
    if (!*psig) {
        eprintf("mapper_db_match_outputs_by_device_name() returned something "
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
        eprintf("Expected 1 record, but counted %d.\n", count);
        result = 1;
        goto done;
    }

    /*********/

    eprintf("\n--- connections ---\n");

    eprintf("\nFind connections with source 'out1':\n");

    mapper_db_connection* pcon =
        mapper_db_get_connections_by_src_signal_name(db, "out1");

    count=0;
    if (!pcon) {
        eprintf("mapper_db_get_connections_by_src_signal_name() returned 0.\n");
        result = 1;
        goto done;
    }
    if (!*pcon) {
        eprintf("mapper_db_get_connections_by_src_signal_name() returned something "
                "which pointed to 0.\n");
        result = 1;
        goto done;
    }

    while (pcon) {
        count ++;
        eprintf("  source=%s, dest=%s\n",
                (*pcon)->src_name, (*pcon)->dest_name);
        pcon = mapper_db_connection_next(pcon);
    }

    if (count != 3) {
        eprintf("Expected 3 records, but counted %d.\n", count);
        result = 1;
        goto done;
    }

    /*********/

    eprintf("\nFind connections for device 'testdb.1', "
            "source 'out1':\n");

    pcon = mapper_db_get_connections_by_src_device_and_signal_names(db, "testdb.1",
                                                                    "/out1");

    count=0;
    if (!pcon) {
        eprintf("mapper_db_get_connections_by_src_device_and_signal_names() "
                "returned 0.\n");
        result = 1;
        goto done;
    }
    if (!*pcon) {
        eprintf("mapper_db_get_connections_by_src_device_and_signal_names() "
                "returned something which pointed to 0.\n");
        result = 1;
        goto done;
    }

    while (pcon) {
        count ++;
        eprintf("  source=%s, dest=%s\n",
                (*pcon)->src_name, (*pcon)->dest_name);
        pcon = mapper_db_connection_next(pcon);
    }

    if (count != 2) {
        eprintf("Expected 2 records, but counted %d.\n", count);
        result = 1;
        goto done;
    }

    /*********/

    eprintf("\nFind connections with destination 'in2':\n");

    pcon = mapper_db_get_connections_by_dest_signal_name(db, "in2");

    count=0;
    if (!pcon) {
        eprintf("mapper_db_get_connections_by_dest_signal_name() returned 0.\n");
        result = 1;
        goto done;
    }
    if (!*pcon) {
        eprintf("mapper_db_get_connections_by_dest_signal_name() returned something "
                "which pointed to 0.\n");
        result = 1;
        goto done;
    }

    while (pcon) {
        count ++;
        eprintf("  source=%s, dest=%s\n",
                (*pcon)->src_name, (*pcon)->dest_name);
        pcon = mapper_db_connection_next(pcon);
    }

    if (count != 2) {
        eprintf("Expected 2 records, but counted %d.\n", count);
        result = 1;
        goto done;
    }

    /*********/

    eprintf("\nFind connections for device 'testdb__.2', "
            "destination 'in1':\n");

    pcon = mapper_db_get_connections_by_dest_device_and_signal_names(db,
                                                                     "testdb__.2",
                                                                     "/in1");

    count=0;
    if (!pcon) {
        eprintf("mapper_db_get_connections_by_dest_device_and_signal_names() "
                "returned 0.\n");
        result = 1;
        goto done;
    }
    if (!*pcon) {
        eprintf("mapper_db_get_connections_by_dest_device_and_signal_names() "
                "returned something which pointed to 0.\n");
        result = 1;
        goto done;
    }

    while (pcon) {
        count ++;
        eprintf("  source=%s, dest=%s\n",
                (*pcon)->src_name, (*pcon)->dest_name);
        pcon = mapper_db_connection_next(pcon);
    }

    if (count != 2) {
        eprintf("Expected 2 records, but counted %d.\n", count);
        result = 1;
        goto done;
    }

    /*********/

    eprintf("\nFind connections for input device 'testdb__.2', signal 'out1',"
            "\n                 and output device 'testdb.1', signal 'in1':\n");

    pcon = mapper_db_get_connections_by_device_and_signal_names(
        db, "testdb__.2", "out1", "testdb.1", "in1");

    count=0;
    if (!pcon) {
        eprintf("mapper_db_get_connections_by_device_and_signal_names() "
                "returned 0.\n");
        result = 1;
        goto done;
    }
    if (!*pcon) {
        eprintf("mapper_db_get_connections_by_device_and_signal_names() "
                "returned something which pointed to 0.\n");
        result = 1;
        goto done;
    }

    while (pcon) {
        count ++;
        eprintf("  source=%s, dest=%s\n",
                (*pcon)->src_name, (*pcon)->dest_name);
        pcon = mapper_db_connection_next(pcon);
    }

    if (count != 1) {
        eprintf("Expected 1 records, but counted %d.\n", count);
        result = 1;
        goto done;
    }

    /*********/

    eprintf("\nFind connections for input device 'testdb__.2', signals "
            "matching 'out',"
            "\n                 and output device 'testdb.1', all signals:\n");

    pcon = mapper_db_get_connections_by_signal_queries(db,
        mapper_db_match_outputs_by_device_name(db, "/testdb__.2", "out"),
        mapper_db_get_inputs_by_device_name(db, "/testdb.1"));

    count=0;
    if (!pcon) {
        eprintf("mapper_db_get_connections_by_signal_queries() "
                "returned 0.\n");
        result = 1;
        goto done;
    }
    if (!*pcon) {
        eprintf("mapper_db_get_connections_by_signal_queries() "
                "returned something which pointed to 0.\n");
        result = 1;
        goto done;
    }

    while (pcon) {
        count ++;
        eprintf("  source=%s, dest=%s\n",
                (*pcon)->src_name, (*pcon)->dest_name);
        pcon = mapper_db_connection_next(pcon);
    }

    if (count != 1) {
        eprintf("Expected 1 records, but counted %d.\n", count);
        result = 1;
        goto done;
    }

    /*********/

    eprintf("\n--- Links ---\n");

    eprintf("\nFind matching links with source '/testdb__.2':\n");

    mapper_db_link* plink =
        mapper_db_get_links_by_src_device_name(db, "/testdb__.2");

    count=0;
    if (!plink) {
        eprintf("mapper_db_get_links_by_src_device_name() returned 0.\n");
        result = 1;
        goto done;
    }
    if (!*plink) {
        eprintf("mapper_db_get_links_by_src_device_name() returned something "
                "which pointed to 0.\n");
        result = 1;
        goto done;
    }

    while (plink) {
        count ++;
        eprintf("  source=%s, dest=%s\n",
                (*plink)->src_name, (*plink)->dest_name);
        plink = mapper_db_link_next(plink);
    }

    if (count != 2) {
        eprintf("Expected 2 records, but counted %d.\n", count);
        result = 1;
        goto done;
    }

    /*********/

    eprintf("\nFind matching links with destination '/testdb__.4':\n");

    plink = mapper_db_get_links_by_dest_device_name(db, "/testdb__.4");

    count=0;
    if (!plink) {
        eprintf("mapper_db_get_links_by_dest_device_name() returned 0.\n");
        result = 1;
        goto done;
    }
    if (!*plink) {
        eprintf("mapper_db_get_links_by_dest_device_name() returned something "
                "which pointed to 0.\n");
        result = 1;
        goto done;
    }

    while (plink) {
        count ++;
        eprintf("  source=%s, dest=%s\n",
                (*plink)->src_name, (*plink)->dest_name);
        plink = mapper_db_link_next(plink);
    }

    if (count != 1) {
        eprintf("Expected 1 record, but counted %d.\n", count);
        result = 1;
        goto done;
    }

    /*********/

    eprintf("\nFind links with source matching 'db' and "
            "destination matching '__':\n");

    pdev = mapper_db_match_devices_by_name(db, "db");

    if (!pdev) {
        eprintf("mapper_db_match_device_by_name() returned 0.\n");
        result = 1;
        goto done;
    }

    mapper_db_device_t **pdev2 = mapper_db_match_devices_by_name(db, "__");

    if (!pdev2) {
        eprintf("mapper_db_match_device_by_name() returned 0.\n");
        result = 1;
        goto done;
    }

    plink = mapper_db_get_links_by_src_dest_devices(db, pdev, pdev2);

    count=0;
    if (!plink) {
        eprintf("mapper_db_get_links_by_src_dest_devices() returned 0.\n");
        result = 1;
        goto done;
    }
    if (!*plink) {
        eprintf("mapper_db_get_links_by_src_dest_devices() "
                "returned something which pointed to 0.\n");
        result = 1;
        goto done;
    }

    while (plink) {
        count ++;
        eprintf("  source=%s, dest=%s\n",
                (*plink)->src_name, (*plink)->dest_name);
        plink = mapper_db_link_next(plink);
    }

    if (count != 2) {
        eprintf("Expected 2 records, but counted %d.\n", count);
        result = 1;
        goto done;
    }

    /*********/

    eprintf("\nFind any links with source matching '2':\n");

    pdev = mapper_db_match_devices_by_name(db, "2");

    if (!pdev) {
        eprintf("mapper_db_match_device_by_name() returned 0.\n");
        result = 1;
        goto done;
    }

    pdev2 = mapper_db_get_all_devices(db);

    if (!pdev2) {
        eprintf("mapper_db_get_all_devices() returned 0.\n");
        result = 1;
        goto done;
    }

    plink = mapper_db_get_links_by_src_dest_devices(db, pdev, pdev2);

    count=0;
    if (!plink) {
        eprintf("mapper_db_get_links_by_src_dest_devices() returned 0.\n");
        result = 1;
        goto done;
    }
    if (!*plink) {
        eprintf("mapper_db_get_links_by_src_dest_devices() "
                "returned something which pointed to 0.\n");
        result = 1;
        goto done;
    }

    while (plink) {
        count ++;
        eprintf("  source=%s, dest=%s\n",
                (*plink)->src_name, (*plink)->dest_name);
        plink = mapper_db_link_next(plink);
    }

    if (count != 2) {
        eprintf("Expected 2 records, but counted %d.\n", count);
        result = 1;
        goto done;
    }

    /*********/
done:
    while (db->registered_devices)
        mapper_db_remove_device_by_name(db, db->registered_devices->name);
    if (!verbose)
        printf("..................................................");
    printf("Test %s.\n", result ? "FAILED" : "PASSED");
    return result;
}
