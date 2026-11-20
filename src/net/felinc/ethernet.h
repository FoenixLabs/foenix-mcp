/***********************************************************************
 * ETHERNET.H - the Ethernet frame, and how the protocol modules talk to
 * the Ethernet driver (internal to FELINC)
 *
 * Part of FELINC, the Foenix Labs Ethernet Library in C. (c) R. Kint
 **********************************************************************/
#ifndef __ETHERNET_H__
#define __ETHERNET_H__

#include <stdlib.h>
#include <stdint.h>

#include "felinc.h"		/* ssize_t */

#define MTU 1500	/* largest frame contents (Maximum Transmission Unit) */

typedef struct eth_frame_header
{
	uint8_t dest_addr[6];   /* receiver's MAC address */
	uint8_t src_addr[6];    /* sender's MAC address */
	uint16_t ether_type;    /* type of contents: ETHER_TYPE_IP4, ETHER_TYPE_ARP */
} eth_frame_header;

typedef struct eth_frame
{
	eth_frame_header header;
	uint8_t data[MTU];
	uint32_t eth_crc;       /* room for the frame checksum (filled by the chip) */
} eth_frame;

typedef struct
{
	uint8_t addr[6];	/* MAC address is 48 bits */
} mac;

/* The Ethernet controller, as the protocol modules see it. eth_init()
 * fills it in; ARP sends its frames through these functions, so it
 * does not need to know which chip is used.
 */
typedef struct
{
	char device_id[20];		/* chip ID as text, e.g. "9221" */
	mac mac_addr;			/* our MAC address */

	ssize_t (*listen)( eth_frame *buffer, size_t buffer_length );
	int16_t (*send_frame)( const uint8_t *dest_mac, uint16_t ether_type,
							const void *data_to_send, uint16_t data_length );
	void (*send_broadcast)( uint16_t ether_type, const void *data_to_send,
							uint16_t data_length );
} eth_controller;

#endif
