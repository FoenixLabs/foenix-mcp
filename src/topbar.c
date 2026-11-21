/**
 * @file topbar.c
 *
 * 2.1.0: the top bar of the text screens (row 0). See topbar.h for the layout.
 *
 * The bar is drawn with the text driver (txt_*), not through a channel, so it can be drawn while a
 * program runs (the sys_txt_topbar system call) without disturbing the program's console: the
 * region, cursor and colours of the screen are saved first and put back afterwards.
 *
 * The clock is not drawn here: the RTC interrupt writes the time straight into the text memory of
 * row 0 (rtc.c), in the last TOPBAR_CLOCK_WIDTH columns. The bar only leaves that part empty.
 */

#include <string.h>

#include "topbar.h"
#include "dev/rtc.h"
#include "dev/txt_screen.h"

#define TOPBAR_SCREENS      2           /* screens with a bar: 0 and 1 */
#define TOPBAR_TEXT_MAX     160         /* the widest text screen: 1280 pixels / 8 */
#define TOPBAR_SEPARATOR    ((char)0xB3)    /* a vertical line in the code page 437 font */
#define TOPBAR_FILL_IDLE    ((char)0xB0)    /* the shaded fill of the screen that is not active */

/* What the bar of one screen shows */
typedef struct {
    char app[TOPBAR_PATH_MAX];          /* the APP text (a program without its extension) */
    char user[TOPBAR_TEXT_MAX + 1];     /* the USER text */
    unsigned char fg, bg;               /* the bar's colours, set by the last topbar_draw */
    char fill;                          /* ' ' on the active screen, TOPBAR_FILL_IDLE on the other */
    short drawn;                        /* 1 once topbar_draw has run, so the colours are known */
} t_topbar;

/* Where the parts are on a screen of a given width */
typedef struct {
    short width;                        /* the width of the screen (and the bar) */
    short app_width;                    /* the APP part: columns 0 .. app_width - 1 */
    short user_start;                   /* the USER part: columns user_start .. clock_sep - 1 */
    short user_width;                   /* 0 if the screen is too narrow for a USER part */
    short clock_sep;                    /* the separator left of the CLOCK part */
} t_layout;

/* What topbar_begin saved of the screen, for topbar_end */
typedef struct {
    t_rect region;
    t_point cursor;
    unsigned char fg, bg;
} t_saved;

static t_topbar topbar[TOPBAR_SCREENS];

/**
 * Work out where the parts of the bar are on a screen.
 *
 * @param screen the text screen
 * @param layout filled in
 */
static void topbar_layout(short screen, t_layout * layout) {
    t_extent size;

    size.width = 0;
    size.height = 0;
    txt_get_sizes(screen, &size, 0);

    layout->width = (size.width > TOPBAR_TEXT_MAX) ? TOPBAR_TEXT_MAX : size.width;
    layout->clock_sep = layout->width - TOPBAR_CLOCK_WIDTH - 1;
    if (layout->width >= TOPBAR_MIN_USER) {
        layout->app_width = TOPBAR_APP_WIDTH;
        layout->user_start = TOPBAR_APP_WIDTH + 1;              /* after the first separator */
        layout->user_width = layout->clock_sep - layout->user_start;    /* the width - 46 */
    } else {
        /* Too narrow: only APP and CLOCK */
        layout->app_width = (layout->clock_sep > 0) ? layout->clock_sep : 0;
        layout->user_start = layout->app_width;
        layout->user_width = 0;
    }
}

/**
 * Where the text of the USER part goes on a screen. See topbar.h.
 */
void topbar_user_columns(short screen, short * start, short * width) {
    t_layout layout;

    topbar_layout(screen, &layout);
    if (layout.user_width >= 3) {
        *start = layout.user_start + 1;                 /* inside its one-column margins */
        *width = layout.user_width - 2;
    } else {
        *start = 0;                                     /* no USER part: up to the clock */
        *width = (layout.clock_sep > 0) ? layout.clock_sep : 0;
    }
}

/**
 * Get ready to write in row 0: save the screen's region, cursor and colours, then switch to the
 * whole screen in the bar's colours.
 *
 * @param screen the text screen
 * @param saved filled in, for topbar_end
 */
static void topbar_begin(short screen, t_saved * saved) {
    t_rect full;

    txt_get_region(screen, &saved->region);
    txt_get_xy(screen, &saved->cursor);
    txt_get_color(screen, &saved->fg, &saved->bg);

    full.origin.x = 0;                  /* a region of size 0 is the whole screen */
    full.origin.y = 0;
    full.size.width = 0;
    full.size.height = 0;
    txt_set_region(screen, &full);
    txt_set_color(screen, topbar[screen].fg, topbar[screen].bg);
}

/**
 * Put back what topbar_begin saved.
 *
 * @param screen the text screen
 * @param saved what topbar_begin saved
 */
static void topbar_end(short screen, t_saved * saved) {
    txt_set_color(screen, saved->fg, saved->bg);
    txt_set_region(screen, &saved->region);
    txt_set_xy(screen, saved->cursor.x, saved->cursor.y);
}

/**
 * Write text into row 0, padded with the fill character to an exact width (between topbar_begin
 * and topbar_end).
 *
 * Control characters are written as spaces: txt_put would act on them (a newline would leave the
 * bar).
 *
 * @param screen the text screen
 * @param column the first column
 * @param width how many columns to write; text beyond that is left out
 * @param text the text (0 = only the fill)
 */
static void topbar_write(short screen, short column, short width, const char * text) {
    short i = 0;

    txt_set_xy(screen, column, 0);
    if (text != 0) {
        for (; (i < width) && (text[i] != 0); i++) {
            char c = text[i];
            txt_put(screen, ((unsigned char)c < 0x20) ? ' ' : c);
        }
    }
    for (; i < width; i++) {
        txt_put(screen, topbar[screen].fill);
    }
}

/**
 * Shorten a path to fit a number of columns: cut it on the left at a folder boundary and put ".."
 * in front, so the end of the path (the folder or the program name) stays visible.
 *   /sd/projects/irc/test/felinc_irc  ->  ../irc/test/felinc_irc
 * A single name that is too long keeps its last characters.
 *
 * @param path the path
 * @param room the number of columns
 * @param shown the result, at least room + 1 characters
 */
static void topbar_shorten(const char * path, short room, char * shown) {
    short len = (short)strlen(path);
    short i;

    if (len <= room) {
        strcpy(shown, path);
        return;
    }
    if (room < 3) {
        shown[0] = 0;
        return;
    }

    /* The first "/" from which the rest still fits after the ".." */
    for (i = len - room + 2; i < len; i++) {
        if (path[i] == '/') {
            break;
        }
    }
    if (i >= len) {
        i = len - room + 2;             /* no folder boundary: just the last characters */
    }
    shown[0] = '.';
    shown[1] = '.';
    strcpy(shown + 2, path + i);
}

/**
 * Draw the APP part (between topbar_begin and topbar_end).
 *
 * @param screen the text screen
 * @param layout where the parts are
 */
static void topbar_draw_app(short screen, const t_layout * layout) {
    char shown[TOPBAR_APP_WIDTH + 1];
    short room = layout->app_width - 2;     /* one column margin on both sides */

    if (room < 1) {
        return;
    }
    topbar_shorten(topbar[screen].app, room, shown);
    topbar_write(screen, 0, 1, 0);
    topbar_write(screen, 1, room, shown);
    topbar_write(screen, 1 + room, 1, 0);
}

/**
 * Draw the USER part (between topbar_begin and topbar_end).
 *
 * @param screen the text screen
 * @param layout where the parts are
 */
static void topbar_draw_user(short screen, const t_layout * layout) {
    if (layout->user_width < 3) {
        return;
    }
    topbar_write(screen, layout->user_start, 1, 0);
    topbar_write(screen, layout->user_start + 1, layout->user_width - 2, topbar[screen].user);
    topbar_write(screen, layout->user_start + layout->user_width - 1, 1, 0);
}

/**
 * Draw the whole bar of a screen. See topbar.h.
 */
void topbar_draw(short screen, short is_active) {
    t_topbar * bar;
    t_layout layout;
    t_saved saved;
    unsigned char fg, bg;
    char separator[2];

    if ((screen < 0) || (screen >= TOPBAR_SCREENS)) {
        return;
    }
    bar = &topbar[screen];

    /* The colours the command line had before 2.1.0: swapped on the active screen, normal and
       shaded on the other one */
    txt_get_color(screen, &fg, &bg);
    bar->fg = is_active ? bg : fg;
    bar->bg = is_active ? fg : bg;
    bar->fill = is_active ? ' ' : TOPBAR_FILL_IDLE;
    bar->drawn = 1;

    topbar_layout(screen, &layout);
    rtc_set_bar_width(screen, layout.width);    /* 2.1.0: where the RTC interrupt puts the network's character */
    if (layout.width < TOPBAR_CLOCK_WIDTH + 2) {
        return;
    }

    separator[0] = TOPBAR_SEPARATOR;
    separator[1] = 0;

    topbar_begin(screen, &saved);
    topbar_draw_app(screen, &layout);
    if (layout.user_width > 0) {
        topbar_write(screen, layout.app_width, 1, separator);
        topbar_draw_user(screen, &layout);
    }
    topbar_write(screen, layout.clock_sep, 1, separator);
    /* The clock part: empty; the RTC interrupt puts the time back at the next second, and the
       network's character (2.1.0) in its first column at the next interrupt */
    topbar_write(screen, layout.clock_sep + 1, TOPBAR_CLOCK_WIDTH, 0);
    topbar_end(screen, &saved);
}

/**
 * Set the text of the APP part and show it. See topbar.h.
 */
void topbar_set_app(short screen, const char * text, short is_program) {
    t_topbar * bar;
    char * dot;
    char * slash;

    if ((screen < 0) || (screen >= TOPBAR_SCREENS)) {
        return;
    }
    bar = &topbar[screen];

    strncpy(bar->app, (text != 0) ? text : "", TOPBAR_PATH_MAX - 1);
    bar->app[TOPBAR_PATH_MAX - 1] = 0;

    if (is_program) {
        /* Leave out the extension: everything that runs is a program (.pgz, .pgx, .elf) */
        dot = strrchr(bar->app, '.');
        slash = strrchr(bar->app, '/');
        if ((dot != 0) && ((slash == 0) || (dot > slash + 1))) {
            *dot = 0;
        }
    }

    if (bar->drawn) {
        t_layout layout;
        t_saved saved;

        topbar_layout(screen, &layout);
        topbar_begin(screen, &saved);
        topbar_draw_app(screen, &layout);
        topbar_end(screen, &saved);
    }
}

/**
 * Copy the text of the APP part. See topbar.h.
 */
void topbar_get_app(short screen, char * buffer) {
    if ((screen < 0) || (screen >= TOPBAR_SCREENS)) {
        buffer[0] = 0;
        return;
    }
    strcpy(buffer, topbar[screen].app);
}

/**
 * Set the text of the USER part and show it. See topbar.h.
 */
short topbar_set_user(short screen, const char * text) {
    t_topbar * bar;
    t_layout layout;

    if ((screen < 0) || (screen >= TOPBAR_SCREENS)) {
        return -1;
    }
    bar = &topbar[screen];

    strncpy(bar->user, (text != 0) ? text : "", TOPBAR_TEXT_MAX);
    bar->user[TOPBAR_TEXT_MAX] = 0;

    topbar_layout(screen, &layout);
    if (bar->drawn) {
        t_saved saved;

        topbar_begin(screen, &saved);
        topbar_draw_user(screen, &layout);
        topbar_end(screen, &saved);
    }

    return (layout.user_width >= 3) ? (short)(layout.user_width - 2) : 0;
}

/**
 * txt_set_region for programs: their row 0 is the screen's row 1. See topbar.h.
 *
 * The region is also kept inside the rows below the bar, so a program cannot make the screen
 * scroll the bar away.
 */
short topbar_prog_set_region(short screen, p_rect region) {
    t_extent size;
    t_rect r;
    short rows;

    size.width = 0;
    size.height = 0;
    txt_get_sizes(screen, &size, 0);
    rows = size.height - 1;             /* the rows below the bar */

    if ((region->size.width == 0) || (region->size.height == 0)) {
        /* "The whole screen": everything below the bar */
        r.origin.x = 0;
        r.origin.y = 1;
        r.size.width = size.width;
        r.size.height = rows;

    } else {
        r = *region;                    /* a copy: the program's own record stays as it was */
        if (r.origin.y < 0) {
            r.origin.y = 0;
        } else if (r.origin.y >= rows) {
            r.origin.y = rows - 1;
        }
        if (r.origin.y + r.size.height > rows) {
            r.size.height = rows - r.origin.y;
        }
        r.origin.y++;                   /* below the bar */
    }

    return txt_set_region(screen, &r);
}

/**
 * txt_get_region for programs: their row 0 is the screen's row 1. See topbar.h.
 */
short topbar_prog_get_region(short screen, p_rect region) {
    short result = txt_get_region(screen, region);
    if (result == 0) {
        if (region->origin.y > 0) {
            region->origin.y--;
        } else if (region->size.height > 1) {
            /* A region the MCP set over the whole screen: the program sees the part below the bar */
            region->size.height--;
        }
    }
    return result;
}

/**
 * txt_get_sizes for programs: the screen is one row shorter. See topbar.h.
 */
void topbar_prog_get_sizes(short screen, p_extent text_size, p_extent pixel_size) {
    txt_get_sizes(screen, text_size, pixel_size);
    if ((text_size != 0) && (text_size->height > 0)) {
        text_size->height--;
    }
}
