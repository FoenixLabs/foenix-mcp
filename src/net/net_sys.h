/**
 * @file net_sys.h
 *
 * 2.1.0: the network system calls for programs (see net_sys.c; numbers KFN_NET_* in
 * include/syscalls.h).
 */

#ifndef __NET_SYS_H
#define __NET_SYS_H

#include <stdint.h>
#include "syscalls.h"

/* What KFN_NET_INFO gives (its first argument) */
#define NET_INFO_IP         0       /* our IPv4 address (0 while DHCP is still asking) */
#define NET_INFO_NTP        1       /* the time server DHCP gave, or 0 */
#define NET_INFO_MAC        2       /* our MAC address: 6 bytes to the second argument */
#define NET_INFO_STATS      3       /* a pointer to FELINC's counters (felinc_stats) */

/**
 * Carry out a network system call (called by the system call dispatcher for group 0x80).
 *
 * @param function KFN_NET_INIT ... KFN_NET_PRINT
 * @param p0 ... p4 the program's arguments
 * @return the result for the program
 */
extern int32_t net_syscall(int32_t function, int32_t p0, int32_t p1, int32_t p2, int32_t p3, int32_t p4);

/**
 * A program is about to run (called by proc_run).
 */
extern void net_program_start(void);

/**
 * A program has ended: close the sockets it left open (called by proc_run).
 */
extern void net_program_end(void);

/**
 * The command line was restarted after a crash: close the sockets of all programs.
 */
extern void net_program_reset(void);

#endif
