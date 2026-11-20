/**
 * @file netpool.h
 *
 * 2.1.0: the network pool, the memory FELINC (the TCP/IP stack in the MCP) uses for its buffers.
 *
 * At boot the MCP takes NETPOOL_SIZE bytes from the top of system RAM with mem_reserve, so the
 * top of memory (sys_mem_get_ramtop) moves down by that much. Programs, the program loader and VI
 * stay below the top of memory, so they never use the pool. On the A2560K the pool is
 * $0038_0000 - $0039_FFFF: Calypsi programs keep their data below $0038_0000 and VBCC programs
 * stay below $0032_0000, so no program has to change.
 *
 * netpool_alloc and netpool_free work like malloc and free, on the pool only. FELINC allocates
 * when the network starts and when a socket opens, and frees when a socket closes.
 */

#ifndef __NETPOOL_H
#define __NETPOOL_H

#include <stddef.h>
#include <stdint.h>

#define NETPOOL_SIZE    0x20000     /* 128 KB */

/**
 * Take the pool from the top of system RAM and make it one free block. Call once at boot,
 * after mem_init.
 */
extern void netpool_init(void);

/**
 * Get a block from the pool.
 *
 * @param size how many bytes
 * @return the block (aligned to 4 bytes), or 0 if no free block is big enough
 */
extern void * netpool_alloc(size_t size);

/**
 * Get a block from the pool, filled with zeroes (like calloc).
 *
 * @param count how many items, size the size of one item
 * @return the block, or 0 if no free block is big enough
 */
extern void * netpool_calloc(size_t count, size_t size);

/**
 * Give a block back to the pool. A pointer that is not a block from netpool_alloc is ignored
 * (and counted, see netpool_info), so a wrong free cannot break the pool.
 *
 * @param block the block from netpool_alloc or netpool_calloc, or 0 (then nothing happens)
 */
extern void netpool_free(void * block);

/* What netpool_info reports */
typedef struct s_netpool_info {
    uint32_t start;             /* address of the pool (0 if there is none) */
    uint32_t size;              /* size of the pool in bytes */
    uint32_t used;              /* bytes in use, including the block headers */
    uint32_t largest_free;      /* the biggest block netpool_alloc can give now */
    uint16_t blocks_used;       /* blocks in use */
    uint16_t bad_frees;         /* netpool_free calls with a pointer that was no block in use */
} t_netpool_info, *p_netpool_info;

/**
 * Report how the pool is used (for SYS INFO and NET).
 *
 * @param info filled in
 */
extern void netpool_info(p_netpool_info info);

#endif
