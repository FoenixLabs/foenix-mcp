/*
 * rtctest.c - find out why the real-time clock (bq4802) runs slow on the A2560K
 *
 * The RTC's seconds are timed against the MCP jiffy clock. The jiffies come
 * from GAVIN timer 3, which counts video frames of channel A (800 x 600 at
 * 60.32 Hz), so they do not depend on the RTC at all.
 *
 * Phase 1: only READ the RTC seconds register directly. Nothing is written to
 *          the RTC, so this shows how fast the chip itself counts.
 * Phase 2: the same, but in between call the MCP's own time routine
 *          (sys_time_getrtc, the MCP call $52) over and over. That routine sets "update
 *          transfer inhibit" and then rewrites the control register on every
 *          call. If the clock is slower here than in phase 1, those writes
 *          are what slows it down.
 *
 * Each phase lasts 20 seconds of jiffies. At the end the program prints, per
 * phase, how many RTC seconds went by and the RTC speed in percent of real
 * time (100% = right).
 *
 * Start it from the MCP (it needs the MCP for printf, the clock and the time
 * routine).
 */

#include <stdio.h>
#include <stdint.h>
#include <mcp/syscalls.h>

/* bq4802 registers (byte access only; see the K25 hardware reference) */
#define RTC_SEC     ((volatile uint8_t *)0xFEC00080)
#define RTC_RATES   ((volatile uint8_t *)0xFEC0008B)
#define RTC_ENABLES ((volatile uint8_t *)0xFEC0008C)
#define RTC_CTRL    ((volatile uint8_t *)0xFEC0008E)

#define PHASE_JIFFIES  (20 * 60)    /* length of each phase */
#define JIFFIES_PER_S  60.32        /* channel A frame rate (only used in the text) */


/*
 * now - read the MCP jiffy clock
 *
 * Input:  none
 * Output: jiffies since the MCP started
 */
static long now( void )
{
	return sys_time_jiffies();
}


/*
 * run_phase - time the RTC seconds against the jiffies for one phase
 *
 * Input:  use_mcp = 0: only read the seconds register;
 *                   1: also call sys_time_getrtc between the reads
 *         ticks   = output: RTC seconds that went by
 *         calls   = output: number of sys_time_getrtc calls made
 * Output: the jiffies between the first and the last change of the seconds
 *         register (the RTC seconds are counted over exactly that time)
 */
static long run_phase( int use_mcp, long *ticks, long *calls )
{
	t_time t;
	uint8_t last, sec;
	long start, first_change = -1, last_change = -1;

	*ticks = 0;
	*calls = 0;
	last = *RTC_SEC;
	start = now();

	while ( now() - start < PHASE_JIFFIES )
	{
		if ( use_mcp )
		{
			sys_time_getrtc( &t );
			(*calls)++;
		}

		sec = *RTC_SEC;
		if ( sec != last )
		{
			/* Count the seconds between the first and the last change seen,
			   so partial seconds at the start and end do not count. */
			if ( first_change < 0 )
				first_change = now();
			else
				(*ticks)++;
			last_change = now();
			last = sec;
		}
	}

	if ( first_change < 0 || last_change <= first_change )
		return 0;
	return last_change - first_change;
}


/*
 * report - print one phase's result
 *
 * Input:  name    = phase name
 *         jiffies = time the seconds were counted over
 *         ticks   = RTC seconds counted
 *         calls   = sys_time_getrtc calls made
 * Output: none
 */
static void report( const char *name, long jiffies, long ticks, long calls )
{
	long real_ms = jiffies * 1000L * 100L / 6032L;     /* jiffies / 60.32 Hz, in ms */
	long pct;

	if ( jiffies == 0 || ticks == 0 )
	{
		printf( "%s: the RTC seconds did not change (clock stopped?)\n", name );
		return;
	}
	/* RTC seconds / real seconds, in tenths of a percent */
	pct = ticks * 1000L * 1000L / real_ms;
	printf( "%s: %ld RTC seconds in %ld.%03ld real seconds = %ld.%ld%% of real time",
		name, ticks, real_ms / 1000, real_ms % 1000, pct / 10, pct % 10 );
	if ( calls )
		printf( " (%ld time reads)", calls );
	printf( "\n" );
}


/*
 * main - print the RTC set-up, run both phases, print the results
 *
 * Input:  none
 * Output: 0
 */
int main( void )
{
	t_time t;
	long j1, j2, ticks1, ticks2, calls1, calls2;
	uint8_t ctrl_before, ctrl_after;

	ctrl_before = *RTC_CTRL;
	sys_time_getrtc( &t );
	printf( "rtctest: bq4802 real-time clock speed\n" );
	printf( "control $%02X (UTI %d, STOP %d, 24h %d, DSE %d), rates $%02X, enables $%02X\n",
		ctrl_before, (ctrl_before >> 3) & 1, (ctrl_before >> 2) & 1,
		(ctrl_before >> 1) & 1, ctrl_before & 1, *RTC_RATES, *RTC_ENABLES );
	printf( "time now: %04d-%02d-%02d %02d:%02d:%02d\n\n",
		t.year, t.month, t.day, t.hour, t.minute, t.second );

	printf( "phase 1: reading the seconds register only (20 s) ...\n" );
	j1 = run_phase( 0, &ticks1, &calls1 );
	printf( "phase 2: also calling the MCP time routine (20 s) ...\n" );
	j2 = run_phase( 1, &ticks2, &calls2 );
	ctrl_after = *RTC_CTRL;

	printf( "\n" );
	report( "phase 1", j1, ticks1, 0 );
	report( "phase 2", j2, ticks2, calls2 );
	printf( "control at the end: $%02X\n", ctrl_after );
	return 0;
}
