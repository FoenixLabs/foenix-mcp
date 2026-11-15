/*
 * rtcclock.c - show the real-time clock in the middle of the screen, driven by
 *              the RTC's own interrupt, to keep track of how it keeps time
 *
 * The bq4802 gives a periodic interrupt 64 times a second (the MCP sets this
 * up at start: rate $0A = 15.625 ms). This program puts its own handler on
 * that interrupt. On every interrupt the handler:
 *   - reads the flags register (that acknowledges the interrupt),
 *   - reads the time the way the MCP 2.0.30 does: set UTI (update transfer
 *     inhibit) in the control register, read all time registers, write the
 *     control register back. So the RTC gets 128 control writes a second.
 * The main program shows the RTC date and time in the right-hand corner of
 * the MCP title bar (in the bar's own colours), and in the middle of the
 * screen how long the board has been on according to the jiffy clock (video
 * frames, independent of the RTC), the number of interrupts and register
 * accesses, and the battery-valid flag of the chip.
 *
 * Interrupt used: the RTC's periodic interrupt, MCP interrupt number INT_RTC
 * ($1F) = GAVIN interrupt bit 31, level 3, vector $4F.
 *
 * Press q to stop: the MCP's own RTC interrupt handler is put back.
 *
 * Start it from the MCP (it needs the MCP for the interrupt, the screen and
 * the jiffy clock).
 */

#include <stdio.h>
#include <stdint.h>
#include <mcp/syscalls.h>
#include <mcp/interrupt.h>

/* bq4802 registers (byte access only; see the K25 hardware reference) */
#define RTC_SEC      ((volatile uint8_t *)0xFEC00080)
#define RTC_MIN      ((volatile uint8_t *)0xFEC00082)
#define RTC_HOUR     ((volatile uint8_t *)0xFEC00084)
#define RTC_DAY      ((volatile uint8_t *)0xFEC00086)
#define RTC_MONTH    ((volatile uint8_t *)0xFEC00089)
#define RTC_YEAR     ((volatile uint8_t *)0xFEC0008A)
#define RTC_FLAGS    ((volatile uint8_t *)0xFEC0008D)
#define RTC_CTRL     ((volatile uint8_t *)0xFEC0008E)
#define RTC_CENTURY  ((volatile uint8_t *)0xFEC0008F)

#define RTC_UTI      0x08    /* control: update transfer inhibit */
#define RTC_STOP     0x04    /* control: 1 = oscillator runs */
#define RTC_PF       0x04    /* flags: periodic interrupt */
#define RTC_BVF      0x01    /* flags: battery valid */

#define KEY_WAITING  0x04    /* sys_chan_status(): a key can be read */
#define BOX_W        46      /* width of the box drawn on the screen */
#define BOX_H        10      /* height of the box */
#define FRAME_HZ_X100 6032   /* jiffies per second x 100 (channel A 800 x 600 frame rate) */

/* Filled in by the interrupt handler, read by the main program */
static volatile uint8_t  t_century, t_year, t_month, t_day, t_hour, t_min, t_sec;
static volatile uint8_t  t_flags_seen;     /* all flag bits seen since the start (OR-ed) */
static volatile uint8_t  t_ctrl;           /* control register before the last read */
static volatile uint32_t irq_count;        /* interrupts handled */
static volatile uint32_t reg_reads;        /* RTC register reads */
static volatile uint32_t reg_writes;       /* RTC register writes */


/*
 * rtc_irq - interrupt handler for the RTC periodic interrupt
 *
 * Input:  none (called by the MCP's interrupt dispatcher)
 * Output: none; fills in the t_... variables and the counters
 *
 * Reads the time the same way the MCP 2.0.30 rtc_get_time does, so the RTC
 * sees the same register traffic as with a program that asks for the time
 * all the time.
 */
static void rtc_irq( void )
{
	uint8_t ctrl;

	t_flags_seen |= *RTC_FLAGS;              /* reading the flags acknowledges the interrupt */
	irq_count++;

	ctrl = *RTC_CTRL;
	*RTC_CTRL = ctrl | RTC_UTI;              /* hold the user registers still while reading */
	t_century = *RTC_CENTURY;
	t_year    = *RTC_YEAR;
	t_month   = *RTC_MONTH;
	t_day     = *RTC_DAY;
	t_hour    = *RTC_HOUR;
	t_min     = *RTC_MIN;
	t_sec     = *RTC_SEC;
	*RTC_CTRL = ( ctrl & 0x07 ) | RTC_STOP;  /* the MCP writes it back like this */
	t_ctrl    = ctrl;

	reg_reads  += 9;                         /* flags, control, 7 time registers */
	reg_writes += 2;
}


/*
 * bcd - turn a BCD byte from the RTC into a number
 *
 * Input:  b = BCD byte (two digits)
 * Output: the value 0-99
 */
static int bcd( uint8_t b )
{
	return ( b >> 4 ) * 10 + ( b & 0x0F );
}


/*
 * at - move the cursor to a place on the screen
 *
 * Input:  x, y = column and row
 * Output: none
 */
static void at( short x, short y )
{
	fflush( stdout );
	sys_txt_set_xy( 0, x, y );
}


/*
 * draw_bar - show the RTC date and time in the right-hand corner of the title bar
 *
 * Input:  width = screen width in characters
 * Output: none
 *
 * The command line prints below the title bar (its print region starts at
 * row 1), so for the bar the full screen region is set for a moment. The
 * bar's colours are the normal text colours swapped, as the MCP draws it.
 * Region, colours and cursor are put back afterwards.
 */
static void draw_bar( short width )
{
	char text[40];
	t_rect old_region, full;
	t_point cursor;
	short fg = 0, bg = 0;
	uint8_t hour = t_hour;
	int n;

	n = sprintf( text, " RTC %02d%02d-%02d-%02d %02d:%02d:%02d%s ",
		bcd( t_century ), bcd( t_year ), bcd( t_month ), bcd( t_day ),
		bcd( hour & 0x7F ), bcd( t_min ), bcd( t_sec ),
		( t_ctrl & 0x02 ) ? "" : ( hour & 0x80 ) ? "PM" : "  " );

	fflush( stdout );
	sys_txt_get_xy( 0, &cursor );
	sys_txt_get_region( 0, &old_region );
	/* The MCP stores BYTES at these addresses although the Calypsi header says
	   short: the colour index ends up in the first (high) byte of each short. */
	sys_txt_get_color( 0, &fg, &bg );
	fg = ( (uint8_t *)&fg )[0];
	bg = ( (uint8_t *)&bg )[0];

	full.origin.x = 0;
	full.origin.y = 0;
	full.size.width = 0;                     /* 0 x 0 = the whole screen */
	full.size.height = 0;
	sys_txt_set_region( 0, &full );
	sys_txt_set_color( 0, bg, fg );          /* swapped, like the MCP's own bar */
	sys_txt_set_xy( 0, width - n, 0 );
	printf( "%s", text );
	fflush( stdout );

	sys_txt_set_color( 0, fg, bg );
	sys_txt_set_region( 0, &old_region );
	sys_txt_set_xy( 0, cursor.x, cursor.y );
}


/*
 * draw - draw the clock box in the middle of the screen
 *
 * Input:  x, y      = top-left corner of the box
 *         start     = jiffies at the start of the program
 *         last_irqs = interrupt count one second ago (for the rate)
 * Output: none
 */
static void draw( short x, short y, long start, uint32_t irqs_per_s )
{
	long on_ms = ( sys_time_jiffies() - start ) * 100000L / FRAME_HZ_X100;   /* ms since start */
	long on_s  = on_ms / 1000;

	at( x, y + 2 );
	printf( "  RTC time: in the title bar, top right        " );
	at( x, y + 3 );
	printf( "  running for %ld:%02ld:%02ld (jiffy clock)      ",
		on_s / 3600, ( on_s / 60 ) % 60, on_s % 60 );
	at( x, y + 5 );
	printf( "  RTC interrupts %-9lu  %3lu per second   ", (unsigned long)irq_count, (unsigned long)irqs_per_s );
	at( x, y + 6 );
	printf( "  register reads %-9lu  writes %-9lu ", (unsigned long)reg_reads, (unsigned long)reg_writes );
	at( x, y + 7 );
	printf( "  battery valid: %s   control $%02X          ",
		( t_flags_seen & RTC_BVF ) ? "yes" : "no ", t_ctrl );
	fflush( stdout );
}


/*
 * main - install the handler, draw the clock until q is pressed, restore
 *
 * Input:  none
 * Output: 0
 */
int main( void )
{
	t_extent text, pixels;
	p_int_handler old;
	short x, y, i;
	long start, next_second;
	uint32_t irqs_then = 0, irqs_per_s = 0;
	int done = 0;

	sys_txt_get_sizes( 0, &text, &pixels );
	x = ( text.width - BOX_W ) / 2;
	y = ( text.height - BOX_H ) / 2;

	printf( "\x1b[2J" );                     /* clear the screen */
	sys_txt_set_cursor( 0, 0, 0, 0 );        /* cursor off */

	/* the box */
	at( x, y );
	printf( "+--------------------------------------------+" );
	for ( i = 1; i < BOX_H - 1; i++ )
	{
		at( x, y + i );
		printf( "|                                            |" );
	}
	at( x, y + BOX_H - 1 );
	printf( "+--------------------------------------------+" );
	at( x + 2, y );
	printf( " RTC clock test - q = quit " );
	fflush( stdout );

	/* our handler instead of the MCP's (the MCP has already set the RTC to 64 interrupts a second) */
	old = sys_int_register( INT_RTC, rtc_irq );
	sys_int_enable( INT_RTC );

	start = sys_time_jiffies();
	next_second = start + 60;

	while ( !done )
	{
		long now = sys_time_jiffies();

		if ( now >= next_second )
		{
			irqs_per_s = irq_count - irqs_then;
			irqs_then  = irq_count;
			next_second += 60;
		}

		/* about 4 redraws a second is plenty for a clock */
		if ( ( now % 15 ) == 0 )
		{
			draw( x + 1, y, start, irqs_per_s );
			draw_bar( text.width );
			if ( irq_count == 0 && now - start > 120 )
			{
				at( x + 1, y + 8 );
				printf( "  no RTC interrupt in 2 s: is it enabled?   " );
			}
			while ( sys_time_jiffies() == now )
				;                            /* draw only once per jiffy */
		}

		if ( sys_chan_status( 0 ) & KEY_WAITING )
		{
			char c = sys_chan_read_b( 0 );
			if ( c == 'q' || c == 'Q' )
				done = 1;
		}
	}

	/* put the MCP's own handler back */
	sys_int_register( INT_RTC, old );
	sys_txt_set_cursor( 0, 1, 0, 0xB1 );
	at( 0, y + BOX_H + 1 );
	printf( "rtcclock stopped: %lu interrupts, %lu reads, %lu writes\n",
		(unsigned long)irq_count, (unsigned long)reg_reads, (unsigned long)reg_writes );
	return 0;
}
