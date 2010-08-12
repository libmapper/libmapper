
#include <string.h>

#include "types_internal.h"
#include "mapper_internal.h"

const char* mapper_msg_param_strings[] =
{
    "@IP",         /* AT_IP */
    "@port",       /* AT_PORT */
    "@canAlias",   /* AT_CANALIAS */
    "@numInputs",  /* AT_NUMINPUTS */
    "@numOutputs", /* AT_NUMOUTPUTS */
    "@hash",       /* AT_HASH */
    "@type",       /* AT_TYPE */
    "@min",        /* AT_MIN */
    "@max",        /* AT_MAX */
    "@scaling",    /* AT_SCALING */
    "@expression", /* AT_EXPRESSION */
    "@clipMin",    /* AT_CLIPMIN */
    "@clipMax",    /* AT_CLIPMAX */
    "@range",      /* AT_RANGE */
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
            /* unknown parameter */
            trace("message %s, unknown parameter '%s'\n",
                  path, &argv[i]->s);
            return 1;
        }

        /* special case: range has 4 float or int parameters */
        // TODO: handle 'invert' and '-'
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
                if (types[i] != 'i' && types[i] != 'f') {
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
    float f;
    char t[] = " ";
    mapper_signal sig;
    mapper_msg_param_t pa = (mapper_msg_param_t) va_arg(aq, int);

    while (pa != N_AT_PARAMS)
    {
        /* if parameter is -1, it means to skip this entry */
        if (pa == -1) {
            pa = (mapper_msg_param_t) va_arg(aq, int);
            pa = (mapper_msg_param_t) va_arg(aq, int);
            continue;
        }

#ifdef DEBUG
        if (pa >= 0 && pa < N_AT_PARAMS)
#endif
            lo_message_add_string(m, mapper_msg_param_strings[pa]);

        switch (pa) {
        case AT_IP:
            s = va_arg(aq, char*);
            lo_message_add_string(m, s);
            break;
        case AT_PORT:
            i = va_arg(aq, int);
            lo_message_add_int32(m, i);
            break;
        case AT_CANALIAS:
            i = va_arg(aq, int);
            if (i)
                lo_message_add_string(m, "yes");
            else
                lo_message_add_string(m, "no");
            break;
        case AT_NUMINPUTS:
            i = va_arg(aq, int);
            lo_message_add_int32(m, i);
            break;
        case AT_NUMOUTPUTS:
            i = va_arg(aq, int);
            lo_message_add_int32(m, i);
            break;
        case AT_HASH:
            i = va_arg(aq, int);
            lo_message_add_int32(m, i);
            break;
        case AT_TYPE:
            i = va_arg(aq, int);
            t[0] = (char)i;
            lo_message_add_string(m, t);
            break;
        case AT_MIN:
            sig = va_arg(aq, mapper_signal);
            mval_add_to_message(m, sig, sig->minimum);
            break;
        case AT_MAX:
            sig = va_arg(aq, mapper_signal);
            mval_add_to_message(m, sig, sig->maximum);
            break;
        case AT_SCALING:
            // TODO: enumerate scaling types
            s = va_arg(aq, char*);
            lo_message_add_string(m, s);
            break;
        case AT_EXPRESSION:
            s = va_arg(aq, char*);
            lo_message_add_string(m, s);
            break;
        case AT_CLIPMIN:
            // TODO: enumerate clipping types
            s = va_arg(aq, char*);
            lo_message_add_string(m, s);
            break;
        case AT_CLIPMAX:
            s = va_arg(aq, char*);
            lo_message_add_string(m, s);
            break;
        case AT_RANGE:
            f = va_arg(aq, double);
            lo_message_add_float(m, f);
            f = va_arg(aq, double);
            lo_message_add_float(m, f);
            f = va_arg(aq, double);
            lo_message_add_float(m, f);
            f = va_arg(aq, double);
            lo_message_add_float(m, f);
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
        switch (pa) {
        case AT_CANALIAS:
            if (a->i)
                lo_message_add_string(m, "yes");
            else
                lo_message_add_string(m, "no");
            break;
        case AT_RANGE:
            msg_add_lo_arg(m, msg->types[pa][0], msg->values[pa][0]);
            msg_add_lo_arg(m, msg->types[pa][1], msg->values[pa][1]);
            msg_add_lo_arg(m, msg->types[pa][2], msg->values[pa][2]);
            msg_add_lo_arg(m, msg->types[pa][3], msg->values[pa][3]);
            break;
        default:
            msg_add_lo_arg(m, *msg->types[pa], a);
            break;
        }
    }
}

void mapper_mapping_prepare_osc_message(lo_message m, mapper_mapping map)
{
    lo_message_add_string(m, mapper_msg_param_strings[AT_SCALING]);
    lo_message_add_string(m, mapper_scaling_type_strings[map->props.scaling]);
    lo_message_add_string(m, mapper_msg_param_strings[AT_EXPRESSION]);
    lo_message_add_string(m, map->props.expression);
    lo_message_add_string(m, mapper_msg_param_strings[AT_RANGE]);
    lo_message_add_float(m, map->props.range.src_min);
    lo_message_add_float(m, map->props.range.src_max);
    lo_message_add_float(m, map->props.range.dest_min);
    lo_message_add_float(m, map->props.range.dest_max);
    lo_message_add_string(m, mapper_msg_param_strings[AT_CLIPMIN]);
    lo_message_add_string(m, mapper_clipping_type_strings[map->props.clip_lower]);
    lo_message_add_string(m, mapper_msg_param_strings[AT_CLIPMAX]);
    lo_message_add_string(m, mapper_clipping_type_strings[map->props.clip_upper]);
}

mapper_scaling_type mapper_msg_get_scaling(mapper_message_t *msg)
{
    lo_arg **a = mapper_msg_get_param(msg, AT_SCALING);
    if (!a || !*a)
        return -1;

    if (strcmp(&(*a)->s, "bypass") == 0)
        return SC_BYPASS;
    if (strcmp(&(*a)->s, "linear") == 0)
        return SC_LINEAR;
    if (strcmp(&(*a)->s, "expression") == 0)
        return SC_EXPRESSION;
    if (strcmp(&(*a)->s, "calibrate") == 0)
        return SC_CALIBRATE;

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
