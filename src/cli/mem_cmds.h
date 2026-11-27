/*
 * Memory commands
 */

#ifndef __MEM_CMDS_H
#define __MEM_CMDS_H

#include "dis68k.h"

/*
 * Command to start execution in supervisor mode at a location in memory:
 *
 * CALL <address>
 */
extern short mem_cmd_call(short channel, int argc, const char * argv[]);

/*
 * Print out the contents of a block of memory
 *
 * DUMP <address> [<count>]
 */
extern short mem_cmd_dump(short channel, int argc, const char * argv[]);

/*
 * Print out the dissassembly of a block of memory
 *
 * DASM <address> [<count>]
 */
extern short mem_cmd_dasm(short channel, int argc, const char * argv[]);

/*
 * Write an 8-bit byte to memory
 *
 * POKE8 <address> <value>
 */
extern short mem_cmd_poke8(short channel, int argc, const char * argv[]);

/*
 * Read an 8-bit byte from memory and display it
 *
 * PEEK8 <address>
 */
extern short mem_cmd_peek8(short channel, int argc, const char * argv[]);

/*
 * Read an 8-bit byte from memory and display it in hex ($ and leading zeros)
 *
 * PEEK8$ <address>
 */
extern short mem_cmd_peek8_hex(short channel, int argc, const char * argv[]);

/*
 * Write an 16-bit word to memory
 *
 * POKE16 <address> <value>
 */
extern short mem_cmd_poke16(short channel, int argc, const char * argv[]);

/*
 * Read an 16-bit word from memory and display it
 *
 * PEEK16 <address>
 */
extern short mem_cmd_peek16(short channel, int argc, const char * argv[]);

/*
 * Read an 16-bit word from memory and display it in hex ($ and leading zeros)
 *
 * PEEK16$ <address>
 */
extern short mem_cmd_peek16_hex(short channel, int argc, const char * argv[]);

/*
 * Write an 32-bit long word to memory
 *
 * POKE32 <address> <value>
 */
extern short mem_cmd_poke32(short channel, int argc, const char * argv[]);

/*
 * Read an 32-bit long word from memory and display it
 *
 * PEEK32 <address>
 */
extern short mem_cmd_peek32(short channel, int argc, const char * argv[]);

/*
 * Read an 32-bit long word from memory and display it in hex ($ and leading zeros)
 *
 * PEEK32$ <address>
 */
extern short mem_cmd_peek32_hex(short channel, int argc, const char * argv[]);

/*
 * Command to start execution in supervisor mode at a location in memory:
 *
 * CALL <address>
 */
extern short mem_cmd_call(short channel, int argc, const char * argv[]);

/*
 * 2.2.0: MEM - print what the system RAM and the SDRAM are used for (every SDRAM block with its
 * owner and name)
 * Input:  channel, argc, argv as for every command   Output: 0
 */
extern short mem_cmd_mem(short channel, int argc, const char * argv[]);

#endif
