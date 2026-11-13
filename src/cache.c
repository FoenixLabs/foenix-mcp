/*
 * cache.c - 68040 / 68060 cache control, C side (MCP 2.0.29, 2.0.30)
 *
 * The real work is done by assembler routines in the CPU's startup file
 * (m68040/startup_m68040.s): cache_hw_enable, cache_flush, cache_data_off
 * and cache_get_cacr.
 */

#include "cache.h"

/* The assembler routine that sets up the cache modes and switches both caches on */
extern void cache_hw_enable(void);

/* The assembler routine that reads the CPU's cache control register (0 on a 68000) */
extern unsigned long cache_get_cacr(void);

/* Cache control register bits (the same on the 68040 and the 68060) */
#define CACR_DE 0x80000000UL    /* data cache enable */
#define CACR_IE 0x00008000UL    /* instruction cache enable */

/**
 * Switch the caches on, if this is a CACHE=1 build.
 */
void cache_enable(void) {
#ifdef CACHE_ON
    cache_hw_enable();
#endif
}

/**
 * Set this MCP's caches up again, if this is a CACHE=1 build.
 *
 * 2.0.30: this does the whole set-up again instead of putting back a saved
 * register value, so it is right whatever happened in between.
 */
void cache_resume(void) {
#ifdef CACHE_ON
    cache_hw_enable();
#endif
}

/**
 * Tell what the caches really do right now.
 *
 * @return CACHE_DATA and/or CACHE_INSTRUCTIONS, or 0 when both are off
 */
short cache_state(void) {
    unsigned long cacr = cache_get_cacr();
    short state = 0;

    if (cacr & CACR_DE) state |= CACHE_DATA;
    if (cacr & CACR_IE) state |= CACHE_INSTRUCTIONS;
    return state;
}

/**
 * The cache state as text.
 *
 * @return "on", "off", "instructions only" or "data only"
 */
const char * cache_state_text(void) {
    switch (cache_state()) {
        case CACHE_DATA | CACHE_INSTRUCTIONS:   return "on";
        case CACHE_INSTRUCTIONS:                return "instructions only";
        case CACHE_DATA:                        return "data only";
        default:                                return "off";
    }
}
