/**
 * @file stubs.c
 *
 * The Calypsi C library's connection to the hardware ("stubs"), for the MCP itself. Only the
 * Calypsi build links this file (2.1.0).
 *
 * The MCP does its own input and output through channels and the file system; it uses only a
 * few C library functions (sprintf, string functions, malloc). Some library code still refers
 * to the stubs, so they must exist. What C library output there is goes to the screen (channel
 * 0); files cannot be opened through the C library.
 */

#include <stddef.h>
#include <stdio.h>
#include <stdint.h>
#include <calypsi/stubs.h>

#include "dev/channel.h"

/**
 * Write bytes for the C library (printf and friends).
 *
 * @param fd the C library's file number (1 = stdout, 2 = stderr; anything else is refused)
 * @param buf the bytes to write
 * @param count how many bytes
 * @return how many bytes were written, or -1 for a file number that is not the screen
 */
size_t _Stub_write(int fd, const void *buf, size_t count) {
    const uint8_t *p = (const uint8_t *)buf;
    size_t left = count;

    if ((fd != 1) && (fd != 2)) {
        return (size_t)-1;
    }

    /* chan_write takes a short count: write long output in pieces */
    while (left > 0) {
        short n = (left > 0x4000) ? 0x4000 : (short)left;
        chan_write(0, p, n);
        p += n;
        left -= (size_t)n;
    }
    return count;
}

/**
 * Read bytes for the C library: not supported (the MCP reads the keyboard through channels).
 *
 * @return 0 (end of file)
 */
size_t _Stub_read(int fd, void *buf, size_t count) {
    (void)fd; (void)buf; (void)count;
    return 0;
}

/**
 * Open, close, seek, get or set the position of, rename and remove C library files: not
 * supported (the MCP uses its own file system calls).
 *
 * @return -1 (error)
 */
int _Stub_open(const char *path, int oflag, ...) { (void)path; (void)oflag; return -1; }
int _Stub_close(int fd) { (void)fd; return -1; }
long _Stub_lseek(int fd, long offset, int whence) { (void)fd; (void)offset; (void)whence; return -1; }
int _Stub_fgetpos(int fd, fpos_t *pos) { (void)fd; (void)pos; return -1; }
int _Stub_fsetpos(int fd, const fpos_t *pos) { (void)fd; (void)pos; return -1; }
int _Stub_rename(const char *oldpath, const char *newpath) { (void)oldpath; (void)newpath; return -1; }
int _Stub_remove(const char *path) { (void)path; return -1; }

/**
 * The environment for getenv(): there is none.
 *
 * @return an empty list
 */
char **_Stub_environ(void) {
    static char *none[1] = { NULL };
    return none;
}

/**
 * exit() and a failed assert(): the MCP itself never ends, so stop here.
 */
void _Stub_exit(int exitCode) {
    (void)exitCode;
    for (;;) { }
}

void _Stub_assert(const char *filename, int linenum) {
    (void)filename; (void)linenum;
    for (;;) { }
}
