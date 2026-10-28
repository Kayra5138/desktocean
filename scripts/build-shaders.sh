#!/usr/bin/env bash
# Compiles GLSL sources in shaders/ to Qt .qsb files inside the package.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
QSB="${QSB:-/usr/lib/qt6/bin/qsb}"
OUT="$ROOT/package/contents/shaders"
mkdir -p "$OUT"
for src in "$ROOT"/shaders/*.vert "$ROOT"/shaders/*.frag; do
    "$QSB" --qt6 -o "$OUT/$(basename "$src").qsb" "$src"
    echo "built $(basename "$src").qsb"
done
