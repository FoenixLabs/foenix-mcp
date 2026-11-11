/*
 * scrtest.c - check how the MCP scrolls the main screen (channel B)
 *
 * Bug being chased: printf("%d\n", i) 200 times on an A2560K25 with MCP
 * 2.0.27 scrolls, but the last 3 lines end up below the visible screen.
 *
 * The program first writes down what the MCP believes about screen 0
 * (text size, pixel size, print region) and what the VICKY III registers
 * really say (master control and border). Then it does the printf loop.
 * At the end it writes the results over the top rows of the screen, where
 * they are always visible, and waits for a key.
 *
 * Start it from the MCP (it needs the MCP for printf and the key).
 */

#include <stdio.h>
#include <stdint.h>
#include <mcp/syscalls.h>

/* VICKY III channel B registers (see the K25 hardware reference) */
#define MCR_B       ((volatile uint32_t *)0xFEC80000)   /* master control */
#define BORDER_B    ((volatile uint32_t *)0xFEC80004)   /* border size */

#define LINES       200     /* how many numbered lines to print */


/*
 * show_mcr - print one master control value with the bits that matter
 *
 * Input:  name  = label for the line
 *         mcr   = the value read from MCR_B
 *         border = the value read from BORDER_CTRL
 * Output: none (one line on the screen)
 *
 * Bit 30 is the hi-res DIP switch, bit 8 the mode the hardware really
 * shows (1 = 1280 x 1024, 0 = 1280 x 960). The MCP should set bit 8
 * equal to bit 30; if they differ, the MCP and the screen disagree.
 */
static void show_mcr( const char *name, uint32_t mcr, uint32_t border )
{
	printf( "%s MCR_B=%08lX  DIP_HIRES=%d  MODE_1024=%d  -> %s   BORDER=%08lX (%d lines top/bottom, %s)\n",
		name, (unsigned long)mcr,
		(int)((mcr >> 30) & 1), (int)((mcr >> 8) & 1),
		((mcr >> 8) & 1) ? "1280x1024" : "1280x960",
		(unsigned long)border, (int)((border >> 16) & 0x3f),
		(border & 1) ? "on" : "off" );
}


/*
 * main - run the scroll test
 *
 * Input:  none
 * Output: 0
 */
int main( void )
{
	t_extent text_size, pixel_size;
	t_rect   region;
	t_point  cursor;
	uint32_t mcr_start, border_start, mcr_end, border_end;
	int      i;

	/* What the hardware and the MCP say before we print anything */
	mcr_start    = *MCR_B;
	border_start = *BORDER_B;
	sys_txt_get_sizes( 0, &text_size, &pixel_size );
	sys_txt_get_region( 0, &region );

	/* The test itself: exactly what the user's program does */
	for ( i = 1; i <= LINES; i++ )
	{
		printf( "%d\n", i );
	}
	fflush( stdout );

	/* Where did the cursor end up, and did anything change the mode? */
	sys_txt_get_xy( 0, &cursor );
	mcr_end    = *MCR_B;
	border_end = *BORDER_B;

	/* Write the report over the top rows, which are always visible.
	   The cursor row is relative to the region, so the last number
	   (LINES) sits on row cursor.y - 1. */
	sys_txt_set_xy( 0, 0, 0 );
	printf( "---- scrtest: MCP screen 0 report -------------------------------------------\n" );
	printf( "MCP pixel size %d x %d, text size %d cols x %d rows\n",
		pixel_size.width, pixel_size.height, text_size.width, text_size.height );
	printf( "MCP region origin (%d,%d) size %d x %d\n",
		region.origin.x, region.origin.y, region.size.width, region.size.height );
	show_mcr( "start:", mcr_start, border_start );
	show_mcr( "end:  ", mcr_end, border_end );
	printf( "cursor after the loop: row %d (line %d is on row %d)\n",
		cursor.y, LINES, cursor.y - 1 );
	printf( "Note the last number at the bottom of the screen, then press a key.\n" );
	printf( "------------------------------------------------------------------------------\n" );
	fflush( stdout );

	sys_chan_read_b( 0 );
	return 0;
}
