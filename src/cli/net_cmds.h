/**
 * @file net_cmds.h
 *
 * 2.1.0: the network commands NET, SET NET and GET NET (see net_cmds.c).
 */

#ifndef __NET_CMDS_H
#define __NET_CMDS_H

/**
 * The NET command: NET, NET ON, NET OFF, NET LOG [CLEAR].
 */
extern short cmd_net(short channel, int argc, const char * argv[]);

/**
 * SET NET <name> <value> (called by SET when its first word is NET).
 */
extern short net_cmd_set(short channel, int argc, const char * argv[]);

/**
 * GET NET [<name>] (called by GET when its first word is NET).
 */
extern short net_cmd_get(short channel, int argc, const char * argv[]);

#endif
