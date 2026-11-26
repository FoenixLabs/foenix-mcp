# Foenix/MCP

A portable kernel for the Foenix series of computers.

## Overview

The purpose of this project is to provide a simple kernel for the Foenix series
of retro-style computers. Emphasis is placed upon the word "simple." This
operating system will be more related to MS-DOS or CP/M than to Unix or Linux.
The main purposes of this kernel are:

* Boot the Foenix computer to a reasonably usable state.
* Provide access to files on IDE hard drives, SD cards, and floppy disk (when
  the computer supports floppies).
* Provide an environment for loading and running binary programs from media.
* Provide access to those devices which are tedious to access at the lowest
  level (_e.g._ storage media, keyboard, text mode screen).
* Provide a common programmatic interface for all the computers in the line.
* Be broadly portable across the different processors the Foenix computers
  support (at the time of this writing, this includes the 65816, and the 68000
  series).

### Foenix Computers Supported

[Flesh this out as we get there. Intention is to support the FMX, U, U+, A2560K,
A2560X, A2560U (68000 and 65816), and GenX.]

## Building

Please refer to processor specific building instructions that will be included.
Since 2.1.0 the MCP is built with the [Calypsi](https://www.calypsi.cc/)
compiler, see "Building with Calypsi" below. For now only the A2560K K25 / K40
image is made; the other images follow when all versions are released again.

### Building for the M68000 CPU Family

Building this project requires the [VBCC](http://www.compilers.de/vbcc.html) compiler and the
```MAKE``` utility. Using your command line, go into the ```src``` directory and type:

```
export VBCC = <use-your-projects-dir>/FoenixMCP/vbcc
make all
```
to build the kernel.

To remove binaries and intermediate files:

```
make clean
```

#### Build Options

There are three option variables that can be set in the makefile:

* `UNIT`: This specifies which Foenix model is the targetted machine for the build. There are currently two options:
    1. `UNIT=a2560u`, this covers both the A2560U and A2560U+. It sets the CPU to be the M68000.
    2. `UNIT=a2560k`, this covers the A2560K and sets the CPU to M68040.
* `MEMORY`: This species where in memory Foenix/MCP will run. There are two values here:
    1. `MEMORY=ram`: This specifies that the MCP will run from RAM.
    2. `MEMORY=flash`: This specifies that the MCP will be programmed to flash memory.
* `KBD_POLLED`: As a temporary feature, if this option variable is defined, it will specify to the kernel that polled I/O is to be used for keyboard access instead of interrupt driven I/O.

Examples:
```
make all UNIT=a2560u MEMORY=ram
```
Builds an SREC file suitable for loading into RAM of the A2560U/U+.

```
make all UNIT=a2560k MEMORY=flash
```
Builds a binary file suitable for programming the flash of the A2560K.

## A2560K Gen1 (K25) notes

This copy of the MCP is maintained for the A2560K Gen1, tested on the
A2560K25 ("A2560Kc"). The flash images for the K25 and the K40 are the same
build: the MCP finds out at start-up which board it runs on.

### Version 2.2.0 (in progress): a fast, reliable start after a warm reset

**SD card and hard drive after a warm reset.** Nothing on the board resets the
SD card: not the reset button, not REBOOT or Ctrl+Alt+Del, not the debug port.
The reset button does reset the IDE drive, but just before lvboot and the MCP
start (after a power-on the board waits about 9 s first). After a warm reset
the start could take a very long time, and the MCP could end with "low level
disk error" until the next power-on. Now:

- the SD card start-up runs on the slow clock divider, sends CMD0 without
  first waiting 0.5 s for a "ready" byte a busy card never sends, and is tried
  three times; a failed start is now reported as a failure;
- a failed SD read or write is tried again after a new card start (three tries
  in all), so one bad moment no longer leaves the card unusable;
- an IDE drive gets up to 10 s to finish its reset (was 1/3 s), and a drive
  left in the middle of a command gets an ATA software reset;
- waiting for the SD controller can no longer hang the MCP for ever.

**Calypsi 5.18.5.** The MCP is built with Calypsi 5.18.5 or newer, which
fixes the last compiler faults that 2.1.x worked around. The workarounds and
`tools/calypsi_check.py` are gone, and the build stops with an older Calypsi
(see "Building with Calypsi").

**Colour 8 is dark grey again.** Colour 8 ("bright black", `ESC[90m`) was
almost black (0x202020) on the A2560K: a test meant for the A2560X only was
always true. It is now the dark grey 0x555555. The 8-pixel purple border stays
as it was, so programs keep the same screen size.

Released together with lvboot 1.5, which has the same SD and IDE fixes (see
[lvboot/README.md](lvboot/README.md)).

### Version 2.1.2: faster console output

**Console output is much faster.** Every character written to the console was
also sent to the serial port COM1 (a copy for a remote terminal), and the MCP
waited for the UART for each one: about 87 µs per character at 115200 baud.
The copy is now off by default. `SET CONSOLE COM1` or `SET CONSOLE COM2` copies
console output to that port again, for example in `mcp.init` when a terminal
is connected; `SET CONSOLE OFF` stops it, and `GET CONSOLE` shows the setting.
Keys typed on COM1 are read either way.

**Runs of characters in one go.** A run of plain characters (no control
characters, no ANSI sequence) now goes to the screen driver at once: it writes
the characters and sets the cursor once, instead of once per character.

On an A2560K25 at 1024 x 768, a full page of text (60 rows x 157 characters,
each row written with one `sys_chan_write`) now takes about 105 ms instead of
870 ms. Programs that print with `printf` gain most with a larger `stdout`
buffer: see "Printing fast" in [sdk/calypsi/README.md](sdk/calypsi/README.md).

**SET HELP.** The CLOCK line ended in a few strange characters: a help line of
80 characters or more lost its end marker (since 2.1.0). Fixed.

The Calypsi SDK's version is 2.1.2; its system calls are the same as in 2.1.1.

### Version 2.1.1: PS/2 Shift fix and a Calypsi SDK

A small release on top of 2.1.0.

**PS/2 keyboard.** After Shift+arrow (or another grey key), Shift no longer
stays pressed, and with NumLock on the grey keys (arrows, Home, End, Page Up,
Page Down, Insert, Delete) work again. The keyboard sends an extra "fake
Shift" code around these keys, and the MCP lost the byte after it.

**Calypsi SDK for programs** (`sdk/calypsi`). It has headers and libraries for
Calypsi programs, matching the MCP. Calypsi's own MCP headers are older than
2.1.0: their `t_sys_info` is laid out differently and 2.1.0's calls are
missing. See "Building with Calypsi" below and
[sdk/calypsi/README.md](sdk/calypsi/README.md). The release has the SDK as a
zip file.

**Memory size.** `SYS INFO` and `sys_get_info` gave 12 MB of system memory
on the A2560K. They now give 4 MB, the SRAM that the MCP and programs use. The
64 MB of SDRAM is not counted, because the MCP does not use it yet.

The MCP calls itself `2.1.1-cache`. lvboot stays at 1.4, and the manual is the
2.1.0 one.

### Version 2.1.0: the network in the MCP

The MCP has a TCP/IP stack built in: [FELINC](https://gitlab.com/renekint/felinc),
the stack for the A2560K's LAN9221 Ethernet chip. It runs in the background, and
programs share it through system calls, so a program no longer needs its own copy.

**Starting it.** `NET ON` starts the network: with DHCP (the default) it waits up
to 10 seconds and shows the address it got. `NET OFF` stops it again and gives
back all its memory; `NET ON` then works again without a restart. A program that
uses the network starts it by itself if it is off.

**Settings.** Normally in `mcp.init`; they are used at the next `NET ON`:

```
SET NET DHCP OFF
SET NET IP 10.77.77.99
SET NET MASK 255.255.255.0
SET NET GATEWAY 10.77.77.1
NET ON
```

- `DHCP` is `ON` by default. With DHCP, `IP`, `MASK` and `GATEWAY` are the
  fall-back: used when no DHCP server answers within a minute.
- `SET NET MAC 00:0D:80:61:94:AB` sets the MAC address.
- `DEFAULT` as the value puts a setting back to FELINC's own value.
- `GET NET` lists the settings, `GET NET IP` shows one.

**Commands.**

| Command | What it does |
|---|---|
| `NET` | on or off, address and MAC, FELINC's counters, the settings, the network pool |
| `NET ON` / `NET OFF` | start / stop the network |
| `NET TCP` / `NET ARP` / `NET DNS` | FELINC's lists: TCP connections, the ARP table, the DNS cache |
| `NET LOG` / `NET LOG CLEAR` | the network log (see below) / empty it |
| `NSLOOKUP <name>` | the address of a name, through DNS |

**The top bar.** The first column of the clock part shows the network:

| Character | Meaning |
|---|---|
| (blank) | off |
| `·` | on, still asking for an address (DHCP) |
| `↕` | on, with an address |
| `‼` | stopped after an error: see `NET LOG` |

**The network log.** The network runs while programs own the screen, so it
never prints there by itself. Its messages (DHCP lease, renewals, errors) go to
the network log, a ring of 4 KB with the time in front of each line: `NET LOG`.

**In the background.** Every 15 ms the RTC interrupt gives the network a turn,
so the board answers ping and renews its DHCP lease while a program is busy.
The command line, and programs through `felinc_poll`, also give it turns, for
speed. Measured on the K25 with the CPU caches on, from a computer on the same
wired network: TFTP through the MCP's network 138 KB/s put and 158 KB/s get
(1 MB); a ping flood of 1000-byte packets without loss, with the command line
working normally meanwhile. (Measure from a wired computer: over a slower link
the TFTP figures drop by half, because TFTP waits for an answer after every
512 bytes.)

**Memory.** At start-up the MCP takes 128 KB for the network from the top of
system RAM: `$0038_0000` - `$0039_FFFF`. The top of memory for programs is
therefore `$0038_0000` (`SYS INFO` shows it, and the pool's use). Calypsi
programs keep their data below `$0038_0000` and VBCC programs below
`$0032_0000`, so no program is affected. FELINC needs about 25 KB, plus twice
the buffer size a program asks for per socket or TCP connection: 128 KB holds
for example 6 TCP connections with 8 KB buffers. `TEST NETPOOL` checks the
pool's allocator (with the network off).

**For programs.** Link with FELINC's `felinc_mcp.a` (FELINC v0.11 or later)
instead of `felinc.a`, with the same `felinc.h`: every `felinc_*` function then
becomes a system call to the MCP's FELINC, and the program shrinks (FELINC_TFTP:
11 KB instead of 33 KB). Differences with a program's own FELINC:

- `felinc_init` starts the network with the MCP's settings if it is off (its
  own MAC and IP arguments are not used) and returns at once: poll until
  `felinc_get_ip()` is not 0, as before. The network stays on afterwards.
- `felinc_shutdown` closes only the program's own sockets.
- When a program ends, the MCP closes the sockets it left open, also after a
  crash. A program started by another program only closes its own.
- `felinc_set_alloc`, `felinc_set_print` and `felinc_set_fatal` do nothing.

**A program linked with `felinc.a` (its own FELINC) must not run while the
network is on**: both would drive the same LAN chip. Use `NET OFF` first, or
link the program again with `felinc_mcp.a`.

The system calls are `0x80` - `0x8F` (`KFN_NET_*` in `src/include/syscalls.h`),
one per `felinc_*` function, with the arguments in the order of `felinc.h`;
`0x8E` gives the address, the NTP server, the MAC and the counters, and `0x8F`
prints FELINC's lists on the program's screen. `tests/nettest` calls them
directly.

**For developers.** `src/net/felinc` is an unchanged copy of a tagged FELINC
version (see its `README.md`); changes go to FELINC itself. It is built as
FELINC's own Makefile does: unsigned `char`, no `-O2`. `src/net/net.c` connects
it to the MCP: memory from the network pool (`src/netpool.c`), text to the
network log, errors that only stop the network. One lock keeps the background
tick out of FELINC while a system call or a command is inside it. Found on the
way: the MCP's own `strtok_r` (not the standard one) replaced the C library's,
so the library's `strtok` gave FELINC a garbage MAC address; it is now called
`mcp_strtok_r`.

**"-cache" instead of "c", and lvboot 1.4.** A cached MCP now calls itself
`2.1.0-cache` (boot screen, SYSINFO, CREDITS), the same name as its release file.
The boot loader no longer makes up that name: from lvboot 1.4 a flash block
names itself. The 16 bytes after the MCP info record (vectors 20-23, offset
`$50`, never read by any 680x0) hold the name, and bit 1 of the record's flags
says it is there; `tools/block_name.py` writes it, and the Makefile runs it
after linking (Calypsi and VBCC). Any other program in a flash block, such as
EmuTOS, can do the same: `tools/block_name.py image.bin "EmuTOS 1.3"` adds a
`"BLKI"` record with that name. lvboot 1.3 ignores the name and still shows a
new MCP as "MCP 2.1.0c"; lvboot 1.4 shows the old MCPs (2.0.30 - 2.0.34) as
before. The record is described in `lvboot/README.md`.

### Version 2.0.34: a usable command line, LS and a small VI

**Programs in any folder.** The loader looked for the file's extension in the
whole path, so a folder with a dot in its name (`SpaceCadet.1/pinball_k.pgz`)
made "file not found". It now takes the extension from the file name only, and
searches in the folder the path names, so `../`, `./` and paths with dots in
folder names work (`src/dev/fsys.c`).

**Editing the command line.**
- Typing inserts at the cursor instead of overwriting.
- The line stops at 127 characters.
- Home and End work.
- Because the A2560K's Left key sends nothing, there are also keys for moving:

| Key | Moves to |
|---|---|
| `Ctrl+A` | start of the line |
| `Ctrl+E` | end of the line |
| `Ctrl+B` | one place left |
| `Ctrl+F` | one place right |

`tests/keytest` shows what the keyboard sends.

**Tab completion.** Tab completes the file or folder name before the cursor:
- With one match, the rest is filled in, followed by `/` or a space.
- With several matches, the part they share is filled in. If there's nothing
  more to fill in, they're listed in columns.
- The first word also matches the built-in commands.

**New commands.**
- `MOVE <src> <dst>` moves files or folders, and accepts `*` and `?`.
  - On the same drive it renames.
  - To another drive it copies, checks the size, then deletes the original.
  - It never overwrites anything.
- `ECHO <text>` prints the text, for example in `mcp.init`.
- `LS [-l] [-a] [<path>]` is a Unix-like listing.
  - The names are shown in columns.
  - `-l` adds `drwxhs` flags, the exact size, and the date and time, with
    totals and the free space at the end.
  - `-a` also shows hidden and system files.
  - `x` marks files the MCP can run: those whose extension has a loader
    (`fsys_is_executable()`).
- `VI <path>` is a very small vi for text files (`src/cli/vi.c`).
  - Normal, insert and `:` modes.
  - `hjkl`, `w b 0 $ gg G`, `/` and `n`, `i a o O`, `x dd yy p J r`.
  - `:w :q :q! :wq` and `ZZ`.
  - A file with CR LF line ends is written back the same way.
  - There's no undo and there are no counts.
  - The text is kept in the free program memory from `$0001_0000` up to the
    top of memory. A file `LOAD`ed there is overwritten.

**Better old commands.**
- HELP, DIR, TYPE and LS pause after each screenful: any key goes on, `q`
  stops.
- `DEL` accepts `*` and `?`. It deletes matching files only, never folders.
- DIR shows the right size for files of 1 MB and more, and ends with the free
  space.
- DUMP prints on the screen again: it used `printf`, which doesn't reach it.

**Colours.**
- `SET FG <colour>` and `SET BG <colour>` set the text and background colours,
  either by name (16 names: `SET HELP` lists them) or as `RGBrrrgggbbb`. They
  change text colours 7 and 0, so ANSI resets keep them. To have them every
  time, put them in `mcp.init`.
- The start-up colours now really are grey on black on every machine. Before,
  an always-true `#if MODEL_FOENIX_A2560X` gave white on dark grey.

**Start-up fix.** The BSS clear in the start-up code stopped only when its
count was exactly 0. When the BSS size wasn't a multiple of 4, it ran through
all memory and the MCP never started: the border stayed green after lvboot. The
size is now rounded up to whole longs. Both `m68040/startup_m68040.s` and
`m68k/startup_m68k.s` are fixed.

Example `system/mcp.init`:

```
SET FG lightgreen
SET BG black
ECHO Welcome back
```

### Version 2.0.33: mcp.init runs to the end, programs return, longer history

**Screen set up before `mcp.init`.** When the MCP boots from SD card or hard
drive (F1 / F2, or the boot DIP switches), it ran `system/mcp.init` before the
command line had set up its screen. A program started from `mcp.init` then
left the screen without banner and with white text (the start-up colour)
instead of light gray. The set-up (console, start-up folder, title bar,
banner) is now `cli_prepare_screen()` in `src/cli/cli.c`, and `src/boot.c`
calls it before `mcp.init`. The start-up folder is set first too, so a `CD` in
`mcp.init` now stays in effect.

**Line endings in `mcp.init`.** `cli_exec_batch()` handed every line to the
command line with its `\n` (and `\r` for a file made on Windows) still on it,
and the commands took that as part of the last word: `SET CLOCK 24` in
`mcp.init` saw `24\n` and answered with its usage text and "general error".
The line ending is now removed first.

**Programs return to their caller.** The end of a program (`sys_exit`) always
restarted the command line from scratch, so nothing after a program ran: not
the rest of `mcp.init`, and not the command line's clean-up. Now
`call_user` (`src/m68040/startup_m68040.s`) saves a return point on the
supervisor stack before it starts a program, and `proc_exit()`
(`src/proc.c`) goes back to it: the command line, or the next line of
`mcp.init`, carries on. A program may start a program (its stack goes below
the first one's). Without a return point (a program started through the
debug port) the command line is restarted as before. The command history
survives running a program. The program's exit code is not shown as an error;
`proc_get_result()` has it. Only the 68040 machines (A2560K, A2560X, GenX)
have this; the 68000 build keeps the old restart.

**Command history.** Up / Down now go back through the last 50 commands
instead of 5 (`MAX_HISTORY_DEPTH` in `src/cli/cli.c`, 6.4 KB of the MCP's
RAM).

Example `system/mcp.init` that uses all of this:

```
/sd/myprogram.pgz
SET CLOCK 24
```

### Version 2.0.32: lost interrupts fixed, clock in the title bar, memory guarded

**Lost interrupts.** `int_clear` (`src/interrupt.c`) is meant to acknowledge
one interrupt, but it cleared every interrupt that was pending in the same
group: it read the pending register, which is "write 1 to clear", and wrote
the value back. Group 1 holds the keyboards, the serial ports, the timers and
the real-time clock, and the keyboard drivers call `int_clear`. At start-up
this wiped the RTC's first interrupt before its flags were read; the RTC then
kept its interrupt line active and never interrupted again (an old comment in
`rtc_init` already mentioned the RTC interrupt "getting acknowledged without
clearing the flags"). Later, a key could swallow a timer, serial or RTC
interrupt that happened to be pending at the same moment. `int_clear` now
writes only its own bit.

**Clock in the title bar.** `SET CLOCK ON` shows the time (`hh:mm:ss`) at the
right-hand end of the MCP title bar, `SET CLOCK OFF` removes it.
`SET CLOCK 24` shows it as `16:30:05` (the default), `SET CLOCK 12` as
`04:30:05 PM`; both also switch the clock on. Only the display changes: the
RTC itself and `GET TIME` stay as they are. It is off
after a start; put `SET CLOCK ON` in `/sd/system/mcp.init` to have it on every
time. The RTC interrupt (64 times a second) draws it once a second, straight
into the text memory of row 0, so it takes the colours of the bar and also
keeps running while programs run.

**24-hour mode kept.** `rtc_init` wrote the RTC control register with only the
STOP bit at every start, which switched the clock to 12-hour mode: after
`SET TIME 16:30:00` and a restart it said 04:30. It now keeps the 24-hour and
daylight-saving bits.

**Top of memory.** The MCP set the top of system RAM (`sys_mem_get_ramtop`,
below which programs may use RAM and from which `sys_mem_reserve` takes
memory) to a fixed `$00BD0000` on every machine. On the K25 that is video RAM
B, not system RAM, so reserved memory landed in the screen. On the A2560K,
A2560X and GenX it now comes from the linker script: `___memory_start`, where
the MCP's own data, heap and stack begin (`$003A0000` in the flash builds).
`sys_mem_reserve` in `src/syscalls.c` also returned its address cut to 16 bits.
The A2560U/U+ keep the old value until their layout is checked.

**Loader checks memory.** The program loaders (PGZ, PGX, ELF and the plain
`LOAD` to an address) wrote wherever the file said. A file linked for the wrong
addresses overwrote the MCP's variables or stack and the machine crashed
halfway through loading. On the A2560K, A2560X and GenX they now refuse, before
writing anything of that block, to load into `$0000_0000`-`$0000_FFFF` (vectors,
the MCP's variables, the program's stack) or from the top of memory up to the
end of system RAM (`$003A0000`-`$003FFFFF`: the MCP's own RAM and any TSRs),
with the error "memory used by the MCP". Video RAM and other memory stay
allowed, since data is loaded there on purpose. This only guards loading: a
program that wants the whole machine (a game, say) loads into free memory and
may take over the rest once it runs. Calypsi programs for the A2560K load at
`$200000` and keep their data below `$380000`, so they are not affected.

**Load errors shown.** `fsys_load` dropped the loader's result and always
returned 0, so every load error ("bad binary file", "out of memory", a read
error) showed up as "file is not executable". It now passes the error on. The
PGZ loader also never freed its read buffer; it does now.

**RTC call names.** The Calypsi SDK calls the RTC functions `sys_time_getrtc`
and `sys_time_setrtc`, the MCP's own syscall library `sys_rtc_get_time` and
`sys_rtc_set_time`. Both are the same kernel calls (`KFN_TIME_GETRTC` $52,
`KFN_TIME_SETRTC` $51). `src/syscalls.c` and both `syscalls.h` files now have
both names; `sys_time_getrtc` / `sys_time_setrtc` are the ones to use (they
match the Calypsi SDK and `sys_time_jiffies`), the old names stay so existing
programs still build.

**RTC test programs** in `tests/` (Calypsi, `make` in each folder):
`rtctest` measures the RTC speed against the video frame clock, `rtcclock`
shows the RTC with its own interrupt handler and counters, and `rtctsr`
installs a small resident clock in memory reserved with `sys_mem_reserve` (an
example of a TSR; with `SET CLOCK` it is not needed any more).
`tests/loadcheck/make_tests.py` writes three PGZ files the loader must refuse
(at `$8000`, at `$3A0000`, and one that runs 16 bytes past the top of memory).

### Version 2.0.31: command-line history fixed

Down in the command line history showed the command that was already on the
screen, so it was always one press late and never got back to an empty line.
Down now shows the next newer command and ends on an empty line. Empty lines
and a command identical to the one before it are no longer stored (an empty
newest entry made Up do nothing). The code is in `cli_readline` and
`cli_repl` in `src/cli/cli.c`.

### Version 2.0.30: the boot menu shows which MCP is in the flash

**A "c" after the version number means the CPU caches are on.** A cached
MCP calls itself `2.0.30c` everywhere: on its boot screen
(`FOENIX/MCP V: 02.0000.0030c`), in `SYSINFO` (`MCP version: v02.00.0030c`)
and in the command line banner. An MCP with the caches off shows plain
`2.0.30`. (`VER_SUFFIX` in `src/version.h`.)

**In the boot loader's menu** (lvboot 1.3, in flash block 0), an entry that
holds MCP 2.0.30 or later is now shown as **"MCP 2.0.30c"** or
**"MCP 2.0.30"**, whatever `BOOT.CFG` calls it. Other entries (EmuTOS, older
MCPs) keep their `BOOT.CFG` name and start exactly as before. Without a
`BOOT.CFG`, lvboot lists the non-blank blocks itself: the MCP then still shows
as "MCP 2.0.30c", the others as "Flash block 2" and so on (tested with
EmuTOS in block 2, which starts fine from the menu).

How lvboot knows: every MCP image now carries a small **info record** in its
exception vector table, in slots 16-19 (offset `$40` of the image). The
68040 and 68060 never use those slots, so they are free for data:

| Offset | Contents |
|---|---|
| `$40` | `"MCPI"` (`$4D435049`) |
| `$44` | version: major (bits 31-24), minor (23-16), build (15-0) |
| `$48` | flags: bit 0 = this MCP switches the CPU caches on |
| `$4C` | check: the three longs above XOR-ed |

lvboot only uses a record whose magic and check are both right. The
Makefile takes the version numbers from `src/version.h`, so the record is
always up to date.

**The cached MCP is harder to upset:**

* The boot screen and `SYSINFO` now read the CPU's cache register itself
  (`on`, `off`, `instructions only` or `data only`). In 2.0.29 they showed
  what the MCP had set, so they could say "on" after a program had switched
  the caches off.
* Every time the cached MCP starts a program, it sets its caches up again. A
  program that changes the cache registers (it can, by getting into
  supervisor mode) no longer leaves them changed for the programs after it.
* After `TEST GDMA`, `TEST BLIT` and `TEST DAC` the caches are set up anew,
  instead of putting back a saved register value.

**lvboot** lives in `lvboot/` (see its own README). Building it on a Mac:

```
cd lvboot
VBCC=$PWD/../vbcc PATH=~/opt/vbcc/bin:$PATH make MACHINE=K25
```

This gives `lvboot_k25.bin` for flash block 0 (`--flash lvboot_k25.bin
--block 0 --board K25`). Make a full flash backup first: block 0 is the
first thing the board starts, so a broken loader means no boot until the old
one is written back through the debug port. A Mac build has the same size
and works the same as a Windows build, but is not byte for byte the same (the
LVGL files are found in another order, and vasm writes `jsr` as `bsr.l`).

### Version 2.0.29: an MCP with the CPU caches on

Every MCP so far runs with the 68040's caches off. From 2.0.29 there is a
second kind of image, built with `CACHE=1`, that switches the data and
instruction caches on. Each release has both: `..._cache.bin` has the caches
on, the image without `_cache` is the same MCP with the caches off.

**How much faster?** Measured on an A2560K25 (68040 at 25 MHz) with
`tests/cachebench` and FELINC:

| Test | Caches off | Caches on | Faster |
|---|---|---|---|
| loop: 2 million rounds of arithmetic in CPU registers | 2483 ms | 1033 ms | 2.4 x |
| read: the same 4 KB read 500 times | 650 ms | 133 ms | 4.9 x |
| sieve: 50 runs of the sieve of Eratosthenes (8192 numbers) | 3066 ms | 933 ms | 3.3 x |
| memory: 8 MB copied with `memcpy` | 8766 ms | 3466 ms | 2.5 x |
| screen: 200 lines printed through the MCP console | 11716 ms | 3933 ms | 3.0 x |
| FELINC TCP fetch of 1 MB (`felinc_test`, key `b`) | 235 KB/s | 440 KB/s | 1.9 x |

The program's code and data were in SRAM. Everything gains: the MCP's own
code (in flash) is cached too, which is why even printing to the screen,
whose text memory is never cached, runs three times as fast.

**What the cached MCP does:**

* **When:** the caches go on at the end of the start-up, after all hardware
  has been set up and just before the boot screen. The start-up code has
  delay loops that count CPU cycles (LPC reset, SuperIO, PS/2); with the
  instruction cache on they would run too fast. Everything after that waits
  with the timers or with register reads, which take the same time with or
  without cache.
* **Cache modes** (the example set from the K25 hardware reference):
  `$FC00_0000-$FFFF_FFFF` (VGE, register blocks, flash data) is
  non-cacheable and serialized; all RAM is cached *write-through*. So every
  CPU write still reaches memory at once, and video, the GDMA, the blitter,
  the DAC and the debug port always see what the CPU wrote. Instructions
  are cached everywhere.
* **Starting code:** a program, a boot sector or `CALL <address>` is loaded
  with data writes, which the instruction cache does not see. The MCP empties
  both caches before it jumps into such code.
* **DMA tests:** `TEST GDMA`, `TEST BLIT` and `TEST DAC` let the hardware
  write memory the CPU then checks. The CPU does not see those writes, so
  these tests run with the data cache off.
* **Showing it:** the boot screen shows `CPU CACHE: ON` or `OFF`, and
  `SYSINFO` shows `CPU cache: on` or `off`.

**For your own programs:** with the caches on, a program that lets the GDMA
or the blitter write memory and then reads it, may read old cached copies.
Such programs must handle the cache themselves (in supervisor mode), or use
the image with the caches off.

The code: `src/cache.h` / `src/cache.c` (C side), the cache routines at the
end of `src/m68040/startup_m68040.s`, and the switch in `src/Makefile`.

### Version 2.0.28: no more lines lost at the bottom of the screen

**What you saw (2.0.27 and older):** print more lines than fit on the
screen, for example `printf("%d\n", i)` 200 times. The text scrolls, but the
last 3 lines end up below the visible screen. With 200 lines the last number
you can see is 197.

**Why it happened:** the main screen (VICKY III channel B) has two modes,
1280 x 1024 and 1280 x 960. The hi-res DIP switch chooses one, and the MCP
sets bit 8 of the channel B master control register (`MCR_B`) to match.

The MCP also keeps a copy of that register in memory (`msr_shadow_b`), so it
can change some bits without reading the register back.
`txt_a2560k_b_set_resolution()` set bit 8 only in the register, not in the
copy. `txt_a2560k_b_set_mode()` writes the whole register from the copy, and
the command line calls it after every command (`cli_ensure_console()`). From
then on the screen ran in 1280 x 960, but the MCP still counted rows for
1280 x 1024. That's 3 rows more than fit, so the bottom 3 rows were printed
where nobody could see them.

**The fix** (`src/dev/txt_a2560k_b.c`): `txt_a2560k_b_set_resolution()` now
puts the mode bit in the copy as well, so later calls to `set_mode` keep it.
The function also got its missing `return 0`. With the DIP switch on, the
screen now stays in 1280 x 1024 all the time.

**Testing it:** `tests/scrtest` prints 200 numbered lines and then writes a
report at the top of the screen. The report shows what the MCP believes
(pixel size, text rows, print region) next to what the hardware register
really says (`DIP_HIRES`, `MODE_1024`). On a fixed MCP with the DIP switch on,
you see `MODE_1024=1 -> 1280x1024`, and 200 is the last number at the
bottom of the screen.

### Building with Calypsi (2.1.0)

The MCP is built with Calypsi (`cc68k`, `as68k`, `ln68k`, version 5.18.5 or
newer); it is needed to build FELINC into the MCP. Only the A2560K K25 / K40
in flash block 1 has Calypsi linker rules so far
(`src/calypsi/a2560k_flash_b1.scm`). `COMPILER=vbcc` still builds with VBCC,
as before 2.1.0 (see "Building on a Mac").

Calypsi runs on Linux here (the Mac version does not work for this project).
On the Linux computer:

```
cd src
make UNIT=a2560k BLOCK=1 clean
make UNIT=a2560k BLOCK=1 CACHE=1
```

The link step gives two raw files, which `tools/calypsi_image.py` puts into
the 512 KB image `src/foenixmcp.bin`, the same as with VBCC.

**Compiler version.** The build stops with an older Calypsi than 5.18.5.
Older versions miscompile code that is in the MCP: a comparison of two
constants inside `&&`, `*reg = *reg | bit` on a 32-bit I/O register (made
into a one-byte `bset.b`), a 16-bit index into a table of records (multiplied
in place), and a bit test of a long (read from the wrong word). 2.1.x worked
around these in the C code and checked the compiler's list files with
`tools/calypsi_check.py`; since 2.2.0 the code is written plainly again and
the check is gone.

**Speed.** In a large function Calypsi keeps loop values on the stack, and it
inlines a small function from the same file back into its caller (it ignores
`noinline`). That is why the inner loops of the text screen drivers are in a
file of their own, `src/dev/txt_cells.c`. With that, `tests/cachebench` (caches
on) gives the same times for both compilers:

| Test | VBCC | Calypsi |
|---|---|---|
| loop | 1033 ms | 1033 ms |
| read | 133 ms | 133 ms |
| sieve | 933 ms | 933 ms |
| memory | 3466 ms | 3466 ms |
| screen | 3583 ms | 3683 ms |

Calypsi's image is also about 14% smaller.

**Calypsi SDK for programs.** The MCP headers in Calypsi's own SDK
(`contrib/A2560K-SDK`) are older than MCP 2.1.0. For example, its `t_sys_info`
has a different layout, and `sys_fsys_stat` and `sys_txt_topbar` are missing.
`sdk/calypsi` has headers and a library that match the MCP. See
[sdk/calypsi/README.md](sdk/calypsi/README.md). The MCP build runs
`tools/check_sdk.py`, which stops it when the SDK and the MCP no longer agree
on system call numbers, error numbers or records.

### Building on a Mac

On a Mac the MCP is built with VBCC: the `vc` front end, the `vbccm68k`
compiler, the `vasmm68k_mot` assembler and the `vlink` linker. Add
`COMPILER=vbcc` to every `make`, since Calypsi is the default (see above).

#### 1. Install the VBCC tools (once)

Get the sources of vbcc, vasm and vlink from <http://www.compilers.de/vbcc.html>
(this machine uses vbcc 0.9h, vasm 2.0f and vlink 0.18a). Unpack them under
`~/opt/vbcc/src` and build them:

```
cd ~/opt/vbcc/src/vbcc  && make TARGET=m68k   # asks a few questions; the defaults are fine
cd ~/opt/vbcc/src/vbcc  && make bin/vc        # the vc front end: easy to forget
cd ~/opt/vbcc/src/vasm  && make CPU=m68k SYNTAX=mot
cd ~/opt/vbcc/src/vlink && make
```

Then put the four programs together in one folder:

```
mkdir -p ~/opt/vbcc/bin
cp ~/opt/vbcc/src/vbcc/bin/vc ~/opt/vbcc/src/vbcc/bin/vbccm68k \
   ~/opt/vbcc/src/vasm/vasmm68k_mot ~/opt/vbcc/src/vlink/vlink ~/opt/vbcc/bin/
```

You don't need a VBCC `config` or `targets` folder in `~/opt/vbcc`: this
repository has its own in `vbcc/`, with the C library and the linker scripts
for the Foenix machines.

#### 2. Make the splash screen file (once after a fresh clone)

The boot picture `src/rsrc/bitmaps/splash_a2560k.bmp` is turned into C code
by a Python script. That script needs the Pillow library (`PIL`). If your
Python has it, the build does this by itself. If not, either install Pillow
(`python3 -m pip install pillow`, in a virtual environment if Homebrew's
Python refuses), or make the file on a machine that has it:

```
cd src
python3 rsrc/bitmaps/bmp2c.py -i rsrc/bitmaps/splash_a2560k.bmp -o rsrc/bitmaps/splash_a2560k.c
```

The generated `.c` file is not kept in git (see `.gitignore`).

#### 3. Build

```
cd src
export VBCC=$PWD/../vbcc            # the vbcc folder of this repository
export PATH=~/opt/vbcc/bin:$PATH    # the VBCC programs
make UNIT=a2560k BLOCK=1 COMPILER=vbcc
```

The result is `src/foenixmcp.bin`, padded to 512 KB: the image for flash
block 1, where the MCP lives in the multi-boot layout (lvboot in block 0,
the MCP in block 1, EmuTOS in block 2). `BLOCK=1` selects
`vbcc/config/a2560k_flash_b1_linux`, which also works on macOS.

**Run `make UNIT=a2560k BLOCK=1 clean` first whenever you change a header
file** (`.h`), and when you switch between `BLOCK`, `CPU060` or `CACHE`
values. The Makefile does not know which `.c` files use which headers, so
without a clean build old object files stay in use. `src/version.h` is a
header too.

Make settings for the other images:

| Image | Settings |
|---|---|
| A2560K K25 / K40 | `UNIT=a2560k BLOCK=1` |
| A2560K K60 (68LC060) | `UNIT=a2560k BLOCK=1 CPU060=1` |
| A2560X | `UNIT=a2560x` |
| GenX | `UNIT=genx` |
| any of these with the CPU caches on | add `CACHE=1` |
| any of these with VBCC | add `COMPILER=vbcc` (the K60, A2560X and GenX have no Calypsi linker rules yet) |

**All images at once:** `tools/build_release.sh` builds every machine twice
(caches off and on), each from clean, into `release/<version>/`, with a
`SHA256SUMS.txt` and a build log per image. It finds the VBCC programs on the
PATH or in `~/opt/vbcc/bin`. Since 2.1.0 it is not used: it builds with
VBCC, and it will be brought up to date when all images are released again. The A2560U is not in the list: its 68000 version
does not build from this source tree yet.

A Mac build works the same as a Windows build, but the two images are not
byte for byte the same: the newer vasm writes some instructions in a shorter
form (for example `jsr` becomes `bsr.l`).

#### 4. Flash and test

Give the image a name that says what it is, for example
`foenixmcp_2_0_30_k25_k40_b1.bin`. Then use the Gen1 debug tool on the computer
that is connected to the A2560K's debug port. Name the port by its fixed
name in `/dev/serial/by-id/` (here `usb-Exar_Corp._XR21B1411_...`): the
`/dev/ttyUSBn` numbers can change after a reboot of that computer. Always
make a full backup first: with it you can go back to the old flash.

```
python3 a2560_gen1_dbg.py --port <port> --backup backups/flash_before.bin
python3 a2560_gen1_dbg.py --port <port> --flash foenixmcp_2_0_30_k25_k40_b1.bin --block 1 --board K25 --dry-run
python3 a2560_gen1_dbg.py --port <port> --flash foenixmcp_2_0_30_k25_k40_b1.bin --block 1 --board K25
```

The dry run checks the image and the board and writes nothing. The real
write erases block 1, writes it, reads it back to check it, and resets the
board. The boot screen then shows the new version. To go back:
`--restore backups/flash_before.bin`.

To check the screen, build `tests/scrtest` (with Calypsi: `make` in that
folder), put `scrtest.pgz` on the SD card and start it from the MCP.

## License

Most of the source code for this kernel is made available under the open source
BSD 3-Clause License (see LICENSE). However, the kernel also includes portions
of other projects, which are covered under their licenses. In particular, the
kernel currently uses the FatFS embedded file system, which is covered under
separate license. Please see the [src/fatfs](src/fatfs) directory for license
details.

### Attribution

This project is based on the original [FoenixMCP](https://github.com/pweingar/FoenixMCP)
project. Special thanks to [pweingar](https://github.com/pweingar) for creating
FoenixMCP and laying the foundation for this work.

Thank you to all contributors to the original project, as listed in its
[contributor history](https://github.com/pweingar/FoenixMCP/graphs/contributors):

* [pweingar](https://github.com/pweingar)
* [vinz6751](https://github.com/vinz6751)
* [daschewie](https://github.com/daschewie)
* [hth313](https://github.com/hth313)

## Features

See the [features list](FEATURES.md) for the features intended for this kernel
and the status of implementation.


# //END-OF-LINE
