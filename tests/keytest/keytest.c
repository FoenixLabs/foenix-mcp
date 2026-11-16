/*
 * keytest.c - show the bytes the console hands a program for each key
 *
 * Press keys (arrows, Home, End, Delete, ...): every byte that arrives on
 * the console channel is printed in hex, ESC as "ESC". Press q to stop.
 * Used to check what the command line receives for the arrow keys.
 *
 * "keytest scan" shows the raw scan codes from the keyboard driver instead
 * (make codes, and break codes with bit 7 set, plus the modifier bits in
 * the high byte). Press Esc to stop in that mode.
 */

#include <stdio.h>
#include <string.h>
#include <mcp/syscalls.h>

#define KEY_WAITING   0x04      /* sys_chan_status(): a byte can be read */
#define CON_ECHO_OFF  0x04      /* sys_chan_ioctrl(): the console does not echo keys */
#define CON_ECHO_ON   0x03

/*
 * show_scan_codes - print every raw scan code until Esc is released
 *
 * Input:  none
 * Output: none
 */
static void show_scan_codes( void )
{
	unsigned short sc;

	printf( "\nkeytest: scan codes, Esc stops\n" );
	for ( ;; )
	{
		sc = sys_kbd_scancode();
		if ( sc == 0 )
			continue;
		printf( "%04X%s", (unsigned)sc, ( sc & 0x80 ) ? "\n" : " " );
		if ( ( sc & 0xFF ) == 0x81 )		/* Esc released */
			break;
	}
}

/*
 * main - print every byte (or scan code) from the keyboard
 *
 * Input:  argv[1] = "scan" for scan codes
 * Output: 0
 */
int main( int argc, char **argv )
{
	short c;

	sys_int_enable_all();
	if ( argc > 1 && ( strcmp( argv[1], "scan" ) == 0 || strcmp( argv[1], "SCAN" ) == 0 ) )
	{
		show_scan_codes();
		printf( "\nkeytest: done\n" );
		return 0;
	}
	sys_chan_ioctrl( 0, CON_ECHO_OFF, 0, 0 );
	printf( "\nkeytest: press keys, q stops\n" );

	for ( ;; )
	{
		if ( !( sys_chan_status( 0 ) & KEY_WAITING ) )
			continue;
		c = sys_chan_read_b( 0 ) & 0xFF;
		if ( c == 0x1B )
			printf( "\nESC " );
		else if ( c >= 0x21 && c < 0x7F )
			printf( "%c(%02X) ", (char)c, (unsigned)c );
		else
			printf( "%02X ", (unsigned)c );
		if ( c == 'q' )
			break;
	}

	sys_chan_ioctrl( 0, CON_ECHO_ON, 0, 0 );
	printf( "\nkeytest: done\n" );
	return 0;
}
