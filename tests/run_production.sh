#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
USE_BUNGEE=1 ./scripts/build_host.sh
for test in test_warp test_renderer; do
  g++ -O2 -std=c++20 -DWARP_USE_BUNGEE -Isrc/common -Ivendor/bungee \
    src/common/warp_core.cpp "tests/$test.cpp" build/host/bungee/libbungee.a \
    -o "build/host/$test"
  "build/host/$test"
done
python3 tests/test_plugin_abi.py
python3 tests/test_ui.py
for engine in melodic drums; do
  DEFINES=()
  if [ "$engine" = drums ]; then DEFINES=(-DTEST_DRUMS); fi
  g++ -O2 -std=c++20 -DWARP_USE_BUNGEE "${DEFINES[@]}" -Isrc/common -Ivendor/bungee \
    src/common/warp_core.cpp tests/test_regressions.cpp build/host/bungee/libbungee.a \
    -o "build/host/test_production_$engine"
  "build/host/test_production_$engine"
done
