# elevtest - check the TRAP #15 elevate test (2.1.0; see elevtest.c)
#   make        build elevtest.pgz (start it from the MCP)
#   make clean  remove the build output

MODEL = --code-model=large --data-model=large

elevtest.pgz: elevtest.o
	ln68k -o $@ $^ a2560k.scm --rtattr printf=nofloat --output-format=pgz --target=A2560K --stack-size=4096 --heap-size=4096

elevtest.o: elevtest.c
	cc68k --core=68000 $(MODEL) --target=A2560K -o $@ $<

clean:
	-rm -f elevtest.o elevtest.pgz
