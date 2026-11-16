# keytest - show what the keyboard sends: the bytes a program reads from the
# console, or with "keytest scan" the raw scan codes (see keytest.c)
#   make        build keytest.pgz (start it from the MCP, e.g. from /sd/)
#   make clean  remove the build output
# Built with Calypsi (cc68k / ln68k), like the other test programs.

MODEL = --code-model=large --data-model=large

keytest.pgz: keytest.o
	ln68k -o $@ $^ a2560k.scm --rtattr printf=nofloat --output-format=pgz --target=A2560K --stack-size=4096 --heap-size=4096

keytest.o: keytest.c
	cc68k --core=68000 $(MODEL) --target=A2560K -o $@ $<

clean:
	-rm -f keytest.o keytest.pgz
