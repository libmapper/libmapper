
#include <stdio.h>
#include <string.h>

#include <lo/lo_lowlevel.h>

#include "../src/types_internal.h"
#include "../src/mapper_internal.h"

int main()
{
    lo_arg *args[20], **a;
    mapper_message_t msg;
    int port=1234, src_length=4, i;
    float r[4] = {1.0, 2.0, -15.0, 25.0};
    int result = 0;

    printf("1: expected success\n");

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

    int rc = mapper_msg_parse_params(&msg, "/test", "sssffffsi", 13, args);
    if (rc) {
        printf("1: Error parsing.\n");
        return 1;
    }

    a = mapper_msg_get_param(&msg, AT_IP);
    if (!a) {
        printf("1: Could not get @IP param.\n");
        return 1;
    }
    if (strcmp(&(*a)->s, "127.0.0.1")!=0)
        result |= 1;
    printf("1: @IP = \"%s\" %s\n", &(*a)->s,
           result ? "WRONG" : "(correct)");
    if (result) return result;

    a = mapper_msg_get_param(&msg, AT_PORT);
    if (!a) {
        printf("1: Could not get @port param.\n");
        return 1;
    }
    if ((*a)->i!=1234)
        result |= 1;
    printf("1: @port = %d %s\n", (*a)->i,
           result ? "WRONG" : "(correct)");
    if (result) return result;

    a = mapper_msg_get_param(&msg, AT_SRC_MIN);
    int count = mapper_msg_get_length(&msg, AT_SRC_MIN);
    if (!a) {
        printf("1: Could not get @src_min param.\n");
        return 1;
    }
    if (count != 4) {
        printf("1: Wrong count returned for @scr_min param.\n");
    }
    for (i=0; i<count; i++) {
        if ((*a)->f!=r[i])
            result |= 1;
        printf("1: @src_min[%d] = %f %s\n", i, (*a++)->f,
               result ? "WRONG" : "(correct)");
        if (result) return result;
    }

    /*****/

    printf("2: expected failure\n");

    args[0] = (lo_arg*)"@port";
    args[1] = (lo_arg*)&port;
    args[2] = (lo_arg*)"@IP";

    rc = mapper_msg_parse_params(&msg, "/test", "sss", 3, args);
    if (!rc) {
        printf("2: Error, unexpected parsing success.\n");
        return 1;
    }

    /*****/

    printf("Test PASSED.\n");
    return 0;
}
