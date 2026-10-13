#!/usr/bin/env bash
# Builds the folder layout to copy to the root of a FAT32 USB drive.
# XeLL auto-boots /xenon.elf, and the emulator reads /mupen64-360/.
#
# Usage: scripts/package.sh <mupen64-360.elf32> <out-dir>
set -euo pipefail
ELF="${1:?elf32 path}"
OUT="${2:?output dir}"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
STRIP="${DEVKITXENON:+$DEVKITXENON/bin/}xenon-strip"

rm -rf "$OUT"
mkdir -p "$OUT/mupen64-360/saves" "$OUT/mupen64-360/sstates" "$OUT/mupen64-360/roms"

cp "$ELF" "$OUT/xenon.elf"
if command -v "$STRIP" >/dev/null 2>&1; then "$STRIP" "$OUT/xenon.elf"; fi

cp "$ROOT"/files/*.png "$ROOT"/files/*.ini "$OUT/mupen64-360/"
rm -f "$OUT/mupen64-360/about.png"   # embedded in the binary
cp "$ROOT/files/readme.txt" "$OUT/mupen64-360/readme-original.txt"
touch "$OUT/mupen64-360/roms/PON_TUS_ROMS_AQUI.txt"

echo "USB layout ready in $OUT:"
find "$OUT" -maxdepth 2 | sort
