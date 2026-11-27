/**
 * @file memory.h
 *
 * Memory managament system: memory in Foenix/MCP is handled very simply.
 * The system will keep track of the top of available system RAM.
 * User programs can do whatever they want with system RAM from $400 (end of 68K vectors area)
 * to the top of system RAM. Memory above top of system RAM is reserved for
 * the kernel and any terminate-stay-resident code the user cares to install.
 *
 * It does not manage video RAM. 2.2.0: it also hands out the SDRAM in blocks
 * (mem_sdram_xxx, see memory.h).
 *
 * A2560K, A2560X and GenX: programs may use $0001_0000 up to the top of RAM;
 * below $0001_0000 are the vectors, the MCP's variables and the program's
 * stack. The program loaders (dev/fsys.c, fsys_check_load) refuse files that
 * would load outside that range in system RAM.
 *
 */

#include <stdint.h>
#include <string.h>
#include "errors.h"
#include "memory.h"
#include "sys_general.h"

uint32_t mem_top_of_ram = 0;

/*
 * Initialize the memory management system
 *
 * @param top_of_ram initial value for the top of system RAM
 */
void mem_init(uint32_t top_of_ram) {
    mem_top_of_ram = top_of_ram;
}

/**
 * Return the top of system RAM... the user program must not use any
 * system memory from this address and above.
 *
 * @return the address of the first byte of reserved system RAM (one above the last byte the user program can use)
 */
uint32_t mem_get_ramtop() {
    return mem_top_of_ram;
}

/**
 * Reserve a block of memory at the top of system RAM.
 *
 * @param bytes the number of bytes to reserve
 * @return address of the first byte of the reserved block
 */
uint32_t mem_reserve(uint32_t bytes) {
    mem_top_of_ram -= bytes;
    return mem_top_of_ram;
}

/*
 * 2.2.0: SDRAM blocks (see memory.h).
 *
 * The SDRAM window of each machine. No fixed SDRAM address may appear anywhere else, so that a
 * machine with its SDRAM somewhere else, or with none (a 68000 machine sees only 16 MB), only
 * needs its own line here. Size 0 means no SDRAM: every allocation returns 0.
 */
#if MODEL == MODEL_FOENIX_A2560K
#define SDRAM_BASE  0x02000000UL        /* 64 MB through MERA, $0200_0000 - $05FF_FFFF */
#define SDRAM_SIZE  0x04000000UL
#else
#define SDRAM_BASE  0UL                 /* No SDRAM known on this machine */
#define SDRAM_SIZE  0UL
#endif

/* The blocks in use, sorted by address, so the free space is simply the gaps between them */
static t_mem_block mem_blocks[MEM_MAX_BLOCKS];
static short mem_block_count = 0;

/*
 * block_end - the first byte after a block
 * Input: block   Output: its end address
 */
static uint32_t block_end(const t_mem_block * block) {
    return block->start + block->pages * MEM_PAGE_SIZE;
}

/*
 * block_insert - put a new block into the table at a position, keeping the address order
 * Input:  index = its place in the table; start, pages, owner, name = the block
 * Output: start, or 0 if the table is full
 */
static uint32_t block_insert(short index, uint32_t start, uint32_t pages, uint8_t owner, const char * name) {
    t_mem_block * block;

    if (mem_block_count >= MEM_MAX_BLOCKS) {
        return 0;
    }

    /* Move the blocks above it up one place (memmove copes with the overlap) */
    memmove(&mem_blocks[index + 1], &mem_blocks[index], (mem_block_count - index) * sizeof(t_mem_block));
    mem_block_count++;

    block = &mem_blocks[index];
    block->start = start;
    block->pages = pages;
    block->owner = owner;
    if (name) {
        strncpy(block->name, name, MEM_NAME_SIZE - 1);
        block->name[MEM_NAME_SIZE - 1] = 0;     /* strncpy leaves it open when the name is too long */
    } else {
        block->name[0] = 0;
    }
    return start;
}

/*
 * block_remove - take a block out of the table
 * Input: index = its place in the table   Output: none
 */
static void block_remove(short index) {
    mem_block_count--;
    memmove(&mem_blocks[index], &mem_blocks[index + 1], (mem_block_count - index) * sizeof(t_mem_block));
}

void mem_sdram_init(void) {
    mem_block_count = 0;
}

uint32_t mem_sdram_alloc(uint32_t size, uint16_t flags, uint8_t owner, const char * name) {
    uint32_t pages, gap_start, address;
    short i;

    /* Checked first, so the rounding below cannot overflow */
    if (size == 0 || size > SDRAM_SIZE) {
        return 0;
    }
    pages = (size + MEM_PAGE_SIZE - 1) / MEM_PAGE_SIZE;

    /* First fit: the first gap below a block, or above the last block, that is large enough */
    gap_start = SDRAM_BASE;
    for (i = 0; i <= mem_block_count; i++) {
        uint32_t gap_end = (i < mem_block_count) ? mem_blocks[i].start : SDRAM_BASE + SDRAM_SIZE;
        if (gap_end - gap_start >= pages * MEM_PAGE_SIZE) {
            address = block_insert(i, gap_start, pages, owner, name);
            if (address && (flags & MEM_CLEAR)) {
                memset((void *)address, 0, pages * MEM_PAGE_SIZE);
            }
            return address;
        }
        if (i < mem_block_count) {
            gap_start = block_end(&mem_blocks[i]);
        }
    }
    return 0;
}

uint32_t mem_sdram_claim(uint32_t address, uint32_t size, uint8_t owner, const char * name) {
    uint32_t start, end;
    short i;

    /* The area must lie inside the SDRAM (checked before rounding, so nothing overflows) */
    if (size == 0 || SDRAM_SIZE == 0 || address < SDRAM_BASE || address - SDRAM_BASE >= SDRAM_SIZE ||
        size > SDRAM_BASE + SDRAM_SIZE - address) {
        return 0;
    }
    start = address & ~(MEM_PAGE_SIZE - 1);
    end = (address + size + MEM_PAGE_SIZE - 1) & ~(MEM_PAGE_SIZE - 1);

    /* Its place in the table: before the first block that starts at or above it */
    for (i = 0; i < mem_block_count && mem_blocks[i].start < start; i++) {
    }

    /* It must not overlap the block below it or the block above it */
    if (i > 0 && block_end(&mem_blocks[i - 1]) > start) {
        return 0;
    }
    if (i < mem_block_count && mem_blocks[i].start < end) {
        return 0;
    }
    return block_insert(i, start, (end - start) / MEM_PAGE_SIZE, owner, name);
}

short mem_sdram_free(uint32_t address) {
    short i;

    for (i = 0; i < mem_block_count; i++) {
        if (mem_blocks[i].start == address) {
            block_remove(i);
            return 0;
        }
    }
    return ERR_NOT_FOUND;
}

short mem_sdram_free_owner(uint8_t owner) {
    short i = 0, freed = 0;

    while (i < mem_block_count) {
        if (mem_blocks[i].owner == owner) {
            block_remove(i);    /* the next block moves into place i: look at i again */
            freed++;
        } else {
            i++;
        }
    }
    return freed;
}

void mem_sdram_get_info(p_mem_sdram_info info) {
    uint32_t gap_start = SDRAM_BASE, used = 0;
    short i;

    info->base = SDRAM_BASE;
    info->size = SDRAM_SIZE;
    info->blocks = mem_block_count;
    info->largest_free = 0;

    for (i = 0; i <= mem_block_count; i++) {
        uint32_t gap_end = (i < mem_block_count) ? mem_blocks[i].start : SDRAM_BASE + SDRAM_SIZE;
        if (gap_end - gap_start > info->largest_free) {
            info->largest_free = gap_end - gap_start;
        }
        if (i < mem_block_count) {
            used += mem_blocks[i].pages * MEM_PAGE_SIZE;
            gap_start = block_end(&mem_blocks[i]);
        }
    }
    info->free = SDRAM_SIZE - used;

    /* A full table cannot take another block, however much room there is */
    if (mem_block_count >= MEM_MAX_BLOCKS) {
        info->largest_free = 0;
    }
}

short mem_sdram_get_block(short index, p_mem_block block) {
    if (index < 0 || index >= mem_block_count) {
        return ERR_NOT_FOUND;
    }
    *block = mem_blocks[index];
    return 0;
}

/*
 * 2.2.0: SDRAM for programs (sys_mem_alloc / sys_mem_free / sys_mem_info).
 *
 * Each running program has a level: 1 for a program started from the command line, 2 for a
 * program that one starts, and so on. Its blocks are owned by its level, so they can all be
 * freed when it ends, however it ends. Calls made by the MCP itself (level 0) get MCP blocks.
 */
#define MEM_NAMED_LEVELS    8           /* Levels whose program name is kept for MEM */

static uint8_t mem_level = 0;
static char mem_level_name[MEM_NAMED_LEVELS + 1][MEM_NAME_SIZE];

void mem_program_start(const char * path) {
    const char * name = path;
    const char * p;

    if (mem_level < MEM_OWNER_LAST_LEVEL) {
        mem_level++;
    }
    if (mem_level <= MEM_NAMED_LEVELS) {
        /* Only the file name: "/sd/bin/hello.pgz" -> "hello.pgz" (cut to 10 characters) */
        for (p = path; p && *p; p++) {
            if (*p == '/' || *p == ':') {
                name = p + 1;
            }
        }
        strncpy(mem_level_name[mem_level], name ? name : "", MEM_NAME_SIZE - 1);
        mem_level_name[mem_level][MEM_NAME_SIZE - 1] = 0;
    }
}

void mem_program_end(void) {
    if (mem_level > 0) {
        mem_sdram_free_owner(mem_level);
        mem_level--;
    }
}

void mem_program_reset(void) {
    short i = 0;

    /* Every block of any program level, and of a TEST command that crashed (no test runs after a
       restart, so its claim would otherwise block that test until the next reset); kept and MCP
       blocks stay */
    while (i < mem_block_count) {
        if ((mem_blocks[i].owner >= 1 && mem_blocks[i].owner <= MEM_OWNER_LAST_LEVEL) ||
            mem_blocks[i].owner == MEM_OWNER_TEST) {
            block_remove(i);
        } else {
            i++;
        }
    }
    mem_level = 0;
}

uint32_t mem_sys_alloc(uint32_t size, uint16_t flags) {
    uint8_t owner;
    const char * name;

    if (mem_level == 0) {
        owner = MEM_OWNER_MCP;
        name = "MCP";
    } else {
        owner = (flags & MEM_KEEP) ? MEM_OWNER_KEPT : mem_level;
        name = (mem_level <= MEM_NAMED_LEVELS) ? mem_level_name[mem_level] : "program";
    }
    return mem_sdram_alloc(size, flags & MEM_CLEAR, owner, name);
}

short mem_sys_free(uint32_t address) {
    short i;

    for (i = 0; i < mem_block_count; i++) {
        if (mem_blocks[i].start == address) {
            uint8_t owner = mem_blocks[i].owner;
            /* The running program's own blocks and kept blocks; at level 0 the MCP's own */
            if (owner == MEM_OWNER_KEPT || (mem_level > 0 && owner == mem_level) ||
                (mem_level == 0 && owner == MEM_OWNER_MCP)) {
                block_remove(i);
                return 0;
            }
            return ERR_PROTECTED_MEMORY;
        }
    }
    return ERR_NOT_FOUND;
}

short mem_sys_info(p_mem_info info) {
    t_mem_sdram_info sdram;

    if (info == 0) {
        return ERR_BAD_ARGUMENT;
    }
    mem_sdram_get_info(&sdram);
    info->sdram_base = sdram.base;
    info->sdram_size = sdram.size;
    info->sdram_free = sdram.free;
    info->sdram_largest = sdram.largest_free;
    info->ram_top = mem_top_of_ram;
    info->blocks = sdram.blocks;
    info->max_blocks = MEM_MAX_BLOCKS;
    return 0;
}

/*
 * 2.2.0: loading files into SDRAM (see memory.h).
 *
 * A loader asks for its memory piece by piece (a segment, or 16 KB at a time), so the pieces of one
 * load that follow each other are merged into one block. The blocks this load made are remembered,
 * so a failed load can give them back.
 */
#define MEM_LOAD_BLOCKS     16          /* Separate blocks one load can make (segments far apart) */

static short mem_load_active = 0;
static uint8_t mem_load_owner;
static char mem_load_name[MEM_NAME_SIZE];
static uint32_t mem_load_made[MEM_LOAD_BLOCKS];   /* start of each block this load made */
static short mem_load_count = 0;

void mem_load_begin(const char * path) {
    const char * name = path;
    const char * p;

    mem_load_active = 1;
    mem_load_count = 0;
    mem_load_owner = (mem_level > 0) ? mem_level : MEM_OWNER_KEPT;
    for (p = path; p && *p; p++) {
        if (*p == '/' || *p == ':') {
            name = p + 1;
        }
    }
    strncpy(mem_load_name, name ? name : "", MEM_NAME_SIZE - 1);
    mem_load_name[MEM_NAME_SIZE - 1] = 0;
}

/*
 * load_made - did this load make the block that starts here?
 * Input: start = a block's first byte   Output: 1 or 0
 */
static short load_made(uint32_t start) {
    short i;

    for (i = 0; i < mem_load_count; i++) {
        if (mem_load_made[i] == start) {
            return 1;
        }
    }
    return 0;
}

/*
 * load_claim_gap - claim one free gap for the load: grow the load's block right below it, or make
 * a new block
 * Input:  index = the table place of the first block above the gap; start, end = the gap (pages)
 * Output: 0, or ERR_PROTECTED_MEMORY (the table, or this load's list, is full)
 */
static short load_claim_gap(short index, uint32_t start, uint32_t end) {
    if (index > 0 && block_end(&mem_blocks[index - 1]) == start && load_made(mem_blocks[index - 1].start)) {
        mem_blocks[index - 1].pages += (end - start) / MEM_PAGE_SIZE;
        return 0;
    }
    if (mem_load_count >= MEM_LOAD_BLOCKS ||
        block_insert(index, start, (end - start) / MEM_PAGE_SIZE, mem_load_owner, mem_load_name) == 0) {
        return ERR_PROTECTED_MEMORY;
    }
    mem_load_made[mem_load_count++] = start;
    return 0;
}

short mem_load_check(uint32_t address, uint32_t count) {
    uint32_t start, end, gap;
    uint8_t owner = mem_load_active ? mem_load_owner : MEM_OWNER_KEPT;
    short i;

    /* Nothing to do outside the SDRAM */
    if (count == 0 || SDRAM_SIZE == 0 || address + count <= SDRAM_BASE || address >= SDRAM_BASE + SDRAM_SIZE) {
        return 0;
    }
    /* Partly outside: the rest would be a gap of the address map */
    if (address < SDRAM_BASE || count > SDRAM_BASE + SDRAM_SIZE - address) {
        return ERR_PROTECTED_MEMORY;
    }
    start = address & ~(MEM_PAGE_SIZE - 1);
    end = (address + count + MEM_PAGE_SIZE - 1) & ~(MEM_PAGE_SIZE - 1);

    /* First: every block in the area must be the load owner's */
    for (i = 0; i < mem_block_count; i++) {
        if (mem_blocks[i].start < end && block_end(&mem_blocks[i]) > start && mem_blocks[i].owner != owner) {
            return ERR_PROTECTED_MEMORY;
        }
    }
    if (!mem_load_active) {
        return 0;               /* only checked: nobody to claim it for */
    }

    /* Then claim the free gaps in it, from low to high */
    gap = start;
    for (i = 0; i <= mem_block_count && gap < end; i++) {
        uint32_t next = (i < mem_block_count) ? mem_blocks[i].start : SDRAM_BASE + SDRAM_SIZE;
        if (i < mem_block_count && block_end(&mem_blocks[i]) <= gap) {
            continue;           /* entirely below what is left of the area */
        }
        if (next > gap) {
            uint32_t gap_end = (next < end) ? next : end;
            short before = mem_block_count;
            if (load_claim_gap(i, gap, gap_end) != 0) {
                return ERR_PROTECTED_MEMORY;
            }
            if (mem_block_count > before) {
                i++;            /* the new block took place i: the block above the gap moved up */
            }
        }
        if (i < mem_block_count) {
            gap = block_end(&mem_blocks[i]);
        }
    }
    return 0;
}

void mem_load_end(short ok) {
    short i;

    if (!ok) {
        for (i = 0; i < mem_load_count; i++) {
            mem_sdram_free(mem_load_made[i]);
        }
    }
    mem_load_active = 0;
    mem_load_count = 0;
}

short mem_free_kept(uint32_t address) {
    short i;

    for (i = 0; i < mem_block_count; i++) {
        if (mem_blocks[i].start == address) {
            if (mem_blocks[i].owner != MEM_OWNER_KEPT) {
                return ERR_PROTECTED_MEMORY;
            }
            block_remove(i);
            return 0;
        }
    }
    return ERR_NOT_FOUND;
}
