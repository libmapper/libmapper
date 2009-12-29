
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <assert.h>

#include "mapper_internal.h"
#include "types_internal.h"
#include <mapper/mapper.h>

mapper_signal msig_float(int length, const char *name,
                         const char *unit, float minimum,
                         float maximum, float *value,
                         mapper_signal_handler *handler,
                         void *user_data)
{
    mapper_signal sig =
        (mapper_signal)calloc(1, sizeof(struct _mapper_signal));
    sig->type = 'f';
    sig->length = length;
    assert(length >= 1);
    assert(name!=0);
    sig->name = strdup(name);
    if (unit)
        sig->unit = strdup(unit);
    sig->value = (mapper_signal_value_t*)value;

    if (minimum != INFINITY
        && minimum != -INFINITY)
    {
        sig->minimum = (mapper_signal_value_t*)
            malloc(sizeof(mapper_signal_value_t));
        sig->minimum->f = minimum;
    }

    if (maximum != INFINITY
        && maximum != -INFINITY)
    {
        sig->maximum = malloc(sizeof(mapper_signal_value_t));
        sig->maximum->f = maximum;
    }

    sig->handler = handler;
    sig->user_data = user_data;
    return sig;
}

void msig_update_scalar(mapper_signal sig, mapper_signal_value_t value)
{
    mdev_route_signal(sig->device, sig, &value);
}

void msig_update(mapper_signal sig, mapper_signal_value_t *value)
{
    mdev_route_signal(sig->device, sig, value);
}
