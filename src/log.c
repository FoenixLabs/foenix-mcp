/*
 * A logging utility
 */

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "interrupt.h"
#include "log.h"
#include "simpleio.h"
#include "syscalls.h"

#include "dev/uart.h"
#include "dev/txt_screen.h"
#include "dev/channel.h"
#include "topbar.h"
#include "vicky3_txt_a_logger.h"

#if MODEL == MODEL_FOENIX_A2560U || MODEL == MODEL_FOENIX_A2560U_PLUS
#include "A2560U/gabe_a2560u.h"
#elif (MODEL == MODEL_FOENIX_A2560K || MODEL == MODEL_FOENIX_GENX || MODEL == MODEL_FOENIX_A2560X)
#include "A2560K/gabe_a2560k.h"
#include "A2560K/vky_chan_a.h"
#include "A2560K/vky_chan_b.h"
#elif MODEL == MODEL_FOENIX_C256U || MODEL == MODEL_FOENIX_C256U_PLUS || MODEL == MODEL_FOENIX_FMX
#include "C256/gabe_c256.h"
#endif

/* Channel to which the logging output should go.
 * See log.h
 */
static short log_channel;


short log_level;
#if DEFAULT_LOG_LEVEL >= 0
char logbuf[LOGBUF_SIZE]; // Should hopefully be long enough ! It's here so we don't require more stack space
#endif
// do_log either points to log_to_uart or log_to_screen.
void (*do_log)(const char* message);

static void log_to_uart(const char* message);
static void log_to_screen(const char* message);

#if MODEL == MODEL_FOENIX_A2560K || MODEL == MODEL_FOENIX_GENX || MODEL == MODEL_FOENIX_A2560X
static void log_to_channel_A_low_level(const char *message);
#endif

static short uart; // UART id to use with the uart_xxx functions
#define UART_COM1 0
#define UART_COM2 1

/* Can use the buzzer as sound clues */
void buzzer_on(void) {
    *(GABE_CTRL_REG) |= BUZZER_CONTROL;
}


void buzzer_off(void) {
    *(GABE_CTRL_REG) &= ~BUZZER_CONTROL;
}


void log_init(void) {
    log_setlevel(DEFAULT_LOG_LEVEL);
    log_channel = LOG_CHANNEL;

    switch (log_channel) {
    case LOG_CHANNEL_COM1:
        do_log = log_to_uart;
        uart = UART_COM1;
        uart_init(uart);
        break;

#if (MODEL == MODEL_FOENIX_A2560K || MODEL == MODEL_FOENIX_GENX || MODEL == MODEL_FOENIX_A2560X)
    case LOG_CHANNEL_COM2:
        do_log = log_to_uart;
        uart = UART_COM2;
        uart_init(uart);
        break;

    case LOG_CHANNEL_CHANNEL_A_LOW_LEVEL:
        channel_A_logger_init();
        do_log = log_to_channel_A_low_level;
        break;
#endif
    default:
        do_log = log_to_screen;
    }
    INFO("FOENIX DEBUG OUTPUT------------");
}

unsigned short panic_number;        /* The number of the kernel panic */
unsigned long panic_pc;             /* The PC where the issue occurred */
unsigned long panic_address;        /* The address that was accessed (for some exceptions) */

const char * err_messages[] = {
    "OK",
    "general error",
    "bad device number",
    "operation timed out",
    "device could not be initialized",
    "could not read from device",
    "could not write to device",
    "out of bounds",
    "no media",
    "device is write protected",
    "bad channel number",
    "out of handles",
    "bad handle",
    "unknown file type",
    "out of memory",
    "bad binary file",
    "file is not executable",
    "not found",
    "low level disk error",
    "file system assertion failed",
    "device not ready",
    "file not found",
    "directory not found",
    "invalid path name",
    "access denied",
    "prohibited access",
    "invalid object",
    "drive is write protected",
    "invalid drive",
    "volume has no work area",
    "no file system found",
    "creation of file system aborted",
    "file system timeout",
    "file locked",
    "not enough core",
    "too many open files",
    "file system invalid parameter",
    "not supported",
    "bad argument",
    "media changed",
    "memory used by the MCP"
};

/*
 * Return human readable message for an error number
 */
const char * err_message(short err_number) {
    short index = 0 - err_number;

    if (index < 41) {
        return err_messages[index];
    } else {
        return "unknown error";
    }
}

/*
 * Print an error message
 *
 * Inputs:
 * channel = the number of the channel to print to
 * message = a message string to print before the error
 * err_number = the number of the error
 */
void err_print(short channel, const char * message, short err_number) {
    char buffer[160];

    if ((err_number < 0) && (err_number > -41)) {
        sprintf(buffer, "%s: %s\n", message, err_message(err_number));
    } else {
        sprintf(buffer, "%s: #%d\n", message, err_number);
    }

    sys_chan_write(channel, buffer, strlen(buffer));
}

#if !(MODEL == MODEL_FOENIX_A2560K || MODEL == MODEL_FOENIX_GENX || MODEL == MODEL_FOENIX_A2560X)
/*
 * panic_box - the panic screen before 2.1.0: a red screen with a box (machines other than the
 * A2560K / A2560X / GenX); waits for ever
 */
static void panic_box(void) {
    char buffer[160];
    short column = 2;
    short row = 2;
    short address_expected = 0;
    t_rect region;
TRACE("PANIC------------------------------------------");
    /* Shut off all interrupts */
    // TODO: int_disable_all();

    /* Re-initialize the text screen */
    txt_init_screen(0);
    txt_set_border(0, 0, 0);
    region.origin.x = 0;
    region.origin.y = 0;
    region.size.width = 0;
    region.size.height = 0;
    txt_set_region(0, &region);
    txt_set_color(0, 15, 1);
    txt_set_cursor(0, 0, 0, 0);
    txt_clear(0, 2);

    txt_set_xy(0, column, row++);
    txt_print(0, "\xDA\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xBF");

    txt_set_xy(0, column, row++);
    txt_print(0, "\xB3                                          \xB3");

    txt_set_xy(0, column, row++);
    txt_print(0, "\xB3 Oh dear, something has gone wrong...     \xB3");

    txt_set_xy(0, column, row++);
    txt_print(0, "\xB3                                          \xB3");

    txt_set_xy(0, column, row++);
    switch (panic_number) {
        case 2:
            txt_print(0, "\xB3 Bus Error                                \xB3");
            address_expected = 1;
            break;
        case 3:
            txt_print(0, "\xB3 Address Error                            \xB3");
            address_expected = 1;
            break;
        case 4:
            txt_print(0, "\xB3 Illegal Instruction Error                \xB3");
            break;
        case 5:
            txt_print(0, "\xB3 Division by Zero Error                   \xB3");
            break;
        case 6:
            txt_print(0, "\xB3 Range Check Exception                    \xB3");
            break;
        case 7:
            txt_print(0, "\xB3 Overflow Exception                       \xB3");
            break;
        case 8:
            txt_print(0, "\xB3 Privilege Exception                      \xB3");
            break;
        case 24:
            txt_print(0, "\xB3 Spurious Interrupt                       \xB3");
            break;

        default:
            txt_print(0, "\xB3 Unknown Exception                        \xB3");
            break;
    }

    txt_set_xy(0, column, row++);
    txt_print(0, "\xB3                                          \xB3");

    if (address_expected) {
        txt_set_xy(0, column, row++);
        sprintf(buffer, "\xB3 PC: %08X           Address: %08X \xB3", (unsigned int)panic_pc, (unsigned int)panic_address);
        txt_print(0, buffer);
    } else {
        txt_set_xy(0, column, row++);
        sprintf(buffer, "\xB3 PC: %08X                             \xB3", (unsigned int)panic_pc);
        txt_print(0, buffer);
    }

    txt_set_xy(0, column, row++);
    txt_print(0, "\xB3                                          \xB3");

    txt_set_xy(0, column, row++);
    txt_print(0, "\xC0\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xD9");

    /* Wait forever */
    while (1) ;
}

#endif

#if MODEL == MODEL_FOENIX_A2560K || MODEL == MODEL_FOENIX_GENX || MODEL == MODEL_FOENIX_A2560X
/*
 * panic_name - the name of the exception in panic_number
 *
 * Input:  address_expected = set to 1 if panic_address means something for it
 * Output: the name
 */
static const char * panic_name(short * address_expected) {
    *address_expected = 0;
    switch (panic_number) {
        /* Short: the line must fit in the USER part of the top bar (32 columns on 80) */
        case 2:  *address_expected = 1; return "Bus Err";
        case 3:  *address_expected = 1; return "Addr Err";
        case 4:  return "Illegal";
        case 5:  return "Div by 0";
        case 6:  return "CHK";
        case 7:  return "TRAPV";
        case 8:  return "Privilege";
        case 24: return "Spurious";
        default: return "Exception";
    }
}

/*
 * panic_bar - write the error in white on red into the USER part of the top bar of a screen,
 * straight into its text memory
 *
 * The APP part keeps the path of the program that went wrong, the CLOCK part the time. The hint
 * is added only where it fits (wide screens).
 *
 * Input:  text, color = the text and colour memory of the screen; screen = its number;
 *         message = the error; hint = added after it if there is room
 * Output: none
 */
static void panic_bar(volatile unsigned char * text, volatile unsigned char * color, short screen,
                      const char * message, const char * hint) {
    short start, width, i;
    short with_hint;

    topbar_user_columns(screen, &start, &width);
    with_hint = ((short)(strlen(message) + 2 + strlen(hint)) <= width);
    for (i = 0; i < width; i++) {
        char c = ' ';
        if (*message != 0) {
            c = *message++;
        } else if (with_hint && i >= width - (short)strlen(hint)) {
            c = hint[i - (width - (short)strlen(hint))];  /* the hint at the right-hand end */
        }
        text[start + i] = c;
        color[start + i] = (15 << 4) | 1;               /* white on red */
    }
}

/*
 * Display a panic: an exception the MCP cannot handle (bus error, illegal instruction, ...).
 *
 * 2.1.0: the screen is left as it is, so the last text of the program that went wrong stays
 * visible; the error is shown in white on red in the USER part of the top bar (row 0 belongs to
 * the MCP) of both screens, so the APP part still shows the program's path; also sent to the log
 * (COM1). After a key the start-up code goes back to the command
 * line (call_panic, restart_cli). If the PC lies in the MCP itself, the MCP may be broken: then the
 * bar says a reset is safer.
 */
void panic(void) {
    char message[120];
    short address_expected;
    const char * name = panic_name(&address_expected);
    short in_mcp = (panic_pc >= 0xFFC00000);

    const char * hint = in_mcp ? "MCP crashed: reset is safer" : "key: back to the MCP";

    /* At most 32 characters: "<!> Addr Err PC FFCCA424 @003DFCCA". In the MCP itself the PC
     * starts with FF, which also tells it. */
    if (address_expected) {
        sprintf(message, "\x13 %s PC %08lX @%08lX", name, panic_pc, panic_address);
    } else {
        sprintf(message, "\x13 %s PC %08lX", name, panic_pc);
    }

    log_num(LOG_ERROR, "PANIC, exception ", panic_number);
    log_num(LOG_ERROR, "PANIC, PC ", (int)panic_pc);
    if (address_expected) {
        log_num(LOG_ERROR, "PANIC, address ", (int)panic_address);
    }

    /* The text layer must be visible, also over graphics a program may have switched on */
    if (!(*VKY3_B_MCR & VKY3_B_MCR_TEXT)) {
        *VKY3_B_MCR = *VKY3_B_MCR | VKY3_B_MCR_TEXT | VKY3_B_MCR_TXT_OVR;
    }

    panic_bar(VKY3_B_TEXT_MATRIX, VKY3_B_COLOR_MATRIX, 0, message, hint);
    panic_bar(VKY3_A_TEXT_MATRIX, VKY3_A_COLOR_MATRIX, 1, message, hint);

    /* Forget keys that were waiting, then wait for a new one */
    while (chan_status(0) & CDEV_STAT_READABLE) {
        chan_read_b(0);
    }
    chan_read_b(0);
}
#else
void panic(void) {
    panic_box();
}
#endif

/*
 * Set the maximum level of verbosity in logging.
 * To be printed, a message must have a level lower or equal to this level.
 *
 * Input:
 * level = the maximum level of verbosity to log
 */
void log_setlevel(short level) {
    log_level = level;
}

/* See do in the .h file */
void set_log_channel(short channel) {
    if (log_channel != channel) {
        log_channel = channel;
        log_init();
    }
}

static void log_to_uart(const char *message) {
    char *c = (char*)message;
    while (*c) {
        if (*c == '\n')
            uart_put(uart,'\r');
        uart_put(uart, *c++);
    }
    uart_put(uart,'\r');
    uart_put(uart,'\n');
}

static void log_to_screen(const char *message) {
    txt_print(log_channel, message);
    txt_print(log_channel, "\n");
}

#if (MODEL == MODEL_FOENIX_A2560K || MODEL == MODEL_FOENIX_GENX || MODEL == MODEL_FOENIX_A2560X)
static void log_to_channel_A_low_level(const char *message) {
    channel_A_write(message);
    channel_A_write("\n");
}
#endif

/*
 * Log a message to the console.
 *
 * Inputs:
 * level = the severity of the message... the logging level will filter messages displayed
 * message/args = like printf.
 *
 * Caveat:
 * The total length should not exceed 512 chars.
 */
void logmsg(short level, const char * message, ...) {
    if (level > log_level)
        return;

    va_list args;
    va_start(args, message);
    vsnprintf(logbuf, sizeof(logbuf), message, args);
    va_end(args);

    do_log(logbuf);
}

/*
 * Log a message to the console
 *
 * Inputs:
 * level = the severity of the message... the logging level will filter messages displayed
 * message1 = the first part of the message to log
 * message2 = the second part of the message to log
 */
void log2(short level, const char * message1, const char * message2) {
    if (level <= log_level) {
        char line[160];
        sprintf(line, "%s%s\n", message1, message2);
        logmsg(level, line);
    }
}

/*
 * Log a message to the console
 *
 * Inputs:
 * level = the severity of the message... the logging level will filter messages displayed
 * message1 = the first part of the message to log
 * message2 = the second part of the message to log
 * message3 = the third part of the message to log
 */
void log3(short level, const char * message1, const char * message2, const char * message3) {
    if (level <= log_level) {
        char line[160];
        sprintf(line, "%s%s%s\n", message1, message2, message3);
        logmsg(level, line);
    }
}

/*
 * Log a message with a number
 *
 * Inputs:
 * level = the severity of the message... the logging level will filter messages displayed
 * message1 = the first part of the message to log
 * n = the number to log
 */
void log_num(short level, char * message, int n) {
    char line[160];

    if (level <= log_level) {
        sprintf(line, "%s%08X", message, n);
        logmsg(level, line);
    }
}

/*
 * Send a single character to the debugging channel
 */
void log_c(short level, char c) {
    char line[2];
    line[0] = c;
    line[1] = '\0';
    logmsg(level, line);
}
