
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
    if (c->expression)
        free(c->expression);
    if (c->slots)
        free(c->slots);
}

void reallocate_combiner_histories(mapper_combiner c,
                                   int input_history_size,
                                   int output_history_size)
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

void mapper_combiner_set_mode_expression(mapper_combiner c, const char *expr)
{
    int max_inputs = 0;
    mapper_connection con = c->parent->connections;
    while (con) {
        max_inputs++;
        con = con->next;
    }

//    int input_history_size, output_history_size;
//    if (replace_expression_string(c, expr, &input_history_size,
//                                  &output_history_size))
//        return;
//
    c->mode = MO_EXPRESSION;
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
    if (expr && (!c->expression || strcmp(c->expression, expr))) {
//        int input_history_size, output_history_size;
//        if (!replace_expression_string(c, expr, &input_history_size,
//                                       &output_history_size)) {
//            if (c->mode == MO_EXPRESSION) {
//                reallocate_combiner_histories(c, input_history_size,
//                                              output_history_size);
//            }
//        }
        updated++;
    }

    /* Now set the mode type depending on the requested type and
     * the known properties. */

    int mode = mapper_msg_get_mode(msg);
    if (mode >= 0 && mode != c->mode)
        updated++;

    switch (mode)
    {
        case -1:
            c->mode = MO_EXPRESSION;
            // continue
        case MO_EXPRESSION:
            {
                if (!c->expression) {
                    c->expression = strdup("y=x");
                }
                mapper_combiner_set_mode_expression(c, c->expression);
            }
            break;
        default:
            trace("unknown result from mapper_msg_get_mode()\n");
            break;
    }
    return updated;
}

//mapper_combiner_update_connection(cb, slot, map->local, value, count, tt);
//void mapper_combiner_update_connection(mapper_combiner c, int slot, int )
//{
//    
//}

mapper_combiner_slot mapper_combiner_get_slot(mapper_combiner combiner, int id)
{
    return 0;
}

int mapper_combiner_perform(mapper_combiner combiner, int instance)
{
    // call expression evaluator with connection data, signal value pointer
    mapper_expr_evaluate(combiner->expr, 0, combiner->expr_vars,
                         combiner->parent->history[instance].value, 0,
                         combiner, instance);
    // updated si->has_value
    // return whether signal instance has value
    return 0;
}
