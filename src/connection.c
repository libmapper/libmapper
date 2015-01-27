
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
static int mapper_connection_set_mode_linear(mapper_connection c);

const char* mapper_boundary_action_strings[] =
{
    "none",        /* BA_NONE */
    "mute",        /* BA_MUTE */
    "clamp",       /* BA_CLAMP */
    "fold",        /* BA_FOLD */
    "wrap",        /* BA_WRAP */
};

const char* mapper_mode_type_strings[] =
{
    NULL,          /* MO_UNDEFINED */
    "none",        /* MO_NONE */
    "raw",         /* MO_RAW */
    "linear",      /* MO_LINEAR */
    "expression",  /* MO_EXPRESSION */
};

const char *mapper_get_boundary_action_string(mapper_boundary_action bound)
{
    die_unless(bound < N_MAPPER_BOUNDARY_ACTIONS,
               "called mapper_get_boundary_action_string() with "
               "bad parameter.\n");

    return mapper_boundary_action_strings[bound];
}

const char *mapper_get_mode_type_string(mapper_mode_type mode)
{
    die_unless(mode < N_MAPPER_MODE_TYPES,
               "called mapper_get_mode_type_string() with "
               "bad parameter.\n");

    return mapper_mode_type_strings[mode];
}

int mapper_connection_perform(mapper_connection c,
                              mapper_signal_history_t *from,
                              mapper_signal_history_t **expr_vars,
                              mapper_signal_history_t *to,
                              char *typestring)
{
    int changed = 0, i;
    int vector_length = from->length < to->length ? from->length : to->length;

    if (c->props.calibrating == 1)
    {
        if (!c->props.local_min) {
            c->props.local_min =
                malloc(c->props.local_length *
                       mapper_type_size(c->props.local_type));
        }
        if (!c->props.local_max) {
            c->props.local_max =
                malloc(c->props.local_length *
                       mapper_type_size(c->props.local_type));
        }

        /* If calibration mode has just taken effect, first data
         * sample sets source min and max */
        if (c->props.local_type == 'f') {
            float *v = msig_history_value_pointer(*from);
            float *src_min = (float*)c->props.local_min;
            float *src_max = (float*)c->props.local_max;
            if (!c->calibrating) {
                for (i = 0; i < from->length; i++) {
                    src_min[i] = v[i];
                    src_max[i] = v[i];
                }
                c->calibrating = 1;
                changed = 1;
            }
            else {
                for (i = 0; i < from->length; i++) {
                    if (v[i] < src_min[i]) {
                        src_min[i] = v[i];
                        changed = 1;
                    }
                    if (v[i] > src_max[i]) {
                        src_max[i] = v[i];
                        changed = 1;
                    }
                }
            }
        }
        else if (c->props.local_type == 'i') {
            int *v = msig_history_value_pointer(*from);
            int *src_min = (int*)c->props.local_min;
            int *src_max = (int*)c->props.local_max;
            if (!c->calibrating) {
                for (i = 0; i < from->length; i++) {
                    src_min[i] = v[i];
                    src_max[i] = v[i];
                }
                c->calibrating = 1;
                changed = 1;
            }
            else {
                for (i = 0; i < from->length; i++) {
                    if (v[i] < src_min[i]) {
                        src_min[i] = v[i];
                        changed = 1;
                    }
                    if (v[i] > src_max[i]) {
                        src_max[i] = v[i];
                        changed = 1;
                    }
                }
            }
        }
        else if (c->props.local_type == 'd') {
            double *v = msig_history_value_pointer(*from);
            double *src_min = (double*)c->props.local_min;
            double *src_max = (double*)c->props.local_max;
            if (!c->calibrating) {
                for (i = 0; i < from->length; i++) {
                    src_min[i] = v[i];
                    src_max[i] = v[i];
                }
                c->calibrating = 1;
                changed = 1;
            }
            else {
                for (i = 0; i < from->length; i++) {
                    if (v[i] < src_min[i]) {
                        src_min[i] = v[i];
                        changed = 1;
                    }
                    if (v[i] > src_max[i]) {
                        src_max[i] = v[i];
                        changed = 1;
                    }
                }
            }
        }

        if (changed && c->props.mode == MO_LINEAR)
            mapper_connection_set_mode_linear(c);
    }

    if (c->props.muted)
        return 0;

    else if (c->props.mode == MO_RAW) {
        // No type coercion, skip expression
        for (i = 0; i < vector_length; i++)
            typestring[i] = c->props.local_type;
        return 1;
    }

    die_unless(c->expr!=0, "Missing expression.\n");
    return (mapper_expr_evaluate(c->expr, from, expr_vars,
                                 to, typestring, 0, 0));
}

int mapper_boundary_perform(mapper_connection c,
                            mapper_signal_history_t *history)
{
    /* TODO: We are currently saving the processed values to output history.
     * it needs to be decided whether boundary processing should be inside the
     * feedback loop when past samples are called in expressions. */
    int i, muted = 0;

    double value;
    double dest_min, dest_max, swap, total_range, difference, modulo_difference;
    mapper_boundary_action bound_min, bound_max;

    if (c->props.bound_min == BA_NONE
        && c->props.bound_max == BA_NONE)
    {
        return 1;
    }
    if (!c->props.remote_min
        && (c->props.bound_min != BA_NONE || c->props.bound_max == BA_WRAP)) {
        return 1;
    }
    if (!c->props.remote_max
        && (c->props.bound_max != BA_NONE || c->props.bound_min == BA_WRAP)) {
        return 1;
    }

    for (i = 0; i < history->length; i++) {
        value = propval_get_double(msig_history_value_pointer(*history),
                                   c->props.remote_type, i);
        dest_min = propval_get_double(c->props.remote_min,
                                      c->props.remote_type, i);
        dest_max = propval_get_double(c->props.remote_max,
                                      c->props.remote_type, i);
        if (dest_min < dest_max) {
            bound_min = c->props.bound_min;
            bound_max = c->props.bound_max;
        }
        else {
            bound_min = c->props.bound_max;
            bound_max = c->props.bound_min;
            swap = dest_max;
            dest_max = dest_min;
            dest_min = swap;
        }
        total_range = fabs(dest_max - dest_min);
        if (value < dest_min) {
            switch (bound_min) {
                case BA_MUTE:
                    // need to prevent value from being sent at all
                    muted = 1;
                    break;
                case BA_CLAMP:
                    // clamp value to range minimum
                    value = dest_min;
                    break;
                case BA_FOLD:
                    // fold value around range minimum
                    difference = fabsf(value - dest_min);
                    value = dest_min + difference;
                    if (value > dest_max) {
                        // value now exceeds range maximum!
                        switch (bound_max) {
                            case BA_MUTE:
                                // need to prevent value from being sent at all
                                muted = 1;
                                break;
                            case BA_CLAMP:
                                // clamp value to range minimum
                                value = dest_max;
                                break;
                            case BA_FOLD:
                                // both boundary actions are set to fold!
                                difference = fabsf(value - dest_max);
                                modulo_difference = difference
                                    - ((int)(difference / total_range)
                                       * total_range);
                                if ((int)(difference / total_range) % 2 == 0) {
                                    value = dest_max - modulo_difference;
                                }
                                else
                                    value = dest_min + modulo_difference;
                                break;
                            case BA_WRAP:
                                // wrap value back from range minimum
                                difference = fabsf(value - dest_max);
                                modulo_difference = difference
                                    - ((int)(difference / total_range)
                                       * total_range);
                                value = dest_min + modulo_difference;
                                break;
                            default:
                                break;
                        }
                    }
                    break;
                case BA_WRAP:
                    // wrap value back from range maximum
                    difference = fabsf(value - dest_min);
                    modulo_difference = difference
                        - (int)(difference / total_range) * total_range;
                    value = dest_max - modulo_difference;
                    break;
                default:
                    // leave the value unchanged
                    break;
            }
        }
        else if (value > dest_max) {
            switch (bound_max) {
                case BA_MUTE:
                    // need to prevent value from being sent at all
                    muted = 1;
                    break;
                case BA_CLAMP:
                    // clamp value to range maximum
                    value = dest_max;
                    break;
                case BA_FOLD:
                    // fold value around range maximum
                    difference = fabsf(value - dest_max);
                    value = dest_max - difference;
                    if (value < dest_min) {
                        // value now exceeds range minimum!
                        switch (bound_min) {
                            case BA_MUTE:
                                // need to prevent value from being sent at all
                                muted = 1;
                                break;
                            case BA_CLAMP:
                                // clamp value to range minimum
                                value = dest_min;
                                break;
                            case BA_FOLD:
                                // both boundary actions are set to fold!
                                difference = fabsf(value - dest_min);
                                modulo_difference = difference
                                    - ((int)(difference / total_range)
                                       * total_range);
                                if ((int)(difference / total_range) % 2 == 0) {
                                    value = dest_max + modulo_difference;
                                }
                                else
                                    value = dest_min - modulo_difference;
                                break;
                            case BA_WRAP:
                                // wrap value back from range maximum
                                difference = fabsf(value - dest_min);
                                modulo_difference = difference
                                    - ((int)(difference / total_range)
                                       * total_range);
                                value = dest_max - modulo_difference;
                                break;
                            default:
                                break;
                        }
                    }
                    break;
                case BA_WRAP:
                    // wrap value back from range minimum
                    difference = fabsf(value - dest_max);
                    modulo_difference = difference
                        - (int)(difference / total_range) * total_range;
                    value = dest_min + modulo_difference;
                    break;
                default:
                    break;
            }
        }
        propval_set_double(msig_history_value_pointer(*history),
                           c->props.remote_type, i, value);
    }
    return !muted;
}

// TODO: call boundary methods
int mapper_connection_combine(mapper_connection c, int instance)
{
    // call expression evaluator with connection data, signal value pointer
    // TODO: add typestring
    if (!cb->expr)
        return 0;

    return mapper_expr_evaluate(cb->expr, 0, c->expr_vars,
                                &c->history[instance], 0, c, instance);
}

/*! Build a value update message for a given connection. */
lo_message mapper_connection_build_message(mapper_connection c, void *value,
                                           int count, char *typestring,
                                           mapper_id_map id_map)
{
    mapper_connection_props props = &c->props;
    int i, length = props->remote_length * count;

    lo_message m = lo_message_new();
    if (!m)
        return 0;

    if (value && typestring) {
        for (i = 0; i < length; i++) {
            switch (typestring[i]) {
                case 'i':
                    lo_message_add_int32(m, ((int*)value)[i]);
                    break;
                case 'f':
                    lo_message_add_float(m, ((float*)value)[i]);
                    break;
                case 'd':
                    lo_message_add_double(m, ((double*)value)[i]);
                    break;
                case 'N':
                    lo_message_add_nil(m);
                    break;
                default:
                    break;
            }
        }
    }
    else if (id_map) {
        for (i = 0; i < length; i++)
            lo_message_add_nil(m);
    }

    if (props->send_as_instance && id_map) {
        lo_message_add_string(m, "@instance");
        lo_message_add_int32(m, id_map->origin);
        lo_message_add_int32(m, id_map->public);
    }

    if (props->slot >= 0) {
        // add slot
        lo_message_add_string(m, "@slot");
        lo_message_add_int32(m, props->slot);
    }

    return m;
}

/* Helper to replace a connection's expression only if the given string
 * parses successfully. Returns 0 on success, non-zero on error. */
static int replace_expression_string(mapper_connection c,
                                     const char *expr_str,
                                     int *input_history_size,
                                     int *output_history_size)
{
    mapper_expr expr = mapper_expr_new_from_string(
        expr_str, c->props.local_type, c->props.remote_type,
        c->props.local_length, c->props.remote_length, 0);

    if (!expr)
        return 1;

    *input_history_size = mapper_expr_input_history_size(expr);
    *output_history_size = mapper_expr_output_history_size(expr);

    if (c->expr)
        mapper_expr_free(c->expr);

    c->expr = expr;

    if (c->props.expression == expr_str)
        return 0;

    int len = strlen(expr_str);
    if (!c->props.expression || len > strlen(c->props.expression))
        c->props.expression = realloc(c->props.expression, len+1);

    /* Using strncpy() here causes memory profiling errors due to possible
     * overlapping memory (e.g. expr_str == c->props.expression). */
    memcpy(c->props.expression, expr_str, len);
    c->props.expression[len] = '\0';

    return 0;
}

void mapper_connection_set_mode_raw(mapper_connection c)
{
    c->props.mode = MO_RAW;
    reallocate_connection_histories(c, 1, 1);
}

static int mapper_connection_set_mode_linear(mapper_connection c)
{
    int i, len;
    char expr[256] = "";
    const char *e = expr;

    if (   !c->props.local_min  || !c->props.local_max
        || !c->props.remote_min || !c->props.remote_max)
        return 1;

    int min_length = c->props.local_length < c->props.remote_length ?
                     c->props.local_length : c->props.remote_length;
    double src_min, src_max, dest_min, dest_max;

    if (c->props.remote_length == c->props.local_length)
        snprintf(expr, 256, "y=x*");
    else if (c->props.remote_length > c->props.local_length) {
        if (min_length == 1)
            snprintf(expr, 256, "y[0]=x*");
        else
            snprintf(expr, 256, "y[0:%i]=x*", min_length-1);
    }
    else {
        if (min_length == 1)
            snprintf(expr, 256, "y=x[0]*");
        else
            snprintf(expr, 256, "y=x[0:%i]*", min_length-1);
    }

    if (min_length > 1) {
        len = strlen(expr);
        snprintf(expr+len, 256-len, "[");
    }

    for (i = 0; i < min_length; i++) {
        // get multiplier
        src_min = propval_get_double(c->props.local_min,
                                     c->props.local_type, i);
        src_max = propval_get_double(c->props.local_max,
                                     c->props.local_type, i);

        len = strlen(expr);
        if (src_min == src_max)
            snprintf(expr+len, 256-len, "0,");
        else {
            dest_min = propval_get_double(c->props.remote_min,
                                          c->props.remote_type, i);
            dest_max = propval_get_double(c->props.remote_max,
                                          c->props.remote_type, i);
            if ((src_min == dest_min) && (src_max == dest_max)) {
                snprintf(expr+len, 256-len, "1,");
            }
            else {
                double scale = ((dest_min - dest_max) / (src_min - src_max));
                snprintf(expr+len, 256-len, "%g,", scale);
            }
        }
    }
    len = strlen(expr);
    if (min_length > 1)
        snprintf(expr+len-1, 256-len+1, "]+[");
    else
        snprintf(expr+len-1, 256-len+1, "+");

    // add offset
    for (i=0; i<min_length; i++) {
        src_min = propval_get_double(c->props.local_min,
                                     c->props.local_type, i);
        src_max = propval_get_double(c->props.local_max,
                                     c->props.local_type, i);

        len = strlen(expr);
        if (src_min == src_max)
            snprintf(expr+len, 256-len, "%g,", dest_min);
        else {
            dest_min = propval_get_double(c->props.remote_min,
                                          c->props.remote_type, i);
            dest_max = propval_get_double(c->props.remote_max,
                                          c->props.remote_type, i);
            if ((src_min == dest_min) && (src_max == dest_max)) {
                snprintf(expr+len, 256-len, "0,");
            }
            else {
                double offset = ((dest_max * src_min - dest_min * src_max)
                                 / (src_min - src_max));
                snprintf(expr+len, 256-len, "%g,", offset);
            }
        }
    }
    len = strlen(expr);
    if (min_length > 1)
        snprintf(expr+len-1, 256-len+1, "]");
    else
        expr[len-1] = '\0';

    // If everything is successful, replace the connection's expression.
    if (e) {
        int input_history_size, output_history_size;
        if (!replace_expression_string(c, e, &input_history_size,
                                       &output_history_size)) {
            reallocate_connection_histories(c, 1, 1);
            c->props.mode = MO_LINEAR;
            return 0;
        }
    }
    return 1;
}

void mapper_connection_set_mode_expression(mapper_connection c,
                                           const char *expr)
{
    int input_history_size, output_history_size;
    if (replace_expression_string(c, expr, &input_history_size,
                                  &output_history_size))
        return;

    c->props.mode = MO_EXPRESSION;
    reallocate_connection_histories(c, input_history_size,
                                    output_history_size);
    /* Special case: if we are the receiver and the new expression
     * evaluates to a constant we can update immediately. */
    /* TODO: should call handler for all instances updated
     * through this connection. */
    mapper_signal sig = c->parent->signal;
    if (!sig->props.is_output && mapper_expr_constant_output(c->expr)
        && !c->props.send_as_instance) {
        int index = 0;
        mapper_timetag_t now;
        mapper_clock_now(&sig->device->admin->clock, &now);
        if (!sig->id_maps[0].instance)
            index = msig_get_instance_with_local_id(sig, 0, 1, &now);
        if (index < 0)
            return;
        mapper_signal_instance si = sig->id_maps[index].instance;

        // evaluate expression
        mapper_signal_history_t h;
        h.type = sig->props.type;
        h.value = si->value;
        h.position = -1;
        h.length = sig->props.length;
        h.size = 1;
        char typestring[h.length];
        mapper_expr_evaluate(c->expr, 0, &c->expr_vars[si->index],
                             &h, typestring, 0, 0);

        // call handler if it exists
        if (sig->handler)
            sig->handler(sig, &sig->props, 0, si->value, 1, &now);
    }
}

/* Helper to check if type is a number. */
static int is_number_type(const char type)
{
    switch (type) {
        case 'i':   return 1;
        case 'f':   return 1;
        case 'd':   return 1;
        default:    return 0;
    }
}

/* Helper to fill in the range (src_min, src_max, dest_min, dest_max)
 * based on message parameters and known connection and signal properties;
 * return flags to indicate which parts of the range were found. */
static int set_range(mapper_connection c,
                     mapper_message_t *msg)
{
    lo_arg **args = NULL;
    const char *types = NULL;
    int i, length = 0, updated = 0, result;

    if (!c)
        return 0;

    /* The logic here is to first try to use information from the
     * message, starting with @srcMax, @srcMin, @destMax, @destMin,
     * and then @min and @max.
     * Next priority is already-known properties of the connection.
     * Lastly, we fill in source range from the signal. */

    int out = (c->props.direction == DI_OUTGOING);

    /* local_max */
    args = mapper_msg_get_param(msg, out ? AT_SRC_MAX : AT_DEST_MAX);
    types = mapper_msg_get_type(msg, out ? AT_SRC_MAX : AT_DEST_MAX);
    length = mapper_msg_get_length(msg, out ? AT_SRC_MAX : AT_DEST_MAX);
    if (args && types && is_number_type(types[0])) {
        if (length == c->props.local_length) {
            if (!c->props.local_max)
                c->props.local_max = calloc(1, length * c->props.local_type);
            for (i=0; i<length; i++) {
                result = propval_set_from_lo_arg(c->props.local_max,
                                                 c->props.local_type,
                                                 args[i], types[i], i);
                if (result == -1) {
                    break;
                }
                else
                    updated += result;
            }
        }
    }

    /* local_min */
    args = mapper_msg_get_param(msg, out ? AT_SRC_MIN : AT_DEST_MIN);
    types = mapper_msg_get_type(msg, out ? AT_SRC_MIN : AT_DEST_MIN);
    length = mapper_msg_get_length(msg, out ? AT_SRC_MIN : AT_DEST_MIN);
    if (args && types && is_number_type(types[0])) {
        if (length == c->props.local_length) {
            if (!c->props.local_min)
                c->props.local_min = calloc(1, length * c->props.local_type);
            for (i=0; i<length; i++) {
                result = propval_set_from_lo_arg(c->props.local_min,
                                                 c->props.local_type,
                                                 args[i], types[i], i);
                if (result == -1) {
                    break;
                }
                else
                    updated += result;
            }
        }
    }

    /* remote_max */
    args = mapper_msg_get_param(msg, out ? AT_DEST_MAX : AT_SRC_MAX);
    types = mapper_msg_get_type(msg, out ? AT_DEST_MAX : AT_SRC_MAX);
    length = mapper_msg_get_length(msg, out ? AT_DEST_MAX : AT_SRC_MAX);
    if (args && types && is_number_type(types[0])) {
        if (length == c->props.remote_length) {
            if (!c->props.remote_max)
                c->props.remote_max = calloc(1, length * c->props.remote_type);
            for (i=0; i<length; i++) {
                result = propval_set_from_lo_arg(c->props.remote_max,
                                                 c->props.remote_type,
                                                 args[i], types[i], i);
                if (result == -1) {
                    break;
                }
                else
                    updated += result;
            }
        }
    }

    /* remote_min */
    args = mapper_msg_get_param(msg, out ? AT_DEST_MIN : AT_SRC_MIN);
    types = mapper_msg_get_type(msg, out ? AT_DEST_MIN : AT_SRC_MIN);
    length = mapper_msg_get_length(msg, out ? AT_DEST_MIN : AT_SRC_MIN);
    if (args && types && is_number_type(types[0])) {
        if (length == c->props.remote_length) {
            if (!c->props.remote_min)
                c->props.remote_min = calloc(1, length * c->props.remote_type);
            for (i=0; i<length; i++) {
                result = propval_set_from_lo_arg(c->props.remote_min,
                                                 c->props.remote_type,
                                                 args[i], types[i], i);
                if (result == -1) {
                    break;
                }
                else
                    updated += result;
            }
        }
    }

    /* @min, @max */
    if (!c->props.remote_min) {
        args = mapper_msg_get_param(msg, AT_MIN);
        types = mapper_msg_get_type(msg, AT_MIN);
        length = mapper_msg_get_length(msg, AT_MIN);
        if (args && types && is_number_type(types[0]))
        {
            if (length == c->props.remote_length) {
                if (!c->props.remote_min)
                    c->props.remote_min = calloc(1, length * c->props.remote_type);
                for (i=0; i<length; i++) {
                    result = propval_set_from_lo_arg(c->props.remote_min,
                                                     c->props.remote_type,
                                                     args[i], types[i], i);
                    if (result == -1) {
                        break;
                    }
                    else
                        updated += result;
                }
            }
        }
    }

    if (!c->props.remote_max) {
        args = mapper_msg_get_param(msg, AT_MAX);
        types = mapper_msg_get_type(msg, AT_MAX);
        length = mapper_msg_get_length(msg, AT_MAX);
        if (args && types && is_number_type(types[0]))
        {
            if (length == c->props.remote_length) {
                if (!c->props.remote_max)
                    c->props.remote_max = calloc(1, length * c->props.remote_type);
                for (i=0; i<length; i++) {
                    result = propval_set_from_lo_arg(c->props.remote_max,
                                                     c->props.remote_type,
                                                     args[i], types[i], i);
                    if (result == -1) {
                        break;
                    }
                    else
                        updated += result;
                }
            }
        }
    }

    /* Signal */
    mapper_signal sig = c->parent->signal;

    /* If parent signal is an output it must be the "source" of this connection,
     * if it is an input it can be either "source" or "destination".
     * According to the protocol for negotiating new connections, we will only
     * fill-in ranges for the "source" signal.*/
    if (!sig || c->props.direction != DI_OUTGOING)
        return updated;

    if (!c->props.local_min && sig->props.minimum)
    {
        c->props.local_min = malloc(msig_vector_bytes(sig));
        memcpy(c->props.local_min, sig->props.minimum,
               msig_vector_bytes(sig));
        updated++;
    }

    if (!c->props.local_max && sig->props.maximum)
    {
        c->props.local_max = malloc(msig_vector_bytes(sig));
        memcpy(c->props.local_max, sig->props.maximum,
               msig_vector_bytes(sig));
        updated++;
    }

    return updated;
}

int mapper_connection_set_from_message(mapper_connection c,
                                       mapper_message_t *msg,
                                       int direction)
{
    int updated = 0;
    /* First record any provided parameters. */

    /* Destination slot */
    int slot = 0;
    mapper_msg_get_param_if_int(msg, AT_SLOT, &slot);

    /* Remote type. */
    const char *remote_type = mapper_msg_get_param_if_char(msg, AT_TYPE);
    if (remote_type && c->props.remote[slot].type != remote_type) {
        if (!c->props.remote[slot]->ready)
            c->props.remote[slot]->type = remote_type;
        else
            trace("error: can't change type of connection endpoint.\n");
        updated++;
    }

    /* Remote length. */
    int remote_length = mapper_msg_get_param_if_int(msg, AT_LENGTH);
    if (remote_length && c->props.remote[slot].length != remote_length) {
        if (!c->props.remote[slot]->ready)
            c->props.remote[slot]->length = remote_length;
        else
            trace("error: can't change length of connection endpoint.\n");
        updated++;
    }

    /* Range information. */
    updated += set_range(c, msg);
    if (c->props.mode == MO_LINEAR) {
        mapper_connection_set_mode_linear(c);
    }

    /* Muting. */
    int muting;
    if (!mapper_msg_get_param_if_int(msg, AT_MUTE, &muting)
        && c->props.muted != muting) {
        c->props.muted = muting;
        updated++;
    }

    /* Calibrating. */
    int calibrating;
    if (!mapper_msg_get_param_if_int(msg, AT_CALIBRATING, &calibrating)
        && c->props.calibrating != calibrating) {
        c->props.calibrating = calibrating;
        updated++;
    }

    /* Range boundary actions. */
    int bound_min = mapper_msg_get_boundary_action(msg, AT_BOUND_MIN);
    if (bound_min >= 0 && c->props.bound_min != bound_min) {
        c->props.bound_min = bound_min;
        updated++;
    }

    int bound_max = mapper_msg_get_boundary_action(msg, AT_BOUND_MAX);
    if (bound_max >= 0 && c->props.bound_max != bound_max) {
        c->props.bound_max = bound_max;
        updated++;
    }

    /* Expression. */
    const char *expr = mapper_msg_get_param_if_string(msg, AT_EXPRESSION);
    if (expr && (!c->props.expression || strcmp(c->props.expression, expr))) {
        if (c->props.direction == DI_OUTGOING
            && c->props.mode == MO_EXPRESSION) {
            int input_history_size, output_history_size;
            if (!replace_expression_string(c, expr, &input_history_size,
                                           &output_history_size)) {
                reallocate_connection_histories(c, input_history_size,
                                                output_history_size);
            }
        }
        else {
            // just copy the expression string
            if (c->props.expression)
                free(c->props.expression);
            c->props.expression = strdup(expr);
        }
        updated++;
    }

    /* Instances. */
    int send_as_instance;
    if (!mapper_msg_get_param_if_int(msg, AT_SEND_AS_INSTANCE, &send_as_instance)
        && c->props.send_as_instance != send_as_instance) {
        c->props.send_as_instance = send_as_instance;
        updated++;
    }

    /* Scopes */
    lo_arg **a_scopes = mapper_msg_get_param(msg, AT_SCOPE);
    int num_scopes = mapper_msg_get_length(msg, AT_SCOPE);
    mapper_db_connection_update_scope(&c->props.scope, a_scopes, num_scopes);

    /* Extra properties. */
    updated += mapper_msg_add_or_update_extra_params(c->props.extra, msg);

    /* Now set the mode type depending on the requested type and
     * the known properties. */

    int mode = mapper_msg_get_mode(msg);
    if (mode >= 0 && mode != c->props.mode)
        updated++;

    switch (mode)
    {
    case MO_RAW:
        mapper_connection_set_mode_raw(c);
        break;
    case MO_LINEAR:
        mapper_connection_set_mode_linear(c);
        break;
    case MO_EXPRESSION:
    case MO_UNDEFINED: {
        if (mode == MO_UNDEFINED) {
            /* No mode type specified; if mode not yet set, see if
             we know the range and choose between linear or direct connection. */
            /* Try to use linear connection .*/
            if (mapper_connection_set_mode_linear(c) == 0)
                break;
        }
        if (!c->props.expression) {
            if (c->props.local_length == c->props.remote_length)
                c->props.expression = strdup("y=x");
            else {
                char expr[256] = "";
                if (c->props.local_length > c->props.remote_length) {
                    // truncate source
                    if (c->props.remote_length == 1)
                        snprintf(expr, 256, "y=x[0]");
                    else
                        snprintf(expr, 256, "y=x[0:%i]",
                                 c->props.remote_length-1);
                }
                else {
                    // truncate destination
                    if (c->props.local_length == 1)
                        snprintf(expr, 256, "y[0]=x");
                    else
                        snprintf(expr, 256, "y[0:%i]=x",
                                 c->props.local_length);
                }
                c->props.expression = strdup(expr);
            }
        }
        mapper_connection_set_mode_expression(c, c->props.expression);
        break;
    }
    default:
        trace("unknown result from mapper_msg_get_mode()\n");
        break;
    }
    return updated;
}

void reallocate_connection_histories(mapper_connection c,
                                     int input_history_size,
                                     int output_history_size)
{
    mapper_signal sig = c->parent->signal;
    int i, j;

    // At least for now, exit if this is an input signal
    if (!sig->props.is_output) {
        return;
    }

    // If there is no expression, then no memory needs to be
    // reallocated.
    if (!c->expr)
        return;

    if (input_history_size < 1)
        input_history_size = 1;

    // Reallocate input histories
    if (input_history_size > c->parent->history_size) {
        size_t sample_size = msig_vector_bytes(sig);
        for (i=0; i<sig->props.num_instances; i++) {
            mhist_realloc(&c->parent->history[i], input_history_size,
                          sample_size, 1);
        }
        c->parent->history_size = input_history_size;
    }
    else if (input_history_size < c->parent->history_size) {
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

    // reallocate output histories
    if (output_history_size > c->output_history_size) {
        int sample_size = mapper_type_size(c->props.remote_type) * c->props.remote_length;
        for (i=0; i<sig->props.num_instances; i++) {
            mhist_realloc(&c->history[i], output_history_size, sample_size, 0);
        }
        c->output_history_size = output_history_size;
    }
    else if (output_history_size < mapper_expr_output_history_size(c->expr)) {
        // Do nothing for now...
    }

    // reallocate user variable histories
    int new_num_vars = mapper_expr_num_variables(c->expr);
    if (new_num_vars > c->num_expr_vars) {
        for (i=0; i<sig->props.num_instances; i++) {
            c->expr_vars[i] = realloc(c->expr_vars[i], new_num_vars *
                                      sizeof(struct _mapper_signal_history));
            // initialize new variables...
            for (j=c->num_expr_vars; j<new_num_vars; j++) {
                c->expr_vars[i][j].type = 'd';
                c->expr_vars[i][j].length = 0;
                c->expr_vars[i][j].size = 0;
                c->expr_vars[i][j].value = 0;
                c->expr_vars[i][j].timetag = 0;
                c->expr_vars[i][j].position = -1;
            }
        }
        c->num_expr_vars = new_num_vars;
    }
    else if (new_num_vars < c->num_expr_vars) {
        // Do nothing for now...
    }
    for (i=0; i<sig->props.num_instances; i++) {
        for (j=0; j<new_num_vars; j++) {
            int history_size = mapper_expr_variable_history_size(c->expr, j);
            int vector_length = mapper_expr_variable_vector_length(c->expr, j);
            mhist_realloc(c->expr_vars[i]+j, history_size,
                          vector_length * sizeof(double), 0);
            (c->expr_vars[i]+j)->length = vector_length;
            (c->expr_vars[i]+j)->size = history_size;
            (c->expr_vars[i]+j)->position = -1;
        }
    }
}

void mhist_realloc(mapper_signal_history_t *history,
                   int history_size,
                   int sample_size,
                   int is_input)
{
    if (!history || !history_size || !sample_size)
        return;
    if (history_size == history->size)
        return;
    if (!is_input || (history_size > history->size) || (history->position == 0)) {
        // realloc in place
        history->value = realloc(history->value, history_size * sample_size);
        history->timetag = realloc(history->timetag, history_size * sizeof(mapper_timetag_t));
        if (!is_input) {
            // Initialize entire history to 0
            memset(history->value, 0, history_size * sample_size);
            history->position = -1;
        }
        else if (history->position == 0) {
            memset(history->value + sample_size * history->size, 0,
                   sample_size * (history_size - history->size));
        }
        else {
            int new_position = history_size - history->size + history->position;
            memcpy(history->value + sample_size * new_position,
                   history->value + sample_size * history->position,
                   sample_size * (history->size - history->position));
            memcpy(&history->timetag[new_position],
                   &history->timetag[history->position], sizeof(mapper_timetag_t)
                   * (history->size - history->position));
            memset(history->value + sample_size * history->position, 0,
                   sample_size * (history_size - history->size));
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

void mapper_connection_mute_slot(mapper_connection c, int slot_num, int mute)
{
    if (c->upstream && slot_num < c->props.num_remote_signals)
        c->upstream[slot_num].props->cause_update = !mute;
}

int mapper_connection_get_slot_info(mapper_connection c, int slot_num,
                                    char *datatype, int *vector_length)
{
    // we are interested in the type & length of the remote signal
    if (!c->upstream || c->props.num_remote_signals <= slot_num)
        return 1;

    *datatype = c->upstream[slot_num]->props.type;
    *vector_length = c->upstream[slot_num]->props.length;

    return 0;
}
