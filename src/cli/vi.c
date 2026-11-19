/*
 * VI: a very small vi-style text editor for the MCP command line (2.0.34)
 *
 *   VI <path>
 *
 * Enough of vi to edit text files such as mcp.init on the machine itself. There are three modes,
 * as in vi:
 *
 * Normal mode (the start), moving:
 *   h j k l, the arrow keys      left, down, up, right (Backspace and Space also go left / right)
 *   0  Home      start of line    $  End      end of line    ^  first non-blank
 *   w  b         next / previous word
 *   gg  G        first / last line             :<number>   go to that line
 *   Ctrl+F  Ctrl+B, PgDn  PgUp     a screen down / up
 *   /text        search forward    n  search again
 * Normal mode, changing:
 *   i a I A o O  start inserting (before, after, line start, line end, new line below / above)
 *   x  Delete    delete the character        D   delete to the end of the line
 *   dd           delete the line (it can be put back with p)
 *   yy           copy the line     p  P      put the copied / deleted line below / above
 *   J            join with the next line     r<c>  replace the character with c
 *   ZZ           write and quit    Ctrl+L    redraw the screen
 * Insert mode: type text; Enter, Backspace, Delete, Tab and the arrow keys work; Ctrl+B / Ctrl+F
 *   go left / right as on the command line (the Left key of the A2560K sends nothing).
 *   Esc goes back to normal mode.
 * Command mode (':' in normal mode): :w  :w <path>  :q  :q!  :wq  :x  :<number>
 *
 * There is no undo and no count before a command (3dd): keep it simple. :q! leaves without
 * saving.
 *
 * How the text is kept: as one "gap buffer". All text is in one block of memory with a gap in
 * it at the place where the user edits; typing fills the gap, deleting widens it, and moving the
 * gap means moving only the text between the old and the new place. The block is the free
 * program memory from $0001_0000 up to the top of memory: while the MCP command line runs, no
 * program uses it (that is where programs are loaded). So a file can be megabytes, but a file
 * loaded there with LOAD for a later CALL is overwritten.
 *
 * The cursor is a steady block (the character under it in swapped colours): Vicky's own text
 * cursor always blinks, so VI hides it while it runs.
 *
 * The text always ends with a newline: every line, the last one too, ends with '\n'. A file
 * with CR LF line ends is shown without the CRs and written back with them.
 */

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include "constants.h"
#include "errors.h"
#include "log.h"
#include "syscalls.h"
#include "simpleio.h"
#include "cli/cli.h"
#include "cli/vi.h"
#include "dev/channel.h"

#define VI_MEMORY_START 0x00010000UL    /* first byte of the program memory */
#define VI_YANK_MAX     32768L          /* the largest line dd / yy can keep for p */
#define VI_TAB          8               /* a tab goes to the next multiple of 8 */
#define VI_MAX_WIDTH    200             /* widest screen line we draw */
#define VI_CMD_MAX      80              /* longest : command or / search */

/* Keys that are not characters: what the ESC [ sequences of the special keys become */
#define VI_KEY_NONE     0x100           /* a key the editor does not use */
#define VI_KEY_UP       0x101
#define VI_KEY_DOWN     0x102
#define VI_KEY_LEFT     0x103
#define VI_KEY_RIGHT    0x104
#define VI_KEY_HOME     0x105
#define VI_KEY_END      0x106
#define VI_KEY_PGUP     0x107
#define VI_KEY_PGDN     0x108
#define VI_KEY_DEL      0x109
#define VI_KEY_ESC      0x10A           /* the Esc key on its own */

#define CTRL(c)         ((c) & 0x1f)

/* How much of the screen must be drawn again after a key */
#define VI_DRAW_STATUS  0               /* only the status line and the cursor */
#define VI_DRAW_LINE    1               /* the cursor's line too */
#define VI_DRAW_ALL     2               /* everything */

/*
 * Everything the editor knows, in one place
 */
static struct {
    char * buf;                 /* the gap buffer */
    long cap;                   /* its size */
    long gs, ge;                /* the gap: buf[gs] up to (not including) buf[ge] */
    long cur;                   /* the cursor: a position in the text (0 = first character) */
    long line;                  /* the cursor's line (0 = first) */
    long lines;                 /* the number of lines */
    long top;                   /* position of the first character on the screen */
    long top_line;              /* its line */
    short left;                 /* the first column shown (for lines wider than the screen) */
    short want;                 /* the column j and k try to keep */
    short insert;               /* 1 in insert mode */
    short modified;             /* 1 when there are changes that are not written */
    short crlf;                 /* 1 if the file had CR LF line ends */
    short channel;              /* the console channel */
    short dev;                  /* its text screen */
    short width;                /* columns on the screen */
    short rows;                 /* screen lines for text (one more is the status line) */
    short draw;                 /* VI_DRAW_...: what to draw before the next key */
    short cx, cy;               /* where the cursor block is drawn (cx < 0: not drawn) */
    char cc;                    /* the character under it */
    char * yank;                /* the line dd / yy kept, for p and P */
    long yank_len;              /* its length (0 = nothing kept) */
    char path[MAX_PATH_LEN];    /* the file */
    char search[VI_CMD_MAX];    /* the last search text, for n */
    char message[VI_CMD_MAX + MAX_PATH_LEN];   /* shown on the status line until the next key */
} vi;

/*
 * The number of characters in the text
 */
static long vi_length(void) {
    return vi.cap - (vi.ge - vi.gs);
}

/*
 * The character at a position in the text
 *
 * Inputs:
 * pos = the position (0 up to vi_length() - 1)
 *
 * Returns:
 * the character
 */
static char vi_at(long pos) {
    if (pos < vi.gs) {
        return vi.buf[pos];
    } else {
        return vi.buf[pos + (vi.ge - vi.gs)];
    }
}

/*
 * Move the gap to a position, so text can be inserted or deleted there
 *
 * Inputs:
 * pos = the position
 */
static void vi_move_gap(long pos) {
    long n;

    if (pos < vi.gs) {
        /* The text between pos and the gap moves to after the gap */
        n = vi.gs - pos;
        memmove(vi.buf + vi.ge - n, vi.buf + pos, n);
        vi.gs -= n;
        vi.ge -= n;

    } else if (pos > vi.gs) {
        /* The text after the gap, up to pos, moves to before the gap */
        n = pos - vi.gs;
        memmove(vi.buf + vi.gs, vi.buf + vi.ge, n);
        vi.gs += n;
        vi.ge += n;
    }
}

/*
 * Move the cursor to a position, keeping its line number right
 *
 * Inputs:
 * pos = the new position
 */
static void vi_goto(long pos) {
    long p;

    if (pos > vi.cur) {
        for (p = vi.cur; p < pos; p++) {
            if (vi_at(p) == '\n') {
                vi.line++;
            }
        }
    } else {
        for (p = pos; p < vi.cur; p++) {
            if (vi_at(p) == '\n') {
                vi.line--;
            }
        }
    }
    vi.cur = pos;
}

/*
 * Insert a character at the cursor; the cursor moves after it
 *
 * Inputs:
 * c = the character
 *
 * Returns:
 * 0 on success, -1 if the memory is full
 */
static short vi_insert(char c) {
    if (vi.gs == vi.ge) {
        strcpy(vi.message, "Out of memory");
        return -1;
    }
    vi_move_gap(vi.cur);
    vi.buf[vi.gs++] = c;
    vi.cur++;
    if (c == '\n') {
        vi.lines++;
        vi.line++;
    }
    vi.modified = 1;
    return 0;
}

/*
 * Delete characters at the cursor; the cursor stays where it is
 *
 * Inputs:
 * count = how many
 */
static void vi_delete(long count) {
    vi_move_gap(vi.cur);
    while ((count-- > 0) && (vi.ge < vi.cap)) {
        if (vi.buf[vi.ge] == '\n') {
            vi.lines--;
        }
        vi.ge++;
        vi.modified = 1;
    }
}

/*
 * Find the start of the line a position is on
 */
static long vi_line_start(long pos) {
    while ((pos > 0) && (vi_at(pos - 1) != '\n')) {
        pos--;
    }
    return pos;
}

/*
 * Find the end of the line a position is on: the position of its '\n'
 */
static long vi_line_end(long pos) {
    while (vi_at(pos) != '\n') {
        pos++;
    }
    return pos;
}

/*
 * The screen column of a position: tabs count as the spaces up to the next tab stop
 */
static short vi_column(long pos) {
    long p;
    short col = 0;

    for (p = vi_line_start(pos); p < pos; p++) {
        if (vi_at(p) == '\t') {
            col = (col / VI_TAB + 1) * VI_TAB;
        } else {
            col++;
        }
    }
    return col;
}

/*
 * Find the position on a line that is at (or just before) a screen column
 *
 * Inputs:
 * start = the start of the line
 * col = the column wanted
 *
 * Returns:
 * the position (the line's '\n' if the line is shorter)
 */
static long vi_at_column(long start, short col) {
    long p = start;
    short c = 0, next;
    char ch;

    while ((ch = vi_at(p)) != '\n') {
        next = (ch == '\t') ? (c / VI_TAB + 1) * VI_TAB : c + 1;
        if (next > col) {
            break;
        }
        c = next;
        p++;
    }
    return p;
}

/*
 * In normal mode the cursor sits on a character, not after the last one of a line
 */
static void vi_normal_clamp(void) {
    if ((vi_at(vi.cur) == '\n') && (vi.cur > vi_line_start(vi.cur))) {
        vi_goto(vi.cur - 1);
    }
}

/*
 * Go up or down a number of lines, keeping the column the user had
 *
 * Inputs:
 * count = lines to go: negative is up
 */
static void vi_move_lines(long count) {
    long p = vi_line_start(vi.cur);

    while ((count < 0) && (p > 0)) {
        p = vi_line_start(p - 1);
        count++;
    }
    while ((count > 0) && (vi_line_end(p) + 1 < vi_length())) {
        p = vi_line_end(p) + 1;
        count--;
    }
    vi_goto(vi_at_column(p, vi.want));
}

/*
 * Go to the start of a line by its number
 *
 * Inputs:
 * number = the line (0 = first; past the end = the last line)
 */
static void vi_goto_line(long number) {
    long p;

    if (number >= vi.lines) {
        number = vi.lines - 1;
    }
    if (number < 0) {
        number = 0;
    }

    if (number < vi.line) {
        /* Count from the start: simpler than counting back */
        vi.cur = 0;
        vi.line = 0;
    }
    p = vi_line_start(vi.cur);
    while (vi.line < number) {
        p = vi_line_end(p) + 1;
        vi_goto(p);
    }
    vi_goto(p);
}

/*
 * The first character on the cursor's line that is not a space or a tab
 */
static long vi_first_non_blank(void) {
    long p = vi_line_start(vi.cur);

    while ((vi_at(p) == ' ') || (vi_at(p) == '\t')) {
        p++;
    }
    return p;
}

/*
 * The kind of a character for w and b: 0 space, 1 letter / digit / _, 2 anything else
 */
static short vi_kind(char c) {
    if (isspace((unsigned char)c)) {
        return 0;
    } else if (isalnum((unsigned char)c) || (c == '_')) {
        return 1;
    }
    return 2;
}

/*
 * w: to the start of the next word
 */
static void vi_word_forward(void) {
    long p = vi.cur, end = vi_length() - 1;
    short kind = vi_kind(vi_at(p));

    while ((p < end) && (kind != 0) && (vi_kind(vi_at(p)) == kind)) {
        p++;
    }
    while ((p < end) && (vi_kind(vi_at(p)) == 0)) {
        p++;
    }
    vi_goto(p);
}

/*
 * b: to the start of this word, or of the previous one
 */
static void vi_word_back(void) {
    long p = vi.cur;
    short kind;

    while ((p > 0) && (vi_kind(vi_at(p - 1)) == 0)) {
        p--;
    }
    if (p > 0) {
        kind = vi_kind(vi_at(p - 1));
        while ((p > 0) && (vi_kind(vi_at(p - 1)) == kind)) {
            p--;
        }
    }
    vi_goto(p);
}

/*
 * Keep the cursor on the screen: scroll up, down, left or right when it is not
 */
static void vi_scroll(void) {
    long p;
    short i, col;

    if (vi.line < vi.top_line) {
        vi.top = vi_line_start(vi.cur);
        vi.top_line = vi.line;
        vi.draw = VI_DRAW_ALL;

    } else if (vi.line >= vi.top_line + vi.rows) {
        /* The cursor's line becomes the last line on the screen */
        p = vi_line_start(vi.cur);
        for (i = 1; (i < vi.rows) && (p > 0); i++) {
            p = vi_line_start(p - 1);
        }
        vi.top = p;
        vi.top_line = vi.line - vi.rows + 1;
        vi.draw = VI_DRAW_ALL;
    }

    col = vi_column(vi.cur);
    if ((col < vi.left) || (col >= vi.left + vi.width)) {
        vi.left = col - vi.width / 2;
        if (vi.left < 0) {
            vi.left = 0;
        }
        vi.draw = VI_DRAW_ALL;
    }
}

/*
 * Draw one screen line: the text line that starts at a position, or '~' past the end
 *
 * Inputs:
 * y = the screen line
 * pos = the start of the text line, or -1 for "past the end of the text"
 */
static void vi_draw_line(short y, long pos) {
    char out[VI_MAX_WIDTH];
    short col = 0, next, n = 0;
    char c;

    if (pos < 0) {
        out[n++] = '~';
    } else {
        while (((c = vi_at(pos)) != '\n') && (col < vi.left + vi.width)) {
            next = (c == '\t') ? (col / VI_TAB + 1) * VI_TAB : col + 1;
            if ((unsigned char)c < ' ' || c == 0x7f) {
                c = (c == '\t') ? ' ' : '?';   /* control characters would upset the screen */
            }
            while ((col < next) && (col < vi.left + vi.width)) {
                if (col >= vi.left) {
                    out[n++] = c;
                }
                col++;
            }
            pos++;
        }
    }

    sys_txt_set_xy(vi.dev, 0, y);
    if (n > 0) {
        sys_chan_write(vi.channel, (unsigned char *)out, n);
    }
    if (n < vi.width) {
        /* Clear the rest of the line (a full line has moved the cursor to the next one) */
        print(vi.channel, "\x1b[0K");
    }
}

/*
 * Draw the status line: a message, or the mode, and where the cursor is
 */
static void vi_draw_status(void) {
    char line[VI_MAX_WIDTH + MAX_PATH_LEN];
    char where[40];
    unsigned char fg, bg;
    short n;

    if (vi.message[0] != 0) {
        strcpy(line, vi.message);
    } else if (vi.insert) {
        strcpy(line, "-- INSERT --");
    } else {
        sprintf(line, "\"%s\"%s", vi.path, vi.modified ? " [+]" : "");
    }

    sprintf(where, "%ld,%d  %ld lines", vi.line + 1, vi_column(vi.cur) + 1, vi.lines);

    /* The position at the right; never write the last column (that would scroll the screen) */
    n = strlen(line);
    if (n > vi.width - (short)strlen(where) - 2) {
        n = vi.width - strlen(where) - 2;
        if (n < 0) {
            n = 0;
        }
        line[n] = 0;
    }
    while (n < vi.width - (short)strlen(where) - 1) {
        line[n++] = ' ';
    }
    line[n] = 0;
    strcat(line, where);

    /* In swapped colours, like the title bar (the console has no ESC[7m); the clear fills the
     * last column with the same colours
     */
    sys_txt_get_color(vi.dev, &fg, &bg);
    sys_txt_set_color(vi.dev, bg, fg);
    sys_txt_set_xy(vi.dev, 0, vi.rows);
    sys_chan_write(vi.channel, (unsigned char *)line, strlen(line));
    print(vi.channel, "\x1b[0K");
    sys_txt_set_color(vi.dev, fg, bg);
}

/*
 * Draw one character cell, in the normal or in swapped colours
 *
 * Inputs:
 * x, y = the cell
 * c = the character
 * swapped = 1 for swapped colours (the cursor block), 0 for normal
 */
static void vi_cell(short x, short y, char c, short swapped) {
    unsigned char fg, bg;

    sys_txt_get_color(vi.dev, &fg, &bg);
    if (swapped) {
        sys_txt_set_color(vi.dev, bg, fg);
    }
    sys_txt_set_xy(vi.dev, x, y);
    sys_chan_write_b(vi.channel, c);
    if (swapped) {
        sys_txt_set_color(vi.dev, fg, bg);
    }
}

/*
 * Draw the cursor as a steady block: the character under it in swapped colours
 *
 * Vicky's text cursor on this screen always blinks (its control register only has blink
 * rates), so VI hides it and draws its own. The cell is remembered, to draw it normally again
 * when the cursor moves away.
 *
 * Inputs:
 * x, y = the cell
 * c = the character under the cursor
 */
static void vi_cursor_show(short x, short y, char c) {
    vi.cx = x;
    vi.cy = y;
    vi.cc = c;
    vi_cell(x, y, c, 1);
}

/*
 * Draw the cell under the cursor block normally again
 */
static void vi_cursor_hide(void) {
    if (vi.cx >= 0) {
        vi_cell(vi.cx, vi.cy, vi.cc, 0);
        vi.cx = -1;
    }
}

/*
 * Draw what changed, then the cursor
 */
static void vi_redraw(void) {
    long p;
    short y;

    char c;

    vi_cursor_hide();
    vi_scroll();

    if (vi.draw == VI_DRAW_ALL) {
        p = vi.top;
        for (y = 0; y < vi.rows; y++) {
            if (p < vi_length()) {
                vi_draw_line(y, p);
                p = vi_line_end(p) + 1;
            } else {
                vi_draw_line(y, -1);
            }
        }
    } else if (vi.draw == VI_DRAW_LINE) {
        vi_draw_line((short)(vi.line - vi.top_line), vi_line_start(vi.cur));
    }

    vi_draw_status();

    /* The cursor block shows the character under it: a space at the end of a line or on a tab */
    c = vi_at(vi.cur);
    if ((unsigned char)c < ' ' || c == 0x7f) {
        c = ((c == '\n') || (c == '\t')) ? ' ' : '?';
    }
    vi_cursor_show(vi_column(vi.cur) - vi.left, (short)(vi.line - vi.top_line), c);
    vi.draw = VI_DRAW_STATUS;
}

/*
 * Wait for a key; the special keys come as ESC [ sequences and become VI_KEY_...
 *
 * The keyboard driver puts a whole sequence in the queue at once. The built-in keyboard
 * (kbd_mo.c) sends the Esc key as ESC ESC, so a sequence can't be taken for it; other
 * keyboards send a lone ESC. Both become VI_KEY_ESC.
 *
 * Returns:
 * the character, or a VI_KEY_... code
 */
static short vi_key(void) {
    short c, number = 0, first = -1;

    c = sys_chan_read_b(vi.channel) & 0xff;
    if (c != CHAR_ESC) {
        return c;
    }
    if ((sys_chan_status(vi.channel) & CDEV_STAT_READABLE) == 0) {
        return VI_KEY_ESC;
    }
    c = sys_chan_read_b(vi.channel) & 0xff;
    if (c == CHAR_ESC) {
        return VI_KEY_ESC;              /* the keyboard driver doubles the Esc key itself */
    }
    if (c != '[') {
        return VI_KEY_NONE;             /* Alt + key: not used */
    }

    while (1) {
        c = sys_chan_read_b(vi.channel) & 0xff;
        if (isdigit(c)) {
            number = number * 10 + (c - '0');
        } else if (c == ';') {
            first = number;             /* what comes after ; are the modifier keys */
            number = 0;
        } else {
            break;
        }
    }
    if (first < 0) {
        first = number;
    }

    switch (c) {
        case 'A': return VI_KEY_UP;
        case 'B': return VI_KEY_DOWN;
        case 'C': return VI_KEY_RIGHT;
        case 'D': return VI_KEY_LEFT;
        case 'H': return VI_KEY_HOME;
        case 'F': return VI_KEY_END;
        case '~':
            switch (first) {
                case 1: return VI_KEY_HOME;
                case 3: return VI_KEY_DEL;
                case 4: return VI_KEY_END;
                case 5: return VI_KEY_PGUP;
                case 6: return VI_KEY_PGDN;
                default: return VI_KEY_NONE;
            }
        default:
            return VI_KEY_NONE;
    }
}

/*
 * Read a line on the status line, for : and /
 *
 * Inputs:
 * prompt = the character shown first (':' or '/')
 * text = where the line goes (VI_CMD_MAX bytes)
 *
 * Returns:
 * 1 if the user pressed Enter, 0 for Esc (or Backspace on an empty line)
 */
static short vi_read_line(char prompt, char * text) {
    short n = 0, key;

    text[0] = 0;
    vi_cursor_hide();
    sys_txt_set_xy(vi.dev, 0, vi.rows);
    sys_chan_write_b(vi.channel, prompt);
    print(vi.channel, "\x1b[0K");

    while (1) {
        /* The cursor block after the text; the line is drawn again after the command */
        vi_cursor_show(n + 1, vi.rows, ' ');
        key = vi_key();
        vi_cursor_hide();
        if ((key == CHAR_CR) || (key == CHAR_NL)) {
            return 1;
        } else if (key == VI_KEY_ESC) {
            return 0;
        } else if ((key == CHAR_BS) || (key == 0x7f)) {
            if (n == 0) {
                return 0;
            }
            text[--n] = 0;
        } else if ((key >= ' ') && (key < 0x7f) && (n < VI_CMD_MAX - 1) && (n < vi.width - 3)) {
            vi_cell(n + 1, vi.rows, (char)key, 0);
            text[n++] = (char)key;
            text[n] = 0;
        }
    }
}

/*
 * Load the file into the buffer
 *
 * Returns:
 * 0 on success (also for a new file), a negative error number if it could not be read
 */
static short vi_load(void) {
    short fd, n;
    long len = 0, i, j;

    fd = (vi.path[0] == 0) ? FSYS_ERR_NO_FILE : sys_fsys_open(vi.path, FSYS_READ);
    if (fd < 0) {
        if ((fd == FSYS_ERR_NO_FILE) || (fd == ERR_NOT_FOUND)) {
            /* A new file: one empty line */
            vi.buf[0] = '\n';
            len = 1;
            if (vi.path[0] == 0) {
                strcpy(vi.message, "[No name] (:w <path> writes it)");
            } else {
                sprintf(vi.message, "\"%s\" [New file]", vi.path);
            }
        } else {
            return fd;
        }
    } else {
        while (1) {
            /* Leave room for a final '\n' */
            long room = vi.cap - len - 1;
            if (room <= 0) {
                sys_fsys_close(fd);
                return ERR_OUT_OF_MEMORY;
            }
            n = sys_chan_read(fd, (unsigned char *)vi.buf + len, (room > 16384) ? 16384 : (short)room);
            if (n <= 0) {
                break;
            }
            len += n;
        }
        sys_fsys_close(fd);

        /* CR LF becomes LF; remember it, to write it back the same way */
        for (i = 0, j = 0; i < len; i++) {
            if ((vi.buf[i] == '\r') && (i + 1 < len) && (vi.buf[i + 1] == '\n')) {
                vi.crlf = 1;
            } else {
                vi.buf[j++] = vi.buf[i];
            }
        }
        len = j;

        /* Every line ends with '\n', the last one too */
        if ((len == 0) || (vi.buf[len - 1] != '\n')) {
            vi.buf[len++] = '\n';
        }
    }

    vi.lines = 0;
    for (i = 0; i < len; i++) {
        if (vi.buf[i] == '\n') {
            vi.lines++;
        }
    }

    /* The gap is everything after the text */
    vi.gs = len;
    vi.ge = vi.cap;
    if (vi.message[0] == 0) {
        sprintf(vi.message, "\"%s\" %ld lines%s", vi.path, vi.lines, vi.crlf ? " [CR LF]" : "");
    }
    return 0;
}

/*
 * Write the text to a file
 *
 * Inputs:
 * path = the file
 *
 * Returns:
 * 0 on success, a negative error number otherwise (the message says what happened)
 */
static short vi_write(const char * path) {
    unsigned char chunk[512];
    short fd, n = 0, result = 0;
    long p, len;

    if (path[0] == 0) {
        strcpy(vi.message, "No file name (use :w <path>)");
        return ERR_BAD_ARGUMENT;
    }

    /* All text before the gap, so it is one block */
    len = vi_length();
    vi_move_gap(len);
    if ((len == 1) && (vi.buf[0] == '\n')) {
        len = 0;                        /* an empty text is an empty file */
    }

    fd = sys_fsys_open(path, FSYS_WRITE | FSYS_CREATE_ALWAYS);
    if (fd < 0) {
        sprintf(vi.message, "Cannot write \"%s\": %s", path, sys_err_message(fd));
        return fd;
    }

    for (p = 0; p < len; p++) {
        if ((vi.buf[p] == '\n') && vi.crlf) {
            chunk[n++] = '\r';
        }
        chunk[n++] = vi.buf[p];
        if ((n >= (short)sizeof(chunk) - 1) || (p == len - 1)) {
            if (sys_chan_write(fd, chunk, n) != n) {
                result = FSYS_ERR_DENIED;
                break;
            }
            n = 0;
        }
    }
    sys_fsys_close(fd);

    if (result != 0) {
        sprintf(vi.message, "Write error on \"%s\" (disk full?)", path);
        return result;
    }
    sprintf(vi.message, "\"%s\" %ld lines written", path, vi.lines);
    vi.modified = 0;
    return 0;
}

/*
 * Keep a copy of the cursor's line (with its '\n') for p and P
 */
static void vi_yank_line(void) {
    long s = vi_line_start(vi.cur), e = vi_line_end(vi.cur) + 1, i;

    if (e - s > VI_YANK_MAX) {
        strcpy(vi.message, "Line too long to keep");
        vi.yank_len = 0;
        return;
    }
    for (i = s; i < e; i++) {
        vi.yank[i - s] = vi_at(i);
    }
    vi.yank_len = e - s;
}

/*
 * dd: delete the cursor's line (it is kept for p)
 */
static void vi_delete_line(void) {
    long s = vi_line_start(vi.cur), e = vi_line_end(vi.cur) + 1;

    vi_yank_line();
    if (vi.lines == 1) {
        /* The only line: empty it, the '\n' stays */
        vi_goto(s);
        vi_delete(e - s - 1);
    } else if (e == vi_length()) {
        /* The last line: delete the '\n' before it instead of its own */
        vi_goto(s - 1);
        vi_delete(e - s);
        vi_goto(vi_line_start(vi.cur));
    } else {
        vi_goto(s);
        vi_delete(e - s);
    }
    vi_goto(vi_first_non_blank());
    vi.draw = VI_DRAW_ALL;
}

/*
 * p / P: put the kept line below / above the cursor's line
 *
 * Inputs:
 * below = 1 for p, 0 for P
 */
static void vi_put(short below) {
    long pos, i;

    if (vi.yank_len == 0) {
        strcpy(vi.message, "Nothing to put (use dd or yy first)");
        return;
    }
    pos = below ? vi_line_end(vi.cur) + 1 : vi_line_start(vi.cur);
    vi_goto(pos);
    for (i = 0; i < vi.yank_len; i++) {
        if (vi_insert(vi.yank[i]) != 0) {
            break;
        }
    }
    vi_goto(pos);
    vi_goto(vi_first_non_blank());
    vi.draw = VI_DRAW_ALL;
}

/*
 * J: join the next line to the cursor's line, with one space between them
 */
static void vi_join(void) {
    long e = vi_line_end(vi.cur);

    if (e + 1 >= vi_length()) {
        return;                         /* the last line: nothing to join */
    }
    vi_goto(e);
    vi_delete(1);                       /* the '\n' */
    while ((vi_at(vi.cur) == ' ') || (vi_at(vi.cur) == '\t')) {
        vi_delete(1);                   /* the indent of the next line */
    }
    if ((vi.cur > vi_line_start(vi.cur)) && (vi_at(vi.cur) != '\n')) {
        vi_insert(' ');
        vi_goto(vi.cur - 1);
    }
    vi.draw = VI_DRAW_ALL;
}

/*
 * /text and n: search forward from the cursor, going on at the start after the end
 */
static void vi_find(void) {
    long len = vi_length(), start, p, i, n = strlen(vi.search);

    if (n == 0) {
        strcpy(vi.message, "No search text");
        return;
    }
    start = vi.cur + 1;
    for (i = 0; i < len; i++) {
        long j;
        p = (start + i) % len;
        for (j = 0; j < n; j++) {
            if ((p + j >= len) || (vi_at(p + j) != vi.search[j])) {
                break;
            }
        }
        if (j == n) {
            if (p < start) {
                strcpy(vi.message, "Search went on at the top");
            }
            vi_goto(p);
            vi.want = vi_column(vi.cur);
            return;
        }
    }
    sprintf(vi.message, "Not found: %s", vi.search);
}

/*
 * Carry out a : command
 *
 * Inputs:
 * cmd = the command, without the ':'
 *
 * Returns:
 * 1 to leave the editor, 0 to go on
 */
static short vi_command(char * cmd) {
    char * arg;

    while (*cmd == ' ') {
        cmd++;
    }
    arg = strchr(cmd, ' ');
    if (arg != 0) {
        *arg++ = 0;
        while (*arg == ' ') {
            arg++;
        }
    } else {
        arg = "";
    }

    if (isdigit((unsigned char)cmd[0])) {
        long number = 0;
        while (isdigit((unsigned char)*cmd)) {
            number = number * 10 + (*cmd++ - '0');
        }
        vi_goto_line(number - 1);
        vi_normal_clamp();
        vi.want = vi_column(vi.cur);

    } else if (strcmp(cmd, "w") == 0) {
        if (arg[0] != 0) {
            if (vi_write(arg) == 0) {
                if (vi.path[0] == 0) {
                    strncpy(vi.path, arg, MAX_PATH_LEN - 1);
                }
            }
        } else {
            vi_write(vi.path);
        }

    } else if ((strcmp(cmd, "wq") == 0) || (strcmp(cmd, "x") == 0)) {
        if ((cmd[0] == 'x') && !vi.modified) {
            return 1;
        }
        return (vi_write((arg[0] != 0) ? arg : vi.path) == 0) ? 1 : 0;

    } else if (strcmp(cmd, "q") == 0) {
        if (vi.modified) {
            strcpy(vi.message, "Not written since the last change (:q! leaves anyway, :wq writes)");
            return 0;
        }
        return 1;

    } else if (strcmp(cmd, "q!") == 0) {
        return 1;

    } else if (cmd[0] != 0) {
        sprintf(vi.message, "Unknown command: %s (use :w :q :q! :wq :x :<line>)", cmd);
    }
    return 0;
}

/*
 * Handle a key in insert mode
 *
 * Inputs:
 * key = the key
 */
static void vi_insert_key(short key) {
    switch (key) {
        case VI_KEY_ESC:
            vi.insert = 0;
            if (vi.cur > vi_line_start(vi.cur)) {
                vi_goto(vi.cur - 1);
            }
            vi.want = vi_column(vi.cur);
            break;

        case CHAR_CR:
        case CHAR_NL:
            vi_insert('\n');
            vi.want = 0;
            vi.draw = VI_DRAW_ALL;
            break;

        case CHAR_BS:
        case 0x7f:
            if (vi.cur > 0) {
                vi_goto(vi.cur - 1);
                if (vi_at(vi.cur) == '\n') {
                    vi.draw = VI_DRAW_ALL;      /* two lines become one */
                } else if (vi.draw < VI_DRAW_LINE) {
                    vi.draw = VI_DRAW_LINE;
                }
                vi_delete(1);
            }
            vi.want = vi_column(vi.cur);
            break;

        case VI_KEY_DEL:
            if (vi.cur < vi_length() - 1) {
                vi.draw = (vi_at(vi.cur) == '\n') ? VI_DRAW_ALL : VI_DRAW_LINE;
                vi_delete(1);
            }
            break;

        case VI_KEY_LEFT:
        case CTRL('B'):
            if (vi.cur > vi_line_start(vi.cur)) {
                vi_goto(vi.cur - 1);
            }
            vi.want = vi_column(vi.cur);
            break;

        case VI_KEY_RIGHT:
        case CTRL('F'):
            if (vi_at(vi.cur) != '\n') {
                vi_goto(vi.cur + 1);
            }
            vi.want = vi_column(vi.cur);
            break;

        case VI_KEY_UP:
            vi_move_lines(-1);
            break;

        case VI_KEY_DOWN:
            vi_move_lines(1);
            break;

        case VI_KEY_HOME:
            vi_goto(vi_line_start(vi.cur));
            vi.want = 0;
            break;

        case VI_KEY_END:
            vi_goto(vi_line_end(vi.cur));
            vi.want = vi_column(vi.cur);
            break;

        default:
            if ((key == CHAR_TAB) || ((key >= ' ') && (key < 0x100) && (key != 0x7f))) {
                vi_insert((char)key);
                vi.want = vi_column(vi.cur);
                if (vi.draw < VI_DRAW_LINE) {
                    vi.draw = VI_DRAW_LINE;
                }
            }
            break;
    }
}

/*
 * Handle a key in normal mode
 *
 * Inputs:
 * key = the key
 *
 * Returns:
 * 1 to leave the editor, 0 to go on
 */
static short vi_normal_key(short key) {
    char text[VI_CMD_MAX];
    short next;

    switch (key) {
        /* Moving */
        case 'h':
        case VI_KEY_LEFT:
        case CHAR_BS:
            if (vi.cur > vi_line_start(vi.cur)) {
                vi_goto(vi.cur - 1);
            }
            vi.want = vi_column(vi.cur);
            break;

        case 'l':
        case ' ':
        case VI_KEY_RIGHT:
            if ((vi_at(vi.cur) != '\n') && (vi_at(vi.cur + 1) != '\n')) {
                vi_goto(vi.cur + 1);
            }
            vi.want = vi_column(vi.cur);
            break;

        case 'j':
        case VI_KEY_DOWN:
            vi_move_lines(1);
            break;

        case 'k':
        case VI_KEY_UP:
            vi_move_lines(-1);
            break;

        case CHAR_CR:
        case CHAR_NL:
        case '+':
            vi_move_lines(1);
            vi_goto(vi_first_non_blank());
            vi.want = vi_column(vi.cur);
            break;

        case '-':
            vi_move_lines(-1);
            vi_goto(vi_first_non_blank());
            vi.want = vi_column(vi.cur);
            break;

        case '0':
        case VI_KEY_HOME:
            vi_goto(vi_line_start(vi.cur));
            vi.want = 0;
            break;

        case '^':
            vi_goto(vi_first_non_blank());
            vi.want = vi_column(vi.cur);
            break;

        case '$':
        case VI_KEY_END:
            vi_goto(vi_line_end(vi.cur));
            vi.want = 32767;            /* stay at the end on j and k */
            break;

        case 'w':
            vi_word_forward();
            vi.want = vi_column(vi.cur);
            break;

        case 'b':
            vi_word_back();
            vi.want = vi_column(vi.cur);
            break;

        case 'G':
            vi_goto_line(vi.lines - 1);
            vi.want = vi_column(vi.cur);
            break;

        case 'g':
            if (vi_key() == 'g') {
                vi_goto_line(0);
                vi.want = 0;
            }
            break;

        case CTRL('F'):
        case VI_KEY_PGDN:
            vi_move_lines(vi.rows - 1);
            break;

        case CTRL('B'):
        case VI_KEY_PGUP:
            vi_move_lines(-(vi.rows - 1));
            break;

        case CTRL('L'):
            vi.draw = VI_DRAW_ALL;
            break;

        /* Searching */
        case '/':
            if (vi_read_line('/', text)) {
                if (text[0] != 0) {
                    strcpy(vi.search, text);    /* an empty / searches for the last text again */
                }
                vi_find();
            }
            break;

        case 'n':
            vi_find();
            break;

        /* Inserting */
        case 'i':
            vi.insert = 1;
            break;

        case 'a':
            if (vi_at(vi.cur) != '\n') {
                vi_goto(vi.cur + 1);
            }
            vi.insert = 1;
            break;

        case 'I':
            vi_goto(vi_first_non_blank());
            vi.insert = 1;
            break;

        case 'A':
            vi_goto(vi_line_end(vi.cur));
            vi.insert = 1;
            break;

        case 'o':
            vi_goto(vi_line_end(vi.cur));
            vi_insert('\n');
            vi.insert = 1;
            vi.draw = VI_DRAW_ALL;
            break;

        case 'O':
            vi_goto(vi_line_start(vi.cur));
            vi_insert('\n');
            vi_goto(vi.cur - 1);
            vi.insert = 1;
            vi.draw = VI_DRAW_ALL;
            break;

        /* Changing */
        case 'x':
        case VI_KEY_DEL:
            if (vi_at(vi.cur) != '\n') {
                vi_delete(1);
                vi.draw = VI_DRAW_LINE;
            }
            break;

        case 'D':
            vi_delete(vi_line_end(vi.cur) - vi.cur);
            vi.draw = VI_DRAW_LINE;
            break;

        case 'r':
            next = vi_key();
            if ((vi_at(vi.cur) != '\n') && (((next >= ' ') && (next < 0x7f)) || (next == CHAR_TAB))) {
                vi_delete(1);
                vi_insert((char)next);
                vi_goto(vi.cur - 1);
                vi.draw = VI_DRAW_LINE;
            }
            break;

        case 'd':
            if (vi_key() == 'd') {
                vi_delete_line();
            }
            break;

        case 'y':
            if (vi_key() == 'y') {
                vi_yank_line();
                if (vi.message[0] == 0) {
                    strcpy(vi.message, "1 line kept (p puts it below, P above)");
                }
            }
            break;

        case 'p':
            vi_put(1);
            break;

        case 'P':
            vi_put(0);
            break;

        case 'J':
            vi_join();
            break;

        case 'u':
            strcpy(vi.message, "No undo in this vi (:q! leaves without saving)");
            break;

        /* Commands */
        case ':':
            if (vi_read_line(':', text)) {
                if (vi_command(text)) {
                    return 1;
                }
            }
            break;

        case 'Z':
            if (vi_key() == 'Z') {
                if (!vi.modified || (vi_write(vi.path) == 0)) {
                    return 1;
                }
            }
            break;

        default:
            break;
    }

    if (!vi.insert) {
        vi_normal_clamp();
    }
    return 0;
}

/*
 * VI <path>: edit a text file (see the top of this file)
 *
 * Inputs:
 * screen = the console channel
 * argc, argv = the command line
 *
 * Returns:
 * 0 on success, a negative error number if the file could not be read
 */
short cmd_vi(short screen, int argc, const char * argv[]) {
    t_rect region;
    unsigned long top;
    short result;

    memset(&vi, 0, sizeof(vi));
    vi.channel = screen;
    vi.dev = sys_chan_device(screen);

    if (argc > 1) {
        strncpy(vi.path, argv[1], MAX_PATH_LEN - 1);
    }

    /* The memory: the program memory up to the top of memory; the kept line at its end */
    top = sys_mem_get_ramtop();
    if (top <= VI_MEMORY_START + 2 * VI_YANK_MAX) {
        print(screen, "VI: no free memory\n");
        return ERR_OUT_OF_MEMORY;
    }
    vi.yank = (char *)(top - VI_YANK_MAX);
    vi.buf = (char *)VI_MEMORY_START;
    vi.cap = (long)(top - VI_YANK_MAX - VI_MEMORY_START);

    result = vi_load();
    if (result != 0) {
        err_print(screen, "VI: cannot read the file", result);
        return result;
    }

    /* The screen: the command window, its last line for the status */
    sys_txt_get_region(vi.dev, &region);
    vi.width = region.size.width;
    if (vi.width > VI_MAX_WIDTH) {
        vi.width = VI_MAX_WIDTH;
    }
    vi.rows = region.size.height - 1;
    if ((vi.rows < 2) || (vi.width < 20)) {
        print(screen, "VI: the window is too small\n");
        return ERR_BAD_ARGUMENT;
    }

    print(screen, "\x1b[2J");
    sys_txt_set_cursor_visible(vi.dev, 0);     /* VI draws its own, steady cursor */
    vi.cx = -1;
    vi.draw = VI_DRAW_ALL;

    while (1) {
        short key;

        vi_redraw();
        key = vi_key();
        vi.message[0] = 0;

        if (vi.insert) {
            vi_insert_key(key);
        } else if (vi_normal_key(key)) {
            break;
        }
    }

    /* Back to the command line on a clean screen, with its own cursor */
    print(screen, "\x1b[2J");
    sys_txt_set_xy(vi.dev, 0, 0);
    sys_txt_set_cursor_visible(vi.dev, 1);
    return 0;
}
