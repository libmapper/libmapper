
#include <string.h>
#include <stdio.h>

#include "types_internal.h"
#include "mapper_internal.h"

const char* mapper_msg_param_strings[] =
{
    "@IP",         /* AT_IP */
    "@port",       /* AT_PORT */
    "@numInputs",  /* AT_NUMINPUTS */
    "@numOutputs", /* AT_NUMOUTPUTS */
    "@numLinks",   /* AT_NUMLINKS */
    "@numConnects",/* AT_NUM_CONNECTIONS */
    "@rev",        /* AT_REV */
    "@type",       /* AT_TYPE */
    "@min",        /* AT_MIN */
    "@max",        /* AT_MAX */
    "@mode",       /* AT_MODE */
    "@expression", /* AT_EXPRESSION */
    "@clipMin",    /* AT_CLIPMIN */
    "@clipMax",    /* AT_CLIPMAX */
    "@range",      /* AT_RANGE */
    "@units",      /* AT_UNITS */
    "@mute",       /* AT_MUTE */
    "@length",     /* AT_LENGTH */
    "@direction",  /* AT_DIRECTION */
    "@instances",  /* AT_INSTANCES */
    "@srcType",    /* AT_SRCTYPE */
    "@destType",   /* AT_DESTTYPE */
    "@srcLength",  /* AT_SRCLENGTH */
    "@destLength", /* AT_DESTLENGTH */
    "",            /* AT_EXTRA (special case, does not represent a
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
        case AT_PORT:
        case AT_NUMINPUTS:
        case AT_NUMOUTPUTS:
        case AT_NUMLINKS:
        case AT_NUMCONNECTIONS:
            i = va_arg(aq, int);
            lo_message_add_int32(m, i);
            break;
        case AT_REV:
            i = va_arg(aq, int);
            lo_message_add_int32(m, i);
            break;
        case AT_TYPE:
        case AT_SRCTYPE:
        case AT_DESTTYPE:
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
        case AT_CLIPMIN:
        case AT_CLIPMAX:
            i = va_arg(aq, int);
            if (i >= 0 && i < N_MAPPER_CLIPPING_TYPES)
                lo_message_add_string(m, mapper_clipping_type_strings[i]);
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
        case AT_LENGTH:
        case AT_SRCLENGTH:
        case AT_DESTLENGTH:
            i = va_arg(aq, int);
            lo_message_add_int32(m, i);
            break;
        case AT_DIRECTION:
            s = va_arg(aq, char*);
            lo_message_add_string(m, s);
            break;
        case AT_INSTANCES:
            i = va_arg(aq, int);
            lo_message_add_int32(m, i);
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
    lo_message_add_string(m, mapper_msg_param_strings[AT_CLIPMIN]);
    lo_message_add_string(m, mapper_clipping_type_strings[con->props.clip_min]);
    lo_message_add_string(m, mapper_msg_param_strings[AT_CLIPMAX]);
    lo_message_add_string(m, mapper_clipping_type_strings[con->props.clip_max]);
    lo_message_add_string(m, mapper_msg_param_strings[AT_MUTE]);
    lo_message_add_int32(m, con->props.muted);
    lo_message_add_string(m, mapper_msg_param_strings[AT_SRCTYPE]);
    lo_message_add_char(m, con->props.src_type);
    lo_message_add_string(m, mapper_msg_param_strings[AT_DESTTYPE]);
    lo_message_add_char(m, con->props.dest_type);
    lo_message_add_string(m, mapper_msg_param_strings[AT_SRCLENGTH]);
    lo_message_add_int32(m, con->props.src_length);
    lo_message_add_string(m, mapper_msg_param_strings[AT_DESTLENGTH]);
    lo_message_add_int32(m, con->props.dest_length);
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
    else
        return -1;

    return -1;
}

mapper_clipping_type mapper_msg_get_clipping(mapper_message_t *msg,
                                             mapper_msg_param_t param)
{
    die_unless(param == AT_CLIPMIN || param == AT_CLIPMAX,
               "bad param in mapper_msg_get_clipping()\n");
    lo_arg **a = mapper_msg_get_param(msg, param);
    if (!a || !*a)
        return -1;

    if (strcmp(&(*a)->s, "none") == 0)
        return CT_NONE;
    if (strcmp(&(*a)->s, "mute") == 0)
        return CT_MUTE;
    if (strcmp(&(*a)->s, "clamp") == 0)
        return CT_CLAMP;
    if (strcmp(&(*a)->s, "fold") == 0)
        return CT_FOLD;
    if (strcmp(&(*a)->s, "wrap") == 0)
        return CT_WRAP;

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
