
#include <string.h>

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

    if (mapping->scaling == SC_BYPASS /*|| mapping->type==LINEAR */ ) {
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


    else if (mapping->scaling == SC_EXPRESSION
             || mapping->scaling == SC_LINEAR) {
        v = EvalTree(mapping->expr_tree, mapping->history_input,
                     mapping->history_output, p, &err);
        mapping->history_output[p] = v;

        --p;
        if (p < 0)
            p = MAX_HISTORY_ORDER - 1;
    }

    else if (mapping->scaling == SC_CALIBRATE) {
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

int mapper_clipping_perform(mapper_mapping mapping,
                            mapper_signal_value_t *from_value,
                            mapper_signal_value_t *to_value)
{
    int muted = 0;
    float v = from_value->f, total_range = abs(mapping->range.dest_max - mapping->range.dest_min), difference, modulo_difference;
    
    if (mapping->range.known) {
        if (v < mapping->range.dest_min) {
            switch (mapping->clip_lower) {
                case CT_MUTE:
                    // need to prevent value from being sent at all
                    muted = 1;
                    break;
                case CT_CLAMP:
                    // clamp value to range minimum
                    v = mapping->range.dest_min;
                    break;
                case CT_FOLD:
                    // fold value around range minimum
                    difference = abs(v - mapping->range.dest_min);
                    v = mapping->range.dest_min + difference;
                    if (v > mapping->range.dest_max) {
                        // value now exceeds range maximum!
                        switch (mapping->clip_upper) {
                            case CT_MUTE:
                                // need to prevent value from being sent at all
                                muted = 1;
                                break;
                            case CT_CLAMP:
                                // clamp value to range minimum
                                v = mapping->range.dest_max;
                                break;
                            case CT_FOLD:
                                // both clip modes are set to fold!
                                difference = abs(v - mapping->range.dest_max);
                                modulo_difference = difference - (int)(difference / total_range) * total_range;
                                if ((int)(difference / total_range) % 2 == 0) {
                                    v = mapping->range.dest_max - modulo_difference;
                                }
                                else
                                    v = mapping->range.dest_min + modulo_difference;
                                break;
                            case CT_WRAP:
                                // wrap value back from range minimum
                                difference = abs(v - mapping->range.dest_max);
                                modulo_difference = difference - (int)(difference / total_range) * total_range;
                                v = mapping->range.dest_min + modulo_difference;
                                break;
                            default:
                                break;
                        }
                    }
                    break;
                case CT_WRAP:
                    // wrap value back from range maximum
                    difference = abs(v - mapping->range.dest_min);
                    modulo_difference = difference - (int)(difference / total_range) * total_range;
                    v = mapping->range.dest_max - modulo_difference;
                    break;
                default:
                    // leave the value unchanged
                    break;
            }
        }
        
        else if (v > mapping->range.dest_max) {
            switch (mapping->clip_upper) {
                case CT_MUTE:
                    // need to prevent value from being sent at all
                    muted = 1;
                    break;
                case CT_CLAMP:
                    // clamp value to range maximum
                    v = mapping->range.dest_max;
                    break;
                case CT_FOLD:
                    // fold value around range maximum
                    difference = abs(v - mapping->range.dest_max);
                    v = mapping->range.dest_max - difference;
                    if (v < mapping->range.dest_min) {
                        // value now exceeds range minimum!
                        switch (mapping->clip_lower) {
                            case CT_MUTE:
                                // need to prevent value from being sent at all
                                muted = 1;
                                break;
                            case CT_CLAMP:
                                // clamp value to range minimum
                                v = mapping->range.dest_min;
                                break;
                            case CT_FOLD:
                                // both clip modes are set to fold!
                                difference = abs(v - mapping->range.dest_min);
                                modulo_difference = difference - (int)(difference / total_range) * total_range;
                                if ((int)(difference / total_range) % 2 == 0) {
                                    v = mapping->range.dest_max + modulo_difference;
                                }
                                else
                                    v = mapping->range.dest_min - modulo_difference;
                                break;
                            case CT_WRAP:
                                // wrap value back from range maximum
                                difference = abs(v - mapping->range.dest_min);
                                modulo_difference = difference - (int)(difference / total_range) * total_range;
                                v = mapping->range.dest_max - modulo_difference;
                                break;
                            default:
                                break;
                        }
                    }
                    break;
                case CT_WRAP:
                    // wrap value back from range minimum
                    difference = abs(v - mapping->range.dest_max);
                    modulo_difference = difference - (int)(difference / total_range) * total_range;
                    v = mapping->range.dest_min + modulo_difference;
                    break;
                default:
                    break;
            }
        }
    }
    to_value->f = v;
    if (muted) {
        return 0;
    }
    else {
        return 1;
    }
    
}

void mapper_mapping_set_direct(mapper_mapping m)
{
    m->scaling = SC_BYPASS;

    if (m->expression)
        free(m->expression);

    m->expression = strdup("y=x");

    // TODO send /modify
}

void mapper_mapping_set_linear_range(mapper_mapping m,
                                     float src_min, float src_max,
                                     float dest_min, float dest_max)
{
    m->scaling = SC_LINEAR;

    float scale = (dest_min - dest_max) / (src_min - src_max);
    float offset =
        (dest_max * src_min - dest_min * src_max) / (src_min - src_max);

    if (m->expression)
        free(m->expression);

    char expr[256];
    snprintf(expr, 256, "y=x*(%g)+(%g)", scale, offset);
    m->expression = strdup(expr);

    m->range.src_min = src_min;
    m->range.src_max = src_max;
    m->range.dest_min = dest_min;
    m->range.dest_max = dest_max;
    m->range.known = RANGE_KNOWN;

    Tree *T = NewTree();

    int success_tree = get_expr_Tree(T, m->expression);
    if (!success_tree)
        return;

    m->expr_tree = T;

    // TODO send /modify
}

void mapper_mapping_set_expression(mapper_mapping m,
                                   const char *expr)
{
    Tree *T = NewTree();
    if (get_expr_Tree(T, expr))
    {
        m->scaling = SC_EXPRESSION;

        if (m->expr_tree)
            DeleteTree(m->expr_tree);
        m->expr_tree = T;

        if (m->expression)
            free(m->expression);
        m->expression = strdup(expr);
    }
    else
        DeleteTree(T);

    // TODO send /modify
}

void mapper_mapping_set_calibrate(mapper_mapping m,
                                  float dest_min, float dest_max)
{
    m->scaling = SC_CALIBRATE;

    if (m->expression)
        free(m->expression);

    char expr[256];
    snprintf(expr, 256, "y=%g", dest_min);
    m->expression = strdup(expr);

    m->range.dest_min = dest_min;
    m->range.dest_max = dest_max;
    m->range.known |= RANGE_DEST_MIN | RANGE_DEST_MAX;
    m->range.rewrite = 1;

    // TODO send /modify
}

/* Helper to fill in the range (src_min, src_max, dest_min, dest_max)
 * based on message parameters and known mapping and signal
 * properties; return flags to indicate which parts of the range were
 * found. */
static int get_range(mapper_signal sig, mapper_mapping mapping,
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
     * priority is already-known properties of the mapping.  Lastly,
     * we fill in source range from the signal. */

    /* @range */

    if (a_range && t_range) {
        int i, known[] = { RANGE_SRC_MIN, RANGE_SRC_MAX,
                           RANGE_DEST_MIN, RANGE_DEST_MAX };
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

    if (!(range_known & RANGE_DEST_MIN)
        && a_min && t_min)
    {
        if (t_min[0]=='f') {
            range_known |= RANGE_DEST_MIN;
            range[2] = (*a_min)->f;
        } else if (t_min[0]=='i') {
            range_known |= RANGE_DEST_MIN;
            range[2] = (float)(*a_min)->i;
        }
    }

    if (!(range_known & RANGE_DEST_MAX)
        && a_max && t_max)
    {
        if (t_max[0]=='f') {
            range_known |= RANGE_DEST_MAX;
            range[3] = (*a_max)->f;
        } else if (t_max[0]=='i') {
            range_known |= RANGE_DEST_MAX;
            range[3] = (float)(*a_max)->i;
        }
    }

    /* Mapping */

    if (mapping) {
        if (!(range_known & RANGE_SRC_MIN)
            && (mapping->range.known & RANGE_SRC_MIN))
        {
            range_known |= RANGE_SRC_MIN;
            range[0] = mapping->range.src_min;
        }

        if (!(range_known & RANGE_SRC_MAX)
            && (mapping->range.known & RANGE_SRC_MAX))
        {
            range_known |= RANGE_SRC_MAX;
            range[1] = mapping->range.src_max;
        }

        if (!(range_known & RANGE_DEST_MIN)
            && (mapping->range.known & RANGE_DEST_MIN))
        {
            range_known |= RANGE_DEST_MIN;
            range[2] = mapping->range.dest_min;
        }

        if (!(range_known & RANGE_DEST_MAX)
            && (mapping->range.known & RANGE_DEST_MAX))
        {
            range_known |= RANGE_DEST_MAX;
            range[3] = mapping->range.dest_max;
        }
    }

    /* Signal */

    if (sig) {
        if (!(range_known & RANGE_SRC_MIN)
            && sig->minimum)
        {
            if (sig->type == 'f') {
                range_known |= RANGE_SRC_MIN;
                range[0] = sig->minimum->f;
            } else if (sig->type == 'i') {
                range_known |= RANGE_SRC_MIN;
                range[0] = sig->minimum->i32;
            }
        }

        if (!(range_known & RANGE_SRC_MAX)
            && sig->maximum)
        {
            if (sig->type == 'f') {
                range_known |= RANGE_SRC_MAX;
                range[1] = sig->maximum->f;
            } else if (sig->type == 'i') {
                range_known |= RANGE_SRC_MAX;
                range[1] = sig->maximum->i32;
            }
        }
    }

    return range_known;
}

void mapper_mapping_set_from_message(mapper_mapping m,
                                     mapper_signal sig,
                                     mapper_message_t *msg)
{
    /* First record any provided parameters. */

    /* Destination type. */

    const char *dest_type = mapper_msg_get_param_if_char(msg, AT_TYPE);
    if (dest_type)
        m->dest_type = dest_type[0];

    /* Range information. */

    float range[4];
    int range_known = get_range(sig, m, msg, range);

    if (range_known & RANGE_SRC_MIN) {
        m->range.known |= RANGE_SRC_MIN;
        m->range.src_min = range[0];
    }

    if (range_known & RANGE_SRC_MAX) {
        m->range.known |= RANGE_SRC_MAX;
        m->range.src_max = range[0];
    }

    if (range_known & RANGE_DEST_MIN) {
        m->range.known |= RANGE_DEST_MIN;
        m->range.dest_min = range[0];
    }

    if (range_known & RANGE_DEST_MAX) {
        m->range.known |= RANGE_DEST_MAX;
        m->range.dest_max = range[0];
    }

    /* Clipping. */

    int clip_min = mapper_msg_get_clipping(msg, AT_CLIPMIN);
    if (clip_min >= 0)
        m->clip_lower = clip_min;

    int clip_max = mapper_msg_get_clipping(msg, AT_CLIPMAX);
    if (clip_max >= 0)
        m->clip_upper = clip_max;

    /* Now set the scaling type depending on the requested type and
     * the known properties. */

    int scaling = mapper_msg_get_scaling(msg);

    switch (scaling)
    {
    case -1:
        /* No scaling type specified; see if we know the range and
           choose between linear or direct mapping. */
        if (range_known == RANGE_KNOWN)
            /* We have enough information for a linear mapping. */
            mapper_mapping_set_linear_range(
                m, range[0], range[1], range[2], range[3]);
        else
            /* No range, default to direct mapping. */
            mapper_mapping_set_direct(m);
        break;
    case SC_BYPASS:
        mapper_mapping_set_direct(m);
        break;
    case SC_LINEAR:
        if (range_known == RANGE_KNOWN)
            mapper_mapping_set_linear_range(
                m, range[0], range[1], range[2], range[3]);
        break;
    case SC_CALIBRATE:
        if (range_known & (RANGE_DEST_MIN | RANGE_DEST_MAX))
            mapper_mapping_set_calibrate(m, range[2], range[3]);
        break;
    case SC_EXPRESSION:
        {
            const char *expr =
                mapper_msg_get_param_if_string(msg, AT_EXPRESSION);
            if (expr)
                mapper_mapping_set_expression(m, expr);
        }
        break;
    default:
        trace("unknown result from mapper_msg_get_scaling()\n");
        break;
    }
}
