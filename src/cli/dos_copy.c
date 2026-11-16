/** @file: dos_copy.c
 * Provide the various functions needed for the COPY command
 */

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dos_copy.h"
#include "constants.h"
#include "errors.h"
#include "log.h"
#include "simpleio.h"
#include "syscalls.h"
#include "utilities.h"
#include "dev/fsys.h"

/*
 * Custom error codes for the COPY command
 */

#define ERR_COPY_SELF           -1000
#define ERR_COPY_SRC_IS_DIR     -1001
#define ERR_COPY_DST_IS_DIR     -1002
#define ERR_COPY_NO_SRC_DIR     -1003
#define ERR_COPY_NO_DST_DIR     -1004
#define ERR_MOVE_EXISTS         -1005   /* 2.0.34: MOVE does not overwrite */
#define ERR_MOVE_DIR_DRIVE      -1006   /* 2.0.34: a folder cannot move to another drive */
#define ERR_MOVE_COPY_SIZE      -1007   /* 2.0.34: the copy on the other drive came out wrong */
#define ERR_MOVE_NOT_FOLDER     -1008   /* 2.0.34: several files need a folder to go to */

/**
 * COPY is a complicated command, hence getting its own file.
 * The command needs to deal with several types of source and destination path types:
 *
 * For the source file(s):
 * S1. Fully specified absolute path to a single source file
 * S2. Relative path to a single source file
 * S3. Absolute path to one or more source files (pattern matching)
 * S4. Relative path to one or more source files (pattern matching)
 *
 * For the destination:
 * D1. Absolute path to the directory to store the file (filename implied)
 * D2. Relative path to the directory (filename inmplied)
 * D3. Absolute path to the file to create (filename provided)
 * D4. Relative path to the file to create (filename provided)
 *
 * There are limitations: D1 and D2 may be used only if the source and destination
 * directories are different, since we can't have the same file name twice in a directory.
 * D3 and D4 may only be used in conjunction with S1 and S2, since S3 and S4 can specify
 * multiple files, which cannot be copied to a single file.
 *
 * Additional rules:
 * 1. COPY will not support copying an entire directory.
 *    (This means all source paths _must_ include a filename or pattern)
 * 2. A file cannot be copied to the same directory with the same name.
 * 3. Patterns (wildcards) may not be used for directory names.
 */

/**
 * Check to see if the path points to a directory
 *
 * @param path the path to check
 * @return true if the path points to a directory, false otherwise
 */
bool is_directory(const char * path) {
    t_file_info file;

    short result = sys_fsys_stat(path, &file);
    if ((result < 0) || ((file.attributes & FSYS_AM_DIR) == 0)) {
        return false;
    }

    return true;
}

/**
 * Perform the actual copy of the files, given the full paths to the files
 *
 * @param src_path the path to the source file to be copied
 * @param dst_path the path to the destination file (will be deleted if it already exists)
 * @return 0 on success, a negative number on error
 */
static short fsys_copy(const char * src_path, const char * dst_path) {
    unsigned char buffer[256];
    t_file_info file;
    short fd_src = -1;
    short fd_dst = -1;
    short result = 0;
    short n = 0;
    short error = 0;

    // Check to make sure the source and destination paths are not the same
    if (strcicmp(src_path, dst_path) == 0) {
        // the two paths are the same... this is an error
        return ERR_COPY_SELF;
    }

    // Check the source to make sure it exists and is not a directory
    result = sys_fsys_stat(src_path, &file);
    if (result < 0) {
        // There was an error... file not found most likely
        return result;
    } else {
        if (file.attributes & FSYS_AM_DIR) {
            // The source path was a directory... this isn't allowed
            return ERR_COPY_SRC_IS_DIR;
        }
    }

    // Check the destination to make sure it is not a directory, if it exists
    if (is_directory(dst_path)) {
        // The source path was a directory... this isn't allowed
        return ERR_COPY_DST_IS_DIR;
    }

    // Try to open the source file...
    fd_src = sys_fsys_open(src_path, FSYS_READ);
    if (fd_src > 0) {
        // Ok... now try to open and create the destination file for writing...
        fd_dst = sys_fsys_open(dst_path, FSYS_WRITE | FSYS_CREATE_ALWAYS);
        if (fd_dst > 0) {
            // Copy the file...
            // 2.0.34: a read or write error (a full disk, say) is reported now. It used to return
            // 0 anyway, which MOVE cannot have: it deletes the source after a good copy.
            do {
                // Try to read a chunk of data from the file
                n = sys_chan_read(fd_src, buffer, 256);
                if (n < 0) {
                    error = n;
                } else if (n > 0) {
                    // Try to write what we got
                    result = sys_chan_write(fd_dst, buffer, n);
                    if (result < 0) {
                        error = result;
                    } else if (result != n) {
                        error = FSYS_ERR_DENIED;       // Fewer bytes written: the disk is full
                    }
                }
            } while ((n > 0) && (error == 0));

            sys_fsys_close(fd_src);
            sys_fsys_close(fd_dst);
            return error;

        } else {
            // Close the source file and return the error
            sys_fsys_close(fd_src);
            return fd_dst;
        }

    } else {
        // There was an error... just return it
        return fd_src;
    }
}

/**
 * Copy one or more file when the source contains a pattern
 *
 * NOTE: it is an error if the source and destination paths are the same
 *
 * @param src_path the path to the directory containing the source files
 * @param src_pattern the search pattern used to find the files
 * @param dst_path the path to the directory in which to store the files
 * @result 0 on success, negative number on error
 */
static short fsys_copy_pattern(const char * src_path, const char * src_pattern, const char * dst_path) {
    char src_file_path[MAX_PATH_LEN];
    char dst_file_path[MAX_PATH_LEN];
    char tmp[160];
    t_file_info file;
    short result = 0;
    short dir = -1;

    // Make sure the source and destination paths are not the same
    if (strcicmp(src_path, dst_path) == 0) {
        // the two paths are the same... this is an error
        return ERR_COPY_SELF;
    }

    // Make sure the src_path exists and is a directory
    result = sys_fsys_stat(src_path, &file);
    if ((result < 0) || ((file.attributes & FSYS_AM_DIR) == 0)) {
        return ERR_COPY_NO_SRC_DIR;
    }

    // Make sure the dst_path exists and is a directory
    result = sys_fsys_stat(dst_path, &file);
    if ((result < 0) || ((file.attributes & FSYS_AM_DIR) == 0)) {
        return ERR_COPY_NO_DST_DIR;
    }

    dir = sys_fsys_findfirst(src_path, src_pattern, &file);
    if (dir < 0) {
        // Could not open the source directory for some reason
        return dir;
    }

    // Try to copy each match...
    while (strlen(file.name) > 0) {
        // Figure out the source and destination paths to copy
        sprintf(src_file_path, "%s%s", src_path, file.name);
        sprintf(dst_file_path, "%s/%s", dst_path, file.name);

        // Try to actually copy them
        result = fsys_copy(src_file_path, dst_file_path);
        if (result < 0) {
            sys_fsys_closedir(dir);
            return result;
        }

        // Try to get the next match
        result = sys_fsys_findnext(dir, &file);
        if (result < 0) {
            sys_fsys_closedir(dir);
            return result;
        }
    }

    sys_fsys_closedir(dir);
    return 0;
}

/**
 * Make sure the provided path is absolute.
 *
 * If the path is not absolute, tack the current working directory onto the front of the path
 *
 * @param path the path to verify
 * @return 0 on success, any other number is an error
 */
short fsys_copy_path_absolute(char * path) {
    char tmp_path[MAX_PATH_LEN];
    char cwd[MAX_PATH_LEN];
    short result = 0;

    if (path[0] != '/') {
        // Path is not absolute... let's get the current working directory and fix that
        result = fsys_get_cwd(cwd, MAX_PATH_LEN);
        if (result) {
            return result;
        }

        // Make a new, absolute path
        sprintf(tmp_path, "%s%s", cwd, path);

        // And copy it over the old path
        strncpy(path, tmp_path, MAX_PATH_LEN);
    }

    return 0;
}

static void fsys_copy_error(short screen, short n) {
    char line[160];
    switch (n) {
        case ERR_COPY_SELF:
            print(screen, "Unable to copy a file onto itself.\n");
            break;

        case ERR_COPY_NO_SRC_DIR:
            print(screen, "Unable to copy a file: no source directory found.\n");
            break;

        case ERR_COPY_NO_DST_DIR:
            print(screen, "Unable to copy a file: no destination directory found.\n");
            break;

        case ERR_COPY_SRC_IS_DIR:
            print(screen, "Unable to copy a file: cannot copy a directory.\n");
            break;

        case ERR_COPY_DST_IS_DIR:
            print(screen, "Unable to copy a file: cannot copy to a directory.\n");
            break;

        case ERR_MOVE_EXISTS:
            print(screen, "Unable to move: the destination already exists.\n");
            break;

        case ERR_MOVE_DIR_DRIVE:
            print(screen, "Unable to move: a folder can only move on the same drive.\n");
            break;

        case ERR_MOVE_COPY_SIZE:
            print(screen, "Unable to move: the copy came out wrong; the original is kept.\n");
            break;

        case ERR_MOVE_NOT_FOLDER:
            print(screen, "Unable to move: the destination of several files must be a folder.\n");
            break;

        default:
            sprintf(line, "Unable to copy the files: %d\n", n);
            print(screen, line);
            break;
    }
}

/**
 * The DOS COPY command itself:
 * COPY <src path> <dst path>
 */
short cmd_copy(short screen, int argc, const char * argv[]) {
    char *x = 0;
    char *src_path = 0;
    char *src_pattern = 0;
    char *dst_path = 0;
    char *dst_name = 0;
    char *tmp_path = 0;
    char * src_solidus = 0;
    char * dst_solidus = 0;
    short result = 0;

    // Allocate memory for the paths, etc.
    x = (char *)malloc(5 * MAX_PATH_LEN);
    if (x == 0) {
        print(screen, "Cannot copy: out of memory\n");
        return -1;
    }

    // Distribute the memory across our buffers...
    src_path = x;
    src_pattern = src_path + MAX_PATH_LEN;
    dst_path = src_pattern + MAX_PATH_LEN;
    dst_name = dst_path + MAX_PATH_LEN;
    tmp_path = dst_name + MAX_PATH_LEN;

    if (argc != 3) {
        print(screen, "USAGE: COPY <src path> <dst path>\n");
        free(x);
        return -1;
    }

    // Copy the source path and make sure it's absolute
    strncpy(src_path, argv[1], MAX_PATH_LEN);
    if (fsys_copy_path_absolute(src_path)) {
        print(screen, "Unable to find the current working directory.\n");
        free(x);
        return -1;
    }

    // Copy the destination path and make sure it's absolute
    strncpy(dst_path, argv[2], MAX_PATH_LEN);
    if (fsys_copy_path_absolute(dst_path)) {
        print(screen, "Unable to find the current working directory.\n");
        free(x);
        return -1;
    }

    // Find the last slash, so we can check the file name portion
    src_solidus = strrchr(src_path, '/');
    if (src_solidus) {
        // Copy the file name field...
        strncpy(src_pattern, src_solidus + 1, MAX_PATH_LEN);
        if (strchr(src_pattern, '*') || strchr(src_pattern, '?')) {
            // There is a pattern in the file name field... remove the pattern from the path
            src_solidus[1] = 0;

            result = fsys_copy_pattern(src_path, src_pattern, dst_path);
            if (result) {
                fsys_copy_error(screen, result);
                free(x);
                return -1;
            }
        } else {
            // Just one file to copy...

            // If the destination is a directory, add the source file name
            if (is_directory(dst_path)) {
                if (dst_path[strlen(dst_path)-1] == '/') {
                    sprintf(tmp_path, "%s%s", dst_path, src_pattern);
                } else {
                    sprintf(tmp_path, "%s/%s", dst_path, src_pattern);
                }
                strcpy(dst_path, tmp_path);
            }

            // Attempt to make the copy
            result = fsys_copy(src_path, dst_path);
            if (result) {
                char line[160];
                fsys_copy_error(screen, result);
                free(x);
                return -1;
            }
        }
    } else {
        print(screen, "Unable to parse the source path.\n");
        free(x);
        return -1;
    }

    free(x);
    return 0;
}

/*
 * MOVE (2.0.34)
 *
 * MOVE <src path> <dst path> moves a file or a folder, or with a pattern (* ?) in the file name
 * several files, like COPY takes them. On the same drive it is a rename, which also moves folders
 * and is quick whatever the size. To another drive a file is copied, the copy's size is checked,
 * and only then the original is deleted. MOVE never overwrites an existing file.
 */

/*
 * Are two absolute paths on the same drive? The drive is the first part: "/sd", "/hd", "/fd".
 *
 * Inputs:
 * a, b = absolute paths
 *
 * Returns:
 * true if their first parts are the same (not case sensitive)
 */
static bool move_same_drive(const char * a, const char * b) {
    short k = 1;
    while ((a[k] != 0) && (a[k] != '/') && (toupper(a[k]) == toupper(b[k]))) {
        k++;
    }
    return ((a[k] == 0) || (a[k] == '/')) && ((b[k] == 0) || (b[k] == '/'));
}

/*
 * Is the path a folder to move things into? A drive's root ("/sd", "/sd/") counts, though the
 * file system cannot give information about it.
 *
 * Inputs:
 * path = an absolute path (a final '/' is removed, except from a drive's root)
 *
 * Returns:
 * true if it is a folder
 */
static bool move_is_folder(char * path) {
    char * slash;
    short len = strlen(path);

    if ((len > 1) && (path[len - 1] == '/')) {
        path[len - 1] = 0;
    }
    slash = strchr(path + 1, '/');
    if (slash == 0) {
        return true;                    // Only the drive: "/sd"
    }
    return is_directory(path);
}

/*
 * Move one file or folder to its new path
 *
 * Inputs:
 * src_path = the absolute path of the file or folder
 * dst_path = its absolute new path (not an existing file or folder)
 *
 * Returns:
 * 0 on success, a negative number on error
 */
static short fsys_move(const char * src_path, const char * dst_path) {
    t_file_info src, dst;
    short result;

    if (strcicmp(src_path, dst_path) == 0) {
        return ERR_COPY_SELF;
    }

    result = sys_fsys_stat(src_path, &src);
    if (result < 0) {
        return result;                  // The source does not exist
    }

    if (sys_fsys_stat(dst_path, &dst) >= 0) {
        return ERR_MOVE_EXISTS;
    }

    if (move_same_drive(src_path, dst_path)) {
        // On the same drive the file system moves it: only the folder entries change.
        // (Not on another drive: there it would land on the source's drive.)
        return fsys_rename(src_path, dst_path);
    }

    if (src.attributes & FSYS_AM_DIR) {
        return ERR_MOVE_DIR_DRIVE;
    }

    // Another drive: copy, check the copy, then delete the original
    result = fsys_copy(src_path, dst_path);
    if (result < 0) {
        sys_fsys_delete(dst_path);      // Do not leave half a file
        return result;
    }

    result = sys_fsys_stat(dst_path, &dst);
    if ((result < 0) || (dst.size != src.size)) {
        sys_fsys_delete(dst_path);
        return ERR_MOVE_COPY_SIZE;
    }

    return sys_fsys_delete(src_path);
}

/**
 * The DOS MOVE command itself:
 * MOVE <src path> <dst path>
 */
short cmd_move(short screen, int argc, const char * argv[]) {
    char * x;
    char * src_path, * dst_path, * src_file, * dst_file;
    char * name;
    t_file_info file;
    short result = 0;
    short dir;

    if (argc != 3) {
        print(screen, "USAGE: MOVE <src path> <dst path>\n");
        return -1;
    }

    x = (char *)malloc(4 * MAX_PATH_LEN);
    if (x == 0) {
        print(screen, "Cannot move: out of memory\n");
        return -1;
    }
    src_path = x;
    dst_path = src_path + MAX_PATH_LEN;
    src_file = dst_path + MAX_PATH_LEN;
    dst_file = src_file + MAX_PATH_LEN;

    strncpy(src_path, argv[1], MAX_PATH_LEN - 1);
    src_path[MAX_PATH_LEN - 1] = 0;
    strncpy(dst_path, argv[2], MAX_PATH_LEN - 1);
    dst_path[MAX_PATH_LEN - 1] = 0;
    if (fsys_copy_path_absolute(src_path) || fsys_copy_path_absolute(dst_path)) {
        print(screen, "Unable to find the current working directory.\n");
        free(x);
        return -1;
    }

    // A source given as "folder/" means the folder itself
    if ((strlen(src_path) > 1) && (src_path[strlen(src_path) - 1] == '/')) {
        src_path[strlen(src_path) - 1] = 0;
    }
    name = strrchr(src_path, '/') + 1;

    if (strchr(name, '*') || strchr(name, '?')) {
        // A pattern: every match goes into the destination folder
        if (!move_is_folder(dst_path)) {
            result = ERR_MOVE_NOT_FOLDER;
        } else {
            strcpy(src_file, name);     // The pattern
            name[0] = 0;                // src_path is now the folder, with its final '/'
            dir = sys_fsys_findfirst(src_path, src_file, &file);
            if (dir < 0) {
                result = dir;
            } else {
                while ((result == 0) && (file.name[0] != 0)) {
                    snprintf(src_file, MAX_PATH_LEN, "%s%s", src_path, file.name);
                    snprintf(dst_file, MAX_PATH_LEN, "%s%s%s", dst_path,
                             (dst_path[strlen(dst_path) - 1] == '/') ? "" : "/", file.name);
                    result = fsys_move(src_file, dst_file);
                    if (result == 0) {
                        result = sys_fsys_findnext(dir, &file);
                        if (result > 0) {
                            result = 0;
                        }
                    }
                }
                sys_fsys_closedir(dir);
            }
        }

    } else {
        // One file or folder: into the destination folder, or to the new name
        if (move_is_folder(dst_path)) {
            snprintf(dst_file, MAX_PATH_LEN, "%s%s%s", dst_path,
                     (dst_path[strlen(dst_path) - 1] == '/') ? "" : "/", name);
        } else {
            strcpy(dst_file, dst_path);
        }
        result = fsys_move(src_path, dst_file);
    }

    if (result < 0) {
        if (result <= ERR_COPY_SELF) {
            fsys_copy_error(screen, result);
        } else {
            err_print(screen, "Unable to move", result);
        }
        free(x);
        return -1;
    }

    free(x);
    return 0;
}
