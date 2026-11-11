/* lvboot.ld - vlink script for the Gen1 LVGL boot loader: one flat image linked at $0000_0000 (SRAM, 4 MB on Gen1).
 * The binary holds VECTORS + CODE + DATA; the first 64 KB are copied by the FPGA at reset, the rest by crt0.s.
 * The C files are compiled with -sec-per-obj (one section per function / variable; vasm names them <n>_code.<name>, <n>_data..., <n>_bss...) and linked with
 * -gc-all, which drops what nothing uses: LVGL's headers put many static inline functions in every file. */

MEMORY
{
    sram : org = 0x00000000, len = 0x003F0000
}

SECTIONS
{
    vectors : { *(VECTORS) } > sram
    text    : { *(CODE) *(*_code.*) } > sram
    data    : { *(DATA) *(*_data.*) . = ALIGN(4); } > sram
    ___image_end = .;
    bss (NOLOAD) : { *(BSS) *(*_bss.*) . = ALIGN(4); } > sram
    ___bss_start = ADDR(bss);
    ___bss_end = ADDR(bss) + SIZEOF(bss);
    ___STACK = 0x003F0000;          /* top of the 4 MB SRAM, below the hand-over record at $003F_BF00 */
}
