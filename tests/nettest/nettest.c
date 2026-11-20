/*
 * nettest.c - check the MCP's network system calls (2.1.0, step M3a)
 *
 * Programs use the MCP's FELINC through system calls 0x80 - 0x8F. Normally FELINC's stub library
 * (felinc_mcp.a) makes those calls; this program makes them itself, so the MCP side can be
 * tested on its own:
 *
 *   1. KFN_NET_INIT starts the network if it is off, and the program polls until it has an
 *      address (KFN_NET_POLL, KFN_NET_INFO)
 *   2. its MAC address and FELINC's status (KFN_NET_INFO, KFN_NET_PRINT)
 *   3. a name looked up through DNS (KFN_NET_RESOLVE)
 *   4. a UDP echo on port 2560 (KFN_NET_SOCKET, BIND, RECVFROM, SENDTO) until a key is pressed:
 *      from another computer:  echo hello | nc -u -w1 <address> 2560
 *   5. the end: "nettest close" closes the socket itself; plain "nettest" leaves it open, so the
 *      MCP must close it when the program ends. Start nettest twice: the second bind on port
 *      2560 only works if the first socket was closed.
 */

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <mcp/syscalls.h>
#include "felinc.h"

#define KFN_NET_INIT        0x80
#define KFN_NET_POLL        0x82
#define KFN_NET_SOCKET      0x83
#define KFN_NET_BIND        0x84
#define KFN_NET_CLOSE       0x85
#define KFN_NET_RECVFROM    0x86
#define KFN_NET_SENDTO      0x87
#define KFN_NET_RESOLVE     0x8D
#define KFN_NET_INFO        0x8E
#define KFN_NET_PRINT       0x8F

#define ECHO_PORT   2560

/*
 * net - make one network system call
 *
 * Input:  function = KFN_NET_..., a1-a5 = its arguments
 * Output: the MCP's result
 */
static int32_t net( int32_t function, int32_t a1, int32_t a2, int32_t a3, int32_t a4, int32_t a5 )
{
	t_syscall_params params = {.arg1 = a1, .arg2 = a2, .arg3 = a3, .arg4 = a4, .arg5 = a5};
	return syscall( function, &params );
}

/*
 * print_ip - print an IPv4 address
 *
 * Input:  ip = the address (a.b.c.d with a in the top byte)
 * Output: none
 */
static void print_ip( uint32_t ip )
{
	printf( "%u.%u.%u.%u", (unsigned)(ip >> 24), (unsigned)((ip >> 16) & 0xFF),
		(unsigned)((ip >> 8) & 0xFF), (unsigned)(ip & 0xFF) );
}

int main( int argc, char *argv[] )
{
	uint8_t mac[6];
	uint32_t ip = 0;
	long until;
	int16_t r;
	int32_t sock, n;
	char buffer[600];
	struct sockaddr_in me, peer;
	int close_it = (argc > 1 && strcmp( argv[1], "close" ) == 0);

	/* 1. start and get an address */
	printf( "\nnettest: KFN_NET_INIT -> %ld\n", (long)net( KFN_NET_INIT, 0, 1, 0, 0, 0 ) );
	until = sys_time_jiffies() + 15 * 60;
	while( (ip = (uint32_t)net( KFN_NET_INFO, 0, 0, 0, 0, 0 )) == 0 && sys_time_jiffies() < until )
		net( KFN_NET_POLL, 0, 0, 0, 0, 0 );
	printf( "  address: " );
	if( ip == 0 ) { printf( "none after 15 s\n" ); return 1; }
	print_ip( ip );

	/* 2. MAC and status */
	net( KFN_NET_INFO, 2, (int32_t)mac, 0, 0, 0 );
	printf( ", MAC %02X:%02X:%02X:%02X:%02X:%02X\n  KFN_NET_PRINT 0:\n",
		mac[0], mac[1], mac[2], mac[3], mac[4], mac[5] );
	net( KFN_NET_PRINT, 0, 0, 0, 0, 0 );

	/* 3. DNS */
	until = sys_time_jiffies() + 10 * 60;
	do {
		r = (int16_t)net( KFN_NET_RESOLVE, (int32_t)"gitlab.com", (int32_t)&ip, 0, 0, 0 );
		net( KFN_NET_POLL, 0, 0, 0, 0, 0 );
	} while( r == FELINC_RESOLVE_BUSY && sys_time_jiffies() < until );
	printf( "  gitlab.com: " );
	if( r == FELINC_RESOLVE_DONE ) print_ip( ip ); else printf( "not found (%d)", r );
	printf( "\n" );

	/* 4. UDP echo */
	sock = net( KFN_NET_SOCKET, AF_INET, SOCK_DGRAM, IPPROTO_UDP, 2048, 0 );
	n = net( KFN_NET_BIND, sock, FELINC_ANY_IP, ECHO_PORT, 0, 0 );
	printf( "  socket %ld, bind port %d -> %ld%s\n", (long)sock, ECHO_PORT, (long)n,
		(n < 0) ? "  (port still in use: the MCP did not close the last socket?)" : "" );
	if( sock < 0 || n < 0 ) return 1;
	printf( "  UDP echo on port %d; press a key to stop\n", ECHO_PORT );
	memset( &me, 0, sizeof(me) );
	while( !(sys_chan_status( 0 ) & 0x04) )			/* 0x04: a key is waiting */
	{
		net( KFN_NET_POLL, 0, 0, 0, 0, 0 );
		n = net( KFN_NET_RECVFROM, sock, (int32_t)buffer, sizeof(buffer), 0, (int32_t)&peer );
		if( n > 0 )
		{
			net( KFN_NET_SENDTO, sock, (int32_t)buffer, n, 0, (int32_t)&peer );
			printf( "  echoed %ld bytes to ", (long)n );
			print_ip( peer.sin_addr.s_addr );
			printf( "\n" );
		}
	}
	sys_chan_read_b( 0 );

	/* 5. the end */
	if( close_it )
	{
		printf( "  KFN_NET_CLOSE -> %ld\n", (long)net( KFN_NET_CLOSE, sock, 0, 0, 0, 0 ) );
	}
	else
	{
		printf( "  socket left open: the MCP must close it now\n" );
	}
	return 0;
}
