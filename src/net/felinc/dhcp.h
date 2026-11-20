/***********************************************************************
 * DHCP.H - DHCP client for FELINC (see dhcp.c)
 *
 * Part of FELINC, the Foenix Labs Ethernet Library in C. (c) R. Kint
 **********************************************************************/
#ifndef __DHCP_H__
#define __DHCP_H__

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define DHCP_SERVER_PORT	67
#define DHCP_CLIENT_PORT	68

/* DHCP message types (option 53) */
#define DHCPDISCOVER	1
#define DHCPOFFER		2
#define DHCPREQUEST		3
#define DHCPDECLINE		4
#define DHCPACK			5
#define DHCPNAK			6
#define DHCPRELEASE		7
#define DHCPINFORM		8

/* The options we use (RFC 2132) */
#define DHCP_OPT_PAD			0
#define DHCP_OPT_SUBNET_MASK	1
#define DHCP_OPT_ROUTER			3
#define DHCP_OPT_DNS			6
#define DHCP_OPT_HOST_NAME		12
#define DHCP_OPT_NTP			42		/* NTP (time) servers */
#define DHCP_OPT_REQUESTED_IP	50
#define DHCP_OPT_LEASE_TIME		51
#define DHCP_OPT_MESSAGE_TYPE	53
#define DHCP_OPT_SERVER_ID		54
#define DHCP_OPT_PARAM_REQUEST	55
#define DHCP_OPT_RENEWAL_TIME	58		/* T1 */
#define DHCP_OPT_REBINDING_TIME	59		/* T2 */
#define DHCP_OPT_CLIENT_ID		61
#define DHCP_OPT_END			255

#define DHCP_INFINITE_TIME	0xFFFFFFFFUL	/* lease that never ends */

/* A DHCP message as it travels in a UDP datagram (RFC 2131). On the
 * 68040 (big-endian) the fields are already in network byte order.
 * 236 bytes of fixed fields, the 4-byte "magic cookie" and room for
 * 308 bytes of options: 548 bytes, the size every server accepts.
 */
typedef struct dhcp_msg
{
	uint8_t		op;			/* 1 = request (from client), 2 = reply (from server) */
	uint8_t		htype;		/* hardware type: 1 = Ethernet */
	uint8_t		hlen;		/* hardware address length: 6 */
	uint8_t		hops;		/* used by relay agents, 0 */
	uint32_t	xid;		/* transaction ID: links replies to our request */
	uint16_t	secs;		/* seconds since we started asking */
	uint16_t	flags;		/* DHCP_FLAG_BROADCAST, see dhcp.c */
	uint32_t	ciaddr;		/* our IP address, only when renewing */
	uint32_t	yiaddr;		/* "your" IP address: what the server gives us */
	uint32_t	siaddr;		/* next server (for network boot), not used */
	uint32_t	giaddr;		/* relay agent, not used */
	uint8_t		chaddr[16];	/* our MAC address (first 6 bytes) */
	uint8_t		sname[64];	/* server host name, not used */
	uint8_t		file[128];	/* boot file name, not used */
	uint32_t	cookie;		/* DHCP_MAGIC_COOKIE: "options follow" */
	uint8_t		options[308];
} dhcp_msg;

void dhcp_init( uint32_t ip, uint32_t mask, uint32_t gateway );
void dhcp_start( void );
void dhcp_stop( void );
void dhcp_poll( uint32_t now );
void dhcp_receive( const uint8_t *data, size_t length );
bool dhcp_is_bound( void );
uint32_t dhcp_get_dns( void );
uint32_t dhcp_get_ntp( void );
void dhcp_print_status( void );

#endif
