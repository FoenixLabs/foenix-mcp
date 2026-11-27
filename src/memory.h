/**
 * @file memory.h
 *
 * Memory mangament system: memory in Foenix/MCP is handled very simply.
 * The system will keep track of the top of available system RAM.
 * User programs can do whatever they want with system RAM from $400 to
 * the top of system RAM. Memory above top of system RAM is reserved for
 * the kernel and any terminate-stay-resident code the user cares to install.
 *
 * NOTE: this code does not manage video RAM or DRAM (on the A2560K)... only
 * system RAM, where code may be executed.
 *
 */

#ifndef __MEMORY_H
#define __MEMORY_H

#include <stdint.h>

/*
 * Initialize the memory management system
 *
 * @param top_of_ram initial value for the top of system RAM
 */
extern void mem_init(uint32_t top_of_ram);

/**
 * Return the top of system RAM... the user program must not use any
 * system memory from this address and above.
 *
 * @return the address of the first byte of reserved system RAM (one above the last byte the user program can use)
 */
extern uint32_t mem_get_ramtop();

/**
 * Reserve a block of memory at the top of system RAM.
 *
 * @param bytes the number of bytes to reserve
 * @return address of the first byte of the reserved block
 */
extern uint32_t mem_reserve(uint32_t bytes);

/*
 * 2.2.0: SDRAM blocks.
 *
 * The SDRAM is handed out in whole pages of MEM_PAGE_SIZE bytes (the 68040 MMU page size, so
 * the MMU can later protect each block). A table in the MCP's RAM lists every block in use
 * with its owner and a short name; the free space is what lies between them. Where the SDRAM
 * is comes from a table for each machine (memory.c): a machine without SDRAM has size 0, and
 * then every allocation simply returns 0.
 */

#define MEM_PAGE_SIZE       4096UL      /* Bytes in one page; blocks start on a page */
#define MEM_MAX_BLOCKS      128         /* Blocks the table can hold */
#define MEM_NAME_SIZE       11          /* A block's name, the final 0 included */

/* Owners of a block (1 - MEM_OWNER_LAST_LEVEL: the program running at that level) */
#define MEM_OWNER_LAST_LEVEL 0xFC       /* Highest program level */
#define MEM_OWNER_TEST      0xFD        /* TEST SDRAM's own blocks */
#define MEM_OWNER_KEPT      0xFE        /* Stays after the program ends (MEM_KEEP) */
#define MEM_OWNER_MCP       0xFF        /* The MCP's own blocks */

/* Flags for mem_sdram_alloc */
#define MEM_CLEAR           0x0002      /* Fill the block with zeroes */

/* One block in use */
typedef struct s_mem_block {
    uint32_t start;                     /* First byte (a page boundary) */
    uint32_t pages;                     /* Size in pages */
    uint8_t owner;                      /* MEM_OWNER_xxx or a program level */
    char name[MEM_NAME_SIZE];           /* Short name for MEM, 0-terminated */
} t_mem_block, *p_mem_block;

/* The state of the SDRAM, for MEM and sys_mem_info */
typedef struct s_mem_sdram_info {
    uint32_t base;                      /* First byte of the SDRAM (0: none) */
    uint32_t size;                      /* Bytes of SDRAM (0: none) */
    uint32_t free;                      /* Bytes not in a block */
    uint32_t largest_free;              /* The largest block that can be allocated now */
    uint16_t blocks;                    /* Blocks in use */
} t_mem_sdram_info, *p_mem_sdram_info;

/*
 * mem_sdram_init - start with all SDRAM free
 * Input: none   Output: none
 */
extern void mem_sdram_init(void);

/*
 * mem_sdram_alloc - take a block of SDRAM: the first free space that is large enough
 * Input:  size = bytes wanted (rounded up to whole pages); flags = 0 or MEM_CLEAR;
 *         owner = MEM_OWNER_xxx or a program level; name = short name (cut at 10 characters), or 0
 * Output: the address of the block, or 0 (no SDRAM, size 0, too little room, or the table is full)
 */
extern uint32_t mem_sdram_alloc(uint32_t size, uint16_t flags, uint8_t owner, const char * name);

/*
 * mem_sdram_claim - take a block of SDRAM at a fixed address (the TEST commands that use fixed areas)
 * Input:  address, size = the area (widened to whole pages); owner, name as for mem_sdram_alloc
 * Output: the address of the block (the area's first page), or 0 if the area is not all SDRAM
 *         or overlaps a block in use
 */
extern uint32_t mem_sdram_claim(uint32_t address, uint32_t size, uint8_t owner, const char * name);

/*
 * mem_sdram_free - give a block back
 * Input:  address = the address mem_sdram_alloc or mem_sdram_claim returned
 * Output: 0, or ERR_NOT_FOUND if no block starts there
 */
extern short mem_sdram_free(uint32_t address);

/*
 * mem_sdram_free_owner - give back every block of one owner (a program that ended, TEST SDRAM)
 * Input:  owner = MEM_OWNER_xxx or a program level
 * Output: the number of blocks freed
 */
extern short mem_sdram_free_owner(uint8_t owner);

/*
 * mem_sdram_get_info - how much SDRAM there is and how much is free
 * Input:  info = where to put the answer
 * Output: none
 */
extern void mem_sdram_get_info(p_mem_sdram_info info);

/*
 * mem_sdram_get_block - one entry of the block table, in address order (for MEM)
 * Input:  index = 0 for the lowest block, 1 for the next, ...; block = where to copy it
 * Output: 0, or ERR_NOT_FOUND after the last block
 */
extern short mem_sdram_get_block(short index, p_mem_block block);

#endif
