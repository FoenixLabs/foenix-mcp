/***********************************************************************
 * FELINC.H - everything a program needs to use FELINC, the Foenix Labs
 * Ethernet Library in C. This is the only FELINC header a program
 * includes: the other headers belong to the modules inside FELINC and
 * may change between versions. See felinc.c for how it works and
 * main.c for an example program.
 *
 * Byte order: IP addresses (uint32_t) and port numbers are always in
 * network byte order, "most significant byte first". That is also the
 * 68040's own order, so nothing needs converting: port 1234 is simply
 * 1234, and FELINC_IP(10,77,77,1) builds the address 10.77.77.1. To
 * print an address, take its bytes from first to last.
 *
 * Part of FELINC, the Foenix Labs Ethernet Library in C. (c) R. Kint
 **********************************************************************/
#ifndef __FELINC_H__
#define __FELINC_H__

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

/***********************************************************************
 * Socket types and constants, with their POSIX names, so socket code
 * looks familiar. FELINC is NOT a POSIX sockets implementation: use
 * the felinc_ calls below. Only what FELINC uses is defined here.
 **********************************************************************/

/* The Calypsi C library has no ssize_t: a size, or -1 for an error */
typedef long long ssize_t;

/* Domain (felinc_socket's 'domain'): only IPv4 is supported */
#define AF_UNSPEC		0
#define AF_INET			2	/* IPv4 */

/* Socket types (felinc_socket's 'type') */
#define SOCK_STREAM		0	/* reliable byte stream (TCP) */
#define SOCK_DGRAM		1	/* datagrams (UDP) */

/* Protocols (felinc_socket's 'protocol') */
#define IPPROTO_IP		0
#define IPPROTO_TCP		6
#define IPPROTO_UDP		17

typedef uint32_t socklen_t;
typedef uint16_t sa_family_t;
typedef uint16_t in_port_t;

struct in_addr
{
	uint32_t s_addr;			/* IPv4 address, network byte order */
};

/* A general socket address, as felinc_recvfrom/felinc_sendto take it.
 * For IPv4 fill a struct sockaddr_in instead and cast its address:
 * both are 16 bytes and line up.
 */
struct sockaddr
{
	sa_family_t	sa_family;		/* AF_INET */
	char		sa_data[14];	/* port (2), address (4), zeroes (8) */
};

/* An IPv4 socket address */
struct sockaddr_in
{
	sa_family_t		sin_family;	/* AF_INET */
	in_port_t		sin_port;	/* port number */
	struct in_addr	sin_addr;	/* IPv4 address */
	unsigned char	sin_zero[8];/* padding, zero */
};

/***********************************************************************
 * The FELINC interface
 **********************************************************************/

#define FELINC_MAX_SOCKETS	8	/* sockets that can be open at the same time */

/* An IPv4 address from its four numbers: FELINC_IP(10,77,77,1) */
#define FELINC_IP(a, b, c, d)	(((uint32_t)(a) << 24) | ((uint32_t)(b) << 16) | \
								 ((uint32_t)(c) << 8) | (uint32_t)(d))

/* For felinc_bind(): accept datagrams sent to any of our addresses */
#define FELINC_ANY_IP			FELINC_IP(0, 0, 0, 0)

/* Largest UDP payload in one Ethernet frame: 1500 bytes minus the IPv4
 * header (20) and the UDP header (8). FELINC does not split datagrams
 * into IP fragments.
 */
#define FELINC_UDP_MAX		1472

/* felinc_recv(), felinc_send(): the TCP connection has ended. Not 0 as in POSIX,
 * because here 0 means "nothing has arrived yet".
 */
#define FELINC_CLOSED		(-2)

/* Results of felinc_connect() */
#define FELINC_CONNECT_DONE		0		/* connected */
#define FELINC_CONNECT_BUSY		1		/* still waiting for the server: call again later */
#define FELINC_CONNECT_REFUSED	(-2)	/* nobody listens on that port */
#define FELINC_CONNECT_TIMEOUT	(-3)	/* the server did not answer */

/* Results of felinc_resolve() */
#define FELINC_RESOLVE_DONE		0		/* the address is known */
#define FELINC_RESOLVE_BUSY		1		/* still asking: call again later */
#define FELINC_RESOLVE_FAILED	(-1)	/* no address (see the message on screen) */

/* Counters of packets FELINC dropped, see felinc_print_status() */
typedef struct felinc_stats
{
	uint32_t ip_bad;			/* bad IPv4 header, options or fragments (not supported) */
	uint32_t ip_not_for_us;		/* IPv4 for another address */
	uint32_t udp_bad;			/* bad UDP length or checksum */
	uint32_t udp_no_socket;		/* no socket bound to the port */
	uint32_t udp_socket_full;	/* receive buffer of the socket full */
	uint32_t tx_dropped;		/* datagrams the stack could not send */
	uint32_t tcp_bad;			/* bad TCP length or checksum */
	uint32_t tcp_no_socket;		/* TCP to a port nobody listens on (answered with a reset) */
} felinc_stats;

/* Where FELINC gets memory (set before the start), sends its text, and
 * reports an error in itself (NULL = malloc/free, stdout, and stop the
 * program, as in a normal program) */
bool felinc_set_alloc( void *(*alloc)( size_t size ), void (*release)( void *block ) );
void felinc_set_print( void (*print)( const char *text ) );
void felinc_set_fatal( void (*fatal)( const char *reason ) );

/* Start and stop */
bool felinc_init( char *mac_string, bool dhcp_flag,
				char *ip4_string, char *mask_string, char *gateway_string );
void felinc_shutdown( void );

/* Call often from the main loop */
ssize_t felinc_poll( void );

/* Sockets */
int felinc_socket( int domain, int type, int protocol, size_t bufsize );
int felinc_bind( int socket, uint32_t ip_address, uint16_t port );
int felinc_close( int socket );

/* UDP */
ssize_t felinc_recvfrom( int socket, void *buffer, size_t buflen,
						int flags, struct sockaddr *src_addr );
ssize_t felinc_sendto( int socket, const void *buffer, size_t buflen,
						int flags, struct sockaddr *dest_addr );

/* TCP */
int felinc_listen( int socket, int backlog );
int felinc_accept( int socket, struct sockaddr *peer_addr );
int felinc_connect( int socket, const struct sockaddr *peer_addr );
ssize_t felinc_recv( int socket, void *buffer, size_t buflen, int flags );
ssize_t felinc_send( int socket, const void *buffer, size_t buflen, int flags );

/* Names and addresses */
int16_t felinc_resolve( const char *name, uint32_t *ip );
uint32_t felinc_get_ip( void );
uint32_t felinc_get_ntp_server( void );
void felinc_get_mac( uint8_t *mac_address );

/* Status */
const felinc_stats *felinc_get_stats( void );
void felinc_print_status( void );
void felinc_print_tcp( void );
void felinc_print_arp( void );
void felinc_print_dns( void );

#endif
