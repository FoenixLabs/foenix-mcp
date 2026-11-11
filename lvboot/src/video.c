/*
 * video.c - Gen1 LVGL boot loader: the channel B GUI mode as LVGL's display (port of the A2560Me loader, 2026-11-06).
 *
 * 640 x 480, 16 bpp RGB565, pixel doubling on (each pixel shown 2 x 2 on the 1280 x 960 output). The frame buffer is
 * in SDRAM at GUI_FB_ADDR (big-endian 16-bit pixels, the first pixel of a long word in bits 31:16, as guitest.s).
 * LVGL draws into a buffer in SRAM; flush_cb copies each finished area to the frame buffer with 32-bit writes.
 */
#include "lvgl.h"
#include "hw.h"
#include "board.h"
#include "video.h"

#define FB_ADDR     GUI_FB_ADDR
#define BUF_LINES   480

static lv_disp_draw_buf_t draw_buf;
static lv_color_t strip[VIDEO_W * BUF_LINES];
static lv_disp_drv_t disp_drv;

#ifdef POPUPBENCH
uint32_t video_flush_ticks;                     /* bus clock ticks spent in flush_cb (pop-up timing test) */
#endif

static void flush_cb(lv_disp_drv_t * drv, const lv_area_t * a, lv_color_t * px) {
#ifdef POPUPBENCH
    uint32_t t0 = TIMER_VALUE0;
#endif
    int32_t w = a->x2 - a->x1 + 1;
    int32_t y;
    const uint16_t * s = (const uint16_t *)px;
    for (y = a->y1; y <= a->y2; y++) {
        volatile uint16_t * d = (volatile uint16_t *)FB_ADDR + (uint32_t)y * VIDEO_W + a->x1;
        volatile uint32_t * d32;
        int32_t n = w;
        if (((uint32_t)d & 2) && n) { *d++ = *s++; n--; }      /* bring the destination to a longword */
        d32 = (volatile uint32_t *)d;
        while (n >= 2) { *d32++ = ((uint32_t)s[0] << 16) | s[1]; s += 2; n -= 2; }
        if (n) { *(volatile uint16_t *)d32 = *s++; }
    }
#ifdef POPUPBENCH
    video_flush_ticks += TIMER_VALUE0 - t0;
#endif
    lv_disp_flush_ready(drv);
}

void video_init(uint16_t bg565) {
    volatile uint32_t * fb = (volatile uint32_t *)FB_ADDR;
    uint32_t fill = ((uint32_t)bg565 << 16) | bg565;
    uint32_t i;

    GUI_CTRL = 0;                               /* off while the buffer is cleared */
    for (i = 0; i < VIDEO_W * VIDEO_H / 2; i++) fb[i] = fill;

    GUI_BASE = FB_ADDR;
    GUI_STRIDE = 0;                             /* packed: 1280 bytes per line */
    GUI_CTRL = 0x01 | (1 << 1) | (1 << 3);      /* fetch on, 16 bpp, pixel doubling: 640 x 480 on 1280 x 960 */
    VKY_BORDER_CTRL = 0;                        /* no border over the picture */
    VKY_MSTR = 0x00000004;                      /* graphic layer only (no text, no TOSGraph), 1280 x 960 */
    GUI_STATUS = 0;                             /* clear the late-line count */

    lv_disp_draw_buf_init(&draw_buf, strip, NULL, VIDEO_W * BUF_LINES);
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res = VIDEO_W;
    disp_drv.ver_res = VIDEO_H;
    disp_drv.flush_cb = flush_cb;
    disp_drv.draw_buf = &draw_buf;
    lv_disp_drv_register(&disp_drv);
}

/* before the reboot: picture off, text back on, so the next program starts on a clean screen */
void video_off(void) {
    GUI_CTRL = 0;
    VKY_MSTR = 0x00000001;
}
