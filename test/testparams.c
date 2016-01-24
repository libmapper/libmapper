
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
    lo_arg *args[20];
    mapper_message msg;
    mapper_message_atom atom;
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
                        eprintf("testparams.c: possible arguments "
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

    args[0]  = (lo_arg*)"@host";
    args[1]  = (lo_arg*)"127.0.0.1";
    args[2]  = (lo_arg*)"@src@min";
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

    msg = mapper_message_parse_properties(13, "sssffffsiscsi", args);
    if (!msg) {
        eprintf("1: Error parsing.\n");
        result = 1;
        goto done;
    }

    atom = mapper_message_property(msg, AT_HOST);
    if (!atom) {
        eprintf("1: Could not get @host property.\n");
        result = 1;
        goto done;
    }
    if (!is_string_type(atom->types[0])) {
        eprintf("1: Type error retrieving @host property.");
        result = 1;
        goto done;
    }
    if (atom->length != 1) {
        eprintf("1: Length error retrieving @host property.");
        result = 1;
        goto done;
    }
    if (strcmp(&(*atom->values)->s, "127.0.0.1")!=0)
        result |= 1;
    eprintf("1: @host = \"%s\" %s\n", &(*atom->values)->s,
            result ? "WRONG" : "(correct)");
    if (result)
        goto done;

    atom = mapper_message_property(msg, AT_PORT);
    if (!atom) {
        eprintf("1: Could not get @port property.\n");
        result = 1;
        goto done;
    }
    if (atom->types[0] != 'i') {
        eprintf("1: Type error retrieving @port property.");
        result = 1;
        goto done;
    }
    if (atom->length != 1) {
        eprintf("1: Length error retrieving @port property.");
        result = 1;
        goto done;
    }
    if ((*atom->values)->i!=1234)
        result |= 1;
    eprintf("1: @port = %d %s\n", (*atom->values)->i,
            result ? "WRONG" : "(correct)");
    if (result)
        goto done;

    atom = mapper_message_property(msg, SRC_SLOT_PROPERTY(0) | AT_MIN);
    if (!atom) {
        eprintf("1: Could not get @src@min property.\n");
        result = 1;
        goto done;
    }
    if (atom->types[0] != 'f') {
        eprintf("1: Type error retrieving @src@min property.");
        result = 1;
        goto done;
    }
    if (atom->length != 4) {
        eprintf("1: Length error retrieving @src@min property.");
        result = 1;
        goto done;
    }
    for (i = 0; i < atom->length; i++) {
        if (atom->values[i]->f != r[i])
            result = 1;
        eprintf("1: @src@min[%d] = %f %s\n", i, atom->values[i]->f,
                result ? "WRONG" : "(correct)");
        if (result)
            goto done;
    }

    /*****/

    eprintf("2: deliberately malformed message\n");

    args[0] = (lo_arg*)"@port";
    args[1] = (lo_arg*)&port;
    args[2] = (lo_arg*)"@host";

    mapper_message_free(msg);
    msg = mapper_message_parse_properties(3, "sis", args);
    if (!msg) {
        eprintf("2: Error parsing.\n");
        result = 1;
        goto done;
    }

    atom = mapper_message_property(msg, AT_PORT);
    if (!atom) {
        eprintf("2: Could not get @port property.\n");
        result = 1;
        goto done;
    }
    if (atom->types[0] != 'i') {
        eprintf("2: Type error retrieving @port property.");
        result = 1;
        goto done;
    }
    if (atom->length != 1) {
        eprintf("2: Length error retrieving @port property.");
        result = 1;
        goto done;
    }

    atom = mapper_message_property(msg, AT_HOST);
    if (atom) {
        eprintf("2: Error, should not have been able to retrieve @host property.\n");
        result = 1;
        goto done;
    }

    /*****/
done:
    mapper_message_free(msg);
    if (!verbose)
        printf("..................................................");
    printf("Test %s.\n", result ? "FAILED" : "PASSED");
    return result;
}
