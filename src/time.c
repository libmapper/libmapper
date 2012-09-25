
#include "config.h"

#include <lo/lo.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <sys/time.h>

#include "mapper_internal.h"
#include "types_internal.h"
#include <mapper/mapper.h>

void mdev_clock_init(mapper_device dev)
{
    mapper_clock_t clock = dev->admin->clock;
    clock.rate = 1;
    clock.offset = 0;
    clock.confidence = 0;
    clock.local_index = 0;
    int i;
    for (i=0; i<10; i++) {
        clock.local[i].device_id = 0;
        clock.remote.device_id = 0;
    }
    mdev_timetag_now(dev, &clock.now);
    clock.next_ping = clock.now.sec + 10;
}

void mdev_clock_adjust(mapper_device dev,
                       mapper_timetag_t then,
                       double confidence)
{
    // set confidence to 1 for now since it is not being updated
    confidence = 1;

    // first get current time
    mapper_timetag_t now;
    mdev_timetag_now(dev, &now);

    // TODO: using doubles for now, get fancier later
    double fnow = now.sec + now.frac * 0.000000000232831;
    double fthen = then.sec + then.frac * 0.000000000232831;

    // use diff to influence rate & offset
    if (fthen < fnow)
        confidence *= 0.1;
    double diff = fnow - (fnow * (1 - confidence) + fthen * confidence);
    dev->admin->clock.offset += diff;

    // adjust stored timetags
    int i;
    for (i=0; i<10; i++) {
        mapper_timetag_add_seconds(&dev->admin->clock.local[i].timetag, diff);
        mapper_timetag_add_seconds(&dev->admin->clock.local[i].timetag, diff);
    }
}

void mdev_timetag_now(mapper_device dev,
                      mapper_timetag_t *timetag)
{
    // first get current time from system clock
    // adjust using rate and offset from mapping network sync
    lo_timetag_now((lo_timetag*)timetag);
    mapper_timetag_add_seconds(timetag, dev->admin->clock.offset);
}

double mapper_timetag_difference(mapper_timetag_t a, mapper_timetag_t b)
{
    return (double)a.sec - (double)b.sec +
        ((double)a.frac - (double)b.frac) * 0.000000000232831;
}

void mapper_timetag_add_seconds(mapper_timetag_t *a, double b)
{
    b += a->frac * 0.000000000232831;
    if (b >= 0.0) {
        a->sec += (uint32_t)b;
        b -= (uint32_t)b;
        a->frac = (uint32_t) (b * (double)(1LL<<32));
    }
    else {
        a->sec -= (uint32_t)b;
        b += (uint32_t)b;
        if (b < 0.0) {
            a->sec--;
            a->frac = (uint32_t) ((1-b) * (double)(1LL<<32));
        }
    }
}
