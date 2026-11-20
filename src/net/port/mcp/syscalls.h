/**
 * @file net/port/mcp/syscalls.h
 *
 * 2.1.0: what FELINC needs from <mcp/syscalls.h> when it is built into the MCP.
 *
 * As a program, FELINC includes the Calypsi SDK's <mcp/syscalls.h> and asks the MCP for the
 * time with a system call (TRAP #15). Inside the MCP it calls the MCP's own jiffy counter
 * directly: no system call from inside the kernel. Only the network files (net/felinc) see this
 * header; the Makefile puts net/port on their include path.
 */

#ifndef __NET_PORT_MCP_SYSCALLS_H
#define __NET_PORT_MCP_SYSCALLS_H

extern long timers_jiffies(void);

#define sys_time_jiffies()  timers_jiffies()

#endif
