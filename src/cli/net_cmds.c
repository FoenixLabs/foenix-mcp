/**
 * @file net_cmds.c
 *
 * 2.1.0: the network commands of the command line:
 *
 *   NET                  the state of the network, its settings and the network pool
 *   NET ON | OFF         start or stop the network
 *   NET LOG [CLEAR]      show (or empty) the network log
 *   SET NET <name> <v>   change a setting: DHCP ON|OFF, IP, MASK, GATEWAY (a.b.c.d), MAC
 *   GET NET [<name>]     show all network settings, or one
 *
 * The settings are normally in mcp.init, for example "SET NET DHCP OFF" and "SET NET IP 10.0.0.9".
 */

#include <stdio.h>
#include <string.h>
#include "simpleio.h"
#include "utilities.h"
#include "netpool.h"
#include "net/net.h"
#include "cli/net_cmds.h"

/*
 * print_setting - print one setting as "NAME value"
 *
 * Input:  channel = where to print, index = which setting
 * Output: none
 */
static void print_setting(short channel, short index) {
    char line[60];
    const char * value = net_setting_get(index);

    sprintf(line, "  %-8s %s\n", net_setting_name(index), (value[0] != 0) ? value : "(default)");
    print(channel, line);
}

/*
 * net_cmd_set - SET NET <name> <value>
 *
 * Input:  channel = where to print, argc/argv = the whole SET command (argv[1] is NET)
 * Output: 0, or -1 for a bad name or value
 */
short net_cmd_set(short channel, int argc, const char * argv[]) {
    short index;

    if (argc != 4) {
        print(channel, "USAGE: SET NET DHCP ON|OFF, SET NET IP|MASK|GATEWAY a.b.c.d, SET NET MAC xx:xx:xx:xx:xx:xx\n"
                       "       (DEFAULT as value: back to the default)\n");
        return -1;
    }
    index = net_setting_index(argv[2]);
    if (index < 0) {
        print(channel, "SET NET: unknown setting (DHCP, IP, MASK, GATEWAY or MAC)\n");
        return -1;
    }
    if (net_setting_set(index, argv[3]) != 0) {
        print(channel, (index == NET_SET_DHCP) ? "SET NET DHCP: ON or OFF\n" :
                       (index == NET_SET_MAC) ? "SET NET MAC: like 00:0D:80:61:94:AB\n" :
                                                "SET NET: an address like 10.77.77.99\n");
        return -1;
    }
    if (net_state() == NET_STATE_ON) {
        print(channel, "Used at the next NET ON.\n");
    }
    return 0;
}

/*
 * net_cmd_get - GET NET [<name>]
 *
 * Input:  channel = where to print, argc/argv = the whole GET command (argv[1] is NET)
 * Output: 0, or -1 for an unknown name
 */
short net_cmd_get(short channel, int argc, const char * argv[]) {
    short index;

    if (argc == 2) {
        for (index = 0; index < NET_SET_COUNT; index++) {
            print_setting(channel, index);
        }
        return 0;
    }
    index = net_setting_index(argv[2]);
    if (argc != 3 || index < 0) {
        print(channel, "USAGE: GET NET [DHCP|IP|MASK|GATEWAY|MAC]\n");
        return -1;
    }
    print(channel, (net_setting_get(index)[0] != 0) ? net_setting_get(index) : "(default)");
    print(channel, "\n");
    return 0;
}

/*
 * net_status - NET without arguments: state, settings, pool
 *
 * Input:  channel = where to print
 * Output: none
 */
static void net_status(short channel) {
    char line[100];
    t_netpool_info pool;
    short index;

    print(channel, (net_state() == NET_STATE_ON) ? "Network: on\n" : "Network: off\n");
    print(channel, "Settings (SET NET <name> <value>, used at NET ON):\n");
    for (index = 0; index < NET_SET_COUNT; index++) {
        print_setting(channel, index);
    }
    netpool_info(&pool);
    sprintf(line, "Network pool: %lu of %lu bytes used in %u blocks\n",
            (unsigned long)pool.used, (unsigned long)pool.size, (unsigned)pool.blocks_used);
    print(channel, line);
}

/*
 * cmd_net - the NET command
 *
 * Input:  channel = where to print, argc/argv = NET [ON|OFF|LOG [CLEAR]]
 * Output: 0, or an error
 */
short cmd_net(short channel, int argc, const char * argv[]) {
    if (argc == 1) {
        net_status(channel);
        return 0;
    }
    if (argc == 2 && strcicmp(argv[1], "ON") == 0) {
        return net_start(channel);
    }
    if (argc == 2 && strcicmp(argv[1], "OFF") == 0) {
        net_stop(channel);
        return 0;
    }
    if (argc == 2 && strcicmp(argv[1], "LOG") == 0) {
        net_log_print(channel);
        return 0;
    }
    if (argc == 3 && strcicmp(argv[1], "LOG") == 0 && strcicmp(argv[2], "CLEAR") == 0) {
        net_log_clear();
        print(channel, "Network log cleared.\n");
        return 0;
    }
    print(channel, "USAGE: NET [ON|OFF|LOG [CLEAR]]\n");
    return -1;
}
