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
