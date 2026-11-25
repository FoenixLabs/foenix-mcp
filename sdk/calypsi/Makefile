# The MCP's Calypsi SDK: builds the system call libraries and the test program
#
#   make                  every library in lib/, and test/sdktest.pgz
#   make clean
#
# Calypsi's linker only links objects built with the same --core, --code-model and --data-model,
# so there is one library per combination, named the way Calypsi names its own:
#
#   lib/mcp_syscalls-<core>-<code>-<data>.a
#     core: 68000, 68040, 68060      (68020: no Foenix machine has one)
#     code: lc = large, sc = small   (--code-model)
#     data: ld = large, sd = small, fod = far-only   (--data-model)
#
# For example, a program built with --core=68040 --code-model=large --data-model=small links
# lib/mcp_syscalls-68040-lc-sd.a.
#
# Calypsi's A2560K library (--target=A2560K) also has system call functions, older than MCP 2.1.0.
# Link this SDK's library before it (it is named on the ln68k command line; Calypsi's own comes
# last), and compile with -I<this folder>/include so <mcp/syscalls.h> is this SDK's.

CORES = 68000 68040 68060
CODES = lc sc
DATAS = ld sd fod

VARIANTS = $(foreach c,$(CORES),$(foreach m,$(CODES),$(foreach d,$(DATAS),$(c)-$(m)-$(d))))
LIBS = $(VARIANTS:%=lib/mcp_syscalls-%.a)

# The compiler options for one variant ("68040-lc-sd"): the words of the name, turned into options
model_name = $(if $(filter lc ld,$(1)),large,$(if $(filter fod,$(1)),far-only,small))
variant_flags = --core=$(word 1,$(subst -, ,$(1))) \
	--code-model=$(call model_name,$(word 2,$(subst -, ,$(1)))) \
	--data-model=$(call model_name,$(word 3,$(subst -, ,$(1)))) \
	--target=A2560K -Iinclude

# The test program: 68000, large code and data
TEST_VARIANT = 68000-lc-ld
# a2560k.scm is Calypsi's own linker rules file (its linker-rules folder)
LNFLAGS = a2560k.scm --target=A2560K --rtattr printf=nofloat --stack-size=8192 --heap-size=65536

all: $(LIBS) test/sdktest.pgz

# One object per variant, in obj/<variant>/
obj/%/mcp_syscalls.o: src/mcp_syscalls.c $(wildcard include/mcp/*.h)
	mkdir -p $(@D)
	cc68k $(call variant_flags,$*) -o $@ $<

lib/mcp_syscalls-%.a: obj/%/mcp_syscalls.o
	mkdir -p lib
	rm -f $@
	nlib $@ $<

obj/$(TEST_VARIANT)/sdktest.o: test/sdktest.c $(wildcard include/mcp/*.h)
	mkdir -p $(@D)
	cc68k $(call variant_flags,$(TEST_VARIANT)) -o $@ $<

test/sdktest.pgz: obj/$(TEST_VARIANT)/sdktest.o lib/mcp_syscalls-$(TEST_VARIANT).a
	ln68k -o $@ $^ $(LNFLAGS) --output-format=pgz --list-file=obj/sdktest.lst

clean:
	rm -rf obj lib test/sdktest.pgz

.PHONY: all clean
# Keep the objects (make would delete them as intermediate files of the pattern rules)
.SECONDARY:
