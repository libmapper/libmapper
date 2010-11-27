
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <assert.h>

#include "mapper_internal.h"
#include "types_internal.h"
#include <mapper/mapper.h>

mapper_signal msig_new(const char *name, int length, char type,
                       int is_output, const char *unit,
                       void *minimum, void *maximum,
                       mapper_signal_handler *handler, void *user_data)
{
    if (length < 1) return 0;
    if (!name) return 0;
    if (type != 'f' && type != 'i')
        return 0;

    mapper_signal sig =
        (mapper_signal) calloc(1, sizeof(struct _mapper_signal));

    mapper_db_signal_init(&sig->props, is_output, type, length, name, unit);

    sig->handler = handler;
    sig->user_data = user_data;
    msig_set_minimum(sig, minimum);
    msig_set_maximum(sig, maximum);
    return sig;
}

mapper_db_signal msig_get_properties(mapper_signal sig)
{
    return &sig->props;
}

void msig_set_property(mapper_signal sig, const char *property,
                       char type, lo_arg *value)
{
    mapper_table_add_or_update_osc_value(sig->props.extra,
                                         property, type, value);
}

void msig_remove_property(mapper_signal sig, const char *property)
{
    table_remove_key(sig->props.extra, property, 1);
}

void msig_set_minimum(mapper_signal sig, mapper_signal_value_t *minimum)
{
    if (minimum) {
        if (!sig->props.minimum)
            sig->props.minimum = (mapper_signal_value_t *)
                malloc(sizeof(mapper_signal_value_t));
        *sig->props.minimum = *minimum;
    }
    else {
        if (sig->props.minimum)
            free(sig->props.minimum);
        sig->props.minimum = 0;
    }
}

void msig_set_maximum(mapper_signal sig, mapper_signal_value_t *maximum)
{
    if (maximum) {
        if (!sig->props.maximum)
            sig->props.maximum = (mapper_signal_value_t *)
                malloc(sizeof(mapper_signal_value_t));
        *sig->props.maximum = *maximum;
    }
    else {
        if (sig->props.maximum)
            free(sig->props.maximum);
        sig->props.maximum = 0;
    }
}

void msig_free(mapper_signal sig)
{
    if (!sig) return;
    if (sig->props.minimum)
        free(sig->props.minimum);
    if (sig->props.maximum)
        free(sig->props.maximum);
    if (sig->props.name)
        free((char*)sig->props.name);
    if (sig->props.unit)
        free((char*)sig->props.unit);
    if (sig->props.extra)
        table_free(sig->props.extra, 1);
    free(sig);
}

void msig_update_int(mapper_signal sig, int value)
{
    if (sig->device)
        mdev_route_signal(sig->device, sig, MSIGVALP(&value));
}

void msig_update_float(mapper_signal sig, float value)
{
    if (sig->device)
        mdev_route_signal(sig->device, sig, MSIGVALP(&value));
}

void msig_update(mapper_signal sig, void *value)
{
    if (sig->device)
        mdev_route_signal(sig->device, sig, MSIGVALP(value));
}

void mval_add_to_message(lo_message m, mapper_signal sig,
                         mapper_signal_value_t *value)
{
    switch (sig->props.type) {
    case 'f':
        lo_message_add_float(m, value->f);
        break;
    case 'd':
        lo_message_add_double(m, value->d);
        break;
    case 'i':
        lo_message_add_int32(m, value->i32);
        break;
    default:
        // Unknown signal type
        assert(0);
        break;
    }
}

int msig_full_name(mapper_signal sig, char *name, int len)
{
    const char *mdname = mdev_name(sig->device);
    if (!mdname)
        return 0;

    int mdlen = strlen(mdname);
    if (mdlen >= len)
        return 0;
    if ((mdlen + strlen(sig->props.name)) > len)
        return 0;

    strncpy(name, mdname, len);
    strncat(name, sig->props.name, len);
    return strlen(name);
}
