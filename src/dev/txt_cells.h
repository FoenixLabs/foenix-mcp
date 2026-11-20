/**
 * @file txt_cells.h
 *
 * The inner loops of the text screen drivers: copy or set a run of character cells in one row,
 * in the text memory and the colour memory at the same time.
 *
 * Scrolling copies the whole screen for every printed line, so these loops decide how fast the
 * console is. They are in a file of their own on purpose: inside the big scroll function Calypsi
 * ran out of registers and kept the loop values on the stack (printing was 1.7 times slower than
 * with VBCC), and it inlines a small function of the same file back into its caller. A function
 * in another file cannot be inlined, so here the loops always run in registers.
 */

#ifndef __TXT_CELLS_H
#define __TXT_CELLS_H

/**
 * Copy a run of cells in one row, text and colour; used for scrolling.
 *
 * @param text the first cell to write in the text memory
 * @param color the same cell in the colour memory
 * @param distance how far the cell to copy from is (the same for every cell of the run)
 * @param count how many cells
 * @param step 1 to go right, -1 to go left (so that cells are read before they are overwritten)
 */
extern void txt_cells_copy(volatile unsigned char * text, volatile unsigned char * color, int distance, int count, int step);

/**
 * Set a run of cells in one row to one character in one colour.
 *
 * @param text the first cell in the text memory
 * @param color the same cell in the colour memory
 * @param c the character
 * @param attr the colour byte (foreground and background)
 * @param count how many cells
 * @param step 1 to go right, -1 to go left
 */
extern void txt_cells_set(volatile unsigned char * text, volatile unsigned char * color, char c, unsigned char attr, int count, int step);

#endif
