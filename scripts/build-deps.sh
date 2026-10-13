#!/usr/bin/env bash
# Builds every library mupen64-360 links against, at pinned commits, with the
# patches in ../patches applied. Requires the xenon toolchain (xenon-gcc)
# already installed in $DEVKITXENON (see README / CI workflow).
#
# Usage: DEVKITXENON=/usr/local/xenon scripts/build-deps.sh
set -euo pipefail

: "${DEVKITXENON:?set DEVKITXENON to the toolchain prefix, e.g. /usr/local/xenon}"
export PATH="$PATH:$DEVKITXENON/bin:$DEVKITXENON/usr/bin"

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DEPS="${DEPS_DIR:-$ROOT/deps}"
PATCHES="$ROOT/patches"
JOBS="${JOBS:-$(nproc)}"
mkdir -p "$DEPS"

# name  url  commit
PINS=(
  "libxenon     https://github.com/Free60Project/libxenon        c32c813c3d630ae922f83395ee4509b0468af71d"
  "fat-xenon    https://github.com/Free60Project/fat-xenon       0f2092a9a86026e07e903b596c903c1629159762"
  "ext2fs-xenon https://github.com/Free60Project/ext2fs-xenon    568ba309a5d742938880f2ad21afe7332d43246f"
  "ntfs-xenon   https://github.com/Free60Project/ntfs-xenon      206008b5b838f37cad7114d335203ced3cd13433"
  "xtaflib      https://github.com/Free60Project/xtaflib         f1cc0b9971f9e11f0f3ce92f7a78b7449168c362"
  "ZLX-Library  https://github.com/LibXenonProject/ZLX-Library   b098f5602aa4753c6bc3abb741f29a0619d7e275"
  "libxemit     https://github.com/gligli/libxemit               3660b70443f4f5c94abccd1810d94127d42bbd20"
)

fetch() { # name url commit
  local dir="$DEPS/$1"
  if [ ! -d "$dir/.git" ]; then
    git init -q "$dir"
    git -C "$dir" remote add origin "$2"
  fi
  git -C "$dir" fetch -q --depth 1 origin "$3"
  git -C "$dir" checkout -q --force FETCH_HEAD
  git -C "$dir" clean -qfdx
}

apply_patches() { # name patchdir
  local p
  for p in "$PATCHES/$2"/*.patch; do
    [ -e "$p" ] || continue
    echo "  patch: $(basename "$p")"
    git -C "$DEPS/$1" apply --whitespace=nowarn "$p"
  done
}

for pin in "${PINS[@]}"; do
  read -r name url sha <<<"$pin"
  echo "== $name @ ${sha:0:8}"
  fetch "$name" "$url" "$sha"
done

apply_patches libxenon libxenon
apply_patches ZLX-Library zlx

echo "== libxenon"
make -C "$DEPS/libxenon/libxenon/ports/xenon" clean >/dev/null
make -j"$JOBS" -C "$DEPS/libxenon/libxenon/ports/xenon" libxenon.a
make -C "$DEPS/libxenon/libxenon/ports/xenon" install

echo "== zlib / bzip2 / libpng / bin2s (libxenon toolchain script)"
# The script downloads the tarballs unless they are already next to it.
if [ -n "${LIBS_TARBALLS:-}" ]; then cp "$LIBS_TARBALLS"/*.tar.* "$DEPS/libxenon/toolchain/"; fi
( cd "$DEPS/libxenon/toolchain" &&
  for t in zlib bzip2 libpng bin2s; do ./build-xenon-toolchain "$t" || { tail -50 build.log; exit 1; }; done )

for fs in fat-xenon ext2fs-xenon ntfs-xenon xtaflib; do
  echo "== $fs"
  make -C "$DEPS/$fs"
  make -C "$DEPS/$fs" install
done

echo "== libxemit"
make -C "$DEPS/libxemit"
make -C "$DEPS/libxemit" install

echo "== ZLX"
make -j"$JOBS" -C "$DEPS/ZLX-Library" -f Makefile_lib
make -C "$DEPS/ZLX-Library" -f Makefile_lib install

echo "All dependencies installed in $DEVKITXENON"
