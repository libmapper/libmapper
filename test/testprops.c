
#include <string.h>
#include <stdio.h>

#include "../src/mapper_internal.h"

/* Test to ensure that setting and getting properties of signals and
 * devices is consistent. */

#define SEEN_DIR      0x0001
#define SEEN_LENGTH   0x0002
#define SEEN_NAME     0x0004
#define SEEN_TYPE     0x0008
#define SEEN_DEVNAME  0x0010

#define SEEN_UNIT     0x0020
#define SEEN_MIN      0x0040
#define SEEN_MAX      0x0080

#define SEEN_X        0x0100
#define SEEN_Y        0x0200
#define SEEN_TEST     0x0400

/* Code to return a key's "seen" code, to mark whether we've seen a
 * value. */
int seen_code(const char *key)
{
    struct { const char *s; int n; } seenvals[] = {
        { "direction",   SEEN_DIR },
        { "length",      SEEN_LENGTH },
        { "name",        SEEN_NAME },
        { "type",        SEEN_TYPE },
        { "device_name", SEEN_DEVNAME },
        { "unit",        SEEN_UNIT },
        { "min",         SEEN_MIN },
        { "max",         SEEN_MAX },
        { "x",           SEEN_X },
        { "y",           SEEN_Y },
        { "test",        SEEN_TEST },
    };
    int i, len = sizeof(seenvals)/sizeof(seenvals[0]);
    for (i=0; i<len; i++) {
        if (strcmp(seenvals[i].s, key)==0)
            return seenvals[i].n;
    }
    return 0;
}

int check_keys(mapper_db_signal sigprop)
{
    const char *key;
    const lo_arg *val;
    lo_type type;
    int i=0, seen=0;
    while (!mapper_db_signal_property_index(sigprop, i++,
                                            &key, &type, &val))
    {
        seen |= seen_code(key);
    }
    return seen;
}

int main()
{
    int seen, rc=0;
    mapper_signal sig = msig_new("/test", 1, 'f', 1, "Hz", 0, 0, 0, 0);
    mapper_db_signal sigprop = msig_properties(sig);

    /* Test that default parameters are all listed. */

    seen = check_keys(sigprop);
    if (seen != (SEEN_DIR | SEEN_LENGTH | SEEN_NAME
                 | SEEN_TYPE | SEEN_UNIT))
    {
        printf("1: mapper_db_signal_property_index() did not "
               "return the expected keys.\n");
        rc=1;
        goto cleanup;
    }

    /* Test that adding maximum causes it to be listed. */

    float mx = 35.0;
    msig_set_maximum(sig, &mx);

    seen = check_keys(sigprop);
    if (seen != (SEEN_DIR | SEEN_LENGTH | SEEN_NAME
                 | SEEN_TYPE | SEEN_UNIT | SEEN_MAX))
    {
        printf("2: mapper_db_signal_property_index() did not "
               "return the expected keys.\n");
        rc=1;
        goto cleanup;
    }

    /* Test that adding an extra parameter causes the extra parameter
     * to be listed. */

    msig_set_property(sig, "test", 's', (lo_arg*)"test_value");

    seen = check_keys(sigprop);
    if (seen != (SEEN_DIR | SEEN_LENGTH | SEEN_NAME
                 | SEEN_TYPE | SEEN_UNIT | SEEN_MAX | SEEN_TEST))
    {
        printf("3: mapper_db_signal_property_index() did not "
               "return the expected keys.\n");
        rc=1;
        goto cleanup;
    }

    /* Test that removing an extra parameter causes the extra
     * parameter to _not_ be listed. */

    msig_remove_property(sig, "test");

    seen = check_keys(sigprop);
    if (seen != (SEEN_DIR | SEEN_LENGTH | SEEN_NAME
                 | SEEN_TYPE | SEEN_UNIT | SEEN_MAX))
    {
        printf("4: mapper_db_signal_property_index() did not "
               "return the expected keys.\n");
        rc=1;
        goto cleanup;
    }

    /* Test that adding two more properties works as expected. */

    int x = 123;
    msig_set_property(sig, "x", 'i', (lo_arg*)&x);

    int y = 234;
    msig_set_property(sig, "y", 'i', (lo_arg*)&y);

    seen = check_keys(sigprop);
    if (seen != (SEEN_DIR | SEEN_LENGTH | SEEN_NAME
                 | SEEN_TYPE | SEEN_UNIT
                 | SEEN_MAX | SEEN_X | SEEN_Y))
    {
        printf("5: mapper_db_signal_property_index() did not "
               "return the expected keys.\n");
        rc=1;
        goto cleanup;
    }

    /* Test the type and value associated with "x". */

    lo_type type;
    const lo_arg *val;
    if (mapper_db_signal_property_lookup(sigprop, "x", &type, &val)) {
        printf("6: mapper_db_signal_property_lookup() did not "
               "find a value for `x'.\n");
        rc=1;
        goto cleanup;
    }

    printf("x: "); lo_arg_pp(type, (lo_arg*)val); printf("\n");

    if (type != 'i') {
        printf("6: mapper_db_signal_property_lookup() returned "
               "type %c, expected type %c.\n", type, 'i');
        rc=1;
        goto cleanup;
    }

    if (val->i != 123) {
        printf("6: mapper_db_signal_property_lookup() returned "
               "%d, expected %d.\n", val->i, 123);
        rc=1;
        goto cleanup;
    }

    /* Check that there is no value associated with previously-removed
     * "test". */

    if (!mapper_db_signal_property_lookup(sigprop, "test", &type, &val)) {
        printf("7: mapper_db_signal_property_lookup() unexpectedly "
               "found a value for removed property `test'.\n");
        rc=1;
        goto cleanup;
    }

    /* Check that there is an integer value associated with static,
     * required property "length". */

    if (mapper_db_signal_property_lookup(sigprop, "length", &type, &val)) {
        printf("8: mapper_db_signal_property_lookup() could not "
               "find a value for static, required property `length'.\n");
        rc=1;
        goto cleanup;
    }

    printf("length: "); lo_arg_pp(type, (lo_arg*)val); printf("\n");

    if (type != 'i') {
        printf("8: property `length' is type '%c', expected 'i'.\n", type);
        rc=1;
        goto cleanup;
    }

    if (val->i != 1) {
        printf("8: property `length' is %d, expected 1.\n", val->i);
        rc=1;
        goto cleanup;
    }

    /* Check that there is a string value associated with static,
     * required property "name". */

    if (mapper_db_signal_property_lookup(sigprop, "name", &type, &val)) {
        printf("9: mapper_db_signal_property_lookup() could not "
               "find a value for static, required property `name'.\n");
        rc=1;
        goto cleanup;
    }

    /* Using lo_arg_pp() here results in memory access errors when
     * profiling with Valgrind, since it assumes that lo_args of type
     * "string" use memory allocated in multiples of 4 bytes. */
    printf("name: %s\n", (char*)val);

    if (type != 's') {
        printf("9: property `name' is type '%c', expected 's'.\n", type);
        rc=1;
        goto cleanup;
    }

    if (strcmp(&val->s, "/test")) {
        printf("9: property `name' is %s, expected `/test'.\n", &val->s);
        rc=1;
        goto cleanup;
    }

    /* Check that there is a string value associated with static,
     * optional property "max". */

    if (mapper_db_signal_property_lookup(sigprop, "max", &type, &val)) {
        printf("10: mapper_db_signal_property_lookup() could not "
               "find a value for static, optional property `maximum'.\n");
        rc=1;
        goto cleanup;
    }

    printf("max: "); lo_arg_pp(type, (lo_arg*)val); printf("\n");

    if (type != 'f') {
        printf("10: property `maximum' is type '%c', expected 'f'.\n", type);
        rc=1;
        goto cleanup;
    }

    if (val->f != 35.0f) {
        printf("10: property `maximum' is %f, expected 35.0.\n", val->f);
        rc=1;
        goto cleanup;
    }

    /* Test that removing maximum causes it to _not_ be listed. */

    msig_set_maximum(sig, 0);

    seen = check_keys(sigprop);
    if (seen != (SEEN_DIR | SEEN_LENGTH | SEEN_NAME
                 | SEEN_TYPE | SEEN_UNIT | SEEN_X | SEEN_Y))
    {
        printf("11: mapper_db_signal_property_index() did not "
               "return the expected keys.\n");
        rc=1;
        goto cleanup;
    }

    if (!mapper_db_signal_property_lookup(sigprop, "max", &type, &val)) {
        printf("11: mapper_db_signal_property_lookup() unexpectedly "
               "found a value for `maximum' after it was removed.\n");
        rc=1;
        goto cleanup;
    }

    printf("Test SUCCESS.\n");

  cleanup:
    if (sig) msig_free(sig);
    return rc;
}
