"""s68tobin.py IN.s68 OUT.bin - flat binary from address 0 (gaps = 0) for the debug-port load."""
import sys
mem = {}
for l in open(sys.argv[1]):
    l = l.strip()
    if l[:2] in ('S1', 'S2', 'S3'):
        al = {'S1': 2, 'S2': 3, 'S3': 4}[l[:2]]
        n = int(l[2:4], 16); a = int(l[4:4 + 2 * al], 16)
        d = bytes.fromhex(l[4 + 2 * al:4 + 2 * n])
        mem[a] = d
top = max(a + len(d) for a, d in mem.items())
out = bytearray(top)
for a, d in mem.items():
    out[a:a + len(d)] = d
open(sys.argv[2], 'wb').write(out)
print('%d bytes, top %X' % (len(out), top))
