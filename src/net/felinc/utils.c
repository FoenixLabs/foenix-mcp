/***********************************************************************
 * UTILS.C - small helpers used by all FELINC modules:
 * Internet checksums, buffer pools, time base and conversions.
 *
 * Part of FELINC, the Foenix Labs Ethernet Library in C. (c) R. Kint
 **********************************************************************/
#include <stdio.h>
#include <stdarg.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>

#include <mcp/syscalls.h>

#include "utils.h"
#include "prof.h"

/***********************************************************************
 * utl_sum16
 * Adds the 16-bit words of a memory block to a running checksum sum.
 * This is the first half of the Internet checksum (RFC 1071). It is
 * separate from utl_checksum() because UDP needs one checksum over two
 * blocks (a "pseudo header" and the UDP packet itself).
 * An odd last byte counts as the high byte of a 16-bit word, as the RFC
 * requires on a big-endian machine like the 68040.
 *
 * Input:
 *   sum    running sum so far (0 for the first block)
 *   addr   start of the memory block (must be at an even address)
 *   len    length of the block in bytes
 * Output:
 *   the new running sum; finish it with utl_checksum_finish()
 **********************************************************************/
uint32_t utl_sum16( uint32_t sum, const void *addr, size_t len )
{
	const uint16_t *w = (const uint16_t *)addr;
	PROF_START( sum );

	while( len > 1 )
	{
		sum += *w++;
		len -= 2;
	}
	if( len == 1 )
		sum += (uint16_t)(*(const uint8_t *)w) << 8;

	PROF_STOP( PROF_CHECKSUM, sum );
	return sum;
}

/***********************************************************************
 * utl_checksum_finish
 * Turns a running sum from utl_sum16() into the final 16-bit Internet
 * checksum: the carries above 16 bits are added back in (one's
 * complement addition) and the result is inverted.
 *
 * Input:
 *   sum    running sum from utl_sum16()
 * Output:
 *   the checksum, ready to store in a packet header
 **********************************************************************/
uint16_t utl_checksum_finish( uint32_t sum )
{
	sum = (sum >> 16) + (sum & 0xFFFF);
	sum += (sum >> 16);		/* the first addition can carry once more */
	return (uint16_t)(~sum & 0xFFFF);
}

/***********************************************************************
 * utl_checksum
 * Computes the Internet checksum (RFC 1071) of one memory block, as
 * used in the IPv4 and ICMP headers.
 * To create a checksum: set the checksum field to 0 first, then store
 * the result in it. To check a received header: the checksum over the
 * whole header (checksum field included) is 0 when it is correct.
 *
 * Input:
 *   addr   start of the memory block (must be at an even address)
 *   len    length of the block in bytes
 * Output:
 *   the 16-bit checksum
 **********************************************************************/
uint16_t utl_checksum( const void *addr, size_t len )
{
	return utl_checksum_finish( utl_sum16( 0, addr, len ) );
}

/***********************************************************************
 * utl_copy
 * Copies a block of memory, like memcpy(), but faster for the large
 * blocks FELINC moves around (segments of up to 1460 bytes). The C
 * library's memcpy() copies one byte at a time; this copies 16 bytes
 * per loop pass, in 32-bit words, so far fewer memory accesses and
 * loop steps are needed. Measured on the A2560K: memcpy() takes about
 * 1 us per byte.
 * Words can only be used when source and destination are both at an
 * even address or both at an odd one (after one byte, both are even);
 * otherwise it copies byte by byte, like memcpy(). Even addresses are
 * enough: the 68040 reads a word at any even address.
 *
 * Input:
 *   dst   where to copy to
 *   src   where to copy from (the blocks must not overlap)
 *   len   number of bytes
 * Output:
 *   none
 **********************************************************************/
void utl_copy( void *dst, const void *src, size_t len )
{
	uint8_t *d = (uint8_t *)dst;
	const uint8_t *s = (const uint8_t *)src;
	uint32_t *dl;
	const uint32_t *sl;

	if( (((uint32_t)d ^ (uint32_t)s) & 1) == 0 )
	{
		if( ((uint32_t)d & 1) && len > 0 )		/* both odd: one byte first */
		{
			*d++ = *s++;
			len--;
		}
		dl = (uint32_t *)d;
		sl = (const uint32_t *)s;
		while( len >= 16 )
		{
			dl[0] = sl[0];
			dl[1] = sl[1];
			dl[2] = sl[2];
			dl[3] = sl[3];
			dl += 4;
			sl += 4;
			len -= 16;
		}
		while( len >= 4 )
		{
			*dl++ = *sl++;
			len -= 4;
		}
		d = (uint8_t *)dl;
		s = (const uint8_t *)sl;
	}
	while( len > 0 )		/* the last bytes, or all of them when not aligned */
	{
		*d++ = *s++;
		len--;
	}
}

/* Where FELINC gets its memory and sends its text. NULL = the C
 * library (malloc, free, stdout). Set with felinc_set_alloc() and
 * felinc_set_print(), so a program that has its own memory or screen
 * (like the MCP) can run FELINC in its own way.
 */
static void *(*alloc_hook)( size_t size ) = NULL;
static void (*release_hook)( void *block ) = NULL;
static void (*print_hook)( const char *text ) = NULL;
static void (*fatal_hook)( const char *reason ) = NULL;

/* One line of output is put together here before it goes to the hook;
 * longer text is cut off. Static: FELINC never prints from two places
 * at the same time, and the stack stays small.
 */
#define UTL_PRINT_MAX	256
static char print_buffer[UTL_PRINT_MAX];

/***********************************************************************
 * utl_set_alloc
 * Sets the functions FELINC gets and gives back memory with.
 *
 * Input:
 *   alloc     gets a block of memory (like malloc), or NULL for malloc
 *   release   gives a block back (like free), or NULL for free
 * Output:
 *   none
 **********************************************************************/
void utl_set_alloc( void *(*alloc)( size_t size ), void (*release)( void *block ) )
{
	alloc_hook = alloc;
	release_hook = release;
}

/***********************************************************************
 * utl_set_print
 * Sets the function all of FELINC's text goes to.
 *
 * Input:
 *   print   takes a piece of text (no newline added), or NULL for
 *           stdout
 * Output:
 *   none
 **********************************************************************/
void utl_set_print( void (*print)( const char *text ) )
{
	print_hook = print;
}

/***********************************************************************
 * utl_set_fatal
 * Sets the function FELINC calls when it finds a program error in
 * itself (see utl_fatal).
 *
 * Input:
 *   fatal   takes the reason as text, or NULL to print it and stop the
 *           program
 * Output:
 *   none
 **********************************************************************/
void utl_set_fatal( void (*fatal)( const char *reason ) )
{
	fatal_hook = fatal;
}

/***********************************************************************
 * utl_fatal
 * Reports a program error inside FELINC, such as a buffer given back
 * twice. Without a hook the reason is printed and the program stops,
 * because FELINC's data can no longer be trusted. With a hook (the
 * MCP's) this returns, and the caller must drop what it was doing.
 *
 * Input:
 *   reason   what went wrong
 * Output:
 *   none (only returns when a hook is set)
 **********************************************************************/
void utl_fatal( const char *reason )
{
	if( fatal_hook )
	{
		fatal_hook( reason );
		return;
	}
	utl_print( "\nFELINC: %s\n", reason );
	abort();
}

/***********************************************************************
 * utl_alloc
 * Gets a block of memory, through the hook if one is set.
 *
 * Input:
 *   size   number of bytes
 * Output:
 *   the block, or NULL if there is not enough memory
 **********************************************************************/
void *utl_alloc( size_t size )
{
	return alloc_hook ? alloc_hook( size ) : malloc( size );
}

/***********************************************************************
 * utl_calloc
 * Gets a block of memory filled with zeroes.
 *
 * Input:
 *   size   number of bytes
 * Output:
 *   the block, or NULL if there is not enough memory
 **********************************************************************/
void *utl_calloc( size_t size )
{
	void *block = utl_alloc( size );

	if( block != NULL ) memset( block, 0, size );
	return block;
}

/***********************************************************************
 * utl_free
 * Gives back a block from utl_alloc() or utl_calloc().
 *
 * Input:
 *   block   the block (NULL is allowed and does nothing)
 * Output:
 *   none
 **********************************************************************/
void utl_free( void *block )
{
	if( block == NULL ) return;
	if( release_hook ) release_hook( block );
	else free( block );
}

/***********************************************************************
 * utl_print
 * Formats text like printf() and sends it to the print hook, or to
 * stdout when none is set. All of FELINC's messages and status lists
 * go through here.
 *
 * Input:
 *   format   printf format, followed by its values
 * Output:
 *   none
 **********************************************************************/
void utl_print( const char *format, ... )
{
	va_list args;

	va_start( args, format );
	vsnprintf( print_buffer, sizeof(print_buffer), format, args );
	va_end( args );

	if( print_hook ) print_hook( print_buffer );
	else fputs( print_buffer, stdout );
}

/***********************************************************************
 * utl_pool_create
 * Creates a pool of 'depth' buffers of 'size' bytes each. All memory is
 * allocated here, at start-up, so that handling packets later never
 * needs memory (see the explanation in utils.h).
 *
 * Input:
 *   size   size of each buffer in bytes
 *   depth  number of buffers in the pool
 * Output:
 *   pointer to the new pool, or NULL if there is not enough memory
 **********************************************************************/
utl_pool *utl_pool_create( size_t size, uint16_t depth )
{
	utl_pool *pool;
	uint16_t i;

	pool = (utl_pool *)utl_alloc( sizeof(utl_pool) );
	if( pool == NULL ) return NULL;

	pool->depth = depth;
	pool->size = size;
	pool->count = 0;
	pool->items = (void **)utl_alloc( depth * sizeof(void *) );
	if( pool->items == NULL )
	{
		utl_pool_destroy( pool );
		return NULL;
	}
	for( i = 0; i < depth; i++ )
	{
		pool->items[i] = utl_alloc( size );
		if( pool->items[i] == NULL )
		{
			utl_pool_destroy( pool );		/* gives back the buffers we got so far */
			return NULL;
		}
		pool->count++;
	}
	return pool;
}

/***********************************************************************
 * utl_pool_get
 * Takes a free buffer from a pool.
 * An empty pool means a module forgot to give buffers back, or pools
 * are too small for how deep the modules call each other. That is a
 * program error, so we stop the program with a message.
 *
 * Input:
 *   pool   the pool (as returned by utl_pool_create)
 * Output:
 *   pointer to a free buffer of the pool's size
 **********************************************************************/
void *utl_pool_get( utl_pool *pool )
{
	if( pool == NULL || pool->count == 0 )
	{
		utl_fatal( "UTL_POOL_GET: buffer pool empty" );
		return NULL;		/* the caller drops its packet */
	}
	return pool->items[--pool->count];
}

/***********************************************************************
 * utl_pool_put
 * Gives a buffer back to its pool.
 * Giving back more buffers than the pool was created with means a
 * module gave back a buffer twice. That is a program error that would
 * corrupt packets later: it goes to utl_fatal().
 *
 * Input:
 *   pool   the pool the buffer came from
 *   data   the buffer, as returned earlier by utl_pool_get()
 * Output:
 *   none
 **********************************************************************/
void utl_pool_put( utl_pool *pool, void *data )
{
	if( pool == NULL || data == NULL || pool->count >= pool->depth )
	{
		utl_fatal( "UTL_POOL_PUT: buffer given back twice" );
		return;				/* the buffer is not taken back */
	}
	pool->items[pool->count++] = data;
}

/***********************************************************************
 * utl_pool_destroy
 * Gives a pool and all its buffers back. Every buffer must be back in
 * the pool (FELINC only holds one while it sends a packet).
 *
 * Input:
 *   pool   the pool (NULL is allowed and does nothing)
 * Output:
 *   none
 **********************************************************************/
void utl_pool_destroy( utl_pool *pool )
{
	if( pool == NULL ) return;
	if( pool->items != NULL )
	{
		while( pool->count > 0 ) utl_free( pool->items[--pool->count] );
		utl_free( pool->items );
	}
	utl_free( pool );
}

/***********************************************************************
 * utl_jiffies
 * Returns the time since boot in jiffies (1/60 s), from the MCP.
 * FELINC uses this for its timers (ARP, DHCP and DNS).
 * The counter wraps around after about 2 years. Always compare times
 * as (now - then) >= interval: with unsigned numbers that stays correct
 * across the wrap-around, where (now >= then + interval) would not.
 *
 * Input:
 *   none
 * Output:
 *   jiffies since boot
 **********************************************************************/
uint32_t utl_jiffies( void )
{
	uint32_t now;
	PROF_START( jiffies );

	now = (uint32_t)sys_time_jiffies();
	PROF_STOP( PROF_JIFFIES, jiffies );
	return now;
}

/***********************************************************************
 * hex_digit_value (internal)
 * Converts one hexadecimal character to its value.
 *
 * Input:
 *   c      '0'-'9', 'A'-'F' or 'a'-'f'
 * Output:
 *   0-15; any other character gives 0
 **********************************************************************/
static uint8_t hex_digit_value( char c )
{
	if( c >= '0' && c <= '9' ) return (uint8_t)(c - '0');
	if( c >= 'A' && c <= 'F' ) return (uint8_t)(c - 'A' + 10);
	if( c >= 'a' && c <= 'f' ) return (uint8_t)(c - 'a' + 10);
	return 0;
}

/***********************************************************************
 * utl_hex_to_uint32
 * Converts a hexadecimal text (without "0x") to a number, for example
 * one part "AB" of the MAC address string "00:0D:80:61:94:AB".
 *
 * Input:
 *   hex_string  up to 8 hexadecimal digits, ended by 0
 * Output:
 *   the value; characters that are not hex digits count as 0
 **********************************************************************/
uint32_t utl_hex_to_uint32( const char *hex_string )
{
	uint32_t value = 0;

	while( *hex_string != '\0' )
		value = (value << 4) | hex_digit_value( *hex_string++ );
	return value;
}

/***********************************************************************
 * utl_poke32
 * Writes a 32-bit value to an absolute memory address, for example to
 * put an interrupt handler in the 68040 vector table.
 *
 * Input:
 *   address  the memory address (must be even)
 *   value    the value to write
 * Output:
 *   none
 **********************************************************************/
void utl_poke32( uint32_t address, uint32_t value )
{
	*(volatile uint32_t *)address = value;
}
