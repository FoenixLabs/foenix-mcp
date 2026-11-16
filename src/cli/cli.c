/**
 * Implementation of the command line interface
 */

#include "log_level.h"
#ifndef DEFAULT_LOG_LEVEL
    #define DEFAULT_LOG_LEVEL LOG_TRACE
#endif


#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "log.h"
#include "types.h"
#include "interrupt.h"
#include "simpleio.h"
#include "syscalls.h"
#include "sys_general.h"
#include "timers.h"
#include "cli/cli.h"
#include "cli/dos_copy.h"
#include "cli/dos_cmds.h"
#include "cli/mem_cmds.h"
#include "cli/settings.h"
#include "cli/sound_cmds.h"
#include "cli/test_cmds.h"
#include "dev/console.h"
#include "dev/ps2.h"
#include "dev/rtc.h"
#include "dev/uart.h"
#include "uart_reg.h"
#include "rtc_reg.h"
#include "utilities.h"
#include "variables.h"
#include "vicky_general.h"
#include "version.h"
#include "cache.h"

#define MAX_HISTORY_DEPTH   5   /* Maximum number of commands we'll record */
#define MAX_COMMAND_SIZE    128 /* Maximum number of characters in a command line */
#define MAX_ARGC            32  /* Maximum number of arguments to a command */

/*
 * CLI special key code definitions
 */

#define CLI_FLAG_CTRL   0x0100  /* Flag indicating CTRL is pressed */
#define CLI_FLAG_SHIFT  0x0200  /* Flag indicating SHIFT is pressed */
#define CLI_FLAG_ALT    0x0400  /* Flag indicating ALT is pressed */
#define CLI_FLAG_OS     0x0800  /* Flag indicating OS key is pressed */
#define CLI_FLAG_FUNC   0x4000  /* Function keys: 0x4001 - 0x400C */
#define CLI_FLAG_SPEC   0x8000  /* Special keys: */
#define CLI_KEY_LEFT    0x8001
#define CLI_KEY_RIGHT   0x8002
#define CLI_KEY_UP      0x8003
#define CLI_KEY_DOWN    0x8004
#define CLI_KEY_DEL     0x8005
#define CLI_KEY_MONITOR 0x8010  /* A2560K Monitor key */
#define CLI_KEY_CTX     0x8011  /* A2560K CTX Switch key */
#define CLI_KEY_HELP    0x8012  /* A2560K Menu/Help key */

// DEBUG: if I uncomment this then I get a crash
#define sys_txt_get_region txt_get_region
#define sys_txt_set_region txt_set_region

//
// Types
//

/*
 * States to interpret ANSI escape codes
 */
typedef enum {
    CLI_ES_BASE = 0,    // Base state
    CLI_ES_ESC,         // "ESC" seen
    CLI_ES_CSI,         // "ESC[" has been seen
    CLI_ES_SEMI         // Semicolon has been seen
} cli_state;

// Structure to hold a record about a command...

typedef struct s_cli_command {
    char *name;
    char *help;
    cli_cmd_handler handler;
} t_cli_command, *p_cli_command;

extern short cmd_sysinfo(short channel, int argc, const char * argv[]);
extern short cmd_cls(short channel, int argc, const char * argv[]);
extern short cmd_showint(short channel, int argc, const char * argv[]);
extern short cmd_getjiffies(short channel, int argc, const char * argv[]);
extern short cmd_fpu(short channel, int argc, const char * argv[]);
extern short cmd_bootsel(short channel, int argc, const char * argv[]);
extern short cmd_save(short channel, int argc, const char * argv[]);
extern short cmd_get_ticks(short channel, int argc, const char * argv[]);
extern short cmd_screen(short channel, int argc, const char * argv[]);
extern short cmd_credits(short channel, int argc, const char * argv[]);

/*
 * Variables
 */

short cli_screen = 0;                               /**< The default screen to use for the REPL of the CLI */
char cli_command_path[MAX_PATH_LEN];                /**< The path to the command processor */

/** The channel to use for interactions */
short g_current_channel = 0;

short g_channels_swapped = 1;

/** Flag to indicate that the current working directory has changed */
short g_cwd_changed = 0;

/** The history of previous commands issued */
char cli_history[MAX_HISTORY_DEPTH][MAX_COMMAND_SIZE];

/** Record of system information */
t_sys_info cli_sys_info;

/** The built-in commands supported */
const t_cli_command g_cli_commands[] = {
    { "?", "? : print this helpful message", cmd_help },
    { "HELP", "HELP : print this helpful message", cmd_help },
    { "BOOTSEL", "BOOTSEL [<block> <512K|1M|2M|4M>] : show what booted / reboot into a flash block (K25 FPGA build 1110+)", cmd_bootsel },
    { "CALL", "CALL <address> : execute code in supervisor mode at <address> ", mem_cmd_call },
    { "CD", "CD <path> : sets the current directory", cmd_cd },
    { "CLS", "CLS : clear the screen", cmd_cls },
    { "COPY", "COPY <src path> <dst path> : Copies files to destination", cmd_copy },
    { "CREDITS", "CREDITS : Print out the credits", cmd_credits },
    { "DASM", "DASM <addr> [<count>] : print a memory disassembly", mem_cmd_dasm },
    { "DEL", "DEL <path> : delete a file or directory", cmd_del },
    { "DIR", "DIR <path> : print directory listing", cmd_dir },
    { "DISKFILL", "DISKFILL <drive #> <sector #> <byte value>", cmd_diskfill },
    { "DISKREAD", "DISKREAD <drive #> <sector #>", cmd_diskread },
    { "DUMP", "DUMP <addr> [<count>] : print a memory dump", mem_cmd_dump},
    { "FPU", "FPU [TEST] : the Floating Math Block at $FEC04000 (K25 FPGA build 1109+)", cmd_fpu },
    { "GETJIFFIES", "GETJIFFIES : print the number of jiffies since bootup", cmd_getjiffies },
    { "GETTICKS", "GETTICKS : print number of ticks since reset", cmd_get_ticks },
    { "LABEL", "LABEL <drive#> <label> : set the label of a drive", cmd_label },
    { "LOAD", "LOAD <path> : load a file into memory", cmd_load },
    { "MKBOOT", "MKBOOT <drive #> -r | -b <boot sector path> | -s <start file path> : make a drive bootable", cmd_mkboot },
    { "MKDIR", "MKDIR <path> : create a directory", cmd_mkdir },
    { "PEEK8", "PEEK8 <addr> : print the byte at the address in memory", mem_cmd_peek8 },
    { "PEEK16", "PEEK16 <addr> : print the 16-bit word at the address in memory", mem_cmd_peek16 },
    { "PEEK32", "PEEK32 <addr> : print the 32-bit long word at the address in memory", mem_cmd_peek32 },
    { "POKE8", "POKE8 <addr> <value> : write the byte value to the address in memory", mem_cmd_poke8 },
    { "POKE16", "POKE16 <addr> <value> : write the 16-bit value to the address in memory", mem_cmd_poke16 },
    { "POKE32", "POKE32 <addr> <value> : write the 32-bit value to the address in memory", mem_cmd_poke32 },
    { "PWD", "PWD : prints the current directory", cmd_pwd },
    { "REN", "REN <old path> <new path> : rename a file or directory", cmd_rename },
    { "SAVE", "SAVE <path> <address> <length> : write memory to a file (e.g. data the debug port put in SDRAM)", cmd_save },
    { "SET", "SET <name> <value> : set the value of a setting (SET HELP lists them)", cli_cmd_set },
    { "GET", "GET <name> : get the value of a setting", cli_cmd_get },
    { "SHOWINT", "SHOWINT : Show information about the interrupt registers", cmd_showint },
    { "SYSINFO", "SYSINFO : prints information about the system", cmd_sysinfo },
    { "TEST", "TEST <feature> : run a test about a feature", cmd_test },
    { "TYPE", "TYPE <path> : print the contents of a text file", cmd_type },
    { 0, 0 }
};

/**
 * Set the path of the command shell
 *
 * @param path the path to the command processor executable (0 or empty string for default)
 */
void cli_command_set(const char * path) {
    short i = 0;
    short result = 0;

    if (path) {
        // Copy the desired path... without any trailing newline
        for (i = 0; i < strlen(path); i++) {
            char c = path[i];
            if ((c == CHAR_NL) || (c == CHAR_CR)) {
                cli_command_path[i] = 0;
                break;
            }
            cli_command_path[i] = c;
        }
        TRACE1("Setting SHELL: %s", cli_command_path);
        result = sys_var_set("SHELL", cli_command_path);
        if (result) {
            ERROR("Unable to set SHELL");
        }
        TRACE("SHELL Set");

    } else {
        // Set to the default CLI
        result = sys_var_set("SHELL", 0);
        if (result) {
            ERROR("Unable to set SHELL");
        }
    }
    TRACE("Leaving cli_command_set");
}

/**
 * Gets the path of the command shell
 *
 * @param path pointer to the buffer to store the path (empty string means default)
 */
void cli_command_get(char * path) {
    // Copy the desired path
    const char * set_path = sys_var_get("SHELL");
    if (set_path) {
        strncpy(path, set_path, MAX_PATH_LEN);
    } else {
        path[0] = 0;
    }
}

/**
 * Set the number of the screen to use for interactions
 *
 * @param screen the number of the text device to use
 */
void cli_txt_screen_set(short screen) {
    if (sys_chan_device(0) != screen) {
        sys_chan_swap(0, 1);
        g_channels_swapped = 1;
    }
}

/**
 * Get the number of the screen used for interactions
 *
 * @return the number of the text device to use
 */
short cli_txt_screen_get() {
    return sys_chan_device(CDEV_CONSOLE); // FIXME that seems to always return rather than e.g. TXT_SCREEN_A2560U
}

//
// List all the commands
//
short cmd_help(short channel, int argc, const char * argv[]) {
    p_cli_command command;

    for (command = (p_cli_command)g_cli_commands; (command != 0) && (command->name != 0); command++) {
        sys_chan_write(channel, command->help, strlen(command->help));
        sys_chan_write(channel, "\n", 2);
    }
    return 0;
}

/*
 * MCP 2.0.20: FPU [TEST] - the Floating Math Block (G1_FPU, $FEC0_4000, K25 FPGA build 1109+; the same registers as the
 * A2560Me FPU). FPU shows the ID and the status; FPU TEST runs known operations (the expected results are stored as bit
 * patterns, the same vectors as the A2560Me MCP) and checks the results bit-exact (SIN / COS within 8 steps of the
 * last bit).
 */
#define FPU_L(o)        (*(volatile unsigned long *)(0xFEC04000UL + (o)))

typedef struct { const char * name; unsigned long ctrl, in0, in1, expect; long tol; } fpu_vec_t;
static const fpu_vec_t fpu_vecs[] = {
    { "3 x 2.5", 0x000UL, 0x40400000UL, 0x40200000UL, 0x40F00000UL, 0 },
    { "1 / 3", 0x200UL, 0x3F800000UL, 0x40400000UL, 0x3EAAAAABUL, 0 },
    { "1.5 + 2.25", 0x400UL, 0x3FC00000UL, 0x40100000UL, 0x40700000UL, 0 },
    { "1.5 - 2.25", 0x410UL, 0x3FC00000UL, 0x40100000UL, 0xBF400000UL, 0 },
    { "sqrt 2", 0x600UL, 0x40000000UL, 0x00000000UL, 0x3FB504F3UL, 0 },
    { "3x4 + 3/4", 0x5C0UL, 0x40400000UL, 0x40800000UL, 0x414C0000UL, 0 },
    { "int 7 x 0.5", 0x008UL, 0x3F000000UL, 0x00000007UL, 0x40600000UL, 0 },
    { "fix 1.25 x 2", 0x001UL, 0x00001400UL, 0x40000000UL, 0x40200000UL, 0 },
    { "sin 0.5", 0x800UL, 0x3F000000UL, 0x00000000UL, 0x3EF57744UL, 8 },
    { "cos 1.0", 0xA00UL, 0x3F800000UL, 0x00000000UL, 0x3F0A5140UL, 8 },
};

short cmd_fpu(short channel, int argc, const char * argv[]) {
    char m[160];
    unsigned long id = FPU_L(0x1C);
    short i, bad = 0;
    if (id != 0x46505531UL) { print(channel, "FPU: needs K25 FPGA build 1109 or later (no FPU at $FEC04000)\n"); return -1; }
    if (argc < 2 || strcicmp(argv[1], "TEST")) {
        unsigned long ctrl = FPU_L(0x00), st = FPU_L(0x04);
        sprintf(m, "FPU at $FEC04000: CTRL $%08lX, STATUS $%08lX (FPU TEST checks it)\n", ctrl, st);
        print(channel, m);
        return 0;
    }
    for (i = 0; i < (short)(sizeof(fpu_vecs) / sizeof(fpu_vecs[0])); i++) {
        const fpu_vec_t * v = &fpu_vecs[i];
        unsigned long res, fix;
        long polls = 0, diff;
        FPU_L(0x00) = v->ctrl; FPU_L(0x08) = v->in0; FPU_L(0x0C) = v->in1;
        while ((FPU_L(0x04) & 1) && polls < 100000) polls++;
        res = FPU_L(0x10); fix = FPU_L(0x14);
        diff = (long)res - (long)v->expect;
        if (diff < 0) diff = -diff;
        {   /* plain int / pointer arguments for sprintf (VBCC) */
            const char * ok = (diff <= v->tol) ? "ok" : "WRONG";
            long fx = (long)fix, w = ((fx < 0) ? -fx : fx) >> 12, fr = ((((fx < 0) ? -fx : fx) & 0xFFF) * 10000L) >> 12;
            const char * sign = (fx < 0) ? "-" : "";
            int np = (int)polls;
            sprintf(m, "  %-13s $%08lX (%s%ld.%04ld)  expected $%08lX  %s  (%d BUSY polls)\n", v->name, res, sign, w, fr,
                    v->expect, ok, np);
            print(channel, m);
            if (diff > v->tol) bad++;
        }
    }
    /* flags: 1 / 0 and sqrt(-4) */
    {
        unsigned long st1, st2;
        FPU_L(0x00) = 0x200UL; FPU_L(0x08) = 0x3F800000UL; FPU_L(0x0C) = 0;
        while (FPU_L(0x04) & 1) ;
        st1 = FPU_L(0x04);
        FPU_L(0x00) = 0x600UL; FPU_L(0x08) = 0xC0800000UL;
        while (FPU_L(0x04) & 1) ;
        st2 = FPU_L(0x04);
        if (!(st1 & 0x8000UL)) { print(channel, "  1 / 0: divide-by-zero flag missing  WRONG\n"); bad++; }
        if (!(st2 & 0x100000UL)) { print(channel, "  sqrt(-4): invalid flag missing  WRONG\n"); bad++; }
    }
    sprintf(m, "FPU TEST: %d wrong -> %s\n", (int)bad, bad ? "FAIL" : "PASS");
    print(channel, m);
    return bad ? -1 : 0;
}

/*
 * MCP 2.0.21: BOOTSEL [<block> <512K|1M|2M|4M>] - multi-boot (K25 FPGA build 1110, Gen1 work area notes\multiboot.md;
 * the same idea and codes as the A2560Me, registers at $FEC0_0030 instead of $FEC0_0040).
 *   BOOTSEL                 : what this boot used (BOOT_INFO $FEC0_0034, base $38, size $3C)
 *   BOOTSEL <block> <size>  : write BOOT_SEL ($FEC0_0030, key $B0): the board resets, block 0's trampoline starts that
 *                             block (a blank or impossible block falls back to block 0). The next reset button,
 *                             power-on or soft reboot goes back to block 0.
 */
#define G1_BOOT_SEL     ((volatile unsigned long *)0xFEC00030)
#define G1_BOOT_INFO    ((volatile unsigned long *)0xFEC00034)
#define G1_BOOT_BASE    ((volatile unsigned long *)0xFEC00038)
#define G1_BOOT_SIZE    ((volatile unsigned long *)0xFEC0003C)
#define G1_FPGA_ID      ((volatile unsigned long *)0xFEC0000C)
short cmd_bootsel(short channel, int argc, const char * argv[]) {
    char buffer[160];
    static const char * names[4] = { "4M", "2M", "1M", "512K" };
    unsigned long info, sel, base, size, build = (*G1_FPGA_ID >> 16) & 0xFFFF;
    short layout, block;
    if (build < 0x1110 || build > 0x9999) {
        sprintf(buffer, "BOOTSEL: needs K25 FPGA build 1110 or later (this is build %04lX)\n", build);
        print(channel, buffer);
        return -1;
    }
    if (argc < 2) {
        info = *G1_BOOT_INFO; sel = *G1_BOOT_SEL; base = *G1_BOOT_BASE; size = *G1_BOOT_SIZE;
        {   /* plain variables for sprintf (VBCC) */
            unsigned long blk = info & 7;
            const char * lay = names[(info >> 4) & 3];
            const char * how = (info & 0x80) ? ", chosen by BOOT_SEL" : ", cold boot";
            const char * fb = (info & 0x40) ? " (the chosen block was blank or does not exist: fell back to block 0)" : "";
            unsigned long kb = size / 1024;
            sprintf(buffer, "Booted from flash block %lu (%s layout) at $%08lX, %lu KB%s%s\n", blk, lay, base, kb, how, fb);
        }
        print(channel, buffer);
        sprintf(buffer, "BOOT_SEL holds: %s\n", (sel & 0x100) ? "a choice not taken yet" : "nothing (block 0 at the next reset)");
        print(channel, buffer);
        return 0;
    }
    block = (short)atoi(argv[1]);
    layout = -1;
    if (argc > 2) {
        for (layout = 0; layout < 4; layout++) {
            const char * a = argv[2], * b = names[layout];
            while (*a && *b && toupper((unsigned char)*a) == *b) { a++; b++; }
            if (!*a && !*b) break;
        }
    }
    if (layout < 0 || layout > 3 || block < 0 || block >= (1 << layout)) {
        print(channel, "BOOTSEL <block> <512K|1M|2M|4M>: blocks 0-7 (512K), 0-3 (1M), 0-1 (2M), 0 (4M)\n");
        return -1;
    }
    base = 0xFFC00000UL + ((unsigned long)block << (22 - layout));
    if (*(volatile unsigned long *)base == 0xFFFFFFFFUL) {
        sprintf(buffer, "BOOTSEL: flash block %d (%s layout, $%08lX) is blank - not rebooting\n", (int)block, names[layout], base);
        print(channel, buffer);
        return -1;
    }
    sprintf(buffer, "Rebooting into flash block %d (%s layout, $%08lX)...\n", (int)block, names[layout], base);
    print(channel, buffer);
    {   /* let the console send the message (COM1 at 115200: ~6 ms) */
        long t0 = timers_jiffies();
        while (timers_jiffies() - t0 < 3) ;
    }
    *G1_BOOT_SEL = 0xB0000000UL | ((unsigned long)layout << 4) | (unsigned long)block;
    for (;;) ;                                          /* the FPGA resets the board */
    return 0;
}

/*
 * MCP 2.0.21: SAVE <path> <address> <length> - write memory to a file (created or replaced); from the A2560Me MCP 168.
 * Numbers in hex with 0x / $, or decimal. E.g. the debug port writes a file's bytes into SDRAM, then
 * SAVE /sd/AUTO/SDTEST.PRG 0x03000000 12345.
 */
short cmd_save(short channel, int argc, const char * argv[]) {
    char buffer[120];
    unsigned long addr, len, done = 0;
    short fd;
    if (argc != 4) { print(channel, "SAVE <path> <address> <length>\n"); return -1; }
    addr = cli_eval_number(argv[2]);
    len = cli_eval_number(argv[3]);
    fd = sys_fsys_open(argv[1], FSYS_WRITE | FSYS_CREATE_ALWAYS);
    if (fd < 0) {
        sprintf(buffer, "SAVE: cannot create %s (%d)\n", argv[1], (int)fd);
        print(channel, buffer);
        return fd;
    }
    while (done < len) {
        short n = (len - done > 16384) ? 16384 : (short)(len - done);
        short w = sys_chan_write(fd, (unsigned char *)(addr + done), n);
        if (w != n) {
            sys_fsys_close(fd);
            {   int wi = w; unsigned long d = done;
                sprintf(buffer, "SAVE: write error after %lu bytes (%d)\n", d, wi); }
            print(channel, buffer);
            return -1;
        }
        done += n;
    }
    sys_fsys_close(fd);
    sprintf(buffer, "SAVE: %lu bytes from $%08lX to %s\n", len, addr, argv[1]);
    print(channel, buffer);
    return 0;
}

short cmd_getjiffies(short channel, int argc, const char * argv[]) {
    char buffer[160];

    sprintf(buffer, "%ld\n", timers_jiffies());
    sys_chan_write(channel, buffer, strlen(buffer));;
    return 0;
}

/*
 * Print the number of ticks since last restart
 */
short cmd_get_ticks(short channel, int argc, const char * argv[]) {
    char buffer[160];

    sprintf(buffer, "%ld\n", rtc_get_jiffies());
    sys_chan_write(channel, buffer, strlen(buffer));
    return 0;
}

/*
 * Clear the screen
 */
short cmd_cls(short channel, int argc, const char * argv[]) {
    const char * ansi_cls = "\x1B[2J\x1B[H";

    sys_chan_write(channel, ansi_cls, strlen(ansi_cls));
    return 0;
}

/*
 * Display information about the system
 */
short cmd_sysinfo(short channel, int argc, const char * argv[]) {
    t_extent text_size, pixel_size;
    char buffer[160];

    sprintf(buffer, "\nSystem information:\nModel: %s\n", cli_sys_info.model_name);
    print(channel, buffer);

    sprintf(buffer, "CPU: %s\n", cli_sys_info.cpu_name);
    print(channel, buffer);

    sprintf(buffer, "Clock (kHz): %lu\n", cli_sys_info.cpu_clock_khz);
    print(channel, buffer);

    sprintf(buffer, "Bus clock (kHz): %lu\n", cli_sys_info.bus_clock_khz);
    print(channel, buffer);

    sprintf(buffer, "System Memory: 0x%lX\n", cli_sys_info.system_ram_size);
    print(channel, buffer);

    sprintf(buffer, "FPGA Model: %08lX\n", cli_sys_info.fpga_model);
    print(channel, buffer);

    sprintf(buffer, "FPGA Version: %04X.%04X\n", cli_sys_info.fpga_version, cli_sys_info.fpga_subver);
    print(channel, buffer);

    sprintf(buffer, "MCP version: v%02u.%02u.%04u%s\n", cli_sys_info.mcp_version, cli_sys_info.mcp_rev, cli_sys_info.mcp_build, VER_SUFFIX);
    print(channel, buffer);

    sprintf(buffer, "CPU cache: %s\n", cache_state_text());   /* 2.0.30: read from the CPU */
    print(channel, buffer);

    short screen = sys_chan_device(channel);

    sys_txt_get_sizes(screen, &text_size, &pixel_size);
    sprintf(buffer, "Screen#%d size: %dx%d characters, %dx%d pixels.\n", screen, text_size.width, text_size.height, pixel_size.width, pixel_size.height);
    print(channel, buffer);

    return 0;
}

/*
 * Show information about the interrupt registers
 */
short cmd_showint(short channel, int argc, const char * argv[]) {
    char buffer[160];

    TRACE("cmd_showint");

    print(channel, "Interrupt control registers...\n\n");
    print(channel, "          GRP0 GRP1 GRP2\n");

    sprintf(buffer, "Polarity: %04x %04x %04x\n", *POL_GRP0, *POL_GRP1, *POL_GRP2);
    print(channel, buffer);

    sprintf(buffer, "    Edge: %04x %04x %04x\n", *EDGE_GRP0, *EDGE_GRP1, *EDGE_GRP2);
    print(channel, buffer);

    sprintf(buffer, "    Mask: %04x %04x %04x\n", *MASK_GRP0, *MASK_GRP1, *MASK_GRP2);
    print(channel, buffer);

    sprintf(buffer, " Pending: %04x %04x %04x\n", *PENDING_GRP0, *PENDING_GRP1, *PENDING_GRP2);
    print(channel, buffer);

    return 0;
}

//
// Attempt to execute a command
//
// Inputs:
//  command = the upper case name of the command (first word of the command line)
//  parameters = the string of parameters to be passed to the command
//
short cli_exec(short channel, char * command, int argc, const char * argv[]) {
    p_cli_command commands = (p_cli_command)g_cli_commands;

    log3(LOG_INFO, "cli_exec: '", argv[0], "'");
    log_num(LOG_INFO, "argc = ", argc);

    while ((commands != 0) && (commands->name != 0)) {
        // Does the command match the name?
        if (strcmp(commands->name, command) == 0) {
            //print(channel,"Executing:");print(channel,commands->name);print(channel,"\r\n");
            // Found it, execute the handler
            return commands->handler(channel, argc, argv);
        } else {
            // No match, keep checking...
            commands++;
        }
    }

    /* No built-in command that matched... try to run a binary file */
    return cmd_run(channel, argc, argv);
}

/**
 * Make sure all the console settings are setup so that the console works correctly
 */
void cli_ensure_console(short channel) {
    // Make sure the console is set up correctly for the CLI
    sys_chan_ioctrl(channel, CON_IOCTRL_ECHO_OFF, 0, 0);
    sys_chan_ioctrl(channel, CON_IOCTRL_ANSI_ON, 0, 0);
    sys_chan_ioctrl(channel, CON_IOCTRL_CURS_ON, 0, 0);

    // Make sure the screen has text enabled
    txt_set_mode(sys_chan_device(channel), TXT_MODE_TEXT);
}

/**
 * Decode ANSI modifier codes
 *
 * @param modifiers the ANSI modifier codes from an escape sequence
 * @return CLI modifier flags
 */
short cli_translate_modifiers(short modifiers) {
    char buffer[10];
    short flags = 0;

    if (modifiers > 0) {
        modifiers--;
    }

    if (modifiers & 0x01) flags |= CLI_FLAG_SHIFT;
    if (modifiers & 0x02) flags |= CLI_FLAG_ALT;
    if (modifiers & 0x04) flags |= CLI_FLAG_CTRL;
    if (modifiers & 0x08) flags |= CLI_FLAG_OS;

    return flags;
}

/**
 * Translate escape sequences that end in a letter code
 *
 * @param modifiers optional parameter
 * @code the letter code of the key
 */
short cli_translate_alpha(short modifiers, char code) {
    short key_code = 0;

    key_code = cli_translate_modifiers(modifiers);

    switch (code) {
        case 'A':
            key_code |= CLI_KEY_UP;
            break;

        case 'B':
            key_code |= CLI_KEY_DOWN;
            break;

        case 'C':
            key_code |= CLI_KEY_RIGHT;
            break;

        case 'D':
            key_code |= CLI_KEY_LEFT;
            break;

        default:
            return 0;
    }

    return key_code;
}

/**
 * Translate escape sequences that end in a numeric code
 *
 * @param modifiers optional parameter
 * @code the numeric code of the key
 */
short cli_translate_numeric(short modifiers, short code) {
    short key_code = 0;

    key_code = cli_translate_modifiers(modifiers);

    if ((code >= 11) && (code <= 15)) {
        // Function keys 1 - 5
        key_code |= CLI_FLAG_FUNC | (code - 10);

    } else if ((code >= 17) && (code < 21)) {   //21 is taken for the PS2 - CTX Switch
        // Function keys 6 - 10
        key_code |= CLI_FLAG_FUNC | (code - 11);

    //} else if ((code == 30) || (code == 20 )) {
    } else if (code == 30)  {        
        // MONITOR key
        key_code |= CLI_KEY_MONITOR;
//    } else if ((code == 31) || (code == 21) ) { //31 is the code from the A2560K Keyboard, 21 is the code from F10 from a PS2 Keyboard
    } else if (code == 31) { //31 is the code from the A2560K Keyboard, 21 is the code from F10 from a PS2 Keyboard
        // CTX SWITCH key
        key_code |= CLI_KEY_CTX;
    //} else if ((code == 32) || (code == 23) ) { //32 code for Key Help, PS2 Keyboard is F11
    } else if (code == 32 ) { //32 code for Key Help, PS2 Keyboard is F11        
        // MENU HELP key
        key_code |= CLI_KEY_HELP;

    } else if (code == 3) {
        // DELETE key
        key_code |= CLI_KEY_DEL;

    } else {
        // Unknown escape code
        key_code = 0;
    }

    return key_code;
}

/**
 * Get a character from the console, processing recognized escape sequences
 *
 * @param channel the number of the input channel
 * @return the 16-bit functional character code
 */
short cli_getchar(short channel) {
    char buffer[10];
    cli_state state = CLI_ES_BASE;      // Current state of the escape sequence
    short number1 = 0, number2 = 0;     // Two numbers that can be embedded in the sequence
    char c;                             // The current character read from the console

    do {
        c = sys_chan_read_b(channel);
        switch (state) {
            case CLI_ES_BASE:
                // We are not processing a sequence...
                if (c == CHAR_ESC) {
                    // Escape has been seen
                    state = CLI_ES_ESC;

                } else {
                    // It's an ordinary character, so return it
                    return c;
                }
                break;

            case CLI_ES_ESC:
                // Escape has been seen... check for CSI
                if (c == '[') {
                    // ESC [ has been seen...
                    state = CLI_ES_CSI;

                } else {
                    // Bad escape sequence...just return the character
                    state = CLI_ES_BASE;
                    return c;
                }
                break;

            case CLI_ES_CSI:
                // ESC [ has been seen... next is either a number, a letter, a semi-colon, or a tilda
                if (isdigit(c)) {
                    // It's a number... shift it onto number1
                    number1 = number1 * 10 + (c - '0');

                } else if (isalpha(c)) {
                    // It's a letter... treat as a code
                    return cli_translate_alpha(number1, c);

                } else if (c == ';') {
                    // Got a semicolon, go to that state
                    state = CLI_ES_SEMI;

                } else if (c == '~') {
                    // Got a tilda... end of numeric code with no parameters
                    return cli_translate_numeric(0, number1);

                } else {
                    // Bad sequence... just return the current character
                    state = CLI_ES_BASE;
                    return c;
                }
                break;

            case CLI_ES_SEMI:
                // Semicolon has been seen... next is either a number or a tilda
                if (isdigit(c)) {
                    // It's a number... shift it onto number1
                    number2 = number2 * 10 + (c - '0');

                } else if (c == '~') {
                    // Got a tilda... end of numeric code with parameters
                    return cli_translate_numeric(number2, number1);

                } else {
                    // Bad sequence... just return the current character
                    state = CLI_ES_BASE;
                    return c;
                }
                break;

            default:
                state = CLI_ES_BASE;
                return c;
        }
    } while (1);
}

char line[256];

/**
 * Print out the credits
 */
short cmd_credits(short channel, int argc, const char * argv[]) {
    short scan_code = 0;

    print(channel, "\x1b[2J\x1b[1;2H");
    print_banner(channel, cli_sys_info.model);

    print(channel, "\n");

    {
        /* 2.0.30: VER_SUFFIX ("c" = caches on) makes the text one longer, so pad it to the box width */
        char ver[24];
        sprintf(ver, "%02d.%02d-alpha+%04d%s", VER_MAJOR, VER_MINOR, VER_BUILD, VER_SUFFIX);
        sprintf(line, "| Version       | %-50s|\n", ver);
    }
    print_box(channel, "{-------------------------------------------------------------------}\n");
    print_box(channel, "| Foenix/MCP - A simple OS for Foenix Retro Systems computers       |\n");
    print_box(channel, ">---------------!---------------------------------------------------<\n");
    print_box(channel, line);
    print_box(channel, ">---------------#---------------------------------------------------<\n");
    print_box(channel, "| License       | BSD-3-Clause                                      |\n");
    print_box(channel, ">---------------#---------------------------------------------------<\n");
    print_box(channel, "| Creators      | Foenix Retro Systems - Stefany Allaire            |\n");
    print_box(channel, "|               >---------------------------------------------------<\n");
    print_box(channel, "|               | Foenix/MCP - Peter Weingartner                    |\n");
    print_box(channel, ">---------------#---------------------------------------------------<\n");
    print_box(channel, "| License       | BSD-3-Clause                                      |\n");
    print_box(channel, ">---------------#---------------------------------------------------<\n");
    print_box(channel, "| Creators      | Foenix Retro Systems - Stefany Allaire            |\n");
    print_box(channel, "|               >---------------------------------------------------<\n");
    print_box(channel, "|               | Foenix/MCP - Peter Weingartner                    |\n");
    print_box(channel, ">---------------#---------------------------------------------------<\n");
    print_box(channel, "| Contributors  | H\x86kan Th\x94rngren, Jesus Garcia, Vincent B.         |\n");
    print_box(channel, ">---------------#---------------------------------------------------<\n");
    print_box(channel, "| Included Code | FatFS - http://elm-chan.org/fsw/ff/00index_e.html |\n");
    print_box(channel, "[---------------@---------------------------------------------------]\n");

    print(channel, "\n\x1b[1mMake the machine yours!\x1b[0m\n");

    print(channel, "\n\x1b[91m//END-OF-LINE\n\x1b[37m");

    do {
        scan_code = sys_kbd_scancode();
    } while (scan_code != 0x01);

    print(channel,"\x1b[2J\x1b[H");
    return 0;
}

/**
 * Read a line of input from the channel, allowing for editing of the line
 *
 * @param channel the input channel to use
 * @param command_line a character buffer in which to store the line
 * @return 0 on success, -1 to swap screens (if applicable), -2 to request command help
 */
short cli_readline(short channel, char * command_line) {
    char buffer[10];
    unsigned short key_code = 0;
    short i = 0, j = 0;
    /* 2.0.31: how many commands back in the history we are. 0 = the new (empty) line;
       n > 0 = cli_history[n - 1] is on the screen (cli_history[0] is the newest command). */
    short history = 0;

    // Make sure key echo is turned off
    sys_chan_ioctrl(channel, 0x04, 0, 0);

    // Zero out the command line buffer
    for (i = 0; i < MAX_COMMAND_SIZE; i++) {
        command_line[i] = 0;
    }

    i = 0;

    do {
        key_code = cli_getchar(channel);

        if ((key_code & 0xF000) == 0) {
            // Ordinary key...
            char c = (char)(key_code & 0x00ff);
            if (c < 0x20) {
                // Control key...
                if (c == CHAR_CR) {
                    // Newline... we're done
                    // TODO: add the line to the command history
                    return 0;

                } else if (c == CHAR_ESC) {
                    // ESC pressed... clear whole line

                    // Zero out the command line buffer
                    for (i = 0; i < MAX_COMMAND_SIZE; i++) {
                        command_line[i] = 0;
                    }
                    i = 0;
                    print(channel, "\x1b[3G\x1b[K");

                } else if (c == CHAR_BS) {
                    // Backspace
                    if (i > 0) {
                        i--;
                        print(channel, "\x1b[D");
                        for (j = i; j < strlen(command_line); j++) {
                            command_line[j] = command_line[j+1];
                        }
                        print(channel, "\x1b[1P");
                    }
                }

            } else {
                // Add key to command line
                command_line[i++] = c;
                sys_chan_write_b(channel, c);
            }

        } else {
            // Special editing key
            switch (key_code & 0xF0FF) {
                case CLI_KEY_LEFT:
                    // Move cursor to the left
                    if (key_code & CLI_FLAG_CTRL) {
                        i = 0;
                        print(channel, "\x1b[3G");
                    } else {
                        if (i > 0) {
                            i--;
                            print(channel, "\x1b[D");
                        }
                    }
                    break;

                case CLI_KEY_RIGHT:
                    // Move cursor right
                    if (key_code & CLI_FLAG_CTRL) {
                        sprintf(buffer, "\x1b[%dG", (short)(3 + strlen(command_line)));
                        print(channel, buffer);
                        i = strlen(command_line);
                    } else {
                        if (command_line[i] != 0) {
                            i++;
                            print(channel, "\x1b[C");
                        }
                    }
                    break;

                case CLI_KEY_UP:
                    // Go back one command in history (the next older one)
                    if ((history < MAX_HISTORY_DEPTH) && (cli_history[history][0] != 0)) {
                        strcpy(command_line, cli_history[history]);
                        history++;
                        i = strlen(command_line);
                        print(channel, "\x1b[3G\x1b[K");
                        print(channel, command_line);
                    }
                    break;

                case CLI_KEY_DOWN:
                    // Go forward one command in history (the next newer one)
                    // 2.0.31: this showed the command that was already on the screen (the counter went
                    // down first, then the same entry was shown again), so Down was one press late and
                    // never got back to an empty line.
                    if (history > 1) {
                        history--;
                        strcpy(command_line, cli_history[history - 1]);
                        i = strlen(command_line);
                        print(channel, "\x1b[3G\x1b[K");
                        print(channel, command_line);

                    } else if (history == 1) {
                        // Past the newest command: back to an empty line, as before the first Up
                        history = 0;
                        for (i = 0; i < MAX_COMMAND_SIZE; i++) {
                            command_line[i] = 0;
                        }
                        i = 0;
                        print(channel, "\x1b[3G\x1b[K");
                    }
                    break;

                case CLI_KEY_DEL:
                    // Delete the character under the cursor
                    for (j = i; j < strlen(command_line); j++) {
                        command_line[j] = command_line[j+1];
                    }
                    print(channel, "\x1b[1P");
                    break;

                case CLI_KEY_CTX:
                    // Switch the console screens (if applicable)
                    return -1;

                case CLI_KEY_HELP:
                    // Request the help screen
                    if (key_code & CLI_FLAG_OS) {
                        return -3;
                    } else {
                        return -2;
                    }

                default:
                    // Unknown... do nothing
                    break;
            }
        }
    } while (1);

    return 0;
}

/**
 * Parse and attempt to execute a command line
 *
 * @param command_line the command line to process
 * @return the result of running the command line
 */
short cli_process_line(short channel, char * command_line) {
    char * arg;
    char * token_save;
    char * delim = " ";
    int argc = 0;
    char * argv[MAX_ARGC];

    for (argc = 0, token_save = command_line; argc < MAX_ARGC; argc++) {
        arg = strtok_r(command_line, delim, &token_save);
        if (arg != 0) {
            argv[argc] = arg;
        } else {
            break;
        }
    }

    if (argc > 0) {
        int i;
        for (i = 0; i < strlen(argv[0]); i++) {
            argv[0][i] = toupper(argv[0][i]);
        }

        // Try to execute the command
        return cli_exec(channel, argv[0], argc, argv);
    }
}

void cli_draw_window(short channel, const char * status, short is_active) {
    const char * title_header = "Foenix/MCP - ";
    unsigned char foreground, background;
    char buffer[256];
    t_point cursor;
    t_rect region, old_region, full_region;
    short i = 0, j;

    short dev = sys_chan_device(channel);
    if (dev == ERR_BADCHANNEL) {
        ERROR1("cli_draw_window on bad channel %d", channel);
        return;
    }

    // Save the current region and cursor location
    sys_txt_get_xy(dev, &cursor);
    sys_txt_get_region(dev, &old_region);
    sys_txt_get_color(dev, &foreground, &background);

    // Return to the full region and get its dimensions
    region.origin.x = 0;
    region.origin.y = 0;
    region.size.width = 0;
    region.size.height = 0;
    sys_txt_set_region(dev, &region);
    sys_txt_get_region(dev, &full_region);

    // Display the titlebar
    i = 0;
    sys_txt_set_xy(dev, 0, 0);
    if (channel == 0) {
        sys_txt_set_color(dev, background, foreground);
    }
    for (j = 0; j < strlen(title_header); j++) {
        buffer[i++] = title_header[j];
    }
    for (j = 0; j < strlen(status); j++) {
        buffer[i++] = status[j];
    }
    while (i < full_region.size.width) {
        if (is_active) {
            buffer[i++] = ' ';
        } else {
            buffer[i++] = 0xB0;
        }
    }
    buffer[i++] = 0;
    print(channel, buffer);

    // Restore the region and cursor location
    sys_txt_set_color(dev, foreground, background);
    sys_txt_set_region(dev, &old_region);
    sys_txt_set_xy(dev, cursor.x, cursor.y);

    // Set cursor visibility based on if the screen is active
    for (i = 0; i < cli_sys_info.screens; i++) {
        sys_chan_ioctrl(i, i == channel ? CON_IOCTRL_CURS_ON : CON_IOCTRL_CURS_OFF, 0, 0);
    }
}

/**
 * Initialize the text screen (set up regions, windows, etc.)
 */
void cli_setup_screen(short channel, const char * path, short is_active) {
    t_rect full_region, command_region;
    char message[160];

    short dev = sys_chan_device(channel);

    // Get the size of the screen
    full_region.origin.x = 0;
    full_region.origin.y = 0;
    full_region.size.width = 0;
    full_region.size.height = 0;
    sys_txt_set_region(dev, &full_region);
    sys_txt_get_region(dev, &full_region);

    // Clear the screen
    print(channel, "\x1b[2J\x1b[H");

    // Figure out the size of the command box and its region
    command_region.origin.x = 0;
    command_region.origin.y = 1;
    command_region.size.width = full_region.size.width;             // Comes back with 158 (160 - 2(border))
    command_region.size.height = full_region.size.height - 1;       // Comes back with 58 (60 - 2(8pixels Border))

    //DEBUG2("command_region.size.width:%d command_region.size.height:%d", command_region.size.width, command_region.size.height);

    // Restrict the region to the command panel
    //INFO(" ENTERIN   sys_txt_set_region(dev, &command_region);<<<<<<<<<<<<<<<<");
    sys_txt_set_region(dev, &command_region);
    //INFO(" EXITING   sys_txt_set_region(dev, &command_region);<<<<<<<<<<<<<<<<");

    // Draw the window
    //INFO(" ENTERIN   cli_draw_window(channel, path, is_active);<<<<<<<<<<<<<<<<");
    cli_draw_window(channel, path, is_active);
    //INFO(" EXITING   cli_draw_window(channel, path, is_active);<<<<<<<<<<<<<<<<");

    print(channel, "\x1b[2J\x1b[1;2H");
    //INFO(" ENTERIN   print_banner(channel, cli_sys_info.model);<<<<<<<<<<<<<<<<");
    print_banner(channel, cli_sys_info.model);
    //INFO(" EXITING   print_banner(channel, cli_sys_info.model);<<<<<<<<<<<<<<<<");


    sprintf(message, "\nFoenix/MCP v%02u.%02u.%04u%s\n\n", (unsigned int)cli_sys_info.mcp_version, (unsigned int)cli_sys_info.mcp_rev, (unsigned int)cli_sys_info.mcp_build, VER_SUFFIX);
    print(channel, message);   
    print(channel, "Type HELP or ? for help.\n");
}

//
// Enter the CLI's read-eval-print loop
//
short cli_repl(short channel) {
    char command_line[MAX_COMMAND_SIZE];
    char cwd_buffer[MAX_PATH_LEN];
    short result = 0;
    short i = 0;
    t_point cursor;
    short old_channel;

    old_channel = channel;
    g_channels_swapped = 1;

    g_cwd_changed = 1;
    cursor.x = 0;
    cursor.y = 0;

    while (1) {
        // Refresh window if the current working directory has changed
        if (g_cwd_changed || g_channels_swapped) {
            g_cwd_changed = 0;

            // Get and display the new working directory
            if (sys_fsys_get_cwd(cwd_buffer, MAX_PATH_LEN) == 0) {
                // char message[160];
                // sprintf(message, "%d", strlen(cwd_buffer));
                print(CDEV_CONSOLE, "");
#if MODEL == MODEL_FOENIX_A2560K || MODEL == MODEL_FOENIX_GENX || MODEL == MODEL_FOENIX_A2560X
                if (g_channels_swapped) {
                    // If channel has changed, deactivate old channel
                    cli_draw_window(CDEV_EVID, cwd_buffer, 0);
                    old_channel = g_current_channel;
                    g_channels_swapped = 0;
                }
#endif
                cli_draw_window(CDEV_CONSOLE, cwd_buffer, 1);
            }
        }

        sys_chan_write(g_current_channel, "\x10 ", 2);                           // Print our prompt
        result = cli_readline(g_current_channel, command_line);
        switch (result) {
            case -1:
                // g_current_channel = (g_current_channel == 0) ? 1 : 0;
                sys_chan_swap(0, 1);
                g_channels_swapped = 1;
                break;

            case -2:
                print(g_current_channel, "\n");
                cmd_help(g_current_channel, 0, 0);
                break;

            case -3:
                // Print the credits
                print(g_current_channel, "\n");
                cmd_credits(g_current_channel, 0, 0);
                break;

            default:
                // Otherwise, good command... lets add it to the history
                // 2.0.31: but not an empty line (it pushed a real command out, and Up stops at an
                // empty entry, so after just pressing Enter the history seemed gone), and not the
                // same command as the one just before it.
                if ((command_line[0] != 0) && (strcmp(command_line, cli_history[0]) != 0)) {
                    for (i = MAX_HISTORY_DEPTH - 1; i > 0; i--) {
                        // Copy previous commands down
                        strcpy(cli_history[i], cli_history[i-1]);
                    }
                    strcpy(cli_history[0], command_line);
                }
                break;
        }

        print(g_current_channel, "\n");
        cli_process_line(g_current_channel, command_line);

        print(g_current_channel, "\n");
        sys_txt_get_xy(sys_chan_device(g_current_channel), &cursor);
    }

    return 0;
}

/* 2.0.33: set once cli_prepare_screen() has run, so cli_start_repl() does not do it twice */
static short cli_screen_prepared = 0;

/**
 * Set up the CLI's console and screen(s): title bar, banner, start-up folder
 *
 * 2.0.33. Split off from cli_start_repl(), so the boot code can call it BEFORE it runs the
 * start-up file (system/mcp.init). Why: a program started from mcp.init does not return to the
 * boot code when it ends; the MCP restarts the command line with cli_rerepl() instead, which only
 * shows a prompt. The screen set-up in cli_start_repl() was then skipped: no banner, and the text
 * kept the colour the screen got at start-up (white instead of light gray). Done first, the screen
 * is ready whatever mcp.init does, and its messages appear in the normal window. Setting the
 * folder first also means a CD in mcp.init now stays in effect.
 *
 * @param channel the channel to use for interactions
 * @param init_cwd the initial current working directory (0 = leave it)
 */
void cli_prepare_screen(short channel, const char * init_cwd) {
    short result = 0;
    short i = 0;

    g_current_channel = channel;

    // Make sure we can see text properly on the channel
    cli_ensure_console(g_current_channel);

    if (init_cwd != 0) {
        result = sys_fsys_set_cwd(init_cwd);
        if (result) {
            char message[160];
            sprintf(message, "Unable to set startup directory: %s\n", sys_err_message(result));
            print(g_current_channel, message);
        }
    }

    // Set up the screen(s)
    cli_setup_screen(channel, init_cwd, 1);             // Initialize our main main screen
    if (cli_sys_info.screens > 1) {
        for (i = 0; i < cli_sys_info.screens; i++) {    // Set up each screen we aren't using
            if (i != channel) {
                cli_setup_screen(i, init_cwd, 0);
            }
        }
    }

    cli_screen_prepared = 1;
}

/**
 * Start the read-eval-print loop
 *
 * Sets up the screen first, unless the boot code already did that with cli_prepare_screen().
 *
 * @param channel the channel to use for interactions
 * @param init_cwd the initial current working directory
 */
short cli_start_repl(short channel, const char * init_cwd) {
    short result = 0;

    // Start up the command shell
    cli_command_get(cli_command_path);
    if (cli_command_path[0] != 0) {
        // Over-ride path provided, boot it
        char * argv[1] = { cli_command_path };

        if (!cli_screen_prepared) {
            // As before 2.0.33: only the console and folder, the other shell draws its own screen
            g_current_channel = channel;
            cli_ensure_console(g_current_channel);
            if (init_cwd != 0) {
                sys_fsys_set_cwd(init_cwd);
            }
        }

        result = sys_proc_run(cli_command_path, 1, argv);
        if (result) {
            print(0, "Unable to start ");
            print(0, cli_command_path);
            print(0, ": ");
            print(0, sys_err_message(result));
            while (1) ;
        }
        return 0;
    } else {
        if (!cli_screen_prepared) {
            cli_prepare_screen(channel, init_cwd);
        }
        return cli_repl(channel);
    }
}

/**
 * Reactivate the CLI's read-eval-print loop after a command has completed
 */
void cli_rerepl() {
    // Start up the command shell
    cli_command_get(cli_command_path);
    if (cli_command_path[0] != 0) {
        char * argv[1] = {cli_command_path};

        // Over-ride path provided, boot it
        short result = sys_proc_run(cli_command_path, 1, argv);
        if (result) {
            print(0, "Unable to start ");
            print(0, cli_command_path);
            print(0, ": ");
            print(0, sys_err_message(result));
            while (1) ;
        }

    } else {
        while (1) {
            // Make sure we can see text properly on the channel
            cli_ensure_console(g_current_channel);

            print(g_current_channel, "\n\n");
            cli_repl(g_current_channel);
        }
    }
}

/**
 * Execute a batch file at the given path
 *
 * @param channel the number of the channel to write any messages to
 * @param path the path to the configuration file to load
 * @return 0 on success, any other number is an error
 */
short cli_exec_batch(short channel, const char * path) {
    char command_line[MAX_COMMAND_SIZE];

    short fd = sys_fsys_open(path, 0x01);   // Open for reading...
    if (fd > 0) {
        // Got a file...

        // Read a line from the file
        short result = 0;

        do {
            result = sys_chan_readline(fd, command_line, MAX_COMMAND_SIZE);
            if (result > 0) {
                // We got a line, so parse it
                cli_process_line(channel, command_line);
            }
        } while (result > 0);   // Until we don't get a line

        // Close the file
        sys_fsys_close(fd);

        return 0;
    } else {
        return fd;
    }
}

long cli_eval_dec(const char * arg) {
    long n = 0;

    TRACE("cli_eval_dec");

    while (*arg) {
        if (*arg == ':') {
            arg++;
            continue;
        } else if (isdigit(*arg)) {
            n *= 10;
            n += *arg - '0';
        } else {
            break;
        }

        arg++;
    }

    return n;
}

long cli_eval_hex(const char * arg) {
    long n = 0;

    TRACE("cli_eval_hex");

    while (*arg) {
        if (*arg == ':') {
            arg++;
            continue;
        } else if (isdigit(*arg)) {
            n *= 16;
            n += *arg - '0';
        } else if ((*arg >= 'a') && (*arg <= 'f')) {
            n *= 16;
            n += *arg - 'a' + 10;
        } else if ((*arg >= 'A') && (*arg <= 'F')) {
            n *= 16;
            n += *arg - 'A' + 10;
        } else {
            break;
        }

        arg++;
    }

    return n;
}

long cli_eval_bin(const char * arg) {
    long n = 0;

    TRACE("cli_eval_bin");

    while (*arg) {
        if (*arg == ':') {
            arg++;
            continue;
        } else if ((*arg == '0') || (*arg == '1')) {
            n *= 2;
            n += *arg - '0';
        } else {
            break;
        }

        arg++;
    }

    return n;
}

/*
 * Evaluate an argument to a number
 */
long cli_eval_number(const char * arg) {
    int len = strlen(arg);

    TRACE("cli_eval_number");

    if (len > 2) {
        if (arg[0] == '0') {
            if (arg[1] == 'x') {
                return cli_eval_hex(&arg[2]);
            } else if (arg[1] == 'b') {
                return cli_eval_bin(&arg[2]);
            }
        }
    }

    if (len > 1) {
        if (arg[0] == '$') {
            return cli_eval_hex(&arg[1]);
        } else if (arg[0] == '%') {
            return cli_eval_bin(&arg[1]);
        }
    }

    return cli_eval_dec(arg);
}

/**
 * Indicate that the current working directory has changed
 */
void cli_flag_cwd() {
    g_cwd_changed = 1;
}

//
// Initialize the CLI
//
// Returns:
//  0 on success, negative number on error
//
short cli_init() {
    short i;

    // Clear out the command history
    for (i = 0; i < MAX_HISTORY_DEPTH; i++) {
        cli_history[i][0] = 0;
    }

    // Get the system information we'll use in several places
    sys_get_info(&cli_sys_info);

    cli_set_init();
    return 0;
}
