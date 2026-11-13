/*
 * cache.c - 68040 / 68060 cache control, C side (MCP 2.0.29)
 *
 * The real work is done by assembler routines in the CPU's startup file
 * (m68040/startup_m68040.s): cache_hw_enable, cache_flush, cache_data_off
 * and cache_restore. This file only keeps track of whether the caches are on,
 * so the boot screen and SYSINFO can show it without reading the CPU's cache
 * register (which a user program is not allowed to do).
 */

#include "cache.h"

/* The assembler routine that sets up the cache modes and switches both caches on */
extern void cache_hw_enable(void);

/* 1 once the caches are on. Only a CACHE=1 build ever sets it. */
static short cache_on = 0;

/**
 * Switch the caches on, if this is a CACHE=1 build.
 */
void cache_enable(void) {
#ifdef CACHE_ON
    cache_hw_enable();
    cache_on = 1;
#endif
}

/**
 * Tell whether the caches are on.
 *
 * @return 1 if the caches are on, 0 if not
 */
short cache_is_on(void) {
    return cache_on;
}
