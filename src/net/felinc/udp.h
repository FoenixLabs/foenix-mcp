/***********************************************************************
 * UDP.H - UDP for FELINC (see udp.c)
 *
 * Part of FELINC, the Foenix Labs Ethernet Library in C. (c) R. Kint
 **********************************************************************/
#ifndef __UDP_H__
#define __UDP_H__

#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>

#include "felinc.h"		/* ssize_t */
#include "lan9221.h"
#include "ip4.h"

typedef struct udp_header
{
	uint16_t src_port;      /* sender's port */
	uint16_t dest_port;     /* receiver's port */
	uint16_t length;        /* header + data, in bytes */
	uint16_t checksum;      /* see udp_calc_checksum(); 0 = not used */
} udp_header;

/* A UDP datagram that fits in one IPv4 packet in one Ethernet frame */
typedef struct udp_pkt
{
	udp_header hdr;
	uint8_t data[MTU-sizeof(ip4_header)-sizeof(udp_header)];
} udp_pkt;

bool udp_init( void );
void udp_shutdown( void );
ssize_t udp_get_info( ip4_pkt *ip4, uint16_t *port, uint8_t **data, size_t *size_of_data );
uint16_t udp_get_src_port( ip4_pkt *ip4 );
ssize_t udp_send( uint16_t src_port, uint16_t dest_port,
		uint32_t ip4_dest_address, uint8_t *data_buffer, size_t data_length );

#endif
