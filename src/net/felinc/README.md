# FELINC in the MCP

These files are a copy of the FELINC library (the TCP/IP stack for the A2560K),
**FELINC v0.12** (tag `v0.12`, commit `0924b4c`, gitlab.com/renekint/felinc):
the files of `LIB_SRCS` in FELINC's Makefile with their headers.

Since v0.10 only `dhcp.c` changed (v0.12: after a restart in the same run,
NET OFF / NET ON in the MCP, the DHCP request asks for the previous address
again, so the router does not hand out a new one). v0.11 only added the stub
library `felinc_mcp.a` for programs, which the MCP does not use.

Do not change them here. Changes go to FELINC itself; then copy the files of
the new tagged version over these and update this note.

How the MCP uses them (see `src/net/net.c`):

- memory: `felinc_set_alloc` with `netpool_alloc` / `netpool_free` (the
  network pool, `src/netpool.c`);
- output: `felinc_set_print` sends FELINC's messages to the network log
  (`NET LOG`), or to the screen of the NET command that asked for them;
- errors: `felinc_set_fatal` only sets a flag; the MCP then stops the network
  outside FELINC (the hook is called in the middle of FELINC's own work);
- time: `src/net/port/mcp/syscalls.h` maps `sys_time_jiffies` to the MCP's
  jiffy counter.

FELINC is written for Calypsi's unsigned `char`, the MCP uses `--char-is-signed`:
the Makefile builds these files with `--char-is-unsigned`.
