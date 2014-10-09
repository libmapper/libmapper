
#include <string.h>
#include <math.h>
#include <stdlib.h>
#include <stdio.h>

#include "mapper_internal.h"
#include "types_internal.h"
#include <mapper/mapper.h>

/* For now we only allow combiners used in combination with raw inputs. */

void mapper_combiner_free(mapper_combiner c)
{
    int i, j;
    if (c->num_expr_vars) {
        for (i=0; i<c->parent->num_instances; i++) {
            for (j=0; j<c->num_expr_vars; j++) {
                free(c->expr_vars[i][j].value);
                free(c->expr_vars[i][j].timetag);
            }
            free(c->expr_vars[i]);
        }
        free(c->expr_vars);
    }
    if (c->expr)
        mapper_expr_free(c->expr);
    if (c->props.expression)
        free(c->props.expression);
    if (c->slots)
        free(c->slots);
}

void reallocate_combiner_output_history()
{

}

void reallocate_combiner_input_history(mapper_combiner c,
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

int mapper_combiner_get_var_info(mapper_combiner combiner, int slot,
                                 char *datatype, int *vector_length)
{
    // we are interested in the type & length of the remote signal
    mapper_connection con = combiner->parent->connections;
    while (con) {
        if (con->props.slot == slot) {
            *datatype = con->props.remote_type;
            *vector_length = con->props.remote_length;
            return 0;
        }
        con = con->next;
    }
    return 1;
}

/* Helper to replace a connection's expression only if the given string
 * parses successfully. Returns 0 on success, non-zero on error. */
static int replace_expression_string(mapper_combiner c,
                                     const char *expr_str,
                                     int *input_history_size,
                                     int *output_history_size)
{
    mapper_expr expr = mapper_expr_new_from_string(
        expr_str, 0, c->parent->signal->props.type,
        0, c->parent->signal->props.length, c);

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

void mapper_combiner_set_mode_expression(mapper_combiner c, const char *expr)
{
    // if input index included in expression > num connections, use zero instead as placeholder
    // we will allow declaring combiner expression with arbitrary number of inputs

    int input_history_size, output_history_size;
    if (replace_expression_string(c, expr, &input_history_size,
                                  &output_history_size))
        return;

    // create matching slots for incoming connections referenced in the expression
    int num_slots = mapper_expr_num_input_slots(c->expr);

    if (c->slots)
        free(c->slots);

    c->slots = (mapper_combiner_slot) calloc(1, num_slots * sizeof(struct _mapper_combiner_slot));

    int i;
    for (i = 0; i < num_slots; i++) {
        c->slots[i].id = i;
    }
    c->props.num_slots = num_slots;

    // check connections for matches
    mapper_connection cn = c->parent->connections;
    while (cn) {
        if (cn->props.slot >= 0 && cn->props.slot < num_slots) {
            c->slots[cn->props.slot].connection = cn;
            // may need to reallocate history memory
            int history_size = mapper_expr_combiner_input_history_size(c->expr, cn->props.slot);
            if (history_size > 0)
                reallocate_combiner_input_history(c, cn->props.slot,
                                                  history_size);
        }
        cn = cn->next;
    }

    c->props.mode = MO_EXPRESSION;
//    reallocate_combiner_histories(c, input_history_size,
//                                  output_history_size);
}

int mapper_combiner_set_from_message(mapper_combiner c,
                                     mapper_message_t *msg)
{
    int updated = 0;
    /* First record any provided parameters. */

    /* Expression. */
    const char *expr = mapper_msg_get_param_if_string(msg, AT_EXPRESSION);

    if (expr && (!c->props.expression || strcmp(c->props.expression, expr))) {
        int input_history_size, output_history_size;
        if (!replace_expression_string(c, expr, &input_history_size,
                                       &output_history_size)) {
        }
        updated++;
    }
    /* Now set the mode type depending on the requested type and
     * the known properties. */

    int mode = mapper_msg_get_mode(msg);
    if (mode >= 0 && mode != c->props.mode)
        updated++;
    switch (mode)
    {
        case -1:
            c->props.mode = MO_EXPRESSION;
            // continue
        case MO_EXPRESSION:
            {
                if (!c->props.expression) {
                    c->props.expression = strdup("y=x");
                }
                mapper_combiner_set_mode_expression(c, c->props.expression);
            }
            break;
        default:
            trace("unknown result from mapper_msg_get_mode()\n");
            break;
    }
    return updated;
}

mapper_combiner_slot mapper_combiner_get_slot(mapper_combiner combiner, int id)
{
    if (!combiner)
        return 0;
    int i;
    for (i = 0; i < combiner->props.num_slots; i++) {
        if (combiner->slots[i].id == id)
            return &combiner->slots[i];
    }
    return 0;
}

int mapper_combiner_perform(mapper_combiner combiner, int instance)
{
    // call expression evaluator with connection data, signal value pointer
    // TODO: add typestring
    return mapper_expr_evaluate(combiner->expr, 0, combiner->expr_vars,
                                &combiner->parent->history[instance], 0,
                                combiner, instance);
}
