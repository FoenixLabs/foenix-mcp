/***********************************************************************
 * ARP.C - ARP (Address Resolution Protocol, RFC 826) for FELINC
 *
 * On Ethernet a frame must carry the receiver's MAC address, but
 * programs only know IP addresses. ARP finds the MAC address that
 * belongs to an IP address by broadcasting "who has IP x?"; the owner
 * answers with its MAC address. This module:
 * - keeps the answers in a fixed-size table (ARP_CACHE_SIZE entries);
 *   when it is full, the entry that was confirmed longest ago is reused
 * - keeps frames for an address that is not resolved yet in a small
 *   queue (ARP_PENDING_SLOTS) and sends them when the answer arrives,
 *   so the first packet to a new address is not lost
 * - repeats unanswered questions every ARP_RETRY_JIFFIES and gives up
 *   (dropping the waiting frames) after ARP_MAX_TRIES
 * - forgets entries after ARP_ENTRY_TTL, because machines can change
 *   their network card or address
 * - answers questions for our own IP address
 * No memory is allocated after arp_init().
 *
 * Part of FELINC, the Foenix Labs Ethernet Library in C. (c) R. Kint
 **********************************************************************/
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include "arp.h"
#include "ethernet.h"
#include "ip4.h"
#include "utils.h"
#include "lan9221.h"

/* States of an ARP table entry */
#define ARP_FREE		0	/* entry not in use */
#define ARP_PENDING		1	/* question sent, waiting for the answer */
#define ARP_RESOLVED	2	/* MAC address known */

typedef struct arp_entry
{
	uint32_t ip;
	uint8_t mac[HLEN_ETHERNET];
	uint8_t state;		/* ARP_FREE, ARP_PENDING or ARP_RESOLVED */
	uint8_t tries;		/* questions sent so far (while pending) */
	uint32_t stamp;		/* jiffies: last question (pending) or last confirmation (resolved) */
} arp_entry;

/* A frame waiting for the MAC address of its next hop */
typedef struct arp_waiting_frame
{
	bool used;
	uint32_t next_hop;		/* IP address whose MAC address we wait for */
	uint16_t ether_type;
	uint16_t length;
	uint8_t data[MTU];
} arp_waiting_frame;

static eth_controller *this_controller;
static arp_entry arp_table[ARP_CACHE_SIZE];
static arp_waiting_frame *waiting;		/* ARP_PENDING_SLOTS frames, allocated once */

/* Just past the last waiting frame. The waiting frames are walked with
 * a pointer, not with a 16-bit index: Calypsi 5.18 miscompiles
 * waiting[i] in such loops (see "A note on the Calypsi compiler" in
 * README.md).
 */
#define ARP_WAITING_END		(waiting + ARP_PENDING_SLOTS)

/***********************************************************************
 * arp_init
 * Sets up the ARP module: empty table, empty waiting queue.
 * Call it after eth_init(), because ARP sends through the controller's
 * send functions.
 *
 * Input:
 *   controller   the Ethernet controller to send ARP frames with
 * Output:
 *   true, or false if there is not enough memory
 **********************************************************************/
bool arp_init( eth_controller *controller )
{
	arp_waiting_frame *w;

	this_controller = controller;
	memset( arp_table, 0, sizeof(arp_table) );

	waiting = (arp_waiting_frame *)utl_alloc( ARP_PENDING_SLOTS * sizeof(arp_waiting_frame) );
	if( waiting == NULL )
	{
		utl_print("\nARP_INIT: no memory for waiting frames");
		return false;
	}
	for( w = waiting; w < ARP_WAITING_END; w++ ) w->used = false;
	return true;
}

/***********************************************************************
 * arp_shutdown
 * Gives back the module's buffers (see felinc_shutdown).
 * Frames still waiting for an ARP answer are dropped.
 *
 * Input:
 *   none
 * Output:
 *   none
 **********************************************************************/
void arp_shutdown( void )
{
	utl_free( waiting );
	waiting = NULL;
}

/***********************************************************************
 * arp_find (internal)
 * Looks up an IP address in the ARP table.
 *
 * Input:
 *   ip     the IP address
 * Output:
 *   the entry (pending or resolved), or NULL if the address is unknown
 **********************************************************************/
static arp_entry *arp_find( uint32_t ip )
{
	uint16_t i;

	for( i = 0; i < ARP_CACHE_SIZE; i++ )
		if( arp_table[i].state != ARP_FREE && arp_table[i].ip == ip )
			return &arp_table[i];
	return NULL;
}

/***********************************************************************
 * arp_alloc (internal)
 * Finds a table entry for a new address: a free one, or else the
 * resolved entry that was confirmed longest ago. Pending entries are
 * never reused, because frames are waiting for them.
 *
 * Input:
 *   none
 * Output:
 *   an entry the caller may overwrite, or NULL if all entries are
 *   pending
 **********************************************************************/
static arp_entry *arp_alloc( void )
{
	uint16_t i;
	arp_entry *oldest = NULL;
	uint32_t now = utl_jiffies();

	for( i = 0; i < ARP_CACHE_SIZE; i++ )
	{
		if( arp_table[i].state == ARP_FREE ) return &arp_table[i];
		if( arp_table[i].state == ARP_RESOLVED &&
			(oldest == NULL || (now - arp_table[i].stamp) > (now - oldest->stamp)) )
			oldest = &arp_table[i];
	}
	return oldest;
}

/***********************************************************************
 * arp_release_waiting (internal)
 * Empties the waiting queue for one IP address: the frames are sent
 * when its MAC address is known, or thrown away when ARP gave up.
 *
 * Input:
 *   ip     the IP address the frames were waiting for
 *   mac    its MAC address to send the frames to, or NULL to drop them
 * Output:
 *   none
 **********************************************************************/
static void arp_release_waiting( uint32_t ip, const uint8_t *mac )
{
	arp_waiting_frame *w;

	for( w = waiting; w < ARP_WAITING_END; w++ )
	{
		if( w->used && w->next_hop == ip )
		{
			if( mac != NULL )
				this_controller->send_frame( mac, w->ether_type, w->data, w->length );
			w->used = false;
		}
	}
}

/***********************************************************************
 * arp_store (internal)
 * Stores (or refreshes) the MAC address of an IP address. If frames
 * were waiting for this address, they are sent now.
 *
 * Input:
 *   e      the table entry to fill
 *   ip     the IP address
 *   mac    its MAC address
 * Output:
 *   none
 **********************************************************************/
static void arp_store( arp_entry *e, uint32_t ip, const uint8_t *mac )
{
	bool was_pending = (e->state == ARP_PENDING);

	e->ip = ip;
	memcpy( e->mac, mac, HLEN_ETHERNET );
	e->state = ARP_RESOLVED;
	e->tries = 0;
	e->stamp = utl_jiffies();

	if( was_pending ) arp_release_waiting( ip, mac );
}

/***********************************************************************
 * arp_send_request (internal)
 * Broadcasts the question "who has this IP address?".
 *
 * Input:
 *   ip     the IP address we want the MAC address of
 * Output:
 *   none
 **********************************************************************/
static void arp_send_request( uint32_t ip )
{
	arp_pkt req;
	uint32_t my_ip = ip4_get_ip();

	req.htype = HTYPE_ETHERNET;
	req.ptype = PTYPE_IP4;
	req.hlen = HLEN_ETHERNET;
	req.plen = PLEN_IP4;
	req.oper = OPER_REQUEST;
	memcpy( req.sha, eth_get_mac_address(), HLEN_ETHERNET );
	memcpy( req.spa, &my_ip, PLEN_IP4 );
	memset( req.tha, 0, HLEN_ETHERNET );	/* unknown: that is the question */
	memcpy( req.tpa, &ip, PLEN_IP4 );

	this_controller->send_broadcast( ETHER_TYPE_ARP, &req, sizeof(req) );
}

/***********************************************************************
 * arp_send_ip
 * Sends a frame to a machine on our LAN, known by its IP address.
 * If its MAC address is known the frame goes out at once. If not, the
 * frame waits in the queue, a question is broadcast (unless one is
 * already running) and the frame is sent when the answer arrives.
 *
 * Input:
 *   next_hop     IP address of the machine on our LAN (the receiver
 *                itself or the gateway, see ip4_next_hop)
 *   ether_type   type of the frame contents, ETHER_TYPE_IP4 for IPv4
 *   data         the frame contents
 *   data_length  length of data in bytes (at most MTU)
 * Output:
 *   0 = sent, 1 = waiting for the ARP answer, -1 = dropped (too big,
 *   waiting queue full, or ARP table full of pending questions)
 **********************************************************************/
int16_t arp_send_ip( uint32_t next_hop, uint16_t ether_type,
						const void *data, uint16_t data_length )
{
	arp_entry *e;
	arp_waiting_frame *w;

	if( data_length > MTU ) return -1;

	e = arp_find( next_hop );
	if( e != NULL && e->state == ARP_RESOLVED )
		return this_controller->send_frame( e->mac, ether_type, data, data_length ) == 0 ? 0 : -1;

	/* Not resolved: ask, unless a question is already running */
	if( e == NULL )
	{
		e = arp_alloc();
		if( e == NULL ) return -1;
		e->ip = next_hop;
		e->state = ARP_PENDING;
		e->tries = 1;
		e->stamp = utl_jiffies();
		arp_send_request( next_hop );
	}

	/* Keep the frame until the answer arrives */
	for( w = waiting; w < ARP_WAITING_END; w++ )
	{
		if( !w->used )
		{
			w->used = true;
			w->next_hop = next_hop;
			w->ether_type = ether_type;
			w->length = data_length;
			memcpy( w->data, data, data_length );
			return 1;
		}
	}
	return -1;
}

/***********************************************************************
 * arp_poll
 * The ARP timers: repeats unanswered questions, gives up after
 * ARP_MAX_TRIES (dropping the waiting frames) and forgets entries
 * older than ARP_ENTRY_TTL. felinc_poll() calls this once per jiffy.
 *
 * Input:
 *   now    current time in jiffies (utl_jiffies)
 * Output:
 *   none
 **********************************************************************/
void arp_poll( uint32_t now )
{
	uint16_t i;
	arp_entry *e;

	for( i = 0; i < ARP_CACHE_SIZE; i++ )
	{
		e = &arp_table[i];
		if( e->state == ARP_PENDING && (now - e->stamp) >= ARP_RETRY_JIFFIES )
		{
			if( e->tries >= ARP_MAX_TRIES )
			{
				arp_release_waiting( e->ip, NULL );
				e->state = ARP_FREE;
			}
			else
			{
				e->tries++;
				e->stamp = now;
				arp_send_request( e->ip );
			}
		}
		else if( e->state == ARP_RESOLVED && (now - e->stamp) >= ARP_ENTRY_TTL )
		{
			e->state = ARP_FREE;
		}
	}
}

/***********************************************************************
 * arp_respond
 * Handles a received ARP frame (question or answer):
 * - learns the sender's MAC address. RFC 826 says: always update an
 *   address we already know (this also handles "gratuitous ARP", a
 *   machine announcing a new MAC address), but only add a new address
 *   when the frame is meant for us. Otherwise every ARP broadcast on
 *   the LAN would fill our table.
 * - answers when somebody asks for our own IP address.
 *
 * Input:
 *   frame  the received Ethernet frame with the ARP packet inside
 * Output:
 *   none
 **********************************************************************/
void arp_respond( eth_frame *frame )
{
	arp_pkt *in = (arp_pkt *)(frame->data);
	arp_pkt reply;
	arp_entry *e;
	uint32_t my_ip = ip4_get_ip();
	uint32_t spa, tpa;
	bool for_me;

	/* We only speak ARP for IPv4 over Ethernet */
	if( in->htype != HTYPE_ETHERNET || in->ptype != PTYPE_IP4 ||
		in->hlen != HLEN_ETHERNET || in->plen != PLEN_IP4 )
		return;

	memcpy( &spa, in->spa, PLEN_IP4 );		/* copied: the fields are not 32-bit aligned */
	memcpy( &tpa, in->tpa, PLEN_IP4 );
	/* Without an address (DHCP still asking) nothing is for us */
	for_me = (my_ip != 0 && tpa == my_ip);

	/* Sender 0.0.0.0 is a machine checking whether an address is free;
	 * our own address as sender is not worth storing.
	 */
	if( spa == 0 || spa == my_ip ) return;

	e = arp_find( spa );
	if( e == NULL && for_me )
		e = arp_alloc();				/* may be NULL: table full of questions */
	if( e != NULL )
		arp_store( e, spa, in->sha );

	/* Somebody asks for our MAC address: answer directly (we know theirs) */
	if( for_me && in->oper == OPER_REQUEST )
	{
		reply = *in;
		reply.oper = OPER_REPLY;
		memcpy( reply.tha, in->sha, HLEN_ETHERNET );
		memcpy( reply.tpa, in->spa, PLEN_IP4 );
		memcpy( reply.sha, eth_get_mac_address(), HLEN_ETHERNET );
		memcpy( reply.spa, &my_ip, PLEN_IP4 );
		this_controller->send_frame( in->sha, ETHER_TYPE_ARP, &reply, sizeof(reply) );
	}
}

/***********************************************************************
 * arp_lookup
 * Looks up the MAC address of an IP address, without asking the
 * network.
 *
 * Input:
 *   ip           the IP address
 *   mac_address  where to copy the 6-byte MAC address (may be NULL)
 * Output:
 *   true if the MAC address is known (and copied), false if not
 **********************************************************************/
bool arp_lookup( uint32_t ip, uint8_t *mac_address )
{
	arp_entry *e = arp_find( ip );

	if( e == NULL || e->state != ARP_RESOLVED ) return false;
	if( mac_address != NULL ) memcpy( mac_address, e->mac, HLEN_ETHERNET );
	return true;
}

/***********************************************************************
 * arp_frames_waiting
 * Tells how many frames are waiting for an ARP answer.
 *
 * Input:
 *   none
 * Output:
 *   number of waiting frames (0 .. ARP_PENDING_SLOTS)
 **********************************************************************/
uint16_t arp_frames_waiting( void )
{
	arp_waiting_frame *w;
	uint16_t n = 0;

	for( w = waiting; w < ARP_WAITING_END; w++ )
		if( w->used ) n++;
	return n;
}

/***********************************************************************
 * arp_print_cache
 * Prints the ARP table on the screen: for each address its MAC address
 * and how many seconds ago it was confirmed, or the number of questions
 * sent while it is still pending. Also prints how many frames wait.
 *
 * Input:
 *   none
 * Output:
 *   none
 **********************************************************************/
void arp_print_cache( void )
{
	uint16_t i, j, n = 0;
	uint32_t now = utl_jiffies();
	arp_entry *e;
	uint8_t *ip;

	utl_print("\n************* ARP CACHE ***************************");
	for( i = 0; i < ARP_CACHE_SIZE; i++ )
	{
		e = &arp_table[i];
		if( e->state == ARP_FREE ) continue;
		ip = (uint8_t *)&e->ip;
		utl_print("\n* %02u: %3u.%3u.%3u.%3u  ", (unsigned)n++,
			(unsigned)ip[0], (unsigned)ip[1], (unsigned)ip[2], (unsigned)ip[3]);
		if( e->state == ARP_RESOLVED )
		{
			for( j = 0; j < HLEN_ETHERNET; j++ )
				utl_print("%02X%c", (unsigned)e->mac[j], j < HLEN_ETHERNET-1 ? ':' : ' ');
			utl_print(" %lus", (unsigned long)((now - e->stamp) / 60));
		}
		else
			utl_print("(waiting, try %u)", (unsigned)e->tries);
	}
	utl_print("\n* %u frame(s) waiting for ARP", (unsigned)arp_frames_waiting());
	utl_print("\n***************************************************\n");
}
