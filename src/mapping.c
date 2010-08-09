
#include "mapper_internal.h"
#include "types_internal.h"
#include <mapper/mapper.h>
#include "operations.h"
#include "expression.h"

void mapper_mapping_perform(mapper_mapping mapping,
                            mapper_signal_value_t *from_value,
                            mapper_signal_value_t *to_value)
{
    int p, changed = 0;
    float v;
    error err = NO_ERR;


    p = mapping->history_pos;
    mapping->history_input[p] = from_value->f;
    v = mapping->history_input[p];

    if (mapping->type == BYPASS /*|| mapping->type==LINEAR */ ) {
        /*for (i=0; i < mapping->order_input; i++)
           v = mapping->history_input[(p+i)%5] * mapping->coef_input[i];

           for (i=0; i < mapping->order_output; i++)
           v = mapping->history_output[(p+i)%5] * mapping->coef_output[i]; */

        /*v=mapping->history_input[p]; */
        mapping->history_output[p] = v;

        --p;
        if (p < 0)
            p = MAX_HISTORY_ORDER;
    }


    else if (mapping->type == EXPRESSION || mapping->type == LINEAR) {
        v = EvalTree(mapping->expr_tree, mapping->history_input,
                     mapping->history_output, p, &err);
        mapping->history_output[p] = v;

        --p;
        if (p < 0)
            p = MAX_HISTORY_ORDER - 1;
    }

    else if (mapping->type == CALIBRATE) {
        /* If calibration mode has just taken effect, first data
         * sample sets source min and max */
        if (mapping->range.rewrite) {
            mapping->range.src_min = from_value->f;
            mapping->range.src_max = from_value->f;
            mapping->range.known |= RANGE_SRC_MIN | RANGE_SRC_MAX;
            mapping->range.rewrite = 0;
            changed = 1;
        } else {
            if (from_value->f < mapping->range.src_min) {
                mapping->range.src_min = from_value->f;
                mapping->range.known |= RANGE_SRC_MIN;
                changed = 1;
            }
            if (from_value->f > mapping->range.src_max) {
                mapping->range.src_max = from_value->f;
                mapping->range.known |= RANGE_SRC_MAX;
                changed = 1;
            }
        }

        if (changed) {
            /* Need to arrange to send an admin bus message stating
             * new ranges and expression.  The expression has to be
             * modified to fit the range. */
            if (mapping->range.src_min == mapping->range.src_max) {
                free(mapping->expression);
                mapping->expression = (char*) malloc(100 * sizeof(char));
                snprintf(mapping->expression, 100, "y=%f",
                         mapping->range.src_min);
            } else if (mapping->range.known == RANGE_KNOWN
                       && mapping->range.src_min == mapping->range.dest_min
                       && mapping->range.src_max ==
                       mapping->range.dest_max)
                snprintf(mapping->expression, 100, "y=x");

            else if (mapping->range.known == RANGE_KNOWN) {
                float scale = (mapping->range.dest_min
                               - mapping->range.dest_max)
                    / (mapping->range.src_min - mapping->range.src_max);
                float offset =
                    (mapping->range.dest_max * mapping->range.src_min
                     - mapping->range.dest_min * mapping->range.src_max)
                    / (mapping->range.src_min - mapping->range.src_max);

                free(mapping->expression);
                mapping->expression = (char*) malloc(256 * sizeof(char));
                snprintf(mapping->expression, 256, "y=x*%g+%g",
                         scale, offset);
            }

            DeleteTree(mapping->expr_tree);
            Tree *T = NewTree();
            int success_tree = get_expr_Tree(T, mapping->expression);

            if (!success_tree)
                return;

            mapping->expr_tree = T;
        }

        v = EvalTree(mapping->expr_tree, mapping->history_input,
                     mapping->history_output, p, &err);
        mapping->history_output[p] = v;

        --p;
        if (p < 0)
            p = MAX_HISTORY_ORDER - 1;
    }


    mapping->history_pos = p;
    to_value->f = v;
}
