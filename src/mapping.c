
#include <string.h>

#include "mapper_internal.h"
#include "types_internal.h"
#include <mapper/mapper.h>
#include "operations.h"
#include "expression.h"

const char* mapper_clipping_type_strings[] =
{
    "none",        /* CT_NONE */
    "mute",        /* CT_MUTE */
    "clamp",       /* CT_CLAMP */
    "fold",        /* CT_FOLD */
    "wrap",        /* CT_WRAP */
};

const char* mapper_scaling_type_strings[] =
{
    NULL,          /* SC_UNDEFINED */
    "bypass",      /* SC_BYPASS */
    "linear",      /* SC_LINEAR */
    "expression",  /* SC_EXPRESSION */
    "calibrate",   /* SC_CALIBRATE */
    "mute",        /* SC_MUTE */
};

const char *mapper_get_clipping_type_string(mapper_clipping_type clipping)
{
    die_unless(clipping < N_MAPPER_CLIPPING_TYPES && clipping >= 0,
               "called mapper_get_clipping_type_string() with "
               "bad parameter.\n");

    return mapper_clipping_type_strings[clipping];
}

const char *mapper_get_scaling_type_string(mapper_scaling_type scaling)
{
    die_unless(scaling < N_MAPPER_SCALING_TYPES && scaling >= 0,
               "called mapper_get_scaling_type_string() with "
               "bad parameter.\n");

    return mapper_scaling_type_strings[scaling];
}

int mapper_mapping_perform(mapper_mapping mapping,
                            mapper_signal_value_t *from_value,
                            mapper_signal_value_t *to_value)
{
    int p, changed = 0;
    float v;
    mapper_expr_error err = NO_ERR;

    p = mapping->history_pos;
    mapping->history_input[p] = from_value->f;
    v = mapping->history_input[p];
    
    if (mapping->props.muted) {
        return 0;
    }

    if (!mapping->props.scaling || mapping->props.scaling == SC_BYPASS /*|| mapping->type==LINEAR */ ) {
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


    else if (mapping->props.scaling == SC_EXPRESSION
             || mapping->props.scaling == SC_LINEAR) {
        die_unless(mapping->expr_tree!=0, "Missing expression.\n");
        v = mapper_expr_eval(mapping->expr_tree, mapping->history_input,
                             mapping->history_output, p, &err);
        mapping->history_output[p] = v;

        --p;
        if (p < 0)
            p = MAX_HISTORY_ORDER - 1;
    }

    else if (mapping->props.scaling == SC_CALIBRATE) {
        /* If calibration mode has just taken effect, first data
         * sample sets source min and max */
        if (mapping->calibrating) {
            mapping->props.range.src_min = from_value->f;
            mapping->props.range.src_max = from_value->f;
            mapping->props.range.known |=
                MAPPING_RANGE_SRC_MIN | MAPPING_RANGE_SRC_MAX;
            mapping->calibrating = 0;
            changed = 1;
        } else {
            if (from_value->f < mapping->props.range.src_min) {
                mapping->props.range.src_min = from_value->f;
                mapping->props.range.known |= MAPPING_RANGE_SRC_MIN;
                changed = 1;
            }
            if (from_value->f > mapping->props.range.src_max) {
                mapping->props.range.src_max = from_value->f;
                mapping->props.range.known |= MAPPING_RANGE_SRC_MAX;
                changed = 1;
            }
        }

        if (changed) {
            mapper_mapping_set_linear_range(mapping,
                                            &mapping->props.range);

            /* Stay in calibrate mode. */
            if (mapping->calibrating)
                mapping->props.scaling = SC_CALIBRATE;
        }

        if (mapping->expr_tree)
            v = mapper_expr_eval(mapping->expr_tree, mapping->history_input,
                                 mapping->history_output, p, &err);
        mapping->history_output[p] = v;

        --p;
        if (p < 0)
            p = MAX_HISTORY_ORDER - 1;
    }


    mapping->history_pos = p;
    to_value->f = v;
    return 1;
}

int mapper_clipping_perform(mapper_mapping mapping,
                            mapper_signal_value_t *from_value,
                            mapper_signal_value_t *to_value)
{
    int muted = 0;
    float v = from_value->f, total_range = abs(mapping->props.range.dest_max - mapping->props.range.dest_min), difference, modulo_difference;
    
    if (mapping->props.range.known) {
        if (v < mapping->props.range.dest_min) {
            switch (mapping->props.clip_lower) {
                case CT_MUTE:
                    // need to prevent value from being sent at all
                    muted = 1;
                    break;
                case CT_CLAMP:
                    // clamp value to range minimum
                    v = mapping->props.range.dest_min;
                    break;
                case CT_FOLD:
                    // fold value around range minimum
                    difference = abs(v - mapping->props.range.dest_min);
                    v = mapping->props.range.dest_min + difference;
                    if (v > mapping->props.range.dest_max) {
                        // value now exceeds range maximum!
                        switch (mapping->props.clip_upper) {
                            case CT_MUTE:
                                // need to prevent value from being sent at all
                                muted = 1;
                                break;
                            case CT_CLAMP:
                                // clamp value to range minimum
                                v = mapping->props.range.dest_max;
                                break;
                            case CT_FOLD:
                                // both clip modes are set to fold!
                                difference = abs(v - mapping->props.range.dest_max);
                                modulo_difference = difference - (int)(difference / total_range) * total_range;
                                if ((int)(difference / total_range) % 2 == 0) {
                                    v = mapping->props.range.dest_max - modulo_difference;
                                }
                                else
                                    v = mapping->props.range.dest_min + modulo_difference;
                                break;
                            case CT_WRAP:
                                // wrap value back from range minimum
                                difference = abs(v - mapping->props.range.dest_max);
                                modulo_difference = difference - (int)(difference / total_range) * total_range;
                                v = mapping->props.range.dest_min + modulo_difference;
                                break;
                            default:
                                break;
                        }
                    }
                    break;
                case CT_WRAP:
                    // wrap value back from range maximum
                    difference = abs(v - mapping->props.range.dest_min);
                    modulo_difference = difference - (int)(difference / total_range) * total_range;
                    v = mapping->props.range.dest_max - modulo_difference;
                    break;
                default:
                    // leave the value unchanged
                    break;
            }
        }
        
        else if (v > mapping->props.range.dest_max) {
            switch (mapping->props.clip_upper) {
                case CT_MUTE:
                    // need to prevent value from being sent at all
                    muted = 1;
                    break;
                case CT_CLAMP:
                    // clamp value to range maximum
                    v = mapping->props.range.dest_max;
                    break;
                case CT_FOLD:
                    // fold value around range maximum
                    difference = abs(v - mapping->props.range.dest_max);
                    v = mapping->props.range.dest_max - difference;
                    if (v < mapping->props.range.dest_min) {
                        // value now exceeds range minimum!
                        switch (mapping->props.clip_lower) {
                            case CT_MUTE:
                                // need to prevent value from being sent at all
                                muted = 1;
                                break;
                            case CT_CLAMP:
                                // clamp value to range minimum
                                v = mapping->props.range.dest_min;
                                break;
                            case CT_FOLD:
                                // both clip modes are set to fold!
                                difference = abs(v - mapping->props.range.dest_min);
                                modulo_difference = difference - (int)(difference / total_range) * total_range;
                                if ((int)(difference / total_range) % 2 == 0) {
                                    v = mapping->props.range.dest_max + modulo_difference;
                                }
                                else
                                    v = mapping->props.range.dest_min - modulo_difference;
                                break;
                            case CT_WRAP:
                                // wrap value back from range maximum
                                difference = abs(v - mapping->props.range.dest_min);
                                modulo_difference = difference - (int)(difference / total_range) * total_range;
                                v = mapping->props.range.dest_max - modulo_difference;
                                break;
                            default:
                                break;
                        }
                    }
                    break;
                case CT_WRAP:
                    // wrap value back from range minimum
                    difference = abs(v - mapping->props.range.dest_max);
                    modulo_difference = difference - (int)(difference / total_range) * total_range;
                    v = mapping->props.range.dest_min + modulo_difference;
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
    m->props.scaling = SC_BYPASS;

    // TODO send /modify
}

void mapper_mapping_set_linear_range(mapper_mapping m,
                                     mapper_mapping_range_t *r)
{
    m->props.scaling = SC_LINEAR;

    char expr[256] = "";
    const char *e = expr;

    if (r->known
        & (MAPPING_RANGE_SRC_MIN | MAPPING_RANGE_SRC_MAX))
    {
        if (r->src_min == r->src_max)
            snprintf(expr, 256, "y=%g", r->src_min);

        else if (r->known == MAPPING_RANGE_KNOWN
                 && r->src_min == r->dest_min
                 && r->src_max == r->dest_max)
            e = strdup("y=x");

        else if (r->known == MAPPING_RANGE_KNOWN) {
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

    if (&m->props.range != r)
        memcpy(&m->props.range, r,
               sizeof(mapper_mapping_range_t));

    // If everything is successful, replace the mapping's expression.
    if (e)
    {
        mapper_expr_tree T = mapper_expr_new();
        if (!T)
            return;
        int success_tree = mapper_expr_create_from_string(T, e);
        if (success_tree)
        {
            if (m->props.expression)
                free(m->props.expression);
            m->props.expression = strdup(e);

            if (m->expr_tree)
                mapper_expr_free(m->expr_tree);
            m->expr_tree = T;
        }
        else
            mapper_expr_free(T);
    }

    // TODO send /modify
}

void mapper_mapping_set_expression(mapper_mapping m,
                                   const char *expr)
{
    mapper_expr_tree T = mapper_expr_new();
    if (expr)
    {
        if (mapper_expr_create_from_string(T, expr))
        {
            if (m->expr_tree)
                mapper_expr_free(m->expr_tree);
            m->expr_tree = T;

            if (m->props.expression)
                free(m->props.expression);
            m->props.expression = strdup(expr);
        }
        else
            mapper_expr_free(T);
    }
    else {
        if (m->props.expression
            && mapper_expr_create_from_string(T, m->props.expression))
        {
            // In this case it is possible that expr_tree exists and is correct
            // Rebuild it anyway?
            if (m->expr_tree)
                mapper_expr_free(m->expr_tree);
            m->expr_tree = T;
        }
        else
            mapper_expr_free(T);
    }


    // TODO send /modify
}

void mapper_mapping_set_calibrate(mapper_mapping m,
                                  float dest_min, float dest_max)
{
    m->props.scaling = SC_CALIBRATE;

    if (m->props.expression)
        free(m->props.expression);

    char expr[256];
    snprintf(expr, 256, "y=%g", dest_min);
    m->props.expression = strdup(expr);

    m->props.range.dest_min = dest_min;
    m->props.range.dest_max = dest_max;
    m->props.range.known |= MAPPING_RANGE_DEST_MIN | MAPPING_RANGE_DEST_MAX;
    m->calibrating = 1;

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
        int i, known[] = { MAPPING_RANGE_SRC_MIN, MAPPING_RANGE_SRC_MAX,
                           MAPPING_RANGE_DEST_MIN, MAPPING_RANGE_DEST_MAX };
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

    if (!(range_known & MAPPING_RANGE_DEST_MIN)
        && a_min && t_min)
    {
        if (t_min[0]=='f') {
            range_known |= MAPPING_RANGE_DEST_MIN;
            range[2] = (*a_min)->f;
        } else if (t_min[0]=='i') {
            range_known |= MAPPING_RANGE_DEST_MIN;
            range[2] = (float)(*a_min)->i;
        }
    }

    if (!(range_known & MAPPING_RANGE_DEST_MAX)
        && a_max && t_max)
    {
        if (t_max[0]=='f') {
            range_known |= MAPPING_RANGE_DEST_MAX;
            range[3] = (*a_max)->f;
        } else if (t_max[0]=='i') {
            range_known |= MAPPING_RANGE_DEST_MAX;
            range[3] = (float)(*a_max)->i;
        }
    }

    /* Mapping */

    if (mapping) {
        if (!(range_known & MAPPING_RANGE_SRC_MIN)
            && (mapping->props.range.known & MAPPING_RANGE_SRC_MIN))
        {
            range_known |= MAPPING_RANGE_SRC_MIN;
            range[0] = mapping->props.range.src_min;
        }

        if (!(range_known & MAPPING_RANGE_SRC_MAX)
            && (mapping->props.range.known & MAPPING_RANGE_SRC_MAX))
        {
            range_known |= MAPPING_RANGE_SRC_MAX;
            range[1] = mapping->props.range.src_max;
        }

        if (!(range_known & MAPPING_RANGE_DEST_MIN)
            && (mapping->props.range.known & MAPPING_RANGE_DEST_MIN))
        {
            range_known |= MAPPING_RANGE_DEST_MIN;
            range[2] = mapping->props.range.dest_min;
        }

        if (!(range_known & MAPPING_RANGE_DEST_MAX)
            && (mapping->props.range.known & MAPPING_RANGE_DEST_MAX))
        {
            range_known |= MAPPING_RANGE_DEST_MAX;
            range[3] = mapping->props.range.dest_max;
        }
    }

    /* Signal */

    if (sig) {
        if (!(range_known & MAPPING_RANGE_SRC_MIN)
            && sig->props.minimum)
        {
            if (sig->props.type == 'f') {
                range_known |= MAPPING_RANGE_SRC_MIN;
                range[0] = sig->props.minimum->f;
            } else if (sig->props.type == 'i') {
                range_known |= MAPPING_RANGE_SRC_MIN;
                range[0] = sig->props.minimum->i32;
            }
        }

        if (!(range_known & MAPPING_RANGE_SRC_MAX)
            && sig->props.maximum)
        {
            if (sig->props.type == 'f') {
                range_known |= MAPPING_RANGE_SRC_MAX;
                range[1] = sig->props.maximum->f;
            } else if (sig->props.type == 'i') {
                range_known |= MAPPING_RANGE_SRC_MAX;
                range[1] = sig->props.maximum->i32;
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
        m->props.dest_type = dest_type[0];

    /* Range information. */

    float range[4];
    int range_known = get_range(sig, m, msg, range);

    if (range_known & MAPPING_RANGE_SRC_MIN) {
        m->props.range.known |= MAPPING_RANGE_SRC_MIN;
        m->props.range.src_min = range[0];
    }

    if (range_known & MAPPING_RANGE_SRC_MAX) {
        m->props.range.known |= MAPPING_RANGE_SRC_MAX;
        m->props.range.src_max = range[1];
    }

    if (range_known & MAPPING_RANGE_DEST_MIN) {
        m->props.range.known |= MAPPING_RANGE_DEST_MIN;
        m->props.range.dest_min = range[2];
    }

    if (range_known & MAPPING_RANGE_DEST_MAX) {
        m->props.range.known |= MAPPING_RANGE_DEST_MAX;
        m->props.range.dest_max = range[3];
    }

    /* Clipping. */

    int clip_min = mapper_msg_get_clipping(msg, AT_CLIPMIN);
    if (clip_min >= 0)
        m->props.clip_lower = clip_min;

    int clip_max = mapper_msg_get_clipping(msg, AT_CLIPMAX);
    if (clip_max >= 0)
        m->props.clip_upper = clip_max;
    
    /* Expression. */
    const char *expr = mapper_msg_get_param_if_string(msg, AT_EXPRESSION);
    mapper_mapping_set_expression(m, expr);

    /* Now set the scaling type depending on the requested type and
     * the known properties. */

    int scaling = mapper_msg_get_scaling(msg);

    switch (scaling)
    {
    case -1:
        /* No scaling type specified; if scaling not yet set, see if 
         we know the range and choose between linear or direct mapping. */
            if (m->props.scaling == SC_UNDEFINED) {
                if (range_known == MAPPING_RANGE_KNOWN) {
                    /* We have enough information for a linear mapping. */
                    mapper_mapping_range_t r;
                    r.src_min = range[0];
                    r.src_max = range[1];
                    r.dest_min = range[2];
                    r.dest_max = range[3];
                    r.known = range_known;
                    mapper_mapping_set_linear_range(m, &r);
                } else
                    /* No range, default to direct mapping. */
                    mapper_mapping_set_direct(m);
            }
        break;
    case SC_BYPASS:
        mapper_mapping_set_direct(m);
        break;
    case SC_LINEAR:
        if (range_known == MAPPING_RANGE_KNOWN) {
            mapper_mapping_range_t r;
            r.src_min = range[0];
            r.src_max = range[1];
            r.dest_min = range[2];
            r.dest_max = range[3];
            r.known = range_known;
            mapper_mapping_set_linear_range(m, &r);
        }
        break;
    case SC_CALIBRATE:
        if (range_known & (MAPPING_RANGE_DEST_MIN
                           | MAPPING_RANGE_DEST_MAX))
            mapper_mapping_set_calibrate(m, range[2], range[3]);
        break;
    case SC_EXPRESSION:
        {
            if (m->props.expression)
                m->props.scaling = SC_EXPRESSION;
        }
        break;
    default:
        trace("unknown result from mapper_msg_get_scaling()\n");
        break;
    }
}

mapper_mapping mapper_mapping_find_by_names(mapper_device md, 
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
                if (strncmp(router->dest_name, dest_name, n) == 0) {
                    // find associated mapping
                    mapper_signal_mapping sm = router->mappings;
                    while (sm && sm->signal != md->outputs[i])
                        sm = sm->next;
                    if (!sm)
                        return NULL;
                    mapper_mapping m = sm->mapping;
                    while (m && strcmp(m->props.dest_name, (dest_name + n)) != 0)
                        m = m->next;
                    if (!m)
                        return NULL;
                    else
                        return m;
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