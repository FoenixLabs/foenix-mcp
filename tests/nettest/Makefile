# nettest - check the MCP's network system calls (2.1.0, step M3a; see nettest.c)
#   make        build nettest.pgz (start it from the MCP)
#   make clean  remove the build output
# Built with Calypsi (cc68k / ln68k), like the other test programs. It only uses felinc.h for the
# types and constants (sockaddr_in, AF_INET, ...); the calls go straight to the MCP.

MODEL = --code-model=large --data-model=large
FELINC_H = ../../src/net/felinc

nettest.pgz: nettest.o
	ln68k -o $@ $^ a2560k.scm --rtattr printf=nofloat --output-format=pgz --target=A2560K --stack-size=4096 --heap-size=4096

nettest.o: nettest.c
	cc68k --core=68000 $(MODEL) --target=A2560K -I$(FELINC_H) -o $@ $<

clean:
	-rm -f nettest.o nettest.pgz
