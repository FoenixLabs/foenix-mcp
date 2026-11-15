#!/usr/bin/env python3
"""
make_tests.py - write three PGZ files that the MCP 2.0.32 loader must refuse

Copy them to the SD card and run each one from the MCP command line. Each must
stop with "Unable to execute file: memory used by the MCP" while nothing is
written and the machine keeps running (with MCP 2.0.31 and older they load
over the MCP and crash it).

  lowtest.pgz  loads at $0000_8000: the vectors / MCP variables / stack area
  toptest.pgz  loads at $003A_0000: the MCP's own RAM on the A2560K
  edgtest.pgz  loads at $0039_FFF0, 32 bytes: starts in free memory but runs
               16 bytes past the top of memory (checks the end, not just the start)

Usage: python3 make_tests.py   (writes the files in the current folder)
"""
import struct


def pgz(name, address, count):
    """Write a 32-bit PGZ ('z') with one data segment and a start segment.

    The data is RTS instructions; they never run, the load must be refused.
    Segments: address (4 bytes, little-endian), size (4 bytes), data.
    A segment with size 0 gives the start address.
    """
    data = b'\x4e\x75' * (count // 2)
    with open(name, 'wb') as f:
        f.write(b'z' + struct.pack('<II', address, count) + data + struct.pack('<II', address, 0))


pgz('lowtest.pgz', 0x00008000, 16)
pgz('toptest.pgz', 0x003A0000, 16)
pgz('edgtest.pgz', 0x0039FFF0, 32)
print('wrote lowtest.pgz, toptest.pgz, edgtest.pgz')
