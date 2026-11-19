;;;
;;; Start-up code, vector table and low-level routines of the MCP for the 68040 / 68060 machines
;;; (A2560K, A2560X, GenX): the Calypsi (as68k) version.
;;;
;;; 2.1.0: this is m68040/startup_m68040.s (vasm, VBCC build) translated to as68k. Keep the two
;;; the same: a change in one belongs in the other. The differences:
;;;  - C names have no extra underscore (VBCC adds one: _main there is main here).
;;;  - Hexadecimal is 0x..., local labels end in $, directives start with a dot.
;;;  - Initialised data: the VBCC build places it at $1000-$FFFF, where the FPGA copies it from
;;;    the flash block at reset. Calypsi keeps it in flash and the C library's start-up copies
;;;    it to RAM (and clears BSS), so nothing depends on the FPGA copy except this vector table.
;;;  - Functions that C calls, or that call C, use the stack for parameters. On the C side
;;;    they are marked ASMCALL (asmcall.h, Calypsi's simple_call): D0, D1, A0 and A1 may be
;;;    changed, every other register must be kept.
;;;  - CPUSHA BC and CINVA BC are written as opcode words: as68k 5.18 does not know them.
;;;  - No block-0 trampoline: the Calypsi build is for flash block 1 only.
;;;

            .extern main
            .extern cli_rerepl
            .extern panic
            .extern panic_number, panic_pc, panic_address
            .extern proc_return_point
            .extern g_int_handler
            .extern int_vicky_channel_a, int_vicky_channel_b
            .extern syscall_dispatch
            .extern __program_start

            .public __program_root_section
            .public syscall
            .public int_enable_all, int_disable_all, int_restore
            .public call_user, restart_cli, proc_return
            .public cache_hw_enable, cache_flush, cache_data_off, cache_get_cacr
            .public __memory_start, __USER_STACK
            .public __low_level_init

;
; Memory layout (the same as the VBCC build, vlink_flash_a2560k_b1.cmd)
;
MCP_STACK:      .equ 0x003E0000     ; supervisor stack: grows down from the end of the MCP's RAM
__memory_start: .equ 0x003A0000     ; start of the MCP's own RAM (data, BSS, heap, stack)
__USER_STACK:   .equ 0x00010000     ; top of the stack a program gets; below it: vectors etc.

;
; Interrupt registers for A2560X/K and GenX
;
PENDING_GRP0:   .equ 0xFEC00100
PENDING_GRP1:   .equ 0xFEC00102
PENDING_GRP2:   .equ 0xFEC00104

BORDER_COLOUR:  .equ 0xFEC80008     ; VICKY channel A border colour: shows how far the start got

;
; 2.0.30: values for the MCP info record in vectors 16-19 (see below). The Makefile passes the
; version from version.h as MCP_VER_MAJOR / MCP_VER_MINOR / MCP_VER_BUILD; without them the
; record says 0.0.0.
;
#ifndef MCP_VER_MAJOR
#define MCP_VER_MAJOR 0
#endif
#ifndef MCP_VER_MINOR
#define MCP_VER_MINOR 0
#endif
#ifndef MCP_VER_BUILD
#define MCP_VER_BUILD 0
#endif
#ifdef CACHE_ON
MCP_INFO_FLAGS: .equ 1              ; bit 0: this MCP switches the CPU caches on
#else
MCP_INFO_FLAGS: .equ 0
#endif
MCP_INFO_MAGIC: .equ 0x4D435049     ; "MCPI"
MCP_INFO_VER:   .equ (MCP_VER_MAJOR << 24) | (MCP_VER_MINOR << 16) | MCP_VER_BUILD

;;; ***************************************************************************
;;; The vector table, placed at address 0 (the first 1 KB of the flash block, which the FPGA
;;; copies to RAM at reset). Its label replaces the C library's own start-up (cstartup), which
;;; the linker would otherwise pull in.
;;; ***************************************************************************

            .section vectors, root
__program_root_section:
            .long MCP_STACK         ; 00 - Initial stack pointer
            .long coldboot          ; 01 - Initial PC
            .long handle_bus_err    ; 02 - Bus error
            .long handle_addr_err   ; 03 - Address error
            .long handle_inst_err   ; 04 - Illegal instruction
            .long handle_div0_err   ; 05 - Zero divide
            .long handle_chk_err    ; 06 - CHK instruction
            .long handle_trapv_err  ; 07 - TRAPV instruction
            .long handle_priv_err   ; 08 - Privilege error
            .long not_impl          ; 09 - Trace
            .long not_impl          ; 10 - Line 1010
            .long not_impl          ; 11 - Line 1111
            .long not_impl          ; 12 - Reserved
            .long not_impl          ; 13 - Reserved
            .long not_impl          ; 14 - Format error
            .long not_impl          ; 15 - Uninitialized Interrupt Error
            ;
            ; 16-19: 2.0.30 MCP info record, read from flash by the boot loader (lvboot) to show
            ; "MCP 2.0.30 caches ON" in its menu. The 68040 and 68060 never use vectors 16-23
            ; (reserved by Motorola), so these four longs are free for data. Offset $40 in the image.
            ;
            .long MCP_INFO_MAGIC    ; 16 - "MCPI"
            .long MCP_INFO_VER      ; 17 - version: major (bits 31-24), minor (23-16), build (15-0)
            .long MCP_INFO_FLAGS    ; 18 - flags: bit 0 = CPU caches on
            .long MCP_INFO_MAGIC ^ MCP_INFO_VER ^ MCP_INFO_FLAGS ; 19 - check: the three XOR-ed
            .long not_impl          ; 20 - Reserved
            .long not_impl          ; 21 - Reserved
            .long not_impl          ; 22 - Reserved
            .long not_impl          ; 23 - Reserved
            .long handle_spurious   ; 24 - Spurious Interrupt
            .long not_impl          ; 25 - Level 1 Interrupt Autovector
            .long not_impl          ; 26 - Level 2 Interrupt Autovector
            .long not_impl          ; 27 - Level 3 Interrupt Autovector
            .long not_impl          ; 28 - Level 4 Interrupt Autovector
            .long int_vicky_a       ; 29 - Level 5 Interrupt Autovector
            .long int_vicky_b       ; 30 - Level 6 Interrupt Autovector
            .long not_impl          ; 31 - Level 7 Interrupt Autovector
            .long not_impl          ; 32 - TRAP #0
            .long not_impl          ; 33 - TRAP #1
            .long not_impl          ; 34 - TRAP #2
            .long not_impl          ; 35 - TRAP #3
            .long not_impl          ; 36 - TRAP #4
            .long not_impl          ; 37 - TRAP #5
            .long not_impl          ; 38 - TRAP #6
            .long not_impl          ; 39 - TRAP #7
            .long not_impl          ; 40 - TRAP #8
            .long not_impl          ; 41 - TRAP #9
            .long not_impl          ; 42 - TRAP #10
            .long not_impl          ; 43 - TRAP #11
            .long not_impl          ; 44 - TRAP #12
            .long not_impl          ; 45 - TRAP #13
            .long not_impl          ; 46 - TRAP #14
            .long h_trap_15         ; 47 - TRAP #15
            .long not_impl          ; 48 - Reserved
            .long not_impl          ; 49 - Reserved
            .long not_impl          ; 50 - Reserved
            .long not_impl          ; 51 - Reserved
            .long not_impl          ; 52 - Reserved
            .long not_impl          ; 53 - Reserved
            .long not_impl          ; 54 - Reserved
            .long not_impl          ; 55 - Reserved
            .long not_impl          ; 56 - Reserved
            .long not_impl          ; 57 - Reserved
            .long not_impl          ; 58 - Reserved
            .long not_impl          ; 59 - Reserved
            .long not_impl          ; 60 - Reserved
            .long not_impl          ; 61 - Reserved
            .long not_impl          ; 62 - Reserved
            .long not_impl          ; 63 - Reserved
            .long interrupt_x10     ; 64 - Interrupt 0x10 - SuperIO - PS/2 Keyboard
            .long interrupt_x11     ; 65 - Interrupt 0x11 - A2560K Built-in Keyboard (Mo)
            .long interrupt_x12     ; 66 - Interrupt 0x12 - SuperIO - PS/2 Mouse
            .long interrupt_x13     ; 67 - Interrupt 0x13 - SuperIO - COM1
            .long interrupt_x14     ; 68 - Interrupt 0x14 - SuperIO - COM2
            .long interrupt_x15     ; 69 - Interrupt 0x15 - SuperIO - LPT1
            .long interrupt_x16     ; 70 - Interrupt 0x16 - SuperIO - Floppy Disk Controller
            .long interrupt_x17     ; 71 - Interrupt 0x17 - SuperIO - MIDI
            .long interrupt_x18     ; 72 - Interrupt 0x18 - Timer 0
            .long interrupt_x19     ; 73 - Interrupt 0x19 - Timer 1
            .long interrupt_x1A     ; 74 - Interrupt 0x1A - Timer 2
            .long interrupt_x1B     ; 75 - Interrupt 0x1B - Timer 3
            .long interrupt_x1C     ; 76 - Interrupt 0x1C - Timer 4
            .long interrupt_x1D     ; 77 - Interrupt 0x1D - Reserved
            .long interrupt_x1E     ; 78 - Interrupt 0x1E - Reserved
            .long interrupt_x1F     ; 79 - Interrupt 0x1F - Real Time Clock

            .long interrupt_x20     ; 80 - Interrupt 0x20 - IDE HDD Generated Interrupt
            .long interrupt_x21     ; 81 - Interrupt 0x21 - SDCard Insert
            .long interrupt_x22     ; 82 - Interrupt 0x22 - SDCard Controller
            .long interrupt_x23     ; 83 - Interrupt 0x23 - Internal OPM
            .long interrupt_x24     ; 84 - Interrupt 0x24 - External OPN2
            .long interrupt_x25     ; 85 - Interrupt 0x25 - External OPL3
            .long interrupt_x26     ; 86 - Interrupt 0x26 - Reserved
            .long interrupt_x27     ; 87 - Interrupt 0x27 - Reserved
            .long interrupt_x28     ; 88 - Interrupt 0x28 - Beatrix Interrupt 0
            .long interrupt_x29     ; 89 - Interrupt 0x29 - Beatrix Interrupt 1
            .long interrupt_x2A     ; 90 - Interrupt 0x2A - Beatrix Interrupt 2
            .long interrupt_x2B     ; 91 - Interrupt 0x2B - Beatrix Interrupt 3
            .long interrupt_x2C     ; 92 - Interrupt 0x2C - Reserved
            .long interrupt_x2D     ; 93 - Interrupt 0x2D - DAC1 Playback Done Interrupt (48K)
            .long interrupt_x2E     ; 94 - Interrupt 0x2E - Reserved
            .long interrupt_x2F     ; 95 - Interrupt 0x2F - DAC0 Playback Done Interrupt (44.1K)

;;; ***************************************************************************
;;; Cold boot: CPU set-up, then the C library's start-up (cstartup, __program_start), which
;;; copies the initialised data to RAM, clears BSS, sets up the C library streams and the heap,
;;; and calls main(). The linker always includes that start-up; it calls __low_level_init
;;; (below) first.
;;; ***************************************************************************

            .section code
            .align 2
coldboot:   move.w #0x2700,sr       ; Supervisor mode, disable all interrupts
            moveq #0,d0             ; Disable 040's MMU
            movec d0,tc

            move.l #0xff00ff00,(BORDER_COLOUR).l ; green border: the MCP has started
            lea (MCP_STACK).l,sp
            bsr int_disable_all
            jmp (__program_start).l ; C data, heap, then main() (never returns)

;
; int __low_level_init(void) - called by the C library start-up before it sets up the C data
;
; Returns 1: stay in supervisor mode with the interrupts off (0 would drop to user mode and
; enable the interrupts before main). The MCP switches the interrupts on itself, later.
;
__low_level_init:
            move.l #0xffffff00,(BORDER_COLOUR).l ; yellow border: the C start-up runs
            moveq #1,d0
            rts

;
; Autovector handler: Used by VICKY III Channel B interrupts
;
int_vicky_b:
            movem.l d0-d7/a0-a6,-(a7)
            jsr (int_vicky_channel_b).l     ; Call the dispatcher for Channel B interrupts
            movem.l (a7)+,d0-d7/a0-a6
            rte

;
; Autovector handler: Used by VICKY III Channel A interrupts
;
int_vicky_a:
            movem.l d0-d7/a0-a6,-(a7)
            jsr (int_vicky_channel_a).l     ; Call the dispatcher for Channel A interrupts
            movem.l (a7)+,d0-d7/a0-a6
            rte

;
; Given the interrupt's offset in the interrupt handler table, fetch the pointer
; to the interrupt handler and call it... if there is one
;
; Assumes registers D0-D7, A0-A6 have been saved to the stack with MOVEM
;
int_dispatch:
            lea (g_int_handler).l,a0        ; Look in the interrupt handler table
            move.l (0,a0,d0.w),d0           ; Get the address of the handler
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
inthandler: .macro num, mask, pend
            movem.l d0-d7/a0-a6,-(a7)       ; Save affected registers
            move.w #\mask,(\pend).l         ; Clear the flag for the interrupt
            move.w #(\num << 2),d0          ; Offset of the handler in the table
            bra int_dispatch
            .endm

;
; Group 1 Interrupt Handlers
;

interrupt_x10:  inthandler 0x10, 0x0001, PENDING_GRP1   ; SuperIO Keyboard
interrupt_x11:  inthandler 0x11, 0x0002, PENDING_GRP1   ; A2560K "Mo" keyboard
interrupt_x12:  inthandler 0x12, 0x0004, PENDING_GRP1   ; SuperIO Mouse
interrupt_x13:  inthandler 0x13, 0x0008, PENDING_GRP1   ; COM1
interrupt_x14:  inthandler 0x14, 0x0010, PENDING_GRP1   ; COM2
interrupt_x15:  inthandler 0x15, 0x0020, PENDING_GRP1   ; LPT1
interrupt_x16:  inthandler 0x16, 0x0040, PENDING_GRP1   ; Floppy drive controller
interrupt_x17:  inthandler 0x17, 0x0080, PENDING_GRP1   ; MIDI
interrupt_x18:  inthandler 0x18, 0x0100, PENDING_GRP1   ; Timer 0
interrupt_x19:  inthandler 0x19, 0x0200, PENDING_GRP1   ; Timer 1
interrupt_x1A:  inthandler 0x1A, 0x0400, PENDING_GRP1   ; Timer 2
interrupt_x1B:  inthandler 0x1B, 0x0800, PENDING_GRP1   ; Timer 3
interrupt_x1C:  inthandler 0x1C, 0x1000, PENDING_GRP1   ; Timer 4
interrupt_x1D:  inthandler 0x1D, 0x2000, PENDING_GRP1   ; Reserved
interrupt_x1E:  inthandler 0x1E, 0x4000, PENDING_GRP1   ; Reserved (LAN9221)
interrupt_x1F:  inthandler 0x1F, 0x8000, PENDING_GRP1   ; Real Time Clock

;
; Group 2 Interrupt Handlers
;

interrupt_x20:  inthandler 0x20, 0x0001, PENDING_GRP2   ; IDE HDD Generated Interrupt
interrupt_x21:  inthandler 0x21, 0x0002, PENDING_GRP2   ; SDCard Insert
interrupt_x22:  inthandler 0x22, 0x0004, PENDING_GRP2   ; SDCard Controller
interrupt_x23:  inthandler 0x23, 0x0008, PENDING_GRP2   ; Internal OPM
interrupt_x24:  inthandler 0x24, 0x0010, PENDING_GRP2   ; External OPN2
interrupt_x25:  inthandler 0x25, 0x0020, PENDING_GRP2   ; External OPL3
interrupt_x26:  inthandler 0x26, 0x0040, PENDING_GRP2   ; Reserved
interrupt_x27:  inthandler 0x27, 0x0080, PENDING_GRP2   ; Reserved
interrupt_x28:  inthandler 0x28, 0x0100, PENDING_GRP2   ; Beatrix Interrupt 0
interrupt_x29:  inthandler 0x29, 0x0200, PENDING_GRP2   ; Beatrix Interrupt 1
interrupt_x2A:  inthandler 0x2A, 0x0400, PENDING_GRP2   ; Beatrix Interrupt 2
interrupt_x2B:  inthandler 0x2B, 0x0800, PENDING_GRP2   ; Beatrix Interrupt 3
interrupt_x2C:  inthandler 0x2C, 0x1000, PENDING_GRP2   ; Reserved
interrupt_x2D:  inthandler 0x2D, 0x2000, PENDING_GRP2   ; DAC1 Playback Done Interrupt (48K)
interrupt_x2E:  inthandler 0x2E, 0x4000, PENDING_GRP2   ; Reserved
interrupt_x2F:  inthandler 0x2F, 0x8000, PENDING_GRP2   ; DAC0 Playback Done Interrupt (44.1K)

;
; Unimplemented Exception Handler -- just return
;
not_impl:   rte

;
; short int_enable_all(void) - enable all interrupts, return the old level
;
int_enable_all:
            move.w sr,d0            ; Save the old level for return
            andi.w #0x0700,d0
            lsr.w #8,d0
            ext.l d0
            andi.w #0xF8FF,sr       ; Clear the level
            rts

;
; short int_disable_all(void) - disable all interrupts, return the old level
;
int_disable_all:
            move.w sr,d0            ; Save the old level for return
            andi.w #0x0700,d0
            lsr.w #8,d0
            ext.l d0
            ori.w #0x0700,sr        ; Set the level to 7
            rts

;
; void int_restore(short level) - restore the interrupt level (the short is a word on the stack)
;
int_restore:
            move.w (4,sp),d0        ; Get the priority into d0
            andi.w #3,d0            ; (as in the VBCC version)
            lsl.w #8,d0
            move.w sr,d1            ; Get the current SR into d1
            andi.w #0xF8FF,d1       ; Clear the old priority
            or.w d0,d1              ; And set it to the current
            move.w d1,sr            ; And set the level
            rts

;
; Handlers for the various exceptions...
;
; These all go to the kernel panic screen after placing the
; triggering PC address in "panic_pc" and the number of the exception
; in "panic_number". Bus and address errors will also put the
; address involved in "panic_address".
;

handle_bus_err:
            lea (panic_number).l,a0
            lea (panic_pc).l,a1
            lea (panic_address).l,a2
            move.w #2,(a0)
            move.l (2,a7),(a1)
            move.l (8,a7),(a2)
            bra call_panic

handle_addr_err:
            lea (panic_number).l,a0
            lea (panic_pc).l,a1
            lea (panic_address).l,a2
            move.w #3,(a0)
            move.l (2,a7),(a1)
            move.l (8,a7),(a2)
            bra call_panic

handle_inst_err:
            lea (panic_number).l,a0
            lea (panic_pc).l,a1
            move.w #4,(a0)
            move.l (2,a7),(a1)
            bra call_panic

handle_div0_err:
            lea (panic_number).l,a0
            lea (panic_pc).l,a1
            move.w #5,(a0)
            move.l (2,a7),(a1)
            bra call_panic

handle_chk_err:
            lea (panic_number).l,a0
            lea (panic_pc).l,a1
            move.w #6,(a0)
            move.l (2,a7),(a1)
            bra call_panic

handle_trapv_err:
            lea (panic_number).l,a0
            lea (panic_pc).l,a1
            move.w #7,(a0)
            move.l (2,a7),(a1)
            bra call_panic

handle_priv_err:
            lea (panic_number).l,a0
            lea (panic_pc).l,a1
            move.w #8,(a0)
            move.l (2,a7),(a1)
            bra call_panic

handle_spurious:
            lea (panic_number).l,a0
            lea (panic_pc).l,a1
            move.w #24,(a0)
            move.l (2,a7),(a1)
            bra call_panic

call_panic: jsr (panic).l
panic_lock: bra panic_lock

;
; Make a system call based on the number of the system function:
; int32_t syscall(int32_t number, int32_t p0, int32_t p1, int32_t p2, int32_t p3, int32_t p4, int32_t p5)
;
syscall:
            movem.l d1-d7,-(sp)     ; Save caller's registers
            move.l (56,sp),d6       ; Parameter 5 to D6
            move.l (52,sp),d5       ; Parameter 4 to D5
            move.l (48,sp),d4       ; Parameter 3 to D4
            move.l (44,sp),d3       ; Parameter 2 to D3
            move.l (40,sp),d2       ; Parameter 1 to D2
            move.l (36,sp),d1       ; Parameter 0 to D1
            move.l (32,sp),d0       ; Function number to D0

            trap #15                ; Call into the kernel

            movem.l (sp)+,d1-d7     ; Restore caller's registers
            rts

;
; TRAP #15 handler... transfer control to the C dispatcher
;
h_trap_15:
            cmp.w #0x43,d1          ; Is this a sys_proc_elevate call?
            beq h_trap_elev         ; Yes, just handle it here

            move.l d6,-(sp)         ; Push the parameters to the stack for the C call
            move.l d5,-(sp)
            move.l d4,-(sp)
            move.l d3,-(sp)
            move.l d2,-(sp)
            move.l d1,-(sp)
            move.l d0,-(sp)

            jsr (syscall_dispatch).l ; Call the C routine to do the dispatch (ASMCALL)

            lea (28,sp),sp          ; Remove parameters from the stack

            rte                     ; Return to the caller

h_trap_elev:
            ori.w #0x2000,(a7)      ; Change the caller's privilege to supervisor
            rte                     ; And return to it

;
; Jump into a user mode code
; void call_user(long start, long stack, int argc, char * argv[])
;
; 2.0.33: before jumping, a "return point" is saved on the supervisor stack (52 bytes):
;   +0  the previous return point (0 = none): a program can start a program
;   +4  the user stack pointer of whoever called us
;   +8  d2-d7/a2-a6, the registers a C caller expects to be kept
; and proc_return_point points at it. When the program ends (sys_exit -> proc_exit), proc_return
; goes back to it, so call_user returns to its caller like a normal subroutine: the command line
; or mcp.init then carries on.
;
CU_SAVED:   .equ 52                 ; bytes of the return point, see above

call_user:
            movem.l d2-d7/a2-a6,-(a7) ; The caller's registers
            move.l usp,a0
            move.l a0,-(a7)         ; The caller's user stack
            move.l (proc_return_point).l,-(a7) ; Link to the previous return point

            ; Set up the user stack. Normally at the top of the program stack area. When a program
            ; starts a program, that area holds the first program's stack, which must survive:
            ; then start just below where it is now.
            move.l #__USER_STACK,a0 ; Get the pointer to the process's stack
            tst.l (a7)              ; Started by a program (an earlier return point)?
            beq cu_stack
            move.l (4,a7),d0        ; Yes: below the caller's user stack,
            sub.l #16,d0            ; with a little room,
            and.l #0xFFFFFFFC,d0    ; on a long word
            move.l d0,a0
cu_stack:
            move.l a7,(proc_return_point).l ; This is where the program comes back to

            move.l (CU_SAVED+12,a7),d1 ; Get the number of parameters passed
            move.l (CU_SAVED+16,a7),a1 ; Get the pointer to the parameters
            move.l a1,-(a0)         ; Push the parameters list
            move.w d1,-(a0)         ; Push the parameter count
            move.l a0,usp           ; Set the User Stack Pointer

            ; Set up the system stack
            move.l (CU_SAVED+4,a7),a0 ; Get the pointer to the code to start
            move.w #0,-(a7)         ; Push the fake vector offset
            move.l a0,-(a7)         ; Push it as the starting address
            move.w #0x0000,-(a7)    ; Push the user's initial SR (to switch to user mode)
#ifdef CACHE_ON
            ; 2.0.29: the program was just loaded with data writes. The instruction cache may still
            ; hold old code from the same addresses (an earlier program), so drop it before jumping in.
            nop
            .word 0xF4F8            ; cpusha bc
            nop
            ; 2.0.30: and set the caches up again. A program can get into supervisor mode and change
            ; the cache registers; without this the caches stayed the way that program left them.
            bsr cache_hw_enable
#endif
            rte                     ; Start the user process

;
; void restart_cli(void) - new supervisor stack, then the command line (does not return)
;
restart_cli:
            lea (MCP_STACK).l,sp
            clr.l (proc_return_point).l ; 2.0.33: the stack is gone, so are the return points
            jsr (cli_rerepl).l
            bra restart_cli

;
; 2.0.33: end the running program and return to where call_user was called (see there).
; C: void proc_return(int result) -- does not return to its own caller; call_user returns.
; Only call it when proc_return_point is not 0. Everything the program left on the supervisor
; stack (its system call's frame, for one) is dropped by going back to the saved stack pointer.
;
proc_return:
            move.l (4,a7),d0        ; The program's result, also returned by call_user
            move.l (proc_return_point).l,a7 ; Back to the stack as call_user left it
            move.l (a7)+,(proc_return_point).l ; The previous return point is current again
            move.l (a7)+,a0
            move.l a0,usp           ; The caller's user stack
            movem.l (a7)+,d2-d7/a2-a6 ; The caller's registers
            rts                     ; Return from call_user

;
; 2.0.29: 68040 / 68060 cache control (see src/cache.h for the C side).
; Only a build with CACHE=1 (-DCACHE_ON) does anything here; in a normal build every routine just
; returns. All of them use privileged instructions: call them from the kernel (supervisor mode) only.
;
; The cache mode of each address comes from the transparent translation registers (the MMU is off):
;   DTT0 $FC03C040 : $FC00_0000-$FFFF_FFFF data (VGE, register blocks, flash) = non-cacheable,
;                    serialized. Serialized, so a register read can never pass an earlier register write.
;   everything else: the 68040 default with the MMU off = cacheable WRITE-THROUGH. Every CPU write goes
;                    to memory at once, so video, the GDMA, the blitter, the DAC and the debug port always
;                    see what the CPU wrote.
;   ITT0 / ITT1 off: instructions from flash and RAM are cached (default mode).
;

;
; void cache_hw_enable(void) - switch the data and instruction caches on
;
cache_hw_enable:
#ifdef CACHE_ON
            moveq #0,d0
            movec d0,itt0           ; instructions: default (cacheable) everywhere
            movec d0,itt1
            movec d0,dtt1
            move.l #0xFC03C040,d0   ; data $FC00_0000-$FFFF_FFFF: non-cacheable, serialized
            movec d0,dtt0
            nop
            .word 0xF4D8            ; cinva bc: throw away whatever the caches hold from before
            move.l #0x80008000,d0   ; DE (bit 31) + IE (bit 15): same bits on the 68040 and the 68060
            movec d0,cacr
            nop
#endif
            rts

;
; void cache_flush(void) - write any changed data to memory and empty both caches
; Use it before running code that was loaded or changed with data writes.
;
cache_flush:
#ifdef CACHE_ON
            nop
            .word 0xF4F8            ; cpusha bc
            nop
#endif
            rts

;
; void cache_data_off(void) - switch only the data cache off (cache_hw_enable switches it on again)
; For code that lets the GDMA or the blitter write memory the CPU then reads (the TEST commands):
; the 68040 does not snoop, so with the data cache on it could read old copies.
;
cache_data_off:
#ifdef CACHE_ON
            movec cacr,d0
            and.l #0x7FFFFFFF,d0    ; DE off, instruction cache unchanged
            nop
            .word 0xF478            ; cpusha dc: changed data to memory first
            movec d0,cacr
            nop
#endif
            rts

;
; unsigned long cache_get_cacr(void) - read the CPU's cache control register
; 2.0.30: lets the boot screen and SYSINFO show what the caches really do (bit 31 = data cache on,
; bit 15 = instruction cache on), also in a normal build.
;
cache_get_cacr:
            movec cacr,d0
            rts
