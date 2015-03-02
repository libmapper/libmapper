
#include <string.h>
#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <limits.h>

#include "mapper_internal.h"
#include "types_internal.h"
#include <mapper/mapper.h>

extern const char* prop_msg_strings[N_AT_PARAMS];

/*! Function prototypes. */
static void reallocate_connection_histories(mapper_connection c);
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
               "called mapper_get_mode_type_string() with bad parameter.\n");

    return mapper_mode_type_strings[mode];
}

int mapper_connection_perform(mapper_connection c, mapper_connection_slot s,
                              int instance, char *typestring)
{
    int changed = 0, i;
    mapper_history from = s->history;
    mapper_history to = c->destination.history;

    if (c->props.calibrating == 1)
    {
        if (!s->props->minimum) {
            s->props->minimum = malloc(s->props->length *
                                       mapper_type_size(s->props->type));
        }
        if (!s->props->maximum) {
            s->props->maximum = malloc(s->props->length *
                                       mapper_type_size(s->props->type));
        }

        /* If calibration mode has just taken effect, first data
         * sample sets source min and max */
        switch (s->props->type) {
            case 'f': {
                float *v = mapper_history_value_ptr(*from);
                float *src_min = (float*)s->props->minimum;
                float *src_max = (float*)s->props->maximum;
                if (!s->calibrating) {
                    for (i = 0; i < from->length; i++) {
                        src_min[i] = v[i];
                        src_max[i] = v[i];
                    }
                    s->calibrating = 1;
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
                break;
            }
            case 'i': {
                int *v = mapper_history_value_ptr(*from);
                int *src_min = (int*)s->props->minimum;
                int *src_max = (int*)s->props->maximum;
                if (!s->calibrating) {
                    for (i = 0; i < from->length; i++) {
                        src_min[i] = v[i];
                        src_max[i] = v[i];
                    }
                    s->calibrating = 1;
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
                break;
            }
            case 'd': {
                double *v = mapper_history_value_ptr(*from);
                double *src_min = (double*)s->props->minimum;
                double *src_max = (double*)s->props->maximum;
                if (!s->calibrating) {
                    for (i = 0; i < from->length; i++) {
                        src_min[i] = v[i];
                        src_max[i] = v[i];
                    }
                    s->calibrating = 1;
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
                break;
            }
            default:
                break;
        }

        if (changed && c->props.mode == MO_LINEAR)
            mapper_connection_set_mode_linear(c);
    }

    if (c->status != MAPPER_ACTIVE || c->props.muted || !s->props->cause_update) {
        return 0;
    }
    else if (c->props.mode == MO_RAW) {
        to[instance].position = 0;
        // copy value without type coercion
        memcpy(mapper_history_value_ptr(to[instance]),
               mapper_history_value_ptr(from[instance]),
               msig_vector_bytes(s->local->signal));
        // copy timetag
        memcpy(mapper_history_tt_ptr(to[instance]),
               mapper_history_tt_ptr(from[instance]),
               sizeof(mapper_timetag_t));
        for (i = 0; i < from->length; i++)
            typestring[i] = c->props.sources[0].type;
        return 1;
    }

    die_unless(c->expr!=0, "Missing expression.\n");
    return (mapper_expr_evaluate(c->expr, c, instance,
                                 mapper_history_tt_ptr(from[instance]),
                                 &to[instance], typestring));
}

int mapper_boundary_perform(mapper_connection c, mapper_history history)
{
    /* TODO: We are currently saving the processed values to output history.
     * it needs to be decided whether boundary processing should be inside the
     * feedback loop when past samples are called in expressions. */
    int i, muted = 0;

    double value;
    double dest_min, dest_max, swap, total_range, difference, modulo_difference;
    mapper_boundary_action bound_min, bound_max;

    if (c->props.bound_min == BA_NONE && c->props.bound_max == BA_NONE) {
        return 1;
    }
    if (!c->props.destination.minimum
        && (c->props.bound_min != BA_NONE || c->props.bound_max == BA_WRAP)) {
        return 1;
    }
    if (!c->props.destination.maximum
        && (c->props.bound_max != BA_NONE || c->props.bound_min == BA_WRAP)) {
        return 1;
    }

    for (i = 0; i < history->length; i++) {
        value = propval_get_double(mapper_history_value_ptr(*history),
                                   c->props.destination.type, i);
        dest_min = propval_get_double(c->props.destination.minimum,
                                      c->props.destination.type, i);
        dest_max = propval_get_double(c->props.destination.maximum,
                                      c->props.destination.type, i);
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
        propval_set_double(mapper_history_value_ptr(*history),
                           c->props.destination.type, i, value);
    }
    return !muted;
}

/*! Build a value update message for a given connection. */
lo_message mapper_connection_build_message(mapper_connection c, void *value,
                                           int count, char *typestring,
                                           mapper_id_map id_map)
{
    mapper_db_connection props = &c->props;
    int i, length = props->destination.length * count;

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

    if (props->destination.slot_id >= 0) {
        // add slot
        lo_message_add_string(m, "@slot");
        lo_message_add_int32(m, props->destination.slot_id);
    }

    return m;
}

/* Helper to replace a connection's expression only if the given string
 * parses successfully. Returns 0 on success, non-zero on error. */
static int replace_expression_string(mapper_connection c,
                                     const char *expr_str)
{
    if (c->expr && c->props.expression && strcmp(c->props.expression, expr_str)==0)
        return 1;

    mapper_expr expr = mapper_expr_new_from_string(expr_str, c);

    if (!expr)
        return 1;

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
    reallocate_connection_histories(c);
}

static int mapper_connection_set_mode_linear(mapper_connection c)
{
    if (c->props.num_sources > 1)
        return 1;

    if (c->status < (MAPPER_TYPE_KNOWN | MAPPER_LENGTH_KNOWN))
        return 1;

    int i, len;
    char expr[256] = "";
    const char *e = expr;

    if (   !c->props.sources[0].minimum  || !c->props.sources[0].maximum
        || !c->props.destination.minimum || !c->props.destination.maximum)
        return 1;

    int min_length = c->props.sources[0].length < c->props.destination.length ?
                     c->props.sources[0].length : c->props.destination.length;
    double src_min, src_max, dest_min, dest_max;

    if (c->props.destination.length == c->props.sources[0].length)
        snprintf(expr, 256, "y=x*");
    else if (c->props.destination.length > c->props.sources[0].length) {
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
        src_min = propval_get_double(c->props.sources[0].minimum,
                                     c->props.sources[0].type, i);
        src_max = propval_get_double(c->props.sources[0].maximum,
                                     c->props.sources[0].type, i);

        len = strlen(expr);
        if (src_min == src_max)
            snprintf(expr+len, 256-len, "0,");
        else {
            dest_min = propval_get_double(c->props.destination.minimum,
                                          c->props.destination.type, i);
            dest_max = propval_get_double(c->props.destination.maximum,
                                          c->props.destination.type, i);
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
        src_min = propval_get_double(c->props.sources[0].minimum,
                                     c->props.sources[0].type, i);
        src_max = propval_get_double(c->props.sources[0].maximum,
                                     c->props.sources[0].type, i);

        len = strlen(expr);
        if (src_min == src_max)
            snprintf(expr+len, 256-len, "%g,", dest_min);
        else {
            dest_min = propval_get_double(c->props.destination.minimum,
                                          c->props.destination.type, i);
            dest_max = propval_get_double(c->props.destination.maximum,
                                          c->props.destination.type, i);
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
        if (!replace_expression_string(c, e)) {
            reallocate_connection_histories(c);
            c->props.mode = MO_LINEAR;
            return 0;
        }
    }
    return 1;
}

void mapper_connection_set_mode_expression(mapper_connection c,
                                           const char *expr)
{
    if (c->status < (MAPPER_TYPE_KNOWN | MAPPER_LENGTH_KNOWN))
        return;

    if (replace_expression_string(c, expr))
        return;

    c->props.mode = MO_EXPRESSION;
    reallocate_connection_histories(c);
    /* TODO: Special case: if we are the receiver and the new expression
     * evaluates to a constant we can update immediately. */
    /* TODO: should call handler for all instances updated
     * through this connection. */
//    if (mapper_expr_constant_output(c->expr) && !c->props.send_as_instance) {
//        int index = 0;
//        mapper_timetag_t now;
//        mapper_clock clock;
//        mapper_clock_now(&c->router->device->admin->clock, &now);
//
//        // evaluate expression
//        mapper_expr_evaluate(c->expr, c, 0, &now, &c->destination.history, 0);
//
//        // call handler if it exists
//        if (c->destination.direction == DI_INCOMING && c->destination.local) {
//            mapper_signal sig = c->destination.local->signal;
//            if (sig->handler)
//                sig->handler(sig, &sig->props, 0, si->value, 1, &now);
//        }
//    }
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
static int set_range(mapper_connection c, mapper_message_t *msg, int slot)
{
    lo_arg **args = NULL;
    const char *types = NULL;
    int i, length = 0, updated = 0, result;

    if (!c || c->props.num_sources > 1)
        return 0;

    /* The logic here is to first try to use information from the
     * message, starting with @srcMax, @srcMin, @destMax, @destMin.
     * Next priority is already-known properties of the connection.
     * Lastly, we fill in source range from the signal. */

    int out = (c->props.destination.direction == DI_OUTGOING);

    if ((c->sources[slot].status & MAPPER_TYPE_KNOWN)
        && (c->sources[slot].status & MAPPER_LENGTH_KNOWN)) {
        /* source maxima */
        args = mapper_msg_get_param(msg, AT_SRC_MAX, &types, &length);
        if (args && types && is_number_type(types[0])) {
            if (length == c->props.sources[slot].length) {
                if (!c->props.sources[slot].maximum)
                    c->props.sources[slot].maximum = calloc(1, length
                                                            * c->props.sources[0].type);
                for (i=0; i<length; i++) {
                    result = propval_set_from_lo_arg(c->props.sources[slot].maximum,
                                                     c->props.sources[slot].type,
                                                     args[i], types[i], i);
                    if (result == -1) {
                        break;
                    }
                    else
                        updated += result;
                }
            }
        }

        /* source minima */
        args = mapper_msg_get_param(msg, AT_SRC_MIN, &types, &length);
        if (args && types && is_number_type(types[0])) {
            if (length == c->props.sources[slot].length) {
                if (!c->props.sources[slot].minimum)
                    c->props.sources[slot].minimum = calloc(1, length * c->props.sources[0].type);
                for (i=0; i<length; i++) {
                    result = propval_set_from_lo_arg(c->props.sources[slot].minimum,
                                                     c->props.sources[slot].type,
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

    if ((c->destination.status & MAPPER_TYPE_KNOWN)
        && (c->destination.status & MAPPER_LENGTH_KNOWN)) {
        /* destination maximum */
        args = mapper_msg_get_param(msg, AT_DEST_MAX, &types, &length);
        if (args && types && is_number_type(types[0])) {
            if (length == c->props.destination.length) {
                if (!c->props.destination.maximum)
                    c->props.destination.maximum = calloc(1, length * c->props.destination.type);
                for (i=0; i<length; i++) {
                    result = propval_set_from_lo_arg(c->props.destination.maximum,
                                                     c->props.destination.type,
                                                     args[i], types[i], i);
                    if (result == -1) {
                        break;
                    }
                    else
                        updated += result;
                }
            }
        }

        /* destination minimum */
        args = mapper_msg_get_param(msg, AT_DEST_MIN, &types, &length);
        if (args && types && is_number_type(types[0])) {
            if (length == c->props.destination.length) {
                if (!c->props.destination.minimum)
                    c->props.destination.minimum = calloc(1, length * c->props.destination.type);
                for (i=0; i<length; i++) {
                    result = propval_set_from_lo_arg(c->props.destination.minimum,
                                                     c->props.destination.type,
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
    mapper_signal sig = 0;
    if (out && c->sources[slot].local)
        sig = c->sources[slot].local->signal;
    else if (!out && c->destination.local)
        sig = c->destination.local->signal;
    if (!sig)
        return updated;

    if (out) {
        if (!c->props.sources[slot].minimum && sig->props.minimum)
        {
            c->props.sources[slot].minimum = malloc(msig_vector_bytes(sig));
            memcpy(c->props.sources[slot].minimum, sig->props.minimum,
                   msig_vector_bytes(sig));
            updated++;
        }
        if (!c->props.sources[slot].maximum && sig->props.maximum)
        {
            c->props.sources[slot].maximum = malloc(msig_vector_bytes(sig));
            memcpy(c->props.sources[slot].maximum, sig->props.maximum,
                   msig_vector_bytes(sig));
            updated++;
        }
    }
    else {
        if (!c->props.destination.minimum && sig->props.minimum)
        {
            c->props.destination.minimum = malloc(msig_vector_bytes(sig));
            memcpy(c->props.destination.minimum, sig->props.minimum,
                   msig_vector_bytes(sig));
            updated++;
        }
        if (!c->props.destination.maximum && sig->props.maximum)
        {
            c->props.destination.maximum = malloc(msig_vector_bytes(sig));
            memcpy(c->props.destination.maximum, sig->props.maximum,
                   msig_vector_bytes(sig));
            updated++;
        }
    }

    return updated;
}

static void init_connection_history(mapper_connection_slot slot,
                                    mapper_db_connection_slot props)
{
    int i;
    if (slot->history)
        return;
    slot->history = malloc(sizeof(struct _mapper_history) * props->num_instances);
    slot->history_size = 1;
    for (i = 0; i < props->num_instances; i++) {
        slot->history[i].type = props->type;
        slot->history[i].length = props->length;
        slot->history[i].size = 1;
        slot->history[i].value = calloc(1, mapper_type_size(props->type)
                                        * props->length);
        slot->history[i].timetag = calloc(1, sizeof(mapper_timetag_t));
        slot->history[i].position = -1;
    }
}

static void apply_mode(mapper_connection c)
{
    switch (c->props.mode) {
        case MO_RAW:
            if (!c->is_admin)
                mapper_connection_set_mode_raw(c);
            break;
        case MO_LINEAR:
            if (mapper_connection_set_mode_linear(c))
                break;
        default: {
            if (c->props.mode != MO_EXPRESSION) {
                /* No mode type specified; if mode not yet set, see if
                 we know the range and choose between linear or direct connection. */
                /* Try to use linear connection .*/
                if (mapper_connection_set_mode_linear(c) == 0)
                    break;
            }
            if (!c->props.expression) {
                if (c->props.num_sources == 1) {
                    if (c->props.sources[0].length == c->props.destination.length)
                        c->props.expression = strdup("y=x");
                    else {
                        char expr[256] = "";
                        if (c->props.sources[0].length > c->props.destination.length) {
                            // truncate source
                            if (c->props.destination.length == 1)
                                snprintf(expr, 256, "y=x[0]");
                            else
                                snprintf(expr, 256, "y=x[0:%i]",
                                         c->props.destination.length-1);
                        }
                        else {
                            // truncate destination
                            if (c->props.sources[0].length == 1)
                                snprintf(expr, 256, "y[0]=x");
                            else
                                snprintf(expr, 256, "y[0:%i]=x",
                                         c->props.sources[0].length-1);
                        }
                        c->props.expression = strdup(expr);
                    }
                }
                else {
                    // check vector lengths
                    int i, j, max_vec_len = 0, min_vec_len = INT_MAX;
                    for (i = 0; i < c->props.num_sources; i++) {
                        if (c->props.sources[i].length > max_vec_len)
                            max_vec_len = c->props.sources[i].length;
                        if (c->props.sources[i].length < min_vec_len)
                            min_vec_len = c->props.sources[i].length;
                    }
                    char expr[256] = "";
                    int offset = 0, dest_vec_len;
                    if (max_vec_len < c->props.destination.length) {
                        snprintf(expr, 256, "y[0:%d]=(", max_vec_len-1);
                        offset = strlen(expr);
                        dest_vec_len = max_vec_len;
                    }
                    else {
                        snprintf(expr, 256, "y=(");
                        offset = 3;
                        dest_vec_len = c->props.destination.length;
                    }
                    for (i = 0; i < c->props.num_sources; i++) {
                        if (c->props.sources[i].length > dest_vec_len) {
                            snprintf(expr+offset, 256-offset, "x%d[0:%d]+",
                                     i, dest_vec_len-1);
                            offset = strlen(expr);
                        }
                        else if (c->props.sources[i].length < dest_vec_len) {
                            snprintf(expr+offset, 256-offset, "[x%d,0", i);
                            offset = strlen(expr);
                            for (j = 1; j < dest_vec_len - c->props.sources[0].length; j++) {
                                snprintf(expr+offset, 256-offset, ",0");
                                offset += 2;
                            }
                            snprintf(expr+offset, 256-offset, "]+");
                            offset += 2;
                        }
                        else {
                            snprintf(expr+offset, 256-offset, "x%d+", i);
                            offset = strlen(expr);
                        }
                    }
                    --offset;
                    snprintf(expr+offset, 256-offset, ")/%d", c->props.num_sources);
                    c->props.expression = strdup(expr);
                }
            }
            mapper_connection_set_mode_expression(c, c->props.expression);
            break;
        }
    }
}

int mapper_connection_check_status(mapper_connection c)
{
    c->status |= MAPPER_READY;
    int mask = ~MAPPER_READY;
    if (c->destination.local
        || (c->destination.link && c->destination.link->props.host))
        c->destination.status |= MAPPER_LINK_KNOWN;
    c->status &= (c->destination.status | mask);

    int i;
    for (i = 0; i < c->props.num_sources; i++) {
        if (c->sources[i].local
            || (c->sources[i].link && c->sources[i].link->props.host))
            c->sources[i].status |= MAPPER_LINK_KNOWN;
        c->status &= (c->sources[i].status | mask);
    }

    if ((c->status & MAPPER_TYPE_KNOWN) && (c->status & MAPPER_LENGTH_KNOWN)) {
        // allocate memory for connection history
        // TODO: should we wait for link information also?
        for (i = 0; i < c->props.num_sources; i++) {
            if (!c->sources[i].local) {
                init_connection_history(&c->sources[i], &c->props.sources[i]);
            }
        }
        if (!c->destination.local) {
            init_connection_history(&c->destination, &c->props.destination);
        }
        if (c->is_admin) {
            apply_mode(c);
        }
        return 1;
    }

    return c->status;
}

// if 'override' flag is not set, only remote properties can be set
int mapper_connection_set_from_message(mapper_connection c,
                                       mapper_message_t *msg, int override)
{
    int updated = 0;
    /* First record any provided parameters. */

    int id;
    if (!mapper_msg_get_param_if_int(msg, AT_ID, &id)
        && c->props.id != id) {
        c->props.id = id;
        updated++;
    }

    int i, slot = 0;
    if (!mapper_msg_get_param_if_int(msg, AT_SLOT, &slot)) {
        // if this is simple connection, "@slot" defines destination slot index
        // if this is convergent connection, "@slot" indicates a specific source slot
        if (c->destination.local) {
            // find slot index
            for (i = 0; i < c->props.num_sources; i++) {
                if (c->sources[i].props->slot_id == slot) {
                    slot = i;
                    break;
                }
            }
            if (i == c->props.num_sources) {
                trace("error: slot index not found.\n");
                return 0;
            }
        }
        else {
            if (slot != c->destination.props->slot_id) {
                c->destination.props->slot_id = slot;
                c->is_admin = 0;
                updated++;
            }
            slot = 0;
        }
    }

    const char *type = 0;
    int length;

    if (!c->sources[slot].local) {
        /* Source type. */
        type = mapper_msg_get_param_if_char(msg, AT_SRC_TYPE);
        if (type && !check_signal_type(type[0])) {
            if (type[0] != c->props.sources[slot].type) {
                c->props.sources[slot].type = type[0];
                c->sources[slot].status |= MAPPER_TYPE_KNOWN;
                updated++;
            }
        }

        /* Source length. */
        if (!mapper_msg_get_param_if_int(msg, AT_SRC_LENGTH, &length)) {
            if (length != c->props.sources[slot].length) {
                c->props.sources[slot].length = length;
                c->sources[slot].status |= MAPPER_LENGTH_KNOWN;
                updated++;
            }
        }
    }

    if (!c->destination.local) {
        /* Destination type. */
        type = mapper_msg_get_param_if_char(msg, AT_DEST_TYPE);
        if (type && !check_signal_type(type[0])) {
            if (type[0] != c->props.destination.type) {
                c->props.destination.type = type[0];
                c->destination.status |= MAPPER_TYPE_KNOWN;
                updated++;
            }
        }

        /* Destination length. */
        if (!mapper_msg_get_param_if_int(msg, AT_DEST_LENGTH, &length)) {
            if (length != c->props.destination.length) {
                c->props.destination.length = length;
                c->destination.status |= MAPPER_LENGTH_KNOWN;
                updated++;
            }
        }
    }

    // TODO: handle range calibration coming from remote device
    if (!c->is_admin || override) {
        /* Range information. */
        updated += set_range(c, msg, slot);
        if (c->is_admin && c->props.mode == MO_LINEAR) {
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

        /* Instances. */
        int send_as_instance;
        if (!mapper_msg_get_param_if_int(msg, AT_SEND_AS_INSTANCE, &send_as_instance)
            && c->props.send_as_instance != send_as_instance) {
            c->props.send_as_instance = send_as_instance;
            updated++;
        }

        /* Expression. */
        const char *expr = mapper_msg_get_param_if_string(msg, AT_EXPRESSION);
        if (expr) {
            if (!c->props.expression) {
                c->props.expression = strdup(expr);
                updated++;
            }
            else if (strcmp(c->props.expression, expr)) {
                if (c->is_admin) {
                    if (c->props.mode == MO_EXPRESSION) {
                        if (!replace_expression_string(c, expr))
                            reallocate_connection_histories(c);
                    }
                }
                else {
                    if (c->props.expression)
                        free(c->props.expression);
                    c->props.expression = strdup(expr);
                }
                updated++;
            }
        }

        /* Scopes */
        const char *types = 0;
        int num_args;
        lo_arg **args = mapper_msg_get_param(msg, AT_SCOPE, &types, &num_args);
        if (num_args && types && (types[0] == 's' || types[0] == 'S'))
            updated += mapper_db_connection_update_scope(&c->props.scope, args,
                                                         num_args);

        /* Cause Update */
        mapper_msg_get_param(msg, AT_CAUSE_UPDATE, &types, &num_args);
        if (num_args == c->props.num_sources) {
            for (i = 0; i < num_args; i++) {
                if (types[i] == 'T' && !c->props.sources[i].cause_update) {
                    c->props.sources[i].cause_update = 1;
                    updated++;
                }
                else if (types[i] == 'F' && c->props.sources[i].cause_update) {
                    c->props.sources[i].cause_update = 0;
                    updated++;
                }
            }
        }

        /* Extra properties. */
        updated += mapper_msg_add_or_update_extra_params(c->props.extra, msg);

        /* Now set the mode type depending on the requested type and
         * the known properties. */
        int mode = mapper_msg_get_mode(msg);
        if (mode < 0) {
            if (c->props.mode)
                return updated;
            else {
                mode = MO_UNDEFINED;
            }
        }
        else if (mode == MO_RAW && c->is_admin) {
            // ignore, this is a subconnection reporting in
        }
        else if (mode != c->props.mode) {
            c->props.mode = mode;
            updated++;
        }
    }
    if (c->status < MAPPER_READY) {
        // check if connection is now "ready"
        mapper_connection_check_status(c);
    }
    else if (updated && c->is_admin)
        apply_mode(c);

    return updated;
}

void reallocate_connection_histories(mapper_connection c)
{
    int i, j;

    // If there is no expression, then no memory needs to be
    // reallocated.
    if (!c->expr)
        return;

    mapper_connection_slot s;
    mapper_db_connection_slot p;
    int history_size;

    // Reallocate source histories
    for (i = 0; i < c->props.num_sources; i++) {
        history_size = mapper_expr_input_history_size(c->expr, i);
        s = &c->sources[i];
        p = &c->props.sources[i];
        if (history_size > s->history_size) {
            size_t sample_size = mapper_type_size(p->type) * p->length;;
            for (j = 0; j < p->num_instances; j++) {
                mhist_realloc(&s->history[j], history_size, sample_size, 1);
            }
            s->history_size = history_size;
        }
        else if (history_size < s->history_size) {
            // Do nothing for now...
        }
    }

    history_size = mapper_expr_output_history_size(c->expr);
    s = &c->destination;
    p = &c->props.destination;
    // reallocate output histories
    if (history_size > s->history_size) {
        int sample_size = mapper_type_size(p->type) * p->length;
        for (i = 0; i < p->num_instances; i++) {
            mhist_realloc(&s->history[i], history_size, sample_size, 0);
        }
        s->history_size = history_size;
    }
    else if (history_size < s->history_size) {
        // Do nothing for now...
    }

    // reallocate user variable histories
    int new_num_vars = mapper_expr_num_variables(c->expr);
    if (new_num_vars > c->num_expr_vars) {
        for (i = 0; i < c->num_var_instances; i++) {
            c->expr_vars[i] = realloc(c->expr_vars[i], new_num_vars *
                                      sizeof(struct _mapper_history));
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

    /* TODO: figuring out the correct number of instances for the user variables
     * is a bit tricky... for now we will use the maximum. */
    for (i = 0; i < c->num_var_instances; i++) {
        for (j = 0; j < new_num_vars; j++) {
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

void mhist_realloc(mapper_history history,
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
            mapper_history_t temp;
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

void mapper_connection_prepare_osc_message(lo_message m, mapper_connection c,
                                           int slot)
{
    /* if message being sent upstream:
     *      - could be part of convergent connection
     * if message being sent downstream:
     *      - could be part of convergent connection
     * anytime "slot" included, means (sub)connection is "raw"
     *      – just for now, this should/could change in future
     * ok, anytime "slot" is included, is part of subconnection
     * "slot" always refers to rs slot at connection destination
     */

    int i;
    mapper_db_connection props = &c->props;

    // Connection id
    lo_message_add_string(m, prop_msg_strings[AT_ID]);
    lo_message_add_int32(m, c->props.id);

    // Connection mode
    if (slot >= 0) {
        lo_message_add_string(m, prop_msg_strings[AT_MODE]);
        lo_message_add_string(m, mapper_mode_type_strings[MO_RAW]);
    }
    else if (props->mode) {
        lo_message_add_string(m, prop_msg_strings[AT_MODE]);
        lo_message_add_string(m, mapper_mode_type_strings[props->mode]);
    }

    if (slot < 0) {
        // Expression string
        if (props->expression) {
            lo_message_add_string(m, prop_msg_strings[AT_EXPRESSION]);
            lo_message_add_string(m, props->expression);
        }
    }

    // Source type(s) and vector length(s)
    if (slot < 0) {
        int known = 1;
        for (i = 0; i < props->num_sources; i++) {
            if (!(c->sources[i].status & MAPPER_LENGTH_KNOWN)
                || !(c->sources[i].status & MAPPER_TYPE_KNOWN)) {
                known = 0;
                break;
            }
        }
        if (known) {
            lo_message_add_string(m, prop_msg_strings[AT_SRC_TYPE]);
            for (i = 0; i < props->num_sources; i++) {
                lo_message_add_char(m, props->sources[i].type);
            }
            lo_message_add_string(m, prop_msg_strings[AT_SRC_LENGTH]);
            for (i = 0; i < props->num_sources; i++) {
                lo_message_add_int32(m, props->sources[i].length);
            }
        }
    }
    else {
        if (c->sources[slot].status & MAPPER_TYPE_KNOWN) {
            lo_message_add_string(m, prop_msg_strings[AT_SRC_TYPE]);
            lo_message_add_char(m, props->sources[slot].type);
        }
        if (c->sources[slot].status & MAPPER_LENGTH_KNOWN) {
            lo_message_add_string(m, prop_msg_strings[AT_SRC_LENGTH]);
            lo_message_add_int32(m, props->sources[slot].length);
        }
    }

    // Destination type
    if (c->destination.status & MAPPER_TYPE_KNOWN) {
        lo_message_add_string(m, prop_msg_strings[AT_DEST_TYPE]);
        lo_message_add_char(m, props->destination.type);
    }

    // Destination vector length
    if (c->destination.status & MAPPER_LENGTH_KNOWN) {
        lo_message_add_string(m, prop_msg_strings[AT_DEST_LENGTH]);
        lo_message_add_int32(m, props->destination.length);
    }

    if (slot < 0 && props->mode != MO_RAW) {
        // TODO: extend to multiple sources
        if (c->props.num_sources == 1) {
            if (props->sources[0].minimum) {
                lo_message_add_string(m, prop_msg_strings[AT_SRC_MIN]);
                mapper_msg_add_typed_value(m, props->sources[0].type,
                                           props->sources[0].length,
                                           props->sources[0].minimum);
            }

            if (props->sources[0].maximum) {
                lo_message_add_string(m, prop_msg_strings[AT_SRC_MAX]);
                mapper_msg_add_typed_value(m, props->sources[0].type,
                                           props->sources[0].length,
                                           props->sources[0].maximum);
            }
        }

        if (props->destination.minimum) {
            lo_message_add_string(m, prop_msg_strings[AT_DEST_MIN]);
            mapper_msg_add_typed_value(m, props->destination.type,
                                       props->destination.length,
                                       props->destination.minimum);
        }

        if (props->destination.maximum) {
            lo_message_add_string(m, prop_msg_strings[AT_DEST_MAX]);
            mapper_msg_add_typed_value(m, props->destination.type,
                                       props->destination.length,
                                       props->destination.maximum);
        }

        lo_message_add_string(m, prop_msg_strings[AT_BOUND_MIN]);
        lo_message_add_string(m, mapper_boundary_action_strings[props->bound_min]);
        lo_message_add_string(m, prop_msg_strings[AT_BOUND_MAX]);
        lo_message_add_string(m, mapper_boundary_action_strings[props->bound_max]);
    }

    // Muting
    lo_message_add_string(m, prop_msg_strings[AT_MUTE]);
    lo_message_add_int32(m, props->muted);

    // Send as Instance
    lo_message_add_string(m, prop_msg_strings[AT_SEND_AS_INSTANCE]);
    lo_message_add_int32(m, props->send_as_instance);

    // Connection scopes
    lo_message_add_string(m, prop_msg_strings[AT_SCOPE]);
    if (props->scope.size) {
        for (i = 0; i < props->scope.size; i++)
            lo_message_add_string(m, props->scope.names[i]);
    }
    else
        lo_message_add_string(m, "none");

    // Connection slot
    if (props->destination.direction == DI_OUTGOING) {
        if (props->destination.slot_id >= 0) {
            lo_message_add_string(m, prop_msg_strings[AT_SLOT]);
            lo_message_add_int32(m, props->destination.slot_id);
        }
    }
    else if (slot >= 0) {
        lo_message_add_string(m, prop_msg_strings[AT_SLOT]);
        lo_message_add_int32(m, slot);
    }

    if (props->num_sources > 1) {
        // Cause update
        lo_message_add_string(m, prop_msg_strings[AT_CAUSE_UPDATE]);
        for (i = 0; i < props->num_sources; i++) {
            if (props->sources[i].cause_update)
                lo_message_add_true(m);
            else
                lo_message_add_false(m);
        }
    }

    // "Extra" properties
    mapper_msg_add_value_table(m, props->extra);
}
