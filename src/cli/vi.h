/*
 * VI: a very small vi-style text editor for the MCP command line (2.0.34)
 */

#ifndef __VI_H
#define __VI_H

/*
 * VI <path>: edit a text file
 *
 * Inputs:
 * screen = the console channel
 * argc, argv = the command line
 *
 * Returns:
 * 0 on success, a negative error number if the file could not be read
 */
extern short cmd_vi(short screen, int argc, const char * argv[]);

#endif
