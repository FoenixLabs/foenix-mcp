# cachebench - measure CPU, memory and screen speed (see cachebench.c)
#   make        build cachebench.pgz (start it from the MCP, e.g. from /sd/)
#   make clean  remove the build output

MODEL = --code-model=large --data-model=large

cachebench.pgz: cachebench.o
	ln68k -o $@ $^ a2560k.scm --rtattr printf=nofloat --output-format=pgz --target=A2560K --stack-size=4096 --heap-size=4096

cachebench.o: cachebench.c
	cc68k --core=68000 $(MODEL) --target=A2560K -o $@ $<

clean:
	-rm -f cachebench.o cachebench.pgz
