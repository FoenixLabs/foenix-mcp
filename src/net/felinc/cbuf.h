/***********************************************************************
 * CBUF.H - circular byte buffer (see cbuf.c)
 *
 * Part of FELINC, the Foenix Labs Ethernet Library in C. (c) R. Kint
 **********************************************************************/
#ifndef __CBUF_H__
#define __CBUF_H__

#include <stdint.h>
#include <stddef.h>

#include "felinc.h"		/* ssize_t */

typedef struct cbuf
{
	int8_t *buf;		/* the memory block */
	size_t len;			/* size of the memory block */
	size_t count;		/* bytes in use */
	size_t inptr;		/* where the next byte is put */
	size_t outptr;		/* where the next byte is taken from */
} cbuf;

cbuf *cbuf_create( size_t size );
ssize_t cbuf_put( cbuf *buf, const void *data, size_t size );
ssize_t cbuf_get( cbuf *buf, void *data, size_t size );
size_t cbuf_peek( cbuf *buf, size_t offset, void *data, size_t size );
ssize_t cbuf_used( cbuf *buf );
size_t cbuf_free( cbuf *buf );
void cbuf_destroy( cbuf *buf );

#endif
