/*
 * board.c - Gen1 LVGL boot loader: time base, delays and the debug console (port of the A2560Me loader, 2026-11-06).
 *
 * Time: timer 0 counts the bus clock (25 MHz on the K25). board_init() measures it against the channel A frame
 * counter (timer 3, 60 Hz) so the millisecond tick is right whatever the bus clock is; without frames it keeps BUS_KHZ.
 * Console: SuperIO COM1 (16550 at I/O $3F8, 115200 8N1 = divisor 1; COM50 on the host PC). On a cold boot nothing has
 * configured the SuperIO yet: board_init() switches on its logical device 4 (COM1) itself, as the MCP's superio_init().
 * Output is for debugging; input gives the same keys as the keyboard (handy for remote tests).
 */
#include <stdarg.h>
#include "lvgl.h"
#include "hw.h"
#include "board.h"

static uint32_t ticks_per_ms = BUS_KHZ;     /* replaced by the measured value */
static uint32_t last_ticks, frac_ticks, now_ms;
static int com_ok;

static void sio_write(uint8_t index, uint8_t value) { SIO_INDEX = index; SIO_DATA = value; }

static void com1_init(void) {
    if (!(GAVIN_CTRL & GAVIN_LPC_RSTN)) GAVIN_CTRL |= GAVIN_LPC_RSTN;   /* (1 after every reset on Gen1) */
    SIO_INDEX = 0x55;                       /* configuration mode */
    sio_write(0x07, 0x04);                  /* logical device 4: serial port 1 */
    sio_write(0x60, 0x03);                  /* base $03F8 */
    sio_write(0x61, 0xF8);
    sio_write(0x70, 0x04);                  /* IRQ 4 (unused: polled) */
    sio_write(0x30, 0x01);                  /* on */
    SIO_INDEX = 0xAA;
    COM1(3) = 0x83;                         /* LCR: divisor latch */
    COM1(0) = 1;                            /* 115200 */
    COM1(1) = 0;
    COM1(3) = 0x03;                         /* 8N1 */
    COM1(2) = 0x07;                         /* FIFOs on and cleared */
    COM1(1) = 0x00;                         /* no interrupts */
    COM1(4) = 0x03;                         /* DTR, RTS */
    com_ok = 1;
}

void board_init(void) {
    uint32_t f, t0, t1, guard;

    TIMER_TCR0 = TCR_ENABLE | TCR_CLEAR | TCR_COUNT_UP;     /* timer 0: free-running up-counter on the bus clock */
    TIMER_TCR0 = TCR_ENABLE | TCR_COUNT_UP;
    TIMER_TCR1 = TCR_ENABLE | TCR_CLEAR | TCR_COUNT_UP;     /* timer 3: channel A frame counter */
    TIMER_TCR1 = TCR_ENABLE | TCR_COUNT_UP;

    com1_init();

    /* measure: bus clock ticks over 6 frames (100 ms); give up after about 0.5 s and keep BUS_KHZ */
    guard = TIMER_VALUE0;
    f = TIMER_VALUE3;
    while (TIMER_VALUE3 == f && TIMER_VALUE0 - guard < BUS_KHZ * 500UL) ;
    t0 = TIMER_VALUE0;
    f = TIMER_VALUE3;
    while (TIMER_VALUE3 - f < 6 && TIMER_VALUE0 - guard < BUS_KHZ * 500UL) ;
    t1 = TIMER_VALUE0;
    if (TIMER_VALUE3 - f >= 6 && t1 - t0 > 1000000UL)
        ticks_per_ms = (t1 - t0) / 100;

    last_ticks = TIMER_VALUE0;
    frac_ticks = 0;
    now_ms = 0;
}

uint32_t board_ms(void) {
    uint32_t t = TIMER_VALUE0;
    frac_ticks += t - last_ticks;           /* unsigned difference: fine across the 32-bit wrap */
    last_ticks = t;
    if (frac_ticks >= ticks_per_ms) {
        uint32_t n = frac_ticks / ticks_per_ms;
        now_ms += n;
        frac_ticks -= n * ticks_per_ms;
    }
    return now_ms;
}

uint32_t board_ticks_per_ms(void) { return ticks_per_ms; }

void delay_us(uint32_t us) {
    uint32_t t0 = TIMER_VALUE0;
    uint32_t n = (us * (ticks_per_ms / 8)) / 125;   /* us x ticks_per_us without overflow below 64 ms */
    while (TIMER_VALUE0 - t0 < n) ;
}

void delay_ms(uint32_t ms) {
    while (ms--) delay_us(1000);
}

/* ---- console ---- */

static void dbg_putc(char c) {
    uint32_t n = 20000;
    if (!com_ok) return;
    if (c == '\n') dbg_putc('\r');
    while (!(COM1(5) & COM_LSR_THRE) && --n) ;     /* 16-byte FIFO, a byte every 87 us: wait a little, then drop */
    if (n) COM1(0) = (uint8_t)c;
}

void dbg_puts(const char * s) {
    while (*s) dbg_putc(*s++);
}

void dbg_printf(const char * fmt, ...) {
    char buf[200];
    va_list va;
    va_start(va, fmt);
    lv_vsnprintf(buf, sizeof(buf), fmt, va);
    va_end(va);
    dbg_puts(buf);
}

char dbg_getc(void) {
    if (!com_ok || !(COM1(5) & COM_LSR_RX)) return 0;
    return (char)COM1(0);
}
