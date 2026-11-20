/***********************************************************************
 * CBUF.C - circular byte buffer
 *
 * Each FELINC socket has two of these: one for received datagrams and
 * one for datagrams waiting to be sent. A circular buffer lets the
 * network side add data while the program takes data out at its own
 * pace, without ever moving the data around in memory.
 * The memory is allocated once in cbuf_create(); put and get never
 * allocate.
 *
 * Part of FELINC, the Foenix Labs Ethernet Library in C. (c) R. Kint
 **********************************************************************/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cbuf.h"
#include "utils.h"
#include "prof.h"

/***********************************************************************
 * cbuf_create
 * Creates an empty circular buffer.
 *
 * Input:
 *   size   capacity of the buffer in bytes
 * Output:
 *   pointer to the new buffer, or NULL if there is not enough memory
 **********************************************************************/
cbuf *cbuf_create( size_t size )
{
	cbuf *this_cbuf;

	this_cbuf = (cbuf *)utl_alloc( sizeof(cbuf) );
	if( this_cbuf == NULL )
	{
		utl_print("\nCBUF_CREATE: no memory for cbuf struct" );
		return NULL;
	}

	this_cbuf->buf = (int8_t *)utl_alloc( size );
	if( this_cbuf->buf == NULL )
	{
		utl_print("\nCBUF_CREATE: no memory for buffer" );
		utl_free( this_cbuf );
		return NULL;
	}

	this_cbuf->inptr = 0;
	this_cbuf->outptr = 0;
	this_cbuf->len = size;
	this_cbuf->count = 0;
	return this_cbuf;
}

/***********************************************************************
 * cbuf_put
 * Adds bytes at the end of the buffer. When the data passes the end of
 * the memory block it continues at the start ("wraps around").
 * It is all or nothing: a datagram that only half fits would be
 * useless, so nothing is stored if it does not fit completely.
 *
 * Input:
 *   buf    the buffer
 *   data   the bytes to add
 *   size   number of bytes
 * Output:
 *   'size' on success, -1 if there is not enough room (nothing stored)
 **********************************************************************/
ssize_t cbuf_put( cbuf *buf, const void *data, size_t size )
{
	const uint8_t *src = (const uint8_t *)data;
	uint8_t *dst = (uint8_t *)buf->buf;
	size_t first;

	if( size > buf->len - buf->count ) return -1;

	/* Part up to the end of the memory block, then the rest at the start */
	first = buf->len - buf->inptr;
	if( first > size ) first = size;
	{
		PROF_START( copy );
		utl_copy( dst + buf->inptr, src, first );
		utl_copy( dst, src + first, size - first );
		PROF_STOP( PROF_COPY, copy );
	}

	buf->inptr = (buf->inptr + size) % buf->len;
	buf->count += size;
	return (ssize_t)size;
}

/***********************************************************************
 * cbuf_get
 * Takes bytes from the front of the buffer (the oldest data).
 *
 * Input:
 *   buf       the buffer
 *   data      where to copy the bytes to; NULL skips them (to throw
 *             away data the caller cannot use)
 *   req_size  number of bytes wanted
 * Output:
 *   number of bytes taken: req_size, or less if the buffer held less
 **********************************************************************/
ssize_t cbuf_get( cbuf *buf, void *data, size_t req_size )
{
	uint8_t *dst = (uint8_t *)data;
	const uint8_t *src = (const uint8_t *)buf->buf;
	size_t size, first;

	size = (req_size < buf->count) ? req_size : buf->count;

	if( dst != NULL )
	{
		/* Part up to the end of the memory block, then the rest from the start */
		first = buf->len - buf->outptr;
		if( first > size ) first = size;
		{
			PROF_START( copy );
			utl_copy( dst, src + buf->outptr, first );
			utl_copy( dst + first, src, size - first );
			PROF_STOP( PROF_COPY, copy );
		}
	}

	buf->outptr = (buf->outptr + size) % buf->len;
	buf->count -= size;
	return (ssize_t)size;
}

/***********************************************************************
 * cbuf_peek
 * Copies bytes from inside the buffer without taking them out. TCP
 * uses this: sent data must stay in the buffer until the other side
 * acknowledges it, because it may have to be sent again.
 *
 * Input:
 *   buf      the buffer
 *   offset   where to start, counted from the oldest byte
 *   data     where to copy the bytes to
 *   size     number of bytes wanted
 * Output:
 *   number of bytes copied: size, or less if the buffer holds less
 *   after offset
 **********************************************************************/
size_t cbuf_peek( cbuf *buf, size_t offset, void *data, size_t size )
{
	uint8_t *dst = (uint8_t *)data;
	const uint8_t *src = (const uint8_t *)buf->buf;
	size_t start, first;

	if( offset >= buf->count ) return 0;
	if( size > buf->count - offset ) size = buf->count - offset;

	/* Part up to the end of the memory block, then the rest from the start */
	start = (buf->outptr + offset) % buf->len;
	first = buf->len - start;
	if( first > size ) first = size;
	{
		PROF_START( copy );
		utl_copy( dst, src + start, first );
		utl_copy( dst + first, src, size - first );
		PROF_STOP( PROF_COPY, copy );
	}
	return size;
}

/***********************************************************************
 * cbuf_used
 * Tells how much data is waiting in the buffer.
 *
 * Input:
 *   buf    the buffer
 * Output:
 *   number of bytes in the buffer
 **********************************************************************/
ssize_t cbuf_used( cbuf *buf )
{
	return (ssize_t)buf->count;
}

/***********************************************************************
 * cbuf_free
 * Tells how much more data fits in the buffer.
 *
 * Input:
 *   buf    the buffer
 * Output:
 *   number of free bytes
 **********************************************************************/
size_t cbuf_free( cbuf *buf )
{
	return buf->len - buf->count;
}

/***********************************************************************
 * cbuf_destroy
 * Frees the memory of a buffer. The pointer must not be used afterwards.
 *
 * Input:
 *   buf    the buffer
 * Output:
 *   none
 **********************************************************************/
void cbuf_destroy( cbuf *buf )
{
	utl_free( buf->buf );
	utl_free( buf );
}
