/***********************************************************************
 * DHCP.C - DHCP client for FELINC (RFC 2131 / RFC 2132)
 *
 * Gets our IP address, subnet mask, gateway, DNS server and (if it
 * sends one) time server from a DHCP server, so the A2560K needs no
 * fixed network settings. It works in four messages:
 *
 *   we      DISCOVER  (broadcast) "is there a DHCP server?"
 *   server  OFFER                 "you can have address x"
 *   we      REQUEST   (broadcast) "I take address x from you"
 *   server  ACK                   "x is yours for n seconds" (the lease)
 *
 * Like the rest of FELINC nothing waits here: dhcp_poll() (called by
 * felinc_poll() every jiffy) sends and repeats the messages, and
 * dhcp_receive() handles the answers as they come in. While this goes
 * on, our IP address is 0.0.0.0.
 *
 * The address is only lent to us for a time (the lease). To keep it:
 *   at T1 (normally half the lease) we ask the same server directly to
 *         extend it (RENEWING), and keep using the address meanwhile
 *   at T2 (normally 7/8 of the lease) that server has not answered, so
 *         we ask any server by broadcast (REBINDING)
 *   at the end of the lease we must stop using the address and start
 *         again with DISCOVER
 *
 * When no server answers within about a minute, we use the fixed
 * settings given to felinc_init() (the fallback), so the machine is
 * reachable anyway. We keep looking for a server in the background;
 * when one answers, its address replaces the fixed one.
 *
 * Part of FELINC, the Foenix Labs Ethernet Library in C. (c) R. Kint
 **********************************************************************/
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "dhcp.h"
#include "ip4.h"
#include "udp.h"
#include "utils.h"
#include "lan9221.h"

#define DHCP_BOOTREQUEST	1
#define DHCP_BOOTREPLY		2
#define DHCP_MAGIC_COOKIE	0x63825363UL

/* Asks the server to broadcast its answers. Why: until the ACK we have
 * no IP address, and a reply sent to the offered address would be
 * dropped as "not for us".
 */
#define DHCP_FLAG_BROADCAST	0x8000

/* The name the router shows in its list of DHCP clients */
#define DHCP_HOST_NAME		"A2560K"

/* Retries (RFC 2131 4.1): wait 4 s for an answer, then 8, 16, 32 and
 * from then on 64 s. In jiffies (1/60 s).
 */
#define DHCP_FIRST_TIMEOUT	(4 * 60)
#define DHCP_MAX_TIMEOUT	(64 * 60)

/* A REQUEST that is not answered this many times means the server has
 * gone: start again with DISCOVER, maybe another server answers.
 */
#define DHCP_REQUEST_TRIES	4

/* For testing renewal without waiting hours: when not 0, we act as if
 * every lease lasted this many seconds (T1 and T2 follow from it). The
 * server does not know; it simply sees us renew early. Keep 0 normally.
 */
#define DHCP_TEST_LEASE		0

/* For testing the fallback: when not 0, we ignore every server for
 * this many seconds after dhcp_start(), as if there were none. Keep 0
 * normally.
 */
#define DHCP_TEST_DEAF		0

/* Fall back to the fixed settings after this many unanswered
 * DISCOVERs: sent at 0, 4, 12 and 28 s, so the fallback starts after
 * 60 s.
 */
#define DHCP_FALLBACK_TRIES	4

/* While renewing, we repeat our request after half the time that is
 * left (RFC 2131 4.4.5), but not more often than once a minute.
 */
#define DHCP_MIN_RETRY		60		/* seconds */

/* Used when the server does not send a subnet mask */
#define DHCP_DEFAULT_MASK	0xFFFFFF00UL	/* 255.255.255.0 */

/* Where we are in the DHCP process. The order matters: from
 * DHCP_BOUND on we have an address (state >= DHCP_BOUND).
 */
typedef enum
{
	DHCP_OFF,			/* not used: fixed settings */
	DHCP_SELECTING,		/* DISCOVER sent, waiting for an OFFER */
	DHCP_REQUESTING,	/* REQUEST sent, waiting for the ACK */
	DHCP_BOUND,			/* we have an address */
	DHCP_RENEWING,		/* past T1: asking our server to extend the lease */
	DHCP_REBINDING		/* past T2: asking any server to extend the lease */
} dhcp_state_t;

/* What we read from the options of a received message */
typedef struct
{
	uint8_t		type;			/* DHCPOFFER, DHCPACK, ... (0 = missing) */
	uint32_t	server_id;
	uint32_t	mask;			/* 0 = not sent */
	uint32_t	router;			/* first router, 0 = not sent */
	uint32_t	dns;			/* first DNS server, 0 = not sent */
	uint32_t	ntp;			/* first NTP server, 0 = not sent */
	uint32_t	lease_time;		/* seconds, 0 = not sent */
	uint32_t	t1, t2;			/* renew / rebind times in seconds, 0 = not sent */
} dhcp_info;

static dhcp_state_t state = DHCP_OFF;
static dhcp_msg msg;			/* the message being built (static: 548 bytes) */
static uint32_t xid;			/* transaction ID of the current exchange */
static uint32_t start_time;		/* jiffies when this exchange started */
static uint32_t next_try;		/* jiffies when to repeat our message */
static uint32_t timeout;		/* current wait time in jiffies */
static uint8_t tries;			/* how often the current message was sent */
static uint32_t offered_ip;		/* address from the OFFER */

/* The address of our last lease, asked for again in the next DISCOVER.
 * Why: after a RELEASE (felinc_shutdown) many routers hand out a new
 * address to the next DISCOVER, unless we say which one we had.
 * dhcp_init() does not clear it, so it lasts while the program runs;
 * 0 = no lease yet.
 */
static uint32_t last_leased_ip = 0;
static uint32_t server_ip;		/* the server we deal with */

/* The fixed settings to fall back to (fallback_ip 0 = none) */
static uint32_t fallback_ip, fallback_mask, fallback_gateway;
static bool fallback_active;	/* true while we use them */
static uint32_t started_at;		/* jiffies at dhcp_start(), for DHCP_TEST_DEAF */

/* The lease we got */
static uint32_t lease_dns;
static uint32_t lease_ntp;		/* time server, 0 = none given */
static uint32_t lease_time, lease_t1, lease_t2;	/* seconds */
static uint32_t lease_start;	/* jiffies when the ACK came */

/* Why: the code below assumes the layout of RFC 2131. If the compiler
 * ever added padding, this line stops the build ("negative array size")
 * instead of sending messages no server understands.
 */
typedef char dhcp_msg_size_check[(sizeof(dhcp_msg) == 548) ? 1 : -1];

/***********************************************************************
 * dhcp_ip_string (internal)
 * Writes an IP address as text, for the messages on screen.
 *
 * Input:
 *   buf   buffer of at least 16 bytes
 *   ip    the address
 * Output:
 *   buf
 **********************************************************************/
static char *dhcp_ip_string( char *buf, uint32_t ip )
{
	ip4_uint32_t_to_string( buf, ip );
	return buf;
}

/***********************************************************************
 * dhcp_add_option (internal)
 * Adds one option to the message being built: code, length, data.
 *
 * Input:
 *   p       where the option goes in msg.options
 *   code    option code (DHCP_OPT_...)
 *   length  number of data bytes
 *   data    the data
 * Output:
 *   pointer behind the option, where the next one goes
 **********************************************************************/
static uint8_t *dhcp_add_option( uint8_t *p, uint8_t code, uint8_t length, const void *data )
{
	*p++ = code;
	*p++ = length;
	memcpy( p, data, length );
	return p + length;
}

/***********************************************************************
 * dhcp_send (internal)
 * Builds a DISCOVER, REQUEST or RELEASE and sends it. What goes in
 * depends on the state:
 *   SELECTING   DISCOVER, broadcast, from 0.0.0.0
 *   REQUESTING  REQUEST for the offered address, broadcast
 *   RENEWING    REQUEST from our address, directly to our server
 *   REBINDING   REQUEST from our address, broadcast
 * A RELEASE (any state with an address) goes directly to our server.
 *
 * Input:
 *   type  DHCPDISCOVER, DHCPREQUEST or DHCPRELEASE
 *   now   jiffies now
 * Output:
 *   none
 **********************************************************************/
static void dhcp_send( uint8_t type, uint32_t now )
{
	/* What we ask the server to tell us */
	static const uint8_t wanted[] = { DHCP_OPT_SUBNET_MASK, DHCP_OPT_ROUTER,
		DHCP_OPT_DNS, DHCP_OPT_LEASE_TIME, DHCP_OPT_RENEWAL_TIME,
		DHCP_OPT_REBINDING_TIME, DHCP_OPT_NTP };
	uint8_t client_id[7];
	uint8_t *p;
	mac *my_mac = eth_get_mac_address();
	bool have_address = (type == DHCPRELEASE ||
						state == DHCP_RENEWING || state == DHCP_REBINDING);
	bool to_server = (type == DHCPRELEASE || state == DHCP_RENEWING);

	memset( &msg, 0, sizeof(msg) );
	msg.op = DHCP_BOOTREQUEST;
	msg.htype = 1;
	msg.hlen = 6;
	msg.xid = xid;
	msg.secs = (uint16_t)((now - start_time) / 60);
	/* With an address, the server can answer us directly */
	msg.flags = have_address ? 0 : DHCP_FLAG_BROADCAST;
	msg.ciaddr = have_address ? ip4_get_ip() : 0;
	memcpy( msg.chaddr, my_mac->addr, 6 );
	msg.cookie = DHCP_MAGIC_COOKIE;

	p = msg.options;
	p = dhcp_add_option( p, DHCP_OPT_MESSAGE_TYPE, 1, &type );

	/* Client ID = hardware type 1 (Ethernet) + MAC address. The server
	 * uses it to recognise us, so we get the same address next time.
	 */
	client_id[0] = 1;
	memcpy( &client_id[1], my_mac->addr, 6 );
	p = dhcp_add_option( p, DHCP_OPT_CLIENT_ID, sizeof(client_id), client_id );

	if( type == DHCPRELEASE )
	{
		/* A RELEASE only says which lease ends: our address (ciaddr)
		 * and the server that gave it. Other options are not allowed
		 * in it (RFC 2131 table 5).
		 */
		p = dhcp_add_option( p, DHCP_OPT_SERVER_ID, 4, &server_ip );
	}
	else
	{
		p = dhcp_add_option( p, DHCP_OPT_HOST_NAME, sizeof(DHCP_HOST_NAME) - 1, DHCP_HOST_NAME );

		/* A REQUEST after an OFFER says which offer we take: there may
		 * be more servers, and the others see from this that we did
		 * not choose them. When renewing, the address is in ciaddr
		 * instead.
		 */
		if( state == DHCP_REQUESTING )
		{
			p = dhcp_add_option( p, DHCP_OPT_REQUESTED_IP, 4, &offered_ip );
			p = dhcp_add_option( p, DHCP_OPT_SERVER_ID, 4, &server_ip );
		}

		/* A DISCOVER may suggest the address we had before (RFC 2131
		 * 4.4.1); a server that cannot give it offers another one.
		 */
		if( type == DHCPDISCOVER && last_leased_ip != 0 )
			p = dhcp_add_option( p, DHCP_OPT_REQUESTED_IP, 4, &last_leased_ip );
		p = dhcp_add_option( p, DHCP_OPT_PARAM_REQUEST, sizeof(wanted), wanted );
	}
	*p = DHCP_OPT_END;

	/* Before we have an address from DHCP our messages must come from
	 * 0.0.0.0 (RFC 2131 4.1), also while we use the fixed address. So
	 * that address is switched off just for this broadcast. (A
	 * broadcast goes out at once, it never waits for ARP.)
	 */
	if( fallback_active ) ip4_config( 0, 0, 0 );
	udp_send( DHCP_CLIENT_PORT, DHCP_SERVER_PORT,
				to_server ? server_ip : 0xFFFFFFFFUL,
				(uint8_t *)&msg, sizeof(msg) );
	if( fallback_active ) ip4_config( fallback_ip, fallback_mask, fallback_gateway );
}

/***********************************************************************
 * dhcp_send_current (internal)
 * Sends the message that belongs to the current state (DISCOVER or
 * REQUEST) and works out when to send it again if no answer comes.
 *
 * Input:
 *   now   jiffies now
 * Output:
 *   none
 **********************************************************************/
static void dhcp_send_current( uint32_t now )
{
	dhcp_send( state == DHCP_SELECTING ? DHCPDISCOVER : DHCPREQUEST, now );
	tries++;
	next_try = now + timeout;
	if( timeout < DHCP_MAX_TIMEOUT ) timeout *= 2;
}

/***********************************************************************
 * dhcp_new_exchange (internal)
 * Starts a new exchange of messages with a fresh transaction ID.
 *
 * Input:
 *   now   jiffies now
 * Output:
 *   none
 **********************************************************************/
static void dhcp_new_exchange( uint32_t now )
{
	static uint16_t counter = 0;
	mac *my_mac = eth_get_mac_address();
	uint32_t mac_low;

	/* A new ID per exchange, different from other A2560Ks (MAC) and
	 * from our previous exchanges (time and counter), so we never take
	 * an answer that was meant for someone else.
	 */
	memcpy( &mac_low, &my_mac->addr[2], 4 );
	xid = mac_low ^ (now << 12) ^ ++counter;
	start_time = now;
}

/***********************************************************************
 * dhcp_send_renewal (internal)
 * Sends our REQUEST while RENEWING or REBINDING and works out when to
 * repeat it: after half the time left until the next deadline (T2
 * when renewing, the end of the lease when rebinding).
 *
 * Input:
 *   now   jiffies now
 *   age   seconds since we got the lease
 * Output:
 *   none
 **********************************************************************/
static void dhcp_send_renewal( uint32_t now, uint32_t age )
{
	uint32_t deadline = (state == DHCP_RENEWING) ? lease_t2 : lease_time;
	uint32_t wait = (deadline > age) ? (deadline - age) / 2 : 0;

	if( wait < DHCP_MIN_RETRY ) wait = DHCP_MIN_RETRY;
	dhcp_send( DHCPREQUEST, now );
	tries++;
	next_try = now + wait * 60;
}

/***********************************************************************
 * dhcp_discover (internal)
 * Starts a new exchange: forgets any address and broadcasts DISCOVER.
 *
 * Input:
 *   now   jiffies now
 * Output:
 *   none
 **********************************************************************/
static void dhcp_discover( uint32_t now )
{
	/* While asking we have no address: we must not answer ARP or ping
	 * for an address that is not (or no longer) ours. The fixed
	 * address, if we use it already, stays.
	 */
	if( !fallback_active ) ip4_config( 0, 0, 0 );

	state = DHCP_SELECTING;
	dhcp_new_exchange( now );
	tries = 0;
	timeout = DHCP_FIRST_TIMEOUT;
	utl_print("\nDHCP: looking for a server");
	dhcp_send_current( now );
}

/***********************************************************************
 * dhcp_read_options (internal)
 * Reads the options we need from a received message. Options are a
 * list of code, length, data; the list ends with DHCP_OPT_END.
 *
 * Input:
 *   m       the received message
 *   length  its length in bytes
 *   info    where to store what was found
 * Output:
 *   none (unknown options are skipped; a list that runs past the end
 *   of the message is cut off there)
 **********************************************************************/
static void dhcp_read_options( const dhcp_msg *m, size_t length, dhcp_info *info )
{
	const uint8_t *p = m->options;
	const uint8_t *end = (const uint8_t *)m + length;
	uint8_t code, len;

	memset( info, 0, sizeof(*info) );

	while( p < end )
	{
		code = *p++;
		if( code == DHCP_OPT_END ) break;
		if( code == DHCP_OPT_PAD ) continue;		/* PAD has no length byte */
		if( p >= end ) break;
		len = *p++;
		if( p + len > end ) break;				/* damaged message */

		/* Addresses and times are copied: options are not aligned */
		switch( code )
		{
			case DHCP_OPT_MESSAGE_TYPE:
				if( len >= 1 ) info->type = p[0];
				break;
			case DHCP_OPT_SERVER_ID:
				if( len >= 4 ) memcpy( &info->server_id, p, 4 );
				break;
			case DHCP_OPT_SUBNET_MASK:
				if( len >= 4 ) memcpy( &info->mask, p, 4 );
				break;
			case DHCP_OPT_ROUTER:		/* a list: we use the first */
				if( len >= 4 ) memcpy( &info->router, p, 4 );
				break;
			case DHCP_OPT_DNS:			/* a list: we use the first */
				if( len >= 4 ) memcpy( &info->dns, p, 4 );
				break;
			case DHCP_OPT_NTP:			/* a list: we use the first */
				if( len >= 4 ) memcpy( &info->ntp, p, 4 );
				break;
			case DHCP_OPT_LEASE_TIME:
				if( len >= 4 ) memcpy( &info->lease_time, p, 4 );
				break;
			case DHCP_OPT_RENEWAL_TIME:
				if( len >= 4 ) memcpy( &info->t1, p, 4 );
				break;
			case DHCP_OPT_REBINDING_TIME:
				if( len >= 4 ) memcpy( &info->t2, p, 4 );
				break;
			default:
				break;
		}
		p += len;
	}
}

/***********************************************************************
 * dhcp_bind (internal)
 * Takes the address from an ACK into use and remembers the lease.
 * Also used when a renewal is acknowledged: the lease starts again.
 *
 * Input:
 *   ip    our new address (yiaddr of the ACK)
 *   info  the options of the ACK
 *   now   jiffies now
 * Output:
 *   none
 **********************************************************************/
static void dhcp_bind( uint32_t ip, const dhcp_info *info, uint32_t now )
{
	char a[16], b[16], c[16];
	uint32_t mask = info->mask ? info->mask : DHCP_DEFAULT_MASK;
	bool renewal = (state == DHCP_RENEWING || state == DHCP_REBINDING) &&
					ip == ip4_get_ip();

	ip4_config( ip, mask, info->router );
	fallback_active = false;
	last_leased_ip = ip;

	lease_dns = info->dns;
	lease_ntp = info->ntp;
	lease_time = info->lease_time;
	if( lease_time == 0 ) lease_time = DHCP_INFINITE_TIME;	/* not sent: keep it */
	/* #if, not if: Calypsi (5.12 and 5.18) gets a comparison of two
	 * constants inside && backwards, so "if( CONSTANT != 0 && ... )"
	 * does the opposite of what it says. And this way a normal build
	 * has no test code in it at all.
	 */
#if DHCP_TEST_LEASE != 0
	if( lease_time != DHCP_INFINITE_TIME ) lease_time = DHCP_TEST_LEASE;
#endif

	/* Defaults from RFC 2131 4.4.5 when the server does not send them.
	 * A T1 or T2 beyond the end of the lease would be useless (and a
	 * test lease makes them so), so then the defaults are used too.
	 */
	lease_t1 = info->t1;
	lease_t2 = info->t2;
	if( lease_t2 == 0 || lease_t2 >= lease_time ) lease_t2 = lease_time / 8 * 7;
	if( lease_t1 == 0 || lease_t1 >= lease_t2 ) lease_t1 = lease_time / 2;
	lease_start = now;
	state = DHCP_BOUND;

	if( renewal )
	{
		utl_print("\nDHCP: lease of %s renewed by %s, %lu s\n",
			dhcp_ip_string( a, ip ), dhcp_ip_string( b, server_ip ),
			(unsigned long)lease_time );
		return;
	}
	utl_print("\nDHCP: got %s, mask %s, gateway %s",
		dhcp_ip_string( a, ip ), dhcp_ip_string( b, mask ),
		dhcp_ip_string( c, info->router ));
	utl_print("\n      DNS %s, server %s, lease %lu s\n",
		dhcp_ip_string( a, lease_dns ), dhcp_ip_string( b, server_ip ),
		(unsigned long)lease_time );
	utl_print("      time server: %s\n",
		lease_ntp ? dhcp_ip_string( a, lease_ntp ) : "none given" );
}

/***********************************************************************
 * dhcp_init
 * Sets up the DHCP client. It does nothing until dhcp_start().
 *
 * Input:
 *   ip, mask, gateway   fixed settings to use when no DHCP server
 *                       answers (ip 0 = no fallback: keep waiting)
 * Output:
 *   none
 **********************************************************************/
void dhcp_init( uint32_t ip, uint32_t mask, uint32_t gateway )
{
	state = DHCP_OFF;
	fallback_ip = ip;
	fallback_mask = mask;
	fallback_gateway = gateway;
	fallback_active = false;
}

/***********************************************************************
 * dhcp_start
 * Starts asking for an address. Call it after felinc_init(); from then
 * on felinc_poll() does the work. dhcp_is_bound() tells when we have
 * an address.
 *
 * Input:
 *   none
 * Output:
 *   none
 **********************************************************************/
void dhcp_start( void )
{
	started_at = utl_jiffies();
	dhcp_discover( started_at );
}

/***********************************************************************
 * dhcp_stop
 * Stops DHCP. If we have a lease, it is given back to the server
 * (DHCPRELEASE), so the address is free for other machines at once
 * instead of only when the lease ends. felinc_shutdown() calls this.
 * The address itself stays configured: FELINC is stopping anyway.
 *
 * Input:
 *   none
 * Output:
 *   none
 **********************************************************************/
void dhcp_stop( void )
{
	if( state >= DHCP_BOUND )
	{
		dhcp_send( DHCPRELEASE, utl_jiffies() );
		utl_print("\nDHCP: lease given back\n");
	}
	state = DHCP_OFF;
}

/***********************************************************************
 * dhcp_poll
 * Repeats our DISCOVER or REQUEST when no answer came in time, and
 * watches the lease: renew at T1, rebind at T2, stop at the end.
 * felinc_poll() calls it once per jiffy.
 *
 * Input:
 *   now   jiffies now
 * Output:
 *   none
 **********************************************************************/
void dhcp_poll( uint32_t now )
{
	uint32_t age;
	char a[16];

	if( state == DHCP_OFF ) return;

	if( state == DHCP_BOUND || state == DHCP_RENEWING || state == DHCP_REBINDING )
	{
		if( lease_time == DHCP_INFINITE_TIME ) return;
		age = (now - lease_start) / 60;

		if( age >= lease_time )
		{
			utl_print("\nDHCP: lease of %s ended, starting again",
				dhcp_ip_string( a, ip4_get_ip() ));
			dhcp_discover( now );
		}
		else if( state != DHCP_REBINDING && age >= lease_t2 )
		{
			utl_print("\nDHCP: no answer from %s, asking any server",
				dhcp_ip_string( a, server_ip ));
			state = DHCP_REBINDING;
			dhcp_new_exchange( now );
			dhcp_send_renewal( now, age );
		}
		else if( state == DHCP_BOUND && age >= lease_t1 )
		{
			utl_print("\nDHCP: renewing the lease");
			state = DHCP_RENEWING;
			tries = 0;
			dhcp_new_exchange( now );
			dhcp_send_renewal( now, age );
		}
		else if( state != DHCP_BOUND && (int32_t)(now - next_try) >= 0 )
			dhcp_send_renewal( now, age );
		return;
	}

	/* The difference is signed so this keeps working when the jiffies
	 * counter wraps around (after 2 years).
	 */
	if( (int32_t)(now - next_try) < 0 ) return;

	if( state == DHCP_REQUESTING && tries >= DHCP_REQUEST_TRIES )
	{
		utl_print("\nDHCP: no answer to our request, starting again");
		dhcp_discover( now );
		return;
	}

	if( state == DHCP_SELECTING && tries >= DHCP_FALLBACK_TRIES &&
		!fallback_active && fallback_ip != 0 )
	{
		utl_print("\nDHCP: no server answers, using fixed address %s"
			   "\n      (still looking for a server)\n", dhcp_ip_string( a, fallback_ip ));
		ip4_config( fallback_ip, fallback_mask, fallback_gateway );
		fallback_active = true;
	}
	dhcp_send_current( now );
}

/***********************************************************************
 * dhcp_receive
 * Handles a datagram that arrived on the DHCP client port (68).
 * felinc.c passes all of them, also those not addressed to us: until
 * the ACK we have no address. Messages that are not answers to our own
 * request (OFFER, ACK or NAK) are ignored.
 *
 * Input:
 *   data    the UDP data (the DHCP message)
 *   length  its length in bytes
 * Output:
 *   none
 **********************************************************************/
void dhcp_receive( const uint8_t *data, size_t length )
{
	const dhcp_msg *m = (const dhcp_msg *)data;
	mac *my_mac = eth_get_mac_address();
	dhcp_info info;
	char a[16], b[16];

	if( state == DHCP_OFF || state == DHCP_BOUND ) return;
#if DHCP_TEST_DEAF != 0		/* #if: see dhcp_bind() */
	if( utl_jiffies() - started_at < (uint32_t)DHCP_TEST_DEAF * 60 ) return;
#endif

	/* Is it a server's answer to our current request? */
	if( length < offsetof(dhcp_msg, options) ||
		m->op != DHCP_BOOTREPLY || m->xid != xid ||
		memcmp( m->chaddr, my_mac->addr, 6 ) != 0 ||
		m->cookie != DHCP_MAGIC_COOKIE )
		return;

	dhcp_read_options( m, length, &info );

	if( state == DHCP_SELECTING && info.type == DHCPOFFER )
	{
		/* We take the first offer: on a home network there is only one
		 * server anyway.
		 */
		if( m->yiaddr == 0 || info.server_id == 0 ) return;
		offered_ip = m->yiaddr;
		server_ip = info.server_id;
		utl_print("\nDHCP: %s offers %s",
			dhcp_ip_string( a, server_ip ), dhcp_ip_string( b, offered_ip ));

		state = DHCP_REQUESTING;
		tries = 0;
		timeout = DHCP_FIRST_TIMEOUT;
		dhcp_send_current( utl_jiffies() );
	}
	else if( state != DHCP_SELECTING &&
			 (info.server_id == server_ip || state == DHCP_REBINDING) )
	{
		/* When rebinding, any server may answer: it becomes ours */
		if( info.server_id != 0 ) server_ip = info.server_id;

		if( info.type == DHCPACK )
			dhcp_bind( m->yiaddr, &info, utl_jiffies() );
		else if( info.type == DHCPNAK )
		{
			/* The server changed its mind (e.g. the address was given
			 * away meanwhile): ask again from the start.
			 */
			utl_print("\nDHCP: server refused, starting again");
			dhcp_discover( utl_jiffies() );
		}
	}
}

/***********************************************************************
 * dhcp_is_bound
 * Tells whether we have an address from a DHCP server.
 *
 * Input:
 *   none
 * Output:
 *   true when the lease is in use (also while it is being renewed)
 **********************************************************************/
bool dhcp_is_bound( void )
{
	return state >= DHCP_BOUND;
}

/***********************************************************************
 * dhcp_get_dns
 * Returns the DNS server the DHCP server told us about (for the DNS
 * module, later).
 *
 * Input:
 *   none
 * Output:
 *   the DNS server's address, or 0 if we do not know one
 **********************************************************************/
uint32_t dhcp_get_dns( void )
{
	return state >= DHCP_BOUND ? lease_dns : 0;
}

/***********************************************************************
 * dhcp_get_ntp
 * Returns the time (NTP) server the DHCP server told us about
 * (option 42). Many home routers do not send one.
 *
 * Input:
 *   none
 * Output:
 *   the time server's address, or 0 if we do not know one (no lease,
 *   fixed settings, or the DHCP server sent none)
 **********************************************************************/
uint32_t dhcp_get_ntp( void )
{
	return state >= DHCP_BOUND ? lease_ntp : 0;
}

/***********************************************************************
 * dhcp_print_status
 * Prints the DHCP state and, when bound, the lease on the screen.
 *
 * Input:
 *   none
 * Output:
 *   none
 **********************************************************************/
void dhcp_print_status( void )
{
	static const char *names[] = { "off (fixed settings)", "looking for a server",
									"requesting", "bound", "renewing", "rebinding" };
	char a[16];
	uint32_t age;

	utl_print("\n    DHCP: %s", names[state]);
	if( state == DHCP_SELECTING || state == DHCP_REQUESTING )
		utl_print(", %u tries", (unsigned)tries);
	if( fallback_active )
		utl_print(", using fixed address %s", dhcp_ip_string( a, fallback_ip ));
	if( state >= DHCP_BOUND )
	{
		age = (utl_jiffies() - lease_start) / 60;
		utl_print(", server %s, age %lu s", dhcp_ip_string( a, server_ip ), (unsigned long)age );
		utl_print(", time server %s", lease_ntp ? dhcp_ip_string( a, lease_ntp ) : "none given" );
		if( lease_time == DHCP_INFINITE_TIME )
			utl_print(", lease never ends");
		else
			utl_print(", renew at %lu s, rebind at %lu s, lease %lu s",
				(unsigned long)lease_t1, (unsigned long)lease_t2,
				(unsigned long)lease_time );
	}
}
