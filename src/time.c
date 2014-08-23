
#include "config.h"

#include <lo/lo.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <sys/time.h>

#include "mapper_internal.h"
#include "types_internal.h"
#include <mapper/mapper.h>

static double multiplier = 1.0/((double)(1LL<<32));

void mapper_clock_init(mapper_clock clock)
{
    mapper_clock_now(clock, &clock->now);
    clock->next_ping = clock->now.sec;
}

void mapper_clock_now(mapper_clock clock,
                      mapper_timetag_t *timetag)
{
    lo_timetag_now((lo_timetag*)timetag);
}

double mapper_timetag_difference(mapper_timetag_t a, mapper_timetag_t b)
{
    return (double)a.sec - (double)b.sec +
        ((double)a.frac - (double)b.frac) * multiplier;
}

void mapper_timetag_add_seconds(mapper_timetag_t *a, double b)
{
    if (!b)
        return;

    b += (double)a->frac * multiplier;
    a->sec += floor(b);
    b -= floor(b);
    if (b < 0.0) {
        a->sec--;
        b = 1.0 - b;
    }
    a->frac = (uint32_t) (((double)b) * (double)(1LL<<32));
}

double mapper_timetag_get_double(mapper_timetag_t timetag)
{
    return (double)timetag.sec + (double)timetag.frac * multiplier;
}

void mapper_timetag_set_int(mapper_timetag_t *tt, int value)
{
    tt->sec = value;
    tt->frac = 0;
}

void mapper_timetag_set_float(mapper_timetag_t *tt, float value)
{
    tt->sec = floor(value);
    value -= tt->sec;
    tt->frac = (uint32_t) (((float)value) * (double)(1LL<<32));
}

void mapper_timetag_set_double(mapper_timetag_t *tt, double value)
{
    tt->sec = floor(value);
    value -= tt->sec;
    tt->frac = (uint32_t) (((double)value) * (double)(1LL<<32));
}

void mapper_timetag_cpy(mapper_timetag_t *ttl, mapper_timetag_t ttr)
{
    ttl->sec = ttr.sec;
    ttl->frac = ttr.frac;
}
