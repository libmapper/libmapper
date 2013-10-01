
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
    const void *val;
    char type;
    int i=0, seen=0, length;
    while (!mapper_db_signal_property_index(sigprop, i++, &key,
                                            &type, &val, &length))
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
    else
        printf("1: OK\n");

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
    else
        printf("2: OK\n");

    /* Test that adding an extra parameter causes the extra parameter
     * to be listed. */

    char *str = "test_value";
    msig_set_property(sig, "test", 's', &str, 1);

    seen = check_keys(sigprop);
    if (seen != (SEEN_DIR | SEEN_LENGTH | SEEN_NAME
                 | SEEN_TYPE | SEEN_UNIT | SEEN_MAX | SEEN_TEST))
    {
        printf("3: mapper_db_signal_property_index() did not "
               "return the expected keys.\n");
        rc=1;
        goto cleanup;
    }
    else
        printf("3: OK\n");

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
    else
        printf("4: OK\n");

    /* Test that adding two more properties works as expected. */

    int x = 123;
    msig_set_property(sig, "x", 'i', &x, 1);

    int y = 234;
    msig_set_property(sig, "y", 'i', &y, 1);

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
    else
        printf("5: OK\n");

    /* Test the type and value associated with "x". */

    char type;
    const void *val;
    int length;
    if (mapper_db_signal_property_lookup(sigprop, "x", &type, &val, &length)) {
        printf("6: mapper_db_signal_property_lookup() did not "
               "find a value for `x'.\n");
        rc=1;
        goto cleanup;
    }
    printf("testprops retieved prop x from %p\n", val);

    if (type != 'i') {
        printf("6: mapper_db_signal_property_lookup() returned "
               "type %c, expected type %c.\n", type, 'i');
        rc=1;
        goto cleanup;
    }
    else
        printf("6: OK\n");

    printf("x: %d\n", *(int*)val);

    if (*(int*)val!= 123) {
        printf("7: mapper_db_signal_property_lookup() returned "
               "%d, expected %d.\n", *(int*)val, 123);
        rc=1;
        goto cleanup;
    }
    else
        printf("7: OK\n");

    /* Check that there is no value associated with previously-removed
     * "test". */

    if (!mapper_db_signal_property_lookup(sigprop, "test", &type,
                                          &val, &length)) {
        printf("8: mapper_db_signal_property_lookup() unexpectedly "
               "found a value for removed property `test'.\n");
        rc=1;
        goto cleanup;
    }
    else
        printf("8: OK\n");

    /* Check that there is an integer value associated with static,
     * required property "length". */

    if (mapper_db_signal_property_lookup(sigprop, "length", &type,
                                         &val, &length)) {
        printf("9: mapper_db_signal_property_lookup() could not "
               "find a value for static, required property `length'.\n");
        rc=1;
        goto cleanup;
    }
    else
        printf("9: OK\n");

    if (type != 'i') {
        printf("10: property `length' is type '%c', expected 'i'.\n", type);
        rc=1;
        goto cleanup;
    }
    else
        printf("10: OK\n");

    printf("length: %d\n", *(int*)val);

    if (*(int*)val != 1) {
        printf("11: property `length' is %d, expected 1.\n", *(int*)val);
        rc=1;
        goto cleanup;
    }
    else
        printf("11: OK\n");

    /* Check that there is a string value associated with static,
     * required property "name". */

    if (mapper_db_signal_property_lookup(sigprop, "name", &type,
                                         &val, &length)) {
        printf("12: mapper_db_signal_property_lookup() could not "
               "find a value for static, required property `name'.\n");
        rc=1;
        goto cleanup;
    }
    else
        printf("12: OK\n");

    char *name = (char*)val;

    /* Using lo_arg_pp() here results in memory access errors when
     * profiling with Valgrind, since it assumes that lo_args of type
     * "string" use memory allocated in multiples of 4 bytes. */
    printf("name: %s\n", name);

    if (type != 's') {
        printf("13: property `name' is type '%c', expected 's'.\n", type);
        rc=1;
        goto cleanup;
    }
    else
        printf("13: OK\n");

    if (strcmp(name, "/test")) {
        printf("14: property `name' is %s, expected `/test'.\n", name);
        rc=1;
        goto cleanup;
    }
    else
        printf("14: OK\n");

    /* Check that there is a string value associated with static,
     * optional property "max". */

    if (mapper_db_signal_property_lookup(sigprop, "max", &type,
                                         &val, &length)) {
        printf("15: mapper_db_signal_property_lookup() could not "
               "find a value for static, optional property `maximum'.\n");
        rc=1;
        goto cleanup;
    }
    else
        printf("15: OK\n");

    float *floatval = (float*)val;

    if (type != 'f') {
        printf("16: property `maximum' is type '%c', expected 'f'.\n", type);
        rc=1;
        goto cleanup;
    }
    else
        printf("16: OK\n");

    printf("max: %f\n", floatval[0]);

    if (floatval[0] != 35.0f) {
        printf("17: property `maximum' is %f, expected 35.0.\n", floatval[0]);
        rc=1;
        goto cleanup;
    }
    else
        printf("17: OK\n");

    /* Test that removing maximum causes it to _not_ be listed. */

    msig_set_maximum(sig, 0);

    seen = check_keys(sigprop);
    if (seen != (SEEN_DIR | SEEN_LENGTH | SEEN_NAME
                 | SEEN_TYPE | SEEN_UNIT | SEEN_X | SEEN_Y))
    {
        printf("18: mapper_db_signal_property_index() did not "
               "return the expected keys.\n");
        rc=1;
        goto cleanup;
    }
    else
        printf("18: OK\n");

    if (!mapper_db_signal_property_lookup(sigprop, "max", &type,
                                          &val, &length)) {
        printf("19: mapper_db_signal_property_lookup() unexpectedly "
               "found a value for `maximum' after it was removed.\n");
        rc=1;
        goto cleanup;
    }
    else
        printf("19: OK\n");

    printf("Test SUCCESS.\n");

  cleanup:
    if (sig) msig_free(sig);
    return rc;
}
