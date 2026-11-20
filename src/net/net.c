/**
 * @file net.c
 *
 * 2.1.0: the network in the MCP: settings, NET ON / NET OFF and the network log (see net.h).
 *
 * Step M2 (groundwork): FELINC itself is not in the MCP yet. NET ON says so and leaves the
 * network off; the settings and the log already work, so mcp.init can set them now.
 */

#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include "errors.h"
#include "simpleio.h"
#include "utilities.h"
#include "dev/rtc.h"
#include "net/net.h"

/*
 * Settings
 */

static const char * const setting_names[NET_SET_COUNT] = {"DHCP", "IP", "MASK", "GATEWAY", "MAC"};

/* The values as typed (upper case for DHCP), "" = FELINC's default. DHCP is ON by default. */
static char setting_values[NET_SET_COUNT][NET_VALUE_MAX] = {"ON", "", "", "", ""};

/*
 * net_setting_index - find a setting by name
 *
 * Input:  name = DHCP, IP, MASK, GATEWAY or MAC, any case
 * Output: the index, or -1
 */
short net_setting_index(const char * name) {
    short i;
    for (i = 0; i < NET_SET_COUNT; i++) {
        if (strcicmp(name, setting_names[i]) == 0) {
            return i;
        }
    }
    return -1;
}

/*
 * net_setting_name - the name of a setting
 *
 * Input:  index = NET_SET_DHCP ... NET_SET_MAC
 * Output: the name
 */
const char * net_setting_name(short index) {
    return setting_names[index];
}

/*
 * is_ip_address - check an IPv4 address in dotted form
 *
 * Input:  text = like "10.77.77.99": four numbers 0-255 with a dot between them
 * Output: 1 if it is one, 0 if not
 */
static short is_ip_address(const char * text) {
    short parts = 0;

    while (parts < 4) {
        short digits = 0;
        short number = 0;
        while (isdigit((unsigned char)*text)) {
            number = number * 10 + (*text - '0');
            text++;
            if (++digits > 3 || number > 255) {
                return 0;
            }
        }
        if (digits == 0) {
            return 0;
        }
        parts++;
        if (parts < 4) {
            if (*text != '.') {
                return 0;
            }
            text++;
        }
    }
    return (*text == 0) ? 1 : 0;
}

/*
 * is_mac_address - check a MAC address
 *
 * Input:  text = like "00:0D:80:61:94:AB": six pairs of hexadecimal digits with a colon between
 * Output: 1 if it is one, 0 if not
 */
static short is_mac_address(const char * text) {
    short i;
    for (i = 0; i < 17; i++) {
        if ((i % 3) == 2) {
            if (text[i] != ':') {
                return 0;
            }
        } else if (!isxdigit((unsigned char)text[i])) {
            return 0;
        }
    }
    return (text[17] == 0) ? 1 : 0;
}

/*
 * net_setting_set - change a setting (used at the next NET ON)
 *
 * Input:  index = which setting, value = the new value ("" or DEFAULT = the default)
 * Output: 0, or ERR_BAD_ARGUMENT
 */
short net_setting_set(short index, const char * value) {
    char * stored;
    short i;

    if (index < 0 || index >= NET_SET_COUNT) {
        return ERR_BAD_ARGUMENT;
    }
    stored = setting_values[index];

    if (index == NET_SET_DHCP) {
        if (strcicmp(value, "ON") == 0 || strcicmp(value, "DEFAULT") == 0 || value[0] == 0) {
            strcpy(stored, "ON");
        } else if (strcicmp(value, "OFF") == 0) {
            strcpy(stored, "OFF");
        } else {
            return ERR_BAD_ARGUMENT;
        }
        return 0;
    }

    if (value[0] == 0 || strcicmp(value, "DEFAULT") == 0) {
        stored[0] = 0;
        return 0;
    }
    if (index == NET_SET_MAC) {
        if (!is_mac_address(value)) {
            return ERR_BAD_ARGUMENT;
        }
    } else if (!is_ip_address(value)) {
        return ERR_BAD_ARGUMENT;
    }

    /* Both checks limit the length, so the value fits */
    for (i = 0; value[i] != 0; i++) {
        stored[i] = toupper((unsigned char)value[i]);
    }
    stored[i] = 0;
    return 0;
}

/*
 * net_setting_get - the value of a setting
 *
 * Input:  index = which setting
 * Output: the value, "" = the default
 */
const char * net_setting_get(short index) {
    if (index < 0 || index >= NET_SET_COUNT) {
        return "";
    }
    return setting_values[index];
}

/*
 * Start and stop
 */

static short state = NET_STATE_OFF;

/*
 * net_start - start the network (NET ON)
 *
 * Input:  channel = the console of the NET ON command
 * Output: 0, or a negative error
 */
short net_start(short channel) {
    if (state == NET_STATE_ON) {
        print(channel, "The network is already on.\n");
        return 0;
    }

    /* Step M2 groundwork: FELINC comes in a later step */
    print(channel, "NET ON: the TCP/IP stack (FELINC) is not in this MCP yet.\n");
    net_log("NET ON: no TCP/IP stack in this MCP yet");
    return ERR_NOT_SUPPORTED;
}

/*
 * net_stop - stop the network (NET OFF)
 *
 * Input:  channel = the console of the NET OFF command
 * Output: none
 */
void net_stop(short channel) {
    if (state == NET_STATE_OFF) {
        print(channel, "The network is already off.\n");
        return;
    }
    state = NET_STATE_OFF;
    net_log("network off");
}

/*
 * net_state - the state of the network
 *
 * Input:  none
 * Output: NET_STATE_OFF or NET_STATE_ON
 */
short net_state(void) {
    return state;
}

/*
 * The network log: a ring of characters. New lines push out the oldest ones; the log never needs
 * memory of its own, so it also works when the network pool is full. It is in the MCP's own RAM,
 * not in the network pool, so it keeps the reason why the network stopped after NET OFF.
 */

#define LOG_SIZE    4096

static char log_ring[LOG_SIZE];
static unsigned short log_start = 0;    /* the oldest character */
static unsigned short log_length = 0;   /* how many characters are in the log */

/*
 * log_put - add one character to the ring, pushing out the oldest one when it is full
 *
 * Input:  c = the character
 * Output: none
 */
static void log_put(char c) {
    log_ring[(log_start + log_length) % LOG_SIZE] = c;
    if (log_length < LOG_SIZE) {
        log_length++;
    } else {
        log_start = (log_start + 1) % LOG_SIZE;
    }
}

/*
 * net_log - add a line with the time in front
 *
 * Input:  text = the message (a final newline is not needed)
 * Output: none
 */
void net_log(const char * text) {
    t_time time;
    char stamp[12];
    short hour;
    const char * p;

    rtc_get_time(&time);
    hour = time.hour;
    if (!time.is_24hours) {
        /* The clock may run in 12-hour mode; the log always shows 24 hours */
        hour = (hour % 12) + (time.is_pm ? 12 : 0);
    }
    sprintf(stamp, "%02d:%02d:%02d ", hour, time.minute, time.second);

    for (p = stamp; *p != 0; p++) {
        log_put(*p);
    }
    for (p = text; *p != 0; p++) {
        if (*p == '\n' && p[1] == 0) {
            break;                      /* the line ends below anyway */
        }
        log_put(*p);
    }
    log_put('\n');
}

/*
 * net_log_print - print the log, oldest line first
 *
 * Input:  channel = where to print
 * Output: none
 */
void net_log_print(short channel) {
    char chunk[81];
    unsigned short i = 0;
    unsigned short skip = 0;

    if (log_length == 0) {
        print(channel, "The network log is empty.\n");
        return;
    }

    /* When the ring is full its oldest line is cut off at the front: start at the next line */
    if (log_length == LOG_SIZE) {
        while (skip < log_length && log_ring[(log_start + skip) % LOG_SIZE] != '\n') {
            skip++;
        }
        skip++;
    }

    while (skip < log_length) {
        chunk[i++] = log_ring[(log_start + skip) % LOG_SIZE];
        skip++;
        if (i == sizeof(chunk) - 1 || skip == log_length) {
            chunk[i] = 0;
            print(channel, chunk);
            i = 0;
        }
    }
}

/*
 * net_log_clear - empty the log
 *
 * Input:  none
 * Output: none
 */
void net_log_clear(void) {
    log_start = 0;
    log_length = 0;
}
