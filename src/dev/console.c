/**
 * Implementation of the console channel device
 *
 * The console maps to the main screen and keyboard.
 *
 */

#include "log_level.h"
#ifndef LOG_LEVEL
    #define LOG_LEVEL LOG_TRACE
#endif

#include <ctype.h>
#include <string.h>
#include "features.h"
#include "log.h"
#include "types.h"
#include "constants.h"
#include "dev/channel.h"
#include "dev/console.h"
#include "dev/ps2.h"
#include "dev/kbd_mo.h"
#include "dev/txt_screen.h"
#include "simpleio.h"

#define ANSI_BUFFER_SIZE    16
#define MAX_ANSI_ARGS       10

#define CON_CTRL_ANSI       0x80            /* Set to enable ANSI escape processing */
#define CON_CTRL_ECHO       0x40            /* Set to enable echo of input characters */

typedef void (*ansi_handler)(p_channel, short, short[]);

/*
 * Structure to map an ANSI escape sequence pattern to a handler
 */
typedef struct s_ansi_seq {
    char code;
    ansi_handler handler;
} t_ansi_seq, *p_ansi_seq;

/*
 * Structure to track console state
 */
typedef struct s_console_data {
    unsigned char control;              /* Control flags for the console: e.g. process ANSI codes */
    unsigned char ansi_buffer_count;    /* Number of characters in the ANSI BUFFER */
    char ansi_buffer[ANSI_BUFFER_SIZE]; /* Used to keep track of characters in the ANSI escape sequences */
    char key_buffer;                    /* Used to peek at keyboard input */
} t_console_data, *p_console_data;

/*
 * Forwards
 */

extern void ansi_cuu(p_channel chan, short arg_count, short args[]);
extern void ansi_cuf(p_channel chan, short arg_count, short args[]);
extern void ansi_cub(p_channel chan, short arg_count, short args[]);
extern void ansi_cud(p_channel chan, short arg_count, short args[]);
extern void ansi_cup(p_channel chan, short arg_count, short args[]);
extern void ansi_cha(p_channel chan, short arg_count, short args[]);
extern void ansi_ed(p_channel chan, short arg_count, short args[]);
extern void ansi_el(p_channel chan, short arg_count, short args[]);
extern void ansi_ich(p_channel chan, short arg_count, short args[]);
extern void ansi_dch(p_channel chan, short arg_count, short args[]);
extern void ansi_sgr(p_channel chan, short arg_count, short args[]);
static short con_flush(p_channel chan);

/*
 * Console variables and constants
 */

/*
 * ANSI escape sequences
 */
const t_ansi_seq ansi_sequence[] = {
    { '@', ansi_ich },
    { 'A', ansi_cuu },
    { 'B', ansi_cud },
    { 'C', ansi_cuf },
    { 'D', ansi_cub },
    { 'G', ansi_cha },
    { 'J', ansi_ed },
    { 'K', ansi_el },
    { 'P', ansi_dch },
    { 'H', ansi_cup },
    { 'm', ansi_sgr },
    { 0, 0 }
};

/*
 * Return true if a character is an initial character for an ANSI escape code
 *
 * Inputs:
 * c = the character to test
 *
 * Returns:
 * 0 if the character is not an ANSI initial, 1 if it is
 */
short ansi_start_code(char c) {
    switch (c) {
        case '\x1b':
            return 1;

        default:
            return 0;
    }
}

/*
 * Find the escape sequence pattern that matches what we've collected.
 *
 * If one is found, execute it.
 * If one is not found, just dump the ANSI buffer
 */
void ansi_match_pattern(p_channel chan, p_console_data con_data) {
    char c;
    short argc = 0;
    short arg[MAX_ANSI_ARGS];
    short i, j;

    TRACE("ansi_match_pattern");

    /* Clear out the argument list */
    for (i = 0; i < MAX_ANSI_ARGS; i++) {
        arg[i] = 0;
    }

    /* Make sure we have an escape sequence */
    if ((con_data->ansi_buffer[0] != '\x1b') || (con_data->ansi_buffer[1] != '[')) {
        /* If not, dump the buffer and return */
        con_flush(chan);
        return;
    }

    /* Try to assemble the arguments */
    for (i = 2, argc = 0; i < con_data->ansi_buffer_count - 1; i++) {
        c = con_data->ansi_buffer[i];
        if (isdigit(c)) {
            /* If the character is a digit, add it to the current argument */
            arg[argc] = arg[argc] * 10 + (c - '0');

        } else if (c == ';') {
            /* If it's a semi-colon, start the next argument */
            argc++;
        }
    }

    argc++;

    /* Get the last character... should be the code */
    c = con_data->ansi_buffer[con_data->ansi_buffer_count - 1];

    /* Try to find the matching escape code */
    for (i = 0; ansi_sequence[i].code != 0; i++) {
        if (ansi_sequence[i].code == c) {
            ansi_handler handler = ansi_sequence[i].handler;
            if (handler != 0) {
                /* If we found the handler... call it */
                handler(chan, argc, arg);
                return;
            }
        }
    }

    /* We got to the end without a match... dump the buffer */
    con_flush(chan);
}

/*
 * Add a character to the ANSI buffer... and process when an escape sequence is found
 */
void ansi_process_c(p_channel chan, p_console_data con_data, char c) {
    // TRACE("ansi_process_c");

    if (c == '\x1B') {
        /* Start the escape sequence */
        con_data->ansi_buffer[0] = c;
        con_data->ansi_buffer_count = 1;

    } else if (con_data->ansi_buffer_count > 0) {
        /* We're processing an escape sequence... add the character to the buffer */
        con_data->ansi_buffer[con_data->ansi_buffer_count++] = c;

        /* Check to see if we just wrote the trigger */
        if (((c >= '@') && (c <= 'Z')) ||
            ((c >= 'a') && (c <= 'z'))) {
            ansi_match_pattern(chan, con_data);
            con_data->ansi_buffer_count = 0;
        }

    } else {
        /* Not working on a sequence... so just print it */
        txt_put(chan->dev, c);
    }
}

/*
 * ANSI Handler: cursor up
 */
void ansi_cuu(p_channel chan, short arg_count, short args[]) {
    t_point position;
    short delta = 1;

    TRACE("ansi_cuu");

    if (arg_count > 0) {
        delta = args[0];
    }

    if (delta == 0) delta = 1;

    txt_get_xy(chan->dev, &position);
    position.y -= delta;
    txt_set_xy(chan->dev, position.x, position.y);
}

/*
 * ANSI Handler: cursor forward
 */
void ansi_cuf(p_channel chan, short arg_count, short args[]) {
    t_point position;
    short delta = 1;

    TRACE("ansi_cuf");

    if (arg_count > 0) {
        delta = args[0];
    }

    if (delta == 0) delta = 1;

    txt_get_xy(chan->dev, &position);
    position.x += delta;
    txt_set_xy(chan->dev, position.x, position.y);
}

/*
 * ANSI Handler: cursor back
 */
void ansi_cub(p_channel chan, short arg_count, short args[]) {
    t_point position;
    short delta = 1;

    TRACE("ansi_cub");

    if (arg_count > 0) {
        delta = args[0];
    }

    if (delta == 0) delta = 1;

    txt_get_xy(chan->dev, &position);
    position.x -= delta;
    txt_set_xy(chan->dev, position.x, position.y);
}

/*
 * ANSI Handler: cursor down
 */
void ansi_cud(p_channel chan, short arg_count, short args[]) {
    t_point position;
    short delta = 1;

    TRACE("ansi_cud");

    if (arg_count > 0) {
        delta = args[0];
    }

    if (delta == 0) delta = 1;

    txt_get_xy(chan->dev, &position);
    position.y += delta;
    txt_set_xy(chan->dev, position.x, position.y);
}

/*
 * ANSI Handler: cursor position
 */
void ansi_cup(p_channel chan, short arg_count, short args[]) {
    unsigned short x = 1;
    unsigned short y = 1;

    TRACE("ansi_cup");

    if (arg_count > 0) {
        x = args[0];
        if (arg_count > 1) {
            y = args[1];
        }
    }

    if (x == 0) x = 1;
    if (y == 0) y = 1;

    txt_set_xy(chan->dev, x - 1, y - 1);
}

/*
 * ANSI Handler: cursor horizontal absolute
 */
void ansi_cha(p_channel chan, short arg_count, short args[]) {
    t_point position;
    short column = 1;

    TRACE("ansi_cha");

    if (arg_count > 0) {
        column = args[0];
    }

    if (column == 0) column = 1;

    txt_get_xy(chan->dev, &position);
    txt_set_xy(chan->dev, column - 1, position.y);
}

/*
 * ANSI Handler: erase in display
 */
void ansi_ed(p_channel chan, short arg_count, short args[]) {
    unsigned short n = 2;

    TRACE("ansi_ed");

    if (arg_count > 0) {
        n = args[0];
    }

    txt_clear(chan->dev, n);
}

/*
 * ANSI Handler: erase in line
 */
void ansi_el(p_channel chan, short arg_count, short args[]) {
    unsigned short n = 2;

    TRACE("ansi_el");

    if (arg_count > 0) {
        n = args[0];
    }

    txt_clear_line(chan->dev, n);
}

/*
 * ANSI Handler: insert a character
 */
void ansi_ich(p_channel chan, short arg_count, short args[]) {
    unsigned short n = 2;

    TRACE("ansi_ich");

    if (arg_count > 0) {
        n = args[0];
    }

    txt_insert(chan->dev, n);
}

/*
 * ANSI Handler: delete a character
 */
void ansi_dch(p_channel chan, short arg_count, short args[]) {
    unsigned short n = 1;

    TRACE("ansi_dch");

    if (arg_count > 0) {
        n = args[0];
    }

    if (n > 0) {
        txt_delete(chan->dev, n);
    }
}

/*
 * Set Graphics Rendition
 */
void ansi_sgr(p_channel chan, short argc, short args[]) {
    unsigned char foreground = 0, background = 0;
    short i;

    TRACE("ansi_sgr");

    /* Get the current colors */
    txt_get_color(chan->dev, &foreground, &background);

    /* Walk through each argument code... */
    for (i = 0; i < argc; i++) {
        short code = args[i];

        if ((code >= 30) && (code <= 37)) {
            /* Set foreground color */
            foreground = code - 30;

        } else if ((code >= 90) && (code <= 97)) {
            /* Set bright foreground color */
            foreground = code - 82;

        } else if ((code >= 40) && (code <= 47)) {
            /* Set background color */
            background = code - 40;

        } else if ((code >= 100) && (code <= 107)) {
            /* Set bright background color */
            background = code - 92;

        } else if ((code == 0) || (code == 2) || (code == 22)) {
            /* Reset, dim, and normal intensity */
            foreground = foreground & 0x07;
            background = background & 0x07;

        } else if (code == 1) {
            /* Bold intensity */
            foreground = foreground | 0x08;
        }
    }

    /* Set the colors */
    txt_set_color(chan->dev, foreground, background);
}

//
// Initialize the console... nothing needs to happen here
//
short con_init() {
    return 0;
}

/*
 * Open the console device for the given channel
 *
 * Inputs:
 * chan = the channel record for this console device
 * path = unused
 * mode = unused
 *
 * Returns
 * 0 on success, negative number on failure
 */
short con_open(p_channel chan, const uint8_t * path, short mode) {
    int i;
    p_console_data con_data;

    TRACE("con_open");

    /* Initialize the console data for this channel */

    con_data = (p_console_data)&(chan->data);
    con_data->control = CON_CTRL_ANSI | CON_CTRL_ECHO;
    con_data->ansi_buffer_count = 0;
    for (i = 0; i < ANSI_BUFFER_SIZE; i++) {
        con_data->ansi_buffer[i] = 0;
    }
    con_data->key_buffer = 0;

    return 0;
}

/*
 * Flush the output to the console...
 *
 * Really only does something if the console is set to process ANSI escape codes
 *
 */
static short con_flush(p_channel chan) {
    int i;
    p_console_data con_data;

    con_data = (p_console_data)&(chan->data);
    if (con_data->control & CON_CTRL_ANSI) {
        for (i = 0; i < con_data->ansi_buffer_count; i++) {
            txt_put(chan->dev, con_data->ansi_buffer[i]);
            con_data->ansi_buffer[i] = 0;
        }
    }

    con_data->ansi_buffer_count = 0;
    return 0;
}

/*
 * Close the console channel... just flush the data
 *
 * Inputs:
 * chan = the channel record for this console device
 */
short con_close(p_channel chan) {
    con_flush(chan);
    return 0;
}

#if MODEL == MODEL_FOENIX_A2560K
/*
 * Gen1 remote console (2026-11-04, as the A2560Me MCP's COM4 console): everything written to the console also goes
 * out on the SuperIO COM1 (16550 at $FEC0_23F8, 115200 8N1 = divisor 1, the UART clock is 1.8432 MHz), and bytes
 * received on COM1 are read like keys. Polled, no interrupts. A missing cable costs nothing (the UART sends anyway).
 */
#define RC_COM1(r)  (*(volatile unsigned char *)(0xFEC023F8 + (r)))
#define RC_LSR_RX   0x01
#define RC_LSR_THRE 0x20

static short rc_ready = 0;

static void rc_init() {
    RC_COM1(3) = 0x83;              /* LCR: divisor latch */
    RC_COM1(0) = 1;                 /* 115200 */
    RC_COM1(1) = 0;
    RC_COM1(3) = 0x03;              /* 8N1 */
    RC_COM1(2) = 0x07;              /* FIFOs on and cleared */
    RC_COM1(1) = 0x00;              /* no interrupts */
    RC_COM1(4) = 0x03;              /* DTR, RTS */
    rc_ready = 1;
}

char rc_getc() {
    char c;
    if (!rc_ready) rc_init();
    if (!(RC_COM1(5) & RC_LSR_RX)) return 0;
    c = (char)RC_COM1(0);
    if (c == 0x7F) c = CHAR_BS;     /* terminal DEL key = backspace */
    return c;
}

void rc_putc(char c) {
    long n = 20000;
    if (!rc_ready) rc_init();
    if (c == 0x0A) rc_putc(0x0D);   /* LF -> CR LF */
    while (!(RC_COM1(5) & RC_LSR_THRE) && (--n > 0)) ;   /* 16-byte FIFO; a byte every 87 us */
    if (n > 0) RC_COM1(0) = (unsigned char)c;
}
#endif

#if MODEL == MODEL_FOENIX_A2560K
/*
 * 2.1.0: the keyboard the console read its last character from (0 = the built-in keyboard, 1 = PS/2)
 */
static short con_last_kbd = 0;

/**
 * Get a character from the keyboards of the A2560K, without waiting.
 *
 * 2.1.0. The A2560K has its built-in keyboard and a PS/2 port. Before, the console only read the
 * built-in keyboard: the PS/2 driver filled its buffer (ps2_init sets it up at boot), but nobody
 * read it, so a PS/2 keyboard did nothing. Now both are read, then the remote console on COM1.
 *
 * A key such as an arrow or a function key gives several characters (an ESC sequence), which the
 * keyboard driver puts in its buffer all at once. The keyboard read last is asked first, so the
 * rest of a sequence is read before the other keyboard gets a turn: the two never get mixed.
 *
 * @return the character, or 0 if no keyboard has one
 */
static char con_kbd_getc() {
    char c;
    short i;

    for (i = 0; i < 2; i++) {
        short kbd = con_last_kbd ^ i;   /* first the keyboard read last, then the other one */
        c = (kbd == 0) ? kbdmo_getc() : kbd_getc();
        if (c != 0) {
            con_last_kbd = kbd;
            return c;
        }
    }
    return rc_getc();                   /* Gen1 remote console: COM1 */
}
#endif

/*
 * Send a byte to the console screen
 */
short con_write_b(p_channel chan, uint8_t b) {
    p_console_data con_data;

#if MODEL == MODEL_FOENIX_A2560K
    rc_putc((char)b);
#endif

    /* Check to see if we need to process ANSI codes */
    con_data = (p_console_data)&(chan->data);
    if (con_data->control & CON_CTRL_ANSI) {
        /* ANSI codes are to be processed */
        ansi_process_c(chan, con_data, (char)b);

    } else {
        /* Not processing ANSI codes... just pass it to the text driver */
        txt_put(chan->dev, (char)b);
    }
    return 0;
}

/*
 * Attempt to read from the keyboard.
 */
short con_read_b(p_channel chan) {
    p_console_data con_data;

    /* Check to see if we need to process ANSI codes */
    con_data = (p_console_data)&(chan->data);

    char c;
    do {
        if (con_data->key_buffer != 0) {
            c = con_data->key_buffer;
            con_data->key_buffer = 0;

        } else {

#if MODEL == MODEL_FOENIX_A2560K
#ifdef KBD_POLLED
            ps2_mouse_get_packet();
            c = kbdmo_getc_poll();
            if (c == 0) c = rc_getc();      /* Gen1 remote console: COM1 */
#else
            c = con_kbd_getc();             /* 2.1.0: built-in keyboard, PS/2, COM1 */
#endif
#else
 #ifdef KBD_POLLED
           c = kbd_getc_poll();
 #else
            c = kbd_getc();
 #endif
#endif
        }

    } while (c == 0);

    if ((con_data->control & CON_CTRL_ECHO) != 0) {
        // Echo the character to the screen
        con_write_b(chan, c);
    }

    return (short)(c & 0x00ff);
}


//
// Attempt to read a buffer's worth of bytes from the keyboard
//
short con_read(p_channel chan, uint8_t * buffer, short size) {
    int i;

    for (i = 0; i < size; i++) {
        short c = con_read_b(chan);
        if (c < 0) {
            return c;
        } else if (c > 0) {
            buffer[i] = (uint8_t)(c & 0xff);
        }
    }

    return i;
}

//
// Attempt to read a line of text from the keyboard (stops at buffer size or newline)
//
// This routine also allows for some basic line editing
//
short con_readline(p_channel chan, uint8_t * buffer, short size) {
    int i = 0;

    while (i < size - 1) {
        short c = con_read_b(chan);
        if (c < 0) {
            // Return the error, if we got one
            return c;

        } else if (c > 0) {
            c = c & 0xff;

            buffer[i] = 0;          // By default, we'll have this as the end of string sentinel

            switch (c) {
                case CHAR_NL:
                    // Newline character, end the string and return the size of the string
                    buffer[i] = 0;
                    return i;

                case CHAR_CR:
                    // Newline character, end the string and return the size of the string
                    buffer[i] = 0;
                    return i;

                case CHAR_BS:
                    // Backspace character, delete the character to the left
                    if (i > 0) {
                        buffer[--i] = 0;
                    }
                    break;

                default:
                    // Ordinary character, add it to the buffer
                    buffer[i++] = (char)c;
                    buffer[i] = 0;
                    break;
            }
        }
    }

    return i;
}

//
// Write a string of bytes to the console.
//
// Terminates writing bytes at a null.
//
// Inputs:
//  buffer = the string of bytes
//  size = the number of bytes to write
//
short con_write(p_channel chan, const uint8_t * buffer, short size) {
    int i;

    TRACE("con_write");

    for (i = 0; i < size; i++) {
        char c = (char)buffer[i];
        if (c == 0) {
            break;
        } else {
            con_write_b(chan, c);
        }
    }

    return i;
}

short con_has_input(p_channel chan) {
    p_console_data con_data;
    char c;

    /* Check to see if we need to process ANSI codes */
    con_data = (p_console_data)&(chan->data);

    if (con_data->key_buffer != 0) {
        /* If we already peeked and have a character... return true */
        return 1;

    } else {
        /* Otherwise, peek at the keyboard to see if there is a valid key */

    #if MODEL == MODEL_FOENIX_A2560K
    #ifdef KBD_POLLED
                ps2_mouse_get_packet();
                c = kbdmo_getc_poll();
                if (c == 0) c = rc_getc();  /* Gen1 remote console: COM1 */
    #else
                c = con_kbd_getc();         /* 2.1.0: built-in keyboard, PS/2, COM1 */
    #endif
    #else
    #ifdef KBD_POLLED  
              c = kbd_getc_poll();      
    #else
                c = kbd_getc();
    #endif
    #endif

        if (c == 0) {
            /* No: return false */
            return 0;

        } else {
            /* Yes: save the key we got and return true */
            con_data->key_buffer = c;
            return 1;
        }
    }
}

//
// Return the status of the console
//
short con_status(p_channel chan) {
    short status = CDEV_STAT_WRITABLE;  /* The console is always writable */
    if (con_has_input(chan)) {
        /* If there is data available, flag that it's readable */
        status |= CDEV_STAT_READABLE;
    }

    return status;
}

//
// We can't seek on the console... just return 0
//
short con_seek(p_channel chan, long position, short base) {
    return 0;
}

/**
 * Show or hide the cursor
 *
 * @param chan
 * @param is_visible boolean to indicate if the cursor should be seen (non-zero) or hidden (0)
 */
short con_set_cursor_visible(p_channel chan, short is_visible) {
    txt_set_cursor_visible(chan->dev, is_visible);
    return 0;
}

short con_ioctrl(p_channel chan, short command, uint8_t * buffer, short size) {
    p_console_data con_data;

    /* Check to see if we need to process ANSI codes */
    con_data = (p_console_data)&(chan->data);

    switch (command) {
        case CON_IOCTRL_ANSI_ON:
            /* Turn on ANSI interpreting */
            con_data->control |= CON_CTRL_ANSI;
            return 0;

        case CON_IOCTRL_ANSI_OFF:
            /* Turn off ANSI interpreting */
            con_data->control &= ~CON_CTRL_ANSI;
            return 0;

        case CON_IOCTRL_ECHO_ON:
            /* Turn on echo interpreting */
            con_data->control |= CON_CTRL_ECHO;
            return 0;

        case CON_IOCTRL_ECHO_OFF:
            /* Turn off echo */
            con_data->control &= ~CON_CTRL_ECHO;
            return 0;

        case CON_IOCTRL_BREAK:
            /* Return the result of the BREAK key test */
#if MODEL == MODEL_FOENIX_A2560K
            return kbdmo_break();
#else
            /* TODO: flesh this out for the A2560U */
            return 0;
#endif

        case CON_IOCTRL_CURS_ON:
            // Show the cursor
            return con_set_cursor_visible(chan, 1);

        case CON_IOCTRL_CURS_OFF:
            // Hide the cursor
            return con_set_cursor_visible(chan, 0);

        default:
            break;
    }
    return 0;
}

//
// Install the console device driver
//
short con_install() {
    short result;
    t_dev_chan dev;

    dev.name = "CONSOLE";
    dev.number = CDEV_CONSOLE;
    dev.init = con_init;
    dev.open = con_open;
    dev.close = con_close;
    dev.read = con_read;
    dev.readline = con_readline;
    dev.read_b = con_read_b;
    dev.write = con_write;
    dev.write_b = con_write_b;
    dev.flush = con_flush;
    dev.seek = con_seek;
    dev.status = con_status;
    dev.ioctrl = con_ioctrl;

    result = cdev_register(&dev);
    if (result) {
        return result;
    }

    dev.name = "EVID";
    dev.number = CDEV_EVID;
    dev.init = con_init;
    dev.open = con_open;
    dev.close = con_close;
    dev.read = con_read;
    dev.readline = con_readline;
    dev.read_b = con_read_b;
    dev.write = con_write;
    dev.write_b = con_write_b;
    dev.flush = con_flush;
    dev.seek = con_seek;
    dev.status = con_status;
    dev.ioctrl = con_ioctrl;

    result = cdev_register(&dev);
    if (result) {
        return result;
    }

    /* Pre-open the console and EVID channels */

    chan_open(CDEV_CONSOLE, 0, 0);

#if MODEL == MODEL_FOENIX_A2560K || MODEL == MODEL_FOENIX_GENX || MODEL == MODEL_FOENIX_A2560X
    chan_open(CDEV_EVID, 0, 0);
#endif

    return result;
}
