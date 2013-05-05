
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
    "@range",           /* AT_RANGE */
    "@rate",            /* AT_RATE */
    "@rev",             /* AT_REV */
    "@scope",           /* AT_SCOPE */
    "@sendAsInstance",  /* AT_SEND_AS_INSTANCE */
    "@srcLength",       /* AT_SRC_LENGTH */
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
            return 1;
        }

        for (j=0; j<N_AT_PARAMS; j++)
            if (strcmp(&argv[i]->s, mapper_msg_param_strings[j])==0)
                break;

        if (j==N_AT_PARAMS) {
            if (argv[i]->s == '@' && extra_count < N_EXTRA_PARAMS) {
                /* Unknown "extra" parameter, record the key index. */
                msg->extra_args[extra_count] = &argv[i];
                i++; // To value
                msg->extra_types[extra_count] = types[i];
                extra_count++;
                continue;
            }
            else
                /* Skip non-keyed parameters */
                continue;
        }

        /* special case: range has 4 float or int parameters */
        if (j==AT_RANGE) {
            int k;
            msg->types[j] = &types[i+1];
            msg->values[j] = &argv[i+1];
            for (k=0; k<4; k++) {
                i++;
                if (i >= argc) {
                    trace("message %s, not enough parameters "
                          "for @range.\n", path);
                    return 1;
                }
                if (((types[i] == 's' || types[i] == 'S')
                     && strcmp("-", &argv[i]->s)==0)
                    || (types[i] == 'c' && argv[i]->c == '-'))
                {
                    /* The '-' character means "don't change this
                     * value", and here we ignore it.  It will be
                     * considered "unknown" during get_range(), and
                     * therefore not modified if already known by some
                     * other means. */
                }
                else if (types[i] != 'i' && types[i] != 'f') {
                    /* range parameter bad type */
#ifdef DEBUG
                    trace("message %s, @range parameter ", path);
                    lo_arg_pp(types[i], argv[i]);
                    trace("not float or int\n");
#endif
                    return 1;
                }
            }
        }
        else {
            i++;
            msg->types[j] = &types[i];
            msg->values[j] = &argv[i];
            if (i >= argc) {
                trace("message %s, not enough parameters for %s\n",
                      path, &argv[i-1]->s);
                return 1;
            }
        }
    }
    return 0;
}

lo_arg** mapper_msg_get_param(mapper_message_t *msg,
                              mapper_msg_param_t param)
{
    die_unless(param >= 0 && param < N_AT_PARAMS,
               "error, unknown parameter\n");
    return msg->values[param];
}

const char* mapper_msg_get_type(mapper_message_t *msg,
                                mapper_msg_param_t param)
{
    die_unless(param >= 0 && param < N_AT_PARAMS,
               "error, unknown parameter\n");
    return msg->types[param];
}

const char* mapper_msg_get_param_if_string(mapper_message_t *msg,
                                           mapper_msg_param_t param)
{
    die_unless(param >= 0 && param < N_AT_PARAMS,
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
    die_unless(param >= 0 && param < N_AT_PARAMS,
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
    die_unless(param >= 0 && param < N_AT_PARAMS,
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
    die_unless(param >= 0 && param < N_AT_PARAMS,
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

int mapper_msg_add_or_update_extra_params(table t,
                                          mapper_message_t *params)
{
    int i=0, updated=0;
    while (params->extra_args[i])
    {
        const char *key = &params->extra_args[i][0]->s + 1; // skip '@'
        lo_arg *arg = *(params->extra_args[i]+1);
        char type = params->extra_types[i];
        updated += mapper_table_add_or_update_osc_value(t, key, type, arg);
        i++;
    }
    return updated;
}

/* helper for mapper_msg_prepare_varargs() */
static void mval_add_to_message(lo_message m, char type,
                                mapper_signal_value_t *value)
{
    switch (type) {
        case 'f':
            lo_message_add_float(m, value->f);
            break;
        case 'd':
            lo_message_add_double(m, value->d);
            break;
        case 'i':
            lo_message_add_int32(m, value->i32);
            break;
        default:
            // Unknown signal type
            assert(0);
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
    mapper_connection_range_t *range = 0;

    while (pa != N_AT_PARAMS)
    {
        /* if parameter is -1, it means to skip this entry */
        if (pa == -1) {
            pa = (mapper_msg_param_t) va_arg(aq, int);
            pa = (mapper_msg_param_t) va_arg(aq, int);
            continue;
        }

        /* Only "extra" is not a real property name */
#ifdef DEBUG
        if (pa >= 0 && pa < N_AT_PARAMS)
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
            mval_add_to_message(m, sig->props.type, sig->props.minimum);
            break;
        case AT_MAX:
            sig = va_arg(aq, mapper_signal);
            mval_add_to_message(m, sig->props.type, sig->props.maximum);
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
        case AT_RANGE:
            range = va_arg(aq, mapper_connection_range_t*);
            if (range->known & CONNECTION_RANGE_SRC_MIN)
                lo_message_add_float(m, range->src_min);
            else
                lo_message_add_string(m, "-");
            if (range->known & CONNECTION_RANGE_SRC_MAX)
                lo_message_add_float(m, range->src_max);
            else
                lo_message_add_string(m, "-");
            if (range->known & CONNECTION_RANGE_DEST_MIN)
                lo_message_add_float(m, range->dest_min);
            else
                lo_message_add_string(m, "-");
            if (range->known & CONNECTION_RANGE_DEST_MAX)
                lo_message_add_float(m, range->dest_max);
            else
                lo_message_add_string(m, "-");
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
        case AT_EXTRA:
            tab = va_arg(aq, table);
            i = 0;
            {
                mapper_osc_value_t *val;
                val = table_value_at_index_p(tab, i++);
                while(val)
                {
                    const char *k = table_key_at_index(tab, i-1);
                    char key[256] = "@";
                    char type[] = "s ";
                    strncpy(&key[1], k, 254);
                    type[1] = val->type;

                    /* Apparently calling lo_message_add simply with
                     * val->value causes errors, so... */
                    switch (val->type) {
                    case 's':
                    case 'S':
                        lo_message_add(m, type, key, &val->value.s); break;
                    case 'f':
                        lo_message_add(m, type, key, val->value.f); break;
                    case 'd':
                        lo_message_add(m, type, key, val->value.d); break;
                    case 'i':
                        lo_message_add(m, type, key, val->value.i); break;
                    case 'h':
                        lo_message_add(m, type, key, val->value.h); break;
                    case 't':
                        lo_message_add(m, type, key, val->value.t); break;
                    case 'c':
                        lo_message_add(m, type, key, val->value.c); break;
                    default:
                        lo_message_add(m, type, key, 0); break;
                    }
                    val = table_value_at_index_p(tab, i++);
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

void mapper_msg_add_osc_value_table(lo_message m, table t)
{
    string_table_node_t *n = t->store;
    int i;
    for (i=0; i<t->len; i++) {
        char keyname[256];
        snprintf(keyname, 256, "@%s", n->key);
        lo_message_add_string(m, keyname);
        mapper_osc_value_t *v = n->value;
        msg_add_lo_arg(m, v->type, &v->value);
        n++;
    }
}

void mapper_msg_prepare_params(lo_message m,
                               mapper_message_t *msg)
{
    mapper_msg_param_t pa = (mapper_msg_param_t) 0;

    for (pa = (mapper_msg_param_t) 0; pa < N_AT_PARAMS; pa = (mapper_msg_param_t) (pa + 1))
    {
        if (!msg->values[pa])
            continue;

        lo_arg *a = *msg->values[pa];
        if (!a)
            continue;

        lo_message_add_string(m, mapper_msg_param_strings[pa]);
        if (pa == AT_RANGE) {
            msg_add_lo_arg(m, msg->types[pa][0], msg->values[pa][0]);
            msg_add_lo_arg(m, msg->types[pa][1], msg->values[pa][1]);
            msg_add_lo_arg(m, msg->types[pa][2], msg->values[pa][2]);
            msg_add_lo_arg(m, msg->types[pa][3], msg->values[pa][3]);
        }
        else {
            msg_add_lo_arg(m, *msg->types[pa], a);
        }
    }
    pa = 0;
    while (msg->extra_args[pa])
    {
        msg_add_lo_arg(m, 's', (lo_arg*) (&msg->extra_args[pa][0]->s));
        msg_add_lo_arg(m, msg->extra_types[pa], *(msg->extra_args[pa]+1));
        pa++;
    }
}

void mapper_link_prepare_osc_message(lo_message m,
                                     mapper_link link)
{
    mapper_msg_add_osc_value_table(m, link->props.extra);
}

void mapper_connection_prepare_osc_message(lo_message m,
                                           mapper_connection con)
{
    if (con->props.mode) {
        lo_message_add_string(m, mapper_msg_param_strings[AT_MODE]);
        lo_message_add_string(m, mapper_mode_type_strings[con->props.mode]);
    }
    if (con->props.expression) {
        lo_message_add_string(m, mapper_msg_param_strings[AT_EXPRESSION]);
        lo_message_add_string(m, con->props.expression);
    }
    if (con->props.range.known & CONNECTION_RANGE_KNOWN) {
        lo_message_add_string(m, mapper_msg_param_strings[AT_RANGE]);
        if (con->props.range.known & CONNECTION_RANGE_SRC_MIN)
            lo_message_add_float(m, con->props.range.src_min);
        else
            lo_message_add_char(m, '-');

        if (con->props.range.known & CONNECTION_RANGE_SRC_MAX)
            lo_message_add_float(m, con->props.range.src_max);
        else
            lo_message_add_char(m, '-');

        if (con->props.range.known & CONNECTION_RANGE_DEST_MIN)
            lo_message_add_float(m, con->props.range.dest_min);
        else
            lo_message_add_char(m, '-');

        if (con->props.range.known & CONNECTION_RANGE_DEST_MAX)
            lo_message_add_float(m, con->props.range.dest_max);
        else
            lo_message_add_char(m, '-');
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

    mapper_msg_add_osc_value_table(m, con->props.extra);
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
