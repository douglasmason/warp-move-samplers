#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
python3 scripts/generate_ui.py
g++ -O2 -std=c++17 -I"$ROOT/src/common" "$ROOT/src/common/warp_core.cpp" "$ROOT/tests/test_core.cpp" -o "$ROOT/tests/test_core"
"$ROOT/tests/test_core"
g++ -O2 -std=c++17 -I"$ROOT/src/common" "$ROOT/src/common/warp_core.cpp" "$ROOT/tests/test_warp.cpp" -o "$ROOT/tests/test_warp"
"$ROOT/tests/test_warp"
python3 "$ROOT/tests/test_manifests.py"
./scripts/build_host.sh
python3 "$ROOT/tests/test_plugin_abi.py"
python3 "$ROOT/tests/test_ui.py"
for engine in melodic drums; do
  DEFINES=()
  if [ "$engine" = drums ]; then DEFINES=(-DTEST_DRUMS); fi
  g++ -O1 -g -pthread -std=c++17 -fsanitize=address,undefined "${DEFINES[@]}" \
    src/common/warp_core.cpp tests/test_regressions.cpp -o "build/host/test_$engine"
  "build/host/test_$engine"
done
g++ -O1 -g -pthread -std=c++17 -fsanitize=address,undefined src/common/warp_core.cpp tests/test_memory.cpp -o build/host/test_memory
build/host/test_memory
