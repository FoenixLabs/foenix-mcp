/***********************************************************************
 * FELINC.C - the Foenix Labs Ethernet Library in C: the part programs
 * use, and the glue between the protocol modules.
 *
 * FELINC is made for a single-tasking machine. There is no background
 * task: the program calls felinc_poll() from its main loop, and every
 * call does a limited amount of network work (FELINC_RX_BUDGET and
 * FELINC_TX_BUDGET) so the program keeps running however busy the
 * network is. Timers (ARP, DHCP, DNS) use the MCP jiffies as time base.
 *
 * Programs talk to the network through sockets, much like POSIX:
 * felinc_socket(), felinc_bind(), felinc_recvfrom(), felinc_sendto()
 * and felinc_close(). These never wait: received data waits in the
 * socket's receive buffer until the program asks for it, and data to
 * send waits in the socket's send buffer until felinc_poll() sends it.
 * At the end, felinc_shutdown() leaves the network tidily.
 *
 * Memory is only allocated in felinc_init() and felinc_socket(), and
 * felinc_close() gives a socket's memory back; nothing is allocated
 * while packets are being handled. felinc_shutdown() gives back the
 * rest, so FELINC can be started again. It gets its memory through
 * felinc_set_alloc() (default malloc) and sends its text through
 * felinc_set_print() (default stdout).
 *
 * Part of FELINC, the Foenix Labs Ethernet Library in C. (c) R. Kint
 **********************************************************************/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include "lan9221.h"
#include "ip4.h"
#include "udp.h"
#include "tcp.h"
#include "icmp.h"
#include "utils.h"
#include "arp.h"
#include "dhcp.h"
#include "dns.h"
#include "felinc.h"
#include "cbuf.h"
#include "prof.h"

/* felinc_shutdown() lets queued datagrams go out first, but waits at
 * most this long for it (in jiffies: 1 s)
 */
#define FELINC_SHUTDOWN_WAIT	60

/* Work done per felinc_poll() call, so the main program keeps running */
#define FELINC_RX_BUDGET	8	/* Ethernet frames received per call */
#define FELINC_TX_BUDGET	8	/* datagrams sent from socket queues per call */

/* Used when felinc_init() gets NULL for a setting */
#define FELINC_IP4_STRING_DEFAULT		"10.77.77.15"
#define FELINC_MAC_STRING_DEFAULT		"00:0D:80:61:94:AB"
#define FELINC_MASK_STRING_DEFAULT		"255.255.255.0"
#define FELINC_GATEWAY_STRING_DEFAULT	"10.77.77.1"

#define FELINC_IP4_STRING_MAX	16		/* "255.255.255.255" + 0 */
#define FELINC_MAC_STRING_MAX	18		/* "00:11:22:33:44:55" + 0 */

/* A datagram in a socket's send or receive buffer is stored as this
 * header, directly followed by 'length' data bytes.
 */
struct fe_socket_record_hdr
{
	struct sockaddr his_addr;		/* the other side: sender (receive) or destination (send) */
	size_t length;					/* number of data bytes that follow */
};

/* A socket. Programs only see its handle (the index in socket_list). */
struct fe_socket
{
	int type;			/* SOCK_DGRAM or SOCK_STREAM (POSIX) */
	int protocol;		/* IPPROTO_UDP or IPPROTO_TCP */
	size_t bufsize;		/* size of each buffer (TCP: the receive buffer in tcp.c of
						 * every connection a listening socket accepts) */
	struct sockaddr_in *socket_addr; /* address and port of this socket (0.0.0.0 = any) */
	cbuf *recv_buffer;	/* UDP: received datagrams, waiting for felinc_recvfrom() */
	cbuf *send_buffer;	/* UDP: datagrams from felinc_sendto(), sent by felinc_poll() */
	int16_t tcp_conn;	/* TCP: number of its entry in tcp.c, -1 = none yet */
	bool listening;		/* TCP: felinc_listen() was called (else: one connection) */
};

static eth_frame *rx_frame;				/* the frame being received */
static uint8_t *tx_data;				/* the datagram being sent */
static struct fe_socket *socket_list[FELINC_MAX_SOCKETS];
static felinc_stats stats;
static uint32_t last_tick;				/* jiffies at the last timer run */
static bool running = false;			/* between felinc_init() and felinc_shutdown() */

/* Only one Ethernet controller is supported for now. Not allocated:
 * felinc_get_mac() reads it, also when FELINC is stopped.
 */
static eth_controller controller_data;
static eth_controller *controller = &controller_data;

static void felinc_recv_frame( eth_frame *frame, size_t frame_length );
static void felinc_release_memory( void );
static uint16_t felinc_send_queued( uint16_t budget );

/***********************************************************************
 * felinc_copy_string (internal)
 * Copies a setting string, or its default if it is missing or too long.
 * The address parsers change the text they read (strtok), so they must
 * get a copy, never the caller's (possibly constant) string.
 *
 * Input:
 *   dst    buffer for the copy
 *   src    the setting, may be NULL
 *   dflt   the default value
 *   size   size of dst in bytes
 * Output:
 *   none (the text is in dst)
 **********************************************************************/
static void felinc_copy_string( char *dst, const char *src, const char *dflt, size_t size )
{
	if( src == NULL || strlen(src) >= size ) src = dflt;
	strcpy( dst, src );
}

/***********************************************************************
 * felinc_valid_socket (internal)
 * Checks a socket handle a program passed in.
 *
 * Input:
 *   handle   the socket handle
 * Output:
 *   true if it is a socket created by felinc_socket()
 **********************************************************************/
static bool felinc_valid_socket( int handle )
{
	return handle >= 0 && handle < FELINC_MAX_SOCKETS && socket_list[handle] != NULL;
}

/***********************************************************************
 * felinc_set_alloc
 * Sets where FELINC gets its memory: for a program (or the MCP) that
 * has its own memory to hand out. Without it FELINC uses malloc() and
 * free(). Call it before felinc_init() (or after felinc_shutdown()):
 * memory that FELINC already has must go back to where it came from.
 *
 * Input:
 *   alloc     gets a block of memory, like malloc() (NULL = malloc)
 *   release   gives a block back, like free() (NULL = free)
 * Output:
 *   true, or false if FELINC is running (nothing changes)
 **********************************************************************/
bool felinc_set_alloc( void *(*alloc)( size_t size ), void (*release)( void *block ) )
{
	if( running ) return false;
	utl_set_alloc( alloc, release );
	return true;
}

/***********************************************************************
 * felinc_set_print
 * Sets where FELINC's text goes: its messages (DHCP lease, errors)
 * and the lists of the felinc_print_ functions. Without it the text
 * goes to stdout (the screen). The function gets pieces of text that
 * already contain their newlines; FELINC adds none. Can be called at
 * any time.
 *
 * Input:
 *   print   takes a piece of text (NULL = stdout)
 * Output:
 *   none
 **********************************************************************/
void felinc_set_print( void (*print)( const char *text ) )
{
	utl_set_print( print );
}

/***********************************************************************
 * felinc_set_fatal
 * Sets the function FELINC calls when it finds an error in itself,
 * such as a send buffer given back twice: something that cannot happen
 * unless FELINC (or memory it uses) is broken. Without it FELINC prints
 * the reason and stops the program. With it, the function gets the
 * reason and FELINC drops the operation it was busy with. The function
 * must not call FELINC itself: it is called in the middle of FELINC's
 * work. To stop the network, note it there and call felinc_shutdown()
 * later from the main loop.
 *
 * Input:
 *   fatal   takes the reason as text (NULL = print it and stop)
 * Output:
 *   none
 **********************************************************************/
void felinc_set_fatal( void (*fatal)( const char *reason ) )
{
	utl_set_fatal( fatal );
}

/***********************************************************************
 * felinc_init
 * Starts FELINC: the Ethernet controller and all protocol modules.
 * Call it before anything else. After felinc_shutdown() it can be
 * called again, to start the network once more. When it fails, all
 * memory it took is given back.
 *
 * Input:
 *   mac_string      our MAC address, like "00:0D:80:61:94:AB"
 *   dhcp_flag       true: get the IP settings from a DHCP server
 *                   (felinc_poll() does that in the background;
 *                   felinc_get_ip() gives 0 until an address is
 *                   known); false: use the fixed settings
 *   ip4_address_string, mask_string, gateway_string
 *                   our IP address, subnet mask and gateway, like
 *                   "10.77.77.99"
 *                   (NULL for any setting selects its default above).
 *                   With DHCP these are the fallback: used when no
 *                   DHCP server answers within a minute
 * Output:
 *   true if FELINC started; false if the Ethernet controller did not
 *   start (there is no network then), there was not enough memory,
 *   or FELINC is already running
 **********************************************************************/
bool felinc_init( char *mac_string, bool dhcp_flag,
				char *ip4_address_string, char *mask_string, char *gateway_string )
{
	uint32_t my_ip, my_mask, my_gateway;
	mac my_mac;
	char my_mac_string[FELINC_MAC_STRING_MAX];
	char my_ip4_string[FELINC_IP4_STRING_MAX];
	char my_mask_string[FELINC_IP4_STRING_MAX];
	char my_gateway_string[FELINC_IP4_STRING_MAX];
	uint16_t i;

	if( running )
	{
		utl_print("\nFELINC_INIT: FELINC is already running\n");
		return false;
	}
	memset( &stats, 0, sizeof(stats) );
	memset( controller, 0, sizeof(*controller) );
	for( i = 0; i < FELINC_MAX_SOCKETS; i++ ) socket_list[i] = NULL;

	rx_frame = (eth_frame *)utl_alloc( sizeof(eth_frame) );
	tx_data = (uint8_t *)utl_alloc( FELINC_UDP_MAX );
	if( rx_frame == NULL || tx_data == NULL )
	{
		utl_print("\nFELINC_INIT: not enough memory\n");
		felinc_release_memory();
		return false;
	}

	felinc_copy_string( my_mac_string, mac_string, FELINC_MAC_STRING_DEFAULT, sizeof(my_mac_string) );
	if( mac_string_to_mac( &my_mac, my_mac_string ) != 0 )
	{
		strcpy( my_mac_string, FELINC_MAC_STRING_DEFAULT );
		mac_string_to_mac( &my_mac, my_mac_string );
	}

	/* The controller first: it also fills in the send/receive
	 * functions the other modules use.
	 */
	if( eth_init( controller, &my_mac ) != 0 )
	{
		utl_print("\nFELINC_INIT: Ethernet controller did not start\n");
		felinc_release_memory();
		return false;
	}

	felinc_copy_string( my_ip4_string, ip4_address_string, FELINC_IP4_STRING_DEFAULT, sizeof(my_ip4_string) );
	felinc_copy_string( my_mask_string, mask_string, FELINC_MASK_STRING_DEFAULT, sizeof(my_mask_string) );
	felinc_copy_string( my_gateway_string, gateway_string, FELINC_GATEWAY_STRING_DEFAULT, sizeof(my_gateway_string) );
	my_ip = ip4_string_to_uint32_t( my_ip4_string );
	my_mask = ip4_string_to_uint32_t( my_mask_string );
	my_gateway = ip4_string_to_uint32_t( my_gateway_string );
	tcp_init();
	if( !ip4_init( my_ip, my_mask, my_gateway ) || !icmp_init() ||
		!arp_init( controller ) || !udp_init() )
	{
		eth_shutdown();
		felinc_release_memory();
		return false;
	}
	dns_init();
	dhcp_init( my_ip, my_mask, my_gateway );
	if( dhcp_flag ) dhcp_start();

	last_tick = utl_jiffies();

	running = true;
	return true;
}

/***********************************************************************
 * felinc_poll
 * Does the network work. Call it as often as possible from the main
 * loop; it never waits. One call:
 * 1. runs the timers (ARP, DHCP, DNS), once per jiffy
 * 2. handles at most FELINC_RX_BUDGET received frames
 * 3. sends at most FELINC_TX_BUDGET datagrams from the socket buffers
 * 4. sends waiting TCP data, a few segments per connection
 *
 * Input:
 *   none
 * Output:
 *   number of frames handled (received + sent); 0 means the network
 *   was quiet (or FELINC is not running)
 **********************************************************************/
ssize_t felinc_poll( void )
{
	uint32_t now = utl_jiffies();
	uint16_t i, handled = 0;
	ssize_t length;

	if( !running ) return 0;
	PROF_START( poll );

	if( now != last_tick )
	{
		last_tick = now;
		arp_poll( now );
		dhcp_poll( now );
		dns_poll( now );
		tcp_poll( now );
	}

	/* Receive before sending: a received ARP answer can release frames
	 * that were waiting, and they then go out in this same call.
	 */
	for( i = 0; i < FELINC_RX_BUDGET; i++ )
	{
		PROF_START( rx );
		length = controller->listen( rx_frame, sizeof(eth_frame) );
		PROF_STOP( length == 0 ? PROF_RX_EMPTY : PROF_RX_FIFO, rx );
		if( length == 0 ) break;		/* nothing waiting */
		if( length > 0 )
		{
			PROF_START( handle );
			felinc_recv_frame( rx_frame, (size_t)length );
			PROF_STOP( PROF_HANDLE, handle );
			handled++;
		}
	}

	{
		PROF_START( pending );
		handled += felinc_send_queued( FELINC_TX_BUDGET );
		tcp_send_pending( now );
		PROF_STOP( PROF_PENDING, pending );
	}
	PROF_STOP( PROF_POLL, poll );
	return handled;
}

/***********************************************************************
 * felinc_find_udp_socket (internal)
 * Finds the socket a received UDP datagram belongs to.
 *
 * Input:
 *   dest_ip  the datagram's destination IP address
 *   port     the datagram's destination port
 * Output:
 *   the socket handle, or -1 if no socket is bound to that port.
 *   A socket bound to 0.0.0.0 accepts any of our addresses, and a
 *   broadcast goes to any socket bound to the port.
 **********************************************************************/
static int16_t felinc_find_udp_socket( uint32_t dest_ip, uint16_t port )
{
	int16_t i;
	struct sockaddr_in *a;

	for( i = 0; i < FELINC_MAX_SOCKETS; i++ )
	{
		if( socket_list[i] == NULL || socket_list[i]->protocol != IPPROTO_UDP ) continue;
		a = socket_list[i]->socket_addr;
		if( a->sin_port != port ) continue;
		if( a->sin_addr.s_addr == 0 || a->sin_addr.s_addr == dest_ip ||
			ip4_is_broadcast( dest_ip ) )
			return i;
	}
	return -1;
}

/***********************************************************************
 * felinc_recv_udp (internal)
 * Handles a received UDP datagram: DHCP and DNS answers go to their
 * modules, all other datagrams into the receive buffer of their socket.
 *
 * Input:
 *   ip4     the received IPv4 packet with the datagram inside (its IPv4
 *           header is already checked)
 *   for_us  true if the packet is addressed to us or is a broadcast
 * Output:
 *   none
 **********************************************************************/
static void felinc_recv_udp( ip4_pkt *ip4, bool for_us )
{
	struct fe_socket_record_hdr hdr;
	struct sockaddr_in *his = (struct sockaddr_in *)&hdr.his_addr;
	uint8_t *data;
	size_t size_of_data;
	uint16_t port;
	int16_t s;
	cbuf *rb;

	if( udp_get_info( ip4, &port, &data, &size_of_data ) < 0 )
	{
		stats.udp_bad++;
		return;
	}

	/* Checked before for_us: a DHCP answer may be sent to the address
	 * we are about to get, which is not ours yet.
	 */
	if( port == DHCP_CLIENT_PORT )
	{
		dhcp_receive( data, size_of_data );
		return;
	}
	if( !for_us )
	{
		stats.ip_not_for_us++;
		return;
	}
	if( port == DNS_CLIENT_PORT )
	{
		dns_receive( ip4->ip4_hdr.src_addr, udp_get_src_port( ip4 ), data, size_of_data );
		return;
	}

	s = felinc_find_udp_socket( ip4->ip4_hdr.dest_addr, port );
	if( s < 0 )
	{
		stats.udp_no_socket++;
		return;
	}

	/* Store as a record: sender's address and length, then the data
	 * (see struct fe_socket_record_hdr).
	 */
	memset( &hdr, 0, sizeof(hdr) );
	his->sin_family = AF_INET;
	his->sin_port = udp_get_src_port( ip4 );
	his->sin_addr.s_addr = ip4->ip4_hdr.src_addr;
	hdr.length = size_of_data;

	rb = socket_list[s]->recv_buffer;
	if( cbuf_free( rb ) < sizeof(hdr) + size_of_data )
	{
		stats.udp_socket_full++;	/* program does not read fast enough */
		return;
	}
	cbuf_put( rb, &hdr, sizeof(hdr) );
	cbuf_put( rb, data, size_of_data );
}

/***********************************************************************
 * felinc_recv_ip4 (internal)
 * Handles a received IPv4 packet: checks the header, answers pings,
 * passes UDP on to felinc_recv_udp() and TCP to tcp_receive().
 * Everything else is dropped.
 *
 * Input:
 *   ip4           the received IPv4 packet
 *   max_length    bytes of the frame after the Ethernet header: the
 *                 packet's own length may not be more
 * Output:
 *   none
 **********************************************************************/
static void felinc_recv_ip4( ip4_pkt *ip4, size_t max_length )
{
	uint32_t my_ip = ip4_get_ip();
	bool for_us;

	/* Only plain IPv4: a 20-byte header (no options), no fragments, a
	 * correct checksum and a length that fits in the frame. The length
	 * is checked because the code after this trusts it.
	 */
	if( ip4->ip4_hdr.ver_hlen != IP4_VER_HLEN ||
		(ip4->ip4_hdr.fragment & 0x3FFF) != 0 ||
		ip4->ip4_hdr.length < sizeof(ip4_header) ||
		ip4->ip4_hdr.length > max_length ||
		utl_checksum( &ip4->ip4_hdr, sizeof(ip4_header) ) != 0 )
	{
		stats.ip_bad++;
		return;
	}

	for_us = ip4->ip4_hdr.dest_addr == my_ip || ip4_is_broadcast( ip4->ip4_hdr.dest_addr );

	if( ip4->ip4_hdr.protocol == IP4_UDP )
	{
		/* Also when not for us: see the DHCP note in felinc_recv_udp() */
		felinc_recv_udp( ip4, for_us );
	}
	else if( !for_us )
	{
		stats.ip_not_for_us++;
	}
	else if( ip4->ip4_hdr.protocol == IP4_ICMP )
	{
		/* Answering a broadcast ping would make every machine answer
		 * at once: only answer pings to us.
		 */
		if( ip4->ip4_hdr.dest_addr == my_ip ) icmp_respond( ip4 );
	}
	else if( ip4->ip4_hdr.protocol == IP4_TCP )
	{
		/* TCP connections are always between two single addresses:
		 * a broadcast cannot be part of one.
		 */
		if( ip4->ip4_hdr.dest_addr != my_ip ) return;
		switch( tcp_receive( ip4 ) )
		{
			case TCP_BAD:		stats.tcp_bad++;		break;
			case TCP_NO_SOCKET:	stats.tcp_no_socket++;	break;
		}
	}
}

/***********************************************************************
 * felinc_recv_frame (internal)
 * Handles one received frame: ARP goes to the ARP module, IPv4 to
 * felinc_recv_ip4(). Other frame types are ignored.
 *
 * Input:
 *   frame         the received frame
 *   frame_length  its length in bytes
 * Output:
 *   none
 **********************************************************************/
static void felinc_recv_frame( eth_frame *frame, size_t frame_length )
{
	if( frame->header.ether_type == ETHER_TYPE_ARP )
		arp_respond( frame );
	else if( frame->header.ether_type == ETHER_TYPE_IP4 )
		felinc_recv_ip4( (ip4_pkt *)frame->data, frame_length - sizeof(eth_frame_header) );
}

/***********************************************************************
 * felinc_send_queued (internal)
 * Sends datagrams that felinc_sendto() put in the socket send buffers.
 *
 * Input:
 *   budget   maximum number of datagrams to send in this call
 * Output:
 *   number of datagrams handled
 **********************************************************************/
static uint16_t felinc_send_queued( uint16_t budget )
{
	struct fe_socket_record_hdr hdr;
	struct sockaddr_in *dest = (struct sockaddr_in *)&hdr.his_addr;
	struct fe_socket *sock;
	uint16_t i, sent = 0;

	for( i = 0; i < FELINC_MAX_SOCKETS && sent < budget; i++ )
	{
		sock = socket_list[i];
		if( sock == NULL || sock->protocol != IPPROTO_UDP ) continue;

		while( sent < budget && (size_t)cbuf_used( sock->send_buffer ) >= sizeof(hdr) )
		{
			cbuf_get( sock->send_buffer, &hdr, sizeof(hdr) );
			cbuf_get( sock->send_buffer, tx_data, hdr.length );

			if( udp_send( sock->socket_addr->sin_port, dest->sin_port,
						dest->sin_addr.s_addr, tx_data, hdr.length ) < 0 )
				stats.tx_dropped++;
			sent++;
		}
	}
	return sent;
}

/***********************************************************************
 * felinc_free_socket (internal)
 * Frees the memory of a socket. Parts that were never allocated (NULL)
 * are skipped, so it also cleans up a half-created socket.
 *
 * Input:
 *   sock   the socket
 * Output:
 *   none
 **********************************************************************/
static void felinc_free_socket( struct fe_socket *sock )
{
	if( sock->recv_buffer != NULL ) cbuf_destroy( sock->recv_buffer );
	if( sock->send_buffer != NULL ) cbuf_destroy( sock->send_buffer );
	utl_free( sock->socket_addr );
	utl_free( sock );
}

/***********************************************************************
 * felinc_new_socket (internal)
 * Creates a socket in the first free place of socket_list. A UDP
 * socket gets its two buffers here; a TCP connection keeps its data in
 * tcp.c, which must have a buffer before the program accepts the
 * connection. Used by felinc_socket() and felinc_accept().
 *
 * Input:
 *   type, protocol   SOCK_DGRAM + IPPROTO_UDP or SOCK_STREAM + IPPROTO_TCP
 *   bufsize          size of each of the two buffers in bytes
 * Output:
 *   the socket handle, or -1 (all handles in use, or not enough memory)
 **********************************************************************/
static int felinc_new_socket( int type, int protocol, size_t bufsize )
{
	struct fe_socket *new_socket;
	int handle;

	/* The handle is simply the first free place in socket_list */
	for( handle = 0; handle < FELINC_MAX_SOCKETS; handle++ )
		if( socket_list[handle] == NULL ) break;
	if( handle == FELINC_MAX_SOCKETS ) return -1;

	new_socket = (struct fe_socket *)utl_calloc( sizeof(struct fe_socket) );
	if( new_socket == NULL ) return -1;
	new_socket->type = type;
	new_socket->protocol = protocol;
	new_socket->bufsize = bufsize;
	new_socket->tcp_conn = -1;

	new_socket->socket_addr = (struct sockaddr_in *)utl_calloc( sizeof(struct sockaddr_in) );
	if( protocol == IPPROTO_UDP )
	{
		new_socket->recv_buffer = cbuf_create( bufsize );
		new_socket->send_buffer = cbuf_create( bufsize );
	}
	if( new_socket->socket_addr == NULL ||
		(protocol == IPPROTO_UDP && (new_socket->recv_buffer == NULL || new_socket->send_buffer == NULL)) )
	{
		utl_print("\nFELINC_SOCKET: not enough memory");
		felinc_free_socket( new_socket );
		return -1;
	}
	new_socket->socket_addr->sin_family = AF_INET;

	socket_list[handle] = new_socket;
	return handle;
}

/***********************************************************************
 * felinc_socket
 * Creates a socket, like the POSIX socket() call, with its own receive
 * and send buffers. Give it an address with felinc_bind().
 * It allocates memory, so preferably create sockets at start-up: a
 * program that keeps creating and closing sockets can fragment the
 * heap.
 *
 * Input:
 *   domain    AF_INET
 *   type      SOCK_DGRAM (UDP) or SOCK_STREAM (TCP)
 *   protocol  IPPROTO_UDP or IPPROTO_TCP
 *   bufsize   size of each of the two buffers in bytes.
 *             UDP: every datagram takes its length +
 *             sizeof(struct fe_socket_record_hdr) (20 bytes); a
 *             datagram that does not fit is dropped.
 *             TCP: every connection on a listening socket gets a
 *             receive buffer of this size (at most 65535 bytes, the
 *             largest TCP window). A bigger buffer lets the other
 *             side send more before it must wait for our ACK.
 * Output:
 *   the socket handle (0 .. FELINC_MAX_SOCKETS-1), or -1 on error
 *   (unsupported type, all handles in use, not enough memory, or
 *   FELINC not running)
 **********************************************************************/
int felinc_socket( int domain, int type, int protocol, size_t bufsize )
{
	bool udp = type == SOCK_DGRAM && protocol == IPPROTO_UDP;
	bool tcp = type == SOCK_STREAM && protocol == IPPROTO_TCP;

	if( !running ) return -1;		/* not started, or stopped */
	if( domain != AF_INET || (!udp && !tcp) )
	{
		utl_print("\nFELINC_SOCKET: only UDP (SOCK_DGRAM) and TCP (SOCK_STREAM) over AF_INET");
		return -1;
	}
	return felinc_new_socket( type, protocol, bufsize );
}

/***********************************************************************
 * felinc_bind
 * Gives a socket its own IP address and port, like POSIX bind(). After
 * this, datagrams to that address and port go into the socket (UDP),
 * or felinc_listen() can listen on the port (TCP).
 *
 * Input:
 *   socket         the socket
 *   ip_address     our IP address; FELINC_ANY_IP (0.0.0.0) accepts any
 *                  of our addresses (the usual choice: with DHCP our
 *                  address is not known yet and may change)
 *   port           the port number
 * Output:
 *   0 on success, -1 for an invalid socket
 **********************************************************************/
int felinc_bind( int socket, uint32_t ip_address, uint16_t port )
{
	struct sockaddr_in *a;

	if( !felinc_valid_socket( socket ) ) return -1;

	a = socket_list[socket]->socket_addr;
	a->sin_port = port;
	a->sin_addr.s_addr = ip_address;
	return 0;
}

/***********************************************************************
 * felinc_recvfrom
 * Takes the oldest received datagram from a socket, like POSIX
 * recvfrom(), but never waits: if nothing has arrived it returns 0 at
 * once, so the program's main loop keeps running.
 *
 * Input:
 *   socket    the socket
 *   buffer    where to copy the data
 *   buflen    size of buffer in bytes
 *   flags     not used
 *   src_addr  where to store the sender's address (may be NULL); use
 *             it to send an answer
 * Output:
 *   the datagram length in bytes, 0 if nothing is waiting, or -1 on
 *   error (invalid or not a UDP socket, or the datagram is bigger than
 *   buflen; it is then thrown away)
 **********************************************************************/
ssize_t felinc_recvfrom( int socket, void *buffer, size_t buflen,
						int flags, struct sockaddr *src_addr )
{
	struct fe_socket_record_hdr hdr;
	cbuf *rb;

	(void)flags;
	if( !felinc_valid_socket( socket ) || socket_list[socket]->protocol != IPPROTO_UDP ) return -1;
	rb = socket_list[socket]->recv_buffer;

	if( (size_t)cbuf_used( rb ) < sizeof(hdr) ) return 0;

	cbuf_get( rb, &hdr, sizeof(hdr) );
	if( hdr.length > buflen )
	{
		/* Must still be removed, or it would block all later datagrams */
		cbuf_get( rb, NULL, hdr.length );
		return -1;
	}
	cbuf_get( rb, buffer, hdr.length );
	if( src_addr != NULL ) memcpy( src_addr, &hdr.his_addr, sizeof(struct sockaddr) );

	return (ssize_t)hdr.length;
}

/***********************************************************************
 * felinc_sendto
 * Sends a datagram from a socket, like POSIX sendto(), but never waits:
 * the datagram goes into the socket's send buffer and felinc_poll()
 * sends it (after an ARP question, if needed).
 *
 * Input:
 *   socket     the socket (its port is used as the source port)
 *   buffer     the data to send
 *   buflen     number of bytes (at most FELINC_UDP_MAX)
 *   flags      not used
 *   dest_addr  the receiver's address and port (a struct sockaddr_in)
 * Output:
 *   buflen if the datagram was queued, or -1 (invalid or not a UDP
 *   socket, too big, or the send buffer is full: try again after
 *   felinc_poll())
 **********************************************************************/
ssize_t felinc_sendto( int socket, const void *buffer, size_t buflen,
						int flags, struct sockaddr *dest_addr )
{
	struct fe_socket_record_hdr hdr;
	cbuf *sb;

	(void)flags;
	if( !felinc_valid_socket( socket ) || dest_addr == NULL ) return -1;
	if( socket_list[socket]->protocol != IPPROTO_UDP || buflen > FELINC_UDP_MAX ) return -1;

	sb = socket_list[socket]->send_buffer;
	if( cbuf_free( sb ) < sizeof(hdr) + buflen ) return -1;

	memcpy( &hdr.his_addr, dest_addr, sizeof(struct sockaddr) );
	hdr.length = buflen;
	cbuf_put( sb, &hdr, sizeof(hdr) );
	cbuf_put( sb, buffer, buflen );

	return (ssize_t)buflen;
}

/***********************************************************************
 * felinc_listen
 * Makes a TCP socket wait for connections on its port, like POSIX
 * listen(). Bind it to a port with felinc_bind() first, then take the
 * connections with felinc_accept().
 *
 * Input:
 *   socket    a TCP socket with a port
 *   backlog   max connections that may wait for felinc_accept(); more
 *             are not answered until there is room again
 * Output:
 *   0 on success, -1 on error (not a TCP socket, no port, already
 *   listening, the port is in use, or no room in the TCP table)
 **********************************************************************/
int felinc_listen( int socket, int backlog )
{
	struct fe_socket *sock;
	size_t window;

	if( !felinc_valid_socket( socket ) ) return -1;
	sock = socket_list[socket];
	if( sock->protocol != IPPROTO_TCP || sock->socket_addr->sin_port == 0 ||
		sock->tcp_conn >= 0 ) return -1;

	/* The window field in TCP is 16 bits */
	window = sock->bufsize < 0xFFFF ? sock->bufsize : 0xFFFF;
	sock->tcp_conn = tcp_listen( sock->socket_addr->sin_port, (uint16_t)backlog, (uint16_t)window );
	sock->listening = sock->tcp_conn >= 0;
	return sock->tcp_conn >= 0 ? 0 : -1;
}

/***********************************************************************
 * felinc_accept
 * Takes a new connection from a listening TCP socket, like POSIX
 * accept(), but never waits: if no connection is waiting it returns -1
 * at once. The connection gets its own new socket, with the buffer
 * size of the listening socket; close it with felinc_close().
 *
 * Input:
 *   socket      the listening socket
 *   peer_addr   where to store the other side's address and port (a
 *               struct sockaddr_in; may be NULL)
 * Output:
 *   the new socket's handle, or -1 (nothing waiting, not listening, or
 *   no free socket: the connection then keeps waiting)
 **********************************************************************/
int felinc_accept( int socket, struct sockaddr *peer_addr )
{
	struct fe_socket *listener;
	struct sockaddr_in *peer = (struct sockaddr_in *)peer_addr;
	uint32_t his_ip;
	uint16_t his_port;
	int16_t conn;
	int handle;

	if( !felinc_valid_socket( socket ) ) return -1;
	listener = socket_list[socket];
	if( listener->protocol != IPPROTO_TCP || !listener->listening ) return -1;
	if( !tcp_can_accept( listener->tcp_conn ) ) return -1;	/* the usual case: nothing waiting */

	/* Make the socket first: without one, the connection must stay
	 * waiting in tcp.c rather than be taken and lost
	 */
	handle = felinc_new_socket( SOCK_STREAM, IPPROTO_TCP, listener->bufsize );
	if( handle < 0 ) return -1;

	conn = tcp_accept( listener->tcp_conn, &his_ip, &his_port );
	if( conn < 0 )
	{
		felinc_free_socket( socket_list[handle] );
		socket_list[handle] = NULL;
		return -1;
	}

	socket_list[handle]->tcp_conn = conn;
	socket_list[handle]->socket_addr->sin_port = listener->socket_addr->sin_port;
	if( peer != NULL )
	{
		memset( peer, 0, sizeof(*peer) );
		peer->sin_family = AF_INET;
		peer->sin_port = his_port;
		peer->sin_addr.s_addr = his_ip;
	}
	return handle;
}

/***********************************************************************
 * felinc_connect
 * Opens a TCP connection to a server, like POSIX connect(), but never
 * waits: the first call sends the request and returns
 * FELINC_CONNECT_BUSY; call it again (with the same arguments) on later
 * passes of the main loop until it returns something else. Then use
 * felinc_send() and felinc_recv() on the socket, and felinc_close()
 * when done (also after a failure).
 * Without felinc_bind() FELINC chooses our port.
 *
 * Input:
 *   socket      a TCP socket from felinc_socket()
 *   peer_addr   the server's address and port (a struct sockaddr_in)
 * Output:
 *   FELINC_CONNECT_DONE (connected), FELINC_CONNECT_BUSY (call again),
 *   FELINC_CONNECT_REFUSED (nobody listens on that port),
 *   FELINC_CONNECT_TIMEOUT (the server did not answer in 20 s), or -1
 *   (not a TCP socket, a listening socket, no IP address yet, or no
 *   room in the TCP table)
 **********************************************************************/
int felinc_connect( int socket, const struct sockaddr *peer_addr )
{
	const struct sockaddr_in *peer = (const struct sockaddr_in *)peer_addr;
	struct fe_socket *sock;
	size_t bufsize;

	if( !felinc_valid_socket( socket ) || peer == NULL ) return -1;
	sock = socket_list[socket];
	if( sock->protocol != IPPROTO_TCP || sock->listening ) return -1;

	/* Later calls: how is it going? */
	if( sock->tcp_conn >= 0 ) return tcp_connect_result( sock->tcp_conn );

	/* First call. Without our own address (DHCP still busy) the server
	 * could not answer us.
	 */
	if( ip4_get_ip() == 0 ) return -1;
	bufsize = sock->bufsize < 0xFFFF ? sock->bufsize : 0xFFFF;	/* the TCP window is 16 bits */
	sock->tcp_conn = tcp_connect( sock->socket_addr->sin_port, peer->sin_addr.s_addr,
								peer->sin_port, (uint16_t)bufsize );
	return sock->tcp_conn >= 0 ? FELINC_CONNECT_BUSY : -1;
}

/***********************************************************************
 * felinc_recv
 * Takes received data from a TCP connection, like POSIX recv(), but
 * never waits: if nothing has arrived it returns 0 at once. (POSIX
 * uses 0 for "closed"; here that is FELINC_CLOSED.)
 * FELINC_CLOSED means the other side sent everything it will send
 * (its FIN), or the connection ended (reset). Data that arrived before
 * that is returned first. After a FIN we may still send: close the
 * socket when we are done too.
 *
 * Input:
 *   socket   a socket from felinc_accept() or felinc_connect()
 *   buffer   where to copy the data
 *   buflen   size of buffer in bytes
 *   flags    not used
 * Output:
 *   the number of bytes, 0 if nothing arrived, FELINC_CLOSED if no
 *   more data will arrive, or -1 on error
 **********************************************************************/
ssize_t felinc_recv( int socket, void *buffer, size_t buflen, int flags )
{
	struct fe_socket *sock;
	size_t n;

	(void)flags;
	if( !felinc_valid_socket( socket ) ) return -1;
	sock = socket_list[socket];
	if( sock->protocol != IPPROTO_TCP || sock->tcp_conn < 0 || sock->listening ) return -1;

	n = tcp_read( sock->tcp_conn, buffer, buflen );
	if( n > 0 ) return (ssize_t)n;
	if( tcp_recv_done( sock->tcp_conn ) ) return FELINC_CLOSED;
	return 0;
}

/***********************************************************************
 * felinc_send
 * Gives data to send on a TCP connection, like POSIX send(), but never
 * waits: it takes as much as fits in the socket's send buffer, and
 * felinc_poll() sends it. Offer the rest again later. The data stays
 * in the buffer until the other side acknowledges it, so the buffer
 * only empties as fast as the other side receives.
 *
 * Input:
 *   socket   a socket from felinc_accept() or felinc_connect()
 *   buffer   the data
 *   buflen   number of bytes
 *   flags    not used
 * Output:
 *   the number of bytes taken (0 if the buffer is full),
 *   FELINC_CLOSED if the connection has ended (reset, or we closed
 *   it), or -1 on error
 **********************************************************************/
ssize_t felinc_send( int socket, const void *buffer, size_t buflen, int flags )
{
	struct fe_socket *sock;
	ssize_t n;

	(void)flags;
	if( !felinc_valid_socket( socket ) ) return -1;
	sock = socket_list[socket];
	if( sock->protocol != IPPROTO_TCP || sock->tcp_conn < 0 || sock->listening ) return -1;

	n = tcp_write( sock->tcp_conn, buffer, buflen );
	return n >= 0 ? n : FELINC_CLOSED;
}

/***********************************************************************
 * felinc_close
 * Closes a socket, like POSIX close(): frees its memory and makes the
 * handle free for a new socket. Datagrams that are still in its
 * buffers are thrown away, also the ones felinc_poll() has not sent
 * yet. An open TCP connection is closed tidily: felinc_poll() still
 * sends the data given to felinc_send(), then a FIN; data that
 * arrives after the close is thrown away.
 *
 * Input:
 *   socket   the socket
 * Output:
 *   0 on success, -1 for an invalid socket
 **********************************************************************/
int felinc_close( int socket )
{
	if( !felinc_valid_socket( socket ) ) return -1;

	if( socket_list[socket]->tcp_conn >= 0 ) tcp_release( socket_list[socket]->tcp_conn );
	felinc_free_socket( socket_list[socket] );
	socket_list[socket] = NULL;
	return 0;
}

/***********************************************************************
 * felinc_work_waiting (internal)
 * Tells whether there is still something to send: datagrams in a
 * socket's send buffer, frames waiting for an ARP answer, or TCP
 * connections whose last data or FIN is not acknowledged yet.
 *
 * Input:
 *   none
 * Output:
 *   true if felinc_poll() still has sending to do
 **********************************************************************/
static bool felinc_work_waiting( void )
{
	uint16_t i;

	if( arp_frames_waiting() > 0 ) return true;
	if( tcp_closing() ) return true;
	for( i = 0; i < FELINC_MAX_SOCKETS; i++ )
		if( socket_list[i] != NULL && socket_list[i]->send_buffer != NULL &&
			cbuf_used( socket_list[i]->send_buffer ) > 0 )
			return true;
	return false;
}

/***********************************************************************
 * felinc_finish_sending (internal)
 * Calls felinc_poll() until everything waiting is sent, or until
 * FELINC_SHUTDOWN_WAIT jiffies have passed (an ARP question that is
 * never answered would otherwise keep us here).
 *
 * Input:
 *   none
 * Output:
 *   none
 **********************************************************************/
static void felinc_finish_sending( void )
{
	uint32_t start = utl_jiffies();

	while( felinc_work_waiting() && (utl_jiffies() - start) < FELINC_SHUTDOWN_WAIT )
		felinc_poll();
}

/***********************************************************************
 * felinc_shutdown
 * Stops FELINC at the end of the program, so the machine leaves the
 * network tidily:
 * 1. TCP connections are closed with a FIN; their last data and the
 *    FIN are sent together with queued datagrams (waiting at most 1 s)
 * 2. a DHCP lease is given back to the server, so the address is free
 *    for other machines at once
 * 3. the other sockets are closed
 * 4. the Ethernet controller stops sending and receiving
 * 5. all memory FELINC took is given back
 * After this, felinc_poll() does nothing and the socket calls return
 * -1, until felinc_init() starts FELINC again. This is the only FELINC
 * call that waits (at most about 2 s).
 *
 * Input:
 *   none
 * Output:
 *   none
 **********************************************************************/
void felinc_shutdown( void )
{
	uint16_t i;

	if( !running ) return;

	/* TCP sockets first: their last data and FIN need the time below */
	for( i = 0; i < FELINC_MAX_SOCKETS; i++ )
		if( socket_list[i] != NULL && socket_list[i]->protocol == IPPROTO_TCP ) felinc_close( i );

	felinc_finish_sending();
	dhcp_stop();
	felinc_finish_sending();		/* the RELEASE may wait for an ARP answer */

	for( i = 0; i < FELINC_MAX_SOCKETS; i++ ) felinc_close( i );

	eth_shutdown();
	felinc_release_memory();
	running = false;
}

/***********************************************************************
 * felinc_release_memory (internal)
 * Gives back all memory FELINC took: the frame buffers, the packet
 * pools, the ARP queue and the buffers of TCP connections that were
 * still closing. Used by felinc_shutdown(), and by felinc_init() when
 * it fails halfway. Each part may already be gone or never have been
 * there. The Ethernet controller must be stopped first.
 *
 * Input:
 *   none
 * Output:
 *   none
 **********************************************************************/
static void felinc_release_memory( void )
{
	tcp_shutdown();
	arp_shutdown();
	udp_shutdown();
	icmp_shutdown();
	ip4_shutdown();
	utl_free( rx_frame );
	rx_frame = NULL;
	utl_free( tx_data );
	tx_data = NULL;
}

/***********************************************************************
 * felinc_get_stats
 * Returns the counters of packets FELINC dropped.
 *
 * Input:
 *   none
 * Output:
 *   pointer to the counters
 **********************************************************************/
const felinc_stats *felinc_get_stats( void )
{
	return &stats;
}

/***********************************************************************
 * felinc_print_status
 * Prints the uptime, FELINC's drop counters, the DHCP state and the
 * driver statistics on the screen.
 *
 * Input:
 *   none
 * Output:
 *   none
 **********************************************************************/
void felinc_print_status( void )
{
	uint32_t now = utl_jiffies();

	utl_print("\n*** FELINC: up %lu s (jiffies %lu)", (unsigned long)(now / 60), (unsigned long)now);
	utl_print("\n    dropped: IP bad %lu, not for us %lu, UDP bad %lu, no socket %lu, socket full %lu, TX %lu",
		(unsigned long)stats.ip_bad, (unsigned long)stats.ip_not_for_us,
		(unsigned long)stats.udp_bad, (unsigned long)stats.udp_no_socket,
		(unsigned long)stats.udp_socket_full, (unsigned long)stats.tx_dropped);
	utl_print("\n    TCP: bad %lu, refused (reset sent) %lu (more with felinc_print_tcp)",
		(unsigned long)stats.tcp_bad, (unsigned long)stats.tcp_no_socket);
	dhcp_print_status();
	eth_print_stats();
}

/***********************************************************************
 * felinc_print_tcp
 * Prints the TCP connections and counters on the screen.
 *
 * Input:
 *   none
 * Output:
 *   none
 **********************************************************************/
void felinc_print_tcp( void )
{
	utl_print("\n*** TCP: bad %lu, refused (reset sent) %lu",
		(unsigned long)stats.tcp_bad, (unsigned long)stats.tcp_no_socket);
	tcp_print_status();
}

/***********************************************************************
 * felinc_resolve
 * Looks up the IPv4 address of a name like "gitlab.com" with DNS. It
 * never waits: call it again on every pass of the main loop until the
 * result is no longer FELINC_RESOLVE_BUSY. A dotted address such as
 * "10.77.77.1" is converted at once. See dns.c for the details.
 *
 * Input:
 *   name   the name to look up
 *   ip     where to store the address (network byte order)
 * Output:
 *   FELINC_RESOLVE_DONE (ip is set), FELINC_RESOLVE_BUSY or
 *   FELINC_RESOLVE_FAILED
 **********************************************************************/
int16_t felinc_resolve( const char *name, uint32_t *ip )
{
	if( !running ) return FELINC_RESOLVE_FAILED;
	return dns_resolve( name, ip );
}

/***********************************************************************
 * felinc_get_ip
 * Returns our current IPv4 address.
 *
 * Input:
 *   none
 * Output:
 *   our address (network byte order), or 0 while DHCP is still
 *   looking for one
 **********************************************************************/
uint32_t felinc_get_ip( void )
{
	return ip4_get_ip();
}

/***********************************************************************
 * felinc_get_ntp_server
 * Returns the time server that the DHCP server handed out (DHCP option
 * 42), for a program that wants the time with SNTP.
 *
 * Input:
 *   none
 * Output:
 *   the time server's address (network byte order), or 0 when there is
 *   none: no DHCP lease (yet), fixed settings, or the DHCP server did
 *   not send one
 **********************************************************************/
uint32_t felinc_get_ntp_server( void )
{
	return dhcp_get_ntp();
}

/***********************************************************************
 * felinc_get_mac
 * Copies our MAC address.
 *
 * Input:
 *   mac_address   buffer of 6 bytes for the address
 * Output:
 *   none (the address is in mac_address)
 **********************************************************************/
void felinc_get_mac( uint8_t *mac_address )
{
	memcpy( mac_address, controller->mac_addr.addr, 6 );
}

/***********************************************************************
 * felinc_print_arp
 * Prints the ARP table on the screen (see arp_print_cache).
 *
 * Input:
 *   none
 * Output:
 *   none
 **********************************************************************/
void felinc_print_arp( void )
{
	arp_print_cache();
}

/***********************************************************************
 * felinc_print_dns
 * Prints the DNS server and the DNS cache on the screen (see
 * dns_print_cache).
 *
 * Input:
 *   none
 * Output:
 *   none
 **********************************************************************/
void felinc_print_dns( void )
{
	dns_print_cache();
}
