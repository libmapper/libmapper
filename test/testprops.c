#include <mapper/mapper.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>

/* Test to ensure that setting and getting properties of signals and devices is consistent. */

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

static void eprintf(const char *format, ...)
{
    va_list args;
    if (!verbose)
        return;
    va_start(args, format);
    vprintf(format, args);
    va_end(args);
}

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

/* Code to return a key's "seen" code, to mark whether we've seen a value. */
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
    for (i = 0; i < len; i++) {
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
    int i = 0, seen = 0, length;
    while (mpr_obj_get_prop_by_idx(obj, i++, &key, &length, &type, &val, 0)) {
        seen |= seen_code(key);
    }
    return seen;
}

int main(int argc, char **argv)
{
    int i, j, int_val, seen, length, result = 0;
    int int_array[] = {1, 2, 3, 4, 5};
    mpr_dev dev;
    mpr_sig sig;
    float flt_val, flt_array[] = {10., 20., 30., 40., 50.};
    const char *str = "test_value", *str_val, *str_array[] = {"foo", "bar"};
    mpr_type type;
    const void *val, *ptr_val = (const void*)0x9813;
    const void *ptr_array[] = {(const void*)0x1111, (const void*)0x2222};
    mpr_obj read_obj;
    mpr_list read_list, check_list;

    /* process flags for -v verbose, -h help */
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

    dev = mpr_dev_new("testprops", 0);
    sig = (mpr_obj)mpr_sig_new(dev, MPR_DIR_IN, "test", 3, MPR_FLT,
                               "Hz", NULL, NULL, NULL, NULL, 0);

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
    flt_val = 35.0;
    mpr_obj_set_prop(sig, MPR_PROP_MAX, NULL, 1, MPR_FLT, &flt_val, 1);
    seen = check_keys(sig);
    if (seen != (SEEN_DIR | SEEN_LENGTH | SEEN_NAME | SEEN_TYPE | SEEN_UNIT | SEEN_MAX)) {
        eprintf("ERROR\n");
        result = 1;
        goto cleanup;
    }
    else
        eprintf("OK\n");

    /* Test that adding an extra parameter causes the extra parameter to be listed. */
    eprintf("Test 3:  adding extra string property 'test'... ");
    mpr_obj_set_prop(sig, MPR_PROP_EXTRA, "test", 1, MPR_STR, str, 1);
    seen = check_keys(sig);
    if (seen != (SEEN_DIR | SEEN_LENGTH | SEEN_NAME | SEEN_TYPE | SEEN_UNIT | SEEN_MAX | SEEN_TEST)) {
        eprintf("ERROR\n");
        result = 1;
        goto cleanup;
    }
    else
        eprintf("OK\n");

    eprintf("Test 4:  retrieving property 'test'... ");
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

    eprintf("Test 5:  retrieving property 'test' using string getter... ");
    str_val = mpr_obj_get_prop_as_str(sig, MPR_PROP_EXTRA, "test");
    if (!str_val) {
        eprintf("ERROR\n");
        result = 1;
        goto cleanup;
    }
    eprintf("OK\n");

    eprintf("\t checking value: '%s' ... ", str_val);
    if (strcmp(str_val, str)) {
        eprintf("ERROR (expected '%s')\n", str);
        result = 1;
        goto cleanup;
    }
    else
        eprintf("OK\n");

    /* Test that removing an extra parameter causes the extra parameter to _not_ be listed. */
    eprintf("Test 6:  removing extra property 'test'... ");
    mpr_obj_remove_prop(sig, MPR_PROP_EXTRA, "test");
    seen = check_keys(sig);
    if (seen != (SEEN_DIR | SEEN_LENGTH | SEEN_NAME | SEEN_TYPE | SEEN_UNIT | SEEN_MAX)) {
        eprintf("ERROR\n");
        result = 1;
        goto cleanup;
    }
    else
        eprintf("OK\n");

    /* Test that adding two more properties works as expected. */
    eprintf("Test 7:  adding extra integer properties 'x' and 'y'... ");
    int_val = 123;
    mpr_obj_set_prop(sig, MPR_PROP_EXTRA, "x", 1, MPR_INT32, &int_val, 1);
    int_val = 234;
    mpr_obj_set_prop(sig, MPR_PROP_EXTRA, "y", 1, MPR_INT32, &int_val, 1);
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
    eprintf("Test 8:  retrieving property 'x'...");
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

    /* Test the type and value associated with "x". */
    eprintf("Test 9:  retrieving property 'x' using int getter...");
    int_val = mpr_obj_get_prop_as_int32(sig, MPR_PROP_EXTRA, "x");

    eprintf("\t checking value: %i ... ", int_val);
    if (int_val != 123) {
        eprintf("ERROR (expected %d)\n", 123);
        result = 1;
        goto cleanup;
    }
    else
        eprintf("OK\n");

    /* Check that there is no value associated with previously-removed "test". */
    eprintf("Test 10: retrieving removed property 'test': ");
    if (mpr_obj_get_prop_by_key(sig, "test", &length, &type, &val, 0)) {
        eprintf("found... ERROR\n");
        result = 1;
        goto cleanup;
    }
    else
        eprintf("not found... OK\n");

    /* Check that there is an integer value associated with static, required property "length". */
    eprintf("Test 11: retrieving static, required property 'length'... ");
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
    if (*(int*)val != 3) {
        eprintf("ERROR (expected %d)\n", 1);
        result = 1;
        goto cleanup;
    }
    else
        eprintf("OK\n");

    eprintf("Test 12: retrieving static, required property 'length' using int getter...\n");
    int_val = mpr_obj_get_prop_as_int32(sig, MPR_PROP_LEN, NULL);

    eprintf("\t checking value: '%d' ... ", int_val);
    if (int_val != 3) {
        eprintf("ERROR (expected %d)\n", 1);
        result = 1;
        goto cleanup;
    }
    else
        eprintf("OK\n");

    /* Check that there is a string value associated with static, required property "name". */
    eprintf("Test 13: retrieving static, required property 'name'... ");
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

    eprintf("Test 14: retrieving static, required property 'name' using string getter... ");
    str_val = mpr_obj_get_prop_as_str(sig, MPR_PROP_NAME, NULL);
    if (!str_val) {
        eprintf("not found... ERROR\n");
        result = 1;
        goto cleanup;
    }
    else
        eprintf("OK\n");

    eprintf("\t checking value: '%s' ... ", str_val);
    if (strcmp(str_val, "test")) {
        eprintf("ERROR (expected '%s')\n", str);
        result = 1;
        goto cleanup;
    }
    else
        eprintf("OK\n");

    /* Check that there is a float value associated with static, optional property "max". */
    eprintf("Test 15: retrieving static, optional property 'max'... ");
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
    if (length != 3) {
        eprintf("ERROR (expected %d)\n", 1);
        result = 1;
        goto cleanup;
    }
    else
        eprintf("OK\n");

    eprintf("\t checking value: '%f' ... ", *(float*)val);
    for (i = 0; i < 3; i++) {
        if (((float*)val)[i] != 35.0f) {
            eprintf("ERROR (expected %f)\n", 35.0f);
            result = 1;
            goto cleanup;
        }
    }
    eprintf("OK\n");

    eprintf("Test 16: retrieving static, optional property 'max' using float getter...\n");
    flt_val = mpr_obj_get_prop_as_flt(sig, MPR_PROP_MAX, NULL);

    eprintf("\t checking value: '%f' ... ", flt_val);
    if (flt_val != 35.0f) {
        eprintf("ERROR (expected %f)\n", 35.0f);
        result = 1;
        goto cleanup;
    }
    else
        eprintf("OK\n");

    /* Test that removing maximum causes it to _not_ be listed. */
    eprintf("Test 17: removing optional property 'max'... ");
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

    eprintf("Test 18: retrieving optional property 'max': ");
    if (mpr_obj_get_prop_by_key(sig, "max", &length, &type, &val, 0)) {
        eprintf("found... ERROR\n");
        result = 1;
        goto cleanup;
    }
    else
        eprintf("not found... OK\n");

    /* Test adding and retrieving an integer vector property. */
    eprintf("Test 19: adding an extra integer vector property 'test'... ");
    mpr_obj_set_prop(sig, MPR_PROP_EXTRA, "test", 5, MPR_INT32, &int_array, 1);
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

    eprintf("Test 20: retrieving vector property 'test': ");
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

    eprintf("\t checking value: [%i,%i,%i,%i,%i] ... ", ((int*)val)[0],
            ((int*)val)[1], ((int*)val)[2], ((int*)val)[3], ((int*)val)[4]);
    for (i = 0; i < 5; i++) {
        if (((int*)val)[i] != int_array[i]) {
            eprintf("ERROR (expected %i at index %d)\n", int_array[i], i);
            result = 1;
            goto cleanup;
        }
    }
    eprintf("OK\n");

    /* Test rewriting 'test' as float vector property. */
    eprintf("Test 21: rewriting 'test' as vector float property... ");
    mpr_obj_set_prop(sig, MPR_PROP_EXTRA, "test", 5, MPR_FLT, &flt_array, 1);
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

    eprintf("Test 22: retrieving property 'test'... ");
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

    eprintf("\t checking value: [%f,%f,%f,%f,%f] ... ", ((float*)val)[0],
            ((float*)val)[1], ((float*)val)[2], ((float*)val)[3], ((float*)val)[4]);
    for (i = 0; i < 5; i++) {
        if (((float*)val)[i] != flt_array[i]) {
            eprintf("ERROR (expected %f at index %d)\n", flt_array[i], i);
            result = 1;
            goto cleanup;
        }
    }
    eprintf("OK\n");

    /* Test rewriting property 'test' as string vector property. */
    eprintf("Test 23: rewriting 'test' as vector string property... ");
    mpr_obj_set_prop(sig, MPR_PROP_EXTRA, "test", 2, MPR_STR, str_array, 1);
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

    eprintf("Test 24: retrieving property 'test'... ");
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

    eprintf("\t checking value: ['%s','%s'] ... ", ((char**)val)[0], ((char**)val)[1]);
    for (i = 0; i < 2; i++) {
        if (!((char**)val)[i] || strcmp(((char**)val)[i], str_array[i])) {
            eprintf("ERROR (expected '%s' at index %d)\n", str_array[i], i);
            result = 1;
            goto cleanup;
        }
    }
    eprintf("OK\n");

    /* Test rewriting property 'test' as void* property. */
    eprintf("Test 25: rewriting 'test' as void* property... ");
    mpr_obj_set_prop(sig, MPR_PROP_EXTRA, "test", 1, MPR_PTR, ptr_val, 1);
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

    eprintf("Test 26: retrieving property 'test'... ");
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

    eprintf("\t checking value: %p ... ", (const void*)val);
    if ((const void*)val != ptr_val) {
        eprintf("ERROR (expected %p)\n", ptr_val);
        result = 1;
        goto cleanup;
    }
    else
        eprintf("OK\n");

    eprintf("Test 27: retrieving property 'test' using ptr getter... ");
    val = mpr_obj_get_prop_as_ptr(sig, MPR_PROP_EXTRA, "test");
    if (!val) {
        eprintf("not found... ERROR\n");
        result = 1;
        goto cleanup;
    }
    else
        eprintf("OK\n");

    eprintf("\t checking value: %p ... ", val);
    if (val != ptr_val) {
        eprintf("ERROR (expected %p)\n", ptr_val);
        result = 1;
        goto cleanup;
    }
    else
        eprintf("OK\n");

    /* Test rewriting property 'test' as void* property to MPR_PROP_DATA. */
    eprintf("Test 28: writing MPR_PROP_DATA as void* property... ");
    mpr_obj_set_prop(sig, MPR_PROP_DATA, NULL, 1, MPR_PTR, ptr_val, 1);
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

    eprintf("Test 29: retrieving property MPR_PROP_DATA... ");
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

    eprintf("\t checking value: %p ... ", val);
    if (val != ptr_val) {
        eprintf("ERROR (expected %p)\n", ptr_val);
        result = 1;
        goto cleanup;
    }
    else
        eprintf("OK\n");

    eprintf("Test 30: retrieving property MPR_PROP_DATA using ptr getter... ");
    val = mpr_obj_get_prop_as_ptr(sig, MPR_PROP_DATA, NULL);
    if (!val) {
        eprintf("not found... ERROR\n");
        result = 1;
        goto cleanup;
    }
    else
        eprintf("OK\n");

    eprintf("\t checking value: %p ... ", val);
    if (val != ptr_val) {
        eprintf("ERROR (expected %p)\n", ptr_val);
        result = 1;
        goto cleanup;
    }
    else
        eprintf("OK\n");

    /* Test rewriting property 'test' as void* vector property. */
    eprintf("Test 31: rewriting 'test' as vector void* property... ");
    mpr_obj_set_prop(sig, MPR_PROP_EXTRA, "test", 2, MPR_PTR, ptr_array, 1);
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

    eprintf("Test 32: retrieving property 'test'... ");
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

    eprintf("\t checking value: [%p,%p] ... ", ((const void**)val)[0], ((const void**)val)[1]);
    for (i = 0; i < 2; i++) {
        if (((const void**)val)[i] != ptr_array[i]) {
            eprintf("ERROR (expected %p at index %d)\n", ((const void**)val)[i], i);
            result = 1;
            goto cleanup;
        }
    }
    eprintf("OK\n");

    /* Test rewriting property 'test' as mpr_obj property. */
    eprintf("Test 33: rewriting 'test' as mpr_obj property... ");
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

    eprintf("Test 34: retrieving property 'test'... ");
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

    eprintf("\t checking value: %p ... ", (mpr_obj)val);
    if ((mpr_obj)val != sig) {
        eprintf("ERROR (expected %p)\n", sig);
        result = 1;
        goto cleanup;
    }
    else
        eprintf("OK\n");

    eprintf("Test 35: retrieving property 'test' using object getter... ");
    read_obj = mpr_obj_get_prop_as_obj(sig, MPR_PROP_EXTRA, "test");
    if (!read_obj) {
        eprintf("not found... ERROR\n");
        result = 1;
        goto cleanup;
    }
    else
        eprintf("OK\n");

    eprintf("\t checking value: %p ... ", read_obj);
    if (read_obj != sig) {
        eprintf("ERROR (expected %p)\n", sig);
        result = 1;
        goto cleanup;
    }
    else
        eprintf("OK\n");

    eprintf("Test 36: retrieving property 'signal'... ");
    if (!mpr_obj_get_prop_by_key((mpr_obj)dev, "signal", &length, &type, &val, 0)) {
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

    read_list = (mpr_list)val;
    check_list = mpr_dev_get_sigs(dev, MPR_DIR_ANY);
    eprintf("\t checking value: %p ... ", read_list);
    if (read_list == check_list) {
        eprintf("ERROR (expected copy)\n");
        result = 1;
        goto cleanup;
    }
    while (check_list && read_list) {
        if (*check_list != *read_list) {
            eprintf("ERROR (list element mismatch)\n");
            result = 1;
            goto cleanup;
        }
        check_list = mpr_list_get_next(check_list);
        read_list = mpr_list_get_next(read_list);
    }
    if (read_list || check_list) {
        eprintf("ERROR (list length mismatch %p, %p)\n", read_list, check_list);
        result = 1;
        goto cleanup;
    }
    eprintf("OK\n");

    eprintf("Test 37: retrieving property 'signal' using list getter... ");
    read_list = mpr_obj_get_prop_as_list((mpr_obj)dev, MPR_PROP_SIG, NULL);
    if (!read_list) {
        eprintf("not found... ERROR\n");
        result = 1;
        goto cleanup;
    }
    else
        eprintf("OK\n");

    /* mpr_obj_get_prop_as_list returns a copy of the list, so we will need to compare the contents */
    eprintf("\t checking value: %p ... ", read_list);
    check_list = mpr_dev_get_sigs(dev, MPR_DIR_ANY);
    while (check_list && read_list) {
        if (*check_list != *read_list) {
            eprintf("ERROR (list element mismatch)\n");
            result = 1;
            goto cleanup;
        }
        check_list = mpr_list_get_next(check_list);
        read_list = mpr_list_get_next(read_list);
    }
    if (read_list || check_list) {
        eprintf("ERROR (list length mismatch)\n");
        result = 1;
        goto cleanup;
    }
    else
        eprintf("OK\n");

  cleanup:
    if (dev) mpr_dev_free(dev);
    if (!verbose)
        printf("..................................................");
    printf("Test %s\x1B[0m.\n", result ? "\x1B[31mFAILED" : "\x1B[32mPASSED");
    return result;
}
