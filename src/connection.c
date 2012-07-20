
#include <string.h>
#include <math.h>
#include <stdlib.h>
#include <stdio.h>

#include "mapper_internal.h"
#include "types_internal.h"
#include <mapper/mapper.h>

const char* mapper_clipping_type_strings[] =
{
    "none",        /* CT_NONE */
    "mute",        /* CT_MUTE */
    "clamp",       /* CT_CLAMP */
    "fold",        /* CT_FOLD */
    "wrap",        /* CT_WRAP */
};

const char* mapper_mode_type_strings[] =
{
    NULL,          /* MO__UNDEFINED */
    "bypass",      /* MO_BYPASS */
    "linear",      /* MO_LINEAR */
    "expression",  /* MO_EXPRESSION */
    "calibrate",   /* MO_CALIBRATE */
};

const char *mapper_get_clipping_type_string(mapper_clipping_type clipping)
{
    die_unless(clipping < N_MAPPER_CLIPPING_TYPES && clipping >= 0,
               "called mapper_get_clipping_type_string() with "
               "bad parameter.\n");

    return mapper_clipping_type_strings[clipping];
}

const char *mapper_get_mode_type_string(mapper_mode_type mode)
{
    die_unless(mode < N_MAPPER_MODE_TYPES && mode >= 0,
               "called mapper_get_mode_type_string() with "
               "bad parameter.\n");

    return mapper_mode_type_strings[mode];
}

int mapper_connection_perform(mapper_connection connection,
                              mapper_signal sig,
                              mapper_signal_value_t *from_value,
                              mapper_signal_value_t *to_value)
{
    int changed = 0;
    float f = 0;
    
    if (connection->props.muted)
        return 0;

    /* If the destination type is unknown, we can't do anything
     * intelligent here -- even bypass mode might screw up if we
     * assume the types work out. */
    if (connection->props.dest_type != 'f'
        && connection->props.dest_type != 'i')
    {
        return 0;
    }

    if (!connection->props.mode || connection->props.mode == MO_BYPASS)
    {
        if (connection->props.src_type == connection->props.dest_type)
            *to_value = *from_value;
        else if (connection->props.src_type == 'f'
                 && connection->props.dest_type == 'i')
            to_value->i32 = (int)from_value->f;
        else if (connection->props.src_type == 'i'
                 && connection->props.dest_type == 'f')
            to_value->f = (float)from_value->i32;
    }
    else if (connection->props.mode == MO_EXPRESSION
             || connection->props.mode == MO_LINEAR)
    {
        die_unless(connection->expr!=0, "Missing expression.\n");
        *to_value = mapper_expr_evaluate(connection->expr, from_value);
    }

    else if (connection->props.mode == MO_CALIBRATE)
    {
        if (connection->props.src_type == 'f')
            f = from_value->f;
        else if (connection->props.src_type == 'i')
            f = (float)from_value->i32;

        /* If calibration mode has just taken effect, first data
         * sample sets source min and max */
        if (!connection->calibrating) {
            connection->props.range.src_min = f;
            connection->props.range.src_max = f;
            connection->props.range.known |=
                CONNECTION_RANGE_SRC_MIN | CONNECTION_RANGE_SRC_MAX;
            connection->calibrating = 1;
            changed = 1;
        } else {
            if (f < connection->props.range.src_min) {
                connection->props.range.src_min = f;
                connection->props.range.known |= CONNECTION_RANGE_SRC_MIN;
                changed = 1;
            }
            if (f > connection->props.range.src_max) {
                connection->props.range.src_max = f;
                connection->props.range.known |= CONNECTION_RANGE_SRC_MAX;
                changed = 1;
            }
        }

        if (changed) {
            mapper_connection_set_linear_range(connection, sig,
                                               &connection->props.range);

            /* Stay in calibrate mode. */
            connection->props.mode = MO_CALIBRATE;
        }

        if (connection->expr)
            *to_value = mapper_expr_evaluate(connection->expr, from_value);
    }

    return 1;
}

int mapper_clipping_perform(mapper_connection connection,
                            mapper_signal_value_t *from_value,
                            mapper_signal_value_t *to_value)
{
    int muted = 0;
    float v = 0;
    float total_range = fabsf(connection->props.range.dest_max
                              - connection->props.range.dest_min);
    float dest_min, dest_max, difference, modulo_difference;
    mapper_clipping_type clip_min, clip_max;

    if (connection->props.clip_min == CT_NONE
        && connection->props.clip_max == CT_NONE)
    {
        *to_value = *from_value;
        return 1;
    }

    if (connection->props.dest_type == 'f')
        v = from_value->f;
    else if (connection->props.dest_type == 'i')
        v = (float)from_value->i32;
    else {
        trace("unknown type in mapper_clipping_perform()\n");
        return 0;
    }

    if (connection->props.range.known) {
        if (connection->props.range.dest_min <= connection->props.range.dest_max) {
            clip_min = connection->props.clip_min;
            clip_max = connection->props.clip_max;
            dest_min = connection->props.range.dest_min;
            dest_max = connection->props.range.dest_max;
        }
        else {
            clip_min = connection->props.clip_max;
            clip_max = connection->props.clip_min;
            dest_min = connection->props.range.dest_max;
            dest_max = connection->props.range.dest_min;
        }
        if (v < dest_min) {
            switch (clip_min) {
                case CT_MUTE:
                    // need to prevent value from being sent at all
                    muted = 1;
                    break;
                case CT_CLAMP:
                    // clamp value to range minimum
                    v = dest_min;
                    break;
                case CT_FOLD:
                    // fold value around range minimum
                    difference = fabsf(v - dest_min);
                    v = dest_min + difference;
                    if (v > dest_max) {
                        // value now exceeds range maximum!
                        switch (clip_max) {
                            case CT_MUTE:
                                // need to prevent value from being sent at all
                                muted = 1;
                                break;
                            case CT_CLAMP:
                                // clamp value to range minimum
                                v = dest_max;
                                break;
                            case CT_FOLD:
                                // both clip modes are set to fold!
                                difference = fabsf(v - dest_max);
                                modulo_difference = difference
                                    - ((int)(difference / total_range)
                                       * total_range);
                                if ((int)(difference / total_range) % 2 == 0) {
                                    v = dest_max - modulo_difference;
                                }
                                else
                                    v = dest_min + modulo_difference;
                                break;
                            case CT_WRAP:
                                // wrap value back from range minimum
                                difference = fabsf(v - dest_max);
                                modulo_difference = difference
                                    - ((int)(difference / total_range)
                                       * total_range);
                                v = dest_min + modulo_difference;
                                break;
                            default:
                                break;
                        }
                    }
                    break;
                case CT_WRAP:
                    // wrap value back from range maximum
                    difference = fabsf(v - dest_min);
                    modulo_difference = difference
                        - (int)(difference / total_range) * total_range;
                    v = dest_max - modulo_difference;
                    break;
                default:
                    // leave the value unchanged
                    break;
            }
        }
        
        else if (v > dest_max) {
            switch (clip_max) {
                case CT_MUTE:
                    // need to prevent value from being sent at all
                    muted = 1;
                    break;
                case CT_CLAMP:
                    // clamp value to range maximum
                    v = dest_max;
                    break;
                case CT_FOLD:
                    // fold value around range maximum
                    difference = fabsf(v - dest_max);
                    v = dest_max - difference;
                    if (v < dest_min) {
                        // value now exceeds range minimum!
                        switch (clip_min) {
                            case CT_MUTE:
                                // need to prevent value from being sent at all
                                muted = 1;
                                break;
                            case CT_CLAMP:
                                // clamp value to range minimum
                                v = dest_min;
                                break;
                            case CT_FOLD:
                                // both clip modes are set to fold!
                                difference = fabsf(v - dest_min);
                                modulo_difference = difference
                                    - ((int)(difference / total_range)
                                       * total_range);
                                if ((int)(difference / total_range) % 2 == 0) {
                                    v = dest_max + modulo_difference;
                                }
                                else
                                    v = dest_min - modulo_difference;
                                break;
                            case CT_WRAP:
                                // wrap value back from range maximum
                                difference = fabsf(v - dest_min);
                                modulo_difference = difference
                                    - ((int)(difference / total_range)
                                       * total_range);
                                v = dest_max - modulo_difference;
                                break;
                            default:
                                break;
                        }
                    }
                    break;
                case CT_WRAP:
                    // wrap value back from range minimum
                    difference = fabsf(v - dest_max);
                    modulo_difference = difference
                        - (int)(difference / total_range) * total_range;
                    v = dest_min + modulo_difference;
                    break;
                default:
                    break;
            }
        }
    }

    if (connection->props.dest_type == 'f')
        to_value->f = v;
    else if (connection->props.dest_type == 'i')
        to_value->i32 = (int)v;

    return !muted;
}

/* Helper to replace a connection's expression only if the given string
 * parses successfully. Returns 0 on success, non-zero on error. */
static int replace_expression_string(mapper_connection c,
                                     mapper_signal s,
                                     const char *expr_str)
{
    mapper_expr expr = mapper_expr_new_from_string(
        expr_str, s->props.type=='f', c->props.dest_type=='f',
        s->props.length);

    if (!expr)
        return 1;

    if (c->expr)
        mapper_expr_free(c->expr);

    c->expr = expr;
    int len = strlen(expr_str)+1;
    if (!c->props.expression || len > strlen(c->props.expression))
        c->props.expression = realloc(c->props.expression, len);
    strncpy(c->props.expression, expr_str, len);
    return 0;
}

void mapper_connection_set_direct(mapper_connection c)
{
    c->props.mode = MO_BYPASS;
}

void mapper_connection_set_linear_range(mapper_connection c,
                                        mapper_signal sig,
                                        mapper_connection_range_t *r)
{
    char expr[256] = "";
    const char *e = expr;

    if (r->known
        & (CONNECTION_RANGE_SRC_MIN | CONNECTION_RANGE_SRC_MAX))
    {
        if (r->src_min == r->src_max)
            snprintf(expr, 256, "y=%g", r->src_min);

        else if (r->known == CONNECTION_RANGE_KNOWN
                 && r->src_min == r->dest_min
                 && r->src_max == r->dest_max)
            e = strdup("y=x");

        else if (r->known == CONNECTION_RANGE_KNOWN) {
            float scale = ((r->dest_min - r->dest_max)
                           / (r->src_min - r->src_max));
            float offset =
                ((r->dest_max * r->src_min
                  - r->dest_min * r->src_max)
                 / (r->src_min - r->src_max));

            snprintf(expr, 256, "y=x*(%g)+(%g)", scale, offset);
        }
        else
            e = 0;
    }
    else
        e = 0;

    if (&c->props.range != r)
        memcpy(&c->props.range, r,
               sizeof(mapper_connection_range_t));

    // If everything is successful, replace the connection's expression.
    if (e && !replace_expression_string(c, sig, e))
        c->props.mode = MO_LINEAR;
}

void mapper_connection_set_expression(mapper_connection c,
                                      mapper_signal sig,
                                      const char *expr)
{
    if (replace_expression_string(c, sig, expr))
        return;

    c->props.mode = MO_EXPRESSION;
}

void mapper_connection_set_calibrate(mapper_connection c,
                                     mapper_signal sig,
                                     float dest_min, float dest_max)
{
    c->props.mode = MO_CALIBRATE;

    if (c->props.expression)
        free(c->props.expression);

    char expr[256];
    snprintf(expr, 256, "y=%g", dest_min);
    c->props.expression = strdup(expr);

    c->props.range.dest_min = dest_min;
    c->props.range.dest_max = dest_max;
    c->props.range.known |= CONNECTION_RANGE_DEST_MIN | CONNECTION_RANGE_DEST_MAX;
    c->calibrating = 0;
}

/* Helper to fill in the range (src_min, src_max, dest_min, dest_max)
 * based on message parameters and known connection and signal
 * properties; return flags to indicate which parts of the range were
 * found. */
static int get_range(mapper_signal sig, mapper_connection connection,
                     mapper_message_t *msg, float range[4])
{
    lo_arg **a_range    = mapper_msg_get_param(msg, AT_RANGE);
    const char* t_range = mapper_msg_get_type(msg, AT_RANGE);
    lo_arg **a_min      = mapper_msg_get_param(msg, AT_MIN);
    const char* t_min   = mapper_msg_get_type(msg, AT_MIN);
    lo_arg **a_max      = mapper_msg_get_param(msg, AT_MAX);
    const char* t_max   = mapper_msg_get_type(msg, AT_MAX);

    int range_known = 0;

    /* The logic here is to first try to use information from the
     * message, starting with @range, then @min and @max.  Next
     * priority is already-known properties of the connection.
     * Lastly, we fill in source range from the signal. */

    /* @range */

    if (a_range && t_range) {
        int i, known[] = { CONNECTION_RANGE_SRC_MIN, CONNECTION_RANGE_SRC_MAX,
                           CONNECTION_RANGE_DEST_MIN, CONNECTION_RANGE_DEST_MAX };
        for (i=0; i<4; i++) {
            if (t_range[i] == 'f') {
                range_known |= known[i];
                range[i] = a_range[i]->f;
            } else if (t_range[i] == 'i') {
                range_known |= known[i];
                range[i] = (float)a_range[i]->i;
            }
        }
    }

    /* @min, @max */

    if (!(range_known & CONNECTION_RANGE_DEST_MIN)
        && a_min && t_min)
    {
        if (t_min[0]=='f') {
            range_known |= CONNECTION_RANGE_DEST_MIN;
            range[2] = (*a_min)->f;
        } else if (t_min[0]=='i') {
            range_known |= CONNECTION_RANGE_DEST_MIN;
            range[2] = (float)(*a_min)->i;
        }
    }

    if (!(range_known & CONNECTION_RANGE_DEST_MAX)
        && a_max && t_max)
    {
        if (t_max[0]=='f') {
            range_known |= CONNECTION_RANGE_DEST_MAX;
            range[3] = (*a_max)->f;
        } else if (t_max[0]=='i') {
            range_known |= CONNECTION_RANGE_DEST_MAX;
            range[3] = (float)(*a_max)->i;
        }
    }

    /* connection */

    if (connection) {
        if (!(range_known & CONNECTION_RANGE_SRC_MIN)
            && (connection->props.range.known & CONNECTION_RANGE_SRC_MIN))
        {
            range_known |= CONNECTION_RANGE_SRC_MIN;
            range[0] = connection->props.range.src_min;
        }

        if (!(range_known & CONNECTION_RANGE_SRC_MAX)
            && (connection->props.range.known & CONNECTION_RANGE_SRC_MAX))
        {
            range_known |= CONNECTION_RANGE_SRC_MAX;
            range[1] = connection->props.range.src_max;
        }

        if (!(range_known & CONNECTION_RANGE_DEST_MIN)
            && (connection->props.range.known & CONNECTION_RANGE_DEST_MIN))
        {
            range_known |= CONNECTION_RANGE_DEST_MIN;
            range[2] = connection->props.range.dest_min;
        }

        if (!(range_known & CONNECTION_RANGE_DEST_MAX)
            && (connection->props.range.known & CONNECTION_RANGE_DEST_MAX))
        {
            range_known |= CONNECTION_RANGE_DEST_MAX;
            range[3] = connection->props.range.dest_max;
        }
    }

    /* Signal */

    if (sig) {
        if (!(range_known & CONNECTION_RANGE_SRC_MIN)
            && sig->props.minimum)
        {
            if (sig->props.type == 'f') {
                range_known |= CONNECTION_RANGE_SRC_MIN;
                range[0] = sig->props.minimum->f;
            } else if (sig->props.type == 'i') {
                range_known |= CONNECTION_RANGE_SRC_MIN;
                range[0] = sig->props.minimum->i32;
            }
        }

        if (!(range_known & CONNECTION_RANGE_SRC_MAX)
            && sig->props.maximum)
        {
            if (sig->props.type == 'f') {
                range_known |= CONNECTION_RANGE_SRC_MAX;
                range[1] = sig->props.maximum->f;
            } else if (sig->props.type == 'i') {
                range_known |= CONNECTION_RANGE_SRC_MAX;
                range[1] = sig->props.maximum->i32;
            }
        }
    }

    return range_known;
}

void mapper_connection_set_from_message(mapper_connection c,
                                        mapper_signal sig,
                                        mapper_message_t *msg)
{
    /* First record any provided parameters. */

    /* Destination type. */

    const char *dest_type = mapper_msg_get_param_if_char(msg, AT_TYPE);
    if (dest_type)
        c->props.dest_type = dest_type[0];

    /* Range information. */

    float range[4];
    int range_known = get_range(sig, c, msg, range);

    if (range_known & CONNECTION_RANGE_SRC_MIN) {
        c->props.range.known |= CONNECTION_RANGE_SRC_MIN;
        c->props.range.src_min = range[0];
    }

    if (range_known & CONNECTION_RANGE_SRC_MAX) {
        c->props.range.known |= CONNECTION_RANGE_SRC_MAX;
        c->props.range.src_max = range[1];
    }

    if (range_known & CONNECTION_RANGE_DEST_MIN) {
        c->props.range.known |= CONNECTION_RANGE_DEST_MIN;
        c->props.range.dest_min = range[2];
    }

    if (range_known & CONNECTION_RANGE_DEST_MAX) {
        c->props.range.known |= CONNECTION_RANGE_DEST_MAX;
        c->props.range.dest_max = range[3];
    }
    
    // TO DO: test if range has actually changed
    if (c->props.range.known == CONNECTION_RANGE_KNOWN 
        && c->props.mode == MO_LINEAR) {
        mapper_connection_set_linear_range(c, sig, &c->props.range);
    }

    /* Muting. */
    int muting;
    if (!mapper_msg_get_param_if_int(msg, AT_MUTE, &muting))
        c->props.muted = muting;
    
    /* Clipping. */
    int clip_min = mapper_msg_get_clipping(msg, AT_CLIPMIN);
    if (clip_min >= 0)
        c->props.clip_min = clip_min;

    int clip_max = mapper_msg_get_clipping(msg, AT_CLIPMAX);
    if (clip_max >= 0)
        c->props.clip_max = clip_max;
    
    /* Expression. */
    const char *expr = mapper_msg_get_param_if_string(msg, AT_EXPRESSION);
    if (expr)
        replace_expression_string(c, sig, expr);

    /* Extra properties. */
    mapper_msg_add_or_update_extra_params(c->props.extra, msg);

    /* Now set the mode type depending on the requested type and
     * the known properties. */

    int mode = mapper_msg_get_mode(msg);

    switch (mode)
    {
    case -1:
        /* No mode type specified; if mode not yet set, see if 
         we know the range and choose between linear or direct connection. */
            if (c->props.mode == MO_UNDEFINED) {
                if (range_known == CONNECTION_RANGE_KNOWN) {
                    /* We have enough information for a linear connection. */
                    mapper_connection_range_t r;
                    r.src_min = range[0];
                    r.src_max = range[1];
                    r.dest_min = range[2];
                    r.dest_max = range[3];
                    r.known = range_known;
                    mapper_connection_set_linear_range(c, sig, &r);
                } else
                    /* No range, default to direct connection. */
                    mapper_connection_set_direct(c);
            }
        break;
    case MO_BYPASS:
        mapper_connection_set_direct(c);
        break;
    case MO_LINEAR:
        if (range_known == CONNECTION_RANGE_KNOWN) {
            mapper_connection_range_t r;
            r.src_min = range[0];
            r.src_max = range[1];
            r.dest_min = range[2];
            r.dest_max = range[3];
            r.known = range_known;
            mapper_connection_set_linear_range(c, sig, &r);
        }
        break;
    case MO_CALIBRATE:
        if (range_known & (CONNECTION_RANGE_DEST_MIN
                           | CONNECTION_RANGE_DEST_MAX))
            mapper_connection_set_calibrate(c, sig, range[2], range[3]);
        break;
    case MO_EXPRESSION:
        {
            if (!c->props.expression)
                c->props.expression = strdup("y=x");
            mapper_connection_set_expression(c, sig, c->props.expression);
        }
        break;
    default:
        trace("unknown result from mapper_msg_get_mode()\n");
        break;
    }
}

mapper_connection mapper_connection_find_by_names(mapper_device md, 
                                                  const char* src_name,
                                                  const char* dest_name)
{
    mapper_router router = md->routers;
    int i = 0;
    int n = strlen(dest_name);
    const char *slash = strchr(dest_name+1, '/');
    if (slash)
        n = n - strlen(slash);
        
    src_name = strchr(src_name+1, '/');
    
    while (i < md->n_outputs) {
        // Check if device outputs includes src_name
        if (strcmp(md->outputs[i]->props.name, src_name) == 0) {
            while (router != NULL) {
                // find associated router
                if (strncmp(router->props.dest_name, dest_name, n) == 0) {
                    // find associated connection
                    mapper_signal_connection sc = router->connections;
                    while (sc && sc->signal != md->outputs[i])
                        sc = sc->next;
                    if (!sc)
                        return NULL;
                    mapper_connection c = sc->connection;
                    while (c && strcmp(c->props.dest_name,
                                       (dest_name + n)) != 0)
                        c = c->next;
                    if (!c)
                        return NULL;
                    else
                        return c;
                }
                else {
                    router = router->next;
                }
            }
            return NULL;
        }
        i++;
    }
    return NULL;
}
