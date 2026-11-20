/***********************************************************************
 * TCP.H - TCP for FELINC (see tcp.c)
 *
 * Part of FELINC, the Foenix Labs Ethernet Library in C. (c) R. Kint
 **********************************************************************/
#ifndef __TCP_H__
#define __TCP_H__

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include "felinc.h"	/* ssize_t */
#include "ip4.h"

/* The control bits in the 'flags' byte of the TCP header */
#define TCP_FIN		0x01	/* the sender has no more data */
#define TCP_SYN		0x02	/* start of a connection: synchronise sequence numbers */
#define TCP_RST		0x04	/* reset: the connection does not exist (any more) */
#define TCP_PSH		0x08	/* push: hand the data to the program at once */
#define TCP_ACK		0x10	/* the 'ack' field is valid */
#define TCP_URG		0x20	/* urgent data (not supported) */

/* TCP header without options (20 bytes). On the 68040 (big-endian)
 * the fields are already in network byte order.
 */
typedef struct tcp_header
{
	uint16_t src_port;		/* sender's port */
	uint16_t dest_port;		/* receiver's port */
	uint32_t seq;			/* sequence number of the first data byte */
	uint32_t ack;			/* next sequence number the sender expects (if TCP_ACK) */
	uint8_t  data_offset;	/* upper 4 bits: header length in 32-bit words */
	uint8_t  flags;			/* TCP_FIN, TCP_SYN, ... */
	uint16_t window;		/* bytes the sender can still receive */
	uint16_t checksum;		/* over a pseudo header and the whole segment */
	uint16_t urgent;		/* urgent pointer (not used) */
} tcp_header;

/* Results of tcp_receive(), so felinc.c can count them */
#define TCP_RECEIVED	0		/* the segment was handled */
#define TCP_BAD			(-1)	/* bad length or checksum: dropped */
#define TCP_NO_SOCKET	(-2)	/* nobody listens on the port: answered with a reset */

void tcp_init( void );
void tcp_shutdown( void );
void tcp_poll( uint32_t now );
void tcp_send_pending( uint32_t now );
int16_t tcp_receive( ip4_pkt *ip4 );

/* Used by the socket calls in felinc.c. A connection is known by its
 * number, as returned by tcp_listen() or tcp_accept().
 */
/* Results of tcp_connect_result(); the same numbers as the
 * FELINC_CONNECT_ results in felinc.h
 */
#define TCP_CONNECT_BUSY		1
#define TCP_CONNECT_DONE		0
#define TCP_CONNECT_REFUSED		(-2)
#define TCP_CONNECT_TIMEOUT		(-3)

int16_t tcp_listen( uint16_t port, uint16_t backlog, uint16_t window );
int16_t tcp_connect( uint16_t my_port, uint32_t his_ip, uint16_t his_port, uint16_t bufsize );
int16_t tcp_connect_result( int16_t conn );
bool tcp_can_accept( int16_t listener );
int16_t tcp_accept( int16_t listener, uint32_t *his_ip, uint16_t *his_port );
bool tcp_recv_done( int16_t conn );
size_t tcp_read( int16_t conn, void *buffer, size_t buflen );
ssize_t tcp_write( int16_t conn, const void *data, size_t length );
void tcp_release( int16_t conn );
bool tcp_closing( void );
void tcp_print_status( void );

#endif
