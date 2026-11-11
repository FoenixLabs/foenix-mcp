/*
 * board.h - A2560Me LVGL boot loader: hardware helpers (timer, delays, debug console).
 */
#ifndef BOARD_H
#define BOARD_H
#include <stdint.h>

uint32_t board_ms(void);                /* milliseconds since board_init() (LVGL's tick source) */
uint32_t board_ticks_per_ms(void);    /* measured bus clock / 1000 */
void     board_init(void);              /* timer calibration, console */
void     delay_us(uint32_t us);
void     delay_ms(uint32_t ms);
void     dbg_puts(const char * s);      /* COM4 remote console (COM19 on the PC) */
void     dbg_printf(const char * fmt, ...);
char     dbg_getc(void);                /* 0 if no character */

#endif
