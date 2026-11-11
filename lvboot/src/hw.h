/*
 * hw.h - Gen1 LVGL boot loader: the registers it uses, per machine (make MACHINE=K25 | K40 | K60 | X40 | X60).
 * Port of the A2560Me loader (FoenixMCP_Gen2/lvboot), 2026-11-06. Everything is polled; interrupts stay masked.
 * Sources: the Gen1 K25 Hardware Reference (rev 0.4) and the Gen1 work area notes (multiboot.md, gui_mode.md).
 *
 * Only the K25 FPGA has the GUI mode, the SD block engine and BOOT_SEL today (build 1110+). The other machines get
 * them with the FPGA port.
 */
#ifndef HW_H
#define HW_H
#include <stdint.h>

#define REG32(a)        (*(volatile uint32_t *)(a))
#define REG16(a)        (*(volatile uint16_t *)(a))
#define REG8(a)         (*(volatile uint8_t *)(a))

#if defined(MACHINE_K25)
# define MACHINE_NAME   "A2560K (K25)"
# define BUS_KHZ        25000UL             /* default bus clock (the timer calibration replaces it) */
# define MACHINE_HAS_MO_KEYBOARD 1          /* the built-in keyboard ("Mo") at $FEC0_0040 */
#elif defined(MACHINE_K60)
# define MACHINE_NAME   "A2560K (K60)"         /* MC68LC060 66.67 MHz, bus 33.33 MHz (GAVIN: CPU 11, speed 6) */
# define BUS_KHZ        33333UL
# define MACHINE_HAS_MO_KEYBOARD 1
# define BOOT_MIN_BUILD 0x1205UL            /* first K60 build with BOOT_SEL */
#elif defined(MACHINE_K40)
# define MACHINE_NAME   "A2560K (K40)"         /* MC68040V 33.33 MHz (GAVIN: CPU 6, speed 3) */
# define BOOT_MIN_BUILD 0x1301UL            /* first K40 build with BOOT_SEL */
# define BUS_KHZ        33333UL
# define MACHINE_HAS_MO_KEYBOARD 1
#elif defined(MACHINE_X40) || defined(MACHINE_X60)
# define MACHINE_NAME   "A2560X"
# define BUS_KHZ        33333UL
# define MACHINE_HAS_MO_KEYBOARD 0          /* PS/2 keyboard through the SuperIO */
#endif

/* GAVIN */
#define GAVIN_CTRL      REG32(0xFEC00000)   /* bit 8 = LPC_RSTn (SuperIO out of reset; 1 after reset on Gen1) */
#define GAVIN_ID        REG32(0xFEC0000C)   /* [31:16] FPGA build number in hex digits ($1112 = build 1112) */
#define FPGA_BUILD      ((GAVIN_ID >> 16) & 0xFFFFUL)
#define BOOT_SEL        REG32(0xFEC00030)   /* write $B0 << 24 | layout << 4 | block: reboot into that block (1110+) */
#define BOOT_INFO       REG32(0xFEC00034)   /* [7] chosen by BOOT_SEL, [6] fell back to block 0 */
#define BOOT_BASE       REG32(0xFEC00038)   /* CPU address of the block this boot came from */
#define BOOT_KEY        0xB0000000UL
#ifndef BOOT_MIN_BUILD
#define BOOT_MIN_BUILD  0x1110UL            /* first K25 build with BOOT_SEL */
#endif

/* timers: 0-2 count the bus clock, 3 counts channel A frames (60 Hz), 4 channel B frames */
#define TIMER_TCR0      REG32(0xFEC00200)
#define TIMER_TCR1      REG32(0xFEC00204)
#define TIMER_VALUE0    REG32(0xFEC00208)
#define TIMER_VALUE3    REG32(0xFEC00220)
#define TCR_ENABLE      0x01UL
#define TCR_CLEAR       0x02UL
#define TCR_COUNT_UP    0x08UL

/* SD card (front slot, /sd in the MCP): G1_SDC - byte SPI port + block engine (K25 build 1107+) */
#define SD_CTRL         REG8(0xFEC00300)    /* [0] CS, [1] slow (284 kHz), [7] busy */
#define SD_DATA         REG8(0xFEC00301)
#define SD_CLKDIV       REG32(0xFEC00304)   /* SCLK half period - 1: 0 = 12.5 MHz (reset 1 = 6.25 MHz) */
#define SD_BLKCMD       REG32(0xFEC00308)   /* N << 16 | token << 8 | op: 1 read N packets, 2 write, 3 abort + clear */
#define SD_BLKSTAT      REG32(0xFEC0030C)   /* [0] busy [1] CRC error [2] time-out [3] write rejected [31:16] done */
#define SD_FIFOCNT      REG32(0xFEC00310)   /* long words in the FIFO */
#define SD_FIFO         REG32(0xFEC00320)
#define SDX_CS          0x01
#define SDX_SLOW        0x02
#define SDX_BUSY        0x80

/* SuperIO (LPC, SMSC, same chip and ports as the A2560Me) */
#define GAVIN_LPC_RSTN  0x00000100UL
#define SIO_INDEX       REG8(0xFEC0202E)
#define SIO_DATA        REG8(0xFEC0202F)

/* PS/2 (8042 in the SuperIO, logical device 7 at I/O $60 / $64): the K25's mouse port, the X's keyboard + mouse */
#define PS2_DATA        REG8(0xFEC02060)
#define PS2_STAT        REG8(0xFEC02064)
#define PS2_CMD         REG8(0xFEC02064)
#define PS2_OBF         0x01
#define PS2_IBF         0x02
#define PS2_AUX         0x20

/* built-in keyboard (K): a 256-word FIFO; a long read pops one word: [31] empty after the read, [29] valid,
   [7:0] scan code (set 1, bit 7 = break; the extra keys have their own codes $60-$6F, no $E0 prefix) */
#define KBD_MO_DATA     REG32(0xFEC00040)
#define KBD_MO_VALID    0x20000000UL

/* console: SuperIO COM1 (16550, logical device 4 at I/O $3F8, 1.8432 MHz clock: divisor 1 = 115200 baud) */
#define COM1(r)         REG8(0xFEC023F8 + (r))
#define COM_LSR_RX      0x01
#define COM_LSR_THRE    0x20

/* video: channel B master control and the GUI mode (frame buffer in SDRAM) */
#define VKY_MSTR        REG32(0xFEC80000)   /* [0] text, [2] graphic layer, [9:8] video mode (0 = 1280 x 960), [20] TOSGraph */
#define VKY_BORDER_CTRL REG32(0xFEC80004)
#define GUI_CTRL        REG32(0xFEC80100)   /* [0] fetch on, [2:1] depth (1 = 16 bpp RGB565), [3] pixel doubling */
#define GUI_BASE        REG32(0xFEC80104)   /* frame buffer CPU address (16-byte aligned) */
#define GUI_STRIDE      REG32(0xFEC80108)   /* 0 = packed */
#define GUI_STATUS      REG32(0xFEC8010C)   /* [7:0] late lines; write = clear */
#define GUI_FB_ADDR     0x04000000UL        /* SDRAM (free at boot) */

/* boot flash */
#define FLASH_BASE      0xFFC00000UL
#define FLASH_SIZE      0x00400000UL

#endif
