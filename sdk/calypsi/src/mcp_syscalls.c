/*
 * mcp_syscalls.c - the Foenix/MCP system calls for Calypsi programs (MCP 2.1.0, A2560K)
 *
 * Part of the MCP's Calypsi SDK (sdk/calypsi). Each function puts its arguments in a
 * t_syscall_params record and calls syscall() (Calypsi's A2560K library), which loads them into
 * d1-d6 and the function number into d0, and does a TRAP #15. What each call does, its inputs and
 * its output are described in include/mcp/syscalls.h.
 *
 * All functions are in this one file on purpose: one object file, so a program that uses any of
 * them gets all of them from here, and the linker never needs the older copies in Calypsi's own
 * A2560K library (which would clash with these).
 */

#include <mcp/syscalls.h>

/* End the program (KFN_EXIT) */
void sys_exit(short result) {
    t_syscall_params params = {.arg1 = (int32_t) result};
    syscall(KFN_EXIT, &params);
}

/* Enable all interrupts (KFN_INT_ENABLE_ALL) */
void sys_int_enable_all(void) {
    t_syscall_params params = {0};
    syscall(KFN_INT_ENABLE_ALL, &params);
}

/* Disable all interrupts (KFN_INT_DISABLE_ALL) */
void sys_int_disable_all(void) {
    t_syscall_params params = {0};
    syscall(KFN_INT_DISABLE_ALL, &params);
}

/* Disable one interrupt (KFN_INT_DISABLE) */
void sys_int_disable(unsigned short n) {
    t_syscall_params params = {.arg1 = (int32_t) n};
    syscall(KFN_INT_DISABLE, &params);
}

/* Enable one interrupt (KFN_INT_ENABLE) */
void sys_int_enable(unsigned short n) {
    t_syscall_params params = {.arg1 = (int32_t) n};
    syscall(KFN_INT_ENABLE, &params);
}

/* Set an interrupt handler (KFN_INT_REGISTER) */
p_int_handler sys_int_register(unsigned short n, p_int_handler handler) {
    t_syscall_params params = {.arg1 = (int32_t) n, .arg2 = (int32_t) handler};
    return (p_int_handler) syscall(KFN_INT_REGISTER, &params);
}

/* Is an interrupt pending? (KFN_INT_PENDING) */
short sys_int_pending(unsigned short n) {
    t_syscall_params params = {.arg1 = (int32_t) n};
    return (short) syscall(KFN_INT_PENDING, &params);
}

/* Clear an interrupt (KFN_INT_CLEAR) */
void sys_int_clear(unsigned short n) {
    t_syscall_params params = {.arg1 = (int32_t) n};
    syscall(KFN_INT_CLEAR, &params);
}

/* Describe the machine (KFN_SYS_GET_INFO) */
void sys_get_info(p_sys_info info) {
    t_syscall_params params = {.arg1 = (int32_t) info};
    syscall(KFN_SYS_GET_INFO, &params);
}

/* Read one byte from a channel (KFN_CHAN_READ_B) */
short sys_chan_read_b(short channel) {
    t_syscall_params params = {.arg1 = (int32_t) channel};
    return (short) syscall(KFN_CHAN_READ_B, &params);
}

/* Read bytes from a channel (KFN_CHAN_READ) */
short sys_chan_read(short channel, unsigned char * buffer, short size) {
    t_syscall_params params = {.arg1 = (int32_t) channel, .arg2 = (int32_t) buffer, .arg3 = (int32_t) size};
    return (short) syscall(KFN_CHAN_READ, &params);
}

/* Read a line from a channel (KFN_CHAN_READ_LINE) */
short sys_chan_readline(short channel, unsigned char * buffer, short size) {
    t_syscall_params params = {.arg1 = (int32_t) channel, .arg2 = (int32_t) buffer, .arg3 = (int32_t) size};
    return (short) syscall(KFN_CHAN_READ_LINE, &params);
}

/* Write one byte to a channel (KFN_CHAN_WRITE_B) */
short sys_chan_write_b(short channel, unsigned char b) {
    t_syscall_params params = {.arg1 = (int32_t) channel, .arg2 = (int32_t) b};
    return (short) syscall(KFN_CHAN_WRITE_B, &params);
}

/* Write bytes to a channel (KFN_CHAN_WRITE) */
short sys_chan_write(short channel, const unsigned char * buffer, short size) {
    t_syscall_params params = {.arg1 = (int32_t) channel, .arg2 = (int32_t) buffer, .arg3 = (int32_t) size};
    return (short) syscall(KFN_CHAN_WRITE, &params);
}

/* The status of a channel (KFN_CHAN_STATUS) */
short sys_chan_status(short channel) {
    t_syscall_params params = {.arg1 = (int32_t) channel};
    return (short) syscall(KFN_CHAN_STATUS, &params);
}

/* Finish pending writes on a channel (KFN_CHAN_FLUSH) */
short sys_chan_flush(short channel) {
    t_syscall_params params = {.arg1 = (int32_t) channel};
    return (short) syscall(KFN_CHAN_FLUSH, &params);
}

/* Move the position of a channel (KFN_CHAN_SEEK) */
short sys_chan_seek(short channel, long position, short base) {
    t_syscall_params params = {.arg1 = (int32_t) channel, .arg2 = (int32_t) position, .arg3 = (int32_t) base};
    return (short) syscall(KFN_CHAN_SEEK, &params);
}

/* A device-specific channel command (KFN_CHAN_IOCTRL) */
short sys_chan_ioctrl(short channel, short command, uint8_t * buffer, short size) {
    t_syscall_params params = {.arg1 = (int32_t) channel, .arg2 = (int32_t) command, .arg3 = (int32_t) buffer, .arg4 = (int32_t) size};
    return (short) syscall(KFN_CHAN_IOCTRL, &params);
}

/* Register a channel device driver (KFN_CHAN_REGISTER) */
short sys_chan_register(p_dev_chan device) {
    t_syscall_params params = {.arg1 = (int32_t) device};
    return (short) syscall(KFN_CHAN_REGISTER, &params);
}

/* Open a channel device (KFN_CHAN_OPEN) */
short sys_chan_open(short dev, const uint8_t * path, short mode) {
    t_syscall_params params = {.arg1 = (int32_t) dev, .arg2 = (int32_t) path, .arg3 = (int32_t) mode};
    return (short) syscall(KFN_CHAN_OPEN, &params);
}

/* Close a channel (KFN_CHAN_CLOSE) */
short sys_chan_close(short chan) {
    t_syscall_params params = {.arg1 = (int32_t) chan};
    return (short) syscall(KFN_CHAN_CLOSE, &params);
}

/* Swap the numbers of two channels (KFN_CHAN_SWAP) */
short sys_chan_swap(short channel1, short channel2) {
    t_syscall_params params = {.arg1 = (int32_t) channel1, .arg2 = (int32_t) channel2};
    return (short) syscall(KFN_CHAN_SWAP, &params);
}

/* The device of a channel (KFN_CHAN_DEVICE) */
short sys_chan_device(short channel) {
    t_syscall_params params = {.arg1 = (int32_t) channel};
    return (short) syscall(KFN_CHAN_DEVICE, &params);
}

/* Register a block device driver (KFN_BDEV_REGISTER) */
short sys_bdev_register(p_dev_block device) {
    t_syscall_params params = {.arg1 = (int32_t) device};
    return (short) syscall(KFN_BDEV_REGISTER, &params);
}

/* Read a block (KFN_BDEV_GETBLOCK) */
short sys_bdev_read(short dev, long lba, unsigned char * buffer, short size) {
    t_syscall_params params = {.arg1 = (int32_t) dev, .arg2 = (int32_t) lba, .arg3 = (int32_t) buffer, .arg4 = (int32_t) size};
    return (short) syscall(KFN_BDEV_GETBLOCK, &params);
}

/* Write a block (KFN_BDEV_PUTBLOCK) */
short sys_bdev_write(short dev, long lba, const unsigned char * buffer, short size) {
    t_syscall_params params = {.arg1 = (int32_t) dev, .arg2 = (int32_t) lba, .arg3 = (int32_t) buffer, .arg4 = (int32_t) size};
    return (short) syscall(KFN_BDEV_PUTBLOCK, &params);
}

/* The status of a block device (KFN_BDEV_STATUS) */
short sys_bdev_status(short dev) {
    t_syscall_params params = {.arg1 = (int32_t) dev};
    return (short) syscall(KFN_BDEV_STATUS, &params);
}

/* Finish pending writes on a block device (KFN_BDEV_FLUSH) */
short sys_bdev_flush(short dev) {
    t_syscall_params params = {.arg1 = (int32_t) dev};
    return (short) syscall(KFN_BDEV_FLUSH, &params);
}

/* A device-specific block device command (KFN_BDEV_IOCTRL) */
short sys_bdev_ioctrl(short dev, short command, unsigned char * buffer, short size) {
    t_syscall_params params = {.arg1 = (int32_t) dev, .arg2 = (int32_t) command, .arg3 = (int32_t) buffer, .arg4 = (int32_t) size};
    return (short) syscall(KFN_BDEV_IOCTRL, &params);
}

/* Open a file (KFN_OPEN) */
short sys_fsys_open(const char * path, short mode) {
    t_syscall_params params = {.arg1 = (int32_t) path, .arg2 = (int32_t) mode};
    return (short) syscall(KFN_OPEN, &params);
}

/* Close a file (KFN_CLOSE) */
short sys_fsys_close(short fd) {
    t_syscall_params params = {.arg1 = (int32_t) fd};
    return (short) syscall(KFN_CLOSE, &params);
}

/* Open a folder (KFN_OPENDIR) */
short sys_fsys_opendir(const char * path) {
    t_syscall_params params = {.arg1 = (int32_t) path};
    return (short) syscall(KFN_OPENDIR, &params);
}

/* Close a folder (KFN_CLOSEDIR) */
short sys_fsys_closedir(short dir) {
    t_syscall_params params = {.arg1 = (int32_t) dir};
    return (short) syscall(KFN_CLOSEDIR, &params);
}

/* Read the next folder entry (KFN_READDIR) */
short sys_fsys_readdir(short dir, p_file_info file) {
    t_syscall_params params = {.arg1 = (int32_t) dir, .arg2 = (int32_t) file};
    return (short) syscall(KFN_READDIR, &params);
}

/* Find the first matching entry (KFN_FINDFIRST) */
short sys_fsys_findfirst(const char * path, const char * pattern, p_file_info file) {
    t_syscall_params params = {.arg1 = (int32_t) path, .arg2 = (int32_t) pattern, .arg3 = (int32_t) file};
    return (short) syscall(KFN_FINDFIRST, &params);
}

/* Find the next matching entry (KFN_FINDNEXT) */
short sys_fsys_findnext(short dir, p_file_info file) {
    t_syscall_params params = {.arg1 = (int32_t) dir, .arg2 = (int32_t) file};
    return (short) syscall(KFN_FINDNEXT, &params);
}

/* Read a volume's label (KFN_GET_LABEL) */
short sys_fsys_get_label(const char * path, char * label) {
    t_syscall_params params = {.arg1 = (int32_t) path, .arg2 = (int32_t) label};
    return (short) syscall(KFN_GET_LABEL, &params);
}

/* Set a volume's label (KFN_SET_LABEL) */
short sys_fsys_set_label(short drive, const char * label) {
    t_syscall_params params = {.arg1 = (int32_t) drive, .arg2 = (int32_t) label};
    return (short) syscall(KFN_SET_LABEL, &params);
}

/* Create a folder (KFN_MKDIR) */
short sys_fsys_mkdir(const char * path) {
    t_syscall_params params = {.arg1 = (int32_t) path};
    return (short) syscall(KFN_MKDIR, &params);
}

/* Delete a file or folder (KFN_DELETE) */
short sys_fsys_delete(const char * path) {
    t_syscall_params params = {.arg1 = (int32_t) path};
    return (short) syscall(KFN_DELETE, &params);
}

/* Rename a file or folder (KFN_RENAME) */
short sys_fsys_rename(const char * old_path, const char * new_path) {
    t_syscall_params params = {.arg1 = (int32_t) old_path, .arg2 = (int32_t) new_path};
    return (short) syscall(KFN_RENAME, &params);
}

/* Set the current folder (KFN_SET_CWD) */
short sys_fsys_set_cwd(const char * path) {
    t_syscall_params params = {.arg1 = (int32_t) path};
    return (short) syscall(KFN_SET_CWD, &params);
}

/* Get the current folder (KFN_GET_CWD) */
short sys_fsys_get_cwd(char * path, short size) {
    t_syscall_params params = {.arg1 = (int32_t) path, .arg2 = (int32_t) size};
    return (short) syscall(KFN_GET_CWD, &params);
}

/* Load a file into memory (KFN_LOAD) */
short sys_fsys_load(const char * path, long destination, long * start) {
    t_syscall_params params = {.arg1 = (int32_t) path, .arg2 = (int32_t) destination, .arg3 = (int32_t) start};
    return (short) syscall(KFN_LOAD, &params);
}

/* Register a file loader (KFN_LOAD_REGISTER) */
short sys_fsys_register_loader(const char * extension, p_file_loader loader) {
    t_syscall_params params = {.arg1 = (int32_t) extension, .arg2 = (int32_t) loader};
    return (short) syscall(KFN_LOAD_REGISTER, &params);
}

/* Does a file exist, and what is it (KFN_STAT) */
short sys_fsys_stat(const char * path, p_file_info file) {
    t_syscall_params params = {.arg1 = (int32_t) path, .arg2 = (int32_t) file};
    return (short) syscall(KFN_STAT, &params);
}

/* Load and run a program (KFN_RUN) */
short sys_proc_run(const char * path, int argc, char * argv[]) {
    t_syscall_params params = {.arg1 = (int32_t) path, .arg2 = (int32_t) argc, .arg3 = (int32_t) argv};
    return (short) syscall(KFN_RUN, &params);
}

/* Supervisor mode (refused by MCP 2.1.0) (KFN_ELEVATE) */
short sys_proc_elevate(void) {
    t_syscall_params params = {0};
    return (short) syscall(KFN_ELEVATE, &params);
}

/* The top of the RAM a program may use (KFN_MEM_GET_RAMTOP) */
unsigned long sys_mem_get_ramtop(void) {
    t_syscall_params params = {0};
    return (unsigned long) syscall(KFN_MEM_GET_RAMTOP, &params);
}

/* Reserve memory at the top of RAM (KFN_MEM_RESERVE) */
unsigned long sys_mem_reserve(unsigned long bytes) {
    t_syscall_params params = {.arg1 = (int32_t) bytes};
    return (unsigned long) syscall(KFN_MEM_RESERVE, &params);
}

/* Set a system variable (KFN_VAR_SET) */
short sys_var_set(const char * name, const char * value) {
    t_syscall_params params = {.arg1 = (int32_t) name, .arg2 = (int32_t) value};
    return (short) syscall(KFN_VAR_SET, &params);
}

/* Get a system variable (KFN_VAR_GET) */
const char * sys_var_get(const char * name) {
    t_syscall_params params = {.arg1 = (int32_t) name};
    return (const char *) syscall(KFN_VAR_GET, &params);
}

/* Ticks since the start (KFN_TIME_JIFFIES) */
long sys_time_jiffies(void) {
    t_syscall_params params = {0};
    return (long) syscall(KFN_TIME_JIFFIES, &params);
}

/* Set the real-time clock (KFN_TIME_SETRTC) */
void sys_time_setrtc(p_time time) {
    t_syscall_params params = {.arg1 = (int32_t) time};
    syscall(KFN_TIME_SETRTC, &params);
}

/* Read the real-time clock (KFN_TIME_GETRTC) */
void sys_time_getrtc(p_time time) {
    t_syscall_params params = {.arg1 = (int32_t) time};
    syscall(KFN_TIME_GETRTC, &params);
}

/* The next keyboard scan code (KFN_KBD_SCANCODE) */
unsigned short sys_kbd_scancode(void) {
    t_syscall_params params = {0};
    return (unsigned short) syscall(KFN_KBD_SCANCODE, &params);
}

/* Set the keyboard translation tables (KFN_KBD_LAYOUT) */
short sys_kbd_layout(const char * tables) {
    t_syscall_params params = {.arg1 = (int32_t) tables};
    return (short) syscall(KFN_KBD_LAYOUT, &params);
}

/* The text of an error number (KFN_ERR_MESSAGE) */
const char * sys_err_message(short err_number) {
    t_syscall_params params = {.arg1 = (int32_t) err_number};
    return (const char *) syscall(KFN_ERR_MESSAGE, &params);
}

/* Reset a screen (KFN_TEXT_INIT_SCREEN) */
void sys_txt_init_screen(short screen) {
    t_syscall_params params = {.arg1 = (int32_t) screen};
    syscall(KFN_TEXT_INIT_SCREEN, &params);
}

/* What a screen can do (KFN_TXT_GET_CAPS) */
const p_txt_capabilities sys_txt_get_capabilities(short screen) {
    t_syscall_params params = {.arg1 = (int32_t) screen};
    return (const p_txt_capabilities) syscall(KFN_TXT_GET_CAPS, &params);
}

/* Set the display mode (KFN_TXT_SET_MODE) */
short sys_txt_set_mode(short screen, short mode) {
    t_syscall_params params = {.arg1 = (int32_t) screen, .arg2 = (int32_t) mode};
    return (short) syscall(KFN_TXT_SET_MODE, &params);
}

/* Recompute the text size (KFN_TEXT_SETSIZES) */
void sys_txt_setsizes(short screen) {
    t_syscall_params params = {.arg1 = (int32_t) screen};
    syscall(KFN_TEXT_SETSIZES, &params);
}

/* Set the resolution (KFN_TXT_SET_RESOLUTION) */
short sys_txt_set_resolution(short screen, short width, short height) {
    t_syscall_params params = {.arg1 = (int32_t) screen, .arg2 = (int32_t) width, .arg3 = (int32_t) height};
    return (short) syscall(KFN_TXT_SET_RESOLUTION, &params);
}

/* Set the border size (KFN_TXT_SET_BORDER) */
void sys_txt_set_border(short screen, short width, short height) {
    t_syscall_params params = {.arg1 = (int32_t) screen, .arg2 = (int32_t) width, .arg3 = (int32_t) height};
    syscall(KFN_TXT_SET_BORDER, &params);
}

/* Set the border colour (KFN_TXT_SET_BORDERCOLOR) */
void sys_txt_set_border_color(short screen, unsigned char red, unsigned char green, unsigned char blue) {
    t_syscall_params params = {.arg1 = (int32_t) screen, .arg2 = (int32_t) red, .arg3 = (int32_t) green, .arg4 = (int32_t) blue};
    syscall(KFN_TXT_SET_BORDERCOLOR, &params);
}

/* Load a font (KFN_TXT_SET_FONT) */
short sys_txt_set_font(short screen, short width, short height, unsigned char * data) {
    t_syscall_params params = {.arg1 = (int32_t) screen, .arg2 = (int32_t) width, .arg3 = (int32_t) height, .arg4 = (int32_t) data};
    return (short) syscall(KFN_TXT_SET_FONT, &params);
}

/* Set the look of the cursor (KFN_TXT_SET_CURSOR) */
void sys_txt_set_cursor(short screen, short enable, short rate, char character) {
    t_syscall_params params = {.arg1 = (int32_t) screen, .arg2 = (int32_t) enable, .arg3 = (int32_t) rate, .arg4 = (int32_t) character};
    syscall(KFN_TXT_SET_CURSOR, &params);
}

/* Show or hide the cursor (KFN_TXT_SET_CURSOR_VIS) */
void sys_txt_set_cursor_visible(short screen, short is_visible) {
    t_syscall_params params = {.arg1 = (int32_t) screen, .arg2 = (int32_t) is_visible};
    syscall(KFN_TXT_SET_CURSOR_VIS, &params);
}

/* Set the text region (KFN_TXT_SET_REGION) */
short sys_txt_set_region(short screen, p_rect region) {
    t_syscall_params params = {.arg1 = (int32_t) screen, .arg2 = (int32_t) region};
    return (short) syscall(KFN_TXT_SET_REGION, &params);
}

/* Get the text region (KFN_TXT_GET_REGION) */
short sys_txt_get_region(short screen, p_rect region) {
    t_syscall_params params = {.arg1 = (int32_t) screen, .arg2 = (int32_t) region};
    return (short) syscall(KFN_TXT_GET_REGION, &params);
}

/* Set the text colours (KFN_TXT_SET_COLOR) */
short sys_txt_set_color(short screen, unsigned char foreground, unsigned char background) {
    t_syscall_params params = {.arg1 = (int32_t) screen, .arg2 = (int32_t) foreground, .arg3 = (int32_t) background};
    return (short) syscall(KFN_TXT_SET_COLOR, &params);
}

/* Get the text colours (KFN_TXT_GET_COLOR) */
void sys_txt_get_color(short screen, unsigned char * foreground, unsigned char * background) {
    t_syscall_params params = {.arg1 = (int32_t) screen, .arg2 = (int32_t) foreground, .arg3 = (int32_t) background};
    syscall(KFN_TXT_GET_COLOR, &params);
}

/* Set the cursor position (KFN_TXT_SET_XY) */
void sys_txt_set_xy(short screen, short x, short y) {
    t_syscall_params params = {.arg1 = (int32_t) screen, .arg2 = (int32_t) x, .arg3 = (int32_t) y};
    syscall(KFN_TXT_SET_XY, &params);
}

/* Get the cursor position (KFN_TXT_GET_XY) */
void sys_txt_get_xy(short screen, p_point position) {
    t_syscall_params params = {.arg1 = (int32_t) screen, .arg2 = (int32_t) position};
    syscall(KFN_TXT_GET_XY, &params);
}

/* Scroll the region (KFN_TXT_SCROLL) */
void sys_txt_scroll(short screen, short horizontal, short vertical) {
    t_syscall_params params = {.arg1 = (int32_t) screen, .arg2 = (int32_t) horizontal, .arg3 = (int32_t) vertical};
    syscall(KFN_TXT_SCROLL, &params);
}

/* The size of a screen (KFN_TXT_GET_SIZES) */
void sys_txt_get_sizes(short screen, p_extent text_size, p_extent pixel_size) {
    t_syscall_params params = {.arg1 = (int32_t) screen, .arg2 = (int32_t) text_size, .arg3 = (int32_t) pixel_size};
    syscall(KFN_TXT_GET_SIZES, &params);
}

/* The program's text in the top bar (KFN_TXT_TOPBAR) */
short sys_txt_topbar(short screen, const char * text) {
    t_syscall_params params = {.arg1 = (int32_t) screen, .arg2 = (int32_t) text};
    return (short) syscall(KFN_TXT_TOPBAR, &params);
}

/*
 * Older names (Calypsi's SDK and older MCP headers): the same calls under the name used now
 */

/* The older name of sys_fsys_get_cwd */
short sys_fsys_getcwd(char * path, short size) {
    return sys_fsys_get_cwd(path, size);
}

/* The older name of sys_fsys_set_cwd */
short sys_fsys_setcwd(const char * path) {
    return sys_fsys_set_cwd(path);
}

/* The older name of sys_bdev_read */
short sys_bdev_getblock(short dev, long lba, unsigned char * buffer, short size) {
    return sys_bdev_read(dev, lba, buffer, size);
}

/* The older name of sys_bdev_write */
short sys_bdev_putblock(short dev, long lba, const unsigned char * buffer, short size) {
    return sys_bdev_write(dev, lba, buffer, size);
}

/* The older name of sys_kbd_layout */
short sys_kbd_setlayout(const char * tables) {
    return sys_kbd_layout(tables);
}

/* The older name of sys_txt_get_capabilities */
const p_txt_capabilities sys_txt_get_caps(short screen) {
    return sys_txt_get_capabilities(screen);
}

/* The older name of sys_txt_set_cursor_visible */
void sys_txt_set_cursor_vis(short screen, short is_visible) {
    sys_txt_set_cursor_visible(screen, is_visible);
}

/* The older name of sys_txt_setsizes */
void sys_text_setsizes(short screen) {
    sys_txt_setsizes(screen);
}

/* The older name of sys_time_setrtc */
void sys_rtc_set_time(p_time time) {
    sys_time_setrtc(time);
}

/* The older name of sys_time_getrtc */
void sys_rtc_get_time(p_time time) {
    sys_time_getrtc(time);
}
