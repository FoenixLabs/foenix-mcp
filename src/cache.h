/*
 * cache.h - 68040 / 68060 cache control (MCP 2.0.29)
 *
 * The MCP normally runs with the CPU caches off. A build made with
 *     make ... CACHE=1
 * (which defines CACHE_ON) switches the data and instruction caches on once
 * the hardware has been set up. Register space and flash data stay
 * non-cacheable; RAM is cached write-through, so every CPU write still
 * reaches memory at once. In a normal build these functions do nothing.
 *
 * The routines use privileged CPU instructions: call them only from kernel
 * code, never from a user program.
 */

#ifndef __CACHE_H
#define __CACHE_H

/**
 * Switch the caches on, if this is a CACHE=1 build.
 * Call it once, after all hardware has been initialized: the start-up code
 * has delay loops that count CPU cycles and would run too fast with the
 * instruction cache on.
 */
extern void cache_enable(void);

/**
 * Tell whether the caches are on.
 *
 * @return 1 if cache_enable() switched the caches on, 0 if not
 */
extern short cache_is_on(void);

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
 *
 * @return the old cache setting, to be given to cache_restore()
 */
extern unsigned long cache_data_off(void);

/**
 * Put back the cache setting that cache_data_off() returned.
 *
 * @param cacr the value cache_data_off() returned
 */
extern void cache_restore(unsigned long cacr);

#endif
