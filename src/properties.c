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
    int length;
    char store_type;
    char protocol_type;
} static_property_t;

const static_property_t static_properties[] = {
    { "@bound_max",         1, 'i', 's' },  /* AT_BOUND_MAX */
    { "@bound_min",         1, 'i', 's' },  /* AT_BOUND_MIN */
    { "@calibrating",       1, 'b', 'b' },  /* AT_CALIBRATING */
    { "@causes_update",     1, 'b', 'b' },  /* AT_CAUSES_UPDATE */
    { "@description",       1, 's', 's' },  /* AT_DESCRIPTION */
    { "@direction",         1, 'i', 's' },  /* AT_DIRECTION */
    { "@expression",        1, 's', 's' },  /* AT_EXPRESSION */
    { "@host",              1, 's', 's' },  /* AT_HOST */
    { "@id",                1, 'h', 'h' },  /* AT_ID */
    { "@instance",          1, 'i', 'i' },  /* AT_INSTANCE */
    { "@is_local",          1, 'b', 'b' },  /* AT_IS_LOCAL */
    { "@length",            1, 'i', 'i' },  /* AT_LENGTH */
    { "@lib_version",       1, 's', 's' },  /* AT_LIB_VERSION */
    { "@max",               0, 'n', 'n' },  /* AT_MAX */
    { "@min",               0, 'n', 'n' },  /* AT_MIN */
    { "@mode",              1, 'i', 's' },  /* AT_MODE */
    { "@muted",             1, 'b', 'b' },  /* AT_MUTED */
    { "@name",              1, 's', 's' },  /* AT_NAME */
    { "@num_incoming_maps", 1, 'i', 'i' },  /* AT_NUM_INCOMING_MAPS */
    { "@num_inputs",        1, 'i', 'i' },  /* AT_NUM_INPUTS */
    { "@num_instances",     1, 'i', 'i' },  /* AT_NUM_INSTANCES */
    { "@num_links",         1, 'i', 'i' },  /* AT_NUM_LINKS */
    { "@num_maps",          2, 'i', 'i' },  /* AT_NUM_MAPS */
    { "@num_outgoing_maps", 1, 'i', 'i' },  /* AT_NUM_OUTGOING_MAPS */
    { "@num_outputs",       1, 'i', 'i' },  /* AT_NUM_OUTPUTS */
    { "@port",              1, 'i', 'i' },  /* AT_PORT */
    { "@process_location",  1, 'i', 's' },  /* AT_PROCESS */
    { "@protocol",          1, 'i', 's' },  /* AT_PROTOCOL */
    { "@rate",              1, 'f', 'f' },  /* AT_RATE */
    { "@scope",             0, 'D', 's' },  /* AT_SCOPE */
    { "@slot",              0, 'i', 'i' },  /* AT_SLOT */
    { "@status",            1, 'i', 'i' },  /* AT_STATUS */
    { "@synced",            1, 't', 't' },  /* AT_SYNCED */
    { "@type",              1, 'c', 'c' },  /* AT_TYPE */
    { "@unit",              1, 's', 's' },  /* AT_UNIT */
    { "@use_instances",     1, 'b', 'b' },  /* AT_USE_INSTANCES */
    { "@user_data",         1, 'v',  0  },  /* AT_USER_DATA */
    { "@version",           1, 'i', 'i' },  /* AT_VERSION */
    { "@extra",             0, 'a', 'a' },  /* AT_EXTRA (special case, does not
                                             * represent a specific property
                                             * name) */
};

const char* mapper_boundary_action_strings[] =
{
    NULL,           /* MAPPER_BOUND_UNDEFINED */
    "none",         /* MAPPER_BOUND_NONE */
    "mute",         /* MAPPER_BOUND_MUTE */
    "clamp",        /* MAPPER_BOUND_CLAMP */
    "fold",         /* MAPPER_BOUND_FOLD */
    "wrap",         /* MAPPER_BOUND_WRAP */
};

const char* mapper_location_strings[] =
{
    NULL,           /* MAPPER_LOC_UNDEFINED */
    "source",       /* MAPPER_LOC_SOURCE */
    "destination",  /* MAPPER_LOC_DESTINATION */
    "any",          /* MAPPER_LOC_ANY */
};

const char* mapper_mode_strings[] =
{
    NULL,           /* MAPPER_MODE_UNDEFINED */
    "raw",          /* MAPPER_MODE_RAW */
    "linear",       /* MAPPER_MODE_LINEAR */
    "expression",   /* MAPPER_MODE_EXPRESSION */
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

mapper_message mapper_message_parse_properties(int argc, const char *types,
                                               lo_arg **argv)
{
    int i, slot_index, num_props=0;
    // get the number of props
    for (i = 0; i < argc; i++) {
        if (types[i] != 's' && types[i] != 'S')
            continue;
        if  (argv[i]->s == '@' || (strncmp(&argv[i]->s, "-@", 2)==0)
             || (strncmp(&argv[i]->s, "+@", 2)==0))
            ++num_props;
    }
    if (!num_props)
        return 0;

    mapper_message msg = ((mapper_message)
                          calloc(1, sizeof(struct _mapper_message)));
    msg->atoms = ((mapper_message_atom_t*)
                  calloc(1, sizeof(struct _mapper_message_atom) * num_props));
    mapper_message_atom atom = &msg->atoms[0];
    const char *key;

    for (i = 0; i < argc; i++) {
        if (!is_string_type(types[i])) {
            /* property ID not a string */
#ifdef DEBUG
            trace("message item '");
            lo_arg_pp(types[i], argv[i]);
            trace("' not a string.\n");
#endif
            continue;
        }
        // new property
        if (atom->types || (atom->index & PROPERTY_REMOVE))
            ++msg->num_atoms;
        atom = &msg->atoms[msg->num_atoms];

        key = &argv[i]->s;
        if (strncmp(&argv[i]->s, "+@", 2)==0) {
            atom->index = PROPERTY_ADD;
            ++key;
        }
        else if (strncmp(&argv[i]->s, "-@", 2)==0) {
            atom->index = PROPERTY_REMOVE;
            ++key;
        }
        if (key[0] != '@') // not a property key
            continue;

        atom->key = key;

        // try to find matching index for static props
        if (strncmp(atom->key, "@dst@", 5)==0) {
            atom->index |= DST_SLOT_PROPERTY;
            atom->key += 5;
        }
        else if (strncmp(atom->key, "@src", 4)==0) {
            if (atom->key[4] == '@') {
                atom->index |= SRC_SLOT_PROPERTY(0);
                atom->key += 5;
            }
            else if (atom->key[4] == '.') {
                // in form 'src.<ordinal>'
                slot_index = atoi(atom->key + 5);
                if (slot_index >= MAX_NUM_MAP_SOURCES) {
                    trace("Bad slot ordinal in property '%s'.\n", atom->key);
                    atom->types = 0;
                    continue;
                }
                atom->key = strchr(atom->key + 5, '@');
                if (!atom->key || !(++atom->key)) {
                    trace("No sub-property found in key '%s'.\n", atom->key);
                    atom->types = 0;
                    continue;
                }
                atom->index |= SRC_SLOT_PROPERTY(slot_index);
            }
        }
        else
            ++atom->key;
        atom->index |= mapper_property_from_string(atom->key);

        if (msg->num_atoms < 0)
            continue;
        atom->types = &types[i+1];
        atom->values = &argv[i+1];
        while (++i < argc) {
            if ((types[i] == 's' || types[i] == 'S')
                && strspn(&argv[i]->s, "+-@")) {
                /* Arrived at next property index. */
                i--;
                break;
            }
            else if (!type_match(types[i], atom->types[0])) {
                trace("Value vector for key '%s' has heterogeneous types.\n",
                      atom->key);
                atom->length = 0;
                atom->types = 0;
                break;
            }
            else
                atom->length++;
        }
        if (!atom->length) {
            atom->types = 0;
            if (!(atom->index & PROPERTY_REMOVE)) {
                trace("Key '%s' has no values.\n", atom->key);
                continue;
            }
        }
        // check type against static props
        if (MASK_PROP_BITFLAGS(atom->index) < AT_EXTRA) {
            static_property_t prop;
            prop = static_properties[MASK_PROP_BITFLAGS(atom->index)];
            if (prop.length) {
                if (prop.length != atom->length) {
                    trace("Static property '%s' cannot have length %d.\n",
                          static_properties[MASK_PROP_BITFLAGS(atom->index)].name,
                          atom->length);
                    atom->length = 0;
                    atom->types = 0;
                    continue;
                }
                if (atom->index & (PROPERTY_ADD | PROPERTY_REMOVE)) {
                    trace("Cannot add or remove values from static property '%s'.\n",
                          static_properties[MASK_PROP_BITFLAGS(atom->index)].name);
                    atom->length = 0;
                    atom->types = 0;
                    continue;
                }
            }
            if (!prop.protocol_type) {
                trace("Static property '%s' cannot be set by message.\n",
                      static_properties[MASK_PROP_BITFLAGS(atom->index)].name);
                atom->length = 0;
                atom->types = 0;
                continue;
            }
            if (prop.protocol_type == 'n') {
                if (!is_number_type(atom->types[0])) {
                    trace("Static property '%s' cannot have type '%c' (2).\n",
                          static_properties[MASK_PROP_BITFLAGS(atom->index)].name,
                          atom->types[0]);
                    atom->length = 0;
                    atom->types = 0;
                    continue;
                }
            }
            else if (prop.protocol_type == 'b') {
                if (!is_boolean_type(atom->types[0])) {
                    trace("Static property '%s' cannot have type '%c' (2).\n",
                          static_properties[MASK_PROP_BITFLAGS(atom->index)].name,
                          atom->types[0]);
                    atom->length = 0;
                    atom->types = 0;
                    continue;
                }
            }
            else if (prop.protocol_type != atom->types[0]) {
                trace("Static property '%s' cannot have type '%c' (1).\n",
                      static_properties[MASK_PROP_BITFLAGS(atom->index)].name,
                      atom->types[0]);
                atom->length = 0;
                atom->types = 0;
                continue;
            }
        }
    }
    // reset last atom if no types unless "remove" flag is set
    if (atom->types || atom->index & PROPERTY_REMOVE)
        msg->num_atoms++;
    else {
        atom->key = 0;
        atom->length = 0;
        atom->values = 0;
    }
#if TRACING
    // print out parsed properties
    printf("%d parsed mapper_messages:\n", msg->num_atoms);
    for (i = 0; i < msg->num_atoms; i++) {
        atom = &msg->atoms[i];
        if (atom->index & PROPERTY_ADD)
            printf(" +");
        else if (atom->index & PROPERTY_REMOVE)
            printf(" -");
        else
            printf("  ");
        if (atom->index & DST_SLOT_PROPERTY)
            printf("'dst/%s' [%d]: ", atom->key, atom->index);
        else if (atom->index >> SRC_SLOT_PROPERTY_BIT_OFFSET)
            printf("'src%d/%s' [%d]: ", SRC_SLOT(atom->index), atom->key,
                   atom->index);
        else
            printf("'%s' [%d]: ", atom->key, atom->index);
        int j;
        for (j = 0; j < atom->length; j++) {
            lo_arg_pp(atom->types[j], atom->values[j]);
            printf(", ");
        }
        printf("\b\b \n");
    }
#endif
    return msg;
}

void mapper_message_free(mapper_message msg)
{
    if (msg) {
        if (msg->atoms)
            free(msg->atoms);
        free(msg);
    }
}

mapper_message_atom mapper_message_property(mapper_message msg,
                                            mapper_property_t prop)
{
    int i;
    for (i = 0; i < msg->num_atoms; i++) {
        if (msg->atoms[i].index == prop) {
            if (!msg->atoms[i].length || !msg->atoms[i].types)
                return 0;
            return &msg->atoms[i];
        }
    }
    return 0;
}

/* helper for mapper_message_varargs() */
void mapper_message_add_typed_value(lo_message msg, int length, char type,
                                    const void *value)
{
    int i;
    if (type && length < 1)
        return;

    switch (type) {
        case 's':
        case 'S':
        {
            if (length == 1)
                lo_message_add_string(msg, (char*)value);
            else {
                char **vals = (char**)value;
                for (i = 0; i < length; i++)
                    lo_message_add_string(msg, vals[i]);
            }
            break;
        }
        case 'f':
        {
            float *vals = (float*)value;
            for (i = 0; i < length; i++)
                lo_message_add_float(msg, vals[i]);
            break;
        }
        case 'd':
        {
            double *vals = (double*)value;
            for (i = 0; i < length; i++)
                lo_message_add_double(msg, vals[i]);
            break;
        }
        case 'i':
        {
            int *vals = (int*)value;
            for (i = 0; i < length; i++)
                lo_message_add_int32(msg, vals[i]);
            break;
        }
        case 'h':
        {
            int64_t *vals = (int64_t*)value;
            for (i = 0; i < length; i++)
                lo_message_add_int64(msg, vals[i]);
            break;
        }
        case 't':
        {
            mapper_timetag_t *vals = (mapper_timetag_t*)value;
            for (i = 0; i < length; i++)
                lo_message_add_timetag(msg, vals[i]);
            break;
        }
        case 'c':
        {
            char *vals = (char*)value;
            for (i = 0; i < length; i++)
                lo_message_add_char(msg, vals[i]);
            break;
        }
        case 'b': {
            int *vals = (int*)value;
            for (i = 0; i < length; i++) {
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

const char *mapper_property_protocol_string(mapper_property_t prop)
{
    prop = MASK_PROP_BITFLAGS(prop);
    die_unless(prop < NUM_AT_PROPERTIES,
               "called mapper_property_protocol_string() with bad property index %d.\n",
               prop);
    return static_properties[prop].name;
}

const char *mapper_property_string(mapper_property_t prop)
{
    prop = MASK_PROP_BITFLAGS(prop);
    die_unless(prop < NUM_AT_PROPERTIES,
               "called mapper_property_string() with bad property index %d.\n",
               prop);
    return static_properties[prop].name + 1;
}

mapper_property_t mapper_property_from_string(const char *string)
{
    // property names are stored alphabetically so we can use a binary search
    int beg = 0, end = NUM_AT_PROPERTIES - 2;
    int mid = (beg + end) * 0.5, cmp;
    while (beg <= end) {
        cmp = strcmp(string, static_properties[mid].name + 1);
        if (cmp > 0)
            beg = mid + 1;
        else if (cmp == 0)
            return mid;
        else
            end = mid - 1;
        mid = (beg + end) * 0.5;
    }
    if (strcmp(string, "maximum")==0)
        return AT_MAX;
    if (strcmp(string, "minimum")==0)
        return AT_MIN;
    return AT_EXTRA;
}

const char *mapper_boundary_action_string(mapper_boundary_action bound)
{
    if (bound <= MAPPER_BOUND_UNDEFINED || bound > NUM_MAPPER_BOUNDARY_ACTIONS)
        return "unknown";
    return mapper_boundary_action_strings[bound];
}

mapper_boundary_action mapper_boundary_action_from_string(const char *str)
{
    if (!str)
        return MAPPER_BOUND_UNDEFINED;
    int i;
    for (i = MAPPER_BOUND_UNDEFINED+1; i < NUM_MAPPER_BOUNDARY_ACTIONS; i++) {
        if (strcmp(str, mapper_boundary_action_strings[i])==0)
            return i;
    }
    return MAPPER_BOUND_UNDEFINED;
}

const char *mapper_location_string(mapper_location loc)
{
    if (loc <= 0 || loc > MAPPER_LOC_ANY)
        return "unknown";
    return mapper_location_strings[loc];
}

mapper_location mapper_location_from_string(const char *str)
{
    if (!str)
        return MAPPER_LOC_UNDEFINED;
    int i;
    for (i = MAPPER_LOC_UNDEFINED+1; i < 3; i++) {
        if (strcmp(str, mapper_location_strings[i])==0)
            return i;
    }
    return MAPPER_LOC_UNDEFINED;
}

const char *mapper_mode_string(mapper_mode mode)
{
    if (mode <= 0 || mode > NUM_MAPPER_MODES)
        return "unknown";
    return mapper_mode_strings[mode];
}

mapper_mode mapper_mode_from_string(const char *str)
{
    if (!str)
        return MAPPER_MODE_UNDEFINED;
    int i;
    for (i = MAPPER_MODE_UNDEFINED+1; i < NUM_MAPPER_MODES; i++) {
        if (strcmp(str, mapper_mode_strings[i])==0)
            return i;
    }
    return MAPPER_MODE_UNDEFINED;
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
int set_coerced_value(void *dst, const void *src, int length, char dst_type,
                      char src_type)
{
    int i;
    switch (dst_type) {
        case 'f':{
            float *dstf = (float*)dst;
            switch (src_type) {
                case 'f':
                    memcpy(dst, src, sizeof(float) * length);
                    break;
                case 'i': {
                    int *srci = (int*)src;
                    for (i = 0; i < length; i++) {
                        dstf[i] = (float)srci[i];
                    }
                    break;
                }
                case 'd': {
                    double *srcd = (double*)src;
                    for (i = 0; i < length; i++) {
                        dstf[i] = (float)srcd[i];
                    }
                    break;
                }
                default:
                    return -1;
            }
            break;
        }
        case 'i':{
            int *dsti = (int*)dst;
            switch (src_type) {
                case 'i':
                    memcpy(dst, src, sizeof(int) * length);
                    break;
                case 'f': {
                    float *srcf = (float*)src;
                    for (i = 0; i < length; i++) {
                        dsti[i] = (int)srcf[i];
                    }
                    break;
                }
                case 'd': {
                    double *srcd = (double*)src;
                    for (i = 0; i < length; i++) {
                        dsti[i] = (int)srcd[i];
                    }
                    break;
                }
                default:
                    return -1;
            }
            break;
        }
        case 'd':{
            double *dstd = (double*)dst;
            switch (src_type) {
                case 'd':
                    memcpy(dst, src, sizeof(double) * length);
                    break;
                case 'i': {
                    int *srci = (int*)src;
                    for (i = 0; i < length; i++) {
                        dstd[i] = (float)srci[i];
                    }
                    break;
                }
                case 'f': {
                    float *srcf = (float*)src;
                    for (i = 0; i < length; i++) {
                        dstd[i] = (double)srcf[i];
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

double propval_double(const void *value, const char type, int index)
{
    switch (type) {
        case 'f':
        {
            float *temp = (float*)value;
            return (double)temp[index];
            break;
        }
        case 'i':
        {
            int *temp = (int*)value;
            return (double)temp[index];
            break;
        }
        case 'd':
        {
            double *temp = (double*)value;
            return temp[index];
            break;
        }
        default:
            return 0;
            break;
    }
}

void propval_set_double(void *to, const char type, int index, double from)
{
    switch (type) {
        case 'f':
        {
            float *temp = (float*)to;
            temp[index] = (float)from;
            break;
        }
        case 'i':
        {
            int *temp = (int*)to;
            temp[index] = (int)from;
            break;
        }
        case 'd':
        {
            double *temp = (double*)to;
            temp[index] = from;
            break;
        default:
            return;
            break;
        }
    }
}

void mapper_property_print(int length, char type, const void *value)
{
    int i;
    if (!value || length < 1) {
        printf("NULL");
        return;
    }

    if (length > 1)
        printf("[");

    switch (type) {
        case 's':
        case 'S':
        {
            if (length == 1)
                printf("'%s', ", (char*)value);
            else {
                char **ps = (char**)value;
                for (i = 0; i < length; i++)
                    printf("'%s', ", ps[i]);
            }
            break;
        }
        case 'f':
        {
            float *pf = (float*)value;
            for (i = 0; i < length; i++)
                printf("%f, ", pf[i]);
            break;
        }
        case 'i':
        {
            int *pi = (int*)value;
            for (i = 0; i < length; i++)
                printf("%d, ", pi[i]);
            break;
        }
        case 'b':
        {
            int *pi = (int*)value;
            for (i = 0; i < length; i++)
                printf("%c, ", pi[i] ? 'T' : 'F');
            break;
        }
        case 'd':
        {
            double *pd = (double*)value;
            for (i = 0; i < length; i++)
                printf("%f, ", pd[i]);
            break;
        }
        case 'h':
        {
            int64_t *pi = (int64_t*)value;
            for (i = 0; i < length; i++)
                printf("%lli, ", (long long)pi[i]);
            break;
        }
        case 't':
        {
            mapper_timetag_t *pt = (mapper_timetag_t*)value;
            for (i = 0; i < length; i++)
                printf("%f, ", mapper_timetag_double(pt[i]));
            break;
        }
        case 'c':
        {
            char *pi = (char*)value;
            for (i = 0; i < length; i++)
                printf("%c, ", pi[i]);
            break;
        }
        case 'v':
        {
            void **v = (void**)value;
            for (i = 0; i < length; i++)
                printf("%p, ", v[i]);
            break;
        }
        default:
            break;
    }

    if (length > 1)
        printf("\b\b] \b");
    else
        printf("\b\b \b");
}
