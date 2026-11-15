# rtcclock - show the RTC time in the middle of the screen, interrupt driven (see rtcclock.c)
#   make        build rtcclock.pgz (start it from the MCP, e.g. from /sd/)
#   make clean  remove the build output
# Built with Calypsi (cc68k / ln68k), like the other test programs.

MODEL = --code-model=large --data-model=large

rtcclock.pgz: rtcclock.o
	ln68k -o $@ $^ a2560k.scm --rtattr printf=nofloat --output-format=pgz --target=A2560K --stack-size=4096 --heap-size=4096

rtcclock.o: rtcclock.c
	cc68k --core=68000 $(MODEL) --target=A2560K -o $@ $<

clean:
	-rm -f rtcclock.o rtcclock.pgz
