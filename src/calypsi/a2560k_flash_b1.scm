;;; Calypsi linker rules: the MCP in flash block 1 of an A2560K (8 x 512 KB layout), 2.1.0.
;;; The same layout as the VBCC build (vbcc/targets/m68k-foenix/vlink_flash_a2560k_b1.cmd):
;;;   $0000_0000  vectors: the first 1 KB of the block; the FPGA copies the block's first 64 KB
;;;               to RAM at reset, so they end up at address 0. (The VBCC build also keeps its
;;;               initialised data at $1000-$FFFF; the Calypsi build copies it from flash itself.)
;;;   $FFC9_0000  code, constants and the initial values of the data: the rest of the block
;;;   $003A_0000  the MCP's RAM: data, BSS, heap; the supervisor stack grows down from $003E_0000
;;;               (set in the start-up code, not here)
;;; tools/calypsi_image.py turns the linker's raw output into the 512 KB image for the block.
(define memories
  '((memory Vector (address (#x00000000 . #x000003ff)) (type ROM)
            (section (vectors #x00000000)))
    (memory flash (address (#xffc90000 . #xffcfffff)) (type ROM)
            (section code libcode cfar switch data_init_table ifar))
    (memory RAM (address (#x003a0000 . #x003dfbff)) (type RAM)
            (section far zfar heap stack))
    (block heap (size #x20000) (align 4))      ; for malloc()
    (block stack (size #x100) (align 4))       ; the C start-up sets USP here; programs get their own
    ))
