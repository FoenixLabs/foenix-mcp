;;;
;;; Assembly code for direct floppy drive support for the A2560K and Gen X: the Calypsi (as68k)
;;; version of m68040/fdc_m68040.s (2.1.0). Keep the two the same. Differences: the parameters
;;; come from the stack (fdc_cmd_asm is ASMCALL in dev/fdc.c), C names have no extra underscore,
;;; and all used registers are saved as longs (the VBCC version saves d4-d7/a4-a5 as words).
;;;

                .public fdc_cmd_asm
                .extern timers_jiffies

                .section code

FDC_BASE:       .equ 0xFEC023F0
FDC_DOR:        .equ 0xFEC023F2     ; Read/Write - Digital Output Register
FDC_DOR_DSEL0:  .equ 0x01     ; Drive 0 Select
FDC_DOR_DSEL1:  .equ 0x02     ; Drive 1 Select
FDC_DOR_NRESET: .equ 0x04    ; Reset the FDC
FDC_DOR_DMAEN:  .equ 0x08     ; Enable DMA
FDC_DOR_MOT0: .equ 0x10     ; Turn on motor 0
FDC_DOR_MOT1: .equ 0x20     ; Turn on motor 1
FDC_DOR_MOT2: .equ 0x40     ; Turn on motor 2
FDC_DOR_MOT3: .equ 0x80     ; Turn on motor 3

FDC_MSR:        .equ 0xFEC023F4     ; Read - Main Status Register
FDC_MSR_DRV0BSY:.equ 0x01   ; Indicates if drive 0 is busy
FDC_MSR_DRV1BSY:.equ 0x02   ; Indicates if drive 1 is busy
FDC_MSR_CMDBSY: .equ 0x10    ; Indicates if a command is in progress
FDC_MSR_NONDMA: .equ 0x20    ;
FDC_MSR_DIO:    .equ 0x40       ; Data direction: 1 = read, 0 = write
FDC_MSR_RQM:    .equ 0x80       ; 1 = host can transfer data, 0 = host must wait

FDC_DSR:        .equ 0xFEC023F4     ; Write - Data Select Register
; Bit[0..1] = data rate
; Bit[2..4] = precompensation select
FDC_DSR_LOPWR:  .equ 0x40     ; Turn on low power mode
FDC_DSR_RESET:  .equ 0x80     ; Software reset of the FDC

FDC_DATA:       .equ 0xFEC023F5    ; Read/Write - Data - FIFO

FDC_RSV:        .equ 0xFEC023F6     ; Reserved

FDC_DIR:        .equ 0xFEC023F7     ; Read - Digital Input Register
FDC_DIR_DSKCHG: .equ 0x80    ; Indicates if the disk has changed

FDC_CCR:        .equ 0xFEC023F7     ; Write - Configuration Control Register
; Bit[0..1] = Data rate

;
; Issue a command to the floppy drive controller
;
; short fdc_cmd_asm(short cmd, short argc, uint8_t * argv, uint8_t * buffer, short resultc, uint8_t * results);
;
; The parameters are on the stack (two bytes for a short, four for a pointer) and are loaded into
; the registers the VBCC version gets them in:
; @param command (byte) the command to execute (d1)
; @param argc the number of bytes in the arguments (d2)
; @param argv (byte array) the bytes to send as arguments to the command (a1)
; @param buffer the buffer to fill with any read data (a2)
; @param resultc the count of result bytes expected (d3)
; @param results the buffer to fill with result bytes (a3)
; @return 0 = done, -1 / -2 = the controller did not accept the command
;
FDC_SAVED:      .equ 40                         ; d2-d7/a2-a5 saved below
                .align 2
fdc_cmd_asm:    movem.l d2-d7/a2-a5,-(a7)       ; Calypsi: everything but d0/d1/a0/a1 must be kept
                move.w (FDC_SAVED+4,a7),d1      ; cmd
                move.w (FDC_SAVED+6,a7),d2      ; argc
                move.l (FDC_SAVED+8,a7),a1      ; argv
                move.l (FDC_SAVED+12,a7),a2     ; buffer
                move.w (FDC_SAVED+16,a7),d3     ; resultc
                move.l (FDC_SAVED+18,a7),a3     ; results

                ; Clear the buffer

                move.w #0x0,d0
clr_buffer:     move.b #0,(a2,d0.w)
                addq.w #1,d0
                cmp.w #512,d0
                bne clr_buffer

                ; Clear the results

                move.w #0x0,d0
clr_results:    move.b #0,(a3,d0.w)
                addq.w #1,d0
                cmp.w #10,d0
                bne clr_results

                ; Make sure the FDC will accept data
                move.b (FDC_MSR).l,d0
                and.b #FDC_MSR_RQM|FDC_MSR_DIO,d0
                cmp.b #FDC_MSR_RQM,d0
                beq snd_cmd
                move.l #0xFFFFFFFF,d0            ; Return -1
                bra fdc_cmd_exit

snd_cmd:        move.b d1,(FDC_DATA).l              ; Send the command

                ; Send the arguments

                move.w #0,d4                    ; Counter = 0
wait_snd_args:  move.b (FDC_MSR).l,d0               ; Get the MSR
                btst #7,d0                      ; Is RQM set?
                beq wait_snd_args
                btst #6,d0                      ; Verify that DIO = 0
                beq snd_arg
                move.w #-2,d0                   ; Return -2
                bra fdc_cmd_exit

snd_arg:        move.b (a1,d4.w),(FDC_DATA).l         ; Send the next argument byte
                addq.w #1,d4
                cmp.w d2,d4
                bne wait_snd_args               ; If not done, wait for the FDC to be ready and send the next byte

                ; Receive data bytes

get_data:       move.b (FDC_MSR).l,d0
                btst #5,d0
                bne result_phase

                move.w #0,d4                    ; Counter = 0

wait_data:      move.b (FDC_MSR).l,d0
                btst #7,d0                      ; Is RQM = 1
                beq wait_data                   ; No: wait until it is

                move.b (FDC_DATA).l,(a2,d4.w)         ; Move the data byte to the data buffer

                move.b (FDC_MSR).l,d0               ; Validate we're still in execution phase
                btst #5,d0
                beq result_phase                ; If not, go to result phase

                addq.w #1,d4                    ; Move to the next byte
                cmp.w #512,d4                   ; (word: the upper half of d4 is not cleared)
                bne wait_data                   ; Keep looping if we are not done with the buffer

                ; Receive result bytes

result_phase:   ; Start by waiting?
                jsr (timers_jiffies).l             ; Wait 5 jiffies? Just adding some delay
                move.l d0,d4
                addq.l #2,d4                    ; Put the target jiffy count in D4

wait_delay:     jsr (timers_jiffies).l             ; Wait until we reach the target time
                cmp.l d0,d4
                bgt wait_delay

                move.w #0,d4                    ; Counter = 0
wait_result:    move.b (FDC_MSR).l,d0               ; Get the MSR
                andi.b #0xD0,d0

                cmp.b #FDC_MSR_RQM,d0
                beq fdc_success                 ; If just RQM, we're done, and the command is done

                cmp.b #0x90,d0
                beq fdc_error_2                 ; If busy and DIO=0, it wants input... something's wrong
                btst #7,d0                      ; Check RQM

                beq wait_result                 ; If RQM=0, keep waiting

                move.b (FDC_DATA).l,(a3,d4.w)         ; Move the data byte to the result buffer
                addq.w #1,d4
                cmp.w d4,d3                     ; Have we reached the end?
                bne wait_result                 ; No: keep looping

chk_done:       ; Make sure the results are not an error

wait_done:      move.b (FDC_MSR).l,d0               ; Get the MSR
                btst #7,d0                      ; Is RQM set?
                beq wait_done                   ; No: wait until it is

                addq.b #1,(2,a3)

                btst #4,d0                      ; Check CMDBUSY
                beq fdc_success                 ; If it's 0, we're done
                bra wait_done

fdc_error_2:    move.l #0xfffffffe,d0
                bra fdc_cmd_exit

fdc_error:      move.l #0xffffffff,d0            ; Return -1 for error
                bra fdc_cmd_exit

fdc_success:    move.l #0,d0                    ; Return 0 for success

                ; Restore our registers
fdc_cmd_exit:   movem.l (a7)+,d2-d7/a2-a5
                rts
