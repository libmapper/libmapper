#include "config.h"

#include <lo/lo.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <sys/time.h>

#include "mapper_internal.h"
#include "types_internal.h"
#include <mapper/mapper.h>

static double multiplier = 0.00000000023283064365;

/*! Internal function to get the current time. */
double mpr_get_current_time()
{
#ifdef HAVE_GETTIMEOFDAY
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (double)tv.tv_sec + ((double)tv.tv_usec) * 0.000001;
#else
#error No timing method known on this platform.
#endif
}

double mpr_time_get_diff(const mpr_time l, const mpr_time r)
{
    return ((double)l.sec - (double)r.sec
            + ((double)l.frac - (double)r.frac) * multiplier);
}

void mpr_time_add_dbl(mpr_time *t, double d)
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
        t->frac = (uint32_t) (((double)d) * 4294967296.);
    }
}

void mpr_time_mul(mpr_time *t, double d)
{
    if (d > 0.) {
        d *= mpr_time_as_dbl(*t);
        t->sec = floor(d);
        d -= t->sec;
        t->frac = (uint32_t) (d * 4294967296.);
    }
    else
        t->sec = t->frac = 0;
}

void mpr_time_add(mpr_time *t, mpr_time addend)
{
    t->sec += addend.sec;
    t->frac += addend.frac;
    if (t->frac < addend.frac) /* overflow */
        ++t->sec;
}

void mpr_time_sub(mpr_time *t, mpr_time subtrahend)
{
    if (t->sec > subtrahend.sec) {
        t->sec -= subtrahend.sec;
        if (t->frac < subtrahend.frac) /* overflow */
            --t->sec;
        t->frac -= subtrahend.frac;
    }
    else
        t->sec = t->frac = 0;
}

double mpr_time_as_dbl(mpr_time t)
{
    return (double)t.sec + (double)t.frac * multiplier;
}

void mpr_time_set_dbl(mpr_time *t, double value)
{
    if (value > 0.) {
        t->sec = floor(value);
        value -= t->sec;
        t->frac = (uint32_t) (((double)value) * 4294967296.);
    }
    else
        t->sec = t->frac = 0;
}

void mpr_time_set(mpr_time *l, mpr_time r)
{
    if (r.sec == 0 && r.frac == 1) /* MPR_NOW */
        lo_timetag_now((lo_timetag*)l);
    else
        memcpy(l, &r, sizeof(mpr_time));
}

MPR_INLINE int mpr_time_cmp(mpr_time l, mpr_time r)
{
    return l.sec == r.sec ? l.frac - r.frac : l.sec - r.sec;
}
