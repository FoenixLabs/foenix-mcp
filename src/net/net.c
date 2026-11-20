/**
 * @file net.c
 *
 * 2.1.0: the network in the MCP: settings, NET ON / NET OFF and the network log (see net.h).
 *
 * FELINC (net/felinc) is connected through three hooks: its memory comes from the network pool,
 * its text goes to the network log (or, for NET status lists, to the screen of that command), and
 * an error inside FELINC only sets a flag, after which net_poll stops the network.
 *
 * FELINC runs when net_poll is called: by the command line while it waits for a key, by programs
 * (felinc_poll), and in the background by net_tick, from the RTC interrupt every 15 ms. FELINC is
 * not reentrant, so every way into it takes the lock (net_lock); the tick skips its turn while
 * someone holds it.
 */

#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include "errors.h"
#include "simpleio.h"
#include "utilities.h"
#include "timers.h"
#include "dev/rtc.h"
#include "netpool.h"
#include "net/net.h"
#include "net/felinc/felinc.h"

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
 * FELINC's text: a line at a time into the network log, or to the screen of a NET command
 */

static char log_line[128];              /* the line FELINC is writing (it sends pieces) */
static short log_line_length = 0;
static short print_channel = -1;        /* >= 0 while a NET command shows a FELINC list */

/*
 * felinc_text - FELINC's print hook
 *
 * Input:  text = a piece of text, with its newlines
 * Output: none. To the screen of the NET command that asked for a list (print_channel), else
 *         line by line into the network log.
 */
static void felinc_text(const char * text) {
    if (print_channel >= 0) {
        print(print_channel, text);
        return;
    }
    for (; *text != 0; text++) {
        if (*text == '\n' || log_line_length == sizeof(log_line) - 1) {
            log_line[log_line_length] = 0;
            if (log_line_length > 0) {
                net_log(log_line);
            }
            log_line_length = 0;
            if (*text == '\n') {
                continue;
            }
        }
        log_line[log_line_length++] = *text;
    }
}

/*
 * Errors inside FELINC
 */

static short fatal_seen = 0;            /* set by felinc_fatal, handled by net_poll */

/*
 * felinc_fatal - FELINC's fatal hook: an error inside FELINC (a program error, not the network)
 *
 * Input:  reason = what went wrong
 * Output: none. FELINC calls this in the middle of its own work, so it must not call FELINC
 *         (felinc_shutdown would free buffers in use): it only logs and sets a flag. FELINC then
 *         drops the operation, and net_poll stops the network.
 */
static void felinc_fatal(const char * reason) {
    net_log("FELINC error, the network stops:");
    net_log(reason);
    fatal_seen = 1;
}

/*
 * Start and stop
 */

static short state = NET_STATE_OFF;
static short problem = 0;               /* 1 = the network stopped after an error (top bar) */

/*
 * The lock: a count, because a locked path can call another (a program's felinc_init starts the
 * network). The tick only reads it, from the interrupt.
 */
static volatile short busy = 0;

/*
 * net_lock / net_unlock - take and give back the lock around work inside FELINC
 *
 * Input:  none
 * Output: none
 */
void net_lock(void) {
    busy++;
}

void net_unlock(void) {
    busy--;
}

/*
 * setting_or_default - a setting for felinc_init
 *
 * Input:  index = which setting
 * Output: the value, or 0 (= FELINC's default) if it is not set
 */
static char * setting_or_default(short index) {
    return (setting_values[index][0] != 0) ? setting_values[index] : 0;
}

/*
 * net_print_ip - print an IPv4 address
 *
 * Input:  channel = where, ip = the address (a.b.c.d with a in the top byte)
 * Output: none
 */
void net_print_ip(short channel, uint32_t ip) {
    char text[16];
    sprintf(text, "%u.%u.%u.%u", (unsigned)(ip >> 24), (unsigned)((ip >> 16) & 0xFF),
            (unsigned)((ip >> 8) & 0xFF), (unsigned)(ip & 0xFF));
    print(channel, text);
}

/*
 * check_pool_empty - after a stop, FELINC must have given back all its memory
 *
 * Input:  none
 * Output: none (a line in the log if blocks are left)
 */
static void check_pool_empty(void) {
    t_netpool_info pool;
    char text[64];

    netpool_info(&pool);
    if (pool.blocks_used != 0) {
        sprintf(text, "network pool: %u blocks (%lu bytes) not given back", (unsigned)pool.blocks_used,
                (unsigned long)pool.used);
        net_log(text);
    }
}

/*
 * net_start - start the network (NET ON, or a program's felinc_init)
 *
 * Input:  channel = the console of the NET ON command; -1 for a program's felinc_init: then
 *         nothing is printed (the screen is the program's) and it does not wait for DHCP (the
 *         program polls until it has an address, as with its own FELINC)
 * Output: 0, or a negative error
 */
short net_start(short channel) {
    long until;
    short dhcp = (strcmp(setting_values[NET_SET_DHCP], "ON") == 0);

    if (state == NET_STATE_ON) {
        if (channel >= 0) {
            print(channel, "The network is already on.\n");
        }
        return 0;
    }

    net_lock();
    felinc_set_alloc(netpool_alloc, netpool_free);
    felinc_set_print(felinc_text);
    felinc_set_fatal(felinc_fatal);
    fatal_seen = 0;

    net_log((channel >= 0) ? "NET ON" : "network on (started by a program)");
    if (!felinc_init(setting_or_default(NET_SET_MAC), dhcp ? true : false, setting_or_default(NET_SET_IP),
                     setting_or_default(NET_SET_MASK), setting_or_default(NET_SET_GATEWAY))) {
        check_pool_empty();
        if (channel >= 0) {
            print(channel, "NET ON: the network did not start (see NET LOG).\n");
        }
        problem = 1;
        net_unlock();
        return ERR_GENERAL;
    }
    state = NET_STATE_ON;
    problem = 0;
    if (channel < 0) {
        net_unlock();
        return 0;
    }

    if (!dhcp) {
        print(channel, "Network on: ");
        net_print_ip(channel, felinc_get_ip());
        print(channel, "\n");
        net_unlock();
        return 0;
    }

    /* With DHCP: wait a little for the address, so it can be shown; FELINC keeps trying after */
    print(channel, "Network on, asking for an address (DHCP)... ");
    until = timers_jiffies() + 10 * 60;
    while (felinc_get_ip() == 0 && timers_jiffies() < until && !fatal_seen) {
        felinc_poll();
    }
    if (felinc_get_ip() != 0) {
        net_print_ip(channel, felinc_get_ip());
        print(channel, "\n");
    } else {
        print(channel, "no answer yet; it keeps asking (NET shows the address).\n");
    }
    net_unlock();
    return 0;
}

/*
 * stop - stop FELINC and check that it gave back its memory
 *
 * Input:  why = for the log
 * Output: none
 */
static void stop(const char * why) {
    net_lock();
    felinc_shutdown();
    state = NET_STATE_OFF;
    net_log(why);
    check_pool_empty();
    net_unlock();
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
    problem = 0;
    stop("NET OFF");
}

/*
 * net_poll - let the network do its work: received frames, timers, DHCP
 *
 * Input:  none
 * Output: none. Stops the network after an error inside FELINC (see felinc_fatal).
 */
void net_poll(void) {
    if (state != NET_STATE_ON) {
        return;
    }
    net_lock();
    if (fatal_seen) {
        fatal_seen = 0;
        problem = 1;
        stop("network stopped after a FELINC error");
    } else {
        felinc_poll();
    }
    net_unlock();
}

/*
 * net_tick - the network's turn in the background (called by the RTC interrupt every 15 ms)
 *
 * Input:  none
 * Output: the character for the top bar: ' ' off, 0xFA on without an address yet (DHCP),
 *         0x12 on with an address, 0x13 stopped after an error
 */
char net_tick(void) {
    /* Someone is inside FELINC (a system call, a NET command, the command line's poll): this
     * interrupt came in the middle of it, so leave FELINC alone until the next tick */
    if (state == NET_STATE_ON && busy == 0) {
        net_poll();
    }

    if (state == NET_STATE_ON) {
        return (felinc_get_ip() != 0) ? 0x12 : 0xFA;
    }
    return problem ? 0x13 : ' ';
}

/*
 * net_resolve - look up a name through DNS (NSLOOKUP)
 *
 * Input:  channel = where to print the result, name = the name (or a.b.c.d)
 * Output: 0 if found, a negative error if not. Waits up to 10 s; FELINC's DNS retries meanwhile.
 */
short net_resolve(short channel, const char * name) {
    uint32_t ip = 0;
    int16_t result;
    long until = timers_jiffies() + 10 * 60;

    if (state != NET_STATE_ON) {
        print(channel, "The network is off (NET ON).\n");
        return ERR_GENERAL;
    }

    /* Ask, then let the network work between the questions: the answer comes in through
     * felinc_poll. Each call takes the lock; the tick polls in between as well. */
    do {
        net_lock();
        result = felinc_resolve(name, &ip);
        if (result == FELINC_RESOLVE_BUSY) {
            felinc_poll();
        }
        net_unlock();
    } while (result == FELINC_RESOLVE_BUSY && state == NET_STATE_ON && timers_jiffies() < until);

    print(channel, name);
    if (result == FELINC_RESOLVE_DONE) {
        print(channel, ": ");
        net_print_ip(channel, ip);
        print(channel, "\n");
        return 0;
    }
    print(channel, (result == FELINC_RESOLVE_BUSY) ? ": no answer within 10 s\n" : ": not found\n");
    return ERR_GENERAL;
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
 * net_print_status - show the network's address and a FELINC list on a screen
 *
 * Input:  channel = where, list = NET_LIST_STATUS, _TCP, _ARP or _DNS
 * Output: none
 */
void net_print_status(short channel, short list) {
    uint8_t mac[6];
    char text[40];

    if (state != NET_STATE_ON) {
        return;
    }
    net_lock();
    if (list == NET_LIST_STATUS) {
        print(channel, "Address: ");
        if (felinc_get_ip() != 0) {
            net_print_ip(channel, felinc_get_ip());
        } else {
            print(channel, "none yet (DHCP)");
        }
        felinc_get_mac(mac);
        sprintf(text, ", MAC %02X:%02X:%02X:%02X:%02X:%02X\n", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
        print(channel, text);
    }

    /* FELINC's lists go to this screen, not to the log */
    print_channel = channel;
    switch (list) {
        case NET_LIST_TCP: felinc_print_tcp(); break;
        case NET_LIST_ARP: felinc_print_arp(); break;
        case NET_LIST_DNS: felinc_print_dns(); break;
        default:           felinc_print_status(); break;
    }
    print_channel = -1;
    net_unlock();
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
