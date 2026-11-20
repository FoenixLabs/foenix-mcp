#!/usr/bin/env python3
"""
calypsi_check.py - find code hit by Calypsi 5.18 code generation problems

    calypsi_check.py FILE.lst [FILE.lst ...]

Reads the compiler's list files (--list-file) and reports three kinds of trouble. The MCP build
(src/Makefile, COMPILER=calypsi) runs it after linking and stops when it finds anything.

Each list file names the compiler version in its header. A check only runs for the versions
that have the problem (2.1.0, Calypsi 5.18.4):
  1. multiply in place       5.18 up to 5.18.2 (fixed in 5.18.3)
  2. narrowed hardware access all versions so far (still in 5.18.4)
  3. wrong bit test of a long 5.18 up to 5.18.3 (fixed in 5.18.4)
Not found by this tool: a comparison of two constants inside && in an if is compiled inverted
(still in 5.18.4). The MCP uses #if for such switches.

1. Multiply in place (a compiler bug). For &table[i] with a 16-bit index i and an element
   size that is not a power of two, cc68k 5.18 multiplies the register that holds i itself
   (mulu.w / muls.w #size,dN). The same happens with "row = y * width" when width is a short
   variable (muls.w width,dN on the register of y): the loop then goes on with y * width. Code after it that still uses i then gets i * size: a wrong
   compare, a wrong slot freed, a wrong argument. It happens at every optimisation level.
   Way around it in C: copy the index into a long variable first and index with that, or make
   the loop counter a long / int.

   After every such multiply the tool follows the instructions of the same function until the
   register gets a new value. If the register is read before that, the place is reported. A call
   (jsr) counts as a read of d0 and d1 (they carry the first parameters; a call to a function
   that takes its first parameters in address registers is a false alarm), and d2-d7 are
   followed past calls (calls keep them). The scan goes straight down to the end of the
   function, also past branches: it can report a use that the multiply never reaches, or the
   product's own intended use. A multiply on a fresh copy ("move.w d2,d0" right before it)
   is the correct code and is not reported.

2. Narrowed hardware access. "*REG = *REG | bit" or "*REG &= ~bit" on a 16- or 32-bit
   register becomes a one-byte (or one-word) bset / bclr / andi / ori on an I/O address
   ($FC00_0000 and up). VBCC reads and writes the whole register. Some FPGA registers ignore
   a narrower write: GABE's control register did, and the SuperIO then stayed in reset (2.1.0).
   Way around it in C: read the register into a local variable, change that, write it back.
   Only addresses that are not a multiple of 4 are reported: a narrowed access lands inside a
   long register ($FEC0_0002 for the long at $FEC0_0000), while byte and word registers mostly
   sit at their own aligned address. A narrowed access to the top byte of a long (at the
   register's own address) is therefore not found. A report on a register that really is a
   byte or word wide is harmless; rewrite it the same way.

3. Wrong bit test of a long. "if (x & 1)" (also & 2, & 4, & 8) with x a 32-bit value in memory
   becomes "move.w x+4,ccr" and a branch on a flag: it reads the word after x instead of x's
   low word at x+2. Through a pointer the same: (n+4,aN). It hit the SD driver's wait for the
   end of a block write (2.1.0d damaged an SD card). Way around it in C: copy the value into a
   local variable first. The tool cannot tell a long from a short in the list file, so it
   reports every "move.w <memory>,ccr"; on a 16-bit variable it is right, so check each place.

Output: one line per place (file, list line, what was found). Exit code 1 if anything was
found, 0 if not.
"""

import re
import sys

# An instruction line in a Calypsi list file: "    \ 0000001a c1fc0036   muls.w  #54,d0"
INSN = re.compile(r"^\s*\\?\s*[0-9a-f]{8}\s+(?:[0-9a-f.]+\s+)?(?:`[^`]*`:|[A-Za-z_][\w]*:)?\s*"
                  r"([a-z][a-z0-9]*(?:\.[bwlsx])?)\s*(.*)$")
MUL = re.compile(r"^mul[su]\.w$")


def operands(text):
    """Split an operand field at top-level commas: '(0,a0,d2.l),a0' -> ['(0,a0,d2.l)', 'a0']."""
    text = text.split(";")[0].strip()
    parts, depth, cur = [], 0, ""
    for ch in text:
        if ch in "([":
            depth += 1
        elif ch in ")]":
            depth -= 1
        if ch == "," and depth == 0:
            parts.append(cur.strip())
            cur = ""
        else:
            cur += ch
    if cur.strip():
        parts.append(cur.strip())
    return parts


def uses(reg, op):
    """True if operand text op mentions data register reg (d0..d7), also as an index."""
    return re.search(r"(?<![\w])" + reg + r"(?![\w])", op) is not None


def check(path):
    """Check one list file; returns a list of report lines."""
    with open(path, errors="replace") as f:
        lines = f.read().split("\n")
    reports = []
    for n, line in enumerate(lines):
        m = INSN.match(line)
        if not m or not MUL.match(m.group(1)):
            continue
        ops = operands(m.group(2))
        if len(ops) != 2 or not re.fullmatch(r"d[0-7]", ops[1]):
            continue        # any multiplier: a constant, a register or a variable in memory
        reg = ops[1]
        # A fresh copy ("move.w d2,d0" right before the multiply) leaves the original in the
        # other register: that is the correct code the compiler makes when it works.
        prev = None
        for j in range(n - 1, max(n - 20, 0), -1):
            prev = INSN.match(lines[j])
            if prev:
                break
        if prev and prev.group(1).split(".")[0] == "move":
            pops = operands(prev.group(2))
            if len(pops) == 2 and pops[1] == reg and re.fullmatch(r"d[0-7]", pops[0]):
                continue
        first = True        # the instruction right after the multiply uses the product: fine
        for k in range(n + 1, min(n + 400, len(lines))):
            if ".section" in lines[k]:
                break           # the next function (each one starts a section fragment)
            m2 = INSN.match(lines[k])
            if not m2:
                continue
            mnem, ops2 = m2.group(1), operands(m2.group(2))
            base = mnem.split(".")[0]
            if first:
                first = False
                if base in ("add", "adda") and ops2 and ops2[-1] == reg:
                    break       # "add.l #table,dN": dN now holds the element's address, fine
                continue
            if base in ("rts", "rte", "jmp"):
                break
            if base == "bra":
                continue        # the code after it may be reached from the multiply too: go on
            if base == "jsr":
                if reg in ("d0", "d1"):
                    # d0 / d1 carry the first parameters, and the call changes them
                    reports.append("%s:%d: %s after multiply passed to a call? %s"
                                   % (path, k + 1, reg, lines[k].strip()))
                    break
                continue        # d2-d7 are kept by every call: keep looking
            if base == "movem":
                if any(uses(reg, o) for o in ops2[:1]) and "-(" in "".join(ops2[1:]):
                    continue    # saving it is not using it
                if any(uses(reg, o) for o in ops2[1:]):
                    break       # restored: new value
                continue
            src, dst = ops2[:-1], ops2[-1:] if ops2 else []
            if any(uses(reg, o) for o in src):
                reports.append("%s:%d: %s read after multiply: %s" % (path, k + 1, reg, lines[k].strip()))
                break
            if dst and dst[0] == reg:
                # written: moves, moveq, clr give it a new value; anything else also reads it
                if base in ("move", "moveq", "clr", "lea"):
                    break
                reports.append("%s:%d: %s read after multiply: %s" % (path, k + 1, reg, lines[k].strip()))
                break
            if dst and uses(reg, dst[0]):
                reports.append("%s:%d: %s read after multiply: %s" % (path, k + 1, reg, lines[k].strip()))
                break
            if base in ("tst", "ext", "extb", "neg", "not", "swap") and ops2 and ops2[0] == reg:
                reports.append("%s:%d: %s read after multiply: %s" % (path, k + 1, reg, lines[k].strip()))
                break
    return reports


NARROW = re.compile(r"^(bset|bclr|bchg)\.b$|^(andi|ori|eori|and|or|eor|addq|subq|add|sub|not|neg|clr)\.[bw]$")
IOADDR = re.compile(r"0xf[c-f][0-9a-f]{6}\.l$")


def check_narrow(path):
    """Report bit and arithmetic operations of byte / word size done in place on an I/O address."""
    with open(path, errors="replace") as f:
        lines = f.read().split("\n")
    reports = []
    for n, line in enumerate(lines):
        m = INSN.match(line)
        if not m or not NARROW.match(m.group(1)):
            continue
        ops = operands(m.group(2))
        if ops and IOADDR.search(ops[-1]):
            if m.group(1).startswith("clr"):
                continue    # a plain write of 0, no read-modify-write
            if int(IOADDR.search(ops[-1]).group(0)[:-2], 16) % 4 == 0:
                continue    # a register's own address: most likely a byte / word register
            reports.append("%s:%d: narrowed read-modify-write of an I/O register: %s"
                           % (path, n + 1, line.strip()))
    return reports


VERSION = re.compile(r"Calypsi ISO C compiler.*version\s+(\d+)\.(\d+)(?:\.(\d+))?")
CCR_FROM_MEMORY = re.compile(r"^move\.w$")


def compiler_version(path):
    """The compiler version from a list file's header, as a tuple (5, 18, 4), or None for an
    assembler list file or an unknown header (then every check runs)."""
    with open(path, errors="replace") as f:
        for _ in range(10):
            m = VERSION.search(f.readline())
            if m:
                return (int(m.group(1)), int(m.group(2)), int(m.group(3) or 0))
    return None


def check_ccr(path):
    """Report every 16-bit move from memory into the condition codes (bit test of a long)."""
    with open(path, errors="replace") as f:
        lines = f.read().split("\n")
    reports = []
    for n, line in enumerate(lines):
        m = INSN.match(line)
        if not m or not CCR_FROM_MEMORY.match(m.group(1)):
            continue
        ops = operands(m.group(2))
        if len(ops) == 2 and ops[1] == "ccr" and not re.fullmatch(r"d[0-7]", ops[0]):
            reports.append("%s:%d: bit test read from memory (right only for a 16-bit value): %s"
                           % (path, n + 1, line.strip()))
    return reports


def main():
    """Check every list file named on the command line. Returns the exit code."""
    found = []
    for path in sys.argv[1:]:
        version = compiler_version(path)
        if version is None or version < (5, 18, 3):
            found += check(path)            # multiply in place: fixed in 5.18.3
        found += check_narrow(path)         # narrowed hardware access: not fixed yet
        if version is not None and version < (5, 18, 4):
            found += check_ccr(path)        # wrong bit test of a long: fixed in 5.18.4
    for r in found:
        print(r)
    if found:
        print("calypsi_check: %d place(s) to check (see tools/calypsi_check.py)" % len(found))
        return 1
    print("calypsi_check: nothing found (checks per compiler version, see tools/calypsi_check.py)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
