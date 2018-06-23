#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>

#include "types_internal.h"
#include "mapper_internal.h"

#ifdef DEBUG
#define TRACING 0 /* Set non-zero to see parsed properties. */
#else
#define TRACING 0
#endif

/* length = 0 indicates variable length. */
typedef struct {
    const char *name;
    int len;
    mapper_type store_type;
    mapper_type protocol_type;
} static_prop_t;

/* Warning! This table needs to be kept synchonised with mapper_property enum
 * found in mapper_constants.h */
const static_prop_t static_props[] = {
    { 0,                   0, 0,             0 },             /* MAPPER_PROP_UNKNOWN */
    { "@calibrating",      1, MAPPER_BOOL,   MAPPER_BOOL },   /* MAPPER_PROP_CALIB */
    { "@device",           1, MAPPER_DEVICE, MAPPER_STRING }, /* MAPPER_PROP_DEVICE */
    { "@direction",        1, MAPPER_INT32,  MAPPER_STRING }, /* MAPPER_PROP_DIR */
    { "@expression",       1, MAPPER_STRING, MAPPER_STRING }, /* MAPPER_PROP_EXPR */
    { "@host",             1, MAPPER_STRING, MAPPER_STRING }, /* MAPPER_PROP_HOST */
    { "@id",               1, MAPPER_INT64,  MAPPER_INT64 },  /* MAPPER_PROP_ID */
    { "@instance",         1, MAPPER_INT32,  MAPPER_INT32 },  /* MAPPER_PROP_INSTANCE */
    { "@is_local",         1, MAPPER_BOOL,   MAPPER_BOOL },   /* MAPPER_PROP_IS_LOCAL */
    { "@jitter",           1, MAPPER_FLOAT,  MAPPER_FLOAT },  /* MAPPER_PROP_JITTER */
    { "@length",           1, MAPPER_INT32,  MAPPER_INT32 },  /* MAPPER_PROP_LENGTH */
    { "@lib_version",      1, MAPPER_STRING, MAPPER_STRING }, /* MAPPER_PROP_LIB_VERSION */
    { "@max",              0, 'n',           'n' },           /* MAPPER_PROP_MAX */
    { "@min",              0, 'n',           'n' },           /* MAPPER_PROP_MIN */
    { "@muted",            1, MAPPER_BOOL,   MAPPER_BOOL },   /* MAPPER_PROP_MUTED */
    { "@name",             1, MAPPER_STRING, MAPPER_STRING }, /* MAPPER_PROP_NAME */
    { "@num_inputs",       1, MAPPER_INT32,  MAPPER_INT32 },  /* MAPPER_PROP_NUM_INPUTS */
    { "@num_instances",    1, MAPPER_INT32,  MAPPER_INT32 },  /* MAPPER_PROP_NUM_INSTANCES */
    { "@num_maps",         2, MAPPER_INT32,  MAPPER_INT32 },  /* MAPPER_PROP_NUM_MAPS */
    { "@num_maps_in",      1, MAPPER_INT32,  MAPPER_INT32 },  /* MAPPER_PROP_NUM_MAPS_IN */
    { "@num_maps_out",     1, MAPPER_INT32,  MAPPER_INT32 },  /* MAPPER_PROP_NUM_MAPS_OUT */
    { "@num_outputs",      1, MAPPER_INT32,  MAPPER_INT32 },  /* MAPPER_PROP_NUM_OUTPUTS */
    { "@ordinal",          1, MAPPER_INT32,  MAPPER_INT32 },  /* MAPPER_PROP_ORDINAL */
    { "@period",           1, MAPPER_FLOAT,  MAPPER_FLOAT },  /* MAPPER_PROP_PERIOD */
    { "@port",             1, MAPPER_INT32,  MAPPER_INT32 },  /* MAPPER_PROP_PORT */
    { "@process_location", 1, MAPPER_INT32,  MAPPER_STRING }, /* MAPPER_PROP_PROCESS_LOC */
    { "@protocol",         1, MAPPER_INT32,  MAPPER_STRING }, /* MAPPER_PROP_PROTOCOL */
    { "@rate",             1, MAPPER_FLOAT,  MAPPER_FLOAT },  /* MAPPER_PROP_RATE */
    { "@scope",            0, MAPPER_DEVICE, MAPPER_STRING }, /* MAPPER_PROP_SCOPE */
    { "@signal",           0, MAPPER_SIGNAL, MAPPER_STRING }, /* MAPPER_PROP_SIGNAL */
    { "@slot",             0, MAPPER_INT32,  MAPPER_INT32 },  /* MAPPER_PROP_SLOT */
    { "@status",           1, MAPPER_INT32,  MAPPER_INT32 },  /* MAPPER_PROP_STATUS */
    { "@synced",           1, MAPPER_TIME,   MAPPER_TIME },   /* MAPPER_PROP_SYNCED */
    { "@type",             1, MAPPER_CHAR,   MAPPER_CHAR },   /* MAPPER_PROP_TYPE */
    { "@unit",             1, MAPPER_STRING, MAPPER_STRING }, /* MAPPER_PROP_UNIT */
    { "@use_instances",    1, MAPPER_BOOL,   MAPPER_BOOL },   /* MAPPER_PROP_USE_INSTANCES */
    { "@user_data",        1, MAPPER_PTR,    0  },            /* MAPPER_PROP_USER_DATA */
    { "@version",          1, MAPPER_INT32,  MAPPER_INT32 },  /* MAPPER_PROP_VERSION */
    { "@extra",            0, 'a', 'a' },  /* MAPPER_PROP_EXTRA (special case, does not
                                             * represent a specific property name) */
};

const char* mapper_loc_strings[] =
{
    NULL,           /* MAPPER_LOC_UNDEFINED */
    "src",          /* MAPPER_LOC_SRC */
    "dst",          /* MAPPER_LOC_DST */
    "any",          /* MAPPER_LOC_ANY */
};

const char* mapper_protocol_strings[] =
{
    NULL,           /* MAPPER_PROTO_UNDEFINED */
    "osc.udp",      /* MAPPER_PROTO_UDP */
    "osc.tcp",      /* MAPPER_PROTO_TCP */
};

int mapper_parse_names(const char *string, char **devnameptr, char **signameptr)
{
    if (!string)
        return 0;
    const char *devname = skip_slash(string);
    if (!devname || devname[0] == '/')
        return 0;
    if (devnameptr)
        *devnameptr = (char*) devname;
    char *signame = strchr(devname+1, '/');
    if (!signame) {
        if (signameptr)
            *signameptr = 0;
        return strlen(devname);
    }
    if (!++signame) {
        if (signameptr)
            *signameptr = 0;
        return strlen(devname)-1;
    }
    if (signameptr)
        *signameptr = signame;
    return (signame - devname - 1);
}

mapper_msg mapper_msg_parse_props(int argc, const mapper_type *types, lo_arg **argv)
{
    int i, slot_idx, num_props=0;
    // get the number of props
    for (i = 0; i < argc; i++) {
        if (types[i] != MAPPER_STRING)
            continue;
        if  (argv[i]->s == '@' || (strncmp(&argv[i]->s, "-@", 2)==0)
             || (strncmp(&argv[i]->s, "+@", 2)==0))
            ++num_props;
    }
    if (!num_props)
        return 0;

    mapper_msg msg = (mapper_msg) calloc(1, sizeof(struct _mapper_msg));
    msg->atoms = ((mapper_msg_atom_t*)
                  calloc(1, sizeof(struct _mapper_msg_atom) * num_props));
    mapper_msg_atom atom = &msg->atoms[0];
    const char *name;

    for (i = 0; i < argc; i++) {
        if (!type_is_str(types[i])) {
            /* property ID not a string */
#ifdef DEBUG
            trace("message item '");
            lo_arg_pp(types[i], argv[i]);
            trace("' not a string.\n");
#endif
            continue;
        }
        // new property
        if (atom->types || (atom->prop & PROP_REMOVE))
            ++msg->num_atoms;
        atom = &msg->atoms[msg->num_atoms];

        name = &argv[i]->s;
        if (strncmp(&argv[i]->s, "+@", 2)==0) {
            atom->prop = PROP_ADD;
            ++name;
        }
        else if (strncmp(&argv[i]->s, "-@", 2)==0) {
            atom->prop = PROP_REMOVE;
            ++name;
        }
        if (name[0] != '@') // not a property key
            continue;

        atom->name = name;

        // try to find matching index for static props
        if (strncmp(atom->name, "@dst@", 5)==0) {
            atom->prop |= DST_SLOT_PROP;
            atom->name += 5;
        }
        else if (strncmp(atom->name, "@src", 4)==0) {
            if (atom->name[4] == '@') {
                atom->prop |= SRC_SLOT_PROP(0);
                atom->name += 5;
            }
            else if (atom->name[4] == '.') {
                // in form 'src.<ordinal>'
                slot_idx = atoi(atom->name + 5);
                if (slot_idx >= MAX_NUM_MAP_SRC) {
                    trace("Bad slot ordinal in property '%s'.\n", atom->name);
                    atom->types = 0;
                    continue;
                }
                atom->name = strchr(atom->name + 5, '@');
                if (!atom->name || !(++atom->name)) {
                    trace("No sub-property found in name '%s'.\n", atom->name);
                    atom->types = 0;
                    continue;
                }
                atom->prop |= SRC_SLOT_PROP(slot_idx);
            }
        }
        else
            ++atom->name;

        atom->prop |= mapper_prop_from_string(atom->name);

        if (msg->num_atoms < 0)
            continue;
        atom->types = &types[i+1];
        atom->vals = &argv[i+1];
        while (++i < argc) {
            if ((types[i] == MAPPER_STRING)
                && strspn(&argv[i]->s, "+-@")) {
                /* Arrived at next property index. */
                --i;
                break;
            }
            else if (!type_match(types[i], atom->types[0])) {
                trace("Value vector for key '%s' has heterogeneous types.\n",
                      atom->name);
                atom->len = 0;
                atom->types = 0;
                break;
            }
            else
                ++atom->len;
        }
        if (!atom->len) {
            atom->types = 0;
            if (!(atom->prop & PROP_REMOVE)) {
                trace("Key '%s' has no values.\n", atom->name);
                continue;
            }
        }
        // check type against static props
        if (MASK_PROP_BITFLAGS(atom->prop) < MAPPER_PROP_EXTRA) {
            static_prop_t prop;
            prop = static_props[PROP_TO_INDEX(atom->prop)];
            if (prop.len) {
                if (prop.len != atom->len) {
                    trace("Static property '%s' cannot have length %d.\n",
                          static_props[PROP_TO_INDEX(atom->prop)].name,
                          atom->len);
                    atom->len = 0;
                    atom->types = 0;
                    continue;
                }
                if (atom->prop & (PROP_ADD | PROP_REMOVE)) {
                    trace("Cannot add or remove values from static property '%s'.\n",
                          static_props[PROP_TO_INDEX(atom->prop)].name);
                    atom->len = 0;
                    atom->types = 0;
                    continue;
                }
            }
            if (!prop.protocol_type) {
                trace("Static property '%s' cannot be set by message.\n",
                      static_props[PROP_TO_INDEX(atom->prop)].name);
                atom->len = 0;
                atom->types = 0;
                continue;
            }
            if (prop.protocol_type == 'n') {
                if (!type_is_num(atom->types[0])) {
                    trace("Static property '%s' cannot have type '%c' (2).\n",
                          static_props[PROP_TO_INDEX(atom->prop)].name,
                          atom->types[0]);
                    atom->len = 0;
                    atom->types = 0;
                    continue;
                }
            }
            else if (prop.protocol_type == MAPPER_BOOL) {
                if (!type_is_bool(atom->types[0])) {
                    trace("Static property '%s' cannot have type '%c' (2).\n",
                          static_props[PROP_TO_INDEX(atom->prop)].name,
                          atom->types[0]);
                    atom->len = 0;
                    atom->types = 0;
                    continue;
                }
            }
            else if (prop.protocol_type != atom->types[0]) {
                trace("Static property '%s' cannot have type '%c' (1).\n",
                      static_props[PROP_TO_INDEX(atom->prop)].name,
                      atom->types[0]);
                atom->len = 0;
                atom->types = 0;
                continue;
            }
        }
    }
    // reset last atom if no types unless "remove" flag is set
    if (atom->types || atom->prop & PROP_REMOVE)
        ++msg->num_atoms;
    else {
        atom->name = 0;
        atom->len = 0;
        atom->vals = 0;
    }
#if TRACING
    // print out parsed properties
    printf("%d parsed mapper_msgs:\n", msg->num_atoms);
    for (i = 0; i < msg->num_atoms; i++) {
        atom = &msg->atoms[i];
        if (atom->prop & PROP_ADD)
            printf(" +");
        else if (atom->prop & PROP_REMOVE)
            printf(" -");
        else
            printf("  ");
        if (atom->prop & DST_SLOT_PROP)
            printf("'dst/%s' [%d]: ", atom->name, atom->prop);
        else if (atom->prop >> SRC_SLOT_PROP_BIT_OFFSET)
            printf("'src%d/%s' [%d]: ", SRC_SLOT(atom->prop), atom->name,
                   atom->prop);
        else
            printf("'%s' [%d]: ", atom->name, atom->prop);
        int j;
        for (j = 0; j < atom->len; j++) {
            lo_arg_pp(atom->types[j], atom->vals[j]);
            printf(", ");
        }
        printf("\b\b \n");
    }
#endif
    return msg;
}

void mapper_msg_free(mapper_msg msg)
{
    if (msg) {
        if (msg->atoms)
            free(msg->atoms);
        free(msg);
    }
}

mapper_msg_atom mapper_msg_prop(mapper_msg msg, mapper_property prop)
{
    int i;
    for (i = 0; i < msg->num_atoms; i++) {
        if (msg->atoms[i].prop == prop) {
            if (!msg->atoms[i].len || !msg->atoms[i].types)
                return 0;
            return &msg->atoms[i];
        }
    }
    return 0;
}

/* helper for mapper_msg_varargs() */
void mapper_msg_add_typed_val(lo_message msg, int len, mapper_type type,
                              const void *val)
{
    int i;
    if (type && len < 1)
        return;

    switch (type) {
        case MAPPER_STRING:
        {
            if (len == 1)
                lo_message_add_string(msg, (char*)val);
            else {
                char **vals = (char**)val;
                for (i = 0; i < len; i++)
                    lo_message_add_string(msg, vals[i]);
            }
            break;
        }
        case MAPPER_FLOAT:
        {
            float *vals = (float*)val;
            for (i = 0; i < len; i++)
                lo_message_add_float(msg, vals[i]);
            break;
        }
        case MAPPER_DOUBLE:
        {
            double *vals = (double*)val;
            for (i = 0; i < len; i++)
                lo_message_add_double(msg, vals[i]);
            break;
        }
        case MAPPER_INT32:
        {
            int *vals = (int*)val;
            for (i = 0; i < len; i++)
                lo_message_add_int32(msg, vals[i]);
            break;
        }
        case MAPPER_INT64:
        {
            int64_t *vals = (int64_t*)val;
            for (i = 0; i < len; i++)
                lo_message_add_int64(msg, vals[i]);
            break;
        }
        case MAPPER_TIME:
        {
            mapper_time_t *vals = (mapper_time_t*)val;
            for (i = 0; i < len; i++)
                lo_message_add_timetag(msg, vals[i]);
            break;
        }
        case MAPPER_CHAR:
        {
            char *vals = (char*)val;
            for (i = 0; i < len; i++)
                lo_message_add_char(msg, vals[i]);
            break;
        }
        case MAPPER_BOOL: {
            int *vals = (int*)val;
            for (i = 0; i < len; i++) {
                if (vals[i])
                    lo_message_add_true(msg);
                else
                    lo_message_add_false(msg);
            }
            break;
        }
        case 0: {
            lo_message_add_nil(msg);
            break;
        }
        default:
            break;
    }
}

const char *mapper_prop_protocol_string(mapper_property prop)
{
    prop = MASK_PROP_BITFLAGS(prop);
    die_unless(prop <= MAPPER_PROP_EXTRA,
               "called mapper_prop_protocol_string() with bad index %d.\n",
               prop);
    return static_props[PROP_TO_INDEX(prop)].name;
}

const char *mapper_prop_string(mapper_property prop)
{
    prop = MASK_PROP_BITFLAGS(prop);
    die_unless(prop > MAPPER_PROP_UNKNOWN && prop <= MAPPER_PROP_EXTRA,
               "called mapper_prop_string() with bad index %d.\n",
               prop);
    return static_props[PROP_TO_INDEX(prop)].name + 1;
}

mapper_property mapper_prop_from_string(const char *string)
{
    // property names are stored alphabetically so we can use a binary search
    int beg = PROP_TO_INDEX(MAPPER_PROP_UNKNOWN) + 1;
    int end = PROP_TO_INDEX(MAPPER_PROP_EXTRA) - 1;
    int mid = (beg + end) * 0.5, cmp;
    while (beg <= end) {
        cmp = strcmp(string, static_props[mid].name + 1);
        if (cmp > 0)
            beg = mid + 1;
        else if (cmp == 0)
            return INDEX_TO_PROP(mid);
        else
            end = mid - 1;
        mid = (beg + end) * 0.5;
    }
    if (strcmp(string, "maximum")==0)
        return MAPPER_PROP_MAX;
    if (strcmp(string, "minimum")==0)
        return MAPPER_PROP_MIN;
    return MAPPER_PROP_EXTRA;
}

const char *mapper_loc_string(mapper_location loc)
{
    if (loc <= 0 || loc > MAPPER_LOC_ANY)
        return "unknown";
    return mapper_loc_strings[loc];
}

mapper_location mapper_loc_from_string(const char *str)
{
    if (!str)
        return MAPPER_LOC_UNDEFINED;
    int i;
    for (i = MAPPER_LOC_UNDEFINED+1; i < 3; i++) {
        if (strcmp(str, mapper_loc_strings[i])==0)
            return i;
    }
    return MAPPER_LOC_UNDEFINED;
}

const char *mapper_protocol_string(mapper_protocol pro)
{
    if (pro <= 0 || pro > NUM_MAPPER_PROTOCOLS)
        return "unknown";
    return mapper_protocol_strings[pro];
}

mapper_protocol mapper_protocol_from_string(const char *str)
{
    if (!str)
        return MAPPER_PROTO_UNDEFINED;
    int i;
    for (i = MAPPER_PROTO_UNDEFINED+1; i < NUM_MAPPER_PROTOCOLS; i++) {
        if (strcmp(str, mapper_protocol_strings[i])==0)
            return i;
    }
    return MAPPER_PROTO_UNDEFINED;
}

// Helper for setting property value from different lo_arg types
int set_coerced_val(int src_len, mapper_type src_type, const void *src_val,
                    int dst_len, mapper_type dst_type, void *dst_val)
{
    int i, j, min_len = src_len < dst_len ? src_len : dst_len;

    if (src_type == dst_type) {
        int size = mapper_type_size(src_type);
        do {
            memcpy(dst_val, src_val, size * min_len);
            dst_len -= min_len;
            if (dst_len < min_len)
                min_len = dst_len;
        } while (dst_len);
        return 0;
    }

    switch (dst_type) {
        case MAPPER_FLOAT:{
            float *dstf = (float*)dst_val;
            switch (src_type) {
                case MAPPER_INT32: {
                    int *srci = (int*)src_val;
                    for (i = 0, j = 0; i < dst_len; i++, j++) {
                        if (j >= src_len)
                            j = 0;
                        dstf[i] = (float)srci[j];
                    }
                    break;
                }
                case MAPPER_DOUBLE: {
                    double *srcd = (double*)src_val;
                    for (i = 0, j = 0; i < dst_len; i++, j++) {
                        if (j >= src_len)
                            j = 0;
                        dstf[i] = (float)srcd[j];
                    }
                    break;
                }
                default:
                    return -1;
            }
            break;
        }
        case MAPPER_INT32:{
            int *dsti = (int*)dst_val;
            switch (src_type) {
                case MAPPER_FLOAT: {
                    float *srcf = (float*)src_val;
                    for (i = 0, j = 0; i < dst_len; i++, j++) {
                        if (j >= src_len)
                            j = 0;
                        dsti[i] = (int)srcf[j];
                    }
                    break;
                }
                case MAPPER_DOUBLE: {
                    double *srcd = (double*)src_val;
                    for (i = 0, j = 0; i < dst_len; i++, j++) {
                        if (j >= src_len)
                            j = 0;
                        dsti[i] = (int)srcd[j];
                    }
                    break;
                }
                default:
                    return -1;
            }
            break;
        }
        case MAPPER_DOUBLE:{
            double *dstd = (double*)dst_val;
            switch (src_type) {
                case MAPPER_INT32: {
                    int *srci = (int*)src_val;
                    for (i = 0, j = 0; i < dst_len; i++, j++) {
                        if (j >= src_len)
                            j = 0;
                        dstd[i] = (float)srci[j];
                    }
                    break;
                }
                case MAPPER_FLOAT: {
                    float *srcf = (float*)src_val;
                    for (i = 0, j = 0; i < dst_len; i++, j++) {
                        if (j >= src_len)
                            j = 0;
                        dstd[i] = (double)srcf[j];
                    }
                    break;
                }
                default:
                    return -1;
            }
            break;
        }
        default:
            return -1;
    }
    return 0;
}

double propval_get_double(const void *val, const mapper_type type, int idx)
{
    switch (type) {
        case MAPPER_FLOAT:
        {
            float *temp = (float*)val;
            return (double)temp[idx];
            break;
        }
        case MAPPER_INT32:
        {
            int *temp = (int*)val;
            return (double)temp[idx];
            break;
        }
        case MAPPER_DOUBLE:
        {
            double *temp = (double*)val;
            return temp[idx];
            break;
        }
        default:
            return 0;
            break;
    }
}

void propval_set_double(void *to, const mapper_type type, int idx, double from)
{
    switch (type) {
        case MAPPER_FLOAT:
        {
            float *temp = (float*)to;
            temp[idx] = (float)from;
            break;
        }
        case MAPPER_INT32:
        {
            int *temp = (int*)to;
            temp[idx] = (int)from;
            break;
        }
        case MAPPER_DOUBLE:
        {
            double *temp = (double*)to;
            temp[idx] = from;
            break;
        }
        default:
            return;
            break;
    }
}

void mapper_prop_print(int len, mapper_type type, const void *val)
{
    int i;
    if (!val || len < 1) {
        printf("NULL");
        return;
    }

    if (len > 1)
        printf("[");

    switch (type) {
        case MAPPER_STRING:
        {
            if (len == 1)
                printf("'%s', ", (char*)val);
            else {
                char **ps = (char**)val;
                for (i = 0; i < len; i++)
                    printf("'%s', ", ps[i]);
            }
            break;
        }
        case MAPPER_FLOAT:
        {
            float *pf = (float*)val;
            for (i = 0; i < len; i++)
                printf("%f, ", pf[i]);
            break;
        }
        case MAPPER_INT32:
        {
            int *pi = (int*)val;
            for (i = 0; i < len; i++)
                printf("%d, ", pi[i]);
            break;
        }
        case MAPPER_BOOL:
        {
            int *pi = (int*)val;
            for (i = 0; i < len; i++)
                printf("%c, ", pi[i] ? 'T' : 'F');
            break;
        }
        case MAPPER_DOUBLE:
        {
            double *pd = (double*)val;
            for (i = 0; i < len; i++)
                printf("%f, ", pd[i]);
            break;
        }
        case MAPPER_INT64:
        {
            int64_t *pi = (int64_t*)val;
            for (i = 0; i < len; i++)
                printf("%lli, ", (long long)pi[i]);
            break;
        }
        case MAPPER_TIME:
        {
            mapper_time_t *pt = (mapper_time_t*)val;
            for (i = 0; i < len; i++)
                printf("%f, ", mapper_time_get_double(pt[i]));
            break;
        }
        case MAPPER_CHAR:
        {
            char *pi = (char*)val;
            for (i = 0; i < len; i++)
                printf("%c, ", pi[i]);
            break;
        }
        case MAPPER_PTR:
        {
            void **v = (void**)val;
            for (i = 0; i < len; i++)
                printf("%p, ", v[i]);
            break;
        }
        case MAPPER_DEVICE:
        {
            // just print device name
            if (1 == len)
                printf("'%s', ", mapper_device_get_name((mapper_device)val));
            else {
                mapper_device *devs = (mapper_device*)val;
                for (i = 0; i < len; i++) {
                    printf("'%s', ", mapper_device_get_name(devs[i]));
                }
            }
            break;
        }
        case MAPPER_SIGNAL:
        {
            // just print signal name
            if (1 == len) {
                mapper_signal sig = (mapper_signal)val;
                printf("'%s:%s', ", mapper_device_get_name(sig->dev), sig->name);
            }
            else {
                mapper_signal *sig = (mapper_signal*)val;
                for (i = 0; i < len; i++)
                    printf("'%s:%s', ", mapper_device_get_name(sig[i]->dev),
                           sig[i]->name);
            }
            break;
        }
        default:
            break;
    }

    if (len > 1)
        printf("\b\b] \b");
    else
        printf("\b\b \b");
}
