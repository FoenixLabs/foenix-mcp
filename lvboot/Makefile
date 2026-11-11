# Makefile - A2560Me LVGL boot loader (VBCC + LVGL 8.3 + FatFs R0.14b). GNU make from this folder:
#   make            lvboot.bin      flash image (block 0, or any block for a test: it copies itself from BOOT_BASE)
#   make RAM=1      lvboot_ram.bin  RAM test image: load at $0 with  a2560me_dbg.py --run lvboot_ram.bin --address 0
#   make clean
#   make RAM=1 EXTRA=-DPOPUPBENCH   pop-up timing test (results in SRAM at $007F_BD00; delete build/obj/src/menu.o first)
# Needs vbccm68k, vasmm68k_mot and vlink on the PATH, and VBCC pointing at the FoenixMCP_Gen2 vbcc folder (libvc).
# VBCC finds "..." includes next to the source file only with backslash paths, hence $(subst /,\,$<).

VBCC    ?= E:/FoenixMCP_Gen1/vbcc
MACHINE ?= K25
ifeq ($(MACHINE),K25)
CPU     = 68040
MACH    = k25
else ifeq ($(MACHINE),K60)
CPU     = 68060
MACH    = k60
else ifeq ($(MACHINE),X60)
CPU     = 68060
MACH    = x60
else ifeq ($(MACHINE),K40)
CPU     = 68040
MACH    = k40
else ifeq ($(MACHINE),X40)
CPU     = 68040
MACH    = x40
else
$(error MACHINE must be K25, K40, K60, X40 or X60)
endif
CC      = vbccm68k
AS      = vasmm68k_mot
LD      = vlink
INC     = -I. -Isrc -Ilvgl -Ifatfs -I$(VBCC)/targets/m68k-foenix/include
CFLAGS  = -quiet -c99 -cpu=$(CPU) -DMACHINE_$(MACHINE)=1 -O=991 -sec-per-obj -dontwarn=51 -dontwarn=153 -dontwarn=208 $(INC) $(EXTRA)
ASFLAGS = -quiet -Fvobj -m$(CPU) -nowarn=62 -DMACHINE_$(MACHINE)=1

OBJ     = build/$(MACH)
LVSRC  := $(shell bash lvsources.sh)
APPSRC := $(filter-out src/sd2.c src/sd_g1.c,$(wildcard src/*.c)) src/sd_g1.c
FFSRC   = fatfs/ff.c
COBJ    = $(patsubst %.c,$(OBJ)/%.o,$(APPSRC) $(FFSRC) $(LVSRC))
LVHDR  := $(shell find lvgl -name "*.h") lvgl/lvgl.h lv_conf.h

ifeq ($(RAM),1)
OUT     = lvboot_$(MACH)_ram.bin
CRT0    = $(OBJ)/crt0_ram.o
CRTDEF  = -DRAMIMAGE=1
else
OUT     = lvboot_$(MACH).bin
CRT0    = $(OBJ)/crt0_flash.o
CRTDEF  =
endif

all: $(OUT)

$(OUT): $(CRT0) $(COBJ) lvboot.ld
	$(LD) -brawbin1 -x -gc-all -e vectors -Tlvboot.ld -o $@ -M$(basename $@).map $(CRT0) $(COBJ) -L$(VBCC)/targets/m68k-foenix/lib -lvc
	@python tools/imgsize.py $@

$(CRT0): src/crt0.s
	@mkdir -p $(dir $@)
	$(AS) $(ASFLAGS) $(CRTDEF) -o $@ $<

# LVGL and FatFs: their own code, warnings off
$(OBJ)/lvgl/%.o: lvgl/%.c $(LVHDR)
	@mkdir -p $(dir $@)
	@$(CC) $(CFLAGS) -dontwarn=-1 $(subst /,\,$<) -o=$(@:.o=.s)
	@$(AS) $(ASFLAGS) -o $@ $(@:.o=.s)

$(OBJ)/fatfs/%.o: fatfs/%.c fatfs/ffconf.h
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -dontwarn=-1 $(subst /,\,$<) -o=$(@:.o=.s)
	@$(AS) $(ASFLAGS) -o $@ $(@:.o=.s)

$(OBJ)/src/%.o: src/%.c src/*.h $(LVHDR)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(subst /,\,$<) -o=$(@:.o=.s)
	@$(AS) $(ASFLAGS) -o $@ $(@:.o=.s)

clean:
	rm -rf build lvboot_*.bin *.map

.PHONY: all clean
