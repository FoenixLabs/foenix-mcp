/*
 * cache.h - 68040 / 68060 cache control (MCP 2.0.29, 2.0.30)
 *
 * The MCP normally runs with the CPU caches off. A build made with
 *     make ... CACHE=1
 * (which defines CACHE_ON) switches the data and instruction caches on once
 * the hardware has been set up. Register space and flash data stay
 * non-cacheable; RAM is cached write-through, so every CPU write still
 * reaches memory at once. In a normal build cache_enable, cache_flush,
 * cache_data_off and cache_resume do nothing.
 *
 * The routines use privileged CPU instructions: call them only from kernel
 * code, never from a user program.
 */

#ifndef __CACHE_H
#define __CACHE_H

#define CACHE_DATA          0x01    /* cache_state(): the data cache is on */
#define CACHE_INSTRUCTIONS  0x02    /* cache_state(): the instruction cache is on */

/**
 * Switch the caches on, if this is a CACHE=1 build.
 * Call it once, after all hardware has been initialized: the start-up code
 * has delay loops that count CPU cycles and would run too fast with the
 * instruction cache on.
 */
extern void cache_enable(void);

/**
 * Tell what the caches really do right now (read from the CPU, so it is
 * also right when a program changed them).
 *
 * @return CACHE_DATA and/or CACHE_INSTRUCTIONS, or 0 when both are off
 */
extern short cache_state(void);

/**
 * The cache state as text, for the boot screen and SYSINFO.
 *
 * @return "on", "off", "instructions only" or "data only"
 */
extern const char * cache_state_text(void);

/**
 * Write changed data to memory and empty both caches.
 * Needed before jumping into code that was loaded with data writes
 * (a program, a boot sector), because the instruction cache does not see
 * those writes.
 */
extern void cache_flush(void);

/**
 * Switch the data cache off for a while (the instruction cache stays on).
 * Needed around code that lets the GDMA or the blitter write memory the CPU
 * then reads: the CPU does not see those writes and could read old copies.
 * Call cache_resume() afterwards.
 */
extern void cache_data_off(void);

/**
 * Set this MCP's caches up again (after cache_data_off, or whenever they may
 * have been changed). Does nothing in a normal build.
 */
extern void cache_resume(void);

#endif
