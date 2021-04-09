
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <lo/lo_lowlevel.h>
#include "../src/types_internal.h"
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

int main(int argc, char **argv)
{
    lo_arg *args[20];
    mpr_msg msg;
    mpr_msg_atom atom;
    int port=1234, src_len=4;
    float r[4] = {1.0, 2.0, -15.0, 25.0};
    int i, j, result = 0;

    /* process flags for -v verbose, -h help */
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
    args[9]  = (lo_arg*)"@src@type";
    args[10] = (lo_arg*)"f";
    args[11] = (lo_arg*)"@src@length";
    args[12] = (lo_arg*)&src_len;

    msg = mpr_msg_parse_props(13, "sssffffsiscsi", args);
    if (!msg) {
        eprintf("1: Error parsing.\n");
        result = 1;
        goto done;
    }

    atom = mpr_msg_get_prop(msg, MPR_PROP_HOST);
    if (!atom) {
        eprintf("1: Could not get @host property.\n");
        result = 1;
        goto done;
    }
    if (!mpr_type_get_is_str(atom->types[0])) {
        eprintf("1: Type error retrieving @host property.");
        result = 1;
        goto done;
    }
    if (atom->len != 1) {
        eprintf("1: Length error retrieving @host property.");
        result = 1;
        goto done;
    }
    if (strcmp(&(*atom->vals)->s, "127.0.0.1")!=0)
        result |= 1;
    eprintf("1: @host = \"%s\" %s\n", &(*atom->vals)->s,
            result ? "WRONG" : "(correct)");
    if (result)
        goto done;

    atom = mpr_msg_get_prop(msg, MPR_PROP_PORT);
    if (!atom) {
        eprintf("1: Could not get @port property.\n");
        result = 1;
        goto done;
    }
    if (atom->types[0] != MPR_INT32) {
        eprintf("1: Type error retrieving @port property.");
        result = 1;
        goto done;
    }
    if (atom->len != 1) {
        eprintf("1: Length error retrieving @port property.");
        result = 1;
        goto done;
    }
    if ((*atom->vals)->i!=1234)
        result |= 1;
    eprintf("1: @port = %d %s\n", (*atom->vals)->i,
            result ? "WRONG" : "(correct)");
    if (result)
        goto done;

    atom = mpr_msg_get_prop(msg, SRC_SLOT_PROP(0) | MPR_PROP_MIN);
    if (!atom) {
        eprintf("1: Could not get @src@min property.\n");
        result = 1;
        goto done;
    }
    if (atom->types[0] != MPR_FLT) {
        eprintf("1: Type error retrieving @src@min property.");
        result = 1;
        goto done;
    }
    if (atom->len != 4) {
        eprintf("1: Length error retrieving @src@min property.");
        result = 1;
        goto done;
    }
    for (i = 0; i < atom->len; i++) {
        if (atom->vals[i]->f != r[i])
            result = 1;
        eprintf("1: @src@min[%d] = %f %s\n", i, atom->vals[i]->f,
                result ? "WRONG" : "(correct)");
        if (result)
            goto done;
    }

    /*****/

    eprintf("2: deliberately malformed message\n");

    args[0] = (lo_arg*)"@port";
    args[1] = (lo_arg*)&port;
    args[2] = (lo_arg*)"@host";

    mpr_msg_free(msg);
    msg = mpr_msg_parse_props(3, "sis", args);
    if (!msg) {
        eprintf("2: Error parsing.\n");
        result = 1;
        goto done;
    }

    atom = mpr_msg_get_prop(msg, MPR_PROP_PORT);
    if (!atom) {
        eprintf("2: Could not get @port property.\n");
        result = 1;
        goto done;
    }
    if (atom->types[0] != MPR_INT32) {
        eprintf("2: Type error retrieving @port property.");
        result = 1;
        goto done;
    }
    if (atom->len != 1) {
        eprintf("2: Length error retrieving @port property.");
        result = 1;
        goto done;
    }

    atom = mpr_msg_get_prop(msg, MPR_PROP_HOST);
    if (atom) {
        eprintf("2: Error, should not have been able to retrieve @host property.\n");
        result = 1;
        goto done;
    }

    /*****/

    eprintf("3: removing properties\n");

    args[0] = (lo_arg*)"-@foo";
    args[1] = (lo_arg*)"@port";
    args[2] = (lo_arg*)&port;
    args[3] = (lo_arg*)"-@bar";

    mpr_msg_free(msg);
    msg = mpr_msg_parse_props(4, "ssis", args);
    if (!msg) {
        eprintf("3: Error parsing.\n");
        result = 1;
        goto done;
    }

    if (msg->num_atoms != 3) {
        eprintf("3: Wrong number of atoms.\n");
        result = 1;
        goto done;
    }

    atom = &msg->atoms[0];
    if (strcmp(atom->key, "foo")) {
        eprintf("3: Could not get -@foo property.\n");
        result = 1;
        goto done;
    }
    if (!(atom->prop & PROP_REMOVE)) {
        eprintf("3: Missing PROP_REMOVE flag.\n");
        result = 1;
        goto done;
    }

    atom = mpr_msg_get_prop(msg, MPR_PROP_PORT);
    if (!atom) {
        eprintf("3: Could not get @port property.\n");
        result = 1;
        goto done;
    }
    if (atom->types[0] != MPR_INT32) {
        eprintf("3: Type error retrieving @port property.");
        result = 1;
        goto done;
    }
    if (atom->len != 1) {
        eprintf("3: Length error retrieving @port property.");
        result = 1;
        goto done;
    }

    atom = &msg->atoms[2];
    if (strcmp(atom->key, "bar")) {
        eprintf("3: Could not get -@bar property.\n");
        result = 1;
        goto done;
    }
    if (!(atom->prop & PROP_REMOVE)) {
        eprintf("3: Missing PROP_REMOVE flag.\n");
        result = 1;
        goto done;
    }

    /*****/
done:
    mpr_msg_free(msg);
    if (!verbose)
        printf("..................................................");
    printf("Test %s\x1B[0m.\n", result ? "\x1B[31mFAILED" : "\x1B[32mPASSED");
    return result;
}
