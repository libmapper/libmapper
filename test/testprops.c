#include <mapper/mapper.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>

/* Test to ensure that setting and getting properties of signals and devices is consistent. */

#define SEEN_DATA     0x0001
#define SEEN_DEVNAME  0x0002
#define SEEN_DIR      0x0004
#define SEEN_LENGTH   0x0008
#define SEEN_MAX      0x0010
#define SEEN_MIN      0x0020
#define SEEN_NAME     0x0040
#define SEEN_SECRET   0x0080
#define SEEN_TEST     0x0100
#define SEEN_TYPE     0x0208
#define SEEN_UNIT     0x0400
#define SEEN_X        0x0800
#define SEEN_Y        0x1000

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
        { "data",        SEEN_DATA },
        { "device_name", SEEN_DEVNAME },
        { "direction",   SEEN_DIR },
        { "length",      SEEN_LENGTH },
        { "max",         SEEN_MAX },
        { "min",         SEEN_MIN },
        { "name",        SEEN_NAME },
        { "secret",      SEEN_SECRET },
        { "test",        SEEN_TEST },
        { "type",        SEEN_TYPE },
        { "unit",        SEEN_UNIT },
        { "x",           SEEN_X },
        { "y",           SEEN_Y },
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
    const void *value;
    mpr_type type;
    int i = 0, seen = 0, length;
    while (mpr_obj_get_prop_by_idx(obj, (mpr_prop)i++, &key, &length, &type, &value, 0)) {
        seen |= seen_code(key);
    }
    return seen;
}

int main(int argc, char **argv)
{
    int i, j, int_value, seen, length, public, result = 0;
    int int_array[] = {1, 2, 3, 4, 5};
    mpr_graph graph;
    mpr_dev dev, remote_dev;
    mpr_sig sig;
    float flt_value, flt_array[] = {10., 20., 30., 40., 50.};
    double dbl_value;
    const char *str = "test_value", *str_value, *str_array[] = {"foo", "bar"};
    mpr_type type;
    const void *value, *ptr_value = (const void*)0x9813;
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

    graph = mpr_graph_new(MPR_OBJ);
    dev = mpr_dev_new("testprops", 0);
    sig = (mpr_obj)mpr_sig_new(dev, MPR_DIR_IN, "test", 3, MPR_FLT,
                               "Hz", NULL, NULL, NULL, NULL, 0);

    while (!mpr_dev_get_is_ready(dev)) {
        mpr_dev_poll(dev, 100);
        mpr_graph_poll(graph, 100);
    }

    /* get a non-local copy of the device */
    do {
        mpr_dev_poll(dev, 100);
        mpr_graph_poll(graph, 100);
        remote_dev = mpr_graph_get_obj(graph,
                                       mpr_obj_get_prop_as_int64((mpr_obj)dev, MPR_PROP_ID, NULL),
                                       MPR_DEV);
    } while (!remote_dev);

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
    flt_value = 35.0;
    mpr_obj_set_prop(sig, MPR_PROP_MAX, NULL, 1, MPR_FLT, &flt_value, 1);
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
    if (!mpr_obj_get_prop_by_key(sig, "test", &length, &type, &value, &public)) {
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

    eprintf("\t checking value: '%s' ... ", (char*)value);
    if (strcmp((char*)value, str)) {
        eprintf("ERROR (expected '%s')\n", str);
        result = 1;
        goto cleanup;
    }
    else
        eprintf("OK\n");

    eprintf("\t checking whether property is public: %d ...", public);
    if (public != 1) {
        eprintf("ERROR (expected %d)\n", 1);
        result = 1;
        goto cleanup;
    }
    else
        eprintf("OK\n");

    eprintf("Test 5:  retrieving property 'test' using string getter... ");
    str_value = mpr_obj_get_prop_as_str(sig, MPR_PROP_EXTRA, "test");
    if (!str_value) {
        eprintf("ERROR\n");
        result = 1;
        goto cleanup;
    }
    eprintf("OK\n");

    eprintf("\t checking value: '%s' ... ", str_value);
    if (strcmp(str_value, str)) {
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
    int_value = 123;
    mpr_obj_set_prop(sig, MPR_PROP_EXTRA, "x", 1, MPR_INT32, &int_value, 1);
    int_value = 234;
    mpr_obj_set_prop(sig, MPR_PROP_EXTRA, "y", 1, MPR_INT32, &int_value, 1);
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
    if (!mpr_obj_get_prop_by_key(sig, "x", &length, &type, &value, &public)) {
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

    eprintf("\t checking value: %i ... ", *(int*)value);
    if (*(int*)value != 123) {
        eprintf("ERROR (expected %d)\n", 123);
        result = 1;
        goto cleanup;
    }
    else
        eprintf("OK\n");

    eprintf("\t checking whether property is public: %d ...", public);
    if (public != 1) {
        eprintf("ERROR (expected %d)\n", 1);
        result = 1;
        goto cleanup;
    }
    else
        eprintf("OK\n");

    /* Test the type and value associated with "x". */
    eprintf("Test 9:  retrieving property 'x' using int getter...");
    int_value = mpr_obj_get_prop_as_int32(sig, MPR_PROP_EXTRA, "x");

    eprintf("\t checking value: %i ... ", int_value);
    if (int_value != 123) {
        eprintf("ERROR (expected %d)\n", 123);
        result = 1;
        goto cleanup;
    }
    else
        eprintf("OK\n");

    /* Check that there is no value associated with previously-removed "test". */
    eprintf("Test 10: retrieving removed property 'test': ");
    if (mpr_obj_get_prop_by_key(sig, "test", &length, &type, &value, &public)) {
        eprintf("found... ERROR\n");
        result = 1;
        goto cleanup;
    }
    else
        eprintf("not found... OK\n");

    /* Check that there is an integer value associated with static, required property "length". */
    eprintf("Test 11: retrieving static, required property 'length'... ");
    if (!mpr_obj_get_prop_by_key(sig, "length", &length, &type, &value, &public)) {
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

    eprintf("\t checking value: '%d' ... ", *(int*)value);
    if (*(int*)value != 3) {
        eprintf("ERROR (expected %d)\n", 1);
        result = 1;
        goto cleanup;
    }
    else
        eprintf("OK\n");

    eprintf("\t checking whether property is public: %d ...", public);
    if (public != 1) {
        eprintf("ERROR (expected %d)\n", 1);
        result = 1;
        goto cleanup;
    }
    else
        eprintf("OK\n");

    eprintf("Test 12: retrieving static, required property 'length' using int getter...\n");
    int_value = mpr_obj_get_prop_as_int32(sig, MPR_PROP_LEN, NULL);

    eprintf("\t checking value: '%d' ... ", int_value);
    if (int_value != 3) {
        eprintf("ERROR (expected %d)\n", 1);
        result = 1;
        goto cleanup;
    }
    else
        eprintf("OK\n");

    /* Check that there is a string value associated with static, required property "name". */
    eprintf("Test 13: retrieving static, required property 'name'... ");
    if (!mpr_obj_get_prop_by_idx(sig, MPR_PROP_NAME, NULL, &length, &type, &value, &public)) {
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

    eprintf("\t checking value: '%s' ... ", (char*)value);
    if (strcmp((char*)value, "test")) {
        eprintf("ERROR (expected '%s')\n", str);
        result = 1;
        goto cleanup;
    }
    else
        eprintf("OK\n");

    eprintf("\t checking whether property is public: %d ...", public);
    if (public != 1) {
        eprintf("ERROR (expected %d)\n", 1);
        result = 1;
        goto cleanup;
    }
    else
        eprintf("OK\n");

    eprintf("Test 14: retrieving static, required property 'name' using string getter... ");
    str_value = mpr_obj_get_prop_as_str(sig, MPR_PROP_NAME, NULL);
    if (!str_value) {
        eprintf("not found... ERROR\n");
        result = 1;
        goto cleanup;
    }
    else
        eprintf("OK\n");

    eprintf("\t checking value: '%s' ... ", str_value);
    if (strcmp(str_value, "test")) {
        eprintf("ERROR (expected '%s')\n", str);
        result = 1;
        goto cleanup;
    }
    else
        eprintf("OK\n");

    /* Check that there is a float value associated with static, optional property "max". */
    eprintf("Test 15: retrieving static, optional property 'max'... ");
    if (!mpr_obj_get_prop_by_idx(sig, MPR_PROP_MAX, NULL, &length, &type, &value, &public)) {
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

    eprintf("\t checking value: '%f' ... ", *(float*)value);
    if (*(float*)value != 35.0f) {
        eprintf("ERROR (expected %f)\n", 35.0f);
        result = 1;
        goto cleanup;
    }
    eprintf("OK\n");

    eprintf("\t checking whether property is public: %d ...", public);
    if (public != 1) {
        eprintf("ERROR (expected %d)\n", 1);
        result = 1;
        goto cleanup;
    }
    else
        eprintf("OK\n");

    eprintf("Test 16: retrieving static, optional property 'max' using float getter...\n");
    flt_value = mpr_obj_get_prop_as_flt(sig, MPR_PROP_MAX, NULL);

    eprintf("\t checking value: '%f' ... ", flt_value);
    if (flt_value != 35.0f) {
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
    if (mpr_obj_get_prop_by_key(sig, "max", &length, &type, &value, &public)) {
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
    if (!mpr_obj_get_prop_by_key(sig, "test", &length, &type, &value, &public)) {
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

    eprintf("\t checking value: [%i,%i,%i,%i,%i] ... ", ((int*)value)[0],
            ((int*)value)[1], ((int*)value)[2], ((int*)value)[3], ((int*)value)[4]);
    for (i = 0; i < 5; i++) {
        if (((int*)value)[i] != int_array[i]) {
            eprintf("ERROR (expected %i at index %d)\n", int_array[i], i);
            result = 1;
            goto cleanup;
        }
    }
    eprintf("OK\n");

    eprintf("\t checking whether property is public: %d ...", public);
    if (public != 1) {
        eprintf("ERROR (expected %d)\n", 1);
        result = 1;
        goto cleanup;
    }
    else
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
    if (!mpr_obj_get_prop_by_key(sig, "test", &length, &type, &value, &public)) {
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

    eprintf("\t checking value: [%f,%f,%f,%f,%f] ... ", ((float*)value)[0],
            ((float*)value)[1], ((float*)value)[2], ((float*)value)[3], ((float*)value)[4]);
    for (i = 0; i < 5; i++) {
        if (((float*)value)[i] != flt_array[i]) {
            eprintf("ERROR (expected %f at index %d)\n", flt_array[i], i);
            result = 1;
            goto cleanup;
        }
    }
    eprintf("OK\n");

    eprintf("\t checking whether property is public: %d ...", public);
    if (public != 1) {
        eprintf("ERROR (expected %d)\n", 1);
        result = 1;
        goto cleanup;
    }
    else
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
    if (!mpr_obj_get_prop_by_key(sig, "test", &length, &type, &value, &public)) {
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

    eprintf("\t checking value: ['%s','%s'] ... ", ((char**)value)[0], ((char**)value)[1]);
    for (i = 0; i < 2; i++) {
        if (!((char**)value)[i] || strcmp(((char**)value)[i], str_array[i])) {
            eprintf("ERROR (expected '%s' at index %d)\n", str_array[i], i);
            result = 1;
            goto cleanup;
        }
    }
    eprintf("OK\n");

    eprintf("\t checking whether property is public: %d ...", public);
    if (public != 1) {
        eprintf("ERROR (expected %d)\n", 1);
        result = 1;
        goto cleanup;
    }
    else
        eprintf("OK\n");

    /* Test rewriting property 'test' as void* property. */
    eprintf("Test 25: rewriting 'test' as void* property... ");
    mpr_obj_set_prop(sig, MPR_PROP_EXTRA, "test", 1, MPR_PTR, ptr_value, 1);
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
    if (!mpr_obj_get_prop_by_key(sig, "test", &length, &type, &value, &public)) {
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

    eprintf("\t checking value: %p ... ", (const void*)value);
    if ((const void*)value != ptr_value) {
        eprintf("ERROR (expected %p)\n", ptr_value);
        result = 1;
        goto cleanup;
    }
    else
        eprintf("OK\n");

    eprintf("\t checking whether property is public: %d ...", public);
    if (public != 1) {
        eprintf("ERROR (expected %d)\n", 1);
        result = 1;
        goto cleanup;
    }
    else
        eprintf("OK\n");

    eprintf("Test 27: retrieving property 'test' using ptr getter... ");
    value = mpr_obj_get_prop_as_ptr(sig, MPR_PROP_EXTRA, "test");
    if (!value) {
        eprintf("not found... ERROR\n");
        result = 1;
        goto cleanup;
    }
    else
        eprintf("OK\n");

    eprintf("\t checking value: %p ... ", value);
    if (value != ptr_value) {
        eprintf("ERROR (expected %p)\n", ptr_value);
        result = 1;
        goto cleanup;
    }
    else
        eprintf("OK\n");

    /* Test rewriting property 'test' as void* property to MPR_PROP_DATA. */
    eprintf("Test 28: writing MPR_PROP_DATA as void* property... ");
    /* MPR_PROP_DATA should always be private, even if user code tries to set it to public */
    /* Try setting it to public=1 to test */
    mpr_obj_set_prop(dev, MPR_PROP_DATA, NULL, 1, MPR_PTR, ptr_value, 1);
    seen = check_keys(dev);
    if (seen != (SEEN_NAME | SEEN_DATA))
    {
        eprintf("ERROR\n");
        result = 1;
        goto cleanup;
    }
    else
        eprintf("OK\n");

    eprintf("Test 29: retrieving property MPR_PROP_DATA... ");
    if (!mpr_obj_get_prop_by_idx(dev, MPR_PROP_DATA, NULL, &length, &type, &value, &public)) {
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

    eprintf("\t checking value: %p ... ", value);
    if (value != ptr_value) {
        eprintf("ERROR (expected %p)\n", ptr_value);
        result = 1;
        goto cleanup;
    }
    else
        eprintf("OK\n");

    eprintf("\t checking whether property is public: %d ...", public);
    if (public != 0) {
        eprintf("ERROR (expected %d)\n", 0);
        result = 1;
        goto cleanup;
    }
    else
        eprintf("OK\n");

    eprintf("Test 30: retrieving property MPR_PROP_DATA using ptr getter... ");
    value = mpr_obj_get_prop_as_ptr(dev, MPR_PROP_DATA, NULL);
    if (!value) {
        eprintf("not found... ERROR\n");
        result = 1;
        goto cleanup;
    }
    else
        eprintf("OK\n");

    eprintf("\t checking value: %p ... ", value);
    if (value != ptr_value) {
        eprintf("ERROR (expected %p)\n", ptr_value);
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
    if (!mpr_obj_get_prop_by_key(sig, "test", &length, &type, &value, &public)) {
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

    eprintf("\t checking value: [%p,%p] ... ", ((const void**)value)[0], ((const void**)value)[1]);
    for (i = 0; i < 2; i++) {
        if (((const void**)value)[i] != ptr_array[i]) {
            eprintf("ERROR (expected %p at index %d)\n", ((const void**)value)[i], i);
            result = 1;
            goto cleanup;
        }
    }
    eprintf("OK\n");

    eprintf("\t checking whether property is public: %d ...", public);
    if (public != 1) {
        eprintf("ERROR (expected %d)\n", 1);
        result = 1;
        goto cleanup;
    }
    else
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
    if (!mpr_obj_get_prop_by_key(sig, "test", &length, &type, &value, &public)) {
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

    eprintf("\t checking value: %p ... ", (mpr_obj)value);
    if ((mpr_obj)value != sig) {
        eprintf("ERROR (expected %p)\n", sig);
        result = 1;
        goto cleanup;
    }
    else
        eprintf("OK\n");

    eprintf("\t checking whether property is public: %d ...", public);
    if (public != 1) {
        eprintf("ERROR (expected %d)\n", 1);
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
    if (!mpr_obj_get_prop_by_key((mpr_obj)dev, "signal", &length, &type, &value, &public)) {
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

    read_list = (mpr_list)value;
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

    eprintf("\t checking whether property is public: %d ...", public);
    if (public != 1) {
        eprintf("ERROR (expected %d)\n", 1);
        result = 1;
        goto cleanup;
    }
    else
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

    eprintf("Test 38: trying to remove static property 'length'... ");
    if (mpr_obj_remove_prop((mpr_obj)sig, MPR_PROP_LEN, NULL)) {
        eprintf("removed... ERROR\n");
        result = 1;
        goto cleanup;
    }
    else
        eprintf("OK\n");

    eprintf("Adding a private property to a remote object... ");
    int_value = 12345;
    mpr_obj_set_prop((mpr_obj)remote_dev, MPR_PROP_EXTRA, "secret", 1, MPR_INT32, &int_value, 0);
    /* this shouldn't do anything for a private property */
    mpr_obj_push((mpr_obj)remote_dev);

    eprintf("Test 39: trying to retrieve a private property to a remote object... ");
    if (!mpr_obj_get_prop_by_key((mpr_obj)remote_dev, "secret", &length, &type, &value, &public)) {
        eprintf("ERROR\n");
        result = 1;
        goto cleanup;
    }
    eprintf("OK\n");

    eprintf("\t checking type: %s ... ", type_name(type));
    if (type != MPR_INT32) {
        eprintf("ERROR (expected '%s')\n", type_name(MPR_INT32));
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

    eprintf("\t checking value: %d ...", *(int*)value);
    if (*(int*)value != 12345) {
        eprintf("ERROR (expected %d)\n", 12345);
        result = 1;
        goto cleanup;
    }
    else
        eprintf("OK\n");

    eprintf("\t checking whether property is public: %d ...", public);
    if (public != 0) {
        eprintf("ERROR (expected %d)\n", 0);
        result = 1;
        goto cleanup;
    }
    else
        eprintf("OK\n");

    mpr_dev_poll(dev, 100);
    mpr_graph_poll(graph, 100);
    mpr_dev_poll(dev, 100);

    /* Check that private properties are not synced across the graph */
    eprintf("test 40: checking that private properties are not synced local -> remote ");
    seen = check_keys(remote_dev);
    if (seen & (SEEN_DATA)) {
        eprintf("ERROR\n");
        result = 1;
        goto cleanup;
    }
    else
        eprintf("OK\n");

    eprintf("test 41: checking that private properties are not synced remote -> local ");
    seen = check_keys(dev);
    if (seen & (SEEN_SECRET)) {
        eprintf("ERROR\n");
        result = 1;
        goto cleanup;
    }
    else
        eprintf("OK\n");

    /* Try removing a private property from a remote object. */
    eprintf("Test 42: removing private property 'secret'... ");
    mpr_obj_remove_prop(remote_dev, MPR_PROP_EXTRA, "secret");
    seen = check_keys(remote_dev);
    if (seen & (SEEN_SECRET)) {
        eprintf("ERROR\n");
        result = 1;
        goto cleanup;
    }
    else
        eprintf("OK\n");

    eprintf("Test 43: retrieving mpr_time property 'synced'... ");
    if (!mpr_obj_get_prop_by_idx(remote_dev, MPR_PROP_SYNCED, NULL, &length, &type, &value, &public)) {
        eprintf("ERROR (not found)\n");
        result = 1;
        goto cleanup;
    }
    else if (verbose) {
        eprintf("%g... OK\n", mpr_time_as_dbl(*(mpr_time*)value));
    }

    eprintf("Test 44: setting read-only mpr_time property 'synced'... ");
    if (mpr_obj_set_prop(remote_dev, MPR_PROP_SYNCED, NULL, 1, MPR_FLT, &flt_value, 1)) {
        eprintf("ERROR (succeeded)\n");
        result = 1;
        goto cleanup;
    }
    else
        eprintf("OK\n");

    /* Check typed getters for mpr_time properties */
    dbl_value = mpr_obj_get_prop_as_dbl(remote_dev, MPR_PROP_SYNCED, NULL);
    eprintf("Test 45: double typed getter for mpr_time: %g... ", dbl_value);
    if (dbl_value != mpr_time_as_dbl(*(mpr_time*)value)) {
        eprintf("ERROR (expected %g)\n", mpr_time_as_dbl(*(mpr_time*)value));
        result = 1;
        goto cleanup;
    }
    else
        eprintf("OK\n");

    flt_value = mpr_obj_get_prop_as_flt(remote_dev, MPR_PROP_SYNCED, NULL);
    eprintf("Test 46: float typed getter for mpr_time: %g... ", flt_value);
    if (flt_value != (float)mpr_time_as_dbl(*(mpr_time*)value)) {
        eprintf("ERROR (expected %g)\n", (float)mpr_time_as_dbl(*(mpr_time*)value));
        result = 1;
        goto cleanup;
    }
    else
        eprintf("OK\n");

    int_value = mpr_obj_get_prop_as_int32(remote_dev, MPR_PROP_SYNCED, NULL);
    eprintf("Test 47: int typed getter for mpr_time: %u... ", int_value);
    if (int_value != ((mpr_time*)value)->sec) {
        eprintf("ERROR (expected %d)\n", ((mpr_time*)value)->sec);
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
