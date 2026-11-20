/***********************************************************************
 * DNS.C - DNS client for FELINC (RFC 1034 / RFC 1035)
 *
 * Turns a name like "gitlab.com" into an IPv4 address. We do not walk
 * the DNS tree ourselves: we ask the DNS server on our LAN (the one
 * DHCP told us about, or else the gateway), and that server does the
 * walking. Its answer may say that the name is an alias (CNAME) for
 * another name; we follow those aliases to the address.
 *
 * Like the rest of FELINC nothing waits here. A program calls
 * dns_resolve() from its main loop until the answer is there:
 *
 *     uint32_t ip;
 *     int16_t r = dns_resolve( "gitlab.com", &ip );
 *     if( r == DNS_DONE )   ... use ip ...
 *     if( r == DNS_BUSY )   ... call again on the next pass ...
 *     if( r == DNS_FAILED ) ... no address ...
 *
 * The first call sends the question; felinc_poll() handles the answer
 * and repeats the question when no answer comes. Answers are kept in a
 * small cache, so asking again for the same name costs nothing.
 *
 * Only IPv4 addresses (type A records) are asked for.
 *
 * Part of FELINC, the Foenix Labs Ethernet Library in C. (c) R. Kint
 **********************************************************************/
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>

#include "dns.h"
#include "dhcp.h"
#include "ip4.h"
#include "udp.h"
#include "utils.h"

#define DNS_CACHE_SIZE		8		/* names remembered (and asked at the same time) */

/* Retries: wait 2 s for an answer, then 4 s, then 8 s, then give up */
#define DNS_FIRST_TIMEOUT	(2 * 60)	/* jiffies */
#define DNS_TRIES			3

/* How long answers are kept, in seconds. The server says how long
 * (the TTL); we keep at least DNS_MIN_TTL so a busy program does not
 * ask again all the time, and at most DNS_MAX_TTL so a changed address
 * is noticed within the hour. Failures are kept DNS_FAILED_TTL, so a
 * program that keeps asking for a bad name does not flood the server.
 */
#define DNS_MIN_TTL			30
#define DNS_MAX_TTL			3600
#define DNS_FAILED_TTL		10

/* A chain of aliases longer than this is treated as an error (it could
 * even be a loop: a -> b -> a).
 */
#define DNS_MAX_ALIASES		8

/* DNS message layout (RFC 1035 4.1). A message starts with a 12-byte
 * header; these are the offsets of its fields we use.
 */
#define DNS_HEADER_SIZE		12
#define DNS_HDR_ID			0			/* links the answer to our question */
#define DNS_HDR_FLAGS		2
#define DNS_HDR_QUESTIONS	4			/* number of questions */
#define DNS_HDR_ANSWERS		6			/* number of answer records */
/* After the name, a question has 4 more bytes (type, class) and an
 * answer record 10 (type, class, TTL, data length), then its data.
 */
#define DNS_QUESTION_TAIL	4
#define DNS_RECORD_TAIL		10
#define DNS_FLAG_RESPONSE	0x8000		/* QR: this is an answer */
#define DNS_FLAG_RECURSE	0x0100		/* RD: "please walk the tree for me" */
#define DNS_RCODE_MASK		0x000F		/* result code in the flags */
#define DNS_RCODE_NAME_ERROR 3			/* the name does not exist */
#define DNS_TYPE_A			1			/* IPv4 address */
#define DNS_TYPE_CNAME		5			/* alias for another name */
#define DNS_CLASS_IN		1			/* Internet */

/* State of a cache entry */
typedef enum
{
	DNS_FREE,			/* not used */
	DNS_ASKING,			/* question sent, waiting for the answer */
	DNS_KNOWN,			/* address known */
	DNS_NOT_FOUND		/* lookup failed (kept DNS_FAILED_TTL seconds) */
} dns_state_t;

typedef struct
{
	char		name[DNS_NAME_MAX + 1];
	dns_state_t	state;
	uint32_t	ip;			/* the address (DNS_KNOWN) */
	uint16_t	id;			/* ID of our question (DNS_ASKING) */
	uint32_t	server;		/* where we sent it (DNS_ASKING) */
	uint8_t		tries;		/* how often it was sent (DNS_ASKING) */
	uint32_t	next_try;	/* jiffies: send again (DNS_ASKING) */
	uint32_t	expires;	/* jiffies: forget it (DNS_KNOWN, DNS_NOT_FOUND) */
	uint32_t	last_used;	/* jiffies: to choose which entry to reuse */
} dns_entry;

/* One answer record of a DNS message, see dns_read_record() */
typedef struct
{
	char		name[DNS_NAME_MAX + 1];	/* the name the record is about */
	uint16_t	type;			/* DNS_TYPE_A, DNS_TYPE_CNAME, ... */
	uint16_t	rr_class;		/* DNS_CLASS_IN */
	uint32_t	ttl;			/* seconds the record may be kept */
	uint16_t	data_length;	/* bytes of record data */
	size_t		data_pos;		/* where the data starts in the message */
} dns_record;

static dns_entry cache[DNS_CACHE_SIZE];
static uint32_t fixed_server;		/* set with dns_set_server(), 0 = none */
static uint16_t next_id;

/***********************************************************************
 * dns_get16, dns_get32 (internal)
 * Read a 16- or 32-bit number from a DNS message. Byte by byte,
 * because inside a message numbers are often not aligned, and the 68000
 * instruction set cannot read words from odd addresses.
 *
 * Input:
 *   p   where the number starts
 * Output:
 *   the number
 **********************************************************************/
static uint16_t dns_get16( const uint8_t *p )
{
	return (uint16_t)((p[0] << 8) | p[1]);
}

static uint32_t dns_get32( const uint8_t *p )
{
	return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
		   ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

/***********************************************************************
 * dns_lower (internal)
 * Makes a letter lower case. DNS names do not care about case:
 * "GitLab.COM" is the same name as "gitlab.com".
 *
 * Input:
 *   c   a character
 * Output:
 *   the lower case letter, or c itself if it is not a letter A-Z
 **********************************************************************/
static char dns_lower( char c )
{
	return (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
}

/***********************************************************************
 * dns_same_name (internal)
 * Compares two names, not caring about upper or lower case.
 *
 * Input:
 *   a, b   the names
 * Output:
 *   true if they are the same name
 **********************************************************************/
static bool dns_same_name( const char *a, const char *b )
{
	while( *a != 0 && dns_lower( *a ) == dns_lower( *b ) )
	{
		a++;
		b++;
	}
	return *a == 0 && *b == 0;
}

/***********************************************************************
 * dns_is_ip (internal)
 * Checks whether a "name" is really an address like "10.77.77.1", and
 * converts it. Then no DNS question is needed.
 *
 * Input:
 *   name   the text
 *   ip     where to store the address
 * Output:
 *   true if name is an address (ip is then filled in)
 **********************************************************************/
static bool dns_is_ip( const char *name, uint32_t *ip )
{
	uint8_t parts[4];
	uint16_t value = 0;
	uint8_t count = 0, digits = 0;

	for( ;; name++ )
	{
		if( *name >= '0' && *name <= '9' )
		{
			value = (uint16_t)(value * 10 + (*name - '0'));
			if( ++digits > 3 || value > 255 ) return false;
		}
		else if( *name == '.' || *name == 0 )
		{
			if( digits == 0 || count == 4 ) return false;
			parts[count++] = (uint8_t)value;
			value = 0;
			digits = 0;
			if( *name == 0 ) break;
		}
		else
			return false;
	}
	if( count != 4 ) return false;

	memcpy( ip, parts, 4 );		/* network order: first part first */
	return true;
}

/***********************************************************************
 * dns_read_name (internal)
 * Reads a name from a received DNS message and writes it as text
 * ("www.gitlab.com"). In the message a name is a list of labels, each
 * with a length byte in front ("3www6gitlab3com0"). To save space, a
 * name may end with a pointer (2 bytes, top 2 bits set) to the rest of
 * the name somewhere earlier in the message.
 *
 * Input:
 *   msg      the message
 *   length   its length in bytes
 *   pos      where the name starts; on return: the first byte after
 *            the name (after the pointer, if there is one)
 *   out      buffer for the text, DNS_NAME_MAX + 1 bytes
 * Output:
 *   true if the name was read; false for a damaged name (one that
 *   runs past the end of the message, is too long, or points around
 *   in circles)
 **********************************************************************/
static bool dns_read_name( const uint8_t *msg, size_t length, size_t *pos, char *out )
{
	size_t p = *pos;
	size_t out_len = 0;
	uint8_t jumps = 0;
	bool jumped = false;
	uint8_t label;

	for( ;; )
	{
		if( p >= length ) return false;
		label = msg[p];

		if( (label & 0xC0) == 0xC0 )			/* pointer */
		{
			if( p + 1 >= length || ++jumps > 16 ) return false;
			if( !jumped ) *pos = p + 2;		/* the name ends here in the message */
			jumped = true;
			p = ((size_t)(label & 0x3F) << 8) | msg[p + 1];
			continue;
		}
		if( label > 63 ) return false;			/* 0x40 and 0x80: not used */

		p++;
		if( label == 0 ) break;					/* end of the name */

		if( p + label > length ) return false;
		if( out_len + (out_len ? 1 : 0) + label > DNS_NAME_MAX ) return false;
		if( out_len ) out[out_len++] = '.';
		memcpy( &out[out_len], &msg[p], label );
		out_len += label;
		p += label;
	}
	out[out_len] = 0;
	if( !jumped ) *pos = p;
	return true;
}

/***********************************************************************
 * dns_server_now (internal)
 * Chooses the DNS server to ask: the one from dns_set_server(), else
 * the one DHCP told us about, else the gateway (home routers usually
 * are a DNS server too).
 *
 * Input:
 *   none
 * Output:
 *   the server's address, or 0 if we know none
 **********************************************************************/
static uint32_t dns_server_now( void )
{
	if( fixed_server != 0 ) return fixed_server;
	if( dhcp_get_dns() != 0 ) return dhcp_get_dns();
	return ip4_get_gateway();
}

/***********************************************************************
 * dns_send (internal)
 * Sends (or sends again) the question for a cache entry and works out
 * when to repeat it if no answer comes.
 *
 * Input:
 *   e     the entry (state DNS_ASKING)
 *   now   jiffies now
 * Output:
 *   none
 **********************************************************************/
static void dns_send( dns_entry *e, uint32_t now )
{
	/* Header + longest name in label form (DNS_NAME_MAX + 2) + type
	 * and class. Static: no big buffer on the stack.
	 */
	static uint8_t q[DNS_HEADER_SIZE + DNS_NAME_MAX + 2 + DNS_QUESTION_TAIL];
	const char *s = e->name;
	uint8_t *p, *len_byte;

	memset( q, 0, DNS_HEADER_SIZE );
	q[DNS_HDR_ID] = (uint8_t)(e->id >> 8);
	q[DNS_HDR_ID + 1] = (uint8_t)e->id;
	q[DNS_HDR_FLAGS] = DNS_FLAG_RECURSE >> 8;
	q[DNS_HDR_QUESTIONS + 1] = 1;			/* 1 question */

	/* "www.gitlab.com" -> 3 www 6 gitlab 3 com 0. The labels were
	 * checked in dns_resolve(), so they all fit.
	 */
	p = &q[DNS_HEADER_SIZE];
	len_byte = p++;
	*len_byte = 0;
	for( ; *s != 0; s++ )
	{
		if( *s == '.' )
		{
			len_byte = p++;
			*len_byte = 0;
		}
		else
		{
			*p++ = (uint8_t)*s;
			(*len_byte)++;
		}
	}
	*p++ = 0;								/* end of the name */
	*p++ = 0; *p++ = DNS_TYPE_A;
	*p++ = 0; *p++ = DNS_CLASS_IN;

	udp_send( DNS_CLIENT_PORT, DNS_SERVER_PORT, e->server, q, (size_t)(p - q) );

	e->next_try = now + ((uint32_t)DNS_FIRST_TIMEOUT << e->tries);
	e->tries++;
}

/***********************************************************************
 * dns_valid_name (internal)
 * Checks that a name can be put in a question: 1 to DNS_NAME_MAX
 * characters, labels (the parts between the dots) of 1 to 63
 * characters. A dot at the end ("gitlab.com.") is not allowed here.
 *
 * Input:
 *   name   the name
 * Output:
 *   true if it is a valid name
 **********************************************************************/
static bool dns_valid_name( const char *name )
{
	size_t total = 0, label = 0;

	for( ; *name != 0; name++ )
	{
		if( ++total > DNS_NAME_MAX ) return false;
		if( *name == '.' )
		{
			if( label == 0 ) return false;		/* empty label: ".." or a leading dot */
			label = 0;
		}
		else if( ++label > 63 )
			return false;
	}
	return label > 0;
}

/***********************************************************************
 * dns_find (internal)
 * Finds a name in the cache.
 *
 * Input:
 *   name   the name
 * Output:
 *   the entry, or NULL if the name is not in the cache
 **********************************************************************/
static dns_entry *dns_find( const char *name )
{
	uint8_t i;

	for( i = 0; i < DNS_CACHE_SIZE; i++ )
		if( cache[i].state != DNS_FREE && dns_same_name( cache[i].name, name ) )
			return &cache[i];
	return NULL;
}

/***********************************************************************
 * dns_alloc (internal)
 * Finds a cache entry for a new name: a free one, or else the one that
 * was not used for the longest time. Entries that are still waiting
 * for an answer are never taken.
 *
 * Input:
 *   now   jiffies now
 * Output:
 *   the entry, or NULL if all entries are waiting for an answer
 **********************************************************************/
static dns_entry *dns_alloc( uint32_t now )
{
	dns_entry *best = NULL;
	uint8_t i;

	for( i = 0; i < DNS_CACHE_SIZE; i++ )
	{
		if( cache[i].state == DNS_FREE ) return &cache[i];
		if( cache[i].state == DNS_ASKING ) continue;
		if( best == NULL || now - cache[i].last_used > now - best->last_used )
			best = &cache[i];
	}
	return best;
}

/***********************************************************************
 * dns_init
 * Sets up the DNS client: an empty cache, the server from DHCP or the
 * gateway (see dns_set_server() to choose one yourself).
 *
 * Input:
 *   none
 * Output:
 *   none
 **********************************************************************/
void dns_init( void )
{
	memset( cache, 0, sizeof(cache) );
	fixed_server = 0;
	next_id = (uint16_t)utl_jiffies();
}

/***********************************************************************
 * dns_set_server
 * Chooses the DNS server to ask, instead of the one from DHCP or the
 * gateway.
 *
 * Input:
 *   server   its IP address; 0 = back to automatic
 * Output:
 *   none
 **********************************************************************/
void dns_set_server( uint32_t server )
{
	fixed_server = server;
}

/***********************************************************************
 * dns_get_server
 * Tells which DNS server is asked now.
 *
 * Input:
 *   none
 * Output:
 *   the server's address, or 0 if we know none
 **********************************************************************/
uint32_t dns_get_server( void )
{
	return dns_server_now();
}

/***********************************************************************
 * dns_resolve
 * Finds the IPv4 address of a name. Never waits: call it again (from
 * the main loop, with felinc_poll() in between) as long as it returns
 * DNS_BUSY. An address written as text ("10.77.77.1") is converted
 * at once.
 *
 * Input:
 *   name   the name, like "gitlab.com"
 *   ip     where to store the address (only on DNS_DONE)
 * Output:
 *   DNS_DONE    the address is in ip
 *   DNS_BUSY    still asking (or we have no IP address yet: DHCP)
 *   DNS_FAILED  no address: the name does not exist, has no IPv4
 *               address, is invalid, or no server answered. The
 *               reason is printed on the screen.
 **********************************************************************/
int16_t dns_resolve( const char *name, uint32_t *ip )
{
	uint32_t now = utl_jiffies();
	dns_entry *e;

	if( name == NULL ) return DNS_FAILED;
	if( dns_is_ip( name, ip ) ) return DNS_DONE;

	e = dns_find( name );
	if( e != NULL )
	{
		e->last_used = now;
		if( e->state == DNS_ASKING ) return DNS_BUSY;
		if( (int32_t)(now - e->expires) < 0 )	/* still valid */
		{
			if( e->state == DNS_NOT_FOUND ) return DNS_FAILED;
			*ip = e->ip;
			return DNS_DONE;
		}
		/* Too old: ask again, in the same entry */
	}
	else
	{
		if( !dns_valid_name( name ) )
		{
			utl_print("\nDNS: \"%s\" is not a valid name", name);
			return DNS_FAILED;
		}
		e = dns_alloc( now );
		if( e == NULL )
		{
			utl_print("\nDNS: too many lookups at the same time");
			return DNS_FAILED;
		}
		strcpy( e->name, name );
	}

	/* Without an address of our own (DHCP still busy) we cannot ask
	 * yet. The entry is not used then, so the next call tries again.
	 */
	if( ip4_get_ip() == 0 || dns_server_now() == 0 )
	{
		e->state = DNS_FREE;
		return DNS_BUSY;
	}

	e->state = DNS_ASKING;
	e->server = dns_server_now();
	e->id = ++next_id;
	e->tries = 0;
	e->last_used = now;
	dns_send( e, now );
	return DNS_BUSY;
}

/***********************************************************************
 * dns_poll
 * Repeats questions that got no answer in time, and gives up after
 * DNS_TRIES tries. felinc_poll() calls it once per jiffy.
 *
 * Input:
 *   now   jiffies now
 * Output:
 *   none
 **********************************************************************/
void dns_poll( uint32_t now )
{
	char a[16];
	uint8_t i;
	dns_entry *e;

	for( i = 0; i < DNS_CACHE_SIZE; i++ )
	{
		e = &cache[i];
		if( e->state != DNS_ASKING || (int32_t)(now - e->next_try) < 0 ) continue;

		if( e->tries >= DNS_TRIES )
		{
			ip4_uint32_t_to_string( a, e->server );
			utl_print("\nDNS: no answer from %s for %s", a, e->name);
			e->state = DNS_NOT_FOUND;
			e->expires = now + DNS_FAILED_TTL * 60;
		}
		else
			dns_send( e, now );
	}
}

/***********************************************************************
 * dns_fail (internal)
 * Marks a lookup as failed and prints why.
 *
 * Input:
 *   e        the entry
 *   reason   text for the screen
 * Output:
 *   none
 **********************************************************************/
static void dns_fail( dns_entry *e, const char *reason )
{
	utl_print("\nDNS: %s: %s", e->name, reason);
	e->state = DNS_NOT_FOUND;
	e->expires = utl_jiffies() + DNS_FAILED_TTL * 60;
}

/***********************************************************************
 * dns_read_record (internal)
 * Reads one answer record of a DNS message: its name, the fixed fields
 * behind the name, and where its data is. Everything is checked against
 * the message length, because the message comes from the network.
 *
 * Input:
 *   data    the DNS message
 *   length  its length in bytes
 *   pos     where the record starts; moved to the next record
 *   rec     where to store the record
 * Output:
 *   true if the record was read, false if the message is damaged
 **********************************************************************/
static bool dns_read_record( const uint8_t *data, size_t length, size_t *pos, dns_record *rec )
{
	size_t p = *pos;

	if( !dns_read_name( data, length, &p, rec->name ) || p + DNS_RECORD_TAIL > length )
		return false;

	rec->type = dns_get16( &data[p] );
	rec->rr_class = dns_get16( &data[p + 2] );
	rec->ttl = dns_get32( &data[p + 4] );
	rec->data_length = dns_get16( &data[p + 8] );
	rec->data_pos = p + DNS_RECORD_TAIL;
	if( rec->data_pos + rec->data_length > length ) return false;

	*pos = rec->data_pos + rec->data_length;
	return true;
}

/***********************************************************************
 * dns_receive
 * Handles a datagram that arrived on our DNS port: checks that it is
 * the answer to one of our questions, and takes the address from it.
 *
 * The answer section holds records: "name has address x" (A) and
 * "name is an alias for other-name" (CNAME). Starting with the name we
 * asked for, we follow the aliases until we find an address.
 *
 * Input:
 *   src_ip     who sent it
 *   src_port   from which port
 *   data       the DNS message
 *   length     its length in bytes
 * Output:
 *   none
 **********************************************************************/
void dns_receive( uint32_t src_ip, uint16_t src_port, const uint8_t *data, size_t length )
{
	/* Static: too big for the stack, and nothing runs at the same time */
	static char target[DNS_NAME_MAX + 1];
	static dns_record rec;
	dns_entry *e = NULL, *c;
	uint16_t id, flags, questions, answers, i;
	uint32_t min_ttl = DNS_MAX_TTL;
	size_t pos, answers_start, alias_pos;
	uint8_t aliases;
	bool found_alias;

	if( src_port != DNS_SERVER_PORT || length < DNS_HEADER_SIZE ) return;

	/* Which of our questions is it the answer to? The ID and the
	 * server must match: an answer from anyone else is not trusted.
	 * (A pointer, not the 16-bit index i: Calypsi 5.18 miscompiles
	 * cache[i] in such a loop, see the Calypsi note in README.md.)
	 */
	id = dns_get16( &data[DNS_HDR_ID] );
	for( c = cache; c < cache + DNS_CACHE_SIZE; c++ )
		if( c->state == DNS_ASKING && c->id == id && c->server == src_ip ) e = c;
	if( e == NULL ) return;

	flags = dns_get16( &data[DNS_HDR_FLAGS] );
	questions = dns_get16( &data[DNS_HDR_QUESTIONS] );
	answers = dns_get16( &data[DNS_HDR_ANSWERS] );
	if( !(flags & DNS_FLAG_RESPONSE) ) return;

	if( (flags & DNS_RCODE_MASK) == DNS_RCODE_NAME_ERROR )
	{
		dns_fail( e, "name does not exist" );
		return;
	}
	if( (flags & DNS_RCODE_MASK) != 0 )
	{
		dns_fail( e, "the DNS server could not answer" );
		return;
	}

	/* The server repeats our question; it must be our name */
	pos = DNS_HEADER_SIZE;
	if( questions != 1 || !dns_read_name( data, length, &pos, rec.name ) ||
		!dns_same_name( rec.name, e->name ) || pos + DNS_QUESTION_TAIL > length )
		return;
	answers_start = pos + DNS_QUESTION_TAIL;

	/* Follow the aliases. Each pass looks through all answers for a
	 * record about the current target name: an address ends the
	 * search, an alias gives the next target name.
	 */
	strcpy( target, e->name );
	for( aliases = 0; aliases <= DNS_MAX_ALIASES; aliases++ )
	{
		found_alias = false;
		pos = answers_start;
		for( i = 0; i < answers; i++ )
		{
			if( !dns_read_record( data, length, &pos, &rec ) )
			{
				dns_fail( e, "damaged answer" );
				return;
			}
			if( rec.rr_class != DNS_CLASS_IN || !dns_same_name( rec.name, target ) )
				continue;			/* not about the name we follow */

			if( rec.type == DNS_TYPE_A && rec.data_length == 4 )
			{
				if( rec.ttl < min_ttl ) min_ttl = rec.ttl;
				if( min_ttl < DNS_MIN_TTL ) min_ttl = DNS_MIN_TTL;
				memcpy( &e->ip, &data[rec.data_pos], 4 );
				e->state = DNS_KNOWN;
				e->expires = utl_jiffies() + min_ttl * 60;
				return;
			}
			if( rec.type == DNS_TYPE_CNAME )
			{
				alias_pos = rec.data_pos;
				if( !dns_read_name( data, length, &alias_pos, target ) )
				{
					dns_fail( e, "damaged answer" );
					return;
				}
				/* The address is only valid as long as every alias is */
				if( rec.ttl < min_ttl ) min_ttl = rec.ttl;
				found_alias = true;
			}
		}
		if( !found_alias ) break;		/* nothing more to follow */
	}

	dns_fail( e, aliases > DNS_MAX_ALIASES ? "too many aliases" : "no IPv4 address" );
}

/***********************************************************************
 * dns_print_cache
 * Prints the DNS server and the cache on the screen.
 *
 * Input:
 *   none
 * Output:
 *   none
 **********************************************************************/
void dns_print_cache( void )
{
	static const char *names[] = { "", "asking", "", "not found" };
	uint32_t now = utl_jiffies();
	char a[16];
	uint8_t i, shown = 0;
	int32_t left;

	ip4_uint32_t_to_string( a, dns_server_now() );
	utl_print("\n*** DNS server %s", a);
	for( i = 0; i < DNS_CACHE_SIZE; i++ )
	{
		if( cache[i].state == DNS_FREE ) continue;
		shown++;
		utl_print("\n    %-30s ", cache[i].name);
		if( cache[i].state == DNS_KNOWN )
		{
			ip4_uint32_t_to_string( a, cache[i].ip );
			utl_print("%-15s ", a);
		}
		else
			utl_print("%-15s ", names[cache[i].state]);
		if( cache[i].state != DNS_ASKING )
		{
			left = (int32_t)(cache[i].expires - now) / 60;
			utl_print("%ld s", (long)(left > 0 ? left : 0));
		}
	}
	/* Otherwise an empty cache looks like nothing was printed */
	if( shown == 0 ) utl_print("\n    (cache empty)");
}
