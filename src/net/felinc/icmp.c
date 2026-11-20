/***********************************************************************
 * ICMP.C - ICMP for FELINC
 *
 * Answers "ping" (ICMP echo request) so other machines can check that
 * the Foenix is on the network. Other ICMP messages are ignored.
 *
 * Part of FELINC, the Foenix Labs Ethernet Library in C. (c) R. Kint
 **********************************************************************/
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "icmp.h"
#include "ip4.h"
#include "utils.h"
#include "arp.h"
#include "lan9221.h"

static utl_pool *icmp_pool;		/* buffers to build replies in */

static void icmp_echo_reply( ip4_pkt *request );

/***********************************************************************
 * icmp_init
 * Sets up the ICMP module: creates its buffers for replies.
 *
 * Input:
 *   none
 * Output:
 *   true, or false if there is not enough memory
 **********************************************************************/
bool icmp_init( void )
{
	icmp_pool = utl_pool_create( sizeof(ip4_pkt), 2 );
	if( icmp_pool == NULL )
	{
		utl_print("\nICMP_INIT: not enough memory");
		return false;
	}
	return true;
}

/***********************************************************************
 * icmp_shutdown
 * Gives back the module's buffers (see felinc_shutdown).
 *
 * Input:
 *   none
 * Output:
 *   none
 **********************************************************************/
void icmp_shutdown( void )
{
	utl_pool_destroy( icmp_pool );
	icmp_pool = NULL;
}

/***********************************************************************
 * icmp_respond
 * Handles a received ICMP message: answers echo requests (ping).
 *
 * Input:
 *   request   the received IPv4 packet with the ICMP message inside
 *             (already checked and addressed to us, see felinc.c)
 * Output:
 *   none
 **********************************************************************/
void icmp_respond( ip4_pkt *request )
{
	icmp_pkt *icmp = (icmp_pkt *)request->data;

	if( icmp->icmp_hdr.type == ICMP_ECHO_REQUEST )
		icmp_echo_reply( request );
}

/***********************************************************************
 * icmp_echo_reply (internal)
 * Answers a ping. An echo reply must contain exactly the same data as
 * the request, so the easiest way is to copy the whole request, change
 * the type, swap the addresses and compute the checksums again.
 *
 * Input:
 *   request   the received IPv4 packet with the echo request
 * Output:
 *   none
 **********************************************************************/
static void icmp_echo_reply( ip4_pkt *request )
{
	ip4_pkt *response;
	icmp_pkt *icmp_response;
	uint16_t length = request->ip4_hdr.length;

	/* The length comes from the network: never copy more than fits */
	if( length < sizeof(ip4_header) + sizeof(icmp_header) || length > sizeof(ip4_pkt) )
		return;

	response = (ip4_pkt *)utl_pool_get( icmp_pool );
	if( response == NULL ) return;		/* cannot happen; the reply is skipped */
	memcpy( response, request, length );

	icmp_response = (icmp_pkt *)response->data;
	icmp_response->icmp_hdr.type = ICMP_ECHO_REPLY;
	icmp_response->icmp_hdr.chksum = 0;
	icmp_response->icmp_hdr.chksum = utl_checksum( icmp_response, length - sizeof(ip4_header) );

	response->ip4_hdr.src_addr = request->ip4_hdr.dest_addr;
	response->ip4_hdr.dest_addr = request->ip4_hdr.src_addr;
	response->ip4_hdr.checksum = 0;
	response->ip4_hdr.checksum = utl_checksum( &response->ip4_hdr, sizeof(ip4_header) );

	/* Back to the sender: directly or through the gateway (ARP may queue it) */
	arp_send_ip( ip4_next_hop( response->ip4_hdr.dest_addr ), ETHER_TYPE_IP4,
				response, response->ip4_hdr.length );

	utl_pool_put( icmp_pool, response );
}
