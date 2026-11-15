/*
 * rtctsr.c - install (or remove) a resident clock in the MCP title bar
 *
 * Run once: a small RTC interrupt handler (tsr_handler.s) is copied into
 * memory reserved with sys_mem_reserve and put on the RTC periodic interrupt
 * (MCP interrupt INT_RTC, $1F: GAVIN bit 31, level 3, vector $4F; 64 times a
 * second). The program then ends; the handler stays and shows the RTC time in
 * the right-hand end of the title bar while you go on working.
 *
 * Run again: the handler is found and removed, and the MCP's own RTC handler
 * is put back. The reserved memory stays reserved (the MCP cannot give it
 * back), which only costs a few dozen bytes.
 *
 * Note: the MCP's program loader does not look at the reserved top of memory,
 * so a program that uses that memory anyway overwrites the handler, and the
 * next RTC interrupt then crashes. Remove the clock before running such a
 * program. On the K25 the MCP 2.0.31 top of memory is $00BD0000, in video
 * RAM B, so the handler ends up there.
 */

#include <stdio.h>
#include <stdint.h>
#include <mcp/syscalls.h>
#include <mcp/interrupt.h>

/* The resident block in tsr_handler.s */
extern char tsr_start[];
extern char tsr_handler[];
extern char tsr_end[];

#define TSR_MAGIC    0x52545352UL                 /* "RTSR" at the start of the block */
#define RTC_FLAGS    ((volatile uint8_t *)0xFEC0008D)
#define TEXT_B       ((volatile uint8_t *)0xFECA0000)
#define TIME_COLUMN  148                          /* same as in tsr_handler.s */
#define TIME_WIDTH   9                            /* " hh:mm:ss" */


/*
 * ack_only - stand-in RTC handler, used for a moment while looking up the
 *            current handler
 *
 * Input:  none
 * Output: none (acknowledges the interrupt)
 */
static void ack_only( void )
{
	(void)*RTC_FLAGS;
}


/*
 * current_rtc_handler - find out which handler is on the RTC interrupt now
 *
 * Input:  none
 * Output: the current handler (it stays installed)
 *
 * The MCP has no call to ask for a handler, but sys_int_register returns
 * the previous one: set a stand-in, then put the old one straight back.
 */
static p_int_handler current_rtc_handler( void )
{
	p_int_handler cur = sys_int_register( INT_RTC, ack_only );
	sys_int_register( INT_RTC, cur );
	return cur;
}


/*
 * clock_remove - take the resident clock off the RTC interrupt
 *
 * Input:  block = start of the resident block (where "RTSR" is)
 * Output: none
 */
static void clock_remove( uint32_t *block )
{
	int i;

	sys_int_register( INT_RTC, (p_int_handler)block[1] );   /* the MCP's handler again */
	block[0] = 0;                                            /* not found again next time */
	for ( i = 0; i < TIME_WIDTH; i++ )
		TEXT_B[TIME_COLUMN + i] = ' ';                       /* clear the time from the bar */
	printf( "rtctsr: clock removed, the MCP's RTC handler is back\n" );
	printf( "        (the %lu bytes at $%08lX stay reserved)\n",
		(unsigned long)( tsr_end - tsr_start ), (unsigned long)block );
}


/*
 * clock_install - copy the resident block to reserved memory and put it on the
 *           RTC interrupt
 *
 * Input:  cur = the handler on the RTC interrupt now (the MCP's)
 * Output: 0 on success, -1 when no memory could be reserved
 */
static int clock_install( p_int_handler cur )
{
	uint32_t size = (uint32_t)( tsr_end - tsr_start );
	uint32_t top_before = sys_mem_get_ramtop();
	uint32_t block = sys_mem_reserve( ( size + 15 ) & ~15UL );   /* keep 16-byte steps */
	char *src = tsr_start, *dst = (char *)block;
	uint32_t i;

	if ( block < 0x10000UL || block >= top_before )
	{
		printf( "rtctsr: could not reserve memory (top of memory $%08lX, got $%08lX)\n",
			(unsigned long)top_before, (unsigned long)block );
		return -1;
	}

	for ( i = 0; i < size; i++ )
		dst[i] = src[i];
	( (uint32_t *)block )[1] = (uint32_t)cur;               /* remember the MCP's handler */

	sys_int_register( INT_RTC, (p_int_handler)( block + ( tsr_handler - tsr_start ) ) );
	sys_int_enable( INT_RTC );

	printf( "rtctsr: clock installed in the title bar (RTC interrupt $%02X)\n", INT_RTC );
	printf( "        %lu bytes at $%08lX, top of memory $%08lX -> $%08lX\n",
		(unsigned long)size, (unsigned long)block,
		(unsigned long)top_before, (unsigned long)sys_mem_get_ramtop() );
	printf( "        run rtctsr again to remove it\n" );
	return 0;
}


/*
 * main - install the clock, or remove it when it is already there
 *
 * Input:  none
 * Output: 0, or -1 when installing failed
 */
int main( void )
{
	p_int_handler cur = current_rtc_handler();
	uint32_t *block = (uint32_t *)( (char *)cur - ( tsr_handler - tsr_start ) );

	if ( cur && block[0] == TSR_MAGIC )
	{
		clock_remove( block );
		return 0;
	}
	return clock_install( cur );
}
