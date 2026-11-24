/*
 * mcp/errors.h - the error numbers the Foenix/MCP returns (MCP 2.1.0)
 *
 * Part of the MCP's Calypsi SDK (sdk/calypsi). The same numbers as the MCP's src/include/errors.h;
 * tools/check_sdk.py compares them when the MCP is built. sys_err_message() gives the text.
 */

#ifndef __MCP_ERRORS_H
#define __MCP_ERRORS_H

#define E_OK                            0   /* Success, no error */
#define ERR_GENERAL                     -1  /* A general error */
#define DEV_ERR_BADDEV                  -2  /* Bad device number (too big, or no device) */
#define DEV_TIMEOUT                     -3  /* The block device timed out */
#define DEV_CANNOT_INIT                 -4  /* The block device could not start */
#define DEV_CANNOT_READ                 -5  /* The device could not read */
#define DEV_CANNOT_WRITE                -6  /* The device could not write */
#define DEV_BOUNDS_ERR                  -7  /* The buffer is too small */
#define DEV_NOMEDIA                     -8  /* No media in the device */
#define DEV_WRITEPROT                   -9  /* The media is write protected */
#define ERR_BADCHANNEL                  -10 /* Bad channel number */
#define ERR_OUT_OF_HANDLES              -11 /* No free handles for channels, files, ... */
#define ERR_BAD_HANDLE                  -12 /* The handle is not valid */
#define ERR_BAD_EXTENSION               -13 /* No loader for the file's extension */
#define ERR_OUT_OF_MEMORY               -14 /* No more memory */
#define ERR_BAD_BINARY                  -15 /* The binary file's format is wrong */
#define ERR_NOT_EXECUTABLE              -16 /* The binary file has no start address */
#define ERR_NOT_FOUND                   -17 /* Not found */

/* The FatFS errors (FatFS's own number in brackets) */
#define FSYS_ERR_DISK_ERR               -18 /* (1) A hard error in the disk I/O layer */
#define FSYS_ERR_INT_ERR                -19 /* (2) Assertion failed */
#define FSYS_ERR_NOT_READY              -20 /* (3) The drive cannot work */
#define FSYS_ERR_NO_FILE                -21 /* (4) The file is not there */
#define FSYS_ERR_NO_PATH                -22 /* (5) The path is not there */
#define FSYS_ERR_INVALID_NAME           -23 /* (6) The path name is not valid */
#define FSYS_ERR_DENIED                 -24 /* (7) Access denied, or the folder or disk is full */
#define FSYS_ERR_EXIST                  -25 /* (8) It exists already */
#define FSYS_ERR_INVALID_OBJECT         -26 /* (9) The file or folder object is not valid */
#define FSYS_ERR_WRITE_PROTECTED        -27 /* (10) The drive is write protected */
#define FSYS_ERR_INVALID_DRIVE          -28 /* (11) The drive number is not valid */
#define FSYS_ERR_NOT_ENABLED            -29 /* (12) The volume has no work area */
#define FSYS_ERR_NO_FILESYSTEM          -30 /* (13) No valid FAT volume */
#define FSYS_ERR_MKFS_ABORTED           -31 /* (14) Formatting stopped */
#define FSYS_ERR_TIMEOUT                -32 /* (15) No access to the volume in time */
#define FSYS_ERR_LOCKED                 -33 /* (16) Refused by the file sharing rules */
#define FSYS_ERR_NOT_ENOUGH_CORE        -34 /* (17) No memory for the long file name buffer */
#define FSYS_ERR_TOO_MANY_OPEN_FILES    -35 /* (18) Too many open files */
#define FSYS_ERR_INVALID_PARAMETER      -36 /* (19) A parameter is not valid */

#define ERR_NOT_SUPPORTED               -37 /* The device or the MCP does not support it */
#define ERR_BAD_ARGUMENT                -38 /* An argument is not valid */
#define ERR_MEDIA_CHANGE                -39 /* The removable media was changed */
#define ERR_PROTECTED_MEMORY            -40 /* The file would load into memory the MCP uses */

#endif
