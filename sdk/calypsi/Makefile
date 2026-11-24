# The MCP's Calypsi SDK: builds the system call library and the test program
#
#   make                  lib/mcp_syscalls.a for the default models, and test/sdktest.pgz
#   make CORE=68040 DATA=small
#                         for other models: use the same --core, --code-model and --data-model
#                         as the program that links the library
#   make clean
#
# Calypsi's A2560K library (--target=A2560K) also has system call functions, older than MCP 2.1.0.
# Link lib/mcp_syscalls.a before it (it is named on the ln68k command line; Calypsi's own comes
# last), and compile with -I<this folder>/include so <mcp/syscalls.h> is this SDK's.

CORE ?= 68000
CODE ?= large
DATA ?= large

CFLAGS = --core=$(CORE) --code-model=$(CODE) --data-model=$(DATA) --target=A2560K -Iinclude
# a2560k.scm is Calypsi's own linker rules file (its linker-rules folder)
LNFLAGS = a2560k.scm --target=A2560K --rtattr printf=nofloat --stack-size=8192 --heap-size=65536

LIB = lib/mcp_syscalls.a

all: $(LIB) test/sdktest.pgz

obj:
	mkdir -p obj

lib:
	mkdir -p lib

obj/mcp_syscalls.o: src/mcp_syscalls.c $(wildcard include/mcp/*.h) | obj
	cc68k $(CFLAGS) -o $@ $<

$(LIB): obj/mcp_syscalls.o | lib
	rm -f $@
	nlib $@ $^

obj/sdktest.o: test/sdktest.c $(wildcard include/mcp/*.h) | obj
	cc68k $(CFLAGS) -o $@ $<

test/sdktest.pgz: obj/sdktest.o $(LIB)
	ln68k -o $@ $^ $(LNFLAGS) --output-format=pgz --list-file=obj/sdktest.lst

clean:
	rm -rf obj lib test/sdktest.pgz

.PHONY: all clean
