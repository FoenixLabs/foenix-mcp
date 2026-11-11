# patch_console.py - add the Gen1 COM1 remote console to MCP 2.0.7 dev/console.c (2026-11-04)
p = r'E:/FoenixMCP_Gen1/src/dev/console.c'
s = open(p, newline='').read()
nl = '\r\n' if '\r\n' in s else '\n'
BS = chr(92)  # a backslash, so the C escapes stay escapes
block = [
 '#if MODEL == MODEL_FOENIX_A2560K',
 '/*',
 ' * Gen1 remote console (2026-11-04, as the A2560Me MCP\'s COM4 console): everything written to the console also goes',
 ' * out on the SuperIO COM1 (16550 at $FEC0_23F8, 115200 8N1 = divisor 1, the UART clock is 1.8432 MHz), and bytes',
 ' * received on COM1 are read like keys. Polled, no interrupts. A missing cable costs nothing (the UART sends anyway).',
 ' */',
 '#define RC_COM1(r)  (*(volatile unsigned char *)(0xFEC023F8 + (r)))',
 '#define RC_LSR_RX   0x01',
 '#define RC_LSR_THRE 0x20',
 '',
 'static short rc_ready = 0;',
 '',
 'static void rc_init() {',
 '    RC_COM1(3) = 0x83;              /* LCR: divisor latch */',
 '    RC_COM1(0) = 1;                 /* 115200 */',
 '    RC_COM1(1) = 0;',
 '    RC_COM1(3) = 0x03;              /* 8N1 */',
 '    RC_COM1(2) = 0x07;              /* FIFOs on and cleared */',
 '    RC_COM1(1) = 0x00;              /* no interrupts */',
 '    RC_COM1(4) = 0x03;              /* DTR, RTS */',
 '    rc_ready = 1;',
 '}',
 '',
 'char rc_getc() {',
 '    char c;',
 '    if (!rc_ready) rc_init();',
 '    if (!(RC_COM1(5) & RC_LSR_RX)) return 0;',
 '    c = (char)RC_COM1(0);',
 '    if (c == 0x7F) c = CHAR_BS;     /* terminal DEL key = backspace */',
 '    return c;',
 '}',
 '',
 'void rc_putc(char c) {',
 '    long n = 20000;',
 '    if (!rc_ready) rc_init();',
 "    if (c == 0x0A) rc_putc(0x0D);   /* LF -> CR LF */",
 '    while (!(RC_COM1(5) & RC_LSR_THRE) && (--n > 0)) ;   /* 16-byte FIFO; a byte every 87 us */',
 '    if (n > 0) RC_COM1(0) = (unsigned char)c;',
 '}',
 '#endif',
 '',
 '']
head = '/*' + nl + ' * Send a byte to the console screen' + nl + ' */' + nl + 'short con_write_b(p_channel chan, uint8_t b) {' + nl + '    p_console_data con_data;' + nl
assert s.count(head) == 1
s = s.replace(head, nl.join(block) + head + nl + '#if MODEL == MODEL_FOENIX_A2560K' + nl + '    rc_putc((char)b);' + nl + '#endif' + nl)
a1 = '            c = kbdmo_getc();' + nl + '#endif' + nl
assert s.count(a1) == 1
s = s.replace(a1, a1 + '            if (c == 0) c = rc_getc();      /* Gen1 remote console: COM1 */' + nl)
a2 = '                c = kbdmo_getc();' + nl + '    #endif' + nl
assert s.count(a2) == 1
s = s.replace(a2, a2 + '                if (c == 0) c = rc_getc();  /* Gen1 remote console: COM1 */' + nl)
open(p, 'w', newline='').write(s)
print('patched')
