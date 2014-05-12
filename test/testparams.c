
#include <stdio.h>
#include <string.h>
#include <lo/lo_lowlevel.h>
#include "../src/types_internal.h"
#include "../src/mapper_internal.h"

int verbose = 1;

#define eprintf(format, ...) do {               \
    if (verbose)                                \
        fprintf(stdout, format, ##__VA_ARGS__); \
} while(0)

int main(int argc, char **argv)
{
    lo_arg *args[20], **a;
    mapper_message_t msg;
    int port=1234, src_length=4;
    float r[4] = {1.0, 2.0, -15.0, 25.0};
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

    eprintf("1: expected success\n");

    args[0]  = (lo_arg*)"@IP";
    args[1]  = (lo_arg*)"127.0.0.1";
    args[2]  = (lo_arg*)"@srcMin";
    args[3]  = (lo_arg*)&r[0];
    args[4]  = (lo_arg*)&r[1];
    args[5]  = (lo_arg*)&r[2];
    args[6]  = (lo_arg*)&r[3];
    args[7]  = (lo_arg*)"@port";
    args[8]  = (lo_arg*)&port;
    args[9]  = (lo_arg*)"@srcType";
    args[10] = (lo_arg*)"f";
    args[11] = (lo_arg*)"@srcLength";
    args[12] = (lo_arg*)&src_length;

    int rc = mapper_msg_parse_params(&msg, "/test", "sssffffsiscsi", 13, args);
    if (rc) {
        eprintf("1: Error parsing.\n");
        result = 1;
        goto done;
    }

    a = mapper_msg_get_param(&msg, AT_IP);
    if (!a) {
        eprintf("1: Could not get @IP param.\n");
        result = 1;
        goto done;
    }
    if (strcmp(&(*a)->s, "127.0.0.1")!=0)
        result |= 1;
    eprintf("1: @IP = \"%s\" %s\n", &(*a)->s, result ? "WRONG" : "(correct)");
    if (result)
        goto done;

    a = mapper_msg_get_param(&msg, AT_PORT);
    if (!a) {
        eprintf("1: Could not get @port param.\n");
        result = 1;
        goto done;
    }
    if ((*a)->i!=1234)
        result |= 1;
    eprintf("1: @port = %d %s\n", (*a)->i, result ? "WRONG" : "(correct)");
    if (result)
        goto done;

    a = mapper_msg_get_param(&msg, AT_SRC_MIN);
    int count = mapper_msg_get_length(&msg, AT_SRC_MIN);
    if (!a) {
        eprintf("1: Could not get @src_min param.\n");
        result = 1;
        goto done;
    }
    if (count != 4) {
        eprintf("1: Wrong count returned for @scr_min param.\n");
    }
    for (i=0; i<count; i++) {
        if (a[i]->f!=r[i])
            result = 1;
        eprintf("1: @src_min[%d] = %f %s\n", i, a[i]->f,
                result ? "WRONG" : "(correct)");
        if (result)
            goto done;
    }

    /*****/

    eprintf("2: deliberately malformed message\n");

    args[0] = (lo_arg*)"@port";
    args[1] = (lo_arg*)&port;
    args[2] = (lo_arg*)"@IP";

    rc = mapper_msg_parse_params(&msg, "/test", "sis", 3, args);
    if (rc) {
        eprintf("2: Error parsing.\n");
        result = 1;
        goto done;
    }

    a = mapper_msg_get_param(&msg, AT_PORT);
    if (!a) {
        eprintf("2: Could not get @port param.\n");
        result = 1;
        goto done;
    }

    a = mapper_msg_get_param(&msg, AT_IP);
    if (a) {
        eprintf("2: Error, should not have been able to retrieve @IP param.\n");
        result = 1;
        goto done;
    }

    /*****/
done:
    if (!verbose)
        printf("..................................................");
    printf("Test %s.\n", result ? "FAILED" : "PASSED");
    return result;
}
