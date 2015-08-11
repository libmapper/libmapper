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

const char* prop_message_strings[] =
{
    "@boundMax",        /* AT_BOUND_MAX */
    "@boundMin",        /* AT_BOUND_MIN */
    "@calibrating",     /* AT_CALIBRATING */
    "@causeUpdate",     /* AT_CAUSE_UPDATE */
    "@direction",       /* AT_DIRECTION */
    "@expression",      /* AT_EXPRESSION */
    "@host",            /* AT_HOST */
    "@id",              /* AT_ID */
    "@instances",       /* AT_INSTANCES */
    "@length",          /* AT_LENGTH */
    "@libVersion",      /* AT_LIB_VERSION */
    "@max",             /* AT_MAX */
    "@min",             /* AT_MIN */
    "@mode",            /* AT_MODE */
    "@mute",            /* AT_MUTE */
    "@numMapsIn",       /* AT_NUM_INCOMING_MAPS */
    "@numMapsOut",      /* AT_NUM_OUTGOING_MAPS */
    "@numInputs",       /* AT_NUM_INPUTS */
    "@numOutputs",      /* AT_NUM_OUTPUTS */
    "@port",            /* AT_PORT */
    "@processAt",       /* AT_PROCESS */
    "@rate",            /* AT_RATE */
    "@rev",             /* AT_REV */
    "@scope",           /* AT_SCOPE */
    "@sendAsInstance",  /* AT_SEND_AS_INSTANCE */
    "@slot",            /* AT_SLOT */
    "@type",            /* AT_TYPE */
    "@units",           /* AT_UNITS */
    "",                 /* AT_EXTRA (special case, does not represent a
                         * specific property name) */
};

const char* mapper_boundary_action_strings[] =
{
    NULL,          /* BA_UNDEFINED */
    "none",        /* BA_NONE */
    "mute",        /* BA_MUTE */
    "clamp",       /* BA_CLAMP */
    "fold",        /* BA_FOLD */
    "wrap",        /* BA_WRAP */
};

const char* mapper_mode_type_strings[] =
{
    NULL,          /* MO_UNDEFINED */
    "raw",         /* MO_RAW */
    "linear",      /* MO_LINEAR */
    "expression",  /* MO_EXPRESSION */
};

inline static int type_match(const char l, const char r)
{
    // allow TRUE and FALSE in value vectors, otherwise enforce same type
    return (l == r || ((l == 'T' || l == 'F') && (r == 'T' || r == 'F')));
}

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

mapper_message mapper_message_parse_params(int argc, const char *types,
                                           lo_arg **argv)
{
    int i, j, slot_index, num_params=0;
    // get the number of params
    for (i = 0; i < argc; i++) {
        if (types[i] == 's' || types[i] == 'S')
            num_params += (argv[i]->s == '@');
    }
    if (!num_params)
        return 0;

    mapper_message msg = ((mapper_message)
                          calloc(1, sizeof(struct _mapper_message)));
    msg->atoms = ((mapper_message_atom_t*)
                  calloc(1, sizeof(struct _mapper_message_atom) * num_params));
    mapper_message_atom atom = &msg->atoms[0];

    for (i = 0; i < argc; i++) {
        if (!is_string_type(types[i])) {
            /* parameter ID not a string */
#ifdef DEBUG
            trace("message parameter '");
            lo_arg_pp(types[i], argv[i]);
            trace("' not a string.\n");
#endif
            continue;
        }
        if (argv[i]->s == '@') {
            // new parameter
            if (atom->types)
                ++msg->num_atoms;
            atom = &msg->atoms[msg->num_atoms];
            atom->key = &argv[i]->s;

            // try to find matching index for static props
            if (strncmp(atom->key, "@dst@", 5)==0) {
                atom->index = DST_SLOT_PARAM;
                atom->key += 5;
            }
            else if (strncmp(atom->key, "@src", 4)==0) {
                if (atom->key[4] == '@') {
                    atom->index = SRC_SLOT_PARAM(0);
                    atom->key += 5;
                }
                else if (atom->key[4] == '.') {
                    // in form 'src.<ordinal>'
                    slot_index = atoi(atom->key + 5);
                    if (slot_index >= MAX_NUM_MAP_SOURCES) {
                        trace("Bad slot ordinal in param '%s'.\n", atom->key);
                        atom->types = 0;
                        continue;
                    }
                    atom->key = strchr(atom->key + 5, '@');
                    if (!atom->key || !(++atom->key)) {
                        trace("No sub-parameter found in key '%s'.\n", atom->key);
                        atom->types = 0;
                        continue;
                    }
                    atom->index = SRC_SLOT_PARAM(slot_index);
                }
            }
            else
                ++atom->key;
            for (j = 0; j < NUM_AT_PARAMS; j++) {
                if (strcmp(atom->key, prop_message_strings[j]+1)==0) {
                    atom->index |= j;
                    break;
                }
            }
            if (j == NUM_AT_PARAMS)
                atom->index = AT_EXTRA;
        }
        if (msg->num_atoms < 0)
            continue;
        atom->types = &types[i+1];
        atom->values = &argv[i+1];
        while (++i < argc) {
            if ((types[i] == 's' || types[i] == 'S') && (argv[i]->s == '@')) {
                /* Arrived at next param index. */
                i--;
                break;
            }
            else if (!type_match(types[i], atom->types[0])) {
                trace("value vector for key %s has heterogeneous types.\n",
                      atom->key);
                atom->length = 0;
                atom->types = 0;
                break;
            }
            atom->length++;
        }
        if (!atom->length) {
            trace("key %s has no values.\n", atom->key);
            atom->types = 0;
            continue;
        }
    }
    // reset last atom if no types
    if (atom->types)
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
        if (atom->index & DST_SLOT_PARAM)
            printf("  'dst/%s' [%d]: ", atom->key, atom->index);
        else if (atom->index >> SRC_SLOT_PARAM_BIT_OFFSET)
            printf("  'src%d/%s' [%d]: ",
                   (atom->index >> SRC_SLOT_PARAM_BIT_OFFSET) - 1, atom->key,
                   atom->index);
        else
            printf("  '%s' [%d]: ", atom->key, atom->index);
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

mapper_message_atom mapper_message_param(mapper_message msg,
                                         mapper_message_param_t param)
{
    int i;
    for (i = 0; i < msg->num_atoms; i++) {
        if (msg->atoms[i].index == param) {
            if (!msg->atoms[i].length || !msg->atoms[i].types)
                return 0;
            return &msg->atoms[i];
        }
    }
    return 0;
}

const char* mapper_message_param_if_string(mapper_message msg,
                                           mapper_message_param_t param)
{
    mapper_message_atom atom = mapper_message_param(msg, param);
    if (!atom || !atom->values)
        return 0;

    if (!atom->types)
        return 0;

    if (!is_string_type(atom->types[0]))
        return 0;

    return &(*(atom->values))->s;
}

const char* mapper_message_param_if_char(mapper_message msg,
                                         mapper_message_param_t param)
{
    mapper_message_atom atom = mapper_message_param(msg, param);
    if (!atom || !atom->values)
        return 0;

    if (!atom->types)
        return 0;

    if (is_string_type(atom->types[0])
        && (&(*atom->values)->s)[0] && (&(*atom->values)->s)[1]==0)
        return &(*atom->values)->s;

    if (atom->types[0] == 'c')
        return (char*)&(*atom->values)->c;

    return 0;
}

int mapper_message_param_if_int(mapper_message msg,
                                mapper_message_param_t param, int *value)
{
    die_unless(value!=0, "bad pointer");

    mapper_message_atom atom = mapper_message_param(msg, param);
    if (!atom)
        return 1;

    if (!atom->types)
        return 1;

    if (atom->types[0] == 'i' && *atom->values)
        *value = (*atom->values)->i;
    else if (atom->types[0] == 'T')
        *value = 1;
    else if (atom->types[0] == 'F')
        *value = 0;
    else
        return 1;

    return 0;
}

int mapper_message_param_if_int64(mapper_message msg,
                                  mapper_message_param_t param,
                                  int64_t *value)
{
    die_unless(value!=0, "bad pointer");

    mapper_message_atom atom = mapper_message_param(msg, param);
    if (!atom)
        return 1;

    if (!atom->types)
        return 1;

    if (atom->types[0] != 'h')
        return 1;

    *value = (*atom->values)->i64;
    return 0;
}

int mapper_message_param_if_float(mapper_message msg,
                                  mapper_message_param_t param,
                                  float *value)
{
    die_unless(value!=0, "bad pointer");

    mapper_message_atom atom = mapper_message_param(msg, param);
    if (!atom || !atom->values)
        return 1;

    if (!atom->types)
        return 1;

    if (atom->types[0] != 'f')
        return 1;

    *value = (*atom->values)->f;
    return 0;
}

int mapper_message_param_if_double(mapper_message msg,
                                   mapper_message_param_t param,
                                   double *value)
{
    die_unless(value!=0, "bad pointer");

    mapper_message_atom atom = mapper_message_param(msg, param);
    if (!atom || !atom->values)
        return 1;

    if (!atom->types)
        return 1;

    if (atom->types[0] != 'd')
        return 1;

    *value = (*atom->values)->d;
    return 0;
}

int mapper_message_add_or_update_extra_params(table tab, mapper_message msg)
{
    int i, updated = 0;
    for (i = 0; i < msg->num_atoms; i++) {
        if (msg->atoms[i].index != AT_EXTRA)
            continue;
        updated += mapper_table_add_or_update_message_atom(tab, &msg->atoms[i]);
    }
    return updated;
}

int mapper_update_string_if_arg(char **pdest_str, mapper_message msg,
                                mapper_message_param_t param)
{
    die_unless(pdest_str!=0, "bad pointer");

    mapper_message_atom atom = mapper_message_param(msg, param);

    if (atom && atom->values && is_string_type(atom->types[0])
        && (!(*pdest_str) || strcmp((*pdest_str), &(*atom->values)->s))) {
        char *str = (char*) realloc((void*)(*pdest_str),
                                    strlen(&(*atom->values)->s)+1);
        strcpy(str, &(*atom->values)->s);
        (*pdest_str) = str;
        return 1;
    }
    return 0;
}

int mapper_update_char_if_arg(char *pdest_char, mapper_message msg,
                              mapper_message_param_t param)
{
    die_unless(pdest_char!=0, "bad pointer");

    mapper_message_atom atom = mapper_message_param(msg, param);

    if (!atom || !atom->values)
        return 0;

    if (is_string_type(atom->types[0])) {
        if (*pdest_char != (&(*atom->values)->s)[0]) {
            (*pdest_char) = (&(*atom->values)->s)[0];
            return 1;
        }
    }
    else if (atom->types[0]=='c') {
        if (*pdest_char != (*atom->values)->c) {
            (*pdest_char) = (*atom->values)->c;
            return 1;
        }
    }
    return 0;
}

int mapper_update_bool_if_arg(int *pdest_bool, mapper_message msg,
                              mapper_message_param_t param)
{
    die_unless(pdest_bool!=0, "bad pointer");

    mapper_message_atom atom = mapper_message_param(msg, param);
    if (!atom || (atom->types[0] != 'T' && atom->types[0] != 'F'))
        return 0;

    if ((atom->types[0] != 'T') != !(*pdest_bool)) {
        (*pdest_bool) = (atom->types[0]=='T');
        return 1;
    }
    return 0;
}

int mapper_update_int_if_arg(int *pdest_int, mapper_message msg,
                             mapper_message_param_t param)
{
    die_unless(pdest_int!=0, "bad pointer");

    mapper_message_atom atom = mapper_message_param(msg, param);
    if (!atom || atom->types[0] != 'i')
        return 0;
    if (*pdest_int != (&(*atom->values)->i)[0]) {
        (*pdest_int) = (&(*atom->values)->i)[0];
        return 1;
    }
    return 0;
}

int mapper_update_int64_if_arg(int64_t *pdest_int64, mapper_message msg,
                               mapper_message_param_t param)
{
    die_unless(pdest_int64!=0, "bad pointer");

    mapper_message_atom atom = mapper_message_param(msg, param);
    if (!atom || atom->types[0] != 'h')
        return 0;
    if (*pdest_int64 != (&(*atom->values)->i64)[0]) {
        (*pdest_int64) = (&(*atom->values)->i64)[0];
        return 1;
    }
    return 0;
}

int mapper_update_float_if_arg(float *pdest_float, mapper_message msg,
                               mapper_message_param_t param)
{
    die_unless(pdest_float!=0, "bad pointer");

    mapper_message_atom atom = mapper_message_param(msg, param);
    if (!atom || atom->types[0] != 'f')
        return 0;
    if (*pdest_float != (&(*atom->values)->f)[0]) {
        (*pdest_float) = (&(*atom->values)->f)[0];
        return 1;
    }
    return 0;
}

int mapper_update_double_if_arg(double *pdest_double, mapper_message msg,
                                mapper_message_param_t param)
{
    die_unless(pdest_double!=0, "bad pointer");

    mapper_message_atom atom = mapper_message_param(msg, param);
    if (!atom || atom->types[0] != 'd')
        return 0;
    if (*pdest_double != (&(*atom->values)->d)[0]) {
        (*pdest_double) = (&(*atom->values)->d)[0];
        return 1;
    }
    return 0;
}

/* helper for mapper_message_varargs() */
void mapper_message_add_typed_value(lo_message m, int length, char type,
                                    const void *value)
{
    int i;
    if (length < 1)
        return;

    switch (type) {
        case 's':
        case 'S':
        {
            if (length == 1)
                lo_message_add_string(m, (char*)value);
            else {
                char **vals = (char**)value;
                for (i = 0; i < length; i++)
                    lo_message_add_string(m, vals[i]);
            }
            break;
        }
        case 'f':
        {
            float *vals = (float*)value;
            for (i = 0; i < length; i++)
                lo_message_add_float(m, vals[i]);
            break;
        }
        case 'd':
        {
            double *vals = (double*)value;
            for (i = 0; i < length; i++)
                lo_message_add_double(m, vals[i]);
            break;
        }
        case 'i':
        {
            int *vals = (int*)value;
            for (i = 0; i < length; i++)
                lo_message_add_int32(m, vals[i]);
            break;
        }
        case 'h':
        {
            int64_t *vals = (int64_t*)value;
            for (i = 0; i < length; i++)
                lo_message_add_int64(m, vals[i]);
            break;
        }
        case 't':
        {
            mapper_timetag_t *vals = (mapper_timetag_t*)value;
            for (i = 0; i < length; i++)
                lo_message_add_timetag(m, vals[i]);
            break;
        }
        case 'c':
        {
            char *vals = (char*)value;
            for (i = 0; i < length; i++)
                lo_message_add_char(m, vals[i]);
            break;
        }
        case 'b': {
            int *vals = (int*)value;
            for (i = 0; i < length; i++) {
                if (vals[i])
                    lo_message_add_true(m);
                else
                    lo_message_add_false(m);
            }
            break;
        }
        default:
            break;
    }
}

void mapper_message_add_value_table(lo_message m, table t)
{
    string_table_node_t *n = t->store;
    int i, remove;
    for (i=0; i<t->len; i++) {
        remove = (n->key[0]=='-');
        char keyname[256];
        snprintf(keyname, 256, "-@%s", n->key + remove);
        lo_message_add_string(m, keyname + 1 - remove);
        mapper_prop_value_t *v = n->value;
        mapper_message_add_typed_value(m, v->length, v->type, v->value);
        n++;
    }
}

const char *mapper_param_string(mapper_message_param_t param)
{
    die_unless(param < NUM_AT_PARAMS,
               "called mapper_param_string() with bad parameter.\n");

    return prop_message_strings[param];
}

int mapper_message_signal_direction(mapper_message msg)
{
    const char *str = mapper_message_param_if_string(msg, AT_DIRECTION);
    if (!str)
        return 0;
    if (strcmp(str, "output")==0)
        return DI_OUTGOING;
    if (strcmp(str, "input")==0)
        return DI_INCOMING;
    if (strcmp(str, "both")==0)
        return DI_BOTH;
    return 0;
}

const char *mapper_boundary_action_string(mapper_boundary_action bound)
{
    if (bound <= BA_UNDEFINED || bound > NUM_MAPPER_BOUNDARY_ACTIONS)
        return "unknown";
    return mapper_boundary_action_strings[bound];
}

mapper_boundary_action mapper_boundary_action_from_string(const char *str)
{
    if (!str)
        return BA_UNDEFINED;
    int i;
    for (i = BA_UNDEFINED+1; i < NUM_MAPPER_BOUNDARY_ACTIONS; i++) {
        if (strcmp(str, mapper_boundary_action_strings[i])==0)
            return i;
    }
    return BA_UNDEFINED;
}

const char *mapper_mode_type_string(mapper_mode_type mode)
{
    if (mode <= 0 || mode > NUM_MAPPER_MODE_TYPES)
        return "unknown";
    return mapper_mode_type_strings[mode];
}

mapper_mode_type mapper_mode_type_from_string(const char *str)
{
    if (!str)
        return MO_UNDEFINED;
    int i;
    for (i = MO_UNDEFINED+1; i < NUM_MAPPER_MODE_TYPES; i++) {
        if (strcmp(str, mapper_mode_type_strings[i])==0)
            return i;
    }
    return MO_UNDEFINED;
}

int mapper_message_mute(mapper_message msg)
{
    int mute;
    if (mapper_message_param_if_int(msg, AT_MUTE, &mute))
        return -1;
    return mute;
}

// Helper for setting property value from different lo_arg types
int propval_set_from_lo_arg(void *dest, const char dest_type,
                            lo_arg *src, const char src_type, int index)
{
    if (dest_type == 'f') {
        float *temp = (float*)dest;
        if (src_type == 'f') {
            if (temp[index] != src->f) {
                temp[index] = src->f;
                return 1;
            }
        }
        else if (src_type == 'i') {
            if (temp[index] != (float)src->i) {
                temp[index] = (float)src->i;
                return 1;
            }
        }
        else if (src_type == 'd') {
            if (temp[index] != (float)src->d) {
                temp[index] = (float)src->d;
                return 1;
            }
        }
        else
            return -1;
    }
    else if (dest_type == 'i') {
        int *temp = (int*)dest;
        if (src_type == 'f') {
            if (temp[index] != (int)src->f) {
                temp[index] = (int)src->f;
                return 1;
            }
        }
        else if (src_type == 'i') {
            if (temp[index] != src->i) {
                temp[index] = src->i;
                return 1;
            }
        }
        else if (src_type == 'd') {
            if (temp[index] != (int)src->d) {
                temp[index] = (int)src->d;
                return 1;
            }
        }
        else
            return -1;
    }
    else if (dest_type == 'd') {
        double *temp = (double*)dest;
        if (src_type == 'f') {
            if (temp[index] != (double)src->f) {
                temp[index] = (double)src->f;
                return 1;
            }
        }
        else if (src_type == 'i') {
            if (temp[index] != (double)src->i) {
                temp[index] = (double)src->i;
                return 1;
            }
        }
        else if (src_type == 'd') {
            if (temp[index] != src->d) {
                temp[index] = src->d;
                return 1;
            }
        }
        else
            return -1;
    }
    else
        return -1;
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

int mapper_prop_set_string(char **property, const char *string)
{
    if (*property) {
        if (!string) {
            free(*property);
            *property = 0;
            return 1;
        }
        else if (strcmp(*property, string)) {
            *property = realloc(*property, strlen(string)+1);
            memcpy(*property, string, strlen(string)+1);
            return 1;
        }
    }
    else if (string) {
        *property = strdup(string);
        return 1;
    }
    return 0;
}

void mapper_prop_pp(int length, char type, const void *value)
{
    int i;
    if (!value || length < 1)
        return;

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
                printf("%lli, ", pi[i]);
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
        default:
            break;
    }

    if (length > 1)
        printf("\b\b] \b");
    else
        printf("\b\b \b");
}
