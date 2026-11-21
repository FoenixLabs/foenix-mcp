#!/usr/bin/env python3
"""
block_name.py - give a flash block image the name the boot loader (lvboot 1.4 and later) shows

    block_name.py IMAGE.bin "MCP 2.1.0-cache"

Any program that lives in a flash block of a Gen1 Foenix (the MCP, EmuTOS, ...) starts with its
680x0 vector table: the FPGA copies the first 64 KB of the block to RAM at address 0 at reset.
Vectors 16-23 (offset $40-$5F of the block) are "unassigned, reserved" on every 680x0 (68000 up
to 68060): the CPU never reads them, so they are free for this record.

The block info record, 8 longs, big-endian (all 680x0 data is):

    $40  magic   "MCPI" (written by the MCP's start-up code since MCP 2.0.30) or
                 "BLKI" (any other program; this tool writes it when there is no record yet)
    $44  version for the MCP: major << 24 | minor << 16 | build; 0 for others
    $48  flags   bit 0 = the MCP switches the CPU caches on
                 bit 1 = $50-$5F holds the name (set by this tool)
    $4C  check   the three longs above XOR-ed together
    $50  name    16 bytes: 1 to 15 printable ASCII characters, the rest 0 bytes

lvboot 1.4 shows the name when bit 1 is set. Older boot loaders ignore bit 1 and $50-$5F, so
nothing changes for them: lvboot 1.3 still shows "MCP x.y.z" (+ "c") from an "MCPI" record.

Input:  the image file (changed in place) and the name
Output: one line saying what was written; exit code 1 (image unchanged) on a bad name or image
"""

import argparse
import struct
import sys

RECORD_OFFSET = 0x40
NAME_OFFSET = 0x50
NAME_SIZE = 16                      # 15 characters and at least one 0 byte
MAGIC_MCP = b"MCPI"
MAGIC_OTHER = b"BLKI"
FLAG_NAME = 0x2


def main():
    parser = argparse.ArgumentParser(description="Write the boot loader name into a flash block image")
    parser.add_argument("image", help="the flash block image (changed in place)")
    parser.add_argument("name", help="the name lvboot shows (1-15 printable ASCII characters)")
    args = parser.parse_args()

    name = args.name.encode("ascii", errors="replace")
    if not 1 <= len(name) < NAME_SIZE or any(c < 0x20 or c > 0x7E for c in name):
        print("block_name: the name must be 1 to %d printable ASCII characters: %r" % (NAME_SIZE - 1, args.name))
        return 1

    with open(args.image, "rb") as f:
        image = bytearray(f.read())
    if len(image) < NAME_OFFSET + NAME_SIZE:
        print("block_name: %s is too short for a vector table" % args.image)
        return 1

    magic, version, flags, check = struct.unpack_from(">4sIII", image, RECORD_OFFSET)
    magic_long = struct.unpack(">I", magic)[0]
    if magic in (MAGIC_MCP, MAGIC_OTHER) and (magic_long ^ version ^ flags) == check:
        # A record is there already (the MCP's): keep its magic, version and flags
        kind = "kept the %s record" % magic.decode()
    else:
        # No record: these vectors only held unused entries; start a record for a non-MCP block
        magic, version, flags = MAGIC_OTHER, 0, 0
        magic_long = struct.unpack(">I", magic)[0]
        kind = "new BLKI record"

    flags |= FLAG_NAME
    check = magic_long ^ version ^ flags
    struct.pack_into(">4sIII", image, RECORD_OFFSET, magic, version, flags, check)
    image[NAME_OFFSET:NAME_OFFSET + NAME_SIZE] = name.ljust(NAME_SIZE, b"\0")

    with open(args.image, "wb") as f:
        f.write(image)
    print("block_name: %s: name \"%s\" (%s)" % (args.image, args.name, kind))
    return 0


if __name__ == "__main__":
    sys.exit(main())
