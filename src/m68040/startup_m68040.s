            xref ___main
            xref _cli_rerepl
            xref _panic
            xref _panic_number
            xref _proc_return_point

            xdef _syscall
            xdef ___exit
            xdef _int_enable_all
            xdef _int_disable_all
            xdef _call_user
            xdef _restart_cli
            xdef _proc_return
            xdef _cache_hw_enable
            xdef _cache_flush
            xdef _cache_data_off
            xdef _cache_get_cacr

;
; Interrupt registers for A2560X/K and GenX
;
PENDING_GRP0 = $FEC00100
PENDING_GRP1 = $FEC00102
PENDING_GRP2 = $FEC00104

;
; 2.0.30: values for the MCP info record in vectors 16-19 (see below). The Makefile passes the version
; from version.h as MCP_VER_MAJOR / MCP_VER_MINOR / MCP_VER_BUILD; without them the record says 0.0.0.
;
        ifnd MCP_VER_MAJOR
MCP_VER_MAJOR = 0
        endif
        ifnd MCP_VER_MINOR
MCP_VER_MINOR = 0
        endif
        ifnd MCP_VER_BUILD
MCP_VER_BUILD = 0
        endif
        ifd CACHE_ON
MCP_INFO_FLAGS = 1                  ; bit 0: this MCP switches the CPU caches on
        else
MCP_INFO_FLAGS = 0
        endif
MCP_INFO_MAGIC = $4D435049          ; "MCPI"
MCP_INFO_VER = (MCP_VER_MAJOR<<24)|(MCP_VER_MINOR<<16)|MCP_VER_BUILD

            section "VECTORS",code

            dc.l ___STACK           ; 00 - Initial stack pointer
            dc.l coldboot           ; 01 - Initial PC
            dc.l _handle_bus_err    ; 02 - Bus error
            dc.l _handle_addr_err   ; 03 - Address error
            dc.l _handle_inst_err   ; 04 - Illegal instruction
            dc.l _handle_div0_err   ; 05 - Zero divide
            dc.l _handle_chk_err    ; 06 - CHK instruction
            dc.l _handle_trapv_err  ; 07 - TRAPV instruction
            dc.l _handle_priv_err   ; 08 - Privilege error
            dc.l not_impl           ; 09 - Trace
            dc.l not_impl           ; 10 - Line 1010
            dc.l not_impl           ; 11 - Line 1111
            dc.l not_impl           ; 12 - Reserved
            dc.l not_impl           ; 13 - Reserved
            dc.l not_impl           ; 14 - Format error
            dc.l not_impl           ; 15 - Uninitialized Interrupt Error
            ;
            ; 16-19: 2.0.30 MCP info record, read from flash by the boot loader (lvboot) to show
            ; "MCP 2.0.30 caches ON" in its menu. The 68040 and 68060 never use vectors 16-23
            ; (reserved by Motorola), so these four longs are free for data. Offset $40 in the image.
            ;
            dc.l MCP_INFO_MAGIC     ; 16 - "MCPI"
            dc.l MCP_INFO_VER       ; 17 - version: major (bits 31-24), minor (23-16), build (15-0)
            dc.l MCP_INFO_FLAGS     ; 18 - flags: bit 0 = CPU caches on
            dc.l MCP_INFO_MAGIC^MCP_INFO_VER^MCP_INFO_FLAGS ; 19 - check: the three longs above XOR-ed
            ;
            ; 20-23: 2.1.0 the name lvboot 1.4 shows (16 bytes, such as "MCP 2.1.0-cache"). The
            ; Makefile writes it into the image after linking with tools/block_name.py, which also
            ; sets bit 1 of the flags (18) and the check (19). Boot loaders before 1.4 ignore it.
            ;
            dc.l 0                 ; 20 - name for the boot loader (tools/block_name.py)
            dc.l 0                 ; 21 - name for the boot loader (tools/block_name.py)
            dc.l 0                 ; 22 - name for the boot loader (tools/block_name.py)
            dc.l 0                 ; 23 - name for the boot loader (tools/block_name.py)
            dc.l _handle_spurious   ; 24 - Spurious Interrupt
            dc.l not_impl           ; 25 - Level 1 Interrupt Autovector
            dc.l not_impl           ; 26 - Level 2 Interrupt Autovector
            dc.l not_impl           ; 27 - Level 3 Interrupt Autovector
            dc.l not_impl           ; 28 - Level 4 Interrupt Autovector
            dc.l int_vicky_a        ; 29 - Level 5 Interrupt Autovector
            dc.l int_vicky_b        ; 30 - Level 6 Interrupt Autovector
            dc.l not_impl           ; 31 - Level 7 Interrupt Autovector
            dc.l not_impl           ; 32 - TRAP #0
            dc.l not_impl           ; 33 - TRAP #1
            dc.l not_impl           ; 34 - TRAP #2
            dc.l not_impl           ; 35 - TRAP #3
            dc.l not_impl           ; 36 - TRAP #4
            dc.l not_impl           ; 37 - TRAP #5
            dc.l not_impl           ; 38 - TRAP #6
            dc.l not_impl           ; 39 - TRAP #7
            dc.l not_impl           ; 40 - TRAP #8
            dc.l not_impl           ; 41 - TRAP #9
            dc.l not_impl           ; 42 - TRAP #10
            dc.l not_impl           ; 43 - TRAP #11
            dc.l not_impl           ; 44 - TRAP #12
            dc.l not_impl           ; 45 - TRAP #13
            dc.l not_impl           ; 46 - TRAP #14
            dc.l h_trap_15          ; 47 - TRAP #15
            dc.l not_impl           ; 48 - Reserved
            dc.l not_impl           ; 49 - Reserved
            dc.l not_impl           ; 50 - Reserved
            dc.l not_impl           ; 51 - Reserved
            dc.l not_impl           ; 52 - Reserved
            dc.l not_impl           ; 53 - Reserved
            dc.l not_impl           ; 54 - Reserved
            dc.l not_impl           ; 55 - Reserved
            dc.l not_impl           ; 56 - Reserved
            dc.l not_impl           ; 57 - Reserved
            dc.l not_impl           ; 58 - Reserved
            dc.l not_impl           ; 59 - Reserved
            dc.l not_impl           ; 60 - Reserved
            dc.l not_impl           ; 61 - Reserved
            dc.l not_impl           ; 62 - Reserved
            dc.l not_impl           ; 63 - Reserved
            dc.l interrupt_x10      ; 64 - Interrupt 0x10 - SuperIO - PS/2 Keyboard
            dc.l interrupt_x11      ; 65 - Interrupt 0x11 - A2560K Built-in Keyboard (Mo)
            dc.l interrupt_x12      ; 66 - Interrupt 0x12 - SuperIO - PS/2 Mouse
            dc.l interrupt_x13      ; 67 - Interrupt 0x13 - SuperIO - COM1
            dc.l interrupt_x14      ; 68 - Interrupt 0x14 - SuperIO - COM2
            dc.l interrupt_x15      ; 69 - Interrupt 0x15 - SuperIO - LPT1
            dc.l interrupt_x16      ; 70 - Interrupt 0x16 - SuperIO - Floppy Disk Controller
            dc.l interrupt_x17      ; 71 - Interrupt 0x17 - SuperIO - MIDI
            dc.l interrupt_x18      ; 72 - Interrupt 0x18 - Timer 0
            dc.l interrupt_x19      ; 73 - Interrupt 0x19 - Timer 1
            dc.l interrupt_x1A      ; 74 - Interrupt 0x1A - Timer 2
            dc.l interrupt_x1B      ; 76 - Interrupt 0x1B - Timer 3
            dc.l interrupt_x1C      ; 75 - Interrupt 0x1C - Timer 4
            dc.l interrupt_x1D      ; 77 - Interrupt 0x1D - Reserved
            dc.l interrupt_x1E      ; 78 - Interrupt 0x1E - Reserved
            dc.l interrupt_x1F      ; 79 - Interrupt 0x1F - Real Time Clock

            dc.l interrupt_x20      ; 80 - Interrupt 0x20 - IDE HDD Generated Interrupt
            dc.l interrupt_x21      ; 81 - Interrupt 0x21 - SDCard Insert
            dc.l interrupt_x22      ; 82 - Interrupt 0x22 - SDCard Controller
            dc.l interrupt_x23      ; 83 - Interrupt 0x23 - Internal OPM
            dc.l interrupt_x24      ; 84 - Interrupt 0x24 - External OPN2
            dc.l interrupt_x25      ; 85 - Interrupt 0x25 - External OPL3
            dc.l interrupt_x26      ; 86 - Interrupt 0x26 - Reserved
            dc.l interrupt_x27      ; 87 - Interrupt 0x27 - Reserved
            dc.l interrupt_x28      ; 88 - Interrupt 0x28 - Beatrix Interrupt 0
            dc.l interrupt_x29      ; 89 - Interrupt 0x29 - Beatrix Interrupt 1
            dc.l interrupt_x2A      ; 90 - Interrupt 0x2A - Beatrix Interrupt 2
            dc.l interrupt_x2B      ; 91 - Interrupt 0x2B - Beatrix Interrupt 3
            dc.l interrupt_x2C      ; 92 - Interrupt 0x2C - Reserved
            dc.l interrupt_x2D      ; 93 - Interrupt 0x2D - DAC1 Playback Done Interrupt (48K)
            dc.l interrupt_x2E      ; 94 - Interrupt 0x2E - Reserved
            dc.l interrupt_x2F      ; 95 - Interrupt 0x2F - DAC0 Playback Done Interrupt (44.1K)

            code

coldboot:   move.w #$2700,SR        ; Supervisor mode, Interrupt mode (68040), disable all interrupts
        ifd TRAMPOLINE
;
; 2.0.21 multi-boot trampoline (FPGA build 1110, Gen1 work area notes\multiboot.md). Only in the block-0 image.
; The FPGA always copies block 0's first 64 KB (vectors + initialised data) to SRAM 0 and starts it. If BOOT_SEL ($FEC0_0030) holds a
; choice (bit 8), start that block instead: copy ITS first 64 KB to SRAM 0 (2.0.23: like the FPGA copier - vectors AND
; the initialised data at $1000-$FFFF; 2.0.21/22 copied only 1 KB), tell the FPGA (TAKE, key $B1) and jump to
; its reset vector, with its stack. A blank block (first long $FFFF_FFFF) or one that does not exist in the layout ->
; FALL-BACK (key $B2: BOOT_INFO $C0) and boot block 0 as usual. The board has just had a full cold reset, so this is the same as if
; the FPGA had copied block N itself. Older FPGAs read 0 at $FEC0_0030: nothing happens.
;
            move.l  $FEC00030,d0        ; BOOT_SEL: [8] pending, [5:4] layout, [2:0] block
            btst    #8,d0
            beq     tr_none
            move.l  d0,d1
            lsr.l   #4,d1
            and.l   #3,d1               ; d1 = layout (0 = 4 MB ... 3 = 512 KB)
            move.l  d0,d2
            and.l   #7,d2               ; d2 = block
            moveq   #1,d3
            lsl.l   d1,d3               ; blocks in this layout
            cmp.l   d3,d2
            bcc     tr_bad              ; block does not exist
            moveq   #22,d3
            sub.l   d1,d3
            lsl.l   d3,d2               ; block x block size
            add.l   #$FFC00000,d2
            move.l  d2,a0               ; a0 = the block in flash
            cmp.l   #$FFFFFFFF,(a0)
            beq     tr_bad              ; blank
            move.l  #0,a1
            move.w  #16383,d3
tr_copy:    move.l  (a0)+,(a1)+         ; 16384 longs = 64 KB, as the FPGA boot copier
            dbra    d3,tr_copy
            move.l  #$B1000000,$FEC00030 ; TAKE: BOOT_INFO = $80 | layout | block
            move.l  $00000000,a7
            move.l  $00000004,a0
            jmp     (a0)
tr_bad:     move.l  #$B2000000,$FEC00030 ; FALL-BACK: BOOT_INFO = $C0 | layout | block, block 0 boots
tr_none:
        endif

            
            moveq #0,d0   ; Disable 040's MMU
            movec d0,TC

  move.l #$ff00ff00,$fec80008 ; border color for debug
            lea ___STACK,sp
            bsr _int_disable_all

            lea	___BSSSTART,a0
            move.l #___BSSSIZE,d0
            addq.l #3,d0            ; 2.0.34: whole longs. The loop below clears 4 bytes at a time and stops
            and.l #$FFFFFFFC,d0     ; only at exactly 0: a size that is not a multiple of 4 ran on through
            beq	callmain            ; all memory and the MCP never started (green border)

clrloop:    move.l #0,(a0)+
            subq.l #4,d0
            bne	clrloop

  move.l #$ffffff00,$fec80008 ; change border color
callmain:   jsr ___main             ; call __main to transfer to the C code

;	endless loop; can be changed accordingly
___exit:
            bra	___exit

;
; Autovector handler: Used by VICKY III Channel B interrupts
;
int_vicky_b:
            movem.l d0-d7/a0-a6,-(a7)
            jsr _int_vicky_channel_b        ; Call the dispatcher for Channel B interrupts
            movem.l (a7)+,d0-d7/a0-a6
            rte

;
; Autovector handler: Used by VICKY III Channel A interrupts
;
int_vicky_a:
            movem.l d0-d7/a0-a6,-(a7)
            jsr _int_vicky_channel_a        ; Call the dispatcher for Channel A interrupts
            movem.l (a7)+,d0-d7/a0-a6
            rte

;
; Given the interrupt's offset in the interrupt handler table, fetch the pointer
; to the interrupt handler and call it... if there is one
;
; Assumes registers D0-D7, A0-A6 have been saved to the stack with MOVEM
;
int_dispatch:
            lea _g_int_handler,a0           ; Look in the interrupt handler table
            move.l (0,a0,d0),d0             ; Get the address of the handler
            beq intdis_end                  ; If there isn't one, just return
            movea.l d0,a0
            jsr (a0)                        ; If there is, call it.

intdis_end: movem.l (a7)+,d0-d7/a0-a6       ; Restore affected registers
            rte

            ;
            ; Macro to implement an interrupt handler for the user interrupt vectors
            ;
            ; Standard handlers will:
            ; 1. Save all registers
            ; 2. Clear the pending flag for this interrupt
            ; 3. Look for a handler registered through the system calls
            ; 4. Call the handler (if found)
            ; 5. Restore all registers
            ; 6. Return to the interrupted code
            ;
            macro inthandler                ; Individual interrupt handler. Parameters: interrupt number, interrupt mask, pending register
            movem.l d0-d7/a0-a6,-(a7)       ; Save affected registers
            move.w #\2,(\3)                 ; Clear the flag for the interrupt
            move.w #(\1<<2),d0              ; Get the offset to interrupt 0x11
            bra int_dispatch
            endm

;
; Group 1 Interrupt Handlers
;

interrupt_x10:  inthandler $10, $0001, PENDING_GRP1     ; Interrupt Vector 0x10 -- SuperIO Keyboard
interrupt_x11:  inthandler $11, $0002, PENDING_GRP1     ; Interrupt Vector 0x11 -- A2560K "Mo" keyboard
interrupt_x12:  inthandler $12, $0004, PENDING_GRP1     ; Interrupt Vector 0x12 -- SuperIO Mouse
interrupt_x13:  inthandler $13, $0008, PENDING_GRP1     ; Interrupt Vector 0x13 -- COM1
interrupt_x14:  inthandler $14, $0010, PENDING_GRP1     ; Interrupt Vector 0x14 -- COM2
interrupt_x15:  inthandler $15, $0020, PENDING_GRP1     ; Interrupt Vector 0x15 -- LPT1
interrupt_x16:  inthandler $16, $0040, PENDING_GRP1     ; Interrupt Vector 0x16 -- Floppy drive controller
interrupt_x17:  inthandler $17, $0080, PENDING_GRP1     ; Interrupt Vector 0x17 -- MIDI
interrupt_x18:  inthandler $18, $0100, PENDING_GRP1     ; Interrupt Vector 0x18 -- Timer 0
interrupt_x19:  inthandler $19, $0200, PENDING_GRP1     ; Interrupt Vector 0x19 -- Timer 1
interrupt_x1A:  inthandler $1A, $0400, PENDING_GRP1     ; Interrupt Vector 0x1A -- Timer 2
interrupt_x1B:  inthandler $1B, $0800, PENDING_GRP1     ; Interrupt Vector 0x1B -- Timer 3
interrupt_x1C:  inthandler $1C, $1000, PENDING_GRP1     ; Interrupt Vector 0x1C -- Timer 4
interrupt_x1D:  inthandler $1D, $2000, PENDING_GRP1     ; Interrupt Vector 0x1D -- Reserved
interrupt_x1E:  inthandler $1E, $4000, PENDING_GRP1     ; Interrupt Vector 0x1E -- Reserved
interrupt_x1F:  inthandler $1F, $8000, PENDING_GRP1     ; Interrupt Vector 0x1F -- Real Time Clock

;
; Group 2 Interrupt Handlers
;

interrupt_x20:  inthandler $20, $0001, PENDING_GRP2     ; Interrupt Vector 0x20 -- IDE HDD Generated Interrupt
interrupt_x21:  inthandler $21, $0002, PENDING_GRP2     ; Interrupt Vector 0x21 -- SDCard Insert
interrupt_x22:  inthandler $22, $0004, PENDING_GRP2     ; Interrupt Vector 0x22 -- SDCard Controller
interrupt_x23:  inthandler $23, $0008, PENDING_GRP2     ; Interrupt Vector 0x23 -- Internal OPM
interrupt_x24:  inthandler $24, $0010, PENDING_GRP2     ; Interrupt Vector 0x24 -- External OPN2
interrupt_x25:  inthandler $25, $0020, PENDING_GRP2     ; Interrupt Vector 0x25 -- External OPL3
interrupt_x26:  inthandler $26, $0040, PENDING_GRP2     ; Interrupt Vector 0x26 -- Reserved
interrupt_x27:  inthandler $27, $0080, PENDING_GRP2     ; Interrupt Vector 0x27 -- Reserved
interrupt_x28:  inthandler $28, $0100, PENDING_GRP2     ; Interrupt Vector 0x28 -- Beatrix Interrupt 0
interrupt_x29:  inthandler $29, $0200, PENDING_GRP2     ; Interrupt Vector 0x29 -- Beatrix Interrupt 1
interrupt_x2A:  inthandler $2A, $0400, PENDING_GRP2     ; Interrupt Vector 0x2A -- Beatrix Interrupt 2
interrupt_x2B:  inthandler $2B, $0800, PENDING_GRP2     ; Interrupt Vector 0x2B -- Beatrix Interrupt 3
interrupt_x2C:  inthandler $2C, $1000, PENDING_GRP2     ; Interrupt Vector 0x2C -- Reserved
interrupt_x2D:  inthandler $2D, $2000, PENDING_GRP2     ; Interrupt Vector 0x2D -- DAC1 Playback Done Interrupt (48K)
interrupt_x2E:  inthandler $2E, $4000, PENDING_GRP2     ; Interrupt Vector 0x2E -- Reserved
interrupt_x2F:  inthandler $2F, $8000, PENDING_GRP2     ; Interrupt Vector 0x2F -- DAC0 Playback Done Interrupt (44.1K)

;
; Unimplemented Exception Handler -- just return
;
not_impl:   rte

;
; Enable all interrupts
;
_int_enable_all:    move.w SR,d0        ; Save the old level for return
                    andi.w #$0700,d0
                    lsr.w #8,d0
                    ext.l d0

                    andi.w #$F8FF,SR    ; Clear the level
                    rts

;
; Disable all interrupts
;
_int_disable_all:   move.w SR,d0        ; Save the old level for return
                    andi.w #$0700,d0
                    lsr.w #8,d0
                    ext.l d0

                    ori.w #$0700,SR     ; Set the level to 7
                    rts

;
; Restore interrupt priority
;
_int_restore:       move.w (4,sp),d0    ; Get the priority into d0
                    andi.w #3,d0
                    lsl.w #8,d0

                    move.w sr,d1        ; Get the current SR into d1
                    andi.w #$F8FF,d1    ; Clear the old priority
                    or.w d0,d1          ; And set it to the current
                    move.w d1,sr        ; And set the level

                    rts

;
; Handlers for the various exceptions...
;
; These all go to the kernel panic screen after placing the
; triggering PC address in "panic_pc" and the number of the exception
; in "panic_number". Bus and address errors will also put the
; address involved in "panic_address".
;

_handle_bus_err:
                    ; 2.1.0: the fault address is at +$14 of the 68040 access error frame (+8 is
                    ; the effective address of the instruction); see the Calypsi version
                    lea _panic_number,a0
                    lea _panic_pc,a1
                    lea _panic_address,a2
                    move.w #2,(a0)
                    move.l (2,a7),(a1)
                    move.l ($14,a7),(a2)
                    bra call_panic

_handle_addr_err:
                    lea _panic_number,a0
                    lea _panic_pc,a1
                    lea _panic_address,a2
                    move.w #3,(a0)
                    move.l (2,a7),(a1)
                    move.l (8,a7),(a2)
                    bra call_panic

_handle_inst_err:
                    lea _panic_number,a0
                    lea _panic_pc,a1
                    move.w #4,(a0)
                    move.l (2,a7),(a1)
                    bra call_panic

_handle_div0_err:
                    lea _panic_number,a0
                    lea _panic_pc,a1
                    move.w #5,(a0)
                    move.l (2,a7),(a1)
                    bra call_panic

_handle_chk_err:
                    lea _panic_number,a0
                    lea _panic_pc,a1
                    move.w #6,(a0)
                    move.l (2,a7),(a1)
                    bra call_panic

_handle_trapv_err:
                    lea _panic_number,a0
                    lea _panic_pc,a1
                    move.w #7,(a0)
                    move.l (2,a7),(a1)
                    bra call_panic

_handle_priv_err:
                    lea _panic_number,a0
                    lea _panic_pc,a1
                    move.w #8,(a0)
                    move.l (2,a7),(a1)
                    bra call_panic

_handle_spurious:
                    lea _panic_number,a0
                    lea _panic_pc,a1
                    move.w #24,(a0)
                    move.l (2,a7),(a1)
                    bra call_panic

; 2.1.0: after panic (error in the top bar, a key) back to the command line; see the Calypsi version
call_panic:         jsr _panic
                    move.w #$2000,sr
                    bra _restart_cli

;
; Function to make a system call based on the number of the system function:
; int32_t syscall(int32_t number, int32_t p0, int32_t p1, int32_t p2, int32_t p3, int32_t p4, int32_t p5)
;
_syscall:
            movem.l d1-d7,-(sp)         ; Save caller's registers
            move.l (56,sp),d6           ; Parameter 5 to D6
            move.l (52,sp),d5           ; Parameter 4 to D5
            move.l (48,sp),d4           ; Parameter 3 to D4
            move.l (44,sp),d3           ; Parameter 2 to D3
            move.l (40,sp),d2           ; Parameter 1 to D2
            move.l (36,sp),d1           ; Parameter 0 to D1
            move.l (32,sp),d0           ; Function number to D0

            TRAP #15                    ; Call into the kernel

            movem.l (sp)+,d1-d7         ; Restore caller's registers
            rts

;
; TRAP#15 handler... transfer control to the C dispatcher
;
h_trap_15:
            ; 2.1.0: the function number is in d0 (d1 is the first argument); see the Calypsi version
            cmp.l #$43,d0               ; Is this a sys_proc_elevate call (KFN_ELEVATE)?
            beq h_trap_elev             ; Yes, just handle it here

            move.l d6,-(sp)             ; Push the parameters to the stack for the C call
            move.l d5,-(sp)
            move.l d4,-(sp)
            move.l d3,-(sp)
            move.l d2,-(sp)
            move.l d1,-(sp)
            move.l d0,-(sp)

            jsr _syscall_dispatch       ; Call the C routine to do the dispatch
                                        ; Note: the C routine depends upon the register push order

            add.l #28,sp                ; Remove parameters from the stack

            rte                         ; Return to the caller

; 2.1.0: sys_proc_elevate is refused (ERR_NOT_SUPPORTED, -37): the caller went on with the
; supervisor stack instead of its own and crashed; see the Calypsi version
h_trap_elev move.l #-37,d0              ; ERR_NOT_SUPPORTED
            rte

;
; Jump into a user mode code
;
; 2.0.33: before jumping, a "return point" is saved on the supervisor stack (52 bytes):
;   +0  the previous return point (0 = none): a program can start a program
;   +4  the user stack pointer of whoever called us
;   +8  d2-d7/a2-a6, the registers a C caller expects to be kept
; and _proc_return_point points at it. When the program ends (sys_exit -> proc_exit), _proc_return
; goes back to it, so call_user returns to its caller like a normal subroutine: the command line
; or mcp.init then carries on. Before, the end of a program always restarted the command line.
;
CU_SAVED    = 52                        ; bytes of the return point, see above

_call_user:
            movem.l d2-d7/a2-a6,-(a7)   ; The caller's registers
            move.l usp,a0
            move.l a0,-(a7)             ; The caller's user stack
            move.l _proc_return_point,-(a7) ; Link to the previous return point

            ; Set up the user stack. Normally at the top of the program stack area. When a program
            ; starts a program, that area holds the first program's stack, which must survive:
            ; then start just below where it is now.
            move.l #$00010000,a0        ; Get the pointer to the process's stack
            tst.l (a7)                  ; Started by a program (an earlier return point)?
            beq cu_stack
            move.l (4,a7),d0            ; Yes: below the caller's user stack,
            sub.l #16,d0                ; with a little room,
            and.l #$FFFFFFFC,d0         ; on a long word
            move.l d0,a0
cu_stack:
            move.l a7,_proc_return_point ; This is where the program comes back to

            move.l (CU_SAVED+12,a7),d1  ; Get the number of parameters passed
            move.l (CU_SAVED+16,a7),a1  ; Get the pointer to the parameters
            move.l a1,-(a0)             ; Push the parameters list
            move.w d1,-(a0)             ; Push the parameter count
            move.l a0,usp               ; Set the User Stack Pointer


            ; Set up the system stack
            move.l (CU_SAVED+4,a7),a0   ; Get the pointer to the code to start
            move.w #0,-(a7)             ; Push the fake vector offset
            move.l a0,-(a7)             ; Push it as the starting address
            move.w #$0000,-(a7)         ; Push the user's initial SR (to switch to user mode)
        ifd CACHE_ON
            ; 2.0.29: the program was just loaded with data writes. The instruction cache may still hold
            ; old code from the same addresses (an earlier program), so drop it before jumping in.
            nop
            cpusha  bc
            nop
            ; 2.0.30: and set the caches up again. A program can get into supervisor mode and change the
            ; cache registers (a test build of felinc_test did); without this the caches stayed the way
            ; that program left them, for every program after it.
            bsr     _cache_hw_enable
        endif
            rte                         ; Start the user process

_restart_cli:
            lea ___STACK,sp
            clr.l _proc_return_point    ; 2.0.33: the stack is gone, so are the return points
            jsr _cli_rerepl
            bra _restart_cli

;
; 2.0.33: end the running program and return to where call_user was called (see there).
; C: void proc_return(int result) -- does not return to its own caller; call_user returns.
; Only call it when _proc_return_point is not 0. Everything the program left on the supervisor
; stack (its system call's frame, for one) is dropped by going back to the saved stack pointer.
;
_proc_return:
            move.l (4,a7),d0            ; The program's result, also returned by call_user
            move.l _proc_return_point,a7 ; Back to the stack as call_user left it
            move.l (a7)+,_proc_return_point ; The previous return point is current again
            move.l (a7)+,a0
            move.l a0,usp               ; The caller's user stack
            movem.l (a7)+,d2-d7/a2-a6   ; The caller's registers
            rts                         ; Return from call_user

;
; 2.0.29: 68040 / 68060 cache control (see src/cache.h for the C side).
; Only a build with CACHE=1 (-DCACHE_ON) does anything here; in a normal build every routine just
; returns, so the MCP behaves exactly as without these routines. All of them use privileged
; instructions: call them from the kernel (supervisor mode) only.
;
; The cache mode of each address comes from the transparent translation registers (the MMU is off):
;   DTT0 $FC03C040 : $FC00_0000-$FFFF_FFFF data (VGE, register blocks, flash) = non-cacheable,
;                    serialized. Serialized, so a register read can never pass an earlier register write.
;   everything else: the 68040 default with the MMU off = cacheable WRITE-THROUGH. Every CPU write goes
;                    to memory at once, so video, the GDMA, the blitter, the DAC and the debug port always
;                    see what the CPU wrote. (Copyback would be faster but needs pushes everywhere.)
;   ITT0 / ITT1 off: instructions from flash and RAM are cached (default mode).
; This is the example set from the K25 hardware reference, without the optional copyback SDRAM.
;

;
; void cache_hw_enable(void) - switch the data and instruction caches on
;
_cache_hw_enable:
        ifd CACHE_ON
            moveq   #0,d0
            movec   d0,itt0             ; instructions: default (cacheable) everywhere
            movec   d0,itt1
            movec   d0,dtt1
            move.l  #$FC03C040,d0       ; data $FC00_0000-$FFFF_FFFF: non-cacheable, serialized
            movec   d0,dtt0
            nop
            cinva   bc                  ; throw away whatever the caches hold from before the reset
            move.l  #$80008000,d0       ; DE (bit 31) + IE (bit 15): same bits on the 68040 and the 68060
            movec   d0,cacr
            nop
        endif
            rts

;
; void cache_flush(void) - write any changed data to memory and empty both caches
; Use it before running code that was loaded or changed with data writes.
;
_cache_flush:
        ifd CACHE_ON
            nop
            cpusha  bc
            nop
        endif
            rts

;
; void cache_data_off(void) - switch only the data cache off (cache_hw_enable switches it on again)
; For code that lets the GDMA or the blitter write memory the CPU then reads (the TEST commands):
; the 68040 does not snoop, so with the data cache on it could read old copies.
;
_cache_data_off:
        ifd CACHE_ON
            movec   cacr,d0
            and.l   #$7FFFFFFF,d0       ; DE off, instruction cache unchanged
            nop
            cpusha  dc                  ; changed data to memory first (write-through: normally nothing)
            movec   d0,cacr
            nop
        endif
            rts

;
; unsigned long cache_get_cacr(void) - read the CPU's cache control register
; 2.0.30: lets the boot screen and SYSINFO show what the caches really do (bit 31 = data cache on,
; bit 15 = instruction cache on), also in a normal build.
;
_cache_get_cacr:
            movec   cacr,d0
            rts
