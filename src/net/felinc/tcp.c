/***********************************************************************
 * TCP.C - TCP for FELINC (RFC 793 / RFC 9293)
 *
 * TCP is built step by step. Done so far:
 * 1. Received segments are checked (length, checksum). A segment for a
 *    port nobody listens on is answered with a reset (RST), so the
 *    other side hears "connection refused" at once.
 * 2. Accepting connections: a socket listens on a port, the three-way
 *    handshake (SYN, SYN-ACK, ACK) makes a connection, and the program
 *    takes it with felinc_accept(). A reset from the other side ends
 *    the connection.
 * 3. Receiving data: data that arrives in order is stored in the
 *    connection's receive buffer and acknowledged; felinc_recv() takes
 *    it from there. The window we advertise is the free room in that
 *    buffer, so the other side never sends more than fits. Segments
 *    that arrive out of order are dropped: the other side sends them
 *    again.
 * 4. Sending data: felinc_send() puts data in the connection's send
 *    buffer, and felinc_poll() sends it, as far as the other side's
 *    window and our congestion window allow. Sent data stays in the
 *    buffer until it is acknowledged. When no acknowledgement comes in
 *    time, sending starts again from the oldest unacknowledged byte.
 * 5. Closing: each side ends its half of the connection with a FIN
 *    (after its last data), and the other side acknowledges it. When
 *    the other side sends its FIN, felinc_recv() reports the end once
 *    all data is read. felinc_close() sends what is still in the send
 *    buffer, then our FIN; the table entry stays until the closing is
 *    done (the socket is free at once).
 * 6. Connecting: felinc_connect() sends a SYN to a server and the
 *    connection waits in SYN_SENT for the SYN-ACK; the SYN is sent
 *    again when no answer comes. A reset means "connection refused".
 *
 * Every connection has an entry in a fixed table (no memory is
 * allocated while running). The table is walked with a pointer, not
 * with a 16-bit index: Calypsi 5.18 miscompiles conns[i] in such loops
 * (see "A note on the Calypsi compiler" in README.md). An entry is "owned" by a socket in felinc.c
 * (a listening socket, or one returned by felinc_accept): an owned
 * entry is only freed when its socket is closed, so the program can
 * still see that the connection ended.
 *
 * Part of FELINC, the Foenix Labs Ethernet Library in C. (c) R. Kint
 **********************************************************************/
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "felinc.h"
#include "cbuf.h"
#include "ip4.h"
#include "tcp.h"
#include "utils.h"

/* Room for every socket, plus connections that are still being set up
 * or waiting to be accepted
 */
#define TCP_MAX_CONNECTIONS		(2 * FELINC_MAX_SOCKETS)

/* Largest data part we accept in one segment: an Ethernet frame of
 * 1500 bytes minus the IPv4 and TCP headers. Sent to the other side
 * as the MSS option in our SYN-ACK.
 */
#define TCP_MY_MSS			(MTU - sizeof(ip4_header) - sizeof(tcp_header))

/* MSS to assume when the other side sends no MSS option (RFC 9293) */
#define TCP_DEFAULT_MSS		536

/* A connection that never finishes its handshake is dropped after this
 * long (in jiffies: 10 s), so a lost ACK cannot keep a table entry
 * busy forever.
 */
#define TCP_HANDSHAKE_TIMEOUT	(10 * 60)

/* A connection we start (felinc_connect) gives up when the server does
 * not answer for this long (20 s). Meanwhile the SYN is sent again
 * after 1, 2, 4 and 8 s.
 */
#define TCP_CONNECT_WAIT		(20 * 60)

/* Our port for a connection we start, when the program did not bind
 * one: taken from the range RFC 6335 leaves for this ("ephemeral")
 */
#define TCP_FIRST_EPHEMERAL		49152
#define TCP_LAST_EPHEMERAL		65535

/* After both FINs, the side that closed first waits in TIME_WAIT, so a
 * lost last ACK can still be sent again when the other side repeats
 * its FIN. RFC 9293 says 4 minutes; with a table of 16 entries we keep
 * it at 10 s, which is plenty on a local network.
 */
#define TCP_TIME_WAIT_TIMEOUT	(10 * 60)

/* We closed and the other side acknowledged our FIN, but never sends
 * its own: give up after 60 s (as Linux does), or the entry stays busy
 * forever.
 */
#define TCP_FIN_WAIT_2_TIMEOUT	(60 * 60)

/* Retransmission timeout (RTO) limits, in jiffies (RFC 6298). Before
 * the first round trip is measured the RTO is 1 s. RFC 6298 asks for a
 * minimum of 1 s too, but on a local network a round trip takes a few
 * milliseconds, and like Linux we use 200 ms so a lost segment does
 * not stall a transfer for a whole second.
 */
#define TCP_RTO_INITIAL		60		/* 1 s */
#define TCP_RTO_MIN			12		/* 200 ms */
#define TCP_RTO_MAX			3600	/* 60 s */

/* Timeouts in a row, without any ACK from the other side, before we
 * give up on a connection and reset it. The timeout doubles each time
 * (up to 60 s), so 10 means about 4 to 6 minutes of silence (RFC 1122
 * 4.2.3.5 asks for at least 100 s; Linux waits about 15 minutes). Why give up at all: a machine that was switched
 * off would otherwise keep a connection, and its buffers, forever.
 * While his window is 0 our probes count too, but he answers them, so
 * a slow reader is never given up.
 */
#define TCP_MAX_RETRIES		10

/* Data segments one connection may send in one felinc_poll() call, so
 * receiving and the program keep their turn during a big transfer
 */
#define TCP_SEND_BURST		4

/* Comparing sequence numbers: they wrap around from 0xFFFFFFFF to 0,
 * so "a comes before b" is decided by the sign of the difference
 */
#define SEQ_LT(a, b)	((int32_t)((a) - (b)) < 0)
#define SEQ_LE(a, b)	((int32_t)((a) - (b)) <= 0)
#define SEQ_GT(a, b)	((int32_t)((a) - (b)) > 0)
#define SEQ_GE(a, b)	((int32_t)((a) - (b)) >= 0)

/* Header length from the data_offset byte, in bytes */
#define TCP_HEADER_LENGTH(hdr)	(((hdr)->data_offset >> 4) * 4)

/* data_offset for a header of 5 words (no options) and of 6 words
 * (with the 4-byte MSS option)
 */
#define TCP_OFFSET_NO_OPTIONS	(5 << 4)
#define TCP_OFFSET_MSS_OPTION	(6 << 4)

/* TCP option kinds (RFC 9293, 3.2) */
#define TCP_OPT_END		0		/* end of the option list */
#define TCP_OPT_NOP		1		/* padding */
#define TCP_OPT_MSS		2		/* maximum segment size, 4 bytes long */

/* The states a connection goes through (RFC 9293, 3.3.2). Opening:
 * a server goes LISTEN -> (SYN) SYN_RECEIVED -> (ACK) ESTABLISHED; a
 * client SYN_SENT -> (SYN-ACK) ESTABLISHED. Both opening at the same
 * time ("simultaneous open") is not supported. Closing has two routes:
 * - he closes first: ESTABLISHED -> (his FIN) CLOSE_WAIT -> (our close)
 *   LAST_ACK -> (ACK of our FIN) gone
 * - we close first: ESTABLISHED -> (our close) FIN_WAIT_1 -> (ACK of
 *   our FIN) FIN_WAIT_2 -> (his FIN) TIME_WAIT -> (timer) gone; if
 *   both FINs cross, FIN_WAIT_1 -> CLOSING -> TIME_WAIT
 */
typedef enum tcp_state
{
	TCP_STATE_FREE = 0,			/* table entry not in use */
	TCP_STATE_LISTEN,			/* waiting for SYNs on a port */
	TCP_STATE_SYN_RECEIVED,		/* SYN received, our SYN-ACK sent, waiting for the ACK */
	TCP_STATE_SYN_SENT,			/* we sent a SYN (felinc_connect), waiting for the SYN-ACK */
	TCP_STATE_ESTABLISHED,		/* connected */
	TCP_STATE_CLOSE_WAIT,		/* he sent his FIN; we may still send */
	TCP_STATE_LAST_ACK,			/* both sent FIN (his first); waiting for the ACK of ours */
	TCP_STATE_FIN_WAIT_1,		/* we closed: our FIN is (being) sent, not yet acknowledged */
	TCP_STATE_FIN_WAIT_2,		/* our FIN is acknowledged; waiting for his */
	TCP_STATE_CLOSING,			/* both FINs crossed; waiting for the ACK of ours */
	TCP_STATE_TIME_WAIT,		/* all done; waiting a while for repeated FINs */
	TCP_STATE_CLOSED			/* ended (reset or timeout); waits until its socket is closed */
} tcp_state;

/* One connection (or one listening port). Sequence numbers count
 * bytes; a SYN and a FIN each count as one byte too.
 */
typedef struct tcp_conn
{
	tcp_state state;
	bool owned;					/* a socket in felinc.c uses this entry */
	struct tcp_conn *parent;	/* the listening entry it came from, or NULL */
	uint32_t started;			/* jiffies when the handshake, FIN_WAIT_2 or
								 * TIME_WAIT started (see tcp_poll) */

	uint16_t my_port;			/* our port */
	uint32_t his_ip;			/* the other side's address and port */
	uint16_t his_port;

	uint32_t iss;				/* our initial sequence number */
	uint32_t snd_una;			/* oldest of our bytes not yet acknowledged */
	uint32_t snd_nxt;			/* sequence number of the next byte we send */
	uint32_t snd_max;			/* one past the highest byte we ever sent; differs
								 * from snd_nxt after a timeout, when we go back */
	uint32_t irs;				/* his initial sequence number */
	uint32_t rcv_nxt;			/* next sequence number we expect from him */

	uint16_t his_mss;			/* largest data part he accepts in one segment */
	uint16_t his_window;		/* bytes he can still receive */
	uint16_t my_window;			/* the window we advertised last (listening
								 * entry: the receive buffer size for its connections) */
	uint16_t backlog;			/* listening entry: max connections waiting for accept */

	cbuf *rcv_buf;				/* received data, waiting for felinc_recv() */
	cbuf *snd_buf;				/* data from felinc_send(): first the bytes sent but
								 * not acknowledged (from snd_una), then the unsent ones */

	uint32_t cwnd;				/* congestion window: bytes we may have in flight */
	uint32_t ssthresh;			/* below it cwnd grows fast ("slow start") */

	bool rtx_running;			/* the retransmission timer runs */
	uint32_t rtx_time;			/* when it runs out (jiffies) */
	uint32_t rto;				/* retransmission timeout in jiffies */
	uint32_t srtt8;				/* smoothed round-trip time, in jiffies * 8 */
	uint32_t rttvar4;			/* its variation, in jiffies * 4 */
	bool rtt_known;				/* at least one round trip was measured */
	bool rtt_timing;			/* a round trip is being measured */
	uint32_t rtt_seq;			/* ... with this byte */
	uint32_t rtt_start;			/* ... sent at this time */
	uint8_t retries;			/* timeouts in a row without an ACK from him */

	bool fin_queued;			/* we closed: send a FIN after the last data */
	bool fin_sent;				/* our FIN went out (at fin_seq) */
	uint32_t fin_seq;			/* sequence number of our FIN */
	bool fin_received;			/* he sent his FIN: no more data comes */
	int16_t connect_result;		/* felinc_connect: TCP_CONNECT_BUSY, _DONE, _REFUSED
								 * or _TIMEOUT */
} tcp_conn;

#define TCP_TABLE_END	(conns + TCP_MAX_CONNECTIONS)	/* just past the last entry */

/* A segment to send: header (+ options) and data */
typedef struct tcp_segment
{
	tcp_header hdr;
	uint8_t data[MTU - sizeof(ip4_header) - sizeof(tcp_header)];
} tcp_segment;

static tcp_conn conns[TCP_MAX_CONNECTIONS];
static tcp_segment out;			/* the segment being built; ip4_send() copies it */
static uint32_t iss_offset;		/* see tcp_new_iss() */

/* Counters for the status screen (felinc_print_tcp) */
typedef struct tcp_counters
{
	uint32_t segments_in;		/* good segments received */
	uint32_t segments_out;		/* segments sent */
	uint32_t opened;			/* connections that got through the handshake */
	uint32_t resets_in;			/* connections ended by a reset from the other side */
	uint32_t aborted;			/* connections given up after TCP_MAX_RETRIES */
	uint32_t timeouts;			/* retransmission timeouts */
	uint32_t probes;			/* zero-window probes sent again */
	uint32_t out_of_order;		/* segments dropped because one before them was missing */
} tcp_counters;

static tcp_counters counts;
static uint16_t next_port = TCP_FIRST_EPHEMERAL;	/* see tcp_free_port() */

/***********************************************************************
 * tcp_init
 * Sets up the TCP module: all connection table entries are free.
 *
 * Input:
 *   none
 * Output:
 *   none
 **********************************************************************/
void tcp_init( void )
{
	memset( conns, 0, sizeof(conns) );
}

/***********************************************************************
 * tcp_calc_checksum (internal)
 * Computes the TCP checksum over a pseudo header and the whole
 * segment. The pseudo header holds fields of the IPv4 header (as in
 * UDP), so a segment delivered to the wrong address is detected too;
 * it is summed, never sent.
 *
 * Input:
 *   src_address   sender's IP address
 *   dest_address  receiver's IP address
 *   segment       the TCP header with the data directly after it
 *   length        TCP header + data, in bytes
 * Output:
 *   the checksum. Store it in an outgoing segment (with its checksum
 *   field set to 0 while computing). For a received segment, computed
 *   with its checksum field as it is, the result is 0 when correct.
 **********************************************************************/
static uint16_t tcp_calc_checksum( uint32_t src_address, uint32_t dest_address,
									const void *segment, uint16_t length )
{
	struct
	{
		uint32_t src_address;
		uint32_t dest_address;
		uint8_t  zero;
		uint8_t  protocol;
		uint16_t tcp_length;	/* not in the TCP header itself */
	} pseudo;
	uint32_t sum;

	pseudo.src_address = src_address;
	pseudo.dest_address = dest_address;
	pseudo.zero = 0;
	pseudo.protocol = IP4_TCP;
	pseudo.tcp_length = length;

	sum = utl_sum16( 0, &pseudo, sizeof(pseudo) );
	sum = utl_sum16( sum, segment, length );
	return utl_checksum_finish( sum );
}

/***********************************************************************
 * tcp_send (internal)
 * Builds a segment and sends it. Data, if any, must already be in
 * out.data.
 *
 * Input:
 *   his_ip     receiver's IP address
 *   my_port    our port
 *   his_port   receiver's port
 *   seq, ack   sequence and acknowledgement numbers
 *   flags      TCP_SYN, TCP_ACK, ...
 *   window     bytes we can still receive
 *   with_mss   true to add the MSS option (only allowed with a SYN,
 *              which carries no data)
 *   data_len   number of data bytes in out.data
 * Output:
 *   none
 **********************************************************************/
static void tcp_send( uint32_t his_ip, uint16_t my_port, uint16_t his_port,
					uint32_t seq, uint32_t ack, uint8_t flags, uint16_t window,
					bool with_mss, uint16_t data_len )
{
	uint16_t length = sizeof(tcp_header) + data_len;

	memset( &out.hdr, 0, sizeof(out.hdr) );
	out.hdr.src_port = my_port;
	out.hdr.dest_port = his_port;
	out.hdr.seq = seq;
	out.hdr.ack = ack;
	out.hdr.data_offset = TCP_OFFSET_NO_OPTIONS;
	out.hdr.flags = flags;
	out.hdr.window = window;

	if( with_mss )
	{
		out.data[0] = TCP_OPT_MSS;
		out.data[1] = 4;
		out.data[2] = (uint8_t)(TCP_MY_MSS >> 8);
		out.data[3] = (uint8_t)TCP_MY_MSS;
		out.hdr.data_offset = TCP_OFFSET_MSS_OPTION;
		length += 4;
	}

	out.hdr.checksum = tcp_calc_checksum( ip4_get_ip(), his_ip, &out, length );
	ip4_send( his_ip, IP4_TCP, (uint8_t *)&out, length );
	counts.segments_out++;
}

/***********************************************************************
 * tcp_window (internal)
 * Computes the window to advertise: the free room in the receive
 * buffer. The window field is 16 bits, so a bigger room is cut down.
 *
 * Input:
 *   c   the connection
 * Output:
 *   the window in bytes
 **********************************************************************/
static uint16_t tcp_window( tcp_conn *c )
{
	size_t room;

	if( c->rcv_buf == NULL ) return 0;
	room = cbuf_free( c->rcv_buf );
	return room < 0xFFFF ? (uint16_t)room : 0xFFFF;
}

/***********************************************************************
 * tcp_send_conn (internal)
 * Sends a segment without data on a connection, with the connection's
 * current sequence numbers and window. The window is remembered, so we
 * know what the other side may send (see tcp_seq_acceptable).
 *
 * Input:
 *   c       the connection
 *   flags   TCP_ACK, TCP_SYN | TCP_ACK, TCP_RST | TCP_ACK, ...
 * Output:
 *   none
 **********************************************************************/
static void tcp_send_conn( tcp_conn *c, uint8_t flags )
{
	/* A SYN carries our initial sequence number; all else the next one */
	uint32_t seq = (flags & TCP_SYN) ? c->iss : c->snd_nxt;

	c->my_window = tcp_window( c );
	tcp_send( c->his_ip, c->my_port, c->his_port, seq, c->rcv_nxt, flags,
				c->my_window, (flags & TCP_SYN) != 0, 0 );
}

/***********************************************************************
 * tcp_send_reset (internal)
 * Answers a received segment with a reset, as RFC 793 prescribes when
 * there is no connection for it. The sequence numbers are chosen so
 * the other side accepts the reset:
 * - if the segment carried an ACK, the reset uses that number as its
 *   own sequence number;
 * - if not, the reset acknowledges everything in the segment (its data,
 *   plus 1 for a SYN and 1 for a FIN).
 * A reset is never answered with a reset, or two machines could keep
 * resetting each other.
 *
 * Input:
 *   ip4       the received IPv4 packet
 *   tcp       the TCP header inside it (already checked)
 *   data_len  number of data bytes in the segment
 * Output:
 *   none
 **********************************************************************/
static void tcp_send_reset( ip4_pkt *ip4, tcp_header *tcp, uint16_t data_len )
{
	uint32_t ack;

	if( tcp->flags & TCP_RST ) return;

	if( tcp->flags & TCP_ACK )
	{
		tcp_send( ip4->ip4_hdr.src_addr, tcp->dest_port, tcp->src_port,
					tcp->ack, 0, TCP_RST, 0, false, 0 );
		return;
	}
	ack = tcp->seq + data_len;
	if( tcp->flags & TCP_SYN ) ack++;
	if( tcp->flags & TCP_FIN ) ack++;
	tcp_send( ip4->ip4_hdr.src_addr, tcp->dest_port, tcp->src_port,
				0, ack, TCP_RST | TCP_ACK, 0, false, 0 );
}

/***********************************************************************
 * tcp_new_iss (internal)
 * Chooses the initial sequence number for a new connection. It must
 * differ from the numbers of earlier connections, so old segments
 * still on the network are not mistaken for new ones. RFC 9293 uses a
 * clock; ours runs in jiffies, and an offset that grows with every
 * connection keeps two connections in the same jiffy apart.
 *
 * Input:
 *   none
 * Output:
 *   the initial sequence number
 **********************************************************************/
static uint32_t tcp_new_iss( void )
{
	iss_offset += 64000;
	return utl_jiffies() * 4167 + iss_offset;	/* 4167: about 4 us per step, as in RFC 793 */
}

/***********************************************************************
 * tcp_read_mss (internal)
 * Finds the MSS option in a received SYN: the largest data part the
 * other side accepts in one segment. The options come from the
 * network, so every length is checked before it is used.
 *
 * Input:
 *   tcp             the TCP header
 *   header_length   its length in bytes, options included
 * Output:
 *   the MSS, or TCP_DEFAULT_MSS if there is no (valid) MSS option
 **********************************************************************/
static uint16_t tcp_read_mss( tcp_header *tcp, uint16_t header_length )
{
	uint8_t *opt = (uint8_t *)tcp + sizeof(tcp_header);
	uint8_t *end = (uint8_t *)tcp + header_length;

	while( opt < end && *opt != TCP_OPT_END )
	{
		if( *opt == TCP_OPT_NOP )
		{
			opt++;
			continue;
		}
		/* All other options have a length byte, counting the whole option */
		if( opt + 1 >= end || opt[1] < 2 || opt + opt[1] > end ) break;
		if( opt[0] == TCP_OPT_MSS && opt[1] == 4 )
			return (uint16_t)((opt[2] << 8) | opt[3]);
		opt += opt[1];
	}
	return TCP_DEFAULT_MSS;
}

/***********************************************************************
 * tcp_seq_acceptable (internal)
 * Tells whether a received sequence number falls in our receive window,
 * as RFC 793 requires before a reset is believed. Why: otherwise anyone
 * could end our connections with a guessed reset. The subtraction
 * works across the wrap-around from 0xFFFFFFFF to 0.
 *
 * Input:
 *   c     the connection
 *   seq   the received sequence number
 * Output:
 *   true if seq is in the window
 **********************************************************************/
static bool tcp_seq_acceptable( tcp_conn *c, uint32_t seq )
{
	if( c->my_window == 0 ) return seq == c->rcv_nxt;
	return (uint32_t)(seq - c->rcv_nxt) < c->my_window;
}

/***********************************************************************
 * tcp_free (internal)
 * Makes a table entry free for a new connection, and frees its
 * buffers.
 *
 * Input:
 *   c   the entry
 * Output:
 *   none
 **********************************************************************/
static void tcp_free( tcp_conn *c )
{
	if( c->rcv_buf != NULL ) cbuf_destroy( c->rcv_buf );
	if( c->snd_buf != NULL ) cbuf_destroy( c->snd_buf );
	memset( c, 0, sizeof(*c) );
	c->state = TCP_STATE_FREE;
}

/***********************************************************************
 * tcp_shutdown
 * Gives back the buffers of every connection still in the table, also
 * of connections that were closed but had not finished closing (see
 * felinc_shutdown). The table is empty afterwards.
 *
 * Input:
 *   none
 * Output:
 *   none
 **********************************************************************/
void tcp_shutdown( void )
{
	tcp_conn *c;

	for( c = conns; c < TCP_TABLE_END; c++ )
		if( c->state != TCP_STATE_FREE ) tcp_free( c );
}

/***********************************************************************
 * tcp_end (internal)
 * Ends a connection that was reset, timed out or closed. A socket that
 * owns it must still see that it ended, so then the entry stays
 * (CLOSED) until the socket is closed; otherwise it is freed at once.
 *
 * Input:
 *   c   the connection
 * Output:
 *   none
 **********************************************************************/
static void tcp_end( tcp_conn *c )
{
	c->rtx_running = false;
	if( c->owned )
		c->state = TCP_STATE_CLOSED;
	else
		tcp_free( c );
}

/***********************************************************************
 * tcp_new_entry (internal)
 * Finds a free entry in the connection table.
 *
 * Input:
 *   none
 * Output:
 *   the entry, or NULL if the table is full
 **********************************************************************/
static tcp_conn *tcp_new_entry( void )
{
	tcp_conn *c;

	for( c = conns; c < TCP_TABLE_END; c++ )
		if( c->state == TCP_STATE_FREE ) return c;
	return NULL;
}

/***********************************************************************
 * tcp_find_conn (internal)
 * Finds the connection a received segment belongs to: the one with the
 * same addresses and ports at both ends.
 *
 * Input:
 *   his_ip, his_port   the sender of the segment
 *   my_port            the port it was sent to
 * Output:
 *   the connection, or NULL if there is none
 **********************************************************************/
static tcp_conn *tcp_find_conn( uint32_t his_ip, uint16_t his_port, uint16_t my_port )
{
	tcp_conn *c;

	for( c = conns; c < TCP_TABLE_END; c++ )
	{
		if( c->state != TCP_STATE_FREE && c->state != TCP_STATE_LISTEN &&
			c->state != TCP_STATE_CLOSED &&
			c->my_port == my_port && c->his_ip == his_ip && c->his_port == his_port )
			return c;
	}
	return NULL;
}

/***********************************************************************
 * tcp_find_listener (internal)
 * Finds the entry that listens on a port.
 *
 * Input:
 *   my_port   the port
 * Output:
 *   the listening entry, or NULL if nobody listens on the port
 **********************************************************************/
static tcp_conn *tcp_find_listener( uint16_t my_port )
{
	tcp_conn *c;

	for( c = conns; c < TCP_TABLE_END; c++ )
		if( c->state == TCP_STATE_LISTEN && c->my_port == my_port ) return c;
	return NULL;
}

/***********************************************************************
 * tcp_waiting_count (internal)
 * Counts the connections of a listening entry that the program has not
 * accepted yet, finished or still in their handshake.
 *
 * Input:
 *   listener   the listening entry
 * Output:
 *   the number of connections
 **********************************************************************/
static uint16_t tcp_waiting_count( tcp_conn *listener )
{
	tcp_conn *c;
	uint16_t count = 0;

	for( c = conns; c < TCP_TABLE_END; c++ )
		if( c->state != TCP_STATE_FREE && !c->owned && c->parent == listener ) count++;
	return count;
}

/***********************************************************************
 * tcp_setup (internal)
 * Fills in a new connection entry: addresses, buffers and our side of
 * the sequence numbers. The buffers are made here, at the start, not
 * at felinc_accept(): the other side may send data as soon as the
 * handshake is done, before the program accepts the connection.
 *
 * Input:
 *   c          a free entry
 *   my_port    our port
 *   his_ip     the other side's address
 *   his_port   the other side's port
 *   bufsize    size of the receive and the send buffer
 * Output:
 *   true, or false if there is no memory for the buffers (the entry
 *   is free again)
 **********************************************************************/
static bool tcp_setup( tcp_conn *c, uint16_t my_port, uint32_t his_ip, uint16_t his_port,
						uint16_t bufsize )
{
	c->rcv_buf = cbuf_create( bufsize );
	c->snd_buf = cbuf_create( bufsize );
	if( c->rcv_buf == NULL || c->snd_buf == NULL )
	{
		tcp_free( c );
		return false;
	}
	c->started = utl_jiffies();
	c->my_port = my_port;
	c->his_ip = his_ip;
	c->his_port = his_port;
	c->iss = tcp_new_iss();
	c->snd_una = c->iss;
	c->snd_nxt = c->iss + 1;		/* our SYN counts as one byte */
	c->snd_max = c->snd_nxt;
	c->his_mss = TCP_DEFAULT_MSS;
	c->rto = TCP_RTO_INITIAL;
	return true;
}

/***********************************************************************
 * tcp_take_syn (internal)
 * Takes what the other side tells in its SYN (or SYN-ACK): his initial
 * sequence number, MSS and window. Then the congestion window can be
 * set, because it counts in segments of his MSS.
 *
 * Input:
 *   c               the connection
 *   tcp             the TCP header with the SYN
 *   header_length   its length in bytes, options included
 * Output:
 *   none
 **********************************************************************/
static void tcp_take_syn( tcp_conn *c, tcp_header *tcp, uint16_t header_length )
{
	c->irs = tcp->seq;
	c->rcv_nxt = tcp->seq + 1;		/* his SYN counts as one byte */
	c->his_mss = tcp_read_mss( tcp, header_length );
	if( c->his_mss > TCP_MY_MSS ) c->his_mss = TCP_MY_MSS;	/* our out buffer holds no more */
	c->his_window = tcp->window;

	/* Initial congestion window, RFC 3390: about 4 KB, as 2 to 4
	 * segments. Then ssthresh as high as possible: slow start runs
	 * until the first loss.
	 */
	c->cwnd = 4 * (uint32_t)c->his_mss;
	if( c->cwnd > 4380 ) c->cwnd = 4380 > 2 * (uint32_t)c->his_mss ? 4380 : 2 * (uint32_t)c->his_mss;
	c->ssthresh = 0xFFFFFFFF;
}

/***********************************************************************
 * tcp_handle_listen (internal)
 * Handles a segment for a port we listen on, when no connection exists
 * for it yet. A SYN starts a new connection: we answer with SYN-ACK.
 * When the backlog or the table is full, or there is no memory for the
 * buffers, the SYN is ignored: the other side sends it again a
 * little later, and by then there may be room.
 *
 * Input:
 *   listener        the listening entry
 *   ip4             the received IPv4 packet
 *   tcp             the TCP header inside it
 *   header_length   TCP header length in bytes (options included)
 *   data_len        number of data bytes
 * Output:
 *   none
 **********************************************************************/
static void tcp_handle_listen( tcp_conn *listener, ip4_pkt *ip4, tcp_header *tcp,
								uint16_t header_length, uint16_t data_len )
{
	tcp_conn *c;

	if( tcp->flags & TCP_RST ) return;			/* nothing to reset */
	if( tcp->flags & TCP_ACK )					/* acknowledges something we never sent */
	{
		tcp_send_reset( ip4, tcp, data_len );
		return;
	}
	if( !(tcp->flags & TCP_SYN) ) return;

	if( tcp_waiting_count( listener ) >= listener->backlog ) return;
	c = tcp_new_entry();
	if( c == NULL ) return;
	if( !tcp_setup( c, tcp->dest_port, ip4->ip4_hdr.src_addr, tcp->src_port, listener->my_window ) )
		return;							/* no memory: as if the backlog is full */

	c->state = TCP_STATE_SYN_RECEIVED;
	c->owned = false;
	c->parent = listener;
	tcp_take_syn( c, tcp, header_length );

	tcp_send_conn( c, TCP_SYN | TCP_ACK );
}

/***********************************************************************
 * tcp_handle_syn_received (internal)
 * Handles a segment for a connection whose handshake is not finished:
 * we sent our SYN-ACK and wait for the ACK on it.
 *
 * Input:
 *   c          the connection
 *   ip4        the received IPv4 packet
 *   tcp        the TCP header inside it
 *   data_len   number of data bytes
 * Output:
 *   none
 **********************************************************************/
static void tcp_handle_syn_received( tcp_conn *c, ip4_pkt *ip4, tcp_header *tcp, uint16_t data_len )
{
	if( tcp->flags & TCP_RST )
	{
		if( tcp_seq_acceptable( c, tcp->seq ) )
		{
			counts.resets_in++;
			tcp_end( c );
		}
		return;
	}

	/* His SYN again: our SYN-ACK was lost, so send it again */
	if( (tcp->flags & TCP_SYN) && tcp->seq == c->irs )
	{
		tcp_send_conn( c, TCP_SYN | TCP_ACK );
		return;
	}

	if( !(tcp->flags & TCP_ACK) ) return;
	if( tcp->ack != c->snd_nxt )
	{
		tcp_send_reset( ip4, tcp, data_len );	/* acknowledges something we never sent */
		return;
	}

	c->snd_una = tcp->ack;
	c->his_window = tcp->window;
	c->state = TCP_STATE_ESTABLISHED;
	counts.opened++;
}

/***********************************************************************
 * tcp_handle_syn_sent (internal)
 * Handles a segment for a connection we are starting: we sent our SYN
 * and wait for the server's SYN-ACK (RFC 9293, 3.10.7.3).
 * - A SYN-ACK that acknowledges our SYN opens the connection; we
 *   answer with an ACK.
 * - A reset that acknowledges our SYN means nobody listens on that
 *   port: "connection refused".
 * - An ACK for anything else belongs to an old connection: we answer
 *   it with a reset, so the other side forgets it.
 * Data that comes with the SYN-ACK is not taken: the server sends it
 * again.
 *
 * Input:
 *   c                the connection
 *   ip4              the received IPv4 packet
 *   tcp              the TCP header inside it
 *   header_length    TCP header length in bytes (options included)
 *   data_len         number of data bytes
 * Output:
 *   none
 **********************************************************************/
static void tcp_handle_syn_sent( tcp_conn *c, ip4_pkt *ip4, tcp_header *tcp,
								uint16_t header_length, uint16_t data_len )
{
	bool ack_ok = (tcp->flags & TCP_ACK) && tcp->ack == c->snd_nxt;

	if( (tcp->flags & TCP_ACK) && !ack_ok )
	{
		tcp_send_reset( ip4, tcp, data_len );	/* never answers a reset itself */
		return;
	}
	if( tcp->flags & TCP_RST )
	{
		if( ack_ok )
		{
			c->connect_result = TCP_CONNECT_REFUSED;
			tcp_end( c );
		}
		return;
	}
	if( !(tcp->flags & TCP_SYN) || !ack_ok ) return;	/* no simultaneous open */

	tcp_take_syn( c, tcp, header_length );
	c->snd_una = tcp->ack;
	c->rtx_running = false;
	c->rto = TCP_RTO_INITIAL;		/* the SYN's doubled timeouts say nothing about data (RFC 6298, 5.7) */
	c->state = TCP_STATE_ESTABLISHED;
	c->connect_result = TCP_CONNECT_DONE;
	counts.opened++;
	tcp_send_conn( c, TCP_ACK );
}

/***********************************************************************
 * tcp_set_rto (internal)
 * Sets the retransmission timeout from the measured round trip, as
 * RFC 6298 describes: RTO = smoothed round trip + 4 * its variation,
 * within TCP_RTO_MIN and TCP_RTO_MAX.
 *
 * Input:
 *   c   the connection (with rtt_known set)
 * Output:
 *   none (c->rto is set)
 **********************************************************************/
static void tcp_set_rto( tcp_conn *c )
{
	c->rto = (c->srtt8 >> 3) + (c->rttvar4 > 0 ? c->rttvar4 : 1);
	if( c->rto < TCP_RTO_MIN ) c->rto = TCP_RTO_MIN;
	if( c->rto > TCP_RTO_MAX ) c->rto = TCP_RTO_MAX;
}

/***********************************************************************
 * tcp_update_rto (internal)
 * Takes a new round-trip measurement into the retransmission timeout,
 * as RFC 6298 describes: RTO = smoothed round trip + 4 * its variation.
 * Kept scaled (* 8 and * 4) so the averages work in whole jiffies.
 *
 * Input:
 *   c     the connection
 *   rtt   the measured round trip in jiffies
 * Output:
 *   none (c->rto is updated)
 **********************************************************************/
static void tcp_update_rto( tcp_conn *c, uint32_t rtt )
{
	int32_t delta;

	if( !c->rtt_known )
	{
		/* First measurement: srtt = rtt, rttvar = rtt / 2 */
		c->srtt8 = rtt << 3;
		c->rttvar4 = rtt << 1;
		c->rtt_known = true;
	}
	else
	{
		/* srtt += (rtt - srtt) / 8; rttvar += (|rtt - srtt| - rttvar) / 4 */
		delta = (int32_t)rtt - (int32_t)(c->srtt8 >> 3);
		c->srtt8 += delta;
		if( delta < 0 ) delta = -delta;
		delta -= (int32_t)(c->rttvar4 >> 2);
		c->rttvar4 += delta;
	}

	tcp_set_rto( c );
}

/***********************************************************************
 * tcp_handle_ack (internal)
 * Takes the acknowledgement and window from a segment on an open
 * connection. Acknowledged data is removed from the send buffer: it
 * will never have to be sent again. Each acknowledgement of new data
 * also lets the congestion window grow: fast at first (by a segment
 * per ACK, "slow start"), then by about one segment per round trip
 * (RFC 5681).
 *
 * Input:
 *   c     the connection
 *   tcp   the TCP header of the received segment (with TCP_ACK set)
 * Output:
 *   false if it acknowledges bytes we never sent (then an ACK was sent
 *   back and the segment must be dropped), true otherwise
 **********************************************************************/
static bool tcp_handle_ack( tcp_conn *c, tcp_header *tcp )
{
	uint32_t acked, mss = c->his_mss;
	uint32_t now = utl_jiffies();

	if( SEQ_GT( tcp->ack, c->snd_max ) )
	{
		tcp_send_conn( c, TCP_ACK );	/* RFC 9293: answer with what we did send */
		return false;
	}
	c->retries = 0;			/* he is still there */
	if( SEQ_LT( tcp->ack, c->snd_una ) ) return true;	/* an old ACK: nothing new in it */

	if( SEQ_GT( tcp->ack, c->snd_una ) )
	{
		acked = tcp->ack - c->snd_una;
		cbuf_get( c->snd_buf, NULL, acked );
		c->snd_una = tcp->ack;
		if( SEQ_LT( c->snd_nxt, c->snd_una ) ) c->snd_nxt = c->snd_una;	/* after going back */

		if( c->rtt_timing && SEQ_GT( tcp->ack, c->rtt_seq ) )
		{
			tcp_update_rto( c, now - c->rtt_start );
			c->rtt_timing = false;
		}
		else if( c->rtt_known )
		{
			/* Timeouts doubled the RTO, but new data gets through
			 * again: go back to the measured value at once (as Linux
			 * does), or one lost segment after a long zero window
			 * would stall the transfer for up to a minute
			 */
			tcp_set_rto( c );
		}

		if( c->cwnd < c->ssthresh )
			c->cwnd += acked < mss ? acked : mss;
		else
			c->cwnd += (mss * mss) / c->cwnd + 1;
		if( c->cwnd > 0xFFFF ) c->cwnd = 0xFFFF;	/* his window can never be bigger */

		/* Restart the timer for what is still in flight */
		c->rtx_running = c->snd_una != c->snd_max;
		c->rtx_time = now + c->rto;
	}
	c->his_window = tcp->window;
	return true;
}

/***********************************************************************
 * tcp_handle_data (internal)
 * Stores the data of a segment in the receive buffer, takes his FIN,
 * and acknowledges. Only data that continues exactly where the
 * previous data ended is taken:
 * - a part we already have (a resent segment) is skipped;
 * - a segment that starts further on (one before it was lost) is
 *   dropped, and our ACK tells the other side where we are, so it
 *   sends the missing part again;
 * - what does not fit in the buffer is dropped; it lies outside the
 *   window we advertised, so the other side should not have sent it.
 * His FIN comes after his last data byte, so it only counts when all
 * data before it is taken. It uses one sequence number, like a byte.
 * Every segment with data or a FIN is answered with an ACK at once.
 * Why not wait and acknowledge two segments together, as most systems
 * do: it is simpler, and our small buffer empties quickly anyway.
 *
 * Input:
 *   c          the connection
 *   tcp        the TCP header of the received segment
 *   data       its data
 *   data_len   number of data bytes (may be 0)
 * Output:
 *   true if his FIN was received now (the caller changes the state)
 **********************************************************************/
static bool tcp_handle_data( tcp_conn *c, tcp_header *tcp, uint8_t *data, uint16_t data_len )
{
	bool fin = (tcp->flags & TCP_FIN) != 0;
	uint32_t skip = c->rcv_nxt - tcp->seq;	/* bytes we already have */
	size_t room;
	uint16_t take;

	if( (int32_t)skip < 0 )
	{
		counts.out_of_order++;
		tcp_send_conn( c, TCP_ACK );	/* out of order: tell him where we are */
		return false;
	}
	if( skip >= data_len && !(fin && skip == data_len) )
	{
		/* Nothing new. An old segment, a resent FIN or a "keep-alive"
		 * probe (one from before rcv_nxt) gets an ACK; a plain ACK
		 * without data needs no answer.
		 */
		if( data_len > 0 || fin || skip > 0 ) tcp_send_conn( c, TCP_ACK );
		return false;
	}

	take = data_len - (uint16_t)skip;
	room = cbuf_free( c->rcv_buf );
	if( take > room )
	{
		take = (uint16_t)room;
		fin = false;					/* data before it is missing */
	}
	if( take > 0 )
	{
		cbuf_put( c->rcv_buf, data + skip, take );
		c->rcv_nxt += take;
		/* After felinc_close() nobody reads any more: throw it away,
		 * so the window stays open and he can finish sending
		 */
		if( c->fin_queued ) cbuf_get( c->rcv_buf, NULL, take );
	}
	if( fin )
	{
		c->rcv_nxt++;
		c->fin_received = true;
	}
	tcp_send_conn( c, TCP_ACK );
	return fin;
}

/***********************************************************************
 * tcp_handle_open (internal)
 * Handles a segment for a connection after the handshake, also while
 * it closes: a reset, the ACK and window, data and his FIN. The state
 * moves on when our FIN is acknowledged and when his FIN arrives (see
 * enum tcp_state for the routes).
 *
 * Input:
 *   c          the connection
 *   tcp        the TCP header of the received segment
 *   data       its data
 *   data_len   number of data bytes
 * Output:
 *   none
 **********************************************************************/
static void tcp_handle_open( tcp_conn *c, tcp_header *tcp, uint8_t *data, uint16_t data_len )
{
	uint32_t now = utl_jiffies();

	if( tcp->flags & TCP_RST )
	{
		if( tcp_seq_acceptable( c, tcp->seq ) )
		{
			counts.resets_in++;
			tcp_end( c );
		}
		return;
	}

	/* A SYN on an open connection: the other side may have restarted.
	 * RFC 5961 says not to believe it, but to send an ACK: a real new
	 * start then answers it with a reset, which ends this connection.
	 */
	if( tcp->flags & TCP_SYN )
	{
		tcp_send_conn( c, TCP_ACK );
		return;
	}

	if( !(tcp->flags & TCP_ACK) ) return;	/* every segment after the handshake has one */
	if( !tcp_handle_ack( c, tcp ) ) return;

	/* Our FIN acknowledged? */
	if( c->fin_sent && c->snd_una == c->fin_seq + 1 )
	{
		if( c->state == TCP_STATE_FIN_WAIT_1 )
		{
			c->state = TCP_STATE_FIN_WAIT_2;
			c->started = now;
		}
		else if( c->state == TCP_STATE_CLOSING )
		{
			c->state = TCP_STATE_TIME_WAIT;
			c->started = now;
		}
		else if( c->state == TCP_STATE_LAST_ACK )
		{
			tcp_end( c );					/* all done */
			return;
		}
	}

	if( tcp_handle_data( c, tcp, data, data_len ) )
	{
		/* His FIN arrived */
		if( c->state == TCP_STATE_ESTABLISHED )
			c->state = TCP_STATE_CLOSE_WAIT;
		else if( c->state == TCP_STATE_FIN_WAIT_1 )
			c->state = TCP_STATE_CLOSING;
		else if( c->state == TCP_STATE_FIN_WAIT_2 )
		{
			c->state = TCP_STATE_TIME_WAIT;
			c->started = now;
		}
	}
}

/***********************************************************************
 * tcp_sending (internal)
 * Tells whether a connection may still have data or a FIN of ours to
 * send or send again: all states after the handshake until our FIN is
 * acknowledged.
 *
 * Input:
 *   c   the connection
 * Output:
 *   true in ESTABLISHED, CLOSE_WAIT, FIN_WAIT_1, CLOSING and LAST_ACK
 **********************************************************************/
static bool tcp_sending( tcp_conn *c )
{
	return c->state == TCP_STATE_ESTABLISHED || c->state == TCP_STATE_CLOSE_WAIT ||
		c->state == TCP_STATE_FIN_WAIT_1 || c->state == TCP_STATE_CLOSING ||
		c->state == TCP_STATE_LAST_ACK;
}

/***********************************************************************
 * tcp_output (internal)
 * Sends what is waiting in the send buffer of an open connection, as
 * far as the windows allow: the other side's window (room in its
 * buffer) and our congestion window (what the network can take). Each
 * segment carries at most his MSS.
 * Two special cases:
 * - When his window is 0 and nothing is in flight, one byte is sent
 *   anyway, as a probe: its acknowledgement brings the new window.
 *   The retransmission timer repeats the probe until there is room.
 *   Why: if his window update got lost, we would wait forever.
 * - When only a small part fits in the window while data is in
 *   flight, we wait for the ACK and then send a full segment ("silly
 *   window syndrome" avoidance, RFC 1122 4.2.3.4).
 * After the program closed the connection, our FIN follows the last
 * data byte. It takes one sequence number and is sent again after a
 * timeout, just like data.
 *
 * Input:
 *   c     the connection
 *   now   the current time in jiffies
 * Output:
 *   none
 **********************************************************************/
static void tcp_output( tcp_conn *c, uint32_t now )
{
	uint32_t data_end, in_flight, unsent, window, len;
	uint16_t burst;
	uint8_t flags;

	if( !tcp_sending( c ) ) return;

	for( burst = 0; burst < TCP_SEND_BURST; burst++ )
	{
		data_end = c->snd_una + (uint32_t)cbuf_used( c->snd_buf );	/* after our last byte */
		if( !SEQ_LT( c->snd_nxt, data_end ) )
		{
			/* All data is sent: now the FIN, if we closed and it is
			 * not sent or acknowledged yet (after a timeout snd_nxt
			 * went back, and then it is sent again)
			 */
			if( c->fin_queued && c->snd_nxt == data_end &&
				!(c->fin_sent && SEQ_GT( c->snd_una, c->fin_seq )) )
			{
				c->fin_sent = true;
				c->fin_seq = c->snd_nxt;
				tcp_send_conn( c, TCP_FIN | TCP_ACK );
				c->snd_nxt++;
				if( SEQ_GT( c->snd_nxt, c->snd_max ) ) c->snd_max = c->snd_nxt;
				if( !c->rtx_running )
				{
					c->rtx_running = true;
					c->rtx_time = now + c->rto;
				}
			}
			return;
		}

		in_flight = c->snd_nxt - c->snd_una;
		unsent = data_end - c->snd_nxt;
		window = c->his_window < c->cwnd ? c->his_window : c->cwnd;
		if( c->his_window == 0 && in_flight == 0 ) window = 1;		/* the probe */
		if( in_flight >= window ) return;

		len = window - in_flight;
		if( len > unsent ) len = unsent;
		if( len > c->his_mss ) len = c->his_mss;
		if( len < c->his_mss && len < unsent && in_flight > 0 ) return;	/* wait for a full segment */

		cbuf_peek( c->snd_buf, in_flight, out.data, len );
		flags = TCP_ACK;
		if( len == unsent ) flags |= TCP_PSH;		/* the end of what the program wrote */
		c->my_window = tcp_window( c );
		tcp_send( c->his_ip, c->my_port, c->his_port, c->snd_nxt, c->rcv_nxt, flags,
					c->my_window, false, (uint16_t)len );

		/* Measure the round trip with one segment at a time, and only
		 * with new data: for a resent segment we could not tell which
		 * of the two copies the ACK answers (Karn's rule).
		 */
		if( !c->rtt_timing && !SEQ_LT( c->snd_nxt, c->snd_max ) )
		{
			c->rtt_timing = true;
			c->rtt_seq = c->snd_nxt;
			c->rtt_start = now;
		}

		c->snd_nxt += len;
		if( SEQ_GT( c->snd_nxt, c->snd_max ) ) c->snd_max = c->snd_nxt;
		if( !c->rtx_running )
		{
			c->rtx_running = true;
			c->rtx_time = now + c->rto;
		}
	}
}

/***********************************************************************
 * tcp_timeout (internal)
 * The retransmission timer of a connection ran out: what we sent was
 * not acknowledged in time, so it was probably lost. We go back to the
 * oldest unacknowledged byte and send again from there, and the
 * timeout doubles each time (RFC 6298). Two cases:
 * - his window is 0: what timed out was our window probe. That says
 *   nothing about the network, so the congestion window stays.
 * - otherwise the network may be overloaded, so the congestion window
 *   drops to one segment (RFC 5681).
 * After TCP_MAX_RETRIES timeouts in a row without any ACK we give up:
 * the connection is reset and the program sees it as closed.
 *
 * Input:
 *   c     the connection
 *   now   the current time in jiffies
 * Output:
 *   none
 **********************************************************************/
static void tcp_timeout( tcp_conn *c, uint32_t now )
{
	uint32_t in_flight = c->snd_max - c->snd_una;
	uint32_t mss = c->his_mss;

	c->snd_nxt = c->snd_una;
	c->rtt_timing = false;
	if( ++c->retries > TCP_MAX_RETRIES )
	{
		/* Nothing heard for minutes: give up. The reset starts at
		 * snd_una, the byte he expects, so he accepts it if he is
		 * still there after all.
		 */
		counts.aborted++;
		tcp_send_conn( c, TCP_RST | TCP_ACK );
		tcp_end( c );
		return;
	}

	if( c->his_window == 0 )
		counts.probes++;
	else
	{
		counts.timeouts++;
		c->ssthresh = in_flight / 2 > 2 * mss ? in_flight / 2 : 2 * mss;
		c->cwnd = mss;
	}
	c->rto = c->rto * 2 < TCP_RTO_MAX ? c->rto * 2 : TCP_RTO_MAX;
	c->rtx_running = false;			/* tcp_output() starts it again */
	tcp_output( c, now );
}

/***********************************************************************
 * tcp_receive
 * Handles a received TCP segment. Lengths come from the network, so
 * they are checked before anything is trusted.
 *
 * Input:
 *   ip4    the received IPv4 packet, addressed to us (its IPv4 header
 *          is already checked)
 * Output:
 *   TCP_RECEIVED, TCP_BAD (dropped) or TCP_NO_SOCKET (reset sent)
 **********************************************************************/
int16_t tcp_receive( ip4_pkt *ip4 )
{
	tcp_header *tcp = (tcp_header *)ip4->data;
	uint16_t length = ip4->ip4_hdr.length - sizeof(ip4_header);	/* TCP header + data */
	uint16_t header_length, data_len;
	tcp_conn *c;

	if( length < sizeof(tcp_header) ) return TCP_BAD;
	header_length = TCP_HEADER_LENGTH( tcp );
	if( header_length < sizeof(tcp_header) || header_length > length ) return TCP_BAD;
	if( tcp_calc_checksum( ip4->ip4_hdr.src_addr, ip4->ip4_hdr.dest_addr, tcp, length ) != 0 )
		return TCP_BAD;
	data_len = length - header_length;
	counts.segments_in++;

	c = tcp_find_conn( ip4->ip4_hdr.src_addr, tcp->src_port, tcp->dest_port );
	if( c != NULL )
	{
		if( c->state == TCP_STATE_SYN_SENT )
		{
			tcp_handle_syn_sent( c, ip4, tcp, header_length, data_len );
			return TCP_RECEIVED;
		}
		if( c->state == TCP_STATE_SYN_RECEIVED )
		{
			tcp_handle_syn_received( c, ip4, tcp, data_len );

			/* The ACK that finishes the handshake may already carry
			 * data: then it is handled as on an open connection
			 */
			if( c->state != TCP_STATE_ESTABLISHED || data_len == 0 ) return TCP_RECEIVED;
		}
		tcp_handle_open( c, tcp, (uint8_t *)tcp + header_length, data_len );
		return TCP_RECEIVED;
	}

	c = tcp_find_listener( tcp->dest_port );
	if( c != NULL )
	{
		tcp_handle_listen( c, ip4, tcp, header_length, data_len );
		return TCP_RECEIVED;
	}

	tcp_send_reset( ip4, tcp, data_len );
	return TCP_NO_SOCKET;
}

/***********************************************************************
 * tcp_poll
 * Runs the TCP timers: unfinished handshakes (both ways),
 * retransmissions, and the end of TIME_WAIT and FIN_WAIT_2.
 * Called by felinc_poll() once per jiffy.
 *
 * Input:
 *   now   the current time in jiffies
 * Output:
 *   none
 **********************************************************************/
void tcp_poll( uint32_t now )
{
	tcp_conn *c;

	for( c = conns; c < TCP_TABLE_END; c++ )
	{
		if( c->state == TCP_STATE_SYN_RECEIVED && now - c->started >= TCP_HANDSHAKE_TIMEOUT )
		{
			tcp_send_conn( c, TCP_RST | TCP_ACK );
			tcp_end( c );
		}
		else if( c->state == TCP_STATE_SYN_SENT && now - c->started >= TCP_CONNECT_WAIT )
		{
			c->connect_result = TCP_CONNECT_TIMEOUT;
			tcp_end( c );
		}
		else if( c->state == TCP_STATE_SYN_SENT && !SEQ_LT( now, c->rtx_time ) )
		{
			/* No SYN-ACK yet: send the SYN again, and wait twice as long */
			c->rto = c->rto * 2 < TCP_RTO_MAX ? c->rto * 2 : TCP_RTO_MAX;
			c->rtx_time = now + c->rto;
			tcp_send_conn( c, TCP_SYN );
		}
		else if( c->rtx_running && tcp_sending( c ) && !SEQ_LT( now, c->rtx_time ) )
			tcp_timeout( c, now );
		else if( c->state == TCP_STATE_TIME_WAIT && now - c->started >= TCP_TIME_WAIT_TIMEOUT )
			tcp_end( c );
		else if( c->state == TCP_STATE_FIN_WAIT_2 && now - c->started >= TCP_FIN_WAIT_2_TIMEOUT )
			tcp_end( c );
	}
}

/***********************************************************************
 * tcp_send_pending
 * Sends waiting data (and FINs) on every open connection. Called by
 * felinc_poll() on every call, so data a program writes in several
 * small pieces between two calls goes out together.
 *
 * Input:
 *   now   the current time in jiffies (felinc_poll asks the MCP once:
 *         that system call takes about 30 us)
 * Output:
 *   none
 **********************************************************************/
void tcp_send_pending( uint32_t now )
{
	tcp_conn *c;

	/* This runs on every poll, so the free entries (most of the table)
	 * are skipped with one quick test before anything else
	 */
	for( c = conns; c < TCP_TABLE_END; c++ )
		if( c->state != TCP_STATE_FREE && tcp_sending( c ) ) tcp_output( c, now );
}

/***********************************************************************
 * tcp_free_port (internal)
 * Chooses our port for a connection we start, when the program did not
 * bind one: the next one in the ephemeral range that no connection in
 * the table uses. Why not always the same: a new connection to the
 * same server would then look like the old one, which may still be in
 * TIME_WAIT there.
 *
 * Input:
 *   none
 * Output:
 *   the port
 **********************************************************************/
static uint16_t tcp_free_port( void )
{
	tcp_conn *c;
	uint16_t port;
	bool used;

	do
	{
		port = next_port;
		next_port = next_port == TCP_LAST_EPHEMERAL ? TCP_FIRST_EPHEMERAL : next_port + 1;
		used = false;
		for( c = conns; c < TCP_TABLE_END; c++ )
			if( c->state != TCP_STATE_FREE && c->my_port == port ) used = true;
	} while( used );		/* ends: the table has far fewer entries than the range */
	return port;
}

/***********************************************************************
 * tcp_connect
 * Starts a connection to a server (for felinc_connect()): sends our
 * SYN. tcp_connect_result() tells when it is open.
 *
 * Input:
 *   my_port    our port, or 0 to choose a free one
 *   his_ip     the server's address
 *   his_port   the server's port
 *   bufsize    size of the receive and the send buffer
 * Output:
 *   the number of the connection, or -1 (table full, or no memory)
 **********************************************************************/
int16_t tcp_connect( uint16_t my_port, uint32_t his_ip, uint16_t his_port, uint16_t bufsize )
{
	tcp_conn *c = tcp_new_entry();

	if( c == NULL ) return -1;
	if( my_port == 0 ) my_port = tcp_free_port();
	if( !tcp_setup( c, my_port, his_ip, his_port, bufsize ) ) return -1;

	c->state = TCP_STATE_SYN_SENT;
	c->owned = true;
	c->parent = NULL;
	c->connect_result = TCP_CONNECT_BUSY;
	c->rtx_time = c->started + c->rto;		/* when the SYN is sent again (tcp_poll) */
	tcp_send_conn( c, TCP_SYN );
	return (int16_t)(c - conns);
}

/***********************************************************************
 * tcp_connect_result
 * Tells how a connection started with tcp_connect() is doing.
 *
 * Input:
 *   conn   the number of the connection
 * Output:
 *   TCP_CONNECT_BUSY (still waiting for the server), TCP_CONNECT_DONE
 *   (open; it may since have ended), TCP_CONNECT_REFUSED (nobody
 *   listens on that port) or TCP_CONNECT_TIMEOUT (no answer)
 **********************************************************************/
int16_t tcp_connect_result( int16_t conn )
{
	return conns[conn].connect_result;
}

/***********************************************************************
 * tcp_listen
 * Starts listening for connections on a port (for felinc_listen()).
 *
 * Input:
 *   port      our port
 *   backlog   max connections waiting to be accepted (at least 1)
 *   window    bytes a new connection can receive (its buffer size)
 * Output:
 *   the number of the listening entry, or -1 (port already in use,
 *   or the table is full)
 **********************************************************************/
int16_t tcp_listen( uint16_t port, uint16_t backlog, uint16_t window )
{
	tcp_conn *c;

	if( tcp_find_listener( port ) != NULL ) return -1;
	c = tcp_new_entry();
	if( c == NULL ) return -1;

	c->state = TCP_STATE_LISTEN;
	c->owned = true;
	c->parent = NULL;
	c->my_port = port;
	c->my_window = window;
	c->backlog = backlog > 0 ? backlog : 1;
	return (int16_t)(c - conns);
}

/***********************************************************************
 * tcp_find_acceptable (internal)
 * Finds a connection that finished its handshake on a listening port
 * and that no socket owns yet. CLOSE_WAIT counts too: a short client
 * may send its data and FIN before the program gets round to
 * accepting.
 *
 * Input:
 *   listener   number of the listening entry
 * Output:
 *   the connection, or NULL if none is waiting
 **********************************************************************/
static tcp_conn *tcp_find_acceptable( int16_t listener )
{
	tcp_conn *l = &conns[listener];
	tcp_conn *c;

	for( c = conns; c < TCP_TABLE_END; c++ )
		if( (c->state == TCP_STATE_ESTABLISHED || c->state == TCP_STATE_CLOSE_WAIT) &&
			!c->owned && c->parent == l )
			return c;
	return NULL;
}

/***********************************************************************
 * tcp_can_accept
 * Tells whether a connection is waiting to be accepted on a listening
 * port. felinc_accept() asks this first, so it only creates a socket
 * when there is something to accept. (Programs call felinc_accept()
 * on every pass of their main loop; a socket made and freed again each
 * time cost 300 us per pass.)
 *
 * Input:
 *   listener   number of the listening entry
 * Output:
 *   true if tcp_accept() will return a connection
 **********************************************************************/
bool tcp_can_accept( int16_t listener )
{
	return tcp_find_acceptable( listener ) != NULL;
}

/***********************************************************************
 * tcp_accept
 * Takes a connection that finished its handshake on a listening port
 * (for felinc_accept()). From now on a socket owns it. It may already
 * have data and even a FIN waiting.
 *
 * Input:
 *   listener   number of the listening entry
 *   his_ip     where to store the other side's address
 *   his_port   where to store the other side's port
 * Output:
 *   the number of the connection, or -1 if none is waiting
 **********************************************************************/
int16_t tcp_accept( int16_t listener, uint32_t *his_ip, uint16_t *his_port )
{
	tcp_conn *c = tcp_find_acceptable( listener );

	if( c == NULL ) return -1;
	c->owned = true;
	*his_ip = c->his_ip;
	*his_port = c->his_port;
	return (int16_t)(c - conns);
}

/***********************************************************************
 * tcp_recv_done
 * Tells whether no more data will arrive on a connection (for
 * felinc_recv()): the other side sent its FIN, or the connection
 * ended. Data that arrived before can still be read with tcp_read().
 *
 * Input:
 *   conn   the number of the connection
 * Output:
 *   true if no more data will arrive
 **********************************************************************/
bool tcp_recv_done( int16_t conn )
{
	/* Through a pointer, not conns[conn] twice: Calypsi 5.18 then
	 * multiplies conn by the entry size in place, so the second access
	 * read far outside the table and a reset was never noticed (see
	 * README, "A note on the Calypsi compiler")
	 */
	tcp_conn *c = &conns[conn];

	return c->fin_received || c->state == TCP_STATE_CLOSED;
}

/***********************************************************************
 * tcp_read
 * Takes received data from a connection (for felinc_recv()). Reading
 * makes room in the buffer. When the room has grown enough, the other
 * side hears so at once with an ACK (a "window update"); otherwise it
 * could keep waiting for room after we advertised a full buffer. Not
 * after every small read, though: announcing a few bytes of room only
 * makes it send many tiny segments ("silly window syndrome",
 * RFC 1122 4.2.3.3).
 *
 * Input:
 *   conn     the number of the connection
 *   buffer   where to copy the data
 *   buflen   size of buffer in bytes
 * Output:
 *   the number of bytes copied (0 if nothing is waiting)
 **********************************************************************/
size_t tcp_read( int16_t conn, void *buffer, size_t buflen )
{
	tcp_conn *c = &conns[conn];
	size_t n, threshold;

	if( c->rcv_buf == NULL ) return 0;
	n = (size_t)cbuf_get( c->rcv_buf, buffer, buflen );

	if( n > 0 && c->state == TCP_STATE_ESTABLISHED )
	{
		/* Worth announcing: a full segment, or half the buffer */
		threshold = c->rcv_buf->len / 2;
		if( threshold > TCP_MY_MSS ) threshold = TCP_MY_MSS;
		if( (int32_t)tcp_window( c ) - (int32_t)c->my_window >= (int32_t)threshold )
			tcp_send_conn( c, TCP_ACK );
	}
	return n;
}

/***********************************************************************
 * tcp_write
 * Puts data in the send buffer of a connection (for felinc_send()).
 * felinc_poll() sends it. What does not fit is not taken: the program
 * must offer it again later.
 *
 * Input:
 *   conn     the number of the connection
 *   data     the data
 *   length   number of bytes
 * Output:
 *   the number of bytes taken (0 if the buffer is full), or -1 if the
 *   connection can no longer send (ended)
 **********************************************************************/
ssize_t tcp_write( int16_t conn, const void *data, size_t length )
{
	tcp_conn *c = &conns[conn];
	size_t room;

	/* After his FIN we may still send (CLOSE_WAIT): he only said he
	 * has nothing more to send himself
	 */
	if( c->state != TCP_STATE_ESTABLISHED && c->state != TCP_STATE_CLOSE_WAIT &&
		c->state != TCP_STATE_SYN_SENT ) return -1;		/* SYN_SENT: kept until connected */
	room = cbuf_free( c->snd_buf );
	if( length > room ) length = room;
	if( length > 0 ) cbuf_put( c->snd_buf, data, length );
	return (ssize_t)length;
}

/***********************************************************************
 * tcp_release
 * The socket that owns an entry is being closed (for felinc_close()).
 * - An open connection is closed tidily: what is still in the send
 *   buffer goes out first, then our FIN. The entry stays (no longer
 *   owned) until the closing is done; data that still arrives is
 *   acknowledged and thrown away.
 * - A listening socket also ends the connections on it that were not
 *   accepted yet, with a reset.
 * - A connection that already ended is freed.
 *
 * Input:
 *   conn   the number of the entry
 * Output:
 *   none
 **********************************************************************/
void tcp_release( int16_t conn )
{
	tcp_conn *c = &conns[conn];
	tcp_conn *child;

	if( c->state == TCP_STATE_LISTEN )
	{
		for( child = conns; child < TCP_TABLE_END; child++ )
			if( child->state != TCP_STATE_FREE && !child->owned && child->parent == c )
			{
				tcp_send_conn( child, TCP_RST | TCP_ACK );
				tcp_free( child );
			}
		tcp_free( c );
		return;
	}
	if( c->state != TCP_STATE_ESTABLISHED && c->state != TCP_STATE_CLOSE_WAIT )
	{
		tcp_free( c );
		return;
	}

	c->owned = false;
	c->parent = NULL;			/* so it no longer counts as waiting for accept */
	c->fin_queued = true;
	cbuf_get( c->rcv_buf, NULL, cbuf_used( c->rcv_buf ) );	/* nobody will read it */
	c->state = c->state == TCP_STATE_ESTABLISHED ? TCP_STATE_FIN_WAIT_1 : TCP_STATE_LAST_ACK;
	tcp_output( c, utl_jiffies() );
}

/***********************************************************************
 * tcp_closing
 * Tells whether a connection is still sending its last data or FIN
 * (for felinc_shutdown(), which waits a little for that).
 *
 * Input:
 *   none
 * Output:
 *   true if any connection still waits for the ACK of our FIN
 **********************************************************************/
bool tcp_closing( void )
{
	tcp_conn *c;

	for( c = conns; c < TCP_TABLE_END; c++ )
		if( c->fin_queued && tcp_sending( c ) ) return true;
	return false;
}

/***********************************************************************
 * tcp_print_status
 * Prints the connection table and the TCP counters on the screen (for
 * felinc_print_tcp). For a connection that can send it also shows the
 * bytes waiting in the send buffer, his window, the retransmission
 * timeout and the timeouts in a row.
 *
 * Input:
 *   none
 * Output:
 *   none
 **********************************************************************/
void tcp_print_status( void )
{
	static const char *state_names[] = { "free", "LISTEN", "SYN_RECEIVED", "SYN_SENT", "ESTABLISHED",
		"CLOSE_WAIT", "LAST_ACK", "FIN_WAIT_1", "FIN_WAIT_2", "CLOSING", "TIME_WAIT", "CLOSED" };
	char ip_text[16];
	tcp_conn *c;

	for( c = conns; c < TCP_TABLE_END; c++ )
	{
		if( c->state == TCP_STATE_FREE ) continue;
		utl_print("\n    %2d: %-12s port %u", (int)(c - conns), state_names[c->state], (unsigned)c->my_port);
		if( c->state == TCP_STATE_LISTEN ) continue;
		ip4_uint32_t_to_string( ip_text, c->his_ip );
		utl_print(" <-> %s:%u%s", ip_text, (unsigned)c->his_port,
				c->owned || c->parent == NULL ? "" : " (not accepted)");
		if( tcp_sending( c ) )
			utl_print("\n        to send %lu, his window %u, rto %lu ms, retries %u",
					(unsigned long)cbuf_used( c->snd_buf ), (unsigned)c->his_window,
					(unsigned long)(c->rto * 50 / 3),		/* a jiffy is 1/60 s */
					(unsigned)c->retries);
	}
	utl_print("\n    segments in %lu, out %lu, out of order %lu",
			(unsigned long)counts.segments_in, (unsigned long)counts.segments_out,
			(unsigned long)counts.out_of_order);
	utl_print("\n    connections opened %lu, reset by him %lu, given up %lu",
			(unsigned long)counts.opened, (unsigned long)counts.resets_in,
			(unsigned long)counts.aborted);
	utl_print("\n    timeouts %lu, window probes %lu",
			(unsigned long)counts.timeouts, (unsigned long)counts.probes);
}
