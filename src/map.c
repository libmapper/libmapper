#include <ctype.h>
#include <string.h>
#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <limits.h>

#include "mapper_internal.h"
#include "types_internal.h"
#include <mapper/mapper.h>

/*! Function prototypes. */
static void reallocate_map_histories(mapper_map_internal map);
static int mapper_map_set_mode_linear(mapper_map_internal map);

mapper_map mapper_map_new(int num_sources, mapper_db_signal *sources,
                          mapper_db_signal destination)
{
    if (num_sources <= 0 || num_sources > MAX_NUM_MAP_SOURCES
        || !sources || !destination)
        return 0;

    int i;
    mapper_map map;
    map = (mapper_map) calloc(1, sizeof(struct _mapper_map));
    map->num_sources = num_sources;
    map->sources = (mapper_map_slot) calloc(1, sizeof(struct _mapper_map_slot));
    for (i = 0; i < num_sources; i++) {
        map->sources[i].signal = sources[i];
    }
    map->destination.signal = destination;
    return map;
}

void mapper_map_free(mapper_map map)
{
    if (!map)
        return;
    free(map->sources);
    free(map);
}

// only called for outgoing maps
int mapper_map_perform(mapper_map_internal map, mapper_slot_internal slot,
                       int instance, char *typestring)
{
    int changed = 0, i;
    mapper_history from = slot->history;
    mapper_history to = map->destination.history;

    if (slot->props->calibrating == 1)
    {
        if (!slot->props->minimum) {
            slot->props->minimum = malloc(slot->props->length *
                                          mapper_type_size(slot->props->type));
        }
        if (!slot->props->maximum) {
            slot->props->maximum = malloc(slot->props->length *
                                          mapper_type_size(slot->props->type));
        }

        /* If calibration mode has just taken effect, first data
         * sample sets source min and max */
        switch (slot->props->type) {
            case 'f': {
                float *v = mapper_history_value_ptr(*from);
                float *src_min = (float*)slot->props->minimum;
                float *src_max = (float*)slot->props->maximum;
                if (!slot->calibrating) {
                    for (i = 0; i < from->length; i++) {
                        src_min[i] = v[i];
                        src_max[i] = v[i];
                    }
                    slot->calibrating = 1;
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
                int *src_min = (int*)slot->props->minimum;
                int *src_max = (int*)slot->props->maximum;
                if (!slot->calibrating) {
                    for (i = 0; i < from->length; i++) {
                        src_min[i] = v[i];
                        src_max[i] = v[i];
                    }
                    slot->calibrating = 1;
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
                double *src_min = (double*)slot->props->minimum;
                double *src_max = (double*)slot->props->maximum;
                if (!slot->calibrating) {
                    for (i = 0; i < from->length; i++) {
                        src_min[i] = v[i];
                        src_max[i] = v[i];
                    }
                    slot->calibrating = 1;
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

        if (changed && map->props.mode == MO_LINEAR)
            mapper_map_set_mode_linear(map);
    }

    if (map->status != MAPPER_ACTIVE || map->props.muted) {
        return 0;
    }
    else if (map->props.process_location == MAPPER_DESTINATION) {
        to[instance].position = 0;
        // copy value without type coercion
        memcpy(mapper_history_value_ptr(to[instance]),
               mapper_history_value_ptr(from[instance]),
               msig_vector_bytes(slot->local->signal));
        // copy timetag
        memcpy(mapper_history_tt_ptr(to[instance]),
               mapper_history_tt_ptr(from[instance]),
               sizeof(mapper_timetag_t));
        for (i = 0; i < from->length; i++)
            typestring[i] = map->props.sources[0].type;
        return 1;
    }

    if (!map->expr) {
        trace("error: missing expression.\n");
        return 0;
    }

    mapper_history sources[map->props.num_sources];
    for (i = 0; i < map->props.num_sources; i++)
        sources[i] = &map->sources[i].history[instance];
    return (mapper_expr_evaluate(map->expr,
                                 sources, &map->expr_vars[instance], &to[instance],
                                 mapper_history_tt_ptr(from[instance]),
                                 typestring));
}

int mapper_boundary_perform(mapper_history history, mapper_map_slot s,
                            char *typestring)
{
    int i, muted = 0;

    double value;
    double dest_min, dest_max, swap, total_range, difference, modulo_difference;
    mapper_boundary_action bound_min, bound_max;

    if (s->bound_min == BA_NONE && s->bound_max == BA_NONE) {
        return 0;
    }
    if (!s->minimum && (s->bound_min != BA_NONE || s->bound_max == BA_WRAP)) {
        return 0;
    }
    if (!s->maximum && (s->bound_max != BA_NONE || s->bound_min == BA_WRAP)) {
        return 0;
    }

    for (i = 0; i < history->length; i++) {
        if (typestring[i] == 'N') {
            muted++;
            continue;
        }
        value = propval_get_double(mapper_history_value_ptr(*history), s->type, i);
        dest_min = propval_get_double(s->minimum, s->type, i);
        dest_max = propval_get_double(s->maximum, s->type, i);
        if (dest_min < dest_max) {
            bound_min = s->bound_min;
            bound_max = s->bound_max;
        }
        else {
            bound_min = s->bound_max;
            bound_max = s->bound_min;
            swap = dest_max;
            dest_max = dest_min;
            dest_min = swap;
        }
        total_range = fabs(dest_max - dest_min);
        if (value < dest_min) {
            switch (bound_min) {
                case BA_MUTE:
                    // need to prevent value from being sent at all
                    typestring[i] = 'N';
                    muted++;
                    break;
                case BA_CLAMP:
                    // clamp value to range minimum
                    value = dest_min;
                    break;
                case BA_FOLD:
                    // fold value around range minimum
                    difference = fabs(value - dest_min);
                    value = dest_min + difference;
                    if (value > dest_max) {
                        // value now exceeds range maximum!
                        switch (bound_max) {
                            case BA_MUTE:
                                // need to prevent value from being sent at all
                                typestring[i] = 'N';
                                muted++;
                                break;
                            case BA_CLAMP:
                                // clamp value to range minimum
                                value = dest_max;
                                break;
                            case BA_FOLD:
                                // both boundary actions are set to fold!
                                difference = fabs(value - dest_max);
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
                                difference = fabs(value - dest_max);
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
                    difference = fabs(value - dest_min);
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
                    typestring[i] = 'N';
                    muted++;
                    break;
                case BA_CLAMP:
                    // clamp value to range maximum
                    value = dest_max;
                    break;
                case BA_FOLD:
                    // fold value around range maximum
                    difference = fabs(value - dest_max);
                    value = dest_max - difference;
                    if (value < dest_min) {
                        // value now exceeds range minimum!
                        switch (bound_min) {
                            case BA_MUTE:
                                // need to prevent value from being sent at all
                                typestring[i] = 'N';
                                muted++;
                                break;
                            case BA_CLAMP:
                                // clamp value to range minimum
                                value = dest_min;
                                break;
                            case BA_FOLD:
                                // both boundary actions are set to fold!
                                difference = fabs(value - dest_min);
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
                                difference = fabs(value - dest_min);
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
                    difference = fabs(value - dest_max);
                    modulo_difference = difference
                        - (int)(difference / total_range) * total_range;
                    value = dest_min + modulo_difference;
                    break;
                default:
                    break;
            }
        }
        propval_set_double(mapper_history_value_ptr(*history), s->type, i, value);
    }
    return (muted == history->length);
}

/*! Build a value update message for a given map. */
lo_message mapper_map_build_message(mapper_map_internal map,
                                    mapper_slot_internal s,
                                    void *value, int count, char *typestring,
                                    mapper_id_map id_map)
{
    mapper_map props = &map->props;
    int i;
    int length = ((props->process_location == MAPPER_SOURCE)
                  ? props->destination.length * count : s->props->length * count);

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

    if (id_map) {
        lo_message_add_string(m, "@instance");
        lo_message_add_int64(m, id_map->global);
    }

    if (props->process_location == MAPPER_DESTINATION) {
        // add slot
        lo_message_add_string(m, "@slot");
        lo_message_add_int32(m, s->props->slot_id);
    }

    return m;
}

static char *fix_expression_source_order(int num, int *order,
                                         const char *expr_str)
{
    int i, li = 0, ri = 0;

    char expr_temp[256];
    while (li < 255 && expr_str[ri]) {
        if (expr_str[ri] == 'x' && isdigit(expr_str[ri+1])) {
            expr_temp[li++] = 'x';
            ri++;
            int index = atoi(expr_str+ri);
            if (index >= num)
                return 0;
            while (isdigit(expr_str[ri]))
                ri++;
            for (i = 0; i < 3; i++) {
                if (order[i] == index) {
                    snprintf(expr_temp+li, 256-li, "%d", i);
                    li += strlen(expr_temp+li);
                }
            }
        }
        else {
            expr_temp[li] = expr_str[ri];
            ri++;
            li++;
        }
        expr_temp[li] = 0;
    }
    return strdup(expr_temp);
}

/* Helper to replace a map's expression only if the given string
 * parses successfully. Returns 0 on success, non-zero on error. */
static int replace_expression_string(mapper_map_internal map, const char *expr_str)
{
    if (map->expr && map->props.expression && strcmp(map->props.expression, expr_str)==0)
        return 1;
    if (map->status < MAPPER_READY)
        return 1;
    int i;
    char source_types[map->props.num_sources];
    int source_lengths[map->props.num_sources];
    for (i = 0; i < map->props.num_sources; i++) {
        source_types[i] = map->props.sources[i].type;
        source_lengths[i] = map->props.sources[i].length;
    }
    mapper_expr expr = mapper_expr_new_from_string(expr_str,
                                                   map->props.num_sources,
                                                   source_types, source_lengths,
                                                   map->props.destination.type,
                                                   map->props.destination.length);

    if (!expr)
        return 1;

    // expression update may force processing location to change
    // e.g. if expression combines signals from different devices
    // e.g. if expression refers to current/past value of destination
    int output_history_size = mapper_expr_output_history_size(expr);
    if (output_history_size > 1 && map->props.process_location == MAPPER_SOURCE) {
        map->props.process_location = MAPPER_DESTINATION;
        // copy expression string but do not execute it
        if (map->props.expression)
            free(map->props.expression);
        map->props.expression = strdup(expr_str);
        return 1;
    }

    if (map->expr)
        mapper_expr_free(map->expr);

    map->expr = expr;

    if (map->props.expression == expr_str)
        return 0;

    int len = strlen(expr_str);
    if (!map->props.expression || len > strlen(map->props.expression))
        map->props.expression = realloc(map->props.expression, len+1);

    /* Using strncpy() here causes memory profiling errors due to possible
     * overlapping memory (e.g. expr_str == map->props.expression). */
    memcpy(map->props.expression, expr_str, len);
    map->props.expression[len] = '\0';

    return 0;
}

void mapper_map_set_mode_raw(mapper_map_internal map)
{
    map->props.mode = MO_RAW;
    reallocate_map_histories(map);
}

static int mapper_map_set_mode_linear(mapper_map_internal map)
{
    if (map->props.num_sources > 1)
        return 1;

    if (map->status < (MAPPER_TYPE_KNOWN | MAPPER_LENGTH_KNOWN))
        return 1;

    int i, len;
    char expr[256] = "";
    const char *e = expr;

    if (   !map->props.sources[0].minimum  || !map->props.sources[0].maximum
        || !map->props.destination.minimum || !map->props.destination.maximum)
        return 1;

    int min_length = map->props.sources[0].length < map->props.destination.length ?
                     map->props.sources[0].length : map->props.destination.length;
    double src_min, src_max, dest_min, dest_max;

    if (map->props.destination.length == map->props.sources[0].length)
        snprintf(expr, 256, "y=x*");
    else if (map->props.destination.length > map->props.sources[0].length) {
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
        src_min = propval_get_double(map->props.sources[0].minimum,
                                     map->props.sources[0].type, i);
        src_max = propval_get_double(map->props.sources[0].maximum,
                                     map->props.sources[0].type, i);

        len = strlen(expr);
        if (src_min == src_max)
            snprintf(expr+len, 256-len, "0,");
        else {
            dest_min = propval_get_double(map->props.destination.minimum,
                                          map->props.destination.type, i);
            dest_max = propval_get_double(map->props.destination.maximum,
                                          map->props.destination.type, i);
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
        src_min = propval_get_double(map->props.sources[0].minimum,
                                     map->props.sources[0].type, i);
        src_max = propval_get_double(map->props.sources[0].maximum,
                                     map->props.sources[0].type, i);

        len = strlen(expr);
        if (src_min == src_max)
            snprintf(expr+len, 256-len, "%g,", dest_min);
        else {
            dest_min = propval_get_double(map->props.destination.minimum,
                                          map->props.destination.type, i);
            dest_max = propval_get_double(map->props.destination.maximum,
                                          map->props.destination.type, i);
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

    // If everything is successful, replace the map's expression.
    if (e) {
        int should_compile = 0;
        if (map->props.process_location == MAPPER_DESTINATION) {
            // check if destination is local
            if (map->destination.local)
                should_compile = 1;
        }
        else {
            for (i = 0; i < map->props.num_sources; i++) {
                if (map->sources[i].local)
                    should_compile = 1;
            }
        }
        if (should_compile) {
            if (!replace_expression_string(map, e))
                reallocate_map_histories(map);
        }
        else {
            if (!map->props.expression)
                map->props.expression = strdup(e);
            else if (strcmp(map->props.expression, e)) {
                free(map->props.expression);
                map->props.expression = strdup(e);
            }
        }
        map->props.mode = MO_LINEAR;
        return 0;
    }
    return 1;
}

void mapper_map_set_mode_expression(mapper_map_internal map, const char *expr)
{
    if (map->status < (MAPPER_TYPE_KNOWN | MAPPER_LENGTH_KNOWN))
        return;

    int i, should_compile = 0;
    if (map->props.process_location == MAPPER_DESTINATION) {
        // check if destination is local
        if (map->destination.local)
            should_compile = 1;
    }
    else {
        for (i = 0; i < map->props.num_sources; i++) {
            if (map->sources[i].local)
                should_compile = 1;
        }
    }
    if (should_compile) {
        if (!replace_expression_string(map, expr)) {
            reallocate_map_histories(map);
            map->props.mode = MO_EXPRESSION;
        }
        else
            return;
    }
    else {
        if (!map->props.expression)
            map->props.expression = strdup(expr);
        else if (strcmp(map->props.expression, expr)) {
            free(map->props.expression);
            map->props.expression = strdup(expr);
        }
        map->props.mode = MO_EXPRESSION;
        return;
    }

    /* Special case: if we are the receiver and the new expression
     * evaluates to a constant we can update immediately. */
    /* TODO: should call handler for all instances updated
     * through this map. */
    int send_as_instance = 0;
    for (i = 0; i < map->props.num_sources; i++) {
        if (map->props.sources[i].send_as_instance) {
            send_as_instance = 1;
            break;
        }
    }
    send_as_instance += map->props.destination.send_as_instance;
    if (mapper_expr_constant_output(map->expr) && !send_as_instance) {
        mapper_timetag_t now;
        mapper_clock_now(&map->router->device->admin->clock, &now);

        // evaluate expression
        mapper_expr_evaluate(map->expr, 0, 0, map->destination.history, &now, 0);

        // call handler if it exists
        if (map->destination.local) {
            mapper_signal sig = map->destination.local->signal;
            if (sig->handler)
                sig->handler(sig, &sig->props, 0,
                             &map->destination.history[0].value, 1, &now);
        }
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
 * based on message parameters and known mapping and signal properties;
 * return flags to indicate which parts of the range were found. */
static int set_range(mapper_map_internal map, mapper_message_t *msg, int slot)
{
    lo_arg **args = NULL;
    const char *types = NULL;
    int i, j, length = 0, updated = 0, result;

    if (!map)
        return 0;

    mapper_slot_internal s = (slot >= 0) ? &map->sources[slot] : 0;

    // calculate value vector length for all sources
    int total_length = 0;
    if (slot < 0) {
        for (i = 0; i < map->props.num_sources; i++) {
            if (!(map->sources[i].status & MAPPER_LENGTH_KNOWN)) {
                total_length = 0;
                break;
            }
            total_length += map->props.sources[i].length;
        }
    }

    /* The logic here is to first try to use information from the
     * message, starting with @srcMax, @srcMin, @destMax, @destMin.
     * Next priority is already-known properties of the map.
     * Lastly, we fill in source range from the signal. */

    /* source maxima */
    args = mapper_msg_get_param(msg, AT_SRC_MAX, &types, &length);
    if (args && types && is_number_type(types[0])) {
        if (s) {
            if (!(s->status & MAPPER_TYPE_KNOWN)) {
                s->props->type = types[0];
                s->status |= MAPPER_TYPE_KNOWN;
            }
            if (!(s->status & MAPPER_LENGTH_KNOWN)) {
                s->props->length = length;
                s->status |= MAPPER_LENGTH_KNOWN;
            }
            if (length == s->props->length) {
                if (!s->props->maximum)
                    s->props->maximum = calloc(1, length
                                               * mapper_type_size(s->props->type));
                for (i = 0; i < length; i++) {
                    result = propval_set_from_lo_arg(s->props->maximum,
                                                     s->props->type,
                                                     args[i], types[i], i);
                    if (result == -1) {
                        break;
                    }
                    else
                        updated += result;
                }
            }
        }
        else if (map->props.num_sources == 1) {
            if (!(map->sources[0].status & MAPPER_TYPE_KNOWN)) {
                map->sources[0].props->type = types[0];
                map->sources[0].status |= MAPPER_TYPE_KNOWN;
            }
            if (!(map->sources[0].status & MAPPER_LENGTH_KNOWN)) {
                map->sources[0].props->length = length;
                map->sources[0].status |= MAPPER_LENGTH_KNOWN;
            }
            if (length == map->sources[0].props->length) {
                if (!map->sources[0].props->maximum)
                    map->sources[0].props->maximum = calloc(1, length
                                                            * mapper_type_size(map->sources[0].props->type));
                for (i = 0; i < length; i++) {
                    result = propval_set_from_lo_arg(map->sources[0].props->maximum,
                                                     map->sources[0].props->type,
                                                     args[i], types[i], i);
                    if (result == -1) {
                        break;
                    }
                    else
                        updated += result;
                }
            }
        }
        else if (total_length && total_length == length) {
            int offset = 0;
            for (i = 0; i < map->props.num_sources; i++) {
                if (!(map->sources[i].status & MAPPER_TYPE_KNOWN)) {
                    offset += map->props.sources[i].length;
                    continue;
                }
                if (!map->props.sources[i].maximum)
                    map->props.sources[i].maximum = calloc(1, map->props.sources[i].length
                                                           * mapper_type_size(map->props.sources[i].type));
                for (j = 0; j < map->props.sources[i].length; j++) {
                    result = propval_set_from_lo_arg(map->props.sources[i].maximum,
                                                     map->props.sources[i].type,
                                                     args[offset], types[offset],
                                                     j);
                    offset++;
                }
            }
        }
    }

    /* source minima */
    args = mapper_msg_get_param(msg, AT_SRC_MIN, &types, &length);
    if (args && types && is_number_type(types[0])) {
        if (s) {
            if (!(s->status & MAPPER_TYPE_KNOWN)) {
                s->props->type = types[0];
                s->status |= MAPPER_TYPE_KNOWN;
            }
            if (!(s->status & MAPPER_LENGTH_KNOWN)) {
                s->props->length = length;
                s->status |= MAPPER_LENGTH_KNOWN;
            }
            if (length == s->props->length) {
                if (!s->props->minimum)
                    s->props->minimum = calloc(1, length
                                               * mapper_type_size(s->props->type));
                for (i = 0; i < length; i++) {
                    result = propval_set_from_lo_arg(s->props->minimum,
                                                     s->props->type,
                                                     args[i], types[i], i);
                    if (result == -1) {
                        break;
                    }
                    else
                        updated += result;
                }
            }
        }
        else if (map->props.num_sources == 1) {
            if (!(map->sources[0].status & MAPPER_TYPE_KNOWN)) {
                map->sources[0].props->type = types[0];
                map->sources[0].status |= MAPPER_TYPE_KNOWN;
            }
            if (!(map->sources[0].status & MAPPER_LENGTH_KNOWN)) {
                map->sources[0].props->length = length;
                map->sources[0].status |= MAPPER_LENGTH_KNOWN;
            }
            if (length == map->sources[0].props->length) {
                if (!map->sources[0].props->minimum)
                    map->sources[0].props->minimum = calloc(1, length
                                                            * mapper_type_size(map->sources[0].props->type));
                for (i = 0; i < length; i++) {
                    result = propval_set_from_lo_arg(map->sources[0].props->minimum,
                                                     map->sources[0].props->type,
                                                     args[i], types[i], i);
                    if (result == -1) {
                        break;
                    }
                    else
                        updated += result;
                }
            }
        }
        else if (total_length && total_length == length) {
            int offset = 0;
            for (i = 0; i < map->props.num_sources; i++) {
                if (!(map->sources[i].status & MAPPER_TYPE_KNOWN)) {
                    offset += map->props.sources[i].length;
                    continue;
                }
                if (!map->props.sources[i].minimum)
                    map->props.sources[i].minimum = calloc(1, map->props.sources[i].length
                                                           * mapper_type_size(map->props.sources[i].type));
                for (j = 0; j < map->props.sources[i].length; j++) {
                    result = propval_set_from_lo_arg(map->props.sources[i].minimum,
                                                     map->props.sources[i].type,
                                                     args[offset], types[offset],
                                                     j);
                    offset++;
                }
            }
        }
    }

    if ((map->destination.status & MAPPER_TYPE_KNOWN)
        && (map->destination.status & MAPPER_LENGTH_KNOWN)) {
        /* destination maximum */
        args = mapper_msg_get_param(msg, AT_DEST_MAX, &types, &length);
        if (args && types && is_number_type(types[0])) {
            if (length == map->props.destination.length) {
                if (!map->props.destination.maximum)
                    map->props.destination.maximum = calloc(1, length * map->props.destination.type);
                for (i=0; i<length; i++) {
                    result = propval_set_from_lo_arg(map->props.destination.maximum,
                                                     map->props.destination.type,
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
            if (length == map->props.destination.length) {
                if (!map->props.destination.minimum)
                    map->props.destination.minimum = calloc(1, length * map->props.destination.type);
                for (i=0; i<length; i++) {
                    result = propval_set_from_lo_arg(map->props.destination.minimum,
                                                     map->props.destination.type,
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
    for (i = 0; i < map->props.num_sources; i++) {
        if (map->sources[i].local) {
            mapper_signal sig = map->sources[i].local->signal;
            if (!map->props.sources[i].maximum && sig->props.maximum) {
                map->props.sources[i].maximum = malloc(msig_vector_bytes(sig));
                memcpy(map->props.sources[i].maximum, sig->props.maximum,
                       msig_vector_bytes(sig));
                updated++;
            }
            if (!map->props.sources[i].minimum && sig->props.minimum) {
                map->props.sources[i].minimum = malloc(msig_vector_bytes(sig));
                memcpy(map->props.sources[i].minimum, sig->props.minimum,
                       msig_vector_bytes(sig));
                updated++;
            }
        }
    }

    if (map->destination.local) {
        mapper_signal sig = map->destination.local->signal;
        if (!map->props.destination.maximum && sig->props.maximum) {
            map->props.destination.maximum = malloc(msig_vector_bytes(sig));
            memcpy(map->props.destination.maximum, sig->props.maximum,
                   msig_vector_bytes(sig));
            updated++;
        }
        if (!map->props.destination.minimum && sig->props.minimum) {
            map->props.destination.minimum = malloc(msig_vector_bytes(sig));
            memcpy(map->props.destination.minimum, sig->props.minimum,
                   msig_vector_bytes(sig));
            updated++;
        }
    }

    return updated;
}

static void init_map_history(mapper_slot_internal slot, mapper_map_slot props)
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

static void apply_mode(mapper_map_internal map)
{
    switch (map->props.mode) {
        case MO_RAW:
            mapper_map_set_mode_raw(map);
            break;
        case MO_LINEAR:
            if (mapper_map_set_mode_linear(map))
                break;
        default: {
            if (map->props.mode != MO_EXPRESSION) {
                /* No mode type specified; if mode not yet set, see if
                 we know the range and choose between linear or direct map. */
                /* Try to use linear mapping .*/
                if (mapper_map_set_mode_linear(map) == 0)
                    break;
            }
            if (!map->props.expression) {
                if (map->props.num_sources == 1) {
                    if (map->props.sources[0].length == map->props.destination.length)
                        map->props.expression = strdup("y=x");
                    else {
                        char expr[256] = "";
                        if (map->props.sources[0].length > map->props.destination.length) {
                            // truncate source
                            if (map->props.destination.length == 1)
                                snprintf(expr, 256, "y=x[0]");
                            else
                                snprintf(expr, 256, "y=x[0:%i]",
                                         map->props.destination.length-1);
                        }
                        else {
                            // truncate destination
                            if (map->props.sources[0].length == 1)
                                snprintf(expr, 256, "y[0]=x");
                            else
                                snprintf(expr, 256, "y[0:%i]=x",
                                         map->props.sources[0].length-1);
                        }
                        map->props.expression = strdup(expr);
                    }
                }
                else {
                    // check vector lengths
                    int i, j, max_vec_len = 0, min_vec_len = INT_MAX;
                    for (i = 0; i < map->props.num_sources; i++) {
                        if (map->props.sources[i].length > max_vec_len)
                            max_vec_len = map->props.sources[i].length;
                        if (map->props.sources[i].length < min_vec_len)
                            min_vec_len = map->props.sources[i].length;
                    }
                    char expr[256] = "";
                    int offset = 0, dest_vec_len;
                    if (max_vec_len < map->props.destination.length) {
                        snprintf(expr, 256, "y[0:%d]=(", max_vec_len-1);
                        offset = strlen(expr);
                        dest_vec_len = max_vec_len;
                    }
                    else {
                        snprintf(expr, 256, "y=(");
                        offset = 3;
                        dest_vec_len = map->props.destination.length;
                    }
                    for (i = 0; i < map->props.num_sources; i++) {
                        if (map->props.sources[i].length > dest_vec_len) {
                            snprintf(expr+offset, 256-offset, "x%d[0:%d]+",
                                     i, dest_vec_len-1);
                            offset = strlen(expr);
                        }
                        else if (map->props.sources[i].length < dest_vec_len) {
                            snprintf(expr+offset, 256-offset, "[x%d,0", i);
                            offset = strlen(expr);
                            for (j = 1; j < dest_vec_len - map->props.sources[0].length; j++) {
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
                    snprintf(expr+offset, 256-offset, ")/%d", map->props.num_sources);
                    map->props.expression = strdup(expr);
                }
            }
            mapper_map_set_mode_expression(map, map->props.expression);
            break;
        }
    }
}

int mapper_map_check_status(mapper_map_internal map)
{
    map->status |= MAPPER_READY;
    int mask = ~MAPPER_READY;
    if (map->destination.local
        || (map->destination.link && map->destination.link->props.host))
        map->destination.status |= MAPPER_LINK_KNOWN;
    map->status &= (map->destination.status | mask);

    int i;
    for (i = 0; i < map->props.num_sources; i++) {
        if (map->sources[i].local
            || (map->sources[i].link && map->sources[i].link->props.host))
            map->sources[i].status |= MAPPER_LINK_KNOWN;
        map->status &= (map->sources[i].status | mask);
    }

    if ((map->status & MAPPER_TYPE_KNOWN) && (map->status & MAPPER_LENGTH_KNOWN)) {
        // allocate memory for map history
        // TODO: should we wait for link information also?
        for (i = 0; i < map->props.num_sources; i++) {
            init_map_history(&map->sources[i], &map->props.sources[i]);
        }
        init_map_history(&map->destination, &map->props.destination);
        if (!map->expr_vars) {
            map->expr_vars = calloc(1, sizeof(mapper_history)
                                  * map->num_var_instances);
        }
        apply_mode(map);
    }

    return map->status;
}

static void upgrade_extrema_memory(mapper_map_slot slot, char type, int length)
{
    int i;
    if (slot->minimum) {
        void *new_mem = calloc(1, length * mapper_type_size(type));
        switch (slot->type) {
            case 'i':
                for (i = 0; i < length; i++) {
                    propval_set_from_lo_arg(new_mem, type,
                                            (lo_arg*)&((int*)slot->minimum)[i],
                                            'i', i);
                }
                break;
            case 'f':
                for (i = 0; i < length; i++) {
                    propval_set_from_lo_arg(new_mem, type,
                                            (lo_arg*)&((float*)slot->minimum)[i],
                                            'f', i);
                }
                break;
            case 'd':
                for (i = 0; i < length; i++) {
                    propval_set_from_lo_arg(new_mem, type,
                                            (lo_arg*)&((double*)slot->minimum)[i],
                                            'd', i);
                }
                break;
            default:
                break;
        }
        free(slot->minimum);
        slot->minimum = new_mem;
    }
    if (slot->maximum) {
        void *new_mem = calloc(1, length * mapper_type_size(type));
        switch (slot->type) {
            case 'i':
                for (i = 0; i < length; i++) {
                    propval_set_from_lo_arg(new_mem, type,
                                            (lo_arg*)&((int*)slot->minimum)[i],
                                            'i', i);
                }
                break;
            case 'f':
                for (i = 0; i < length; i++) {
                    propval_set_from_lo_arg(new_mem, type,
                                            (lo_arg*)&((float*)slot->minimum)[i],
                                            'f', i);
                }
                break;
            case 'd':
                for (i = 0; i < length; i++) {
                    propval_set_from_lo_arg(new_mem, type,
                                            (lo_arg*)&((double*)slot->minimum)[i],
                                            'd', i);
                }
                break;
            default:
                break;
        }
        free(slot->maximum);
        slot->maximum = new_mem;
    }
    slot->type = type;
    slot->length= length;
}

// if 'override' flag is not set, only remote properties can be set
int mapper_map_set_from_message(mapper_map_internal map, mapper_message_t *msg,
                                int *order, int override)
{
    int updated = 0;
    lo_arg **args;
    const char *types = 0;
    int num_args;
    /* First record any provided parameters. */

    int64_t id;
    if (!mapper_msg_get_param_if_int64(msg, AT_ID, &id)
        && *(int64_t*)&map->props.id != id) {
        map->props.id = id;
        updated++;
    }

    int i, slot = -1;
    args = mapper_msg_get_param(msg, AT_SLOT, &types, &num_args);
    if (args && types) {
        if (map->destination.local) {
            if (num_args == 1 && types[0] == 'i') {
                // find slot index
                for (i = 0; i < map->props.num_sources; i++) {
                    if (map->sources[i].props->slot_id == args[0]->i) {
                        slot = i;
                        break;
                    }
                }
                if (i == map->props.num_sources) {
                    trace("error: slot index not found.\n");
                    return 0;
                }
            }
        }
        else if (num_args == map->props.num_sources) {
            for (i = 0; i < num_args; i++) {
                if (types[i] != 'i')
                    continue;
                map->props.sources[i].slot_id = args[i]->i;
            }
        }
    }

    const char *type = 0;
    /* source type */
    args = mapper_msg_get_param(msg, AT_SRC_TYPE, &types, &num_args);
    if (args && types && (types[0] == 'c' || types[0] == 's' || types[0] == 'S')) {
        char type;
        if (slot >= 0) {
            if (!map->sources[slot].local) {
                type = types[0] == 'c' ? (*args)->c : (&(*args)->s)[0];
                if (!(map->sources[slot].status & MAPPER_TYPE_KNOWN)) {
                    map->props.sources[slot].type = type;
                    map->sources[slot].status |= MAPPER_TYPE_KNOWN;
                    updated++;
                }
                else if (map->props.sources[slot].type != type) {
                    // type may have been tentatively set for min or max properties
                    upgrade_extrema_memory(&map->props.sources[slot], type,
                                           map->props.sources[slot].length);
                    updated++;
                }
            }
        }
        else if (num_args == map->props.num_sources) {
            for (i = 0; i < num_args; i++) {
                if (map->sources[i].local)
                    continue;
                if (types[i] != 'c' && types[i] != 's' && types[i] != 'S')
                    continue;
                type = types[i] == 'c' ? (args[i])->c : (&(args[i])->s)[0];
                if (!(map->sources[i].status & MAPPER_TYPE_KNOWN)) {
                    map->props.sources[i].type = type;
                    map->sources[i].status |= MAPPER_TYPE_KNOWN;
                    updated++;
                }
                else if (map->props.sources[i].type != type) {
                    upgrade_extrema_memory(&map->props.sources[i], type,
                                           map->props.sources[i].length);
                    updated++;
                }
            }
        }
    }

    /* source length */
    args = mapper_msg_get_param(msg, AT_SRC_LENGTH, &types, &num_args);
    if (args && types && types[0] == 'i') {
        if (slot >= 0) {
            if (!map->sources[slot].local) {
                if (!(map->sources[slot].status & MAPPER_LENGTH_KNOWN)) {
                    map->props.sources[slot].length = (*args)->i;
                    map->sources[slot].status |= MAPPER_LENGTH_KNOWN;
                    updated++;
                }
                else if (map->props.sources[slot].length != (*args)->i) {
                    upgrade_extrema_memory(&map->props.sources[slot],
                                           map->props.sources[slot].type,
                                           (*args)->i);
                    updated++;
                }
            }
        }
        else if (num_args == map->props.num_sources) {
            for (i = 0; i < num_args; i++) {
                if (map->sources[i].local)
                    continue;
                if (types[i] != 'i')
                    continue;
                if (!(map->sources[i].status & MAPPER_LENGTH_KNOWN)) {
                    map->props.sources[i].length = args[i]->i;
                    map->sources[i].status |= MAPPER_LENGTH_KNOWN;
                    updated++;
                }
                else if (map->props.sources[i].length != args[i]->i) {
                    upgrade_extrema_memory(&map->props.sources[i],
                                           map->props.sources[i].type,
                                           args[i]->i);
                }
            }
        }
    }

    if (!map->destination.local) {
        /* Destination type. */
        type = mapper_msg_get_param_if_char(msg, AT_DEST_TYPE);
        if (type && !check_signal_type(type[0])) {
            if (type[0] != map->props.destination.type) {
                map->props.destination.type = type[0];
                map->destination.status |= MAPPER_TYPE_KNOWN;
                updated++;
            }
        }

        /* Destination length. */
        if (!mapper_msg_get_param_if_int(msg, AT_DEST_LENGTH, &num_args)) {
            if (num_args != map->props.destination.length) {
                map->props.destination.length = num_args;
                map->destination.status |= MAPPER_LENGTH_KNOWN;
                updated++;
            }
        }
    }

    // TODO: handle range calibration coming from remote device
    /* Range information. */
    if (set_range(map, msg, slot)) {
        // TODO: may need to set linear expression without being admin
        if (map->props.mode == MO_LINEAR)
            mapper_map_set_mode_linear(map);
        updated++;
    }

    /* Muting. */
    int muting;
    if (!mapper_msg_get_param_if_int(msg, AT_MUTE, &muting)
        && map->props.muted != muting) {
        map->props.muted = muting;
        updated++;
    }

    /* Calibrating. */
    args = mapper_msg_get_param(msg, AT_CALIBRATING, &types, &num_args);
    if (args && types) {
        int calibrating;
        if (slot >= 0) {
            calibrating = (types[0] == 'T');
            if (map->sources[slot].props->calibrating != calibrating) {
                map->sources[slot].props->calibrating = calibrating;
                updated++;
            }
        }
        else if (num_args == map->props.num_sources) {
            for (i = 0; i < num_args; i++) {
                calibrating = (types[i] == 'T');
                if (map->sources[i].props->calibrating != calibrating) {
                    map->sources[i].props->calibrating = calibrating;
                    updated++;
                }
            }
        }
    }

    /* Range boundary actions */
    args = mapper_msg_get_param(msg, AT_SRC_BOUND_MAX, &types, &num_args);
    if (args && types) {
        int bound_max;
        if (slot >= 0 && (types[0] == 's' || types[0] == 'S')) {
            bound_max = mapper_get_boundary_action_from_string(&args[0]->s);
            if (bound_max != BA_UNDEFINED
                && bound_max != map->props.sources[slot].bound_max) {
                map->props.sources[slot].bound_max = bound_max;
                updated++;
            }
        }
        else if (num_args == map->props.num_sources) {
            for (i = 0; i < num_args; i++) {
                if (types[i] != 's' && types[i] != 'S')
                    continue;
                bound_max = mapper_get_boundary_action_from_string(&args[i]->s);
                if (bound_max != BA_UNDEFINED
                    && bound_max != map->props.sources[i].bound_max) {
                    map->props.sources[i].bound_max = bound_max;
                    updated++;
                }
            }
        }
    }
    args = mapper_msg_get_param(msg, AT_SRC_BOUND_MIN, &types, &num_args);
    if (args && types) {
        int bound_min;
        if (slot >= 0 && (types[0] == 's' || types[0] == 'S')) {
            bound_min = mapper_get_boundary_action_from_string(&args[0]->s);
            if (bound_min != BA_UNDEFINED
                && bound_min != map->props.sources[slot].bound_min) {
                map->props.sources[slot].bound_min = bound_min;
                updated++;
            }
        }
        else if (num_args == map->props.num_sources) {
            for (i = 0; i < num_args; i++) {
                if (types[i] != 's' && types[i] != 'S')
                    continue;
                bound_min = mapper_get_boundary_action_from_string(&args[i]->s);
                if (bound_min != BA_UNDEFINED
                    && bound_min != map->props.sources[i].bound_min) {
                    map->props.sources[i].bound_min = bound_min;
                    updated++;
                }
            }
        }
    }
    args = mapper_msg_get_param(msg, AT_DEST_BOUND_MAX, &types, &num_args);
    if (args && types) {
        int bound_max;
        if (types[0] == 's' || types[0] == 'S') {
            bound_max = mapper_get_boundary_action_from_string(&args[0]->s);
            if (bound_max != BA_UNDEFINED
                && bound_max != map->props.destination.bound_max) {
                map->props.destination.bound_max = bound_max;
                updated++;
            }
        }
    }
    args = mapper_msg_get_param(msg, AT_DEST_BOUND_MIN, &types, &num_args);
    if (args && types) {
        int bound_min;
        if (types[0] == 's' || types[0] == 'S') {
            bound_min = mapper_get_boundary_action_from_string(&args[0]->s);
            if (bound_min != BA_UNDEFINED
                && bound_min != map->props.destination.bound_min) {
                map->props.destination.bound_min = bound_min;
                updated++;
            }
        }
    }

    /* Processing location. */
    args = mapper_msg_get_param(msg, AT_PROCESS, &types, &num_args);
    if (args && types && (types[0] == 's' || types[0] == 'S')) {
        int at_source = (strcmp(&args[0]->s, "source") == 0);
        if (at_source && !map->one_source) {
            /* Processing must take place at destination if map
             * includes source signals from different devices. */
            at_source = 0;
        }
        if (at_source && map->props.process_location == MAPPER_DESTINATION) {
            map->props.process_location = MAPPER_SOURCE;
            updated++;
        }
        else if (!at_source && map->props.process_location == MAPPER_SOURCE) {
            map->props.process_location = MAPPER_DESTINATION;
            updated++;
        }
    }

    /* Expression. */
    const char *expr = mapper_msg_get_param_if_string(msg, AT_EXPRESSION);
    if (expr) {
        if (strstr(expr, "y{-")) {
            map->props.process_location = MAPPER_DESTINATION;
        }
        int should_compile = 0;
        if (map->props.process_location == MAPPER_DESTINATION) {
            // check if destination is local
            if (map->destination.local)
                should_compile = 1;
        }
        else {
            for (i = 0; i < map->props.num_sources; i++) {
                if (map->sources[i].local)
                    should_compile = 1;
            }
        }
        if (should_compile) {
            if (map->props.num_sources == 1 || slot < 0) {
                // expression may need to be modified to match ordered sources
                char *new_expr = fix_expression_source_order(map->props.num_sources,
                                                             order, expr);
                if (new_expr) {
                    if ((map->sources[i].status & MAPPER_LENGTH_KNOWN)
                        && (map->sources[i].status & MAPPER_TYPE_KNOWN)) {
                        if (!replace_expression_string(map, new_expr))
                            reallocate_map_histories(map);
                    }
                    else {
                        // check if process_location needs to change
                        if (!map->props.expression)
                            map->props.expression = strdup(new_expr);
                        else if (strcmp(map->props.expression, new_expr)) {
                            free(map->props.expression);
                            map->props.expression = strdup(new_expr);
                        }
                    }
                    free(new_expr);
                }
            }
        }
        else {
            if (!map->props.expression)
                map->props.expression = strdup(expr);
            else if (strcmp(map->props.expression, expr)) {
                free(map->props.expression);
                map->props.expression = strdup(expr);
            }
        }
    }

    /* Scopes */
    args = mapper_msg_get_param(msg, AT_SCOPE, &types, &num_args);
    if (num_args && types && (types[0] == 's' || types[0] == 'S'))
        updated += mapper_map_update_scope(&map->props.scope, args, num_args);
    /* Instances. */
    args = mapper_msg_get_param(msg, AT_SEND_AS_INSTANCE, &types, &num_args);
    if (args && types) {
        int send_as_instance;
        if (slot >= 0) {
            send_as_instance = (types[0] == 'T');
            if (map->props.sources[slot].send_as_instance != send_as_instance) {
                map->props.sources[slot].send_as_instance = send_as_instance;
                updated++;
            }
        }
        else if (num_args == map->props.num_sources) {
            for (i = 0; i < num_args; i++) {
                send_as_instance = (types[i] == 'T');
                if (map->props.sources[i].send_as_instance != send_as_instance) {
                    map->props.sources[i].send_as_instance = send_as_instance;
                    updated++;
                }
            }
        }
    }

    /* Cause Update */
    args = mapper_msg_get_param(msg, AT_CAUSE_UPDATE, &types, &num_args);
    if (args && types) {
        int cause_update;
        if (num_args == map->props.num_sources) {
            for (i = 0; i < num_args; i++) {
                cause_update = (types[i] == 'T');
                if (map->props.sources[i].cause_update != cause_update) {
                    map->props.sources[i].cause_update = cause_update;
                    updated++;
                }
            }
        }
    }

    /* Extra properties. */
    updated += mapper_msg_add_or_update_extra_params(map->props.extra, msg);

    /* Mode */
    args = mapper_msg_get_param(msg, AT_MODE, &types, &num_args);
    if (args && types && (types[0] == 's' || types[0] == 'S')) {
        int mode = mapper_get_mode_type_from_string(&args[0]->s);
        if (mode != map->props.mode) {
            map->props.mode = mode;
            updated++;
        }
    }

    if (map->status < MAPPER_READY) {
        // check if mapping is now "ready"
        mapper_map_check_status(map);
    }
    else if (updated) {
        apply_mode(map);
    }

    return updated;
}

/* TODO: figuring out the correct number of instances for the user variables
 * is a bit tricky... for now we will use the maximum. */
void reallocate_map_histories(mapper_map_internal map)
{
    int i, j;

    mapper_slot_internal s;
    mapper_map_slot p;
    int history_size;

    // Reallocate source histories
    for (i = 0; i < map->props.num_sources; i++) {
        s = &map->sources[i];
        p = &map->props.sources[i];

        // If there is no expression, then no memory needs to be reallocated.
        if (!map->expr)
            continue;

        history_size = mapper_expr_input_history_size(map->expr, i);
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

        // reallocate user variable histories
        int new_num_vars = mapper_expr_num_variables(map->expr);
        if (new_num_vars > map->num_expr_vars) {
            for (i = 0; i < map->num_var_instances; i++) {
                map->expr_vars[i] = realloc(map->expr_vars[i], new_num_vars *
                                          sizeof(struct _mapper_history));
                // initialize new variables...
                for (j = map->num_expr_vars; j < new_num_vars; j++) {
                    map->expr_vars[i][j].type = 'd';
                    map->expr_vars[i][j].length = 0;
                    map->expr_vars[i][j].size = 0;
                    map->expr_vars[i][j].value = 0;
                    map->expr_vars[i][j].timetag = 0;
                    map->expr_vars[i][j].position = -1;
                }
                for (j = 0; j < new_num_vars; j++) {
                    int history_size = mapper_expr_variable_history_size(map->expr, j);
                    int vector_length = mapper_expr_variable_vector_length(map->expr, j);
                    mhist_realloc(map->expr_vars[i]+j, history_size,
                                  vector_length * sizeof(double), 0);
                    (map->expr_vars[i]+j)->length = vector_length;
                    (map->expr_vars[i]+j)->size = history_size;
                    (map->expr_vars[i]+j)->position = -1;
                }
            }
            map->num_expr_vars = new_num_vars;
        }
        else if (new_num_vars < map->num_expr_vars) {
            // Do nothing for now...
        }
    }

    history_size = mapper_expr_output_history_size(map->expr);
    s = &map->destination;
    p = &map->props.destination;

    // If there is no expression, then no memory needs to be reallocated.
    if (map->expr) {
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
        int new_num_vars = mapper_expr_num_variables(map->expr);
        if (new_num_vars > map->num_expr_vars) {
            for (i = 0; i < map->num_var_instances; i++) {
                map->expr_vars[i] = realloc(map->expr_vars[i], new_num_vars *
                                          sizeof(struct _mapper_history));
                // initialize new variables...
                for (j = map->num_expr_vars; j < new_num_vars; j++) {
                    map->expr_vars[i][j].type = 'd';
                    map->expr_vars[i][j].length = 0;
                    map->expr_vars[i][j].size = 0;
                    map->expr_vars[i][j].value = 0;
                    map->expr_vars[i][j].timetag = 0;
                    map->expr_vars[i][j].position = -1;
                }
                for (j = 0; j < new_num_vars; j++) {
                    int history_size = mapper_expr_variable_history_size(map->expr, j);
                    int vector_length = mapper_expr_variable_vector_length(map->expr, j);
                    mhist_realloc(map->expr_vars[i]+j, history_size,
                                  vector_length * sizeof(double), 0);
                    (map->expr_vars[i]+j)->length = vector_length;
                    (map->expr_vars[i]+j)->size = history_size;
                    (map->expr_vars[i]+j)->position = -1;
                }
            }
            map->num_expr_vars = new_num_vars;
        }
        else if (new_num_vars < map->num_expr_vars) {
            // Do nothing for now...
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

inline static void message_add_bool(lo_message m, int value) {
    if (value)
        lo_message_add_true(m);
    else
        lo_message_add_false(m);
}

void mapper_map_prepare_osc_message(lo_message m, mapper_map_internal map,
                                    int slot, int suppress_remote_props)
{
    int i;
    mapper_map props = &map->props;

    // Mapping id
    lo_message_add_string(m, mapper_get_param_string(AT_ID));
    lo_message_add_int64(m, *((int64_t*)&map->props.id));

    // Mapping mode
    lo_message_add_string(m, mapper_get_param_string(AT_MODE));
    lo_message_add_string(m, mapper_get_mode_type_string(props->mode));

    // Processing location
    lo_message_add_string(m, mapper_get_param_string(AT_PROCESS));
    if (props->process_location == MAPPER_SOURCE)
        lo_message_add_string(m, "source");
    else
        lo_message_add_string(m, "destination");

    // Expression string
    if (props->expression) {
        lo_message_add_string(m, mapper_get_param_string(AT_EXPRESSION));
        lo_message_add_string(m, props->expression);
    }

    // Source type(s) and vector length(s)
    if (slot < 0) {
        int known = 1;
        for (i = 0; i < props->num_sources; i++) {
            if (!(map->sources[i].status & MAPPER_LENGTH_KNOWN)
                || !(map->sources[i].status & MAPPER_TYPE_KNOWN)) {
                known = 0;
                break;
            }
        }
        if (known) {
            lo_message_add_string(m, mapper_get_param_string(AT_SRC_TYPE));
            for (i = 0; i < props->num_sources; i++) {
                lo_message_add_char(m, props->sources[i].type);
            }
            lo_message_add_string(m, mapper_get_param_string(AT_SRC_LENGTH));
            for (i = 0; i < props->num_sources; i++) {
                lo_message_add_int32(m, props->sources[i].length);
            }
        }
    }
    else if (map->sources[slot].local || !suppress_remote_props) {
        if (map->sources[slot].status & MAPPER_TYPE_KNOWN) {
            lo_message_add_string(m, mapper_get_param_string(AT_SRC_TYPE));
            lo_message_add_char(m, props->sources[slot].type);
        }
        if (map->sources[slot].status & MAPPER_LENGTH_KNOWN) {
            lo_message_add_string(m, mapper_get_param_string(AT_SRC_LENGTH));
            lo_message_add_int32(m, props->sources[slot].length);
        }
    }

    if (map->destination.local || !suppress_remote_props) {
        // Destination type
        if (map->destination.status & MAPPER_TYPE_KNOWN) {
            lo_message_add_string(m, mapper_get_param_string(AT_DEST_TYPE));
            lo_message_add_char(m, props->destination.type);
        }

        // Destination vector length
        if (map->destination.status & MAPPER_LENGTH_KNOWN) {
            lo_message_add_string(m, mapper_get_param_string(AT_DEST_LENGTH));
            lo_message_add_int32(m, props->destination.length);
        }
    }

    if (slot >= 0) {
        if (props->sources[slot].minimum) {
            lo_message_add_string(m, mapper_get_param_string(AT_SRC_MIN));
            mapper_msg_add_typed_value(m, props->sources[slot].type,
                                       props->sources[slot].length,
                                       props->sources[slot].minimum);
        }

        if (props->sources[slot].maximum) {
            lo_message_add_string(m, mapper_get_param_string(AT_SRC_MAX));
            mapper_msg_add_typed_value(m, props->sources[slot].type,
                                       props->sources[slot].length,
                                       props->sources[slot].maximum);
        }
    }
    else if (map->props.num_sources == 1) {
        // TODO: extend to multiple sources
        if (props->sources[0].minimum) {
            lo_message_add_string(m, mapper_get_param_string(AT_SRC_MIN));
            mapper_msg_add_typed_value(m, props->sources[0].type,
                                       props->sources[0].length,
                                       props->sources[0].minimum);
        }

        if (props->sources[0].maximum) {
            lo_message_add_string(m, mapper_get_param_string(AT_SRC_MAX));
            mapper_msg_add_typed_value(m, props->sources[0].type,
                                       props->sources[0].length,
                                       props->sources[0].maximum);
        }
    }

    if (props->destination.minimum) {
        lo_message_add_string(m, mapper_get_param_string(AT_DEST_MIN));
        mapper_msg_add_typed_value(m, props->destination.type,
                                   props->destination.length,
                                   props->destination.minimum);
    }

    if (props->destination.maximum) {
        lo_message_add_string(m, mapper_get_param_string(AT_DEST_MAX));
        mapper_msg_add_typed_value(m, props->destination.type,
                                   props->destination.length,
                                   props->destination.maximum);
    }

    // Boundary actions
    if (slot >= 0) {
        if (map->sources[slot].local || !suppress_remote_props)  {
            lo_message_add_string(m, mapper_get_param_string(AT_SRC_BOUND_MIN));
            lo_message_add_string(m, mapper_get_boundary_action_string(props->sources[slot].bound_min));
        }
    }
    else if (!suppress_remote_props || !map->one_source || map->sources[0].local) {
        lo_message_add_string(m, mapper_get_param_string(AT_SRC_BOUND_MIN));
        for (i = 0; i < props->num_sources; i++)
            lo_message_add_string(m, mapper_get_boundary_action_string(props->sources[i].bound_min));
    }

    if (slot >= 0) {
        if (map->sources[slot].local || !suppress_remote_props) {
            lo_message_add_string(m, mapper_get_param_string(AT_SRC_BOUND_MAX));
            lo_message_add_string(m, mapper_get_boundary_action_string(props->sources[slot].bound_max));
        }
    }
    else if (!suppress_remote_props || !map->one_source || map->sources[0].local) {
        lo_message_add_string(m, mapper_get_param_string(AT_SRC_BOUND_MAX));
        for (i = 0; i < props->num_sources; i++)
            lo_message_add_string(m, mapper_get_boundary_action_string(props->sources[i].bound_max));
    }

    if (map->destination.local || !suppress_remote_props) {
        lo_message_add_string(m, mapper_get_param_string(AT_DEST_BOUND_MIN));
        lo_message_add_string(m, mapper_get_boundary_action_string(props->destination.bound_min));
        lo_message_add_string(m, mapper_get_param_string(AT_DEST_BOUND_MAX));
        lo_message_add_string(m, mapper_get_boundary_action_string(props->destination.bound_max));
    }

    // Muting
    lo_message_add_string(m, mapper_get_param_string(AT_MUTE));
    message_add_bool(m, props->muted);

    // Calibrating
    lo_message_add_string(m, mapper_get_param_string(AT_CALIBRATING));
    if (slot >= 0)
        message_add_bool(m, props->sources[slot].calibrating);
    else {
        for (i = 0; i < props->num_sources; i++)
            message_add_bool(m, props->sources[i].calibrating);
    }

    // Mapping scopes
    lo_message_add_string(m, mapper_get_param_string(AT_SCOPE));
    if (props->scope.size) {
        for (i = 0; i < props->scope.size; i++)
            lo_message_add_string(m, props->scope.names[i]);
    }
    else
        lo_message_add_string(m, "none");

    // Slot
    lo_message_add_string(m, mapper_get_param_string(AT_SLOT));
    if (slot >= 0) {
        lo_message_add_int32(m, props->sources[slot].slot_id);
    }
    else {
        for (i = 0; i < props->num_sources; i++) {
            lo_message_add_int32(m, props->sources[i].slot_id);
        }
    }

    // Send as Instance
    lo_message_add_string(m, mapper_get_param_string(AT_SEND_AS_INSTANCE));
    if (slot >= 0)
        message_add_bool(m, props->sources[slot].send_as_instance);
    else {
        for (i = 0; i < props->num_sources; i++)
            message_add_bool(m, props->sources[i].send_as_instance);
    }

    // Cause update
    if (props->num_sources > 1) {
        lo_message_add_string(m, mapper_get_param_string(AT_CAUSE_UPDATE));
        if (slot >= 0)
            message_add_bool(m, props->sources[slot].cause_update);
        else {
            for (i = 0; i < props->num_sources; i++)
                message_add_bool(m, props->sources[i].cause_update);
        }
    }

    // "Extra" properties
    mapper_msg_add_value_table(m, props->extra);
}
