# The MCP's Calypsi SDK

Headers and a small library for writing A2560K programs for the MCP with
Calypsi (`cc68k`). They match MCP 2.2.0, which has the same system calls as
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

Link the library that matches the program's compiler options with the
program's objects. Calypsi's own library is added by `--target=A2560K` and is
searched last:

```
ln68k -o prog.pgz main.o <sdk>/lib/mcp_syscalls-68000-lc-ld.a a2560k.scm --target=A2560K --output-format=pgz ...
```

Calypsi's linker only links objects built with the same `--core`,
`--code-model` and `--data-model`, so `lib` has a library for each
combination. The names are the same as those of Calypsi's own libraries,
`mcp_syscalls-<core>-<code>-<data>.a`:

| Part of the name | Compiler option |
|---|---|
| `68000`, `68040`, `68060` | `--core` |
| `lc` / `sc` | `--code-model=large` / `small` |
| `ld` / `sd` / `fod` | `--data-model=large` / `small` / `far-only` |

For example, a program built with `--core=68040 --code-model=large
--data-model=small` links `lib/mcp_syscalls-68040-lc-sd.a`. With a library
that doesn't match, `ln68k` stops with "runtime model attribute ... mismatch".
The size of `double` doesn't matter: the library has no floating point.

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

## Printing fast

Since MCP 2.1.2 the console writes a run of plain characters in one go, so
give it large pieces of text:

- Calypsi's `stdout` buffer is small (64 bytes), so `printf` reaches the MCP in
  small pieces. Call `setvbuf(stdout, buf, _IOFBF, 1024)` at the start of
  `main`, before printing anything (`buf` is a static array of 1024 bytes).
- Then call `fflush(stdout)` before every MCP call that moves the cursor or
  changes the region or the colours (`sys_txt_set_xy`, `sys_txt_set_region`,
  ...), so the text already printed lands in the right place.
- For a full screen, build each row (text and ANSI colour codes) in a buffer
  and write it with one `sys_chan_write`. To move the view by a few rows,
  `sys_txt_scroll` is faster than printing the rows again.

Measured on an A2560K25 at 1024 x 768, one page of 60 rows x 157 characters:

| How | MCP 2.1.1 | MCP 2.1.2 |
|---|---|---|
| `printf`, Calypsi's default `stdout` | not measured | 698 ms |
| `printf` after `setvbuf(..., 1024)` | not measured | 220 ms |
| each row with one `sys_chan_write` | 870 ms | 91-105 ms |
| `sys_txt_scroll` by one row | not measured | 19 ms |

## Building the libraries

On the computer with Calypsi:

```
make                              # every library in lib/, and test/sdktest.pgz
```

## Testing

`test/sdktest.pgz` calls the changed functions and prints what it gets back:
- the machine (`sys_get_info`)
- the current folder under both names
- `sys_fsys_stat`
- the clock and the screen size
- text in the top bar
- `sys_proc_elevate` (refused in 2.1.0)
- a file written and read with `fopen`

Run it from the MCP command line and check the values. It is built for the
68000 with large code and data (`mcp_syscalls-68000-lc-ld.a`) and runs on
every A2560K.

## Keeping it in line with the MCP

The MCP's build runs `tools/check_sdk.py` and stops when this SDK no longer
matches the MCP's sources:
- system call numbers
- a function for every call the MCP handles
- error and interrupt numbers
- the shared constants
- the records (`t_sys_info`, `t_file_info`, `t_time`, ...)

A change to a system call or a record therefore needs the same change here.
