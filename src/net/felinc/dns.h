/***********************************************************************
 * DNS.H - DNS client for FELINC (see dns.c)
 *
 * Part of FELINC, the Foenix Labs Ethernet Library in C. (c) R. Kint
 **********************************************************************/
#ifndef __DNS_H__
#define __DNS_H__

#include <stdint.h>
#include <stddef.h>

#include "felinc.h"

#define DNS_SERVER_PORT		53
#define DNS_CLIENT_PORT		49153	/* our side, see dns.c */

#define DNS_NAME_MAX		253		/* longest name DNS allows, without the final 0 */

/* Results of dns_resolve() */
/* Results of dns_resolve(): the same as felinc_resolve() returns */
#define DNS_DONE		FELINC_RESOLVE_DONE
#define DNS_BUSY		FELINC_RESOLVE_BUSY
#define DNS_FAILED		FELINC_RESOLVE_FAILED

void dns_init( void );
void dns_set_server( uint32_t server );
uint32_t dns_get_server( void );
int16_t dns_resolve( const char *name, uint32_t *ip );
void dns_poll( uint32_t now );
void dns_receive( uint32_t src_ip, uint16_t src_port, const uint8_t *data, size_t length );
void dns_print_cache( void );

#endif
