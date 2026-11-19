# topbartest - check the MCP 2.1.0 top bar from a program (see topbartest.c)
#   make        build topbartest.pgz (start it from the MCP)
#   make clean  remove the build output
# Built with Calypsi (cc68k / ln68k), like the other test programs.

MODEL = --code-model=large --data-model=large

topbartest.pgz: topbartest.o
	ln68k -o $@ $^ a2560k.scm --rtattr printf=nofloat --output-format=pgz --target=A2560K --stack-size=4096 --heap-size=4096

topbartest.o: topbartest.c
	cc68k --core=68000 $(MODEL) --target=A2560K -o $@ $<

clean:
	-rm -f topbartest.o topbartest.pgz
