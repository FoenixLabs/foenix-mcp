/*
 * mcp/syscalls.h - calling the Foenix/MCP from a Calypsi program (MCP 2.1.0, A2560K)
 *
 * Part of the MCP's Calypsi SDK (sdk/calypsi in the foenix-mcp repository). Put its include
 * folder before Calypsi's own (-I <sdk>/include) and link the library that matches the program's
 * core and models (lib/mcp_syscalls-<core>-<code>-<data>.a, see README.md) or its source
 * (src/mcp_syscalls.c) with the program: it replaces the mcp/ headers and the system call
 * functions in Calypsi's contrib/A2560K-SDK, which are older than MCP 2.1.0.
 *
 * The function numbers (KFN_...) are the MCP's own (src/include/syscalls.h); tools/check_sdk.py
 * compares them when the MCP is built. The function names are the MCP's, as in the MCP manual.
 * Programs written for Calypsi's SDK still build: its other names are here too, marked "older
 * name" (sys_fsys_getcwd, sys_bdev_getblock, sys_txt_get_caps, ...).
 *
 * The network (KFN_NET_..., 0x80-0x8F) is called through FELINC's felinc_mcp.a and felinc.h,
 * not through this file.
 */

#ifndef __MCP_SYSCALLS_H
#define __MCP_SYSCALLS_H

#include "types.h"
#include "errors.h"
#include "interrupt.h"

/* The SDK's version: the MCP version it belongs to */
#define MCP_SDK_VERSION_MAJOR   2
#define MCP_SDK_VERSION_MINOR   2
#define MCP_SDK_VERSION_BUILD   0

/*
 * System call function numbers
 */

/* Core calls */
#define KFN_EXIT                0x00    /* End the program and go back to the command line */
#define KFN_WARMBOOT            0x01    /* Soft re-initialisation (does nothing in 2.1.0) */
#define KFN_INT_REGISTER        0x02    /* Set a handler for an interrupt */
#define KFN_INT_ENABLE          0x03    /* Enable an interrupt */
#define KFN_INT_DISABLE         0x04    /* Disable an interrupt */
#define KFN_INT_ENABLE_ALL      0x05    /* Enable all interrupts */
#define KFN_INT_DISABLE_ALL     0x06    /* Disable all interrupts */
#define KFN_INT_CLEAR           0x07    /* Clear (acknowledge) an interrupt */
#define KFN_INT_PENDING         0x08    /* Is the interrupt pending? */
#define KFN_SYS_GET_INFO        0x09    /* Describe the machine */

/* Channels */
#define KFN_CHAN_READ           0x10    /* Read bytes from a channel */
#define KFN_CHAN_READ_B         0x11    /* Read a byte from a channel */
#define KFN_CHAN_READ_LINE      0x12    /* Read a line from a channel */
#define KFN_CHAN_WRITE          0x13    /* Write bytes to a channel */
#define KFN_CHAN_WRITE_B        0x14    /* Write a byte to a channel */
#define KFN_CHAN_FLUSH          0x15    /* Finish pending writes on a channel */
#define KFN_CHAN_SEEK           0x16    /* Move the position of a channel */
#define KFN_CHAN_STATUS         0x17    /* The status of a channel */
#define KFN_CHAN_IOCTRL         0x18    /* A device-specific command */
#define KFN_CHAN_REGISTER       0x19    /* Register a channel device driver */
#define KFN_CHAN_OPEN           0x1A    /* Open a channel device */
#define KFN_CHAN_CLOSE          0x1B    /* Close a channel (not for files) */
#define KFN_CHAN_SWAP           0x1C    /* Swap the numbers of two channels */
#define KFN_CHAN_DEVICE         0x1D    /* The device number of a channel */

/* Block devices */
#define KFN_BDEV_GETBLOCK       0x20    /* Read a block */
#define KFN_BDEV_PUTBLOCK       0x21    /* Write a block */
#define KFN_BDEV_FLUSH          0x22    /* Finish pending writes */
#define KFN_BDEV_STATUS         0x23    /* The status of a block device */
#define KFN_BDEV_IOCTRL         0x24    /* A device-specific command */
#define KFN_BDEV_REGISTER       0x25    /* Register a block device driver */
#define KFN_STAT                0x2F    /* Does a file exist, and its information */

/* Files and folders */
#define KFN_OPEN                0x30    /* Open a file as a channel */
#define KFN_CLOSE               0x31    /* Close a file channel */
#define KFN_OPENDIR             0x32    /* Open a folder for reading */
#define KFN_CLOSEDIR            0x33    /* Close a folder */
#define KFN_READDIR             0x34    /* Read the next entry of a folder */
#define KFN_FINDFIRST           0x35    /* Find the first entry matching a pattern */
#define KFN_FINDNEXT            0x36    /* Find the next entry matching the pattern */
#define KFN_DELETE              0x37    /* Delete a file or folder */
#define KFN_RENAME              0x38    /* Rename a file or folder */
#define KFN_MKDIR               0x39    /* Create a folder */
#define KFN_LOAD                0x3A    /* Load a file into memory */
#define KFN_GET_LABEL           0x3B    /* Read a volume's label */
#define KFN_SET_LABEL           0x3C    /* Set a volume's label */
#define KFN_SET_CWD             0x3D    /* Set the current folder */
#define KFN_GET_CWD             0x3E    /* Get the current folder */
#define KFN_LOAD_REGISTER       0x3F    /* Register a loader for a file type */

/* Programs and memory */
#define KFN_RUN                 0x40    /* Load and run a program */
#define KFN_MEM_GET_RAMTOP      0x41    /* The top of the RAM programs may use */
#define KFN_MEM_RESERVE         0x42    /* Reserve memory at the top of RAM */
#define KFN_ELEVATE             0x43    /* Go to supervisor mode (2.1.0: refused, ERR_NOT_SUPPORTED) */
#define KFN_VAR_SET             0x44    /* Set a system variable */
#define KFN_VAR_GET             0x45    /* Get a system variable */

/* Other calls */
#define KFN_TIME_JIFFIES        0x50    /* Ticks since the start */
#define KFN_TIME_SETRTC         0x51    /* Set the real-time clock */
#define KFN_TIME_GETRTC         0x52    /* Read the real-time clock */
#define KFN_KBD_SCANCODE        0x53    /* The next scan code from the keyboard */
#define KFN_KBD_LAYOUT          0x54    /* Set the keyboard translation tables */
#define KFN_ERR_MESSAGE         0x55    /* The text of an error number */

/* Text screens */
#define KFN_TEXT_INIT_SCREEN    0x60    /* Reset a screen to its default mode */
#define KFN_TXT_GET_CAPS        0x61    /* What a screen can do */
#define KFN_TXT_SET_MODE        0x62    /* Set a screen's display mode */
#define KFN_TEXT_SETSIZES       0x63    /* Recompute the text size from the display mode */
#define KFN_TXT_SET_RESOLUTION  0x64    /* Set a screen's resolution */
#define KFN_TXT_SET_BORDER      0x65    /* Set the size of the border */
#define KFN_TXT_SET_BORDERCOLOR 0x66    /* Set the colour of the border */
#define KFN_TXT_SET_FONT        0x67    /* Load a font */
#define KFN_TXT_SET_CURSOR      0x68    /* Set the look of the cursor */
#define KFN_TXT_SET_REGION      0x69    /* Set the region for text and scrolling */
#define KFN_TXT_GET_REGION      0x6A    /* Get that region */
#define KFN_TXT_SET_COLOR       0x6B    /* Set the text colours */
#define KFN_TXT_GET_COLOR       0x6C    /* Get the text colours */
#define KFN_TXT_SET_XY          0x6D    /* Set the cursor position */
#define KFN_TXT_GET_XY          0x6E    /* Get the cursor position */
#define KFN_TXT_SCROLL          0x6F    /* Scroll the region */
#define KFN_TXT_SET_CURSOR_VIS  0x71    /* Show or hide the cursor */
#define KFN_TXT_GET_SIZES       0x72    /* The size of a screen in characters and pixels */
#define KFN_TXT_TOPBAR          0x73    /* 2.1.0: the program's text in the top bar */

/* The network: FELINC in the MCP (2.1.0). Use felinc_mcp.a and felinc.h for these. */
#define KFN_NET_INIT            0x80
#define KFN_NET_SHUTDOWN        0x81
#define KFN_NET_POLL            0x82
#define KFN_NET_SOCKET          0x83
#define KFN_NET_BIND            0x84
#define KFN_NET_CLOSE           0x85
#define KFN_NET_RECVFROM        0x86
#define KFN_NET_SENDTO          0x87
#define KFN_NET_LISTEN          0x88
#define KFN_NET_ACCEPT          0x89
#define KFN_NET_CONNECT         0x8A
#define KFN_NET_RECV            0x8B
#define KFN_NET_SEND            0x8C
#define KFN_NET_RESOLVE         0x8D
#define KFN_NET_INFO            0x8E
#define KFN_NET_PRINT           0x8F

/* Older names of some numbers (Calypsi's SDK) */
#define KFN_PROC_RUN            KFN_RUN
#define KFN_PROC_ELEVATE        KFN_ELEVATE
#define KFN_TXT_INIT_SCREEN     KFN_TEXT_INIT_SCREEN
#define KFN_TXT_SETSIZES        KFN_TEXT_SETSIZES
#define KFN_TXT_SET_BORDER_COLOR KFN_TXT_SET_BORDERCOLOR

/*
 * Call the MCP: function number in d0, the six arguments in d1-d6, then TRAP #15.
 * Provided by Calypsi's A2560K library (syscall.s).
 *
 * Input:  function = a KFN_ number; params = its arguments (unused ones may be anything)
 * Output: the MCP's result (d0)
 */
extern int32_t syscall(int32_t function, p_syscall_params params);

/***
 *** Core calls
 ***/

/* End the program and go back to the command line. Input: result = the program's result. */
extern void sys_exit(short result);

/* Enable all interrupts (a program runs in user mode: use this, not __enable_interrupts). */
extern void sys_int_enable_all(void);

/* Disable all interrupts. */
extern void sys_int_disable_all(void);

/* Disable (mask) one interrupt. Input: n = INT_ number. */
extern void sys_int_disable(unsigned short n);

/* Enable one interrupt. Input: n = INT_ number. */
extern void sys_int_enable(unsigned short n);

/*
 * Set the handler of an interrupt.
 * Input:  n = INT_ number; handler = the new handler
 * Output: the previous handler
 */
extern p_int_handler sys_int_register(unsigned short n, p_int_handler handler);

/* Is an interrupt pending? Input: n = INT_ number. Output: non-zero if it is. */
extern short sys_int_pending(unsigned short n);

/* Clear (acknowledge) an interrupt. Input: n = INT_ number. */
extern void sys_int_clear(unsigned short n);

/* Describe the machine. Input: info = the record to fill in. */
extern void sys_get_info(p_sys_info info);

/***
 *** Channels
 ***/

/* Read one byte. Input: channel. Output: the byte, or a negative error. */
extern short sys_chan_read_b(short channel);

/* Read bytes. Input: channel, buffer, its size. Output: bytes read, or a negative error. */
extern short sys_chan_read(short channel, unsigned char * buffer, short size);

/* Read a line. Input: channel, buffer, its size. Output: bytes read, or a negative error. */
extern short sys_chan_readline(short channel, unsigned char * buffer, short size);

/* Write one byte. Input: channel, b. Output: 0, or a negative error. */
extern short sys_chan_write_b(short channel, unsigned char b);

/* Write bytes. Input: channel, buffer, size. Output: bytes written, or a negative error. */
extern short sys_chan_write(short channel, const unsigned char * buffer, short size);

/* The status of a channel. Input: channel. Output: CDEV_STAT_ bits, or a negative error. */
extern short sys_chan_status(short channel);

/* Finish pending writes. Input: channel. Output: 0, or a negative error. */
extern short sys_chan_flush(short channel);

/*
 * Move the position of a channel (a file).
 * Input:  channel; position; base = CDEV_SEEK_START, CDEV_SEEK_RELATIVE or CDEV_SEEK_END
 * Output: 0, or a negative error
 */
extern short sys_chan_seek(short channel, long position, short base);

/* A device-specific command. Input: channel, command, buffer, size. Output: 0 or an error. */
extern short sys_chan_ioctrl(short channel, short command, uint8_t * buffer, short size);

/* Register a channel device driver. Input: device. Output: 0, or a negative error. */
extern short sys_chan_register(p_dev_chan device);

/*
 * Open a channel device.
 * Input:  dev = CDEV_ number; path = how to open it (device-specific); mode = 1 read, 2 write, 3 both
 * Output: the channel number, or a negative error
 */
extern short sys_chan_open(short dev, const uint8_t * path, short mode);

/* Close a channel (files: sys_fsys_close). Input: chan. Output: 0, or a negative error. */
extern short sys_chan_close(short chan);

/* Swap the numbers of two channels. Input: channel1, channel2. Output: 0, or an error. */
extern short sys_chan_swap(short channel1, short channel2);

/* The device of a channel. Input: channel. Output: the CDEV_ number, or a negative error. */
extern short sys_chan_device(short channel);

/***
 *** Block devices
 ***/

/* Register a block device driver. Input: device. Output: 0, or a negative error. */
extern short sys_bdev_register(p_dev_block device);

/*
 * Read a block.
 * Input:  dev = BDEV_ number; lba = block number; buffer; size = its size
 * Output: bytes read, or a negative error
 */
extern short sys_bdev_read(short dev, long lba, unsigned char * buffer, short size);

/*
 * Write a block.
 * Input:  dev = BDEV_ number; lba = block number; buffer; size = its size
 * Output: bytes written, or a negative error
 */
extern short sys_bdev_write(short dev, long lba, const unsigned char * buffer, short size);

/* The status of a block device. Input: dev. Output: the status, or a negative error. */
extern short sys_bdev_status(short dev);

/* Finish pending writes. Input: dev. Output: 0, or a negative error. */
extern short sys_bdev_flush(short dev);

/* A device-specific command. Input: dev, command, buffer, size. Output: 0 or an error. */
extern short sys_bdev_ioctrl(short dev, short command, unsigned char * buffer, short size);

/***
 *** Files and folders
 ***/

/*
 * Open a file.
 * Input:  path; mode = FA_ bits (FA_READ, FA_WRITE | FA_CREATE_ALWAYS, ...)
 * Output: the channel number (read and write it with sys_chan_...), or a negative error
 */
extern short sys_fsys_open(const char * path, short mode);

/* Close a file. Input: fd = its channel. Output: 0, or a negative error. */
extern short sys_fsys_close(short fd);

/* Open a folder for reading. Input: path. Output: a folder handle, or a negative error. */
extern short sys_fsys_opendir(const char * path);

/* Close a folder. Input: dir = its handle. Output: 0, or a negative error. */
extern short sys_fsys_closedir(short dir);

/*
 * Read the next entry of a folder.
 * Input:  dir = the folder handle; file = the record to fill in
 * Output: 0 (file->name is "" after the last entry), or a negative error
 */
extern short sys_fsys_readdir(short dir, p_file_info file);

/*
 * Find the first entry of a folder that matches a pattern.
 * Input:  path = the folder; pattern = e.g. "*.pgz"; file = the record to fill in
 * Output: a folder handle for sys_fsys_findnext and sys_fsys_closedir, or a negative error
 */
extern short sys_fsys_findfirst(const char * path, const char * pattern, p_file_info file);

/* Find the next matching entry. Input: dir, file. Output: 0, or a negative error. */
extern short sys_fsys_findnext(short dir, p_file_info file);

/* Read a volume's label. Input: path on the drive, label (at least 35 bytes). Output: 0 or error. */
extern short sys_fsys_get_label(const char * path, char * label);

/* Set a volume's label. Input: drive number, label. Output: 0, or a negative error. */
extern short sys_fsys_set_label(short drive, const char * label);

/* Create a folder. Input: path. Output: 0, or a negative error. */
extern short sys_fsys_mkdir(const char * path);

/* Delete a file or (empty) folder. Input: path. Output: 0, or a negative error. */
extern short sys_fsys_delete(const char * path);

/* Rename or move a file or folder. Input: old_path, new_path. Output: 0, or a negative error. */
extern short sys_fsys_rename(const char * old_path, const char * new_path);

/* Set the current folder (and drive). Input: path. Output: 0, or a negative error. */
extern short sys_fsys_set_cwd(const char * path);

/* Get the current folder. Input: path = buffer, size = its size. Output: 0, or a negative error. */
extern short sys_fsys_get_cwd(char * path, short size);

/*
 * Load a file into memory.
 * Input:  path; destination = address, or 0 for the address the file gives (PGZ, PGX, ...);
 *         start = filled in with the start address (0 if the file is not a program)
 * Output: 0, or a negative error
 */
extern short sys_fsys_load(const char * path, long destination, long * start);

/* Register a loader for an extension. Input: extension, loader. Output: 0 or an error. */
extern short sys_fsys_register_loader(const char * extension, p_file_loader loader);

/*
 * Does a file or folder exist, and what is it?
 * Input:  path; file = the record to fill in
 * Output: 0 if it exists, FSYS_ERR_NO_FILE (or another negative error) if not
 */
extern short sys_fsys_stat(const char * path, p_file_info file);

/***
 *** Programs and memory
 ***/

/*
 * Load and run a program.
 * Input:  path; argc and argv = its arguments (argv[0] is the program's name)
 * Output: the program's result, or a negative error
 */
extern short sys_proc_run(const char * path, int argc, char * argv[]);

/* 2.1.0: refused, the result is ERR_NOT_SUPPORTED (it crashed the program before). */
extern short sys_proc_elevate(void);

/* The top of the RAM a program may use (one past the last byte). */
extern unsigned long sys_mem_get_ramtop(void);

/* Reserve memory at the top of RAM. Input: bytes. Output: the address of the block. */
extern unsigned long sys_mem_reserve(unsigned long bytes);

/* Set a system variable. Input: name, value. Output: 0, or a negative error. */
extern short sys_var_set(const char * name, const char * value);

/* Get a system variable. Input: name. Output: its value, or 0 if there is none. */
extern const char * sys_var_get(const char * name);

/***
 *** Other calls
 ***/

/* Ticks since the start (60 per second; for time-outs, not for exact timing). */
extern long sys_time_jiffies(void);

/* Set the real-time clock. Input: time. */
extern void sys_time_setrtc(p_time time);

/* Read the real-time clock. Input: time = the record to fill in. */
extern void sys_time_getrtc(p_time time);

/* The next scan code from the keyboard; 0 if there is none. */
extern unsigned short sys_kbd_scancode(void);

/*
 * Set the keyboard translation tables: eight tables of 128 characters (plain, Shift, Ctrl,
 * Ctrl+Shift, CapsLock, CapsLock+Shift, Alt, Alt+Shift).
 * Input:  tables
 * Output: 0, or a negative error
 */
extern short sys_kbd_layout(const char * tables);

/* The text of an error number. Input: err_number. Output: the text. */
extern const char * sys_err_message(short err_number);

/***
 *** Text screens (screen 0 = channel A, 1 = channel B)
 ***
 *** 2.1.0: row 0 of a screen is the MCP's top bar. The region and sizes a program gets and sets
 *** leave it out, so (0, 0) is the first row below the bar.
 ***/

/* Reset a screen to its default mode. Input: screen. */
extern void sys_txt_init_screen(short screen);

/* What a screen can do. Input: screen. Output: a read-only description, or 0. */
extern const p_txt_capabilities sys_txt_get_capabilities(short screen);

/* Set the display mode. Input: screen, mode = TXT_MODE_ bits. Output: 0, or an error. */
extern short sys_txt_set_mode(short screen, short mode);

/* Recompute the text size from the display mode. Input: screen. */
extern void sys_txt_setsizes(short screen);

/* Set the resolution. Input: screen, width, height (pixels). Output: 0, or an error. */
extern short sys_txt_set_resolution(short screen, short width, short height);

/* Set the border size. Input: screen, width, height (pixels per side, 0-32). */
extern void sys_txt_set_border(short screen, short width, short height);

/* Set the border colour. Input: screen, red, green, blue (0-255). */
extern void sys_txt_set_border_color(short screen, unsigned char red, unsigned char green, unsigned char blue);

/* Load a font. Input: screen, width, height (pixels), data. Output: 0, or an error. */
extern short sys_txt_set_font(short screen, short width, short height, unsigned char * data);

/*
 * Set the look of the cursor.
 * Input: screen; enable = show it; rate = blink rate; character = its character
 */
extern void sys_txt_set_cursor(short screen, short enable, short rate, char character);

/* Show or hide the cursor. Input: screen, is_visible. */
extern void sys_txt_set_cursor_visible(short screen, short is_visible);

/*
 * Set the region for text and scrolling (in characters). A region of size 0 is the whole screen.
 * Input:  screen, region
 * Output: 0, or an error
 */
extern short sys_txt_set_region(short screen, p_rect region);

/* Get the region. Input: screen, region = the record to fill in. Output: 0, or an error. */
extern short sys_txt_get_region(short screen, p_rect region);

/* Set the text colours. Input: screen, foreground, background (0-15). Output: 0 or an error. */
extern short sys_txt_set_color(short screen, unsigned char foreground, unsigned char background);

/* Get the text colours. Input: screen, foreground and background = filled in. */
extern void sys_txt_get_color(short screen, unsigned char * foreground, unsigned char * background);

/* Set the cursor position in the region. Input: screen, x, y. */
extern void sys_txt_set_xy(short screen, short x, short y);

/* Get the cursor position in the region. Input: screen, position = filled in. */
extern void sys_txt_get_xy(short screen, p_point position);

/* Scroll the region. Input: screen, horizontal, vertical (characters). */
extern void sys_txt_scroll(short screen, short horizontal, short vertical);

/*
 * The size of a screen.
 * Input: screen; text_size = filled in with the visible characters, pixel_size = with the
 *        pixels (either may be 0)
 */
extern void sys_txt_get_sizes(short screen, p_extent text_size, p_extent pixel_size);

/*
 * 2.1.0: put the program's text in the middle part of the screen's top bar. The MCP clears it
 * when the program ends; text that does not fit is cut off.
 * Input:  screen; text (0 or "" clears it)
 * Output: how many characters fit, or a negative error for a screen that does not exist
 */
extern short sys_txt_topbar(short screen, const char * text);

/***
 *** Older names (Calypsi's SDK and older MCP headers): the same calls as above
 ***/

extern short sys_fsys_getcwd(char * path, short size);              /* sys_fsys_get_cwd */
extern short sys_fsys_setcwd(const char * path);                    /* sys_fsys_set_cwd */
extern short sys_bdev_getblock(short dev, long lba, unsigned char * buffer, short size);   /* sys_bdev_read */
extern short sys_bdev_putblock(short dev, long lba, const unsigned char * buffer, short size); /* sys_bdev_write */
extern short sys_kbd_setlayout(const char * tables);               /* sys_kbd_layout */
extern const p_txt_capabilities sys_txt_get_caps(short screen);     /* sys_txt_get_capabilities */
extern void sys_txt_set_cursor_vis(short screen, short is_visible); /* sys_txt_set_cursor_visible */
extern void sys_text_setsizes(short screen);                        /* sys_txt_setsizes */
extern void sys_rtc_set_time(p_time time);                          /* sys_time_setrtc */
extern void sys_rtc_get_time(p_time time);                          /* sys_time_getrtc */

#endif
