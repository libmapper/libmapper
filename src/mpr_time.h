
#ifndef __MAPPER_TIME_H__
#define __MAPPER_TIME_H__

/*! Get the current time. */
double mpr_get_current_time(void);

/*! Return the difference in seconds between two mpr_times.
 *  \param minuend      The minuend.
 *  \param subtrahend   The subtrahend.
 *  \return             The difference a-b in seconds. */
double mpr_time_get_diff(mpr_time minuend, mpr_time subtrahend);

#endif /* __MAPPER_TIME_H__ */
