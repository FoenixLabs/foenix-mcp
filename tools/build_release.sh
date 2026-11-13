#!/bin/bash
########################################################################
# build_release.sh - build the MCP flash images for every machine,
# each one twice: CPU caches off (as always) and caches on (CACHE=1)
#
#   tools/build_release.sh            build into release/<version>/
#
# Run it from anywhere; it works in the src folder of this repository.
# It needs the VBCC programs (vc, vbccm68k, vasmm68k_mot, vlink) on the
# PATH or in ~/opt/vbcc/bin, and the splash screen C files
# (src/rsrc/bitmaps/splash_<unit>.c, see the README).
#
# Every image is a clean build: the Makefile cannot tell that CACHE,
# CPU060 or BLOCK changed, so old object files must go first.
#
# Output (for version 2.0.29):
#   foenixmcp_2_0_29_k25_k40_b1.bin        A2560K K25 / K40, flash block 1
#   foenixmcp_2_0_29_k25_k40_b1_cache.bin  the same with the caches on
#   foenixmcp_2_0_29_k60_b1(_cache).bin    A2560K K60 (68LC060), block 1
#   foenixmcp_2_0_29_a2560x(_cache).bin    A2560X
#   foenixmcp_2_0_29_genx(_cache).bin      GenX
#   SHA256SUMS.txt                         checksums of all images
#   logs/                                  one build log per image
########################################################################

set -e

# The repository root is the folder above this script
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SRC="$ROOT/src"

# VBCC: the config and target files come from this repository; the
# programs from the PATH, or from ~/opt/vbcc/bin if they are not there
export VBCC="$ROOT/vbcc"
if ! command -v vc >/dev/null && [ -x "$HOME/opt/vbcc/bin/vc" ]; then
    export PATH="$HOME/opt/vbcc/bin:$PATH"
fi
command -v vc >/dev/null || { echo "vc (VBCC) not found: see 'Building on a Mac' in the README"; exit 1; }

# Version from src/version.h, e.g. 2_0_29 (tr removes Windows line ends)
ver() { grep "define VER_$1 " "$SRC/version.h" | tr -d '\r' | awk '{print $3}'; }
VERSION="$(ver MAJOR)_$(ver MINOR)_$(ver BUILD)"

OUT="$ROOT/release/${VERSION//_/.}"
mkdir -p "$OUT/logs"

# One line per machine: name in the file, then the make settings
MACHINES=(
    "k25_k40_b1 UNIT=a2560k BLOCK=1"
    "k60_b1     UNIT=a2560k BLOCK=1 CPU060=1"
    "a2560x     UNIT=a2560x"
    "genx       UNIT=genx"
)

cd "$SRC"
for line in "${MACHINES[@]}"; do
    read -r name settings <<< "$line"
    unit=$(echo "$settings" | sed 's/.*UNIT=\([a-z0-9]*\).*/\1/')
    [ -f "rsrc/bitmaps/splash_$unit.c" ] || { echo "missing rsrc/bitmaps/splash_$unit.c (see the README)"; exit 1; }

    for cache in 0 1; do
        file="foenixmcp_${VERSION}_${name}"
        [ $cache = 1 ] && file="${file}_cache"
        echo -n "building $file.bin ... "
        make $settings clean >/dev/null 2>&1 || true
        # The Makefile's clean does not remove every object file: remove them all
        find . -name "*.o" -delete
        if make $settings CACHE=$cache > "$OUT/logs/$file.log" 2>&1; then
            cp foenixmcp.bin "$OUT/$file.bin"
            echo "ok"
        else
            echo "FAILED, see $OUT/logs/$file.log"
            exit 1
        fi
    done
done

cd "$OUT"
shasum -a 256 foenixmcp_*.bin > SHA256SUMS.txt
echo
echo "Images in $OUT:"
cat SHA256SUMS.txt
