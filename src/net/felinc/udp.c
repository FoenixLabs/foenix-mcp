/***********************************************************************
 * UDP.C - UDP for FELINC
 *
 * Checks received UDP datagrams and builds outgoing ones. Delivering
 * received data to the right socket happens in felinc.c.
 *
 * Part of FELINC, the Foenix Labs Ethernet Library in C. (c) R. Kint
 **********************************************************************/
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "ip4.h"
#include "udp.h"
#include "utils.h"

/* The UDP checksum also covers these fields of the IPv4 header, so a
 * datagram that was delivered to the wrong address is detected too.
 * They are summed as this "pseudo header"; it is never sent.
 */
typedef struct udp_pseudo_header
{
	uint32_t    src_address;    /* sender's IP address */
	uint32_t    dest_address;   /* receiver's IP address */
	uint8_t     zero;           /* always 0 */
	uint8_t     protocol;       /* IP4_UDP */
	uint16_t    udp_length;     /* UDP header + data, in bytes */
} udp_pseudo_header;

static utl_pool *udp_pool;		/* buffers to build outgoing datagrams in */

/***********************************************************************
 * udp_init
 * Sets up the UDP module: creates its buffers for outgoing datagrams.
 *
 * Input:
 *   none
 * Output:
 *   true, or false if there is not enough memory
 **********************************************************************/
bool udp_init( void )
{
	udp_pool = utl_pool_create( sizeof(udp_pkt), 2 );
	if( udp_pool == NULL )
	{
		utl_print("\nUDP_INIT: not enough memory");
		return false;
	}
	return true;
}

/***********************************************************************
 * udp_shutdown
 * Gives back the module's buffers (see felinc_shutdown).
 *
 * Input:
 *   none
 * Output:
 *   none
 **********************************************************************/
void udp_shutdown( void )
{
	utl_pool_destroy( udp_pool );
	udp_pool = NULL;
}

/***********************************************************************
 * udp_calc_checksum (internal)
 * Computes the UDP checksum over the pseudo header (see above) and the
 * whole datagram. The checksum field itself counts as 0.
 *
 * Input:
 *   ip4_src_address   sender's IP address
 *   ip4_dest_address  receiver's IP address
 *   udp               the datagram (its header length must be filled in)
 * Output:
 *   the checksum. A result of 0 must be sent as 0xFFFF, because 0 in
 *   the header means "no checksum".
 **********************************************************************/
static uint16_t udp_calc_checksum( uint32_t ip4_src_address, uint32_t ip4_dest_address,
									udp_pkt *udp )
{
	udp_pseudo_header pseudo;
	uint16_t saved = udp->hdr.checksum;
	uint32_t sum;

	pseudo.src_address = ip4_src_address;
	pseudo.dest_address = ip4_dest_address;
	pseudo.zero = 0;
	pseudo.protocol = IP4_UDP;
	pseudo.udp_length = udp->hdr.length;

	udp->hdr.checksum = 0;
	sum = utl_sum16( 0, &pseudo, sizeof(pseudo) );
	sum = utl_sum16( sum, udp, udp->hdr.length );
	udp->hdr.checksum = saved;

	return utl_checksum_finish( sum );
}

/***********************************************************************
 * udp_get_info
 * Checks a received UDP datagram and tells where it is going and what
 * is in it. Lengths come from the network, so they are checked before
 * anything is trusted.
 *
 * Input:
 *   ip4           the received IPv4 packet with the datagram inside
 *   port          where to store the destination port
 *   data          where to store a pointer to the data
 *   size_of_data  where to store the number of data bytes
 * Output:
 *   0 if the datagram is fine (port, data and size_of_data are set),
 *   -1 if its length or checksum is wrong (throw it away)
 **********************************************************************/
ssize_t udp_get_info( ip4_pkt *ip4, uint16_t *port, uint8_t **data, size_t *size_of_data )
{
	udp_pkt *udp = (udp_pkt *)ip4->data;
	size_t ip_payload = ip4->ip4_hdr.length - sizeof(ip4_header);
	uint16_t calculated;

	if( udp->hdr.length < sizeof(udp_header) || udp->hdr.length > ip_payload )
		return -1;

	if( udp->hdr.checksum != 0 )	/* 0: the sender did not compute one */
	{
		calculated = udp_calc_checksum( ip4->ip4_hdr.src_addr, ip4->ip4_hdr.dest_addr, udp );
		if( calculated == 0 ) calculated = 0xFFFF;
		if( calculated != udp->hdr.checksum ) return -1;
	}

	*port = udp->hdr.dest_port;
	*data = udp->data;
	*size_of_data = udp->hdr.length - sizeof(udp_header);
	return 0;
}

/***********************************************************************
 * udp_get_src_port
 * Tells from which port a received UDP datagram was sent, so an answer
 * can go back to it.
 *
 * Input:
 *   ip4    the received IPv4 packet with the datagram inside
 * Output:
 *   the sender's port
 **********************************************************************/
uint16_t udp_get_src_port( ip4_pkt *ip4 )
{
	return ((udp_pkt *)ip4->data)->hdr.src_port;
}

/***********************************************************************
 * udp_send
 * Builds a UDP datagram and sends it in an IPv4 packet.
 *
 * Input:
 *   src_port          our port (the receiver answers to it)
 *   dest_port         the receiver's port
 *   ip4_dest_address  the receiver's IP address
 *   data              the data to send
 *   data_length       number of data bytes (at most FELINC_UDP_MAX)
 * Output:
 *   0 = sent, 1 = waiting for ARP, -1 = not sent
 **********************************************************************/
ssize_t udp_send( uint16_t src_port, uint16_t dest_port,
		uint32_t ip4_dest_address, uint8_t *data, size_t data_length )
{
	udp_pkt *udp;
	ssize_t result;

	if( data_length > sizeof(udp->data) ) return -1;

	udp = (udp_pkt *)utl_pool_get( udp_pool );
	if( udp == NULL ) return -1;
	udp->hdr.src_port = src_port;
	udp->hdr.dest_port = dest_port;
	udp->hdr.length = (uint16_t)(data_length + sizeof(udp_header));
	memcpy( udp->data, data, data_length );
	udp->hdr.checksum = udp_calc_checksum( ip4_get_ip(), ip4_dest_address, udp );
	if( udp->hdr.checksum == 0 ) udp->hdr.checksum = 0xFFFF;	/* 0 would mean "no checksum" */

	result = ip4_send( ip4_dest_address, IP4_UDP, (uint8_t *)udp, udp->hdr.length );

	utl_pool_put( udp_pool, udp );
	return result;
}
