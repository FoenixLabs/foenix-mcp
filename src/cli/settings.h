/*
 * Mechanism for handling the SET command
 *
 * SET <setting name> <setting value>
 */

#ifndef __SETTINGS_H
#define __SETTINGS_H

/* Pointer to a function to set a value */
typedef short (*cli_setter)(short, const char *);

/* Pointer to a function to get a value */
typedef short (*cli_getter)(short, char * buffer, short size);

/*
 * Initialize the settings tables
 */
extern void cli_set_init();

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
extern short cli_set_register(const char * name, const char * help, cli_setter setter, cli_getter getter);

/*
 * Set the value of a setting
 *
 * Inputs:
 * channel = the channel to use for reporting success or error
 * name = the name of the setting to update (case insensitive)
 * value = the value to store in the setting (may be interpreted by the setting)
 *
 * Returns:
 * 0 on success, any other number is an error
 */
extern short cli_set_value(short channel, const char * name, const char * value);

/*
 * Get the value of a setting
 *
 * Inputs:
 * channel = the channel to use for reporting success or error
 * name = the name of the setting to update (case insensitive)
 * buffer = the place to copy a string representing the value of the setting
 * size = the number of characters that can be stored in the buffer
 *
 * Returns:
 * 0 on success, any other number is an error
 */
extern short cli_get_value(short channel, const char * name, char * buffer, short size);

/**
 * 2.1.0: the command of a function key (set with SET F1 <command> or SET +F1 <command>).
 *
 * @param number the key number, 1 - 8
 * @param shifted non-zero if Shift was held down
 * @return the command, or 0 if the key has none
 */
extern const char * cli_fkey_command(short number, short shifted);

/*
 * Command to set the value of a setting
 */
extern short cli_cmd_set(short screen, int argc, const char * argv[]);

/*
 * Command to set the value of a setting
 */
extern short cli_cmd_get(short screen, int argc, const char * argv[]);

/**
 * Switch the clock in the top bar on or off, or choose 12 or 24 hours (SET CLOCK and, since 2.1.0,
 * the CLOCK command).
 *
 * @param channel where to print the usage when the value is wrong
 * @param value ON, OFF, 12 or 24 (also 1 / 0)
 * @return 0, or ERR_GENERAL for a wrong value
 */
extern short cli_clock_set(short channel, const char * value);

#endif
