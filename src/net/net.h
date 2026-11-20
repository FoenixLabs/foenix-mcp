/**
 * @file net.h
 *
 * 2.1.0: the network in the MCP. FELINC (the TCP/IP stack) runs inside the MCP; this part keeps
 * its settings (SET NET ...), starts and stops it (NET ON / NET OFF) and keeps the network log.
 *
 * FELINC never prints to the screen by itself: it can run while a program owns the screen.
 * Messages it gives in the background go to the network log (NET LOG); output the user asks for
 * at the command line goes to that command's screen.
 */

#ifndef __NET_H
#define __NET_H

#include <stdint.h>

/* The network settings (SET NET <name> <value>) */
#define NET_SET_DHCP        0       /* ON or OFF */
#define NET_SET_IP          1       /* our address, a.b.c.d */
#define NET_SET_MASK        2       /* subnet mask, a.b.c.d */
#define NET_SET_GATEWAY     3       /* gateway, a.b.c.d */
#define NET_SET_MAC         4       /* our MAC address, xx:xx:xx:xx:xx:xx */
#define NET_SET_COUNT       5

#define NET_VALUE_MAX       18      /* longest value: "00:0D:80:61:94:AB" + 0 */

/**
 * Find a network setting by name (DHCP, IP, MASK, GATEWAY, MAC; any case).
 *
 * @param name the name
 * @return NET_SET_DHCP ... NET_SET_MAC, or -1 for an unknown name
 */
extern short net_setting_index(const char * name);

/**
 * The name of a network setting, as SET NET and GET NET use it.
 *
 * @param index NET_SET_DHCP ... NET_SET_MAC
 * @return the name
 */
extern const char * net_setting_name(short index);

/**
 * Change a network setting. The value is checked; "" (or DEFAULT) makes it the default again.
 * A change is used at the next NET ON.
 *
 * @param index NET_SET_DHCP ... NET_SET_MAC
 * @param value the new value
 * @return 0, or ERR_BAD_ARGUMENT if the value is not valid for this setting
 */
extern short net_setting_set(short index, const char * value);

/**
 * The value of a network setting.
 *
 * @param index NET_SET_DHCP ... NET_SET_MAC
 * @return the value; "" means the default (FELINC's own)
 */
extern const char * net_setting_get(short index);

/** The state of the network */
#define NET_STATE_OFF       0       /* not started (or stopped with NET OFF) */
#define NET_STATE_ON        1       /* FELINC runs */

/**
 * Start the network with the current settings (NET ON).
 *
 * @param channel where to print what happens (the console of the NET ON command)
 * @return 0 if the network runs, or a negative error
 */
extern short net_start(short channel);

/**
 * Stop the network (NET OFF).
 *
 * @param channel where to print what happens
 */
extern void net_stop(short channel);

/**
 * Let the network do its work (received frames, timers, DHCP). Call it often while the network
 * is on; it does nothing when the network is off. Stops the network after an error inside FELINC.
 */
extern void net_poll(void);

/* What net_print_status shows */
#define NET_LIST_STATUS     0       /* address, MAC and FELINC's counters */
#define NET_LIST_TCP        1       /* TCP connections */
#define NET_LIST_ARP        2       /* the ARP table */
#define NET_LIST_DNS        3       /* the DNS cache */

/**
 * Show the network's address and one of FELINC's lists (only while the network is on).
 *
 * @param channel the screen of the NET command that asks for it
 * @param list NET_LIST_STATUS, NET_LIST_TCP, NET_LIST_ARP or NET_LIST_DNS
 */
extern void net_print_status(short channel, short list);

/**
 * The state of the network.
 *
 * @return NET_STATE_OFF or NET_STATE_ON
 */
extern short net_state(void);

/**
 * Add a line to the network log, with the time in front. Never prints to the screen.
 * Also the output hook for FELINC's background messages.
 *
 * @param text the message; a newline at its end is not needed (one is added)
 */
extern void net_log(const char * text);

/**
 * Print the network log, oldest line first.
 *
 * @param channel where to print
 */
extern void net_log_print(short channel);

/**
 * Empty the network log.
 */
extern void net_log_clear(void);

#endif
