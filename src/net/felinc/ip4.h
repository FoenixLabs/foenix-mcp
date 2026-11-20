/***********************************************************************
 * IP4.H - IPv4 for FELINC (see ip4.c)
 *
 * Part of FELINC, the Foenix Labs Ethernet Library in C. (c) R. Kint
 **********************************************************************/
#ifndef __IP4_H__
#define __IP4_H__

#include <stdint.h>
#include <stdbool.h>

#include <stddef.h>

#include "ethernet.h"

#define IP4_VER_HLEN   	0x45	/* version 4, header of 5 x 32 bits = 20 bytes (no options) */

/* IPv4 protocol numbers (the 'protocol' field in the header) */
#define IP4_ICMP	0x01
#define	IP4_TCP		0x06
#define	IP4_UDP		0x11

/* IPv4 header. On the 68040 (big-endian) the fields are already in
 * network byte order, so they can be used directly.
 */
typedef struct ip4_header
{
	uint8_t   ver_hlen;   	/* version (4) and header length in 32-bit words */
	uint8_t   service;    	/* type of service, 0 */
	uint16_t  length;     	/* total length: header + data, in bytes */
	uint16_t  id;      		/* packet identification, unique per packet */
	uint16_t  fragment;   	/* 3 bits flags, 13 bits fragment offset */
	uint8_t   ttl; 			/* time to live: routers drop the packet at 0 */
	uint8_t   protocol;   	/* what is inside: IP4_ICMP, IP4_UDP, ... */
	uint16_t  checksum;   	/* checksum over the header only */
	uint32_t  src_addr;   	/* sender's IP address */
	uint32_t  dest_addr;  	/* receiver's IP address */
} ip4_header;

/* A complete IPv4 packet that fits in one Ethernet frame */
typedef struct ip4_pkt
{
	ip4_header	ip4_hdr;
	uint8_t		data[MTU-sizeof(ip4_header)];
} ip4_pkt;

/* Our IPv4 settings */
struct ip4_configuration
{
	uint32_t ip;		/* our own IP address */
	uint32_t gateway; 	/* router for addresses outside our subnet */
	uint32_t mask;		/* subnet mask */
	uint32_t broadcast;	/* broadcast address of our subnet */
};

bool ip4_init( uint32_t ip, uint32_t mask, uint32_t gateway );
void ip4_shutdown( void );
void ip4_config( uint32_t ip, uint32_t mask, uint32_t gateway );

int16_t ip4_send( uint32_t ip4_dest, uint8_t protocol, uint8_t *data, size_t data_length );
void ip4_broadcast( uint8_t protocol, uint8_t *data, size_t data_length );
uint32_t ip4_next_hop( uint32_t dest );
bool ip4_is_broadcast( uint32_t dest );

uint32_t ip4_get_ip( void );
uint32_t ip4_get_mask( void );
uint32_t ip4_get_gateway( void );
uint32_t ip4_get_broadcast( void );
uint32_t ip4_calc_broadcast( void );

/* Conversion between "10.77.77.99" and a 32-bit address */
void ip4_uint32_t_to_string( char *ip4_string, uint32_t ip4_uint32_t );
uint32_t ip4_string_to_uint32_t( char *ip4_string );

#endif
