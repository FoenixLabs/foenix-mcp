/*
 * Memory commands
 */

#include <stdio.h>
#include "log.h"
#include "cache.h"
#include "cli.h"
#include "simpleio.h"
#include "sys_general.h"
#include "syscalls.h"
#include "types.h"
#include "mem_cmds.h"
#include "memory.h"

/* Pointer to a function taking void and returning void */
typedef void (*p_thunk)();

/*
 * Print out the contents of a block of memory
 *
 * DUMP <address> [<count>]
 */
short mem_cmd_dump(short channel, int argc, const char * argv[]) {
    unsigned char * address = 0;
    long count = 16;
    long i;


    TRACE("mem_cmd_dump");

    if (argc >= 2) {
        address = (unsigned char *)cli_eval_number(argv[1]);

        if (argc > 2) {
            count = cli_eval_number(argv[2]);
        }

        dump_buffer(channel, address, count, 2);

        return  0;
    } else {
        logmsg(LOG_ERROR, "USAGE: DUMP <address> <count>");
        return -1;
    }
}

void test_thunk() {
    logmsg(LOG_ERROR, "CALL is working.");
}

/*
 * Command to start execution in supervisor mode at a location in memory:
 *
 * CALL <address>
 */
short mem_cmd_call(short channel, int argc, const char * argv[]) {
    p_thunk thunk = 0;
    TRACE ("mem_cmd_call");

    thunk = (p_thunk)cli_eval_number(argv[1]);
    if (thunk) {
        /* 2.0.29: the code may have been put there with data writes; make sure the CPU runs that code */
        cache_flush();
        thunk();
    }

    return 0;
}

short mem_cmd_dasm(short channel, int argc, const char * argv[]) {
    unsigned long address = 0;
    long count = 1000;
    long i;

    TRACE("mem_cmd_dasm");

#if defined(__m68k__) || defined(__M68K__)
    if (argc >= 2) {
        address = cli_eval_number(argv[1]);

        if (argc > 2) {
            count = cli_eval_number(argv[2]);
        }
        print(0, "=================\n");
        disasm(address, address + count);
        print(0, "=================\n");

        return  0;
    } else {
        logmsg(LOG_ERROR, "USAGE: DASM <address> <count>");
        return -1;
    }
#else
    logmsg(LOG_ERROR, "DASM only available on m68k machines");
    return -1;
#endif

}

/*
 * Write an 8-bit byte to memory
 *
 * POKE8 <address> <value>
 */
short mem_cmd_poke8(short channel, int argc, const char * argv[]) {
    TRACE("mem_cmd_poke8");

    if (argc == 3) {
        unsigned char * address = (unsigned char *)cli_eval_number(argv[1]);
        unsigned char c = (unsigned char)cli_eval_number(argv[2]);
        *address = c;

        return 0;
    } else {
        logmsg(LOG_ERROR, "USAGE: POKE8 <address> <value>");
        return -1;
    }
}


/*
 * Write an 16-bit word to memory
 *
 * POKE16 <address> <value>
 */
short mem_cmd_poke16(short channel, int argc, const char * argv[]) {
    TRACE("mem_cmd_poke16");

    if (argc == 3) {
        unsigned short * address = (unsigned short *)cli_eval_number(argv[1]);
        unsigned short c = (unsigned short)cli_eval_number(argv[2]);
        *address = c;

        return 0;
    } else {
        logmsg(LOG_ERROR, "USAGE: POKE16 <address> <value>");
        return -1;
    }
}


/*
 * Write an 32-bit long word to memory
 *
 * POKE32 <address> <value>
 */
short mem_cmd_poke32(short channel, int argc, const char * argv[]) {
    TRACE("mem_cmd_poke32");

    if (argc == 3) {
        unsigned long * address = (unsigned long *)cli_eval_number(argv[1]);
        unsigned long c = (unsigned long)cli_eval_number(argv[2]);
        *address = c;

        return 0;
    } else {
        logmsg(LOG_ERROR, "USAGE: POKE32 <address> <value>");
        return -1;
    }
}

/*
 * mem_peek - read one value from memory and print it (the PEEK commands)
 *
 * Input:  channel = where to print; argc / argv = the command line, argv[1] is the address
 *         ($ = hex, plain = decimal, as every number in the CLI); size = 1, 2 or 4 bytes;
 *         hex = 0 prints an unsigned decimal number, 1 prints hex with a $ and leading zeros
 * Output: the command's result: the value read for PEEK8 / PEEK16 (as before), 0 for PEEK32,
 *         -1 for a wrong command line
 *
 * 2.2.0: PEEK printed hex without a $, while a plain number on the command line is decimal,
 * so "POKE32 $3000000 12345678" read back as "00BC614E". Now PEEKn prints decimal, the same
 * way the number is typed, and PEEKn$ prints hex marked with a $.
 */
static short mem_peek(short channel, int argc, const char * argv[], short size, short hex) {
    char text[16];
    unsigned long value;

    if (argc != 2) {
        sprintf(text, "PEEK%d%s", size * 8, hex ? "$" : "");
        print(channel, "USAGE: ");
        print(channel, text);
        print(channel, " <address>\n");
        return -1;
    }

    if (size == 1) {
        value = *(volatile unsigned char *)cli_eval_number(argv[1]);
    } else if (size == 2) {
        value = *(volatile unsigned short *)cli_eval_number(argv[1]);
    } else {
        value = *(volatile unsigned long *)cli_eval_number(argv[1]);
    }

    if (hex) {
        /* Leading zeros show the size: $4E, $614E, $00BC614E */
        sprintf(text, "$%0*lX\n", size * 2, value);
    } else {
        sprintf(text, "%lu\n", value);
    }
    print(channel, text);

    return (size == 4) ? 0 : (short)value;
}

/*
 * PEEK8 <address> - print the byte at the address, in decimal
 * PEEK8$ <address> - the same in hex ($xx)
 * Input:  channel, argc, argv as for every command   Output: the byte, or -1 for a wrong command line
 */
short mem_cmd_peek8(short channel, int argc, const char * argv[])     { return mem_peek(channel, argc, argv, 1, 0); }
short mem_cmd_peek8_hex(short channel, int argc, const char * argv[]) { return mem_peek(channel, argc, argv, 1, 1); }

/*
 * PEEK16 <address> - print the 16-bit word at the address, in decimal
 * PEEK16$ <address> - the same in hex ($xxxx)
 * Input:  channel, argc, argv as for every command   Output: the word, or -1 for a wrong command line
 */
short mem_cmd_peek16(short channel, int argc, const char * argv[])     { return mem_peek(channel, argc, argv, 2, 0); }
short mem_cmd_peek16_hex(short channel, int argc, const char * argv[]) { return mem_peek(channel, argc, argv, 2, 1); }

/*
 * PEEK32 <address> - print the 32-bit long word at the address, in decimal
 * PEEK32$ <address> - the same in hex ($xxxxxxxx)
 * Input:  channel, argc, argv as for every command   Output: 0, or -1 for a wrong command line
 */
short mem_cmd_peek32(short channel, int argc, const char * argv[])     { return mem_peek(channel, argc, argv, 4, 0); }
short mem_cmd_peek32_hex(short channel, int argc, const char * argv[]) { return mem_peek(channel, argc, argv, 4, 1); }

/*
 * 2.2.0: MEM - what the memory is used for: the parts of the system RAM, and every SDRAM block
 * with its owner and name.
 */

#if MODEL == MODEL_FOENIX_A2560K || MODEL == MODEL_FOENIX_GENX || MODEL == MODEL_FOENIX_A2560X
/* From the linker script and the start-up code: where programs start, where the MCP's RAM starts */
extern char __USER_STACK[];
extern char __memory_start[];
#define MEM_HAS_LAYOUT 1
#endif

/*
 * mem_line - print one line of the system RAM layout: "  $start - $end  size KB  what"
 * Input:  channel; start = first byte; end = one past the last byte; what = what is there
 * Output: none (nothing for an empty area)
 */
static void mem_line(short channel, uint32_t start, uint32_t end, const char * what) {
    char line[100];

    if (end > start) {
        sprintf(line, "  $%08lX - $%08lX %7lu KB  %s\n", (unsigned long)start, (unsigned long)(end - 1),
                (unsigned long)((end - start) / 1024), what);
        print(channel, line);
    }
}

/*
 * mem_owner_name - an SDRAM block's owner, for people
 * Input:  owner = MEM_OWNER_xxx or a program level; text = room for 16 characters
 * Output: text
 */
static const char * mem_owner_name(uint8_t owner, char * text) {
    switch (owner) {
        case MEM_OWNER_MCP:     return "MCP";
        case MEM_OWNER_KEPT:    return "kept";
        case MEM_OWNER_TEST:    return "TEST";
        default:
            sprintf(text, "program %u", (unsigned)owner);
            return text;
    }
}

/*
 * MEM - print what the system RAM and the SDRAM are used for
 * Input:  channel, argc, argv as for every command (MEM takes no arguments)
 * Output: 0
 */
short mem_cmd_mem(short channel, int argc, const char * argv[]) {
    char line[100];
    char owner[16];
    t_sys_info info;
    t_mem_sdram_info sdram;
    t_mem_block block;
    uint32_t ramtop = mem_get_ramtop();
    short i;

    TRACE("mem_cmd_mem");

    sys_get_info(&info);
    sprintf(line, "System RAM: %lu KB\n", (unsigned long)(info.system_ram_size / 1024));
    print(channel, line);
#ifdef MEM_HAS_LAYOUT
    mem_line(channel, 0, (uint32_t)__USER_STACK, "vectors, MCP variables, program stack");
    mem_line(channel, (uint32_t)__USER_STACK, ramtop, "free for programs");
    mem_line(channel, ramtop, (uint32_t)__memory_start, "reserved (network pool, sys_mem_reserve)");
    mem_line(channel, (uint32_t)__memory_start, info.system_ram_size, "MCP: data, heap, stack");
#else
    sprintf(line, "  programs may use RAM up to $%08lX\n", (unsigned long)ramtop);
    print(channel, line);
#endif

    mem_sdram_get_info(&sdram);
    if (sdram.size == 0) {
        print(channel, "No SDRAM.\n");
        return 0;
    }
    sprintf(line, "\nSDRAM: %lu KB at $%08lX, %lu KB free (largest %lu KB), %u of %u blocks\n",
            (unsigned long)(sdram.size / 1024), (unsigned long)sdram.base, (unsigned long)(sdram.free / 1024),
            (unsigned long)(sdram.largest_free / 1024), (unsigned)sdram.blocks, (unsigned)MEM_MAX_BLOCKS);
    print(channel, line);
    if (sdram.blocks > 0) {
        print(channel, "  Start - End                 Size  Owner       Name\n");
        for (i = 0; mem_sdram_get_block(i, &block) == 0; i++) {
            sprintf(line, "  $%08lX - $%08lX %7lu KB  %-10s  %s\n", (unsigned long)block.start,
                    (unsigned long)(block.start + block.pages * MEM_PAGE_SIZE - 1),
                    (unsigned long)(block.pages * MEM_PAGE_SIZE / 1024), mem_owner_name(block.owner, owner),
                    block.name);
            print(channel, line);
        }
    }
    return 0;
}

/*
 * 2.2.0: mem_test_claim - a TEST command that uses a fixed SDRAM area takes it first, so it cannot
 * overwrite a block a program or the MCP is using, and nothing can be allocated there while it runs
 *
 * Input:  channel = where to print a refusal; start, size = the area; name = the test's name (shown by MEM)
 * Output: start if the area is now the test's (give it back with mem_sdram_free(start)), or 0: the
 *         machine has no SDRAM there or part of the area is in use (a message says so)
 */
uint32_t mem_test_claim(short channel, uint32_t start, uint32_t size, const char * name) {
    char line[120];

    if (mem_sdram_claim(start, size, MEM_OWNER_TEST, name) == start) {
        return start;
    }
    sprintf(line, "%s needs SDRAM $%08lX - $%08lX, but part of it is in use (see MEM) or not there.\n",
            name, (unsigned long)start, (unsigned long)(start + size - 1));
    print(channel, line);
    return 0;
}
