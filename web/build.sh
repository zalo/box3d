#!/usr/bin/env bash
# Build the Box3D WebAssembly bindings (box3d.js + box3d.wasm).
#
# Requires the Emscripten SDK on PATH. Activate it first, e.g.:
#   source /path/to/emsdk/emsdk_env.sh
#
# Then:
#   ./build.sh            # configure + build into web/build, copy artifacts to web/
#   ./build.sh --clean    # wipe the build dir first
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD="$HERE/build"

if ! command -v emcmake >/dev/null 2>&1; then
	echo "error: emcmake not found. Activate the Emscripten SDK first:" >&2
	echo "  source /path/to/emsdk/emsdk_env.sh" >&2
	exit 1
fi

if [ "${1:-}" = "--clean" ]; then
	rm -rf "$BUILD"
fi

emcmake cmake -S "$HERE" -B "$BUILD" -DCMAKE_BUILD_TYPE=Release
cmake --build "$BUILD" -j"$(nproc 2>/dev/null || echo 4)"

cp "$BUILD/box3d.js" "$BUILD/box3d.wasm" "$HERE/"
echo "Built: $HERE/box3d.js + $HERE/box3d.wasm"
ls -lh "$HERE/box3d.js" "$HERE/box3d.wasm"
