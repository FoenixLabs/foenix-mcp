# ps2test - check the PS/2 keyboard without the MCP's keyboard driver (see ps2test.c)
#   make        build ps2test.pgz (start it from the MCP)
#   make clean  remove the build output
# Built with Calypsi (cc68k / ln68k), like the other test programs.

MODEL = --code-model=large --data-model=large

ps2test.pgz: ps2test.o
	ln68k -o $@ $^ a2560k.scm --rtattr printf=nofloat --output-format=pgz --target=A2560K --stack-size=4096 --heap-size=4096

ps2test.o: ps2test.c
	cc68k --core=68000 $(MODEL) --target=A2560K -o $@ $<

clean:
	-rm -f ps2test.o ps2test.pgz
