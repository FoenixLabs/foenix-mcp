;;; ***************************************************************************
;;;
;;; tsr_handler.s - the resident part of rtctsr: an RTC interrupt handler that
;;;                 shows the time in the right-hand end of the MCP title bar
;;;
;;; This block is COPIED by rtctsr.c into memory reserved with sys_mem_reserve
;;; and runs from there after rtctsr has ended. So it must work at any address:
;;; it only uses branches relative to the program counter and fixed hardware
;;; addresses, never an address inside this program.
;;;
;;; Block layout (rtctsr.c relies on it):
;;;   +0  "RTSR"                 magic, so a second run can find and remove it
;;;   +4  original handler       the MCP's RTC handler, filled in by rtctsr.c
;;;   +8  tsr_handler            the code below
;;;
;;; Called by the MCP's interrupt dispatcher (which saves all registers) on the
;;; RTC periodic interrupt, 64 times a second. It only reads the RTC: it does
;;; not write any RTC register.
;;;
;;; ***************************************************************************

              .rtmodel version, "1"
              .rtmodel cpu, "*"

              .public tsr_start, tsr_handler, tsr_end

RTC_SEC       .equ    0xFEC00080        ; bq4802 seconds (BCD)
RTC_MIN       .equ    0xFEC00082        ; minutes (BCD)
RTC_HOUR      .equ    0xFEC00084        ; hours (BCD; bit 7 = PM in 12-hour mode)
RTC_FLAGS     .equ    0xFEC0008D        ; reading clears the flags = acknowledges
TEXT_B        .equ    0xFECA0000        ; channel B text memory, 160 bytes per row
TIME_COLUMN   .equ    148               ; " hh:mm:ss" in columns 148-156 of row 0

              .section code
              .align  2

tsr_start:
              .long   0x52545352        ; "RTSR"
              .long   0                 ; original handler (rtctsr.c fills it in)

tsr_handler:
              movem.l d0-d1/a0,-(sp)
              move.b  RTC_FLAGS,d0      ; acknowledge the periodic interrupt
              lea     TEXT_B+TIME_COLUMN,a0
              move.b  #0x20,(a0)+       ; a space before the time
              move.b  RTC_HOUR,d0
              andi.b  #0x7F,d0          ; drop the PM bit
              bsr.s   put_bcd
              move.b  #0x3A,(a0)+       ; ':'
              move.b  RTC_MIN,d0
              bsr.s   put_bcd
              move.b  #0x3A,(a0)+       ; ':'
              move.b  RTC_SEC,d0
              bsr.s   put_bcd
              movem.l (sp)+,d0-d1/a0
              rts

;;; put_bcd - write a BCD byte as two digits
;;; Input:  d0 = BCD byte, a0 = where to write (moves on by 2)
;;; Output: none (d1 is used)
put_bcd:
              move.b  d0,d1
              lsr.b   #4,d1
              addi.b  #0x30,d1          ; '0' + tens
              move.b  d1,(a0)+
              andi.b  #0x0F,d0
              addi.b  #0x30,d0          ; '0' + ones
              move.b  d0,(a0)+
              rts

tsr_end:
