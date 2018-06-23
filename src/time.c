
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

/*! Internal function to get the current time. */
double mapper_get_current_time()
{
#ifdef HAVE_GETTIMEOFDAY
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (double)tv.tv_sec + tv.tv_usec / 1000000.0;
#else
#error No timing method known on this platform.
#endif
}

void mapper_time_now(mapper_time_t *t)
{
    lo_timetag_now((lo_timetag*)t);
}

double mapper_time_difference(const mapper_time_t l, const mapper_time_t r)
{
    return ((double)l.sec - (double)r.sec
            + ((double)l.frac - (double)r.frac) * multiplier);
}

void mapper_time_add_double(mapper_time_t *t, double d)
{
    if (!d)
        return;

    d += (double)t->frac * multiplier;
    if (d < 0 && floor(d) > t->sec) {
        t->sec = 0;
        t->frac = 0;
    }
    else {
        t->sec += floor(d);
        d -= floor(d);
        if (d < 0.0) {
            --t->sec;
            d = 1.0 - d;
        }
        t->frac = (uint32_t) (((double)d) * (double)(1LL<<32));
    }
}

void mapper_time_multiply(mapper_time_t *t, double d)
{
    if (d > 0.) {
        d *= mapper_time_get_double(*t);
        t->sec = floor(d);
        d -= t->sec;
        t->frac = (uint32_t) (d * (double)(1LL<<32));
    }
    else {
        t->sec = 0;
        t->frac = 0;
    }
}

void mapper_time_add(mapper_time_t *t, mapper_time_t addend)
{
    t->sec += addend.sec;
    t->frac += addend.frac;
    if (t->frac < addend.frac) // overflow
        ++t->sec;
}

void mapper_time_subtract(mapper_time_t *t, mapper_time_t subtrahend)
{
    if (t->sec > subtrahend.sec) {
        t->sec -= subtrahend.sec;
        t->frac -= subtrahend.frac;
        if (t->frac > subtrahend.frac) // overflow
            --t->sec;
    }
    else {
        t->sec = 0;
        t->frac = 0;
    }
}

double mapper_time_get_double(mapper_time_t t)
{
    return (double)t.sec + (double)t.frac * multiplier;
}

void mapper_time_set_double(mapper_time_t *t, double value)
{
    if (value > 0.) {
        t->sec = floor(value);
        value -= t->sec;
        t->frac = (uint32_t) (((double)value) * (double)(1LL<<32));
    }
    else {
        t->sec = 0;
        t->frac = 0;
    }
}

void mapper_time_copy(mapper_time_t *l, mapper_time_t r)
{
    l->sec = r.sec;
    l->frac = r.frac;
}
