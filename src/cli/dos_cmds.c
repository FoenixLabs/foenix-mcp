#include <ctype.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "constants.h"
#include "syscalls.h"
#include "log.h"
#include "simpleio.h"
#include "cli.h"
#include "proc.h"
#include "boot.h"
#include "cli/dos_cmds.h"
#include "dev/block.h"
#include "dev/console.h"
#include "dev/fsys.h"
#include "dev/kbd_mo.h"
#include "fatfs/ff.h"

/*
 * Read a sector off a drive
 *
 * DISKREAD <drive #> <sector #>
 */
short cmd_diskread(short screen, int argc, const char * argv[]) {
    unsigned char buffer[512];
    short bdev_number = 0;
    long lba = 0;
    short result;
    short i;

    if (argc < 3) {
        print(screen, "USAGE: DISKREAD <drive #> <sector #>\n");
        return -1;
    }

    bdev_number = (short)cli_eval_number(argv[1]);
    lba = cli_eval_number(argv[2]);

    sprintf((char *)buffer, "Reading drive #%d, sector %p\n", bdev_number, (void*)lba);
    print(screen, (char *)buffer);

    result = bdev_read(bdev_number, lba, buffer, 512);
    if (result < 512) {
        err_print(screen, "Unable to read sector", result);
        return -2;
    }

    dump_buffer(screen, buffer, 512, 1);

    return 0;
}

/*
 * Fill a sector of a drive with a byte value
 *
 * DISKFILL <drive #> <sector #> <value>
 */
short cmd_diskfill(short screen, int argc, const char * argv[]) {
    unsigned char buffer[512];
    unsigned char value;
    short bdev_number = 0;
    long lba = 0;
    short result;
    short i;

    if (argc < 4) {
        print(screen, "USAGE: DISKFILL <drive #> <sector #> <byte value>\n");
        return -1;
    }

    bdev_number = (short)cli_eval_number(argv[1]);
    lba = cli_eval_number(argv[2]);
    value = (unsigned char)cli_eval_number(argv[3]);

    sprintf((char *)buffer, "Filling drive #%d, sector %p with 0x%02X\n", bdev_number, (void*)lba, (short)value);
    print(screen, (char *)buffer);

    for (i = 0; i < 512; i++) {
        buffer[i] = value;
    }

    result = bdev_write(bdev_number, lba, buffer, 512);
    if (result < 512) {
        err_print(screen, "Unable to write sector", result);
        return -2;
    }

    /* Read the sector back for verification */
    return cmd_diskread(screen, argc, argv);
}


/*
 * Try to run a command from storage.
 *
 * Command name is in argv[0].
 */
short cmd_run(short screen, int argc, const char * argv[]) {
    TRACE("cmd_run");

    sys_chan_ioctrl(screen, CON_IOCTRL_ECHO_ON, 0, 0);
    short result = proc_run(argv[0], argc, (char **)argv);
    if (result < 0) {
        sys_chan_ioctrl(screen, CON_IOCTRL_ECHO_OFF, 0, 0);     /* 2.0.33: the command line types its own echo */
        err_print(screen, "Unable to execute file", result);
        return -1;
    }

    /* 2.0.33: the program has ended and we are back (before, the command line was restarted).
     * The program may have left the screen in graphics mode, the cursor off or ANSI codes off:
     * put the console back the way the command line needs it, as the restart used to do. */
    cli_ensure_console(screen);
    return result;
}

/*
 * Create a directory
 */
short cmd_mkdir(short screen, int argc, const char * argv[]) {

    TRACE("cmd_mkdir");

    if (argc > 1) {
        short result = fsys_mkdir(argv[1]);
        if (result < 0) {
            err_print(screen, "Unable to create directory", result);
            return result;
        }
    } else {
        print(screen, "USAGE: MKDIR <path>\n");
        return -1;
    }
    return 0;       // 2.1.0: success did not return anything
}

/*
 * Delete a file
 */
/*
 * Delete the files that match a pattern (2.0.34, DEL with * or ? in the name)
 *
 * Only files: a folder that matches is left alone, so "DEL *" cannot take folders with it.
 * Each deleted file is listed.
 *
 * Inputs:
 * screen = the channel to report on
 * path = the path with the pattern, e.g. "*.o", or the folder /sd/tmp with the pattern *.bak
 *
 * Returns:
 * 0 on success, a negative number on error
 */
static short del_pattern(short screen, const char * path) {
    char folder[MAX_PATH_LEN];
    char file_path[MAX_PATH_LEN];
    char message[MAX_PATH_LEN + 32];
    const char * pattern;
    const char * slash = strrchr(path, '/');
    t_file_info file;
    short dir, result = 0, count = 0;

    // Split into the folder (with its final '/', or "" for the current one) and the pattern
    if (slash != 0) {
        short len = (short)(slash - path) + 1;
        if (len >= MAX_PATH_LEN) {
            return ERR_BAD_ARGUMENT;
        }
        memcpy(folder, path, len);
        folder[len] = 0;
        pattern = slash + 1;
    } else {
        folder[0] = 0;
        pattern = path;
    }

    dir = sys_fsys_findfirst(folder, pattern, &file);
    if (dir < 0) {
        return dir;
    }

    while (file.name[0] != 0) {
        if ((file.attributes & FSYS_AM_DIR) == 0) {
            snprintf(file_path, MAX_PATH_LEN, "%s%s", folder, file.name);
            result = fsys_delete(file_path);
            if (result < 0) {
                break;
            }
            snprintf(message, sizeof(message), "Deleted %s\n", file_path);
            print(screen, message);
            count++;
        }
        if (sys_fsys_findnext(dir, &file) < 0) {
            break;
        }
    }
    sys_fsys_closedir(dir);

    if (result < 0) {
        return result;
    }
    if (count == 0) {
        print(screen, "No files match.\n");
    }
    return 0;
}

short cmd_del(short screen, int argc, const char * argv[]) {
    short result;

    TRACE("cmd_del");

    if (argc > 1) {
        const char * name = strrchr(argv[1], '/');
        name = (name != 0) ? name + 1 : argv[1];
        if (strchr(name, '*') || strchr(name, '?')) {
            // 2.0.34: a pattern deletes every matching file
            result = del_pattern(screen, argv[1]);
        } else {
            result = fsys_delete(argv[1]);
        }
        if (result < 0) {
            err_print(screen, "Unable to delete file", result);
            return result;
        }
        return 0;           // 2.0.34: it fell off its end here without a return value

    } else {
        print(screen, "USAGE: DEL <path>\n");
        return -1;
    }
}

/*
 * ECHO <text> (2.0.34): print the text, for messages from mcp.init
 *
 * The words are printed with one space between them (the command line splits on spaces).
 */
short cmd_echo(short screen, int argc, const char * argv[]) {
    short i;

    for (i = 1; i < argc; i++) {
        if (i > 1) {
            print(screen, " ");
        }
        print(screen, argv[i]);
    }
    print(screen, "\n");
    return 0;
}

/*
 * Change the directory
 */
short cmd_cd(short screen, int argc, const char * argv[]) {

    TRACE("cmd_cd");

    if (argc > 1) {
        short result = fsys_set_cwd(argv[1]);
        if (result < 0) {
            err_print(screen, "Unable to change directory", result);
            return result;
        } else {
            cli_flag_cwd();
            print(screen, "Changed to: ");
            print(screen, argv[1]);
            print(screen, "\n");
        }
    } else {
        print(screen, "USAGE: CD <path>\n");
        return -1;
    }

    return 0;
}

/*
 * Change the directory
 */
short cmd_pwd(short screen, int argc, const char * argv[]) {
    char buffer[128];

    TRACE("cmd_pwd");

    short result = fsys_get_cwd(buffer, 128);
    if (result < 0) {
        err_print(screen, "Unable to get current directory", result);
        return result;
    } else {
        print(screen, buffer);
        return 0;
    }
}

/*
 * Rename a file or directory
 */
short cmd_rename(short screen, int argc, const char * argv[]) {

    TRACE("cmd_rename");

    if (argc > 2) {
        short result = fsys_rename(argv[1], argv[2]);
        if (result < 0) {
            err_print(screen, "Unable to rename file", result);
            return result;
        }
    }

    return 0;
}

/**
 * Structure to hold file and directory information for sorting
 */
typedef struct s_dir_entry {
    t_file_info info;
    struct s_dir_entry *next, *prev;
} t_dir_entry, *p_dir_entry;

/**
 * Add a directory entry to a list of directory entries using simple insertion sort
 *
 * @param list_pointer a pointer to a pointer to a list of directory entries
 * @param entry a pointer to a directory entry to add
 */
void dir_entry_insert(p_dir_entry * list_pointer, p_dir_entry entry) {
    p_dir_entry list = *list_pointer;

    if (list == 0) {
        *list_pointer = entry;
        entry->next = 0;
        entry->prev = 0;

    } else {
        p_dir_entry x = list;
        while (x != 0) {
            if (strcmp(x->info.name, entry->info.name) >= 0) {
                if (x->prev == 0) {
                    *list_pointer = entry;
                    entry->next = x;
                    entry->prev = 0;
                    x->prev = entry;
                } else {
                    entry->prev = x->prev;
                    entry->next = x;
                    x->prev->next = entry;
                    x->prev = entry;
                }
                return;

            } else {
                if (x->next == 0) {
                    x->next = entry;
                    entry->prev = x;
                    entry->next = 0;
                    return;
                } else {
                    x = x->next;
                }
            }
        }
    }
}

/**
 * Examine the optional path/pattern argument and separate it into a path and a pattern
 *
 * @param arg the string describing the directory and pattern to search
 * @param path a pointer to a pointer to a string... set to the path portion or 0 if no path
 * @param pattern a pointer to a pointer to a string... set to the pattern poriton, or 0 if none provided
 */
void dir_parse_pattern(char * arg, char ** path, char ** pattern) {
    *path = 0;
    *pattern = 0;
    if ((arg != 0) && (strlen(arg) > 0)) {
        // And argument was provided... see if there is a pattern
        if ((strchr(arg, '*') == 0) && (strchr(arg, '?') == 0)) {
            // No pattern found... return the whole thing as path
            *path = arg;

        } else {
            // Found a pattern... do we specify a folder?
            char * x = strrchr(arg, '/');
            if (x == 0) {
                // No... it's all pattern
                *pattern = arg;

            } else {
                // Yes... split the string into path and pattern at x
                *x = 0;
                *path = arg;
                *pattern = x + 1;
            }
        }
    }
}

short cmd_dir(short screen, int argc, const char * argv[]) {
    short result = 0, dir = -1;
    char buffer[160];
    char arg[128];
    t_file_info my_file;
    p_dir_entry directories = 0, files = 0, entry = 0, prev = 0;
    char *path=0, *pattern = 0;
    char label[40];

    if (argc > 1) {
        strcpy(arg, argv[1]);
        dir_parse_pattern(arg, &path, &pattern);
    }

    if (path == 0) {
        // Make the path the empty string if it was not provided
        path = "";
    }

    if (pattern != 0) {
        // A pattern was provided
        dir = sys_fsys_findfirst(path, pattern, &my_file);

    } else {
        // No pattern... just open the path as a directory
        dir = sys_fsys_opendir(path);
    }

    if (dir >= 0) {
        result = fsys_getlabel(path, label);
        if ((result == 0) && (strlen(label) > 0)) {
            sprintf(buffer, "Drive: %s\n", label);
            print(screen, buffer);
        }

        while (1) {
            if (pattern == 0) {
                result = sys_fsys_readdir(dir, &my_file);
                if (result != 0) break;
            }

            if (my_file.name[0] != 0) {
                if ((my_file.attributes & AM_HID) == 0) {
                    if (my_file.attributes & AM_DIR) {
                        entry = (p_dir_entry)malloc(sizeof(t_dir_entry));
                        if (entry) {
                            memcpy(&entry->info, &my_file, sizeof(t_file_info));
                            dir_entry_insert(&directories, entry);
                        } else {
                            print(screen, "Unable to display directory... out of memory.\n");
                            fsys_closedir(dir);     /* 2.1.0: give the handle back */
                            return -1;
                        }

                    } else {
                        entry = (p_dir_entry)malloc(sizeof(t_dir_entry));
                        if (entry) {
                            memcpy(&entry->info, &my_file, sizeof(t_file_info));
                            dir_entry_insert(&files, entry);
                        } else {
                            print(screen, "Unable to display directory... out of memory.\n");
                            fsys_closedir(dir);     /* 2.1.0: give the handle back */
                            return -1;
                        }
                    }
                }
            } else {
                break;
            }

            if (pattern != 0) {
                result = fsys_findnext(dir, &my_file);
                if (result != 0) {
                    break;
                }
            }
        }

        fsys_closedir(dir);

        // 2.0.34: paged (a long listing stops after a screenful); after q the rest is only freed
        cli_pager_start(screen);

        // Print the directories
        entry = directories;
        while (entry != 0) {
            sprintf(buffer, "%s/\n", entry->info.name);
            cli_pager_print(buffer);
            prev = entry;
            entry = entry->next;
            free(prev);
        }

        // Print the files
        // 2.0.34: the size checks used my_file (the last entry read) instead of the file itself,
        // so a file of 1 MB or more could show as thousands of KB; and the MB line was wider
        entry = files;
        while (entry != 0) {
            if (entry->info.size < 1024) {
                sprintf(buffer, "%-20.20s %d B\n", entry->info.name, (int)entry->info.size);
            } else if (entry->info.size < 1024*1024) {
                sprintf(buffer, "%-20.20s %d KB\n", entry->info.name, (int)(entry->info.size / 1024));
            } else {
                sprintf(buffer, "%-20.20s %d MB\n", entry->info.name, (int)(entry->info.size / (1024*1024)));
            }
            cli_pager_print(buffer);
            prev = entry;
            entry = entry->next;
            free(prev);
        }

        // 2.0.34: the free space on the drive
        {
            unsigned long free_kb = 0;
            if (fsys_getfree(path, &free_kb) == 0) {
                if (free_kb < 10UL * 1024) {
                    sprintf(buffer, "%lu KB free\n", free_kb);
                } else {
                    sprintf(buffer, "%lu MB free\n", free_kb / 1024);
                }
                cli_pager_print(buffer);
            }
        }

    } else {
        err_print(screen, "Unable to open directory", dir);
        return dir;
    }

    return 0;
}

/*
 * LS (2.0.34): a Unix-like listing that shows more than DIR
 *
 *   LS [-l] [-a] [<path>]
 *
 * <path> may be a folder, a single file, or a pattern with * and ?. Without -l the names are
 * listed in columns, sorted, folders with a '/' after them. With -l there is one line per entry:
 *
 *   drwxhs       size  yyyy-mm-dd hh:mm  name
 *
 * The flags are drwxhs, '-' when not set:
 *   d  a folder
 *   r  readable: always (FAT has no way to hide a file from reading)
 *   w  writable: unless the file is read-only (the only access flag FAT has)
 *   x  the MCP can run it: a folder (it can be entered), or a file whose extension has a loader
 *      (PGZ, PGX, ELF, ...)
 *   h  hidden, s  system: FAT attributes, shown by LS -a and left out otherwise
 * The size is in bytes. -a also shows hidden and system files.
 */

#define LS_MAX_COLUMN 48    /* names longer than this get a line of their own in the columns */

/*
 * Write the date and time of an entry as "yyyy-mm-dd hh:mm"
 *
 * FAT keeps the date as bits yyyyyyym mmmddddd (years since 1980) and the time as
 * hhhhhmmm mmmsssss (seconds / 2). A file without a date has 0 there.
 *
 * Inputs:
 * info = the directory entry
 * text = buffer of at least 17 bytes
 */
static void ls_date(const t_file_info * info, char * text) {
    if (info->date == 0) {
        strcpy(text, "                ");
    } else {
        sprintf(text, "%04u-%02u-%02u %02u:%02u",
            (unsigned short)(1980 + (info->date >> 9)), (unsigned short)((info->date >> 5) & 0x0f),
            (unsigned short)(info->date & 0x1f), (unsigned short)(info->time >> 11),
            (unsigned short)((info->time >> 5) & 0x3f));
    }
}

/*
 * Read the entries LS shows into a sorted list
 *
 * Inputs:
 * path = the folder ("" = the current one)
 * pattern = the names to show (* and ? allowed), or 0 for all
 * all = 1 to include hidden and system files
 * list = where to put the sorted list (free it with ls_free)
 *
 * Returns:
 * the number of entries, or a negative error number if the folder could not be read
 */
static short ls_read(const char * path, const char * pattern, short all, p_dir_entry * list) {
    t_file_info info;
    p_dir_entry entry;
    short dir, count = 0;

    *list = 0;
    if (pattern != 0) {
        dir = sys_fsys_findfirst(path, pattern, &info);
    } else {
        dir = sys_fsys_opendir(path);
        if ((dir >= 0) && (sys_fsys_readdir(dir, &info) != 0)) {
            info.name[0] = 0;
        }
    }
    if (dir < 0) {
        return dir;
    }

    while (info.name[0] != 0) {
        if (all || ((info.attributes & (AM_HID | AM_SYS)) == 0)) {
            entry = (p_dir_entry)malloc(sizeof(t_dir_entry));
            if (entry == 0) {
                break;          /* out of memory: show what we have */
            }
            memcpy(&entry->info, &info, sizeof(t_file_info));
            dir_entry_insert(list, entry);
            count++;
        }

        if (pattern != 0) {
            if (fsys_findnext(dir, &info) != 0) {
                break;
            }
        } else if (sys_fsys_readdir(dir, &info) != 0) {
            break;
        }
    }

    fsys_closedir(dir);
    return count;
}

/*
 * Free a list made by ls_read
 */
static void ls_free(p_dir_entry list) {
    p_dir_entry next;

    while (list != 0) {
        next = list->next;
        free(list);
        list = next;
    }
}

/*
 * Print the names in columns, top to bottom then left to right, as Unix ls does
 *
 * Inputs:
 * screen = the channel (for the width of the screen)
 * list = the sorted entries
 * count = how many there are
 */
static void ls_columns(short screen, p_dir_entry list, short count) {
    char buffer[MAX_PATH_LEN + 4];
    p_dir_entry * table;
    p_dir_entry entry;
    t_rect region;
    short i, row, col, rows, cols, width = 0, screen_width, len;

    sys_txt_get_region(sys_chan_device(screen), &region);
    screen_width = region.size.width;
    if (screen_width < 20) {
        screen_width = 80;
    }

    /* An array, so an entry can be found by its number */
    table = (p_dir_entry *)malloc(count * sizeof(p_dir_entry));
    if (table == 0) {
        for (entry = list; entry != 0; entry = entry->next) {
            sprintf(buffer, "%s%s\n", entry->info.name, (entry->info.attributes & AM_DIR) ? "/" : "");
            if (cli_pager_print(buffer)) {
                return;
            }
        }
        return;
    }

    /* The column width: the longest name (up to LS_MAX_COLUMN), its '/' and two spaces */
    for (entry = list, i = 0; entry != 0; entry = entry->next, i++) {
        table[i] = entry;
        len = strlen(entry->info.name) + ((entry->info.attributes & AM_DIR) ? 1 : 0);
        if ((len > width) && (len <= LS_MAX_COLUMN)) {
            width = len;
        }
    }
    width += 2;
    cols = screen_width / width;
    if (cols < 1) {
        cols = 1;
    }
    rows = (count + cols - 1) / cols;

    for (row = 0; row < rows; row++) {
        buffer[0] = 0;
        for (col = 0; col < cols; col++) {
            i = col * rows + row;
            if (i >= count) {
                break;
            }
            entry = table[i];
            len = strlen(entry->info.name) + ((entry->info.attributes & AM_DIR) ? 1 : 0);
            if (len > LS_MAX_COLUMN) {
                /* Too long for a column: a line of its own (after what is on this line) */
                if (buffer[0] != 0) {
                    strcat(buffer, "\n");
                    if (cli_pager_print(buffer)) {
                        free(table);
                        return;
                    }
                    buffer[0] = 0;
                }
                sprintf(buffer, "%s%s\n", entry->info.name, (entry->info.attributes & AM_DIR) ? "/" : "");
                if (cli_pager_print(buffer)) {
                    free(table);
                    return;
                }
                buffer[0] = 0;
                continue;
            }
            /* The last column needs no spaces after it */
            sprintf(buffer + strlen(buffer), "%s%s", entry->info.name, (entry->info.attributes & AM_DIR) ? "/" : "");
            if ((col < cols - 1) && (i + rows < count)) {
                while (len++ < width) {
                    strcat(buffer, " ");
                }
            }
            /* Print what is there before it gets too long for the buffer */
            if (strlen(buffer) + width > MAX_PATH_LEN) {
                if (cli_pager_print(buffer)) {
                    free(table);
                    return;
                }
                buffer[0] = 0;
            }
        }
        strcat(buffer, "\n");
        if (cli_pager_print(buffer)) {
            break;
        }
    }

    free(table);
}

/*
 * Print one line per entry with its attributes, size, date and name, and a total at the end
 *
 * Inputs:
 * path = the folder (for the free space)
 * list = the sorted entries
 */
static void ls_long(const char * path, p_dir_entry list) {
    char buffer[MAX_PATH_LEN + 48];
    char date[20];
    char size[16];
    p_dir_entry entry;
    unsigned char a;
    unsigned long total = 0, free_kb = 0;
    short files = 0, folders = 0;

    for (entry = list; entry != 0; entry = entry->next) {
        a = entry->info.attributes;
        ls_date(&entry->info, date);
        if (a & AM_DIR) {
            strcpy(size, "");
            folders++;
        } else {
            sprintf(size, "%lu", (unsigned long)entry->info.size);
            total += (unsigned long)entry->info.size;
            files++;
        }
        sprintf(buffer, "%cr%c%c%c%c %10s  %s  %s%s\n",
            (a & AM_DIR) ? 'd' : '-',
            (a & AM_RDO) ? '-' : 'w',
            ((a & AM_DIR) || fsys_is_executable(entry->info.name)) ? 'x' : '-',
            (a & AM_HID) ? 'h' : '-',
            (a & AM_SYS) ? 's' : '-',
            size, date, entry->info.name, (a & AM_DIR) ? "/" : "");
        if (cli_pager_print(buffer)) {
            return;
        }
    }

    sprintf(buffer, "%d folder%s, %d file%s, %lu bytes", folders, (folders == 1) ? "" : "s",
        files, (files == 1) ? "" : "s", total);
    if (fsys_getfree(path, &free_kb) == 0) {
        if (free_kb < 10UL * 1024) {
            sprintf(buffer + strlen(buffer), "; %lu KB free", free_kb);
        } else {
            sprintf(buffer + strlen(buffer), "; %lu MB free", free_kb / 1024);
        }
    }
    strcat(buffer, "\n");
    cli_pager_print(buffer);
}

/*
 * LS [-l] [-a] [<path>]: list files, see above
 *
 * Inputs:
 * screen = the channel to print on
 * argc, argv = the command line
 *
 * Returns:
 * 0 on success, a negative error number otherwise
 */
short cmd_ls(short screen, int argc, const char * argv[]) {
    char arg[MAX_PATH_LEN];
    char *path = 0, *pattern = 0, *slash;
    p_dir_entry list = 0;
    short i, count, is_long = 0, all = 0;
    const char * o;

    arg[0] = 0;
    for (i = 1; i < argc; i++) {
        if (argv[i][0] == '-') {
            for (o = argv[i] + 1; *o != 0; o++) {
                if ((*o == 'l') || (*o == 'L')) {
                    is_long = 1;
                } else if ((*o == 'a') || (*o == 'A')) {
                    all = 1;
                } else {
                    print(screen, "USAGE: LS [-l] [-a] [<path>]\n");
                    return ERR_BAD_ARGUMENT;
                }
            }
        } else {
            strncpy(arg, argv[i], MAX_PATH_LEN - 1);
            arg[MAX_PATH_LEN - 1] = 0;
        }
    }

    dir_parse_pattern(arg, &path, &pattern);
    if (path == 0) {
        path = "";
    }

    count = ls_read(path, pattern, all, &list);
    if ((count < 0) && (pattern == 0) && (path[0] != 0)) {
        /* Not a folder: perhaps a single file. Look for its name in its folder. */
        slash = strrchr(path, '/');
        if (slash == 0) {
            pattern = path;
            path = "";
        } else if (slash[1] != 0) {
            pattern = slash + 1;
            if (slash == path) {
                path = "/";
            } else {
                *slash = 0;
            }
        }
        if (pattern != 0) {
            count = ls_read(path, pattern, 1, &list);
        }
    }

    if (count < 0) {
        err_print(screen, "Unable to list", count);
        return count;
    }

    cli_pager_start(screen);
    if (count == 0) {
        if (pattern != 0) {
            print(screen, "No files match.\n");
        }
    } else if (is_long) {
        ls_long(path, list);
    } else {
        ls_columns(screen, list, count);
    }

    ls_free(list);
    return 0;
}

/*
 * Print the contents of a file to the screen
 */
short cmd_type(short screen, int argc, const char * argv[]) {
    if (argc > 1) {
        unsigned char buffer[128];

        log3(LOG_INFO, "Attempting to type [", (char*)(argv[1]), "]");
        short fd = fsys_open(argv[1], FA_READ);
        if (fd >= 0) {
            log_num(LOG_INFO, "File open: ", fd);
            cli_pager_start(screen);        // 2.0.34: paged
            while (1) {
                short n = chan_read(fd, buffer, 128);
                log_num(LOG_INFO, "cmd_type chan_read: ", n);
                if (n > 0) {
                    if (cli_pager_write((const char *)buffer, n)) {
                        break;              // q or Esc at the pause
                    }
                } else {
                    break;
                }
            }

            fsys_close(fd);
            return 0;

        } else {
            err_print(screen, "Unable to open file", fd);
            return fd;
        }
    } else {
        print(screen, "USAGE: TYPE <path>\n");     // 2.0.34: on the screen, not only in the log
        return -1;
    }
}

/*
 * Load a binary file into memory
 * parameters: path [address]
 */
short cmd_load(short screen, int argc, const char * argv[]) {
    if (argc > 1) {
        long start = 0;
        long destination = 0;

        if (argc > 2) {
            destination = cli_eval_number(argv[2]);
        }

        short result = fsys_load(argv[1], destination, &start);
        if (result == 0) {
            if (start != 0) {
                logmsg(LOG_INFO, "Loaded file with a start adddress.");
            } else {
                logmsg(LOG_INFO, "File loaded.");
            }
        } else {
            err_print(screen, "Unable to open file", result);
            return -1;
        }

        return result;
    } else {
        logmsg(LOG_ERROR, "Usage: LOAD <path> [<destination>]");
        return -1;
    }
}

/*
 * Set the label of a drive
 *
 * LABEL <drive #> <label>
 *
 */
short cmd_label(short screen, int argc, const char * argv[]) {
    if (argc > 2) {
        short drive = cli_eval_number(argv[1]);
        short result = fsys_setlabel(drive, argv[2]);
        if (result != 0) {
            err_print(screen, "Unable to set volume label", result);
            return -1;
        }

    } else {
        print(screen, "USAGE: LABEL <drive #> <label>\n");
        return -1;
    }
    return 0;       // 2.1.0: success did not return anything
}

/**
 * Command to make a device bootable by writing to the MBR or VBR
 *
 * MKBOOT <drive #> -r --- removes boot record
 * MKBOOT <drive #> -b <boot record path> --- installs boot record
 * MKBOOT <drive #> -s <start file path> --- defines a startup file
 */
short cmd_mkboot(short screen, int argc, const char * argv[]) {
    const char * usage = "USAGE: MKBOOT <drive #> -r\n       MKBOOT <drive #> -b <boot record path>\n       MKBOOT <drive #> -s <start file path>\n";
    short mode = 0;
    unsigned char * boot_sector = 0;
    unsigned char * new_boot_sector = 0;
    char message[160];
    short dev = 0;
    short i = 0;
    short result = 0;

    // Parse the inputs...
    if (argc == 3) {
        // Must be -r
        if (strcmp("-r", argv[2]) == 0) {
            mode = 0;
            dev = cli_eval_number(argv[1]);
        } else {
            print(screen, usage);
            return -1;
        }

    } else if (argc == 4) {
        // Can be either -b or -s
        dev = cli_eval_number(argv[1]);

        if (strcmp("-b", argv[2]) == 0) {
            // -b
            mode = 1;

        } else if (strcmp("-s", argv[2]) == 0) {
            // -s
            mode = 2;

        } else {
            print(screen, usage);
            return -1;
        }

    } else {
        // Bad arguments...
        print(screen, usage);
        return -1;
    }

    switch (mode) {
        case 0:
            // Clear out the boot record
            result = boot_non_booting(dev);
            if (result != 0) {
                sprintf(message, "Could not change boot record: %s\n", err_message(result));
                print(screen, message);
            } else {
                print(screen, "Boot record updated.\n");
            }
            break;

        case 2:
            // Write a boot sector that loads and runs a file
             result = boot_set_file(dev, argv[3]);
             if (result != 0) {
                 sprintf(message, "Could not change boot record: %s\n", err_message(result));
                 print(screen, message);
             } else {
                 print(screen, "Boot record updated.\n");
             }
             break;

        default:
            print(screen, "Unknown MKBOOT operation.\n");
            result = -1;
            break;
    }

    return result;
}

/*
 * Format a drive
 *
 * FORMAT <drive #>
 */
short cmd_format(short screen, int argc, const char * argv[]) {
    if (argc > 1) {
        short drive = cli_eval_number(argv[1]);
        short result = fsys_mkfs(drive, "");
        if (result != 0) {
            err_print(screen, "Unable to format volume", result);
            return -1;
        }

    } else {
        print(screen, "USAGE: FORMAT <drive #>\n");
        return -1;
    }
    return 0;       // 2.1.0: success did not return anything
}
