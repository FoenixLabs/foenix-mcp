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

/* Flags for mem_sdram_alloc and sys_mem_alloc */
#define MEM_KEEP            0x0001      /* sys_mem_alloc: the block stays after the program ends */
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

/*
 * 2.2.0: the answer of sys_mem_info (the same layout as in the SDK's mcp/types.h). The SDRAM is
 * reported here and not in t_sys_info, so programs built for an older MCP keep working.
 */
typedef struct s_mem_info {
    uint32_t sdram_base;                /* First byte of the SDRAM; 0: this machine has none */
    uint32_t sdram_size;                /* Bytes of SDRAM (0: none) */
    uint32_t sdram_free;                /* Bytes of SDRAM not in a block */
    uint32_t sdram_largest;             /* The largest block sys_mem_alloc can give now */
    uint32_t ram_top;                   /* The top of the system RAM programs may use (sys_mem_get_ramtop) */
    uint16_t blocks;                    /* SDRAM blocks in use */
    uint16_t max_blocks;                /* SDRAM blocks the MCP can keep track of */
} t_mem_info, *p_mem_info;

/*
 * mem_program_start - a program is about to run (proc_run): its SDRAM blocks get a new level
 * Input:  path = the program's path (its file name becomes the name of its blocks)
 * Output: none
 */
extern void mem_program_start(const char * path);

/*
 * mem_program_end - a program has ended (returned, or called sys_exit): free its blocks
 * Input: none   Output: none
 */
extern void mem_program_end(void);

/*
 * mem_program_reset - the command line was restarted after a crash: free the blocks of every
 * program that was running, and of a TEST command (kept and MCP blocks stay)
 * Input: none   Output: none
 */
extern void mem_program_reset(void);

/*
 * mem_sys_alloc - sys_mem_alloc: a block of SDRAM for the running program
 * Input:  size = bytes wanted; flags = MEM_KEEP and / or MEM_CLEAR
 * Output: the block's address (on a page boundary), or 0 if there is no room or no SDRAM
 */
extern uint32_t mem_sys_alloc(uint32_t size, uint16_t flags);

/*
 * mem_sys_free - sys_mem_free: give a block back
 * Input:  address = what sys_mem_alloc returned
 * Output: 0; ERR_NOT_FOUND if no block starts there; ERR_PROTECTED_MEMORY if the block belongs
 *         to the MCP or to another program (a kept block may be freed by anyone)
 */
extern short mem_sys_free(uint32_t address);

/*
 * mem_sys_info - sys_mem_info: how much SDRAM there is and how much is free
 * Input:  info = where to put the answer
 * Output: 0, or ERR_BAD_ARGUMENT if info is 0
 */
extern short mem_sys_info(p_mem_info info);

/*
 * 2.2.0: loading files into SDRAM (dev/fsys.c). A load claims the free SDRAM it writes; it may also
 * write blocks that already belong to its owner (a program loading into its own sys_mem_alloc
 * block), but never another owner's block. Owner: the running program, so the area is freed when it
 * ends; for a LOAD typed at the command line (no program), a kept block named after the file.
 *
 * mem_load_begin - a file load starts
 * Input:  path = the file (its name becomes the name of the blocks)   Output: none
 */
extern void mem_load_begin(const char * path);

/*
 * mem_load_check - may the load write this memory? Claims the free SDRAM pages in it.
 * Input:  address, count = what the loader wants to write
 * Output: 0 (also for memory outside the SDRAM: the caller checks that), or ERR_PROTECTED_MEMORY
 *         if part of it is another owner's block, runs past the SDRAM, or the block table is full
 */
extern short mem_load_check(uint32_t address, uint32_t count);

/*
 * mem_load_end - the file load is over
 * Input:  ok = 1 if the file loaded; 0 frees the SDRAM the load claimed   Output: none
 */
extern void mem_load_end(short ok);

/*
 * mem_free_kept - MEM FREE: give back a kept block
 * Input:  address = the block's first byte
 * Output: 0; ERR_NOT_FOUND if no block starts there; ERR_PROTECTED_MEMORY if it is not a kept block
 */
extern short mem_free_kept(uint32_t address);

#endif
