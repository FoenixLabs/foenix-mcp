/**
 * @file topbar.h
 *
 * 2.1.0: the top bar of the text screens (row 0).
 *
 * Row 0 of every text screen belongs to the MCP. It has three parts with a separator between them:
 *
 *   | APP (32 columns)               | USER (the rest)                   | CLOCK (12) |
 *
 * APP shows the current folder at the command line and the path of the running program. USER is
 * free for the running program (sys_txt_topbar). CLOCK holds the time when SET CLOCK is on. On a
 * screen narrower than 60 columns there is no USER part.
 *
 * Programs do not see row 0: the text system calls make their row 0 the screen's row 1 (see
 * topbar_prog_set_region and friends). A program that writes into the text memory itself can still
 * overwrite the bar.
 */

#ifndef __TOPBAR_H
#define __TOPBAR_H

#include "types.h"

#define TOPBAR_APP_WIDTH    32      /* the APP part, including its margins */
#define TOPBAR_CLOCK_WIDTH  12      /* the CLOCK part: room for " hh:mm:ss AM" */
#define TOPBAR_MIN_USER     60      /* below this screen width there is no USER part */
#define TOPBAR_PATH_MAX     256     /* the longest APP text kept (it is shortened when shown) */

/**
 * Draw the whole bar of a screen, in the colours the screen has now (swapped on the active screen).
 *
 * @param screen the text screen (0 or 1)
 * @param is_active 1 for the screen the command line works on, 0 for the other one
 */
extern void topbar_draw(short screen, short is_active);

/**
 * Set the text of the APP part and show it.
 *
 * @param screen the text screen (0 or 1)
 * @param text a folder or program path; a long one is cut on the left at a folder boundary
 * @param is_program 1 to leave out the file extension (for a program), 0 to show the text as is
 */
extern void topbar_set_app(short screen, const char * text, short is_program);

/**
 * Copy the text of the APP part (before shortening), to put it back later with topbar_set_app.
 *
 * @param screen the text screen (0 or 1)
 * @param buffer at least TOPBAR_PATH_MAX characters
 */
extern void topbar_get_app(short screen, char * buffer);

/**
 * Set the text of the USER part and show it (the sys_txt_topbar system call).
 *
 * @param screen the text screen (0 or 1)
 * @param text the text; 0 or "" clears the part. Text that does not fit is cut off on the right.
 * @return how many characters fit in the USER part of this screen (0 if it has none), or -1 for a
 *         screen that does not exist
 */
extern short topbar_set_user(short screen, const char * text);

/**
 * The text system calls as programs see them: the same as txt_set_region, txt_get_region and
 * txt_get_sizes, but without row 0. The program's row 0 is the screen's row 1, the screen is one
 * row shorter, and a region of size 0 means "everything below the bar".
 */
extern short topbar_prog_set_region(short screen, p_rect region);
extern short topbar_prog_get_region(short screen, p_rect region);
extern void topbar_prog_get_sizes(short screen, p_extent text_size, p_extent pixel_size);

#endif
