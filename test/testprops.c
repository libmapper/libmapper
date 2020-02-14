#include <mpr/mpr.h>
#include <string.h>
#include <stdio.h>

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

int verbose = 1;

#define eprintf(format, ...) do {               \
    if (verbose)                                \
        fprintf(stdout, format, ##__VA_ARGS__); \
} while(0)

const char *type_name(mpr_type type)
{
    switch (type) {
        case MPR_DEV:       return "MPR_DEV";
        case MPR_SIG_IN:    return "MPR_SIG_IN";
        case MPR_SIG_OUT:   return "MPR_SIG_OUT";
        case MPR_SIG:       return "MPR_SIG";
        case MPR_MAP_IN:    return "MPR_MAP_IN";
        case MPR_MAP_OUT:   return "MPR_MAP_OUT";
        case MPR_MAP:       return "MPR_MAP";
        case MPR_OBJ:       return "MPR_OBJ";
        case MPR_LIST:      return "MPR_LIST";
        case MPR_BOOL:      return "MPR_BOOL";
        case MPR_TYPE:      return "MPR_TYPE";
        case MPR_DBL:       return "MPR_DBL";
        case MPR_FLT:       return "MPR_FLT";
        case MPR_INT64:     return "MPR_INT64";
        case MPR_INT32:     return "MPR_INT32";
        case MPR_STR:       return "MPR_STR";
        case MPR_TIME:      return "MPR_TIME";
        case MPR_PTR:       return "MPR_PTR";
        case MPR_NULL:      return "MPR_NULL";
        default:            return "unknown type!";
    }
}

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

int check_keys(mpr_obj obj)
{
    const char *key;
    const void *val;
    mpr_type type;
    int i=0, seen=0, length;
    while (mpr_obj_get_prop_by_idx(obj, i++, &key, &length, &type, &val, 0)) {
        seen |= seen_code(key);
    }
    return seen;
}

int main(int argc, char **argv)
{
    int i, j, seen, result = 0;

    // process flags for -v verbose, -h help
    for (i = 1; i < argc; i++) {
        if (argv[i] && argv[i][0] == '-') {
            int len = strlen(argv[i]);
            for (j = 1; j < len; j++) {
                switch (argv[i][j]) {
                    case 'h':
                        eprintf("testprops.c: possible arguments "
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

    mpr_dev dev = mpr_dev_new("testprops", 0);
    mpr_sig sig = mpr_sig_new(dev, MPR_DIR_IN, "test", 1, MPR_FLT, "Hz",
                              NULL, NULL, NULL, NULL, 0);

    while (!mpr_dev_get_is_ready(dev)) {
        mpr_dev_poll(dev, 100);
    }

    /* Test that default parameters are all listed. */
    eprintf("Test 1:  checking default parameters... ");
    seen = check_keys(sig);
    if (seen != (SEEN_DIR | SEEN_LENGTH | SEEN_NAME | SEEN_TYPE | SEEN_UNIT)) {
        eprintf("ERROR\n");
        result = 1;
        goto cleanup;
    }
    else
        eprintf("OK\n");

    /* Test that adding maximum causes it to be listed. */
    eprintf("Test 2:  adding static property 'maximum'... ");
    float fval = 35.0;
    mpr_obj_set_prop(sig, MPR_PROP_MAX, NULL, 1, MPR_FLT, &fval, 1);
    seen = check_keys(sig);
    if (seen != (SEEN_DIR | SEEN_LENGTH | SEEN_NAME | SEEN_TYPE | SEEN_UNIT
                 | SEEN_MAX)) {
        eprintf("ERROR\n");
        result = 1;
        goto cleanup;
    }
    else
        eprintf("OK\n");

    /* Test that adding an extra parameter causes the extra parameter
     * to be listed. */
    eprintf("Test 3:  adding extra string property 'test'... ");
    const char *str = "test_value";
    mpr_obj_set_prop(sig, MPR_PROP_EXTRA, "test", 1, MPR_STR, str, 1);
    seen = check_keys(sig);
    if (seen != (SEEN_DIR | SEEN_LENGTH | SEEN_NAME | SEEN_TYPE | SEEN_UNIT
                 | SEEN_MAX | SEEN_TEST)) {
        eprintf("ERROR\n");
        result = 1;
        goto cleanup;
    }
    else
        eprintf("OK\n");

    eprintf("Test 4:  retrieving property 'test'... ");
    mpr_type type;
    const void *val;
    int length;
    if (!mpr_obj_get_prop_by_key(sig, "test", &length, &type, &val, 0)) {
        eprintf("ERROR\n");
        result = 1;
        goto cleanup;
    }
    eprintf("OK\n");

    eprintf("\t checking type: %s ... ", type_name(type));
    if (type != MPR_STR) {
        eprintf("ERROR (expected %s)\n", type_name(MPR_STR));
        result = 1;
        goto cleanup;
    }
    else
        eprintf("OK\n");

    eprintf("\t checking length: %d ... ", length);
    if (length != 1) {
        eprintf("ERROR (expected %d)\n", 1);
        result = 1;
        goto cleanup;
    }
    else
        eprintf("OK\n");

    eprintf("\t checking value: '%s' ... ", (char*)val);
    if (strcmp((char*)val, str)) {
        eprintf("ERROR (expected '%s')\n", str);
        result = 1;
        goto cleanup;
    }
    else
        eprintf("OK\n");

    /* Test that removing an extra parameter causes the extra
     * parameter to _not_ be listed. */
    eprintf("Test 5:  removing extra property 'test'... ");
    mpr_obj_remove_prop(sig, MPR_PROP_EXTRA, "test");
    seen = check_keys(sig);
    if (seen != (SEEN_DIR | SEEN_LENGTH | SEEN_NAME | SEEN_TYPE | SEEN_UNIT
                 | SEEN_MAX)) {
        eprintf("ERROR\n");
        result = 1;
        goto cleanup;
    }
    else
        eprintf("OK\n");

    /* Test that adding two more properties works as expected. */
    eprintf("Test 6:  adding extra integer properties 'x' and 'y'... ");
    int x = 123;
    mpr_obj_set_prop(sig, MPR_PROP_EXTRA, "x", 1, MPR_INT32, &x, 1);
    int y = 234;
    mpr_obj_set_prop(sig, MPR_PROP_EXTRA, "y", 1, MPR_INT32, &y, 1);
    seen = check_keys(sig);
    if (seen != (SEEN_DIR | SEEN_LENGTH | SEEN_NAME | SEEN_TYPE | SEEN_UNIT
                 | SEEN_MAX | SEEN_X | SEEN_Y)) {
        eprintf("ERROR\n");
        result = 1;
        goto cleanup;
    }
    else
        eprintf("OK\n");

    /* Test the type and value associated with "x". */
    eprintf("Test 7:  retrieving property 'x'...");
    if (!mpr_obj_get_prop_by_key(sig, "x", &length, &type, &val, 0)) {
        eprintf("ERROR\n");
        result = 1;
        goto cleanup;
    }
    eprintf("OK\n");

    eprintf("\t checking type: %s ... ", type_name(type));
    if (type != MPR_INT32) {
        eprintf("ERROR (expected %s)\n", type_name(MPR_INT32));
        result = 1;
        goto cleanup;
    }
    else
        eprintf("OK\n");

    eprintf("\t checking length: %d ... ", length);
    if (length != 1) {
        eprintf("ERROR (expected %d)\n", 1);
        result = 1;
        goto cleanup;
    }
    else
        eprintf("OK\n");

    eprintf("\t checking value: %i ... ", *(int*)val);
    if (*(int*)val != 123) {
        eprintf("ERROR (expected %d)\n", 123);
        result = 1;
        goto cleanup;
    }
    else
        eprintf("OK\n");

    /* Check that there is no value associated with previously-removed
     * "test". */
    eprintf("Test 8:  retrieving removed property 'test': ");
    if (mpr_obj_get_prop_by_key(sig, "test", &length, &type, &val, 0)) {
        eprintf("found... ERROR\n");
        result = 1;
        goto cleanup;
    }
    else
        eprintf("not found... OK\n");

    /* Check that there is an integer value associated with static,
     * required property "length". */
    eprintf("Test 9:  retrieving static, required property 'length'... ");
    if (!mpr_obj_get_prop_by_key(sig, "length", &length, &type, &val, 0)) {
        eprintf("not found... ERROR\n");
        result = 1;
        goto cleanup;
    }
    else
        eprintf("OK\n");

    eprintf("\t checking type: %s ... ", type_name(type));
    if (type != MPR_INT32) {
        eprintf("ERROR (expected %s)\n", type_name(MPR_INT32));
        result = 1;
        goto cleanup;
    }
    else
        eprintf("OK\n");

    eprintf("\t checking length: %d ... ", length);
    if (length != 1) {
        eprintf("ERROR (expected %d)\n", 1);
        result = 1;
        goto cleanup;
    }
    else
        eprintf("OK\n");

    eprintf("\t checking value: '%d' ... ", *(int*)val);
    if (*(int*)val != 1) {
        eprintf("ERROR (expected %d)\n", 1);
        result = 1;
        goto cleanup;
    }
    else
        eprintf("OK\n");

    /* Check that there is a string value associated with static,
     * required property "name". */
    eprintf("Test 10: retrieving static, required property 'name'... ");
    if (!mpr_obj_get_prop_by_idx(sig, MPR_PROP_NAME, NULL, &length, &type, &val, 0)) {
        eprintf("not found... ERROR\n");
        result = 1;
        goto cleanup;
    }
    else
        eprintf("OK\n");

    eprintf("\t checking type: %s ... ", type_name(type));
    if (type != MPR_STR) {
        eprintf("ERROR (expected %s)\n", type_name(MPR_STR));
        result = 1;
        goto cleanup;
    }
    else
        eprintf("OK\n");

    eprintf("\t checking length: %d ... ", length);
    if (length != 1) {
        eprintf("ERROR (expected %d)\n", 1);
        result = 1;
        goto cleanup;
    }
    else
        eprintf("OK\n");

    eprintf("\t checking value: '%s' ... ", (char*)val);
    if (strcmp((char*)val, "test")) {
        eprintf("ERROR (expected '%s')\n", str);
        result = 1;
        goto cleanup;
    }
    else
        eprintf("OK\n");

    /* Check that there is a float value associated with static,
     * optional property "max". */
    eprintf("Test 11: retrieving static, optional property 'max'... ");
    if (!mpr_obj_get_prop_by_idx(sig, MPR_PROP_MAX, NULL, &length, &type, &val, 0)) {
        eprintf("not found... ERROR\n");
        result = 1;
        goto cleanup;
    }
    else
        eprintf("OK\n");

    eprintf("\t checking type: %s ... ", type_name(type));
    if (type != MPR_FLT) {
        eprintf("ERROR (expected %s)\n", type_name(MPR_FLT));
        result = 1;
        goto cleanup;
    }
    else
        eprintf("OK\n");

    eprintf("\t checking length: %d ... ", length);
    if (length != 1) {
        eprintf("ERROR (expected %d)\n", 1);
        result = 1;
        goto cleanup;
    }
    else
        eprintf("OK\n");

    eprintf("\t checking value: '%f' ... ", *(float*)val);
    if (*(float*)val != 35.0f) {
        eprintf("ERROR (expected %f)\n", 35.0f);
        result = 1;
        goto cleanup;
    }
    else
        eprintf("OK\n");

    /* Test that removing maximum causes it to _not_ be listed. */
    eprintf("Test 12: removing optional property 'max'... ");
    mpr_obj_remove_prop(sig, MPR_PROP_MAX, NULL);
    seen = check_keys(sig);
    if (seen & SEEN_MAX)
    {
        eprintf("ERROR\n");
        result = 1;
        goto cleanup;
    }
    else
        eprintf("OK\n");

    eprintf("Test 13: retrieving optional property 'max': ");
    if (mpr_obj_get_prop_by_key(sig, "max", &length, &type, &val, 0)) {
        eprintf("found... ERROR\n");
        result = 1;
        goto cleanup;
    }
    else
        eprintf("not found... OK\n");

    /* Test adding and retrieving an integer vector property. */
    eprintf("Test 14: adding an extra integer vector property 'test'... ");
    int set_int[] = {1, 2, 3, 4, 5};
    mpr_obj_set_prop(sig, MPR_PROP_EXTRA, "test", 5, MPR_INT32, &set_int, 1);
    seen = check_keys(sig);
    if (seen != (SEEN_DIR | SEEN_LENGTH | SEEN_NAME | SEEN_TYPE | SEEN_UNIT
                 | SEEN_X | SEEN_Y | SEEN_TEST))
    {
        eprintf("ERROR\n");
        result = 1;
        goto cleanup;
    }
    else
        eprintf("OK\n");

    eprintf("Test 15: retrieving vector property 'test': ");
    if (!mpr_obj_get_prop_by_key(sig, "test", &length, &type, &val, 0)) {
        eprintf("not found... ERROR\n");
        result = 1;
        goto cleanup;
    }
    else
        eprintf("OK\n");

    eprintf("\t checking type: %s ... ", type_name(type));
    if (type != MPR_INT32) {
        eprintf("ERROR (expected %s)\n", type_name(MPR_INT32));
        result = 1;
        goto cleanup;
    }
    else
        eprintf("OK\n");

    eprintf("\t checking length: %d ... ", length);
    if (length != 5) {
        eprintf("ERROR (expected %d)\n", 1);
        result = 1;
        goto cleanup;
    }
    else
        eprintf("OK\n");

    int *read_int = (int*)val;
    int matched = 0;
    eprintf("\t checking value: [%i,%i,%i,%i,%i] ... ", read_int[0],
           read_int[1], read_int[2], read_int[3], read_int[4]);
    for (i = 0; i < 5; i++) {
        if (read_int[i] == set_int[i])
            matched++;
    }
    if (matched != 5) {
        eprintf("ERROR (expected [%i,%i,%i,%i,%i])\n", set_int[0],
               set_int[1], set_int[2], set_int[3], set_int[4]);
        result = 1;
        goto cleanup;
    }
    else
        eprintf("OK\n");

    /* Test rewriting 'test' as float vector property. */
    eprintf("Test 16: rewriting 'test' as vector float property... ");
    float set_float[] = {10., 20., 30., 40., 50.};
    mpr_obj_set_prop(sig, MPR_PROP_EXTRA, "test", 5, MPR_FLT, &set_float, 1);
    seen = check_keys(sig);
    if (seen != (SEEN_DIR | SEEN_LENGTH | SEEN_NAME | SEEN_TYPE | SEEN_UNIT
                 | SEEN_X | SEEN_Y | SEEN_TEST))
    {
        eprintf("ERROR\n");
        result = 1;
        goto cleanup;
    }
    else
        eprintf("OK\n");

    eprintf("Test 17: retrieving property 'test'... ");
    if (!mpr_obj_get_prop_by_key(sig, "test", &length, &type, &val, 0)) {
        eprintf("not found... ERROR\n");
        result = 1;
        goto cleanup;
    }
    else
        eprintf("OK\n");

    eprintf("\t checking type: %s ... ", type_name(type));
    if (type != MPR_FLT) {
        eprintf("ERROR (expected '%s')\n", type_name(MPR_FLT));
        result = 1;
        goto cleanup;
    }
    else
        eprintf("OK\n");

    eprintf("\t checking length: %i ... ", length);
    if (length != 5) {
        eprintf("ERROR (expected %d)\n", length);
        result = 1;
        goto cleanup;
    }
    else
        eprintf("OK\n");

    float *read_float = (float*)val;
    eprintf("\t checking value: [%f,%f,%f,%f,%f] ... ", read_float[0],
           read_float[1], read_float[2], read_float[3], read_float[4]);
    matched = 0;
    for (i = 0; i < 5; i++) {
        if (read_float[i] == set_float[i])
            matched++;
    }
    if (matched != 5) {
        eprintf("ERROR (expected [%f,%f,%f,%f,%f]\n", set_float[0],
               set_float[1], set_float[2], set_float[3], set_float[4]);
        result = 1;
        goto cleanup;
    }
    else
        eprintf("OK\n");

    /* Test rewriting property 'test' as string vector property. */
    eprintf("Test 18: rewriting 'test' as vector string property... ");
    char *set_string[] = {"foo", "bar"};
    mpr_obj_set_prop(sig, MPR_PROP_EXTRA, "test", 2, MPR_STR, set_string, 1);
    seen = check_keys(sig);
    if (seen != (SEEN_DIR | SEEN_LENGTH | SEEN_NAME | SEEN_TYPE | SEEN_UNIT
                 | SEEN_X | SEEN_Y | SEEN_TEST))
    {
        eprintf("ERROR\n");
        result = 1;
        goto cleanup;
    }
    else
        eprintf("OK\n");

    eprintf("Test 19: retrieving property 'test'... ");
    if (!mpr_obj_get_prop_by_key(sig, "test", &length, &type, &val, 0)) {
        eprintf("not found... ERROR\n");
        result = 1;
        goto cleanup;
    }
    else
        eprintf("OK\n");

    eprintf("\t checking type: %s ... ", type_name(type));
    if (type != MPR_STR) {
        eprintf("ERROR (expected '%s')\n", type_name(MPR_STR));
        result = 1;
        goto cleanup;
    }
    else
        eprintf("OK\n");

    eprintf("\t checking length: %d ...", length);
    if (length != 2) {
        eprintf("ERROR (expected %d)\n", 2);
        result = 1;
        goto cleanup;
    }
    else
        eprintf("OK\n");

    char **read_string = (char**)val;
    eprintf("\t checking value: ['%s','%s'] ... ",
           read_string[0], read_string[1]);
    matched = 0;
    for (i = 0; i < 2; i++) {
        if (read_string[i] && strcmp(read_string[i], set_string[i]) == 0)
            matched++;
    }
    if (matched != 2) {
        eprintf("ERROR (expected ['%s','%s'])\n", set_string[0], set_string[1]);
        result = 1;
        goto cleanup;
    }
    else
        eprintf("OK\n");

    /* Test rewriting property 'test' as void* property. */
    eprintf("Test 20: rewriting 'test' as void* property... ");
    const void *set_ptr = (const void*)0x9813;
    mpr_obj_set_prop(sig, MPR_PROP_EXTRA, "test", 1, MPR_PTR, set_ptr, 1);
    seen = check_keys(sig);
    if (seen != (SEEN_DIR | SEEN_LENGTH | SEEN_NAME | SEEN_TYPE | SEEN_UNIT
                 | SEEN_X | SEEN_Y | SEEN_TEST))
    {
        eprintf("ERROR\n");
        result = 1;
        goto cleanup;
    }
    else
        eprintf("OK\n");

    eprintf("Test 21: retrieving property 'test'... ");
    if (!mpr_obj_get_prop_by_key(sig, "test", &length, &type, &val, 0)) {
        eprintf("not found... ERROR\n");
        result = 1;
        goto cleanup;
    }
    else
        eprintf("OK\n");

    eprintf("\t checking type: %s ... ", type_name(type));
    if (type != MPR_PTR) {
        eprintf("ERROR (expected '%s')\n", type_name(MPR_PTR));
        result = 1;
        goto cleanup;
    }
    else
        eprintf("OK\n");

    eprintf("\t checking length: %d ...", length);
    if (length != 1) {
        eprintf("ERROR (expected %d)\n", 1);
        result = 1;
        goto cleanup;
    }
    else
        eprintf("OK\n");

    const void *read_ptr = (const void*)val;
    eprintf("\t checking value: %p ... ", read_ptr);
    if (read_ptr != set_ptr) {
        eprintf("ERROR (expected %p)\n", set_ptr);
        result = 1;
        goto cleanup;
    }
    else
        eprintf("OK\n");

    /* Test rewriting property 'test' as void* property to MPR_PROP_DATA. */
    eprintf("Test 22: writing MPR_PROP_DATA as void* property... ");
    mpr_obj_set_prop(sig, MPR_PROP_DATA, NULL, 1, MPR_PTR, set_ptr, 1);
    seen = check_keys(sig);
    if (seen != (SEEN_DIR | SEEN_LENGTH | SEEN_NAME | SEEN_TYPE | SEEN_UNIT
                 | SEEN_X | SEEN_Y | SEEN_TEST))
    {
        eprintf("ERROR\n");
        result = 1;
        goto cleanup;
    }
    else
        eprintf("OK\n");

    eprintf("Test 23: retrieving property MPR_PROP_DATA... ");
    if (!mpr_obj_get_prop_by_idx(sig, MPR_PROP_DATA, NULL, &length, &type, &val, 0)) {
        eprintf("not found... ERROR\n");
        result = 1;
        goto cleanup;
    }
    else
        eprintf("OK\n");

    eprintf("\t checking type: %s ... ", type_name(type));
    if (type != MPR_PTR) {
        eprintf("ERROR (expected '%s')\n", type_name(MPR_PTR));
        result = 1;
        goto cleanup;
    }
    else
        eprintf("OK\n");

    eprintf("\t checking length: %d ...", length);
    if (length != 1) {
        eprintf("ERROR (expected %d)\n", 1);
        result = 1;
        goto cleanup;
    }
    else
        eprintf("OK\n");

    read_ptr = (const void*)val;
    eprintf("\t checking value: %p ... ", read_ptr);
    if (read_ptr != set_ptr) {
        eprintf("ERROR (expected %p)\n", set_ptr);
        result = 1;
        goto cleanup;
    }
    else
        eprintf("OK\n");

    /* Test rewriting property 'test' as void* vector property. */
    eprintf("Test 24: rewriting 'test' as vector void* property... ");
    const void *set_ptrs[] = {(const void*)0x1111, (const void*)0x2222};
    mpr_obj_set_prop(sig, MPR_PROP_EXTRA, "test", 2, MPR_PTR, set_ptrs, 1);
    seen = check_keys(sig);
    if (seen != (SEEN_DIR | SEEN_LENGTH | SEEN_NAME | SEEN_TYPE | SEEN_UNIT
                 | SEEN_X | SEEN_Y | SEEN_TEST))
    {
        eprintf("ERROR\n");
        result = 1;
        goto cleanup;
    }
    else
        eprintf("OK\n");

    eprintf("Test 25: retrieving property 'test'... ");
    if (!mpr_obj_get_prop_by_key(sig, "test", &length, &type, &val, 0)) {
        eprintf("not found... ERROR\n");
        result = 1;
        goto cleanup;
    }
    else
        eprintf("OK\n");

    eprintf("\t checking type: %s ... ", type_name(type));
    if (type != MPR_PTR) {
        eprintf("ERROR (expected '%s')\n", type_name(MPR_PTR));
        result = 1;
        goto cleanup;
    }
    else
        eprintf("OK\n");

    eprintf("\t checking length: %d ...", length);
    if (length != 2) {
        eprintf("ERROR (expected %d)\n", 2);
        result = 1;
        goto cleanup;
    }
    else
        eprintf("OK\n");

    const void **read_ptrs = (const void**)val;
    eprintf("\t checking value: [%p,%p] ... ", read_ptrs[0], read_ptrs[1]);
    matched = 0;
    for (i = 0; i < 2; i++) {
        if (read_ptrs[i] == set_ptrs[i])
            ++matched;
    }
    if (matched != 2) {
        eprintf("ERROR (expected [%p, %p])\n", set_ptrs[0], set_ptrs[1]);
        result = 1;
        goto cleanup;
    }
    else
        eprintf("OK\n");

    /* Test rewriting property 'test' as mpr_obj property. */
    eprintf("Test 26: rewriting 'test' as mpr_obj property... ");
    mpr_obj_set_prop(sig, MPR_PROP_EXTRA, "test", 1, MPR_OBJ, sig, 1);
    seen = check_keys(sig);
    if (seen != (SEEN_DIR | SEEN_LENGTH | SEEN_NAME | SEEN_TYPE | SEEN_UNIT
                 | SEEN_X | SEEN_Y | SEEN_TEST))
    {
        eprintf("ERROR\n");
        result = 1;
        goto cleanup;
    }
    else
        eprintf("OK\n");

    eprintf("Test 27: retrieving property 'test'... ");
    if (!mpr_obj_get_prop_by_key(sig, "test", &length, &type, &val, 0)) {
        eprintf("not found... ERROR\n");
        result = 1;
        goto cleanup;
    }
    else
        eprintf("OK\n");

    eprintf("\t checking type: %s ... ", type_name(type));
    if (type != MPR_OBJ) {
        eprintf("ERROR (expected '%s')\n", type_name(MPR_OBJ));
        result = 1;
        goto cleanup;
    }
    else
        eprintf("OK\n");

    eprintf("\t checking length: %d ...", length);
    if (length != 1) {
        eprintf("ERROR (expected %d)\n", 1);
        result = 1;
        goto cleanup;
    }
    else
        eprintf("OK\n");

    mpr_obj read_obj = (mpr_obj)val;
    eprintf("\t checking value: %p ... ", read_obj);
    if (read_obj != sig) {
        eprintf("ERROR (expected %p)\n", sig);
        result = 1;
        goto cleanup;
    }
    else
        eprintf("OK\n");

    /* Test rewriting property 'test' as mpr_list property. */
    eprintf("Test 28: rewriting 'test' as mpr_list property... ");
    mpr_list set_list = mpr_dev_get_sigs(dev, MPR_DIR_ANY);
    mpr_obj_set_prop(sig, MPR_PROP_EXTRA, "test", 1, MPR_LIST, set_list, 1);
    seen = check_keys(sig);
    if (seen != (SEEN_DIR | SEEN_LENGTH | SEEN_NAME | SEEN_TYPE | SEEN_UNIT
                 | SEEN_X | SEEN_Y | SEEN_TEST))
    {
        eprintf("ERROR\n");
        result = 1;
        goto cleanup;
    }
    else
        eprintf("OK\n");

    eprintf("Test 29: retrieving property 'test'... ");
    if (!mpr_obj_get_prop_by_key(sig, "test", &length, &type, &val, 0)) {
        eprintf("not found... ERROR\n");
        result = 1;
        goto cleanup;
    }
    else
        eprintf("OK\n");

    eprintf("\t checking type: %s ... ", type_name(type));
    if (type != MPR_LIST) {
        eprintf("ERROR (expected '%s')\n", type_name(MPR_LIST));
        result = 1;
        goto cleanup;
    }
    else
        eprintf("OK\n");

    eprintf("\t checking length: %d ...", length);
    if (length != 1) {
        eprintf("ERROR (expected %d)\n", 1);
        result = 1;
        goto cleanup;
    }
    else
        eprintf("OK\n");

    mpr_list read_list = (mpr_list)val;
    eprintf("\t checking value: %p ... ", read_list);
    matched = 0;
    // for (i = 0; i < 2; i++) {
    //     if (read_ptrs[i] == set_ptrs[i])
    //         ++matched;
    // }
    // if (matched != 2) {
    //     eprintf("ERROR (expected [%p, %p])\n", set_ptrs[0], set_ptrs[1]);
    //     result = 1;
    //     goto cleanup;
    // }
    // else
        eprintf("OK\n");

  cleanup:
    if (dev) mpr_dev_free(dev);
    if (!verbose)
        printf("..................................................");
    printf("Test %s\x1B[0m.\n", result ? "\x1B[31mFAILED" : "\x1B[32mPASSED");
    return result;
}
