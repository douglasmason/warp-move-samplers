#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
mkdir -p "$ROOT/build/host"
CXX="${CXX:-g++}"
COMMON=(-O2 -fPIC -std=c++17 -I"$ROOT/src/common")
EXTRA=()
LIBS=(-lm)
if [ "${USE_BUNGEE:-0}" = "1" ]; then
  B="${BUNGEE_DIR:-$ROOT/vendor/bungee}"
  [ -f "$B/bungee/Bungee.h" ] || { echo "Bungee missing. Run scripts/fetch_bungee.sh"; exit 1; }
  mkdir -p "$ROOT/build/host/bungee"
  gcc -O3 -fPIC -ffast-math -fno-finite-math-only -c "$B/submodules/pffft/pffft.c" -o "$ROOT/build/host/bungee/pffft.o"
  gcc -O3 -fPIC -ffast-math -fno-finite-math-only -c "$B/submodules/pffft/fftpack.c" -o "$ROOT/build/host/bungee/fftpack.o"
  for src in "$B"/src/*.cpp; do
    obj="$ROOT/build/host/bungee/$(basename "$src" .cpp).o"
    g++ -O3 -fPIC -std=c++20 -fwrapv -I"$B/submodules/eigen" -I"$B/submodules" -I"$B" \
      '-DBUNGEE_VISIBILITY=__attribute__((visibility("default")))' -DBUNGEE_SELF_TEST=0 \
      -Deigen_assert=BUNGEE_ASSERT1 -DEIGEN_DONT_PARALLELIZE=1 '-DBUNGEE_VERSION="0.0.0"' -c "$src" -o "$obj"
  done
  ar rcs "$ROOT/build/host/bungee/libbungee.a" "$ROOT/build/host/bungee"/*.o
  COMMON=(-O2 -fPIC -std=c++20 -DWARP_USE_BUNGEE -I"$ROOT/src/common" -I"$B")
  LIBS=("$ROOT/build/host/bungee/libbungee.a" -lm)
fi
"$CXX" "${COMMON[@]}" -shared "$ROOT/src/common/warp_core.cpp" "$ROOT/src/melodic/plugin.cpp" -o "$ROOT/build/host/melodic_dsp.so" "${LIBS[@]}"
"$CXX" "${COMMON[@]}" -shared "$ROOT/src/common/warp_core.cpp" "$ROOT/src/drums/plugin.cpp" -o "$ROOT/build/host/drums_dsp.so" "${LIBS[@]}"
file "$ROOT/build/host"/*_dsp.so
