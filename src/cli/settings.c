/*
 * Mechanism for handling the SET command
 *
 * SET <setting name> <setting value>
 */

#include <ctype.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "cli.h"
#include "log.h"
#include "errors.h"
#include "settings.h"
#include "net_cmds.h"
#include "simpleio.h"
#include "syscalls.h"
#include "interrupt.h"
#include "rtc_reg.h"
#include "dev/rtc.h"
#include "dev/txt_screen.h"
#include "snd/codec.h"
#include "utilities.h"
#include "vicky_general.h"

#if MODEL == MODEL_FOENIX_A2560K
#include "dev/kbd_mo.h"
#include "dev/console.h"
#endif

#define MAX_SETTING_NAME    64
#define MAX_SETTING_HELP    128     /* 2.1.2: longest help line; was 80, the CLOCK line is longer */

/*
 * Structure to hold a setting
 */
typedef struct s_setting {
    char name[MAX_SETTING_NAME];        /* Name of the setting (in upper case) */
    char help[MAX_SETTING_HELP + 1];    /* Help line for this setting (2.1.2: + 1 for the terminating 0, which
                                         * was written past the end and then overwritten by setter, so SET
                                         * HELP printed garbage after a line of 80 characters or more) */
    cli_setter setter;                  /* The function to set the value of the setting */
    cli_getter getter;                  /* The function to get the value of the setting */
    struct s_setting * next;            /* Pointer to the next registered setting */
} t_setting, *p_setting;

static p_setting cli_first_setting;    /* Pointer to the first registered setting */
static p_setting cli_last_setting;     /* Pointer to the last registered setting */

#if MODEL == MODEL_FOENIX_A2560K || MODEL == MODEL_FOENIX_A2560X || MODEL == MODEL_FOENIX_GENX
static void color_help(short channel);
#endif

/*
 * Copy the name to a buffer as an upper case setting name
 *
 * Inputs:
 * name_upper = pointer to the buffer of at least MAX_SETTING_NAME bytes to receive the copy
 * name = the name to copy
 */
void cli_name_upper(char * name_upper, const char *name) {
    short i;

    for (i = 0; (i < MAX_SETTING_NAME) && (name[i] != 0); i++) {
        name_upper[i] = toupper(name[i]);
    }
    name_upper[i] = 0;
}

/*
 * Add a setting
 *
 * Inputs:
 * name = the name of the setting
 * setter = the function to change the value of the setting
 * getter = the function to get the value of a setting
 *
 * Returns:
 * 0 on success, any other number is an error
 */
short cli_set_register(const char * name, const char * help, cli_setter setter, cli_getter getter) {
    p_setting setting = (p_setting)malloc(sizeof(t_setting));
    if (setting == 0) {
        /* Could not allocate the setting record... return an error */
        return -1;

    } else {
        /* Set the fields for the setting */
        cli_name_upper(setting->name, name);
        strncpy(setting->help, help, MAX_SETTING_HELP);
        setting->help[MAX_SETTING_HELP] = '\0';
        setting->setter = setter;
        setting->getter = getter;
        setting->next = 0;

        if (cli_first_setting == 0) {
            /* This is the first and last setting so far */
            cli_first_setting = setting;
            cli_last_setting = setting;

        } else {
            /* This is the new last setting */
            cli_last_setting->next = setting;
            cli_last_setting = setting;
        }

        /* Return success */
        return 0;
    }
}

/*
 * Find the setting that matches the name (case insensitive search)
 *
 * Inputs:
 * name = the setting name to find
 *
 * Returns:
 * pointer to the matching setting (0 if none found)
 */
p_setting cli_find_setting(const char * name) {
    char name_upper[MAX_SETTING_NAME];
    p_setting setting;

    /* Convert the name to upper case */
    cli_name_upper(name_upper, name);

    /* Walk through the list of settings to find a name that matches */
    for (setting = cli_first_setting; setting != 0; setting = setting->next) {
        if (strcmp(setting->name, name_upper) == 0) {
            /* Found the setting... */
            return setting;
        }
    }

    /* Didn't find the setting... */
    return 0;
}

/*
 * Set the value of a setting
 *
 * Inputs:
 * name = the name of the setting to update (case insensitive)
 * value = the value to store in the setting (may be interpreted by the setting)
 *
 * Returns:
 * 0 on success, any other number is an error
 */
short cli_set_value(short channel, const char * name, const char * value) {
    p_setting setting = cli_find_setting(name);
    if (setting == 0) {
        /* Setting not found: set a system variable */
        return sys_var_set(name, value);

    } else {
        /* Attempt to set the value, and return the results */
        return setting->setter(channel, value);
    }
}

/*
 * Get the value of a setting
 *
 * Inputs:
 * name = the name of the setting to update (case insensitive)
 * buffer = the place to copy a string representing the value of the setting
 * size = the number of characters that can be stored in the buffer
 *
 * Returns:
 * 0 on success, any other number is an error
 */
short cli_get_value(short channel, const char * name, char * buffer, short size) {
    p_setting setting = cli_find_setting(name);
    if (setting == 0) {
        /* Setting not found: try to get an environment variable */
        const char * value = sys_var_get(name);
        if (value) {
            strncpy(buffer, value, size);
            return 0;

        } else {
            return ERR_NOT_FOUND;
        }

    } else if (setting->getter == 0) {
        /* 2.0.32: a setting that can only be set (CLOCK) */
        return ERR_NOT_SUPPORTED;

    } else {
        /* Attempt to set the value, and return the results */
        return setting->getter(channel, buffer, size);
    }
}

/*
 * 2.1.0: programmable function keys
 *
 * F1 - F8 and Shift+F1 - Shift+F8 (written +F1 - +F8) can each hold a command line. Pressing the
 * key on the command line runs it at once (see cli_readline in cli.c). The commands are set with
 * SET F1 <command> and shown with GET F1 or GET FX (all keys). They are not normal settings: the
 * command is the whole rest of the SET line, spaces included, while a normal setting takes one
 * word. To keep them after a restart, put the SET lines in system/mcp.init.
 *
 * The A2560K keyboard has no F9 and F10, so only F1 - F8 are used.
 */

#define FKEY_COUNT      8       /* F1 - F8 */
#define FKEY_MAX        128     /* longest command + 1: the size of a command line (cli.c) */

/* The commands: [0..7] = F1 - F8, [8..15] = +F1 - +F8. An empty string = no command. */
static char fkey_command[2 * FKEY_COUNT][FKEY_MAX];

/**
 * Find which function key a name means.
 *
 * @param name the name from the SET or GET line: F1 - F8 or +F1 - +F8 (upper or lower case)
 * @return 0 - 7 for F1 - F8, 8 - 15 for +F1 - +F8, or -1 if the name is not a function key
 */
static short fkey_index(const char * name) {
    short shifted = 0;

    if (name[0] == '+') {
        shifted = 1;
        name++;
    }

    if (((name[0] == 'F') || (name[0] == 'f')) && (name[1] >= '1') && (name[1] <= '8') && (name[2] == 0)) {
        return (short)(shifted * FKEY_COUNT + (name[1] - '1'));
    }
    return -1;
}

/**
 * Write the name of a function key ("F1" or "+F1") into a buffer.
 *
 * @param index 0 - 15, as given by fkey_index
 * @param name the buffer, at least 4 characters
 */
static void fkey_name(short index, char * name) {
    sprintf(name, "%sF%d", (index >= FKEY_COUNT) ? "+" : "", (short)(index % FKEY_COUNT + 1));
}

/**
 * SET F1 <command>: store a command for a function key.
 *
 * The command line was cut into words at the spaces before the SET command got it, so the words
 * after the key name are put back together with one space between them.
 *
 * @param channel the channel for messages
 * @param index the key, 0 - 15 (see fkey_index)
 * @param argc the number of words on the SET line
 * @param argv the words: SET, the key name, then the command (no command = clear the key)
 * @return 0 on success, -1 if the command is too long (the key is then cleared)
 */
static short fkey_set(short channel, short index, int argc, const char * argv[]) {
    char * command = fkey_command[index];
    short len = 0;
    int a;

    command[0] = 0;
    for (a = 2; a < argc; a++) {
        short n = (short)strlen(argv[a]);
        short space = (a > 2) ? 1 : 0;

        if (len + space + n >= FKEY_MAX) {
            command[0] = 0;
            print(channel, "Unable to change setting: the command is too long\n");
            return -1;
        }
        if (space) {
            command[len++] = ' ';
        }
        strcpy(command + len, argv[a]);
        len += n;
    }
    return 0;
}

/**
 * Print the command of one function key, as "F1   DIR" (GET FX) or just the command (GET F1).
 *
 * @param channel the channel to print on
 * @param index the key, 0 - 15 (see fkey_index)
 * @param with_name non-zero to put the key name in front
 */
static void fkey_print(short channel, short index, short with_name) {
    char name[4];

    if (with_name) {
        char line[16];
        fkey_name(index, name);
        sprintf(line, "%-4s ", name);
        print(channel, line);
    }
    print(channel, (fkey_command[index][0] != 0) ? fkey_command[index] : "(not set)");
    print(channel, "\n");
}

/**
 * The command of a function key, for the command line (cli_readline in cli.c).
 *
 * @param number the key number, 1 - 8 (other numbers have no command)
 * @param shifted non-zero if Shift was held down
 * @return the command, or 0 if the key has none
 */
const char * cli_fkey_command(short number, short shifted) {
    const char * command;

    if ((number < 1) || (number > FKEY_COUNT)) {
        return 0;
    }
    command = fkey_command[(shifted ? FKEY_COUNT : 0) + number - 1];
    return (command[0] != 0) ? command : 0;
}

/*
 * Print help information about the SET command
 */
void cli_set_help(short channel) {
    char message[160];
    p_setting setting;

    sprintf(message, "SET/GET command supported settings:\n");
    sys_chan_write(channel, message, strlen(message));

    for (setting = cli_first_setting; setting != 0; setting = setting->next) {
        sys_chan_write(channel, setting->help, strlen(setting->help));
        sys_chan_write(channel, "\n", 1);
    }
    print(channel, "F1-F8|+F1-+F8 <command> -- run <command> with a function key (+ = Shift; GET FX lists them)\n");
    print(channel, "NET DHCP ON|OFF, NET IP|MASK|GATEWAY a.b.c.d, NET MAC xx:..:xx -- network settings (GET NET lists them)\n");

#if MODEL == MODEL_FOENIX_A2560K || MODEL == MODEL_FOENIX_A2560X || MODEL == MODEL_FOENIX_GENX
    color_help(channel);
#endif
}

/*
 * Command to set the value of a setting
 */
short cli_cmd_set(short channel, int argc, const char * argv[]) {
    char message[160];
    short result;

    if ((argc >= 2) && (fkey_index(argv[1]) >= 0)) {
        /* 2.1.0: a function key takes the whole rest of the line, not one word */
        return fkey_set(channel, fkey_index(argv[1]), argc, argv);

    } else if ((argc >= 2) && (strcicmp(argv[1], "NET") == 0)) {
        /* 2.1.0: SET NET <name> <value> has two words after SET NET */
        return net_cmd_set(channel, argc, argv);

    } else if (argc == 3) {
        result = cli_set_value(channel, argv[1], argv[2]);
        if (result != 0) {
            sprintf(message, "Unable to change setting: %s\n", sys_err_message(result));
            sys_chan_write(channel, message, strlen(message));
            return 0;
        }
        return result;

    } else if ((argc == 2) && (strcmp(argv[1], "HELP") == 0 || strcmp(argv[1], "help") == 0 || strcmp(argv[1], "?") == 0)) {
        cli_set_help(channel);
        return 0;

    } else {
        print(channel, "USAGE: SET <name> <value>\n");
        return -1;
    }
}

/*
 * Command to set the value of a setting
 */
short cli_cmd_get(short channel, int argc, const char * argv[]) {
    char buffer[128];
    short result;

    if ((argc >= 2) && (strcicmp(argv[1], "NET") == 0)) {
        /* 2.1.0: GET NET lists the network settings, GET NET <name> shows one */
        return net_cmd_get(channel, argc, argv);
    }

    if (argc == 2) {

        if ((strcmp(argv[1], "HELP") == 0) || (strcmp(argv[1], "help") == 0) || (strcmp(argv[1], "?") == 0)) {
            cli_set_help(channel);

        } else if (strcicmp(argv[1], "FX") == 0) {
            /* 2.1.0: GET FX lists the commands of all function keys */
            short index;
            for (index = 0; index < 2 * FKEY_COUNT; index++) {
                fkey_print(channel, index, 1);
            }

        } else if (fkey_index(argv[1]) >= 0) {
            /* 2.1.0: GET F1 shows the command of one function key */
            fkey_print(channel, fkey_index(argv[1]), 0);

        } else {
            result = cli_get_value(channel, (char*)argv[1], buffer, 128);
            if (result == 0) {
                print(channel, buffer);
                print(channel, "\n");
                return 0;

            } else {
                sprintf(buffer, "Unable to get setting: %s\n", sys_err_message(result));
                sys_chan_write(channel, buffer, strlen(buffer));
                return result;
            }
        }

    } else {
        print(channel, "USAGE: GET <name>\n");
        return -1;
    }
    return 0;       // 2.1.0: GET HELP did not return anything
}

/*
 * SOF setter
 */
short cli_sof_set(short channel, const char * value) {
    char message[160];

    if (strcmp(value, "1") == 0) {
        int_enable(INT_SOF_A);
        sprintf(message, "Start Of Frame interrupt enabled.\n");
    } else if (strcmp(value, "0") == 0) {
        int_disable(INT_SOF_A);
        sprintf(message, "Start Of Frame interrupt disabled.\n");
    } else {
        sprintf(message, "USAGE: SET SOF 0|1\n");
    }

    sys_chan_write(channel, message, strlen(message));
    return 0;
}

/*
 * SOF getter
 */
short cli_sof_get(short channel, char * buffer, short size) {
    return 0;
}

/*
 * RTC setter
 */
short cli_rtc_set(short channel, const char * value) {
    char message[160];

    if (strcmp(value, "1") == 0) {
        rtc_enable_ticks();
        sprintf(message, "RTC interrupt enabled.\n");

    } else if (strcmp(value, "0") == 0) {
        int_disable(INT_RTC);
        sprintf(message, "RTC interrupt disabled.\n");

    } else {
        sprintf(message, "USAGE: SET RTC 0|1\n");
    }

    sys_chan_write(channel, message, strlen(message));
    return 0;
}

/*
 * RTC getter
 */
short cli_rtc_get(short channel, char * buffer, short size) {
    return 0;
}

short atoi_n(char * text, short n) {
    short result = 0;
    short i;

    for (i = 0; i < n; i++) {
        result = result * 10;
        result = result + (text[i] - '0');
    }

    return result;
}

/*
 * CLOCK setter (2.0.32):
 *   CLOCK ON / OFF   show / stop the time in the right-hand end of the title bar
 *   CLOCK 24 / 12    show it as 16:30:05 or as 04:30:05 PM (and switch it on)
 * Upper or lower case, and 1 / 0 for ON / OFF, are accepted too.
 * 2.1.0: CLOCK is a command of its own (cmd_clock in cli.c); SET CLOCK still works the same, so
 * existing mcp.init files keep working.
 */
short cli_clock_set(short channel, const char * value) {
    const char * usage = "USAGE: CLOCK ON|OFF|12|24\n";

    if ((toupper(value[0]) == 'O') && (toupper(value[1]) == 'N') && (value[2] == 0)) {
        rtc_clock_set(1);
    } else if ((toupper(value[0]) == 'O') && (toupper(value[1]) == 'F') && (toupper(value[2]) == 'F') && (value[3] == 0)) {
        rtc_clock_set(0);
    } else if ((value[0] == '1') && (value[1] == 0)) {
        rtc_clock_set(1);
    } else if ((value[0] == '0') && (value[1] == 0)) {
        rtc_clock_set(0);
    } else if ((value[0] == '2') && (value[1] == '4') && (value[2] == 0)) {
        rtc_clock_set_24h(1);
        rtc_clock_set(1);
    } else if ((value[0] == '1') && (value[1] == '2') && (value[2] == 0)) {
        rtc_clock_set_24h(0);
        rtc_clock_set(1);
    } else {
        sys_chan_write(channel, usage, strlen(usage));
        return ERR_GENERAL;
    }
    return 0;
}

/*
 * DATE setter
 */
short cli_date_set(short channel, const char * date) {
    t_time date_time;
    short i;
    const char * usage = "USAGE: SET DATE yyyy-mm-dd\n";

    rtc_get_time(&date_time);
    date_time.is_24hours = 1;
    date_time.is_pm = 0;

    /* Validate date is correct format */

    for (i = 0; i < 10; i++) {
        if ((i == 4) || (i == 7)) {
            if (date[i] != '-') {
                sys_chan_write(channel, usage, strlen(usage));
                return ERR_GENERAL;
            }
        } else {
            if ((date[i] < '0') || (date[i] > '9')) {
                sys_chan_write(channel, usage, strlen(usage));
                return ERR_GENERAL;
            }
        }
    }

    date_time.year = atoi_n(&((char*)date)[0], 4);
    date_time.month = atoi_n(&((char*)date)[5], 2);
    date_time.day = atoi_n(&((char*)date)[8], 2);

    rtc_set_time(&date_time);

    return 0;
}

/*
 * DATE getter
 */
short cli_date_get(short channel, char * value, short size) {
    t_time time;

    rtc_get_time(&time);
    sprintf(value, "%04d-%02d-%02d\n", time.year, time.month, time.day);

    return 0;
}

/*
 * TIME setter
 */
short cli_time_set(short channel, const char * time) {
    t_time date_time;
    short i;
    const char * usage = "USAGE: SET TIME HH:MM:SS\n";

    rtc_get_time(&date_time);
    date_time.is_24hours = 1;
    date_time.is_pm = 0;

    /* Validate time is correct format */

    for (i = 0; i < 8; i++) {
        if ((i == 2) || (i == 5)) {
            if (time[i] != ':') {
                sys_chan_write(channel, usage, strlen(usage));
                return ERR_GENERAL;
            }
        } else {
            if ((time[i] < '0') || (time[i] > '9')) {
                sys_chan_write(channel, usage, strlen(usage));
                return ERR_GENERAL;
            }
        }
    }

    date_time.hour = atoi_n(&((char*)time)[0], 2);
    date_time.minute = atoi_n(&((char*)time)[3], 2);
    date_time.second = atoi_n(&((char*)time)[6], 2);

    rtc_set_time(&date_time);

    return 0;
}

/*
 * TIME getter
 */
short cli_time_get(short channel, char * value, short size) {
    t_time time;

    rtc_get_time(&time);
    sprintf(value, "%02d:%02d:%02d\n", time.hour, time.minute, time.second);

    return 0;
}

short cli_set_font(short screen, const char * path) {
    const unsigned long load_address = 0x10000;
    unsigned long jump_address = 0;
    char message[160];
    t_file_info filinfo;

    short result = sys_fsys_stat(path, &filinfo);
    if (result == 0) {
        if (filinfo.size == 256 * 8) {
            // It's a simple 8x8 font
            result = sys_fsys_load(path, load_address, &jump_address);
            if (result == 0) {
                sys_txt_set_font(screen, 8, 8, (unsigned char *)load_address);
                return 0;
            } else {
                return result;
            }

        } else {
            // It's 8x16.. check to make sure this device can support that
            p_txt_capabilities txt_caps = sys_txt_get_capabilities(screen);
            if (txt_caps != 0) {
                short supports_8x16 = 0;
                int i;
                for (i = 0; i < txt_caps->font_size_count; i++) {
                    p_extent font_size = &(txt_caps->font_sizes[i]);
                    if ((font_size->width == 8) && (font_size->height == 16)) {
                        // 8x16 is supported... load the font
                        result = sys_fsys_load(path, load_address, &jump_address);
                        if (result == 0) {
                            sys_txt_set_font(screen, 8, 16, (unsigned char *)load_address);
                            return 0;
                        } else {
                            return result;
                        }
                    }
                }

                // 8x16 is not a supported font size
                return ERR_NOT_SUPPORTED;

            } else {
                return -1;
            }

        }
    } else {
        return result;
    }
}

/**
 * Font@0 setter -- SET FONT@0 <path>
 *
 * Sets the font for the main screen (channel B)
 */
short cli_font0_set(short screen, const char * value) {
    // Load the font into memory
    short result = cli_set_font(0, value);
    return result;
}

/**
 * Font@0 getter -- GET FONT@0
 */
short cli_font0_get(short screen, char * value, short size) {
    value[0] = 0;
    return 0;
}

/**
 * Font@1 setter -- SET FONT@1 <path>
 *
 * Sets the font for the secondary screen (channel A or EVID)
 */
short cli_font1_set(short screen, const char * value) {
    // Load the font into memory
    short result = cli_set_font(1, value);
    return result;
}

/**
 * Font@0 getter -- GET FONT@0
 */
short cli_font1_get(short screen, char * value, short size) {
    value[0] = 0;
    return 0;
}

/*
 * Volume setter -- SET VOLUME <value>
 */
short cli_volume_set(short channel, const char * value) {
    unsigned char volume = (unsigned char)cli_eval_number(value);
    codec_set_volume(volume);
    return 0;
}

/*
 * Volume getter -- GET VOLUME
 */
short cli_volume_get(short channel, char * value, short size) {
    sprintf(value, "%d", (short)codec_get_volume());
    return 0;
}

/*
 * Set the keyboard layout given a keyboard layout file -- SET LAYOUT <path>
 */
short cli_layout_set(short channel, const char * value) {
    short result, fd;
    char buffer[1024];      /* Temporary storage for the translation tables */

    /* Try to open the file */
    fd = sys_fsys_open(value, 1);
    if (fd >= 0) {
        /* Try to read the data */
        result = sys_chan_read(fd, buffer, 1024);

        if (result > 0) {
            /* If we got something, set the layout */
            result = sys_kbd_layout(buffer);
        }

        sys_fsys_close(fd);
        return result;
    } else {
        return fd;
    }
}

/*
 * Get the keyboard layout given a keyboard layout file -- GET LAYOUT
 */
short cli_layout_get(short channel, char * value, short size) {
    value[0] = 0;
    return 0;
}

#if MODEL == MODEL_FOENIX_A2560K

unsigned short kbd_mo_color = 0x0000;

short cli_keycolor_set(short channel, const char * value) {
    kbd_mo_color = (unsigned short)cli_eval_number(value);
    kbdmo_set_led_matrix_fill(kbd_mo_color);
    return 0;
}

short cli_keycolor_get(short channel, char * value, short size)  {
    sprintf(value, "%04x", kbd_mo_color);
    return 0;
}

/*
 * SET CONSOLE OFF|COM1|COM2: copy console output to a serial port (2.1.2)
 *
 * Input:  channel = the console, for the usage text; value = OFF, COM1 or COM2 (any case)
 * Output: 0, or ERR_GENERAL for another value
 *
 * For a terminal on a serial port (115200 8N1) as a remote console. OFF by default: copying waits
 * for the UART for every character and made console output many times slower. Keys typed on COM1
 * are read either way.
 */
short cli_console_set(short channel, const char * value) {
    const char * usage = "USAGE: SET CONSOLE OFF|COM1|COM2\n";
    short target;

    if (strcicmp(value, "OFF") == 0) {
        target = CON_COPY_OFF;
    } else if (strcicmp(value, "COM1") == 0) {
        target = CON_COPY_COM1;
    } else if (strcicmp(value, "COM2") == 0) {
        target = CON_COPY_COM2;
    } else {
        sys_chan_write(channel, usage, strlen(usage));
        return ERR_GENERAL;
    }
    return con_set_copy(target);
}

/*
 * GET CONSOLE: where console output is copied to (2.1.2)
 *
 * Input:  channel = not used; value = where to put "OFF", "COM1" or "COM2"; size = its size
 * Output: 0
 */
short cli_console_get(short channel, char * value, short size) {
    short target = con_get_copy();
    strncpy(value, (target == CON_COPY_COM1) ? "COM1" : ((target == CON_COPY_COM2) ? "COM2" : "OFF"), size);
    return 0;
}

#endif

#if MODEL == MODEL_FOENIX_A2560K || MODEL == MODEL_FOENIX_A2560X || MODEL == MODEL_FOENIX_GENX

/*
 * SET FG and SET BG: the colours of the text screen.
 *
 * The screen does not store a colour per character, but a number (0-15)
 * that points into a table of 16 colours (the "LUT"). Programs and the
 * MCP itself pick those numbers, also through ANSI codes such as ESC[0m
 * and ESC[37m. So instead of changing the numbers (which the next ANSI
 * code would undo), these settings change the colours in the table:
 *
 *   - normal text uses number 7: SET FG changes colour 7
 *   - the background uses number 0: SET BG changes colour 0. Vicky
 *     shows that colour as transparent, so SET BG also sets the
 *     screen's background colour register, which shows through
 *
 * Both are changed in the foreground and the background table, because
 * the title bar swaps them (text 0 on background 7). Everything already
 * on the screen changes colour at once. Other colours (bold text, the
 * banner, ...) stay as they are.
 */

#define COLOR_FG_INDEX  7       /* LUT entry of normal text */
#define COLOR_BG_INDEX  0       /* LUT entry of the background */

typedef struct s_color_name {
    const char * name;
    unsigned long rgb;          /* 0x00RRGGBB */
} t_color_name;

/* The 16 predefined colours: the MCP's own start-up colours */
static const t_color_name color_names[] = {
    { "BLACK",       0x000000 },
    { "RED",         0x800000 },
    { "GREEN",       0x008000 },
    { "YELLOW",      0x808000 },
    { "BLUE",        0x000080 },
    { "ORANGE",      0xAA5500 },
    { "CYAN",        0x008080 },
    { "GREY",        0x808080 },
    { "DARKGREY",    0x555555 },
    { "LIGHTRED",    0xFF0000 },
    { "LIGHTGREEN",  0x55FF55 },
    { "LIGHTYELLOW", 0xFFFF55 },
    { "LIGHTBLUE",   0x5555FF },
    { "PINK",        0xFF7FFF },
    { "LIGHTCYAN",   0x55FFFF },
    { "WHITE",       0xFFFFFF },
    { 0, 0 }
};

static unsigned long color_fg = 0x808080;   /* what SET FG last set (start: GREY) */
static unsigned long color_bg = 0x000000;   /* what SET BG last set (start: BLACK) */

/*
 * Turn a colour as the user typed it into an RGB value
 *
 * Inputs:
 * value = a colour name (BLACK, RED, ... see color_names, any case), or
 *         RGBrrrgggbbb with three decimal numbers 0-255 of three digits
 *         each, e.g. RGB255128000 for orange
 * rgb = where to put the colour as 0x00RRGGBB
 *
 * Returns:
 * 0 on success, ERR_BAD_ARGUMENT if the colour is not understood
 */
static short color_parse(const char * value, unsigned long * rgb) {
    short i, j;

    for (i = 0; color_names[i].name != 0; i++) {
        if (strcicmp(value, color_names[i].name) == 0) {
            *rgb = color_names[i].rgb;
            return 0;
        }
    }

    /* RGB and exactly nine digits: three groups of three */
    if ((toupper(value[0]) == 'R') && (toupper(value[1]) == 'G') && (toupper(value[2]) == 'B') && (strlen(value) == 12)) {
        unsigned long result = 0;
        for (i = 0; i < 3; i++) {
            short part = 0;
            for (j = 0; j < 3; j++) {
                char c = value[3 + i * 3 + j];
                if (!isdigit(c)) {
                    return ERR_BAD_ARGUMENT;
                }
                part = part * 10 + (c - '0');
            }
            if (part > 255) {
                return ERR_BAD_ARGUMENT;
            }
            result = (result << 8) | part;
        }
        *rgb = result;
        return 0;
    }

    return ERR_BAD_ARGUMENT;
}

/*
 * Write a colour as the user can type it back: its name if it is one of
 * the 16, otherwise RGBrrrgggbbb
 *
 * Inputs:
 * rgb = the colour as 0x00RRGGBB
 * value = buffer for the text (at least 13 bytes)
 */
static void color_format(unsigned long rgb, char * value) {
    short i;

    for (i = 0; color_names[i].name != 0; i++) {
        if (color_names[i].rgb == rgb) {
            strcpy(value, color_names[i].name);
            return;
        }
    }
    sprintf(value, "RGB%03u%03u%03u", (unsigned short)((rgb >> 16) & 0xff),
        (unsigned short)((rgb >> 8) & 0xff), (unsigned short)(rgb & 0xff));
}

/*
 * Put a colour in one entry of both text colour tables of a screen
 *
 * Inputs:
 * channel = the channel whose screen is meant
 * index = the LUT entry (0-15)
 * rgb = the colour as 0x00RRGGBB
 *
 * Returns:
 * 0 on success, an error if the channel is not a screen
 */
static short color_store(short channel, short index, unsigned long rgb) {
    volatile unsigned long * lut_fg;
    volatile unsigned long * lut_bg;
    short screen = sys_chan_device(channel);

    /* The hardware wants AARRGGBB: always fully opaque */
    rgb |= 0xFF000000;

    if (screen == 0) {
        /* Screen 0 is Vicky's channel B */
        lut_fg = FG_CLUT_B;
        lut_bg = BG_CLUT_B;
    } else if (screen == 1) {
        lut_fg = FG_CLUT_A;
        lut_bg = BG_CLUT_A;
    } else {
        return ERR_BAD_ARGUMENT;
    }

    lut_fg[index] = rgb;
    lut_bg[index] = rgb;

    if (index == COLOR_BG_INDEX) {
        /* Vicky shows background colour 0 as transparent: what is seen
         * there is the screen's own background colour register (0x00RRGGBB)
         */
        if (screen == 0) {
            *BackGroundControlReg_B = rgb & 0x00FFFFFF;
        } else {
            *BackGroundControlReg_A = rgb & 0x00FFFFFF;
        }
    }
    return 0;
}

/*
 * List the colour names for SET HELP
 *
 * Inputs:
 * channel = the channel to print on
 */
static void color_help(short channel) {
    short i;

    print(channel, "Colours for FG and BG:");
    for (i = 0; color_names[i].name != 0; i++) {
        print(channel, " ");
        print(channel, color_names[i].name);
    }
    print(channel, "\nor RGBrrrgggbbb, three numbers 0-255 (e.g. RGB255128000)\n");
}

/*
 * SET FG <colour> -- the colour of normal text
 */
short cli_fg_set(short channel, const char * value) {
    unsigned long rgb;
    short result = color_parse(value, &rgb);
    if (result == 0) {
        result = color_store(channel, COLOR_FG_INDEX, rgb);
        if (result == 0) {
            color_fg = rgb;
        }
    }
    return result;
}

/*
 * GET FG -- the colour of normal text
 */
short cli_fg_get(short channel, char * value, short size) {
    color_format(color_fg, value);
    return 0;
}

/*
 * SET BG <colour> -- the colour of the background
 */
short cli_bg_set(short channel, const char * value) {
    unsigned long rgb;
    short result = color_parse(value, &rgb);
    if (result == 0) {
        result = color_store(channel, COLOR_BG_INDEX, rgb);
        if (result == 0) {
            color_bg = rgb;
        }
    }
    return result;
}

/*
 * GET BG -- the colour of the background
 */
short cli_bg_get(short channel, char * value, short size) {
    color_format(color_bg, value);
    return 0;
}

#endif

/**
 * Set the number of the text screen to use for interactions
 */
short cli_screen_set(short channel, const char * value) {
    char message[160];
    t_sys_info info;
    short screen = (short)cli_eval_number(value);

    sys_get_info(&info);
    if (screen < info.screens) {
        cli_txt_screen_set(screen);
    } else {
        sprintf(message, "Screen #%d not present.\n", screen);
        print(channel, message);
    }
    return 0;
}

/**
 * Get the number of the text screen to use for interactions
 */
short cli_screen_get(short channel, char * value, short size) {
    sprintf(value, "%d", cli_txt_screen_get());
    return 0;
}

/**
 * Set the path to the command processor "CLI" for built-in command processor
 */
short cli_shell_set(short channel, const char * value) {
    if (strcicmp(value, "cli") == 0) {
        cli_command_set(0);
    } else {
        cli_command_set(value);
    }
    return 0;
}

/**
 * Get the path to the command processor "CLI" for built-in command processor
 */
short cli_shell_get(short channel, char * value, short size) {
    char tmp[255];

    // Get the path...
    cli_command_get(tmp);

    // If it's empty... return that CLI is the current setting
    if (strlen(tmp) == 0) {
        strncpy(value, "cli", size);
    } else {
        strncpy(value, tmp, size);
    }
    return 0;
}


/*
 * Initialize the settings tables
 */
void cli_set_init() {
    t_sys_info info;

    cli_first_setting = 0;
    cli_last_setting = 0;

    sys_get_info(&info);

    cli_set_register("CLOCK", "CLOCK ON|OFF|12|24 -- the same as the CLOCK command (kept for old mcp.init files)", cli_clock_set, 0);
#if MODEL == MODEL_FOENIX_A2560K
    cli_set_register("CONSOLE", "CONSOLE OFF|COM1|COM2 -- copy console output to a serial port (OFF by default)", cli_console_set, cli_console_get);
#endif
#if MODEL == MODEL_FOENIX_A2560K || MODEL == MODEL_FOENIX_A2560X || MODEL == MODEL_FOENIX_GENX
    cli_set_register("BG", "BG <name>|RGBrrrgggbbb -- background colour (see SET HELP)", cli_bg_set, cli_bg_get);
#endif
    cli_set_register("DATE", "DATE yyyy-mm-dd -- set the date in the realtime clock", cli_date_set, cli_date_get);

    if (info.screens == 1) {
        cli_set_register("FONT", "FONT <path> -- set a font for the display", cli_font0_set, cli_font0_get);
    } else {
        cli_set_register("FONT@0", "FONT@0 <path> -- set a font for the display #0", cli_font0_set, cli_font0_get);
        cli_set_register("FONT@1", "FONT@1 <path> -- set a font for the display #1", cli_font1_set, cli_font1_get);
    }

#if MODEL == MODEL_FOENIX_A2560K || MODEL == MODEL_FOENIX_A2560X || MODEL == MODEL_FOENIX_GENX
    cli_set_register("FG", "FG <name>|RGBrrrgggbbb -- colour of normal text (see SET HELP)", cli_fg_set, cli_fg_get);
#endif

    cli_set_register("KEYBOARD", "KEYBOARD <path> -- set the keyboard layout", cli_layout_set, cli_layout_get);

#if MODEL == MODEL_FOENIX_A2560K
    cli_set_register("KEYCOLOR", "KEYCOLOR 0x0RGB -- set the keyboard color", cli_keycolor_set, cli_keycolor_get);
#endif

    if (info.screens > 1) {
        cli_set_register("SCREEN", "SCREEN <0 - 1> -- set the channel number to use for interactions", cli_screen_set, cli_screen_get);
    }

    cli_set_register("SHELL", "SHELL <path> -- set the path for the command processor shell", cli_shell_set, cli_shell_get);
    cli_set_register("TIME", "TIME HH:MM:SS -- set the time in the realtime clock", cli_time_set, cli_time_get);
}
