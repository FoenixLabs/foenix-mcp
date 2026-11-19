/*
 * topbartest.c - check the MCP 2.1.0 top bar from a program
 *
 * What it checks:
 *   1. the screen as a program sees it: one row shorter than the real screen (SYS INFO shows the
 *      real size), and the region starts at row 0 (= the screen's row 1, below the bar)
 *   2. clear screen and "home" stay below the bar, also with a "whole screen" region
 *   3. sys_txt_topbar: a short text, then a text too long for the USER part (cut off on the right),
 *      the number of characters that fit, and clearing it
 *   4. the APP part shows the program's path (without .pgz) while it runs; when it ends the MCP
 *      clears the USER part and shows the folder again
 *
 * Start it from the MCP; press a key to go to the next step.
 */

#include <stdio.h>
#include <stdint.h>
#include <mcp/syscalls.h>

#define KFN_TXT_TOPBAR  0x73        /* 2.1.0; not in the Calypsi SDK yet */

/*
 * topbar - the sys_txt_topbar system call (not in the Calypsi SDK yet)
 *
 * Input:  screen = the text screen, text = the text for the USER part (0 = clear)
 * Output: how many characters fit in the USER part, or negative for a bad screen
 */
static short topbar( short screen, const char *text )
{
	t_syscall_params params = {.arg1 = screen, .arg2 = (int32_t)text};
	return (short)syscall( KFN_TXT_TOPBAR, &params );
}

/*
 * wait_key - print a prompt and wait for a key
 *
 * Input:  what = the next step
 * Output: none
 */
static void wait_key( const char *what )
{
	printf( "\nPress a key for: %s\n", what );
	sys_chan_read_b( 0 );
}

int main( int argc, char *argv[] )
{
	short screen = sys_chan_device( 0 );
	t_extent text_size, pixel_size;
	t_rect region, full = {{0, 0}, {0, 0}};
	short fit;
	int i;

	(void)argc; (void)argv;

	/* 1. what the program sees */
	sys_txt_get_sizes( screen, &text_size, &pixel_size );
	sys_txt_get_region( screen, &region );
	printf( "\ntopbartest: screen %d\n", screen );
	printf( "  size as a program sees it: %d x %d characters (SYS INFO: one row more)\n",
		text_size.width, text_size.height );
	printf( "  region at the start: x=%d y=%d  %d x %d\n",
		region.origin.x, region.origin.y, region.size.width, region.size.height );

	/* 3. the USER part */
	fit = topbar( screen, "topbartest: hello from the USER part" );
	printf( "  sys_txt_topbar: %d characters fit in the USER part\n", fit );
	wait_key( "a text that is too long for the USER part" );

	fit = topbar( screen, "This text is much too long for the USER part of the top bar, so the MCP "
		"must cut it off on the right instead of writing over the separator and the clock" );
	printf( "  long text: %d characters fit (the end must be cut off before the separator)\n", fit );
	wait_key( "a 'whole screen' region, clear screen and 100 lines (the bar must stay)" );

	/* 2. clear and scroll below the bar */
	sys_txt_set_region( screen, &full );
	sys_txt_get_region( screen, &region );
	printf( "\x1b[2J\x1b[H" );
	printf( "Row 0 of the program = the line below the bar. Region now: x=%d y=%d  %d x %d\n",
		region.origin.x, region.origin.y, region.size.width, region.size.height );
	for ( i = 1; i <= 100; i++ )
		printf( "line %d\n", i );
	wait_key( "clearing the USER part" );

	topbar( screen, 0 );
	printf( "  USER part cleared\n" );
	topbar( screen, "set again: the MCP must clear this when the program ends" );
	wait_key( "the end (the bar must show the folder again, USER part empty)" );

	return 0;
}
