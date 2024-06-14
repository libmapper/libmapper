
#ifndef __MPR_TIME_H__
#define __MPR_TIME_H__

#include <lo/lo.h>

/*! A 64-bit data structure containing an NTP-compatible time tag, as used by OSC. */
typedef lo_timetag mpr_time;
#define MPR_NOW LO_TT_IMMEDIATE

/*! Get the current time. */
double mpr_get_current_time(void);

/*! Return the difference in seconds between two mpr_times.
 *  \param minuend      The minuend.
 *  \param subtrahend   The subtrahend.
 *  \return             The difference a-b in seconds. */
double mpr_time_get_diff(mpr_time minuend, mpr_time subtrahend);

#endif /* __MPR_TIME_H__ */
