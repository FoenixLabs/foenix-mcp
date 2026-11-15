/*
 * Definitions for access the bq4802LY real time clock
 */

#include "log.h"
#include "interrupt.h"
#include "gabe_reg.h"
#include "rtc.h"
#include "rtc_reg.h"
#include "simpleio.h"
#include "timers.h"
#include "dev/txt_screen.h"

#if MODEL == MODEL_FOENIX_A2560K || MODEL == MODEL_FOENIX_GENX || MODEL == MODEL_FOENIX_A2560X
#include "A2560K/vky_chan_b.h"
/* 2.0.32: the title bar clock writes into row 0 of the console screen (screen 0 = VICKY channel B) */
#define RTC_CLOCK_TEXT      VKY3_B_TEXT_MATRIX
#define RTC_CLOCK_SCREEN    0
#endif
#define RTC_CLOCK_LEN       9           /* " hh:mm:ss" */

static unsigned long rtc_ticks;

static short rtc_clock_on = 0;              /* 2.0.32: 1 = show the time in the title bar (SET CLOCK ON) */
static unsigned char rtc_clock_sec = 0xFF;  /* the seconds shown last (0xFF = nothing shown yet) */
static short rtc_clock_column = 0;          /* first column of the clock text in row 0 */

void rtc_handle_int();                      /* below; rtc_clock_set puts it back on the RTC interrupt */

/*
 * Draw the time in the right-hand end of the title bar, if the seconds changed
 *
 * 2.0.32. Called from the RTC interrupt (64 times a second), so it does not use the screen driver:
 * it writes the characters straight into the text memory of row 0 and leaves the colours alone, so
 * the time takes the colours of the bar. When the command line redraws its bar, the time comes back
 * at the next second. It only reads the RTC, it never writes to it.
 */
static void rtc_clock_draw() {
#ifdef RTC_CLOCK_TEXT
    unsigned char sec, min, hour;
    volatile unsigned char * p;

    sec = *RTC_SEC;
    if (sec == rtc_clock_sec) {
        return;                         /* still the same second: nothing to do */
    }
    rtc_clock_sec = sec;
    min = *RTC_MIN;
    hour = *RTC_HOUR & 0x7f;            /* without the PM bit of 12-hour mode */

    p = RTC_CLOCK_TEXT + rtc_clock_column;
    *p++ = ' ';
    *p++ = '0' + (hour >> 4);
    *p++ = '0' + (hour & 0x0f);
    *p++ = ':';
    *p++ = '0' + (min >> 4);
    *p++ = '0' + (min & 0x0f);
    *p++ = ':';
    *p++ = '0' + (sec >> 4);
    *p   = '0' + (sec & 0x0f);
#endif
}

/**
 * Switch the title bar clock on or off (2.0.32, SET CLOCK ON / OFF)
 *
 * @param on 1 = show the time in the right-hand end of the title bar, 0 = stop showing it
 */
void rtc_clock_set(short on) {
#ifdef RTC_CLOCK_TEXT
    t_extent text_size;
    short i;

    if (on) {
        /* The clock ends at the last visible column; the screen width can differ per mode */
        txt_get_sizes(RTC_CLOCK_SCREEN, &text_size, 0);
        rtc_clock_column = text_size.width - RTC_CLOCK_LEN;
        rtc_clock_sec = 0xFF;           /* draw at the next interrupt */
        rtc_clock_on = 1;

        /* Make sure the RTC interrupt really runs. GAVIN sees one event per edge of the RTC's
           interrupt line, and the bq4802 keeps that line active until its flags are read. If an
           event got lost (cleared before the flags were read), the line stays active and no new
           interrupt ever comes. Putting our handler back and reading the flags (in
           rtc_enable_ticks) starts it again. */
        int_register(INT_RTC, rtc_handle_int);
        rtc_enable_ticks();

    } else {
        rtc_clock_on = 0;
        for (i = 0; i < RTC_CLOCK_LEN; i++) {
            RTC_CLOCK_TEXT[rtc_clock_column + i] = ' ';
        }
    }
#endif
}

/**
 * Count of RTC interrupts handled since start (2.0.32, shown by GET CLOCK)
 *
 * @return the number of RTC periodic interrupts the MCP's handler has seen
 */
unsigned long rtc_get_ticks() {
    return rtc_ticks;
}

/**
 * Tell whether the title bar clock is on (2.0.32)
 *
 * @return 1 if the clock is shown, 0 if not
 */
short rtc_clock_get() {
    return rtc_clock_on;
}

/*
 * Interrupt handler for the real time clock
 */
void rtc_handle_int() {
    unsigned char flags;

    flags = *RTC_FLAGS; /* Acknowledge the interrupt */
    
    rtc_ticks++;

    if (rtc_clock_on) {
        rtc_clock_draw();               /* 2.0.32: the title bar clock */
    }
}

/*
 * Initialize the RTC
 */
void rtc_init() {
    unsigned char flags;
    unsigned char rates;
    unsigned char enables;
    unsigned char ctrl;

    TRACE("rtc_init");

    int_disable(INT_RTC);

    /*
     * Make sure the RTC is on.
     * 2.0.32: this wrote RTC_STOP alone, which also switched the clock to 12-hour mode (and DSE off)
     * at every boot: after SET TIME 16:30:00 and a restart the clock said 04:30 PM. Now only STOP is
     * set and UTI cleared; the 24/12 and DSE bits stay, and the register is only written if needed.
     */
    ctrl = *RTC_CTRL;
    if ((ctrl & (RTC_STOP | RTC_UTI)) != RTC_STOP) {
        *RTC_CTRL = (ctrl & (RTC_2412 | RTC_DSE)) | RTC_STOP;
    }

    /*
     * For the moment: Every so often, the RTC interrupt gets acknowledged
     * without clearing the flags. Until I can sort out why, I will use
     * the SOF A interrupt as a surrogate for the RTC jiffie timer
     */
#if !(MODEL == MODEL_FOENIX_C256U || MODEL == MODEL_FOENIX_C256U_PLUS || MODEL == MODEL_FOENIX_FMX)
    // /* Set the periodic interrupt to 15 millisecs */
    *RTC_RATES = RTC_RATE_15ms;

    int_register(INT_RTC, rtc_handle_int);
    rtc_ticks = 0;

    /* Enable the periodic interrupt */
    rtc_enable_ticks();
#endif
}

/*
 * Make sure the RTC tick counter is enabled
 */
void rtc_enable_ticks() {
    /* Set the periodic interrupt to 15 millisecs */
    *RTC_RATES = RTC_RATE_15ms;

    unsigned char flags = *RTC_FLAGS; /* Acknowledge any previous interrupt before we start. */

    *RTC_ENABLES = RTC_PIE;

    int_enable(INT_RTC);
}

/**
 * Register a function to be called periodically
 *
 * @param rate the rate at which the function should be called using the bq4802LY periodic rate values (0 to disable)
 * @param handler a pointer to a function from void to void to be called
 * @return 0 on success, any other number is an error
 */
short rtc_register_periodic(short rate, FUNC_V_2_V handler) {
    if (rate == 0) {
        int_disable(INT_RTC);
        *RTC_RATES = 0;
        *RTC_ENABLES &= ~RTC_PIE;
        
    } else {
        int_register(INT_RTC, handler);
        *RTC_RATES = rate;
        unsigned char flags = *RTC_FLAGS;
        *RTC_ENABLES = RTC_PIE;
        int_enable(INT_RTC);
    }

	return 0;
}

/*
 * Set the time on the RTC
 *
 * Inputs:
 * time = pointer to a t_time record containing the correct time
 */
void rtc_set_time(p_time time) {
    unsigned char ctrl;
    unsigned char century_bcd, year_bcd, month_bcd, day_bcd;
    unsigned char hour_bcd, minute_bcd, second_bcd;
    unsigned short century;
    unsigned short year;

    century = time->year / 100;
    year = time->year - (century * 100);

    /* Compute the BCD values for the time */

    century_bcd = i_to_bcd(century);
    year_bcd = i_to_bcd(year);
    month_bcd = i_to_bcd(time->month);
    day_bcd = i_to_bcd(time->day);
    hour_bcd = i_to_bcd(time->hour);
    minute_bcd = i_to_bcd(time->minute);
    second_bcd = i_to_bcd(time->second);

    log_num(LOG_INFO, "Century: ", century_bcd);
    log_num(LOG_INFO, "Year: ", year_bcd);
    log_num(LOG_INFO, "Month: ", month_bcd);
    log_num(LOG_INFO, "Day: ", day_bcd);
    log_num(LOG_INFO, "Hour: ", hour_bcd);
    log_num(LOG_INFO, "Minute: ", minute_bcd);
    log_num(LOG_INFO, "Second: ", second_bcd);

    if (!time->is_24hours) {
        if (time->is_pm) {
            hour_bcd = hour_bcd | 0x80;
        }
    }
    minute_bcd = i_to_bcd(time->minute);
    second_bcd = i_to_bcd(time->second);

    /* Temporarily disable updates to the clock */
    ctrl = *RTC_CTRL;
    *RTC_CTRL = ctrl | RTC_UTI;
    logmsg(LOG_INFO, "RTC Disabled");
    log_num(LOG_INFO, "RTC Rates: ", *RTC_RATES);
    log_num(LOG_INFO, "RTC Enables: ", *RTC_ENABLES);
    log_num(LOG_INFO, "RTC Flags: ", *RTC_FLAGS);
    log_num(LOG_INFO, "RTC Control: ", *RTC_CTRL);

    /* Set the time in the RTC */

    *RTC_CENTURY = century_bcd;
    *RTC_YEAR = year_bcd;
    *RTC_MONTH = month_bcd;
    *RTC_DAY = day_bcd;
    *RTC_HOUR = hour_bcd;
    *RTC_MIN = minute_bcd;
    *RTC_SEC = second_bcd;

    /* Verify */

    century_bcd = *RTC_CENTURY;
    year_bcd = *RTC_YEAR;
    month_bcd = *RTC_MONTH;
    day_bcd = *RTC_DAY;
    hour_bcd = *RTC_HOUR;
    minute_bcd = *RTC_MIN;
    second_bcd = *RTC_SEC;
    log_num(LOG_INFO, "REG Century: ", century_bcd);
    log_num(LOG_INFO, "REG Year: ", year_bcd);
    log_num(LOG_INFO, "REG Month: ", month_bcd);
    log_num(LOG_INFO, "REG Day: ", day_bcd);
    log_num(LOG_INFO, "REG Hour: ", hour_bcd);
    log_num(LOG_INFO, "REG Minute: ", minute_bcd);
    log_num(LOG_INFO, "REG Second: ", second_bcd);

    /* Set the 24/12 hour control bit if needed */
    if (time->is_24hours) {
        ctrl = ctrl | RTC_2412;
    }

    /* Re-enable updates to the clock */
    *RTC_CTRL = (ctrl & 0x07) | RTC_STOP;
    logmsg(LOG_INFO, "RTC Enabled");
    log_num(LOG_INFO, "RTC Rates: ", *RTC_RATES);
    log_num(LOG_INFO, "RTC Enables: ", *RTC_ENABLES);
    log_num(LOG_INFO, "RTC Flags: ", *RTC_FLAGS);
    log_num(LOG_INFO, "RTC Control: ", *RTC_CTRL);
}

/*
 * Get the time on the RTC
 *
 * Inputs:
 * time = pointer to a t_time record in which to put the current time
 */
void rtc_get_time(p_time time) {
    unsigned char ctrl;
    unsigned char century_bcd, year_bcd, month_bcd, day_bcd;
    unsigned char hour_bcd, minute_bcd, second_bcd;

    /* Temporarily disable updates to the clock */
    ctrl = *RTC_CTRL;
    *RTC_CTRL = ctrl | RTC_UTI;
    logmsg(LOG_INFO, "RTC Disabled");
    log_num(LOG_INFO, "RTC Rates: ", *RTC_RATES);
    log_num(LOG_INFO, "RTC Enables: ", *RTC_ENABLES);
    log_num(LOG_INFO, "RTC Flags: ", *RTC_FLAGS);
    log_num(LOG_INFO, "RTC Control: ", *RTC_CTRL);

    if (*RTC_CTRL & RTC_2412) {
        time->is_24hours = 1;
    } else {
        time->is_24hours = 0;
    }

    century_bcd = *RTC_CENTURY;
    year_bcd = *RTC_YEAR;
    month_bcd = *RTC_MONTH;
    day_bcd = *RTC_DAY;
    hour_bcd = *RTC_HOUR;
    minute_bcd = *RTC_MIN;
    second_bcd = *RTC_SEC;

    /* Re-enable updates to the clock */
    *RTC_CTRL = (ctrl & 0x07) | RTC_STOP;
    logmsg(LOG_INFO, "RTC Enabled");
    log_num(LOG_INFO, "RTC Rates: ", *RTC_RATES);
    log_num(LOG_INFO, "RTC Enables: ", *RTC_ENABLES);
    log_num(LOG_INFO, "RTC Flags: ", *RTC_FLAGS);
    log_num(LOG_INFO, "RTC Control: ", *RTC_CTRL);

    log_num(LOG_INFO, "Century: ", century_bcd);
    log_num(LOG_INFO, "Year: ", year_bcd);
    log_num(LOG_INFO, "Month: ", month_bcd);
    log_num(LOG_INFO, "Day: ", day_bcd);
    log_num(LOG_INFO, "Hour: ", hour_bcd);
    log_num(LOG_INFO, "Minute: ", minute_bcd);
    log_num(LOG_INFO, "Second: ", second_bcd);

    /* Fill out the time record */

    time->year = bcd_to_i(century_bcd) * 100 + bcd_to_i(year_bcd);
    time->month = bcd_to_i(month_bcd);
    time->day = bcd_to_i(day_bcd);
    time->hour = bcd_to_i(hour_bcd & 0x7f);
    time->is_pm = ((hour_bcd & 0x80) == 0x80) ? 1 : 0;
    time->minute = bcd_to_i(minute_bcd);
    time->second = bcd_to_i(second_bcd);
}

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
long rtc_get_jiffies() {
    return timers_jiffies();
}

