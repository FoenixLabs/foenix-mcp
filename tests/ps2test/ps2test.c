/*
 * ps2test.c - check the PS/2 keyboard of the A2560K without the MCP's keyboard driver
 *
 * The MCP sets up the SuperIO keyboard controller (8042) at boot and reads the PS/2 keyboard from
 * interrupt 16 (GAVIN group 1, bit 0). When no PS/2 key arrives, this program shows where it stops:
 *
 *   1. the controller status and the GAVIN registers of interrupt 16 (mask, pending, edge, polarity)
 *   2. with interrupts 16 and 18 masked (so the MCP does not take the bytes), polled:
 *      - the controller's own command byte
 *      - ECHO ($EE), RESET ($FF) and ENABLE ($F4) to the device on port 1 (keyboard port), then
 *        the same to the device on port 2 (mouse port). A keyboard answers ECHO with $EE, RESET
 *        with $FA and then $AA after its self-test (its LEDs flash), ENABLE with $FA.
 *        The A2560K has ONE PS/2 socket: this shows which port of the 8042 it is wired to.
 *   3. 10 seconds of raw bytes, each marked K (port 1) or M (port 2): press keys on the keyboard
 *   4. interrupts 16 and 18 back as they were
 *
 * Start it from the MCP; it ends by itself.
 */

#include <stdio.h>
#include <stdint.h>
#include <mcp/syscalls.h>

#define PS2_DATA    ((volatile uint8_t *)0xFEC02060)    /* 8042 data */
#define PS2_STAT    ((volatile uint8_t *)0xFEC02064)    /* 8042 status (read) / command (write) */
#define ST_OBF      0x01                                /* output buffer full: a byte to read */
#define ST_IBF      0x02                                /* input buffer full: busy */

#define PENDING_G1  ((volatile uint16_t *)0xFEC00102)   /* GAVIN group 1 = interrupts 16-31 */
#define POL_G1      ((volatile uint16_t *)0xFEC0010A)
#define EDGE_G1     ((volatile uint16_t *)0xFEC00112)
#define MASK_G1     ((volatile uint16_t *)0xFEC0011A)
#define INT16       0x0001                              /* interrupt 16 = PS/2 keyboard */
#define INT18       0x0004                              /* interrupt 18 = PS/2 mouse */
#define ST_AUX      0x20                                /* the waiting byte comes from port 2 */

/*
 * wait_jiffies - wait a number of jiffies (the MCP's 60 Hz tick)
 *
 * Input:  n = how many
 * Output: none
 */
static void wait_jiffies( long n )
{
	long end = sys_time_jiffies() + n;
	while ( sys_time_jiffies() < end )
		;
}

/*
 * read_byte - wait for a byte from the controller
 *
 * Input:  jiffies = how long to wait at most
 * Output: the byte, or -1 if none came
 */
static int read_byte( long jiffies )
{
	long end = sys_time_jiffies() + jiffies;
	do {
		uint8_t st = *PS2_STAT;
		if ( st & ST_OBF )
			return *PS2_DATA | ( ( st & ST_AUX ) ? 0x100 : 0 );    /* 0x100 = from port 2 */
	} while ( sys_time_jiffies() < end );
	return -1;
}

/*
 * write_to - send a byte to the controller (command port) or the keyboard (data port)
 *
 * Input:  port = PS2_STAT for a controller command, PS2_DATA for the keyboard; b = the byte
 * Output: 0, or -1 if the controller stayed busy
 */
static int write_to( volatile uint8_t *port, uint8_t b )
{
	long end = sys_time_jiffies() + 30;
	while ( *PS2_STAT & ST_IBF ) {
		if ( sys_time_jiffies() >= end )
			return -1;
	}
	*port = b;
	return 0;
}

/*
 * kbd_command - send a byte to the keyboard and print all bytes that come back
 *
 * Input:  name = what it is, cmd = the byte, jiffies = how long to collect answers
 * Output: none (one line)
 */
static void kbd_command( int port, const char *name, uint8_t cmd, long jiffies )
{
	int b, n = 0;

	printf( "  port %d %-6s $%02X ->", port, name, cmd );
	if ( port == 2 && write_to( PS2_STAT, 0xD4 ) ) {        /* $D4: the next byte goes to port 2 */
		printf( " controller busy, not sent\n" );
		return;
	}
	if ( write_to( PS2_DATA, cmd ) ) {
		printf( " controller busy, not sent\n" );
		return;
	}
	while ( ( b = read_byte( jiffies ) ) >= 0 ) {
		printf( " %c$%02X", ( b & 0x100 ) ? 'M' : 'K', b & 0xFF );
		n++;
	}
	printf( n ? "\n" : " no answer\n" );
}

int main( int argc, char *argv[] )
{
	uint16_t mask_before;
	int b;
	long end;

	(void)argc; (void)argv;

	/* 1. how things are */
	printf( "\nps2test: PS/2 keyboard check\n" );
	printf( "  8042 status $%02X (bit 0 = byte waiting, bit 1 = busy, bit 2 = self-test passed)\n", *PS2_STAT );
	printf( "  GAVIN group 1: mask $%04X pending $%04X edge $%04X polarity $%04X\n",
		*MASK_G1, *PENDING_G1, *EDGE_G1, *POL_G1 );
	printf( "  interrupt 16 (PS/2 keyboard) is %s, %s\n",
		( *MASK_G1 & INT16 ) ? "MASKED" : "enabled", ( *PENDING_G1 & INT16 ) ? "PENDING" : "not pending" );

	/* 2. talk to the controller and the keyboard, polled */
	mask_before = *MASK_G1;
	*MASK_G1 = mask_before | INT16 | INT18;
	while ( ( b = read_byte( 2 ) ) >= 0 )
		printf( "  left in the controller: %c$%02X\n", ( b & 0x100 ) ? 'M' : 'K', b & 0xFF );

	if ( write_to( PS2_STAT, 0x20 ) == 0 ) {
		b = read_byte( 30 );
		if ( b >= 0 )
			printf( "  controller command byte $%02X (bit 0 = kbd interrupt, bit 4 = kbd off, bit 6 = translate)\n", b & 0xFF );
		else
			printf( "  controller command byte: no answer\n" );
	} else {
		printf( "  controller busy\n" );
	}

	kbd_command( 1, "ECHO", 0xEE, 30 );
	kbd_command( 1, "RESET", 0xFF, 120 );
	kbd_command( 1, "ENABLE", 0xF4, 30 );
	kbd_command( 2, "ECHO", 0xEE, 30 );
	kbd_command( 2, "RESET", 0xFF, 120 );
	kbd_command( 2, "ENABLE", 0xF4, 30 );

	/* 3. raw bytes */
	printf( "  Press keys on the PS/2 keyboard now (10 seconds; K = port 1, M = port 2):\n  " );
	end = sys_time_jiffies() + 600;
	while ( sys_time_jiffies() < end ) {
		b = read_byte( 0 );
		if ( b >= 0 )
			printf( "%c$%02X ", ( b & 0x100 ) ? 'M' : 'K', b & 0xFF );
	}
	printf( "\n" );

	/* 4. back as it was */
	*PENDING_G1 = INT16 | INT18;    /* write 1 = clear */
	*MASK_G1 = mask_before;
	printf( "  done; interrupts 16 and 18 back as they were\n" );
	wait_jiffies( 1 );
	return 0;
}
