; crt0.s - Gen1 LVGL boot loader: start-up code (port of the A2560Me loader, 2026-11-06)
;
; The image is linked at $0000_0000 and runs from SRAM. At reset the K25 FPGA copies the first 64 KB of flash block 0
; into SRAM $0000_0000-$0000_FFFF (vectors + this code) and starts the CPU from it. This code then copies the rest of
; its own block (BOOT_BASE + $1_0000 .. image end) to SRAM $0001_0000, so the same image runs from any block.
; The RAM test build (-DRAMIMAGE, loaded whole by the debug port) skips that copy and the trampoline.
;
; Multi-boot trampoline (flash build, block 0): the FPGA copier always copies block 0. If BOOT_SEL ($FEC0_0030) holds a
; choice, the chosen block is started instead: its first 64 KB go to SRAM 0 (as the copier would have done), TAKE ($B1)
; tells the FPGA, and its reset vector runs. That copy overwrites this code in SRAM, so the trampoline runs from the
; flash copy of block 0 ($FFC0_0000 + its offset; position independent). A blank block, or one that does not exist in
; its layout: FALL-BACK ($B2, BOOT_INFO $C0) and the menu starts as usual (it shows "could not be booted").
; Older FPGAs read 0 at $FEC0_0030: no choice, nothing happens.
; No interrupts are used: everything is polled, with SR = $2700.

    ifd MACHINE_K60
            machine 68060                   ; K60: MC68LC060 (same start-up as the 68040, plus the PCR below)
    else
            machine 68040
    endif

            xref    _main
            xref    ___STACK
            xref    ___image_end
            xref    ___bss_start
            xref    ___bss_end
            xdef    _exc_info
            xdef    vectors                 ; the link root (vlink -e vectors -gc-all keeps what it reaches)

BOOT_SEL    equ     $FEC00030
BOOT_BASE   equ     $FEC00038
FLASH_BASE  equ     $FFC00000
BORDER_CTRL equ     $FEC80004
BORDER_COL  equ     $FEC80008

            section "VECTORS",code
vectors:    dc.l    ___STACK                ; 0: initial stack pointer
            dc.l    start                   ; 1: initial PC
            rept    254
            dc.l    exception               ; 2-255: any exception or interrupt
            endr

            section "CODE",code
start:      move.w  #$2700,sr
            moveq   #0,d0
            movec   d0,cacr                 ; caches off while the code is copied
            movec   d0,tc                   ; no MMU
            movec   d0,vbr                  ; vectors at $0000_0000
            cinva   bc

    ifnd RAMIMAGE
            move.l  BOOT_SEL,d0             ; a multi-boot choice?
            btst    #8,d0
            beq.s   .nochoice
            lea     trampoline,a0           ; run the trampoline from the flash copy of block 0
            adda.l  #FLASH_BASE,a0
            jmp     (a0)
.nochoice:
    endif
            lea     ___STACK,sp

    ifnd RAMIMAGE
            movea.l BOOT_BASE,a0            ; copy the rest of the image from our flash block
            adda.l  #$10000,a0
            lea     $10000,a1
            lea     ___image_end,a2
.copy:      cmpa.l  a2,a1
            bhs.s   .copied
            move.l  (a0)+,(a1)+
            bra.s   .copy
.copied:
    endif

            lea     ___bss_start,a0         ; clear the BSS (LVGL heap, draw buffer, variables)
            lea     ___bss_end,a1
            moveq   #0,d0
.clr:       cmpa.l  a1,a0
            bhs.s   .cleared
            move.l  d0,(a0)+
            bra.s   .clr
.cleared:
            ; Caches (68040, MMU off: the transparent translation registers give the cache mode):
            ;   DTT0 / ITT0 $0000_0000-$00FF_FFFF  SRAM (code, LVGL heap, draw buffer) and VRAM: copyback / cached
            ;   DTT1 / ITT1 $FC00_0000-$FFFF_FFFF  registers (GAVIN, VICKY, VGE) and flash: not cached, serialized
            ;   anything else (the frame buffer in SDRAM at $0400_0000) takes the 68040 default with the MMU off:
            ;   cacheable write-through, so every pixel the flush writes reaches the SDRAM at once for the GUI fetch.
            ; SRAM that the next program or the host reads (hand-over record, exc_info) is pushed with CPUSHA.
            move.l  #$0000C020,d0
            movec   d0,dtt0
            move.l  #$FC03C040,d0
            movec   d0,dtt1
            move.l  #$0000C000,d0
            movec   d0,itt0
            move.l  #$FC03C040,d0
            movec   d0,itt1
            cinva   bc
            move.l  #$80008000,d0           ; DE + IE: data and instruction cache on (same bits on the 68060)
            movec   d0,cacr
    ifd MACHINE_K60
            moveq   #1,d0                   ; 68060 PCR: ESS = superscalar dispatch on (the FPU bits do not exist on the LC)
            movec   d0,pcr
    endif
            jsr     _main
.halt:      bra.s   .halt

    ifnd RAMIMAGE
; ---- runs from flash (FLASH_BASE + offset): only absolute addresses, no SRAM data ----
trampoline: move.l  BOOT_SEL,d0             ; [8] pending, [5:4] layout, [2:0] block
            move.l  d0,d1
            lsr.l   #4,d1
            and.l   #3,d1                   ; d1 = layout (0 = 4 MB ... 3 = 512 KB)
            move.l  d0,d2
            and.l   #7,d2                   ; d2 = block
            moveq   #1,d3
            lsl.l   d1,d3                   ; blocks in this layout
            cmp.l   d3,d2
            bcc.s   .bad                    ; the block does not exist
            moveq   #22,d3
            sub.l   d1,d3
            lsl.l   d3,d2                   ; block x block size
            add.l   #FLASH_BASE,d2
            movea.l d2,a0                   ; a0 = the block in flash
            cmpi.l  #$FFFFFFFF,(a0)
            beq.s   .bad                    ; blank
            suba.l  a1,a1
            move.w  #16383,d3
.tcopy:     move.l  (a0)+,(a1)+             ; 64 KB, as the FPGA copier does for block 0
            dbra    d3,.tcopy
            move.l  #$B1000000,BOOT_SEL     ; TAKE: BOOT_INFO = $80 | layout | block
            move.l  #200000,d3              ; wait until PENDING is 0 (a few us through the oscillator side): an image
.twait:     move.l  BOOT_SEL,d0             ; with its own trampoline must not see the choice again
            btst    #8,d0
            beq.s   .tgo
            subq.l  #1,d3
            bne.s   .twait
.tgo:       movea.l $00000000,sp
            movea.l $00000004,a0
            jmp     (a0)
.bad:       move.l  #$B2000000,BOOT_SEL     ; FALL-BACK: BOOT_INFO = $C0, the menu starts
            move.l  #200000,d3
.bwait:     move.l  BOOT_SEL,d0
            btst    #8,d0
            beq.s   .bgo
            subq.l  #1,d3
            bne.s   .bwait
.bgo:       jmp     start                   ; block 0's own copy in SRAM is untouched: boot the menu
    endif

; Any exception: record it for the debug port (exc_info = $E0E0E0E0, format/vector word, PC, SP), red border, stop.
exception:  move.w  #$2700,sr
            move.l  a0,-(sp)
            lea     _exc_info,a0
            move.l  #$E0E0E0E0,(a0)
            move.w  10(sp),6(a0)            ; format / vector offset (the frame starts 4 bytes up: a0 was pushed)
            move.l  6(sp),8(a0)             ; PC
            move.l  sp,12(a0)
            move.l  #$00FF0000,BORDER_COL
            move.l  #$00100801,BORDER_CTRL  ; border on, 8 pixels / 16 lines
            nop
            cpusha  dc                      ; exc_info out of the data cache, so the debug port can read it
            nop
.stop:      bra.s   .stop

            section "BSS",bss
_exc_info:  ds.l    4
