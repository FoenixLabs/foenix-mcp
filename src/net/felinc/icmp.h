/***********************************************************************
 * ICMP.H - ICMP (ping) for FELINC (see icmp.c)
 *
 * Part of FELINC, the Foenix Labs Ethernet Library in C. (c) R. Kint
 **********************************************************************/
#ifndef __ICMP_H__
#define __ICMP_H__

#include <stdint.h>
#include <stdbool.h>

#include "ip4.h"
#include "ethernet.h"

/* ICMP message types we know */
#define ICMP_ECHO_REPLY         0	/* answer to a ping */
#define ICMP_DEST_UNREACHABLE   3
#define ICMP_ECHO_REQUEST       8	/* a ping */

typedef struct icmp_hdr
{
	uint8_t     type;		/* ICMP_ECHO_REQUEST, ... */
	uint8_t     code;		/* subtype, 0 for echo */
	uint16_t    chksum; 	/* checksum over the whole ICMP message */
	uint32_t    hdr_data;	/* echo: identifier and sequence number */
} icmp_header;

typedef struct icmp_pkt
{
	icmp_header icmp_hdr;
	uint8_t     data[MTU];
} icmp_pkt;

bool icmp_init( void );
void icmp_shutdown( void );
void icmp_respond( ip4_pkt *request );

#endif
