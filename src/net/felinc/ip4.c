/***********************************************************************
 * IP4.C - IPv4 for FELINC
 *
 * Builds and sends IPv4 packets, decides where a packet must go first
 * (directly to the receiver or to the gateway) and holds our IPv4
 * settings. Received packets are checked and dispatched in felinc.c.
 * Not supported (on purpose): IP options and fragments.
 *
 * Thanks to the BARR group.
 *
 * Part of FELINC, the Foenix Labs Ethernet Library in C. (c) R. Kint
 **********************************************************************/
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ip4.h"
#include "utils.h"
#include "ethernet.h"
#include "arp.h"
#include "lan9221.h"

static struct ip4_configuration ip4_settings;	/* our IPv4 settings */
static struct ip4_configuration *ip4_cfg = &ip4_settings;
static utl_pool *ip4_pool;				/* buffers to build outgoing packets in */

/***********************************************************************
 * ip4_init
 * Sets up the IPv4 module: creates its packet buffers and stores our
 * IPv4 settings.
 *
 * Input:
 *   my_ip       our IP address
 *   my_mask     subnet mask
 *   my_gateway  router for addresses outside our subnet
 * Output:
 *   true, or false if there is not enough memory
 **********************************************************************/
bool ip4_init( uint32_t my_ip, uint32_t my_mask,
									uint32_t my_gateway )
{
	/* 4 buffers: sending can nest (UDP -> IP -> ARP request) */
	ip4_pool = utl_pool_create( sizeof(ip4_pkt), 4 );
	if( ip4_pool == NULL )
	{
		utl_print("\nIP4_INIT: not enough memory");
		return false;
	}

	memset( ip4_cfg, 0, sizeof(*ip4_cfg) );
	ip4_config( my_ip, my_mask, my_gateway );
	return true;
}

/***********************************************************************
 * ip4_shutdown
 * Gives back the module's buffers (see felinc_shutdown).
 * Our settings stay readable, so ip4_get_ip() and the status still
 * work.
 *
 * Input:
 *   none
 * Output:
 *   none
 **********************************************************************/
void ip4_shutdown( void )
{
	utl_pool_destroy( ip4_pool );
	ip4_pool = NULL;
}

/***********************************************************************
 * ip4_config
 * Stores our IPv4 settings and works out the broadcast address.
 *
 * Input:
 *   ip       our IP address; 0 = no address (while DHCP is asking a
 *            server; we then only send and receive broadcasts)
 *   mask     subnet mask
 *   gateway  router for addresses outside our subnet
 * Output:
 *   none
 **********************************************************************/
void ip4_config( uint32_t ip, uint32_t mask, uint32_t gateway )
{
	ip4_cfg->ip = ip;
	ip4_cfg->mask = mask;
	ip4_cfg->gateway = gateway;
	ip4_cfg->broadcast = ip4_calc_broadcast();
}

/***********************************************************************
 * ip4_next_hop
 * Decides which machine on our LAN a packet must be sent to: hosts on
 * our own subnet are reached directly, everything else goes through
 * the gateway (router).
 *
 * Input:
 *   dest   final destination IP address
 * Output:
 *   IP address of the machine on our LAN to send the frame to
 **********************************************************************/
uint32_t ip4_next_hop( uint32_t dest )
{
	if( (dest & ip4_cfg->mask) == (ip4_cfg->ip & ip4_cfg->mask) )
		return dest;
	return ip4_cfg->gateway;
}

/***********************************************************************
 * ip4_is_broadcast
 * Tells whether an address is a broadcast: either the "limited"
 * broadcast 255.255.255.255 or the broadcast address of our subnet.
 *
 * Input:
 *   dest   IP address
 * Output:
 *   true for a broadcast address
 **********************************************************************/
bool ip4_is_broadcast( uint32_t dest )
{
	return dest == 0xFFFFFFFFUL || dest == ip4_cfg->broadcast;
}

/***********************************************************************
 * ip4_build (internal)
 * Fills in an IPv4 header for an outgoing packet and copies the data
 * behind it.
 *
 * Input:
 *   ip4          buffer for the packet
 *   ip4_dest     destination IP address
 *   protocol     IP4_ICMP, IP4_UDP, ...
 *   data         the packet contents (for example a UDP datagram)
 *   data_length  length of data in bytes
 * Output:
 *   none (the packet is in ip4)
 **********************************************************************/
static void ip4_build( ip4_pkt *ip4, uint32_t ip4_dest, uint8_t protocol,
						const uint8_t *data, size_t data_length )
{
	/* The ID must differ per packet: receivers use it to tell the
	 * pieces of different fragmented packets apart.
	 */
	static uint16_t ip4_id = 0;

	ip4->ip4_hdr.ver_hlen = IP4_VER_HLEN;
	ip4->ip4_hdr.service = 0;
	ip4->ip4_hdr.length = (uint16_t)(sizeof(ip4_header) + data_length);
	ip4->ip4_hdr.id = ip4_id++;
	ip4->ip4_hdr.fragment = 0x0000;
	ip4->ip4_hdr.ttl = 64;
	ip4->ip4_hdr.protocol = protocol;
	ip4->ip4_hdr.src_addr = ip4_get_ip();
	ip4->ip4_hdr.dest_addr = ip4_dest;
	ip4->ip4_hdr.checksum = 0;		/* must be 0 while computing it */
	ip4->ip4_hdr.checksum = utl_checksum( ip4, sizeof(ip4_header) );
	utl_copy( ip4->data, data, data_length );
}

/***********************************************************************
 * ip4_send
 * Sends data as one IPv4 packet. Broadcasts go to everybody on the
 * LAN; other packets go to the next hop (see ip4_next_hop). If the
 * next hop's MAC address is not known yet, ARP keeps the packet until
 * it is.
 *
 * Input:
 *   ip4_dest     destination IP address
 *   protocol     IP4_ICMP, IP4_UDP, ...
 *   data         the packet contents
 *   data_length  length of data in bytes (at most MTU - 20)
 * Output:
 *   0 = sent, 1 = waiting for ARP, -1 = not sent (too big, or ARP
 *   queue full)
 **********************************************************************/
int16_t ip4_send( uint32_t ip4_dest, uint8_t protocol, uint8_t *data, size_t data_length )
{
	ip4_pkt *ip4;
	int16_t result;

	if( data_length > sizeof(ip4->data) ) return -1;

	ip4 = (ip4_pkt *)utl_pool_get( ip4_pool );
	if( ip4 == NULL ) return -1;
	ip4_build( ip4, ip4_dest, protocol, data, data_length );

	if( ip4_is_broadcast( ip4_dest ) )
	{
		eth_send_broadcast( ETHER_TYPE_IP4, ip4, ip4->ip4_hdr.length );
		result = 0;
	}
	else
		result = arp_send_ip( ip4_next_hop( ip4_dest ), ETHER_TYPE_IP4,
								ip4, ip4->ip4_hdr.length );

	utl_pool_put( ip4_pool, ip4 );
	return result;
}

/***********************************************************************
 * ip4_broadcast
 * Sends data as one IPv4 packet to everybody on our subnet.
 *
 * Input:
 *   protocol     IP4_ICMP, IP4_UDP, ...
 *   data         the packet contents
 *   data_length  length of data in bytes
 * Output:
 *   none
 **********************************************************************/
void ip4_broadcast( uint8_t protocol, uint8_t *data, size_t data_length )
{
	ip4_send( ip4_get_broadcast(), protocol, data, data_length );
}

/***********************************************************************
 * ip4_calc_broadcast
 * Works out the broadcast address of our subnet: our address with all
 * host bits (the bits that are 0 in the mask) set to 1.
 *
 * Input:
 *   none (uses our IP address and mask)
 * Output:
 *   the subnet broadcast address, e.g. 10.77.77.255
 **********************************************************************/
uint32_t ip4_calc_broadcast( void )
{
	return ip4_get_ip() | ~ip4_get_mask();
}

/***********************************************************************
 * ip4_get_ip, ip4_get_mask, ip4_get_gateway, ip4_get_broadcast
 * Return our current IPv4 settings.
 *
 * Input:
 *   none
 * Output:
 *   our IP address / subnet mask / gateway / subnet broadcast address
 **********************************************************************/
uint32_t ip4_get_ip( void )
{
	return ip4_cfg->ip;
}

uint32_t ip4_get_mask( void )
{
	return ip4_cfg->mask;
}

uint32_t ip4_get_gateway( void )
{
	return ip4_cfg->gateway;
}

uint32_t ip4_get_broadcast( void )
{
	return ip4_cfg->broadcast;
}

/***********************************************************************
 * ip4_uint32_t_to_string
 * Writes an IPv4 address as text, like "10.77.77.99".
 *
 * Input:
 *   ip4_string    buffer for the text, at least 16 bytes
 *   ip4_uint32_t  the address
 * Output:
 *   none (the text is in ip4_string)
 **********************************************************************/
void ip4_uint32_t_to_string( char *ip4_string, uint32_t ip4_uint32_t )
{
	uint8_t *ip = (uint8_t *)&ip4_uint32_t;	/* network order: first byte first */

	sprintf( ip4_string, "%u.%u.%u.%u",
		(unsigned)ip[0], (unsigned)ip[1], (unsigned)ip[2], (unsigned)ip[3] );
}

/***********************************************************************
 * ip4_string_to_uint32_t
 * Reads an IPv4 address from text like "10.77.77.99".
 * Note: the text is changed while it is read (strtok puts 0 bytes in
 * it), so pass a copy if you still need the text afterwards.
 *
 * Input:
 *   ip4_string   the text
 * Output:
 *   the address, or 0 if the text does not hold 4 numbers
 **********************************************************************/
uint32_t ip4_string_to_uint32_t( char *ip4_string )
{
	uint32_t result;
	uint8_t ip[4];
	char *token;
	uint8_t i = 0;

	if( ip4_string == NULL ) return 0;

	token = strtok( ip4_string, "." );
	while( i < 4 && token != NULL )
	{
		ip[i++] = (uint8_t)atoi( token );
		token = strtok( NULL, "." );
	}
	if( i != 4 ) return 0;

	memcpy( &result, ip, 4 );
	return result;
}
