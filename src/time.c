
#include "config.h"

#include <lo/lo.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <sys/time.h>

#include "mpr_internal.h"
#include "types_internal.h"
#include <mpr/mpr.h>

static double multiplier = 1.0/((double)(1LL<<32));

/*! Internal function to get the current time. */
double mpr_get_current_time()
{
#ifdef HAVE_GETTIMEOFDAY
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (double)tv.tv_sec + tv.tv_usec / 1000000.0;
#else
#error No timing method known on this platform.
#endif
}

void mpr_time_now(mpr_time_t *t)
{
    lo_timetag_now((lo_timetag*)t);
}

double mpr_time_diff(const mpr_time_t l, const mpr_time_t r)
{
    return ((double)l.sec - (double)r.sec
            + ((double)l.frac - (double)r.frac) * multiplier);
}

void mpr_time_add_dbl(mpr_time_t *t, double d)
{
    if (!d)
        return;

    d += (double)t->frac * multiplier;
    if (d < 0 && floor(d) > t->sec)
        t->sec = t->frac = 0;
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

void mpr_time_mul(mpr_time_t *t, double d)
{
    if (d > 0.) {
        d *= mpr_time_get_dbl(*t);
        t->sec = floor(d);
        d -= t->sec;
        t->frac = (uint32_t) (d * (double)(1LL<<32));
    }
    else
        t->sec = t->frac = 0;
}

void mpr_time_add(mpr_time_t *t, mpr_time_t addend)
{
    t->sec += addend.sec;
    t->frac += addend.frac;
    if (t->frac < addend.frac) // overflow
        ++t->sec;
}

void mpr_time_sub(mpr_time_t *t, mpr_time_t subtrahend)
{
    if (t->sec > subtrahend.sec) {
        t->sec -= subtrahend.sec;
        if (t->frac < subtrahend.frac) // overflow
            --t->sec;
        t->frac -= subtrahend.frac;
    }
    else
        t->sec = t->frac = 0;
}

double mpr_time_get_dbl(mpr_time_t t)
{
    return (double)t.sec + (double)t.frac * multiplier;
}

void mpr_time_set_dbl(mpr_time_t *t, double value)
{
    if (value > 0.) {
        t->sec = floor(value);
        value -= t->sec;
        t->frac = (uint32_t) (((double)value) * (double)(1LL<<32));
    }
    else
        t->sec = t->frac = 0;
}

void mpr_time_cpy(mpr_time_t *l, mpr_time_t r)
{
    l->sec = r.sec;
    l->frac = r.frac;
}
