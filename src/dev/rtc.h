/*
 * Declarations for access the bq4802LY real time clock
 */

#ifndef __RTC_H
#define __RTC_H

#include "types.h"

typedef struct s_time {
    short year;
    short month;
    short day;
    short hour;
    short minute;
    short second;
    short is_pm;
    short is_24hours;
} t_time, *p_time;

/*
 * Initialize the RTC
 */
extern void rtc_init();

/*
 * Make sure the RTC tick counter is enabled
 */
extern void rtc_enable_ticks();

/*
 * Set the time on the RTC
 *
 * Inputs:
 * time = pointer to a t_time record containing the correct time
 */
extern void rtc_set_time(p_time time);

/*
 * Get the time on the RTC
 *
 * Inputs:
 * time = pointer to a t_time record in which to put the current time
 */
extern void rtc_get_time(p_time time);

/*
 * Get the number of jiffies since the system last booted.
 *
 * NOTE: a jiffie is 1/60 of a second. This timer will not be
 *       100% precise, so it should be used for timeout purposes
 *       where precision is not critical.
 *
 * Returns:
 * the number of jiffies since the last reset
 */
extern long rtc_get_jiffies();

/**
 * Register a function to be called periodically
 *
 * @param rate the rate at which the function should be called using the bq4802LY periodic rate values (0 to disable)
 * @param handler a pointer to a function from void to void to be called
 * @return 0 on success, any other number is an error
 */
extern short rtc_register_periodic(short rate, FUNC_V_2_V handler);

/**
 * Switch the title bar clock on or off (2.0.32, SET CLOCK ON / OFF)
 *
 * @param on 1 = show the time in the right-hand end of the title bar, 0 = stop showing it
 */
extern void rtc_clock_set(short on);

/**
 * Choose 24-hour or 12-hour display for the title bar clock (2.0.32, SET CLOCK 24 / 12)
 *
 * Only the display changes; the RTC itself and GET TIME stay as they are.
 *
 * @param hours24 1 = show 00:00:00 - 23:59:59, 0 = show 12:00:00 AM - 11:59:59 PM
 */
extern void rtc_clock_set_24h(short hours24);

#endif
