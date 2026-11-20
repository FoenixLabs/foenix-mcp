/**
 * @file netpool.c
 *
 * 2.1.0: the network pool (see netpool.h).
 *
 * The pool is a row of blocks, one after the other, from the start of the pool to its end. Each
 * block starts with a header of 8 bytes: its size (header included) and a mark that says whether
 * it is free or in use. netpool_alloc takes the first free block that is big enough and splits
 * off what is left; netpool_free marks the block free and joins it with free neighbours, so the
 * pool does not fall apart into small pieces. FELINC has at most a few dozen blocks, so walking
 * the row is fast enough.
 */

#include <string.h>
#include "memory.h"
#include "netpool.h"

#define MARK_FREE   0x4E504652      /* "NPFR" */
#define MARK_USED   0x4E505553      /* "NPUS" */
#define ALIGN       8               /* block sizes are a multiple of this (keeps blocks long-aligned) */
#define MIN_SPLIT   32              /* split a free block only if at least this much is left over */

typedef struct s_block {
    uint32_t size;                  /* size of the block in bytes, this header included */
    uint32_t mark;                  /* MARK_FREE or MARK_USED */
} t_block;

static uint8_t * pool_start = 0;    /* first byte of the pool (0 = no pool) */
static uint8_t * pool_end = 0;      /* one past the last byte */
static uint16_t bad_frees = 0;      /* see netpool_free */

/*
 * netpool_init - take the pool from the top of system RAM
 *
 * Input:  none
 * Output: none (the pool is one free block)
 */
void netpool_init(void) {
    t_block * first;

    pool_start = (uint8_t *)mem_reserve(NETPOOL_SIZE);
    pool_end = pool_start + NETPOOL_SIZE;

    first = (t_block *)pool_start;
    first->size = NETPOOL_SIZE;
    first->mark = MARK_FREE;
}

/*
 * next_block - the block after a block
 *
 * Input:  b = a block
 * Output: the next block, or 0 at the end of the pool
 */
static t_block * next_block(t_block * b) {
    uint8_t * next = (uint8_t *)b + b->size;
    if (b->size < sizeof(t_block)) {
        return 0;                   /* a damaged header: stop instead of walking forever */
    }
    return (next < pool_end) ? (t_block *)next : 0;
}

/*
 * netpool_alloc - get a block from the pool (first fit)
 *
 * Input:  size = how many bytes
 * Output: the block, or 0 if no free block is big enough
 */
void * netpool_alloc(size_t size) {
    t_block * b;
    uint32_t need;

    if (pool_start == 0 || size == 0 || size > NETPOOL_SIZE) {
        return 0;
    }

    /* The header plus the bytes asked for, rounded up so the next block stays aligned */
    need = (sizeof(t_block) + size + ALIGN - 1) & ~(uint32_t)(ALIGN - 1);

    for (b = (t_block *)pool_start; b != 0; b = next_block(b)) {
        if (b->mark == MARK_FREE && b->size >= need) {
            if (b->size - need >= MIN_SPLIT) {
                /* Split: the rest becomes a free block of its own */
                t_block * rest = (t_block *)((uint8_t *)b + need);
                rest->size = b->size - need;
                rest->mark = MARK_FREE;
                b->size = need;
            }
            b->mark = MARK_USED;
            return (void *)(b + 1);
        }
    }
    return 0;
}

/*
 * netpool_calloc - get a block filled with zeroes
 *
 * Input:  count = how many items, size = the size of one item
 * Output: the block, or 0 if no free block is big enough
 */
void * netpool_calloc(size_t count, size_t size) {
    void * block;

    if (size != 0 && count > NETPOOL_SIZE / size) {
        return 0;                   /* count * size would be too big (or overflow) */
    }
    block = netpool_alloc(count * size);
    if (block != 0) {
        memset(block, 0, count * size);
    }
    return block;
}

/*
 * netpool_free - give a block back and join it with free neighbours
 *
 * Input:  block = from netpool_alloc / netpool_calloc, or 0
 * Output: none. A pointer that is not a block in use is only counted (bad_frees): freeing it
 *         would break the row of blocks.
 */
void netpool_free(void * block) {
    t_block * b;
    t_block * prev = 0;

    if (block == 0) {
        return;
    }

    /* Find the block by walking the row: this also proves the pointer is a real block */
    for (b = (t_block *)pool_start; b != 0; prev = b, b = next_block(b)) {
        if ((void *)(b + 1) == block) {
            break;
        }
    }
    if (b == 0 || b->mark != MARK_USED) {
        bad_frees++;
        return;
    }

    b->mark = MARK_FREE;

    /* Join with the next block if that is free */
    {
        t_block * next = next_block(b);
        if (next != 0 && next->mark == MARK_FREE) {
            b->size += next->size;
            next->mark = 0;         /* no stale mark inside the joined block */
        }
    }

    /* Join with the previous block if that is free */
    if (prev != 0 && prev->mark == MARK_FREE) {
        prev->size += b->size;
        b->mark = 0;
    }
}

/*
 * netpool_info - report how the pool is used
 *
 * Input:  info = filled in
 * Output: none
 */
void netpool_info(p_netpool_info info) {
    t_block * b;

    memset(info, 0, sizeof(t_netpool_info));
    info->bad_frees = bad_frees;
    if (pool_start == 0) {
        return;
    }

    info->start = (uint32_t)pool_start;
    info->size = NETPOOL_SIZE;
    for (b = (t_block *)pool_start; b != 0; b = next_block(b)) {
        if (b->mark == MARK_USED) {
            info->used += b->size;
            info->blocks_used++;
        } else if (b->size - sizeof(t_block) > info->largest_free) {
            info->largest_free = b->size - sizeof(t_block);
        }
    }
}
