/***********************************************************************
 * PROF.C - a simple profiler, see prof.h. Compiled to nothing unless
 * FELINC_PROFILE is 1.
 *
 * Part of FELINC, the Foenix Labs Ethernet Library in C. (c) R. Kint
 **********************************************************************/
#include "prof.h"

#if FELINC_PROFILE

#include <stdio.h>
#include "utils.h"

/* Timer control: timer 2 is set in bits 16-23 of TIMER_TCR0 */
#define PROF_TCR0			(*(volatile uint32_t *)0xFEC00200UL)
#define PROF_TCR0_TIMER2	0x00FF0000UL	/* all control bits of timer 2 */
#define PROF_TIMER2_RUN		0x00090000UL	/* ENABLE_2 + CNTUP_2: count up, never clear */

uint32_t prof_cycles[PROF_SLOTS];
uint32_t prof_calls[PROF_SLOTS];

static uint32_t start_cycles;		/* timer 2 at the last reset */
static uint32_t start_jiffies;		/* jiffies at the last reset */

/***********************************************************************
 * prof_reset
 * Sets all totals to 0 and starts a new measurement. The first call
 * starts timer 2. The other timers' bits in TIMER_TCR0 are kept.
 *
 * Input:
 *   none
 * Output:
 *   none
 **********************************************************************/
void prof_reset( void )
{
	uint16_t i;

	PROF_TCR0 = (PROF_TCR0 & ~PROF_TCR0_TIMER2) | PROF_TIMER2_RUN;
	for( i = 0; i < PROF_SLOTS; i++ )
	{
		prof_cycles[i] = 0;
		prof_calls[i] = 0;
	}
	start_jiffies = utl_jiffies();
	start_cycles = PROF_TIMER;
}

/***********************************************************************
 * prof_print
 * Prints the time spent in each measured part since the last reset:
 * the total in milliseconds, its share of the elapsed time, the number
 * of calls and the time per call in microseconds. The time outside
 * felinc_poll() is the program's own work (and printing).
 * Then it starts a new measurement.
 * Check: the elapsed time by timer 2 and by the jiffies must agree,
 * or timer 2 is used by something else as well.
 *
 * Input:
 *   none
 * Output:
 *   none
 **********************************************************************/
void prof_print( void )
{
	static const char *names[PROF_SLOTS] = { "felinc_poll", "  RX nothing", "  RX FIFO",
		"  handle frame", "  send pending", "TX FIFO", "checksums", "ring buffer copy",
		"utl_jiffies" };
	uint32_t elapsed = PROF_TIMER - start_cycles;
	uint32_t jiffies = utl_jiffies() - start_jiffies;
	uint32_t pct = elapsed / 1000;	/* elapsed / 1000: for percentages in tenths */
	uint16_t i;

	if( pct == 0 ) pct = 1;
	printf("\n*** profile: %lu ms by timer 2, %lu ms by jiffies",
			(unsigned long)(elapsed / 25000), (unsigned long)(jiffies * 50 / 3));
	printf("\n    part              total ms   share   calls   us/call");
	for( i = 0; i < PROF_SLOTS; i++ )
		printf("\n    %-16s %9lu %5lu.%lu%% %7lu %9lu", names[i],
				(unsigned long)(prof_cycles[i] / 25000),
				(unsigned long)(prof_cycles[i] / pct / 10), (unsigned long)(prof_cycles[i] / pct % 10),
				(unsigned long)prof_calls[i],
				(unsigned long)(prof_calls[i] ? prof_cycles[i] / prof_calls[i] / 25 : 0));
	printf("\n    outside felinc_poll %7lu ms\n",
			(unsigned long)((elapsed - prof_cycles[PROF_POLL]) / 25000));
	prof_reset();
}

#endif
