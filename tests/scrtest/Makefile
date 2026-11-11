# scrtest - check the MCP main-screen scrolling (see scrtest.c)
#   make        build scrtest.pgz (start it from the MCP, e.g. from /sd/)
#   make clean  remove the build output

MODEL = --code-model=large --data-model=large

scrtest.pgz: scrtest.o
	ln68k -o $@ $^ a2560k.scm --rtattr printf=nofloat --output-format=pgz --target=A2560K --stack-size=4096 --heap-size=4096

scrtest.o: scrtest.c
	cc68k --core=68000 $(MODEL) --target=A2560K -o $@ $<

clean:
	-rm -f scrtest.o scrtest.pgz
