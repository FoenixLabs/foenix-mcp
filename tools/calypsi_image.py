#!/usr/bin/env python3
"""
calypsi_image.py - put the Calypsi linker's raw output together into an MCP flash block image

    calypsi_image.py VECTORS.raw CODE.raw OUTPUT.bin [--size 524288] [--code-offset 0x10000]

With --raw-multiple-memories the Calypsi linker (ln68k) writes one raw file per memory of the
linker rules (src/calypsi/*.scm): the vector table (memory "Vector", address 0) and the code
(memory "flash", from $FFC9_0000). A flash block holds both:

    offset $0_0000  the vector table; at reset the FPGA copies the block's first 64 KB to RAM
                    at address 0, so the CPU finds the vectors there
    offset $1_0000  the code, constants and initial data values ($FFC9_0000 for block 1)

Everything else in the image is 0 (as in the VBCC build, padded with truncate). The tool stops
with an error if a part does not fit, so a too-large MCP never overwrites the next block.

Output: the image file, and one line with the sizes.
"""

import argparse
import sys


def main():
    """Read the two raw files, check their sizes, write the image. Returns the exit code."""
    parser = argparse.ArgumentParser(description="Build an MCP flash image from Calypsi raw output")
    parser.add_argument("vectors", help="raw file of the Vector memory (address 0)")
    parser.add_argument("code", help="raw file of the flash memory (code)")
    parser.add_argument("output", help="the image to write")
    parser.add_argument("--size", type=lambda s: int(s, 0), default=524288,
                        help="image size in bytes (default 512 KB, one flash block)")
    parser.add_argument("--code-offset", type=lambda s: int(s, 0), default=0x10000,
                        help="where the code starts in the image (default $10000)")
    args = parser.parse_args()

    with open(args.vectors, "rb") as f:
        vectors = f.read()
    with open(args.code, "rb") as f:
        code = f.read()

    # The vectors must stay inside the part of the block the FPGA copies, below the code
    if len(vectors) > args.code_offset:
        print("calypsi_image: vector part is %d bytes, more than the %d before the code"
              % (len(vectors), args.code_offset), file=sys.stderr)
        return 1
    if args.code_offset + len(code) > args.size:
        print("calypsi_image: code is %d bytes, %d too many for the block"
              % (len(code), args.code_offset + len(code) - args.size), file=sys.stderr)
        return 1

    image = bytearray(args.size)
    image[0:len(vectors)] = vectors
    image[args.code_offset:args.code_offset + len(code)] = code

    with open(args.output, "wb") as f:
        f.write(image)

    print("calypsi_image: %s: vectors %d bytes, code %d bytes, %d of %d bytes free"
          % (args.output, len(vectors), len(code), args.size - args.code_offset - len(code),
             args.size - args.code_offset))
    return 0


if __name__ == "__main__":
    sys.exit(main())
