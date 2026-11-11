/*
 * main.c - Gen1 LVGL boot loader (port of the A2560Me loader, 2026-11-06).
 *
 * Lives in flash block 0 and starts at every cold boot (crt0.s first hands over to a block chosen by BOOT_SEL). It
 * shows the Foenix Labs logo and the entries of BOOT.CFG on the SD card, counts down, and reboots the board into the
 * chosen flash block through BOOT_SEL ($FEC0_0030, K25 FPGA build 1110+). See the Gen1 work area notes\multiboot.md
 * and the K25 Hardware Reference, Multi-boot. Built with VBCC and LVGL 8.3 (MIT).
 */
#include "lvgl.h"
#include "hw.h"
#include "board.h"
#include "video.h"
#include "input.h"
#include "menu.h"

int main(void) {
    board_init();
    dbg_printf("\nGen1 LVGL boot loader %s (%s): FPGA build %04lX, bus clock %lu kHz, BOOT_INFO $%02lX, BOOT_BASE $%08lX\n",
               LOADER_VERSION, MACHINE_NAME, (unsigned long)FPGA_BUILD, (unsigned long)board_ticks_per_ms(),
               (unsigned long)(BOOT_INFO & 0xFF), (unsigned long)BOOT_BASE);
    lv_init();
    video_init(menu_bg565());
    menu_create();
    lv_refr_now(NULL);                  /* the logo is up before the slower set-up below */
    dbg_printf("screen up at %lu ms\n", (unsigned long)board_ms());
    input_init();
    input_lv_init();
    menu_run();
    return 0;
}
