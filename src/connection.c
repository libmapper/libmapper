
#include <string.h>
#include <math.h>
#include <stdlib.h>
#include <stdio.h>

#include "mapper_internal.h"
#include "types_internal.h"
#include <mapper/mapper.h>

/*! Reallocate memory used by connection. */
static void reallocate_connection_histories(mapper_connection c,
                                            int input_history_size,
                                            int output_history_size);

static void mhist_realloc(mapper_signal_history_t *history,
                          int history_size,
                          int sample_size,
                          int is_output);

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
    NULL,          /* MO_UNDEFINED */
    "bypass",      /* MO_BYPASS */
    "linear",      /* MO_LINEAR */
    "expression",  /* MO_EXPRESSION */
    "calibrate",   /* MO_CALIBRATE */
    "reverse",     /* MO_REVERSE */
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
                              mapper_signal_history_t *from,
                              mapper_signal_history_t *to)
{
    /* Currently expressions on vectors are not supported by the
     * evaluator.  For now, we half-support it by performing
     * element-wise operations on each item in the vector. */

    int changed = 0, i;
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
        /* Increment index position of output data structure. */
        to->position = (to->position + 1) % to->size;
        if (connection->props.src_type == connection->props.dest_type) {
            memcpy(msig_history_value_pointer(*to),
                   msig_history_value_pointer(*from),
                   mapper_type_size(to->type) * to->length);
        }
        else if (connection->props.src_type == 'f'
                 && connection->props.dest_type == 'i') {
            float *vfrom = msig_history_value_pointer(*from);
            int *vto = msig_history_value_pointer(*to);
            for (i = 0; i < to->length; i++) {
                vto[i] = (int)vfrom[i];
            }
        }
        else if (connection->props.src_type == 'i'
                 && connection->props.dest_type == 'f') {
            int *vfrom = msig_history_value_pointer(*from);
            float *vto = msig_history_value_pointer(*to);
            for (i = 0; i < to->length; i++) {
                vto[i] = (float)vfrom[i];
            }
        }
        return 1;
    }
    else if (connection->props.mode == MO_EXPRESSION
             || connection->props.mode == MO_LINEAR)
    {
        die_unless(connection->expr!=0, "Missing expression.\n");
        return (mapper_expr_evaluate(connection->expr, from, to));
    }

    else if (connection->props.mode == MO_CALIBRATE)
    {
        /* TODO: Switch to vector min and max */
        /* Increment index position of output data structure. */
        to->position = (to->position + 1) % to->size;
        if (connection->props.src_type == 'f') {
            float *v = msig_history_value_pointer(*from);
            for (i = 0; i < to->length; i++)
                f = v[i];
        }
        else if (connection->props.src_type == 'i') {
            int *v = msig_history_value_pointer(*from);
            for (i = 0; i < to->length; i++)
                f = (float)v[i];
        }

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
            mapper_connection_set_linear_range(connection,
                                               &connection->props.range);

            /* Stay in calibrate mode. */
            connection->props.mode = MO_CALIBRATE;
        }

        if (connection->expr)
            return (mapper_expr_evaluate(connection->expr, from, to));
        else
            return 0;
    }
    return 1;
}

int mapper_clipping_perform(mapper_connection connection,
                            mapper_signal_history_t *history)
{
    /* TODO: We are currently saving the clipped values to output history.
     * it needs to be decided whether clipping should be inside the
     * feedback loop when past samples are called in expressions. */
    int i, muted = 0;
    float v[connection->props.dest_length];
    float total_range = fabsf(connection->props.range.dest_max
                              - connection->props.range.dest_min);
    float dest_min, dest_max, difference, modulo_difference;
    mapper_clipping_type clip_min, clip_max;

    if (connection->props.clip_min == CT_NONE
        && connection->props.clip_max == CT_NONE)
    {
        return 1;
    }

    if (connection->props.dest_type == 'f') {
        float *vhistory = msig_history_value_pointer(*history);
        for (i = 0; i < history->length; i++)
            v[i] = vhistory[i];
    }
    else if (connection->props.dest_type == 'i') {
        int *vhistory = msig_history_value_pointer(*history);
        for (i = 0; i < history->length; i++)
            v[i] = (float)vhistory[i];
    }
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
        for (i = 0; i < history->length; i++) {
            if (v[i] < dest_min) {
                switch (clip_min) {
                    case CT_MUTE:
                        // need to prevent value from being sent at all
                        muted = 1;
                        break;
                    case CT_CLAMP:
                        // clamp value to range minimum
                        v[i] = dest_min;
                        break;
                    case CT_FOLD:
                        // fold value around range minimum
                        difference = fabsf(v[i] - dest_min);
                        v[i] = dest_min + difference;
                        if (v[i] > dest_max) {
                            // value now exceeds range maximum!
                            switch (clip_max) {
                                case CT_MUTE:
                                    // need to prevent value from being sent at all
                                    muted = 1;
                                    break;
                                case CT_CLAMP:
                                    // clamp value to range minimum
                                    v[i] = dest_max;
                                    break;
                                case CT_FOLD:
                                    // both clip modes are set to fold!
                                    difference = fabsf(v[i] - dest_max);
                                    modulo_difference = difference
                                        - ((int)(difference / total_range)
                                           * total_range);
                                    if ((int)(difference / total_range) % 2 == 0) {
                                        v[i] = dest_max - modulo_difference;
                                    }
                                    else
                                        v[i] = dest_min + modulo_difference;
                                    break;
                                case CT_WRAP:
                                    // wrap value back from range minimum
                                    difference = fabsf(v[i] - dest_max);
                                    modulo_difference = difference
                                        - ((int)(difference / total_range)
                                           * total_range);
                                    v[i] = dest_min + modulo_difference;
                                    break;
                                default:
                                    break;
                            }
                        }
                        break;
                    case CT_WRAP:
                        // wrap value back from range maximum
                        difference = fabsf(v[i] - dest_min);
                        modulo_difference = difference
                            - (int)(difference / total_range) * total_range;
                        v[i] = dest_max - modulo_difference;
                        break;
                    default:
                        // leave the value unchanged
                        break;
                }
            }
            else if (v[i] > dest_max) {
                switch (clip_max) {
                    case CT_MUTE:
                        // need to prevent value from being sent at all
                        muted = 1;
                        break;
                    case CT_CLAMP:
                        // clamp value to range maximum
                        v[i] = dest_max;
                        break;
                    case CT_FOLD:
                        // fold value around range maximum
                        difference = fabsf(v[i] - dest_max);
                        v[i] = dest_max - difference;
                        if (v[i] < dest_min) {
                            // value now exceeds range minimum!
                            switch (clip_min) {
                                case CT_MUTE:
                                    // need to prevent value from being sent at all
                                    muted = 1;
                                    break;
                                case CT_CLAMP:
                                    // clamp value to range minimum
                                    v[i] = dest_min;
                                    break;
                                case CT_FOLD:
                                    // both clip modes are set to fold!
                                    difference = fabsf(v[i] - dest_min);
                                    modulo_difference = difference
                                        - ((int)(difference / total_range)
                                           * total_range);
                                    if ((int)(difference / total_range) % 2 == 0) {
                                        v[i] = dest_max + modulo_difference;
                                    }
                                    else
                                        v[i] = dest_min - modulo_difference;
                                    break;
                                case CT_WRAP:
                                    // wrap value back from range maximum
                                    difference = fabsf(v[i] - dest_min);
                                    modulo_difference = difference
                                        - ((int)(difference / total_range)
                                           * total_range);
                                    v[i] = dest_max - modulo_difference;
                                    break;
                                default:
                                    break;
                            }
                        }
                        break;
                    case CT_WRAP:
                        // wrap value back from range minimum
                        difference = fabsf(v[i] - dest_max);
                        modulo_difference = difference
                            - (int)(difference / total_range) * total_range;
                        v[i] = dest_min + modulo_difference;
                        break;
                    default:
                        break;
                }
            }
        }
    }

    if (connection->props.dest_type == 'f') {
        float *vhistory = msig_history_value_pointer(*history);
        for (i = 0; i < history->length; i++)
            vhistory[i] = v[i];
    }
    else if (connection->props.dest_type == 'i') {
        int *vhistory = msig_history_value_pointer(*history);
        for (i = 0; i < history->length; i++)
            vhistory[i] = (int)v[i];
    }
    return !muted;
}

/* Helper to replace a connection's expression only if the given string
 * parses successfully. Returns 0 on success, non-zero on error. */
static int replace_expression_string(mapper_connection c,
                                     const char *expr_str,
                                     int *input_history_size,
                                     int *output_history_size)
{
    mapper_expr expr = mapper_expr_new_from_string(
        expr_str, c->props.src_type=='f', c->props.dest_type=='f',
        c->props.src_length, input_history_size, output_history_size);

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
    reallocate_connection_histories(c, 1, 1);
}

void mapper_connection_set_linear_range(mapper_connection c,
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
    if (e) {
        int input_history_size, output_history_size;
        if (!replace_expression_string(c, e, &input_history_size,
                                       &output_history_size)) {
            reallocate_connection_histories(c, 1, 1);
            c->props.mode = MO_LINEAR;
        }
    }
}

void mapper_connection_set_expression(mapper_connection c,
                                      const char *expr)
{
    int input_history_size, output_history_size;
    if (replace_expression_string(c, expr, &input_history_size,
                                  &output_history_size))
        return;

    c->props.mode = MO_EXPRESSION;
    reallocate_connection_histories(c, input_history_size,
                                    output_history_size);
}

void mapper_connection_set_reverse(mapper_connection c)
{
    c->props.mode = MO_REVERSE;
}

void mapper_connection_set_calibrate(mapper_connection c,
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
static int get_range(mapper_connection connection,
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
    mapper_signal sig = connection->parent->signal;
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
                                        mapper_message_t *msg)
{
    /* First record any provided parameters. */

    /* Destination type. */

    const char *dest_type = mapper_msg_get_param_if_char(msg, AT_TYPE);
    if (dest_type)
        c->props.dest_type = dest_type[0];

    /* Range information. */

    float range[4];
    int range_known = get_range(c, msg, range);

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
        mapper_connection_set_linear_range(c, &c->props.range);
    }

    /* Muting. */
    int muting;
    if (!mapper_msg_get_param_if_int(msg, AT_MUTE, &muting))
        c->props.muted = muting;

    /* Clipping. */
    int clip_min = mapper_msg_get_clipping(msg, AT_CLIP_MIN);
    if (clip_min >= 0)
        c->props.clip_min = clip_min;

    int clip_max = mapper_msg_get_clipping(msg, AT_CLIP_MAX);
    if (clip_max >= 0)
        c->props.clip_max = clip_max;

    /* Expression. */
    const char *expr = mapper_msg_get_param_if_string(msg, AT_EXPRESSION);
    if (expr) {
        int input_history_size, output_history_size;
        if (!replace_expression_string(c, expr, &input_history_size,
                                       &output_history_size)) {
            if (c->props.mode == MO_EXPRESSION)
                reallocate_connection_histories(c, input_history_size,
                                                output_history_size);
        }
    }

    /* Instances. */
    int send_as_instance;
    if (!mapper_msg_get_param_if_int(msg, AT_SEND_AS_INSTANCE, &send_as_instance))
        c->props.send_as_instance = send_as_instance;

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
                    mapper_connection_set_linear_range(c, &r);
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
            mapper_connection_set_linear_range(c, &r);
        }
        break;
    case MO_CALIBRATE:
        if (range_known & (CONNECTION_RANGE_DEST_MIN
                           | CONNECTION_RANGE_DEST_MAX))
            mapper_connection_set_calibrate(c, range[2], range[3]);
        break;
    case MO_EXPRESSION:
        {
            if (!c->props.expression)
                c->props.expression = strdup("y=x");
            mapper_connection_set_expression(c, c->props.expression);
        }
        break;
    case MO_REVERSE:
        mapper_connection_set_reverse(c);
        break;
    default:
        trace("unknown result from mapper_msg_get_mode()\n");
        break;
    }
}

void reallocate_connection_histories(mapper_connection c,
                                     int input_history_size,
                                     int output_history_size)
{
    mapper_signal sig = c->parent->signal;
    int i;

    // At least for now, exit if this is an input signal
    if (!sig->props.is_output)
        return;

    // If there is no expression, then no memory needs to be
    // reallocated.
    if (!c->expr)
        return;

    if (input_history_size < 1)
        input_history_size = 1;

    if (input_history_size > sig->props.history_size) {
        int sample_size = msig_vector_bytes(sig);
        for (i=0; i<sig->props.num_instances; i++) {
            mhist_realloc(&c->parent->history[i], input_history_size,
                          sample_size, 0);
        }
        sig->props.history_size = input_history_size;
    }
    else if (input_history_size < sig->props.history_size) {
        // Do nothing for now...
        // Find maximum input length needed for connections
        /*mapper_connection temp = c->parent->connections;
        while (c) {
            if (c->props.mode == MO_EXPRESSION) {
                if (c->expr->input_history_size > input_history_size) {
                    input_history_size = c->expr->input_history_size;
                }
            }
            c = c->next;
        }*/
    }
    if (output_history_size > c->props.dest_history_size) {
        int sample_size = mapper_type_size(c->props.dest_type) * c->props.dest_length;
        for (i=0; i<sig->props.num_instances; i++) {
            mhist_realloc(&c->history[i], output_history_size, sample_size, 1);
        }
        c->props.dest_history_size = output_history_size;
    }
    else if (output_history_size < mapper_expr_output_history_size(c->expr)) {
        // Do nothing for now...
    }
}

void mhist_realloc(mapper_signal_history_t *history,
                   int history_size,
                   int sample_size,
                   int is_output)
{
    if (!history || !history_size || !sample_size)
        return;
    if (history_size == history->size)
        return;
    if (is_output || (history_size > history->size) || (history->position == 0)) {
        // realloc in place
        history->value = realloc(history->value, history_size * sample_size);
        history->timetag = realloc(history->timetag, history_size * sizeof(mapper_timetag_t));
        if (is_output) {
            history->position = -1;
        }
        else if (history->position != 0) {
            int new_position = history_size - history->size + history->position;
            memcpy(history->value + sample_size * new_position,
                   history->value + sample_size * history->position,
                   sample_size * (history->size - history->position));
            memcpy(&history->timetag[new_position],
                   &history->timetag[history->position], sizeof(mapper_timetag_t)
                   * (history->size - history->position));
            history->position = new_position;
        }
    }
    else {
        // copying into smaller array
        if (history->position >= history_size * 2) {
            // no overlap - memcpy ok
            int new_position = history_size - history->size + history->position;
            memcpy(history->value,
                   history->value + sample_size * (new_position - history_size),
                   sample_size * history_size);
            memcpy(&history->timetag,
                   &history->timetag[history->position - history_size],
                   sizeof(mapper_timetag_t) * history_size);
            history->value = realloc(history->value, history_size * sample_size);
            history->timetag = realloc(history->timetag, history_size * sizeof(mapper_timetag_t));
        }
        else {
            // there is overlap between new and old arrays - need to allocate new memory
            mapper_signal_history_t temp;
            temp.value = malloc(sample_size * history_size);
            temp.timetag = malloc(sizeof(mapper_timetag_t) * history_size);
            if (history->position < history_size) {
                memcpy(temp.value, history->value,
                       sample_size * history->position);
                memcpy(temp.value + sample_size * history->position,
                       history->value + sample_size
                       * (history->size - history_size + history->position),
                       sample_size * (history_size - history->position));
                memcpy(temp.timetag, history->timetag,
                       sizeof(mapper_timetag_t) * history->position);
                memcpy(&temp.timetag[history->position],
                       &history->timetag[history->size - history_size + history->position],
                       sizeof(mapper_timetag_t) * (history_size - history->position));
            }
            else {
                memcpy(temp.value, history->value + sample_size
                       * (history->position - history_size),
                       sample_size * history_size);
                memcpy(temp.timetag,
                       &history->timetag[history->position - history_size],
                       sizeof(mapper_timetag_t) * history_size);
                history->position = history_size - 1;
            }
            free(history->value);
            free(history->timetag);
            history->value = temp.value;
            history->timetag = temp.timetag;
        }
    }
    history->size = history_size;
}
