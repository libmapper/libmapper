
#include <string.h>
#include <math.h>
#include <stdlib.h>
#include <stdio.h>

#include "mapper_internal.h"
#include "types_internal.h"
#include <mapper/mapper.h>

/* For now we only allow combiners used in combination with raw inputs. */

void mapper_combiner_free(mapper_combiner cb)
{
    int i, j;
    if (cb->num_expr_vars) {
        for (i = 0; i < cb->parent->num_instances; i++) {
            for (j = 0; j < cb->num_expr_vars; j++) {
                free(cb->expr_vars[i][j].value);
                free(cb->expr_vars[i][j].timetag);
            }
            free(cb->expr_vars[i]);
        }
        free(cb->expr_vars);
    }
    if (cb->expr)
        mapper_expr_free(cb->expr);
    if (cb->props.expression)
        free(cb->props.expression);
    if (cb->slots)
        free(cb->slots);
}

void reallocate_combiner_output_history()
{

}

void reallocate_combiner_input_history(mapper_combiner cb,
                                       int index,
                                       int history_size)
{
//    mapper_signal sig = c->parent->signal;
//    int i, j;
//
//    // At least for now, exit if this is an output signal
//    if (sig->props.is_output) {
//        return;
//    }
//
//    // If there is no expression, then no memory needs to be
//    // reallocated.
//    if (!c->expr)
//        return;
//
//    if (output_history_size < 1)
//        output_history_size = 1;
//
//    // Reallocate router_signal histories
//    if (output_history_size > c->parent->history_size) {
//        size_t sample_size = msig_vector_bytes(sig);
//        for (i=0; i<sig->props.num_instances; i++) {
//            mhist_realloc(&c->parent->history[i], input_history_size,
//                          sample_size, 1);
//        }
//        c->parent->history_size = input_history_size;
//    }
//    else if (output_history_size < c->parent->history_size) {
//        // Do nothing for now...
//    }
//
//    // reallocate output histories
//    if (output_history_size > c->output_history_size) {
//        int sample_size = mapper_type_size(c->props.dest_type) * c->props.dest_length;
//        for (i=0; i<sig->props.num_instances; i++) {
//            mhist_realloc(&c->history[i], output_history_size, sample_size, 0);
//        }
//        c->output_history_size = output_history_size;
//    }
//    else if (output_history_size < mapper_expr_output_history_size(c->expr)) {
//        // Do nothing for now...
//    }
//
//    // reallocate user variable histories
//    int new_num_vars = mapper_expr_num_variables(c->expr);
//    if (new_num_vars > c->num_expr_vars) {
//        for (i=0; i<sig->props.num_instances; i++) {
//            c->expr_vars[i] = realloc(c->expr_vars[i], new_num_vars *
//                                      sizeof(struct _mapper_signal_history));
//            // initialize new variables...
//            for (j=c->num_expr_vars; j<new_num_vars; j++) {
//                c->expr_vars[i][j].type = 'd';
//                c->expr_vars[i][j].length = 0;
//                c->expr_vars[i][j].size = 0;
//                c->expr_vars[i][j].value = 0;
//                c->expr_vars[i][j].timetag = 0;
//                c->expr_vars[i][j].position = -1;
//            }
//        }
//        c->num_expr_vars = new_num_vars;
//    }
//    else if (new_num_vars < c->num_expr_vars) {
//        // Do nothing for now...
//    }
//    for (i=0; i<sig->props.num_instances; i++) {
//        for (j=0; j<new_num_vars; j++) {
//            int history_size = mapper_expr_variable_history_size(c->expr, j);
//            int vector_length = mapper_expr_variable_vector_length(c->expr, j);
//            mhist_realloc(c->expr_vars[i]+j, history_size,
//                          vector_length * sizeof(double), 0);
//            (c->expr_vars[i]+j)->length = vector_length;
//            (c->expr_vars[i]+j)->size = history_size;
//            (c->expr_vars[i]+j)->position = -1;
//        }
//    }
}

int mapper_combiner_get_slot_info(mapper_combiner cb, int slot_num,
                                  char *datatype, int *vector_length)
{
    // we are interested in the type & length of the remote signal
    if (!cb->slots || cb->props.num_slots <= slot_num
        || !cb->slots[slot_num].connection)
        return 1;

    *datatype = cb->slots[slot_num].connection->props.remote_type;
    *vector_length = cb->slots[slot_num].connection->props.remote_length;

    return 0;
}

void mapper_combiner_mute_slot(mapper_combiner cb, int slot_num, int mute)
{
    if (cb->slots && slot_num < cb->props.num_slots)
        cb->slots[slot_num].cause_update = !mute;
}

/* Helper to replace a connection's expression only if the given string
 * parses successfully. Returns 0 on success, non-zero on error. */
static int replace_expression_string(mapper_combiner cb,
                                     const char *expr_str,
                                     int *input_history_size,
                                     int *output_history_size)
{
    if (!cb->ready) {
        // copy expression string
        int len = strlen(expr_str);
        cb->props.expression = realloc(cb->props.expression, len+1);
        memcpy(cb->props.expression, expr_str, len);
        cb->props.expression[len] = '\0';
        return 1;
    }

    mapper_expr expr = mapper_expr_new_from_string(
        expr_str, 0, cb->parent->signal->props.type,
        0, cb->parent->signal->props.length, cb);

    if (!expr)
        return 1;

    *input_history_size = mapper_expr_input_history_size(expr);
    *output_history_size = mapper_expr_output_history_size(expr);

    if (cb->expr)
        mapper_expr_free(cb->expr);

    cb->expr = expr;

    if (cb->props.expression == expr_str)
        return 0;

    int len = strlen(expr_str);
    if (!cb->props.expression || len > strlen(cb->props.expression))
        cb->props.expression = realloc(cb->props.expression, len+1);

    /* Using strncpy() here causes memory profiling errors due to possible
     * overlapping memory (e.g. expr_str == c->props.expression). */
    memcpy(cb->props.expression, expr_str, len);
    cb->props.expression[len] = '\0';

    return 0;
}

void mapper_combiner_set_num_slots(mapper_combiner cb, int num_slots)
{
    if (cb->slots)
        free(cb->slots);

    cb->slots = (mapper_combiner_slot) calloc(1, num_slots * sizeof(struct _mapper_combiner_slot));

    int i;
    for (i = 0; i < num_slots; i++) {
        cb->slots[i].id = i;
    }
    cb->props.num_slots = num_slots;
}

static void mapper_combiner_set_mode_expression(mapper_combiner cb,
                                                const char *expr)
{
    cb->props.mode = MO_EXPRESSION;

    // Skip if we have not yet collected all relevant connection info.
    if (!cb->ready)
        return;

    // if input index included in expression > num connections, use zero instead as placeholder
    // we will allow declaring combiner expression with arbitrary number of inputs

    int input_history_size, output_history_size;
    if (replace_expression_string(cb, expr, &input_history_size,
                                  &output_history_size))
        return;

    // create matching slots for incoming connections referenced in the expression
    int num_slots = mapper_expr_num_input_slots(cb->expr);

    if (num_slots < cb->props.num_slots) {
        trace("error: not enough connected inputs for combiner expression.");
        return;
    }

    // check connections for matches
    mapper_connection cn = cb->parent->connections;
    while (cn) {
        if (cn->props.slot >= 0 && cn->props.slot < num_slots) {
            cb->slots[cn->props.slot].connection = cn;
            // may need to reallocate history memory
            int history_size = mapper_expr_combiner_input_history_size(cb->expr, cn->props.slot);
            if (history_size > 0)
                reallocate_combiner_input_history(cb, cn->props.slot,
                                                  history_size);
        }
        cn = cn->next;
    }

//    reallocate_combiner_histories(c, input_history_size,
//                                  output_history_size);
}

static void mapper_combiner_set_mode_none(mapper_combiner c)
{
    // need to remove combiner
    c->props.mode = MO_NONE;
}

int mapper_combiner_set_slot_connection(mapper_combiner cb, int slot_num,
                                        mapper_connection cn)
{
    if (slot_num >= cb->props.num_slots)
        return -1;

    cb->slots[slot_num].connection = cn;

    if (!cb->ready) {
        int i, missing = 0;
        for (i = 0; i < cb->props.num_slots; i++) {
            if (!cb->slots[i].connection) {
                missing = 1;
                break;
            }
        }
        if (missing)
            return -1;
        cb->ready = 1;
    }

    // ok to evaluate expression
    if (cb->props.mode == MO_EXPRESSION && cb->props.expression)
        mapper_combiner_set_mode_expression(cb, cb->props.expression);

    return 0;
}

int mapper_combiner_set_from_message(mapper_combiner cb,
                                     mapper_message_t *msg)
{
    int updated = 0;
    /* First record any provided parameters. */

    /* Expression. */
    const char *expr = mapper_msg_get_param_if_string(msg, AT_EXPRESSION);

    if (expr && (!cb->props.expression || strcmp(cb->props.expression, expr))) {
        int input_history_size, output_history_size;
        if (!replace_expression_string(cb, expr, &input_history_size,
                                       &output_history_size)) {
        }
        updated++;
    }
    /* Now set the mode type depending on the requested type and
     * the known properties. */

    int mode = mapper_msg_get_mode(msg);
    switch (mode)
    {
        case -1:
            cb->props.mode = MO_EXPRESSION;
            // continue
        case MO_EXPRESSION:
            if (cb->props.expression)
                mapper_combiner_set_mode_expression(cb, cb->props.expression);
            break;
        case MO_NONE:
            mapper_combiner_set_mode_none(cb);
            break;
        default:
            trace("unknown result from mapper_msg_get_mode()\n");
            break;
    }
    if (mode != cb->props.mode)
        updated++;
    return updated;
}

void mapper_combiner_new_connection(mapper_combiner cb, mapper_connection cn)
{
    // check if connection slot matches any of our combiner inputs
//    int i;
//    for (i = 0; i < cb->props.num_slots; i++) {
//    }
}

mapper_combiner_slot mapper_combiner_get_slot(mapper_combiner cb, int id)
{
    if (!cb)
        return 0;
    int i;
    for (i = 0; i < cb->props.num_slots; i++) {
        if (cb->slots[i].id == id)
            return &cb->slots[i];
    }
    return 0;
}

int mapper_combiner_perform(mapper_combiner cb, int instance)
{
    // call expression evaluator with connection data, signal value pointer
    // TODO: add typestring
    if (!cb->expr)
        return 0;

    return mapper_expr_evaluate(cb->expr, 0, cb->expr_vars,
                                &cb->parent->history[instance], 0,
                                cb, instance);
}
