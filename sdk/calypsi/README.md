# The MCP's Calypsi SDK

Headers and a small library for writing A2560K programs for the MCP with
Calypsi (`cc68k`). They match MCP 2.1.1, which has the same system calls as
2.1.0.

Calypsi comes with an SDK of its own (`contrib/A2560K-SDK`, used with
`--target=A2560K`). Its MCP part is older than MCP 2.1.0:

- **`t_sys_info` has a different layout.** `sub_model`, `cpu_clock_khz` and
  `bus_clock_khz` are missing, and `pcb_version` and `vicky_rev` are there, so
  every field after `model` is read from the wrong place.
- **The model and CPU numbers are wrong.** For example, `MODEL_FOENIX_A2560K`
  is 13 instead of 11.
- **2.1.0's calls are missing.** These include `sys_fsys_stat`,
  `sys_txt_topbar`, the error numbers -37 to -40 and the network numbers.
  `sys_txt_get_color` is declared but not in the library.
- **Some names differ from the MCP's and the manual's.** For example,
  `sys_fsys_getcwd` instead of `sys_fsys_get_cwd`.

This SDK replaces only that MCP part (`<mcp/syscalls.h>`, `<mcp/types.h>`,
`<mcp/errors.h>`, `<mcp/interrupt.h>` and the system call functions).
Calypsi's start-up code, C library and `syscall()` stay as they are.

## Use

Compile with this SDK's `include` folder, so `<mcp/syscalls.h>` is this one:

```
cc68k --core=68000 --code-model=large --data-model=large --target=A2560K -I<sdk>/include ...
```

Link `lib/mcp_syscalls.a` with the program's objects. Calypsi's own library
is added by `--target=A2560K` and is searched last:

```
ln68k -o prog.pgz main.o <sdk>/lib/mcp_syscalls.a a2560k.scm --target=A2560K --output-format=pgz ...
```

Then the program and Calypsi's C library (`printf`, `fopen`, ...) both use
this SDK's functions. Calypsi's older copies are left out. All functions are
in one object file on purpose: once the linker takes one of them, it has all of
them, so Calypsi's older copies are never needed and can't clash.

Another way is to compile `src/mcp_syscalls.c` with the program, as one of its
own source files.

Programs written for Calypsi's SDK build unchanged: its other names
(`sys_fsys_getcwd`, `sys_bdev_getblock`, `sys_txt_get_caps`, ...) are here too,
as the same calls. If such a program reads `t_sys_info` or uses the model or
CPU numbers, it now gets the right values.

The network calls (0x80-0x8F) come from FELINC: link FELINC's
`felinc_mcp.a` and include `felinc.h`.

## Building the library

On the computer with Calypsi:

```
make                              # lib/mcp_syscalls.a and test/sdktest.pgz
make CORE=68040 DATA=small        # for a program built with other models
```

Build the library with the same `--core`, `--code-model` and `--data-model` as
the program. The defaults are 68000, large and large.

## Testing

`test/sdktest.pgz` calls the changed functions and prints what it gets back:
- the machine (`sys_get_info`)
- the current folder under both names
- `sys_fsys_stat`
- the clock and the screen size
- text in the top bar
- `sys_proc_elevate` (refused in 2.1.0)
- a file written and read with `fopen`

Run it from the MCP command line and check the values.

## Keeping it in line with the MCP

The MCP's build runs `tools/check_sdk.py` and stops when this SDK no longer
matches the MCP's sources:
- system call numbers
- a function for every call the MCP handles
- error and interrupt numbers
- the shared constants
- the records (`t_sys_info`, `t_file_info`, `t_time`, ...)

A change to a system call or a record therefore needs the same change here.
