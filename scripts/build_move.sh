#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
IMAGE=warp-move-builder
if [ -z "${CROSS_PREFIX:-}" ] && [ ! -f /.dockerenv ]; then
  [ -d "$ROOT/vendor/bungee/submodules/pffft" ] || { echo "Bungee not vendored. Run ./scripts/fetch_bungee.sh first."; exit 1; }
  docker image inspect "$IMAGE" >/dev/null 2>&1 || docker build -t "$IMAGE" -f "$ROOT/scripts/Dockerfile" "$ROOT"
  docker run --rm -v "$ROOT:/build" -u "$(id -u):$(id -g)" -w /build "$IMAGE" ./scripts/build_move.sh
  exit 0
fi
CROSS_PREFIX="${CROSS_PREFIX:-aarch64-linux-gnu-}"
B="${BUNGEE_DIR:-$ROOT/vendor/bungee}"
[ -f "$B/bungee/Bungee.h" ] || { echo "Bungee tree missing"; exit 1; }
rm -rf "$ROOT/build/move" "$ROOT/dist"
mkdir -p "$ROOT/build/move/bungee" "$ROOT/dist"
${CROSS_PREFIX}gcc -O3 -fPIC -ffast-math -fno-finite-math-only -c "$B/submodules/pffft/pffft.c" -o "$ROOT/build/move/bungee/pffft.o"
${CROSS_PREFIX}gcc -O3 -fPIC -ffast-math -fno-finite-math-only -c "$B/submodules/pffft/fftpack.c" -o "$ROOT/build/move/bungee/fftpack.o"
for src in "$B"/src/*.cpp; do
  obj="$ROOT/build/move/bungee/$(basename "$src" .cpp).o"
  ${CROSS_PREFIX}g++ -O3 -fPIC -std=c++20 -fwrapv -I"$B/submodules/eigen" -I"$B/submodules" -I"$B" \
    '-DBUNGEE_VISIBILITY=__attribute__((visibility("default")))' -DBUNGEE_SELF_TEST=0 \
    -Deigen_assert=BUNGEE_ASSERT1 -DEIGEN_DONT_PARALLELIZE=1 '-DBUNGEE_VERSION="0.0.0"' -c "$src" -o "$obj"
done
${CROSS_PREFIX}ar rcs "$ROOT/build/move/bungee/libbungee.a" "$ROOT/build/move/bungee"/*.o
FLAGS=(-O3 -shared -fPIC -std=c++20 -DWARP_USE_BUNGEE -I"$ROOT/src/common" -I"$B")
${CROSS_PREFIX}g++ "${FLAGS[@]}" "$ROOT/src/common/warp_core.cpp" "$ROOT/src/melodic/plugin.cpp" "$ROOT/build/move/bungee/libbungee.a" -lm -o "$ROOT/build/move/melodic_dsp.so"
${CROSS_PREFIX}g++ "${FLAGS[@]}" "$ROOT/src/common/warp_core.cpp" "$ROOT/src/drums/plugin.cpp" "$ROOT/build/move/bungee/libbungee.a" -lm -o "$ROOT/build/move/drums_dsp.so"
for id in warpmrsample warpmelodic warpmrdrums warpdrumkit; do mkdir -p "$ROOT/dist/$id"; cp "$ROOT/modules/$id/module.json" "$ROOT/dist/$id/module.json"; done
cp "$ROOT/build/move/melodic_dsp.so" "$ROOT/dist/warpmrsample/dsp.so"
cp "$ROOT/build/move/melodic_dsp.so" "$ROOT/dist/warpmelodic/dsp.so"
cp "$ROOT/build/move/drums_dsp.so" "$ROOT/dist/warpmrdrums/dsp.so"
cp "$ROOT/build/move/drums_dsp.so" "$ROOT/dist/warpdrumkit/dsp.so"
chmod +x "$ROOT/dist"/*/dsp.so
for id in warpmrsample warpmelodic warpmrdrums warpdrumkit; do (cd "$ROOT/dist" && tar -czf "$id-module.tar.gz" "$id"); done
file "$ROOT/dist"/*/dsp.so
