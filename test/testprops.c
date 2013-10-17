
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
    int i, seen, rc=0;
    mapper_signal sig = msig_new("/test", 1, 'f', 1, "Hz", 0, 0, 0, 0);
    mapper_db_signal sigprop = msig_properties(sig);

    /* Test that default parameters are all listed. */
    printf("Test 1:  checking default parameters... ");
    seen = check_keys(sigprop);
    if (seen != (SEEN_DIR | SEEN_LENGTH | SEEN_NAME
                 | SEEN_TYPE | SEEN_UNIT))
    {
        printf("ERROR\n");
        rc=1;
        goto cleanup;
    }
    else
        printf("OK\n");

    /* Test that adding maximum causes it to be listed. */
    float mx = 35.0;
    msig_set_maximum(sig, &mx);
    printf("Test 2:  adding static property 'maximum'... ");
    seen = check_keys(sigprop);
    if (seen != (SEEN_DIR | SEEN_LENGTH | SEEN_NAME
                 | SEEN_TYPE | SEEN_UNIT | SEEN_MAX))
    {
        printf("ERROR\n");
        rc=1;
        goto cleanup;
    }
    else
        printf("OK\n");

    /* Test that adding an extra parameter causes the extra parameter
     * to be listed. */
    char *str = "test_value";
    msig_set_property(sig, "test", 's', &str, 1);
    printf("Test 3:  adding extra string property 'test'... ");
    seen = check_keys(sigprop);
    if (seen != (SEEN_DIR | SEEN_LENGTH | SEEN_NAME
                 | SEEN_TYPE | SEEN_UNIT | SEEN_MAX | SEEN_TEST))
    {
        printf("ERROR\n");
        rc=1;
        goto cleanup;
    }
    else
        printf("OK\n");

    printf("Test 4:  retrieving property 'test':");
    char type;
    const void *val;
    int length;
    if (mapper_db_signal_property_lookup(sigprop, "test", &type, &val, &length)) {
        printf("ERROR\n");
        rc=1;
        goto cleanup;
    }
    printf("%s... OK?\n", (char*)val);

    printf("\t checking type: %c ... ", type);
    if (type != 's') {
        printf("ERROR (expected %c)\n", 's');
        rc=1;
        goto cleanup;
    }
    else
        printf("OK\n");

    printf("\t checking length: %d ... ", length);
    if (length != 1) {
        printf("ERROR (expected %d)\n", 1);
        rc=1;
        goto cleanup;
    }
    else
        printf("OK\n");

    printf("\t checking value: '%s' ... ", (char*)val);
    if (strcmp((char*)val, str)) {
        printf("ERROR (expected '%s')\n", str);
        rc=1;
        goto cleanup;
    }
    else
        printf("OK\n");

    /* Test that removing an extra parameter causes the extra
     * parameter to _not_ be listed. */
    msig_remove_property(sig, "test");
    printf("Test 5:  removing extra property 'test'... ");
    seen = check_keys(sigprop);
    if (seen != (SEEN_DIR | SEEN_LENGTH | SEEN_NAME
                 | SEEN_TYPE | SEEN_UNIT | SEEN_MAX))
    {
        printf("ERROR\n");
        rc=1;
        goto cleanup;
    }
    else
        printf("OK\n");

    /* Test that adding two more properties works as expected. */
    int x = 123;
    msig_set_property(sig, "x", 'i', &x, 1);
    int y = 234;
    msig_set_property(sig, "y", 'i', &y, 1);
    printf("Test 6:  adding extra integer properties 'x' and 'y'... ");
    seen = check_keys(sigprop);
    if (seen != (SEEN_DIR | SEEN_LENGTH | SEEN_NAME
                 | SEEN_TYPE | SEEN_UNIT
                 | SEEN_MAX | SEEN_X | SEEN_Y))
    {
        printf("ERROR\n");
        rc=1;
        goto cleanup;
    }
    else
        printf("OK\n");

    /* Test the type and value associated with "x". */
    printf("Test 7:  retrieving property 'x'...");
    if (mapper_db_signal_property_lookup(sigprop, "x", &type, &val, &length)) {
        printf("ERROR\n");
        rc=1;
        goto cleanup;
    }
    printf("OK\n");

    printf("\t checking type: %c ... ", type);
    if (type != 'i') {
        printf("ERROR (expected %c)\n", 'i');
        rc=1;
        goto cleanup;
    }
    else
        printf("OK\n");

    printf("\t checking length: %d ... ", length);
    if (length != 1) {
        printf("ERROR (expected %d)\n", 1);
        rc=1;
        goto cleanup;
    }
    else
        printf("OK\n");

    printf("\t checking value: %i ... ", *(int*)val);
    if (*(int*)val != 123) {
        printf("ERROR (expected %d)\n", 123);
        rc=1;
        goto cleanup;
    }
    else
        printf("OK\n");

    /* Check that there is no value associated with previously-removed
     * "test". */
    printf("Test 8:  retrieving removed property 'test': ");
    if (!mapper_db_signal_property_lookup(sigprop, "test", &type,
                                          &val, &length)) {
        printf("found... ERROR\n");
        rc=1;
        goto cleanup;
    }
    else
        printf("not found... OK\n");

    /* Check that there is an integer value associated with static,
     * required property "length". */
    printf("Test 9:  retrieving static, required property 'length'... ");
    if (mapper_db_signal_property_lookup(sigprop, "length", &type,
                                         &val, &length)) {
        printf("not found... ERROR\n");
        rc=1;
        goto cleanup;
    }
    else
        printf("OK\n");

    printf("\t checking type: %c ... ", type);
    if (type != 'i') {
        printf("ERROR (expected %c)\n", 'i');
        rc=1;
        goto cleanup;
    }
    else
        printf("OK\n");

    printf("\t checking length: %d ... ", length);
    if (length != 1) {
        printf("ERROR (expected %d)\n", 1);
        rc=1;
        goto cleanup;
    }
    else
        printf("OK\n");

    printf("\t checking value: '%d' ... ", *(int*)val);
    if (*(int*)val != 1) {
        printf("ERROR (expected %d)\n", 1);
        rc=1;
        goto cleanup;
    }
    else
        printf("OK\n");

    /* Check that there is a string value associated with static,
     * required property "name". */
    printf("Test 10: retrieving static, required property 'name'... ");
    if (mapper_db_signal_property_lookup(sigprop, "name", &type,
                                         &val, &length)) {
        printf("not found... ERROR\n");
        rc=1;
        goto cleanup;
    }
    else
        printf("OK\n");

    printf("\t checking type: %c ... ", type);
    if (type != 's') {
        printf("ERROR (expected %c)\n", 's');
        rc=1;
        goto cleanup;
    }
    else
        printf("OK\n");

    printf("\t checking length: %d ... ", length);
    if (length != 1) {
        printf("ERROR (expected %d)\n", 1);
        rc=1;
        goto cleanup;
    }
    else
        printf("OK\n");

    printf("\t checking value: '%s' ... ", (char*)val);
    if (strcmp((char*)val, "/test")) {
        printf("ERROR (expected '%s')\n", str);
        rc=1;
        goto cleanup;
    }
    else
        printf("OK\n");

    /* Check that there is a float value associated with static,
     * optional property "max". */
    printf("Test 11: retrieving static, optional property 'max'... ");
    if (mapper_db_signal_property_lookup(sigprop, "max", &type,
                                         &val, &length)) {
        printf("not found... ERROR\n");
        rc=1;
        goto cleanup;
    }
    else
        printf("OK\n");

    printf("\t checking type: %c ... ", type);
    if (type != 'f') {
        printf("ERROR (expected %c)\n", 'f');
        rc=1;
        goto cleanup;
    }
    else
        printf("OK\n");

    printf("\t checking length: %d ... ", length);
    if (length != 1) {
        printf("ERROR (expected %d)\n", 1);
        rc=1;
        goto cleanup;
    }
    else
        printf("OK\n");

    printf("\t checking value: '%f' ... ", *(float*)val);
    if (*(float*)val != 35.0f) {
        printf("ERROR (expected %f)\n", *(float*)val);
        rc=1;
        goto cleanup;
    }
    else
        printf("OK\n");

    /* Test that removing maximum causes it to _not_ be listed. */
    msig_set_maximum(sig, 0);
    printf("Test 12: removing optional property 'max'... ");
    seen = check_keys(sigprop);
    if (seen & SEEN_MAX)
    {
        printf("ERROR\n");
        rc=1;
        goto cleanup;
    }
    else
        printf("OK\n");

    printf("Test 13: retrieving optional property 'max': ");
    if (!mapper_db_signal_property_lookup(sigprop, "max", &type,
                                          &val, &length)) {
        printf("found... ERROR\n");
        rc=1;
        goto cleanup;
    }
    else
        printf("not found... OK\n");

    /* Test adding and retrieving an integer vector property. */
    printf("Test 14: adding an extra integer vector property 'test'... ");
    int set_int[] = {1, 2, 3, 4, 5};
    msig_set_property(sig, "test", 'i', &set_int, 5);
    seen = check_keys(sigprop);
    if (seen != (SEEN_DIR | SEEN_LENGTH | SEEN_NAME
                 | SEEN_TYPE | SEEN_UNIT | SEEN_X
                 | SEEN_Y | SEEN_TEST))
    {
        printf("ERROR\n");
        rc=1;
        goto cleanup;
    }
    else
        printf("OK\n");

    printf("Test 15: retrieving vector property 'test': ");
    if (mapper_db_signal_property_lookup(sigprop, "test", &type,
                                         &val, &length)) {
        printf("not found... ERROR\n");
        rc=1;
        goto cleanup;
    }
    else
        printf("OK\n");

    printf("\t checking type: %c ... ", type);
    if (type != 'i') {
        printf("ERROR (expected %c)\n", 'i');
        rc=1;
        goto cleanup;
    }
    else
        printf("OK\n");

    printf("\t checking length: %d ... ", length);
    if (length != 5) {
        printf("ERROR (expected %d)\n", 1);
        rc=1;
        goto cleanup;
    }
    else
        printf("OK\n");

    int *read_int = (int*)val;
    int matched = 0;
    printf("\t checking value: [%i,%i,%i,%i,%i] ... ", read_int[0],
           read_int[1], read_int[2], read_int[3], read_int[4]);
    for (i = 0; i < 5; i++) {
        if (read_int[i] == set_int[i])
            matched++;
    }
    if (matched != 5) {
        printf("ERROR (expected [%i,%i,%i,%i,%i])\n", set_int[0],
               set_int[1], set_int[2], set_int[3], set_int[4]);
        rc=1;
        goto cleanup;
    }
    else
        printf("OK\n");

    /* Test rewriting 'test' as float vector property. */
    printf("Test 16: rewriting 'test' as vector float property... ");
    float set_float[] = {10., 20., 30., 40., 50.};
    msig_set_property(sig, "test", 'f', &set_float, 5);
    seen = check_keys(sigprop);
    if (seen != (SEEN_DIR | SEEN_LENGTH | SEEN_NAME
                 | SEEN_TYPE | SEEN_UNIT | SEEN_X
                 | SEEN_Y | SEEN_TEST))
    {
        printf("ERROR\n");
        rc=1;
        goto cleanup;
    }
    else
        printf("OK\n");

    printf("Test 17: retrieving property 'test'... ");
    if (mapper_db_signal_property_lookup(sigprop, "test", &type,
                                         &val, &length)) {
        printf("not found... ERROR\n");
        rc=1;
        goto cleanup;
    }
    else
        printf("OK\n");

    printf("\t checking type: %c ... ", type);
    if (type != 'f') {
        printf("ERROR (expected '%c')\n", 'f');
        rc=1;
        goto cleanup;
    }
    else
        printf("OK\n");

    printf("\t checking length: %i ... ", length);
    if (length != 5) {
        printf("ERROR (expected %d)\n", length);
        rc=1;
        goto cleanup;
    }
    else
        printf("OK\n");

    float *read_float = (float*)val;
    printf("\t checking value: [%f,%f,%f,%f,%f] ... ", read_float[0],
           read_float[1], read_float[2], read_float[3], read_float[4]);
    matched = 0;
    for (i = 0; i < 5; i++) {
        if (read_float[i] == set_float[i])
            matched++;
    }
    if (matched != 5) {
        printf("ERROR (expected [%f,%f,%f,%f,%f]\n", set_float[0],
               set_float[1], set_float[2], set_float[3], set_float[4]);
        rc=1;
        goto cleanup;
    }
    else
        printf("OK\n");

    /* Test rewriting property 'test' as string vector property. */
    printf("Test 18: rewriting 'test' as vector string property... ");
    char *set_string[] = {"foo", "bar"};
    msig_set_property(sig, "test", 's', &set_string, 2);
    seen = check_keys(sigprop);
    if (seen != (SEEN_DIR | SEEN_LENGTH | SEEN_NAME
                 | SEEN_TYPE | SEEN_UNIT | SEEN_X
                 | SEEN_Y | SEEN_TEST))
    {
        printf("ERROR\n");
        rc=1;
        goto cleanup;
    }
    else
        printf("OK\n");

    printf("Test 19: retrieving property 'test'... ");
    if (mapper_db_signal_property_lookup(sigprop, "test", &type,
                                         &val, &length)) {
        printf("not found... ERROR\n");
        rc=1;
        goto cleanup;
    }
    else
        printf("OK\n");

    printf("\t checking type: %c ... ", type);
    if (type != 's') {
        printf("ERROR (expected '%c')\n", 's');
        rc=1;
        goto cleanup;
    }
    else
        printf("OK\n");

    printf("\t checking length: %d ...", length);
    if (length != 2) {
        printf("ERROR (expected %d)\n", 2);
        rc=1;
        goto cleanup;
    }
    else
        printf("OK\n");

    char **read_string = (char**)val;
    printf("\t checking value: ['%s','%s'] ... ",
           read_string[0], read_string[1]);
    matched = 0;
    for (i = 0; i < 2; i++) {
        if (read_string[i] && strcmp(read_string[i], set_string[i]) == 0)
            matched++;
    }
    if (matched != 2) {
        printf("ERROR (expected ['%s','%s'])\n", set_string[0], set_string[1]);
        rc=1;
        goto cleanup;
    }
    else
        printf("OK\n");

    printf("Test SUCCESS.\n");

  cleanup:
    if (sig) msig_free(sig);
    return rc;
}
