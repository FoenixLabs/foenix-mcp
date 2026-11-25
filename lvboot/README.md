# Gen1 LVGL boot loader (K25 first)

**1.5 (2026-11-25):** a fast start after a warm reset. Nothing on the board resets the SD card (not the reset button,
not REBOOT, not a debug-port release), and the reset button resets the IDE drive just before lvboot runs (after a
power-on the board waits about 9 s first). lvboot 1.4 could then take a very long time at "reading boot.cfg".
Now: the CMD12 that stops a card left in a multi-block read goes out with the card selected (1.4 sent it with the card
not selected, so the card never heard it); CMD0 no longer waits for a "ready" byte first; an empty SD slot is seen at
once (card-detect pin); the SD card is mounted again only after a read error, not after a failed start; an IDE drive
gets up to 10 s to finish its reset (was 3 s), and a drive left in the middle of a command gets an ATA software reset.

**1.4 (2026-11-21):** an entry shows the name its flash block gives itself, so lvboot never needs to know how a
program is called. Any program in a flash block (the MCP, EmuTOS, ...) can carry a block info record in its 680x0
vector table, vectors 16-23 (offset `$40`-`$5F` of the block): Motorola reserved these on every 680x0 (68000 up to
68060) and the CPU never reads them.

| Offset | Long | Contents |
|--------|------|----------|
| `$40` | magic | `"MCPI"` (the MCP, since 2.0.30) or `"BLKI"` (any other program) |
| `$44` | version | the MCP: major << 24 \| minor << 16 \| build; 0 for others |
| `$48` | flags | bit 0 = the MCP switches the CPU caches on; bit 1 = `$50` holds the name |
| `$4C` | check | the three longs above XOR-ed together |
| `$50`-`$5F` | name | 1-15 printable ASCII characters, then 0 bytes (only with flags bit 1) |

`tools/block_name.py IMAGE.bin "EmuTOS 1.3"` writes the name into any block image (it adds a `"BLKI"` record when
there is none); the MCP's Makefile runs it from MCP 2.1.0 on ("MCP 2.1.0-cache"). The record replaces the BOOT.CFG
name. An `"MCPI"` record without a name (MCP 2.0.30 - 2.0.34) is shown as in 1.3 ("MCP 2.0.34c"). Older boot
loaders ignore bit 1 and the name, so lvboot 1.3 still shows a new MCP as "MCP 2.1.0c".

**1.3 (2026-11-13):** an entry whose flash block holds MCP 2.0.30 or later is shown as "MCP 2.0.30c" (c = caches on) or "MCP 2.0.30",
from the MCP info record at offset `$40` of the block (see the main README, version 2.0.30). The Makefile also builds on
macOS / Linux now: `VBCC=../vbcc PATH=~/opt/vbcc/bin:$PATH make MACHINE=K25`.

Port of the A2560Me LVGL boot loader (FoenixMCP_Gen2/lvboot) to the Gen1 machines, 2026-11-06. One source tree;
`make MACHINE=K25` (K40 / K60 / X40 / X60 follow with their FPGA ports: src/hw.h has the per-machine layer).

- **Images:** `lvboot_k25.bin` = flash block 0 (`a2560me_dbg.py --port COM49 --flash lvboot_k25.bin --address FFC00000`,
  back up the block first); `lvboot_k25_ram.bin` = RAM test (`--run lvboot_k25_ram.bin --address 0`). 313 KB of 512.
- **Needs:** K25 FPGA build 1110+ (BOOT_SEL at $FEC0_0030; 1112 recommended), the MCP linked for block 1
  (E:\FoenixMCP_Gen1 `make UNIT=a2560k MEMORY=flash BLOCK=1`).
- **Gen1 differences:** 68040 (cache set-up in src/crt0.s: SRAM copyback, registers / flash not cached, the frame
  buffer in SDRAM write-through); the multi-boot trampoline is in crt0.s and runs from the flash copy of block 0
  (it copies the chosen block's first 64 KB over the loader in SRAM); screen = channel B GUI mode 640 x 480 16 bpp
  doubled, frame buffer SDRAM $0400_0000; SD = G1_SDC with the block engine (src/sd_g1.c), BOOT.CFG in the root of
  the SD card; keys = the built-in keyboard (codes $68-$6B arrows) + PS/2 mouse; console COM1 (115200, COM50 on the
  host); hand-over record at SRAM $003F_BF00 (4 MB SRAM).
- **No BOOT.CFG / no card:** the non-blank 512 KB blocks are listed and the first one starts after 5 s.

The A2560Me README follows (keys, boot.cfg format, speed notes).

---

# A2560Me LVGL boot loader

A small graphical boot menu for the Foenix A2560Me (FPGA build 83 or later). It is a stand-alone program, not built on
the MCP. It shows the Foenix Labs logo and the entries of `/sd2/boot.cfg`, counts down, and reboots the board into the
chosen flash block through the `BOOT_SEL` register (see `docs/A2560Me_Boot_Selection.md` in the FPGA work area).

- **Screen:** 640 x 480, 16 bpp (GUI bitmap engine, frame buffer in DDR3 at `$3000_0000`), LVGL 8.3.
- **Input:** PS/2 keyboard and mouse (polled 8042 in the SuperIO), and the COM4 console (115200 baud) for keys and
  debug messages.
- **Storage:** the internal SD card (SPI port at `$FEC0_0380`) with FatFs R0.14b. It reads `BOOT.CFG` and writes back
  `DEFAULT =` (verified by reading it back).
- **Size:** about 310 KB of the 512 KB block. It runs from SRAM with the 68060 caches on (1.1: data cache too).

## Speed (1.1, 2026-10-15)

Measured on the board (FPGA build 85) with the `POPUPBENCH` test build:

| | 1.0 | 1.1 |
|---|---|---|
| open the options pop-up | 1062 ms | **103 ms** |
| close it | 454 ms | **63 ms** |
| redraw the whole screen | 446 ms | **106 ms** |

What made the difference, in order:
1. **Data cache on** (copyback for SRAM / DDR3 below `$2000_0000`, write-through for the frame buffer, registers and
   flash not cached), the same setup as the MCP's `TEST CACHE`: 2.7 x on everything.
   - The hand-over record and `exc_info` are pushed out with `CPUSHA` before anyone else reads them.
2. **No full-screen dimming layer behind the pop-up:** with it, every open and close redrew and blended all 307 200
   pixels in software. The panel now sits on LVGL's top layer, and the top layer catches the clicks outside the panel.
   The old look (menu dimmed behind) is still a build option: `make EXTRA=-DPOPUP_DIM` (open about 0.3 s).
3. **The logos are converted once** from 8-bit indexed (small in flash) to plain RGB565 in SRAM: LVGL copies them
   instead of decoding the palette on every redraw.
4. **A full-screen draw buffer** (600 KB in SRAM) instead of a 64-line strip.
5. **Each pop-up is built once, hidden,** when the menu starts: opening only shows it (about 70 ms saved per open).

The copy to the DDR3 frame buffer is only 24 ms of a full-screen redraw, so DMA (MDMA / VDMA) would gain little: the
time is LVGL's software drawing.

## Keys

`1`-`9` start that entry; the arrows or Tab move the selection; Enter starts the selected entry. Any key or mouse
movement stops the countdown; a click starts an entry. An entry whose block is blank shows "empty" and cannot be started.

An entry with options (`3.1 = "..."` lines) opens a pop-up in its own colours (VioletBSD: violet, with its
phoenix logo, `assets/violetbsd_logo.png`). 1-n, the arrows or Enter start an option; Esc goes back. `3.DEFAULT` is
remembered. The started program finds the chosen entry and option in the hand-over record at SRAM `$007F_BF00`
(magic "LVBT"). See `src/bootcfg.h` and section 8 of `docs/A2560Me_Boot_Selection.md`.

## `boot.cfg`

The same file and format as the MCP boot menu (`#` starts a comment):

```
TIMEOUT = 3          # seconds, 0 = wait for a key
DEFAULT = 1          # the last choice, written back by the menu
1 = $FFC80000  512K  "Foenix/MCP"
2 = $FFD00000  512K  "EmuTOS"
3 = $FFD80000  512K  "VioletBSD"
3.1 = "VioletBSD"
3.2 = "VioletBSD (Expand HDD)"
3.3 = "VioletBSD (Single User)"
3.DEFAULT = 1        # the last option, written back
```

With no card, no file or no usable entry, it lists the non-blank 512 KB blocks 1-7 and waits. If the last choice
could not be booted (`BOOT_INFO` bits 7 and 6 set: the block was blank), it says so and waits, so it can never loop.

## Building

VBCC (`vbccm68k`, `vasmm68k_mot`, `vlink` on the PATH) and GNU make, from this folder:

```
make            # lvboot.bin: flash image (block 0, or any block: it copies itself from BOOT_BASE)
make RAM=1      # lvboot_ram.bin: RAM test image
make clean
```

`VBCC` must point to the `FoenixMCP_Gen2/vbcc` folder (for `libvc` and the target headers).

Notes:
- Compiled with `-cpu=68060` (VBCC then uses 68060-safe helpers for 64-bit arithmetic) and `-sec-per-obj`. It is
  linked with `vlink -gc-all`, because LVGL's headers put many `static inline` functions in every file. Without this,
  the image is 722 KB.
- VBCC finds `#include "..."` files next to the source file only when the source path uses backslashes. The Makefile
  converts the paths.
- LVGL needed three small changes for VBCC on a big-endian CPU: see `lvgl/PATCHES.md`.
- The logos are generated from `assets/*.png` with `python tools/mklogo.py`.

## Trying it without flashing

```
python a2560me_dbg.py --port COM18 --run lvboot_ram.bin --address 0 --reset-first
```

Or flash it into a free block (for example block 7) and start it from the MCP with `BOOTSEL 7 512K`.

Pop-up timing test: `make RAM=1 EXTRA=-DPOPUPBENCH` (delete `build/obj/src/menu.o` and `video.o` first). It opens and
closes the pop-up of the first entry with options 5 times and writes the times to SRAM `$007F_BD00` ('BNCH', rounds,
then open / close / full-screen ms per round; `+$5C` = object building ms of the last open; `+$48`.. = flush ms). Read
them over the debug port while it runs.

## Memory map

| Range | Use |
|---|---|
| SRAM `$0000_0000-$0000_03FF` | vectors (every exception: red border, record in `exc_info`, stop) |
| SRAM `$0000_0400-` | code and data (image), then BSS: LVGL heap 256 KB, draw buffer 600 KB, logos 148 KB |
| SRAM `$007F_0000` | top of the stack |
| SRAM `$007F_BF00` | hand-over record for the started program (100 bytes) |
| DDR3 `$3000_0000` | frame buffer, 600 KB |

## Known board issue (2026-10-14)

Reads of the SD2 data register (`$FEC0_0381`, local bus to the 10M02) sometimes return bit 6 set by mistake: about 1 in
900 bytes when read back to back, 1 in 2700 with a wait for the busy flag. The loader reads the register until two
reads agree; that gave 0 errors in 1.3 MB. The card data itself is fine. The MCP's SD2 driver does not do this yet.

## Licences

The loader code is A2560Me project code. LVGL is MIT (`lvgl/LICENCE.txt`). FatFs is ChaN's BSD-style licence (header
of `fatfs/ff.c`).
