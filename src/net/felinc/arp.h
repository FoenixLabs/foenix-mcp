/***********************************************************************
 * ARP.H - ARP for FELINC (see arp.c)
 *
 * Part of FELINC, the Foenix Labs Ethernet Library in C. (c) R. Kint
 **********************************************************************/
#ifndef __ARP_H__
#define __ARP_H__

#include <stdint.h>
#include <stdbool.h>

#include "ethernet.h"

/* Values of the ARP packet fields for IPv4 over Ethernet */
#define HTYPE_ETHERNET  1
#define PTYPE_IP4       0x0800
#define HLEN_ETHERNET   6
#define PLEN_IP4        4
#define OPER_REQUEST    1
#define OPER_REPLY      2

#define ARP_CACHE_SIZE		16		/* entries in the ARP table */
#define ARP_PENDING_SLOTS	4		/* frames that can wait for an ARP reply */
#define ARP_RETRY_JIFFIES	60		/* 1 s between ARP requests */
#define ARP_MAX_TRIES		3		/* requests before we give up */
#define ARP_ENTRY_TTL		(20UL*60UL*60UL)	/* 20 minutes, in jiffies */

/* An ARP packet (the contents of an Ethernet frame of type 0x0806) */
typedef struct arp_pkt
{
	uint16_t htype;     /* hardware type: HTYPE_ETHERNET */
	uint16_t ptype;     /* protocol type: PTYPE_IP4 */
	uint8_t hlen;       /* hardware address length: 6 */
	uint8_t plen;       /* protocol address length: 4 */
	uint16_t oper;      /* OPER_REQUEST (question) or OPER_REPLY (answer) */
	uint8_t sha[6];     /* sender's MAC address */
	uint8_t spa[4];     /* sender's IP address */
	uint8_t tha[6];     /* target's MAC address (unknown in a question) */
	uint8_t tpa[4];     /* target's IP address */
} arp_pkt;

bool arp_init( eth_controller *controller );
void arp_shutdown( void );
int16_t arp_send_ip( uint32_t next_hop, uint16_t ether_type,
						const void *data, uint16_t data_length );
void arp_respond( eth_frame *frame );
void arp_poll( uint32_t now );
bool arp_lookup( uint32_t ip, uint8_t *mac_address );
uint16_t arp_frames_waiting( void );
void arp_print_cache( void );

#endif
