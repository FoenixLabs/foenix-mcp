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
For the 65816 and 68000 computers, the project will be built using the VBCC
compiler.

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

### Version 2.0.29: an MCP with the CPU caches on

Every MCP so far runs with the 68040's caches off. From 2.0.29 there is a
second kind of image, built with `CACHE=1`, that switches the data and
instruction caches on. Each release has both: `..._cache.bin` has the caches
on, the image without `_cache` is the same MCP with the caches off.

**How much faster?** Measured on an A2560K25:

| Test | Caches off | Caches on |
|---|---|---|
| FELINC TCP fetch of 1 MB (`felinc_test`, key `b`) | 230 KB/s | 440 KB/s |
| `tests/cachebench`, CPU (50 sieve runs) | | 3000 ms |

Code and data in RAM gain the most. Screen output gains little: the text
screen is a register block, and register blocks are never cached.

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

### Building on a Mac

The MCP is built with VBCC: the `vc` front end, the `vbccm68k` compiler, the
`vasmm68k_mot` assembler and the `vlink` linker. Calypsi is not used for the
68040 version.

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
make UNIT=a2560k BLOCK=1
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

**All images at once:** `tools/build_release.sh` builds every machine twice
(caches off and on), each from clean, into `release/<version>/`, with a
`SHA256SUMS.txt` and a build log per image. It finds the VBCC programs on the
PATH or in `~/opt/vbcc/bin`. The A2560U is not in the list: its 68000 version
does not build from this source tree yet.

A Mac build works the same as a Windows build, but the two images are not
byte for byte the same: the newer vasm writes some instructions in a shorter
form (for example `jsr` becomes `bsr.l`).

#### 4. Flash and test

Give the image a name that says what it is, for example
`foenixmcp_2_0_29_k25_k40_b1.bin`. Then use the Gen1 debug tool on the computer
that is connected to the A2560K's debug port. Name the port by its fixed
name in `/dev/serial/by-id/` (here `usb-Exar_Corp._XR21B1411_...`): the
`/dev/ttyUSBn` numbers can change after a reboot of that computer. Always
make a full backup first: with it you can go back to the old flash.

```
python3 a2560_gen1_dbg.py --port <port> --backup backups/flash_before.bin
python3 a2560_gen1_dbg.py --port <port> --flash foenixmcp_2_0_29_k25_k40_b1.bin --block 1 --board K25 --dry-run
python3 a2560_gen1_dbg.py --port <port> --flash foenixmcp_2_0_29_k25_k40_b1.bin --block 1 --board K25
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

## Features

See the [features list](FEATURES.md) for the features intended for this kernel
and the status of implementation.


# //END-OF-LINE
