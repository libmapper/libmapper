
#include <string.h>
#include <math.h>
#include <stdlib.h>

#include "mapper_internal.h"
#include "types_internal.h"
#include <mapper/mapper.h>

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
    NULL,          /* SC_UNDEFINED */
    "bypass",      /* SC_BYPASS */
    "linear",      /* SC_LINEAR */
    "expression",  /* SC_EXPRESSION */
    "calibrate",   /* SC_CALIBRATE */
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

int mapper_mapping_perform(mapper_mapping mapping,
                           mapper_signal sig,
                           mapper_signal_value_t *from_value,
                           mapper_signal_value_t *to_value)
{
    int changed = 0;
    
    if (mapping->props.muted)
        return 0;

    if (!mapping->props.mode || mapping->props.mode == SC_BYPASS)
        *to_value = *from_value;

    else if (mapping->props.mode == SC_EXPRESSION
             || mapping->props.mode == SC_LINEAR)
    {
        die_unless(mapping->expr!=0, "Missing expression.\n");
        *to_value = mapper_expr_evaluate(mapping->expr, from_value);
    }

    else if (mapping->props.mode == SC_CALIBRATE)
    {
        /* If calibration mode has just taken effect, first data
         * sample sets source min and max */
        if (!mapping->calibrating) {
            mapping->props.range.src_min = from_value->f;
            mapping->props.range.src_max = from_value->f;
            mapping->props.range.known |=
                MAPPING_RANGE_SRC_MIN | MAPPING_RANGE_SRC_MAX;
            mapping->calibrating = 1;
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
            mapper_mapping_set_linear_range(mapping, sig,
                                            &mapping->props.range);

            /* Stay in calibrate mode. */
            mapping->props.mode = SC_CALIBRATE;
        }

        if (mapping->expr)
            *to_value = mapper_expr_evaluate(mapping->expr, from_value);
    }

    return 1;
}

int mapper_clipping_perform(mapper_mapping mapping,
                            mapper_signal_value_t *from_value,
                            mapper_signal_value_t *to_value)
{
    int muted = 0;
    // TODO: this doesn't check the value type, assumes float
    float v = from_value->f;
    float total_range = fabsf(mapping->props.range.dest_max
                              - mapping->props.range.dest_min);
    float difference, modulo_difference;
    
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
                    difference = fabsf(v - mapping->props.range.dest_min);
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
                                difference = fabsf(v - mapping->props.range.dest_max);
                                modulo_difference = difference - (int)(difference / total_range) * total_range;
                                if ((int)(difference / total_range) % 2 == 0) {
                                    v = mapping->props.range.dest_max - modulo_difference;
                                }
                                else
                                    v = mapping->props.range.dest_min + modulo_difference;
                                break;
                            case CT_WRAP:
                                // wrap value back from range minimum
                                difference = fabsf(v - mapping->props.range.dest_max);
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
                    difference = fabsf(v - mapping->props.range.dest_min);
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
                    difference = fabsf(v - mapping->props.range.dest_max);
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
                                difference = fabsf(v - mapping->props.range.dest_min);
                                modulo_difference = difference - (int)(difference / total_range) * total_range;
                                if ((int)(difference / total_range) % 2 == 0) {
                                    v = mapping->props.range.dest_max + modulo_difference;
                                }
                                else
                                    v = mapping->props.range.dest_min - modulo_difference;
                                break;
                            case CT_WRAP:
                                // wrap value back from range maximum
                                difference = fabsf(v - mapping->props.range.dest_min);
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
                    difference = fabsf(v - mapping->props.range.dest_max);
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

/* Helper to replace a mapping's expression only if the given string
 * parses successfully. Returns 0 on success, non-zero on error. */
static int replace_expression_string(mapper_mapping m,
                                     mapper_signal s,
                                     const char *expr_str)
{
    mapper_expr expr = mapper_expr_new_from_string(
        expr_str, s->props.type=='f', s->props.length);

    if (!expr)
        return 1;

    if (m->expr)
        mapper_expr_free(m->expr);

    m->expr = expr;
    int len = strlen(expr_str)+1;
    if (!m->props.expression || len > strlen(m->props.expression))
        m->props.expression = realloc(m->props.expression, len);
    strncpy(m->props.expression, expr_str, len);
    return 0;
}

void mapper_mapping_set_direct(mapper_mapping m)
{
    m->props.mode = SC_BYPASS;

    // TODO send /modify
}

void mapper_mapping_set_linear_range(mapper_mapping m,
                                     mapper_signal sig,
                                     mapper_mapping_range_t *r)
{
    m->props.mode = SC_LINEAR;

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
    if (e) replace_expression_string(m, sig, e);

    // TODO send /modify
}

void mapper_mapping_set_expression(mapper_mapping m,
                                   mapper_signal sig,
                                   const char *expr)
{
    if (replace_expression_string(m, sig, expr))
        return;

    m->props.mode = SC_EXPRESSION;

    // TODO send /modify
}

void mapper_mapping_set_calibrate(mapper_mapping m,
                                  mapper_signal sig,
                                  float dest_min, float dest_max)
{
    m->props.mode = SC_CALIBRATE;

    if (m->props.expression)
        free(m->props.expression);

    char expr[256];
    snprintf(expr, 256, "y=%g", dest_min);
    m->props.expression = strdup(expr);

    m->props.range.dest_min = dest_min;
    m->props.range.dest_max = dest_max;
    m->props.range.known |= MAPPING_RANGE_DEST_MIN | MAPPING_RANGE_DEST_MAX;
    m->calibrating = 0;

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
    
    // TO DO: test if range has actually changed
    if (m->props.range.known == MAPPING_RANGE_KNOWN) {
        mapper_mapping_set_linear_range(m, sig, &m->props.range);
    }

    /* Muting. */
    int muting;
    if (!mapper_msg_get_param_if_int(msg, AT_MUTE, &muting))
        m->props.muted = muting;
    
    /* Clipping. */
    int clip_min = mapper_msg_get_clipping(msg, AT_CLIPMIN);
    if (clip_min >= 0)
        m->props.clip_lower = clip_min;

    int clip_max = mapper_msg_get_clipping(msg, AT_CLIPMAX);
    if (clip_max >= 0)
        m->props.clip_upper = clip_max;
    
    /* Expression. */
    const char *expr = mapper_msg_get_param_if_string(msg, AT_EXPRESSION);
    if (expr)
        replace_expression_string(m, sig, expr);

    /* Now set the mode type depending on the requested type and
     * the known properties. */

    int mode = mapper_msg_get_mode(msg);

    switch (mode)
    {
    case -1:
        /* No mode type specified; if mode not yet set, see if 
         we know the range and choose between linear or direct mapping. */
            if (m->props.mode == SC_UNDEFINED) {
                if (range_known == MAPPING_RANGE_KNOWN) {
                    /* We have enough information for a linear mapping. */
                    mapper_mapping_range_t r;
                    r.src_min = range[0];
                    r.src_max = range[1];
                    r.dest_min = range[2];
                    r.dest_max = range[3];
                    r.known = range_known;
                    mapper_mapping_set_linear_range(m, sig, &r);
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
            mapper_mapping_set_linear_range(m, sig, &r);
        }
        break;
    case SC_CALIBRATE:
        if (range_known & (MAPPING_RANGE_DEST_MIN
                           | MAPPING_RANGE_DEST_MAX))
            mapper_mapping_set_calibrate(m, sig, range[2], range[3]);
        break;
    case SC_EXPRESSION:
        {
            if (m->props.expression)
                m->props.mode = SC_EXPRESSION;
        }
        break;
    default:
        trace("unknown result from mapper_msg_get_mode()\n");
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
