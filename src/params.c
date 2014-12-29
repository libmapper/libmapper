
#include <string.h>
#include <stdio.h>
#include <assert.h>

#include "types_internal.h"
#include "mapper_internal.h"

const char* mapper_msg_param_strings[] =
{
    "@boundMax",        /* AT_BOUND_MAX */
    "@boundMin",        /* AT_BOUND_MIN */
    "@destLength",      /* AT_DEST_LENGTH */
    "@destMax",         /* AT_DEST_MAX */
    "@destMin",         /* AT_DEST_MIN */
    "@destPort",        /* AT_DEST_PORT */
    "@destType",        /* AT_DEST_TYPE */
    "@direction",       /* AT_DIRECTION */
    "@expression",      /* AT_EXPRESSION */
    "@ID",              /* AT_ID */
    "@instances",       /* AT_INSTANCES */
    "@IP",              /* AT_IP */
    "@length",          /* AT_LENGTH */
    "@libVersion",      /* AT_LIB_VERSION */
    "@max",             /* AT_MAX */
    "@min",             /* AT_MIN */
    "@mode",            /* AT_MODE */
    "@mute",            /* AT_MUTE */
    "@numConnectsIn",   /* AT_NUM_CONNECTIONS_IN */
    "@numConnectsOut",  /* AT_NUM_CONNECTIONS_OUT */
    "@numInputs",       /* AT_NUM_INPUTS */
    "@numLinksIn",      /* AT_NUM_LINKS_IN */
    "@numLinksOut",     /* AT_NUM_LINKS_OUT */
    "@numOutputs",      /* AT_NUM_OUTPUTS */
    "@port",            /* AT_PORT */
    "@rate",            /* AT_RATE */
    "@rev",             /* AT_REV */
    "@scope",           /* AT_SCOPE */
    "@sendAsInstance",  /* AT_SEND_AS_INSTANCE */
    "@srcLength",       /* AT_SRC_LENGTH */
    "@srcMax",          /* AT_SRC_MAX */
    "@srcMin",          /* AT_SRC_MIN */
    "@srcPort",         /* AT_SRC_PORT */
    "@srcType",         /* AT_SRC_TYPE */
    "@type",            /* AT_TYPE */
    "@units",           /* AT_UNITS */
    "",                 /* AT_EXTRA (special case, does not represent a
                         * specific property name) */
};

int mapper_msg_parse_params(mapper_message_t *msg,
                            const char *path, const char *types,
                            int argc, lo_arg **argv)
{
    int i, j;

    /* Sanity check: complain loudly and quit string if number of
     * strings and params doesn't match up. */
    die_unless(sizeof(mapper_msg_param_strings)/sizeof(const char*)
               == N_AT_PARAMS,
               "libmapper ERROR: wrong number of known parameters\n");

    memset(msg, 0, sizeof(mapper_message_t));
    msg->path = path;
    msg->extra_args[0] = 0;
    int extra_count = 0;

    for (i=0; i<argc; i++) {
        if (types[i]!='s') {
            /* parameter ID not a string */
#ifdef DEBUG
            trace("message %s, parameter '", path);
            lo_arg_pp(types[i], argv[i]);
            trace("' not a string.\n");
#endif
            continue;
        }

        for (j=0; j<N_AT_PARAMS; j++)
            if (strcmp(&argv[i]->s, mapper_msg_param_strings[j])==0)
                break;

        if (j==N_AT_PARAMS) {
            if (argv[i]->s == '@' && extra_count < N_EXTRA_PARAMS) {
                /* Unknown "extra" parameter, record the key index. */
                msg->extra_args[extra_count] = &argv[i];
                msg->extra_types[extra_count] = types[i+1];
                msg->extra_lengths[extra_count] = 0;
                while (++i < argc) {
                    if ((types[i] == 's' || types[i] == 'S')
                        && (&argv[i]->s)[0] == '@') {
                        /* arrived at next parameter index. */
                        i--;
                        break;
                    }
                    else if (types[i] != msg->extra_types[extra_count]) {
                        trace("message %s, value vector for key %s has heterogeneous types.\n", path, &(*msg->extra_args[extra_count])->s);
                        msg->extra_lengths[extra_count] = 0;
                        break;
                    }
                    msg->extra_lengths[extra_count]++;
                }
                if (!msg->extra_lengths[extra_count]) {
                    trace("message %s, key %s has no values.\n",
                          path, &(*msg->extra_args[extra_count])->s);
                    msg->extra_args[extra_count] = 0;
                    msg->extra_types[extra_count] = 0;
                    continue;
                }
                extra_count++;
                continue;
            }
            else
                /* Skip non-keyed parameters */
                continue;
        }

        msg->types[j] = &types[i+1];
        msg->values[j] = &argv[i+1];
        msg->lengths[j] = 0;
        while (++i < argc) {
            if ((types[i] == 's' || types[i] == 'S')
                && (&argv[i]->s)[0] == '@') {
                /* Arrived at next param index. */
                i--;
                break;
            }
            else if (types[i] != *msg->types[j]) {
                trace("message %s, value vector for key %s has heterogeneous types.\n",
                      path, mapper_msg_param_strings[j]);
                msg->lengths[j] = 0;
                break;
            }
            else if (types[i] == LO_NIL) {
                msg->lengths[j] = 0;
                break;
            }
            msg->lengths[j]++;
        }
        if (!msg->lengths[j]) {
            trace("message %s, key %s has no values.\n",
                  path, mapper_msg_param_strings[j]);
            msg->types[j] = 0;
            msg->values[j] = 0;
            continue;
        }
    }
    return 0;
}

lo_arg** mapper_msg_get_param(mapper_message_t *msg,
                              mapper_msg_param_t param)
{
    die_unless(param < N_AT_PARAMS,
               "error, unknown parameter\n");
    return msg->values[param];
}

const char* mapper_msg_get_type(mapper_message_t *msg,
                                mapper_msg_param_t param)
{
    die_unless(param < N_AT_PARAMS,
               "error, unknown parameter\n");
    return msg->types[param];
}

int mapper_msg_get_length(mapper_message_t *msg,
                          mapper_msg_param_t param)
{
    die_unless(param < N_AT_PARAMS,
               "error, unknown parameter\n");
    return msg->lengths[param];
}

const char* mapper_msg_get_param_if_string(mapper_message_t *msg,
                                           mapper_msg_param_t param)
{
    die_unless(param < N_AT_PARAMS,
               "error, unknown parameter\n");

    lo_arg **a = mapper_msg_get_param(msg, param);
    if (!a || !(*a)) return 0;

    const char *t = mapper_msg_get_type(msg, param);
    if (!t) return 0;

    if (t[0] != 's' && t[0] != 'S')
        return 0;

    return &(*a)->s;
}

const char* mapper_msg_get_param_if_char(mapper_message_t *msg,
                                         mapper_msg_param_t param)
{
    die_unless(param < N_AT_PARAMS,
               "error, unknown parameter\n");

    lo_arg **a = mapper_msg_get_param(msg, param);
    if (!a || !(*a)) return 0;

    const char *t = mapper_msg_get_type(msg, param);
    if (!t) return 0;

    if ((t[0] == 's' || t[0] == 'S')
        && (&(*a)->s)[0] && (&(*a)->s)[1]==0)
        return &(*a)->s;

    if (t[0] == 'c')
        return (char*)&(*a)->c;

    return 0;
}

int mapper_msg_get_param_if_int(mapper_message_t *msg,
                                mapper_msg_param_t param,
                                int *value)
{
    die_unless(param < N_AT_PARAMS,
               "error, unknown parameter\n");
    die_unless(value!=0, "bad pointer");

    lo_arg **a = mapper_msg_get_param(msg, param);
    if (!a || !(*a)) return 1;

    const char *t = mapper_msg_get_type(msg, param);
    if (!t) return 1;

    if (t[0] != 'i')
        return 1;

    *value = (*a)->i;
    return 0;
}

int mapper_msg_get_param_if_float(mapper_message_t *msg,
                                  mapper_msg_param_t param,
                                  float *value)
{
    die_unless(param < N_AT_PARAMS,
               "error, unknown parameter\n");
    die_unless(value!=0, "bad pointer");

    lo_arg **a = mapper_msg_get_param(msg, param);
    if (!a || !(*a)) return 1;

    const char *t = mapper_msg_get_type(msg, param);
    if (!t) return 1;

    if (t[0] != 'f')
        return 1;

    *value = (*a)->f;
    return 0;
}

int mapper_msg_get_param_if_double(mapper_message_t *msg,
                                   mapper_msg_param_t param,
                                   double *value)
{
    die_unless(param < N_AT_PARAMS,
               "error, unknown parameter\n");
    die_unless(value!=0, "bad pointer");

    lo_arg **a = mapper_msg_get_param(msg, param);
    if (!a || !(*a)) return 1;

    const char *t = mapper_msg_get_type(msg, param);
    if (!t) return 1;

    if (t[0] != 'd')
        return 1;

    *value = (*a)->d;
    return 0;
}

int mapper_msg_add_or_update_extra_params(table t, mapper_message_t *params)
{
    int i=0, updated=0;
    while (params->extra_args[i])
    {
        const char *key = &params->extra_args[i][0]->s + 1; // skip '@'
        char type = params->extra_types[i];
        int length = params->extra_lengths[i];
        updated += mapper_table_add_or_update_msg_value(t, key, type,
                                                        &params->extra_args[i][1],
                                                        length);
        i++;
    }
    return updated;
}

/* helper for mapper_msg_prepare_varargs() */
void msg_add_typed_value(lo_message m, char type, int length, void *value)
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
        default:
            break;
    }
}

void mapper_msg_prepare_varargs(lo_message m, va_list aq)
{
    char *s;
    int i;
    char t[] = " ";
    table tab;
    mapper_signal sig;
    mapper_msg_param_t pa = (mapper_msg_param_t) va_arg(aq, int);
    mapper_db_connection_t *con;

    while (pa != N_AT_PARAMS)
    {
        /* if parameter is -1, it means to skip this entry */
        if ((int)pa == -1) {
            pa = (mapper_msg_param_t) va_arg(aq, int);
            pa = (mapper_msg_param_t) va_arg(aq, int);
            continue;
        }

        /* Only "extra" is not a real property name */
#ifdef DEBUG
        if ((int)pa >= 0 && pa < N_AT_PARAMS)
#endif
            if (pa != AT_EXTRA)
                lo_message_add_string(m, mapper_msg_param_strings[pa]);

        switch (pa) {
        case AT_IP:
            s = va_arg(aq, char*);
            lo_message_add_string(m, s);
            break;
        case AT_DEST_LENGTH:
        case AT_DEST_PORT:
        case AT_ID:
        case AT_LENGTH:
        case AT_NUM_CONNECTIONS_IN:
        case AT_NUM_CONNECTIONS_OUT:
        case AT_NUM_INPUTS:
        case AT_NUM_LINKS_IN:
        case AT_NUM_LINKS_OUT:
        case AT_NUM_OUTPUTS:
        case AT_PORT:
        case AT_REV:
        case AT_SEND_AS_INSTANCE:
        case AT_SRC_LENGTH:
        case AT_SRC_PORT:
            i = va_arg(aq, int);
            lo_message_add_int32(m, i);
            break;
        case AT_TYPE:
        case AT_SRC_TYPE:
        case AT_DEST_TYPE:
            i = va_arg(aq, int);
            t[0] = (char)i;
            lo_message_add_string(m, t);
            break;
        case AT_UNITS:
            sig = va_arg(aq, mapper_signal);
            lo_message_add_string(m, sig->props.unit);
            break;
        case AT_MIN:
            sig = va_arg(aq, mapper_signal);
            msg_add_typed_value(m, sig->props.type, sig->props.length,
                                sig->props.minimum);
            break;
        case AT_MAX:
            sig = va_arg(aq, mapper_signal);
            msg_add_typed_value(m, sig->props.type, sig->props.length,
                                sig->props.maximum);
            break;
        case AT_RATE:
            sig = va_arg(aq, mapper_signal);
            lo_message_add_float(m, sig->props.rate);
            break;
        case AT_MODE:
            i = va_arg(aq, int);
            if (i >= 0 && i < N_MAPPER_MODE_TYPES)
                lo_message_add_string(m, mapper_mode_type_strings[i]);
            else
                lo_message_add_string(m, "unknown");
            break;
        case AT_EXPRESSION:
            s = va_arg(aq, char*);
            lo_message_add_string(m, s);
            break;
        case AT_BOUND_MIN:
        case AT_BOUND_MAX:
            i = va_arg(aq, int);
            if (i >= 0 && i < N_MAPPER_BOUNDARY_ACTIONS)
                lo_message_add_string(m, mapper_boundary_action_strings[i]);
            else
                lo_message_add_string(m, "unknown");
            break;
        case AT_SRC_MIN:
            con = va_arg(aq, mapper_db_connection_t*);
            msg_add_typed_value(m, con->src_type, con->src_length,
                                con->src_min);
            break;
        case AT_SRC_MAX:
            con = va_arg(aq, mapper_db_connection_t*);
            msg_add_typed_value(m, con->src_type, con->src_length,
                                con->src_max);
            break;
        case AT_DEST_MIN:
            con = va_arg(aq, mapper_db_connection_t*);
            msg_add_typed_value(m, con->dest_type, con->dest_length,
                                con->dest_min);
            break;
        case AT_DEST_MAX:
            con = va_arg(aq, mapper_db_connection_t*);
            msg_add_typed_value(m, con->dest_type, con->dest_length,
                                con->dest_max);
            break;
        case AT_MUTE:
            i = va_arg(aq, int);
            lo_message_add_int32(m, i!=0);
            break;
        case AT_DIRECTION:
            s = va_arg(aq, char*);
            lo_message_add_string(m, s);
            break;
        case AT_INSTANCES:
            sig = va_arg(aq, mapper_signal);
            lo_message_add_int32(m, sig->props.num_instances);
            break;
        case AT_LIB_VERSION:
            s = va_arg(aq, char*);
            lo_message_add_string(m, s);
            break;
        case AT_SCOPE:
            con = va_arg(aq, mapper_db_connection_t*);
            msg_add_typed_value(m, 's', con->scope.size, con->scope.names);
            break;
        case AT_EXTRA:
            tab = va_arg(aq, table);
            i = 0;
            {
                mapper_prop_value_t *prop;
                prop = table_value_at_index_p(tab, i++);
                while(prop)
                {
                    const char *k = table_key_at_index(tab, i-1);
                    char key[256] = "@";
                    strncpy(&key[1], k, 254);
                    lo_message_add_string(m, key);
                    msg_add_typed_value(m, prop->type, prop->length, prop->value);
                    prop = table_value_at_index_p(tab, i++);
                }
            }
            break;
        default:
            die_unless(0, "unknown parameter %d\n", pa);
        }
        pa = (mapper_msg_param_t) va_arg(aq, int);
    }
}

/* helper for mapper_msg_prepare_params() */
static void msg_add_lo_arg(lo_message m, char type, lo_arg *a)
{
    switch (type) {
    case 'i':
        lo_message_add_int32(m, a->i);
        break;
    case 'f':
        lo_message_add_float(m, a->f);
        break;
    case 'd':
        lo_message_add_double(m, a->d);
        break;
    case 's':
        lo_message_add_string(m, &a->s);
        break;
    case 'c':
        lo_message_add_char(m, a->c);
        break;
    default:
        trace("unknown type in msg_add_lo_arg()\n");
        break;
    }
}

void mapper_msg_add_value_table(lo_message m, table t)
{
    string_table_node_t *n = t->store;
    int i;
    for (i=0; i<t->len; i++) {
        char keyname[256];
        snprintf(keyname, 256, "@%s", n->key);
        lo_message_add_string(m, keyname);
        mapper_prop_value_t *v = n->value;
        msg_add_typed_value(m, v->type, v->length, v->value);
        n++;
    }
}

void mapper_msg_prepare_params(lo_message m,
                               mapper_message_t *msg)
{
    int i;
    mapper_msg_param_t pa = (mapper_msg_param_t) 0;

    for (pa = (mapper_msg_param_t) 0; pa < N_AT_PARAMS; pa = (mapper_msg_param_t) (pa + 1))
    {
        if (!msg->values[pa])
            continue;

        lo_arg **a = msg->values[pa];
        if (!a)
            continue;

        lo_message_add_string(m, mapper_msg_param_strings[pa]);

        for (i = 0; i < msg->lengths[pa]; i++) {
            msg_add_lo_arg(m, *msg->types[pa], a[i]);
        }
    }
    pa = 0;
    while (msg->extra_args[pa])
    {
        msg_add_lo_arg(m, 's', (lo_arg*) (&msg->extra_args[pa][0]->s));
        for (i = 0; i < msg->extra_lengths[pa]; i++) {
            msg_add_lo_arg(m, msg->extra_types[pa], *(msg->extra_args[pa]+i+1));
        }
        pa++;
    }
}

void mapper_link_prepare_osc_message(lo_message m,
                                     mapper_link link)
{
    mapper_msg_add_value_table(m, link->props.extra);
}

void mapper_connection_prepare_osc_message(lo_message m,
                                           mapper_connection con)
{
    int i;
    if (con->props.mode) {
        lo_message_add_string(m, mapper_msg_param_strings[AT_MODE]);
        lo_message_add_string(m, mapper_mode_type_strings[con->props.mode]);
    }
    if (con->props.expression) {
        lo_message_add_string(m, mapper_msg_param_strings[AT_EXPRESSION]);
        lo_message_add_string(m, con->props.expression);
    }

    if (con->props.range_known & CONNECTION_RANGE_SRC_MIN) {
        lo_message_add_string(m, mapper_msg_param_strings[AT_SRC_MIN]);
        msg_add_typed_value(m, con->props.src_type, con->props.src_length,
                            con->props.src_min);
    }

    if (con->props.range_known & CONNECTION_RANGE_SRC_MAX) {
        lo_message_add_string(m, mapper_msg_param_strings[AT_SRC_MAX]);
        msg_add_typed_value(m, con->props.src_type, con->props.src_length,
                            con->props.src_max);
    }
    
    if (con->props.range_known & CONNECTION_RANGE_DEST_MIN) {
        lo_message_add_string(m, mapper_msg_param_strings[AT_DEST_MIN]);
        msg_add_typed_value(m, con->props.dest_type, con->props.dest_length,
                            con->props.dest_min);
    }

    if (con->props.range_known & CONNECTION_RANGE_DEST_MAX) {
        lo_message_add_string(m, mapper_msg_param_strings[AT_DEST_MAX]);
        msg_add_typed_value(m, con->props.dest_type, con->props.dest_length,
                            con->props.dest_max);
    }

    lo_message_add_string(m, mapper_msg_param_strings[AT_BOUND_MIN]);
    lo_message_add_string(m, mapper_boundary_action_strings[con->props.bound_min]);
    lo_message_add_string(m, mapper_msg_param_strings[AT_BOUND_MAX]);
    lo_message_add_string(m, mapper_boundary_action_strings[con->props.bound_max]);
    lo_message_add_string(m, mapper_msg_param_strings[AT_MUTE]);
    lo_message_add_int32(m, con->props.muted);
    lo_message_add_string(m, mapper_msg_param_strings[AT_SRC_TYPE]);
    lo_message_add_char(m, con->props.src_type);
    lo_message_add_string(m, mapper_msg_param_strings[AT_DEST_TYPE]);
    lo_message_add_char(m, con->props.dest_type);
    lo_message_add_string(m, mapper_msg_param_strings[AT_SRC_LENGTH]);
    lo_message_add_int32(m, con->props.src_length);
    lo_message_add_string(m, mapper_msg_param_strings[AT_DEST_LENGTH]);
    lo_message_add_int32(m, con->props.dest_length);
    lo_message_add_string(m, mapper_msg_param_strings[AT_SEND_AS_INSTANCE]);
    lo_message_add_int32(m, con->props.send_as_instance);
    lo_message_add_string(m, mapper_msg_param_strings[AT_SCOPE]);
    if (con->props.scope.size) {
        for (i = 0; i < con->props.scope.size; i++)
            lo_message_add_string(m, con->props.scope.names[i]);
    }
    else
        lo_message_add_nil(m);

    mapper_msg_add_value_table(m, con->props.extra);
}

mapper_mode_type mapper_msg_get_direction(mapper_message_t *msg)
{
    lo_arg **a = mapper_msg_get_param(msg, AT_DIRECTION);
    if (!a || !*a)
        return -1;

    if (strcmp(&(*a)->s, "input") == 0)
        return 0;
    else if (strcmp(&(*a)->s, "output") == 0)
        return 1;
    else
        return -1;

    return -1;
}

mapper_mode_type mapper_msg_get_mode(mapper_message_t *msg)
{
    lo_arg **a = mapper_msg_get_param(msg, AT_MODE);
    if (!a || !*a)
        return -1;

    if (strcmp(&(*a)->s, "bypass") == 0)
        return MO_BYPASS;
    else if (strcmp(&(*a)->s, "linear") == 0)
        return MO_LINEAR;
    else if (strcmp(&(*a)->s, "expression") == 0)
        return MO_EXPRESSION;
    else if (strcmp(&(*a)->s, "calibrate") == 0)
        return MO_CALIBRATE;
    else if (strcmp(&(*a)->s, "reverse") == 0)
        return MO_REVERSE;
    else
        return -1;

    return -1;
}

mapper_boundary_action mapper_msg_get_boundary_action(mapper_message_t *msg,
                                                      mapper_msg_param_t param)
{
    die_unless(param == AT_BOUND_MIN || param == AT_BOUND_MAX,
               "bad param in mapper_msg_get_boundary_action()\n");
    lo_arg **a = mapper_msg_get_param(msg, param);
    if (!a || !*a)
        return -1;

    if (strcmp(&(*a)->s, "none") == 0)
        return BA_NONE;
    if (strcmp(&(*a)->s, "mute") == 0)
        return BA_MUTE;
    if (strcmp(&(*a)->s, "clamp") == 0)
        return BA_CLAMP;
    if (strcmp(&(*a)->s, "fold") == 0)
        return BA_FOLD;
    if (strcmp(&(*a)->s, "wrap") == 0)
        return BA_WRAP;

    return -1;
}

int mapper_msg_get_mute(mapper_message_t *msg)
{
    lo_arg **a = mapper_msg_get_param(msg, AT_MUTE);
    const char *t = mapper_msg_get_type(msg, AT_MUTE);
    if (!a || !*a || !t)
        return -1;

    if (*t == 'i')
        return (*a)->i;
    else if (*t == LO_TRUE)
        return 1;
    else if (*t == LO_FALSE)
        return 0;
    else
        return -1;
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

void mapper_prop_pp(char type, int length, const void *value)
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
                printf("%f, ", mapper_timetag_get_double(pt[i]));
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
        printf("\b\b]");
    else
        printf("\b\b");
}
