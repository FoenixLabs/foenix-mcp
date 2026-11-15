# rtctsr - resident clock in the MCP title bar (see rtctsr.c and tsr_handler.s)
#   make        build rtctsr.pgz (start it from the MCP; start it again to remove the clock)
#   make clean  remove the build output
# Built with Calypsi (cc68k / as68k / ln68k), like the other test programs.

MODEL = --code-model=large --data-model=large

rtctsr.pgz: rtctsr.o tsr_handler.o
	ln68k -o $@ $^ a2560k.scm --rtattr printf=nofloat --output-format=pgz --target=A2560K --stack-size=4096 --heap-size=4096 --list-file=rtctsr.lst

rtctsr.o: rtctsr.c
	cc68k --core=68000 $(MODEL) --target=A2560K -o $@ $<

tsr_handler.o: tsr_handler.s
	as68k --core=68000 $(MODEL) --target=A2560K --list-file=tsr_handler.lst -o $@ $<

clean:
	-rm -f rtctsr.o tsr_handler.o rtctsr.pgz rtctsr.lst tsr_handler.lst
