#!/usr/bin/env python3
"""imgsize.py - report a loader image's size against its 512 KB flash block (and the 64 KB the FPGA copies)."""
import sys
d = open(sys.argv[1], 'rb').read()
print('%s: %d bytes = %d KB of the 512 KB block%s' % (sys.argv[1], len(d), (len(d) + 1023) // 1024,
      '  ** TOO BIG **' if len(d) > 0x80000 else ''))
