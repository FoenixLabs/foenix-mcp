/***********************************************************************
 * UTILS.H - small helpers used by all FELINC modules:
 * Internet checksums, buffer pools, time base and conversions.
 *
 * Part of FELINC, the Foenix Labs Ethernet Library in C. (c) R. Kint
 **********************************************************************/
#ifndef __UTILS_H__
#define __UTILS_H__

#include <stdint.h>
#include <stddef.h>

/* A pool of equal-sized buffers, all allocated once by
 * utl_pool_create(). Modules take a buffer with utl_pool_get() while
 * they build a packet and give it back with utl_pool_put().
 * This way no memory is allocated while packets are being handled,
 * so the heap cannot fragment or run out during operation.
 */
typedef struct utl_pool
{
	void **items;		/* the free buffers */
	uint16_t count;		/* number of free buffers in items[] */
	uint16_t depth;		/* total number of buffers in the pool */
	size_t size;		/* size of each buffer in bytes */
} utl_pool;

/* Memory and text, through the hooks of felinc_set_alloc() and
 * felinc_set_print() */
void utl_set_alloc( void *(*alloc)( size_t size ), void (*release)( void *block ) );
void utl_set_print( void (*print)( const char *text ) );
void *utl_alloc( size_t size );
void *utl_calloc( size_t size );
void utl_free( void *block );
void utl_print( const char *format, ... );
void utl_set_fatal( void (*fatal)( const char *reason ) );
void utl_fatal( const char *reason );

/* Internet checksum (RFC 1071) */
uint32_t utl_sum16( uint32_t sum, const void *addr, size_t len );
uint16_t utl_checksum_finish( uint32_t sum );
uint16_t utl_checksum( const void *addr, size_t len );

/* Copying memory: a faster memcpy() for large blocks */
void utl_copy( void *dst, const void *src, size_t len );

/* Buffer pools */
utl_pool *utl_pool_create( size_t size, uint16_t depth );
void *utl_pool_get( utl_pool *pool );
void utl_pool_put( utl_pool *pool, void *data );
void utl_pool_destroy( utl_pool *pool );

/* Time base: jiffies (1/60 s) since boot */
uint32_t utl_jiffies( void );

/* Conversions and memory access */
uint32_t utl_hex_to_uint32( const char *hex_string );
void utl_poke32( uint32_t address, uint32_t value );

#endif
