/**
 * @file txt_cells.c
 *
 * The inner loops of the text screen drivers (see txt_cells.h for why they are in a file of
 * their own).
 */

#include "txt_cells.h"

/*
 * txt_cells_copy - copy a run of cells in one row, text and colour (for scrolling)
 *
 * Input:  text, color = the first cell to write in the text and the colour memory
 *         distance = how far the cell to copy from is; count = how many cells
 *         step = 1 to go right, -1 to go left
 * Output: none
 */
void txt_cells_copy(volatile unsigned char * text, volatile unsigned char * color, int distance, int count, int step) {
    for (; count > 0; count--) {
        *text = text[distance];
        *color = color[distance];
        text += step;
        color += step;
    }
}

/*
 * txt_cells_set - set a run of cells in one row to one character in one colour
 *
 * Input:  text, color = the first cell in the text and the colour memory
 *         c = the character, attr = the colour byte, count = how many cells
 *         step = 1 to go right, -1 to go left
 * Output: none
 */
void txt_cells_set(volatile unsigned char * text, volatile unsigned char * color, char c, unsigned char attr, int count, int step) {
    for (; count > 0; count--) {
        *text = c;
        *color = attr;
        text += step;
        color += step;
    }
}
